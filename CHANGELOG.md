# Release notes

The version is shown at the bottom right of the title screen
(`engine/src/core/vs_version.h`). Each release says whether the data pack
must be rebuilt; when it doesn't, only `default.xex` (or the PC executable)
changes.

## 1.0.0-rc1 (release candidate, not tagged yet)

The first complete build of Vampire Survivors 0.2.10 (Steam build,
manifest 8292959317163221061), on Xbox 360.

**Xbox 360 only:** the game is no longer playable on PC. The PC build is
now `vsdemake-test`, a headless test tool for development (no window,
controls or sound output); see "Why Xbox 360 only" in the README.

**Data pack:** a pack built by the current patcher (88 entries, 30 of them
sounds) works as is. Packs from before the sound work (no `SND` entries)
still load but play no sound; rebuild them with `vspatch build`.

**Save:** `vsdemake.sav` from earlier builds keeps working. Characters
unlocked before the shop existed now need buying once at the character
select, as in the original.

### Game

- **Stages:** Mad Forest and Inlaid Library, with day/night, the stage
  events (bat and ghost swarms, flower and jellyfish walls, Medusa heads,
  skull swarms, Shade bombs), the bosses and their treasure, and the map
  bonus items.
- **Characters:** the 13 playable characters of the version, unlocked and
  bought as in the original.
- **Weapons:** all of them except Pentagram (left out on purpose), with the
  nine evolutions, Vandalier included, and all 14 passive items.
- **Enemies:** the version's enemies, bosses and their behaviours, including
  the Shade bombs that flash a speeding-up red ring and explode.
- **Pickups:** gems, gold, coin bags, Floor Chicken, hearts, Vacuum,
  Rosary, Orologion (with its blue time-stop flash), Clover, Nduja Fritta
  and treasure chests, with the original's drop odds.
- **Treasure chests:** the original's opening scene: coins, ribbons, reels,
  prize stars, the level-3 fireworks and the rising bubbles.
- **Level-up screen** with the original's icons and texts.
- **Power Up shop:** 14 permanent bonuses with the original's prices and
  refund.
- **Collection** and **Achievements** screens, as in the original's main
  menu; 40 achievements with their unlocks and gold prizes.
- **Pause and options:** volumes, Flashing VFX, Damage Numbers, the weapons
  and stats panels, Quit / Resume.
- **Sound and music:** the original's sound effects and stage tracks with
  their volume, pitch and repeat rules.
- **Look:** additive blending where the original uses it (glows, trails,
  explosions, the chest scene), a Courier-style font like the original's
  menus, and the demake's own title screen art.

### Robustness

- The data pack is checked in full when it loads: a damaged or edited pack
  is refused with the name of the bad entry instead of crashing the game.
- Saves are plain text with a checksum and a backup copy.

### Known limitations

- Hyper mode and Green Acres can be unlocked and are saved, but cannot be
  played yet.
- Console performance with very large crowds and long sessions is still
  being checked on hardware.
- The libxenon (Free60) build is a fallback without sound or alpha
  blending; the XDK build is the supported one.

### Building and installing

See [Install](README.md#install) in the README, and
[docs/XBOX360_GUIDE.md](docs/XBOX360_GUIDE.md) for the console.
