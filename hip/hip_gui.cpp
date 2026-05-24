// Kasim Zeeshan Alvi || 24i0549 || CS-A
// Abdullah Junaid    || 24i0569 || CS-A
// hip_gui.cpp - sfml gui for hip: screens, animations, inventory ui, weapon projectiles

//std headers first 
#include <array>
#include <memory>
#include <string>
#include <deque>
#include <unordered_map>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <csignal>
//SFML
#include <SFML/Audio.hpp>
#include <SFML/Graphics.hpp>
//POSIX
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <signal.h>
//project
#include "shared.h"

static const int   WIN_W = 1280;
static const int   WIN_H = 720;
static const int   FPS   = 60;

// player display names & sprite prefixes
static const char* PNAME [MAX_PLAYERS] = {"Optimus Prime","Bumblebee","Arcee","Ratchet"};
static const char* PSPRITE[MAX_PLAYERS]= {"optimus","bumblebee","arcee","ratchet"};
static const sf::Color PCOL[MAX_PLAYERS] = {
    {220,80,80},{240,200,0},{180,80,220},{60,200,80}
};

// enemy sprite prefixes (cycle by index mod 4)
// Enemy sprites: first 3 enemies get unique boss sprites, rest are grunts
static std::string getEnemySprite(int enemyIndex, const std::string& state)
{
    static const char* boss[3] = {"megatron","soundwave","starscream"};
    const char* name = (enemyIndex < 3) ? boss[enemyIndex] : "grunt";
    return "assets/sprites/enemies/" + std::string(name) + state + ".png";
}

// weapon icon filenames (index = weapon ID)
static const char* WICON[WEAPON_COUNT] = {
    "","solar_core","lunar_blade","iron_halberd","venom_dagger",
    "thunderstaff","obsidian_axe","frostbow","splinter_stick","eclipse_relic"
};

class AudioMgr {
public:
    static AudioMgr& get()
    { 
        static AudioMgr inst; 
        return inst; 
    }
    void music(const std::string&, bool=true)
    {
        // stub - no actual audio in this version
    }
    void stopMusic()
    {
        // stub
    }
    void sfx(const std::string&)
    {
        // stub
    }
private:
    AudioMgr() = default;
};

class AssetMgr {
public:
    static AssetMgr& get()
    { 
        static AssetMgr inst; 
        return inst; 
    }

    sf::Texture& tex(const std::string& p)
    {
        auto it = texCache_.find(p);
        if (it != texCache_.end()) 
        {
            return it->second;
        }
        sf::Texture& t = texCache_[p];
        if (!t.loadFromFile(p))
        {
            sf::Image img; 
            img.create(64, 64, sf::Color(255, 0, 255, 200));
            t.loadFromImage(img);
        }
        return t;
    }
    
    sf::Font& font(const std::string& p)
    {
        auto it = fontCache_.find(p);
        if (it != fontCache_.end()) 
        {
            return it->second;
        }
        sf::Font& f = fontCache_[p];
        if (!f.loadFromFile(p))
        {
            f.loadFromFile("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
        }
        return f;
    }
    
    sf::Font& main()
    { 
        return font("assets/fonts/main.ttf"); 
    }
    sf::Font& bold()
    { 
        return font("assets/fonts/bold.ttf"); 
    }
    sf::Font& mono()
    { 
        return font("assets/fonts/mono.ttf"); 
    }

private:
    std::unordered_map<std::string, sf::Texture> texCache_;
    std::unordered_map<std::string, sf::Font>    fontCache_;
};

struct Snap {
    int running, result, wave, kills;
    int turnIdx, turnIsPlayer;
    int nPlayers, nEnemies;
    int ultimateActive;
    float ultimateTimer;
    int dropPending, dropWeapon, dropForPlayer;
    int arbiterPid;
    Entity players[MAX_PLAYERS];
    Entity enemies[MAX_ENEMIES];
};

static Snap takeSnap(GameState* gs)
{
    Snap s{};
    sem_wait(&gs->stateLock);
    s.running        = gs->gameRunning;
    s.result         = gs->gameResult;
    s.wave           = gs->waveNumber;
    s.kills          = gs->totalEnemiesKilled;
    s.turnIdx        = gs->currentTurnEntityIndex;
    s.turnIsPlayer   = gs->currentTurnIsPlayer;
    s.nPlayers       = gs->numberOfPlayers;
    s.nEnemies       = gs->numberOfEnemies;
    s.ultimateActive = gs->ultimateAbilityActive;
    s.ultimateTimer  = gs->ultimateTimer;
    s.dropPending    = gs->weaponDrop.pending;
    s.dropWeapon     = gs->weaponDrop.weaponId;
    s.dropForPlayer  = gs->weaponDrop.forPlayerIndex;
    s.arbiterPid     = gs->arbiterPid;
    
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        s.players[i] = gs->players[i];
    }
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        s.enemies[i] = gs->enemies[i];
    }
    sem_post(&gs->stateLock);
    return s;
}

static void txt(sf::RenderWindow& w, const std::string& s,
                sf::Font& f, unsigned sz, float x, float y,
                sf::Color c, bool shadow=false)
{
    sf::Text t(s, f, sz);
    if (shadow)
    { 
        t.setFillColor({0, 0, 0, 150}); 
        t.setPosition(x + 2, y + 2); 
        w.draw(t); 
    }
    t.setFillColor(c); 
    t.setPosition(x, y); 
    w.draw(t);
}

static void txtC(sf::RenderWindow& w, const std::string& s,
                 sf::Font& f, unsigned sz, float cx, float y, sf::Color c)
{
    sf::Text t(s, f, sz);
    auto b = t.getLocalBounds();
    t.setFillColor(c);
    t.setPosition(cx - b.width / 2.f - b.left, y);
    w.draw(t);
}

static void rect(sf::RenderWindow& w, float x, float y, float ww, float hh,
                 sf::Color fill, sf::Color outline={0,0,0,0}, float thick=0)
{
    sf::RectangleShape r({ww, hh});
    r.setPosition(x, y); 
    r.setFillColor(fill);
    if (thick > 0)
    { 
        r.setOutlineColor(outline); 
        r.setOutlineThickness(thick); 
    }
    w.draw(r);
}

static void drawBar(sf::RenderWindow& w, bool isStam,
                    float x, float y, float bw, float bh,
                    int cur, int mx)
{
    if (mx <= 0) 
    {
        return;
    }
    float ratio = std::max(0.f, std::min(1.f, (float)cur / mx));

    // background
    rect(w, x, y, bw, bh, {15, 15, 20, 220}, {60, 60, 80, 255}, 1.f);

    // fill colour
    sf::Color fill;
    if (isStam)
    {
        fill = {40, 180, 220, 230};
    } 
    else 
    {
        if (ratio > 0.6f)      
            fill = {50, 200, 80, 230};
        else if (ratio > 0.3f) 
            fill = {220, 190, 40, 230};
        else                
            fill = {210, 50, 50, 230};
    }

    if (ratio > 0.001f)
    {
        sf::RectangleShape bar({bw * ratio, bh});
        bar.setPosition(x, y);
        bar.setFillColor(fill);
        w.draw(bar);
    }
    
    // white border on top
    sf::RectangleShape border({bw, bh});
    border.setPosition(x, y);
    border.setFillColor({0, 0, 0, 0});
    border.setOutlineColor({180, 180, 210, 200});
    border.setOutlineThickness(1.f);
    w.draw(border);
}

// Draw a sprite from assets folder with given display size
static void drawSprite(sf::RenderWindow& w, const std::string& path,
                       float x, float y, float dispW, float dispH,
                       sf::Uint8 alpha=255)
{
    sf::Sprite s(AssetMgr::get().tex(path));
    auto tb = s.getTexture()->getSize();
    s.setScale(dispW / tb.x, dispH / tb.y);
    s.setPosition(x, y);
    sf::Color c = s.getColor(); 
    c.a = alpha; 
    s.setColor(c);
    w.draw(s);
}

// drawButton removed — replaced by inline rect+txt in action menu

class Screen {
public:
    virtual ~Screen() = default;
    virtual void onEnter() {}
    virtual void onExit() {}
    virtual void handleEvent(const sf::Event&) = 0;
    virtual void update(float dt, const Snap&) = 0;
    virtual void draw(sf::RenderWindow&) = 0;

    bool wantTransition = false;
    int  nextScreen = 0; // 0=battle 1=win 2=lose 3=quit 4=wave
};

class MenuScreen : public Screen {
    float time_ = 0, pulse_ = 1;
    int   sel_ = 0;
public:
    void onEnter() override 
    {
        AudioMgr::get().music("assets/audio/music/main_menu.ogg");
        sel_ = 0; 
        time_ = 0;
    }
    
    void handleEvent(const sf::Event& e) override 
    {
        if (e.type != sf::Event::KeyPressed) 
        {
            return;
        }
        auto k = e.key.code;
        if (k == sf::Keyboard::Up || k == sf::Keyboard::Down ||
            k == sf::Keyboard::W  || k == sf::Keyboard::S)
        {
            sel_ ^= 1;
            AudioMgr::get().sfx("assets/audio/sfx/menu_select.ogg");
        }
        if (k == sf::Keyboard::Return || k == sf::Keyboard::Space)
        {
            AudioMgr::get().sfx("assets/audio/sfx/menu_confirm.ogg");
            wantTransition = true; 
            nextScreen = (sel_ == 0) ? 0 : 3;
        }
        if (k == sf::Keyboard::Num1 || k == sf::Keyboard::Numpad1)
        {
            AudioMgr::get().sfx("assets/audio/sfx/menu_confirm.ogg");
            wantTransition = true; 
            nextScreen = 0;
        }
        if (k == sf::Keyboard::Num2 || k == sf::Keyboard::Numpad2)
        {
            wantTransition = true; 
            nextScreen = 3;
        }
    }
    
