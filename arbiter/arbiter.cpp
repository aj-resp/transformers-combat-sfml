// Kasim Zeeshan Alvi || 24i0549 || CS-A
// Abdullah Junaid    || 24i0569 || CS-A
// arbiter.cpp - central game authority: manages state, scheduling, signals, deadlock detection

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <pthread.h>
#include <time.h>
#include "shared.h"

GameState *gameState = NULL;
pid_t hipPid = -1;
pid_t aspPid = -1;

// signal handlers
void handleQuit(int sig) {
    (void)sig;
    if (gameState) 
    { 
        gameState->gameResult = 3; 
        gameState->gameRunning = 0; 
    }
}

// resumes asp after 10s ultimate and clears ultimate flag
void handleAlarm(int sig) {
    (void)sig;
    if (aspPid > 0 && gameState) 
    {
        kill(aspPid, SIGCONT);
        gameState->ultimateAbilityActive = 0;
    }
}

void handleChildDeath(int sig) {
    (void)sig;
    int status;
    waitpid(-1, &status, WNOHANG);
}

// inventory helpers

int findWeaponStart(int *inv, int s) {
    int wid = inv[s];
    if (!wid) 
        return s;
    while (s > 0 && inv[s-1] == wid) 
        s--;
    return s;
}

// returns start index of first contiguous run of size free slots or -1
int findContiguousSlots(int *inv, int size) {
    for (int start = 0; start <= INVENTORY_SLOTS - size; start++) 
    {
        int ok = 1;
        for (int j = 0; j < size; j++)
        {
            if (inv[start+j]) 
            { 
                ok = 0; 
                break;
            }
        }
        if (ok) 
            return start;
    }
    return -1;
}

void removeWeaponAtSlot(int *inv, int start) {
    int wid = inv[start]; 
    if (!wid) 
        return;
    int sz = WEAPON_SLOT_SIZE[wid];
    for (int i = start; i < start+sz && i < INVENTORY_SLOTS; i++)
    {
        if (inv[i] == wid)
        {
            inv[i] = 0;
        }
    }
}

void removeWeaponById(Entity *e, int wid) {
    for (int i = 0; i < INVENTORY_SLOTS; i++)
    {
        if (e->inventory[i] == wid) 
            e->inventory[i] = 0;
    }
}

void placeWeaponAtSlot(int *inv, int start, int wid) {
    int sz = WEAPON_SLOT_SIZE[wid];
    for (int i = start; i < start+sz; i++) 
        inv[i] = wid;
}

int addToInventory(Entity *e, int wid) {
    int sz = WEAPON_SLOT_SIZE[wid];
    int s = findContiguousSlots(e->inventory, sz);
    if (s == -1) 
        return 0;
    placeWeaponAtSlot(e->inventory, s, wid);
    return 1;
}

// evicts minimum weapons to lts until needed contiguous slots are free
void makeRoom(Entity *e, int needed) {
    int safety = INVENTORY_SLOTS * 2;
    while (findContiguousSlots(e->inventory, needed) == -1 && safety-- > 0) 
    {
        int first = -1;
        for (int i = 0; i < INVENTORY_SLOTS; i++) 
        {
            if (e->inventory[i]) 
            { 
                first = i; 
                break; 
            }
        }
        if (first == -1) 
            break;
        int ts = findWeaponStart(e->inventory, first);
        int wid = e->inventory[ts];
        if (e->longTermStorageCount < MAX_WEAPONS_IN_LTS)
        {
            e->longTermStorage[e->longTermStorageCount++] = wid;
        }
        removeWeaponAtSlot(e->inventory, ts);
    }
}

void forceAdd(Entity *e, int wid) {
    if (!addToInventory(e, wid)) 
    { 
        makeRoom(e, WEAPON_SLOT_SIZE[wid]); 
        addToInventory(e, wid); 
    }
}

