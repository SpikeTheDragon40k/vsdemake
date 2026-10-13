'use strict';
// Validates extracted tables against schema/*.json (JSON Schema 2020-12).

const fs = require('fs');
const path = require('path');
const Ajv = require('ajv/dist/2020');

const SCHEMA_DIR = path.join(__dirname, '..', '..', 'schema');

let ajv;
function getAjv() {
  if (ajv) return ajv;
  // strictTuples off: the level tables use prefixItems for the full first
  // entry and items for the open-ended deltas, which is what we mean.
  ajv = new Ajv({ allErrors: true, strict: true, strictTuples: false, allowUnionTypes: true });
  for (const f of fs.readdirSync(SCHEMA_DIR)) {
    if (f.endsWith('.json')) ajv.addSchema(JSON.parse(fs.readFileSync(path.join(SCHEMA_DIR, f), 'utf8')));
  }
  return ajv;
}

/** @returns {string[]} human-readable errors, empty when everything validates */
function validateAll(data) {
  const errors = [];
  const a = getAjv();
  for (const [name, table] of Object.entries(data)) {
    const v = a.getSchema(`vsdemake:${name}`);
    if (!v) { errors.push(`${name}: no schema`); continue; }
    if (!v(table)) {
      for (const e of v.errors.slice(0, 20)) errors.push(`${name}${e.instancePath}: ${e.message}${e.params.additionalProperty ? ` (${e.params.additionalProperty})` : ''}`);
      if (v.errors.length > 20) errors.push(`${name}: …and ${v.errors.length - 20} more`);
    }
  }
  return errors;
}

module.exports = { validateAll };