    void update(float dt, const Snap&) override 
    {
        time_ += dt;
        pulse_ = 1.f + 0.05f * std::sin(time_ * 2.f);
    }
    
    void draw(sf::RenderWindow& w) override 
    {
        // background
        drawSprite(w, "assets/backgrounds/main_menu.png", 0, 0, WIN_W, WIN_H);
        // deep dark overlay for contrast
        rect(w, 0, 0, WIN_W, WIN_H, {0, 0, 10, 160});

        // animated energy orbs
        for (int i = 0; i < 6; i++)
        {
            float a = time_ * 0.4f + i * 1.047f;
            float r = 180 + 50 * std::sin(time_ * 0.7f + i);
            sf::CircleShape c(14 + 8 * std::sin(time_ * 1.2f + i * 0.8f));
            c.setOrigin(c.getRadius(), c.getRadius());
            c.setPosition(WIN_W / 2.f + r * std::cos(a) * 2.f, WIN_H * 0.38f + r * std::sin(a) * 0.5f);
            c.setFillColor({255, 180, 0, (sf::Uint8)(18 + 12 * std::sin(time_ + i))});
            w.draw(c);
        }

        // title block
        float cx = WIN_W / 2.f;

        // CHRONO RIFT — large gold pulsing
        {
            float sc = pulse_;
            sf::Text t("CHRONO RIFT", AssetMgr::get().bold(), (unsigned)(72 * sc));
            t.setFillColor({255, 210, 0});
            t.setOutlineColor({120, 60, 0, 200});
            t.setOutlineThickness(3.f);
            auto b = t.getLocalBounds();
            t.setOrigin(b.left + b.width / 2.f, b.top + b.height / 2.f);
            t.setPosition(cx, 160);
            w.draw(t);
        }

        // subtitle
        txtC(w, "TRANSFORMERS EDITION", AssetMgr::get().main(), 22,
             cx, 220, {200, 160, 255, 230});

        // divider line
        sf::RectangleShape div({300.f, 2.f});
        div.setOrigin(150.f, 1.f);
        div.setPosition(cx, 250);
        div.setFillColor({255, 200, 0, 180});
        w.draw(div);

        // buttons — bigger, cleaner
        float bw = 340, bh = 62, bx = cx - bw / 2.f;

        // ROLL OUT button
        {
            bool sel = (sel_ == 0);
            sf::Color bg = sel ? sf::Color(180, 130, 0, 240) : sf::Color(20, 20, 50, 210);
            sf::Color br = sel ? sf::Color(255, 220, 0, 255) : sf::Color(100, 80, 200, 200);
            rect(w, bx, 280, bw, bh, bg, br, sel ? 3.f : 1.5f);
            sf::Color tc = sel ? sf::Color(0, 0, 0) : sf::Color(255, 255, 255);
            txtC(w, sel ? "▶  AUTOBOTS ROLL OUT!  ◀" : "   AUTOBOTS ROLL OUT!",
                 AssetMgr::get().bold(), 20, cx, 280 + bh / 2.f - 10, tc);
        }

        // RETREAT button
        {
            bool sel = (sel_ == 1);
            sf::Color bg = sel ? sf::Color(120, 20, 20, 240) : sf::Color(20, 20, 50, 210);
            sf::Color br = sel ? sf::Color(255, 80, 80, 255) : sf::Color(100, 80, 200, 200);
            rect(w, bx, 360, bw, bh, bg, br, sel ? 3.f : 1.5f);
            sf::Color tc = sel ? sf::Color(255, 200, 200) : sf::Color(255, 255, 255);
            txtC(w, sel ? "▶  RETREAT  (QUIT)  ◀" : "   RETREAT  (QUIT)",
                 AssetMgr::get().bold(), 20, cx, 360 + bh / 2.f - 10, tc);
        }

        // controls hint
        txtC(w, "W/S or ↑↓ to navigate    ENTER to confirm    1 or 2 to select",
             AssetMgr::get().mono(), 13, cx, 448, {160, 160, 200, 200});

        // version/credit
        txtC(w, "CS2006 OS Project  •  Spring 2026  •  Kasim & Abdullah",
             AssetMgr::get().mono(), 11, cx, WIN_H - 24, {80, 80, 120, 180});
    }
};

class WaveScreen : public Screen {
    float timer_ = 0, scale_ = 0;
public:
    int waveNum = 1;
    
    void onEnter() override 
    {
        timer_ = 0; 
        scale_ = 0;
        AudioMgr::get().sfx("assets/audio/sfx/wave_start.ogg");
    }
    
    void handleEvent(const sf::Event&) override {}
    
    void update(float dt, const Snap& s) override 
    {
        waveNum = s.wave + 1;
        timer_ += dt;
        scale_ = std::min(1.f, scale_ + dt * 4.f);
        if (timer_ > 2.5f)
        { 
            wantTransition = true; 
            nextScreen = 0; 
        }
    }
    
    void draw(sf::RenderWindow& w) override 
    {
        drawSprite(w, "assets/backgrounds/wave_transition.png", 0, 0, WIN_W, WIN_H);
        rect(w, 0, 0, WIN_W, WIN_H, {0, 0, 0, 100});

        std::string wt = "WAVE " + std::to_string(waveNum);
        unsigned sz = (unsigned)(88 * scale_);
        if (sz < 4) 
        {
            sz = 4;
        }
        sf::Text t(wt, AssetMgr::get().bold(), sz);
        t.setFillColor({255, 220, 0});
        auto b = t.getLocalBounds();
        t.setOrigin(b.left + b.width / 2.f, b.top + b.height / 2.f);
        t.setPosition(WIN_W / 2.f, WIN_H / 2.f - 20);
        w.draw(t);

        txtC(w, "DECEPTICONS INCOMING!", AssetMgr::get().main(), (unsigned)(22 * scale_),
             WIN_W / 2.f, WIN_H / 2.f + 60, {255, 100, 100});
    }
};

class VictoryScreen : public Screen {
    float t_ = 0; 
    int kills_ = 0;
public:
    void onEnter() override 
    {
        AudioMgr::get().music("assets/audio/music/victory.ogg", false);
    }
    
    void handleEvent(const sf::Event& e) override 
    {
        if (e.type == sf::Event::KeyPressed)
        { 
            wantTransition = true; 
            nextScreen = 3; 
        }
    }
    
    void update(float dt, const Snap& s) override 
    { 
        t_ += dt; 
        kills_ = s.kills; 
    }
    
    void draw(sf::RenderWindow& w) override 
    {
        drawSprite(w, "assets/backgrounds/victory.png", 0, 0, WIN_W, WIN_H);
        rect(w, 0, 0, WIN_W, WIN_H, {0, 0, 0, 80});
        float sc = 1.f + 0.03f * std::sin(t_ * 3.f);
        sf::Text t("AUTOBOTS VICTORIOUS!", AssetMgr::get().bold(), (unsigned)(58 * sc));
        t.setFillColor({80, 255, 120});
        auto b = t.getLocalBounds();
        t.setOrigin(b.left + b.width / 2.f, b.top + b.height / 2.f);
        t.setPosition(WIN_W / 2.f, WIN_H / 2.f - 80);
        w.draw(t);
        txtC(w, "Decepticons destroyed: " + std::to_string(kills_) + "/10",
             AssetMgr::get().main(), 22, WIN_W / 2.f, WIN_H / 2.f + 10, {220, 255, 220});
        txtC(w, "[ Press ENTER to exit ]", AssetMgr::get().mono(), 16,
             WIN_W / 2.f, WIN_H / 2.f + 80, {255, 220, 0});
    }
};

class DefeatScreen : public Screen {
    float t_ = 0; 
    int kills_ = 0;
public:
    void onEnter() override 
    {
        AudioMgr::get().music("assets/audio/music/defeat.ogg", false);
    }
    
    void handleEvent(const sf::Event& e) override 
    {
        if (e.type == sf::Event::KeyPressed)
        { 
            wantTransition = true; 
            nextScreen = 3; 
        }
    }
    
    void update(float dt, const Snap& s) override 
    { 
        t_ += dt; 
        kills_ = s.kills; 
    }
    
    void draw(sf::RenderWindow& w) override 
    {
        drawSprite(w, "assets/backgrounds/defeat.png", 0, 0, WIN_W, WIN_H);
        rect(w, 0, 0, WIN_W, WIN_H, {0, 0, 0, 100});
        sf::Text t("AUTOBOTS DEFEATED", AssetMgr::get().bold(), 58);
        t.setFillColor({255, 60, 60});
        auto b = t.getLocalBounds();
        t.setOrigin(b.left + b.width / 2.f, b.top + b.height / 2.f);
        t.setPosition(WIN_W / 2.f, WIN_H / 2.f - 80);
        w.draw(t);
        txtC(w, "Decepticons destroyed: " + std::to_string(kills_) + "/10",
             AssetMgr::get().main(), 22, WIN_W / 2.f, WIN_H / 2.f + 10, {255, 200, 200});
        txtC(w, "[ Press ENTER to exit ]", AssetMgr::get().mono(), 16,
             WIN_W / 2.f, WIN_H / 2.f + 80, {255, 220, 0});
    }
};

class BattleScreen : public Screen {
public:
    explicit BattleScreen(GameState* gs): gs_(gs) {}

    // input state
    struct Input {
        int phase = 0;   // 0=menu 1=target 2=weapon 3=lts 4=pickup 5=quit 6=evict
        int menuSel = 0, targetSel = 0, weaponSel = 0, ltsSel = 0, evictSel = 0;
        int aliveE[MAX_ENEMIES]; 
        int aliveN = 0;
        int wSlot[INVENTORY_SLOTS]; 
        int wCount = 0;
        int evictSlots[INVENTORY_SLOTS]; 
        int evictCount = 0; // inventory weapons for evict pick
        Action act{};
        bool   ready = false;
    } inp_;