// rebuilds artifact ownership table by scanning all inventories
void syncArtifacts(GameState *s) {
    s->artifacts.solarCoreHeldBy = s->artifacts.lunarBladeHeldBy = s->artifacts.eclipseRelicHeldBy = -1;
    for (int pi = 0; pi < s->numberOfPlayers; pi++) 
    {
        Entity *p = &s->players[pi];
        p->holdsSolarCore = p->holdsLunarBlade = p->holdsEclipseRelic = 0;
        for (int sl = 0; sl < INVENTORY_SLOTS; sl++) 
        {
            if (p->inventory[sl] == WEAPON_SOLAR_CORE)    
            { 
                p->holdsSolarCore = 1;    
                s->artifacts.solarCoreHeldBy = pi; 
            }
            if (p->inventory[sl] == WEAPON_LUNAR_BLADE)   
            { 
                p->holdsLunarBlade = 1;   
                s->artifacts.lunarBladeHeldBy = pi; 
            }
            if (p->inventory[sl] == WEAPON_ECLIPSE_RELIC) 
            { 
                p->holdsEclipseRelic = 1; 
                s->artifacts.eclipseRelicHeldBy = pi; 
            }
        }
    }
    for (int ei = 0; ei < s->numberOfEnemies; ei++) 
    {
        Entity *e = &s->enemies[ei];
        e->holdsSolarCore = e->holdsLunarBlade = e->holdsEclipseRelic = 0;
        for (int sl = 0; sl < INVENTORY_SLOTS; sl++) 
        {
            if (e->inventory[sl] == WEAPON_SOLAR_CORE)    
            { 
                e->holdsSolarCore = 1;    
                if (s->artifacts.solarCoreHeldBy == -1) 
                    s->artifacts.solarCoreHeldBy = 100 + ei; 
            }
            if (e->inventory[sl] == WEAPON_LUNAR_BLADE)   
            { 
                e->holdsLunarBlade = 1;   
                if (s->artifacts.lunarBladeHeldBy == -1) 
                    s->artifacts.lunarBladeHeldBy = 100 + ei; 
            }
            if (e->inventory[sl] == WEAPON_ECLIPSE_RELIC) 
            { 
                e->holdsEclipseRelic = 1; 
                if (s->artifacts.eclipseRelicHeldBy == -1) 
                    s->artifacts.eclipseRelicHeldBy = 100 + ei; 
            }
        }
    }
}

// entity init using roll number 549 for stat formulas
void initPlayer(Entity *p, int numPlayers) {
    p->isAlive = 1; 
    p->shouldExit = 0;
    p->maxHp = ROLL_NUMBER + (rand() % 901 + 100);
    p->hp = p->maxHp;
    p->damage = (ROLL_NUMBER % 10) + 10;
    p->speed = 100 / numPlayers;
    p->stamina = 0; 
    p->maxStamina = 100;
    p->isStunned = 0; 
    p->stunTicksRemaining = 0;
    p->longTermStorageCount = 0;
    p->holdsSolarCore = p->holdsLunarBlade = p->holdsEclipseRelic = 0;
    p->waitingForSolarCore = p->waitingForLunarBlade = p->waitingForEclipseRelic = 0;
    memset(p->inventory, 0, sizeof(p->inventory));
    memset(p->longTermStorage, 0, sizeof(p->longTermStorage));
    snprintf(p->actionLog, ACTION_LOG_LEN, "Waiting for stamina...");
}

void initEnemy(Entity *e) {
    e->isAlive = 1; 
    e->shouldExit = 0;
    e->maxHp = (ROLL_NUMBER % 100) + (rand() % 151 + 50);
    e->hp = e->maxHp;
    e->damage = ((ROLL_NUMBER / 10) % 10) + 10;
    e->speed = rand() % 21 + 10;
    e->stamina = 0; 
    e->maxStamina = 150;
    e->isStunned = 0; 
    e->stunTicksRemaining = 0;
    e->longTermStorageCount = 0;
    e->holdsSolarCore = e->holdsLunarBlade = e->holdsEclipseRelic = 0;
    e->waitingForSolarCore = e->waitingForLunarBlade = e->waitingForEclipseRelic = 0;
    memset(e->inventory, 0, sizeof(e->inventory));
    memset(e->longTermStorage, 0, sizeof(e->longTermStorage));
    snprintf(e->actionLog, ACTION_LOG_LEN, "Waiting...");
}

