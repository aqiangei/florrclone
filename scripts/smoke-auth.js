'use strict';
// Production-shaped local smoke: guest → register → login → bad password.
const crypto = require('node:crypto');
const {send, wsConnect: connectWebSocket} = require('./smoke-websocket.cjs');
const fs = require('fs');
const path = require('path');

const PORT = Number(process.env.PORT || 3001);
const wsConnect = (port = PORT) => connectWebSocket(port);
const ROOT = path.join(__dirname, '..');
// Server WASM embeds staged content from cmake (cpp/build-web/data). Host
// `data/` may only hold mobs/petals/drops — hash must match the embed.
const DATA = process.env.DATA_DIR || path.join(ROOT, 'cpp/build-web/data');
const proto = fs.readFileSync(path.join(ROOT, 'cpp/shared/net/protocol.h'), 'utf8');
const ver = Number(proto.match(/kProtocolVersion\s*=\s*(\d+)/)[1]);

/// Same FNV-1a fold as net::contentHash in protocol.h (byte-wise).
function contentHash(bytes, seed = 2166136261) {
  const b = Buffer.isBuffer(bytes) ? bytes : Buffer.from(String(bytes), 'utf8');
  let h = seed >>> 0;
  for (let i = 0; i < b.length; i++) {
    h = Math.imul(h ^ b[i], 16777619) >>> 0;
  }
  return h >>> 0;
}
function fileNameOf(p) {
  const s = String(p).replace(/\\/g, '/');
  const i = s.lastIndexOf('/');
  return i >= 0 ? s.slice(i + 1) : s;
}
function readBytes(p) {
  return fs.existsSync(p) ? fs.readFileSync(p) : Buffer.alloc(0);
}
/// Mirror ContentRegistry::load / foldMapsIntoHash over the staged data dir.
function computeContentHash(dataDir) {
  let hash = contentHash(fs.readFileSync(path.join(dataDir, 'mobs.json')));
  hash = contentHash(fs.readFileSync(path.join(dataDir, 'petals.json')), hash);
  const dropsPath = path.join(dataDir, 'mob_drops.json');
  if (fs.existsSync(dropsPath)) hash = contentHash(fs.readFileSync(dropsPath), hash);
  const manifestPath = path.join(dataDir, 'maps.json');
  if (!fs.existsSync(manifestPath)) return hash;
  const manifestBuf = fs.readFileSync(manifestPath);
  hash = contentHash(manifestBuf, hash);
  let manifest;
  try { manifest = JSON.parse(manifestBuf.toString('utf8')); } catch { return hash; }
  for (const entry of manifest.maps || []) {
    const file = entry && typeof entry === 'object' ? entry.file : entry;
    if (!file) continue;
    const mapBuf = readBytes(path.join(dataDir, file));
    hash = contentHash(mapBuf, hash);
    let map;
    try { map = JSON.parse(mapBuf.toString('utf8')); } catch { continue; }
    for (const tileset of map.tilesets || []) {
      const source = tileset && tileset.source;
      if (!source) continue;
      hash = contentHash(readBytes(path.join(dataDir, fileNameOf(source))), hash);
    }
  }
  return hash;
}
const CONTENT_HASH = computeContentHash(DATA);

function enumValues(enumName) {
  const block = proto.match(new RegExp(`enum class ${enumName}[^{]*\\{([\\s\\S]*?)\n\\};`));
  if (!block) throw new Error('missing enum ' + enumName);
  const out = {};
  let next = 0;
  for (const line of block[1].split('\n')) {
    const m = line.match(/^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*(\d+))?\s*,/);
    if (!m) continue;
    next = m[2] !== undefined ? Number(m[2]) : next;
    out[m[1]] = next;
    next += 1;
  }
  return out;
}
const ClientMessage = enumValues('ClientMessage');
const ServerMessage = enumValues('ServerMessage');
const AuthStatus = {
  Ok: 0, BadCredentials: 1, UsernameTaken: 2, UsernameInvalid: 3,
  PasswordInvalid: 4, RateLimited: 5, ServerError: 6,
};

