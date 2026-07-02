# Flipper DOOM

A DOOM-style first-person shooter demake for the Flipper Zero
(128×64 monochrome LCD). Built on the raycaster engine from
[FlipperCatacombs](https://github.com/apfxtech/FlipperCatacombs) (a port of
*Catacombs of the Damned* / arduboy3d by jhhoward), re-skinned with graphics
converted **at build time from your own DOOM shareware WAD**.

---

## Quick Start

1. Copy `dist/flipdoom.fap` to your SD card under `SD/apps/Games/`
   (or run `ufbt launch` with the Flipper connected via USB).
2. On the Flipper: **Apps → Games → Flipper DOOM**.
3. Title screen → main menu → select **Play** with OK.
4. Fight through procedurally generated levels, find the exit gate on each
   floor, survive as deep as you can. Your score and high score are saved
   automatically.

---

## Controls

### Title screen

| Button | Action |
|---|---|
| Any button | Skip the title screen |

### Main menu

| Button | Action |
|---|---|
| **Up / Down** | Move cursor (Play / Sound on-off / Score / High score) |
| **OK** | Select item |
| **Back (hold ~300 ms)** | Exit the app |

### In game

| Button | Action |
|---|---|
| **Up / Down** | Move forward / backward |
| **Left / Right** | Turn left / right |
| **OK** | Fire the shotgun (hold for continuous fire) |
| **OK held + Left / Right** | **Strafe** (circle-strafe while firing) — essential for dodging fireballs |
| **Back (hold ~300 ms)** | Leave the game and return to the menu |

Notes:

- Firing costs ammo (bottom HUD bar); ammo regenerates when not shooting.
- Strafing only works while OK is held. Tap OK to fire while turning
  normally; hold OK to switch the side arrows into dodge mode.
- Walking into a backpack opens it; walking over pickups collects them.

### HUD

- **Bottom bar with cross icon** — health.
- **Bar above it with bullets icon** — ammo.
- Screen border flashes when you take damage.
- Pickup/event messages appear at the top of the screen.

---

## Gameplay

### Enemies

| Enemy | Behavior |
|---|---|
| **Zombieman** | Weak, shoots from range, appears from level 1 |
| **Sergeant** | Fast, shoots hard, keeps distance |
| **Imp** | Throws fireballs, backs away if you get close |
| **Demon** | Melee tank — charges you and bites |

Difficulty scales with depth: early levels spawn mostly zombies; imps join
from level 3, and demons dominate from level 6.

### Exploding barrels

Shooting a barrel triggers an area explosion that deals heavy damage to every
enemy nearby (one-shots zombies and sergeants) and hurts you if you stand too
close — though it will never kill you outright, it can leave you at 1 HP.
Barrels sometimes leave a pickup behind.

### Pickups

| Item | Effect |
|---|---|
| Stimpack | Restores health |
| Backpack (walk into it) | Bonus points |
| Armor / helmet / bonus items | Points |

### Scoring

Points are awarded for floors cleared, kills per enemy type, and items
collected. Escaping the base (final exit) grants a large bonus. Score and
high score are stored on the SD card (`apps_data/flipdoom/`) and persist
between sessions.

---

## Sound

Tone-based sound effects through the Flipper buzzer (shotgun blast, hits,
kills, pickups, player damage), all tuned within the buzzer's physical
100–2500 Hz range. Sound can be toggled in the main menu; the system
Stealth Mode is respected.

---

## Building from source

Requires Python 3 and [ufbt](https://pypi.org/project/ufbt/):

```sh
pip install ufbt

cd FlipperDoom

# 1) Generate the sprite header from YOUR shareware WAD (not included here):
python3 tools/extract_doom_assets.py /path/to/Doom1.WAD

# 2) Build / install:
ufbt          # produces dist/flipdoom.fap
ufbt launch   # builds, installs and runs on a connected Flipper
```

### About the assets

No id Software assets are stored in this repository. The generated header
`game/Generated/DoomSprites.inc.h` is produced locally by
`tools/extract_doom_assets.py`, which decodes sprites from the user's own
shareware WAD (freely distributable as a whole), rescales them and quantizes
to 1-bit (black / 50% checker / white) in the engine's sprite formats.
Do not redistribute the generated header — always regenerate it from a WAD
you own. The "DOOM" title lettering and the HUD icons are original pixel art
made for this project. Preview images of the converted assets are written to
`tools/preview/`.

Entity mapping (game mechanics are inherited from the Catacombs engine):

| Engine entity | DOOM sprite | Role |
|---|---|---|
| Skeleton | Demon (SARG) | melee tank |
| Mage | Imp (TROO) | fireball thrower |
| Bat | Sergeant (SPOS) | fast shooter |
| Spider | Zombieman (POSS) | weak shooter |
| Weapon | Shotgun (SHTG + SHTF flash) | first person, centered |
| Urn | Barrel (BAR1) | explodes when shot |
| Potion | Stimpack (STIM) | health |
| Chest / opened | Backpack (BPAK) / Clip (CLIP) | treasure |
| Crown / scroll / coins | Armor / helmet / potion bottle (ARM1, BON2, BON1) | points |
| Sign | Skull pile (POL5) | decoration |
| Projectiles | Fireball (BAL1) | player & enemies |

## Why not a real doomgeneric port?

The hardware makes it impossible — not a software choice:

| | Flipper Zero | Real DOOM (doomgeneric) |
|---|---|---|
| Total RAM | 256 KB | — |
| Free heap for apps | ~140 KB | ~7 MB (zone memory + framebuffer) |
| Engine binary | FAPs load fully into RAM | ~524 KB compiled for Cortex-M4 |

The Flipper cannot execute code from the SD card (no XIP), so it can't even
load the DOOM engine binary. DOOM ports to 256 KB microcontrollers (GBA,
nRF52840) rely on memory-mapped flash, which the Flipper does not have. This
demake keeps the aesthetic with an engine that actually fits: ~30 KB loaded,
30 FPS on the 64 MHz Cortex-M4.

## License

Same license as the base project (see `LICENSE`). WAD contents are property
of id Software; the extraction tool only transforms them locally for
personal use.
