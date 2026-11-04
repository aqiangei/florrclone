'use strict';
const assert = require('node:assert/strict');
const {apply} = require('./grant-requested-petals.cjs');
const fixture = () => ({users: {k: {username: 'king', id: '24gg6n0t221h'}}, players: {
  '24gg6n0t221h': {xp: 123, inventory: {unique: {petal_beetle_egg: 3}, super: {petal_golden_leaf: 7}}},
  other: {xp: 9}
}});
const db = fixture();
apply(db);
assert.equal(db.players['24gg6n0t221h'].inventory.unique.petal_beetle_egg, 5);
assert.equal(db.players['24gg6n0t221h'].inventory.super.petal_golden_leaf, 12);
assert.equal(db.players['24gg6n0t221h'].xp, 123);
assert.deepEqual(db.players.other, {xp: 9});
const once = JSON.stringify(db);
assert.equal(apply(db).alreadyApplied, true);
assert.equal(JSON.stringify(db), once);
const wrong = fixture(); wrong.users.k.id = 'other';
assert.throws(() => apply(wrong), /identity changed/);
const duplicate = fixture(); duplicate.users.second = {...duplicate.users.k};
assert.throws(() => apply(duplicate), /resolve uniquely/);
console.log('PASS: exact increments, unrelated progress, idempotency and identity checks');

assert.equal(db.players['24gg6n0t221h'].inventory.unique.petal_dice, 1);
assert.equal(db.players['24gg6n0t221h'].inventory.unique.petal_poker_chip, 1);
const prior = fixture();
prior.players['24gg6n0t221h'].operatorAdjustments=['20261104-king-two-et-beetle-eggs-five-s-golden-leaves'];
apply(prior);
assert.equal(prior.players['24gg6n0t221h'].inventory.unique.petal_beetle_egg,3);
assert.equal(prior.players['24gg6n0t221h'].inventory.unique.petal_dice,1);
assert.equal(prior.players['24gg6n0t221h'].inventory.unique.petal_poker_chip,1);
