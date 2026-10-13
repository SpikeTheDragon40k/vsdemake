'use strict';
// Static table extraction from the de-obfuscated bundle.
//
// The game keeps its data in plain object literals keyed by TypeScript enum
// members, e.g. `{[Weapon["WHIP"]]: [{level: 1, ...}, ...]}`. We parse the
// code, recover every enum, then evaluate those literals with JS semantics,
// without executing anything.

const acorn = require('acorn');
const walk = require('acorn-walk');

class ExtractError extends Error {}

// Table name → first key, per notes/FABLE_GUIDE.md Phase 1.
const TABLES = {
  weapons: 'WHIP',
  characters: 'ANTONIO',
  enemies: 'BAT1',
  stages: 'FOREST',
  pickups: 'GEM',
  powerups: 'MIGHT',
  destructibles: 'BRAZIER',
  achievements: 'ReachLV5',
  hitvfx: 'NONE',
};
const CONSTANTS_MARKER = 'PlayerPxSpeed';

const isIdent = (n, name) => n && n.type === 'Identifier' && (name === undefined || n.name === name);
const isLit = (n) => n && n.type === 'Literal';

function propKey(n) {
  // obj["key"] or obj.key → "key"
  if (n.computed) return isLit(n.property) ? String(n.property.value) : null;
  return n.property.name;
}

/**
 * TS enum IIFE: `!function(p){ p["A"]="A", p[p["B"]=0]="B" }(h||(h={}))`
 * Returns Map<holderName, {members: Map<name, value>, line}>.
 */
function findEnums(ast) {
  const enums = new Map();
  walk.simple(ast, {
    CallExpression(n) {
      const fn = n.callee;
      if (fn.type !== 'FunctionExpression' || fn.params.length !== 1 || n.arguments.length !== 1) return;
      const arg = n.arguments[0];
      if (arg.type !== 'LogicalExpression' || arg.operator !== '||' || !isIdent(arg.left)) return;
      const r = arg.right;
      if (r.type !== 'AssignmentExpression' || !isIdent(r.left, arg.left.name) || r.right.type !== 'ObjectExpression' || r.right.properties.length) return;
      const param = fn.params[0].name;
      const members = new Map();
      walk.simple(fn.body, {
        AssignmentExpression(a) {
          if (a.left.type !== 'MemberExpression' || !isIdent(a.left.object, param)) return;
          // p[p["B"]=0]="B": inner assignment is visited separately; here the
          // outer gives the reverse mapping, which we skip.
          if (a.left.computed && a.left.property.type === 'AssignmentExpression') return;
          const key = propKey(a.left);
          if (key === null || !isLit(a.right)) return;
          members.set(key, a.right.value);
        },
      });
      enums.set(arg.left.name, { members, line: n.loc.start.line });
    },
  });
  return enums;
}

/** `const a = b` chains, so `a` can be resolved to the enum holder `b`. */
function findAliases(ast) {
  const aliases = new Map();
  walk.simple(ast, {
    VariableDeclarator(n) {
      if (isIdent(n.id) && isIdent(n.init)) aliases.set(n.id.name, n.init.name);
    },
  });
  const resolve = (name) => {
    const seen = new Set();
    while (aliases.has(name) && !seen.has(name)) { seen.add(name); name = aliases.get(name); }
    return name;
  };
  return resolve;
}

/**
 * Expression evaluator for data literals. Anything outside the supported
 * subset is reported through `warn` and becomes null, so a single odd value
 * never aborts the whole extraction but is never silently wrong either.
 */
function makeEvaluator({ enums, resolveAlias, constants, warn }) {
  const enumOf = (name) => enums.get(resolveAlias(name));

  function ev(n, path) {
    switch (n.type) {
      case 'Literal':
        if (n.value instanceof RegExp) break;
        return n.value;
      case 'TemplateLiteral':
        if (n.expressions.length === 0) return n.quasis.map((q) => q.value.cooked).join('');
        break;
      case 'Identifier':
        if (n.name === 'undefined') return null;
        if (n.name === 'NaN') return NaN;
        if (n.name === 'Infinity') return Infinity;
        break;
      case 'UnaryExpression': {
        const v = ev(n.argument, path);
        switch (n.operator) {
          case '!': return !v;
          case '-': return -v;
          case '+': return +v;
          case 'void': return null;
        }
        break;
      }
      case 'BinaryExpression': {
        const a = ev(n.left, path), b = ev(n.right, path);
        switch (n.operator) {
          case '+': return a + b;
          case '-': return a - b;
          case '*': return a * b;
          case '/': return a / b;
          case '%': return a % b;
          case '**': return a ** b;
        }
        break;
      }
      case 'ArrayExpression':
        return n.elements.map((e, i) => (e === null ? null : ev(e, `${path}[${i}]`)));
      case 'ObjectExpression': {
        const out = {};
        for (const p of n.properties) {
          if (p.type !== 'Property' || p.kind !== 'init' || p.method) {
            warn(`${path}: unsupported object member ${p.type} at line ${p.loc.start.line}`);
            continue;
          }
          let key;
          if (p.computed) {
            key = ev(p.key, `${path}.<key>`);
            if (key === null || key === undefined) {
              warn(`${path}: computed key resolves to ${key} at line ${p.key.loc.start.line}; entry dropped`);
              continue;
            }
            key = String(key);
          } else {
            key = p.key.type === 'Identifier' ? p.key.name : String(p.key.value);
          }
          if (Object.prototype.hasOwnProperty.call(out, key)) {
            warn(`${path}.${key}: duplicate key at line ${p.loc.start.line}; last one wins`);
          }
          out[key] = ev(p.value, `${path}.${key}`);
        }
        return out;
      }
      case 'MemberExpression': {
        const key = propKey(n);
        if (key === null) break;
        if (isIdent(n.object)) {
          const e = enumOf(n.object.name);
          if (e) {
            if (e.members.has(key)) return e.members.get(key);
            warn(`${path}: enum member ${n.object.name}.${key} does not exist (line ${n.loc.start.line}); null`);
            return null;
          }
          if (constants && resolveAlias(n.object.name) === constants.holder) {
            if (key in constants.values) return constants.values[key];
            warn(`${path}: constant ${key} not defined yet (line ${n.loc.start.line}); null`);
            return null;
          }
          if (n.object.name === 'Number' && key in Number && typeof Number[key] === 'number') return Number[key];
          if (n.object.name === 'Math' && key in Math && typeof Math[key] === 'number') return Math[key];
        }
        break;
      }
    }
    warn(`${path}: unsupported expression ${n.type}${n.operator ? ' ' + n.operator : ''} at line ${n.loc.start.line}; null`);
    return null;
  }
  return ev;
}

