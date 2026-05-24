// Kasim Zeeshan Alvi || 24i0549 || CS-A
// Abdullah Junaid    || 24i0569 || CS-A
// shared.h - shared memory layout, constants, structs used by all three processes

#pragma once
#include <semaphore.h>

#define SHM_NAME              "/chrono_rift_shm"
#define ROLL_NUMBER           549
#define MAX_PLAYERS           4
#define MAX_ENEMIES           9
#define INVENTORY_SLOTS       20
#define MAX_WEAPONS_IN_LTS    20
#define ACTION_LOG_LEN        80
#define WEAPON_COUNT          10

// weapon ids
#define WEAPON_NONE            0
#define WEAPON_SOLAR_CORE      1
#define WEAPON_LUNAR_BLADE     2
#define WEAPON_IRON_HALBERD    3
#define WEAPON_VENOM_DAGGER    4
#define WEAPON_THUNDERSTAFF    5
#define WEAPON_OBSIDIAN_AXE    6
#define WEAPON_FROSTBOW        7
#define WEAPON_SPLINTER_STICK  8
#define WEAPON_ECLIPSE_RELIC   9

// slot size and damage per weapon (index = weapon id)
static const int  WEAPON_SLOT_SIZE[WEAPON_COUNT] = {0,10,10,7,4,6,5,6,2,8};
static const int  WEAPON_DAMAGE   [WEAPON_COUNT] = {0,95,90,55,30,50,45,48,12,100};
static const char WEAPON_NAME[WEAPON_COUNT][20]  = {"None","Solar Core","Lunar Blade","Iron Halberd","Venom Dagger","Thunderstaff","Obsidian Axe","Frostbow","Splinter Stick","Eclipse Relic"};
static const char WEAPON_SHORT[WEAPON_COUNT][4] = {"--","SC","LB","IH","VD","TS","OA","FB","SS","ER"};

// all valid player actions
typedef enum {
    ACTION_NONE = 0,
    ACTION_STRIKE,
    ACTION_EXHAUST,
    ACTION_USE_WEAPON,
    ACTION_SWAP_IN,
    ACTION_HEAL,
    ACTION_SKIP,
    ACTION_ULTIMATE
} ActionType;

// action submitted by hip or asp into pendingAction
typedef struct {
    int        sourceEntityIndex;
    int        isPlayer;
    ActionType actionType;
    int        targetIndex;
    int        weaponStartSlot;
    int        ltsIndex;
} Action;

// one player or enemy
typedef struct {
    int isAlive;
    int shouldExit;
    int hp;
    int maxHp;
    int damage;
    int speed;
    int stamina;
    int maxStamina;
    int isStunned;
    int stunTicksRemaining;

    int inventory[INVENTORY_SLOTS];
    int longTermStorage[MAX_WEAPONS_IN_LTS];
    int longTermStorageCount;

    int holdsSolarCore;
    int holdsLunarBlade;
    int holdsEclipseRelic;
    int waitingForSolarCore;
    int waitingForLunarBlade;
    int waitingForEclipseRelic;

    char actionLog[ACTION_LOG_LEN];
} Entity;

// tracks who holds each exclusive artifact
typedef struct {
    int solarCoreHeldBy;
    int lunarBladeHeldBy;
    int eclipseRelicHeldBy;
    int eclipseRelicExists;
    sem_t artifactTableLock;
} ArtifactTable;

// weapon drop state written by arbiter read by hip
typedef struct {
    int pending;
    int weaponId;
    int forPlayerIndex;
    int decision;
} WeaponDropState;

// ui state for the active player menu
typedef struct {
    int activePlayer;
    int showMenu;
    int menuSel;
    int showTargetMenu;
    int targetSel;
    int showWeaponMenu;
    int weaponMenuSel;
    int showLtsMenu;
    int ltsSel;
    int showPickup;
    int showWaveBanner;
    int waveBannerNum;
    sem_t uiLock;
} UIState;

// entire game state in shared memory
typedef struct {
    sem_t stateLock;
    sem_t actionSubmitted;
    sem_t actionConsumed;

    int arbiterPid;
    int hipPid;
    int aspPid;

    int gameRunning;
    int gameResult;
    int waveNumber;
    int totalEnemiesKilled;
    int currentTurnEntityIndex;
    int currentTurnIsPlayer;

    int    numberOfPlayers;
    int    numberOfEnemies;
    Entity players[MAX_PLAYERS];
    Entity enemies[MAX_ENEMIES];

    Action pendingAction;
    int    actionReady;

    ArtifactTable   artifacts;
    WeaponDropState weaponDrop;

    int   ultimateAbilityActive;
    float ultimateTimer;

    UIState ui;
} GameState;

// stamina tick = 1 second so 3 ticks = exactly 3 second stun as per spec
#define STAMINA_TICK_MS 1000
