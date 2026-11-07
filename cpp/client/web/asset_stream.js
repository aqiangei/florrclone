// Keep one stream for the decoder while replacing an interrupted HTTP reader.
function immutableAssetPath(response) {
  if (!response.url) return '';
  const path = new URL(response.url).pathname;
  return /^\/client-assets\/sha256-[a-f0-9]{64}\/bundle\.(?:js|wasm)(?:\.bin)?$/.test(path) ? path : '';
}

function resumableAssetBody(initial, {controller, reopen, progress, complete, reconnecting,
                                      idleMillis = 25000, wait, range}) {
  const encoding = initial.headers.get('content-encoding');
  const length = Number(initial.headers.get('content-length'));
  const total = (!encoding || encoding === 'identity') && Number.isSafeInteger(length) && length > 0
    ? length : 0;
  const etag = initial.headers.get('etag');
  const assetPath = immutableAssetPath(initial);
  const canResume = reopen && total > 0 && (assetPath || /^"[^"]*"$/.test(etag || ''));
  let reader = initial.body.getReader();
  let received = 0;
  let failures = 0;
  let cancelled = false;

  const timed = async (run) => {
    const timer = setTimeout(() => controller.abort(), idleMillis);
    try { return await run(); }
    finally { clearTimeout(timer); }
  };
  return new ReadableStream({
    async pull(out) {
      while (!cancelled) {
        try {
          if (!reader) {
            await wait(Math.min(failures * 600, 2400));
            if (cancelled) return;
            controller = new AbortController();
            const response = await timed(() => reopen(received, controller.signal, controller));
            if (cancelled) return;
            // A verified digest URL names the same bytes across mirror hosts.
            const sameAsset = assetPath ? immutableAssetPath(response) === assetPath :
              response.headers.get('etag') === etag;
            if (response.status !== 206 ||
                response.headers.get('content-range') !== (range
                  ? `bytes ${range.start + received}-${range.end}/${range.size}`
                  : `bytes ${received}-${total - 1}/${total}`) ||
                !sameAsset ||
                (response.headers.get('content-encoding') || 'identity') !== 'identity') {
              controller.abort();
              out.error(new Error('Resource changed or the server refused the requested byte range'));
              return;
            }
            reader = response.body.getReader();
          }
          const next = await timed(() => reader.read());
          if (cancelled) return;
          if (next.done) {
            if (total && received !== total) throw Error('Resource transfer ended early');
            complete();
            out.close();
            return;
          }
          received += next.value.byteLength;
          if (total && received > total) {
            controller.abort();
            out.error(new Error('Resource exceeded its expected size'));
            return;
          }
          if (next.value.byteLength) failures = 0;
          progress(received, total);
          out.enqueue(next.value);
          return;
        } catch (error) {
          if (cancelled) return;
          controller.abort();
          reader = null;
          if (!canResume || received === 0 || received >= total || ++failures > 4) {
            out.error(error);
            return;
          }
          reconnecting();
        }
      }
    },
    cancel() {
      cancelled = true;
      controller.abort();
      return reader?.cancel().catch(() => {});
    }
  });
}

// Only verified, content-addressed compressed WASM is split. Each range can
// finish on the origin without discarding the bytes already read from a mirror.
async function parallelMirrorAsset(fetchAsset, {mirror, origin, path, progress, wait, reconnecting}) {
  const controllers = new Set();
  let cancelled = false;
  const request = async (url, start, end, controller, headerMillis) => {
    if (cancelled) throw Error('Mirror download cancelled');
    controllers.add(controller);
    const timer = setTimeout(() => controller.abort(), headerMillis);
    try {
      // Keep concurrent 206 responses in separate HTTP cache entries. Chrome
      // otherwise tries to merge sparse writes for the same URL while in flight.
      const segmentUrl = url + '?part=' + start + '-' + end;
      const response = await fetchAsset(segmentUrl, {
        cache: 'force-cache', signal: controller.signal, headers: {Range: `bytes=${start}-${end}`},
      });
      if (response.status !== 206 || immutableAssetPath(response) !== path ||
          (response.headers.get('content-encoding') || 'identity') !== 'identity' ||
          Number(response.headers.get('content-length')) !== end - start + 1) {
        throw Error('Mirror did not return the requested asset range');
      }
      return response;
    } catch (error) {
      controller.abort();
      throw error;
    } finally {
      clearTimeout(timer);
    }
  };

  try {
    const probe = await request(mirror, 0, 0, new AbortController(), 6000);
    const size = Number(/^bytes 0-0\/(\d+)$/.exec(probe.headers.get('content-range') || '')?.[1]);
    await probe.body.cancel();
    // Bound memory on the normal startup path. Low-memory mode streams the
    // serial download and does not enter this function.
    if (!Number.isSafeInteger(size) || size < 4 || size > 16 * 1024 * 1024)
      throw Error('Invalid mirror asset size');

    const received = [0, 0, 0, 0];
    const parts = await Promise.all(received.map(async (_, index) => {
      const start = Math.floor(index * size / received.length);
      const end = Math.floor((index + 1) * size / received.length) - 1;
      const open = async (url, offset, controller, headerMillis) => {
        const response = await request(url, offset, end, controller, headerMillis);
        if (response.headers.get('content-range') !== `bytes ${offset}-${end}/${size}`) {
          controller.abort();
          throw Error('Incorrect mirror content range');
        }
        return response;
      };
      let controller = new AbortController();
      let response;
      try {
        response = await open(mirror, start, controller, 6000);
      } catch {
        reconnecting();
        controller = new AbortController();
        response = await open(origin, start, controller, 25000);
      }
      const stream = resumableAssetBody(response, {
        controller, wait, reconnecting, complete() {},
        range: {start, end, size},
        reopen: (offset, _signal, nextController) => open(origin, start + offset, nextController, 25000),
        progress: count => {
          received[index] = count;
          progress(received.reduce((sum, part) => sum + part, 0), size);
        },
      });
      return new Response(stream).blob();
    }));
    return new Blob(parts);
  } finally {
    cancelled = true;
    for (const controller of controllers) controller.abort();
  }
}
