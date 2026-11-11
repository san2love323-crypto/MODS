// Auto Layout - builds a layout around the way the player plays an editor playtest, while they play.
//
// With GEN on, during a playtest:
//   * clicking in mid-air puts a jump orb exactly where the player is, so the click becomes an orb jump
//   * every few seconds a gamemode / speed / mini portal appears right in front of the player
//   * blocks and spikes are built just behind the player, around the path that was actually played
// When the playtest stops, every portal gets walled in above and below so it cannot be avoided.
//
// Every obstacle is checked against the recorded path first (with boxes a little larger than the real hitboxes),
// so the result is always beatable with the inputs that were played.
//
// Positions are editor positions: the ground surface is at y = 90, so an object standing on it has y = 105.
#include <Geode/Geode.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
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
static const int   ORB_ID[3] = {36, 141, 1333};      // yellow, pink, red
constexpr int   GEN_LAYER = 77;                      // editor layer that marks generated objects
constexpr float GROUND = 90.f;                       // y of the ground surface
constexpr float WALL_TOP = GROUND + 465.f;           // walls are filled up to here (above any flying-mode ceiling)
constexpr int   ID_BLOCK = 1, ID_SPIKE = 8, ID_MINI_ON = 101, ID_MINI_OFF = 99;

struct Sample { float x, y, v, hw, hh; int mode; bool ground, mini; };   // hw / hh = half of the player's real hitbox
struct Box { float x0, y0, x1, y1; };
struct Portal { float x, y, halfW, halfH; };

static bool g_armed = false;
static bool g_recording = false;
static ButtonSprite* g_label = nullptr;
static std::vector<Sample> g_path;
static std::vector<Portal> g_portals;                // gamemode / speed / mini portals placed this run
static std::vector<Portal> g_orbs;                   // jump orbs placed this run
static int g_objects = 0;

// state of the current run
static bool  g_live = true;
static float g_nextCol = 0.f;                        // next 30-unit column to build
static long long g_airStart = -1;                    // index of the last grounded sample before the current jump
static int   g_fpLeft = 0, g_fpMade = 0;
static float g_density = 0.6f, g_baseGap = 33.f, g_fps = 240.f;
static bool  g_wasClicking = false;
static bool  g_useOrbs = true, g_useSpeeds = true, g_useMinis = false;
static float g_modeTimer = 0.f, g_speedTimer = 0.f, g_miniTimer = 0.f, g_orbCooldown = 0.f;
static float g_lastPortalX = -1000.f;
static std::vector<int> g_modes;
static std::mt19937 g_rng{(unsigned)std::chrono::steady_clock::now().time_since_epoch().count()};

static float frand(float a, float b) { return std::uniform_real_distribution<float>(a, b)(g_rng); }
static int   irand(int a, int b)     { return std::uniform_int_distribution<int>(a, b)(g_rng); }
static bool  chance(float p)         { return frand(0.f, 1.f) < p; }

static void setLabel() {
    if (g_label) g_label->setString(g_armed ? "GEN: ON" : "GEN: OFF");
}

// ---- object helpers ---------------------------------------------------------------------------------
static GameObject* place(LevelEditorLayer* lel, int id, float x, float y, bool flipY = false) {
    auto obj = lel->createObject(id, ccp(x, y), true);
    if (!obj) return nullptr;
    if (flipY) obj->setFlipY(true);
    obj->m_editorLayer = GEN_LAYER;
    ++g_objects;
    return obj;
}

// conservative boxes (a little larger than the real hitboxes, so "valid" really means safe)
static Box blockBox(float x, float y) { return {x - 15.f, y - 15.f, x + 15.f, y + 15.f}; }
static Box spikeBox(float x, float y, bool down) {
    return down ? Box{x - 5.f, y - 6.f, x + 5.f, y + 15.f} : Box{x - 5.f, y - 15.f, x + 5.f, y + 6.f};
}

