'use strict';

const http = require('node:http');
const fs = require('node:fs/promises');
const {isIP} = require('node:net');
const path = require('node:path');
const { randomUUID } = require('node:crypto');

const realms = [1, 2, 3];
const totalUnits = 3_000_000;
const proposalRank = 35;
const pageSize = 30;
const day = 86_400_000;
const choices = new Set(['yes', 'no', 'abstain']);
const accountKey = (realm, id) => `${realm}:${id}`;

function combineRanking(results) {
  const rows = results.flatMap(({ realm, players }) => {
    const ids = new Set();
    if (!Array.isArray(players)) throw Error(`无法读取${realm}服权重`);
    for (const row of players) {
      if (typeof row.id !== 'string' || !row.id || ids.has(row.id) ||
          ![row.levelScore, row.recordedSeconds, row.petals].every(score => Number.isFinite(score) && score >= 0)) {
        throw Error(`无法读取${realm}服权重`);
      }
      ids.add(row.id);
    }
    // Older game servers still send historical credits in the combined
    // seconds and timeScore fields. Count recorded play time on every realm.
    return players.filter(row => row.levelScore > 0 || row.recordedSeconds >= 60)
      .map(({creditedSeconds, creditLevel, ...row}) => ({...row, realm, id: accountKey(realm, row.id),
        seconds: row.recordedSeconds, timeScore: Math.log1p(row.recordedSeconds / 3600)}));
  });
  if (!rows.length) return [];

  const totals = rows.reduce((sum, row) => ({level: sum.level + row.levelScore,
    time: sum.time + row.timeScore, petals: sum.petals + row.petals}), {level: 0, time: 0, petals: 0});
  const petalWeight = totals.petals > 0 ? 0.10 : 0;
  const activityWeight = (1 - petalWeight) / 2;
  const prior = rows.length * Math.log1p(10);
  let allocated = 0;
  const ranking = rows.map(row => {
    const levelShare = totals.level > 0 ? row.levelScore / totals.level : 1 / rows.length;
    const levelContribution = activityWeight * levelShare;
    const activityContribution = activityWeight * (row.timeScore + prior * levelShare) / (totals.time + prior);
    const petalContribution = petalWeight ? petalWeight * row.petals / totals.petals : 0;
    const exact = (levelContribution + activityContribution + petalContribution) * totalUnits;
    const units = Math.floor(exact);
    allocated += units;
    return {...row, activityWeight, levelContribution, activityContribution, petalContribution,
      units, remainder: exact - units};
  });
  const tieBreak = (a, b) => a.realm - b.realm || a.id.localeCompare(b.id, 'en');
  ranking.sort((a, b) => b.remainder - a.remainder || tieBreak(a, b));
  for (let index = 0; index < totalUnits - allocated; ++index) ++ranking[index].units;
  ranking.sort((a, b) => b.units - a.units || tieBreak(a, b));
  return ranking.map(({remainder, ...row}, index) => ({...row, rank: index + 1}));
}

function rankingPage(rows, input) {
  const query = typeof input.query === 'string' ? input.query.slice(0, 100).trim() : '';
  const needle = query.toLowerCase();
  const matches = needle ? rows.filter(row => row.name.toLowerCase().includes(needle)) : rows;
  const pages = Math.max(1, Math.ceil(matches.length / pageSize));
  const page = Number.isSafeInteger(input.page) ? Math.max(1, Math.min(input.page, pages)) : 1;
  return {players: matches.slice((page - 1) * pageSize, page * pageSize),
    pagination: {page, pages, pageSize, matches: matches.length, query}};
}

function importLegacy(realm, polls) {
  return Object.entries(polls).map(([id, poll]) => [`legacy-${realm}-${id}`, {
    ...poll,
    realm,
    authorId: accountKey(realm, poll.authorId),
    weights: Object.fromEntries(poll.weights.map(row => [accountKey(realm, row.id), row.units * 3])),
    votes: Object.fromEntries(Object.entries(poll.votes).map(([voter, vote]) => [accountKey(realm, voter), vote])),
  }]);
}

