'use strict';
const http = require('node:http');
const crypto = require('node:crypto');

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
function wsConnect(port) {
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
    req.on('upgrade', (response, socket, head) => {
      const expected = crypto.createHash('sha1')
        .update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
      if (response.statusCode !== 101 || response.headers['sec-websocket-accept'] !== expected
          || response.headers['sec-websocket-protocol'] !== 'binary') {
        socket.destroy();
        reject(new Error('Invalid WebSocket upgrade response'));
        return;
      }
      socket.setTimeout(0);
      socket.on('error', () => {});
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
    req.setTimeout(10000, () => req.destroy(new Error('WebSocket upgrade timed out')));
    req.on('response', (response) => {
      response.resume();
      reject(new Error('WebSocket upgrade HTTP ' + response.statusCode));
    });
    req.on('error', reject);
    req.end();
  });
}

module.exports = { send, wsConnect };
