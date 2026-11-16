// Image Background - rebuilds a picture out of editor objects so it can sit behind a level.
//
// Two styles (setting "style"):
//   painting - the picture is rebuilt from overlapping rotated ellipses, big strokes first, like a painting
//   pixels   - a grid of squares
//
// How the pixel style works:
//   * the picture is shrunk to a grid of cells (settings: width in cells)
//   * every cell becomes one plain square object (ID 211) on the constant white colour channel (1011),
//     tinted with a per-object HSV value, so no colour channels are used up
//   * neighbouring cells with the same colour are merged into one stretched square
//   * the squares go on the lowest Z layer, are marked NoTouch (the player passes through them)
//     and are pasted in the middle of the editor view, selected, so they can be moved or scaled right away
#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <cwctype>
#include <algorithm>
#include <random>
#include <chrono>

using namespace geode::prelude;
namespace fs = std::filesystem;

namespace img {

constexpr int ID_SQUARE = 211;          // plain colourable square, 30 x 30
constexpr int ID_CIRCLE = 1764;         // plain colourable circle (its size is measured at run time)
constexpr int CHANNEL_WHITE = 1011;     // built-in channel that is always white
constexpr int Z_LAYER_B5 = -5;          // furthest back
constexpr int MAX_OBJECTS = 40000;

static fs::path folder() { return Mod::get()->getConfigDir(); }

// newest .png / .jpg / .jpeg in the mod's folder
static fs::path newestImage() {
    std::error_code ec;
    fs::path best;
    fs::file_time_type bestTime{};
    for (auto& e : fs::directory_iterator(folder(), ec)) {
        if (!e.is_regular_file(ec)) continue;
        auto ext = e.path().extension().wstring();
        for (auto& c : ext) c = (wchar_t)towlower(c);
        if (ext != L".png" && ext != L".jpg" && ext != L".jpeg") continue;
        auto t = e.last_write_time(ec);
        if (best.empty() || t > bestTime) { best = e.path(); bestTime = t; }
    }
    return best;
}

struct Pixels { int w = 0, h = 0; std::vector<unsigned char> rgba; };   // top row first

static bool load(const fs::path& path, Pixels& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() < 16) return false;
    auto image = new CCImage();
    bool ok = image->initWithImageData(bytes.data(), (int)bytes.size());
    if (ok && image->getData() && image->getWidth() > 0 && image->getHeight() > 0) {
        out.w = image->getWidth();
        out.h = image->getHeight();
        int bpp = image->hasAlpha() ? 4 : 3;
        const unsigned char* src = image->getData();
        out.rgba.resize((size_t)out.w * out.h * 4);
        for (size_t i = 0, n = (size_t)out.w * out.h; i < n; ++i) {
            out.rgba[i * 4 + 0] = src[i * bpp + 0];
            out.rgba[i * 4 + 1] = src[i * bpp + 1];
            out.rgba[i * 4 + 2] = src[i * bpp + 2];
            out.rgba[i * 4 + 3] = bpp == 4 ? src[i * bpp + 3] : 255;
        }
    } else ok = false;
    image->release();
    return ok;
}

struct Cell { bool empty = true; int h = 0, s = 0, v = 0; };            // quantised colour: h in degrees, s / v in steps

// average colour of every grid cell, converted to HSV and rounded so that similar cells become equal
static std::vector<Cell> grid(const Pixels& p, int cols, int rows, int detail) {
    static const int HUE_STEP[5] = {30, 20, 12, 8, 5};
    static const int SV_STEPS[5] = {5, 8, 12, 16, 24};
    int hueStep = HUE_STEP[detail - 1], sv = SV_STEPS[detail - 1];
    std::vector<Cell> cells((size_t)cols * rows);
    for (int cy = 0; cy < rows; ++cy) {
        for (int cx = 0; cx < cols; ++cx) {
            int x0 = cx * p.w / cols, x1 = std::max(x0 + 1, (cx + 1) * p.w / cols);
            int y0 = cy * p.h / rows, y1 = std::max(y0 + 1, (cy + 1) * p.h / rows);
            double r = 0, g = 0, b = 0, a = 0;
            int n = 0;
            for (int y = y0; y < y1 && y < p.h; ++y)
                for (int x = x0; x < x1 && x < p.w; ++x) {
                    const unsigned char* q = &p.rgba[((size_t)y * p.w + x) * 4];
                    r += q[0]; g += q[1]; b += q[2]; a += q[3];
                    ++n;
                }
            if (n == 0 || a / n < 24.0) continue;                       // transparent: leave a hole
            double R = r / n / 255.0, G = g / n / 255.0, B = b / n / 255.0;
            double mx = std::max({R, G, B}), mn = std::min({R, G, B}), d = mx - mn, H = 0;
            if (d > 1e-6) {
                if (mx == R) H = 60.0 * std::fmod((G - B) / d, 6.0);
                else if (mx == G) H = 60.0 * ((B - R) / d + 2.0);
                else H = 60.0 * ((R - G) / d + 4.0);
                if (H < 0) H += 360.0;
            }
            double S = mx <= 1e-6 ? 0.0 : d / mx, V = mx;
            Cell& c = cells[(size_t)cy * cols + cx];
            c.empty = false;
            c.s = (int)std::lround(S * sv);
            c.v = (int)std::lround(V * sv);
            c.h = (c.s == 0 || c.v == 0) ? 0 : ((int)std::lround(H / hueStep) * hueStep) % 360;   // greys share one hue
            if (c.v == 0) c.s = 0;
        }
    }
    return cells;
}