// creates and maps the shared memory segment
GameState *createShm() {
    shm_unlink(SHM_NAME);
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd < 0) 
    { 
        perror("shm_open"); 
        exit(1); 
    }
    ftruncate(fd, sizeof(GameState));
    GameState *s = (GameState*)mmap(NULL, sizeof(GameState), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (s == MAP_FAILED) 
    { 
        perror("mmap"); 
        exit(1); 
    }
    close(fd);
    return s;
}

// initializes all semaphores with pshared=1 so they work across processes
void initSems(GameState *s) {
    sem_init(&s->stateLock, 1, 1);
    sem_init(&s->actionSubmitted, 1, 0);
    sem_init(&s->actionConsumed, 1, 1);
    sem_init(&s->artifacts.artifactTableLock, 1, 1);
    sem_init(&s->ui.uiLock, 1, 1);
}

// background thread: detects circular waits in artifact table every 2s
void *deadlockThread(void *arg) {
    GameState *s = (GameState*)arg;
    while (s->gameRunning) 
    {
        sleep(2);
        sem_wait(&s->artifacts.artifactTableLock);

        typedef struct { int id; int holds[3]; int wants[3]; } ENode;
        ENode nodes[MAX_PLAYERS + MAX_ENEMIES];
        int nc = 0;

        for (int pi = 0; pi < s->numberOfPlayers; pi++) 
        {
            Entity *e = &s->players[pi]; 
            if (!e->isAlive) 
                continue;
            ENode *n = &nodes[nc++]; 
            n->id = pi;
            n->holds[0] = e->holdsSolarCore;  
            n->holds[1] = e->holdsLunarBlade;  
            n->holds[2] = e->holdsEclipseRelic;
            n->wants[0] = e->waitingForSolarCore; 
            n->wants[1] = e->waitingForLunarBlade; 
            n->wants[2] = e->waitingForEclipseRelic;
        }
        for (int ei = 0; ei < s->numberOfEnemies; ei++) 
        {
            Entity *e = &s->enemies[ei]; 
            if (!e->isAlive) 
                continue;
            ENode *n = &nodes[nc++]; 
            n->id = 100 + ei;
            n->holds[0] = e->holdsSolarCore;  
            n->holds[1] = e->holdsLunarBlade;  
            n->holds[2] = e->holdsEclipseRelic;
            n->wants[0] = e->waitingForSolarCore; 
            n->wants[1] = e->waitingForLunarBlade; 
            n->wants[2] = e->waitingForEclipseRelic;
        }

        // check all pairs for A holds X wants Y while B holds Y wants X
        for (int a = 0; a < nc; a++) 
        {
            for (int b = a + 1; b < nc; b++) 
            {
                int deadlocked = 0, victimArtifact = -1;
                for (int ax = 0; ax < 3 && !deadlocked; ax++) 
                {
                    if (!nodes[a].holds[ax]) 
                        continue;
                    for (int ay = 0; ay < 3 && !deadlocked; ay++) 
                    {
                        if (ax == ay || !nodes[a].wants[ay]) 
                            continue;
                        if (nodes[b].holds[ay] && nodes[b].wants[ax]) 
                        {
                            deadlocked = 1; 
                            victimArtifact = ay;
                        }
                    }
                }
                if (!deadlocked) 
                    continue;

                // force victim to release the contested artifact
                Entity *victim = (nodes[b].id < 100)
                    ? &s->players[nodes[b].id]
                    : &s->enemies[nodes[b].id - 100];

                int wid;
                if (victimArtifact == 0)
                    wid = WEAPON_SOLAR_CORE;
                else if (victimArtifact == 1)
                    wid = WEAPON_LUNAR_BLADE;
                else
                    wid = WEAPON_ECLIPSE_RELIC;
                    
                for (int sl = 0; sl < INVENTORY_SLOTS; sl++)
                {
                    if (victim->inventory[sl] == wid) 
                        victim->inventory[sl] = 0;
                }
                // only clear the specific released artifact waitingFor flag
                if (victimArtifact == 0)
                { 
                    victim->holdsSolarCore = 0;    
                    s->artifacts.solarCoreHeldBy = -1;    
                    victim->waitingForSolarCore = 0; 
                }
                if (victimArtifact == 1)
                { 
                    victim->holdsLunarBlade = 0;   
                    s->artifacts.lunarBladeHeldBy = -1;   
                    victim->waitingForLunarBlade = 0; 
                }
                if (victimArtifact == 2)
                { 
                    victim->holdsEclipseRelic = 0; 
                    s->artifacts.eclipseRelicHeldBy = -1; 
                    victim->waitingForEclipseRelic = 0; 
                }
                snprintf(victim->actionLog, ACTION_LOG_LEN, "Deadlock! %s forced away.", WEAPON_NAME[wid]);
            }
        }
        sem_post(&s->artifacts.artifactTableLock);
    }
    return NULL;
}

