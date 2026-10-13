'use strict';
// Generates schema/*.json from the field inventory of build 0.2.10. Field
// names describe the structure of the tables, not their content, so the
// schemas are ours and committed. Run: node patcher/tools/gen-schema.js
const fs = require('fs');
const path = require('path');
const OUT = path.join(__dirname, '..', '..', 'schema');
const S = '$schema', ID = '$id';
const base = (name, title) => ({ [S]: 'https://json-schema.org/draft/2020-12/schema', [ID]: `vsdemake:${name}`, title });
const num = { type: 'number' }, int = { type: 'integer' }, str = { type: 'string' }, bool = { type: 'boolean' };
const arr = (items) => ({ type: 'array', items });
const strs = arr(str);
const obj = (properties, required = [], extra = {}) => ({ type: 'object', properties, required, additionalProperties: false, ...extra });
const table = (name, title, entry, { list = false, keyPattern = '^[A-Z0-9_]+$' } = {}) => ({
  ...base(name, title), type: 'object', minProperties: 1, propertyNames: { pattern: keyPattern },
  additionalProperties: list ? { type: 'array', minItems: 1, ...entry } : entry,
});
const write = (name, schema) => fs.writeFileSync(path.join(OUT, `${name}.json`), JSON.stringify(schema, null, 2) + '\n');

const statFields = { power: num, cooldown: num, area: num, speed: num, duration: num, amount: num, luck: num, growth: num, greed: num, magnet: num, moveSpeed: num, armor: num, regen: num, maxHp: num };

const weaponLevel = obj({
  level: int, bulletType: str, name: str, description: str, tips: str, desc: str, texture: str, frameName: str,
  isUnlocked: bool, hidden: bool, poolLimit: int, rarity: int, interval: num, repeatInterval: num,
  hitsWalls: bool, addEvolvedWeapon: str, isEvolution: bool, evolvesFrom: strs, requires: strs, requiresLevel: int,
  hitVFX: { type: ['integer', 'null'] }, critChance: num, critMul: num, penetrating: num, volume: num, knockback: num,
  hitBoxDelay: num, intervalDependsOnDuration: bool, chance: num, charges: int, shieldInvulTime: num, isSpecialOnly: bool,
  isPowerUp: bool, price: int, ...statFields,
});
write('weapons', table('weapons', 'Weapon and passive item tables, one array of per-level deltas per item',
  { prefixItems: [{ ...weaponLevel, required: ['level', 'bulletType', 'name', 'texture', 'frameName', 'rarity'] }], items: weaponLevel }, { list: true }));

const powerupLevel = obj({ level: int, bulletType: str, name: str, description: str, texture: str, frameName: str, isPowerUp: bool, price: int, hidden: bool, ...statFields });
write('powerups', table('powerups', 'Shop power-ups, one array of per-rank deltas per power-up',
  { prefixItems: [{ ...powerupLevel, required: ['level', 'bulletType', 'name', 'price'] }], items: powerupLevel }, { list: true }));

const charLevel = obj({
  level: int, hidden: bool, startingWeapon: str, charName: str, surname: str, prefix: str, spriteName: str, portraitName: str, walkingFrames: int,
  description: str, isBought: bool, price: int, showcase: strs, debugTime: num, debugEnemies: num, ...statFields,
});
write('characters', table('characters', 'Characters, one array of per-level bonuses per character',
  { prefixItems: [{ ...charLevel, required: ['level', 'startingWeapon', 'charName', 'spriteName', 'walkingFrames', 'maxHp'] }], items: charLevel }, { list: true }));

const enemy = obj({
  level: int, maxHp: num, speed: num, maxSpeed: num, power: num, knockback: num, maxKnockback: num, deathKB: num, xp: num,
  idleFrameCount: int, killedAmount: int, textureName: str, end: int, frameNames: strs, skills: arr({ type: ['integer', 'null'] }),
  alpha: num, moreX: num, moreY: num, scale: num, passThroughWalls: bool, res_Freeze: num, res_Rosary: num, res_Debuffs: num, res_Knockback: num,
  colliderOverride: obj({ radius: num, offsetY: num }),
}, ['level', 'maxHp', 'speed', 'power', 'knockback', 'xp', 'textureName', 'frameNames']);
// Key pattern allows the lower-case `test` enemy present in this build.
write('enemies', table('enemies', 'Enemy types; each is a one-element array', { items: enemy, maxItems: 1 }, { list: true, keyPattern: '^[A-Za-z0-9_]+$' }));