// ---------------------------------------------------------------------------------------------------
// Painting mode: the picture is rebuilt from overlapping rotated ellipses, largest strokes first.
// For every stroke many random ellipses are tried and nudged; the one that brings the canvas closest to the
// picture is kept, filled with the average colour of the picture underneath it.
// ---------------------------------------------------------------------------------------------------
struct Stroke { float cx, cy, a, b, ang; float r, g, bl; };              // centre, radii (work pixels), angle (radians), colour 0..255

struct Painter {
    int w = 0, h = 0;
    std::vector<float> target, canvas;                                   // RGB per pixel
    std::mt19937 rng{(unsigned)std::chrono::steady_clock::now().time_since_epoch().count()};

    float rnd(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); }

    // keeps the whole ellipse inside the picture
    void fit(Stroke& s) const {
        s.a = std::clamp(s.a, 1.f, w * 0.5f);
        s.b = std::clamp(s.b, 1.f, h * 0.5f);
        float c = std::cos(s.ang), sn = std::sin(s.ang);
        float ex = std::sqrt(s.a * s.a * c * c + s.b * s.b * sn * sn), ey = std::sqrt(s.a * s.a * sn * sn + s.b * s.b * c * c);
        if (ex > w * 0.5f) ex = w * 0.5f;
        if (ey > h * 0.5f) ey = h * 0.5f;
        s.cx = std::clamp(s.cx, ex, w - ex);
        s.cy = std::clamp(s.cy, ey, h - ey);
    }

    // how much closer the canvas gets to the picture if this stroke is painted (also fills in its best colour)
    double gain(Stroke& s, bool apply) {
        float c = std::cos(s.ang), sn = std::sin(s.ang);
        float ex = std::sqrt(s.a * s.a * c * c + s.b * s.b * sn * sn), ey = std::sqrt(s.a * s.a * sn * sn + s.b * s.b * c * c);
        int x0 = std::max(0, (int)std::floor(s.cx - ex)), x1 = std::min(w - 1, (int)std::ceil(s.cx + ex));
        int y0 = std::max(0, (int)std::floor(s.cy - ey)), y1 = std::min(h - 1, (int)std::ceil(s.cy + ey));
        float ia = 1.f / (s.a * s.a), ib = 1.f / (s.b * s.b);
        double sr = 0, sg = 0, sb = 0, before = 0;
        int n = 0;
        for (int y = y0; y <= y1; ++y) {
            float dy = y + 0.5f - s.cy;
            for (int x = x0; x <= x1; ++x) {
                float dx = x + 0.5f - s.cx;
                float u = c * dx + sn * dy, v = -sn * dx + c * dy;
                if (u * u * ia + v * v * ib > 1.f) continue;
                size_t i = ((size_t)y * w + x) * 3;
                sr += target[i]; sg += target[i + 1]; sb += target[i + 2];
                float d0 = canvas[i] - target[i], d1 = canvas[i + 1] - target[i + 1], d2 = canvas[i + 2] - target[i + 2];
                before += d0 * d0 + d1 * d1 + d2 * d2;
                ++n;
            }
        }
        if (n == 0) return -1.0;
        s.r = (float)(sr / n); s.g = (float)(sg / n); s.bl = (float)(sb / n);
        double after = 0;
        for (int y = y0; y <= y1; ++y) {
            float dy = y + 0.5f - s.cy;
            for (int x = x0; x <= x1; ++x) {
                float dx = x + 0.5f - s.cx;
                float u = c * dx + sn * dy, v = -sn * dx + c * dy;
                if (u * u * ia + v * v * ib > 1.f) continue;
                size_t i = ((size_t)y * w + x) * 3;
                float d0 = s.r - target[i], d1 = s.g - target[i + 1], d2 = s.bl - target[i + 2];
                after += d0 * d0 + d1 * d1 + d2 * d2;
                if (apply) { canvas[i] = s.r; canvas[i + 1] = s.g; canvas[i + 2] = s.bl; }
            }
        }
        return before - after;
    }

    Stroke next(float maxR) {
        Stroke best{};
        double bestGain = -1e18;
        for (int k = 0; k < 28; ++k) {                                   // random tries
            Stroke s{rnd(0.f, (float)w), rnd(0.f, (float)h), rnd(1.5f, maxR), rnd(1.5f, maxR), rnd(0.f, 3.14159f), 0, 0, 0};
            fit(s);
            double g = gain(s, false);
            if (g > bestGain) { bestGain = g; best = s; }
        }
        for (int k = 0; k < 48; ++k) {                                   // nudge the best one while it keeps improving
            Stroke s = best;
            switch (k % 4) {
                case 0: s.cx += rnd(-4.f, 4.f); s.cy += rnd(-4.f, 4.f); break;
                case 1: s.a *= rnd(0.75f, 1.3f); break;
                case 2: s.b *= rnd(0.75f, 1.3f); break;
                default: s.ang += rnd(-0.5f, 0.5f); break;
            }
            fit(s);
            double g = gain(s, false);
            if (g > bestGain) { bestGain = g; best = s; }
        }
        gain(best, true);
        return best;
    }
};

