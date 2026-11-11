// Auto Layout - builds a layout around the way the player plays an editor playtest, while they play.
//
// With GEN on, during a playtest:
//   * clicking in mid-air puts an orb exactly where the player is, so the click becomes an orb jump
//     (yellow / pink / red / black / dash, blue and green for the ball - which ones depends on the difficulty)
//   * some jumps end on a new platform that appears under the player (stairs); pads appear in front of the feet
//   * every few seconds a gamemode / speed / mini / gravity portal appears right in front of the player
//   * what the player does decides what gets built: flying level makes a tight straight-fly tunnel,
//     spamming makes a spike-lined spam corridor, a dash gets a wall of spikes after it
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
constexpr int ORB_YELLOW = 36, ORB_PINK = 141, ORB_RED = 1333, ORB_BLUE = 84, ORB_GREEN = 1022, ORB_BLACK = 1330, ORB_DASH = 1704;
constexpr int PAD_YELLOW = 35, PAD_PINK = 140, PAD_RED = 1332;
constexpr int GRAVITY_NORMAL = 10, GRAVITY_FLIPPED = 11;

// everything that changes with the difficulty setting (index = difficulty - 1)
struct Tuning {
    float gap;            // free room between the path and corridor walls
    float snap;           // corridor walls snap to this grid (smaller = walls hug the path)
    float straightGap;    // room in a straight-fly tunnel
    float jumpMargin;     // how close spikes sit to a normal jump
    int   maxSpikes;      // spikes under one normal jump
    float blockSpike;     // chance of a block with a spike on top instead of plain spikes
    float hang;           // chance of a hanging block over a running part
    float wallSpike;      // chance of a spike on a corridor wall
    float platform;       // chance that a jump ends on a new platform (stairs)
    int   maxSpeed;       // fastest speed portal: 1 = 1x ... 4 = 4x
    int   orbTier;        // 1 yellow/pink, 2 + red/black, 3 + dash (and blue/green for ball)
    bool  gravity;        // gravity portals while flying
};
static const Tuning TUNING[5] = {
    {60.f, 30.f,  45.f, 8.f, 1, 0.10f, 0.10f, 0.10f, 0.15f, 2, 1, false},
    {45.f, 15.f,  32.f, 6.f, 2, 0.20f, 0.20f, 0.25f, 0.25f, 3, 1, false},
    {33.f, 7.5f,  22.f, 4.f, 3, 0.35f, 0.30f, 0.40f, 0.35f, 3, 2, true},
    {24.f, 7.5f,  14.f, 3.f, 3, 0.45f, 0.40f, 0.60f, 0.40f, 4, 3, true},
    {16.f, 7.5f,   9.f, 2.f, 4, 0.50f, 0.50f, 0.80f, 0.45f, 4, 3, true},
};
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
static std::vector<Portal> g_pads;                   // jump pads placed this run
static std::vector<Portal> g_stoppers;               // spots right after a dash ended, waiting for a wall of spikes
static Tuning g_t = TUNING[2];
struct Platform { bool active = false; float y = 0.f, lastX = 0.f; int left = 0; };
static Platform g_plat;
static bool  g_wasFalling = false, g_wasDashing = false, g_platArmed = false;
static float g_peakY = 0.f, g_platDrop = 0.f;
static float g_padTimer = 0.f, g_gravityTimer = 0.f;
static float g_surfLo = 1e9f, g_surfHi = -1e9f;       // ball / spider: the floor and ceiling surfaces touched in this part
static int g_objects = 0;

