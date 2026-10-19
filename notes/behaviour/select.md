# Character and stage selection

Lines refer to `work/main.deob.js`.

## Character select (`CharSelectionScene`, line 7681)

- Header `character_header` ("Character Selection").
- Cards (line 7650): every character whose first row is not `hidden`
  (hidden ones appear once unlocked), 4 columns of 110 × 110 in the
  original. A card: `frame1_c2` background (yellow 0xffff00 once the
  character is confirmed), the name at (6, 6) (dark grey 0x444444 when
  not bought), the sprite at the bottom left at scale 2 (tinted black when
  not bought), the starting weapon's icon at the bottom right at scale 2
  over a 0x111111 copy 3 px down-right (the icon itself hidden when not
  bought). `SelectionHighlight` around the selected card.
- Panel (line 7600): "prefix charName surname", the sprite (black when
  not bought), the starting weapon in `frameB` (when bought), the
  description, a `GreenButton` (`button_c5_normal`) reading
  `charConfirm_confirm` ("Confirm"), `charConfirm_unlock` ("Unlock") when
  not bought, `charConfirm_start` ("START") once confirmed (the panel turns
  yellow); the price with a coin when not bought.
- `OnConfirmClicked`: bought and not confirmed → select it, panel to
  START, ClickIn (detune 100), `SetSeenWeapon(startingWeapon)`; confirmed
  → go on (ClickIn, detune 200); not bought and gold ≥ price × CharMarkup
  → pay from gold and lifetime gold, mark bought, select it, ClickOut
  (detune −400).
- Stats panel (`StatsPanel`, line 1779), shown when more than one
  character is unlocked: the 14 stats of the character's row, each plus
  every bought power-up rank of that stat (Max Health: the character's
  value × the rank's share); values raised by power-ups are yellow.
  Magnet is shown as a bonus in % ("percentagePlusOne").

## Stage select (`StageSelectionScene`, line 10785)

- Header `stageSelection_header` ("Stage Selection").
- One card per stage whose first row is not `hidden` (Mad Forest, Inlaid
  Library, Green Acres; Old Sanctuary is hidden): the stage picture
  (`uiTexture` / `uiFrame`, e.g. `stage_forest.png`, 156 × 96), the name in
  yellow and the description; locked stages read `stageSelectionPanel_?`
  ("???") and `stageSelectionPanel_hidden` ("Not yet discovered."). The
  selected card gets a faint yellow fill and `SelectionHighlight`.
- Panel: the name, the stage's `tips` in dark red (0x880000), the hyper
  toggle when the hyper is unlocked, and START (`stageConfirm_start`).
- Confirm works like the character select: the first press selects
  (panel yellow), the second starts the run.

## Demake

Same contents on the 810 × 456 screen: 5 columns of 104 × 86 cards, the
stats panel on the left, the panels at the bottom; the title art, dimmed,
behind both screens. Green Acres shows but cannot be started yet ("not in
the demake yet"), and there is no hyper toggle (Hyper mode is not
implemented). B goes back one screen.
