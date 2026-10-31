#!/usr/bin/env python3
"""Check that suite failures cannot produce a successful validation report."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("runner", ROOT / "scripts/test.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

for failed in (None, "host", "preemption"):
    with tempfile.TemporaryDirectory() as directory:
        def fake_run(name, command):
            return {"name": name, "seconds": 0, "returncode": int(name == failed)}
        with patch.object(runner, "OUTPUT", Path(directory)), patch.object(runner, "run", fake_run), \
                patch("sys.argv", ["test.py", "--jobs", "2"]), contextlib.redirect_stdout(io.StringIO()):
            status = runner.main()
        report = json.loads((Path(directory) / "result.json").read_text())
        assert bool(status) == (failed is not None)
        assert report["passed"] == (failed is None)
        assert len(report["suites"]) == (1 if failed == "host" else 17)
print("PASS: runner success, host prerequisite failure and guest failure reports")

# Check the optimized byte-range oracle against an independent sector oracle,
# including changes immediately before/after a requested write and the BPB edge.
spec = importlib.util.spec_from_file_location("fat_host", ROOT / "tests/fat16-write-host-test.py")
fat_host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fat_host)
before = bytes(20 * 512)
written = {1, 7, 8, 15}
for sector in range(20):
    for byte in (0, 511):
        after = bytearray(before)
        after[sector * 512 + byte] = 1
        with patch.object(fat_host.images.fat, "geometry", return_value={"reserved": 1, "total": 16}):
            try:
                fat_host.canaries(before, after, [f"W {lba}" for lba in written])
                accepted = True
            except AssertionError:
                accepted = False
        assert accepted == (sector in written), (sector, byte)
print("PASS: byte-range canaries detect unrequested, reserved and out-of-volume changes")