// state of the current run
static bool  g_live = true;
static float g_nextCol = 0.f;                        // next 30-unit column to build
static long long g_airStart = -1;                    // index of the last grounded sample before the current jump
static int   g_fpLeft = 0, g_fpMade = 0;
static float g_density = 0.6f, g_baseGap = 33.f, g_fps = 240.f, g_fpLeniency = 2.f;
static bool  g_wasClicking = false;
static bool  g_useOrbs = true, g_useSpeeds = true, g_useMinis = false, g_usePads = true, g_usePlatforms = true, g_useGravity = true;
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
    g_pads.clear();
    g_stoppers.clear();
    g_plat = Platform{};
    g_surfLo = 1e9f;
    g_surfHi = -1e9f;
    g_wasFalling = g_wasDashing = g_platArmed = false;
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
    g_t = TUNING[std::clamp((int)mod->getSettingValue<int64_t>("difficulty"), 1, 5) - 1];
    g_baseGap = g_t.gap;
    g_usePads = mod->getSettingValue<bool>("pads");
    g_usePlatforms = mod->getSettingValue<bool>("platforms");
    g_useGravity = mod->getSettingValue<bool>("gravity-portals");
    g_padTimer = frand(3.f, 6.f);
    g_gravityTimer = frand(6.f, 12.f);
    g_fps = (float)std::atof(mod->getSettingValue<std::string>("fp-fps").c_str());
    if (g_fps < 30.f) g_fps = 240.f;
    g_fpLeniency = (float)mod->getSettingValue<double>("fp-leniency");
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
static float speedOf(PlayerObject* p) {               // horizontal speed in units per second
    float sp = p->m_playerSpeed;
    if (sp < 0.8f) return 251.16f;
    if (sp < 1.0f) return 311.58f;
    if (sp < 1.2f) return 387.42f;
    if (sp < 1.45f) return 468.0f;
    return 576.0f;
}

static void portalAtPlayer(LevelEditorLayer* lel, PlayerObject* p, int id, float halfH, bool speed = false) {
    auto pos = p->getPosition();
    float x = pos.x + 30.f;
    float y = std::max(pos.y, GROUND + halfH + 2.f);          // never sunk into the ground
    if (auto obj = place(lel, id, x, y)) {
        if (speed)
            if (auto fx = typeinfo_cast<EffectGameObject*>(obj)) {
                fx->m_shouldPreview = true;
                lel->tryUpdateSpeedObject(fx, false);
            }
        g_portals.push_back({x, y, 20.f, halfH});
        g_lastPortalX = x;
    }
}

// which orb a mid-air click turns into, by difficulty and situation
static int pickOrb(int mode, bool falling) {
    std::vector<int> pool = {ORB_YELLOW, ORB_YELLOW, ORB_PINK};
    if (g_t.orbTier >= 2) {
        pool.push_back(ORB_RED);
        if (!falling && mode != BALL) pool.push_back(ORB_BLACK);          // black orb: slam back down from the way up
    }
    if (g_t.orbTier >= 3) {
        if (falling) { pool.push_back(ORB_DASH); pool.push_back(ORB_DASH); }   // dash orb: hold to fly straight
        if (mode == BALL) { pool.push_back(ORB_BLUE); pool.push_back(ORB_GREEN); }   // gravity orbs only where there is a ceiling
    }
    return pool[irand(0, (int)pool.size() - 1)];
}

