#!/usr/bin/env python3
"""Publish the approved public subset of Beijing's verified game snapshots."""

from datetime import datetime
import hashlib
import json
import lzma
import math
import os
from pathlib import Path
import re
import sys
import tempfile


ROLES = ('one', 'two', 'three')
MAX_INVENTORY_BYTES = 256 * 1024**2
SHAPE_KEYS = ('t', 'x', 'y', 'r', 'rx', 'ry', 'x2', 'y2', 'cx1', 'cy1', 'cx2', 'cy2',
              'points', 'rot', 'fill', 'stroke', 'sw')
SHAPE_TYPES = ('circle', 'ellipse', 'rect', 'polygon', 'line', 'curve')


def is_number(value):
    return type(value) in (int, float) and math.isfinite(value)


def public_shape(shape):
    fields = {}
    for key in SHAPE_KEYS:
        value = shape.get(key)
        if key in ('t', 'fill', 'stroke'):
            if isinstance(value, str):
                fields[key] = value
        elif key == 'points':
            if isinstance(value, list) and len(value) <= 32 and all(is_number(n) for n in value):
                fields[key] = value
        elif is_number(value):
            fields[key] = value
    return fields


def public_skins(inventory):
    catalog = inventory.get('customSkins', {})
    if not isinstance(catalog, dict):
        raise ValueError('invalid skin catalog')
    skins = []
    for key, row in sorted(catalog.items()):
        # The game treats older records without a visibility flag as public.
        if not isinstance(row, dict) or row.get('published', True) is not True:
            continue
        review = row.get('review')
        if review is not None and (not is_number(review) or review != 0):
            continue
        if row.get('id') != key or not re.fullmatch(r'[a-zA-Z0-9_-]{1,120}', key):
            continue
        shapes = row.get('shapes')
        if not isinstance(shapes, list) or not 0 < len(shapes) <= 128 or not all(isinstance(s, dict) for s in shapes):
            continue
        if not isinstance(row.get('name'), str) or not isinstance(row.get('author'), str):
            continue
        shapes = [public_shape(shape) for shape in shapes if shape.get('t') in SHAPE_TYPES]
        shapes = [shape for shape in shapes if shape['t'] != 'polygon' or len(shape.get('points', [])) >= 6]
        if not shapes:
            continue
        created = row.get('createdAt', 0)
        skins.append(dict(id=key, name=row['name'], author=row['author'],
                          createdAt=created if is_number(created) else 0, published=True, review=0,
                          shapes=shapes))
    return skins


def read_source(path, role, previous):
    candidates = sorted((p for p in (path / role).iterdir() if re.fullmatch(r'\d{8}T\d{12}Z', p.name)), reverse=True)
    if not candidates:
        raise ValueError('no verified backup')
    folder = candidates[0]
    meta = json.loads((folder / 'backup-metadata.json').read_bytes())
    size = meta.get('bytes')
    if (meta.get('verified') is not True or meta.get('status') != 'verified' or meta.get('role') != role
            or type(size) is not int or not 0 < size <= MAX_INVENTORY_BYTES):
        raise ValueError('invalid backup metadata')
    captured_at = int(datetime.fromisoformat(meta['collectedAtUtc']).timestamp() * 1000)
    if previous and previous['capturedAt'] >= captured_at:
        return previous
    with lzma.open(folder / 'inventory.json.xz', 'rb') as stream:
        raw = stream.read(size + 1)
    if len(raw) != size or hashlib.sha256(raw).hexdigest() != meta['sha256']:
        raise ValueError('backup checksum mismatch')
    return {'capturedAt': captured_at, 'skins': public_skins(json.loads(raw))}


def publish(source, destination):
    previous = json.loads(destination.read_bytes()).get('sources', {}) if destination.exists() else {}
    sources = {}
    for realm, role in enumerate(ROLES, 1):
        key = str(realm)
        try:
            sources[key] = read_source(source, role, previous.get(key))
        except (OSError, ValueError, KeyError, lzma.LZMAError) as error:
            print(f'{role}: {type(error).__name__}; keeping the previous verified generation', file=sys.stderr)
            if key in previous:
                sources[key] = previous[key]
    if sources == previous:
        return
    blob = json.dumps({'version': 1, 'sources': sources}, ensure_ascii=False,
                      allow_nan=False, separators=(',', ':')).encode() + b'\n'
    stream = tempfile.NamedTemporaryFile(dir=destination.parent, delete=False)
    staged = Path(stream.name)
    try:
        with stream:
            stream.write(blob)
            stream.flush()
            os.fsync(stream.fileno())
        staged.chmod(0o644)
        staged.replace(destination)
    finally:
        staged.unlink(missing_ok=True)
    print('Published skin counts:', {realm: len(source['skins']) for realm, source in sources.items()})


if __name__ == '__main__':
    publish(Path(sys.argv[1]), Path(sys.argv[2]))
