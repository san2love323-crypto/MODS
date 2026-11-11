// Ghost & Bet — Geode mod for Geometry Dash 2.2081 (Windows)
//
//  * Ghost : your best run of each level is recorded and played back as a see-through icon.
//  * Bet   : on the level page, bet points that you will reach a target % within N tries.
//  * Shop  : spend points on trail colors and death-taunt packs.
//
// UI uses only GD's own sprites / fonts so it looks like the base game.
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace geode::prelude;

// =====================================================================================
// Saved data: points + shop
// =====================================================================================
namespace gb {

struct Item { const char* name; int cost; int kind; };   // kind 0 = trail color, 1 = taunt pack
const Item ITEMS[] = {
    {"Trail: Default", 0, 0},   {"Trail: Red", 150, 0},   {"Trail: Cyan", 150, 0},
    {"Trail: Gold", 300, 0},    {"Trail: Pink", 300, 0},  {"Trail: Rainbow", 800, 0},
    {"Taunts: Basic", 0, 1},    {"Taunts: Spicy", 200, 1}, {"Taunts: Savage", 500, 1},
};
constexpr int ITEM_COUNT = 9;
constexpr int FREE_MASK = (1 << 0) | (1 << 6);
const ccColor3B TRAIL_COLORS[] = {{255, 255, 255}, {255, 50, 50}, {0, 230, 255}, {255, 200, 0}, {255, 90, 200}, {255, 255, 255}};

int points() { return Mod::get()->getSavedValue<int>("points", 100); }
void setPoints(int p) { Mod::get()->setSavedValue<int>("points", (std::max)(0, p)); }
int owned() { return Mod::get()->getSavedValue<int>("owned", FREE_MASK) | FREE_MASK; }
bool isOwned(int i) { return (owned() >> i) & 1; }
void setOwned(int i) { Mod::get()->setSavedValue<int>("owned", owned() | (1 << i)); }
int trail() { return Mod::get()->getSavedValue<int>("trail", 0); }
void setTrail(int i) { Mod::get()->setSavedValue<int>("trail", i); }

// never leave the player with nothing to bet
void bailout() { if (points() < 10) setPoints(10); }

// ---------- taunts (Korean, drawn with a system font because GD's fonts have no Hangul) ----------
const std::vector<std::string> TAUNTS[3] = {
    {"또 죽었네 ㅋㅋ", "아깝다!", "손 미끄러졌어?", "다시 다시!", "괜찮아, 그럴 수 있어", "거의 다 왔는데..."},
    {"그걸 못 넘어?", "눈 감고 했어?", "가시가 웃고 있어", "연습모드 추천드립니다", "키보드 탓 금지", "방금 건 좀 심했다"},
    {"{n}번째 죽음, 축하해!", "맵이 널 깨는 중", "오늘은 여기까지 할까?", "가시: 1승 추가", "이 정도면 재능이야", "{n}번 죽고도 포기 안 하는 건 인정"},
};

std::string randomTaunt(int deaths) {
    static std::mt19937 rng{std::random_device{}()};
    std::vector<const std::string*> pool;
    for (int p = 0; p < 3; p++) if (isOwned(6 + p)) for (auto const& s : TAUNTS[p]) pool.push_back(&s);
    if (pool.empty()) return "";
    std::string s = *pool[rng() % pool.size()];
    auto at = s.find("{n}");
    if (at != std::string::npos) s.replace(at, 3, std::to_string(deaths));
    return s;
}

// ---------- bet ----------
struct Bet {
    bool placed = false;
    std::string key;
    int target = 50, stake = 10, tries = 5, triesLeft = 5;
    float mult = 1.f;
};
Bet g_bet;

const int TRY_OPTIONS[] = {1, 5, 10};
float multiplier(int target, int best, int tries) {
    float m = target > best ? 1.5f + (target - best) / 100.f * 3.f : 1.2f;   // beating your best pays far more
    m *= tries <= 1 ? 2.0f : tries <= 5 ? 1.3f : 1.0f;                        // fewer tries pays more
    return m;
}
int profit(int stake, float mult) { return (std::max)(1, (int)std::lround(stake * (mult - 1.f))); }

std::string levelKey(GJGameLevel* level) {
    int id = level->m_levelID.value();
    if (id > 0) return std::to_string(id);
    return fmt::format("local_{}", std::hash<std::string>{}(std::string(level->m_levelName)));
}

// ---------- ghost ----------
struct Sample { float t, x, y, rot; uint8_t mode, flags; uint16_t pad; };
struct Ghost {
    std::vector<Sample> s;
    int percent = 0;
    bool completed = false;
    float time = 0.f;
};

std::filesystem::path ghostPath(std::string const& key) {
    return Mod::get()->getSaveDir() / fmt::format("ghost_{}.bin", key);
}
bool loadGhost(std::string const& key, Ghost& g) {
    std::ifstream f(ghostPath(key), std::ios::binary);
    if (!f) return false;
    char magic[4]; uint8_t completed = 0; uint32_t count = 0;
    f.read(magic, 4);
    if (!f || std::memcmp(magic, "GBG1", 4) != 0) return false;
    f.read((char*)&g.percent, 4); f.read((char*)&completed, 1); f.read((char*)&g.time, 4); f.read((char*)&count, 4);
    if (!f || count == 0 || count > 2000000) return false;
    g.completed = completed != 0;
    g.s.resize(count);
    f.read((char*)g.s.data(), (std::streamsize)count * sizeof(Sample));
    return (bool)f;
}
void saveGhost(std::string const& key, Ghost const& g) {
    std::ofstream f(ghostPath(key), std::ios::binary | std::ios::trunc);
    if (!f) return;
    uint8_t completed = g.completed ? 1 : 0; uint32_t count = (uint32_t)g.s.size();
    f.write("GBG1", 4);
    f.write((const char*)&g.percent, 4); f.write((const char*)&completed, 1); f.write((const char*)&g.time, 4); f.write((const char*)&count, 4);
    f.write((const char*)g.s.data(), (std::streamsize)count * sizeof(Sample));
}

// ---------- state of the level being played ----------
struct Run {
    PlayLayer* owner = nullptr;
    std::string key;
    bool alive = false;
    float t = 0.f, lastSample = -1.f;
    std::vector<Sample> rec;
    Ghost ghost;
    bool hasGhost = false;
    size_t cursor = 0;
    int ghostMode = -1;
    bool passedShown = false;
    int deaths = 0;
    float hue = 0.f;
    SimplePlayer* ghostNode = nullptr;
    CCLabelBMFont* marker = nullptr;
    CCLabelBMFont* hud = nullptr;
};
Run g_run;

ccColor3B hueColor(float h) {
    float x = std::fmod(h, 1.f) * 6.f; int i = (int)x; float f = x - i;
    float r = 0, g = 0, b = 0;
    switch (i % 6) {
        case 0: r = 1; g = f; break;      case 1: r = 1 - f; g = 1; break;
        case 2: g = 1; b = f; break;      case 3: g = 1 - f; b = 1; break;
        case 4: r = f; b = 1; break;      default: r = 1; b = 1 - f; break;
    }
    return {(GLubyte)(r * 255), (GLubyte)(g * 255), (GLubyte)(b * 255)};
}

// ---------- in-level messages ----------
void banner(PlayLayer* pl, std::string const& text, ccColor3B color) {
    auto win = CCDirector::sharedDirector()->getWinSize();
    auto lbl = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    lbl->setColor(color);
    lbl->setPosition(CCPoint(win.width / 2, win.height - 62));
    lbl->setScale(0.1f);
    pl->addChild(lbl, 1000);
    lbl->runAction(CCSequence::create(
        CCEaseBackOut::create(CCScaleTo::create(0.25f, 0.6f)),
        CCDelayTime::create(1.6f),
        CCFadeOut::create(0.4f),
        CCRemoveSelf::create(),
        nullptr));
}

void taunt(PlayLayer* pl, std::string const& text) {
    if (text.empty()) return;
    auto win = CCDirector::sharedDirector()->getWinSize();
    for (int i = 0; i < 2; i++) {        // i = 0 : black shadow, i = 1 : text
        auto lbl = CCLabelTTF::create(text.c_str(), "Malgun Gothic", 22);
        if (!lbl) return;
        lbl->setColor(i == 0 ? ccColor3B{0, 0, 0} : ccColor3B{255, 240, 120});
        lbl->setPosition(CCPoint(win.width / 2 + (i == 0 ? 1.5f : 0.f), win.height * 0.7f - (i == 0 ? 1.5f : 0.f)));
        pl->addChild(lbl, 999 + i);
        lbl->runAction(CCSequence::create(CCDelayTime::create(0.9f), CCFadeOut::create(0.4f), CCRemoveSelf::create(), nullptr));
    }
}

void updateHud() {
    if (!g_run.hud) return;
    bool show = g_bet.placed && g_bet.key == g_run.key && Mod::get()->getSettingValue<bool>("hud");
    g_run.hud->setVisible(show);
    if (show) g_run.hud->setString(fmt::format("BET {}%   x{:.1f}   TRIES {}/{}   STAKE {}",
        g_bet.target, g_bet.mult, g_bet.triesLeft, g_bet.tries, g_bet.stake).c_str());
}

void betWon(PlayLayer* pl) {
    int gain = profit(g_bet.stake, g_bet.mult);
    setPoints(points() + gain);
    g_bet.placed = false;
    banner(pl, fmt::format("BET WON!  +{}", gain), {90, 255, 90});
    updateHud();
}
void betLost(PlayLayer* pl) {
    setPoints(points() - g_bet.stake);
    bailout();
    int lost = g_bet.stake;
    g_bet.placed = false;
    if (pl) banner(pl, fmt::format("BET LOST  -{}", lost), {255, 80, 80});
    else Notification::create(fmt::format("Bet forfeited: -{}", lost), NotificationIcon::Error)->show();
    updateHud();
}

} // namespace gb

