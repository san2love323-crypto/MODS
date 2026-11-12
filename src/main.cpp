// Roulette Challenge — Geode mod for Geometry Dash 2.2081 (Windows)
// Spin before a level, play it with a random handicap, reach the goal % to win points.
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace rc {

// =====================================================================================
// The 100 challenges
// =====================================================================================
enum Type {
    // camera (whole play screen)
    W_FLIPX, W_FLIPY, W_ROT, W_ZOOM, W_SHAKE, W_WOBBLE, W_PULSE, W_DRIFT, W_SPIN, W_SQUASH, W_FLIPTIMER, W_ROTSTEP,
    // screen covers
    O_TINT, O_HUE, O_BLINK, O_COVER, O_LETTER, O_TUNNEL, O_SWEEPV, O_SWEEPH, O_STRIPEV, O_STRIPEH, O_FADE, O_BLOCKS, O_CURTAIN,
    // icon
    P_INVIS, P_FLICKER, P_FADE, P_SCALE, P_BLACK, P_RAINBOW, P_NOTRAIL,
    // game speed
    S_SCALE, S_PULSE, S_RANDOM, S_LAG,
    // distractions
    H_NOBAR, X_FLY, X_BIG, X_FAKE, X_COUNT, X_ADVICE,
    // rules (no visuals)
    R_TRIES, R_GOALPLUS, R_FULL, R_NONE,
};
enum Rarity { COMMON, RARE, EPIC, LEGENDARY };
enum Cat { CAT_WORLD, CAT_OVERLAY, CAT_PLAYER, CAT_SPEED, CAT_TEXT, CAT_RULES };

struct Ch { const char* name; const char* desc; Rarity rar; Cat cat; Type type; float a, b; bool flashy; };

const Ch CH[] = {
    // ---------------- camera (30) ----------------
    {"Mirror World", "Everything is mirrored left to right", COMMON, CAT_WORLD, W_FLIPX, 0, 0, false},
    {"Upside Down", "The screen is flipped upside down", RARE, CAT_WORLD, W_FLIPY, 0, 0, false},
    {"Total Flip", "The screen is rotated 180 degrees", RARE, CAT_WORLD, W_ROT, 180, 0, false},
    {"Tilt Left", "The screen leans 10 degrees left", COMMON, CAT_WORLD, W_ROT, -10, 0, false},
    {"Tilt Right", "The screen leans 10 degrees right", COMMON, CAT_WORLD, W_ROT, 10, 0, false},
    {"Steep Tilt", "The screen leans 25 degrees", RARE, CAT_WORLD, W_ROT, 25, 0, false},
    {"Sideways", "The screen is turned 90 degrees", EPIC, CAT_WORLD, W_ROT, 90, 0, false},
    {"Other Sideways", "The screen is turned 90 degrees the other way", EPIC, CAT_WORLD, W_ROT, -90, 0, false},
    {"Diagonal", "The screen is turned 45 degrees", RARE, CAT_WORLD, W_ROT, 45, 0, false},
    {"Zoom In", "The camera is zoomed in", COMMON, CAT_WORLD, W_ZOOM, 1.4f, 0, false},
    {"Super Zoom", "The camera is zoomed in a LOT", RARE, CAT_WORLD, W_ZOOM, 1.9f, 0, false},
    {"Far Away", "The camera is zoomed out", COMMON, CAT_WORLD, W_ZOOM, 0.6f, 0, false},
    {"Ant View", "The whole game is tiny", RARE, CAT_WORLD, W_ZOOM, 0.35f, 0, false},
    {"Earthquake", "The screen shakes", COMMON, CAT_WORLD, W_SHAKE, 4, 0, false},
    {"Big Earthquake", "The screen shakes hard", RARE, CAT_WORLD, W_SHAKE, 12, 0, false},
    {"Seasick", "The screen rocks side to side", COMMON, CAT_WORLD, W_WOBBLE, 8, 2.0f, false},
    {"Storm", "The screen rocks wildly", RARE, CAT_WORLD, W_WOBBLE, 20, 2.6f, false},
    {"Heartbeat", "The screen pulses in and out", COMMON, CAT_WORLD, W_PULSE, 0.08f, 6.0f, false},
    {"Breathing", "The screen slowly grows and shrinks", RARE, CAT_WORLD, W_PULSE, 0.25f, 1.5f, false},
    {"Zoom Yo-Yo", "The zoom swings between tiny and huge", EPIC, CAT_WORLD, W_PULSE, 0.5f, 2.2f, false},
    {"Drunk Camera", "The camera drifts around", COMMON, CAT_WORLD, W_DRIFT, 30, 1.6f, false},
    {"Runaway Camera", "The camera wanders far away", RARE, CAT_WORLD, W_DRIFT, 90, 1.2f, false},
    {"Washing Machine", "The screen keeps spinning slowly", EPIC, CAT_WORLD, W_SPIN, 40, 0, false},
    {"Tornado", "The screen spins fast", LEGENDARY, CAT_WORLD, W_SPIN, 150, 0, true},
    {"Wide Screen", "The game is stretched wide", COMMON, CAT_WORLD, W_SQUASH, 1.5f, 0.7f, false},
    {"Tall Screen", "The game is stretched tall", COMMON, CAT_WORLD, W_SQUASH, 0.6f, 1.3f, false},
    {"Pancake", "The game is squashed flat", RARE, CAT_WORLD, W_SQUASH, 1.0f, 0.4f, false},
    {"Flip Flop", "The screen mirrors itself every 4 seconds", RARE, CAT_WORLD, W_FLIPTIMER, 4, 0, false},
    {"Fast Flip Flop", "The screen mirrors itself every 1.5 seconds", EPIC, CAT_WORLD, W_FLIPTIMER, 1.5f, 0, true},
    {"Clockwork", "The screen turns 90 degrees every 5 seconds", EPIC, CAT_WORLD, W_ROTSTEP, 5, 0, false},
    // ---------------- screen covers (30) ----------------
    {"Sunglasses", "The screen is a bit dark", COMMON, CAT_OVERLAY, O_TINT, 0, 110, false},
    {"Midnight", "The screen is very dark", RARE, CAT_OVERLAY, O_TINT, 0, 190, false},
    {"Blackout", "You can barely see anything", EPIC, CAT_OVERLAY, O_TINT, 0, 235, false},
    {"Red Alert", "Everything is red", COMMON, CAT_OVERLAY, O_TINT, 1, 100, false},
    {"Matrix", "Everything is green", COMMON, CAT_OVERLAY, O_TINT, 2, 100, false},
    {"Deep Sea", "Everything is blue", COMMON, CAT_OVERLAY, O_TINT, 3, 110, false},
    {"Bubblegum", "Everything is pink", COMMON, CAT_OVERLAY, O_TINT, 4, 100, false},
    {"Disco", "The screen slowly changes color", COMMON, CAT_OVERLAY, O_HUE, 0, 0, false},
    {"Blink", "The screen goes black for a moment every 2 seconds", COMMON, CAT_OVERLAY, O_BLINK, 2.0f, 0.25f, false},
    {"Sleepy Eyes", "The screen is black more than half the time", RARE, CAT_OVERLAY, O_BLINK, 2.0f, 0.6f, false},
    {"Broken Lamp", "The lights flicker quickly", EPIC, CAT_OVERLAY, O_BLINK, 0.25f, 0.5f, true},
    {"No Sky", "The top half of the screen is covered", COMMON, CAT_OVERLAY, O_COVER, 0, 0, false},
    {"No Floor", "The bottom of the screen is covered", RARE, CAT_OVERLAY, O_COVER, 1, 0, false},
    {"Rear View", "The left side of the screen is covered", COMMON, CAT_OVERLAY, O_COVER, 2, 0, false},
    {"No Future", "You cannot see what is coming", EPIC, CAT_OVERLAY, O_COVER, 3, 0, false},
    {"Censor Bar", "A black bar across the middle", COMMON, CAT_OVERLAY, O_COVER, 4, 0, false},
    {"Pillar", "A black pillar in the middle", RARE, CAT_OVERLAY, O_COVER, 5, 0, false},
    {"Cinema", "Movie-style black bars", COMMON, CAT_OVERLAY, O_LETTER, 0.2f, 0, false},
    {"Mail Slot", "You look through a thin slot", EPIC, CAT_OVERLAY, O_LETTER, 0.36f, 0, false},
    {"Tunnel Vision", "You only see a window in the middle", RARE, CAT_OVERLAY, O_TUNNEL, 0.6f, 0, false},
    {"Keyhole", "You only see a tiny window", LEGENDARY, CAT_OVERLAY, O_TUNNEL, 0.35f, 0, false},
    {"Scanner", "A bar slides across the screen", COMMON, CAT_OVERLAY, O_SWEEPV, 3.0f, 0, false},
    {"Fast Scanner", "A bar races across the screen", RARE, CAT_OVERLAY, O_SWEEPV, 1.0f, 0, false},
    {"Elevator", "A bar slides up the screen", COMMON, CAT_OVERLAY, O_SWEEPH, 3.0f, 0, false},
    {"Prison Bars", "Vertical bars over the screen", RARE, CAT_OVERLAY, O_STRIPEV, 8, 0, false},
    {"Window Blinds", "Horizontal bars over the screen", RARE, CAT_OVERLAY, O_STRIPEH, 6, 0, false},
    {"Fading", "The screen slowly fades to black and back", RARE, CAT_OVERLAY, O_FADE, 4.0f, 0, false},
    {"Sticker Bomb", "Colored squares cover random spots", COMMON, CAT_OVERLAY, O_BLOCKS, 6, 2.0f, false},
    {"Sticker Storm", "Lots of squares, moving often", EPIC, CAT_OVERLAY, O_BLOCKS, 16, 1.0f, false},
    {"Closing Curtain", "A curtain closes as you get further", EPIC, CAT_OVERLAY, O_CURTAIN, 0, 0, false},
    // ---------------- icon (12) ----------------
    {"Invisible Icon", "You cannot see your icon", EPIC, CAT_PLAYER, P_INVIS, 0, 0, false},
    {"Flickering Icon", "Your icon blinks in and out", RARE, CAT_PLAYER, P_FLICKER, 0.3f, 0, false},
    {"Slow Blink Icon", "Your icon disappears every other second", COMMON, CAT_PLAYER, P_FLICKER, 1.0f, 0, false},
    {"Ghost Icon", "Your icon is almost see-through", COMMON, CAT_PLAYER, P_FADE, 60, 0, false},
    {"Giant Icon", "Your icon looks huge (hitbox is normal)", COMMON, CAT_PLAYER, P_SCALE, 2.2f, 2.2f, false},
    {"Mega Icon", "Your icon looks enormous (hitbox is normal)", RARE, CAT_PLAYER, P_SCALE, 4.0f, 4.0f, false},
    {"Tiny Icon", "Your icon looks small (hitbox is normal)", COMMON, CAT_PLAYER, P_SCALE, 0.4f, 0.4f, false},
    {"Dot Icon", "Your icon is a dot (hitbox is normal)", RARE, CAT_PLAYER, P_SCALE, 0.15f, 0.15f, false},
    {"Stretchy Icon", "Your icon is stretched wide", COMMON, CAT_PLAYER, P_SCALE, 3.0f, 0.5f, false},
    {"Shadow Icon", "Your icon is pure black", COMMON, CAT_PLAYER, P_BLACK, 0, 0, false},
    {"Rainbow Icon", "Your icon keeps changing color", COMMON, CAT_PLAYER, P_RAINBOW, 0, 0, false},
    {"No Trail", "Your trail is gone", COMMON, CAT_PLAYER, P_NOTRAIL, 0, 0, false},
    // ---------------- game speed (10) ----------------
    {"A Bit Slow", "The game runs at 0.9x", COMMON, CAT_SPEED, S_SCALE, 0.9f, 0, false},
    {"Slow Motion", "The game runs at 0.7x", COMMON, CAT_SPEED, S_SCALE, 0.7f, 0, false},
    {"Molasses", "The game runs at 0.5x", RARE, CAT_SPEED, S_SCALE, 0.5f, 0, false},
    {"A Bit Fast", "The game runs at 1.1x", COMMON, CAT_SPEED, S_SCALE, 1.1f, 0, false},
    {"Fast Forward", "The game runs at 1.25x", RARE, CAT_SPEED, S_SCALE, 1.25f, 0, false},
    {"Turbo", "The game runs at 1.5x", EPIC, CAT_SPEED, S_SCALE, 1.5f, 0, false},
    {"Hyper", "The game runs at 2x", LEGENDARY, CAT_SPEED, S_SCALE, 2.0f, 0, false},
    {"Speed Waves", "The speed keeps rising and falling", RARE, CAT_SPEED, S_PULSE, 0.25f, 3.0f, false},
    {"Speed Roulette", "A new random speed every 3 seconds", EPIC, CAT_SPEED, S_RANDOM, 3.0f, 0, false},
    {"Lag Spikes", "The game stutters every few seconds", RARE, CAT_SPEED, S_LAG, 2.0f, 0, false},
    // ---------------- distractions (10) ----------------
    {"No Progress Bar", "The progress bar and % are hidden", COMMON, CAT_TEXT, H_NOBAR, 0, 0, false},
    {"Backseat: JUMP", "Text keeps yelling at you to jump", COMMON, CAT_TEXT, X_FLY, 0, 5, false},
    {"Backseat: WAIT", "Text keeps telling you not to jump", COMMON, CAT_TEXT, X_FLY, 1, 5, false},
    {"LOL Storm", "The screen fills with laughing", RARE, CAT_TEXT, X_FLY, 2, 14, false},
    {"Fan Club", "Compliments fly across the screen", COMMON, CAT_TEXT, X_FLY, 3, 6, false},
    {"Number Rain", "Random numbers fall down the screen", RARE, CAT_TEXT, X_FLY, 4, 14, false},
    {"Big Question", "A giant question mark sits on the screen", COMMON, CAT_TEXT, X_BIG, 0, 0, false},
    {"Fake Warnings", "Fake warning boxes keep popping up", RARE, CAT_TEXT, X_FAKE, 0, 0, false},
    {"Bad Countdown", "A countdown tells you to jump at the wrong time", COMMON, CAT_TEXT, X_COUNT, 0, 0, false},
    {"Bad Advice", "Unhelpful tips at the bottom of the screen", COMMON, CAT_TEXT, X_ADVICE, 0, 0, false},
    // ---------------- rules (8) ----------------
    {"One Shot", "You get exactly 1 try", LEGENDARY, CAT_RULES, R_TRIES, 1, 0, false},
    {"Three Lives", "You get 3 tries", EPIC, CAT_RULES, R_TRIES, 3, 0, false},
    {"Five Lives", "You get 5 tries", RARE, CAT_RULES, R_TRIES, 5, 0, false},
    {"Ten Lives", "You get 10 tries", COMMON, CAT_RULES, R_TRIES, 10, 0, false},
    {"Overachiever", "The goal is 15% further", RARE, CAT_RULES, R_GOALPLUS, 15, 0, false},
    {"Big Dreams", "The goal is 30% further", EPIC, CAT_RULES, R_GOALPLUS, 30, 0, false},
    {"All or Nothing", "You must beat the whole level", LEGENDARY, CAT_RULES, R_FULL, 0, 0, false},
    {"Lucky Day", "No handicap at all. Free points!", COMMON, CAT_RULES, R_NONE, 0, 0, false},
};
constexpr int N = (int)(sizeof(CH) / sizeof(CH[0]));

const char* RARITY_NAME[] = {"COMMON", "RARE", "EPIC", "LEGENDARY"};
const ccColor3B RARITY_COLOR[] = {{255, 255, 255}, {80, 180, 255}, {200, 100, 255}, {255, 190, 0}};
const int RARITY_REWARD[] = {20, 40, 80, 200};
const char* CAT_SETTING[] = {"cat-world", "cat-overlay", "cat-player", "cat-speed", "cat-text", "cat-rules"};

std::mt19937& rng() { static std::mt19937 r{std::random_device{}()}; return r; }
float frand(float lo, float hi) { return lo + (hi - lo) * (float)(rng()() % 10001) / 10000.f; }
int irand(int n) { return n <= 0 ? 0 : (int)(rng()() % (unsigned)n); }

// =====================================================================================
// Saved data
// =====================================================================================
template <class T> T sv(const char* k, T def) { return Mod::get()->getSavedValue<T>(k, def); }
template <class T> void setSv(const char* k, T v) { Mod::get()->setSavedValue<T>(k, v); }

// points are shared with Ghost & Bet when that mod is installed
int points() {
    if (auto m = Loader::get()->getLoadedMod("matjang.ghostbet")) return m->getSavedValue<int>("points", 100);
    return sv<int>("points", 100);
}
void addPoints(int n) {
    int p = (std::max)(0, points() + n);
    if (auto m = Loader::get()->getLoadedMod("matjang.ghostbet")) m->setSavedValue<int>("points", p);
    else setSv<int>("points", p);
    if (n > 0) setSv<int>("earned", sv<int>("earned", 0) + n);
}

// collection: one char per challenge, '0' never rolled, '1' rolled, '2' cleared
std::string book() {
    auto s = sv<std::string>("book", "");
    s.resize(N, '0');
    return s;
}
void markBook(int id, char state) {
    auto s = book();
    if (id >= 0 && id < N && s[id] < state) { s[id] = state; setSv<std::string>("book", s); }
}

std::string today() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    return fmt::format("{:04}{:02}{:02}", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
}

bool allowed(int id) {
    auto const& c = CH[id];
    if (c.flashy && Mod::get()->getSettingValue<bool>("safe-mode")) return false;
    return Mod::get()->getSettingValue<bool>(CAT_SETTING[c.cat]);
}

// weighted by rarity; never returns `exclude`
int pick(int exclude = -1) {
    bool spicy = Mod::get()->getSettingValue<bool>("spicy");
    const int wNormal[4] = {55, 28, 13, 4}, wSpicy[4] = {35, 30, 25, 10};
    const int* w = spicy ? wSpicy : wNormal;
    int roll = irand(100), rar = 0;
    for (int acc = 0; rar < 3; rar++) { acc += w[rar]; if (roll < acc) break; }
    std::vector<int> pool, any;
    for (int i = 0; i < N; i++) {
        if (i == exclude || !allowed(i)) continue;
        any.push_back(i);
        if (CH[i].rar == rar) pool.push_back(i);
    }
    if (!pool.empty()) return pool[irand((int)pool.size())];
    if (!any.empty()) return any[irand((int)any.size())];
    return N - 1;        // everything disabled -> Lucky Day
}

int dailyId() {
    auto h = std::hash<std::string>{}(today());
    for (int k = 0; k < N; k++) {
        int id = (int)((h + k) % N);
        if (!(CH[id].flashy && Mod::get()->getSettingValue<bool>("safe-mode"))) return id;
    }
    return 0;
}

std::string levelKey(GJGameLevel* level) {
    int id = level->m_levelID.value();
    if (id > 0) return std::to_string(id);
    return fmt::format("local_{}", std::hash<std::string>{}(std::string(level->m_levelName)));
}

// =====================================================================================
// The accepted challenge
// =====================================================================================
struct Active {
    bool on = false;
    std::string key;
    std::vector<int> ids;
    int goal = 100, triesLeft = -1, tries = -1, reward = 0;
    bool daily = false;
};
Active g_act;

// goal: half of your best + 10 (so it is always reachable), 100 if you already beat the level
Active makeChallenge(GJGameLevel* level, std::vector<int> ids, bool daily) {
    Active a; a.on = true; a.key = levelKey(level); a.ids = std::move(ids); a.daily = daily;
    int best = level->m_normalPercent.value();
    int goal = best >= 100 ? 100 : std::clamp(best / 2 + 10, 10, 90);
    int base = 0;
    for (int id : a.ids) {
        auto const& c = CH[id];
        base += RARITY_REWARD[c.rar];
        if (c.type == R_TRIES) { int t = (int)c.a; a.triesLeft = a.triesLeft < 0 ? t : (std::min)(a.triesLeft, t); }
        if (c.type == R_GOALPLUS) goal += (int)c.a;
        if (c.type == R_FULL) goal = 100;
    }
    a.tries = a.triesLeft;
    a.goal = std::clamp(goal, 5, 100);
    float mult = (a.ids.size() > 1 ? 1.5f : 1.f) * (1.f + 0.1f * (std::min)(sv<int>("streak", 0), 10)) * (daily ? 3.f : 1.f);
    a.reward = (int)std::lround(base * mult);
    return a;
}

// =====================================================================================
// Effects while playing
// =====================================================================================
struct Fx { int id; std::vector<CCNode*> nodes; float timer = 0, v = 1, w = 0; int step = -1; };
struct Run {
    PlayLayer* owner = nullptr;
    std::string key;
    float t = 0, chaos = 0;
    bool attemptCounted = false;
    CCNode* root = nullptr;
    CCLabelBMFont* hud = nullptr;
    std::vector<Fx> fx;
    bool barVisible = true, pctVisible = true, hadBar = false;
};
Run g_run;

bool live(PlayLayer* pl) { return g_act.on && g_run.owner == pl && g_act.key == g_run.key; }
bool counts(PlayLayer* pl) { return !pl->m_isPracticeMode && !pl->m_isTestMode; }

ccColor3B hue(float h) {
    float x = std::fmod(std::fabs(h), 1.f) * 6.f; int i = (int)x; float f = x - i; float r = 0, g = 0, b = 0;
    switch (i % 6) {
        case 0: r = 1; g = f; break;   case 1: r = 1 - f; g = 1; break;  case 2: g = 1; b = f; break;
        case 3: g = 1 - f; b = 1; break; case 4: r = f; b = 1; break;    default: r = 1; b = 1 - f; break;
    }
    return {(GLubyte)(r * 255), (GLubyte)(g * 255), (GLubyte)(b * 255)};
}

void banner(PlayLayer* pl, std::string const& text, ccColor3B color) {
    auto win = CCDirector::sharedDirector()->getWinSize();
    auto lbl = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    lbl->setColor(color);
    lbl->setPosition(CCPoint(win.width / 2, win.height - 84));
    lbl->setScale(0.1f);
    pl->addChild(lbl, 1001);
    lbl->runAction(CCSequence::create(CCEaseBackOut::create(CCScaleTo::create(0.25f, 0.55f)), CCDelayTime::create(1.8f),
                                      CCFadeOut::create(0.4f), CCRemoveSelf::create(), nullptr));
}

void updateHud() {
    if (!g_run.hud) return;
    bool show = g_run.owner && live(g_run.owner) && Mod::get()->getSettingValue<bool>("hud");
    g_run.hud->setVisible(show);
    if (!show) return;
    std::string names;
    for (int id : g_act.ids) { if (!names.empty()) names += " + "; names += CH[id].name; }
    std::string tries = g_act.triesLeft >= 0 ? fmt::format("   TRIES {}", g_act.triesLeft) : "";
    g_run.hud->setString(fmt::format("{}   GOAL {}%{}   +{}", names, g_act.goal, tries, g_act.reward).c_str());
}

CCLayerColor* bar(ccColor3B c, GLubyte alpha, float x, float y, float w, float h) {
    auto l = CCLayerColor::create(ccc4(c.r, c.g, c.b, alpha), w, h);
    l->setPosition(CCPoint(x, y));
    g_run.root->addChild(l);
    return l;
}

const std::vector<std::string> FLY_TEXT[5] = {
    {"JUMP!", "NOW!", "JUMP JUMP", "CLICK!"}, {"DON'T JUMP", "WAIT", "NOT YET", "HOLD ON"},
    {"LOL", "XD", "LMAO", "HAHA", "OOF"}, {"NICE!", "GG", "WOW", "PRO", "SO GOOD"}, {}};
const std::vector<std::string> FAKE_TEXT = {"WARNING: Low battery", "Geometry Dash is not responding", "Your mouse has been disconnected",
    "Update available. Restart now?", "Achievement unlocked: Nothing", "Saving... do not turn off"};
const std::vector<std::string> ADVICE_TEXT = {"Tip: try not dying", "Tip: the spikes are bad", "Tip: jump earlier. or later.", "Tip: have you tried being better?",
    "Tip: blink less", "Tip: this part is easy", "Tip: don't look at this text"};

std::string flyText(int set) {
    if (set == 4) return std::to_string(irand(1000));
    auto const& v = FLY_TEXT[set];
    return v[irand((int)v.size())];
}

// build the nodes one challenge needs
void buildFx(PlayLayer* pl, int id) {
    auto win = CCDirector::sharedDirector()->getWinSize();
    float W = win.width, H = win.height;
    auto const& c = CH[id];
    Fx f; f.id = id;
    const ccColor3B tintCol[5] = {{0, 0, 0}, {255, 0, 0}, {0, 255, 60}, {0, 60, 255}, {255, 80, 200}};
    const ccColor3B black = {0, 0, 0};
    switch (c.type) {
        case O_TINT: f.nodes.push_back(bar(tintCol[(int)c.a % 5], (GLubyte)c.b, 0, 0, W, H)); break;
        case O_HUE: f.nodes.push_back(bar({255, 0, 0}, 80, 0, 0, W, H)); break;
        case O_BLINK: case O_FADE: f.nodes.push_back(bar(black, 255, 0, 0, W, H)); break;
        case O_COVER:
            switch ((int)c.a) {
                case 0: f.nodes.push_back(bar(black, 255, 0, H * 0.5f, W, H * 0.5f)); break;
                case 1: f.nodes.push_back(bar(black, 255, 0, 0, W, H * 0.3f)); break;
                case 2: f.nodes.push_back(bar(black, 255, 0, 0, W * 0.3f, H)); break;
                case 3: f.nodes.push_back(bar(black, 255, W * 0.55f, 0, W * 0.45f, H)); break;
                case 4: f.nodes.push_back(bar(black, 255, 0, H * 0.4f, W, H * 0.2f)); break;
                default: f.nodes.push_back(bar(black, 255, W * 0.42f, 0, W * 0.16f, H)); break;
            }
            break;
        case O_LETTER:
            f.nodes.push_back(bar(black, 255, 0, 0, W, H * c.a));
            f.nodes.push_back(bar(black, 255, 0, H * (1 - c.a), W, H * c.a));
            break;
        case O_TUNNEL: {
            float ww = W * c.a, hh = H * c.a, x0 = (W - ww) / 2, y0 = (H - hh) / 2;
            f.nodes.push_back(bar(black, 255, 0, 0, x0, H));
            f.nodes.push_back(bar(black, 255, x0 + ww, 0, x0, H));
            f.nodes.push_back(bar(black, 255, x0, 0, ww, y0));
            f.nodes.push_back(bar(black, 255, x0, y0 + hh, ww, y0));
            break;
        }
        case O_SWEEPV: f.nodes.push_back(bar(black, 255, -W, 0, W * 0.18f, H)); break;
        case O_SWEEPH: f.nodes.push_back(bar(black, 255, 0, -H, W, H * 0.2f)); break;
        case O_STRIPEV: { int n = (int)c.a; for (int i = 0; i < n; i++) f.nodes.push_back(bar(black, 255, W / n * i, 0, W / n / 2, H)); break; }
        case O_STRIPEH: { int n = (int)c.a; for (int i = 0; i < n; i++) f.nodes.push_back(bar(black, 255, 0, H / n * i, W, H / n / 2)); break; }
        case O_BLOCKS: for (int i = 0; i < (int)c.a; i++) f.nodes.push_back(bar(hue(frand(0, 1)), 255, frand(0, W - 60), frand(0, H - 60), 60, 60)); break;
        case O_CURTAIN: f.nodes.push_back(bar({120, 0, 20}, 255, W, 0, W, H)); break;
        case X_FLY:
            for (int i = 0; i < (int)c.b; i++) {
                auto l = CCLabelBMFont::create(flyText((int)c.a).c_str(), "bigFont.fnt");
                l->setScale(frand(0.4f, 0.8f)); l->setColor(hue(frand(0, 1))); l->setOpacity(210);
                l->setPosition(CCPoint(frand(0, W), frand(20, H - 20)));
                l->setTag((int)frand(60, 160));              // speed
                g_run.root->addChild(l); f.nodes.push_back(l);
            }
            break;
        case X_BIG: {
            auto l = CCLabelBMFont::create("?", "bigFont.fnt");
            l->setScale(7.f); l->setOpacity(110); l->setPosition(CCPoint(W / 2, H / 2));
            g_run.root->addChild(l); f.nodes.push_back(l);
            break;
        }
        case X_FAKE: {
            auto box = cocos2d::extension::CCScale9Sprite::create("GJ_square01.png", CCRect(0, 0, 80, 80));
            box->setContentSize(CCSize(300, 70)); box->setPosition(CCPoint(W / 2, H / 2)); box->setVisible(false);
            auto l = CCLabelBMFont::create("", "chatFont.fnt");
            l->setPosition(CCPoint(150, 35)); box->addChild(l);
            g_run.root->addChild(box); f.nodes.push_back(box); f.nodes.push_back(l);
            f.timer = 2.f;
            break;
        }
        case X_COUNT: {
            auto l = CCLabelBMFont::create("", "goldFont.fnt");
            l->setScale(1.4f); l->setPosition(CCPoint(W / 2, H * 0.72f));
            g_run.root->addChild(l); f.nodes.push_back(l);
            f.timer = frand(2, 4);
            break;
        }
        case X_ADVICE: {
            auto l = CCLabelBMFont::create(ADVICE_TEXT[0].c_str(), "bigFont.fnt");
            l->setScale(0.45f); l->setColor({255, 255, 120}); l->setPosition(CCPoint(W / 2, 22));
            g_run.root->addChild(l); f.nodes.push_back(l);
            break;
        }
        case H_NOBAR:
            if (!g_run.hadBar) {
                g_run.hadBar = true;
                g_run.barVisible = pl->m_progressBar ? pl->m_progressBar->isVisible() : true;
                g_run.pctVisible = pl->m_percentageLabel ? pl->m_percentageLabel->isVisible() : true;
            }
            break;
        default: break;
    }
    g_run.fx.push_back(std::move(f));
}

void restorePlayers(PlayLayer* pl) {
    auto gm = GameManager::get();
    auto c1 = gm->colorForIdx(gm->getPlayerColor()), c2 = gm->colorForIdx(gm->getPlayerColor2());
    int k = 0;
    for (auto p : {pl->m_player1, pl->m_player2}) {
        if (p) {
            p->setOpacity(255);
            if (p->m_mainLayer) { p->m_mainLayer->setScaleX(1.f); p->m_mainLayer->setScaleY(1.f); }
            p->setColor(k == 0 ? c1 : c2); p->setSecondColor(k == 0 ? c2 : c1);
            if (p->m_regularTrail) p->m_regularTrail->setOpacity(255);
        }
        k++;
    }
}

// put everything back the way the game had it (also used while paused)
void neutral(PlayLayer* pl) {
    pl->setRotation(0); pl->setScaleX(1); pl->setScaleY(1); pl->setPosition(CCPoint(0, 0));
    CCDirector::sharedDirector()->getScheduler()->setTimeScale(1.f);
    if (g_run.root) g_run.root->setVisible(false);
}

void clearFx(PlayLayer* pl) {
    neutral(pl);
    if (g_run.root) g_run.root->removeAllChildren();
    bool usedPlayer = false;
    for (auto const& f : g_run.fx) if (CH[f.id].cat == CAT_PLAYER) usedPlayer = true;
    if (usedPlayer) restorePlayers(pl);
    if (g_run.hadBar) {
        if (pl->m_progressBar) pl->m_progressBar->setVisible(g_run.barVisible);
        if (pl->m_percentageLabel) pl->m_percentageLabel->setVisible(g_run.pctVisible);
        g_run.hadBar = false;
    }
    g_run.fx.clear();
}

void buildAll(PlayLayer* pl) {
    clearFx(pl);
    for (int id : g_act.ids) buildFx(pl, id);
}

void finish(PlayLayer* pl, bool won) {
    if (!g_act.on) return;
    if (won) {
        addPoints(g_act.reward);
        int streak = sv<int>("streak", 0) + 1;
        setSv<int>("streak", streak);
        if (streak > sv<int>("best-streak", 0)) setSv<int>("best-streak", streak);
        setSv<int>("wins", sv<int>("wins", 0) + 1);
        for (int id : g_act.ids) markBook(id, '2');
        if (g_act.daily) setSv<std::string>("daily-done", today());
        if (pl) banner(pl, fmt::format("CHALLENGE CLEAR!  +{}  (streak {})", g_act.reward, streak), {90, 255, 90});
    } else {
        setSv<int>("streak", 0);
        if (pl) banner(pl, "CHALLENGE FAILED", {255, 80, 80});
        else Notification::create("Roulette challenge failed (streak reset)", NotificationIcon::Error)->show();
    }
    g_act.on = false;
    if (pl) clearFx(pl);
    updateHud();
}

// runs every frame while a challenge is live
void tick(PlayLayer* pl, float dt) {
    auto win = CCDirector::sharedDirector()->getWinSize();
    float W = win.width, H = win.height;
    g_run.t += dt;
    float T = g_run.t;

    // chaos mode: swap the first handicap for a new one every N seconds
    int chaos = (int)Mod::get()->getSettingValue<int64_t>("chaos-seconds");
    if (chaos > 0 && !g_act.ids.empty()) {
        g_run.chaos += dt;
        if (g_run.chaos >= chaos) {
            g_run.chaos = 0;
            for (int tries = 0; tries < 20; tries++) {
                int id = pick(g_act.ids[0]);
                if (CH[id].cat != CAT_RULES) { g_act.ids[0] = id; markBook(id, '1'); break; }
            }
            buildAll(pl);
            banner(pl, fmt::format("CHAOS: {}", CH[g_act.ids[0]].name), RARITY_COLOR[CH[g_act.ids[0]].rar]);
            updateHud();
        }
    }

    if (g_run.root) g_run.root->setVisible(true);
    float rot = 0, sx = 1, sy = 1, ox = 0, oy = 0, ts = 1;
    PlayerObject* players[2] = {pl->m_player1, pl->m_player2};

    for (auto& f : g_run.fx) {
        auto const& c = CH[f.id];
        f.timer -= dt;
        switch (c.type) {
            case W_FLIPX: sx = -sx; break;
            case W_FLIPY: sy = -sy; break;
            case W_ROT: rot += c.a; break;
            case W_ZOOM: sx *= c.a; sy *= c.a; break;
            case W_SHAKE: ox += frand(-c.a, c.a); oy += frand(-c.a, c.a); break;
            case W_WOBBLE: rot += c.a * std::sin(T * c.b); break;
            case W_PULSE: { float z = 1.f + c.a * std::sin(T * c.b); sx *= z; sy *= z; break; }
            case W_DRIFT: ox += c.a * std::sin(T * c.b); oy += c.a * 0.6f * std::cos(T * c.b * 1.3f); break;
            case W_SPIN: rot += c.a * T; break;
            case W_SQUASH: sx *= c.a; sy *= c.b; break;
            case W_FLIPTIMER: if (((int)(T / c.a)) % 2 == 1) sx = -sx; break;
            case W_ROTSTEP: rot += 90.f * (int)(T / c.a); break;

            case O_HUE: static_cast<CCLayerColor*>(f.nodes[0])->setColor(hue(T * 0.3f)); break;
            case O_BLINK: f.nodes[0]->setVisible(std::fmod(T, c.a) < c.a * c.b); break;
            case O_FADE: static_cast<CCLayerColor*>(f.nodes[0])->setOpacity((GLubyte)(115 + 115 * std::sin(T * 6.2832f / c.a))); break;
            case O_SWEEPV: { float bw = W * 0.18f; f.nodes[0]->setPositionX(std::fmod(T, c.a) / c.a * (W + bw) - bw); break; }
            case O_SWEEPH: { float bh = H * 0.2f; f.nodes[0]->setPositionY(std::fmod(T, c.a) / c.a * (H + bh) - bh); break; }
            case O_BLOCKS:
                if (f.timer <= 0) { f.timer = c.b; for (auto n : f.nodes) n->setPosition(CCPoint(frand(0, W - 60), frand(0, H - 60))); }
                break;
            case O_CURTAIN: f.nodes[0]->setPositionX(W - W * 0.7f * std::clamp(pl->getCurrentPercentInt() / 100.f, 0.f, 1.f)); break;

            case P_INVIS: for (auto p : players) if (p) p->setOpacity(0); break;
            case P_FLICKER: { bool on = std::fmod(T, c.a * 2) < c.a; for (auto p : players) if (p) p->setOpacity(on ? 255 : 0); break; }
            case P_FADE: for (auto p : players) if (p) p->setOpacity((GLubyte)c.a); break;
            case P_SCALE: for (auto p : players) if (p && p->m_mainLayer) { p->m_mainLayer->setScaleX(c.a); p->m_mainLayer->setScaleY(c.b); } break;
            case P_BLACK: for (auto p : players) if (p) { p->setColor({0, 0, 0}); p->setSecondColor({25, 25, 25}); } break;
            case P_RAINBOW: for (auto p : players) if (p) { p->setColor(hue(T * 0.8f)); p->setSecondColor(hue(T * 0.8f + 0.5f)); } break;
            case P_NOTRAIL: for (auto p : players) if (p && p->m_regularTrail) p->m_regularTrail->setOpacity(0); break;

            case S_SCALE: ts *= c.a; break;
            case S_PULSE: ts *= 1.f + c.a * std::sin(T * 6.2832f / c.b); break;
            case S_RANDOM: if (f.timer <= 0) { f.timer = c.a; f.v = frand(0.7f, 1.4f); } ts *= f.v; break;
            case S_LAG:
                if (f.timer <= 0) { f.timer = frand(c.a * 0.6f, c.a * 1.4f); f.w = 0.15f; }
                if (f.w > 0) { f.w -= dt / 0.3f; ts *= 0.3f; }
                break;

            case H_NOBAR:
                if (pl->m_progressBar) pl->m_progressBar->setVisible(false);
                if (pl->m_percentageLabel) pl->m_percentageLabel->setVisible(false);
                break;
            case X_FLY:
                for (auto n : f.nodes) {
                    auto l = static_cast<CCLabelBMFont*>(n);
                    if ((int)c.a == 4) {           // numbers fall
                        l->setPositionY(l->getPositionY() - l->getTag() * dt);
                        if (l->getPositionY() < -20) { l->setPosition(CCPoint(frand(0, W), H + 20)); l->setString(flyText(4).c_str()); }
                    } else {
                        l->setPositionX(l->getPositionX() - l->getTag() * dt);
                        if (l->getPositionX() < -120) { l->setPosition(CCPoint(W + 120, frand(20, H - 20))); l->setString(flyText((int)c.a).c_str()); }
                    }
                }
                break;
            case X_FAKE:
                if (f.timer <= 0) {
                    bool show = !f.nodes[0]->isVisible();
                    f.nodes[0]->setVisible(show);
                    f.timer = show ? 1.6f : frand(2.5f, 5.f);
                    if (show) {
                        static_cast<CCLabelBMFont*>(f.nodes[1])->setString(FAKE_TEXT[irand((int)FAKE_TEXT.size())].c_str());
                        f.nodes[0]->setPosition(CCPoint(frand(W * 0.35f, W * 0.65f), frand(H * 0.4f, H * 0.65f)));
                    }
                }
                break;
            case X_COUNT:
                if (f.timer <= 0) {
                    static const char* seq[5] = {"3", "2", "1", "JUMP!", ""};
                    f.step = (f.step + 1) % 5;
                    static_cast<CCLabelBMFont*>(f.nodes[0])->setString(seq[f.step]);
                    f.timer = f.step == 4 ? frand(2.f, 5.f) : 0.5f;
                }
                break;
            case X_ADVICE:
                if (f.timer <= 0) { f.timer = 3.f; static_cast<CCLabelBMFont*>(f.nodes[0])->setString(ADVICE_TEXT[irand((int)ADVICE_TEXT.size())].c_str()); }
                break;
            default: break;
        }
    }

    pl->setRotation(rot);
    pl->setScaleX(sx);
    pl->setScaleY(sy);
    pl->setPosition(CCPoint(ox, oy));
    CCDirector::sharedDirector()->getScheduler()->setTimeScale(std::clamp(ts, 0.2f, 3.f));
}

} // namespace rc