// colour 0..255 -> the HSV text GD stores on an object that starts out white
static std::string hsvText(float r, float g, float b) {
    double R = r / 255.0, G = g / 255.0, B = b / 255.0;
    double mx = std::max({R, G, B}), mn = std::min({R, G, B}), d = mx - mn, H = 0;
    if (d > 1e-6) {
        if (mx == R) H = 60.0 * std::fmod((G - B) / d, 6.0);
        else if (mx == G) H = 60.0 * ((B - R) / d + 2.0);
        else H = 60.0 * ((R - G) / d + 4.0);
        if (H < 0) H += 360.0;
    }
    double S = mx <= 1e-6 ? 0.0 : d / mx, V = mx;
    if (H > 180.0) H -= 360.0;
    return fmt::format("{:.0f}a{:.3f}a{:.3f}a1a1", H, S, V - 1.0);
}

static float contentWidth(int id, float fallback) {
    if (auto obj = GameObject::createWithKey(id)) {
        float w = obj->getContentSize().width;
        if (w > 0.5f) return w;
    }
    return fallback;
}

// builds the object text for painting mode; returns the number of objects
static int paint(const Pixels& px, float widthUnits, int strokes, CCPoint centre, int group, int layer, std::string& str, CCPoint& firstPos) {
    Painter p;
    p.w = std::min(px.w, 128);
    p.h = std::max(8, (int)std::lround((double)p.w * px.h / px.w));
    if (p.h > 160) { p.h = 160; p.w = std::max(8, (int)std::lround(160.0 * px.w / px.h)); }
    p.target.resize((size_t)p.w * p.h * 3);
    double ar = 0, ag = 0, ab = 0;
    for (int y = 0; y < p.h; ++y)                                         // shrink with a box filter
        for (int x = 0; x < p.w; ++x) {
            int x0 = x * px.w / p.w, x1 = std::max(x0 + 1, (x + 1) * px.w / p.w);
            int y0 = y * px.h / p.h, y1 = std::max(y0 + 1, (y + 1) * px.h / p.h);
            double r = 0, g = 0, b = 0;
            int n = 0;
            for (int yy = y0; yy < y1 && yy < px.h; ++yy)
                for (int xx = x0; xx < x1 && xx < px.w; ++xx) {
                    const unsigned char* q = &px.rgba[((size_t)yy * px.w + xx) * 4];
                    r += q[0]; g += q[1]; b += q[2];
                    ++n;
                }
            if (n == 0) n = 1;
            size_t i = ((size_t)y * p.w + x) * 3;
            p.target[i] = (float)(r / n); p.target[i + 1] = (float)(g / n); p.target[i + 2] = (float)(b / n);
            ar += p.target[i]; ag += p.target[i + 1]; ab += p.target[i + 2];
        }
    size_t total = (size_t)p.w * p.h;
    float br = (float)(ar / total), bg = (float)(ag / total), bb = (float)(ab / total);
    p.canvas.resize(p.target.size());
    for (size_t i = 0; i < total; ++i) { p.canvas[i * 3] = br; p.canvas[i * 3 + 1] = bg; p.canvas[i * 3 + 2] = bb; }

    float unit = widthUnits / p.w;                                        // level units per work pixel
    float heightUnits = p.h * unit;
    float left = centre.x - widthUnits / 2.f, top = centre.y + heightUnits / 2.f;
    float circle = contentWidth(ID_CIRCLE, 8.f);
    auto tail = [&](int z) {
        std::string t = fmt::format(",21,{},24,{},25,{},20,{},41,1,121,1", CHANNEL_WHITE, Z_LAYER_B5, z, layer);
        if (group > 0) t += fmt::format(",57,{}", group);
        return t + ";";
    };
    // canvas: one big rectangle in the average colour
    str += fmt::format("1,{},2,{:.2f},3,{:.2f},43,{},128,{:.4f},129,{:.4f}", ID_SQUARE, centre.x, centre.y - 90.f,
                       hsvText(br, bg, bb), widthUnits / 30.f, heightUnits / 30.f) + tail(-100);
    firstPos = centre;
    int count = 1;
    for (int i = 0; i < strokes; ++i) {
        float t = (float)i / std::max(1, strokes - 1);
        float maxR = std::max(3.f, p.w * (0.30f * (1.f - t) * (1.f - t) + 0.035f));   // big strokes first, fine detail last
        Stroke s = p.next(maxR);
        float x = left + s.cx * unit, y = top - s.cy * unit;
        int z = -99 + (int)(198.f * i / std::max(1, strokes));            // later strokes lie on top
        str += fmt::format("1,{},2,{:.2f},3,{:.2f},6,{:.1f},43,{},128,{:.4f},129,{:.4f}", ID_CIRCLE, x, y - 90.f,
                           s.ang * 57.29578f, hsvText(s.r, s.g, s.bl), 2.f * s.a * unit / circle, 2.f * s.b * unit / circle) + tail(z);
        ++count;
    }
    return count;
}