// =====================================================================================
// GD-style popup base (brown box, gold title, close button) built on FLAlertLayer
// =====================================================================================
class GBPopup : public FLAlertLayer {
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

        m_buttonMenu = CCMenu::create();          // sits at the screen centre, so button positions are relative to the box centre
        m_buttonMenu->setZOrder(10);
        m_mainLayer->addChild(m_buttonMenu);

        auto t = CCLabelBMFont::create(title, "goldFont.fnt");
        t->setScale(0.8f);
        t->setPosition(CCPoint(m_center.x, m_center.y + h / 2 - 20));
        m_mainLayer->addChild(t, 5);

        if (auto spr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png")) {
            spr->setScale(0.8f);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(GBPopup::onClose));
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

    // dark inset strip behind a row, like GD's option rows
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
        this->setKeypadEnabled(false);
        this->setTouchEnabled(false);
        this->removeFromParentAndCleanup(true);
    }
    void keyBackClicked() override { onClose(nullptr); }
};

// =====================================================================================
// Shop
// =====================================================================================
class ShopPopup : public GBPopup {
protected:
    bool init() override {
        if (!initPopup(360, 270, "Point Shop")) return false;
        label(fmt::format("Points: {}", gb::points()).c_str(), "bigFont.fnt", 0.45f, 0, 96)->setColor({120, 255, 255});

        float y = 70;
        for (int i = 0; i < gb::ITEM_COUNT; i++, y -= 21) {
            auto const& it = gb::ITEMS[i];
            strip(y, 19);
            auto name = label(it.name, "bigFont.fnt", 0.38f, -160, y);
            name->setAnchorPoint(CCPoint(0, 0.5f));
            if (it.kind == 0 && i < 5) name->setColor(gb::TRAIL_COLORS[i]);

            bool own = gb::isOwned(i);
            if (!own) {
                auto cost = label(fmt::format("{} pts", it.cost).c_str(), "goldFont.fnt", 0.5f, 60, y);
                cost->setAnchorPoint(CCPoint(1, 0.5f));
                button("BUY", gb::points() >= it.cost ? "GJ_button_01.png" : "GJ_button_04.png", 0.42f, 125, y, menu_selector(ShopPopup::onBuy), i);
            } else if (it.kind == 0) {
                bool using_ = gb::trail() == i;
                button(using_ ? "USING" : "USE", using_ ? "GJ_button_02.png" : "GJ_button_01.png", 0.42f, 125, y, menu_selector(ShopPopup::onUse), i);
            } else {
                label("OWNED", "bigFont.fnt", 0.35f, 125, y)->setColor({120, 255, 120});
            }
        }
        label("Win bets on level pages to earn points", "chatFont.fnt", 0.6f, 0, -122);
        return true;
    }

