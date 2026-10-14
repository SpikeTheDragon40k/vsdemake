'use strict';
// Field layouts of the TAB blobs (notes/pack-format.md). Order matters: it
// is the record layout the engine's generated header (vs_tables.h) uses.
//
// Types: f32, i32, bool, str, enum:<Enum>, list:<f32|i32|str|enum:<Enum>>,
// sub:<name> (records of the sub-table "<table>.<field>").
// `path` maps a flattened field to a nested source value.

const STATS = ['power', 'cooldown', 'area', 'speed', 'duration', 'amount', 'luck', 'growth', 'greed', 'magnet', 'moveSpeed', 'armor', 'regen', 'maxHp'];
const stats = () => STATS.map((n) => [n, 'f32']);

const MODS = ['PlayerPxSpeed', 'EnemySpeed', 'ProjectileSpeed', 'GoldMultiplier', 'EnemyHealth', 'EnemyMinimumMul', 'StartingSpawns', 'BGM_rate', 'BGM_detune', 'LuckBonus'];
const nested = (prefix, names, type) => names.map((n) => [`${prefix}_${n}`, type, `${prefix}.${n}`]);

// Enum names refer to keys of enums.json ("<firstMember>@<holder>" → we
// look them up by first member).
const E = { weapon: 'VOID@weapons', char: 'VOID@characters', enemy: 'BAT1', stage: 'FOREST', pickup: 'VOID@pickups', hitvfx: 'NONE', destructible: 'CANDLE', event: 'FLOWER_WALL', prize: 'POWERUP', bgm: 'BGM_Forest', spawn: 'STANDARD' };

const TABLES = {
  constants: {
    keyed: false,
    fields: [['value', 'f32']],
  },
  weapons: {
    keyEnum: E.weapon,
    fields: [
      ['level', 'i32'], ['bulletType', `enum:${E.weapon}`], ['name', 'str'], ['description', 'str'], ['tips', 'str'], ['desc', 'str'],
      ['texture', 'str'], ['frameName', 'str'], ['isUnlocked', 'bool'], ['hidden', 'bool'], ['poolLimit', 'i32'], ['rarity', 'i32'],
      ['interval', 'f32'], ['repeatInterval', 'f32'], ['hitsWalls', 'bool'], ['addEvolvedWeapon', `enum:${E.weapon}`],
      ['isEvolution', 'bool'], ['evolvesFrom', `list:enum:${E.weapon}`], ['requires', `list:enum:${E.weapon}`], ['requiresLevel', 'i32'],
      ['hitVFX', `enum:${E.hitvfx}`], ['critChance', 'f32'], ['critMul', 'f32'], ['penetrating', 'f32'], ['volume', 'f32'], ['knockback', 'f32'],
      ['hitBoxDelay', 'f32'], ['intervalDependsOnDuration', 'bool'], ['chance', 'f32'], ['charges', 'i32'], ['shieldInvulTime', 'f32'],
      ['isSpecialOnly', 'bool'], ['isPowerUp', 'bool'], ['price', 'i32'], ...stats(),
      // computed at pack time (assets/lang.js), not in the game's table
      ['levelUpText', 'str'],
    ],
  },
  powerups: {
    keyEnum: E.weapon,
    fields: [['level', 'i32'], ['bulletType', `enum:${E.weapon}`], ['name', 'str'], ['description', 'str'], ['texture', 'str'], ['frameName', 'str'], ['isPowerUp', 'bool'], ['price', 'i32'], ['hidden', 'bool'], ...stats()],
  },
  characters: {
    keyEnum: E.char,
    fields: [
      ['level', 'i32'], ['hidden', 'bool'], ['startingWeapon', `enum:${E.weapon}`], ['charName', 'str'], ['surname', 'str'], ['prefix', 'str'],
      ['spriteName', 'str'], ['portraitName', 'str'], ['walkingFrames', 'i32'], ['description', 'str'], ['isBought', 'bool'], ['price', 'i32'],
      ['showcase', `list:enum:${E.weapon}`], ['debugTime', 'f32'], ['debugEnemies', 'f32'], ...stats(),
    ],
  },
  enemies: {
    keyEnum: E.enemy,
    fields: [
      ['level', 'i32'], ['maxHp', 'f32'], ['speed', 'f32'], ['maxSpeed', 'f32'], ['power', 'f32'], ['knockback', 'f32'], ['maxKnockback', 'f32'],
      ['deathKB', 'f32'], ['xp', 'f32'], ['idleFrameCount', 'i32'], ['killedAmount', 'i32'], ['textureName', 'str'], ['end', 'i32'],
      ['frameNames', 'list:str'], ['skills', 'list:i32'], ['alpha', 'f32'], ['moreX', 'f32'], ['moreY', 'f32'], ['scale', 'f32'],
      ['passThroughWalls', 'bool'], ['res_Freeze', 'f32'], ['res_Rosary', 'f32'], ['res_Debuffs', 'f32'], ['res_Knockback', 'f32'],
      ['collider_radius', 'f32', 'colliderOverride.radius'], ['collider_offsetY', 'f32', 'colliderOverride.offsetY'],
    ],
  },
  stages: {
    keyEnum: E.stage,
    fields: [
      ['minute', 'i32'], ['stageName', 'str'], ['description', 'str'], ['uiTexture', 'str'], ['uiFrame', 'str'], ['texture', 'str'],
      ['frameName', 'str'], ['frameNameUnlock', 'str'], ['unlocked', 'bool'], ['hidden', 'bool'], ['BGM', `enum:${E.bgm}`], ['legacyBGM', `enum:${E.bgm}`],
      ['tips', 'str'], ['hyperTips', 'str'],
      ...nested('mods', MODS, 'f32'), ...nested('hyper', MODS, 'f32'), ['hyper_unlocked', 'bool', 'hyper.unlocked'],
      ['hyper_BGM_ignoreMods', 'bool', 'hyper.BGM_ignoreModsForNewSoundtrack'],
      ['tileset_setKey', 'str', 'tileset.setKey'], ['tileset_mapKey', 'str', 'tileset.mapKey'], ['tileset_isTiling', 'bool', 'tileset.isTiling'],
      ['tilemapPos_x', 'f32', 'tilemapPos.x'], ['tilemapPos_y', 'f32', 'tilemapPos.y'],
      ['startingSpawns', 'i32'], ['destructibleType', `enum:${E.destructible}`], ['destructibleFreq', 'f32'], ['destructibleChance', 'f32'],
      ['destructibleChanceMax', 'f32'], ['maxDestructibles', 'i32'], ['BGTextureName', 'str'], ['dayNight', 'bool'], ['spawnType', `enum:${E.spawn}`],
      ['randomMinutes', 'bool'], ['minimum', 'i32'], ['frequency', 'f32'], ['enemies', `list:enum:${E.enemy}`], ['bosses', `list:enum:${E.enemy}`],
      ['treasure', 'sub:treasure'], ['events', 'sub:events'],
    ],
    sub: {
      treasure: { single: true, fields: [['chances', 'list:f32'], ['level', 'i32'], ['prizeTypes', `list:enum:${E.prize}`], ['fixedPrizes', `list:enum:${E.weapon}`]] },
      events: { fields: [['eventType', `enum:${E.event}`], ['delay', 'f32'], ['repeat', 'i32'], ['chance', 'f32'], ['duration', 'f32'], ['moreX', 'f32'], ['moreY', `enum:${E.enemy}`]] },
    },
  },
  pickups: {
    keyEnum: E.pickup,
    single: true,
    fields: [['name', 'str'], ['description', 'str'], ['tips', 'str'], ['texture', 'str'], ['frameName', 'str'], ['pickedupAmount', 'i32'], ['rarity', 'f32'], ['unlocksAt', 'f32'], ['value', 'f32'], ['inTreasures', 'bool'], ['seen', 'bool'], ['hidden', 'bool'], ['isRare', 'bool']],
  },
  destructibles: {
    keyEnum: E.destructible,
    single: true,
    fields: [['textureName', 'str'], ['frameName', 'str'], ['destroyedAmount', 'i32'], ['maxHp', 'f32'], ['destructibleType', `enum:${E.destructible}`]],
  },
  achievements: {
    keyEnum: 'ReachLV5',
    single: true,
    fields: [['description', 'str'], ['achieved', 'bool'], ['weaponToUnlock', `enum:${E.weapon}`], ['stageToUnlock', `enum:${E.stage}`], ['characterToUnlock', `enum:${E.char}`], ['hyperToUnlock', `enum:${E.stage}`], ['goldPrize', 'i32'], ['weaponIcon', `enum:${E.weapon}`]],
  },
  hitvfx: {
    keyEnum: E.hitvfx,
    single: true,
    fields: [['isTintFill', 'bool'], ['targetTint', 'i32'], ['hitFrameName', 'str'], ['impactFrameName', 'str'], ['duration', 'f32']],
  },
};

