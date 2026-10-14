# Audio

Lines refer to `work/main.deob.js`. Sound manager 7198–7236, preload list
(search `load"]["audio"]`), stage music choice 5049, music start 6944–6950,
scene transitions 6484–6498 and 7058–7161.

## Files (sound key → file in `assets/sfx/`)

| Key | File | Key | File |
| --- | --- | --- | --- |
| Alert | sfx_alarm_loop6 | LittleHeart | sfx_littlepickup |
| BGM_Forest | VS_Music_v04-01 | LossSFX | sxf_loss |
| BGM_GameOver | VS_GameOver_v02-01 | MagicMissile | VS_ProjectileMagic_v04-03 |
| BGM_Intro | VS_TitleIntro_v01-03 | Orologion | sfx_sounds_impact9 |
| BGM_Library | bgm_elrond_library | PentagramSFX | sfx_wpn_cannon3 |
| BGM_Library_Legacy | bgm_library | Roast | sfx_sounds_powerup2 |
| ClickIn | sfx_sounds_pause7_in | Rosary | sfx_exp_double1 |
| ClickOut | sfx_sounds_pause7_out | Shot | VS_Projectile_v06-02 |
| Coin | sfx_coin_double4 | ThingFound | VS_TreasureFound_v04-01 |
| Explosion | sfx_exp_odd1 | Treasure1 | VS_Treasure1Small_v04-03 |
| Fireloop | sfx_fireloop | Treasure2 | VS_Treasure2Medium_v02-03 |
| Gem | VS_Gem_v04-01-B | Treasure3 | VS_Treasure3Large_v01-03 |
| Hit | VS_EnemyHit_v06-02 | Vacuum | sfx_sounds_powerup18 |
| Holywater | sfx_frostimpact | Whip | sfx_sounds_impact15_2 |
| LevelUp | VS_LevelUp_v02-02 | Lightning | sfx_lightningimpact |

All OGG Vorbis, 44.1 kHz; the two library tracks are stereo, the rest mono.

## Sound effects: `PlaySound(key, config, delay, maxInstances)` (line 7212)

1. Nothing happens when the sound-volume option is 0.
2. Throttle: when `delay > 0` and `maxInstances > 0`, the key keeps a
   counter; if it is already `>= maxInstances` the sound is not played;
   otherwise the counter goes up and comes back down `delay` ms later.
3. Volume: `config.volume × 0.5`, or 0.1 when there is no `config.volume`;
   then × the sound-volume option (default 0.5).
4. `config.detune` is in cents (pitch × 2^(cents/1200)); `config.rate`
   multiplies the playback speed (pitch and speed together, as Phaser).
   Every play starts a new voice; sounds never loop here.

## Music

- `PlayMusic(key, {volume, loop, rate, detune})`: one instance per key;
  remembers `volume` as its base, plays at `volume × music-volume option`
  (default 0.5).
- `FadeMusic(key, v, ms)`: linear fade to `v × music option` over `ms`.
- `StopMusic(key)`.
- Stage track (line 5049): with the "stream-safe" option on (default) the
  stage's `BGM` field; otherwise `legacyBGM` when the stage has one. Mad
  Forest → BGM_Forest, Inlaid Library → BGM_Library.
- Run start (line 6950): `PlayMusic(stage track, volume 0.3, loop, rate 1,
  detune 0)` (hyper mode uses the stage's `BGM_rate`/`BGM_detune`).
- Level-up screen and item-found screen open: fade to 0.2 over 500 ms;
  level-up closes: fade back to 0.3 over 500 ms.
- Treasure screen opens: fade to 0 over 500 ms; closes: back to 0.3.
- Game over: fade to 0 over 500 ms; the game-over screen plays
  `BGM_GameOver` as a sound (volume 1.4).
- Back to the menus: `StopMusic`.
- After the start-up warning, `BGM_Intro` plays once as a sound (volume
  0.7). The menus have no looping music.

## Sound events (config values; volume rule above applies)

| Event | Key | volume | detune (cents) | rate | delay / max |
| --- | --- | --- | --- | --- | --- |
| Player takes damage | Hit | 0.4 | 1000 + 500 r | | 150 / 3 |
| … same moment | LossSFX | 0.1 | −500 − 500 r | | 450 / 1 |
| Enemy damaged (every hit, also the killing one) | Hit | 0.5 | 500 (r − 0.5) | | 150 / 3 |
| Enemy explodes (`MakeExplosion`) | Explosion | 0.5 | 500 (r − 0.5) | | 150 / 3 |
| Self-destruct enemy warning, on each blink | Alert | 0.2 | | | 250 / 3 |
| Gem picked up | Gem | 0.1 | | | 1 / 1 |
| Coin, coin bag | Coin | 0.5 | | | |
| Big coin bag | Coin | 0.65 | | | |
| Floor Chicken | Roast | 0.6 | | 1 | |
| Little Heart | LittleHeart | 0.35 | 200 (r − 0.5) | 1.2 | 50 / 3 |
| Vacuum | Vacuum | 0.6 | | 1 | |
| Rosary effect | Rosary | 1.8 | | 2 | |
| Orologion effect | Orologion | 1.8 | | 2 | |
| Whip / Bloody Tear slash | Whip | 1.7 | −100 i | 2 | |
| Magic/Holy Wand shot | MagicMissile | weapon volume or 0.15 | −100 i | | 200 / 12 |
| Runetracer | MagicMissile | 0.15 | −100 i | | 200 / 10 |
| Fire Wand / Hellfire fire | MagicMissile | 0.2 | 0 | | 200 / 10 |
| Knife / Thousand Edge | Shot | weapon volume or 0.4 | −100 i | | 200 / 10 |
| Axe, Cross, Bone | Shot | 0.45 | −100 i | | 200 / 10 |
| Death Spiral | Shot | 0.4 | −100 i | | 200 / 10 |
| Heaven Sword | Shot | 0.5 | −100 i | | 200 / 10 |
| Santa Water bursts | Holywater | 0.35 | −200 + 50 i | 2 | 200 / 12 |
| Lightning strike | Lightning | 0.6 | −100 i | 2 | 200 / 4 |
| Peachone / Ebony / Vandalier shot | MagicMissile | 0.15 | −100 i | | 200 / 12 |
| Pentagram | PentagramSFX | 0.9 | | 2 | |
| Nduja flames | Fireloop | 1 | −100 i | 2 | 200 / 1 |
| Level-up screen opens | LevelUp | 1.5 | | | |
| Level-up choice | ClickIn | (0.1) | | | |
| Treasure screen appears | ThingFound | 0.7 | | | |
| Chest opens (level 1/2/3) | Treasure1 / 2 / 3 (ThingFound stopped) | 1 / 1 / 1.1 | | | |
| Menu confirm / back | ClickIn / ClickOut | (0.1) | (±some cents) | | |

`i` = the bullet's index in its volley, `r` = a random number in [0, 1).
"(0.1)" = no volume given, so the 0.1 default applies. Menu clicks carry
small detunes in some places (character start +200, back −400); the demake
may use plain clicks.

Note: the gem sound has no pitch change in this build (the melody table at
lines 619–649 is never used; `pickups.md` corrected).
