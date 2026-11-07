#!/usr/bin/env python3
"""Collect verified copies of both independent, already-persisted game saves."""

import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timedelta, timezone
import fcntl
import hashlib
import io
import json
import lzma
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tempfile

TARGETS = {
    "one": ("154.219.105.158", "florrclone", "/var/lib/florrclone", "/var/www/florrclone-maintenance"),
    "two": ("154.219.105.161", "florr-fresh", "/var/lib/florr-fresh", "/var/www/florr-fresh-maintenance"),
}
MAX_BYTES = 256 * 1024**2
STAMP = "%Y%m%dT%H%M%S%fZ"


class Maintenance(Exception):
    pass


def snapshot_guard(service, state, gate, source):
    if os.path.lexists(state + "/deploy.lock") or os.path.lexists(gate + "/enabled"):
        raise Maintenance
    output = subprocess.check_output(
        ["systemctl", "show", service, "-p", "ActiveState", "-p", "MainPID", "-p", "InvocationID"],
        text=True, timeout=5,
    )
    status = dict(line.split("=", 1) for line in output.splitlines())
    if status["ActiveState"] != "active" or int(status["MainPID"]) <= 0:
        raise ValueError("game service is inactive")
    proc = Path("/proc") / status["MainPID"]
    args = (proc / "cmdline").read_bytes().split(b"\0")
    # Verify the live process owns this particular path, not a retired cache.
    if (b"--db", os.fsencode(source)) not in zip(args, args[1:]):
        raise ValueError("game process uses another save path")
    if any(entry.startswith(b"FLIX_AUTHORITY_MODULE=") and entry.split(b"=", 1)[1]
           for entry in (proc / "environ").read_bytes().split(b"\0")):
        raise ValueError("game process uses shared storage")
    return status["MainPID"], status["InvocationID"]


def inventory_counts(raw):
    db = json.loads(raw)
    if not isinstance(db, dict) or any(not isinstance(db.get(k), dict) for k in ("users", "players")):
        raise ValueError("invalid inventory tables")
    return {k: len(db[k]) for k in ("users", "players")}


def export_snapshot(role):
    os.nice(8)
    _, service, state, gate = TARGETS[role]
    source = state + "/cpp-state/inventory.json"
    try:
        before = snapshot_guard(service, state, gate, source)
        # Game saves are published by atomic rename. Read and hash one open inode.
        with os.fdopen(os.open(source, os.O_RDONLY | os.O_NOFOLLOW), "rb") as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or not 0 < info.st_size <= MAX_BYTES:
                raise ValueError("invalid snapshot size or file type")
            raw = stream.read(MAX_BYTES + 1)
        if len(raw) != info.st_size:
            raise ValueError("snapshot size changed during read")
        counts = inventory_counts(raw)
        packed = lzma.compress(raw, preset=1)
        if snapshot_guard(service, state, gate, source) != before:
            raise ValueError("game service changed during snapshot")
    except Maintenance:
        print(json.dumps({"status": "skipped", "role": role, "reason": "maintenance"}))
        return
    metadata = {
        "status": "captured", "role": role, "source": source,
        "sourceModifiedUtc": datetime.fromtimestamp(info.st_mtime, timezone.utc).isoformat(),
        "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest(), "encoding": "xz", **counts,
    }
    out = sys.stdout.buffer
    out.write(json.dumps(metadata).encode() + b"\n")
    out.write(packed)


def verify_snapshot(role, response):
    line, separator, packed = response.partition(b"\n")
    if not separator or len(line) > 4096:
        raise ValueError("missing snapshot header")
    info = json.loads(line)
    if info.get("role") != role:
        raise ValueError("unexpected backup source")
    if info.get("status") == "skipped" and info.get("reason") == "maintenance" and not packed:
        return info, None
    size = info.get("bytes")
    if (info.get("status") != "captured" or info.get("source") != TARGETS[role][2] + "/cpp-state/inventory.json"
            or info.get("encoding") != "xz"
            or type(size) is not int or not 0 < size <= MAX_BYTES):
        raise ValueError("invalid snapshot metadata")
    with lzma.LZMAFile(io.BytesIO(packed)) as stream:
        raw = stream.read(size + 1)
    if len(raw) != size or hashlib.sha256(raw).hexdigest() != info.get("sha256"):
        raise ValueError("snapshot SHA256 or size mismatch")
    if any(info.get(k) != count for k, count in inventory_counts(raw).items()):
        raise ValueError("snapshot table count mismatch")
    return info, packed