    // animation state per entity
    enum AnimState { ANIM_IDLE=0, ANIM_ATTACK, ANIM_HIT, ANIM_DEAD };
    struct EntAnim {
        AnimState state     = ANIM_IDLE;
        float     timer     = 0.f;
        float     lungeT    = 0.f;
        float     offX      = 0.f;
        float     deathTimer= -1.f; // counts down from 5s, -1=not started
        bool      deathDone = false; // true once linger is fully over
    };
    EntAnim pAnim_[MAX_PLAYERS];
    EntAnim eAnim_[MAX_ENEMIES];

    void onEnter() override 
    {
        AudioMgr::get().music("assets/audio/music/battle_normal.ogg");
        pulseT_ = 0; 
        animT_ = 0; 
        animFrame_ = 0; 
        prevWave_ = -1;
        ultimaAlpha_ = 0;
        memset(lastLog_, 0, sizeof(lastLog_));
        for (auto& a : pAnim_) 
        {
            a = {};
        }
        for (auto& a : eAnim_) 
        {
            a = {};
        }
    }

    // events
    void handleEvent(const sf::Event& e) override 
    {
        if (e.type != sf::Event::KeyPressed) 
        {
            return;
        }
        auto k = e.key.code;
        // U easter egg: give P1 Solar Core + Lunar Blade
        if (k == sf::Keyboard::U)
        {
            sem_wait(&gs_->stateLock);
            memset(gs_->players[0].inventory, 0, sizeof(gs_->players[0].inventory));
            for (int i = 0; i < 10; i++) 
            {
                gs_->players[0].inventory[i] = WEAPON_SOLAR_CORE;
            }
            for (int i = 10; i < 20; i++) 
            {
                gs_->players[0].inventory[i] = WEAPON_LUNAR_BLADE;
            }
            gs_->players[0].holdsSolarCore = 1;
            gs_->players[0].holdsLunarBlade = 1;
            gs_->artifacts.solarCoreHeldBy = 0;
            gs_->artifacts.lunarBladeHeldBy = 0;
            sem_post(&gs_->stateLock);
            pushLog("*** Easter Egg: Solar Core + Lunar Blade! ***");
            return;
        }
        if (activeP_ < 0) 
        {
            return;
        }
        switch (inp_.phase)
        {
            case 0: doMenuKey(k); break;
            case 1: doTargetKey(k); break;
            case 2: doWeaponKey(k); break;
            case 3: doLtsKey(k); break;
            case 4: doPickupKey(k); break;
            case 5: doQuitKey(k); break;
            case 6: doEvictKey(k); break;
        }
    }

    // update
    void update(float dt, const Snap& s) override 
    {
        snap_ = s;
        pulseT_ += dt * 3.f;
        animT_ += dt;
        if (animT_ > 0.45f)
        { 
            animT_ = 0; 
            animFrame_ = (animFrame_ + 1) % 2; 
        }

        // ultimate: splash only shows for first 1.5s, freeze lasts full 10s
        if (s.ultimateActive)
        {
            AudioMgr::get().music("assets/audio/music/battle_ultimate.ogg");
            if (s.ultimateTimer < 1.5f)
            {
                ultimaAlpha_ = std::min(1.f, ultimaAlpha_ + dt * 4.f);
            }
            else
            {
                ultimaAlpha_ = std::max(0.f, ultimaAlpha_ - dt * 3.f);
            }
        } 
        else 
        {
            AudioMgr::get().music("assets/audio/music/battle_normal.ogg");
            ultimaAlpha_ = std::max(0.f, ultimaAlpha_ - dt * 2.f);
        }

        // wave transition
        if (prevWave_ != -1 && s.wave != prevWave_)
        {
            wantTransition = true; 
            nextScreen = 4;
        }
        prevWave_ = s.wave;

        // B2/B4: Detect new action logs to trigger animation states
        for (int i = 0; i < s.nPlayers; i++)
        {
            if (strcmp(s.players[i].actionLog, lastLog_[i]) != 0)
            {
                strncpy(lastLog_[i], s.players[i].actionLog, ACTION_LOG_LEN - 1);
                pushLog(s.players[i].actionLog);
                // Trigger attack animation if player acted
                const char* l = s.players[i].actionLog;
                if (strstr(l, "strikes") || strstr(l, "exhausts") || strstr(l, "used ") || strstr(l, "ULTIMATE"))
                {
                    pAnim_[i].state = ANIM_ATTACK; 
                    pAnim_[i].timer = 0.67f; 
                    pAnim_[i].lungeT = 0.f;
                    // Fire weapon projectile if "used" a weapon
                    if (strstr(l, "used ") && !proj_.active)
                    {
                        // find which weapon was used from log
                        for (int wi = 1; wi < WEAPON_COUNT; wi++)
                        {
                            if (strstr(l, WEAPON_NAME[wi]))
                            {
                                proj_.weaponId = wi;
                                proj_.active = true;
                                proj_.t = 0.f;
                                proj_.dur = 0.5f;
                                // start: player sprite center
                                float panH = 458.f / std::max(1, s.nPlayers);
                                proj_.sx = 140.f;
                                proj_.sy = 44.f + i * panH + panH / 2.f;
                                // end: first enemy panel center
                                proj_.tx = 900.f;
                                proj_.ty = 44.f + 75.f;
                                break;
                            }
                        }
                    }
                    // Mark target enemy as hit
                    for (int j = 0; j < s.nEnemies; j++)
                    {
                        char tag[16]; 
                        snprintf(tag, sizeof(tag), "E%d", j + 1);
                        if (strstr(l, tag))
                        { 
                            eAnim_[j].state = ANIM_HIT; 
                            eAnim_[j].timer = 0.4f;
                            // aim projectile at actual target
                            if (proj_.active)
                            {
                                int cols = s.nEnemies <= 3 ? 1 : s.nEnemies <= 6 ? 2 : 3;
                                float cw = 490.f / cols, ch = 150.f;
                                int drawn = 0;
                                for (int k = 0; k < j; k++) 
                                {
                                    if (s.enemies[k].isAlive) 
                                    {
                                        drawn++;
                                    }
                                }
                                int col = drawn % cols, row = drawn / cols;
                                proj_.tx = 790.f + col * cw + cw / 2.f;
                                proj_.ty = 44.f + row * ch + ch / 2.f;
                            }
                            break;
                        }
                    }
                } 
                else if (strstr(l, "healed") || strstr(l, "skipped") || strstr(l, "swapped"))
                {
                    pAnim_[i].state = ANIM_IDLE;
                }
                if (!s.players[i].isAlive) 
                {
                    pAnim_[i].state = ANIM_DEAD;
                }
            }
        }
        
        for (int i = 0; i < s.nEnemies; i++)
        {
            if (strcmp(s.enemies[i].actionLog, lastLog_[MAX_PLAYERS + i]) != 0)
            {
                strncpy(lastLog_[MAX_PLAYERS + i], s.enemies[i].actionLog, ACTION_LOG_LEN - 1);
                pushLog(s.enemies[i].actionLog);
                const char* l = s.enemies[i].actionLog;
                if (strstr(l, "strikes"))
                {
                    eAnim_[i].state = ANIM_ATTACK; 
                    eAnim_[i].timer = 0.67f; 
                    eAnim_[i].lungeT = 0.f;
                    // Mark target player as hit
                    for (int j = 0; j < s.nPlayers; j++)
                    {
                        char tag[16]; 
                        snprintf(tag, sizeof(tag), "P%d", j + 1);
                        if (strstr(l, tag))
                        { 
                            pAnim_[j].state = ANIM_HIT; 
                            pAnim_[j].timer = 0.4f; 
                            break; 
                        }
                    }
                }
                if (!s.enemies[i].isAlive) 
                {
                    eAnim_[i].state = ANIM_DEAD;
                }
            }
        }

        for (int i = 0; i < MAX_PLAYERS; i++)
        {
            EntAnim& a = pAnim_[i];
            if (!s.players[i].isAlive && !a.deathDone && a.deathTimer < 0.f)
            { 
                a.state = ANIM_DEAD; 
                a.deathTimer = 5.f; 
            }
            if (a.deathTimer > 0.f)
            { 
                a.deathTimer -= dt; 
                if (a.deathTimer <= 0.f) 
                {
                    a.deathDone = true;
                }
            }
        }
        
        for (int i = 0; i < MAX_ENEMIES; i++)
        {
            EntAnim& a = eAnim_[i];
            if (!s.enemies[i].isAlive && !a.deathDone && a.deathTimer < 0.f)
            { 
                a.state = ANIM_DEAD; 
                a.deathTimer = 5.f; 
            }
            if (a.deathTimer > 0.f)
            { 
                a.deathTimer -= dt; 
                if (a.deathTimer <= 0.f) 
                {
                    a.deathDone = true;
                }
            }
        }

        // B4: Tick animation timers and compute lunge offset
        auto tickAnim = [&](EntAnim& a, float dt2, float lungeDir)
        {
            if (a.state == ANIM_ATTACK || a.state == ANIM_HIT)
            {
                a.timer -= dt2;
                if (a.state == ANIM_ATTACK)
                {
                    // lunge: 0→0.5 go forward, 0.5→1 return
                    float dur = 0.67f;
                    a.lungeT = std::min(1.f, a.lungeT + dt2 / (dur * 0.5f));
                    float progress = (a.lungeT <= 1.f) ? a.lungeT : 2.f - a.lungeT;
                    a.offX = lungeDir * 60.f * progress;
                }
                if (a.timer <= 0.f)
                {
                    a.state = ANIM_IDLE; 
                    a.timer = 0.f; 
                    a.offX = 0.f; 
                    a.lungeT = 0.f;
                }
            }
        };
        for (int i = 0; i < MAX_PLAYERS; i++) 
        {
            tickAnim(pAnim_[i], dt, +1.f);  // players lunge right
        }
        for (int i = 0; i < MAX_ENEMIES; i++) 
        {
            tickAnim(eAnim_[i], dt, -1.f);  // enemies lunge left
        }

        // tick weapon projectile
        if (proj_.active)
        {
            proj_.t += dt / proj_.dur;
            proj_.x = proj_.sx + (proj_.tx - proj_.sx) * proj_.t;
            proj_.y = proj_.sy + (proj_.ty - proj_.sy) * proj_.t;
            if (proj_.t >= 1.f) 
            {
                proj_.active = false;
            }
        }

        for (auto& e : log_) 
        {
            e.age += dt;
        }

        // weapon drop takes priority over everything — lock phase 4 until resolved
        if (s.dropPending)
        {
            int dp = s.dropForPlayer;
            if (dp >= 0 && dp < s.nPlayers && inp_.phase != 4)
            {
                activeP_ = dp;
                inp_ = Input{};
                inp_.phase = 4;
            }
            // don't let turn detection override while drop is pending
            goto skip_turn_detect;
        }
        if (inp_.phase == 4 && !s.dropPending)
        {
            // drop resolved — return to normal
            inp_.phase = 0; 
            inp_.ready = false;
        }

        // detect active player turn
        {
            int ap = (s.turnIsPlayer == 1) ? s.turnIdx : -1;
            if (ap != activeP_)
            {
                activeP_ = ap;
                if (ap >= 0)
                {
                    inp_ = Input{};
                    buildAliveEnemies();
                }
            }
        }

        skip_turn_detect:

        // submit action if ready
        if (inp_.ready)
        {
            submitAction();
            inp_.ready = false;
            inp_.phase = 0;
            activeP_ = -1;
        }

        // game over handled by Renderer loop
    }

