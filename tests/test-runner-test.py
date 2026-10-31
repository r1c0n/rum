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
