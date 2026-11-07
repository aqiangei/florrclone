// Keep one stream for the decoder while replacing an interrupted HTTP reader.
function resumableAssetBody(initial, {controller, reopen, progress, complete, reconnecting,
                                      idleMillis = 25000, wait}) {
  const encoding = initial.headers.get('content-encoding');
  const length = Number(initial.headers.get('content-length'));
  const total = (!encoding || encoding === 'identity') && Number.isSafeInteger(length) && length > 0
    ? length : 0;
  const etag = initial.headers.get('etag');
  const canResume = reopen && total > 0 && /^"[^"]*"$/.test(etag || '');
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
            const response = await timed(() => reopen(received, controller.signal));
            if (cancelled) return;
            if (response.status !== 206 ||
                response.headers.get('content-range') !== `bytes ${received}-${total - 1}/${total}` ||
                response.headers.get('etag') !== etag ||
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