    // draw
    void draw(sf::RenderWindow& w) override 
    {
        // background alternates per wave
        std::string bg = (snap_.wave % 2 == 0)
            ? "assets/backgrounds/battle_cybertron.png"
            : "assets/backgrounds/battle_earth.png";
        drawSprite(w, bg, 0, 0, WIN_W, WIN_H);
        rect(w, 0, 0, WIN_W, WIN_H, {0, 0, 0, 90});

        drawHUD(w);
        drawPlayerPanel(w);
        drawEnemyPanel(w);
        drawDivider(w);
        drawEventLog(w);
        drawActionArea(w);

        // draw weapon projectile on top of everything
        if (proj_.active && proj_.weaponId > 0 && WICON[proj_.weaponId][0])
        {
            std::string wp = "assets/sprites/weapons/" + std::string(WICON[proj_.weaponId]) + ".png";
            // spin effect: rotate based on progress
            float sz = 48.f;
            sf::Texture& tex = AssetMgr::get().tex(wp);
            sf::Sprite sp(tex);
            sp.setTextureRect({0, 0, (int)tex.getSize().x, (int)tex.getSize().y});
            float sc = sz / std::max(1u, tex.getSize().x);
            sp.setScale(sc, sc);
            sp.setOrigin(tex.getSize().x / 2.f, tex.getSize().y / 2.f);
            sp.setPosition(proj_.x, proj_.y);
            sp.setRotation(proj_.t * 360.f);
            w.draw(sp);
        }

        // ultimate flash
        if (ultimaAlpha_ > 0.01f)
        {
            sf::Sprite fl(AssetMgr::get().tex("assets/sprites/ui/ultimate_flash.png"));
            fl.setScale((float)WIN_W / 1280.f, (float)WIN_H / 720.f);
            sf::Color c = fl.getColor();
            c.a = (sf::Uint8)(ultimaAlpha_ * 160);
            fl.setColor(c); 
            w.draw(fl);
            txtC(w, "─── ULTIMATE ABILITY  ·  DECEPTICONS FROZEN ───",
                 AssetMgr::get().bold(), 26, WIN_W / 2.f, WIN_H / 2.f - 16,
                 {255, 220, 0, (sf::Uint8)(ultimaAlpha_ * 255)});
        }

        // bottom help bar
        rect(w, 0, WIN_H - 18, WIN_W, 18, {0, 0, 0, 210});
        txt(w, "ARROWS/WASD: navigate    1-8: quick select    ENTER/SPACE: confirm    ESC/0: back    Y/N: dialogs",
            AssetMgr::get().mono(), 10, 8, WIN_H - 16, {140, 150, 200});
    }

private:
    
    void doMenuKey(sf::Keyboard::Key k)
    {
        if (activeP_ < 0 || activeP_ >= snap_.nPlayers) 
        {
            return;
        }
        const Entity& pl = snap_.players[activeP_];

        bool hasWeap = false;
        for (int i = 0; i < INVENTORY_SLOTS; i++) 
        {
            if (pl.inventory[i])
            { 
                hasWeap = true; 
                break; 
            }
        }
        bool hasLts = (pl.longTermStorageCount > 0);
        bool hasUlt = (pl.holdsSolarCore && pl.holdsLunarBlade);

        int& s = inp_.menuSel;
        // navigation
        if (k == sf::Keyboard::Up   || k == sf::Keyboard::W) 
        { 
            s = (s - 2 + 8) % 8; 
        }
        if (k == sf::Keyboard::Down || k == sf::Keyboard::S) 
        { 
            s = (s + 2) % 8;   
        }
        if (k == sf::Keyboard::Left || k == sf::Keyboard::A) 
        { 
            if (s % 2 == 1) 
            {
                s--;
            }
        }
        if (k == sf::Keyboard::Right || k == sf::Keyboard::D)
        { 
            if (s % 2 == 0 && s < 7) 
            {
                s++;
            }
        }
        // number shortcuts
        for (int n = 1; n <= 8; n++)
        {
            if (k == (sf::Keyboard::Key)(sf::Keyboard::Num0 + n) ||
                k == (sf::Keyboard::Key)(sf::Keyboard::Numpad0 + n))
            {
                s = n - 1; 
                break;
            }
        }

        AudioMgr::get().sfx("assets/audio/sfx/menu_select.ogg");

        bool confirm = (k == sf::Keyboard::Return || k == sf::Keyboard::Space);
        if (!confirm) 
        {
            return;
        }

        AudioMgr::get().sfx("assets/audio/sfx/menu_confirm.ogg");
        Action& a = inp_.act;
        memset(&a, 0, sizeof(a));
        a.sourceEntityIndex = activeP_; 
        a.isPlayer = 1;

        switch (s)
        {
            case 0: // Strike
                a.actionType = ACTION_STRIKE;
                buildAliveEnemies();
                if (inp_.aliveN == 0) 
                {
                    return;
                }
                if (inp_.aliveN == 1)
                { 
                    a.targetIndex = inp_.aliveE[0]; 
                    inp_.ready = true; 
                }
                else 
                { 
                    inp_.phase = 1; 
                    inp_.targetSel = 0; 
                }
                break;
            case 1: // Exhaust
                a.actionType = ACTION_EXHAUST;
                buildAliveEnemies();
                if (inp_.aliveN == 0) 
                {
                    return;
                }
                if (inp_.aliveN == 1)
                { 
                    a.targetIndex = inp_.aliveE[0]; 
                    inp_.ready = true; 
                }
                else 
                { 
                    inp_.phase = 1; 
                    inp_.targetSel = 0; 
                }
                break;
            case 2: // Heal
                a.actionType = ACTION_HEAL; 
                inp_.ready = true; 
                break;
            case 3: // Skip
                a.actionType = ACTION_SKIP; 
                inp_.ready = true; 
                break;
            case 4: // Use Weapon
                if (!hasWeap) 
                {
                    return;
                }
                buildWeaponList(pl);
                if (inp_.wCount == 0) 
                {
                    return;
                }
                inp_.phase = 2; 
                inp_.weaponSel = 0;
                break;
            case 5: // Swap In — 3-step flow
            {
                if (!hasLts) 
                {
                    return;
                }
                // check if first LTS item would fit without eviction
                int ltsPreviewWid = pl.longTermStorage[0];
                bool needsEvict = true;
                for (int si = 0; si <= INVENTORY_SLOTS - WEAPON_SLOT_SIZE[ltsPreviewWid]; si++)
                {
                    bool ok = true;
                    for (int j = si; j < si + WEAPON_SLOT_SIZE[ltsPreviewWid]; j++)
                    {
                        if (pl.inventory[j])
                        {
                            ok = false;
                            break;
                        }
                    }
                    if (ok)
                    {
                        needsEvict = false;
                        break;
                    }
                }
                if (needsEvict)
                {
                    // Step 1: pick what to evict
                    buildWeaponList(pl);
                    if (inp_.evictCount == 0)
                    { 
                        inp_.phase = 3; 
                        inp_.ltsSel = 0; 
                        break; 
                    }
                    inp_.phase = 6; 
                    inp_.evictSel = 0;
                } 
                else 
                {
                    // space available — go straight to LTS pick
                    inp_.phase = 3; 
                    inp_.ltsSel = 0;
                }
                break;
            }
            case 6: // Ultimate
                if (!hasUlt) 
                {
                    return;
                }
                a.actionType = ACTION_ULTIMATE; 
                inp_.ready = true;
                AudioMgr::get().sfx("assets/audio/sfx/ultimate_activate.ogg");
                break;
            case 7: // Quit
                inp_.phase = 5; 
                break;
        }
    }