    void reopen() {
        auto next = ShopPopup::create();
        next->m_noElasticity = true;
        this->onClose(nullptr);
        next->show();
    }
    void onBuy(CCObject* s) {
        int i = static_cast<CCNode*>(s)->getTag();
        auto const& it = gb::ITEMS[i];
        if (gb::points() < it.cost) {
            FLAlertLayer::create("Not enough points", fmt::format("You need <cy>{}</c> points.\nWin some bets first!", it.cost).c_str(), "OK")->show();
            return;
        }
        gb::setPoints(gb::points() - it.cost);
        gb::setOwned(i);
        if (it.kind == 0) gb::setTrail(i);
        reopen();
    }
    void onUse(CCObject* s) { gb::setTrail(static_cast<CCNode*>(s)->getTag()); reopen(); }

public:
    static ShopPopup* create() {
        auto ret = new ShopPopup();
        if (ret->init()) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }
};

// =====================================================================================
// Bet popup
// =====================================================================================
class BetPopup : public GBPopup {
protected:
    GJGameLevel* m_level = nullptr;
    int m_best = 0, m_target = 50, m_stake = 10, m_tryIdx = 1;
    CCLabelBMFont *m_targetLbl = nullptr, *m_stakeLbl = nullptr, *m_triesLbl = nullptr, *m_infoLbl = nullptr, *m_pointsLbl = nullptr;

