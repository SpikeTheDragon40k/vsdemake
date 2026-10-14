# Level-up screen

Lines refer to `work/main.deob.js`: item panel class 7334–7414, level-up
scene layout 7514–7530, nine-slice configs 1612–1660, language lookup
~7290, strings in `assets/lang/lang.json` (`en.translations`).

## Layout

- Menu background: nine-slice `UI/frame5_c4.png`, corners 12 px, size
  `0.8 × width` by `0.8 × height`, centred.
- Header `levelup_header` ("Level Up!") at top + 45, twice the normal
  text size, centred.
- Panels (up to 4): width `0.95 × background width`, height
  `0.2 × background height`; panel *i* at
  `y = top + h × i + h − 0.025 × background width`, x slides in to
  `left + 0.025 × background width` (150 + 30 i ms).
- Footer `levelup_footer` (luck hint) centred at `top + 4.25 × h`, i.e.
  where a fourth panel would be.
- The selected panel gets the `SelectionHighlight` nine-slice
  (`selectionSquareActive_03.png`, corners 7).

## One panel

- Background nine-slice `UI/frame1_c2.png`, corners 12; tinted 0xffff00
  for an evolution.
- Icon frame `UI/frameB.png` at (4, 4), scale 2; item icon (`texture`,
  `frameName` of the level-1 entry) centred at (28, 28), scale 2.
- Name (level-1 `name`) at (80, 6).
- At `0.7 × panel width`: `weapon_new` ("New!", tinted yellow) for an item
  not owned, else `weapon_level_` + the level it would become ("level: 3").
- Description at (10, 60), wrapped at `0.9 × panel width`: the level-1
  `description` for a new item; for an upgrade, the text built below.
- Item mode (store empty): the pickup's name, description and icon, no
  level text.

The demake keeps the order and contents but packs the panel shorter (the
original's panels are ~109 px high in its 683 px tall layout): the
description sits right of the icon at (64, 28).

## Upgrade text (`ParseLevelUpInfo`)

For the level entry being offered, visit its keys **in the entry's own
order** and append, per key (`all` = the `weaponLevelUp_all` prefix
"[All] " when the item is a passive, else empty; `fixed(n)` = JavaScript
`toFixed(n)`):

| key | text |
| --- | --- |
| power | passive: `damageAll` with fixed0(100 v); weapon: `damage` with fixed0(10 v) |
| area | all + `area` fixed0(100 v) |
| speed | all + `speed` fixed0(100 v) |
| amount | all + `projectiles` if parseInt(v) > 1 else `projectile`, fixed0(v) |
| penetrating | all + `enemies` / `enemy` likewise |
| charges | `charges` / `charge` with v |
| duration | passive: `effect%` fixed0(100 v); weapon: `effect` fixed1(0.001 v) |
| cooldown | all + `cooldown%` fixed1(−100 v) |
| interval | all + `cooldown` fixed1(−0.001 v) |
| maxHp | `health` fixed0(100 v) |
| growth | `xp` fixed0(100 v) |
| moveSpeed | `movement` fixed0(100 v) |
| magnet | `range` fixed0(100 v) |
| luck | `luck` fixed0(100 v) |
| regen | `recovery` fixed1(v) |
| greed | `value` fixed0(100 v) |
| armor | `reduceDamage` with v |
| shieldInvulTime | `shield` fixed1(0.001 v) |
| desc | v + newline |
| anything else | nothing |

String keys above are `weaponLevelUp_<name>`. `%0` in a string is replaced
by the value (first occurrence). The demake computes this text in the
patcher, since only the patcher sees the key order, and stores it in the
pack as `weapons.levelUpText`.