    void doTargetKey(sf::Keyboard::Key k)
    {
        int n = inp_.aliveN;
        if (n == 0)
        { 
            inp_.phase = 0; 
            return; 
        }
        if (k == sf::Keyboard::Up   || k == sf::Keyboard::W)    
        {
            inp_.targetSel = (inp_.targetSel - 1 + n) % n;
        }
        if (k == sf::Keyboard::Down || k == sf::Keyboard::S)  
        {
            inp_.targetSel = (inp_.targetSel + 1) % n;
        }
        for (int i = 1; i <= 9 && i <= n; i++)
        {
            if (k == (sf::Keyboard::Key)(sf::Keyboard::Num0 + i) ||
                k == (sf::Keyboard::Key)(sf::Keyboard::Numpad0 + i))
            {
                inp_.targetSel = i - 1;
            }
        }
        if (k == sf::Keyboard::Escape || k == sf::Keyboard::Num0 || k == sf::Keyboard::Numpad0)
        { 
            inp_.phase = 0; 
            return; 
        }
        if (k == sf::Keyboard::Return || k == sf::Keyboard::Space)
        {
            inp_.act.targetIndex = inp_.aliveE[inp_.targetSel];
            inp_.ready = true;
        }
        AudioMgr::get().sfx("assets/audio/sfx/menu_select.ogg");
    }

    void doWeaponKey(sf::Keyboard::Key k)
    {
        int n = inp_.wCount;
        if (n == 0)
        { 
            inp_.phase = 0; 
            return; 
        }
        if (k == sf::Keyboard::Up   || k == sf::Keyboard::W)   
        {
            inp_.weaponSel = (inp_.weaponSel - 1 + n) % n;
        }
        if (k == sf::Keyboard::Down || k == sf::Keyboard::S) 
        {
            inp_.weaponSel = (inp_.weaponSel + 1) % n;
        }
        for (int i = 1; i <= 9 && i <= n; i++)
        {
            if (k == (sf::Keyboard::Key)(sf::Keyboard::Num0 + i) ||
                k == (sf::Keyboard::Key)(sf::Keyboard::Numpad0 + i))
            {
                inp_.weaponSel = i - 1;
            }
        }
        if (k == sf::Keyboard::Escape || k == sf::Keyboard::Num0) 
        { 
            inp_.phase = 0; 
            return; 
        }
        if (k == sf::Keyboard::Return || k == sf::Keyboard::Space)
        {
            inp_.act.actionType = ACTION_USE_WEAPON;
            inp_.act.weaponStartSlot = inp_.wSlot[inp_.weaponSel];
            buildAliveEnemies();
            if (inp_.aliveN == 0)
            { 
                inp_.phase = 0; 
                return; 
            }
            if (inp_.aliveN == 1)
            { 
                inp_.act.targetIndex = inp_.aliveE[0]; 
                inp_.ready = true; 
            }
            else 
            { 
                inp_.phase = 1; 
                inp_.targetSel = 0; 
            }
        }
        AudioMgr::get().sfx("assets/audio/sfx/menu_select.ogg");
    }

    void doLtsKey(sf::Keyboard::Key k)
    {
        if (activeP_ < 0)
        { 
            inp_.phase = 0; 
            return; 
        }
        int n = snap_.players[activeP_].longTermStorageCount;
        if (n == 0)
        { 
            inp_.phase = 0; 
            return; 
        }
        if (k == sf::Keyboard::Up   || k == sf::Keyboard::W)   
        {
            inp_.ltsSel = (inp_.ltsSel - 1 + n) % n;
        }
        if (k == sf::Keyboard::Down || k == sf::Keyboard::S) 
        {
            inp_.ltsSel = (inp_.ltsSel + 1) % n;
        }
        for (int i = 1; i <= 9 && i <= n; i++)
        {
            if (k == (sf::Keyboard::Key)(sf::Keyboard::Num0 + i) ||
                k == (sf::Keyboard::Key)(sf::Keyboard::Numpad0 + i))
            {
                inp_.ltsSel = i - 1;
            }
        }
        if (k == sf::Keyboard::Escape || k == sf::Keyboard::Num0) 
        { 
            inp_.phase = 0; 
            return; 
        }
        if (k == sf::Keyboard::Return || k == sf::Keyboard::Space)
        {
            inp_.act.actionType = ACTION_SWAP_IN;
            inp_.act.ltsIndex = inp_.ltsSel;
            inp_.ready = true;
        }
        AudioMgr::get().sfx("assets/audio/sfx/menu_select.ogg");
    }

    void doEvictKey(sf::Keyboard::Key k)
    {
        // Phase 6: player picks which inventory weapon to swap OUT
        if (activeP_ < 0)
        { 
            inp_.phase = 0; 
            return; 
        }
        const Entity& pl = snap_.players[activeP_];
        int n = inp_.evictCount;
        if (n == 0)
        { 
            inp_.phase = 3; 
            inp_.ltsSel = 0; 
            return; 
        }
        if (k == sf::Keyboard::Up   || k == sf::Keyboard::W)   
        {
            inp_.evictSel = (inp_.evictSel - 1 + n) % n;
        }
        if (k == sf::Keyboard::Down || k == sf::Keyboard::S) 
        {
            inp_.evictSel = (inp_.evictSel + 1) % n;
        }
        for (int i = 1; i <= 9 && i <= n; i++)
        {
            if (k == (sf::Keyboard::Key)(sf::Keyboard::Num0 + i) ||
                k == (sf::Keyboard::Key)(sf::Keyboard::Numpad0 + i))
            {
                inp_.evictSel = i - 1;
            }
        }
        if (k == sf::Keyboard::Escape || k == sf::Keyboard::Num0)
        { 
            inp_.phase = 0; 
            return; 
        }
        if (k == sf::Keyboard::Return || k == sf::Keyboard::Space)
        {
            // manually evict the chosen weapon to LTS in shared memory
            int slot = inp_.evictSlots[inp_.evictSel];
            int wid = pl.inventory[slot];
            int sz = WEAPON_SLOT_SIZE[wid];
            sem_wait(&gs_->stateLock);
            Entity* p = &gs_->players[activeP_];
            for (int i = slot; i < slot + sz && i < INVENTORY_SLOTS; i++) 
            {
                p->inventory[i] = 0;
            }
            if (p->longTermStorageCount < MAX_WEAPONS_IN_LTS)
            {
                p->longTermStorage[p->longTermStorageCount++] = wid;
            }
            sem_post(&gs_->stateLock);
            pushLog(("Sent " + std::string(WEAPON_NAME[wid]) + " to LTS").c_str());
            // now go to LTS pick (phase 3)
            inp_.phase = 3; 
            inp_.ltsSel = 0;
        }
        AudioMgr::get().sfx("assets/audio/sfx/menu_select.ogg");
    }

    void doPickupKey(sf::Keyboard::Key k)
    {
        if (k == sf::Keyboard::Y)
        {
            gs_->weaponDrop.decision = 1;
            AudioMgr::get().sfx("assets/audio/sfx/weapon_pickup.ogg");
            inp_.phase = 0;
        } 
        else if (k == sf::Keyboard::N || k == sf::Keyboard::Escape)
        {
            gs_->weaponDrop.decision = 0;
            AudioMgr::get().sfx("assets/audio/sfx/weapon_drop.ogg");
            inp_.phase = 0;
        }
    }

    void doQuitKey(sf::Keyboard::Key k)
    {
        if (k == sf::Keyboard::Y)
        {
            if (gs_->arbiterPid > 0) 
            {
                kill(gs_->arbiterPid, SIGTERM);
            }
            gs_->gameRunning = 0;
        } 
        else if (k == sf::Keyboard::N || k == sf::Keyboard::Escape)
        {
            inp_.phase = 0;
        }
    }

    
    void buildAliveEnemies()
    {
        inp_.aliveN = 0;
        for (int i = 0; i < snap_.nEnemies; i++)
        {
            if (snap_.enemies[i].isAlive) 
            {
                inp_.aliveE[inp_.aliveN++] = i;
            }
        }
    }
    
    void buildWeaponList(const Entity& pl)
    {
        inp_.wCount = 0; 
        inp_.evictCount = 0;
        int visited[INVENTORY_SLOTS] = {};
        for (int i = 0; i < INVENTORY_SLOTS && inp_.wCount < INVENTORY_SLOTS; i++)
        {
            if (!pl.inventory[i] || visited[i]) 
            {
                continue;
            }
            int wid = pl.inventory[i];
            int sz = WEAPON_SLOT_SIZE[wid];
            inp_.wSlot[inp_.wCount++] = i;
            inp_.evictSlots[inp_.evictCount++] = i;
            for (int j = i; j < i + sz && j < INVENTORY_SLOTS; j++) 
            {
                visited[j] = 1;
            }
        }
    }
    
    // weapon SFX helper
    static const char* weaponSfx(int wid)
    {
        if (wid == WEAPON_SOLAR_CORE)   
            return "assets/audio/sfx/weapon_solar_core.ogg";
        if (wid == WEAPON_LUNAR_BLADE)  
            return "assets/audio/sfx/weapon_lunar_blade.ogg";
        if (wid == WEAPON_ECLIPSE_RELIC)
            return "assets/audio/sfx/weapon_eclipse_relic.ogg";
        return "assets/audio/sfx/weapon_generic.ogg";
    }