// =====================================================================================
// GD-style popup base
// =====================================================================================
class RCPopup : public FLAlertLayer {
protected:
    float m_w = 0, m_h = 0;
    CCPoint m_center{};

    bool initPopup(float w, float h, const char* title) {
        if (!this->initWithColor({0, 0, 0, 130})) return false;
        auto win = CCDirector::sharedDirector()->getWinSize();
        m_w = w; m_h = h; m_center = CCPoint(win.width / 2, win.height / 2);
        m_mainLayer = CCLayer::create();
        this->addChild(m_mainLayer);
        auto bg = cocos2d::extension::CCScale9Sprite::create("GJ_square01.png", CCRect(0, 0, 80, 80));
        bg->setContentSize(CCSize(w, h));
        bg->setPosition(m_center);
        m_mainLayer->addChild(bg);
        m_buttonMenu = CCMenu::create();
        m_buttonMenu->setZOrder(10);
        m_mainLayer->addChild(m_buttonMenu);
        auto t = CCLabelBMFont::create(title, "goldFont.fnt");
        t->setScale(0.8f);
        t->setPosition(CCPoint(m_center.x, m_center.y + h / 2 - 20));
        m_mainLayer->addChild(t, 5);
        if (auto spr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png")) {
            spr->setScale(0.8f);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(RCPopup::onClose));
            btn->setPosition(CCPoint(-w / 2 + 6, h / 2 - 6));
            m_buttonMenu->addChild(btn);
        }
        this->setKeypadEnabled(true);
        this->setTouchEnabled(true);
        return true;
    }
    CCLabelBMFont* label(const char* text, const char* font, float scale, float x, float y) {
        auto l = CCLabelBMFont::create(text, font);
        l->setScale(scale);
        l->setPosition(CCPoint(m_center.x + x, m_center.y + y));
        m_mainLayer->addChild(l, 5);
        return l;
    }
    CCMenuItemSpriteExtra* button(const char* text, const char* bg, float scale, float x, float y, SEL_MenuHandler sel, int tag = 0) {
        auto spr = ButtonSprite::create(text, "bigFont.fnt", bg, 0.7f);
        spr->setScale(scale);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, sel);
        btn->setPosition(CCPoint(x, y));
        btn->setTag(tag);
        m_buttonMenu->addChild(btn);
        return btn;
    }
    void strip(float y, float h) {
        auto s = cocos2d::extension::CCScale9Sprite::create("square02_small.png", CCRect(0, 0, 40, 40));
        if (!s) return;
        s->setOpacity(70);
        s->setContentSize(CCSize(m_w - 30, h));
        s->setPosition(CCPoint(m_center.x, m_center.y + y));
        m_mainLayer->addChild(s, 1);
    }