function encStr(s) {
  const b = Buffer.from(String(s), 'utf8');
  const out = Buffer.alloc(2 + b.length);
  out.writeUInt16LE(b.length, 0);
  b.copy(out, 2);
  return out;
}
function decStr(buf, at) {
  const n = buf.readUInt16LE(at);
  return { value: buf.toString('utf8', at + 2, at + 2 + n), next: at + 2 + n };
}
function readMessages(socket, ms) {
  return new Promise((resolve) => {
    const state = socket.smokeReader;
    const out = state.queue.splice(0);
    const onFrame = frame => out.push(frame);
    state.listeners.add(onFrame);
    setTimeout(() => { state.listeners.delete(onFrame); resolve(out); }, ms);
  });
}
function parseAuth(msgs) {
  let auth = null;
  let profile = null;
  let welcome = null;
  const others = [];
  for (const m of msgs) {
    if (m[0] === ServerMessage.Welcome) {
      // boolean is u8 on the wire
      welcome = { ver: m.readUInt16LE(1), accepted: m[3] !== 0, reason: decStr(m, 4).value };
    } else if (m[0] === ServerMessage.AuthResult) {
      let at = 1;
      const status = m[at++];
      const token = decStr(m, at); at = token.next;
      const username = decStr(m, at); at = username.next;
      const reason = decStr(m, at);
      auth = {
        status,
        name: Object.keys(AuthStatus).find((k) => AuthStatus[k] === status),
        username: username.value,
        reason: reason.value,
        tokenLen: token.value.length,
      };
      Object.defineProperty(auth,'token',{value:token.value,enumerable:false});
    } else if (m[0] === ServerMessage.Profile) {
      let at = 1;
      at += 4;
      const username = decStr(m, at); at = username.next;
      at += 1;
      const xp = m.readDoubleLE(at); at += 8;
      const level = m.readUInt16LE(at); at += 2;
      at += 8 + 4; // stars and migration TP credit (v72)
      const stacks = m.readUInt16LE(at);
      profile = { username: username.value, xp, level, stacks };
    } else {
      others.push(m[0]);
    }
  }
  return { welcome, auth, profile, others };
}

async function session(label, write) {
  const sock = await wsConnect();
  const hello = Buffer.alloc(1 + 2 + 4);
  hello[0] = ClientMessage.Hello;
  hello.writeUInt16LE(ver, 1);
  hello.writeUInt32LE(CONTENT_HASH >>> 0, 3);
  send(sock, hello);
  const afterHello = await readMessages(sock, 500);
  const helloParsed = parseAuth(afterHello);
  if (!helloParsed.welcome || !helloParsed.welcome.accepted) {
    sock.destroy();
    console.log(label, { hello: helloParsed });
    return { ...helloParsed, auth: null, profile: null };
  }
  write(sock);
  const msgs = await readMessages(sock, 2500);
  const parsed = parseAuth(msgs);
  sock.destroy();
  console.log(label, parsed);
  return parsed;
}

function fail(msg) {
  console.error('FAIL', msg);
  process.exit(1);
}

if (require.main === module) (async () => {
  console.log({
    ver,
    contentHash: CONTENT_HASH >>> 0,
    RegisterGuest: ClientMessage.RegisterGuest,
    Register: ClientMessage.Register,
  });

  const guest = await session('guest', (sock) => {
    send(sock, Buffer.from([ClientMessage.RegisterGuest]));
  });
  if (!guest.auth || guest.auth.status !== AuthStatus.Ok) fail('guest auth');
  // Profile may arrive in a later frame; username on AuthResult is enough for guest.
  if (!guest.auth.username) fail('guest username');

  const name = 'p' + crypto.randomBytes(3).toString('hex');
  const pass = 'testpass1';
  const reg = await session('register', (sock) => {
    send(sock, Buffer.concat([
      Buffer.from([ClientMessage.Register]), encStr(name), encStr(pass),
    ]));
  });
  if (!reg.auth || reg.auth.status !== AuthStatus.Ok) {
    fail('register ' + (reg.auth && (reg.auth.reason || reg.auth.name)));
  }
  if (reg.auth.username !== name) fail('register username got ' + reg.auth.username);

  const login = await session('login', (sock) => {
    send(sock, Buffer.concat([
      Buffer.from([ClientMessage.Login]), encStr(name), encStr(pass),
    ]));
  });
  if (!login.auth || login.auth.status !== AuthStatus.Ok) fail('login');
  if (login.auth.username !== name) fail('login username');

  const bad = await session('badpass', (sock) => {
    send(sock, Buffer.concat([
      Buffer.from([ClientMessage.Login]), encStr(name), encStr('wrongpass'),
    ]));
  });
  if (!bad.auth || bad.auth.status !== AuthStatus.BadCredentials) fail('expected BadCredentials');

  const html = process.env.SMOKE_SERVER_ONLY === '1' ? '' : fs.readFileSync(path.join(__dirname, '../dist/index.html'), 'utf8');
  if (html.includes('admin1234') || /r\?"aqa":null/.test(html) || /r\?"admin":null/.test(html)) {
    fail('dist/index.html still has local credential defaults');
  }

  console.log('PASS smoke-auth');
})().catch((e) => {
  console.error(e);
  process.exit(1);
});
module.exports={wsConnect,readMessages,parseAuth,send,encStr,ClientMessage,ServerMessage,AuthStatus,CONTENT_HASH,ver};
