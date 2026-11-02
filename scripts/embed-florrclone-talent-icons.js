'use strict';
const fs = require('fs');
const path = require('path');

const dir = path.resolve(
  __dirname,
  '../../florrclone/florr-local-site/assets/talents',
);
const keys = [
  'health',
  'loadout',
  'rotation',
  'antennae',
  'luck',
  'medic',
  'petalHealth',
  'poison',
  'reach',
  'reload',
  'sharpEdges',
  'secondChance',
  'magnetism',
  'duplicator',
  'bodyToxicity',
  'concentratedPoison',
  'summoner',
];
const enumMap = {
  health: 'Health',
  loadout: 'Loadout',
  rotation: 'Rotation',
  antennae: 'Antennae',
  luck: 'Luck',
  medic: 'Medic',
  petalHealth: 'PetalHealth',
  poison: 'Poison',
  reach: 'Reach',
  reload: 'Reload',
  sharpEdges: 'SharpEdges',
  secondChance: 'SecondChance',
  magnetism: 'Magnetism',
  duplicator: 'Duplicator',
  bodyToxicity: 'BodyToxicity',
  concentratedPoison: 'ConcentratedPoison',
  summoner: 'Summoner',
};

let out = '';
out += '// Auto-generated from florrclone/florr-local-site/assets/talents/*.svg\n';
out += '// Do not edit by hand — run scripts/embed-florrclone-talent-icons.js\n';
out += '#pragma once\n\n';
out += '#include "shared/game/skills.h"\n\n';
out += 'namespace flix::ui {\n\n';
out += 'inline constexpr const char* talentSvg(SkillId id) {\n';
out += '    switch (id) {\n';
for (const key of keys) {
  const svg = fs.readFileSync(path.join(dir, `${key}.svg`), 'utf8').trim();
  const escaped = svg.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
  out += `    case SkillId::${enumMap[key]}: return "${escaped}";\n`;
}
out += '    default: return nullptr;\n';
out += '    }\n';
out += '}\n\n';
out += '}  // namespace flix::ui\n';

const dest = path.resolve(__dirname, '../cpp/client/ui/talent_icons.h');
fs.writeFileSync(dest, out);
console.log('wrote', dest, 'bytes', out.length);
