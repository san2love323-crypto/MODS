// Auto Layout - generates a layout around the path the player actually played in an editor playtest.
//
// Flow (GEN button ON):
//   playtest starts -> everything generated earlier is removed, random gamemode / speed / mini portals are placed ahead
//   while playing   -> the player's position, gamemode and state are sampled every frame and, in live mode,
//                      blocks and spikes appear right behind the player while they play
//   playtest stops  -> portals the player never touched are removed (and, with live mode off, everything is built now)
//
// Every obstacle is checked against the recorded path with conservative (slightly too large) hitboxes,
// so the level is always beatable with the inputs that were recorded.
#include <Geode/Geode.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <vector>
#include <random>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <string>

using namespace geode::prelude;

namespace gen {

enum Mode { CUBE, SHIP, BALL, UFO, WAVE, ROBOT, SPIDER, SWING, MODE_COUNT };
static const int   PORTAL_ID[MODE_COUNT] = {12, 13, 47, 111, 660, 745, 1331, 1933};
static const char* MODE_KEY[MODE_COUNT]  = {"mode-cube", "mode-ship", "mode-ball", "mode-ufo", "mode-wave", "mode-robot", "mode-spider", "mode-swing"};
static const int   SPEED_ID[5] = {200, 201, 202, 203, 1334};
constexpr int GEN_LAYER = 77;                        // editor layer that marks generated objects
static const int ORB_ID[3] = {36, 141, 1333};        // yellow, pink, red
constexpr float WALL_TOP = 465.f;                    // walls are filled up to here (above any flying-mode ceiling)
constexpr int ID_BLOCK = 1, ID_SPIKE = 8, ID_MINI_ON = 101, ID_MINI_OFF = 99;

struct Sample { float x, y, v; int mode; bool ground, mini; };
struct Box { float x0, y0, x1, y1; };
struct Portal { Ref<GameObject> obj; float x, y, halfW, halfH; };

static bool g_armed = false;
static bool g_recording = false;
static LevelEditorLayer* g_owner = nullptr;          // editor the vectors below belong to
static ButtonSprite* g_label = nullptr;
static std::vector<Sample> g_path;
static std::vector<Ref<GameObject>> g_obstacles;
static std::vector<Portal> g_portals;
static std::vector<Portal> g_orbs;                   // jump orbs offered to the player; the ones never touched are removed
// state of the current run
static bool  g_live = true;
static float g_doneX = 0.f;                          // everything up to here already existed: never generate there
static float g_nextCol = 0.f;                        // next 30-unit column to build
static long long g_airStart = -1;                    // index of the last grounded sample before the current jump
static int   g_fpLeft = 0, g_fpMade = 0;
static float g_density = 0.6f, g_baseGap = 33.f, g_fps = 240.f;
static std::mt19937 g_rng{(unsigned)std::chrono::steady_clock::now().time_since_epoch().count()};

static float frand(float a, float b) { return std::uniform_real_distribution<float>(a, b)(g_rng); }
static int   irand(int a, int b)     { return std::uniform_int_distribution<int>(a, b)(g_rng); }
static bool  chance(float p)         { return frand(0.f, 1.f) < p; }

static void setLabel() {
    if (g_label) g_label->setString(g_armed ? "GEN: ON" : "GEN: OFF");
}

// ---- object helpers ---------------------------------------------------------------------------------
static GameObject* place(LevelEditorLayer* lel, int id, float x, float y, float rot = 0.f) {
    auto obj = lel->createObject(id, ccp(x, y), true);
    if (obj && rot != 0.f) obj->setRotation(rot);
    if (obj) obj->m_editorLayer = GEN_LAYER;
    return obj;
}

static void obstacle(LevelEditorLayer* lel, int id, float x, float y, float rot = 0.f) {
    if (auto obj = place(lel, id, x, y, rot)) g_obstacles.push_back(obj);
}

// conservative hitboxes (a little larger than the real ones, so "valid" really means safe)
static Box blockBox(float x, float y) { return {x - 15.f, y - 15.f, x + 15.f, y + 15.f}; }
static Box spikeBox(float x, float y, bool down) {
    return down ? Box{x - 5.f, y - 6.f, x + 5.f, y + 15.f} : Box{x - 5.f, y - 15.f, x + 5.f, y + 6.f};
}

static float halfSize(const Sample& s) { return s.mini ? 9.f : 15.f; }      // full-size box for every gamemode: never smaller than the real one

// true if the recorded path never comes closer than `margin` to the box
static bool valid(const Box& b, float margin) {
    auto it = std::lower_bound(g_path.begin(), g_path.end(), b.x0 - 60.f, [](const Sample& s, float x) { return s.x < x; });
    size_t i = it == g_path.begin() ? 0 : (size_t)(it - g_path.begin()) - 1;
    for (; i + 1 < g_path.size(); ++i) {
        const Sample& a = g_path[i];
        const Sample& c = g_path[i + 1];
        if (a.x > b.x1 + 60.f) break;
        float h = std::max(halfSize(a), halfSize(c)) + margin;
        float x0 = std::min(a.x, c.x) - h, x1 = std::max(a.x, c.x) + h;     // swept box between two samples
        float y0 = std::min(a.y, c.y) - h, y1 = std::max(a.y, c.y) + h;
        if (x0 < b.x1 && x1 > b.x0 && y0 < b.y1 && y1 > b.y0) return false;
    }
    return true;
}

static bool nearPortal(float x) {
    for (auto& p : g_portals)
        if (std::fabs(p.x - x) < 50.f) return true;
    return false;
}

// ---- before the playtest: place random portals past the existing content ---------------------------
static void beginRun(LevelEditorLayer* lel) {
    g_obstacles.clear();
    g_portals.clear();
    g_orbs.clear();
    g_path.clear();
    g_owner = lel;
    g_doneX = 0.f;
    // remove everything this mod generated before (it is all on editor layer GEN_LAYER), then build over the whole level again
    if (lel->m_objects) {
        std::vector<Ref<GameObject>> old;
        for (auto obj : CCArrayExt<GameObject*>(lel->m_objects))
            if (obj && obj->m_editorLayer == GEN_LAYER) old.push_back(obj);
        for (auto& obj : old) lel->removeObject(obj, true);
    }
    auto mod = Mod::get();
    g_live = mod->getSettingValue<bool>("live");
    g_density = (float)mod->getSettingValue<double>("density");
    static const float GAPS[5] = {60.f, 45.f, 33.f, 24.f, 16.f};
    g_baseGap = GAPS[std::clamp((int)mod->getSettingValue<int64_t>("difficulty"), 1, 5) - 1];
    g_fps = (float)std::atof(mod->getSettingValue<std::string>("fp-fps").c_str());
    if (g_fps < 30.f) g_fps = 240.f;
    g_fpLeft = (int)mod->getSettingValue<int64_t>("fp-count");
    g_fpMade = 0;
    g_airStart = -1;
    g_nextCol = 0.f;
}

// portals are stacked over the whole play height so they are hit no matter where the player flies
static void portalStack(LevelEditorLayer* lel, int id, float x, float halfH) {
    for (float y = halfH + 2.f; y < 320.f; y += halfH * 2.f)
        if (auto obj = place(lel, id, x, y)) g_portals.push_back({obj, x, y, 20.f, halfH});
}

// jump orbs along a cube / robot stretch, at a height a normal jump reaches. avoid = x positions of other portals.
static void orbRow(LevelEditorLayer* lel, float x0, float x1, const std::vector<float>& avoid) {
    for (float x = x0 + frand(200.f, 400.f); x < x1 - 150.f; x += frand(300.f, 650.f)) {
        bool clash = false;
        for (float a : avoid) clash = clash || std::fabs(a - x) < 90.f;
        if (clash) continue;
        float y = chance(0.5f) ? 75.f : 105.f;
        if (auto obj = place(lel, ORB_ID[irand(0, 2)], x, y)) g_orbs.push_back({obj, x, y, 18.f, 18.f});
    }
}

static void prepare(LevelEditorLayer* lel) {
    beginRun(lel);
    auto mod = Mod::get();
    std::vector<int> modes;
    for (int m = 0; m < MODE_COUNT; ++m)
        if (mod->getSettingValue<bool>(MODE_KEY[m])) modes.push_back(m);
    if (modes.empty()) modes.push_back(CUBE);
    bool speeds = mod->getSettingValue<bool>("speed-portals");
    bool minis = mod->getSettingValue<bool>("mini-portals");
    bool orbs = mod->getSettingValue<bool>("orbs");
    int cur = CUBE;                                  // the player starts as a cube
    bool mini = false;
    float x = 105.f;
    if (std::find(modes.begin(), modes.end(), (int)CUBE) != modes.end()) x = frand(900.f, 1800.f);   // cube for a while
    float total = x + (float)mod->getSettingValue<int64_t>("length") * 390.f;
    if (orbs && x > 600.f) orbRow(lel, 250.f, x, {});                  // the opening cube stretch
    while (x < total) {
        int next = modes[irand(0, (int)modes.size() - 1)];
        if (modes.size() > 1)
            while (next == cur) next = modes[irand(0, (int)modes.size() - 1)];
        if (next != cur) {
            portalStack(lel, PORTAL_ID[next], x, 43.f);
            cur = next;
        }
        float seg = frand(900.f, 2400.f);
        std::vector<float> extra;
        if (speeds && chance(0.6f)) {
            extra.push_back(x + seg * frand(0.3f, 0.5f));
            portalStack(lel, SPEED_ID[irand(1, 4)], extra.back(), 22.f);
        }
        if (minis && chance(0.3f)) {
            mini = !mini;
            extra.push_back(x + seg * frand(0.6f, 0.8f));
            portalStack(lel, mini ? ID_MINI_ON : ID_MINI_OFF, extra.back(), 43.f);
        }
        if (orbs && (cur == CUBE || cur == ROBOT)) orbRow(lel, x, x + seg, extra);
        x += seg;
    }
}

// ---- while playing ----------------------------------------------------------------------------------
static void record(PlayerObject* p, float dt) {
    if (!p || p->m_isDead) return;
    auto pos = p->getPosition();
    if (!g_path.empty() && pos.x <= g_path.back().x + 0.01f) return;       // x must increase
    int mode = CUBE;
    if (p->m_isShip) mode = SHIP;
    else if (p->m_isBall) mode = BALL;
    else if (p->m_isBird) mode = UFO;
    else if (p->m_isDart) mode = WAVE;
    else if (p->m_isRobot) mode = ROBOT;
    else if (p->m_isSpider) mode = SPIDER;
    else if (p->m_isSwing) mode = SWING;
    float v = (!g_path.empty() && dt > 0.f) ? (pos.x - g_path.back().x) / dt : 311.58f;
    g_path.push_back({pos.x, pos.y, v, mode, (bool)p->m_isOnGround, p->m_vehicleSize < 0.9f});
}

static void step(LevelEditorLayer* lel, size_t idx);

// ---- after the playtest: build the layout -----------------------------------------------------------
struct Jump { size_t i0, i1; bool fp = false; };     // i0 = last grounded sample, i1 = first grounded sample after the jump

static void keepTouchedPortals(LevelEditorLayer* lel) {
    std::vector<Portal> kept;
    for (auto& p : g_portals) {
        Box b{p.x - p.halfW - 8.f, p.y - p.halfH - 8.f, p.x + p.halfW + 8.f, p.y + p.halfH + 8.f};
        bool touched = !g_path.empty() && p.x <= g_path.back().x && !valid(b, 0.f);
        if (touched) kept.push_back(p);
        else if (p.obj && p.obj->getParent()) lel->removeObject(p.obj, true);
    }
    g_portals.swap(kept);
    std::vector<Portal> keptOrbs;
    for (auto& o : g_orbs) {
        Box b{o.x - 20.f, o.y - 20.f, o.x + 20.f, o.y + 20.f};
        bool touched = !g_path.empty() && o.x <= g_path.back().x && !valid(b, 0.f);
        if (touched) keptOrbs.push_back(o);
        else if (o.obj && o.obj->getParent()) lel->removeObject(o.obj, true);
    }
    g_orbs.swap(keptOrbs);
}

// true if the path went through a jump orb somewhere between x0 and x1
static bool orbUsed(float x0, float x1) {
    for (auto& o : g_orbs)
        if (o.x > x0 && o.x < x1 && !valid(Box{o.x - 20.f, o.y - 20.f, o.x + 20.f, o.y + 20.f}, 0.f)) return true;
    return false;
}

static void buildJump(LevelEditorLayer* lel, const Jump& j) {
    float density = g_density, fps = g_fps;
    const Sample& a = g_path[j.i0];
    const Sample& b = g_path[j.i1];
    float gy = a.y - halfSize(a) + 15.f;              // centre height of an object standing on the ground
    if (std::fabs(b.y - a.y) > 4.f) return;           // only jumps that land on the same height
    if (j.fp) {
        float w = (a.v > 100.f ? a.v : 311.58f) / fps;                      // distance travelled in one frame
        float m = std::max(0.5f, w * 0.5f);
        float first = 0.f, last = 0.f;
        for (float x = a.x + 6.f; x < a.x + 70.f && x < b.x; x += 0.5f)     // as close to the takeoff as the path allows
            if (valid(spikeBox(x, gy, false), m)) { first = x; break; }
        for (float x = b.x - 6.f; x > b.x - 70.f && x > a.x; x -= 0.5f)     // as close to the landing as the path allows
            if (valid(spikeBox(x, gy, false), m)) { last = x; break; }
        if (first == 0.f || last == 0.f || last < first) return;
        obstacle(lel, ID_SPIKE, first, gy);
        if (last - first > 12.f) obstacle(lel, ID_SPIKE, last, gy);
        for (float x = first + 30.f; x < last - 20.f; x += 30.f)
            if (valid(spikeBox(x, gy, false), 1.5f)) obstacle(lel, ID_SPIKE, x, gy);
        return;
    }
    // normal jump: up to three spikes under the arc, sometimes a block with a spike on top
    std::vector<float> xs;
    for (float x = std::floor(a.x / 30.f) * 30.f + 15.f; x < b.x; x += 30.f)
        if (x > a.x && !nearPortal(x) && valid(spikeBox(x, gy, false), 5.f)) xs.push_back(x);
    if (xs.empty()) return;
    if (orbUsed(a.x, b.x)) {                          // orb jump: spikes all the way under it, so the orb is really needed
        for (float x : xs) obstacle(lel, ID_SPIKE, x, gy);
        return;
    }
    size_t mid = xs.size() / 2;
    if (chance(0.35f) && valid(blockBox(xs[mid], gy), 3.f) && valid(spikeBox(xs[mid], gy + 30.f, false), 3.f)) {
        obstacle(lel, ID_BLOCK, xs[mid], gy);
        obstacle(lel, ID_SPIKE, xs[mid], gy + 30.f);
        return;
    }
    int want = 1 + (chance(density) ? 1 : 0) + (chance(density * 0.6f) ? 1 : 0);
    size_t from = mid >= (size_t)(want / 2) ? mid - want / 2 : 0;
    for (size_t k = from; k < xs.size() && k < from + want; ++k) obstacle(lel, ID_SPIKE, xs[k], gy);
}

// one 30-unit column at x: hanging block for cube / robot, floor + ceiling of a corridor for every other gamemode
static void buildColumn(LevelEditorLayer* lel, float x) {
    if (x <= g_doneX + 60.f || nearPortal(x)) return;
    auto it = std::lower_bound(g_path.begin(), g_path.end(), x - 20.f, [](const Sample& s, float v) { return s.x < v; });
    if (it == g_path.end()) return;
    float ymin = 1e9f, ymax = -1e9f, half = 15.f;
    int mode = it->mode;
    bool grounded = true, sameMode = true;
    for (auto k = it; k != g_path.end() && k->x <= x + 20.f; ++k) {
        ymin = std::min(ymin, k->y);
        ymax = std::max(ymax, k->y);
        half = halfSize(*k);
        grounded = grounded && k->ground;
        sameMode = sameMode && k->mode == mode;
    }
    if (ymin > ymax || !sameMode) return;
    if (mode == CUBE || mode == ROBOT) {
        if (!grounded || !chance(g_density * 0.25f)) return;
        float by = ymin - half + 15.f + 90.f;                              // block hanging three blocks above the ground
        if (!valid(blockBox(x, by), 4.f)) return;
        obstacle(lel, ID_BLOCK, x, by);
        if (chance(0.6f) && valid(spikeBox(x, by - 30.f, true), 4.f)) obstacle(lel, ID_SPIKE, x, by - 30.f, 180.f);
        if (valid(spikeBox(x, by + 30.f, false), 4.f)) obstacle(lel, ID_SPIKE, x, by + 30.f);       // nobody rides on top of it
        return;
    }
    float gap = g_baseGap + frand(0.f, 12.f);
    // Walls are solid all the way down to the ground and up to WALL_TOP, so the only way through is the corridor itself
    // (no flying over or under it). Every single block is still checked against the path.
    float fy = std::floor((ymin - half - gap) / 30.f) * 30.f - 15.f;       // floor
    if (fy >= 15.f && valid(blockBox(x, fy), 1.f)) {
        obstacle(lel, ID_BLOCK, x, fy);
        for (float y = fy - 30.f; y >= 15.f; y -= 30.f)
            if (valid(blockBox(x, y), 1.f)) obstacle(lel, ID_BLOCK, x, y);
        if (chance(g_density * 0.5f) && valid(spikeBox(x, fy + 30.f, false), 2.f)) obstacle(lel, ID_SPIKE, x, fy + 30.f);
    }
    float cy = std::ceil((ymax + half + gap) / 30.f) * 30.f + 15.f;        // ceiling
    if (cy <= WALL_TOP && valid(blockBox(x, cy), 1.f)) {
        obstacle(lel, ID_BLOCK, x, cy);
        for (float y = cy + 30.f; y <= WALL_TOP; y += 30.f)
            if (valid(blockBox(x, y), 1.f)) obstacle(lel, ID_BLOCK, x, y);
        if (chance(g_density * 0.5f) && valid(spikeBox(x, cy - 30.f, true), 2.f)) obstacle(lel, ID_SPIKE, x, cy - 30.f, 180.f);
    }
}

// A kept portal must not be avoidable: wall off everything above and below it at the same x.
static void framePortals(LevelEditorLayer* lel) {
    for (auto& p : g_portals) {
        auto freeCell = [&](float y) {
            Box b = blockBox(p.x, y);
            for (auto& q : g_portals)                                       // never cover another kept portal
                if (std::fabs(q.x - p.x) < 40.f && b.y0 < q.y + q.halfH + 4.f && b.y1 > q.y - q.halfH - 4.f) return false;
            return valid(b, 1.f);
        };
        for (float y = std::ceil((p.y + p.halfH + 15.f) / 30.f) * 30.f + 15.f; y <= WALL_TOP; y += 30.f)
            if (freeCell(y)) obstacle(lel, ID_BLOCK, p.x, y);
        for (float y = std::floor((p.y - p.halfH - 15.f) / 30.f) * 30.f - 15.f; y >= 15.f; y -= 30.f)
            if (freeCell(y)) obstacle(lel, ID_BLOCK, p.x, y);
    }
}

// advance the generator to path sample `idx`. Only looks at samples up to idx, so it can run while the player is playing:
// a jump is built the moment the player lands, a column once the player is 60 units past it.
static void step(LevelEditorLayer* lel, size_t idx) {
    if (idx == 0 || idx >= g_path.size()) return;
    const Sample& prev = g_path[idx - 1];
    const Sample& cur = g_path[idx];
    bool jumpMode = cur.mode == CUBE || cur.mode == ROBOT;
    if (g_airStart >= 0 && (cur.mode != g_path[(size_t)g_airStart].mode)) g_airStart = -1;      // changed gamemode mid-air
    if (jumpMode && prev.ground && !cur.ground && prev.mode == cur.mode) g_airStart = (long long)idx - 1;
    else if (g_airStart >= 0 && cur.ground) {
        Jump j{(size_t)g_airStart, idx};
        g_airStart = -1;
        const Sample& a = g_path[j.i0];
        if (a.x > g_doneX + 60.f) {
            if (g_fpLeft > 0 && chance(0.25f) && !nearPortal(a.x) && !nearPortal(cur.x)) { j.fp = true; --g_fpLeft; ++g_fpMade; }
            buildJump(lel, j);
        }
    }
    if (g_nextCol == 0.f) g_nextCol = std::floor(g_path.front().x / 30.f) * 30.f + 75.f;
    while (g_nextCol + 60.f < cur.x) {
        buildColumn(lel, g_nextCol);
        g_nextCol += 30.f;
    }
}

static void finish(LevelEditorLayer* lel) {
    if (g_path.size() < 20) {
        Notification::create("Auto Layout: nothing recorded", NotificationIcon::Error)->show();
        return;
    }
    if (!g_live)
        for (size_t i = 1; i < g_path.size(); ++i) step(lel, i);
    keepTouchedPortals(lel);
    framePortals(lel);
    auto msg = fmt::format("Auto Layout: {} objects, {} frame perfects", g_obstacles.size(), g_fpMade);
    Notification::create(msg, NotificationIcon::Success)->show();
    log::info("{} (path samples: {})", msg, g_path.size());
}

} // namespace gen