    void refresh() {
        int pts = gb::points();
        m_target = std::clamp(m_target, 1, 100);
        m_stake = std::clamp(m_stake, 10, (std::max)(10, pts));
        int tries = gb::TRY_OPTIONS[m_tryIdx];
        float mult = gb::multiplier(m_target, m_best, tries);
        m_pointsLbl->setString(fmt::format("Points: {}", pts).c_str());
        m_targetLbl->setString(fmt::format("{}%", m_target).c_str());
        m_stakeLbl->setString(fmt::format("{}", m_stake).c_str());
        m_triesLbl->setString(tries == 1 ? "1 try" : fmt::format("{} tries", tries).c_str());
        m_infoLbl->setString(fmt::format("Best {}%    Payout x{:.1f}    Win +{}    Lose -{}", m_best, mult, gb::profit(m_stake, mult), m_stake).c_str());
    }

    void row(float y, const char* name, CCLabelBMFont*& value, const char* b1, const char* b2, const char* b3, const char* b4, int baseTag) {
        strip(y, 28);
        auto n = label(name, "goldFont.fnt", 0.6f, -150, y);
        n->setAnchorPoint(CCPoint(0, 0.5f));
        value = label("", "bigFont.fnt", 0.55f, 40, y);
        if (b1) button(b1, "GJ_button_06.png", 0.5f, -45, y, menu_selector(BetPopup::onAdjust), baseTag);
        if (b2) button(b2, "GJ_button_06.png", 0.5f, -10, y, menu_selector(BetPopup::onAdjust), baseTag + 1);
        if (b3) button(b3, "GJ_button_01.png", 0.5f, 92, y, menu_selector(BetPopup::onAdjust), baseTag + 2);
        if (b4) button(b4, "GJ_button_01.png", 0.5f, 130, y, menu_selector(BetPopup::onAdjust), baseTag + 3);
    }