public:
    void onClose(CCObject*) {
        this->unscheduleAllSelectors();
        this->setKeypadEnabled(false);
        this->setTouchEnabled(false);
        this->removeFromParentAndCleanup(true);
    }
    void keyBackClicked() override { onClose(nullptr); }
};

// ---------------- collection book ----------------
class BookPopup : public RCPopup {
protected:
    int m_page = 0;
    CCNode* m_rows = nullptr;
    CCLabelBMFont* m_pageLbl = nullptr;

    void fill() {
        m_rows->removeAllChildren();
        auto bk = rc::book();
        float y = 72;
        for (int i = m_page * 10; i < (std::min)(rc::N, m_page * 10 + 10); i++, y -= 19) {
            auto const& c = rc::CH[i];
            bool seen = bk[i] != '0';
            auto name = CCLabelBMFont::create(fmt::format("#{:03}  {}", i + 1, seen ? c.name : "???").c_str(), "bigFont.fnt");
            name->setScale(0.36f); name->setAnchorPoint(CCPoint(0, 0.5f));
            name->setPosition(CCPoint(m_center.x - 175, m_center.y + y));
            name->setColor(seen ? rc::RARITY_COLOR[c.rar] : ccColor3B{130, 130, 130});
            m_rows->addChild(name);
            auto st = CCLabelBMFont::create(bk[i] == '2' ? "CLEARED" : seen ? rc::RARITY_NAME[c.rar] : "", "goldFont.fnt");
            st->setScale(0.45f); st->setAnchorPoint(CCPoint(1, 0.5f));
            st->setPosition(CCPoint(m_center.x + 175, m_center.y + y));
            if (bk[i] == '2') st->setColor({120, 255, 120});
            m_rows->addChild(st);
        }
        int cleared = (int)std::count(bk.begin(), bk.end(), '2'), seen = rc::N - (int)std::count(bk.begin(), bk.end(), '0');
        m_pageLbl->setString(fmt::format("Page {}/{}     Seen {}/{}     Cleared {}/{}", m_page + 1, (rc::N + 9) / 10, seen, rc::N, cleared, rc::N).c_str());
    }
    void onPage(CCObject* s) {
        int pages = (rc::N + 9) / 10;
        m_page = (m_page + static_cast<CCNode*>(s)->getTag() + pages) % pages;
        fill();
    }
    bool init() override {
        if (!initPopup(400, 270, "Challenge Book")) return false;
        m_rows = CCNode::create();
        m_mainLayer->addChild(m_rows, 5);
        m_pageLbl = label("", "chatFont.fnt", 0.6f, 0, -112);
        button("<", "GJ_button_01.png", 0.7f, -170, -112, menu_selector(BookPopup::onPage), -1);
        button(">", "GJ_button_01.png", 0.7f, 170, -112, menu_selector(BookPopup::onPage), 1);
        fill();
        return true;
    }
public:
    static BookPopup* create() {
        auto ret = new BookPopup();
        if (ret->init()) { ret->autorelease(); return ret; }
        delete ret; return nullptr;
    }
};

