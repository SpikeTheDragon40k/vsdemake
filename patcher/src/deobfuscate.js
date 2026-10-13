'use strict';
// Static resolver for obfuscator.io-style string-array obfuscation.
//
// The bundle never runs. We parse it, find the three pieces of the scheme
// from the AST shape alone (no hard-coded names or constants), interpret the
// rotation checksum with a tiny expression evaluator, then splice every
// decoder call `alias(0x1a2)` with the string it resolves to.

const acorn = require('acorn');
const walk = require('acorn-walk');

class DeobfuscateError extends Error {}

function parse(src, opts = {}) {
  return acorn.parse(src, { ecmaVersion: 'latest', sourceType: 'script', ...opts });
}

const isNum = (n) => n && n.type === 'Literal' && typeof n.value === 'number';
const isStr = (n) => n && n.type === 'Literal' && typeof n.value === 'string';
const isIdent = (n, name) => n && n.type === 'Identifier' && (name === undefined || n.name === name);

// function A(){ var x = ['..', ...]; A = function(){ return x; }; return A(); }
function findArrayFunction(ast) {
  for (const node of ast.body) {
    if (node.type !== 'FunctionDeclaration' || node.params.length !== 0) continue;
    const [decl] = node.body.body;
    if (!decl || decl.type !== 'VariableDeclaration') continue;
    const init = decl.declarations[0] && decl.declarations[0].init;
    if (!init || init.type !== 'ArrayExpression' || init.elements.length < 16) continue;
    if (!init.elements.every(isStr)) continue;
    return { name: node.id.name, node, strings: init.elements.map((e) => e.value) };
  }
  throw new DeobfuscateError('string array function not found: unsupported bundle layout');
}

// function D(i, k){ var a = A(); return D = function(i, k){ i = i - OFFSET; var s = a[i]; ... return s; }, D(i, k); }
function findDecoder(ast, arrayName) {
  for (const node of ast.body) {
    if (node.type !== 'FunctionDeclaration' || node.id.name === arrayName) continue;
    let callsArray = false;
    let offset = null;
    let suspicious = [];
    walk.full(node, (n) => {
      if (n.type === 'CallExpression' && isIdent(n.callee, arrayName)) callsArray = true;
      // param = param - OFFSET
      if (n.type === 'AssignmentExpression' && n.operator === '=' && isIdent(n.left) &&
          n.right.type === 'BinaryExpression' && n.right.operator === '-' &&
          isIdent(n.right.left, n.left.name) && isNum(n.right.right)) {
        offset = n.right.right.value;
      }
      // Markers of the encoded variants (base64 / rc4) of the same obfuscator.
      if (isIdent(n) && /^(atob|fromCharCode|charCodeAt|decodeURIComponent)$/.test(n.name)) suspicious.push(n.name);
      if (isStr(n) && /^(fromCharCode|charCodeAt|atob)$/.test(n.value)) suspicious.push(n.value);
      if (isStr(n) && n.value.length >= 64 && /^[A-Za-z0-9+/=]+$/.test(n.value)) suspicious.push('base64 alphabet');
    });
    if (!callsArray) continue;
    if (suspicious.length) {
      throw new DeobfuscateError(`decoder ${node.id.name} uses an encoded string scheme (${[...new Set(suspicious)].join(', ')}); not supported`);
    }
    if (offset === null) throw new DeobfuscateError(`decoder ${node.id.name}: index offset not found`);
    return { name: node.id.name, node, offset };
  }
  throw new DeobfuscateError('string decoder function not found');
}

