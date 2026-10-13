#!/usr/bin/env node
'use strict';

const fs = require('fs');
const path = require('path');
const { openSource, verify, BUNDLE, LocateError } = require('../src/locate');
const { deobfuscate, DeobfuscateError } = require('../src/deobfuscate');
const { extract, ExtractError } = require('../src/extract');
const { validateAll } = require('../src/validate');
const { buildPack } = require('../src/pack');

const USAGE = `usage:
  vspatch verify  <game>                 check the files against known builds
  vspatch deob    <game> [--out FILE]    write the de-obfuscated bundle (debug)
  vspatch extract <game> [--out DIR] [--allow-unknown] [--debug]
                                         write game tables as JSON
  vspatch build   <game> --out FILE [--verbose] [--allow-unknown]
                                         write the data pack

<game> is the folder Steam downloaded (…/depot_1794681), or its resources/app
folder, or resources/app.asar. Nothing is written into it.`;

function parseArgs(argv) {
  const args = { _: [], flags: {} };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a.startsWith('--')) {
      const k = a.slice(2);
      if (['out'].includes(k)) args.flags[k] = argv[++i];
      else args.flags[k] = true;
    } else args._.push(a);
  }
  return args;
}

function reportVerify(v) {
  if (v.status === 'unknown') {
    console.error(`unknown build: bundle sha256 ${v.bundleHash}`);
    console.error('Only manifest 8292959317163221061 (v0.2.10) is supported. Rerun download_depot with that manifest.');
  } else if (v.status === 'modified') {
    console.error(`files modified: build ${v.manifest} recognised but ${v.mismatches.length} file(s) differ and ${v.missing.length} missing.`);
    for (const f of [...v.mismatches, ...v.missing].slice(0, 10)) console.error('  ' + f);
    console.error('Delete the downloaded folder and run download_depot again.');
  } else {
    console.error(`ok: manifest ${v.manifest}, ${v.files} files match${v.extra.length ? ` (${v.extra.length} extra file(s) ignored)` : ''}`);
  }
}

function loadBundle(source, flags) {
  const v = verify(source);
  reportVerify(v);
  if (v.status !== 'ok' && !flags['allow-unknown']) {
    throw new LocateError('refusing to continue; pass --allow-unknown to try anyway');
  }
  return { code: source.read(BUNDLE).toString('utf8'), verify: v };
}

function main(argv) {
  const args = parseArgs(argv);
  const [cmd, game] = args._;
  if (!cmd || !game) { console.error(USAGE); return 2; }
  const source = openSource(game);
  console.error(`source: ${source.kind} ${source.root || source.file}`);

  switch (cmd) {
    case 'verify': {
      const v = verify(source);
      reportVerify(v);
      return v.status === 'ok' ? 0 : 1;
    }
    case 'deob': {
      const { code } = loadBundle(source, args.flags);
      const r = deobfuscate(code);
      const out = args.flags.out || path.join('work', 'main.deob.js');
      fs.mkdirSync(path.dirname(out), { recursive: true });
      fs.writeFileSync(out, r.code);
      console.error(`de-obfuscated: ${JSON.stringify(r.stats)} → ${out}`);
      return 0;
    }
    case 'extract': {
      const { code, verify: v } = loadBundle(source, args.flags);
      const deob = deobfuscate(code);
      console.error(`de-obfuscated: ${deob.stats.resolvedCalls} calls, ${deob.stats.strings} strings, rotation ${deob.stats.rotation}`);
      const r = extract(deob.code);
      for (const w of r.warnings) console.error('  warn: ' + w);
      const errors = validateAll(r.data);
      for (const e of errors) console.error('  schema: ' + e);
      const outDir = args.flags.out || path.join('work', 'out', 'data');
      fs.mkdirSync(outDir, { recursive: true });
      for (const [name, table] of Object.entries(r.data)) {
        fs.writeFileSync(path.join(outDir, `${name}.json`), JSON.stringify(table, null, 1) + '\n');
      }
      fs.writeFileSync(path.join(outDir, 'enums.json'), JSON.stringify(r.enums, null, 1) + '\n');
      fs.writeFileSync(path.join(outDir, 'meta.json'), JSON.stringify({
        manifest: v.manifest, bundleHash: v.bundleHash, deob: deob.stats, warnings: r.warnings, sources: r.sources,
      }, null, 1) + '\n');
      if (args.flags.debug) fs.writeFileSync(path.join(outDir, '..', 'main.deob.js'), deob.code);
      console.error(`extracted ${Object.keys(r.data).length} tables → ${outDir}` + (errors.length ? ` with ${errors.length} schema error(s)` : ''));
      return errors.length ? 1 : 0;
    }
    case 'build': {
      if (!args.flags.out) { console.error('build: --out FILE is required'); return 2; }
      const { code } = loadBundle(source, args.flags);
      const deob = deobfuscate(code);
      const r = extract(deob.code);
      for (const w of r.warnings) console.error('  warn: ' + w);
      const errors = validateAll(r.data);
      for (const e of errors) console.error('  schema: ' + e);
      if (errors.length) { console.error('build: extracted data does not match the schemas; aborting'); return 1; }
      const { buffer, entries } = buildPack(source, r, args.flags.verbose ? (m) => console.error('  ' + m) : () => {});
      fs.mkdirSync(path.dirname(path.resolve(args.flags.out)), { recursive: true });
      fs.writeFileSync(args.flags.out, buffer);
      const kinds = entries.reduce((a, e) => ((a[e.kind] = (a[e.kind] || 0) + 1), a), {});
      console.error(`wrote ${args.flags.out}: ${(buffer.length / 1048576).toFixed(1)} MB, ${entries.length} entries (${Object.entries(kinds).map(([k, n]) => `${n} ${k}`).join(', ')})`);
      console.error('This file is made from your copy of the game. Keep it to yourself.');
      return 0;
    }
    default:
      console.error(USAGE);
      return 2;
  }
}

try {
  process.exitCode = main(process.argv.slice(2));
} catch (e) {
  if (e instanceof LocateError || e instanceof DeobfuscateError || e instanceof ExtractError) {
    console.error(`error: ${e.message}`);
    process.exitCode = 1;
  } else throw e;
}