// ---------------- stats ----------------
class StatsPopup : public RCPopup {
protected:
    bool init() override {
        if (!initPopup(300, 220, "Roulette Stats")) return false;
        int spins = rc::sv<int>("spins", 0), wins = rc::sv<int>("wins", 0);
        std::pair<const char*, std::string> rows[] = {
            {"Points", std::to_string(rc::points())},
            {"Spins", std::to_string(spins)},
            {"Challenges cleared", std::to_string(wins)},
            {"Win rate", spins > 0 ? fmt::format("{}%", wins * 100 / spins) : "-"},
            {"Current streak", std::to_string(rc::sv<int>("streak", 0))},
            {"Best streak", std::to_string(rc::sv<int>("best-streak", 0))},
            {"Points earned", std::to_string(rc::sv<int>("earned", 0))},
            {"Jackpots", std::to_string(rc::sv<int>("jackpots", 0))},
        };
        float y = 62;
        for (auto const& [k, v] : rows) {
            strip(y, 18);
            auto a = label(k, "bigFont.fnt", 0.38f, -125, y); a->setAnchorPoint(CCPoint(0, 0.5f));
            auto b = label(v.c_str(), "goldFont.fnt", 0.55f, 125, y); b->setAnchorPoint(CCPoint(1, 0.5f));
            y -= 20;
        }
        return true;
    }
public:
    static StatsPopup* create() {
        auto ret = new StatsPopup();
        if (ret->init()) { ret->autorelease(); return ret; }
        delete ret; return nullptr;
    }
};

