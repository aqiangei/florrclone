from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import lzma
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest


spec = importlib.util.spec_from_file_location('public_skins', Path(__file__).with_name('florr-public-skins.py'))
gallery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gallery)


def backup(root, role, minute, skins, corrupt=False):
    captured = datetime(2026, 11, 10, 6, minute, tzinfo=timezone.utc)
    folder = root / role / captured.strftime('%Y%m%dT%H%M%S%fZ')
    folder.mkdir(parents=True)
    raw = json.dumps({'customSkins': skins, 'users': {'secret': 'private'}}).encode()
    (folder / 'inventory.json.xz').write_bytes(lzma.compress(raw))
    meta = dict(role=role, status='verified', verified=True, bytes=len(raw), collectedAtUtc=captured.isoformat(),
                sha256='invalid' if corrupt else hashlib.sha256(raw).hexdigest())
    (folder / 'backup-metadata.json').write_text(json.dumps(meta), encoding='utf-8')


class PublicSkinsTest(unittest.TestCase):
    def test_privacy_namespaces_withdrawal_and_failed_backup(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            out = root / 'skins.json'
            public = dict(id='same', name='Example', author='Artist', published=True, review=0,
                          ownerId='private', shapes=[dict(t='circle', r=3, internal='private')])
            pending = {**public, 'id': 'pending', 'review': 2}
            legacy = {k: v for k, v in public.items() if k not in ('published', 'review')}
            backup(root, 'one', 1, {'same': public, 'pending': pending})
            backup(root, 'two', 1, {'same': legacy})
            backup(root, 'three', 1, {'same': {**public, 'published': False}})
            gallery.publish(root, out)
            first = out.read_text(encoding='utf-8')
            self.assertNotIn('private', first)
            self.assertNotIn('pending', first)
            self.assertEqual([len(s['skins']) for s in json.loads(first)['sources'].values()], [1, 1, 0])

            backup(root, 'one', 6, {})
            backup(root, 'two', 6, {}, corrupt=True)
            gallery.publish(root, out)
            sources = json.loads(out.read_bytes())['sources']
            self.assertEqual([len(s['skins']) for s in sources.values()], [0, 1, 0])
            self.assertGreater(sources['1']['capturedAt'], sources['2']['capturedAt'])


if __name__ == '__main__':
    unittest.main()