    void onAdjust(CCObject* s) {
        switch (static_cast<CCNode*>(s)->getTag()) {
            case 10: m_target -= 10; break;  case 11: m_target -= 1; break;
            case 12: m_target += 1; break;   case 13: m_target += 10; break;
            case 20: m_stake -= 50; break;   case 21: m_stake -= 10; break;
            case 22: m_stake += 10; break;   case 23: m_stake += 50; break;
            case 31: m_tryIdx = (m_tryIdx + 2) % 3; break;
            case 32: m_tryIdx = (m_tryIdx + 1) % 3; break;
        }
        refresh();
    }

    void onPlace(CCObject*) {
        auto& b = gb::g_bet;
        b.placed = true;
        b.key = gb::levelKey(m_level);
        b.target = m_target; b.stake = m_stake;
        b.tries = b.triesLeft = gb::TRY_OPTIONS[m_tryIdx];
        b.mult = gb::multiplier(m_target, m_best, b.tries);
        this->onClose(nullptr);
        FLAlertLayer::create("Bet placed!",
            fmt::format("Reach <cg>{}%</c> within <cy>{}</c> {} to win <cg>+{}</c>.\nFail or quit the level and you lose <cr>{}</c>.\n<co>Practice mode does not count.</c>",
                b.target, b.tries, b.tries == 1 ? "try" : "tries", gb::profit(b.stake, b.mult), b.stake).c_str(),
            "PLAY")->show();
    }
    void onShop(CCObject*) { ShopPopup::create()->show(); }

    bool init(GJGameLevel* level) {
        if (!initPopup(380, 250, "Place Your Bet")) return false;
        m_level = level;
        m_best = level->m_normalPercent.value();
        m_target = std::clamp(m_best >= 100 ? 100 : m_best + 10, 1, 100);
        gb::bailout();

        m_pointsLbl = label("", "bigFont.fnt", 0.45f, 0, 82);
        m_pointsLbl->setColor({120, 255, 255});
        row(50, "Target", m_targetLbl, "-10", "-1", "+1", "+10", 10);
        row(16, "Stake", m_stakeLbl, "-50", "-10", "+10", "+50", 20);
        row(-18, "Tries", m_triesLbl, nullptr, "<", ">", nullptr, 30);
        m_infoLbl = label("", "chatFont.fnt", 0.65f, 0, -50);
        button("PLACE BET", "GJ_button_01.png", 0.8f, -70, -92, menu_selector(BetPopup::onPlace));
        button("SHOP", "GJ_button_02.png", 0.8f, 100, -92, menu_selector(BetPopup::onShop));
        refresh();
        return true;
    }

public:
    static BetPopup* create(GJGameLevel* level) {
        auto ret = new BetPopup();
        if (ret->init(level)) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }
};

// =====================================================================================
// Level page: BET button
// =====================================================================================
class $modify(GBLevelInfo, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;
        if (!level || level->isPlatformer()) return true;      // % does not exist in platformer levels
        auto win = CCDirector::sharedDirector()->getWinSize();
        auto spr = ButtonSprite::create("BET", "goldFont.fnt", "GJ_button_02.png", 0.9f);
        spr->setScale(0.7f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(GBLevelInfo::onBet));
        auto menu = CCMenu::create();
        menu->addChild(btn);
        menu->setPosition(CCPoint(34, win.height / 2 + 5));
        menu->setID("bet-menu"_spr);
        this->addChild(menu, 10);
        return true;
    }
    void onBet(CCObject*) {
        if (m_level) BetPopup::create(m_level)->show();
    }
};

// =====================================================================================
// Gameplay
// =====================================================================================
static bool runCounts(PlayLayer* pl) { return !pl->m_isPracticeMode && !pl->m_isTestMode && !pl->m_isPlatformer; }

static void startAttempt(PlayLayer* pl) {
    auto& r = gb::g_run;
    r.alive = true; r.t = 0.f; r.lastSample = -1.f; r.cursor = 0; r.passedShown = false;
    r.rec.clear();
    if (r.marker) r.marker->setVisible(false);
    if (r.ghostNode) r.ghostNode->setVisible(false);
}

