'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '../cpp/client/web/asset_stream.js'), 'utf8');
const streamAsset = vm.runInNewContext(source + '\nresumableAssetBody', {
  AbortController, ReadableStream, URL, clearTimeout, setTimeout,
});
const parallelAsset = vm.runInNewContext(source + '\nparallelMirrorAsset', {
  AbortController, Blob, ReadableStream, Response, URL, clearTimeout, setTimeout,
});
const headers = {'Content-Length': '6', 'ETag': '"asset"'};

function brokenResponse(responseHeaders = headers) {
  let first = true;
  return new Response(new ReadableStream({
    pull(out) {
      if (!first) return out.error(new Error('connection lost'));
      first = false;
      out.enqueue(Buffer.from('abc'));
    },
  }), {headers: responseHeaders});
}

function stream(initial, overrides) {
  return new Response(streamAsset(initial, {
    controller: new AbortController(),
    progress() {},
    complete() {},
    reconnecting() {},
    wait: async () => {},
    ...overrides,
  })).text();
}

test('the same decoder stream continues at the received byte offset', async () => {
  const offsets = [], progress = [];
  let completed = 0;
  const body = await stream(brokenResponse(), {
    async reopen(offset) {
      offsets.push(offset);
      return new Response('def', {status: 206, headers: {
        'Content-Range': 'bytes 3-5/6', 'ETag': '"asset"' ,
      }});
    },
    progress: (received) => progress.push(received),
    complete: () => completed++,
  });
  assert.equal(body, 'abcdef');
  assert.deepEqual(offsets, [3]);
  assert.deepEqual(progress, [3, 6]);
  assert.equal(completed, 1);
});

test('a changed file or wrong range cannot be appended', async () => {
  for (const response of [
    new Response('def', {status: 206, headers: {'Content-Range': 'bytes 3-5/6', ETag: '"other"'}}),
    new Response('cde', {status: 206, headers: {'Content-Range': 'bytes 2-4/6', ETag: '"asset"'}}),
    new Response('abcdef', {headers}),
  ]) {
    await assert.rejects(stream(brokenResponse(), {
      reopen: async () => response,
      complete: () => assert.fail('mixed or duplicate bytes counted as complete'),
    }), /changed or the server refused/);
  }
});

test('a missing strong validator falls back to a full retry by the caller', async () => {
  await assert.rejects(stream(brokenResponse({'Content-Length': '6'}), {
    reopen: async () => assert.fail('resumed without an ETag'),
  }), /connection lost/);
});

test('an interrupted digest-pinned asset continues from another host with a different ETag', async () => {
  const path = '/client-assets/sha256-' + 'a'.repeat(64) + '/bundle.wasm.bin';
  const initial = brokenResponse();
  Object.defineProperty(initial, 'url', {value: 'http://mirror.example'+path});
  const continued = new Response('def', {status: 206, headers: {
    'Content-Range': 'bytes 3-5/6', ETag: '"origin-etag"',
  }});
  Object.defineProperty(continued, 'url', {value: 'http://origin.example'+path});
  assert.equal(await stream(initial, {reopen: async offset => {
    assert.equal(offset, 3);
    return continued;
  }}), 'abcdef');
});

test('a different digest cannot be resumed even if its ETag and length match', async () => {
  const digestUrl = digit => 'http://origin.example/client-assets/sha256-' + digit.repeat(64) + '/bundle.wasm.bin';
  const initial = brokenResponse();
  Object.defineProperty(initial, 'url', {value: digestUrl('a')});
  const changed = new Response('def', {status: 206, headers: {
    'Content-Range': 'bytes 3-5/6', ETag: '"asset"',
  }});
  Object.defineProperty(changed, 'url', {value: digestUrl('b')});
  await assert.rejects(stream(initial, {reopen: async () => changed}), /changed or the server refused/);
});

test('mirror parts are combined in order and only the interrupted suffix goes to the origin', async () => {
  const path = '/client-assets/sha256-' + 'a'.repeat(64) + '/bundle.wasm.bin';
  const mirror = 'http://mirror.example' + path, origin = 'http://origin.example' + path;
  const bytes = 'abcdefghijklmnop';
  const requests = [];
  const progress = [];
  const result = await parallelAsset(async (url, {headers}) => {
    const base = url.split('?')[0];
    requests.push([base, headers.Range]);
    const [, first, last] = /^bytes=(\d+)-(\d+)$/.exec(headers.Range);
    const start = Number(first), end = Number(last);
    let body = bytes.slice(start, end + 1);
    if (base === mirror && start === 4) {
      body = new ReadableStream({
        start(out) {
          out.enqueue(Buffer.from('ef'));
          setTimeout(() => out.error(new Error('connection lost')), 4);
        },
      });
    }
    if (start === 0 && end === 3) await new Promise(resolve => setTimeout(resolve, 20));
    const response = new Response(body, {status: 206, headers: {
      'Content-Length': String(end - start + 1),
      'Content-Range': `bytes ${start}-${end}/${bytes.length}`,
    }});
    Object.defineProperty(response, 'url', {value: url});
    return response;
  }, {mirror, origin, path, wait: async () => {}, reconnecting() {},
      progress: received => progress.push(received)});
  assert.equal(await result.text(), bytes);
  assert.deepEqual(requests.filter(([url]) => url === origin), [[origin, 'bytes=6-7']]);
  assert.equal(progress.at(-1), bytes.length);
  assert(progress.every((value, index) => index === 0 || value >= progress[index - 1]));
});

test('a mirror without a valid range is rejected for the normal serial fallback', async () => {
  const path = '/client-assets/sha256-' + 'a'.repeat(64) + '/bundle.wasm.bin';
  await assert.rejects(parallelAsset(async () => {
    const response = new Response('not a range');
    Object.defineProperty(response, 'url', {value: 'http://mirror.example' + path});
    return response;
  }, {mirror: 'http://mirror.example' + path, origin: path, path}), /requested asset range/);
});