static void reactive(LevelEditorLayer* lel, PlayerObject* p, float dt) {
    int mode = modeOf(p);
    bool clicking = p->m_jumpBuffered;
    bool grounded = p->m_isOnGround;
    bool upside = p->m_isUpsideDown;
    bool dashing = p->m_isDashing;
    auto pos = p->getPosition();
    auto size = p->getObjectRect().size;
    float hw = size.width / 2.f, hh = size.height / 2.f;
    float feet = pos.y - hh;
    float vx = speedOf(p) * dt;                               // movement this frame
    float vy = (float)p->getYVelocity() * 60.f * dt;
    bool jumpMode = mode == CUBE || mode == ROBOT;
    bool falling = !grounded && vy < -0.01f;
    g_orbCooldown -= dt;

    // --- orb chain: a fresh click in mid-air becomes an orb jump, the orb is created right on the player
    bool orbMode = jumpMode || mode == BALL;
    if (g_useOrbs && orbMode && clicking && !g_wasClicking && !grounded && !dashing && p->m_stateRingJump &&
        g_orbCooldown <= 0.f && pos.x - g_lastPortalX > 60.f && (!upside || mode == BALL)) {
        int id = pickOrb(mode, falling);
        if (auto obj = place(lel, id, pos.x, pos.y)) {
            if (auto ring = typeinfo_cast<RingObject*>(obj)) p->addToTouchedRings(ring);     // counts as touched right away
            g_orbs.push_back({pos.x, pos.y, 18.f, 18.f});
            g_orbCooldown = 0.12f;
            g_plat.active = false;
            g_platArmed = false;
        }
    }
    g_wasClicking = clicking;

    // --- dash orb stopper: when a dash ends, remember the spot; a wall of spikes goes right after it later
    if (g_wasDashing && !dashing) g_stoppers.push_back({pos.x + 75.f, pos.y, 0.f, 0.f});
    g_wasDashing = dashing;

    if (jumpMode && !upside) {
        // --- stairs: some jumps end on a brand new platform that appears under the player on the way down
        if (falling && !g_wasFalling) {
            g_peakY = pos.y;
            g_platDrop = frand(0.f, 50.f);
            g_platArmed = g_usePlatforms && !g_plat.active && chance(g_t.platform);
        }
        if (g_platArmed && falling && !clicking && !dashing && g_peakY - pos.y >= g_platDrop &&
            feet > GROUND + 55.f && pos.x - g_lastPortalX > 90.f) {
            float bx = pos.x + vx, by = feet + vy - 15.f;     // top of the block = where the feet are next frame
            if (place(lel, ID_BLOCK, bx, by)) g_plat = Platform{true, by, bx, irand(2, 7)};
            g_platArmed = false;
        }
        if (g_plat.active) {
            bool onIt = std::fabs(feet - (g_plat.y + 15.f)) < 6.f;
            if (clicking || pos.x > g_plat.lastX + 60.f || (grounded && !onIt)) g_plat.active = false;
            while (g_plat.active && g_plat.left > 0 && g_plat.lastX + 15.f < pos.x + hw + vx * 4.f + 8.f) {
                g_plat.lastX += 30.f;                          // keep the platform ahead of the player
                place(lel, ID_BLOCK, g_plat.lastX, g_plat.y);
                if (--g_plat.left == 0) g_plat.active = false;
            }
        }
        // --- pads: now and then a pad appears right in front of the feet
        g_padTimer -= dt;
        if (g_usePads && g_padTimer <= 0.f && grounded && !clicking && !dashing && pos.x - g_lastPortalX > 90.f) {
            int id = g_t.orbTier >= 2 && chance(0.2f) ? PAD_RED : (chance(0.4f) ? PAD_PINK : PAD_YELLOW);
            float px = pos.x + hw + 14.f, py = feet + 3.f;     // low on the ground, just ahead of the player
            if (place(lel, id, px, py)) g_pads.push_back({px, py, 15.f, 6.f});
            g_padTimer = frand(3.f, 7.f);
        }
    } else {
        g_plat.active = false;
        g_platArmed = false;
    }
    g_wasFalling = falling;

    // --- portals appear in front of the player every few seconds
    g_modeTimer -= dt;
    g_speedTimer -= dt;
    g_miniTimer -= dt;
    g_gravityTimer -= dt;
    bool spaced = pos.x - g_lastPortalX > 240.f && !dashing;
    bool bounded = mode == SHIP || mode == UFO || mode == WAVE || mode == SWING;       // floor and ceiling exist
    if (g_modeTimer <= 0.f && spaced && g_modes.size() > 0) {
        int next = g_modes[irand(0, (int)g_modes.size() - 1)];
        if (g_modes.size() > 1)
            while (next == mode) next = g_modes[irand(0, (int)g_modes.size() - 1)];
        if (next != mode) {
            if (upside && (next == CUBE || next == ROBOT)) {
                portalAtPlayer(lel, p, GRAVITY_NORMAL, 43.f);              // cube and robot have no ceiling: flip back first
                g_modeTimer = 0.5f;
            } else {
                portalAtPlayer(lel, p, PORTAL_ID[next], 43.f);
                g_modeTimer = frand(4.f, 9.f);
            }
        } else g_modeTimer = frand(4.f, 9.f);
    } else if (g_useSpeeds && g_speedTimer <= 0.f && spaced) {
        int tier = chance(0.08f) ? 0 : irand(1, g_t.maxSpeed);            // 0 = half speed, rarely
        portalAtPlayer(lel, p, SPEED_ID[tier], 22.f, true);
        g_speedTimer = frand(5.f, 11.f);
    } else if (g_useMinis && g_miniTimer <= 0.f && spaced) {
        portalAtPlayer(lel, p, p->m_vehicleSize < 0.9f ? ID_MINI_OFF : ID_MINI_ON, 43.f);
        g_miniTimer = frand(8.f, 16.f);
    } else if (g_useGravity && g_t.gravity && bounded && g_gravityTimer <= 0.f && spaced && g_modeTimer > 1.5f) {
        portalAtPlayer(lel, p, upside ? GRAVITY_NORMAL : GRAVITY_FLIPPED, 43.f);   // upside-down flying
        g_gravityTimer = frand(6.f, 12.f);
    }
}