static void convert(EditorUI* ui) {
    auto lel = LevelEditorLayer::get();
    if (!lel || !ui) return;
    auto path = newestImage();
    if (path.empty()) {
        FLAlertLayer::create("Image Background", "Put a <cy>.png</c> or <cy>.jpg</c> picture into the folder first (press <cg>IMG Folder</c>).", "OK")->show();
        return;
    }
    Pixels px;
    if (!load(path, px)) {
        FLAlertLayer::create("Image Background", "That picture could not be read. Try saving it as a normal PNG or JPG.", "OK")->show();
        return;
    }
    auto mod = Mod::get();
    int cols = std::clamp((int)mod->getSettingValue<int64_t>("width"), 16, 240);
    int rows = std::max(1, (int)std::lround((double)cols * px.h / px.w));
    float cell = (float)mod->getSettingValue<double>("cell");
    int detail = std::clamp((int)mod->getSettingValue<int64_t>("colors"), 1, 5);
    int group = (int)mod->getSettingValue<int64_t>("group");
    int layer = (int)mod->getSettingValue<int64_t>("editor-layer");
    static const int SV_STEPS[5] = {5, 8, 12, 16, 24};
    float sv = (float)SV_STEPS[detail - 1];
    bool painting = mod->getSettingValue<std::string>("style") == "painting";

    // middle of what the editor is showing right now, in level coordinates
    auto win = CCDirector::sharedDirector()->getWinSize();
    auto layerNode = lel->m_objectLayer;
    CCPoint centre = layerNode ? (ccp(win.width / 2.f, win.height / 2.f) - layerNode->getPosition()) / layerNode->getScale() : ccp(300.f, 250.f);
    float left = centre.x - cols * cell / 2.f, top = centre.y + rows * cell / 2.f;

    std::string str;
    int count = 0;
    float firstX = 0.f, firstY = 0.f;
    if (painting) {
        int strokes = std::clamp((int)mod->getSettingValue<int64_t>("strokes"), 50, 5000);
        float widthUnits = (float)mod->getSettingValue<double>("size") * 30.f;
        CCPoint first;
        count = paint(px, widthUnits, strokes, centre, group, layer, str, first);
        firstX = first.x;
        firstY = first.y;
        cols = px.w;
        rows = px.h;
    }
    auto cells = painting ? std::vector<Cell>() : grid(px, cols, rows, detail);
    str.reserve(str.size() + (painting ? 0 : (size_t)cols * rows * 40));
    for (int cy = 0; !painting && cy < rows; ++cy) {
        for (int cx = 0; cx < cols;) {
            const Cell& c = cells[(size_t)cy * cols + cx];
            if (c.empty) { ++cx; continue; }
            int run = 1;                                                // merge equal neighbours into one wide square
            while (cx + run < cols) {
                const Cell& n = cells[(size_t)cy * cols + cx + run];
                if (n.empty || n.h != c.h || n.s != c.s || n.v != c.v) break;
                ++run;
            }
            float x = left + (cx + run / 2.f) * cell, y = top - (cy + 0.5f) * cell;
            float hue = c.h > 180 ? c.h - 360.f : (float)c.h;
            // start colour is white (S = 0, V = 1); saturation and brightness are added to it (the two 1 flags)
            str += fmt::format("1,{},2,{:.2f},3,{:.2f},21,{},24,{},25,-100,20,{},41,1,43,{:.0f}a{:.3f}a{:.3f}a1a1,121,1,128,{:.4f},129,{:.4f}",
                               ID_SQUARE, x, y - 90.f, CHANNEL_WHITE, Z_LAYER_B5, layer, hue, c.s / sv, c.v / sv - 1.f,
                               run * cell / 30.f + 0.01f, cell / 30.f + 0.01f);
            if (group > 0) str += fmt::format(",57,{}", group);
            str += ";";
            if (count == 0) { firstX = x; firstY = y; }
            ++count;
            cx += run;
        }
    }
    if (count == 0) {
        FLAlertLayer::create("Image Background", "The picture is completely transparent.", "OK")->show();
        return;
    }
    if (count > MAX_OBJECTS) {
        FLAlertLayer::create("Image Background", fmt::format("That would be <cr>{}</c> objects. Lower <cy>Width</c> or <cy>Colour detail</c> in the settings.", count), "OK")->show();
        return;
    }
    auto pasted = ui->pasteObjects(str, false, false);
    // the game may shift pasted objects; put the picture exactly where it was planned
    if (pasted && pasted->count() > 0) {
        if (auto first = typeinfo_cast<GameObject*>(pasted->objectAtIndex(0))) {
            CCPoint delta = ccp(firstX, firstY) - first->getPosition();
            if (std::fabs(delta.x) > 0.5f || std::fabs(delta.y) > 0.5f)
                for (auto obj : CCArrayExt<GameObject*>(pasted))
                    if (obj) ui->moveObject(obj, delta);
        }
    }
    Notification::create(fmt::format("Image Background: {} objects", count), NotificationIcon::Success)->show();
    log::info("converted {}x{} px into {} objects", px.w, px.h, count);
}

} // namespace img

