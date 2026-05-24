// Kasim Zeeshan Alvi || 24i0549 || CS-A
// Abdullah Junaid    || 24i0569 || CS-A
// hip.cpp - human interfacing process: ncurses tui, per-player pthreads, dedicated render pthread

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <ncurses.h>
#include "shared.h"

typedef struct {
    // game running state
    int gameRunning, gameResult;
    int waveNumber, totalEnemiesKilled;
    int currentTurnEntityIndex, currentTurnIsPlayer;
    int numberOfPlayers, numberOfEnemies;
    int ultimateAbilityActive;
    // entities — only plain data
    Entity players[MAX_PLAYERS];   // Entity has no sem_t — safe
    Entity enemies[MAX_ENEMIES];
    // weapon drop
    int dropPending, dropWeaponId, dropForPlayer;
} RenderSnap;

static RenderSnap rsnap;
static pthread_mutex_t rsnapMtx = PTHREAD_MUTEX_INITIALIZER; // kept for drawScreen reads
// A6: ncurses is not thread-safe — all ncurses calls must be under this mutex
static pthread_mutex_t ncursesMtx = PTHREAD_MUTEX_INITIALIZER;
#define NCURSES_LOCK   pthread_mutex_lock(&ncursesMtx)
#define NCURSES_UNLOCK pthread_mutex_unlock(&ncursesMtx)

static void takeSnapshot(GameState *s) {
    sem_wait(&s->stateLock);
    rsnap.gameRunning           = s->gameRunning;
    rsnap.gameResult            = s->gameResult;
    rsnap.waveNumber            = s->waveNumber;
    rsnap.totalEnemiesKilled    = s->totalEnemiesKilled;
    rsnap.currentTurnEntityIndex= s->currentTurnEntityIndex;
    rsnap.currentTurnIsPlayer   = s->currentTurnIsPlayer;
    rsnap.numberOfPlayers       = s->numberOfPlayers;
    rsnap.numberOfEnemies       = s->numberOfEnemies;
    rsnap.ultimateAbilityActive = s->ultimateAbilityActive;
    rsnap.dropPending           = s->weaponDrop.pending;
    rsnap.dropWeaponId          = s->weaponDrop.weaponId;
    rsnap.dropForPlayer         = s->weaponDrop.forPlayerIndex;
    
    for (int i = 0; i < MAX_PLAYERS; i++) 
    {
        rsnap.players[i] = s->players[i];
    }
    for (int i = 0; i < MAX_ENEMIES; i++) 
    {
        rsnap.enemies[i] = s->enemies[i];
    }
    sem_post(&s->stateLock);
}

#define TITLE_ROW   0
#define SEP1_ROW    1
#define STATS_TOP   2
#define STATS_ROWS  22       // rows 2-23 inclusive
#define SEP2_ROW    24
#define LOG_TOP     25
#define LOG_ROWS    4
#define SEP3_ROW    29
#define MENU_TOP    30
#define MENU_ROWS   10
#define SEP4_ROW    40
#define HELP_ROW    41
#define MIN_ROWS    43
#define MIN_COLS    100      // need wider for 2-col menu

#define CP_TITLE        1
#define CP_OPTIMUS      2
#define CP_BUMBLEBEE    3
#define CP_ARCEE        4
#define CP_RATCHET      5
#define CP_DECEPTICON   6
#define CP_HP_HIGH      7
#define CP_HP_LOW       8
#define CP_STAMINA_BAR  9
#define CP_BORDER       10
#define CP_DEAD         11
#define CP_HIGHLIGHT    12
#define CP_WEAPON       13
#define CP_LOG          14
#define CP_TURN         15
#define CP_ULTIMATE     16
#define CP_UNAVAIL      17
#define CP_OPTIMUS_BLUE CP_BORDER

static const int  PLAYER_CP[MAX_PLAYERS]    = {CP_OPTIMUS,CP_BUMBLEBEE,CP_ARCEE,CP_RATCHET};
static const char *PLAYER_NAME[MAX_PLAYERS] = {"Optimus Prime","Bumblebee","Arcee","Ratchet"};
static const char *PLAYER_SHORT[MAX_PLAYERS]= {"OPTIMUS","BUMBLE ","ARCEE  ","RATCH  "};

GameState *gameState = NULL;

#define KEYBUF_SIZE 64
static int             keyBuf[KEYBUF_SIZE];
static int             keyHead=0, keyTail=0;
static pthread_mutex_t keyMtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  keyCond= PTHREAD_COND_INITIALIZER;
static volatile int    activeConsumer = -1;

static void keyPush(int k) {
    pthread_mutex_lock(&keyMtx);
    int next = (keyHead + 1) % KEYBUF_SIZE;
    if (next != keyTail) 
    {
        keyBuf[keyHead] = k;
        keyHead = next;
        pthread_cond_broadcast(&keyCond);
    }
    pthread_mutex_unlock(&keyMtx);
}

static int keyPop(int pi, int timeout_ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) 
    {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&keyMtx);
    while (keyHead == keyTail || activeConsumer != pi) 
    {
        if (pthread_cond_timedwait(&keyCond, &keyMtx, &ts) != 0) 
        {
            pthread_mutex_unlock(&keyMtx);
            return ERR;
        }
    }
    int k = keyBuf[keyTail];
    keyTail = (keyTail + 1) % KEYBUF_SIZE;
    pthread_mutex_unlock(&keyMtx);
    return k;
}

static void keyFlush() {
    pthread_mutex_lock(&keyMtx);
    keyHead = 0;
    keyTail = 0;
    pthread_mutex_unlock(&keyMtx);
}

static void setActiveConsumer(int pi) {
    pthread_mutex_lock(&keyMtx);
    activeConsumer = pi;
    pthread_cond_broadcast(&keyCond);
    pthread_mutex_unlock(&keyMtx);
}

static volatile int keyReaderRunning = 0; // unused, kept for compat
void *keyReaderThread(void *arg) { 
    (void)arg;
    return NULL; 
}

#define LOG_BUF_SIZE 5
static char            eventLog[LOG_BUF_SIZE][ACTION_LOG_LEN];
static int             logHead=0;
static pthread_mutex_t logMtx=PTHREAD_MUTEX_INITIALIZER;

static void pushLog(const char *msg) {
    if (!msg || !msg[0]) 
    {
        return;
    }
    pthread_mutex_lock(&logMtx);
    snprintf(eventLog[logHead % LOG_BUF_SIZE], ACTION_LOG_LEN, "%s", msg);
    logHead++;
    pthread_mutex_unlock(&logMtx);
}

typedef struct {
    int activePlayer;
    int showMenu,      menuSel;
    int showTargetMenu,targetSel;
    int showWeaponMenu,weaponMenuSel;
    int showLtsMenu,   ltsSel;
    int showPickup;
    int showQuitConfirm;
    int showRegenMsg;
    int showWaveBanner;   // C2: flash NEW WAVE banner for 2s
    int waveBannerNum;
} UISnap;