static bool record(PlayerObject* p, float dt) {
    if (!p || p->m_isDead) return false;
    auto pos = p->getPosition();
    if (!g_path.empty() && pos.x <= g_path.back().x + 0.01f) return false;     // paused, or not moving forward
    auto size = p->getObjectRect().size;
    float hw = std::clamp(size.width / 2.f, 5.f, 15.f), hh = std::clamp(size.height / 2.f, 5.f, 15.f);
    float v = speedOf(p);
    g_path.push_back({pos.x, pos.y, v, hw, hh, modeOf(p), (bool)p->m_isOnGround, p->m_vehicleSize < 0.9f});
    return true;
}

// ---- obstacles built behind the player --------------------------------------------------------------
struct Jump { size_t i0, i1; bool fp = false; };     // i0 = last grounded sample, i1 = first grounded sample after the jump

// true if the path went through a jump orb (or over a pad) somewhere between x0 and x1
static bool orbUsed(float x0, float x1) {
    for (auto& o : g_orbs)
        if (o.x > x0 && o.x < x1) return true;
    for (auto& o : g_pads)
        if (o.x > x0 - 45.f && o.x < x1) return true;
    return false;
}

static bool onPad(float x) {
    for (auto& o : g_pads)
        if (std::fabs(o.x - x) < 26.f) return true;
    return false;
}

static void buildJump(LevelEditorLayer* lel, const Jump& j) {
    const Sample& a = g_path[j.i0];
    const Sample& b = g_path[j.i1];
    float gy = a.y - a.hh + 15.f;                     // centre height of an object standing where the player stood
    if (std::fabs(b.y - a.y) > 4.f) return;           // only jumps that land on the same height
    if (j.fp) {
        float w = a.v / g_fps;                                              // distance travelled in one frame
        // half a frame of room on each side = a one-frame window, plus the leniency from the settings on top
        float m = w * 0.5f + g_fpLeniency;
        float first = 0.f, last = 0.f;
        for (float x = a.x + 6.f; x < a.x + 70.f && x < b.x; x += 0.5f)     // as close to the takeoff as the path allows
            if (!onPad(x) && valid(spikeBox(x, gy, false), m)) { first = x; break; }
        for (float x = b.x - 6.f; x > b.x - 70.f && x > a.x; x -= 0.5f)     // as close to the landing as the path allows
            if (valid(spikeBox(x, gy, false), m)) { last = x; break; }
        if (first == 0.f || last == 0.f || last < first) return;
        place(lel, ID_SPIKE, first, gy);
        if (last - first > 12.f) place(lel, ID_SPIKE, last, gy);
        for (float x = first + 30.f; x < last - 20.f; x += 30.f)
            if (valid(spikeBox(x, gy, false), 4.f)) place(lel, ID_SPIKE, x, gy);
        return;
    }
    std::vector<float> xs;
    for (float x = std::floor(a.x / 30.f) * 30.f + 15.f; x < b.x; x += 30.f)
        if (x > a.x && !nearPortal(x) && !onPad(x) && valid(spikeBox(x, gy, false), g_t.jumpMargin)) xs.push_back(x);
    if (xs.empty()) return;
    if (orbUsed(a.x, b.x)) {                          // orb jump: spikes all the way under it, so the orbs are really needed
        for (float x : xs) place(lel, ID_SPIKE, x, gy);
        return;
    }
    size_t mid = xs.size() / 2;
    // robot: its jumps go higher, so it gets a two-block pillar with a spike on top when the jump clears it
    if (a.mode == ROBOT && chance(g_t.blockSpike + 0.2f) && valid(blockBox(xs[mid], gy), 3.f) &&
        valid(blockBox(xs[mid], gy + 30.f), 3.f) && valid(spikeBox(xs[mid], gy + 60.f, false), 3.f)) {
        place(lel, ID_BLOCK, xs[mid], gy);
        place(lel, ID_BLOCK, xs[mid], gy + 30.f);
        place(lel, ID_SPIKE, xs[mid], gy + 60.f);
        return;
    }
    // cube: up to a few spikes under the arc, sometimes a block with a spike on top
    if (chance(g_t.blockSpike) && valid(blockBox(xs[mid], gy), 3.f) && valid(spikeBox(xs[mid], gy + 30.f, false), 3.f)) {
        place(lel, ID_BLOCK, xs[mid], gy);
        place(lel, ID_SPIKE, xs[mid], gy + 30.f);
        return;
    }
    int want = 1;                                     // harder = longer rows of spikes
    for (int k = 1; k < g_t.maxSpikes; ++k) want += chance(g_density) ? 1 : 0;
    size_t from = mid >= (size_t)(want / 2) ? mid - want / 2 : 0;
    for (size_t k = from; k < xs.size() && k < from + want; ++k) place(lel, ID_SPIKE, xs[k], gy);
}