// true if the recorded path never comes closer than `margin` to the box
static bool valid(const Box& b, float margin) {
    auto it = std::lower_bound(g_path.begin(), g_path.end(), b.x0 - 60.f, [](const Sample& s, float x) { return s.x < x; });
    size_t i = it == g_path.begin() ? 0 : (size_t)(it - g_path.begin()) - 1;
    for (; i + 1 < g_path.size(); ++i) {
        const Sample& a = g_path[i];
        const Sample& c = g_path[i + 1];
        if (a.x > b.x1 + 60.f) break;
        float hw = std::max(a.hw, c.hw) + margin, hh = std::max(a.hh, c.hh) + margin;
        float x0 = std::min(a.x, c.x) - hw, x1 = std::max(a.x, c.x) + hw;       // swept box between two samples
        float y0 = std::min(a.y, c.y) - hh, y1 = std::max(a.y, c.y) + hh;
        if (x0 < b.x1 && x1 > b.x0 && y0 < b.y1 && y1 > b.y0) return false;
    }
    return true;
}

static bool nearPortal(float x) {
    for (auto& p : g_portals)
        if (std::fabs(p.x - x) < 50.f) return true;
    return false;
}

static int modeOf(PlayerObject* p) {
    if (p->m_isShip) return SHIP;
    if (p->m_isBall) return BALL;
    if (p->m_isBird) return UFO;
    if (p->m_isDart) return WAVE;
    if (p->m_isRobot) return ROBOT;
    if (p->m_isSpider) return SPIDER;
    if (p->m_isSwing) return SWING;
    return CUBE;
}

// ---- before the playtest ----------------------------------------------------------------------------
static void beginRun(LevelEditorLayer* lel) {
    g_portals.clear();
    g_orbs.clear();
    g_path.clear();
    g_objects = 0;
    // remove everything this mod generated before (it is all on editor layer GEN_LAYER), then build the level again
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
    g_wasClicking = false;
    g_useOrbs = mod->getSettingValue<bool>("orbs");
    g_useSpeeds = mod->getSettingValue<bool>("speed-portals");
    g_useMinis = mod->getSettingValue<bool>("mini-portals");
    g_modes.clear();
    for (int m = 0; m < MODE_COUNT; ++m)
        if (mod->getSettingValue<bool>(MODE_KEY[m])) g_modes.push_back(m);
    if (g_modes.empty()) g_modes.push_back(CUBE);
    bool cubeFirst = std::find(g_modes.begin(), g_modes.end(), (int)CUBE) != g_modes.end();
    g_modeTimer = cubeFirst ? frand(3.f, 6.f) : 0.6f;        // cube is not selected: switch away from it right at the start
    g_speedTimer = frand(5.f, 10.f);
    g_miniTimer = frand(8.f, 16.f);
    g_orbCooldown = 0.f;
    g_lastPortalX = -1000.f;
}

// ---- while playing: things placed AT the player (they take effect immediately) ----------------------
static void portalAtPlayer(LevelEditorLayer* lel, PlayerObject* p, int id, float halfH) {
    auto pos = p->getPosition();
    float x = pos.x + 30.f;
    float y = std::max(pos.y, GROUND + halfH + 2.f);          // never sunk into the ground
    if (place(lel, id, x, y)) {
        g_portals.push_back({x, y, 20.f, halfH});
        g_lastPortalX = x;
    }
}

static void reactive(LevelEditorLayer* lel, PlayerObject* p, float dt) {
    int mode = modeOf(p);
    bool clicking = p->m_jumpBuffered;
    auto pos = p->getPosition();
    g_orbCooldown -= dt;

    // a fresh click in mid-air becomes an orb jump: the orb is created right on the player
    bool orbMode = mode == CUBE || mode == ROBOT || mode == BALL;
    if (g_useOrbs && orbMode && clicking && !g_wasClicking && !p->m_isOnGround && p->m_stateRingJump &&
        g_orbCooldown <= 0.f && pos.x - g_lastPortalX > 60.f) {
        if (place(lel, ORB_ID[irand(0, 2)], pos.x, pos.y)) {
            g_orbs.push_back({pos.x, pos.y, 18.f, 18.f});
            g_orbCooldown = 0.12f;
        }
    }
    g_wasClicking = clicking;

    // portals appear in front of the player every few seconds
    g_modeTimer -= dt;
    g_speedTimer -= dt;
    g_miniTimer -= dt;
    bool spaced = pos.x - g_lastPortalX > 240.f;
    if (g_modeTimer <= 0.f && spaced && g_modes.size() > 0) {
        int next = g_modes[irand(0, (int)g_modes.size() - 1)];
        if (g_modes.size() > 1)
            while (next == mode) next = g_modes[irand(0, (int)g_modes.size() - 1)];
        if (next != mode) portalAtPlayer(lel, p, PORTAL_ID[next], 43.f);
        g_modeTimer = frand(4.f, 9.f);
    } else if (g_useSpeeds && g_speedTimer <= 0.f && spaced) {
        portalAtPlayer(lel, p, SPEED_ID[irand(1, 4)], 22.f);
        g_speedTimer = frand(5.f, 11.f);
    } else if (g_useMinis && g_miniTimer <= 0.f && spaced) {
        portalAtPlayer(lel, p, p->m_vehicleSize < 0.9f ? ID_MINI_OFF : ID_MINI_ON, 43.f);
        g_miniTimer = frand(8.f, 16.f);
    }
}