    void pushLog(const char* msg)
    {
        if (!msg || !msg[0]) 
        {
            return;
        }
        if (!log_.empty() && log_.front().msg == msg) 
        {
            return;
        }
        log_.push_front({std::string(msg), 0.f});
        if ((int)log_.size() > 8) 
        {
            log_.resize(8);
        }

        // trigger SFX based on log content
        std::string m(msg);
        auto has = [&](const char* s)
        { 
            return m.find(s) != std::string::npos; 
        };

        if (has("stunned"))
        {
            AudioMgr::get().sfx("assets/audio/sfx/stun.ogg");
        }
        else if (has("dies") || has("dead") || has("killed"))
        {
            // player or enemy death
            if (has("P") || has("player") || has("Autobots") || has("Optimus") ||
                has("Bumblebee") || has("Arcee") || has("Ratchet"))
            {
                AudioMgr::get().sfx("assets/audio/sfx/player_death.ogg");
            }
            else
            {
                AudioMgr::get().sfx("assets/audio/sfx/enemy_death.ogg");
            }
        }
        else if (has("misses") || has("miss"))
        {
            AudioMgr::get().sfx("assets/audio/sfx/strike_miss.ogg");
        }
        else if (has("heals") || has("heal"))
        {
            AudioMgr::get().sfx("assets/audio/sfx/heal.ogg");
        }
        else if (has("exhaust"))
        {
            AudioMgr::get().sfx("assets/audio/sfx/exhaust_hit.ogg");
        }
        else if (has("Solar Core") || has("solar_core"))
        {
            AudioMgr::get().sfx("assets/audio/sfx/weapon_solar_core.ogg");
        }
        else if (has("Lunar Blade") || has("lunar_blade"))
        {
            AudioMgr::get().sfx("assets/audio/sfx/weapon_lunar_blade.ogg");
        }
        else if (has("Eclipse Relic") || has("eclipse_relic"))
        {
            AudioMgr::get().sfx("assets/audio/sfx/weapon_eclipse_relic.ogg");
        }
        else if (has("uses") || has("weapon") || has("Weapon"))
        {
            AudioMgr::get().sfx("assets/audio/sfx/weapon_generic.ogg");
        }
        else if (has("strikes") || has("hits") || has("dmg"))
        {
            AudioMgr::get().sfx("assets/audio/sfx/attack_hit.ogg");
        }
        else if (has("stamina full") || has("stamina_full"))
        {
            AudioMgr::get().sfx("assets/audio/sfx/stamina_full.ogg");
        }
        else if (has("picks up") || has("pickup"))
        {
            AudioMgr::get().sfx("assets/audio/sfx/weapon_pickup.ogg");
        }
        else if (has("drops") || has("drop"))
        {
            AudioMgr::get().sfx("assets/audio/sfx/weapon_drop.ogg");
        }
    }
    
    void submitAction()
    {
        if (!gs_) 
        {
            return;
        }
        if (gs_->currentTurnIsPlayer != 1 || gs_->currentTurnEntityIndex != activeP_) 
        {
            return;
        }
        struct timespec ts; 
        clock_gettime(CLOCK_REALTIME, &ts); 
        ts.tv_sec += 3;
        if (sem_timedwait(&gs_->actionConsumed, &ts) != 0) 
        {
            return;
        }
        if (gs_->currentTurnIsPlayer != 1 || gs_->currentTurnEntityIndex != activeP_)
        {
            sem_post(&gs_->actionConsumed); 
            return;
        }
        sem_wait(&gs_->stateLock);
        gs_->pendingAction = inp_.act;
        gs_->actionReady = 1;
        sem_post(&gs_->stateLock);
        sem_post(&gs_->actionSubmitted);
    }

    
    void drawHUD(sf::RenderWindow& w)
    {
        rect(w, 0, 0, WIN_W, 42, {0, 0, 0, 190});
        txt(w, "AUTOBOTS", AssetMgr::get().bold(), 16, 10, 10, {255, 120, 80}, true);
        std::string c = "WAVE " + std::to_string(snap_.wave + 1)
                       + "    KILLS: " + std::to_string(snap_.kills) + "/10";
        txtC(w, c, AssetMgr::get().bold(), 16, WIN_W / 2.f, 10, {255, 220, 100});
        txt(w, "DECEPTICONS", AssetMgr::get().bold(), 16, WIN_W - 160.f, 10, {160, 80, 255}, true);
    }

    void drawPlayerPanel(sf::RenderWindow& w)
    {
        int np = snap_.nPlayers;
        float panH = 458.f / std::max(1, np);

        for (int i = 0; i < np; i++)
        {
            const Entity& p = snap_.players[i];
            float py = 44.f + i * panH;
            float ph = panH - 3;
            bool myTurn = (snap_.turnIsPlayer == 1 && snap_.turnIdx == i);
            bool dead = !p.isAlive;
            bool lingering = (dead && pAnim_[i].deathTimer > 0.f);
            if (dead && pAnim_[i].deathDone) continue;   // fully expired — don't draw
            if (dead && !lingering) continue;

            // fade alpha during last second of linger
            sf::Uint8 fadeAlpha = 255;
            if (lingering && pAnim_[i].deathTimer < 1.f)
            {
                fadeAlpha = (sf::Uint8)(pAnim_[i].deathTimer * 255.f);
            }

            // panel background
            sf::Color bg = dead       ? sf::Color(40, 15, 15, 180)
                          : myTurn    ? sf::Color(15, 55, 15, 210)
                                      : sf::Color(8, 8, 28, 180);
            rect(w, 0, py, 470, ph, bg);
            if (myTurn)
            {
                float g = 0.5f + 0.5f * std::sin(pulseT_);
                rect(w, 0, py, 470, ph, {0, 0, 0, 0}, {0, (sf::Uint8)(220 * g), 60, (sf::Uint8)(200 * g)}, 2.f);
            }

            // B2/B4: character sprite — use animation state
            const EntAnim& pa = pAnim_[i];
            std::string state;
            if (dead)                      state = "_dead";
            else if (pa.state == ANIM_ATTACK) state = "_attack";
            else if (pa.state == ANIM_HIT)    state = "_hurt";
            else                           state = "_idle";
            std::string spPath = "assets/sprites/players/" + std::string(PSPRITE[i]) + state + ".png";
            float sprSz = std::min(ph - 10, 140.f);
            drawSprite(w, spPath, 6 + pa.offX, py + (ph - sprSz) / 2.f, sprSz, sprSz, fadeAlpha);

            if (lingering)
            {
                txtC(w, "DEFEATED", AssetMgr::get().bold(), 12, sprSz / 2.f + 6, py + ph / 2.f - 8, {200, 50, 50, fadeAlpha});
                continue;
            }

            // dead overlay on top of sprite
            if (dead)
            {
                drawSprite(w, "assets/sprites/ui/dead_overlay.png",
                           6, py + (ph - sprSz) / 2.f, sprSz, sprSz, 180);
            }

            // stun icon floating above
            if (p.isStunned)
            {
                drawSprite(w, "assets/sprites/ui/stun_icon.png",
                           6, py - 22, 32, 32);
            }

            float ix = sprSz + 14;

            // name + turn indicator
            sf::Color nc = dead ? sf::Color(100, 100, 100) : PCOL[i];
            txt(w, PNAME[i], AssetMgr::get().bold(), 13, ix, py + 4, nc, true);
            if (myTurn)
            {
                txt(w, "► YOUR TURN", AssetMgr::get().bold(), 11, ix + 160, py + 5, {80, 255, 80});
            }
            if (p.isStunned)
            {
                txt(w, "[STUNNED " + std::to_string(p.stunTicksRemaining) + "]",
                    AssetMgr::get().bold(), 11, ix + 160, py + 5, {255, 80, 80});
            }

            // HP bar
            drawBar(w, false, ix, py + 22, 280, 14, p.hp, p.maxHp);
            txt(w, std::to_string(p.hp) + "/" + std::to_string(p.maxHp),
                AssetMgr::get().mono(), 10, ix + 285, py + 22, {200, 230, 200});

            // Stamina bar
            drawBar(w, true, ix, py + 40, 280, 14, p.stamina, p.maxStamina);
            txt(w, std::to_string(p.stamina) + "/" + std::to_string(p.maxStamina),
                AssetMgr::get().mono(), 10, ix + 285, py + 40, {230, 230, 160});

            // Inventory strip — show unique weapons as small icons
            {
                float sx = ix, sy = py + 60;
                int visited[INVENTORY_SLOTS] = {};
                int col = 0;
                for (int j = 0; j < INVENTORY_SLOTS && col < 10; j++)
                {
                    if (!p.inventory[j] || visited[j]) 
                    {
                        continue;
                    }
                    int wid = p.inventory[j];
                    int sz = WEAPON_SLOT_SIZE[wid];
                    for (int jj = j; jj < j + sz && jj < INVENTORY_SLOTS; jj++) 
                    {
                        visited[jj] = 1;
                    }
                    float sx2 = sx + col * 26;
                    // slot background
                    drawSprite(w, "assets/sprites/ui/inventory_slot.png", sx2, sy, 24, 24);
                    // weapon icon
                    if (wid > 0 && WICON[wid][0])
                    {
                        std::string wp = "assets/sprites/weapons/" + std::string(WICON[wid]) + ".png";
                        drawSprite(w, wp, sx2, sy, 24, 24);
                    }
                    // weapon short code
                    txt(w, WEAPON_SHORT[wid], AssetMgr::get().mono(), 7, sx2 + 2, sy + 14, {255, 255, 255, 200});
                    col++;
                }
            }

            // artifact indicators
            float ax = ix;
            if (p.holdsSolarCore)
            {
                txt(w, "[SC]", AssetMgr::get().bold(), 10, ax, py + ph - 16, {255, 200, 0});
            }
            if (p.holdsLunarBlade)
            {
                txt(w, "[LB]", AssetMgr::get().bold(), 10, ax + 36, py + ph - 16, {180, 180, 255});
            }
            if (p.holdsEclipseRelic)
            {
                txt(w, "[ER]", AssetMgr::get().bold(), 10, ax + 72, py + ph - 16, {200, 100, 255});
            }
        }
    }