static UISnap          ui;
static pthread_mutex_t uiMtx = PTHREAD_MUTEX_INITIALIZER;

#define UI_LOCK   pthread_mutex_lock(&uiMtx)
#define UI_UNLOCK pthread_mutex_unlock(&uiMtx)

static GameState *attachShm() {
    int fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (fd < 0) 
    {
        perror("hip shm_open");
        exit(1);
    }
    GameState *s = (GameState*)mmap(NULL, sizeof(GameState), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (s == MAP_FAILED) 
    {
        perror("hip mmap");
        exit(1);
    }
    close(fd);
    return s;
}

static int findContiguousSlots(int *inv, int size) {
    for (int i = 0; i <= INVENTORY_SLOTS - size; i++)
    {
        int ok = 1;
        for (int j = i; j < i + size; j++)
        {
            if (inv[j])
            {
                ok = 0;
                break;
            }
        }
        if (ok)
        {
            return i;
        }
    }
    return -1;
}

static int getInventoryWeapons(Entity *p, int *startSlots) {
    int count = 0;
    int visited[INVENTORY_SLOTS] = {};
    for (int i = 0; i < INVENTORY_SLOTS; i++) 
    {
        if (p->inventory[i] && !visited[i]) 
        {
            startSlots[count++] = i;
            int wid = p->inventory[i];
            int sz = WEAPON_SLOT_SIZE[wid];
            for (int j = i; j < i + sz && j < INVENTORY_SLOTS; j++) 
            {
                visited[j] = 1;
            }
        }
    }
    return count;
}

static int playerHasUltimate(Entity *p) {
    int sc = 0, lb = 0;
    for (int i = 0; i < INVENTORY_SLOTS; i++) 
    {
        if (p->inventory[i] == WEAPON_SOLAR_CORE)
        {
            sc = 1;
        }
        if (p->inventory[i] == WEAPON_LUNAR_BLADE)
        {
            lb = 1;
        }
    }
    return sc && lb;
}

static void drawHLine(int row, int cols) 
{ 
    attron(COLOR_PAIR(CP_BORDER));
    mvhline(row, 0, ACS_HLINE, cols);
    attroff(COLOR_PAIR(CP_BORDER));
}

static void drawVLine(int r, int h, int c)
{ 
    attron(COLOR_PAIR(CP_BORDER));
    mvvline(r, c, ACS_VLINE, h);
    attroff(COLOR_PAIR(CP_BORDER));
}

static void drawBar(int row, int col, int width, int cur, int max, int cpFill) {
    if (max <= 0)
    {
        max = 1;
    }
    int f = (cur * width) / max;
    if (f > width)
    {
        f = width;
    }
    if (f < 0)
    {
        f = 0;
    }
    attron(COLOR_PAIR(cpFill) | A_BOLD);
    for (int i = 0; i < f; i++)
    {
        mvaddch(row, col + i, '|');
    }
    attroff(COLOR_PAIR(cpFill) | A_BOLD);
    attron(COLOR_PAIR(CP_BORDER) | A_DIM);
    for (int i = f; i < width; i++)
    {
        mvaddch(row, col + i, '.');
    }
    attroff(COLOR_PAIR(CP_BORDER) | A_DIM);
}

static void drawEnemies(RenderSnap *s, int right, int div, int cols, int barW) {
    int alive_count = 0;
    for (int i = 0; i < s->numberOfEnemies; i++)
    {
        if (s->enemies[i].isAlive)
        {
            alive_count++;
        }
    }
    int twoCol = (alive_count > 7);
    int halfW = (cols - div - 2) / 2;
    int drawn = 0;
    for (int i = 0; i < s->numberOfEnemies; i++) 
    {
        Entity *e = &s->enemies[i];
        // Calculate row based on drawn index, not absolute index — skip dead silently
        if (!e->isAlive)
        {
            continue;
        }
        int cx, cy;
        if (twoCol) 
        { 
            cx = (drawn % 2 == 0) ? right : right + halfW;
            cy = STATS_TOP + 1 + (drawn / 2) * 3;
        }
        else        
        { 
            cx = right;
            cy = STATS_TOP + 1 + drawn * 3;
        }
        // Hard clip: never draw past stats section
        if (cy + 2 > STATS_TOP + STATS_ROWS - 2)
        {
            break;
        }
        int bw = twoCol ? (barW - 3) : barW;
        int myTurn = (s->currentTurnIsPlayer == 0 && s->currentTurnEntityIndex == i);
        if (myTurn) 
        {
            attron(COLOR_PAIR(CP_TURN) | A_BOLD);
            mvprintw(cy, cx, "E%-2d<ACT", i + 1);
            attroff(COLOR_PAIR(CP_TURN) | A_BOLD);
        } 
        else 
        {
            attron(COLOR_PAIR(CP_DECEPTICON) | A_BOLD);
            mvprintw(cy, cx, "E%-2d", i + 1);
            attroff(COLOR_PAIR(CP_DECEPTICON) | A_BOLD);
            if (e->isStunned) 
            {
                attron(COLOR_PAIR(CP_HP_LOW));
                mvprintw(cy, cx + 4, "[S%d]", e->stunTicksRemaining);
                attroff(COLOR_PAIR(CP_HP_LOW));
            }
        }
        // HP row — always draw on cy+1
        int hpCp = (e->hp * 3 > e->maxHp) ? CP_HP_HIGH : CP_HP_LOW;
        attron(COLOR_PAIR(CP_DECEPTICON));
        mvprintw(cy + 1, cx, "HP");
        attroff(COLOR_PAIR(CP_DECEPTICON));
        mvaddch(cy + 1, cx + 2, '[');
        drawBar(cy + 1, cx + 3, bw, e->hp, e->maxHp, hpCp);
        mvprintw(cy + 1, cx + 3 + bw, "]%-4d", e->hp);
        // ST row
        attron(COLOR_PAIR(CP_DECEPTICON));
        mvprintw(cy + 2, cx, "ST");
        attroff(COLOR_PAIR(CP_DECEPTICON));
        mvaddch(cy + 2, cx + 2, '[');
        drawBar(cy + 2, cx + 3, bw, e->stamina, e->maxStamina, CP_STAMINA_BAR);
        mvprintw(cy + 2, cx + 3 + bw, "]%-4d", e->stamina);
        drawn++;
    }
}

static void drawScreen() {
    
    // Copy both data structs first, then draw with zero locks held
    RenderSnap s;
    pthread_mutex_lock(&rsnapMtx);
    s = rsnap;
    pthread_mutex_unlock(&rsnapMtx);
    UISnap u;
    UI_LOCK;
    u = ui;
    UI_UNLOCK;

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    if (rows < MIN_ROWS || cols < MIN_COLS) 
    {
        erase();
        attron(COLOR_PAIR(CP_HP_LOW) | A_BOLD);
        mvprintw(rows/2, 0, "Terminal too small! Need %dx%d — currently %dx%d. Please resize.", MIN_COLS, MIN_ROWS, cols, rows);
        attroff(COLOR_PAIR(CP_HP_LOW) | A_BOLD);
        wnoutrefresh(stdscr);
        doupdate();
        return;
    }
    erase();

    int div = cols / 2, left = 2, right = div + 2, barW = 13;

    // Title
    attron(COLOR_PAIR(CP_TITLE) | A_BOLD);
    mvprintw(TITLE_ROW, (cols - 39) / 2, "CHRONO RIFT  |  Autobots vs Decepticons");
    attroff(COLOR_PAIR(CP_TITLE) | A_BOLD);
    drawHLine(SEP1_ROW, cols);

    // Column headers
    attron(COLOR_PAIR(CP_OPTIMUS) | A_BOLD);
    mvprintw(STATS_TOP, left, "AUTOBOTS");
    attroff(COLOR_PAIR(CP_OPTIMUS) | A_BOLD);
    attron(COLOR_PAIR(CP_DECEPTICON) | A_BOLD);
    mvprintw(STATS_TOP, right, "DECEPTICONS  Kills:%d/10  Wave:%d", s.totalEnemiesKilled, s.waveNumber + 1);
    attroff(COLOR_PAIR(CP_DECEPTICON) | A_BOLD);
    drawVLine(STATS_TOP, STATS_ROWS, div);

    // Players — 5 rows each, fits 4 players in 20 rows
    for (int i = 0; i < s.numberOfPlayers; i++) 
    {
        Entity *p = &s.players[i];
        int pr = STATS_TOP + 1 + i * 5;
        int cp = PLAYER_CP[i];
        if (!p->isAlive) 
        {
            attron(COLOR_PAIR(CP_DEAD) | A_DIM);
            mvprintw(pr, left, "%-14s [DEAD]", PLAYER_SHORT[i]);
            attroff(COLOR_PAIR(CP_DEAD) | A_DIM);
            continue;
        }
        int myTurn = (s.currentTurnIsPlayer == 1 && s.currentTurnEntityIndex == i);
        if (myTurn) 
        {
            attron(COLOR_PAIR(CP_TURN) | A_BOLD);
            mvprintw(pr, left, "%-7s >>> YOUR TURN <<<", PLAYER_SHORT[i]);
            attroff(COLOR_PAIR(CP_TURN) | A_BOLD);
        } 
        else if (i == 0) 
        {
            attron(COLOR_PAIR(CP_OPTIMUS) | A_BOLD);
            mvprintw(pr, left, "OPTIMUS");
            attroff(COLOR_PAIR(CP_OPTIMUS) | A_BOLD);
            attron(COLOR_PAIR(CP_OPTIMUS_BLUE) | A_BOLD);
            mvprintw(pr, left + 8, "PRIME");
            attroff(COLOR_PAIR(CP_OPTIMUS_BLUE) | A_BOLD);
        } 
        else 
        {
            attron(COLOR_PAIR(cp) | A_BOLD);
            mvprintw(pr, left, "%-7s", PLAYER_SHORT[i]);
            attroff(COLOR_PAIR(cp) | A_BOLD);
        }
        if (p->isStunned) 
        {
            attron(COLOR_PAIR(CP_HP_LOW) | A_BOLD);
            mvprintw(pr, left + 15, "[STUNNED %d]", p->stunTicksRemaining);
            attroff(COLOR_PAIR(CP_HP_LOW) | A_BOLD);
        }
        int hpCp = (p->hp * 3 > p->maxHp) ? CP_HP_HIGH : CP_HP_LOW;
        attron(COLOR_PAIR(cp));
        mvprintw(pr + 1, left, "HP");
        attroff(COLOR_PAIR(cp));
        mvaddch(pr + 1, left + 2, '[');
        drawBar(pr + 1, left + 3, barW, p->hp, p->maxHp, hpCp);
        mvprintw(pr + 1, left + 3 + barW, "] %d/%d", p->hp, p->maxHp);
        attron(COLOR_PAIR(cp));
        mvprintw(pr + 2, left, "ST");
        attroff(COLOR_PAIR(cp));
        mvaddch(pr + 2, left + 2, '[');
        drawBar(pr + 2, left + 3, barW, p->stamina, p->maxStamina, CP_STAMINA_BAR);
        mvprintw(pr + 2, left + 3 + barW, "] %d/%d", p->stamina, p->maxStamina);
        attron(COLOR_PAIR(CP_WEAPON));
        mvprintw(pr + 3, left, "INV:");
        int wc = left + 6;
        int lastwid = -1;
        for (int sl = 0; sl < INVENTORY_SLOTS && wc < div - 3; sl++) 
        { 
            int wid = p->inventory[sl];
            if (wid != lastwid)
            {
                mvprintw(pr + 3, wc, "%s", WEAPON_SHORT[wid]);
                wc += 3;
            }
            lastwid = wid;
        }
        attroff(COLOR_PAIR(CP_WEAPON));
    }

    drawEnemies(&s, right, div, cols, barW);
    if (s.ultimateAbilityActive) 
    { 
        attron(COLOR_PAIR(CP_ULTIMATE) | A_BOLD);
        mvprintw(STATS_TOP + STATS_ROWS - 1, right, "*** ULTIMATE — DECEPTICONS FROZEN ***");
        attroff(COLOR_PAIR(CP_ULTIMATE) | A_BOLD);
    }
    drawHLine(SEP2_ROW, cols);

    // Event log
    attron(COLOR_PAIR(CP_BORDER) | A_BOLD);
    mvprintw(LOG_TOP, left, "[ EVENT LOG ]");
    attroff(COLOR_PAIR(CP_BORDER) | A_BOLD);
    pthread_mutex_lock(&logMtx);
    int logStart = (logHead >= LOG_ROWS) ? (logHead - LOG_ROWS) : 0;
    for (int i = 0; i < LOG_ROWS; i++) 
    {
        int idx = (logStart + i) % LOG_BUF_SIZE;
        if (logHead == 0 || (logStart + i) >= logHead)
        {
            break;
        }
        attron(COLOR_PAIR(CP_LOG) | A_DIM);
        mvprintw(LOG_TOP + 1 + i, left + 2, "%-*.*s", cols - 6, cols - 6, eventLog[idx]);
        attroff(COLOR_PAIR(CP_LOG) | A_DIM);
    }
    pthread_mutex_unlock(&logMtx);
    drawHLine(SEP3_ROW, cols);

    
    int ap = u.activePlayer;

    // Quit confirm
    if (u.showQuitConfirm) 
    {
        attron(COLOR_PAIR(CP_ULTIMATE) | A_BOLD);
        mvprintw(MENU_TOP + 1, left + 4, "Are you sure you want to quit?");
        mvprintw(MENU_TOP + 2, left + 4, "[Y] Yes — retreat from battle");
        mvprintw(MENU_TOP + 3, left + 4, "[N] No  — stay and fight!");
        attroff(COLOR_PAIR(CP_ULTIMATE) | A_BOLD);
        goto draw_bottom;
    }

    // Weapon pickup
    if (u.showPickup && s.dropPending) 
    {
        attron(COLOR_PAIR(CP_ULTIMATE) | A_BOLD);
        mvprintw(MENU_TOP, left, "WEAPON DROP: %-14s DMG:%-3d  SIZE:%d slots",
                 WEAPON_NAME[s.dropWeaponId], WEAPON_DAMAGE[s.dropWeaponId], WEAPON_SLOT_SIZE[s.dropWeaponId]);
        mvprintw(MENU_TOP + 1, left, "P%d: Pick up?   [Y] Yes     [N] No", s.dropForPlayer + 1);
        attroff(COLOR_PAIR(CP_ULTIMATE) | A_BOLD);
        goto draw_bottom;
    }

    // Regenerating energon message
    if (u.showRegenMsg) 
    {
        attron(COLOR_PAIR(CP_STAMINA_BAR) | A_BOLD);
        int cx = (cols - 42) / 2;
        mvprintw(MENU_TOP + 2, cx, "~ Autobots regenerating Energon... ~");
        mvprintw(MENU_TOP + 3, cx, "  Waiting for next Autobot's turn   ");
        attroff(COLOR_PAIR(CP_STAMINA_BAR) | A_BOLD);
        goto draw_bottom;
    }

    
    if (u.showMenu && ap >= 0 && ap < MAX_PLAYERS) 
    {
        Entity *pl = &s.players[ap];
        int hasW = 0;
        for (int sl = 0; sl < INVENTORY_SLOTS; sl++)
        {
            if (pl->inventory[sl])
            {
                hasW = 1;
                break;
            }
        }
        int hasLTS = (pl->longTermStorageCount > 0);
        int hasUlt = playerHasUltimate(pl);

        // Header
        if (ap == 0) 
        {
            attron(COLOR_PAIR(CP_OPTIMUS) | A_BOLD);
            mvprintw(MENU_TOP, left, "Optimus");
            attroff(COLOR_PAIR(CP_OPTIMUS) | A_BOLD);
            attron(COLOR_PAIR(CP_OPTIMUS_BLUE) | A_BOLD);
            mvprintw(MENU_TOP, left + 8, "Prime");
            attroff(COLOR_PAIR(CP_OPTIMUS_BLUE) | A_BOLD);
            attron(COLOR_PAIR(CP_LOG));
            mvprintw(MENU_TOP, left + 14, " — Choose Action:");
            attroff(COLOR_PAIR(CP_LOG));
        } 
        else 
        {
            attron(COLOR_PAIR(PLAYER_CP[ap]) | A_BOLD);
            mvprintw(MENU_TOP, left, "%s — Choose Action:", PLAYER_NAME[ap]);
            attroff(COLOR_PAIR(PLAYER_CP[ap]) | A_BOLD);
        }

        // 2-column layout: col A = left+2 (width 22), col B = left+26 (width 22)
        const char *items[8] = {"[1] Strike","[2] Exhaust","[3] Heal","[4] Skip","[5] Use Weapon","[6] Swap In","[7] Ultimate","[8] Quit"};
        int avail[8] = {1, 1, 1, 1, hasW, hasLTS, hasUlt, 1};
        int colA = left + 2, colB = left + 26;
        // Fixed item width — MUST pad to exactly 22 so each rewrite fully clears previous
        #define ITEM_W 22

        for (int i = 0; i < 8; i++) 
        {
            int col = (i % 2 == 0) ? colA : colB;
            int row = MENU_TOP + 1 + (i / 2);
            int sel = (i == u.menuSel);
            char label[32];
            snprintf(label, sizeof(label), "%-*.*s", ITEM_W, ITEM_W, items[i]);
            if (sel) 
            {
                int cp = avail[i] ? CP_HIGHLIGHT : CP_UNAVAIL;
                attron(COLOR_PAIR(cp) | A_BOLD);
                mvprintw(row, col, "%s", label);
                attroff(COLOR_PAIR(cp) | A_BOLD);
            } 
            else if (!avail[i]) 
            {
                attron(COLOR_PAIR(CP_UNAVAIL));
                mvprintw(row, col, "%s", label);
                attroff(COLOR_PAIR(CP_UNAVAIL));
            } 
            else 
            {
                attron(COLOR_PAIR(CP_LOG));
                mvprintw(row, col, "%s", label);
                attroff(COLOR_PAIR(CP_LOG));
            }
        }
        #undef ITEM_W
        attron(COLOR_PAIR(CP_BORDER) | A_DIM);
        mvprintw(MENU_TOP + 5, colA, "[0] Quit game");
        attroff(COLOR_PAIR(CP_BORDER) | A_DIM);
    }

    // Target menu — right side
    if (u.showTargetMenu) 
    {
        int mx = div + 2;
        attron(COLOR_PAIR(CP_DECEPTICON) | A_BOLD);
        mvprintw(MENU_TOP, mx, "Select Target:  [0]=Back");
        attroff(COLOR_PAIR(CP_DECEPTICON) | A_BOLD);
        int ac = 0;
        for (int i = 0; i < s.numberOfEnemies; i++) 
        {
            if (!s.enemies[i].isAlive)
            {
                continue;
            }
            int r = MENU_TOP + 1 + ac;
            if (ac == u.targetSel)
            {
                attron(COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD);
            }
            else
            {
                attron(COLOR_PAIR(CP_DECEPTICON));
            }
            mvprintw(r, mx + 2, "[%d] E%-2d  HP:%d/%d", ac + 1, i + 1, s.enemies[i].hp, s.enemies[i].maxHp);
            attroff(COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD | COLOR_PAIR(CP_DECEPTICON));
            ac++;
        }
    }

    // Weapon menu — right side
    if (u.showWeaponMenu && ap >= 0) 
    {
        int mx = div + 2;
        int ss[INVENTORY_SLOTS];
        int wc = getInventoryWeapons(&s.players[ap], ss);
        attron(COLOR_PAIR(CP_WEAPON) | A_BOLD);
        mvprintw(MENU_TOP, mx, "Select Weapon:  [0]=Back");
        attroff(COLOR_PAIR(CP_WEAPON) | A_BOLD);
        for (int i = 0; i < wc; i++) 
        {
            int wid = s.players[ap].inventory[ss[i]];
            int r = MENU_TOP + 1 + i;
            if (i == u.weaponMenuSel)
            {
                attron(COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD);
            }
            else
            {
                attron(COLOR_PAIR(CP_WEAPON));
            }
            mvprintw(r, mx + 2, "[%d] %-14s DMG:%-3d SZ:%d", i + 1, WEAPON_NAME[wid], WEAPON_DAMAGE[wid], WEAPON_SLOT_SIZE[wid]);
            attroff(COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD | COLOR_PAIR(CP_WEAPON));
        }
    }

    // LTS menu — right side
    if (u.showLtsMenu && ap >= 0) 
    {
        int mx = div + 2;
        Entity *pl = &s.players[ap];
        attron(COLOR_PAIR(CP_WEAPON) | A_BOLD);
        mvprintw(MENU_TOP, mx, "Long-Term Storage:  [0]=Back");
        attroff(COLOR_PAIR(CP_WEAPON) | A_BOLD);
        for (int i = 0; i < pl->longTermStorageCount; i++) 
        {
            int wid = pl->longTermStorage[i];
            int r = MENU_TOP + 1 + i;
            if (i == u.ltsSel)
            {
                attron(COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD);
            }
            else
            {
                attron(COLOR_PAIR(CP_WEAPON));
            }
            mvprintw(r, mx + 2, "[%d] %-14s DMG:%-3d SZ:%d", i + 1, WEAPON_NAME[wid], WEAPON_DAMAGE[wid], WEAPON_SLOT_SIZE[wid]);
            attroff(COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD | COLOR_PAIR(CP_WEAPON));
        }
    }

draw_bottom:
    drawHLine(SEP4_ROW, cols);
    attron(COLOR_PAIR(CP_BORDER) | A_DIM);
    mvprintw(HELP_ROW, 2, "UP/DOWN:navigate  1-8:select  ENTER:confirm  0:back/quit  Q:quit");
    attroff(COLOR_PAIR(CP_BORDER) | A_DIM);

    // C2: Wave banner overlay — centered box shown for 2s on new wave
    if (u.showWaveBanner)
    {
        int bw = 38;
        int bx = (cols - bw) / 2;
        int by = LINES / 2 - 2;
        attron(COLOR_PAIR(3) | A_BOLD);
        for (int r = 0; r < 4; r++)
        {
            mvhline(by + r, bx, ' ', bw);
        }
        char wb[48];
        snprintf(wb, sizeof(wb), "    *** WAVE %d INCOMING ***    ", u.waveBannerNum);
        mvprintw(by + 1, (cols - (int)strlen(wb)) / 2, "%s", wb);
        mvprintw(by + 2, (cols - 28) / 2, "    DECEPTICONS ARE HERE!    ");
        attroff(COLOR_PAIR(3) | A_BOLD);
    }

    wnoutrefresh(stdscr);
    doupdate();
    NCURSES_UNLOCK;
}

void *renderThread(void *arg) {
    GameState *s = (GameState*)arg;
    while (s->gameRunning) 
    {
        takeSnapshot(s);
        NCURSES_LOCK;
        drawScreen();  // drawScreen itself no longer locks ncursesMtx
        NCURSES_UNLOCK;
        usleep(50000); // 50ms = 20fps
    }
    return NULL;
}

static void submitAction(GameState *s, Action *a) {
    if (s->currentTurnIsPlayer != 1 || s->currentTurnEntityIndex != a->sourceEntityIndex)
    {
        return;
    }
    sem_wait(&s->actionConsumed);
    if (s->currentTurnIsPlayer != 1 || s->currentTurnEntityIndex != a->sourceEntityIndex) 
    { 
        sem_post(&s->actionConsumed);
        return;
    }
    sem_wait(&s->stateLock);
    s->pendingAction = *a;
    s->actionReady = 1;
    sem_post(&s->stateLock);
    sem_post(&s->actionSubmitted);
}

typedef struct { int idx; GameState *s; } PTA;
#define MYPOP(ms) keyPop(pi,(ms))

void *playerThread(void *arg) {
    PTA *a = (PTA*)arg;
    int pi = a->idx;
    GameState *s = a->s;

    while (s->gameRunning) {

        // D3: Weapon drop — read under stateLock to prevent race
        sem_wait(&s->stateLock);
        int dropPending = s->weaponDrop.pending &&
                          s->weaponDrop.forPlayerIndex == pi &&
                          s->weaponDrop.decision == -1;
        sem_post(&s->stateLock);

        if (dropPending) 
        {
            setActiveConsumer(pi);
            UI_LOCK;
            ui.showPickup = 1;
            ui.activePlayer = pi;
            UI_UNLOCK;
            keyFlush();
            int key;
            do 
            { 
                key = MYPOP(200);
            } 
            while (key != 'y' && key != 'Y' && key != 'n' && key != 'N' && s->gameRunning);
            sem_wait(&s->stateLock);
            s->weaponDrop.decision = (key == 'y' || key == 'Y') ? 1 : 0;
            sem_post(&s->stateLock);
            UI_LOCK;
            ui.showPickup = 0;
            UI_UNLOCK;
            continue;
        }

        // Not my turn
        if (s->currentTurnIsPlayer != 1 || s->currentTurnEntityIndex != pi || !s->players[pi].isAlive) 
        {
            if (pi == 0) 
            {
                UI_LOCK;
                ui.showRegenMsg = (s->currentTurnIsPlayer != 1);
                UI_UNLOCK;
            }
            usleep(30000);
            continue;
        }

        // MY TURN
        Entity *pl = &s->players[pi];
        setActiveConsumer(pi);
        UI_LOCK;
        ui.activePlayer = pi;
        ui.menuSel = 0;
        ui.showMenu = 1;
        ui.showTargetMenu = 0;
        ui.showWeaponMenu = 0;
        ui.showLtsMenu = 0;
        ui.showRegenMsg = 0;
        ui.showQuitConfirm = 0;
        UI_UNLOCK;

        
main_menu:;
        UI_LOCK;
        ui.showMenu = 1;
        ui.showTargetMenu = 0;
        ui.showWeaponMenu = 0;
        ui.showLtsMenu = 0;
        ui.showQuitConfirm = 0;
        UI_UNLOCK;

        int chosen = -1;
        while (chosen == -1 && s->gameRunning && s->currentTurnIsPlayer == 1 && s->currentTurnEntityIndex == pi) 
        {
            int key = MYPOP(100);
            if (key == ERR)
            {
                continue;
            }
            UI_LOCK;
            int ms = ui.menuSel;
            
            if (key == KEY_UP)
            {
                ms = (ms <= 1) ? ms : (ms - 2);
            }
            else if (key == KEY_DOWN)
            {
                ms = (ms >= 6) ? ms : (ms + 2);
            }
            else if (key == KEY_LEFT)
            {
                if (ms % 2 == 1)
                {
                    ms--;
                }
            }
            else if (key == KEY_RIGHT)
            {
                if (ms % 2 == 0 && ms < 7)
                {
                    ms++;
                }
            }
            else if (key >= '1' && key <= '8')
            {
                ms = key - '1';
                chosen = ms;
            }
            else if (key == '\n' || key == KEY_ENTER)
            {
                chosen = ms;
            }
            else if (key == 'q' || key == 'Q' || key == '0')
            {
                ui.menuSel = ms;
                UI_UNLOCK;
                goto quit_confirm;
            }
            // EASTER EGG: press U to give Player 1 Solar Core + Lunar Blade for testing
            else if (key == 'u' || key == 'U') 
            {
                UI_UNLOCK;
                sem_wait(&s->stateLock);
                // clear P1 inventory and give both artifacts
                memset(s->players[0].inventory, 0, sizeof(s->players[0].inventory));
                for (int i = 0; i < 10; i++)
                {
                    s->players[0].inventory[i] = WEAPON_SOLAR_CORE;
                }
                for (int i = 10; i < 20; i++)
                {
                    s->players[0].inventory[i] = WEAPON_LUNAR_BLADE;
                }
                s->players[0].holdsSolarCore = 1;
                s->players[0].holdsLunarBlade = 1;
                s->artifacts.solarCoreHeldBy = 0;
                s->artifacts.lunarBladeHeldBy = 0;
                sem_post(&s->stateLock);
                pushLog("*** EASTER EGG: Solar Core + Lunar Blade equipped! ***");
                goto main_menu;
            }
            ui.menuSel = ms;
            UI_UNLOCK;
        }
        if (chosen == -1)
        {
            usleep(30000);
            continue;
        }

        UI_LOCK;
        ui.showMenu = 0;
        UI_UNLOCK;
        Action act;
        memset(&act, 0, sizeof(act));
        act.sourceEntityIndex = pi;
        act.isPlayer = 1;

        if (chosen == 7)
        {
            goto quit_confirm;
        }
        if (chosen == 2)
        {
            act.actionType = ACTION_HEAL;
            submitAction(s, &act);
            continue;
        }
        if (chosen == 3)
        {
            act.actionType = ACTION_SKIP;
            submitAction(s, &act);
            continue;
        }
        if (chosen == 6) 
        {
            if (!playerHasUltimate(pl))
            {
                goto main_menu;
            }
            act.actionType = ACTION_ULTIMATE;
            submitAction(s, &act);
            continue;
        }
        if (chosen == 4) 
        { 
            // Use Weapon
            int ss[INVENTORY_SLOTS];
            int wc = getInventoryWeapons(pl, ss);
            if (!wc)
            {
                goto main_menu;
            }
            UI_LOCK;
            ui.showWeaponMenu = 1;
            ui.weaponMenuSel = 0;
            UI_UNLOCK;
            int wpick = -1;
            while (wpick == -1 && s->gameRunning) 
            {
                wc = getInventoryWeapons(pl, ss);
                if (!wc)
                {
                    wpick = -2;
                    break;
                }
                int key = MYPOP(100);
                if (key == ERR)
                {
                    continue;
                }
                UI_LOCK;
                int ws = ui.weaponMenuSel;
                if (key == KEY_UP)
                {
                    ws = (ws - 1 + wc) % wc;
                }
                else if (key == KEY_DOWN)
                {
                    ws = (ws + 1) % wc;
                }
                else if (key >= '1' && key <= '9')
                {
                    int n = key - '1';
                    if (n < wc)
                    {
                        ws = n;
                        wpick = n;
                    }
                }
                else if (key == '\n' || key == KEY_ENTER)
                {
                    wpick = ws;
                }
                else if (key == '0' || key == 'q' || key == 'Q')
                {
                    wpick = -2;
                }
                ui.weaponMenuSel = ws;
                UI_UNLOCK;
            }
            UI_LOCK;
            ui.showWeaponMenu = 0;
            UI_UNLOCK;
            if (wpick < 0)
            {
                goto main_menu;
            }
            act.weaponStartSlot = ss[wpick];
            // pick target
            int alive[MAX_ENEMIES];
            int ac = 0;
            for (int i = 0; i < s->numberOfEnemies; i++)
            {
                if (s->enemies[i].isAlive)
                {
                    alive[ac++] = i;
                }
            }
            if (!ac)
            {
                continue;
            }
            UI_LOCK;
            ui.showTargetMenu = 1;
            ui.targetSel = 0;
            UI_UNLOCK;
            int tpick = -1;
            while (tpick == -1 && s->gameRunning) 
            {
                int key = MYPOP(100);
                if (key == ERR)
                {
                    continue;
                }
                UI_LOCK;
                int ts = ui.targetSel;
                if (key == KEY_UP)
                {
                    ts = (ts - 1 + ac) % ac;
                }
                else if (key == KEY_DOWN)
                {
                    ts = (ts + 1) % ac;
                }
                else if (key >= '1' && key <= '9')
                {
                    int n = key - '1';
                    if (n < ac)
                    {
                        ts = n;
                        tpick = n;
                    }
                }
                else if (key == '\n' || key == KEY_ENTER)
                {
                    tpick = ts;
                }
                else if (key == '0' || key == 'q' || key == 'Q')
                {
                    tpick = -2;
                }
                ui.targetSel = ts;
                UI_UNLOCK;
            }
            UI_LOCK;
            ui.showTargetMenu = 0;
            UI_UNLOCK;
            if (tpick < 0)
            {
                goto main_menu;
            }
            act.actionType = ACTION_USE_WEAPON;
            act.targetIndex = alive[tpick];
            submitAction(s, &act);
            continue;
        }
        if (chosen == 5) 
        { 
            // Swap In — full 3-step flow
            // Step 0: need something in LTS to swap in
            if (!pl->longTermStorageCount) 
            {
                pushLog("LTS is empty!");
                goto main_menu;
            }

            
            int ss2[INVENTORY_SLOTS];
            int wc2 = getInventoryWeapons(pl, ss2);
            int needsEvict = 0;
            // check if LTS weapon would fit without eviction
            int ltsPreview = pl->longTermStorage[0]; // preview first item
            if (findContiguousSlots(pl->inventory, WEAPON_SLOT_SIZE[ltsPreview]) < 0)
            {
                needsEvict = 1;
            }

            int evictSlot = -1; // -1 = let arbiter auto-evict
            if (needsEvict && wc2 > 0) 
            {
                // Show inventory swap-out menu
                UI_LOCK;
                ui.showWeaponMenu = 1;
                ui.weaponMenuSel = 0;
                UI_UNLOCK;
                pushLog("Choose weapon to swap OUT:");
                int epick = -1;
                while (epick == -1 && s->gameRunning)
                {
                    wc2 = getInventoryWeapons(pl, ss2);
                    int key = MYPOP(100);
                    if (key == ERR)
                    {
                        continue;
                    }
                    UI_LOCK;
                    int ws = ui.weaponMenuSel;
                    if (key == KEY_UP)
                    {
                        ws = (ws - 1 + wc2 + 1) % wc2;
                    }
                    else if (key == KEY_DOWN)
                    {
                        ws = (ws + 1) % wc2;
                    }
                    else if (key >= '1' && key <= '9')
                    {
                        int n = key - '1';
                        if (n < wc2)
                        {
                            ws = n;
                            epick = n;
                        }
                    }
                    else if (key == '\n' || key == KEY_ENTER)
                    {
                        epick = ws;
                    }
                    else if (key == '0' || key == 'q' || key == 'Q')
                    {
                        epick = -2;
                    }
                    ui.weaponMenuSel = ws;
                    UI_UNLOCK;
                }
                UI_LOCK;
                ui.showWeaponMenu = 0;
                UI_UNLOCK;
                if (epick < 0)
                {
                    goto main_menu;
                }
                evictSlot = ss2[epick];
                // Manually evict chosen weapon to LTS
                int wid = pl->inventory[evictSlot];
                int sz = WEAPON_SLOT_SIZE[wid];
                for (int i = evictSlot; i < evictSlot + sz && i < INVENTORY_SLOTS; i++)
                {
                    pl->inventory[i] = 0;
                }
                if (pl->longTermStorageCount < MAX_WEAPONS_IN_LTS)
                {
                    pl->longTermStorage[pl->longTermStorageCount++] = wid;
                }
                char evlog[ACTION_LOG_LEN];
                snprintf(evlog, ACTION_LOG_LEN, "Swapped %s to LTS", WEAPON_NAME[wid]);
                pushLog(evlog);
            }

            // Step 2: Show LTS — player picks what to swap IN
            if (!pl->longTermStorageCount) 
            {
                pushLog("LTS empty after eviction!");
                goto main_menu;
            }
            UI_LOCK;
            ui.showLtsMenu = 1;
            ui.ltsSel = 0;
            UI_UNLOCK;
            pushLog("Choose weapon to swap IN:");
            int ltsPick = -1;
            while (ltsPick == -1 && s->gameRunning)
            {
                int key = MYPOP(100);
                if (key == ERR)
                {
                    continue;
                }
                UI_LOCK;
                int ls = ui.ltsSel;
                int lc = pl->longTermStorageCount;
                if (key == KEY_UP)
                {
                    ls = (ls - 1 + lc) % lc;
                }
                else if (key == KEY_DOWN)
                {
                    ls = (ls + 1) % lc;
                }
                else if (key >= '1' && key <= '9')
                {
                    int n = key - '1';
                    if (n < lc)
                    {
                        ls = n;
                        ltsPick = n;
                    }
                }
                else if (key == '\n' || key == KEY_ENTER)
                {
                    ltsPick = ls;
                }
                else if (key == '0' || key == 'q' || key == 'Q')
                {
                    ltsPick = -2;
                }
                ui.ltsSel = ls;
                UI_UNLOCK;
            }
            UI_LOCK;
            ui.showLtsMenu = 0;
            UI_UNLOCK;
            if (ltsPick < 0)
            {
                goto main_menu;
            }
            act.actionType = ACTION_SWAP_IN;
            act.ltsIndex = ltsPick;
            submitAction(s, &act);
            continue;
        }
        // Strike / Exhaust
        {
            act.actionType = (chosen == 0) ? ACTION_STRIKE : ACTION_EXHAUST;
            int alive2[MAX_ENEMIES];
            int ac2 = 0;
            for (int i = 0; i < s->numberOfEnemies; i++)
            {
                if (s->enemies[i].isAlive)
                {
                    alive2[ac2++] = i;
                }
            }
            if (!ac2)
            {
                continue;
            }
            UI_LOCK;
            ui.showTargetMenu = 1;
            ui.targetSel = 0;
            UI_UNLOCK;
            int tpick2 = -1;
            while (tpick2 == -1 && s->gameRunning) 
            {
                int key = MYPOP(100);
                if (key == ERR)
                {
                    continue;
                }
                UI_LOCK;
                int ts = ui.targetSel;
                if (key == KEY_UP)
                {
                    ts = (ts - 1 + ac2) % ac2;
                }
                else if (key == KEY_DOWN)
                {
                    ts = (ts + 1) % ac2;
                }
                else if (key >= '1' && key <= '9')
                {
                    int n = key - '1';
                    if (n < ac2)
                    {
                        ts = n;
                        tpick2 = n;
                    }
                }
                else if (key == '\n' || key == KEY_ENTER)
                {
                    tpick2 = ts;
                }
                else if (key == '0' || key == 'q' || key == 'Q')
                {
                    tpick2 = -2;
                }
                ui.targetSel = ts;
                UI_UNLOCK;
            }
            UI_LOCK;
            ui.showTargetMenu = 0;
            UI_UNLOCK;
            if (tpick2 < 0)
            {
                goto main_menu;
            }
            act.targetIndex = alive2[tpick2];
            submitAction(s, &act);
        }
        continue;

quit_confirm:;
        UI_LOCK;
        ui.showMenu = 0;
        ui.showQuitConfirm = 1;
        UI_UNLOCK;
        keyFlush();
        int qkey = -1;
        while (qkey == -1 && s->gameRunning) 
        {
            int key = MYPOP(200);
            if (key == ERR)
            {
                continue;
            }
            if (key == 'y' || key == 'Y')
            {
                qkey = 1;
            }
            else if (key == 'n' || key == 'N' || key == '0')
            {
                qkey = 0;
            }
        }
        UI_LOCK;
        ui.showQuitConfirm = 0;
        UI_UNLOCK;
        if (qkey == 1) 
        { 
            kill(s->arbiterPid, SIGTERM);
            s->gameRunning = 0;
            pthread_exit(NULL);
        }
        goto main_menu;
    }
    pthread_exit(NULL);
}

static void drawEndScreen(GameState *s) {
    clear();
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    const char *l1 = "", *l2 = "";
    int cp = CP_TITLE;
    switch(s->gameResult) 
    {
        case 1:
            l1 = "AUTOBOTS DEFEATED";
            l2 = "The Decepticons have won.";
            cp = CP_HP_LOW;
            break;
        case 2:
            l1 = "AUTOBOTS VICTORIOUS";
            l2 = "Megatron's forces are vanquished!";
            cp = CP_HP_HIGH;
            break;
        case 3:
            l1 = "RETREAT";
            l2 = "The Autobots have withdrawn.";
            cp = CP_STAMINA_BAR;
            break;
        default:
            l1 = "GAME OVER";
            l2 = "";
            cp = CP_TITLE;
            break;
    }
    attron(COLOR_PAIR(cp) | A_BOLD);
    mvprintw(rows/2 - 2, (cols - (int)strlen(l1)) / 2, "%s", l1);
    attroff(COLOR_PAIR(cp) | A_BOLD);
    attron(COLOR_PAIR(CP_LOG));
    mvprintw(rows/2, (cols - (int)strlen(l2)) / 2, "%s", l2);
    mvprintw(rows/2 + 2, (cols - 30) / 2, "Decepticons defeated: %d / 10", s->totalEnemiesKilled);
    attroff(COLOR_PAIR(CP_LOG));
    attron(COLOR_PAIR(CP_BORDER));
    mvprintw(rows/2 + 4, (cols - 28) / 2, "[ Press any key to exit ]");
    attroff(COLOR_PAIR(CP_BORDER));
    nodelay(stdscr, FALSE); // blocking getch
    refresh();
    getch();
    nodelay(stdscr, TRUE);
}

volatile sig_atomic_t stunFlag=0;
void handleStun(int sig)      
{ 
    (void)sig;
    stunFlag = 1;
}
void handleTerminate(int sig) 
{ 
    (void)sig;
    if (gameState)
    {
        gameState->gameRunning = 0;
    }
}

int main() {
    signal(SIGTERM, handleTerminate);
    signal(SIGUSR1, handleStun);
    sleep(1);
    gameState = attachShm();

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    nodelay(stdscr, TRUE);

    start_color();
    use_default_colors();
    assume_default_colors(COLOR_WHITE, COLOR_BLACK);
    init_pair(CP_TITLE,      COLOR_WHITE,  COLOR_BLACK);
    init_pair(CP_OPTIMUS,    COLOR_RED,    COLOR_BLACK);
    init_pair(CP_BUMBLEBEE,  COLOR_YELLOW, COLOR_BLACK);
    init_pair(CP_ARCEE,      COLOR_MAGENTA,COLOR_BLACK);
    init_pair(CP_RATCHET,    COLOR_GREEN,  COLOR_BLACK);
    init_pair(CP_DECEPTICON, COLOR_MAGENTA,COLOR_BLACK);
    init_pair(CP_HP_HIGH,    COLOR_GREEN,  COLOR_BLACK);
    init_pair(CP_HP_LOW,     COLOR_RED,    COLOR_BLACK);
    init_pair(CP_STAMINA_BAR,COLOR_YELLOW, COLOR_BLACK);
    init_pair(CP_BORDER,     COLOR_BLUE,   COLOR_BLACK);
    init_pair(CP_DEAD,       COLOR_WHITE,  COLOR_BLACK);
    init_pair(CP_HIGHLIGHT,  COLOR_BLACK,  COLOR_YELLOW);
    init_pair(CP_WEAPON,     COLOR_CYAN,   COLOR_BLACK);
    init_pair(CP_LOG,        COLOR_WHITE,  COLOR_BLACK);
    init_pair(CP_TURN,       COLOR_GREEN,  COLOR_BLACK);
    init_pair(CP_ULTIMATE,   COLOR_RED,    COLOR_BLACK);
    init_pair(CP_UNAVAIL,    COLOR_WHITE,  COLOR_RED);
    if (COLORS >= 256)
    {
        init_pair(CP_DECEPTICON, 93, COLOR_BLACK);
    }

    memset(eventLog, 0, sizeof(eventLog));
    logHead = 0;
    memset(&ui, 0, sizeof(ui));
    ui.activePlayer = -1;
    memset(&rsnap, 0, sizeof(rsnap));

    // D1: keyReaderThread is a no-op — removed pthread_create to avoid wasting resources

    
    pthread_t renderTid;
    pthread_create(&renderTid, NULL, renderThread, gameState);

    PTA args[MAX_PLAYERS];
    pthread_t ptids[MAX_PLAYERS];
    for (int i = 0; i < gameState->numberOfPlayers; i++) 
    {
        args[i].idx = i;
        args[i].s = gameState;
        pthread_create(&ptids[i], NULL, playerThread, &args[i]);
    }

    char lastLog[MAX_PLAYERS + MAX_ENEMIES][ACTION_LOG_LEN];
    memset(lastLog, 0, sizeof(lastLog));

    
    while (gameState->gameRunning) 
    {
        // 1. Read ONE key non-blocking under ncursesMtx and push to buffer
        NCURSES_LOCK;
        int k = getch();
        NCURSES_UNLOCK;
        if (k != ERR)
        {
            keyPush(k);
        }

        // 2. Poll event log every frame
        sem_wait(&gameState->stateLock);
        for (int i = 0; i < gameState->numberOfPlayers; i++) 
        {
            if (strncmp(gameState->players[i].actionLog, lastLog[i], ACTION_LOG_LEN - 1) != 0) 
            {
                strncpy(lastLog[i], gameState->players[i].actionLog, ACTION_LOG_LEN - 1);
                pushLog(gameState->players[i].actionLog);
            }
        }
        for (int i = 0; i < gameState->numberOfEnemies; i++) 
        {
            if (strncmp(gameState->enemies[i].actionLog, lastLog[MAX_PLAYERS + i], ACTION_LOG_LEN - 1) != 0) 
            {
                strncpy(lastLog[MAX_PLAYERS + i], gameState->enemies[i].actionLog, ACTION_LOG_LEN - 1);
                pushLog(gameState->enemies[i].actionLog);
            }
        }
        sem_post(&gameState->stateLock);

        // 3. Handle stun on HIP process itself
        if (stunFlag) 
        {
            stunFlag = 0;
            sleep(3);
        }

        // C2: Detect wave change and show banner for 2 seconds
        static int lastWave = -1;
        static float waveBannerTimer = 0.f;
        sem_wait(&gameState->stateLock);
        int curWave = gameState->waveNumber;
        sem_post(&gameState->stateLock);
        if (lastWave != -1 && curWave != lastWave)
        {
            UI_LOCK;
            ui.showWaveBanner = 1;
            ui.waveBannerNum = curWave + 1;
            UI_UNLOCK;
            waveBannerTimer = 2.0f;
        }
        lastWave = curWave;
        if (waveBannerTimer > 0.f)
        {
            waveBannerTimer -= 0.016f;
            if (waveBannerTimer <= 0.f)
            {
                UI_LOCK;
                ui.showWaveBanner = 0;
                UI_UNLOCK;
            }
        }

        usleep(16000);
    }

    setActiveConsumer(-1);
    for (int i = 0; i < gameState->numberOfPlayers; i++)
    {
        pthread_cancel(ptids[i]);
    }
    for (int i = 0; i < gameState->numberOfPlayers; i++)
    {
        pthread_join(ptids[i], NULL);
    }
    pthread_cancel(renderTid);
    pthread_join(renderTid, NULL);

    drawEndScreen(gameState);
    endwin();
    munmap(gameState, sizeof(GameState));
    return 0;
}