// background thread: adds speed to stamina every 1s tick for all entities
// stun countdown also runs here - 3 ticks = 3 seconds exactly
void *staminaThread(void *arg) {
    GameState *s = (GameState*)arg;
    while (s->gameRunning) 
    {
        usleep(STAMINA_TICK_MS * 1000);
        sem_wait(&s->stateLock);

        for (int i = 0; i < s->numberOfPlayers; i++) 
        {
            Entity *p = &s->players[i]; 
            if (!p->isAlive) 
                continue;
            if (p->isStunned) 
            { 
                if (--p->stunTicksRemaining <= 0)
                {
                    p->isStunned = 0;
                    p->stunTicksRemaining = 0;
                } 
                continue; 
            }
            if (s->currentTurnIsPlayer == 1 && s->currentTurnEntityIndex == i) 
                continue;
            p->stamina += p->speed;
            if (p->stamina > p->maxStamina) 
                p->stamina = p->maxStamina;
        }

        for (int i = 0; i < s->numberOfEnemies; i++) 
        {
            Entity *e = &s->enemies[i]; 
            if (!e->isAlive || s->ultimateAbilityActive) 
                continue;
            if (e->isStunned) 
            { 
                if (--e->stunTicksRemaining <= 0)
                {
                    e->isStunned = 0;
                    e->stunTicksRemaining = 0;
                } 
                continue; 
            }
            if (s->currentTurnIsPlayer == 0 && s->currentTurnEntityIndex == i) 
                continue;
            e->stamina += e->speed;
            if (e->stamina > e->maxStamina) 
                e->stamina = e->maxStamina;
        }

        if (s->ultimateAbilityActive) 
            s->ultimateTimer += STAMINA_TICK_MS / 1000.0f;
        else 
            s->ultimateTimer = 0.0f;

        sem_post(&s->stateLock);
    }
    return NULL;
}

// scans for the first entity at full stamina and assigns it the turn
// call under stateLock - returns 1 if turn was assigned
int findNextTurn(GameState *s) {
    if (s->currentTurnEntityIndex != -1) 
        return 0;
    for (int i = 0; i < s->numberOfPlayers; i++) 
    {
        Entity *p = &s->players[i];
        if (!p->isAlive || p->isStunned) 
            continue;
        if (p->stamina >= p->maxStamina) 
        {
            s->currentTurnEntityIndex = i; 
            s->currentTurnIsPlayer = 1; 
            return 1;
        }
    }
    if (s->ultimateAbilityActive) 
        return 0;
    for (int i = 0; i < s->numberOfEnemies; i++) 
    {
        Entity *e = &s->enemies[i];
        if (!e->isAlive || e->isStunned) 
            continue;
        if (e->stamina >= e->maxStamina) 
        {
            s->currentTurnEntityIndex = i; 
            s->currentTurnIsPlayer = 0; 
            return 1;
        }
    }
    return 0;
}