    void drawEnemyPanel(sf::RenderWindow& w)
    {
        int ne = snap_.nEnemies;
        int alive = 0;
        for (int i = 0; i < ne; i++) 
        {
            if (snap_.enemies[i].isAlive) 
            {
                alive++;
            }
        }
        if (alive == 0 && [&]{ for (int i = 0; i < ne; i++) 
        {
            if (!eAnim_[i].deathDone && eAnim_[i].deathTimer > 0.f) 
            {
                return true;
            }
        } return false; }() == false) 
        {
            return;
        }

        int cols = alive <= 3 ? 1 : alive <= 6 ? 2 : 3; 
        if (cols < 1) 
        {
            cols = 1;
        }
        float cw = 490.f / cols, ch = 150.f;
        int drawn = 0;

        for (int i = 0; i < ne; i++)
        {
            const Entity& e = snap_.enemies[i];
            bool lingering = (!e.isAlive && eAnim_[i].deathTimer > 0.f);
            if (!e.isAlive && eAnim_[i].deathDone) continue;  // fully expired
            if (!e.isAlive && !lingering) continue;
            int col = drawn % cols, row = drawn / cols;
            float ex = 790.f + col * cw, ey = 44.f + row * ch;
            bool myTurn = (snap_.turnIsPlayer == 0 && snap_.turnIdx == i && e.isAlive);

            // fade out during last second of linger
            sf::Uint8 fadeAlpha = 255;
            if (lingering && eAnim_[i].deathTimer < 1.f)
            {
                fadeAlpha = (sf::Uint8)(eAnim_[i].deathTimer * 255.f);
            }

            sf::Color bg = lingering ? sf::Color(8, 8, 8, 100)
                          : myTurn   ? sf::Color(55, 8, 8, 210)
                                     : sf::Color(18, 8, 28, 170);
            rect(w, ex, ey, cw - 4, ch - 4, bg);
            if (myTurn) 
            {
                rect(w, ex, ey, cw - 4, ch - 4, {0, 0, 0, 0}, {255, 50, 50, 200}, 2.f);
            }

            const EntAnim& ea = eAnim_[i];
            std::string state;
            if (!e.isAlive)                 state = "_dead";
            else if (ea.state == ANIM_ATTACK) state = "_attack";
            else if (ea.state == ANIM_HIT)    state = "_hurt";
            else                           state = "_idle";
            std::string sp = getEnemySprite(i, state);
            float sprSz = std::min(ch - 28, 110.f); // slightly bigger
            // mirror enemy sprite so it faces LEFT toward players
            {
                sf::Texture& tex = AssetMgr::get().tex(sp);
                sf::Sprite spr(tex);
                float scaleX = -(sprSz / (float)tex.getSize().x); // negative = mirror
                float scaleY =  sprSz / (float)tex.getSize().y;
                spr.setScale(scaleX, scaleY);
                spr.setPosition(ex + 2 + ea.offX + sprSz, ey + 2); // offset by sprSz due to flip origin
                sf::Color c = spr.getColor(); 
                c.a = fadeAlpha; 
                spr.setColor(c);
                w.draw(spr);
            }

            // blue freeze tint during ultimate
            if (snap_.ultimateActive && e.isAlive)
            {
                rect(w, ex + 2, ey + 2, sprSz, sprSz, {0, 100, 255, 60});
                // countdown: 10 - ultimateTimer
                float remaining = 10.f - snap_.ultimateTimer;
                if (remaining < 0.f) 
                {
                    remaining = 0.f;
                }
                char cd[8]; 
                snprintf(cd, sizeof(cd), "%.0fs", remaining);
                txtC(w, cd, AssetMgr::get().bold(), 16, ex + sprSz / 2.f + 2, ey + sprSz / 2.f - 8, {100, 200, 255});
            }

            if (lingering)
            {
                // DEFEATED label over dead sprite
                txt(w, "DEFEATED", AssetMgr::get().bold(), 12, ex + sprSz + 6, ey + ch / 2.f - 8, {200, 50, 50, fadeAlpha});
                drawn++; 
                continue;
            }

            if (e.isStunned)
            {
                drawSprite(w, "assets/sprites/ui/stun_icon.png", ex + 2, ey - 14, 22, 22);
            }

            float ix2 = ex + sprSz + 6;
            txt(w, "E" + std::to_string(i + 1), AssetMgr::get().bold(), 12,
                ix2, ey + 3, myTurn ? sf::Color(255, 100, 100) : sf::Color(200, 120, 255), true);

            float bw2 = cw - sprSz - 16;
            drawBar(w, false, ix2, ey + 18, bw2, 10, e.hp, e.maxHp);
            txt(w, std::to_string(e.hp) + "/" + std::to_string(e.maxHp),
                AssetMgr::get().mono(), 8, ix2, ey + 30, {210, 180, 180});
            drawBar(w, true, ix2, ey + 42, bw2, 10, e.stamina, e.maxStamina);
            txt(w, std::to_string(e.stamina), AssetMgr::get().mono(), 8, ix2, ey + 54, {210, 210, 150});

            drawn++;
        }
    }

    void drawDivider(sf::RenderWindow& w)
    {
        // Removed texture divider (eyesore) — replaced with clean line
        sf::RectangleShape line({2.f, 460.f});
        line.setPosition(762, 44);
        line.setFillColor({50, 50, 80, 120});
        w.draw(line);
    }

    void drawEventLog(sf::RenderWindow& w)
    {
        float lx = 780, ly = 505, lw = 492, lh = 196;
        rect(w, lx, ly, lw, lh, {0, 0, 0, 170}, {50, 90, 180, 180}, 1.f);
        txt(w, "[ EVENT LOG ]", AssetMgr::get().bold(), 12, lx + 8, ly + 4, {100, 160, 255});
        for (int i = 0; i < (int)log_.size(); i++)
        {
            float alpha = 1.f - log_[i].age * 0.05f;
            if (alpha < 0.1f) 
            {
                alpha = 0.1f;
            }
            sf::Uint8 a = (sf::Uint8)(alpha * 220);
            txt(w, log_[i].msg, AssetMgr::get().mono(), 11, lx + 8, ly + 22 + i * 18, {210, 225, 255, a});
        }
    }