/**
 * Global constants: a run of `Holder["Name"] = expr` assignments on one
 * identifier, where one of the names is PlayerPxSpeed. Evaluated in source
 * order so later entries can reference earlier ones.
 */
function findConstants(ast, enums, resolveAlias, warn) {
  const groups = new Map();
  walk.simple(ast, {
    AssignmentExpression(a) {
      if (a.operator !== '=' || a.left.type !== 'MemberExpression' || !isIdent(a.left.object)) return;
      const key = propKey(a.left);
      if (key === null) return;
      const holder = resolveAlias(a.left.object.name);
      const g = groups.get(holder) || [];
      g.push({ key, node: a.right, line: a.loc.start.line, holder });
      groups.set(holder, g);
    },
  });
  const holder = [...groups.keys()].find((h) => groups.get(h).some((e) => e.key === CONSTANTS_MARKER));
  if (!holder) throw new ExtractError(`constants block not found (no ${CONSTANTS_MARKER} assignment)`);
  const constants = { holder, values: {}, lines: {} };
  const ev = makeEvaluator({ enums, resolveAlias, constants, warn });
  for (const e of groups.get(holder)) {
    // Only the module-level definitions count. Those are built from literals
    // and earlier constants only; anything that reads game state (stage
    // modifiers, `this`, calls) is a runtime mutation and is skipped.
    let ok = true;
    walk.full(e.node, (m) => {
      if (m.type === 'Identifier' && resolveAlias(m.name) !== holder && !['Number', 'Math', 'undefined', 'NaN', 'Infinity'].includes(m.name)) ok = false;
      if (['ThisExpression', 'CallExpression', 'FunctionExpression', 'ArrowFunctionExpression'].includes(m.type)) ok = false;
    });
    if (!ok) continue;
    if (e.key in constants.values) continue; // first definition wins, later ones are resets
    constants.values[e.key] = ev(e.node, `constants.${e.key}`);
    constants.lines[e.key] = e.line;
  }
  return constants;
}

function findTables(ast, enums, resolveAlias) {
  const found = {};
  walk.simple(ast, {
    VariableDeclarator(n) {
      if (!n.init || n.init.type !== 'ObjectExpression' || !n.init.properties.length) return;
      const first = n.init.properties[0];
      if (first.type !== 'Property' || !first.computed || first.key.type !== 'MemberExpression' || !isIdent(first.key.object)) return;
      const e = enums.get(resolveAlias(first.key.object.name));
      if (!e) return;
      const key = propKey(first.key);
      for (const [table, firstKey] of Object.entries(TABLES)) {
        if (key !== firstKey) continue;
        if (found[table]) throw new ExtractError(`table ${table}: more than one object literal starts with ${firstKey} (lines ${found[table].line}, ${n.loc.start.line})`);
        found[table] = { node: n.init, line: n.loc.start.line, enumHolder: resolveAlias(first.key.object.name) };
      }
    },
  });
  const missing = Object.keys(TABLES).filter((t) => !found[t]);
  if (missing.length) throw new ExtractError(`tables not found: ${missing.join(', ')}`);
  return found;
}

/**
 * @param {string} code de-obfuscated bundle
 * @returns {{ data: object, enums: object, constants: object, warnings: string[], sources: object }}
 */
function extract(code) {
  const ast = acorn.parse(code, { ecmaVersion: 'latest', sourceType: 'script', locations: true });
  const warnings = [];
  const warn = (m) => warnings.push(m);

  const enums = findEnums(ast);
  const resolveAlias = findAliases(ast);
  const constants = findConstants(ast, enums, resolveAlias, warn);
  const ev = makeEvaluator({ enums, resolveAlias, constants, warn });
  const tables = findTables(ast, enums, resolveAlias);

  const data = {};
  const sources = { constants: constants.lines, tables: {} };
  for (const [name, t] of Object.entries(tables)) {
    data[name] = ev(t.node, name);
    sources.tables[name] = { line: t.line, keys: Object.keys(data[name]).length };
  }
  data.constants = constants.values;

  // Enums by their first member, so the engine can map names ↔ ids stably.
  const enumsOut = {};
  for (const [holder, e] of enums) {
    const first = e.members.keys().next().value;
    enumsOut[`${first}@${holder}`] = Object.fromEntries(e.members);
  }

  return { data, enums: enumsOut, constants: constants.values, warnings, sources };
}

module.exports = { extract, ExtractError, TABLES };