class $modify(ImgEditorUI, EditorUI) {
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        auto make = [this](const char* text, const char* bg, SEL_MenuHandler cb) {
            auto spr = ButtonSprite::create(text, "goldFont.fnt", bg, 0.6f);
            spr->setScale(0.55f);
            return CCMenuItemSpriteExtra::create(spr, this, cb);
        };
        auto menu = CCMenu::create();
        menu->addChild(make("IMG to BG", "GJ_button_01.png", menu_selector(ImgEditorUI::onImgConvert)));
        menu->addChild(make("IMG Folder", "GJ_button_04.png", menu_selector(ImgEditorUI::onImgFolder)));
        menu->addChild(make("IMG Settings", "GJ_button_05.png", menu_selector(ImgEditorUI::onImgSettings)));
        menu->alignItemsHorizontallyWithPadding(5.f);
        auto win = CCDirector::sharedDirector()->getWinSize();
        menu->setPosition({win.width / 2.f, win.height - 88.f});
        this->addChild(menu, 100);
        return true;
    }

    void onImgConvert(CCObject*) { img::convert(this); }

    void onImgFolder(CCObject*) {
        std::error_code ec;
        fs::create_directories(img::folder(), ec);
        utils::file::openFolder(img::folder());
    }

    void onImgSettings(CCObject*) { geode::openSettingsPopup(Mod::get(), false); }
};