    void drawActionArea(sf::RenderWindow& w)
    {
        float ax = 0, ay = 505, aw = 470, ah = 196;

        if (activeP_ < 0)
        {
            rect(w, ax, ay, aw, ah, {0, 0, 0, 160});
            txtC(w, "~ Energon regenerating ~", AssetMgr::get().main(), 16,
                 ax + aw / 2.f, ay + 88, {80, 180, 255});
            return;
        }

        const Entity& pl = snap_.players[activeP_];
        bool hasWeap = false;
        for (int i = 0; i < INVENTORY_SLOTS; i++) 
        {
            if (pl.inventory[i])
            { 
                hasWeap = true; 
                break; 
            }
        }
        bool hasLts = (pl.longTermStorageCount > 0);
        bool hasUlt = (pl.holdsSolarCore && pl.holdsLunarBlade);

        // panel background
        sf::Sprite pan(AssetMgr::get().tex("assets/sprites/ui/panel_bg.png"));
        pan.setScale(aw / 400.f, ah / 600.f);
        pan.setPosition(ax, ay); 
        w.draw(pan);

        if (inp_.phase == 0)
        {
            // player name header
            txt(w, PNAME[activeP_], AssetMgr::get().bold(), 16, ax + 8, ay + 5, PCOL[activeP_]);

            // 4×2 action button grid
            struct Btn{ const char* label; bool avail; } btns[8] = {
                {"[1] Strike",   true},
                {"[2] Exhaust",  true},
                {"[3] Heal",     true},
                {"[4] Skip",     true},
                {"[5] UseWeapon", hasWeap},
                {"[6] Swap In",  hasLts},
                {"[7] Ultimate", hasUlt},
                {"[8] Quit",     true}
            };
            float bw = (aw - 20) / 2.f - 4, bh = 36;
            for (int i = 0; i < 8; i++)
            {
                int c = i % 2, r = i / 2;
                float bx = ax + 10 + c * (bw + 8);
                float by = ay + 26 + r * (bh + 6);
                bool sel = (inp_.menuSel == i);
                bool dis = !btns[i].avail;
                // background
                sf::Color bgc = dis  ? sf::Color(20, 20, 20, 120)
                               : sel  ? sf::Color(80, 70, 0, 220)
                                      : sf::Color(25, 25, 50, 200);
                sf::Color brd = dis  ? sf::Color(60, 60, 60, 100)
                               : sel  ? sf::Color(255, 220, 0, 255)
                                      : sf::Color(100, 100, 160, 180);
                rect(w, bx, by, bw, bh, bgc, brd, sel ? 2.f : 1.f);
                // text: selected=yellow, normal=white, disabled=grey
                sf::Color tc = dis ? sf::Color(80, 80, 80)
                              : sel ? sf::Color(255, 220, 0)
                                    : sf::Color(255, 255, 255);
                txt(w, btns[i].label, AssetMgr::get().bold(), 14, bx + 8, by + 10, tc);
            }
        } 
        else if (inp_.phase == 1)
        {
            // target selection
            rect(w, ax, ay, aw, ah, {0, 0, 0, 185});
            txt(w, "SELECT TARGET  [0/ESC = back]", AssetMgr::get().bold(), 15, ax + 8, ay + 6, {255, 180, 80});
            for (int i = 0; i < inp_.aliveN; i++)
            {
                int ei = inp_.aliveE[i];
                bool sel = (i == inp_.targetSel);
                if (sel) 
                {
                    rect(w, ax + 6, ay + 28 + i * 24, aw - 12, 22, {60, 50, 0, 180});
                }
                std::string lb = "[" + std::to_string(i + 1) + "] Enemy "
                    + std::to_string(ei + 1) + "  HP:" + std::to_string(snap_.enemies[ei].hp)
                    + "/" + std::to_string(snap_.enemies[ei].maxHp);
                txt(w, lb, AssetMgr::get().mono(), 14, ax + 12, ay + 31 + i * 24,
                    sel ? sf::Color(255, 220, 0) : sf::Color(255, 255, 255));
            }
        } 
        else if (inp_.phase == 2)
        {
            // weapon selection
            rect(w, ax, ay, aw, ah, {0, 0, 0, 185});
            txt(w, "SELECT WEAPON  [0/ESC = back]", AssetMgr::get().bold(), 15, ax + 8, ay + 6, {80, 220, 255});
            for (int i = 0; i < inp_.wCount; i++)
            {
                int wid = pl.inventory[inp_.wSlot[i]];
                bool sel = (i == inp_.weaponSel);
                if (sel) 
                {
                    rect(w, ax + 6, ay + 28 + i * 24, aw - 12, 22, {0, 50, 0, 180});
                }
                std::string lb = "[" + std::to_string(i + 1) + "] " + WEAPON_NAME[wid]
                    + "  DMG:" + std::to_string(WEAPON_DAMAGE[wid])
                    + "  SZ:" + std::to_string(WEAPON_SLOT_SIZE[wid]);
                txt(w, lb, AssetMgr::get().mono(), 14, ax + 12, ay + 31 + i * 24,
                    sel ? sf::Color(255, 220, 0) : sf::Color(255, 255, 255));
            }
        } 
        else if (inp_.phase == 6)
        {
            // evict selection — choose what to swap OUT
            rect(w, ax, ay, aw, ah, {0, 0, 0, 185});
            txt(w, "SWAP OUT — choose weapon to send to LTS:", AssetMgr::get().bold(), 14, ax + 8, ay + 6, {255, 160, 40});
            txt(w, "[0/ESC = back]", AssetMgr::get().mono(), 11, ax + 8, ay + 22, {150, 150, 180});
            for (int i = 0; i < inp_.evictCount; i++)
            {
                int wid = pl.inventory[inp_.evictSlots[i]];
                bool sel = (i == inp_.evictSel);
                if (sel) 
                {
                    rect(w, ax + 6, ay + 38 + i * 24, aw - 12, 22, {80, 40, 0, 180});
                }
                std::string lb = "[" + std::to_string(i + 1) + "] " + WEAPON_NAME[wid]
                    + "  DMG:" + std::to_string(WEAPON_DAMAGE[wid])
                    + "  SZ:" + std::to_string(WEAPON_SLOT_SIZE[wid]);
                txt(w, lb, AssetMgr::get().mono(), 14, ax + 12, ay + 41 + i * 24,
                    sel ? sf::Color(255, 220, 0) : sf::Color(255, 255, 255));
            }
        } 
        else if (inp_.phase == 3)
        {
            // LTS selection
            rect(w, ax, ay, aw, ah, {0, 0, 0, 185});
            txt(w, "LONG-TERM STORAGE  [0/ESC = back]", AssetMgr::get().bold(), 15, ax + 8, ay + 6, {100, 180, 255});
            for (int i = 0; i < pl.longTermStorageCount; i++)
            {
                int wid = pl.longTermStorage[i];
                bool sel = (i == inp_.ltsSel);
                if (sel) 
                {
                    rect(w, ax + 6, ay + 28 + i * 24, aw - 12, 22, {0, 0, 50, 180});
                }
                std::string lb = "[" + std::to_string(i + 1) + "] " + WEAPON_NAME[wid]
                    + "  DMG:" + std::to_string(WEAPON_DAMAGE[wid]);
                txt(w, lb, AssetMgr::get().mono(), 14, ax + 12, ay + 31 + i * 24,
                    sel ? sf::Color(255, 220, 0) : sf::Color(255, 255, 255));
            }
        } 
        else if (inp_.phase == 4)
        {
            // weapon pickup
            rect(w, ax, ay, aw, ah, {20, 10, 0, 200}, {255, 200, 0}, 2.f);
            txt(w, "!!! WEAPON DROPPED !!!", AssetMgr::get().bold(), 16, ax + 10, ay + 8, {255, 220, 0});
            if (snap_.dropWeapon > 0 && snap_.dropWeapon < WEAPON_COUNT)
            {
                txt(w, std::string(WEAPON_NAME[snap_.dropWeapon])
                    + "  DMG:" + std::to_string(WEAPON_DAMAGE[snap_.dropWeapon])
                    + "  SZ:" + std::to_string(WEAPON_SLOT_SIZE[snap_.dropWeapon]),
                    AssetMgr::get().main(), 14, ax + 10, ay + 36, {255, 200, 150});
                // weapon icon preview
                if (WICON[snap_.dropWeapon][0])
                {
                    drawSprite(w, "assets/sprites/weapons/" + std::string(WICON[snap_.dropWeapon]) + ".png",
                               ax + 10, ay + 60, 64, 64);
                }
            }
            txt(w, "Pick it up?    [Y] Yes      [N] No",
                AssetMgr::get().bold(), 14, ax + 10, ay + 135, {200, 255, 200});
        } 
        else if (inp_.phase == 5)
        {
            // quit confirm
            rect(w, ax, ay, aw, ah, {20, 0, 0, 220}, {255, 50, 50}, 2.f);
            txtC(w, "Retreat from battle?", AssetMgr::get().bold(), 18, ax + aw / 2.f, ay + 60, {255, 100, 100});
            txtC(w, "[Y] Yes — surrender", AssetMgr::get().main(), 15, ax + aw / 2.f, ay + 100, {255, 200, 200});
            txtC(w, "[N] No  — keep fighting!", AssetMgr::get().main(), 15, ax + aw / 2.f, ay + 126, {200, 255, 200});
        }
    }

    
    // weapon projectile
    struct Projectile {
        bool   active = false;
        int    weaponId = 0;
        float  x = 0, y = 0;       // current pos
        float  sx = 0, sy = 0;     // start pos
        float  tx = 0, ty = 0;     // target pos
        float  t = 0;              // 0→1 progress
        float  dur = 0.5f;
    } proj_;

    GameState* gs_;
    Snap snap_{};
    int  activeP_ = -1;
    int  prevWave_ = -1;
    float pulseT_ = 0, animT_ = 0, ultimaAlpha_ = 0;
    int   animFrame_ = 0;

    struct LogEntry{ std::string msg; float age; };
    std::deque<LogEntry> log_;
    char lastLog_[MAX_PLAYERS + MAX_ENEMIES][ACTION_LOG_LEN] = {};
};

static GameState* gGs = nullptr;

void sigHandler(int sig)
{
    if (sig == SIGTERM || sig == SIGINT)
    {
        if (gGs) 
        {
            gGs->gameRunning = 0;
        }
    }
}

class Renderer {
public:
    explicit Renderer(GameState* gs): gs_(gs) {}

    void run()
    {
        sf::RenderWindow win(
            sf::VideoMode(WIN_W, WIN_H),
            "Chrono Rift  —  SFML GUI",
            sf::Style::Titlebar | sf::Style::Close);
        win.setFramerateLimit(FPS);
        win.setMouseCursorVisible(true);
        // Disable joystick polling to prevent /dev/input spam in Docker
        sf::Joystick::update();

        setScreen(new MenuScreen());

        sf::Clock clk;
        bool gameOverShown = false;
        while (win.isOpen())
        {
            sf::Event e;
            while (win.pollEvent(e))
            {
                if (e.type == sf::Event::Closed)
                {
                    gs_->gameRunning = 0; 
                    win.close(); 
                    return;
                }
                if (cur_) 
                {
                    cur_->handleEvent(e);
                }
            }

            float dt = clk.restart().asSeconds();
            if (dt > 0.05f) 
            {
                dt = 0.05f;
            }

            Snap snap = takeSnap(gs_);

            // show win/lose screen when game ends, even if gameRunning==0
            if (!snap.running && !gameOverShown)
            {
                gameOverShown = true;
                if (snap.result == 2)
                { 
                    auto* s = new VictoryScreen(); 
                    s->onEnter(); 
                    setScreen(s, false); 
                }
                else if (snap.result == 1)
                { 
                    auto* s = new DefeatScreen(); 
                    s->onEnter(); 
                    setScreen(s, false); 
                }
            }

            if (cur_) 
            {
                cur_->update(dt, snap);
            }

            // handle transitions
            if (cur_ && cur_->wantTransition)
            {
                int next = cur_->nextScreen;
                switch (next)
                {
                    case 0: 
                        setScreen(new BattleScreen(gs_)); 
                        break;
                    case 1: 
                    { 
                        auto* s = new VictoryScreen(); 
                        s->onEnter(); 
                        setScreen(s, false); 
                        break; 
                    }
                    case 2: 
                    { 
                        auto* s = new DefeatScreen();  
                        s->onEnter(); 
                        setScreen(s, false); 
                        break; 
                    }
                    case 3:
                        if (gs_->arbiterPid > 0) 
                        {
                            kill(gs_->arbiterPid, SIGTERM);
                        }
                        gs_->gameRunning = 0; 
                        win.close(); 
                        return;
                    case 4: 
                    { 
                        auto* s = new WaveScreen(); 
                        setScreen(s); 
                        break; 
                    }
                }
            }

            win.clear({4, 4, 14});
            if (cur_) 
            {
                cur_->draw(win);
            }
            win.display();
        }
        delete cur_;
    }

private:
    void setScreen(Screen* s, bool callEnter = true)
    {
        delete cur_;
        cur_ = s;
        if (callEnter && cur_) 
        {
            cur_->onEnter();
        }
    }
    
    GameState* gs_;
    Screen*    cur_ = nullptr;
};

int main()
{
    signal(SIGTERM, sigHandler);
    signal(SIGINT, sigHandler);

    // Suppress joystick polling spam in Docker (no /dev/input devices)
    setenv("SDL_JOYSTICK_DEVICE", "", 1);

    // Suppress ALSA/OpenAL error spam when no audio device exists
    setenv("ALSA_CONFIG_PATH", "/dev/null", 1);
    setenv("AUDIODEV",         "null",       1);
    setenv("SDL_AUDIODRIVER",  "dummy",      1);

    sleep(1); // let arbiter finish shm setup

    int fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (fd < 0)
    { 
        perror("hip_gui shm_open"); 
        return 1; 
    }
    GameState* gs = (GameState*)mmap(NULL, sizeof(GameState),
                                     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (gs == MAP_FAILED)
    { 
        perror("hip_gui mmap"); 
        return 1; 
    }
    close(fd);
    gGs = gs;

    // pre-load fonts so first frame is instant
    AssetMgr::get().main();
    AssetMgr::get().bold();
    AssetMgr::get().mono();

    Renderer r(gs);
    r.run();

    munmap(gs, sizeof(GameState));
    return 0;
}
