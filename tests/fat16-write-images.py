#!/usr/bin/env python3
"""Disposable FAT16 write fixtures, exact host reads, and allocation auditing."""
import importlib.util
from pathlib import Path
import struct
import subprocess

spec = importlib.util.spec_from_file_location("fat16_images", Path(__file__).with_name("fat16-images.py"))
fat = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fat)


def command(*args):
    return subprocess.run(args, check=True, capture_output=True, timeout=20).stdout


def pattern(size):
    return bytes(((i * 37) ^ (i >> 7) ^ 0x59) & 255 for i in range(size))


def entry(name, first=0, size=0, directory=False):
    data = bytearray(32)
    base, _, ext = name.partition(".")
    data[:11] = (base.ljust(8) + ext.ljust(3)).encode()
    data[11] = 0x10 if directory else 0x20
    for offset in (16, 18, 24):
        struct.pack_into("<H", data, offset, 0x21)
    struct.pack_into("<H", data, 26, first)
    struct.pack_into("<I", data, 28, size)
    return data


def build(path, cluster=1):
    path.parent.mkdir(parents=True, exist_ok=True)
    sectors = 8192 if cluster == 1 else 16384 if cluster == 2 else 65536
    command("mformat", "-C", "-i", str(path), "-T", str(sectors), "-c", str(cluster), "-v", "RUMTEST", "::")
    command("mmd", "-i", str(path), "::/VOID", "::/FULL")
    source = path.with_suffix(".source")
    source.write_bytes(bytes((i * 17 + 3) & 255 for i in range(1537)))
    command("mcopy", "-i", str(path), str(source), "::/TARGET.BIN")
    source.write_bytes(bytes((i * 13 ^ 0xaa) & 255 for i in range(777)))
    command("mcopy", "-i", str(path), str(source), "::/KEEP.BIN")
    data = bytearray(path.read_bytes())
    g = fat.geometry(data)
    slot = fat.root_slot(data, "FULL")
    first = struct.unpack_from("<H", data, slot + 26)[0]
    start = (g["start"] + (first - 2) * cluster) * 512
    for index in range(2, cluster * 512 // 32):
        data[start + index * 32:start + (index + 1) * 32] = entry(f"E{index:07}.TXT")
    # Device bounds exceed BPB volume bounds. Every tail sector is a canary.
    data.extend(bytes(range(256)) * 64)
    path.write_bytes(data)
    source.unlink()
    return g


def file_bytes(image, path):
    return command("mcopy", "-i", str(image), "::" + path, "-")


def audit(image, require_clean=True):
    """Independent walk includes unsupported names and both FAT copies."""
    data = image.read_bytes()
    g = fat.geometry(data)
    start = g["reserved"] * 512
    table = data[start:start + g["fat"] * 512]
    issues = []
    if table != data[start + len(table):start + 2 * len(table)]:
        issues.append("FAT copies differ")
    owners, files, directories, slots = {}, {}, {}, {}

    def chain(first, path):
        values = []
        while first < 0xfff8:
            if not 2 <= first <= g["clusters"] + 1 or first >= 0xfff0 or first in values:
                raise ValueError("invalid or cyclic chain")
            if first in owners:
                raise ValueError("shared cluster")
            owners[first] = path
            values.append(first)
            first = struct.unpack_from("<H", table, first * 2)[0]
        return values

    def contents(values):
        return b"".join(data[(g["start"] + (c - 2) * g["cluster"]) * 512:
                             (g["start"] + (c - 1) * g["cluster"]) * 512] for c in values)

    def walk(raw, path, self_cluster, parent, depth=0, physical=None):
        if depth > 16:
            raise ValueError("deep directory")
        for index in range(0, len(raw), 32):
            ent = raw[index:index + 32]
            if not ent[0]:
                return
            if ent[0] == 0xe5 or ent[11] & 8:
                continue
            name = ent[:8].decode("ascii").rstrip() + ("." + ent[8:11].decode("ascii").rstrip() if ent[8:11].strip() else "")
            first, size = struct.unpack_from("<H", ent, 26)[0], struct.unpack_from("<I", ent, 28)[0]
            if name in (".", ".."):
                if not self_cluster or index != (0 if name == "." else 32) or ent[11] != 0x10 or size or \
                        first != (self_cluster if name == "." else parent):
                    raise ValueError("invalid dot entry")
                continue
            full = path + "/" + name
            if full in files or full in directories:
                raise ValueError("duplicate name")
            if ent[11] & 0xc0 or struct.unpack_from("<H", ent, 20)[0]:
                raise ValueError("invalid attributes or high cluster word")
            values = chain(first, full) if first else []
            if ent[11] & 0x10:
                if size or not values:
                    raise ValueError("invalid directory")
                directories[full] = values
                walk(contents(values), full, first, self_cluster, depth + 1, values)
            else:
                if not size and first:
                    raise ValueError("allocated empty file")
                if len(values) != (size + g["cluster"] * 512 - 1) // (g["cluster"] * 512):
                    raise ValueError("file size and chain disagree")
                files[full] = contents(values)[:size]
            if physical is None:
                slots[full] = g["root"] * 512 + index
            else:
                c = physical[index // (g["cluster"] * 512)]
                slots[full] = (g["start"] + (c - 2) * g["cluster"]) * 512 + index % (g["cluster"] * 512)
        if self_cluster and (raw[:11] != b".          " or raw[32:43] != b"..         "):
            raise ValueError("missing dot entries")

    try:
        walk(data[g["root"] * 512:g["root"] * 512 + g["entries"] * 32], "", 0, 0)
    except (ValueError, UnicodeError, struct.error) as error:
        issues.append(str(error))
    orphaned = [c for c in range(2, g["clusters"] + 2) if c not in owners and
                struct.unpack_from("<H", table, c * 2)[0] not in (0, 0xfff7)]
    if orphaned:
        issues.append(f"{len(orphaned)} unreachable allocated clusters")
    if require_clean:
        assert not issues, issues
    return dict(geometry=g, files=files, directories=directories, slots=slots, issues=issues, owners=owners)


def verify_outputs(image):
    state = audit(image)
    assert state["files"]["/RESULT.BIN"] == pattern(16385)
    assert file_bytes(image, "/RESULT.BIN") == pattern(16385)
    assert file_bytes(image, "/SAVED/NOTE.TXT") == b"rum writes FAT16\n"
    assert file_bytes(image, "/KEEP.BIN") == bytes((i * 13 ^ 0xaa) & 255 for i in range(777))
    g = state["geometry"]
    assert len(state["directories"]["/GROW"]) >= 2
    expected = g["cluster"] * 512 // 32 + 3
    assert len([name for name in state["files"] if name.startswith("/GROW/")]) == expected
    assert state["files"]["/GROW/F0000.TXT"] == b"r"
    for directory in ("/", "/GROW", "/SAVED"):
        command("mdir", "-i", str(image), "::" + directory)
    for path, payload in state["files"].items():
        assert file_bytes(image, path) == payload, path
    return state