// ---------------- the roulette ----------------
class RoulettePopup : public RCPopup {
protected:
    GJGameLevel* m_level = nullptr;
    std::vector<int> m_result;
    bool m_spinning = false, m_daily = false, m_usedFreeReroll = false;
    float m_spinTime = 0, m_nextSwap = 0;
    int m_spinCount = 1;
    CCLabelBMFont *m_reel = nullptr, *m_reel2 = nullptr, *m_rarity = nullptr, *m_desc = nullptr, *m_info = nullptr, *m_points = nullptr;
    CCMenuItemSpriteExtra *m_accept = nullptr, *m_reroll = nullptr;

    void showReel(CCLabelBMFont* l, int id) {
        l->setString(rc::CH[id].name);
        l->setColor(rc::RARITY_COLOR[rc::CH[id].rar]);
        l->setScale((std::min)(0.9f, 330.f / (l->getContentSize().width + 1.f)));
    }

    void refreshTop() {
        m_points->setString(fmt::format("Points: {}     Streak: {}", rc::points(), rc::sv<int>("streak", 0)).c_str());
    }

    void startSpin(int count, bool daily) {
        if (m_spinning) return;
        m_spinning = true; m_daily = daily; m_spinCount = count; m_spinTime = 0; m_nextSwap = 0;
        m_result.clear();
        if (daily) m_result.push_back(rc::dailyId());
        else {
            m_result.push_back(rc::pick());
            if (count > 1) m_result.push_back(rc::pick(m_result[0]));
        }
        m_accept->setVisible(false); m_reroll->setVisible(false);
        m_reel2->setVisible(count > 1);
        m_rarity->setString(""); m_desc->setString(""); m_info->setString("Spinning...");
        rc::setSv<int>("spins", rc::sv<int>("spins", 0) + 1);
        this->schedule(schedule_selector(RoulettePopup::spinTick));
    }

