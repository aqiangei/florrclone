#include "client/web/reload.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace flix::web {

#ifdef __EMSCRIPTEN__

// The URL both bypasses cached resources and limits automatic recovery to one
// attempt until a handshake succeeds, even when browser storage is blocked.
EM_JS(int, flix_reload_for_stale_build, (), {
  if (typeof window.flixRecoverStartup === 'function')
    return window.flixRecoverStartup() ? 1 : 0;
  const url = new URL(location.href);
  if (url.searchParams.has('_flix_recover')) return 0;
  url.searchParams.set('_flix_recover', Date.now().toString(36));
  location.replace(url.href);
  return 1;
});

EM_JS(void, flix_clear_stale_build_guard, (), {
  try { sessionStorage.removeItem('florr:proto-reload'); } catch (e) { }
  const url = new URL(location.href);
  if (!url.searchParams.has('_flix_recover')) return;
  url.searchParams.delete('_flix_recover');
  try { history.replaceState(history.state, '', url.href); } catch (e) { }
});

bool reloadForStaleBuild() { return flix_reload_for_stale_build() != 0; }
void clearStaleBuildGuard() { flix_clear_stale_build_guard(); }

#else

bool reloadForStaleBuild() { return false; }
void clearStaleBuildGuard() {}

#endif

} // namespace flix::web
