'use strict';
const fs = require('node:fs');
const path = require('node:path');
const { loadEngine } = require('../../florrclone/server/engine');

const root = path.join(__dirname, '..');
const api = loadEngine(path.join(__dirname, '../../florrclone/florr-local-site'));
const petals = Object.keys(api.petals).sort();
const mobs = Object.keys(api.mobs).sort();
const refPetals = Object.keys(JSON.parse(fs.readFileSync(path.join(root, 'data/petals.json'), 'utf8'))).sort();
const refMobs = Object.keys(JSON.parse(fs.readFileSync(path.join(root, 'data/mobs.json'), 'utf8'))).sort();

const norm = (s) => s.toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_|_$/g, '');
function mapNames(list) {
  const m = new Map();
  for (const n of list) {
    const k = norm(n);
    if (!m.has(k)) m.set(k, []);
    m.get(k).push(n);
  }
  return m;
}

const fP = mapNames(petals), cP = mapNames(refPetals), fM = mapNames(mobs), cM = mapNames(refMobs);
const sharedP = [...fP.keys()].filter((k) => cP.has(k)).sort();
const sharedM = [...fM.keys()].filter((k) => cM.has(k)).sort();
const onlyFlorP = [...fP.keys()].filter((k) => !cP.has(k)).sort().map((k) => ({ key: k, names: fP.get(k) }));
const onlyCppP = [...cP.keys()].filter((k) => !fP.has(k)).sort().map((k) => ({ key: k, names: cP.get(k) }));
const onlyFlorM = [...fM.keys()].filter((k) => !cM.has(k)).sort().map((k) => ({ key: k, names: fM.get(k) }));
const onlyCppM = [...cM.keys()].filter((k) => !fM.has(k)).sort().map((k) => ({ key: k, names: cM.get(k) }));

const lines = [];
const bullet = (x) => `- \`${x.key}\` ← ${x.names.map((n) => `\`${n}\``).join(', ')}`;
lines.push('# 内容表审计（初稿）', '');
lines.push('生成自 florrclone JS Engine vs florrclone-cpp `data/*.json`。');
lines.push('打击/受击/死亡/掉落动画与落点/手感 **不** 按 florrclone 对齐，一律用 reference。', '');
lines.push('## 数量', '');
lines.push('| | florrclone | florrclone-cpp |');
lines.push('|---|---:|---:|');
lines.push(`| 花瓣 | ${petals.length} | ${refPetals.length} |`);
lines.push(`| 怪物 | ${mobs.length} | ${refMobs.length} |`);
lines.push(`| 花瓣名归一后共有 | ${sharedP.length} | |`);
lines.push(`| 怪物名归一后共有 | ${sharedM.length} | |`, '');
lines.push('## 仅 florrclone 有的花瓣（需迁入或确认改名）', '');
for (const x of onlyFlorP) lines.push(bullet(x));
lines.push('', '## 仅 cpp/reference 有的花瓣', '');
for (const x of onlyCppP) lines.push(bullet(x));
lines.push('', '## 仅 florrclone 有的怪', '');
for (const x of onlyFlorM) lines.push(bullet(x));
lines.push('', '## 仅 cpp/reference 有的怪', '');
for (const x of onlyCppM) lines.push(bullet(x));
lines.push('', '## 下一步', '');
lines.push('1. 对共有条目做字段级数值 diff（damage/hp/reload 等）。');
lines.push('2. 缺项按 florrclone 补 data + 必要 C++ 行为。');
lines.push('3. 表现层保持 reference。', '');

fs.mkdirSync(path.join(root, 'docs'), { recursive: true });
fs.writeFileSync(path.join(root, 'docs/content-audit.md'), lines.join('\n'));
fs.writeFileSync(
  path.join(root, 'docs/content-audit.json'),
  JSON.stringify(
    {
      florrclone: { petals: petals.length, mobs: mobs.length },
      cpp: { petals: refPetals.length, mobs: refMobs.length },
      sharedPetals: sharedP.length,
      sharedMobs: sharedM.length,
      onlyFlorP,
      onlyCppP,
      onlyFlorM,
      onlyCppM,
    },
    null,
    2,
  ),
);
console.log(
  JSON.stringify(
    {
      sharedP: sharedP.length,
      sharedM: sharedM.length,
      onlyFlorP: onlyFlorP.length,
      onlyFlorM: onlyFlorM.length,
      onlyCppP: onlyCppP.length,
      onlyCppM: onlyCppM.length,
    },
    null,
    2,
  ),
);
