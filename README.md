# Transformers: Chrono Rift

A turn-based combat RPG set in the Transformers universe, built in C++ using SFML for the GUI and ncurses for the TUI. The game runs as three cooperating Linux processes communicating over POSIX shared memory and semaphores.

**Authors:** Kasim Zeeshan Alvi (24i-0549) · Abdullah Junaid (24i-0569) — CS-A, FAST NUCES Islamabad

---

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Features](#features)
- [Characters](#characters)
- [Weapons](#weapons)
- [Prerequisites](#prerequisites)
- [Building & Running](#building--running)
  - [With Docker (recommended)](#with-docker-recommended)
  - [Bare Metal (Linux)](#bare-metal-linux)
- [How to Play](#how-to-play)
- [Project Structure](#project-structure)

---

## Overview

Chrono Rift is a wave-based, turn-order combat game. Up to **4 Autobot players** battle through waves of Decepticon enemies (Megatron, Soundwave, Starscream, and Grunt-class soldiers). Each player manages a limited inventory of weapons, stamina, and an ultimate ability that freezes all enemies for 10 seconds.

The project demonstrates several OS-level concepts:

- Multi-process design with `fork` / `exec`
- POSIX shared memory (`shm_open`, `mmap`)
- Named and unnamed POSIX semaphores for synchronisation
- `pthreads` (one thread per enemy in ASP, a dedicated render thread in HIP)
- POSIX signals for stun mechanics (`SIGUSR2`) and ultimate ability (`SIGALRM`)
- Deadlock detection and prevention via resource-ordering for three exclusive artifact weapons

---

## Architecture

The game is composed of three executables that run concurrently and share state through a single `GameState` struct in shared memory (`/chrono_rift_shm`):

```
┌─────────────────────────────────────────────┐
│              POSIX Shared Memory             │
│  GameState: entities, turn order, artifacts  │
└────────────┬───────────────┬────────────────┘
             │               │
    ┌────────┴──────┐ ┌──────┴────────┐
    │   arbiter     │ │    asp        │
    │  (Game Logic) │ │  (AI Enemies) │
    │               │ │               │
    │ • turn engine │ │ • 1 pthread   │
    │ • wave spawner│ │   per enemy   │
    │ • deadlock    │ │ • SIGUSR2     │
    │   detection   │ │   stun handler│
    │ • artifact    │ └───────────────┘
    │   lock table  │
    └────────┬──────┘
             │  forks
    ┌────────┴──────────────────────────┐
    │             hip / hip_gui         │
    │         (Human Interface)         │
    │                                   │
    │  hip      — ncurses TUI           │
    │  hip_gui  — SFML fullscreen GUI   │
    │  • per-player pthreads            │
    │  • dedicated render thread        │
    │  • weapon drop prompts            │
    └───────────────────────────────────┘
```

| Process | Binary | Purpose |
|---------|--------|---------|
| **Arbiter** | `arbiter` | Central authority — manages shared memory, turn scheduling, wave logic, deadlock detection, artifact ownership |
| **HIP** | `hip` / `hip_gui` | Human Interface Process — ncurses TUI (`hip`) or SFML GUI (`hip_gui`) for player input and rendering |
| **ASP** | `asp` | Automated Strategic Process — runs one pthread per active enemy, implements enemy AI |

---

## Features

- **Wave-based combat** — enemy count scales with each new wave
- **Turn-based engine** — speed-sorted turn order, stamina regeneration per tick
- **Inventory system** — 20-slot inventory with variable weapon sizes; overflow auto-moves weapons to long-term storage (LTS)
- **Exclusive artifact weapons** — Solar Core, Lunar Blade, and Eclipse Relic use a resource-ordering lock table to prevent deadlock
- **Ultimate ability** — sends `SIGALRM` to freeze all enemies (ASP suspended via `SIGSTOP`) for 10 seconds
- **Stun mechanic** — `SIGUSR2` dispatched to a specific enemy pthread, which sleeps for exactly 3 seconds
- **SFML GUI** — animated sprites, weapon projectiles, HP/stamina bars, inventory grid, wave banners, and screen transitions (main menu → battle → victory/defeat)
- **Full sound design** — per-weapon SFX, hit/miss/death sounds, background music that switches on ultimate activation
- **Deadlock detection** — arbiter monitors circular wait conditions among artifact holders and resolves them

---

## Screenshots

### SFML GUI — Main Menu

![Main Menu](OS_GUIMainMenu.png)

### SFML GUI — Battle Screen

![GUI Battle Screen](OS_GUI.png)

### ncurses TUI — Terminal Interface

![Terminal Interface](OS_TerminalUserInterface.png)

### Terminal Output

![Terminal](OS_Terminal.png)

---

## Characters

### Playable (Autobots)

| Character | Role |
|-----------|------|
| **Optimus Prime** | High HP tank |
| **Bumblebee** | Balanced fighter |
| **Arcee** | Fast, lower HP |
| **Ratchet** | Support/healer |

### Enemies (Decepticons)

| Character | Type |
|-----------|------|
| **Megatron** | Boss (wave 1+) |
| **Soundwave** | Boss (wave 1+) |
| **Starscream** | Boss (wave 1+) |
| **Grunt** | Standard enemy (fills remaining slots) |

---

## Weapons

| Weapon | ID | Slots | Damage | Notes |
|--------|----|-------|--------|-------|
| Solar Core | 1 | 10 | 95 | Exclusive artifact |
| Lunar Blade | 2 | 10 | 90 | Exclusive artifact |
| Iron Halberd | 3 | 7 | 55 | |
| Venom Dagger | 4 | 4 | 30 | |
| Thunderstaff | 5 | 6 | 50 | |
| Obsidian Axe | 6 | 5 | 45 | |
| Frostbow | 7 | 6 | 48 | |
| Splinter Stick | 8 | 2 | 12 | Smallest, fast grab |
| Eclipse Relic | 9 | 8 | 100 | Exclusive artifact, rarest |

Exclusive artifacts (Solar Core, Lunar Blade, Eclipse Relic) can only be held by one entity at a time. The arbiter enforces acquisition order to prevent circular wait.

---

## Prerequisites

### Docker (recommended — no host setup needed)

- [Docker](https://docs.docker.com/get-docker/)
- An X server or VNC for the SFML GUI window (see note below)

### Bare metal (Linux)

```
g++ with C++17 support
libsfml-dev      (graphics, window, audio, system)
libncurses-dev
librt / pthread  (usually part of glibc)
```

On Ubuntu/Debian:

```bash
sudo apt-get install build-essential libsfml-dev libncurses-dev
```

> **GUI on headless / WSL / Docker:** The SFML window requires a display. Set `DISPLAY` and forward X11, or use a VNC/Xvfb setup. On native Linux desktops this works out of the box.

---

## Building & Running

### With Docker (recommended)

```bash
# 1. Build the image
docker build -t chrono-rift .

# 2. Run with X11 forwarding (Linux host)
docker run -it \
  -e DISPLAY=$DISPLAY \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  chrono-rift bash run.sh

# 3. For headless / no display — TUI only (skips hip_gui)
docker run -it chrono-rift bash
# inside container:
make
./arbiter
```

### Bare Metal (Linux)

```bash
git clone https://github.com/aj-resp/transformers-combat-sfml.git
cd transformers-combat-sfml

make          # builds arbiter, hip, hip_gui, asp
bash run.sh   # builds, copies binaries to /tmp, launches arbiter
```

`run.sh` copies all binaries and the `assets/` directory to `/tmp` before launching so that relative asset paths resolve correctly regardless of where you cloned the repo.

To do a clean rebuild:

```bash
make clean && make
```

---

## How to Play

The game launches the **SFML GUI** (`hip_gui`) by default. The arbiter forks HIP and ASP automatically; you only need to start the arbiter.

### Controls (in-game)

| Key | Action |
|-----|--------|
| `↑` / `↓` | Navigate menus |
| `Enter` | Confirm selection |
| `Escape` | Back / cancel |

### Actions per turn

- **Strike** — basic attack against a chosen enemy
- **Exhaust** — powerful attack that costs stamina (can stun enemy for 3 s)
- **Use Weapon** — deal weapon damage from inventory
- **Swap In** — pull a weapon from long-term storage into active inventory
- **Heal** — restore HP at the cost of stamina
- **Ultimate** — freeze all enemies for 10 seconds (one-time, charges over time)
- **Skip** — pass the turn

### Inventory management

Each player has 20 inventory slots. Weapons occupy 2–10 slots depending on size. When your inventory is full and you pick up a new weapon, the arbiter automatically evicts the smallest weapon(s) to long-term storage to make room. You can swap weapons between inventory and LTS during your turn.

---

## Project Structure

```
.
├── arbiter/
│   ├── arbiter.cpp      # Game logic, turn engine, wave manager, deadlock detection
│   └── shared.h         # Shared memory layout (GameState, Entity, ArtifactTable…)
├── hip/
│   ├── hip.cpp          # ncurses TUI — per-player pthreads, render thread
│   ├── hip_gui.cpp      # SFML GUI — sprites, animations, projectiles, screens
│   └── shared.h         # Same layout (copy) for hip process
├── asp/
│   ├── asp.cpp          # Enemy AI — one pthread per enemy, signal-based stun
│   └── shared.h         # Same layout (copy) for asp process
├── assets/
│   ├── sprites/
│   │   ├── players/     # idle / attack / hurt / dead PNGs for each Autobot
│   │   ├── enemies/     # idle / attack / hurt / dead PNGs for each Decepticon
│   │   ├── weapons/     # weapon icon PNGs
│   │   └── ui/          # HP/stamina bars, buttons, overlays, cursor, logo
│   ├── backgrounds/     # main_menu, battle_earth, battle_cybertron, victory, defeat
│   └── audio/
│       ├── music/       # battle_normal, battle_ultimate, main_menu, victory, defeat
│       └── sfx/         # per-weapon sounds, hit/miss/death/stun/heal/menu SFX
├── Dockerfile
├── Makefile
├── requirements.txt
└── run.sh
```
