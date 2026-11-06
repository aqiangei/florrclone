'use strict';
// Production-shaped local smoke: guest → register → login → bad password.
const http = require('http');
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');

const PORT = Number(process.env.PORT || 3001);
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
function appFrame(body) {
  const out = Buffer.alloc(4 + body.length);
  out.writeUInt32LE(body.length, 0);
  body.copy(out, 4);
  return out;
}
function mask(payload) {
  const m = crypto.randomBytes(4);
  const len = payload.length;
  let header;
  if (len < 126) {
    header = Buffer.alloc(2);
    header[0] = 0x82;
    header[1] = 0x80 | len;
  } else if (len < 65536) {
    header = Buffer.alloc(4);
    header[0] = 0x82;
    header[1] = 0x80 | 126;
    header.writeUInt16BE(len, 2);
  } else {
    throw new Error('payload too large');
  }
  const out = Buffer.alloc(header.length + 4 + len);
  header.copy(out, 0);
  m.copy(out, header.length);
  for (let i = 0; i < len; i++) out[header.length + 4 + i] = payload[i] ^ m[i & 3];
  return out;
}
function send(sock, body) {
  sock.write(mask(appFrame(body)));
}
function parseFrames(buffer, state) {
  // Reassemble WebSocket messages (continuation frames), then peel the
  // transport [u32 length][app] layer the same way the C++ client does.
  const messages = [];
  let at = 0;
  let pending = state.pending || null;
  while (at + 2 <= buffer.length) {
    const first = buffer[at];
    const second = buffer[at + 1];
    const opcode = first & 0x0f;
    const fin = (first & 0x80) !== 0;
    let len = second & 0x7f;
    let header = 2;
    if (len === 126) {
      if (buffer.length < at + 4) break;
      len = buffer.readUInt16BE(at + 2);
      header = 4;
    } else if (len === 127) {
      if (buffer.length < at + 10) break;
      const hi = buffer.readUInt32BE(at + 2);
      const lo = buffer.readUInt32BE(at + 6);
      len = hi * 2 ** 32 + lo;
      header = 10;
    }
    const masked = (second & 0x80) !== 0;
    const need = header + (masked ? 4 : 0) + len;
    if (buffer.length < at + need) break;
    let payload = buffer.subarray(at + header + (masked ? 4 : 0), at + need);
    if (masked) {
      const mk = buffer.subarray(at + header, at + header + 4);
      payload = Buffer.from(payload);
      for (let i = 0; i < payload.length; i++) payload[i] ^= mk[i & 3];
    }
    at += need;
    if (opcode === 0x8 || opcode === 0x9 || opcode === 0xa) continue; // control
    if (opcode === 0x0 && pending) {
      pending = Buffer.concat([pending, payload]);
    } else if (opcode === 0x1 || opcode === 0x2) {
      pending = Buffer.from(payload);
    } else {
      continue;
    }
    if (!fin) continue;
    const msg = Buffer.concat([state.application || Buffer.alloc(0), pending]);
    pending = null;
    // One WebSocket binary may carry several [u32 length][app] records.
    let off = 0;
    while (off + 4 <= msg.length) {
      const appLen = msg.readUInt32LE(off);
      if (appLen === 0 || off + 4 + appLen > msg.length) break;
      messages.push(msg.subarray(off + 4, off + 4 + appLen));
      off += 4 + appLen;
    }
    state.application = Buffer.from(msg.subarray(off));
  }
  state.pending = pending;
  return { frames: messages, rest: buffer.subarray(at) };
}
function wsConnect(port = PORT) {
  return new Promise((resolve, reject) => {
    const key = crypto.randomBytes(16).toString('base64');
    const req = http.request({
      host: '127.0.0.1', port, path: '/ws',
      headers: {
        Connection: 'Upgrade', Upgrade: 'websocket',
        'Sec-WebSocket-Version': '13', 'Sec-WebSocket-Key': key,
        'Sec-WebSocket-Protocol': 'binary',
      },
    });
    req.on('upgrade', (_r, socket, head) => {
      const state = { buffer: Buffer.alloc(0), queue: [], listeners: new Set() };
      socket.smokeReader = state;
      const data = chunk => {
        const parsed = parseFrames(Buffer.concat([state.buffer, chunk]), state);
        state.buffer = parsed.rest;
        for (const frame of parsed.frames) {
          if (state.listeners.size) for (const listener of state.listeners) listener(frame);
          else state.queue.push(frame);
        }
      };
      socket.on('data', data);
      if (head.length) data(head);
      resolve(socket);
    });
    req.on('response', (r) => reject(new Error('no upgrade ' + r.statusCode)));
    req.on('error', reject);
    req.end();
  });
}
// (send helper above)
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
