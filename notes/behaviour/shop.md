# Shop: PowerUp selection and buying characters

Lines refer to `work/main.deob.js`. PowerUp scene 8210–8350 (panels 8096,
confirmation panel 8140), prices 6725–6734, run-start application
6401–6404 and 6735, refund 2997, character select purchase ~7690.

## Power-ups

- Table `powerups` (14 entries in 0.2.10, none hidden): per power-up a list
  of rank rows; row 0 holds name, description, icon, `price`; every row
  holds the stat that rank adds (e.g. Might 5 × power 0.05, Armor 3 × 1,
  Max Health 3 × maxHp 0.1, Amount 1 × 1, Cooldown 2 × −0.025).
- Price of the next rank: `(1 + rank) × price × PowerUpMarkup`, markup
  `1 + 0.1 × (sum of all ranks bought)`; shown rounded (`toFixed(0)`), the
  unrounded value is compared and paid. Buying needs `rank < ranks` and
  gold ≥ price; ClickIn; saved at once.
- Refund (button above the grid): every rank back to 0 and **gold = lifetime
  gold** (the run gold earned ever); ClickOut. Because buying a character
  takes the price from the lifetime gold too, that money is not returned.
- Applied at every run start, after the stage, the starting weapon and the
  loot table, rank by rank (`ApplyBoughtPowerup`): power, area, speed,
  cooldown, duration, amount, moveSpeed, growth, luck, armor, regen, greed
  are added; `maxHp += maxHp × v` (so ranks compound) and hp = maxHp;
  magnet `radius += radius × v`, `percentage += v`.
- Screen: header `powerup_header`, refund button `powerup_refund`, a grid of
  panels (4 columns in the original, scrolling): frame, `frameD` icon
  frame, icon at scale 2, name (grey while rank 0), one checkbox per rank,
  background tinted 0xcccc88 when maxed. Confirmation panel: icon, name,
  description, price with a coin, green BUY button (`powerupConfirm_buy`,
  or `powerupConfirm_maxed`). With a pad, A on a panel buys it directly.

## Characters

- `isBought` in the table (Antonio true) or bought later
  (`BoughtCharacters`). Price: `price × CharMarkup`, markup `1 + 0.1 ×
  (characters bought − 1)`. Imelda 10, Pasqualina 100, Gennaro / Arca /
  Porta / Poe / Clerici / Dommario / Mortaccio 500, Exdash 777, Poppea 1…
- Buying (character select, confirm on an unbought character with enough
  gold): gold and lifetime gold both lose the price; ClickOut at −400
  cents; the character becomes selected; the next confirm starts.

## Demake

- Title menu: START / POWER UP / OPTIONS.
- 7 × 2 grid (all 14 visible, no scrolling); arrows move, up from the top
  row reaches Refund, A buys or refunds, B goes back. "Not enough gold"
  notice. Prices and rules as above.
- Character select: unbought characters show "buy: N gold"; A buys, the
  next A starts.
- `--unlock-all` treats every character as bought; headless runs without
  `--save` have no ranks.