const TYPE_IDS = { f32: 1, i32: 2, bool: 3, str: 4, enum: 5, list: 6, sub: 7 };

// Enum lookup: enums.json keys are "<firstMember>@<holder>". Several enums
// share a first member (VOID), so E.* uses "VOID@<table>" and we resolve
// those by the table's key names.
function resolveEnums(enumsJson, data) {
  const byFirst = {};
  for (const [k, members] of Object.entries(enumsJson)) {
    const first = k.split('@')[0];
    (byFirst[first] = byFirst[first] || []).push(members);
  }
  const pick = (first, mustHave) => {
    const cands = (byFirst[first] || []).filter((m) => !mustHave || mustHave in m);
    if (cands.length !== 1) throw new Error(`enum ${first}${mustHave ? ' with ' + mustHave : ''}: ${cands.length} candidates`);
    return cands[0];
  };
  const out = {};
  // Weapon and pickup enums both contain WHIP and VOID; tell them apart by
  // members only one of them has.
  out[E.weapon] = pick('VOID', 'MAGIC_MISSILE');
  out[E.char] = pick('VOID', 'ANTONIO');
  out[E.pickup] = pick('VOID', 'COINBAG1');
  for (const first of ['BAT1', 'FOREST', 'NONE', 'CANDLE', 'FLOWER_WALL', 'POWERUP', 'BGM_Forest', 'STANDARD', 'ReachLV5']) out[first] = pick(first);
  // index by declaration order; value may differ from name (MIGHT → "POWER")
  const indexed = {};
  for (const [name, members] of Object.entries(out)) {
    const names = Object.keys(members);
    indexed[name] = { names, values: names.map((n) => String(members[n])), indexOfValue: Object.fromEntries(names.map((n, i) => [String(members[n]), i])) };
  }
  return indexed;
}

module.exports = { TABLES, TYPE_IDS, E, resolveEnums, STATS };
