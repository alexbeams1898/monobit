# 🔒 Prison Break Game
### *A comedic top-down roguelike about getting out before things get weird*

> ⚠️ Early development — everything is subject to change.

---

## What is this?

You are Bud. You're a cop. A bad one — but a loyal drinking buddy.

You've got a walkie talkie, a corrupt badge, and a whole prison full of inmates
you're helping break out one run at a time.

Get them out.

---

## Gameplay

A top-down action roguelike inspired by **Vampire Survivors** and the **Dark
Souls** series. Each run drops you into a procedurally generated prison. You
fight through escalating waves of enemies, craft weapons from whatever you find,
level up your stats, and try to reach the exit before the guards — or something
much worse — stops you.

- **Craft weapons** from materials dropped by enemies and found in the world
- **Level up** STR, DEX, END, and LCK by killing enemies
- **Build your loadout** — dual one-handed weapons or a powerful two-hander
- **Equip armor** assembled from enemy drops for build variety
- **Find upgrade stations** scattered across the map to evolve your weapons
- **Discover recipes** that are permanently unlocked for future runs
- **Defeat the final boss** to escape — the exit is earned, not found

Enemies start as normal prison guards. They don't stay that way.

---

## Design Goals

- Simple to pick up, deep to master
- Procedural generation that always feels fair, never predetermined
- A fully custom engine — built to last, built to own
- Modding as a first-class feature, not an afterthought
- Comedic tone throughout — body-gore humor, absurd not disturbing

---

## Your Character

Create your character once at the start of a fresh save. Allocate your starting
stats, pick a perk, and go. Everything after that is earned in the run.

**Stats:**
| Stat | What it does |
|------|-------------|
| STR | Attack power. Favors heavy two-handed weapons. |
| DEX | Attack speed. Favors light dual-wield builds. |
| END | Max health. |
| LCK | Rare drop rate and ranged weapon accuracy. |

DEF is derived automatically from your stats, level, and equipped armor — you
never allocate points into it directly.

---

## Weapons & Crafting

All weapons are crafted from materials found in the world and dropped by enemies.
Nothing is handed to you. Even fists are a viable build if you know what you're
doing.

The same weapon can be crafted from different materials — and the materials you
use affect the weapon's starting stat bias. A shiv made from a toothbrush plays
differently than one made from a key.

Weapons are upgraded at stations scattered randomly across the map, or via a
portable upgrader purchased in the meta store.

---

## Meta Progression

Money earned during runs carries over. Spend it between runs on crafting
materials, QOL upgrades, character cosmetics, and unlockables.

Rare discoveries found during runs — crafting guides, hidden schematics — can
permanently change what you know and what you can build.

The further you go, the more you learn. The more you learn, the further you go.

---

## Enemies

Guards at first. Then something else. The escalation is gradual, then it isn't.

By the time you reach the final boss, you'll have stopped recognizing what
you're fighting.

---

## Drip

The game has a cosmetic system — clothes, armor skins, weapon skins — that is
entirely separate from gameplay. Unlock it through progression, achievements,
and mods. It never affects how you play. It just affects how you look while
doing it.

---

## Modding

Modding is built into the engine from day one. Supply your own:
- Map layouts and tile assets
- Enemy roster (mapped to the game's rank hierarchy schema)
- Weapons, crafting recipes, and materials
- Characters, perks, and stat spreads
- Cosmetics and drip

The engine fills any missing assets with base game defaults. You don't need
to build everything from scratch — just what you want to change.

---

## Engine

Built from scratch. No Unity. No Unreal. No shortcuts.

**Stack:**
- C++17/20
- SDL2 — windowing, input, audio
- OpenGL — hardware-accelerated rendering
- entt — ECS architecture
- FMOD — audio middleware
- CMake — cross-platform build system

The engine is designed around an Entity-Component-System architecture, meaning
all game content — enemies, weapons, maps, characters — is defined in config
files. Adding new content never requires touching engine code.

The goal is a FromSoftware-style model: build the engine once, build games on
top of it, own it completely.

---

## Status

Active development. Custom engine is running — windowing, input, OpenGL rendering,
ECS, sprite atlas, camera system, JSON config loader, and enemy AI (flow field
pathfinding, collision) are all implemented.

Target platform: **Steam** (Windows/Mac/Linux). Cross-platform support planned for later.

---

## Documentation

- [docs/DESIGN.md](docs/DESIGN.md) — full game design document (gameplay, systems, enemies, narrative)