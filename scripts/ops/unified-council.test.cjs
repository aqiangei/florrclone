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

test('three server shares, unified nomination and independently authenticated ballots', async t => {
  const folder = await fs.mkdtemp(path.join(os.tmpdir(), 'unified-council-'));
  t.after(() => fs.rm(folder, {recursive: true, force: true}));
  const stateFile = path.join(folder, 'polls.json');
  const pageFile = path.join(folder, 'council.html');
  const upstreams = {};
  const realmRows = [];
  const timestamp = 1_800_000_000_000;
  for (const realm of [1, 2, 3]) {
    const players = Array.from({length: 12}, (_, index) => ({id: String(index), name: `玩家${index}`, units: 80000}));
    players[0].units += 40000;
    realmRows.push({realm, players});
    const url = await listen(t, http.createServer(async (req, res) => {
      const chunks = [];
      for await (const chunk of req) chunks.push(chunk);
      const body = JSON.parse(Buffer.concat(chunks));
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
  await fs.writeFile(stateFile, JSON.stringify(Object.fromEntries(legacy)));
  await fs.writeFile(pageFile, '<!doctype html><title>玩家议会</title>');
  const url = await listen(t, await createCouncil({upstreams, stateFile, pageFile, now: () => timestamp}));
  const request = async (body, realm = 0, token = '') => (await fetch(`${url}/api/council`, {
    method: 'POST', headers: {'Content-Type': 'application/json', 'X-Council-Realm': realm, Authorization: `Bearer ${token}`,
      'X-Real-IP': `192.0.2.${realm + 1}`},
    body: JSON.stringify(body),
  })).json();
  const view = await request({action: 'view'});
  assert.equal(view.totalUnits, 3000000);
  for (const realm of [1, 2, 3]) assert.equal(view.players.filter(row => row.realm === realm)
    .reduce((sum, row) => sum + row.units, 0), 1000000);
  assert.equal(view.polls[0].yes, 3000000);
  const ranked = combineRanking(realmRows);
  for (const index of [34, 35]) {
    const player = ranked[index];
    const id = player.id.split(':')[1];
    const eligibility = await request({action: 'view'}, player.realm, `game${player.realm}-${id}`);
    assert.equal(eligibility.canPublish, index === 34);
  }
  const result = await request({action: 'create', title: '统一议题'}, 1, 'game1-0');
  const poll = result.polls.find(p => p.scope === 'unified');
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
  assert.deepEqual(disk[poll.id].votes, {'1:0': 'yes', '2:0': 'no', '3:0': 'yes'});
  assert.deepEqual(await request({action: 'player', id: '3:0'}), {realm: 3, id: '0'});
});
