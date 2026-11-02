'use strict';
// Manual name bridges between florrclone display types and cpp data keys.
// Keep this the single source for audit + sync scripts.
module.exports = {
  // florrclone type name → cpp petals.json key
  petalFlorrToCpp: {
    'Mecha Antennae': 'observer',
    "Champion's Crown": 'champion_crown',
    'Light Bulb': 'bulb',
    'Ant Egg': 'egg',
    'Blueberries': 'magic_peas',
    // Beetle Egg may need its own entry later; egg is ant-oriented in reference.
    'Glass': 'glasss', // reference typo key
    "Pharaoh's Crown": 'pharaoh_s_crown',
    // Nazar Amulet is its own petal (nazar_amulet); do not alias onto amulet.
  },
  // Fields we refuse to blind-copy (unit systems differ).
  skipMobFields: ['speed', 'chase_speed', 'size', 'radius', 'visual_scale'],
};