    void spinTick(float dt) {
        m_spinTime += dt;
        if (m_spinTime >= m_nextSwap) {          // names change fast at first, then slow down
            m_nextSwap = m_spinTime + 0.04f + m_spinTime * m_spinTime * 0.06f;
            showReel(m_reel, rc::irand(rc::N));
            if (m_spinCount > 1) showReel(m_reel2, rc::irand(rc::N));
        }
        if (m_spinTime < 2.2f) return;
        this->unschedule(schedule_selector(RoulettePopup::spinTick));
        m_spinning = false;

        // 2% jackpot: free points, no challenge
        if (!m_daily && rc::irand(100) < 2) {
            rc::addPoints(100);
            rc::setSv<int>("jackpots", rc::sv<int>("jackpots", 0) + 1);
            m_reel->setString("JACKPOT!"); m_reel->setColor({255, 220, 0}); m_reel->setScale(0.9f);
            m_reel2->setVisible(false);
            m_rarity->setString(""); m_desc->setString("Free +100 points. No challenge this time!");
            m_info->setString("Spin again whenever you like");
            m_result.clear();
            refreshTop();
            return;
        }
        showResult();
    }

    void showResult() {
        showReel(m_reel, m_result[0]);
        if (m_result.size() > 1) showReel(m_reel2, m_result[1]);
        for (int id : m_result) rc::markBook(id, '1');
        auto ch = rc::makeChallenge(m_level, m_result, m_daily);
        std::string rar, desc;
        for (int id : m_result) {
            if (!rar.empty()) { rar += " + "; desc += "  /  "; }
            rar += rc::RARITY_NAME[rc::CH[id].rar]; desc += rc::CH[id].desc;
        }
        m_rarity->setString(rar.c_str());
        m_rarity->setColor(rc::RARITY_COLOR[rc::CH[m_result[0]].rar]);
        m_desc->setString(desc.c_str());
        m_desc->setScale((std::min)(0.7f, 360.f / (m_desc->getContentSize().width + 1.f)));
        std::string tries = ch.tries >= 0 ? fmt::format("   Tries: {}", ch.tries) : "";
        m_info->setString(fmt::format("Goal: reach {}%{}     Reward: +{}{}", ch.goal, tries, ch.reward, m_daily ? "  (DAILY x3)" : "").c_str());
        m_accept->setVisible(true);
        m_reroll->setVisible(!m_daily);
    }

