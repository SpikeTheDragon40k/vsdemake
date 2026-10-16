# Pause and options

Lines refer to `work/main.deob.js`. Pause scene 8802–8930, scene manager
7059 (`QuitFromPauseMenu`), 7115 (`EnterPause`, `ResumeFromPause`), player
options 2779 (defaults), 2857 (save), 2914 (load).

## Opening

- In a run, the pause button (touch) or gamepad button 9 (Start) calls
  `EnterPause`: the main scene is paused (nothing moves, timers stop) and
  the pause scene is launched over a frozen snapshot. Not while input is
  blocked (level-up / treasure screens).
- Sounds already playing and the music go on; the music volume slider
  applies at once (`UpdateMusicVolume`).

## Contents

- Black layer over the game at alpha 0.5; menu background nine-slice
  (`frame5_c4.png`, corners 12).
- Rows: Sounds (slider), Music (slider), Flashing VFX (switch), Visible
  joystick (switch, touch only), Damage Numbers (switch). Slider knob
  `menu_square_flat_24.png` on a black 4 px track; switch box
  `menu_checkbox_24_bg.png` with `yes16.png` when on.
- Buttons: Quit (`button_c8_normal.png`, corners 9 × 14) on the left,
  Resume (`button_c9_normal.png`) on the right. Strings: `pause_quit`,
  `pause_resume`, `options_sounds`, `options_music`,
  `options_flashing_VFX`, `options_damageNumbers`, `options_header`.
- With more than 5 unlocked weapons, a weapons panel and a stats panel are
  shown on the left (not done in the demake).
- Options are saved when a slider is released or a switch is changed.

## Actions

- Resume: `ResumeFromPause` (main scene resumes).
- Quit: `OnPlayerQuit` (weapon clean-up), `QuitFromPauseMenu`: pause, game
  over and main scenes stop, the post-game (results) scene starts, the music
  stops, ClickOut plays. The results scene is the same one that follows a
  game over, and it is where the run's achievements are computed
  (`GetNewAchievementsEarnedOnThisRun`, line 10412), so a quit run counts.

## Option effects

- Defaults: sound 0.5 and music 0.5 (0 when disabled), Flashing VFX on,
  Damage Numbers on.
- Damage Numbers: enemy damage text only appears when on (line 4519).
- Flashing VFX: guards the enemy hit tint (4525), the Lightning Ring impact
  circle (3881), the Rosary/Orologion screen flashes (6541, 6551), the white
  tint of hit destructibles/player (6011), and several UI animations.

## Demake

- Start pauses, B or Start resumes; up/down choose a row, left/right move a
  slider (5 % steps, repeats when held) or flip a switch, A selects.
- A sound test row is added (A = a game sound, X = plain tone, Y = output
  byte order swap), with the mixer state under the panel.
- The same screen with a Back button is the title menu's Options.
- Options are written to the save when the screen closes (keys
  `sound_volume`, `music_volume`, `flashing_vfx`, `damage_numbers`).
- Flashing VFX currently gates the enemy hit tint and the Lightning Ring
  impact circle (the other flashes are not drawn by the demake yet).

## Weapons and stats panels (pause only)

- Weapons panel (class 7432) at the top left: weapons first, then passives,
  6 per row, 32 px apart, 64 px per row: `weaponBG` / `passiveBG` at scale
  2 (tinted yellow when maxed), the icon at scale 2, and one pip per level
  (`weaponLevelFull` / `weaponLevelEmpty`, 3 per row) under it.
- Stats panel (class 1779) under it, 16 px per line with two gaps: Max
  Health (whole number), Recovery (one decimal, "-" for 0), Armor (+N),
  Move Speed, | Might, Area, Speed, Duration, Amount (+N), Cooldown, | Luck,
  Growth, Greed (±N % from 1, "-" when 0), Magnet (the magnet radius); each
  with the power-up's icon and name.
- Both on panels at alpha 0.75. The original shows them only when more than
  5 weapons are unlocked in the save; the demake always does. Hidden weapons
  (Nduja Fritta) are not listed.
