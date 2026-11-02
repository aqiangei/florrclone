'use strict';
/**
 * Drive scripts/build-web.js inside WSL Ubuntu with /root/emsdk (same toolchain
 * path as d:\\ashflor\\DEPLOY.md for ashflor). Windows PATH does not need emcc.
 */
const { spawnSync } = require('node:child_process');
const path = require('node:path');

const rootWin = path.resolve(__dirname, '..');
const rootWsl = '/mnt/' + rootWin[0].toLowerCase() + rootWin.slice(2).replace(/\\/g, '/');
const target = process.argv[2] || 'all';
const bash = [
  'source /root/emsdk/emsdk_env.sh',
  `cd '${rootWsl}'`,
  `FLIX_BUILD=release node scripts/build-web.js ${target}`,
].join(' && ');

console.log(`[build-wsl] ${bash}`);
const res = spawnSync('wsl', ['-d', 'Ubuntu', '-u', 'root', '--', 'bash', '-lc', bash], {
  stdio: 'inherit',
  windowsHide: true,
});
process.exit(res.status ?? 1);