def write_durable(path, data):
    with path.open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def sync_directory(path):
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def publish(root, info, packed):
    now = datetime.now(timezone.utc)
    destination = root / now.strftime(STAMP)
    root.mkdir(mode=0o700, parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".incoming-", dir=root) as temp:
        stage = Path(temp) / "snapshot"
        stage.mkdir(mode=0o700)
        write_durable(stage / "inventory.json.xz", packed)
        info.update(status="verified", verified=True, collectedAtUtc=now.isoformat())
        write_durable(stage / "backup-metadata.json", (json.dumps(info, indent=2) + "\n").encode())
        sync_directory(stage)
        stage.rename(destination)
        sync_directory(root)
    return destination


def prune(root):
    # Keep every capture for two days and the newest one per UTC day for 30 days.
    now = datetime.now(timezone.utc)
    daily = set()
    for child in sorted(root.iterdir(), reverse=True):
        if (not re.fullmatch(r"\d{8}T\d{12}Z", child.name) or child.is_symlink()
                or not child.is_dir() or not (child / "backup-metadata.json").is_file()
                or not any((child / f"inventory.json.{ext}").is_file() for ext in ("xz", "gz"))):
            continue
        captured = datetime.strptime(child.name, STAMP).replace(tzinfo=timezone.utc)
        age = now - captured
        if age >= timedelta(days=30) or (age >= timedelta(days=2) and captured.date() in daily):
            shutil.rmtree(child)
        else:
            daily.add(captured.date())


def collect_one(role, root, key, known_hosts):
    host = TARGETS[role][0]
    result = subprocess.run(
        ["ssh", "-T", "-i", key, "-o", "IdentitiesOnly=yes", "-o", "BatchMode=yes",
         "-o", "StrictHostKeyChecking=yes", "-o", "GlobalKnownHostsFile=/dev/null",
         "-o", f"UserKnownHostsFile={known_hosts}", "-o", "ConnectTimeout=12",
         "-o", "ServerAliveInterval=10", "-o", "ServerAliveCountMax=2", f"root@{host}"],
        stdin=subprocess.DEVNULL, capture_output=True, timeout=255, check=False,
    )
    if result.returncode != 0:
        raise ValueError(f"SSH or export failed ({result.returncode})")
    info, packed = verify_snapshot(role, result.stdout)
    if packed is not None:
        directory = publish(root / role, info, packed)
        prune(root / role)
        info["directory"] = str(directory)
    return info


def collect(root, key, known_hosts):
    root.mkdir(mode=0o700, parents=True, exist_ok=True)
    with (root / ".collect.lock").open("ab") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print('{"status":"skipped","reason":"collector-already-running"}')
            return 0
        failed = False
        with ThreadPoolExecutor(max_workers=len(TARGETS)) as executor:
            jobs = {role: executor.submit(collect_one, role, root, key, known_hosts) for role in TARGETS}
            for role, job in jobs.items():
                try:
                    print(json.dumps(job.result()), flush=True)
                except Exception as exc:
                    # Avoid tracebacks and raw SSH output, which may contain private details.
                    reason = str(exc) if type(exc) is ValueError else type(exc).__name__
                    print(json.dumps({"status": "failed", "role": role, "reason": reason}), flush=True)
                    failed = True
        return int(failed)


def main():
    os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    export = commands.add_parser("export")
    export.add_argument("role", choices=TARGETS)
    pull = commands.add_parser("collect")
    pull.add_argument("root", type=Path)
    pull.add_argument("key")
    pull.add_argument("known_hosts")
    args = parser.parse_args()
    if args.command == "export":
        try:
            export_snapshot(args.role)
            return 0
        except Exception as exc:
            print(f"Snapshot export failed ({type(exc).__name__})", file=sys.stderr)
            return 1
    return collect(args.root, args.key, args.known_hosts)


if __name__ == "__main__":
    sys.exit(main())
