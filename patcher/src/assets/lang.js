'use strict';
// Language strings (assets/lang/lang.json) → LNG blob, and the level-up
// "what this level adds" text, built at pack time the way the original's
// level-up panel builds it (notes/behaviour/levelup-ui.md).

const { StringTable } = require('./util');

/** Original-style getLang: the key's text with %0, %1, ... replaced (first occurrence each). */
function getLang(t, key, args = []) {
  let s = t[key];
  if (typeof s !== 'string') return key;
  args.forEach((a, i) => { s = s.replace('%' + i, String(a)); });
  return s;
}

/**
 * Text for one level entry, keys visited in the entry's own order.
 * @param entry   weapon level entry (level 2+)
 * @param isPowerUp  whether the item (its level-1 entry) is a passive
 * @param t       English translations
 */
function levelUpText(entry, isPowerUp, t) {
  const all = isPowerUp ? getLang(t, 'weaponLevelUp_all') : '';
  let s = '';
  for (const [k, v] of Object.entries(entry)) {
    switch (k) {
      case 'power':
        s += isPowerUp ? getLang(t, 'weaponLevelUp_damageAll', [(100 * v).toFixed(0)]) : getLang(t, 'weaponLevelUp_damage', [(10 * v).toFixed(0)]);
        break;
      case 'area': s += all + getLang(t, 'weaponLevelUp_area', [(100 * v).toFixed(0)]); break;
      case 'speed': s += all + getLang(t, 'weaponLevelUp_speed', [(100 * v).toFixed(0)]); break;
      case 'amount':
        s += all + getLang(t, Number.parseInt(v) > 1 ? 'weaponLevelUp_projectiles' : 'weaponLevelUp_projectile', [v.toFixed(0)]);
        break;
      case 'penetrating':
        s += all + getLang(t, Number.parseInt(v) > 1 ? 'weaponLevelUp_enemies' : 'weaponLevelUp_enemy', [v.toFixed(0)]);
        break;
      case 'charges': s += getLang(t, Number.parseInt(v) > 1 ? 'weaponLevelUp_charges' : 'weaponLevelUp_charge', [v]); break;
      case 'duration':
        s += isPowerUp ? getLang(t, 'weaponLevelUp_effect%', [(100 * v).toFixed(0)]) : getLang(t, 'weaponLevelUp_effect', [(0.001 * v).toFixed(1)]);
        break;
      case 'cooldown': s += all + getLang(t, 'weaponLevelUp_cooldown%', [(-100 * v).toFixed(1)]); break;
      case 'interval': s += all + getLang(t, 'weaponLevelUp_cooldown', [(-0.001 * v).toFixed(1)]); break;
      case 'maxHp': s += getLang(t, 'weaponLevelUp_health', [(100 * v).toFixed(0)]); break;
      case 'growth': s += getLang(t, 'weaponLevelUp_xp', [(100 * v).toFixed(0)]); break;
      case 'moveSpeed': s += getLang(t, 'weaponLevelUp_movement', [(100 * v).toFixed(0)]); break;
      case 'magnet': s += getLang(t, 'weaponLevelUp_range', [(100 * v).toFixed(0)]); break;
      case 'luck': s += getLang(t, 'weaponLevelUp_luck', [(100 * v).toFixed(0)]); break;
      case 'regen': s += getLang(t, 'weaponLevelUp_recovery', [v.toFixed(1)]); break;
      case 'greed': s += getLang(t, 'weaponLevelUp_value', [(100 * v).toFixed(0)]); break;
      case 'armor': s += getLang(t, 'weaponLevelUp_reduceDamage', [v]); break;
      case 'shieldInvulTime': s += getLang(t, 'weaponLevelUp_shield', [(0.001 * v).toFixed(1)]); break;
      case 'desc': s += v + '\n'; break;
      default: break;
    }
  }
  return s;
}

/** Copy of the weapons table with `levelUpText` on every level 2+ entry. */
function withLevelUpText(weapons, t) {
  const out = {};
  for (const [key, levels] of Object.entries(weapons)) {
    const isPowerUp = !!(levels[0] && levels[0].isPowerUp);
    out[key] = levels.map((e, i) => (i === 0 ? e : { ...e, levelUpText: levelUpText(e, isPowerUp, t) }));
  }
  return out;
}

/** LNG blob: u32 count, u32 0, count × (u32 keyOffset, u32 valueOffset) sorted by key bytes, string pool. */
function langBlob(translations) {
  const keys = Object.keys(translations).filter((k) => typeof translations[k] === 'string')
    .sort((a, b) => Buffer.compare(Buffer.from(a), Buffer.from(b)));
  const strings = new StringTable();
  const head = Buffer.alloc(8 + 8 * keys.length);
  head.writeUInt32BE(keys.length, 0);
  keys.forEach((k, i) => {
    head.writeUInt32BE(strings.add(k), 8 + 8 * i);
    head.writeUInt32BE(strings.add(translations[k]), 12 + 8 * i);
  });
  return Buffer.concat([head, strings.buffer()]);
}

module.exports = { getLang, levelUpText, withLevelUpText, langBlob };