function pollView(id, poll, me, now) {
  const tally = {yes: 0, no: 0, abstain: 0, count: 0};
  for (const [voter, choice] of Object.entries(poll.votes)) {
    const units = poll.weights[voter];
    if (!choices.has(choice) || units === undefined) continue;
    tally[choice] += units;
    ++tally.count;
  }
  return {id, title: poll.title, authorName: poll.authorName, realm: poll.realm, scope: poll.scope || 'legacy',
    weightBasis: poll.scope === 'unified' ? poll.weightBasis || 'equal-realms' : 'legacy',
    createdAt: poll.createdAt, endsAt: poll.endsAt, closed: now >= poll.endsAt,
    legacyWeights: poll.scope !== 'unified' && (poll.weightRule || 0) < 3,
    ...tally, result: tally.yes > tally.no ? 'passed' : tally.no > tally.yes ? 'rejected' : 'tied',
    mine: poll.votes[me] || '', canVote: poll.weights[me] > 0};
}

async function save(file, data) {
  const temp = `${file}.${randomUUID()}.tmp`;
  try {
    const handle = await fs.open(temp, 'wx', 0o600);
    try {
      await handle.writeFile(JSON.stringify(data) + '\n');
      await handle.sync();
    } finally { await handle.close(); }
    await fs.rename(temp, file);
  } finally { await fs.rm(temp, {force: true}); }
}

