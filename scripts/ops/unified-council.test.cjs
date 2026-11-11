'use strict';

const assert = require('node:assert/strict');
const http = require('node:http');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const {test} = require('node:test');
const {createCouncil, combineRanking, importLegacy} = require('./unified-council.cjs');

async function listen(t, server) {
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  t.after(() => new Promise(resolve => server.close(resolve)));
  return `http://127.0.0.1:${server.address().port}`;
}

test('identical contributions have the same weight across differently populated servers', () => {
  const results = [3, 2, 1].map((count, index) => ({realm: index + 1,
    players: Array.from({length: count}, (_, id) => ({id: String(id),
      levelScore: 25, recordedSeconds: 36000, creditedSeconds: index === 0 ? 558009 : 0,
      timeScore: index === 0 ? 5 : 2, petals: 3, units: id === 0 ? 1_000_000 : 0}))}));
  const ranked = combineRanking(results);
  assert.deepEqual(ranked.map(row => row.units), Array(6).fill(500000));
  assert(ranked.every(row => row.seconds === 36000 && row.creditedSeconds === undefined));
  assert.deepEqual([1, 2, 3].map(realm => ranked.filter(row => row.realm === realm)
    .reduce((sum, row) => sum + row.units, 0)), [1500000, 1000000, 500000]);
  assert.equal(combineRanking([{realm: 1, players: []}, ...results.slice(1)])
    .reduce((sum, row) => sum + row.units, 0), 3000000);
  assert.deepEqual(combineRanking(results.map(row => ({...row, players: []}))), []);
});

test('paginated ranking, unified nomination and independently authenticated ballots', async t => {
  const folder = await fs.mkdtemp(path.join(os.tmpdir(), 'unified-council-'));
  t.after(() => fs.rm(folder, {recursive: true, force: true}));
  const stateFile = path.join(folder, 'polls.json');
  const pageFile = path.join(folder, 'council.html');
  const upstreams = {};
  const realmRows = [];
  const timestamp = 1_800_000_000_000;
  let now = timestamp;
  let firstAvailable = true;
  for (const realm of [1, 2, 3]) {
    const players = Array.from({length: 12}, (_, index) => ({id: String(index), name: `玩家${index}`,
      levelScore: index ? 80000 : 120000, recordedSeconds: 0, timeScore: 0, petals: 0}));
    realmRows.push({realm, players});
    const url = await listen(t, http.createServer(async (req, res) => {
      const chunks = [];
      for await (const chunk of req) chunks.push(chunk);
      const body = JSON.parse(Buffer.concat(chunks));
      if (realm === 1 && !firstAvailable) {
        res.writeHead(503).end();
        return;
      }
      const match = new RegExp(`^Bearer game${realm}-([0-9]+)$`).exec(req.headers.authorization || '');
      res.end(JSON.stringify(body.action === 'player' ? {realm, id: body.id} : {
        players, me: match?.[1] || '', accountName: '同名玩家', canClose: match?.[1] === '0',
      }));
    }));
    upstreams[realm] = url;
  }
  const legacy = importLegacy(2, {old: {title: '旧议题', authorId: '0', authorName: '同名玩家',
    createdAt: timestamp - 90_000_000, endsAt: timestamp + 1000,
    weights: [{id: '0', units: 1000000}], votes: {'0': 'yes'}}});
  const equalRealms = {title: '三服等权旧议题', authorId: '1:0', realm: 1, scope: 'unified',
    createdAt: timestamp - 90_000_000, endsAt: timestamp + 1000,
    weights: {'1:0': 1000000, '2:0': 1000000, '3:0': 1000000}, votes: {'3:0': 'yes'}};
  await fs.writeFile(stateFile, JSON.stringify({...Object.fromEntries(legacy), 'old-unified': equalRealms}));
  await fs.writeFile(pageFile, '<!doctype html><title>玩家议会</title>');
  const url = await listen(t, await createCouncil({upstreams, stateFile, pageFile, now: () => now}));
  const request = async (body, realm = 0, token = '') => (await fetch(`${url}/api/council`, {
    method: 'POST', headers: {'Content-Type': 'application/json', 'X-Council-Realm': realm, Authorization: `Bearer ${token}`,
      'X-Real-IP': `192.0.2.${realm + 1}`},
    body: JSON.stringify(body),
  })).json();
  const view = await request({action: 'view'});
  assert.equal(view.totalUnits, 3000000);
  assert.equal(view.playerCount, 36);
  assert.equal(view.players.length, 30);
  assert.deepEqual(view.realmShares.map(row => row.units), [1000000, 1000000, 1000000]);
  assert.equal(view.polls[0].yes, 3000000);
  assert.equal(view.polls[1].weightBasis, 'equal-realms');
  assert.equal(view.polls[1].yes, 1000000);
  const second = await request({action: 'ranking', page: 99});
  assert.deepEqual(second.pagination, {page: 2, pages: 2, pageSize: 30, matches: 36, query: ''});
  assert.deepEqual(second.players.map(row => row.rank), [31, 32, 33, 34, 35, 36]);
  const search = await request({action: 'ranking', query: '玩家9'});
  assert.equal(search.pagination.matches, 3);
  assert(search.players.some(row => row.rank > 30));
  const empty = await request({action: 'ranking', query: '不存在'});
  assert.deepEqual(empty.players, []);
  assert.equal(empty.pagination.pages, 1);
  const ranked = combineRanking(realmRows);
  for (const index of [34, 35]) {
    const player = ranked[index];
    const id = player.id.split(':')[1];
    const eligibility = await request({action: 'view'}, player.realm, `game${player.realm}-${id}`);
    assert.equal(eligibility.canPublish, index === 34);
    assert.equal(eligibility.self.rank, index + 1);
  }
  const result = await request({action: 'create', title: '统一议题'}, 1, 'game1-0');
  const poll = result.polls.find(p => p.weightBasis === 'global');
  assert(poll);
  const vote = {action: 'vote', id: poll.id, choice: 'yes'};
  assert.match((await request(vote, 2, 'game1-0')).error, /登录/);
  assert.match((await request({...vote, id: 'legacy-2-old'}, 1, 'game1-0')).error, /投票权重/);
  await Promise.all([1, 2, 3].map(async realm => {
    const result = await request(vote, realm, `game${realm}-0`);
    assert(!result.error);
  }));
  const changed = await request({...vote, choice: 'no'}, 2, 'game2-0');
  const tally = changed.polls.find(p => p.id === poll.id);
  assert.deepEqual([tally.yes, tally.no, tally.count], [240000, 120000, 3]);
  const disk = JSON.parse(await fs.readFile(stateFile, 'utf8'));
  assert.deepEqual(disk['old-unified'], equalRealms);
  assert.equal(Object.values(disk[poll.id].weights).reduce((sum, units) => sum + units, 0), 3000000);
  assert.deepEqual(disk[poll.id].votes, {'1:0': 'yes', '2:0': 'no', '3:0': 'yes'});
  assert.deepEqual(await request({action: 'player', id: '3:0'}), {realm: 3, id: '0'});
  now += 16_000;
  firstAvailable = false;
  assert.equal((await request({action: 'view'})).playerCount, 36);
  assert.equal((await request({action: 'ranking'})).players.length, 30);
  assert.match((await request({action: 'create', title: '过期排名不可发题'}, 2, 'game2-0')).error,
    /1服议会接口暂不可用/);
  now += 120_000;
  assert.match((await request({action: 'view'})).error, /1服议会接口暂不可用/);
});