// (function(A, TARGET){ var d = D, a = A(); while(true){ try { var c = <expr>; if (c === TARGET) break; else a.push(a.shift()); } catch(e){ a.push(a.shift()); } } })(A, TARGET)
function findRotation(ast, arrayName, decoderName) {
  let found = null;
  walk.simple(ast, {
    CallExpression(n) {
      if (found || n.callee.type !== 'FunctionExpression' || n.arguments.length !== 2) return;
      if (!isIdent(n.arguments[0], arrayName) || !isNum(n.arguments[1])) return;
      const fn = n.callee;
      const aliases = new Set([decoderName]);
      let checksum = null;
      walk.full(fn.body, (m) => {
        if (m.type === 'VariableDeclarator' && isIdent(m.init) && aliases.has(m.init.name)) aliases.add(m.id.name);
        if (m.type === 'TryStatement' && !checksum) {
          const d = m.block.body.find((s) => s.type === 'VariableDeclaration');
          if (d) checksum = d.declarations[0].init;
        }
      });
      if (!checksum) throw new DeobfuscateError('rotation loop found but checksum expression missing');
      found = { node: n, target: n.arguments[1].value, checksum, aliases };
    },
  });
  if (!found) throw new DeobfuscateError('array rotation call not found');
  return found;
}

// Evaluates the checksum expression with JS number semantics. Only the node
// types the obfuscator emits are supported; anything else aborts.
function evalChecksum(expr, decode, aliases) {
  const ev = (n) => {
    switch (n.type) {
      case 'Literal':
        if (typeof n.value !== 'number') break;
        return n.value;
      case 'UnaryExpression':
        if (n.operator === '-') return -ev(n.argument);
        if (n.operator === '+') return +ev(n.argument);
        break;
      case 'BinaryExpression': {
        const a = ev(n.left), b = ev(n.right);
        switch (n.operator) {
          case '+': return a + b;
          case '-': return a - b;
          case '*': return a * b;
          case '/': return a / b;
          case '%': return a % b;
        }
        break;
      }
      case 'CallExpression':
        if (isIdent(n.callee, 'parseInt') && n.arguments.length === 1) {
          const arg = n.arguments[0];
          if (arg.type === 'CallExpression' && isIdent(arg.callee) && aliases.has(arg.callee.name) && isNum(arg.arguments[0])) {
            const s = decode(arg.arguments[0].value);
            return Number.parseInt(s === undefined ? 'undefined' : s);
          }
        }
        break;
    }
    throw new DeobfuscateError(`unsupported node in rotation checksum: ${n.type}${n.operator ? ' ' + n.operator : ''}`);
  };
  return ev(expr);
}

function resolveRotation(strings, offset, rot) {
  const arr = strings.slice();
  const decode = (i) => arr[i - offset];
  for (let r = 0; r < arr.length; r++) {
    const v = evalChecksum(rot.checksum, decode, rot.aliases);
    if (v === rot.target) return { arr, rotation: r };
    arr.push(arr.shift());
  }
  throw new DeobfuscateError('no rotation of the string array satisfies the checksum');
}

// Collect every identifier bound to the decoder (`var x = D`, transitively).
// Obfuscator names are unique per scope in practice; if one of them is also
// bound to something else we cannot do a name-based pass safely, so abort.
function collectAliases(ast, decoderName) {
  const aliases = new Set([decoderName]);
  const otherBindings = new Set();
  let grew = true;
  while (grew) {
    grew = false;
    walk.simple(ast, {
      VariableDeclarator(n) {
        if (!isIdent(n.id)) return;
        if (isIdent(n.init) && aliases.has(n.init.name)) {
          if (!aliases.has(n.id.name)) { aliases.add(n.id.name); grew = true; }
        }
      },
    });
  }
  walk.simple(ast, {
    VariableDeclarator(n) {
      if (isIdent(n.id) && aliases.has(n.id.name) && !(isIdent(n.init) && aliases.has(n.init.name))) otherBindings.add(n.id.name);
    },
  });
  otherBindings.delete(decoderName);
  if (otherBindings.size) {
    throw new DeobfuscateError(`decoder alias names reused for other values: ${[...otherBindings].join(', ')}`);
  }
  return aliases;
}