static void recordBest(PlayLayer* pl, int percent, bool completed) {
    auto& r = gb::g_run;
    if (!runCounts(pl) || r.rec.size() < 10) return;
    bool better = !r.hasGhost
        || (completed && (!r.ghost.completed || r.t < r.ghost.time))
        || (!completed && !r.ghost.completed && percent > r.ghost.percent);
    if (!better) return;
    r.ghost.s = r.rec; r.ghost.percent = percent; r.ghost.completed = completed; r.ghost.time = r.t;
    r.hasGhost = true;
    gb::saveGhost(r.key, r.ghost);
}

class $modify(GBPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        gb::g_run = gb::Run{};
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        auto& r = gb::g_run;
        r.owner = this;
        r.key = gb::levelKey(level);
        r.hasGhost = !m_isPlatformer && gb::loadGhost(r.key, r.ghost);

        // a bet made for another level is simply dropped
        if (gb::g_bet.placed && gb::g_bet.key != r.key) gb::g_bet.placed = false;

        auto win = CCDirector::sharedDirector()->getWinSize();
        r.hud = CCLabelBMFont::create("", "bigFont.fnt");
        r.hud->setScale(0.3f);
        r.hud->setAnchorPoint(CCPoint(0, 1));
        r.hud->setPosition(CCPoint(6, win.height - 26));
        r.hud->setOpacity(200);
        r.hud->setID("bet-hud"_spr);
        this->addChild(r.hud, 1000);
        gb::updateHud();

        if (m_objectLayer && !m_isPlatformer) {
            auto gm = GameManager::get();
            int z = m_player1 ? m_player1->getZOrder() - 1 : 0;
            r.ghostNode = SimplePlayer::create(gm->getPlayerFrame());
            if (r.ghostNode) {
                r.ghostNode->setColors(gm->colorForIdx(gm->getPlayerColor()), gm->colorForIdx(gm->getPlayerColor2()));
                r.ghostNode->setVisible(false);
                r.ghostNode->setID("ghost"_spr);
                m_objectLayer->addChild(r.ghostNode, z);
                r.ghostMode = 0;
            }
            r.marker = CCLabelBMFont::create("X", "bigFont.fnt");
            r.marker->setColor({255, 60, 60});
            r.marker->setScale(0.6f);
            r.marker->setOpacity(190);
            r.marker->setVisible(false);
            m_objectLayer->addChild(r.marker, z);
        }
        startAttempt(this);
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (gb::g_run.owner == this) startAttempt(this);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
        auto& r = gb::g_run;
        if (object == m_anticheatSpike || r.owner != this || !r.alive) return;
        r.alive = false;
        r.deaths++;
        int percent = this->getCurrentPercentInt();
        recordBest(this, percent, false);

        bool said = false;
        auto& b = gb::g_bet;
        if (b.placed && b.key == r.key && runCounts(this)) {
            if (percent >= b.target) { gb::betWon(this); said = true; }
            else if (--b.triesLeft <= 0) { gb::betLost(this); said = true; }
            else gb::updateHud();
        }
        if (!said && Mod::get()->getSettingValue<bool>("taunts")) gb::taunt(this, gb::randomTaunt(r.deaths));
    }

    void levelComplete() {
        auto& r = gb::g_run;
        if (r.owner == this && r.alive) {
            r.alive = false;
            recordBest(this, 100, true);
            if (gb::g_bet.placed && gb::g_bet.key == r.key && runCounts(this)) gb::betWon(this);
        }
        PlayLayer::levelComplete();
    }

    void onQuit() {
        auto& r = gb::g_run;
        if (r.owner == this) {
            if (gb::g_bet.placed && gb::g_bet.key == r.key) { r.hud = nullptr; gb::betLost(nullptr); }   // leaving = giving up
            gb::g_run = gb::Run{};
        }
        PlayLayer::onQuit();
    }
};

