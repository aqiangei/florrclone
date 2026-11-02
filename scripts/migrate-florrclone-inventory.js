'use strict';
// Convert JS florrclone inventory.json (accounts/save) → cpp users/players DB.
const fs = require('fs');
const path = require('path');
const aliases = require('./content-aliases');

const RARITY = [
  'common', 'uncommon', 'rare', 'epic', 'legendary', 'mythic', 'ultra', 'super',
  'unique', 'apex', 'universal', 'admin',
];

const args = process.argv.slice(2);
const onlyUser = (() => {
  const i = args.indexOf('--user');
  return i >= 0 ? String(args[i + 1] || '').toLowerCase() : null;
})();
const password = (() => {
  const i = args.indexOf('--password');
  return i >= 0 ? String(args[i + 1] || '') : '';
})();
const srcPath = args.find((a, i) => !a.startsWith('--') &&
  !['--user', '--password', '--out'].includes(args[i - 1])) ||
  path.resolve(__dirname, '../migrate/prod-inventory.json');
const outPath =
  args.find((a, i) => a === '--out' && args[i + 1]) ?
    args[args.indexOf('--out') + 1] :
    path.resolve(__dirname, '../migrate/cpp-inventory.json');

function toPetalKey(displayName) {
  if (aliases.petalFlorrToCpp[displayName]) return aliases.petalFlorrToCpp[displayName];
  return String(displayName)
    .trim()
    .toLowerCase()
    .replace(/['']/g, '')
    .replace(/[^a-z0-9]+/g, '_')
    .replace(/^_|_$/g, '');
}

function xpForNext(level) {
  return Math.floor(15 * Math.pow(1.08, Math.max(0, level - 1)));
}

function totalXpFromLevel(level, xpInto) {
  let total = Math.max(0, xpInto | 0);
  for (let lv = 1; lv < level; lv++) total += xpForNext(lv);
  return total;
}

function convertInventory(srcInv) {
  const out = {};
  for (const [key, count] of Object.entries(srcInv || {})) {
    const n = Number(count) | 0;
    if (n <= 0) continue;
    const [name, rarStr] = key.split(':');
    const rar = Number(rarStr);
    if (!Number.isFinite(rar) || rar < 0 || rar >= RARITY.length) continue;
    const petal = toPetalKey(name);
    if (!petal) continue;
    const tier = RARITY[rar];
    out[tier] = out[tier] || {};
    const ik = 'petal_' + petal;
    out[tier][ik] = (out[tier][ik] || 0) + n;
  }
  return out;
}

function convertLoadout(src) {
  return (src || []).map((slot) => {
    if (!slot || !slot.type) return null;
    const rar = Number(slot.rarity);
    return {
      type: 'petal',
      rarity: RARITY[Number.isFinite(rar) ? rar : 0] || 'common',
      petalType: toPetalKey(slot.type),
    };
  });
}

function convertMobKills(src) {
  const out = {};
  for (const [key, count] of Object.entries(src || {})) {
    const n = Number(count) | 0;
    if (n <= 0) continue;
    const [name, rarStr] = key.split(':');
    const rar = Number(rarStr);
    if (!Number.isFinite(rar) || rar < 0 || rar >= RARITY.length) continue;
    const mob = toPetalKey(name); // same slug rules
    const tier = RARITY[rar];
    out[mob] = out[mob] || {};
    out[mob][tier] = (out[mob][tier] || 0) + n;
  }
  return out;
}

function convertTalents(src) {
  const out = {};
  for (const [k, v] of Object.entries(src || {})) {
    const n = Number(v) | 0;
    if (n > 0) out[k] = n;
  }
  return out;
}

const src = JSON.parse(fs.readFileSync(srcPath, 'utf8'));
const accounts = Object.values(src.accounts || {});
const picked = onlyUser
  ? accounts.filter((a) => String(a.username || '').toLowerCase() === onlyUser)
  : accounts;

if (!picked.length) {
  console.error('no accounts matched', onlyUser || '(all)');
  process.exit(1);
}

const users = {};
const players = {};
const report = {accounts: 0, skippedPetals: [], sample: null};

for (const acct of picked) {
  const username = String(acct.username || '').trim();
  if (!username) continue;
  const id = String(acct.id || username);
  const save = acct.save || {};
  const level = Math.max(1, Number(save.level) || 1);
  const xpInto = Number(save.xp) || 0;
  const inventory = convertInventory(save.inventory);
  const loadout = convertLoadout(save.loadout);
  const skills = convertTalents(save.talents);
  const stars = Number(save.shop && save.shop.stars) || 0;

  // Prefer the original florrclone scrypt salt+hash so the real password
  // still works. Fall back to an optional --password plaintext only when
  // the source account has no hash (manual cutovers).
  const userRow = {
    id,
    username,
    createdAt: Date.now(),
  };
  if (acct.passwordHash && acct.salt) {
    userRow.password = String(acct.passwordHash);
    userRow.salt = String(acct.salt);
  } else {
    if (!password) throw new Error(`Account ${username} has no password hash; supply --password explicitly.`);
    userRow.password = password;
    userRow.isPlainText = true;
  }
  users[username] = userRow;
  players[id] = {
    totalXP: totalXpFromLevel(level, xpInto),
    stars,
    inventory,
    loadout,
    skills,
    mobKills: convertMobKills(save.mobKills),
  };
  report.accounts++;
  if (username.toLowerCase() === (onlyUser || 'aqa')) {
    report.sample = {
      username,
      level,
      xpInto,
      totalXP: players[id].totalXP,
      skills,
      loadoutSlotsHint: 5 + (skills.loadout || 0),
      invTiers: Object.keys(inventory),
      invStacks: Object.values(inventory).reduce(
        (n, bucket) => n + Object.keys(bucket).length,
        0,
      ),
    };
  }
}

const out = {ipSalt: 'migrate', customSkins: {}, players, users, sessions: {}};
fs.mkdirSync(path.dirname(outPath), {recursive: true});
fs.writeFileSync(outPath, JSON.stringify(out));
console.log(JSON.stringify({outPath, ...report}, null, 2));