// The bundle is a single 750 KB line, which makes source-line references in
// the extractor and the behaviour notes meaningless. Insert a newline after
// every `;`, `{` and `}` token. Working on tokens keeps string and regex
// literals intact; a newline in those positions never changes the parse
// (restricted productions only care about a newline *before* `++`/`--` or
// after `return`/`throw`/..., and `{` can't follow those without a space).
// Inside template literals the closing `}` of `${}` is followed by a
// template token, which we leave alone.
function breakLines(code) {
  const tokens = [...acorn.tokenizer(code, { ecmaVersion: 'latest' })];
  let out = '';
  let pos = 0;
  for (let i = 0; i < tokens.length; i++) {
    const t = tokens[i];
    const label = t.type.label;
    if (label !== ';' && label !== '{' && label !== '}') continue;
    const next = tokens[i + 1];
    if (next && next.type.label === 'template') continue;
    if (next && next.start === t.end && next.type.label === '`') continue;
    out += code.slice(pos, t.end) + '\n';
    pos = t.end;
  }
  out += code.slice(pos);
  // Token streams must be identical apart from positions.
  const a = tokens.map((t) => t.type.label + ':' + (t.value === undefined ? '' : String(t.value)));
  const b = [...acorn.tokenizer(out, { ecmaVersion: 'latest' })].map((t) => t.type.label + ':' + (t.value === undefined ? '' : String(t.value)));
  if (a.length !== b.length || a.some((x, i) => x !== b[i])) {
    throw new DeobfuscateError('internal: line breaking changed the token stream');
  }
  return out;
}

/**
 * @param {string} src obfuscated bundle source
 * @param {{lineBreaks?: boolean}} options
 * @returns {{ code: string, stats: object }}
 */
function deobfuscate(src, options = {}) {
  const ast = parse(src, { ranges: true });
  const arrFn = findArrayFunction(ast);
  const dec = findDecoder(ast, arrFn.name);
  const rot = findRotation(ast, arrFn.name, dec.name);
  const { arr, rotation } = resolveRotation(arrFn.strings, dec.offset, rot);
  const aliases = collectAliases(ast, dec.name);

  const edits = [];
  const unresolved = [];
  walk.ancestor(ast, {
    CallExpression(n, _state, ancestors) {
      if (!isIdent(n.callee) || !aliases.has(n.callee.name)) return;
      // The definitions themselves are dropped below; skip calls inside them.
      if (ancestors.some((a) => a === arrFn.node || a === dec.node || a === rot.node)) return;
      const idx = n.arguments[0];
      if (!isNum(idx) || arr[idx.value - dec.offset] === undefined) {
        unresolved.push(n.start);
        return;
      }
      edits.push({ start: n.start, end: n.end, text: JSON.stringify(arr[idx.value - dec.offset]) });
    },
  });
  if (unresolved.length) {
    throw new DeobfuscateError(`${unresolved.length} decoder call(s) could not be resolved (first at offset ${unresolved[0]})`);
  }

  // Drop the alias declarations' targets too: `var x = D` becomes `var x = 0`
  // so the output no longer references the removed decoder.
  walk.simple(ast, {
    VariableDeclarator(n) {
      if (isIdent(n.id) && aliases.has(n.id.name) && isIdent(n.init) && aliases.has(n.init.name)) {
        edits.push({ start: n.init.start, end: n.init.end, text: '0' });
      }
    },
  });
  // Remove the preamble: array function, decoder, rotation call.
  for (const node of [arrFn.node, dec.node]) edits.push({ start: node.start, end: node.end, text: '' });
  edits.push({ start: rot.node.start, end: rot.node.end, text: 'void 0' });

  edits.sort((a, b) => a.start - b.start);
  let out = '';
  let pos = 0;
  for (const e of edits) {
    if (e.start < pos) continue; // nested inside an already-removed range
    out += src.slice(pos, e.start) + e.text;
    pos = e.end;
  }
  out += src.slice(pos);

  // Sanity: the result must parse and must not mention the decoder anymore.
  const outAst = parse(out);
  let leftovers = 0;
  walk.simple(outAst, { Identifier(n) { if (n.name === dec.name || n.name === arrFn.name) leftovers++; } });
  if (leftovers) throw new DeobfuscateError(`${leftovers} reference(s) to the decoder remain after resolution`);

  if (options.lineBreaks !== false) out = breakLines(out);

  return {
    code: out,
    stats: {
      arrayFunction: arrFn.name,
      decoder: dec.name,
      offset: dec.offset,
      strings: arr.length,
      rotation,
      aliases: aliases.size,
      resolvedCalls: edits.filter((e) => e.text.startsWith('"')).length,
    },
  };
}

module.exports = { deobfuscate, DeobfuscateError, parse };
