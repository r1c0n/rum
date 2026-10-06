#!/usr/bin/env python3
"""Host write matrix: exact bytes, canaries, full media, and every I/O stage."""
import importlib.util
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT / "build/test-artifacts/fat16-write"
spec = importlib.util.spec_from_file_location("write_images", ROOT / "tests/fat16-write-images.py")
images = importlib.util.module_from_spec(spec)
spec.loader.exec_module(images)


def execute(image, operation, failure=-1, tear=0):
    trace = image.with_suffix(".trace")
    process = subprocess.run([str(ROOT / "build/tests/fat16-write-test"), str(image), operation,
        str(failure), str(tear), str(trace)], capture_output=True, text=True, timeout=30)
    assert process.returncode == 0, f"{operation} stage={failure} tear={tear}: {process.stdout}\n{process.stderr}"
    events = trace.read_text().splitlines()
    result = re.search(r"result=(.*?) events=(\d+) writes=(\d+) transferred=(\d+)", process.stdout)
    return events, result.groups() if result else None


def canaries(before, after, events):
    written = {int(line.split()[1]) for line in events if line.startswith("W ")}
    assert len(before) == len(after)
    for sector in range(len(before) // 512):
        if sector not in written:
            offset = sector * 512
            assert before[offset:offset + 512] == after[offset:offset + 512], f"sector {sector} changed without a request"
    g = images.fat.geometry(before)
    assert before[:g["reserved"] * 512] == after[:g["reserved"] * 512]
    assert before[g["total"] * 512:] == after[g["total"] * 512:], "write escaped BPB volume"


def ordering(before, after, events, operation):
    """Verify the sector-level publication order independently of the backend."""
    g = images.fat.geometry(before)
    writes = [(i, int(line.split()[1])) for i, line in enumerate(events) if line.startswith("W ")]
    fat0 = [i for i, lba in writes if g["reserved"] <= lba < g["reserved"] + g["fat"]]
    fat1 = [i for i, lba in writes if g["reserved"] + g["fat"] <= lba < g["root"]]
    assert fat0 and fat1
    # Create/replace publish the root slot after data and both complete FAT copies.
    if operation in ("create", "replace", "write", "grow", "shrink", "mkdir"):
        metadata = [i for i, lba in writes if g["root"] <= lba < g["start"]]
        assert len(metadata) == 1
        publish = metadata[0]
        allocation = [i for i in fat0 + fat1 if i < publish]
        assert allocation and events[max(allocation) + 1] == "F"
        data = [i for i, lba in writes if lba >= g["start"]]
        if data:
            assert max(data) < min(allocation) and events[max(data) + 1] == "F"
        assert events[publish + 1] == "F"
        # Old allocations are not released until the new directory entry is durable.
        if operation in ("replace", "write", "grow", "shrink"):
            assert max(fat0 + fat1) > publish + 1
    if operation in ("delete", "rmdir"):
        metadata = [i for i, lba in writes if g["root"] <= lba < g["start"]]
        assert len(metadata) == 1 and metadata[0] + 1 < min(fat0 + fat1) and events[metadata[0] + 1] == "F"


def keep_canary(before, after, baseline):
    g = baseline["geometry"]
    slot = baseline["slots"]["/KEEP.BIN"]
    assert before[slot:slot + 32] == after[slot:slot + 32], "unrelated directory entry changed"
    for cluster, owner in baseline["owners"].items():
        if owner != "/KEEP.BIN":
            continue
        for copy in range(2):
            offset = (g["reserved"] + copy * g["fat"]) * 512 + cluster * 2
            assert before[offset:offset + 2] == after[offset:offset + 2], "unrelated FAT entry changed"
        offset = (g["start"] + (cluster - 2) * g["cluster"]) * 512
        size = g["cluster"] * 512
        assert before[offset:offset + size] == after[offset:offset + size], "unrelated file cluster changed"


def main():
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    count = 0
    summary = []
    with tempfile.TemporaryDirectory(prefix="rum-fat-writes-") as directory:
        directory = Path(directory)
        base = directory / "base.raw"
        images.build(base)
        original = base.read_bytes()
        baseline = images.audit(base)
        work = directory / "work.raw"
        for cluster in (1, 2, 8):
            image = ARTIFACTS / f"host-cluster{cluster}.raw"
            images.build(image, cluster)
            before = image.read_bytes()
            events, _ = execute(image, "exercise")
            canaries(before, image.read_bytes(), events)
            images.verify_outputs(image)
            print(f"PASS: host writes, directory growth and mtools validation, cluster={cluster}", flush=True)
            count += 1
        for special in ("attribute", "hidden", "lfn", "orphan", "endclear", "fat-boundary", "last-clusters"):
            data = bytearray(original)
            g = baseline["geometry"]
            end = next(o for o in range(g["root"] * 512, g["root"] * 512 + g["entries"] * 32, 32) if not data[o])
            if special == "attribute":
                data[baseline["slots"]["/TARGET.BIN"] + 11] |= 1
            elif special == "hidden":
                first = baseline["directories"]["/VOID"][0]
                offset = (g["start"] + (first - 2) * g["cluster"]) * 512 + 64
                data[offset:offset + 32] = images.entry("HIDDEN~1.TXT")
            elif special in ("lfn", "orphan"):
                lfn = bytearray(32); lfn[0] = 0x41; lfn[11] = 0x0f
                if special == "lfn":
                    slot = baseline["slots"]["/TARGET.BIN"]
                    data[slot + 32:end + 64] = data[slot:end + 32]
                else:
                    slot = end
                data[slot:slot + 32] = lfn
            elif special == "endclear":
                # Stale bytes behind the logical end must not become a cross-link.
                slot = baseline["slots"]["/KEEP.BIN"]
                data[end + 32:end + 64] = data[slot:slot + 32]
                data[end + 32:end + 43] = b"STALE   BIN"
            else:
                limit = 253 if special == "fat-boundary" else g["clusters"] + 2 - 9
                for cluster in range(2, limit):
                    if not struct.unpack_from("<H", data, g["reserved"] * 512 + cluster * 2)[0]:
                        images.fat.fat_set(data, cluster, 0xfff7)
            work.write_bytes(data)
            mode = "create" if special in ("fat-boundary", "last-clusters") else special
            events, _ = execute(work, mode)
            after = work.read_bytes()
            canaries(data, after, events)
            if special in ("attribute", "hidden", "lfn", "orphan"):
                assert after == data
            else:
                images.audit(work)
                assert images.file_bytes(work, "/NEW.BIN") == images.pattern(4097)
                if special == "endclear":
                    assert not any(after[end + 32:end + 64])
            print(f"PASS: {special}", flush=True)
            count += 1
        for mode in ("readonly", "oom", "readfail"):
            work.write_bytes(original)
            events, result = execute(work, mode)
            canaries(original, work.read_bytes(), events)
            if mode != "readfail":
                assert work.read_bytes() == original
            print(f"PASS: {mode}: {result[0]}", flush=True)
            count += 1
        # Refuse a shared chain concealed behind an unsupported short name.
        for malformed in ("crosslink", "label", "dot", "full", "rootfull"):
            data = bytearray(original)
            g = baseline["geometry"]
            if malformed == "crosslink":
                target = images.fat.root_slot(data, "TARGET.BIN")
                slot = next(o for o in range(g["root"] * 512, g["root"] * 512 + g["entries"] * 32, 32) if not data[o])
                data[slot:slot + 32] = data[target:target + 32]
                data[slot:slot + 11] = b"HIDDEN~1BIN"
            elif malformed == "label":
                # A malformed label must not conceal a live allocation.
                data[baseline["slots"]["/TARGET.BIN"] + 11] = 8
            elif malformed == "dot":
                first = baseline["directories"]["/VOID"][0]
                data[(g["start"] + (first - 2) * g["cluster"]) * 512 + 26] ^= 1
            elif malformed == "full":
                for c in range(2, g["clusters"] + 2):
                    if not struct.unpack_from("<H", data, g["reserved"] * 512 + c * 2)[0]:
                        images.fat.fat_set(data, c, 0xfff7)
            else:
                for index in range(g["entries"]):
                    offset = g["root"] * 512 + index * 32
                    data[offset:offset + 32] = images.entry(f"R{index:07}.TXT")
            work.write_bytes(data)
            execute(work, "full" if malformed in ("full", "rootfull") else "reject")
            assert work.read_bytes() == data
            print(f"PASS: {malformed} rejected without writes", flush=True)
            count += 1
            if malformed == "full":
                for mode in ("fullwrite", "fullshrink"):
                    execute(work, mode)
                    assert work.read_bytes() == data
                    count += 1
                execute(work, "zero")
                images.audit(work)
                assert images.file_bytes(work, "/TARGET.BIN") == b""
                print("PASS: full-volume overwrite/shrink fail safely; truncate to zero frees space", flush=True)
                count += 1
        for free, mode in ((9, "fullgrowdir"), (1, "fullgrowmkdir")):
            data = bytearray(original)
            g = baseline["geometry"]
            for cluster in range(2, g["clusters"] + 2 - free):
                if not struct.unpack_from("<H", data, g["reserved"] * 512 + cluster * 2)[0]:
                    images.fat.fat_set(data, cluster, 0xfff7)
            work.write_bytes(data)
            execute(work, mode)
            assert work.read_bytes() == data
            print(f"PASS: {mode} reserves all needed clusters before writing", flush=True)
            count += 1
        for operation in ("create", "replace", "write", "grow", "shrink", "delete", "mkdir", "rmdir", "growdir", "growmkdir"):
            work.write_bytes(original)
            events, result = execute(work, operation)
            assert result[0] == "ok"
            completed = images.audit(work)
            canaries(original, work.read_bytes(), events)
            ordering(original, work.read_bytes(), events, operation)
            assert completed["files"]["/KEEP.BIN"] == baseline["files"]["/KEEP.BIN"]
            images.command("mdir", "-i", str(work), "::/")
            if operation == "create":
                assert images.file_bytes(work, "/NEW.BIN") == images.pattern(4097)
            elif operation == "replace":
                assert images.file_bytes(work, "/TARGET.BIN") == images.pattern(4097)
            elif operation == "write":
                assert images.file_bytes(work, "/TARGET.BIN") == baseline["files"]["/TARGET.BIN"] + images.pattern(1025)
            elif operation == "grow":
                assert images.file_bytes(work, "/TARGET.BIN") == baseline["files"]["/TARGET.BIN"] + bytes(4097 - 1537)
            elif operation == "shrink":
                assert images.file_bytes(work, "/TARGET.BIN") == baseline["files"]["/TARGET.BIN"][:513]
            for index in range(len(events)):
                for tear in (0, 16, 30, 256, 512):
                    work.write_bytes(original)
                    trace, failed = execute(work, operation, index, tear)
                    assert len(trace) == index + 1
                    data = work.read_bytes()
                    canaries(original, data, trace)
                    state = images.audit(work, require_clean=False)
                    keep_canary(original, data, baseline)
                    if operation == "write":
                        g = baseline["geometry"]
                        publication = next(i for i, event in enumerate(events) if event.startswith("W ") and
                                           g["root"] <= int(event.split()[1]) < g["start"])
                        assert int(failed[3]) == (1025 if index > publication + 1 else 0), "committed-byte count"
                    # Trace every observed interruption and whether host repair is required.
                    summary.append(dict(operation=operation, stage=index, event=events[index], tear=tear,
                                        transferred=int(failed[3]), repair=state["issues"]))
                    count += 1
            for kind in (1024, 2048):
                work.write_bytes(original)
                execute(work, operation, 0, kind)
                count += 1
            print(f"PASS: {operation}: all {len(events)} write/flush stages, five failure extents, timeout and device fault", flush=True)
        (ARTIFACTS / "faults.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"PASS: {count} host FAT16 write scenarios", flush=True)


if __name__ == "__main__":
    main()