// one 30-unit column at x. What is built depends on the gamemode the player was in at that spot:
//   cube / robot  hanging blocks over the running parts (the jumps themselves are handled in buildJump)
//   ball / spider spikes on the opposite surface            ufo   gates with one opening
//   wave          tight toothed walls                       ship  smooth tunnel, straight-fly tunnel
//   swing         a slightly wider tunnel
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
        if (!grounded) return;
        float feet = ymin - half;
        // running on a raised platform: spikes on the real ground below, so walking underneath is not a way through
        if (feet > GROUND + 70.f && valid(spikeBox(x, GROUND + 15.f, false), 5.f)) place(lel, ID_SPIKE, x, GROUND + 15.f);
        if (!chance(g_density * g_t.hang)) return;
        float by = feet + 15.f + 90.f;                                     // block hanging three blocks above the feet
        if (!valid(blockBox(x, by), 4.f)) return;
        place(lel, ID_BLOCK, x, by);
        if (chance(0.6f) && valid(spikeBox(x, by - 30.f, true), 4.f)) place(lel, ID_SPIKE, x, by - 30.f, true);
        if (valid(spikeBox(x, by + 30.f, false), 4.f)) place(lel, ID_SPIKE, x, by + 30.f);          // nobody rides on top of it
        return;
    }
    // look a little wider around the column to recognise what the player is doing
    float wmin = 1e9f, wmax = -1e9f;
    int turns = 0, dir = 0;
    auto w0 = std::lower_bound(g_path.begin(), g_path.end(), x - 105.f, [](const Sample& s, float v) { return s.x < v; });
    for (auto k = w0; k != g_path.end() && k->x <= x + 60.f; ++k) {
        wmin = std::min(wmin, k->y);
        wmax = std::max(wmax, k->y);
        if (k != w0) {
            float d = k->y - (k - 1)->y;
            int nd = d > 0.3f ? 1 : (d < -0.3f ? -1 : 0);
            if (nd != 0 && dir != 0 && nd != dir) ++turns;
            if (nd != 0) dir = nd;
        }
    }
    bool straight = (mode == SHIP || mode == SWING || mode == UFO) && wmax - wmin < 14.f;      // straight fly
    bool spam = (mode == WAVE || mode == UFO || mode == BALL || mode == SPIDER) && turns >= 3;  // wave / ufo / ball spam

    // ---- ball and spider: they live on the floor and the ceiling, so the danger goes on the surface they are NOT on.
    //      Where the player switched to the ceiling the floor gets spikes (and the other way round): the switch is required.
    if (mode == BALL || mode == SPIDER) {
        if (!grounded || g_surfHi - g_surfLo < 90.f) return;              // both surfaces must be known first
        bool onFloor = ymin - half < g_surfLo + 8.f, onCeil = ymax + half > g_surfHi - 8.f;
        bool switching = wmax - wmin > 60.f;                               // a switch happens close to this column
        float c = g_density * g_t.wallSpike + (switching ? 0.5f : 0.f) + (spam ? 0.5f : 0.f);
        if (!chance(c)) return;
        if (onFloor && valid(spikeBox(x, g_surfHi - 15.f, true), 4.f)) place(lel, ID_SPIKE, x, g_surfHi - 15.f, true);
        else if (onCeil && valid(spikeBox(x, g_surfLo + 15.f, false), 4.f)) place(lel, ID_SPIKE, x, g_surfLo + 15.f);
        return;
    }

    float gap = straight ? g_t.straightGap : g_baseGap + frand(0.f, 12.f);
    float snap = straight ? 3.f : g_t.snap;                                                 // tunnels hug the path exactly
    float spikeChance = spam ? 1.f : g_density * g_t.wallSpike;                             // spam parts get spikes on every wall
    if (mode == UFO && !straight && !spam) {
        // ---- ufo: gates. A wall with one opening every few blocks instead of a continuous tunnel, spikes on the edges.
        int every = g_t.maxSpikes >= 3 ? 3 : (g_t.maxSpikes == 2 ? 4 : 5);
        if (((int)std::lround(x / 30.f)) % every != 0) return;
        gap = g_baseGap * 0.7f + 6.f;
        snap = 7.5f;
        spikeChance = 0.5f + g_t.wallSpike * 0.5f;
    } else if (mode == WAVE) {
        // ---- wave: walls right on the path with teeth on them
        snap = std::min(snap, 7.5f);
        spikeChance = std::max(spikeChance, g_density * (g_t.wallSpike + 0.2f));
    } else if (mode == SWING) {
        // ---- swing: it drifts a lot, so a little more room than the ship gets
        gap += 10.f;
    } else if (mode == SHIP && !straight) {
        // ---- ship: smooth tunnel, spikes mostly where the player was climbing or diving hard
        if (wmax - wmin > 70.f) spikeChance = std::min(1.f, spikeChance + 0.25f);
    }
    float lowest = GROUND + 15.f;
    // Walls are solid all the way down to the ground and up to WALL_TOP, so the only way through is the corridor itself
    // (no flying over or under it). Every single block is still checked against the path.
    float fy = std::floor((ymin - half - gap - GROUND) / snap) * snap + GROUND - 15.f;      // floor
    if (fy >= lowest && valid(blockBox(x, fy), 1.f)) {
        place(lel, ID_BLOCK, x, fy);
        for (float y = fy - 30.f; y >= lowest; y -= 30.f)
            if (valid(blockBox(x, y), 1.f)) place(lel, ID_BLOCK, x, y);
        if (chance(spikeChance) && valid(spikeBox(x, fy + 30.f, false), 2.f)) place(lel, ID_SPIKE, x, fy + 30.f);
    }
    float cy = std::ceil((ymax + half + gap - GROUND) / snap) * snap + GROUND + 15.f;       // ceiling
    if (cy <= WALL_TOP && valid(blockBox(x, cy), 1.f)) {
        place(lel, ID_BLOCK, x, cy);
        for (float y = cy + 30.f; y <= WALL_TOP; y += 30.f)
            if (valid(blockBox(x, y), 1.f)) place(lel, ID_BLOCK, x, y);
        if (chance(spikeChance) && valid(spikeBox(x, cy - 30.f, true), 2.f)) place(lel, ID_SPIKE, x, cy - 30.f, true);
    }
}

