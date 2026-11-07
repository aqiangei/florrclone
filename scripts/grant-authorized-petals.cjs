'use strict';
const assert = require('node:assert/strict');
// Called only after the deployment has stopped and SHA-verified its save.
function apply(db, request) {
  if (Array.isArray(request)) {
    assert(request.length > 0 && request.length <= 100, 'Invalid grant batch');
    const staged = structuredClone(db);
    const results = request.map(item => apply(staged, item));
    // Validate the entire batch before changing the caller's save.
    Object.assign(db, staged);
    return results;
  }
  const {id, accountId, username, petal, rarity, count} = request;
  assert(typeof id === 'string' && id.length > 0);
  assert(typeof petal === 'string' && /^[a-z_]+$/.test(petal));
  assert(['common','unusual','rare','epic','legendary','mythic','ultra','super','unique'].includes(rarity));
  assert(Number.isSafeInteger(count) && count > 0 && count <= 100);
  const matches = Object.values(db.users || {}).filter(u => u.username === username);
  assert(matches.length === 1 && matches[0].id === accountId, 'Account identity mismatch');
  const player = db.players[accountId];
  assert(player, 'Missing player');
  player.operatorAdjustments ??= [];
  assert(Array.isArray(player.operatorAdjustments));
  if (player.operatorAdjustments.includes(id)) return {id, accountId, alreadyApplied: true};
  player.inventory ??= {};
  const bag = player.inventory[rarity] ??= {};
  const key = 'petal_' + petal, before = bag[key] ?? 0;
  assert(Number.isSafeInteger(before) && before >= 0 && Number.isSafeInteger(before + count));
  bag[key] = before + count;
  player.operatorAdjustments.push(id);
  return {id, accountId, username, petal, rarity, before, added: count, after: bag[key]};
}
module.exports = {apply};
