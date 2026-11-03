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
  'damage',
  'absorbing',
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
  damage: 'Damage',
  absorbing: 'Absorbing',
};

// The two reference branches have code-drawn icons instead of source assets.
const extraIcons = {
  damage: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="-32 -32 64 64"><path fill="#fff" stroke="#666" stroke-width="3" d="M-22 25L-25 22L-10 2L-18 -6L-13 -11L-6 -4L19 -28L28 -28L28 -19L4 6L11 13L6 18L-2 10Z"/></svg>',
  absorbing: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="-32 -32 64 64"><g fill="none" stroke="#fff" stroke-width="5" stroke-linecap="round"><path d="M-18 -26V-7Q-10 4 -2 -7V-26M-10 -26V27M19 -26Q3 -11 15 3H20V27M20 -26V3"/></g></svg>',
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
  const svg = extraIcons[key] || fs.readFileSync(path.join(dir, `${key}.svg`), 'utf8').trim();
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
