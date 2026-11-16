// Image Background - rebuilds a picture out of editor objects so it can sit behind a level.
//
// How it works:
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

using namespace geode::prelude;
namespace fs = std::filesystem;

namespace img {

constexpr int ID_SQUARE = 211;          // plain colourable square, 30 x 30
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
    auto cells = grid(px, cols, rows, detail);

    // middle of what the editor is showing right now, in level coordinates
    auto win = CCDirector::sharedDirector()->getWinSize();
    auto layerNode = lel->m_objectLayer;
    CCPoint centre = layerNode ? (ccp(win.width / 2.f, win.height / 2.f) - layerNode->getPosition()) / layerNode->getScale() : ccp(300.f, 250.f);
    float left = centre.x - cols * cell / 2.f, top = centre.y + rows * cell / 2.f;

    std::string str;
    str.reserve((size_t)cols * rows * 40);
    int count = 0;
    float firstX = 0.f, firstY = 0.f;
    for (int cy = 0; cy < rows; ++cy) {
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
    Notification::create(fmt::format("Image Background: {} objects ({} x {} cells)", count, cols, rows), NotificationIcon::Success)->show();
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
