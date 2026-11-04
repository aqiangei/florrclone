'use strict';
// Restore only still-locked rotations. Run on the post-stop, SHA-verified
// deployment candidate before starting the new server, never on a live save.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

function restoreOriginalShopLocks(save, manifest) {
  if (!save || !save.players || manifest.schemaVersion !== 1 ||
      typeof manifest.id !== 'string' || !Array.isArray(manifest.accounts)) {
    throw new Error('Invalid save or shop lock manifest');
  }
  const receipt = manifest.id + ':' + crypto.createHash('sha256')
    .update(JSON.stringify(manifest)).digest('hex');
  const stats = { restored: 0, missing: 0, unlockedOrChanged: 0, existing: 0, receipt: 0 };
  for (const entry of manifest.accounts) {
    if (typeof entry.userId !== 'string' || !Number.isSafeInteger(entry.rotation) ||
        entry.rotation < 0 || !Array.isArray(entry.offers) || entry.offers.length !== 10 ||
        entry.offers.some(o => !o || typeof o.petalType !== 'string' || !o.petalType ||
          !['common','uncommon','rare','epic','legendary','mythic','ultra','super'].includes(o.rarity) ||
          ![0,10,20,30].includes(o.discountPercent))) {
      throw new Error('Invalid shop lock manifest entry');
    }
    const player = save.players[entry.userId];
    if (!player) { ++stats.missing; continue; }
    // Explicit unlocks and newer locks are authoritative, even if a receipt
    // was removed or a player was absent from the audit snapshot.
    if (player.lockedShopRotation !== entry.rotation) { ++stats.unlockedOrChanged; continue; }
    if (Array.isArray(player.shopLockRestoreReceipts) &&
        player.shopLockRestoreReceipts.includes(receipt)) { ++stats.receipt; continue; }
    if (player.lockedShopOffersRotation === entry.rotation &&
        Array.isArray(player.lockedShopOffers) && player.lockedShopOffers.length) {
      ++stats.existing; continue;
    }
    player.lockedShopOffers = entry.offers.map(({petalType,rarity,discountPercent}) =>
      ({petalType,rarity,discountPercent}));
    player.lockedShopOffersRotation = entry.rotation;
    player.shopLockRestoreReceipts = [...(Array.isArray(player.shopLockRestoreReceipts)
      ? player.shopLockRestoreReceipts : []), receipt];
    ++stats.restored;
  }
  return stats;
}

if (require.main === module) {
  const args = process.argv.slice(2);
  const value = flag => { const i=args.indexOf(flag); return i<0?null:args[i+1]; };
  const input=value('--input'), output=value('--output');
  const manifestPath=value('--manifest') || path.join(__dirname,'manifests/shop-locks-20261104.json');
  if (!input || !output) throw new Error('Usage: node restore-original-shop-locks.cjs --input candidate.json --output candidate.json [--manifest manifest.json]');
  const save=JSON.parse(fs.readFileSync(input,'utf8'));
  const manifest=JSON.parse(fs.readFileSync(manifestPath,'utf8'));
  const stats=restoreOriginalShopLocks(save,manifest);
  const target=path.resolve(output), temporary=target+'.shop-lock-'+process.pid+'.tmp';
  fs.writeFileSync(temporary,JSON.stringify(save,null,2)+'\n',{flag:'wx'});
  fs.renameSync(temporary,target);
  console.log(JSON.stringify({migration:manifest.id,...stats}));
}
module.exports={restoreOriginalShopLocks};
