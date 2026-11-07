'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '../cpp/client/web/asset_stream.js'), 'utf8');
const streamAsset = vm.runInNewContext(source + '\nresumableAssetBody', {
  AbortController, ReadableStream, clearTimeout, setTimeout,
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
