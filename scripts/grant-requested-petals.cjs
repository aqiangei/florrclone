'use strict';
// Apply only to the stopped, SHA-verified deployment candidate.
const assert = require('node:assert/strict');
function applyPrevious(db) {
  const id = '20261104-king-two-et-beetle-eggs-five-s-golden-leaves';
  const users = Object.values(db.users || {}).filter(u => u.username === 'king');
  assert.equal(users.length, 1, 'king must resolve uniquely');
  assert.equal(users[0].id, '24gg6n0t221h', 'king account identity changed');
  const player = db.players[users[0].id];
  assert(player, 'Missing king progress');
  player.operatorAdjustments ??= [];
  assert(Array.isArray(player.operatorAdjustments));
  if (player.operatorAdjustments.includes(id)) return { username: 'king', alreadyApplied: true };
  player.inventory ??= {};
  const changes = [];
  for (const [rarity, petal, count] of [['unique', 'beetle_egg', 2], ['super', 'golden_leaf', 5]]) {
    const bag = player.inventory[rarity] ??= {};
    const key = 'petal_' + petal, before = bag[key] ?? 0;
    assert(Number.isSafeInteger(before) && before >= 0 && Number.isSafeInteger(before + count));
    bag[key] = before + count;
    changes.push({rarity, petal, before, added: count, after: bag[key]});
  }
  player.operatorAdjustments.push(id);
  return {username: 'king', changes};
}
function apply(db) {
  const previous = applyPrevious(db);
  const id = '20261104-king-one-et-dice-one-et-poker-chip';
  const player = db.players['24gg6n0t221h'];
  if (player.operatorAdjustments.includes(id)) return {...previous, alreadyApplied: true};
  const changes = [];
  const bag = player.inventory.unique ??= {};
  for (const petal of ['dice', 'poker_chip']) {
    const key = 'petal_' + petal, before = bag[key] ?? 0;
    assert(Number.isSafeInteger(before) && before >= 0 && Number.isSafeInteger(before + 1));
    bag[key] = before + 1;
    changes.push({rarity: 'unique', petal, before, added: 1, after: bag[key]});
  }
  player.operatorAdjustments.push(id);
  return {username: 'king', previous, changes};
}
module.exports = {apply};