async function createCouncil({upstreams, stateFile, pageFile, now = Date.now}) {
  let polls = JSON.parse(await fs.readFile(stateFile, 'utf8'));
  let writes = Promise.resolve();
  const page = await fs.readFile(pageFile);
  let cache = null;
  let loading = null;
  const limits = new Map();
  const legacyEndpoints = new Map();

  async function game(realm, body, token = '', address = '') {
    let response;
    try {
      const timeout = realm === 1 ? 30_000 : 6500;
      response = await fetch(upstreams[realm], {method: 'POST', redirect: 'error', signal: AbortSignal.timeout(timeout),
        headers: {'Content-Type': 'application/json', ...(token ? {Authorization: `Bearer ${token}`} : {}),
          ...(address ? {'X-Real-IP': address} : {})},
        body: JSON.stringify(body)});
      if (!response.ok) throw Error();
      return await response.json();
    } catch { throw Error(`${realm}服议会接口暂不可用，请稍后重试`); }
  }

  async function readGame(realm, action, token, address) {
    const key = `${realm}:${action}`;
    if (now() < (legacyEndpoints.get(key) || 0))
      return game(realm, {action: 'view'}, token, address);
    const result = await game(realm, {action}, token, address);
    // During a rolling upgrade, older game servers reject the two read actions.
    if (result.error === '未知操作' || result.error === '请先在游戏首页登录正式账号') {
      legacyEndpoints.set(key, now() + 60_000);
      return game(realm, {action: 'view'}, token, address);
    }
    return result;
  }

  async function ranking(refresh = false, allowRecentSnapshot = false) {
    if (!refresh && cache && now() - cache.at < 15_000) return cache.rows;
    if (!loading) {
      loading = Promise.all(realms.map(async realm => ({realm, ...(await readGame(realm, 'ranking-data'))})))
        .then(results => {
          cache = {at: now(), rows: combineRanking(results)};
          return cache.rows;
        }).finally(() => { loading = null; });
    }
    if (allowRecentSnapshot && cache && now() - cache.at < 120_000) {
      // Reads can show the last complete ranking while a new one is fetched.
      // Publishing still requires a fresh complete ranking.
      loading.catch(() => {});
      return cache.rows;
    }
    return loading;
  }

  async function handle(realm, token, input, address) {
    const action = input.action ?? 'view';
    if (action === 'ranking') return rankingPage(await ranking(false, true), input);
    if (action === 'player') {
      const match = /^([123]):(.{1,120})$/.exec(input.id);
      if (!match) throw Error('该玩家暂无公开详情');
      return game(Number(match[1]), {action, id: match[2]}, '', address);
    }
    if (!['view', 'create', 'vote', 'close'].includes(action)) throw Error('未知操作');
    const needsRanking = action === 'view' || action === 'create';
    const [rows, account] = await Promise.all([
      needsRanking ? ranking(action === 'create', action === 'view') : cache?.rows || [],
      realm && token ? readGame(realm, 'identity', token, address) : null,
    ]);
    if (account?.error) throw Error(account.error);
    const me = account?.me ? accountKey(realm, account.me) : '';
    const canPublish = rows.slice(0, proposalRank).some(row => row.id === me && row.units > 0);
    const canClose = Boolean(me && account.canClose);
    if (action !== 'view') {
      if (!me) throw Error('请从对应游戏服登录后打开议会');
      const write = writes.then(async () => {
        const timestamp = now();
        const next = {...polls};
        if (action === 'create') {
          if (!canPublish) throw Error(`仅综合权重前${proposalRank}名可以发布议题`);
          const own = Object.values(polls).filter(poll => poll.authorId === me);
          if (own.some(poll => timestamp - poll.createdAt < day)) throw Error('每个账号每24小时最多发布1个议题');
          if (own.filter(poll => poll.endsAt > timestamp).length >= 3) throw Error('每个账号最多同时有3个进行中的议题');
          const title = typeof input.title === 'string' ? input.title.trim() : '';
          if (!title || Buffer.byteLength(title) > 240) throw Error('标题为空或过长');
          next[randomUUID()] = {title, authorId: me, authorName: account.accountName, realm,
            scope: 'unified', weightBasis: 'global', createdAt: timestamp, endsAt: timestamp + day,
            weights: Object.fromEntries(rows.map(row => [row.id, row.units])), votes: {}};
        } else {
          if (!Object.hasOwn(next, input.id)) throw Error('议题不存在');
          const poll = next[input.id];
          if (action === 'close') {
            if (!canClose || (poll.scope !== 'unified' && poll.realm !== realm)) throw Error('当前账号没有结束该议题的权限');
            if (poll.endsAt <= timestamp) return;
            next[input.id] = {...poll, endsAt: timestamp, closedBy: me};
          } else {
            if (poll.endsAt <= timestamp) throw Error('投票已结束');
            if (!(poll.weights[me] > 0)) throw Error('本议题开始时你的账号没有投票权重');
            if (!choices.has(input.choice)) throw Error('无效选项');
            next[input.id] = {...poll, votes: {...poll.votes, [me]: input.choice}};
          }
        }
        await save(stateFile, next);
        polls = next;
      });
      writes = write.catch(() => {});
      await write;
    }
    const timestamp = now();
    const realmShares = realms.map(realm => ({realm, players: 0, units: 0}));
    for (const row of rows) {
      ++realmShares[row.realm - 1].players;
      realmShares[row.realm - 1].units += row.units;
    }
    return {...rankingPage(rows, input), totalUnits, playerCount: rows.length, realmShares,
      self: rows.find(row => row.id === me) || null, proposalRank, me, realm, canPublish, canClose,
      accountName: me ? account.accountName : '', polls: Object.entries(polls)
        .map(([id, poll]) => pollView(id, poll, me, timestamp))};
  }

  const server = http.createServer(async (req, res) => {
    res.setHeader('Cache-Control', 'no-store');
    res.setHeader('X-Content-Type-Options', 'nosniff');
    const route = req.url.split('?', 1)[0];
    if (route === '/internal/skin-review') {
      const token = /^Bearer ([a-zA-Z0-9_-]{32,128})$/.exec(req.headers.authorization || '')?.[1];
      if (req.method !== 'POST' || req.socket.remoteAddress !== '127.0.0.1' || !token ||
          req.headers['content-type'] !== 'application/json') {
        res.writeHead(403).end();
        return;
      }
      try {
        let size = 0;
        const chunks = [];
        for await (const chunk of req) {
          size += chunk.length;
          if (size > 1024) { res.writeHead(413).end(); return; }
          chunks.push(chunk);
        }
        const {realm, local, action, id} = JSON.parse(Buffer.concat(chunks));
        if (!realms.includes(local) || (action === 'skin-review-list' ? realm !== 0 :
            !['skin-review-approve', 'skin-review-reject'].includes(action) ||
            !realms.includes(realm) || realm === local ||
            typeof id !== 'string' || !/^[a-zA-Z0-9_-]{1,120}$/.test(id))) {
          res.writeHead(400).end();
          return;
        }
        const call = async (target, body) => {
          const response = await fetch(upstreams[target], {
            method: 'POST', redirect: 'error', signal: AbortSignal.timeout(6500),
            headers: {'Content-Type': 'application/json', Authorization: `Bearer ${token}`},
            body: JSON.stringify(body)
          });
          if (!response.ok) throw Error();
          return response.json();
        };
        let result = {};
        if (realm) {
          try { result = await call(realm, {action, id}); }
          catch { result = {error: '来源服未确认审核结果，请刷新队列。'}; }
          if (!result.error && result.ok !== true) result.error = '来源服未确认审核结果，请刷新队列。';
        }
        const sources = {};
        if (!realm) {
          const unavailable = [];
          await Promise.all(realms.filter(target => target !== local).map(async target => {
            try {
              const reply = await call(target, {action: 'skin-review-list'});
              if (!Array.isArray(reply.rows)) throw Error();
              sources[target] = reply.rows;
            } catch { unavailable.push(target); }
          }));
          if (unavailable.length) result.error = `${unavailable.sort().join('、')}服审核队列暂不可用。`;
        }
        res.setHeader('Content-Type', 'application/json; charset=utf-8');
        res.end(JSON.stringify({...result, sources}));
      } catch { res.writeHead(400).end(); }
      return;
    }
    if (req.method === 'GET' && route === '/council.html') {
      res.setHeader('Content-Type', 'text/html; charset=utf-8');
      res.end(page);
      return;
    }
    if (req.method !== 'POST' || route !== '/api/council') {
      res.writeHead(404).end();
      return;
    }
    res.setHeader('Content-Type', 'application/json; charset=utf-8');
    if (!/^application\/json(?:;|$)/i.test(req.headers['content-type'] || '') ||
        req.headers['sec-fetch-site'] === 'cross-site') {
      res.writeHead(403).end('{}');
      return;
    }
    const address = isIP(req.headers['x-real-ip'] || '') ? req.headers['x-real-ip'] : req.socket.remoteAddress;
    const time = now();
    if (limits.size >= 4096) for (const [key, window] of limits) {
      if (time - window.at >= 10000) limits.delete(key);
    }
    if (!limits.has(address) && limits.size >= 4096) { res.writeHead(429).end('{}'); return; }
    let limit = limits.get(address);
    if (!limit || time - limit.at >= 10000) limits.set(address, limit = {at: time, count: 0});
    if (++limit.count > 10) { res.writeHead(429).end('{}'); return; }
    try {
      const chunks = [];
      let size = 0;
      for await (const chunk of req) {
        size += chunk.length;
        if (size > 8192) { res.writeHead(413).end('{}'); return; }
        chunks.push(chunk);
      }
      const selected = Number(req.headers['x-council-realm']);
      const realm = realms.includes(selected) ? selected : 0;
      const token = /^Bearer ([a-zA-Z0-9_-]{1,256})$/.exec(req.headers.authorization || '')?.[1];
      const input = JSON.parse(Buffer.concat(chunks));
      if (!input || typeof input !== 'object' || Array.isArray(input)) throw new SyntaxError();
      const result = await handle(realm, token, input, address);
      res.end(JSON.stringify(result));
    } catch (error) {
      res.end(JSON.stringify({error: error instanceof SyntaxError ? '请求格式无效' : error.message}));
    }
  });
  server.once('listening', () => { ranking().catch(() => {}); });
  return server;
}

if (require.main === module) {
  const [stateFile, pageFile] = process.argv.slice(2);
  const upstreams = Object.fromEntries(realms.map(realm => [realm, `http://127.0.0.1:${19100 + realm}/api/council`]));
  createCouncil({upstreams, stateFile, pageFile: path.resolve(pageFile)}).then(server => {
    server.listen(19090, '127.0.0.1');
  }).catch(error => { console.error(error); process.exitCode = 1; });
}

module.exports = {createCouncil, combineRanking, importLegacy};