    void onSpin(CCObject*) { startSpin(1, false); }
    void onDouble(CCObject*) { startSpin(2, false); }
    void onDaily(CCObject*) {
        if (rc::sv<std::string>("daily-done", "") == rc::today()) {
            FLAlertLayer::create("Daily Challenge", "You already cleared today's daily.\nCome back tomorrow!", "OK")->show();
            return;
        }
        startSpin(1, true);
    }
    void onReroll(CCObject*) {
        if (m_spinning || m_result.empty()) return;
        if (m_usedFreeReroll) {
            if (rc::points() < 20) { FLAlertLayer::create("Reroll", "A reroll costs <cy>20</c> points.", "OK")->show(); return; }
            rc::addPoints(-20);
        }
        m_usedFreeReroll = true;
        refreshTop();
        startSpin(m_spinCount, false);
    }
    void onAccept(CCObject*) {
        if (m_spinning || m_result.empty()) return;
        rc::g_act = rc::makeChallenge(m_level, m_result, m_daily);
        auto const& a = rc::g_act;
        this->onClose(nullptr);
        FLAlertLayer::create("Challenge accepted!",
            fmt::format("Reach <cg>{}%</c> to win <cg>+{}</c> points.\nQuitting the level counts as a <cr>fail</c>.\n<co>Practice mode does not count.</c>", a.goal, a.reward).c_str(),
            "PLAY")->show();
    }
    void onBook(CCObject*) { if (!m_spinning) BookPopup::create()->show(); }
    void onStats(CCObject*) { if (!m_spinning) StatsPopup::create()->show(); }