// handles weapon drop after a kill
// eclipse relic appears after kill 3, then 60% chance of random weapon
void triggerDrop(GameState *s, int killedEnemyIndex, int killedByPlayer) {
    if (s->weaponDrop.pending) 
        return;
    (void)killedEnemyIndex;
    if (s->totalEnemiesKilled >= 10) 
        return;

    if (s->totalEnemiesKilled >= 3 && !s->artifacts.eclipseRelicExists) 
    {
        s->artifacts.eclipseRelicExists = 1;
        s->weaponDrop.weaponId = WEAPON_ECLIPSE_RELIC;
        s->weaponDrop.forPlayerIndex = killedByPlayer;
        s->weaponDrop.decision = -1;
        s->weaponDrop.pending = 1;
        return;
    }

    if (rand() % 10 >= 6) 
        return;

    s->weaponDrop.weaponId = (rand() % (WEAPON_COUNT - 2)) + 1;
    s->weaponDrop.forPlayerIndex = killedByPlayer;
    s->weaponDrop.decision = -1;
    s->weaponDrop.pending = 1;
}

// applies a submitted action to the game state
// arbiter is the only entity that calls this
void applyAction(GameState *s, Action *a) {
    if (a->isPlayer) 
    {
        Entity *p = &s->players[a->sourceEntityIndex];
        switch(a->actionType) 
        {
            case ACTION_STRIKE: 
            {
                Entity *t = &s->enemies[a->targetIndex];
                t->hp -= p->damage;
                if (t->hp <= 0) 
                { 
                    t->hp = 0; 
                    t->isAlive = 0; 
                    t->shouldExit = 1; 
                    s->totalEnemiesKilled++; 
                    triggerDrop(s, a->targetIndex, a->sourceEntityIndex); 
                }
                snprintf(p->actionLog, ACTION_LOG_LEN, "P%d strikes E%d for %d dmg", a->sourceEntityIndex + 1, a->targetIndex + 1, p->damage);
                p->stamina = 0; 
                break;
            }
            case ACTION_EXHAUST: 
            {
                Entity *t = &s->enemies[a->targetIndex];
                t->stamina -= p->damage; 
                if (t->stamina < 0) 
                    t->stamina = 0;
                snprintf(p->actionLog, ACTION_LOG_LEN, "P%d exhausts E%d: -%d ST", a->sourceEntityIndex + 1, a->targetIndex + 1, p->damage);
                p->stamina = 0; 
                break;
            }
            case ACTION_USE_WEAPON: 
            {
                int wid = p->inventory[a->weaponStartSlot];
                if (!wid) 
                { 
                    snprintf(p->actionLog, ACTION_LOG_LEN, "No weapon in slot!"); 
                    p->stamina = 0; 
                    break; 
                }
                Entity *t = &s->enemies[a->targetIndex];
                int dmg = WEAPON_DAMAGE[wid];
                t->hp -= dmg;
                if (t->hp <= 0) 
                { 
                    t->hp = 0; 
                    t->isAlive = 0; 
                    t->shouldExit = 1; 
                    s->totalEnemiesKilled++; 
                    triggerDrop(s, a->targetIndex, a->sourceEntityIndex); 
                }
                // send sigusr2 to asp so the dispatcher pthread_kills the stunned enemy thread
                if (wid == WEAPON_SOLAR_CORE || wid == WEAPON_LUNAR_BLADE || wid == WEAPON_ECLIPSE_RELIC) 
                {
                    if (t->isAlive) 
                    {
                        t->isStunned = 1; 
                        t->stunTicksRemaining = 3;
                        if (s->aspPid > 0) 
                            kill(s->aspPid, SIGUSR2);
                    }
                }
                snprintf(p->actionLog, ACTION_LOG_LEN, "P%d used %s on E%d: -%d HP%s",
                    a->sourceEntityIndex + 1, WEAPON_NAME[wid], a->targetIndex + 1, dmg,
                    (wid == WEAPON_SOLAR_CORE || wid == WEAPON_LUNAR_BLADE || wid == WEAPON_ECLIPSE_RELIC) ?
                    " [STUNNED]" : "");
                p->stamina = 0; 
                break;
            }
            case ACTION_SWAP_IN: 
            {
                int idx = a->ltsIndex;
                if (idx < 0 || idx >= p->longTermStorageCount) 
                {
                    snprintf(p->actionLog, ACTION_LOG_LEN, "Invalid LTS index!");
                } 
                else 
                {
                    int wid = p->longTermStorage[idx];
                    for (int i = idx; i < p->longTermStorageCount - 1; i++) 
                        p->longTermStorage[i] = p->longTermStorage[i + 1];
                    p->longTermStorageCount--;
                    forceAdd(p, wid);
                    syncArtifacts(s);
                    snprintf(p->actionLog, ACTION_LOG_LEN, "P%d swapped in %s", a->sourceEntityIndex + 1, WEAPON_NAME[wid]);
                }
                p->stamina = 0; 
                break;
            }
            case ACTION_HEAL: 
            {
                int h = p->maxHp / 10; 
                p->hp += h; 
                if (p->hp > p->maxHp) 
                    p->hp = p->maxHp;
                snprintf(p->actionLog, ACTION_LOG_LEN, "P%d healed +%d HP", a->sourceEntityIndex + 1, h);
                p->stamina = 0; 
                break;
            }
            case ACTION_SKIP:
                snprintf(p->actionLog, ACTION_LOG_LEN, "P%d skipped turn", a->sourceEntityIndex + 1);
                p->stamina = p->maxStamina / 2; 
                break;
            case ACTION_ULTIMATE:
                // sigstop suspends asp entirely, alarm triggers sigcont after 10s
                if (s->aspPid > 0) 
                    kill(s->aspPid, SIGSTOP);
                s->ultimateAbilityActive = 1; 
                alarm(10);
                snprintf(p->actionLog, ACTION_LOG_LEN, "P%d ULTIMATE! Enemies frozen 10s!", a->sourceEntityIndex + 1);
                p->stamina = 0; 
                break;
            default: 
                break;
        }
    } 
    else 
    {
        Entity *e = &s->enemies[a->sourceEntityIndex];
        if (a->actionType == ACTION_STRIKE) 
        {
            Entity *t = &s->players[a->targetIndex];
            int best = e->damage, bestW = 0;
            for (int i = 0; i < INVENTORY_SLOTS; i++)
            {
                int w = e->inventory[i];
                if (w && WEAPON_DAMAGE[w] > best)
                {
                    best = WEAPON_DAMAGE[w];
                    bestW = w;
                }
            }
            t->hp -= best; 
            if (t->hp <= 0)
            {
                t->hp = 0;
                t->isAlive = 0;
            }
            // send sigusr1 to hip process if stun weapon used
            if (bestW == WEAPON_SOLAR_CORE || bestW == WEAPON_LUNAR_BLADE || bestW == WEAPON_ECLIPSE_RELIC)
            {
                t->isStunned = 1;
                t->stunTicksRemaining = 3;
                if (s->hipPid > 0)
                    kill(s->hipPid, SIGUSR1);
            }
            snprintf(e->actionLog, ACTION_LOG_LEN, "E%d strikes P%d for %d dmg", a->sourceEntityIndex + 1, a->targetIndex + 1, best);
            e->stamina = 0;
        } 
        else 
        {
            snprintf(e->actionLog, ACTION_LOG_LEN, "E%d skipped", a->sourceEntityIndex + 1);
            e->stamina = e->maxStamina / 2;
        }
    }
}