class $modify(GenEditorUI, EditorUI) {
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        gen::g_armed = false;
        gen::g_recording = false;
        auto spr = ButtonSprite::create("GEN: OFF", "goldFont.fnt", "GJ_button_04.png", 0.6f);
        spr->setScale(0.6f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(GenEditorUI::onToggleGen));
        auto setSpr = ButtonSprite::create("Settings", "goldFont.fnt", "GJ_button_05.png", 0.6f);
        setSpr->setScale(0.6f);
        auto setBtn = CCMenuItemSpriteExtra::create(setSpr, this, menu_selector(GenEditorUI::onGenSettings));
        auto menu = CCMenu::create();
        menu->addChild(btn);
        menu->addChild(setBtn);
        menu->alignItemsHorizontallyWithPadding(6.f);
        auto win = CCDirector::sharedDirector()->getWinSize();
        menu->setPosition({win.width / 2.f, win.height - 62.f});
        this->addChild(menu, 100);
        gen::g_label = spr;
        return true;
    }

    void onGenSettings(CCObject*) {
        geode::openSettingsPopup(Mod::get());
    }

    void onToggleGen(CCObject*) {
        gen::g_armed = !gen::g_armed;
        gen::setLabel();
    }
};

class $modify(GenEditorLayer, LevelEditorLayer) {
    void onPlaytest() {
        if (gen::g_armed) gen::prepare(this);             // portals must exist before the playtest starts
        LevelEditorLayer::onPlaytest();
        gen::g_recording = gen::g_armed;
    }

    void onStopPlaytest() {
        bool was = gen::g_recording;
        gen::g_recording = false;
        LevelEditorLayer::onStopPlaytest();
        if (was) {
            gen::finish(this);                            // GEN stays on until the player turns it off
        }
    }
};

class $modify(GenGameLayer, GJBaseGameLayer) {
    void update(float dt) {
        GJBaseGameLayer::update(dt);
        if (!gen::g_recording) return;
        size_t before = gen::g_path.size();
        gen::record(m_player1, dt);
        if (gen::g_live && gen::g_owner && gen::g_path.size() > before)
            gen::step(gen::g_owner, gen::g_path.size() - 1);          // build right behind the player, while they play
    }
};
