# Collection and achievements screens

Lines refer to `work/main.deob.js`. Both are reached from the main menu
(`menu_collection`, `menu_achievements`, line 8008), have the
`frame5_c4` menu background, a header, a masked scrolling list, and a
description panel (`frame1_c2`, 0.96 × screen width, 104 px) at the bottom.

## Collection (`CollectionWeaponScene`, line 8991)

- Header `weapon_header` ("Collection").
- Entries: every weapon whose first row is not `hidden`, then every pickup
  not `hidden`, in data order; 8 columns, 48 px cells 12 px apart.
- Cell (line 8937 / 8978): `frameB` (weapons) or `frameC` (pickups) at
  scale 2 with the item's icon at scale 2 when `seen`; otherwise
  `QuestionMark` at scale 1 over the frame at scale 1.6 and alpha 0.5.
- Selection: `SelectionHighlight` 36 × 36 at scale 1.5, at the cell + 5.
- Panel (line 8950): when seen, white background, the name in yellow, the
  description and the tips in orange, the icon in `frameB`; when not, grey
  (0x888888) background, "???", `weaponCollectionPanel_notFound` ("Not
  yet found."), no tips, the question mark.
- Weapons are `seen` once collected (the save's `CollectedWeapons`);
  pickups once picked up, or from their data (`seen`, e.g. coins and gems).

## Achievements (`CollectionAchievementsScene`, line 10591)

- Header `achievements_header` ("Completed: %0 of %1"): achieved count and
  the number of achievements.
- One row per achievement (line 10560), 32 px apart plus 12: a 24 px
  checkbox with a tick when achieved, the description, and on the right
  the icon of the unlock (scale 2): the weapon's icon; the character's
  `portraitName` (UI atlas); the stage's `frameNameUnlock`; the hyper
  stage's `frameName`; for a gold prize, the `weaponIcon` with origin
  (0.75, 0.5) and `MoneyBagGreen` over it. Later checks win, in that order.
- Selection: `SelectionHighlight` 440 × 36; a scroll bar on the right.
- Panel (`AssignAchievement`, line 9265): background white when achieved,
  grey otherwise; title `genericPopup_obtained` ("Obtained") when achieved,
  else empty; the description; tips "Unlocked: " / "Unlocks: " + the
  weapon name / prefix + character name / stage name, or "Unlocked: Hyper
  %0" / "Unlocks: Hyper %0", or "Obtained: %0 gold coins" / "Obtains: %0
  gold coins". The panel's character icon is the character sprite (not the
  portrait); a weapon icon sits in `frameB`.

Demake: the same layout on the 810 × 456 screen with the built-in font;
pads move the selection (left/right page through achievements), B goes
back. `--unlock-all` shows every collection entry as found.