const mods = obj({ PlayerPxSpeed: num, EnemySpeed: num, ProjectileSpeed: num, GoldMultiplier: num, EnemyHealth: num, EnemyMinimumMul: num, StartingSpawns: num, BGM_rate: num, BGM_detune: num, LuckBonus: num, unlocked: bool, BGM_ignoreModsForNewSoundtrack: bool });
const minuteRow = obj({
  // Row 0 carries the stage header; every row carries spawn data.
  stageName: str, description: str, uiTexture: str, uiFrame: str, texture: str, frameName: str, frameNameUnlock: str, unlocked: bool, hidden: bool,
  BGM: { type: ['string', 'null'] }, legacyBGM: str, tips: str, hyperTips: str, mods, hyper: mods,
  tileset: obj({ setKey: str, setPath: str, mapKey: str, mapPath: str, isTiling: bool }),
  tilemapTiledJSON: obj({ name: str, path: str }), tilemapTiledIMG: obj({ name: str, path: str }), tilemapPos: obj({ x: num, y: num }),
  startingSpawns: int, destructibleType: str, destructibleFreq: num, destructibleChance: num, destructibleChanceMax: num, maxDestructibles: int,
  BGTextureName: str, dayNight: bool, spawnType: str, randomMinutes: bool,
  minute: int, minimum: int, frequency: num, enemies: strs, bosses: strs,
  treasure: obj({ chances: arr(num), level: int, prizeTypes: strs, fixedPrizes: strs }),
  // moreY holds an enemy-type name for MEDUSA walls; null where the original
  // referenced a non-existent enum member (see notes/depot-report.md §3).
  events: arr(obj({ eventType: str, delay: num, repeat: int, chance: num, duration: num, moreX: num, moreY: { type: ['string', 'null'] } }, ['eventType'])),
}, ['minute']);
write('stages', table('stages', 'Stages, one array of minute rows per stage; row 0 also holds the stage header', { items: minuteRow }, { list: true }));

write('pickups', table('pickups', 'Pickups (gems, coins, chicken, …)',
  obj({ name: str, description: str, tips: str, texture: str, frameName: str, pickedupAmount: int, rarity: num, unlocksAt: num, value: num, inTreasures: bool, seen: bool, hidden: bool, isRare: bool }, ['name', 'texture', 'frameName', 'rarity', 'value'])));
write('destructibles', table('destructibles', 'Destructible light sources',
  obj({ textureName: str, frameName: str, destroyedAmount: int, maxHp: num, destructibleType: str }, ['textureName', 'frameName', 'maxHp'])));
write('achievements', table('achievements', 'Achievements and what they unlock',
  obj({ description: str, achieved: bool, weaponToUnlock: str, stageToUnlock: str, characterToUnlock: str, hyperToUnlock: str, goldPrize: int, weaponIcon: str }, ['description']), { keyPattern: '^[A-Za-z0-9_]+$' }));
write('hitvfx', table('hitvfx', 'Hit effects by weapon hit type',
  obj({ isTintFill: bool, targetTint: int, hitFrameName: str, impactFrameName: str, duration: num }, ['isTintFill', 'targetTint', 'duration'])));
write('constants', { ...base('constants', 'Global tuning constants'), type: 'object', properties: { PlayerPxSpeed: num, EnemySpeed: num, ProjectileSpeed: num, PixelScale: num }, required: ['PlayerPxSpeed', 'EnemySpeed', 'ProjectileSpeed', 'PixelScale'], additionalProperties: num });
console.log('wrote', fs.readdirSync(OUT).join(' '));
