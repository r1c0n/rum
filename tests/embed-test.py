#!/usr/bin/env python3
"""Check binary/empty assets, deterministic updates, removals and invalid inputs."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile

script = Path(__file__).resolve().parent.parent / "scripts/embed-files.py"
spec = importlib.util.spec_from_file_location("embed_files", script)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory(prefix="rum-embed-") as temporary:
    root = Path(temporary)
    assets = root / "assets"
    assets.mkdir()
    output = root / "files.c"
    (assets / "binary.bin").write_bytes(b"\0rum\xff\n")
    (assets / "empty").write_bytes(b"")
    def generate():
        subprocess.run([sys.executable, str(script), str(assets), str(output)], check=True)
    generate()
    text = output.read_text()
    assert '"binary.bin", data_0, 6' in text and "0x00, 0x72, 0x75, 0x6d, 0xff, 0x0a" in text
    assert '"empty", data_1, 0' in text
    stamp = output.stat().st_mtime_ns
    generate()
    assert output.stat().st_mtime_ns == stamp
    (assets / "binary.bin").unlink()
    generate()
    assert "binary.bin" not in output.read_text() and '"empty", data_0, 0' in output.read_text()
    for name, data in (("bad name", b""), ("large", bytes(65537)), ("a" * 64, b"")):
        invalid = assets / name
        invalid.write_bytes(data)
        try:
            module.generate(assets)
            raise AssertionError("invalid asset accepted")
        except ValueError:
            pass
        invalid.unlink()
    (assets / "empty").unlink()
    generate()
    assert "NULL, NULL, 0" in output.read_text()
print("PASS: embedded binary/empty files, stable generation, asset removal, size/name validation")
