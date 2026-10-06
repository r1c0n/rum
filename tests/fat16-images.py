#!/usr/bin/env python3
"""Build mtools FAT16 volumes and targeted corruptions of those volumes."""
import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess


def command(*args):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def geometry(data):
    sector, cluster, reserved, fats, entries, small, _, fat = struct.unpack_from("<HBHBHHBH", data, 11)
    total = small or struct.unpack_from("<I", data, 32)[0]
    root = reserved + fats * fat
    start = root + (entries * 32 + sector - 1) // sector
    return dict(sector=sector, cluster=cluster, reserved=reserved, fats=fats, entries=entries,
                fat=fat, total=total, root=root, start=start, clusters=(total - start) // cluster)


def root_slot(data, name):
    g = geometry(data)
    stem, _, extension = name.partition(".")
    encoded = (stem.upper().ljust(8) + extension.upper().ljust(3)).encode()
    for offset in range(g["root"] * 512, g["root"] * 512 + g["entries"] * 32, 32):
        if data[offset:offset + 11] == encoded:
            return offset
    raise AssertionError(f"Missing root fixture entry {name}")


def fat_set(data, cluster, value, copies=(0, 1)):
    g = geometry(data)
    for copy in copies:
        struct.pack_into("<H", data, (g["reserved"] + copy * g["fat"]) * 512 + cluster * 2, value)


def chain(data, first):
    g = geometry(data)
    result = []
    while first < 0xfff8:
        assert 2 <= first <= g["clusters"] + 1 and first not in result
        result.append(first)
        first = struct.unpack_from("<H", data, g["reserved"] * 512 + first * 2)[0]
    return result


def build(directory, name, sectors, cluster):
    image = directory / f"{name}.raw"
    command("mformat", "-C", "-i", str(image), "-T", str(sectors), "-c", str(cluster), "-v", "RUMTEST", "::")
    command("mmd", "-i", str(image), "::/DOCS", "::/DOCS/NEST")
    payloads = {
        "ROOT.TXT": b"rum on disk\n",
        "EMPTY.BIN": b"",
        "BINARY.BIN": bytes(((i * 29 + (i >> 8) * 11) ^ 0xa5) & 255 for i in range(10037)),
        "DOCS/NEST/NOTE.TXT": b"An island within an island.\n",
        "DOCS/ABCDEFGH.XYZ": bytes(range(256)) * 4,
        **{f"DOCS/F{i:03}.TXT": f"file {i}\n".encode() for i in range(70)},
    }
    for path, payload in payloads.items():
        source = directory / "source.bin"
        source.write_bytes(payload)
        command("mcopy", "-o", "-i", str(image), str(source), "::/" + path)
    source.write_bytes(b"unsupported long name")
    command("mcopy", "-i", str(image), str(source), "::/Long filename with spaces.txt")
    source.write_bytes(b"deleted")
    command("mcopy", "-i", str(image), str(source), "::/GONE.TXT")
    command("mdel", "-i", str(image), "::/GONE.TXT")
    data = bytearray(image.read_bytes())
    g = geometry(data)
    assert 4085 <= g["clusters"] < 65525 and g["fats"] == 2
    # Reorder a host-created chain to exercise genuinely fragmented reads.
    slot = root_slot(data, "BINARY.BIN")
    clusters = chain(data, struct.unpack_from("<H", data, slot + 26)[0])
    blocks = [bytes(data[(g["start"] + (c - 2) * cluster) * 512:
                              (g["start"] + (c - 1) * cluster) * 512]) for c in clusters]
    reordered = [clusters[0], *reversed(clusters[1:])]
    for index, c in enumerate(reordered):
        offset = (g["start"] + (c - 2) * cluster) * 512
        data[offset:offset + cluster * 512] = blocks[index]
        fat_set(data, c, reordered[index + 1] if index + 1 < len(reordered) else 0xffff)
    image.write_bytes(data)
    # The host FAT implementation independently verifies every expected payload.
    for path, payload in payloads.items():
        assert command("mcopy", "-i", str(image), "::/" + path, "-") == payload
    listing = []
    for path in ("", "DOCS", "DOCS/NEST"):
        names = command("mdir", "-b", "-i", str(image), "::/" + path + "/").decode().splitlines()
        for value in names:
            value = value.removeprefix("::/").removeprefix("::").rstrip("/").upper()
            if " " in value or "~" in value:  # Outside rum's supported short-name subset.
                continue
            kind = "D" if value in ("DOCS", "DOCS/NEST") else "F"
            size = 0 if kind == "D" else len(payloads[value])
            listing.append(f"/{value}\t{kind}\t{size}")
    return image, payloads, sorted(listing)


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    for tool in ("mformat", "mcopy", "mmd", "mdir", "mdel"):
        if not shutil.which(tool):
            raise RuntimeError(f"{tool} is required; install mtools for FAT16 tests")
    cases = []
    for name, sectors, cluster in (("valid", 32768, 2), ("cluster1", 32768, 1), ("cluster8", 65536, 8)):
        image, payloads, listing = build(directory, name, sectors, cluster)
        cases.append(dict(name=name, image=image.name, phase="valid", path="", error="ok", listing=listing))
    base = bytearray((directory / "valid.raw").read_bytes())
    g = geometry(base)
    file_slot, dir_slot, empty_slot = (root_slot(base, n) for n in ("BINARY.BIN", "DOCS", "EMPTY.BIN"))
    first = struct.unpack_from("<H", base, file_slot + 26)[0]
    file_chain = chain(base, first)
    dir_first = struct.unpack_from("<H", base, dir_slot + 26)[0]

    def case(name, change, phase="mount", path="", error="I/O error"):
        data = bytearray(base)
        change(data)
        (directory / f"{name}.raw").write_bytes(data)
        cases.append(dict(name=name, image=f"{name}.raw", phase=phase, path=path, error=error))

    def field(offset, fmt, value):
        return lambda data: struct.pack_into(fmt, data, offset, value)

    for name, offset, fmt, value, error in (
        ("signature", 510, "<H", 0, "invalid request"),
        ("sector-size", 11, "<H", 1024, "unsupported operation"),
        ("zero-cluster", 13, "<B", 0, "invalid request"),
        ("odd-cluster", 13, "<B", 3, "invalid request"),
        ("large-cluster", 13, "<B", 128, "unsupported operation"),
        ("no-reserved", 14, "<H", 0, "invalid request"),
        ("one-fat", 16, "<B", 1, "unsupported operation"),
        ("zero-root", 17, "<H", 0, "invalid request"),
        ("unaligned-root", 17, "<H", 17, "invalid request"),
        ("overlap", 14, "<H", 32767, "offset range"),
        ("small-fat", 22, "<H", 1, "offset range"),
        ("large-fat", 22, "<H", 65535, "unsupported operation"),
        ("partition", 28, "<I", 2048, "invalid request"),
        ("ambiguous-total", 32, "<I", 32768, "invalid request"),
        ("fat12", 19, "<H", 4096, "unsupported operation"),
    ):
        case(name, field(offset, fmt, value), error=error)
    case("truncated", lambda data: data.__delitem__(slice(-512, None)), error="offset range")
    case("fat-copy", lambda data: fat_set(data, first, 0xfff8, copies=(1,)))
    case("fat-header", lambda data: fat_set(data, 0, 0xff00))
    for name, value in (("self-loop", first), ("reserved", 0xfff0), ("bad-cluster", 0xfff7),
                        ("free-cluster", 0), ("cluster-one", 1), ("out-of-range", g["clusters"] + 2),
                        ("premature-end", 0xffff)):
        case(name, lambda data, value=value: fat_set(data, first, value), "open", "/disk/BINARY.BIN")
    case("tail-loop", lambda data: fat_set(data, file_chain[-1], file_chain[1]), "open", "/disk/BINARY.BIN")
    case("extra-chain", field(file_slot + 28, "<I", 1), "open", "/disk/BINARY.BIN")
    case("too-large-file", field(file_slot + 28, "<I", 0xffffffff), "open", "/disk/BINARY.BIN")
    case("file-outside", field(file_slot + 26, "<H", g["clusters"] + 2), "open", "/disk/BINARY.BIN")
    case("high-word", field(file_slot + 20, "<H", 1), "open", "/disk/BINARY.BIN")
    case("allocated-empty", field(empty_slot + 26, "<H", first), "open", "/disk/EMPTY.BIN")
    case("dir-loop", lambda data: fat_set(data, dir_first, dir_first), "open", "/disk/DOCS")
    case("truncated-dir", lambda data: fat_set(data, dir_first, 0), "open", "/disk/DOCS")
    case("dir-outside", field(dir_slot + 26, "<H", 0xffef), "open", "/disk/DOCS")
    case("dir-size", field(dir_slot + 28, "<I", 32), "open", "/disk/DOCS")
    (directory / "manifest.json").write_text(json.dumps(cases, indent=2) + "\n")
    return cases


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    print(f"Built {len(generate(args.directory))} FAT16 image fixtures with mtools")