// checks win lose conditions, spawns new wave if all enemies on screen are dead
int checkGameOver(GameState *s) {
    if (s->totalEnemiesKilled >= 10) 
        return 2;
    int allDead = 1;
    for (int i = 0; i < s->numberOfEnemies; i++) 
    {
        if (s->enemies[i].isAlive)
        {
            allDead = 0;
            break;
        }
    }
    if (allDead) 
    {
        int n = rand() % 8 + 2;
        s->numberOfEnemies = n;
        for (int i = 0; i < n; i++) 
            initEnemy(&s->enemies[i]);
        s->waveNumber++;
        snprintf(s->players[0].actionLog, ACTION_LOG_LEN, "=== WAVE %d! %d Decepticons! ===", s->waveNumber + 1, n);
        return 0;
    }
    for (int i = 0; i < s->numberOfPlayers; i++) 
    {
        if (s->players[i].isAlive) 
            return 0;
    }
    return 1;
}

int main() {
    srand(ROLL_NUMBER ^ (unsigned)time(NULL));
    signal(SIGTERM, handleQuit);
    signal(SIGALRM, handleAlarm);
    signal(SIGCHLD, handleChildDeath);

    gameState = createShm();
    memset(gameState, 0, sizeof(GameState));
    initSems(gameState);

    int guiMode = 0;
    printf("=== CHRONO RIFT ===\n[1] TUI Mode\n[2] GUI Mode\nSelect: ");
    fflush(stdout);
    { 
        int choice = 0; 
        scanf("%d", &choice); 
        if (choice == 2) 
            guiMode = 1; 
    }

    int np = 0;
    printf("\n  CHRONO RIFT - Transformers Edition\n");
    printf("  Enter number of Autobots (1-4): ");
    fflush(stdout);
    scanf("%d", &np);
    if (np < 1 || np > 4) 
        np = 1;

    gameState->numberOfPlayers = np;
    gameState->numberOfEnemies = rand() % 8 + 2;
    gameState->gameRunning = 1;
    gameState->gameResult = 0;
    gameState->waveNumber = 0;
    gameState->totalEnemiesKilled = 0;
    gameState->ultimateAbilityActive = 0;
    gameState->arbiterPid = getpid();
    gameState->currentTurnEntityIndex = -1;
    gameState->currentTurnIsPlayer = -1;
    gameState->weaponDrop.pending = 0;
    gameState->weaponDrop.decision = -1;
    gameState->artifacts.solarCoreHeldBy = gameState->artifacts.lunarBladeHeldBy = gameState->artifacts.eclipseRelicHeldBy = -1;
    gameState->artifacts.eclipseRelicExists = 0;
    gameState->ui.activePlayer = -1; 
    gameState->ui.showMenu = 0;
    gameState->ui.menuSel = 0; 
    gameState->ui.showTargetMenu = 0;
    gameState->ui.targetSel = 0; 
    gameState->ui.showWeaponMenu = 0;
    gameState->ui.weaponMenuSel = 0; 
    gameState->ui.showLtsMenu = 0;
    gameState->ui.ltsSel = 0; 
    gameState->ui.showPickup = 0;

    for (int i = 0; i < np; i++) 
        initPlayer(&gameState->players[i], np);
    for (int i = 0; i < gameState->numberOfEnemies; i++) 
        initEnemy(&gameState->enemies[i]);

    // fork hip (gui or tui) then asp
    hipPid = fork();
    if (hipPid == 0)
    {
        if (guiMode) 
        { 
            execl("./hip_gui", "hip_gui", NULL); 
            perror("execl hip_gui"); 
        }
        else       
        { 
            execl("./hip", "hip", NULL);         
            perror("execl hip");     
        }
        exit(1);
    }
    gameState->hipPid = hipPid;

    aspPid = fork();
    if (aspPid == 0)
    {
        execl("./asp", "asp", NULL);
        perror("execl asp");
        exit(1);
    }
    gameState->aspPid = aspPid;

    pthread_t st, dt;
    pthread_create(&st, NULL, staminaThread, gameState);
    pthread_create(&dt, NULL, deadlockThread, gameState);

    // main game loop - player turns wait indefinitely, enemy turns have 3s timeout
    while (gameState->gameRunning) 
    {
        sem_wait(&gameState->stateLock);
        int found = findNextTurn(gameState);
        sem_post(&gameState->stateLock);

        if (!found) 
        { 
            usleep(50000); 
            continue; 
        }

        if (gameState->currentTurnIsPlayer) 
        {
            sem_wait(&gameState->actionSubmitted);
        } 
        else 
        {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 3;
            int r = sem_timedwait(&gameState->actionSubmitted, &ts);
            if (r != 0) 
            {
                // enemy timed out - force skip
                sem_wait(&gameState->stateLock);
                int ei = gameState->currentTurnEntityIndex;
                if (ei >= 0 && ei < MAX_ENEMIES) 
                {
                    gameState->enemies[ei].stamina = gameState->enemies[ei].maxStamina / 2;
                    snprintf(gameState->enemies[ei].actionLog, ACTION_LOG_LEN, "E%d timed out (Skip)", ei + 1);
                }
                gameState->currentTurnEntityIndex = -1;
                gameState->currentTurnIsPlayer = -1;
                sem_post(&gameState->stateLock);
                sem_post(&gameState->actionConsumed);
                continue;
            }
        }

        sem_wait(&gameState->stateLock);
        applyAction(gameState, &gameState->pendingAction);
        syncArtifacts(gameState);
        gameState->actionReady = 0;
        gameState->currentTurnEntityIndex = -1;
        gameState->currentTurnIsPlayer = -1;
        sem_post(&gameState->stateLock);
        sem_post(&gameState->actionConsumed);

        // weapon drop: give gui 500ms to render the pickup prompt before polling
        if (gameState->weaponDrop.pending) 
        {
            usleep(500000);
            int dropWait = 0;
            while (gameState->weaponDrop.decision == -1 && gameState->gameRunning) 
            {
                usleep(100000);
                if (++dropWait > 40)
                { 
                    gameState->weaponDrop.decision = 0; 
                    break; 
                }
            }
            sem_wait(&gameState->stateLock);
            int wid = gameState->weaponDrop.weaponId;
            int pi = gameState->weaponDrop.forPlayerIndex;
            int dec = gameState->weaponDrop.decision;
            gameState->weaponDrop.pending = 0;
            gameState->weaponDrop.decision = -1;
            sem_post(&gameState->stateLock);

            if (dec == 1 && pi >= 0 && pi < gameState->numberOfPlayers) 
            {
                forceAdd(&gameState->players[pi], wid);
                syncArtifacts(gameState);
                snprintf(gameState->players[pi].actionLog, ACTION_LOG_LEN, "Picked up %s!", WEAPON_NAME[wid]);
            } 
            else 
            {
                for (int i = 0; i < gameState->numberOfEnemies; i++) 
                {
                    if (gameState->enemies[i].isAlive) 
                    {
                        forceAdd(&gameState->enemies[i], wid);
                        syncArtifacts(gameState);
                        break;
                    }
                }
            }
        }

        int res = checkGameOver(gameState);
        if (res == 1)
        {
            gameState->gameResult = 1;
            gameState->gameRunning = 0;
            sleep(4);
            break;
        }
        if (res == 2)
        {
            gameState->gameResult = 2;
            gameState->gameRunning = 0;
            sleep(4);
            break;
        }
    }

    int finalResult = gameState->gameResult;
    int finalKills = gameState->totalEnemiesKilled;

    kill(hipPid, SIGTERM); 
    kill(aspPid, SIGTERM); 
    sleep(1);
    pthread_cancel(st); 
    pthread_cancel(dt);
    sem_destroy(&gameState->stateLock);
    sem_destroy(&gameState->actionSubmitted);
    sem_destroy(&gameState->actionConsumed);
    sem_destroy(&gameState->ui.uiLock);
    sem_destroy(&gameState->artifacts.artifactTableLock);
    munmap(gameState, sizeof(GameState));
    shm_unlink(SHM_NAME);
    printf("Game ended. Result=%d  Kills=%d\n", finalResult, finalKills);
    return 0;
}