    bool init(GJGameLevel* level) {
        if (!initPopup(420, 280, "Roulette Challenge")) return false;
        m_level = level;
        m_points = label("", "bigFont.fnt", 0.4f, 0, 98);
        m_points->setColor({120, 255, 255});

        strip(52, 64);
        m_reel = label("? ? ?", "bigFont.fnt", 0.9f, 0, 64);
        m_reel2 = label("? ? ?", "bigFont.fnt", 0.9f, 0, 38);
        m_reel2->setVisible(false);
        m_rarity = label("", "goldFont.fnt", 0.6f, 0, 8);
        m_desc = label("Spin to get a random handicap", "chatFont.fnt", 0.7f, 0, -12);
        m_info = label("", "bigFont.fnt", 0.36f, 0, -34);
        m_info->setColor({255, 255, 150});

        m_accept = button("ACCEPT", "GJ_button_01.png", 0.75f, -60, -66, menu_selector(RoulettePopup::onAccept));
        m_reroll = button("REROLL", "GJ_button_06.png", 0.6f, 70, -66, menu_selector(RoulettePopup::onReroll));
        m_accept->setVisible(false); m_reroll->setVisible(false);

        button("SPIN", "GJ_button_01.png", 0.75f, -140, -108, menu_selector(RoulettePopup::onSpin));
        button("x2", "GJ_button_02.png", 0.75f, -72, -108, menu_selector(RoulettePopup::onDouble));
        button("DAILY", "GJ_button_03.png", 0.75f, -5, -108, menu_selector(RoulettePopup::onDaily));
        button("BOOK", "GJ_button_04.png", 0.6f, 85, -108, menu_selector(RoulettePopup::onBook));
        button("STATS", "GJ_button_04.png", 0.6f, 155, -108, menu_selector(RoulettePopup::onStats));
        refreshTop();
        return true;
    }
public:
    static RoulettePopup* create(GJGameLevel* level) {
        auto ret = new RoulettePopup();
        if (ret->init(level)) { ret->autorelease(); return ret; }
        delete ret; return nullptr;
    }
};

// =====================================================================================
// Level page button
// =====================================================================================
class $modify(RCLevelInfo, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;
        if (!level || level->isPlatformer()) return true;
        auto win = CCDirector::sharedDirector()->getWinSize();
        auto spr = ButtonSprite::create("ROULETTE", "goldFont.fnt", "GJ_button_03.png", 0.9f);
        spr->setScale(0.55f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(RCLevelInfo::onRoulette));
        auto menu = CCMenu::create();
        menu->addChild(btn);
        menu->setPosition(CCPoint(52, win.height / 2 - 28));
        menu->setID("roulette-menu"_spr);
        this->addChild(menu, 10);
        return true;
    }
    void onRoulette(CCObject*) {
        if (m_level) RoulettePopup::create(m_level)->show();
    }
};

// =====================================================================================
// Gameplay hooks
// =====================================================================================
class $modify(RCPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        rc::g_run = rc::Run{};
        CCDirector::sharedDirector()->getScheduler()->setTimeScale(1.f);
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        auto& r = rc::g_run;
        r.owner = this;
        r.key = rc::levelKey(level);
        if (rc::g_act.on && (rc::g_act.key != r.key || m_isPlatformer)) rc::g_act.on = false;   // challenge was for another level

        auto win = CCDirector::sharedDirector()->getWinSize();
        r.root = CCNode::create();
        r.root->setID("fx"_spr);
        this->addChild(r.root, 900);
        r.hud = CCLabelBMFont::create("", "bigFont.fnt");
        r.hud->setScale(0.3f);
        r.hud->setAnchorPoint(CCPoint(0, 1));
        r.hud->setPosition(CCPoint(6, win.height - 40));
        r.hud->setOpacity(210);
        r.hud->setColor({255, 220, 120});
        r.hud->setID("hud"_spr);
        this->addChild(r.hud, 1000);

        if (rc::live(this)) rc::buildAll(this);
        rc::updateHud();
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (rc::g_run.owner == this) { rc::g_run.attemptCounted = false; rc::g_run.t = 0; }
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
        auto& r = rc::g_run;
        if (object == m_anticheatSpike || r.owner != this || r.attemptCounted) return;
        r.attemptCounted = true;
        if (!rc::live(this) || !rc::counts(this)) return;
        auto& a = rc::g_act;
        if (this->getCurrentPercentInt() >= a.goal) { rc::finish(this, true); return; }
        if (a.triesLeft > 0 && --a.triesLeft == 0) { rc::finish(this, false); return; }
        rc::updateHud();
    }

    void levelComplete() {
        if (rc::g_run.owner == this) {
            if (rc::live(this) && rc::counts(this)) rc::finish(this, true);
            else rc::clearFx(this);          // practice clear: just remove the effects for the end screen
        }
        PlayLayer::levelComplete();
    }

    void pauseGame(bool unfocused) {
        if (rc::g_run.owner == this) rc::neutral(this);   // pause menu must look and behave normally
        PlayLayer::pauseGame(unfocused);
    }

    void onQuit() {
        if (rc::g_run.owner == this) {
            rc::neutral(this);
            if (rc::live(this)) { rc::g_run.hud = nullptr; rc::finish(nullptr, false); }
            rc::g_run = rc::Run{};
        }
        CCDirector::sharedDirector()->getScheduler()->setTimeScale(1.f);
        PlayLayer::onQuit();
    }
};

class $modify(RCGameLayer, GJBaseGameLayer) {
    void update(float dt) {
        GJBaseGameLayer::update(dt);
        auto& r = rc::g_run;
        if (!r.owner || static_cast<GJBaseGameLayer*>(r.owner) != this) return;
        if (rc::live(r.owner)) rc::tick(r.owner, dt);
    }
};