static bool record(PlayerObject* p, float dt) {
    if (!p || p->m_isDead) return false;
    auto pos = p->getPosition();
    if (!g_path.empty() && pos.x <= g_path.back().x + 0.01f) return false;     // paused, or not moving forward
    auto size = p->getObjectRect().size;
    float hw = std::clamp(size.width / 2.f, 5.f, 15.f), hh = std::clamp(size.height / 2.f, 5.f, 15.f);
    float v = (!g_path.empty() && dt > 0.f) ? (pos.x - g_path.back().x) / dt : 311.58f;
    g_path.push_back({pos.x, pos.y, v, hw, hh, modeOf(p), (bool)p->m_isOnGround, p->m_vehicleSize < 0.9f});
    return true;
}

// ---- obstacles built behind the player --------------------------------------------------------------
struct Jump { size_t i0, i1; bool fp = false; };     // i0 = last grounded sample, i1 = first grounded sample after the jump

// true if the path went through a jump orb somewhere between x0 and x1
static bool orbUsed(float x0, float x1) {
    for (auto& o : g_orbs)
        if (o.x > x0 && o.x < x1) return true;
    return false;
}

static void buildJump(LevelEditorLayer* lel, const Jump& j) {
    const Sample& a = g_path[j.i0];
    const Sample& b = g_path[j.i1];
    float gy = a.y - a.hh + 15.f;                     // centre height of an object standing where the player stood
    if (std::fabs(b.y - a.y) > 4.f) return;           // only jumps that land on the same height
    if (j.fp) {
        float w = (a.v > 100.f ? a.v : 311.58f) / g_fps;                    // distance travelled in one frame
        float m = std::max(0.5f, w * 0.5f);
        float first = 0.f, last = 0.f;
        for (float x = a.x + 6.f; x < a.x + 70.f && x < b.x; x += 0.5f)     // as close to the takeoff as the path allows
            if (valid(spikeBox(x, gy, false), m)) { first = x; break; }
        for (float x = b.x - 6.f; x > b.x - 70.f && x > a.x; x -= 0.5f)     // as close to the landing as the path allows
            if (valid(spikeBox(x, gy, false), m)) { last = x; break; }
        if (first == 0.f || last == 0.f || last < first) return;
        place(lel, ID_SPIKE, first, gy);
        if (last - first > 12.f) place(lel, ID_SPIKE, last, gy);
        for (float x = first + 30.f; x < last - 20.f; x += 30.f)
            if (valid(spikeBox(x, gy, false), 1.5f)) place(lel, ID_SPIKE, x, gy);
        return;
    }
    std::vector<float> xs;
    for (float x = std::floor(a.x / 30.f) * 30.f + 15.f; x < b.x; x += 30.f)
        if (x > a.x && !nearPortal(x) && valid(spikeBox(x, gy, false), 5.f)) xs.push_back(x);
    if (xs.empty()) return;
    if (orbUsed(a.x, b.x)) {                          // orb jump: spikes all the way under it, so the orbs are really needed
        for (float x : xs) place(lel, ID_SPIKE, x, gy);
        return;
    }
    // normal jump: up to three spikes under the arc, sometimes a block with a spike on top
    size_t mid = xs.size() / 2;
    if (chance(0.35f) && valid(blockBox(xs[mid], gy), 3.f) && valid(spikeBox(xs[mid], gy + 30.f, false), 3.f)) {
        place(lel, ID_BLOCK, xs[mid], gy);
        place(lel, ID_SPIKE, xs[mid], gy + 30.f);
        return;
    }
    int want = 1 + (chance(g_density) ? 1 : 0) + (chance(g_density * 0.6f) ? 1 : 0);
    size_t from = mid >= (size_t)(want / 2) ? mid - want / 2 : 0;
    for (size_t k = from; k < xs.size() && k < from + want; ++k) place(lel, ID_SPIKE, xs[k], gy);
}

