#!/usr/bin/env python3
"""Check FAT rename bytes, publication order and every interrupted write/flush."""
import importlib.util
import json
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("rename_writes", ROOT / "tests/fat16-write-host-test.py")
writes = importlib.util.module_from_spec(spec); spec.loader.exec_module(writes)
images = writes.images
OUTPUT = ROOT / "build/test-artifacts/file-tools"


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True); report = []
    with tempfile.TemporaryDirectory(prefix="rum-rename-") as temporary:
        disk = Path(temporary) / "disk.raw"
        for cluster in (1, 2):
            images.build(disk, cluster); original = disk.read_bytes(); baseline = images.audit(disk)
            for operation, destination in (("rename", "/RENAMED.BIN"), ("rename-move", "/VOID/MOVED.BIN"),
                                           ("rename-replace", "/KEEP.BIN"), ("rename-grow", "/FULL/MOVED.BIN")):
                disk.write_bytes(original); events, result = writes.execute(disk, operation)
                data = disk.read_bytes(); state = images.audit(disk)
                assert result[0] == "ok" and "/TARGET.BIN" not in state["files"]
                assert images.file_bytes(disk, destination) == baseline["files"]["/TARGET.BIN"]
                assert state["files"][destination] == baseline["files"]["/TARGET.BIN"]
                if operation != "rename-replace": assert state["files"]["/KEEP.BIN"] == baseline["files"]["/KEEP.BIN"]
                images.command("mdir", "-i", str(disk), "::" + destination)
                writes.canaries(original, data, events)
                source = baseline["slots"]["/TARGET.BIN"]; target = state["slots"][destination]
                assert data[target + 11:target + 32] == original[source + 11:source + 32], "rename changed data/attributes/timestamps"
                if operation == "rename":
                    assert events == [f"W {source // 512}", "F"]
                else:
                    assert data[source] == 0xe5
                    # The source tombstone is followed by a flush before old
                    # target clusters may be freed in either FAT copy.
                    source_stage = max(i for i, e in enumerate(events) if e == f"W {source // 512}")
                    assert events[source_stage + 1] == "F"
                    g = state["geometry"]
                    later_fat = [i for i, e in enumerate(events) if e.startswith("W ") and
                                 g["reserved"] <= int(e.split()[1]) < g["root"] and i > source_stage]
                    assert bool(later_fat) == (operation == "rename-replace")
                # Fully written sectors, tears, and failed flushes all stop the
                # mounted writer; the C harness checks no later mutation runs.
                for stage, event in enumerate(events):
                    for tear in ((0,) if event == "F" else (0, 16, 30, 256, 512)):
                        disk.write_bytes(original); trace, _ = writes.execute(disk, operation, stage, tear)
                        assert len(trace) == stage + 1
                        after = disk.read_bytes(); writes.canaries(original, after, trace)
                        state = images.audit(disk, require_clean=False)
                        report.append(dict(cluster=cluster, operation=operation, stage=stage, tear=tear, repair=state["issues"]))
                print(f"PASS: FAT16 {cluster * 512}-byte clusters, {operation}: exact bytes, mtools, ordering and every I/O stage", flush=True)
        (OUTPUT / "rename-failures.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__": main()