// dash orb stopper: a column of spikes right after the spot where the dash ended, so holding forever is not an option
static void buildStoppers(LevelEditorLayer* lel, float playerX) {
    for (size_t i = 0; i < g_stoppers.size();) {
        auto& st = g_stoppers[i];
        if (playerX < st.x + 90.f) { ++i; continue; }          // wait until the path around it is known
        for (float dy = -60.f; dy <= 60.f; dy += 30.f) {
            float y = st.y + dy;
            if (y >= GROUND + 15.f && valid(spikeBox(st.x, y, false), 4.f)) place(lel, ID_SPIKE, st.x, y);
        }
        g_stoppers.erase(g_stoppers.begin() + (long long)i);
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
    if (prev.mode != cur.mode) {                      // new gamemode: forget the surfaces of the last part
        g_surfLo = 1e9f;
        g_surfHi = -1e9f;
    }
    if ((cur.mode == BALL || cur.mode == SPIDER) && cur.ground) {
        g_surfLo = std::min(g_surfLo, cur.y - cur.hh);
        g_surfHi = std::max(g_surfHi, cur.y + cur.hh);
    }
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
    buildStoppers(lel, cur.x);
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
    buildStoppers(lel, 1e9f);
    framePortals(lel);
    auto msg = fmt::format("Auto Layout: {} objects, {} orbs, {} pads, {} frame perfects", g_objects, g_orbs.size(), g_pads.size(), g_fpMade);
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