// one 30-unit column at x: hanging block for cube / robot, floor + ceiling of a corridor for every other gamemode
static void buildColumn(LevelEditorLayer* lel, float x) {
    if (nearPortal(x)) return;
    auto it = std::lower_bound(g_path.begin(), g_path.end(), x - 20.f, [](const Sample& s, float v) { return s.x < v; });
    if (it == g_path.end()) return;
    float ymin = 1e9f, ymax = -1e9f, half = 15.f;
    int mode = it->mode;
    bool grounded = true, sameMode = true;
    for (auto k = it; k != g_path.end() && k->x <= x + 20.f; ++k) {
        ymin = std::min(ymin, k->y);
        ymax = std::max(ymax, k->y);
        half = k->hh;
        grounded = grounded && k->ground;
        sameMode = sameMode && k->mode == mode;
    }
    if (ymin > ymax || !sameMode) return;
    if (mode == CUBE || mode == ROBOT) {
        if (!grounded || !chance(g_density * 0.25f)) return;
        float by = ymin - half + 15.f + 90.f;                              // block hanging three blocks above the feet
        if (!valid(blockBox(x, by), 4.f)) return;
        place(lel, ID_BLOCK, x, by);
        if (chance(0.6f) && valid(spikeBox(x, by - 30.f, true), 4.f)) place(lel, ID_SPIKE, x, by - 30.f, true);
        if (valid(spikeBox(x, by + 30.f, false), 4.f)) place(lel, ID_SPIKE, x, by + 30.f);          // nobody rides on top of it
        return;
    }
    float gap = g_baseGap + frand(0.f, 12.f);
    float lowest = GROUND + 15.f;
    // Walls are solid all the way down to the ground and up to WALL_TOP, so the only way through is the corridor itself
    // (no flying over or under it). Every single block is still checked against the path.
    float fy = std::floor((ymin - half - gap - GROUND) / 30.f) * 30.f + GROUND - 15.f;      // floor
    if (fy >= lowest && valid(blockBox(x, fy), 1.f)) {
        place(lel, ID_BLOCK, x, fy);
        for (float y = fy - 30.f; y >= lowest; y -= 30.f)
            if (valid(blockBox(x, y), 1.f)) place(lel, ID_BLOCK, x, y);
        if (chance(g_density * 0.5f) && valid(spikeBox(x, fy + 30.f, false), 2.f)) place(lel, ID_SPIKE, x, fy + 30.f);
    }
    float cy = std::ceil((ymax + half + gap - GROUND) / 30.f) * 30.f + GROUND + 15.f;       // ceiling
    if (cy <= WALL_TOP && valid(blockBox(x, cy), 1.f)) {
        place(lel, ID_BLOCK, x, cy);
        for (float y = cy + 30.f; y <= WALL_TOP; y += 30.f)
            if (valid(blockBox(x, y), 1.f)) place(lel, ID_BLOCK, x, y);
        if (chance(g_density * 0.5f) && valid(spikeBox(x, cy - 30.f, true), 2.f)) place(lel, ID_SPIKE, x, cy - 30.f, true);
    }
}

// A portal must not be avoidable: wall off everything above and below it at the same x.
static void framePortals(LevelEditorLayer* lel) {
    for (auto& p : g_portals) {
        auto freeCell = [&](float y) {
            Box b = blockBox(p.x, y);
            for (auto& q : g_portals)                                       // never cover another portal
                if (std::fabs(q.x - p.x) < 40.f && b.y0 < q.y + q.halfH + 4.f && b.y1 > q.y - q.halfH - 4.f) return false;
            return valid(b, 1.f);
        };
        for (float y = std::ceil((p.y + p.halfH - GROUND) / 30.f) * 30.f + GROUND + 15.f; y <= WALL_TOP; y += 30.f)
            if (freeCell(y)) place(lel, ID_BLOCK, p.x, y);
        for (float y = std::floor((p.y - p.halfH - GROUND) / 30.f) * 30.f + GROUND - 15.f; y >= GROUND + 15.f; y -= 30.f)
            if (freeCell(y)) place(lel, ID_BLOCK, p.x, y);
    }
}