class $modify(GBGameLayer, GJBaseGameLayer) {
    void update(float dt) {
        GJBaseGameLayer::update(dt);
        auto& r = gb::g_run;
        if (!r.owner || static_cast<GJBaseGameLayer*>(r.owner) != this) return;
        auto pl = r.owner;

        // ----- trail color from the shop -----
        int tr = gb::trail();
        if (tr > 0 && tr < 6) {
            r.hue += dt * 0.5f;
            ccColor3B c = tr == 5 ? gb::hueColor(r.hue) : gb::TRAIL_COLORS[tr];
            if (m_player1 && m_player1->m_regularTrail) m_player1->m_regularTrail->setColor(c);
            if (m_player2 && m_player2->m_regularTrail) m_player2->m_regularTrail->setColor(c);
        }

        if (!r.alive || !m_player1) return;
        r.t += dt;
        bool counts = runCounts(pl);

        // ----- record this attempt (60 samples per second) -----
        if (counts && r.t - r.lastSample >= 1.f / 60.f - 0.0005f) {
            auto p = m_player1;
            uint8_t mode = p->m_isShip ? 1 : p->m_isBall ? 2 : p->m_isBird ? 3 : p->m_isDart ? 4 : p->m_isRobot ? 5 : p->m_isSpider ? 6 : p->m_isSwing ? 7 : 0;
            uint8_t flags = (p->m_isUpsideDown ? 1 : 0) | (p->m_vehicleSize < 1.f ? 2 : 0);
            auto pos = p->getPosition();
            r.rec.push_back({r.t, pos.x, pos.y, p->getRotation(), mode, flags, 0});
            r.lastSample = r.t;
        }

        // ----- play the ghost back -----
        if (!r.ghostNode) return;
        if (!r.hasGhost || !counts || !Mod::get()->getSettingValue<bool>("ghost")) {
            r.ghostNode->setVisible(false);
            if (r.marker) r.marker->setVisible(false);
            return;
        }
        auto const& s = r.ghost.s;
        while (r.cursor + 1 < s.size() && s[r.cursor + 1].t <= r.t) r.cursor++;
        auto const& a = s[r.cursor];
        bool ended = r.cursor + 1 >= s.size();
        float x = a.x, y = a.y, rot = a.rot;
        if (!ended) {
            auto const& b = s[r.cursor + 1];
            float span = b.t - a.t, k = span > 0.0001f ? std::clamp((r.t - a.t) / span, 0.f, 1.f) : 0.f;
            x += (b.x - a.x) * k; y += (b.y - a.y) * k;
            if (std::fabs(b.rot - a.rot) < 180.f) rot += (b.rot - a.rot) * k;
        }

        if (a.mode != r.ghostMode) {       // ghost changes vehicle when you did
            static const IconType types[8] = {IconType::Cube, IconType::Ship, IconType::Ball, IconType::Ufo,
                                              IconType::Wave, IconType::Robot, IconType::Spider, IconType::Swing};
            auto type = types[a.mode & 7];
            r.ghostNode->updatePlayerFrame(GameManager::get()->activeIconForType(type), type);
            r.ghostMode = a.mode;
        }
        int opacity = (int)Mod::get()->getSettingValue<int64_t>("ghost-opacity");
        float size = (a.flags & 2) ? 0.6f : 1.f;
        r.ghostNode->setVisible(true);
        r.ghostNode->setPosition(CCPoint(x, y));
        r.ghostNode->setRotation(rot);
        r.ghostNode->setScaleX(size);
        r.ghostNode->setScaleY((a.flags & 1) ? -size : size);
        r.ghostNode->setOpacity((GLubyte)(ended && !r.ghost.completed ? opacity / 2 : opacity));

        if (r.marker) {                    // red X where the ghost died
            bool show = ended && !r.ghost.completed;
            r.marker->setVisible(show);
            if (show) r.marker->setPosition(CCPoint(x, y + 28));
        }

        if (!r.passedShown && !r.ghost.completed && r.ghost.percent > 0 && pl->getCurrentPercentInt() > r.ghost.percent) {
            r.passedShown = true;
            gb::banner(pl, fmt::format("PASSED YOUR GHOST!  ({}%)", r.ghost.percent), {120, 220, 255});
        }
    }
};
