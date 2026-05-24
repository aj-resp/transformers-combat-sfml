// Kasim Zeeshan Alvi || 24i0549 || CS-A
// Abdullah Junaid    || 24i0569 || CS-A
// asp.cpp - automated strategic process: one pthread per enemy, signal-based stun

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
#include "shared.h"

GameState *gameState = NULL;

// thread registry so sigusr2 dispatcher can pthread_kill the right enemy thread
static pthread_t    threadRegistry[MAX_ENEMIES];
static int          threadActive[MAX_ENEMIES];
static pthread_mutex_t registryMtx = PTHREAD_MUTEX_INITIALIZER;

// per-thread sigusr2 handler: signal-based stun pauses thread for exactly 3 seconds
static void enemyStunHandler(int sig) {
    (void)sig;
    sleep(3);
}

// process-level sigusr2 dispatcher: forwards stun to the correct enemy thread
static void aspStunDispatcher(int sig) {
    (void)sig;
    if (!gameState) return;
    pthread_mutex_lock(&registryMtx);
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (threadActive[i] && gameState->enemies[i].isStunned) {
            pthread_kill(threadRegistry[i], SIGUSR2);
            break;
        }
    }
    pthread_mutex_unlock(&registryMtx);
}

GameState *attachShm() {
    int fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (fd < 0) { perror("asp shm_open"); exit(1); }
    GameState *s = (GameState*)mmap(NULL, sizeof(GameState),
                                    PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (s == MAP_FAILED) { perror("asp mmap"); exit(1); }
    close(fd);
    return s;
}

int findAlivePlayer(GameState *s) {
    int alive[MAX_PLAYERS], c = 0;
    for (int i = 0; i < s->numberOfPlayers; i++)
        if (s->players[i].isAlive) alive[c++] = i;
    return c ? alive[rand() % c] : 0;
}

typedef struct { int idx; int birthWave; } ETA;

// one thread per enemy: waits for its turn, decides action, submits via semaphore protocol
void *enemyThread(void *arg) {
    ETA *a = (ETA*)arg;
    int ei = a->idx;
    int bw = a->birthWave;
    free(a);

    GameState *s = gameState;

    pthread_mutex_lock(&registryMtx);
    threadRegistry[ei] = pthread_self();
    threadActive[ei]   = 1;
    pthread_mutex_unlock(&registryMtx);

    // install per-thread sigusr2 handler for signal-based stun
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = enemyStunHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR2, &sa, NULL);

    while (s->gameRunning) {
        // exit immediately if shouldExit set by arbiter on kill
        if (s->enemies[ei].shouldExit) break;
        if (s->waveNumber != bw) break;

        if (s->currentTurnIsPlayer != 0 ||
            s->currentTurnEntityIndex != ei ||
            !s->enemies[ei].isAlive) {
            usleep(10000);
            continue;
        }

        Action act;
        memset(&act, 0, sizeof(act));
        act.sourceEntityIndex = ei;
        act.isPlayer          = 0;

        // 80% strike 20% skip
        if (rand() % 5 == 0) {
            act.actionType = ACTION_SKIP;
        } else {
            act.actionType = ACTION_STRIKE;
            act.targetIndex = findAlivePlayer(s);
        }

        // acquire actionConsumed then re-verify turn ownership before submitting
        sem_wait(&s->actionConsumed);
        if (s->currentTurnIsPlayer != 0 || s->currentTurnEntityIndex != ei) {
            sem_post(&s->actionConsumed);
            continue;
        }
        sem_wait(&s->stateLock);
        s->pendingAction = act;
        s->actionReady   = 1;
        sem_post(&s->stateLock);
        sem_post(&s->actionSubmitted);
    }

    pthread_mutex_lock(&registryMtx);
    threadActive[ei] = 0;
    pthread_mutex_unlock(&registryMtx);

    pthread_exit(NULL);
}

void handleTerminate(int sig) {
    (void)sig;
    if (gameState) gameState->gameRunning = 0;
    exit(0);
}

void spawnWaveThreads(int count, int wave) {
    for (int i = 0; i < count; i++) {
        ETA *a = (ETA*)malloc(sizeof(ETA));
        a->idx = i;
        a->birthWave = wave;
        pthread_t tid;
        pthread_create(&tid, NULL, enemyThread, a);
        pthread_detach(tid);
    }
}

int main() {
    signal(SIGTERM, handleTerminate);
    // process-level sigusr2 dispatcher forwards stun to correct enemy thread
    signal(SIGUSR2, aspStunDispatcher);
    signal(SIGUSR1, SIG_IGN);

    srand(ROLL_NUMBER + getpid());

    sleep(1);
    gameState = attachShm();

    memset(threadRegistry, 0, sizeof(threadRegistry));
    memset(threadActive,   0, sizeof(threadActive));

    int currentWave = gameState->waveNumber;
    spawnWaveThreads(gameState->numberOfEnemies, currentWave);

    while (gameState->gameRunning) {
        if (gameState->waveNumber != currentWave) {
            currentWave = gameState->waveNumber;
            usleep(300000);
            spawnWaveThreads(gameState->numberOfEnemies, currentWave);
        }
        usleep(200000);
    }
    munmap(gameState, sizeof(GameState));
    return 0;
}