// advance the generator to path sample `idx`. Only looks at samples up to idx, so it can run while the player is playing:
// a jump is built the moment the player lands, a column once the player is 60 units past it.
static void step(LevelEditorLayer* lel, size_t idx) {
    if (idx == 0 || idx >= g_path.size()) return;
    const Sample& prev = g_path[idx - 1];
    const Sample& cur = g_path[idx];
    bool jumpMode = cur.mode == CUBE || cur.mode == ROBOT;
    if (g_airStart >= 0 && cur.mode != g_path[(size_t)g_airStart].mode) g_airStart = -1;        // changed gamemode mid-air
    if (jumpMode && prev.ground && !cur.ground && prev.mode == cur.mode) g_airStart = (long long)idx - 1;
    else if (g_airStart >= 0 && cur.ground) {
        Jump j{(size_t)g_airStart, idx};
        g_airStart = -1;
        const Sample& a = g_path[j.i0];
        if (g_fpLeft > 0 && chance(0.25f) && !nearPortal(a.x) && !nearPortal(cur.x) && !orbUsed(a.x, cur.x)) {
            j.fp = true;
            --g_fpLeft;
            ++g_fpMade;
        }
        buildJump(lel, j);
    }
    if (g_nextCol == 0.f) g_nextCol = std::floor(g_path.front().x / 30.f) * 30.f + 75.f;
    while (g_nextCol + 60.f < cur.x) {
        buildColumn(lel, g_nextCol);
        g_nextCol += 30.f;
    }
}

// called every frame of the playtest
static void tick(LevelEditorLayer* lel, float dt) {
    auto p = lel->m_player1;
    if (!p || p->m_isDead) return;
    if (!record(p, dt)) return;
    reactive(lel, p, dt);
    if (g_live) step(lel, g_path.size() - 1);
}

static void finish(LevelEditorLayer* lel) {
    if (g_path.size() < 20) {
        Notification::create("Auto Layout: nothing recorded", NotificationIcon::Error)->show();
        return;
    }
    if (!g_live)
        for (size_t i = 1; i < g_path.size(); ++i) step(lel, i);
    framePortals(lel);
    auto msg = fmt::format("Auto Layout: {} objects, {} orbs, {} frame perfects", g_objects, g_orbs.size(), g_fpMade);
    Notification::create(msg, NotificationIcon::Success)->show();
    log::info("{} (path samples: {})", msg, g_path.size());
}

// runs the generator once per frame, from the scheduler (a safe place to create objects during a playtest)
class Ticker : public CCNode {
public:
    static Ticker* create() {
        auto ret = new Ticker();
        if (ret->init()) {
            ret->autorelease();
            ret->scheduleUpdate();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void update(float dt) override {
        if (!g_recording) return;
        if (auto lel = LevelEditorLayer::get()) tick(lel, dt);
    }
};

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
        geode::openSettingsPopup(Mod::get(), false);
    }

    void onToggleGen(CCObject*) {
        gen::g_armed = !gen::g_armed;
        gen::setLabel();
    }
};

class $modify(GenEditorLayer, LevelEditorLayer) {
    bool init(GJGameLevel* level, bool noUI) {
        if (!LevelEditorLayer::init(level, noUI)) return false;
        if (auto ticker = gen::Ticker::create()) this->addChild(ticker);
        return true;
    }

    void onPlaytest() {
        if (gen::g_armed) gen::beginRun(this);            // clear the previous result before the playtest starts
        LevelEditorLayer::onPlaytest();
        gen::g_recording = gen::g_armed;
    }

    void onStopPlaytest() {
        bool was = gen::g_recording;
        gen::g_recording = false;
        LevelEditorLayer::onStopPlaytest();
        if (was) gen::finish(this);                       // GEN stays on until the player turns it off
    }
};
