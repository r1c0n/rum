#!/usr/bin/env python3
"""Validate release provenance and collect the six GitHub release downloads."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
PAYLOADS = {"rum.iso": ("rum.iso", "iso_sha256"), "rum.zip": ("rum.zip", None),
            "rum-sdk.zip": ("rum-sdk.zip", "sdk_sha256"),
            "rum-vm-images.zip": ("vm-images/rum-vm-images.zip", "vm_images_sha256")}
ASSETS = {*PAYLOADS, "SHA256SUMS.txt", "validation.json"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def git(*arguments):
    return subprocess.check_output(["git", *arguments], cwd=ROOT, text=True).strip()


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def metadata(tag):
    version = (ROOT / "assets/disk/readme.txt").read_text().splitlines()[0].removeprefix("rum OS v")
    require(re.fullmatch(r"\d+\.\d+\.\d+", version) is not None, "invalid source release version")
    require(tag == "v" + version, "release tag must match the version in assets/disk/readme.txt")
    commit = git("rev-parse", "HEAD")
    require(git("rev-parse", "refs/tags/" + tag + "^{commit}") == commit, "tag must identify the checked-out commit")
    subprocess.run(["git", "merge-base", "--is-ancestor", commit, "origin/master"], cwd=ROOT, check=True)
    require(not git("status", "--porcelain"), "release checkout must be clean")
    return {"version": version, "tag": tag, "commit": commit}


def stage(release, output, tag):
    identity = metadata(tag)
    require(not output.exists(), "download directory already exists")
    report = read_json(release / "result.json")
    require(report.get("passed") is True and report.get("commit") == identity["commit"], "clean gate source/result mismatch")
    suite = read_json(release / "test-artifacts/suite/result.json")
    require(suite.get("passed") is True and suite.get("suites") and
            all(item["returncode"] == 0 for item in suite["suites"]), "complete suite did not pass")
    for host in ("linux", "windows"):
        evidence = read_json(release / ("host-" + host) / "result.json")
        require(evidence.get("passed") is True and evidence.get("tests_commit") == report["commit"] and
                evidence.get("host", "").lower() == host and evidence.get("commit") == identity["commit"] and
                evidence.get("iso_sha256") == report["iso_sha256"],
                host + " validation source/ISO mismatch")
    require(digest(release / "rum-system.raw") == report["system_disk_sha256"], "validated system disk changed")
    hashes = {}
    for name, (relative, field) in PAYLOADS.items():
        path = release / relative
        require(path.is_file() and not path.is_symlink(), "missing or linked download: " + name)
        hashes[name] = digest(path)
        if field:
            require(hashes[name] == report[field], "validated hash mismatch: " + name)
        if name.endswith(".zip"):
            with ZipFile(path) as archive:
                require(archive.testzip() is None, "corrupt ZIP: " + name)
                if name == "rum.zip":
                    require(hashlib.sha256(archive.read("rum.iso")).hexdigest() == hashes["rum.iso"], "packaged ISO changed")
                    require(not any(member.lower().endswith((".raw", ".img", ".vdi", ".vmdk")) for member in archive.namelist()),
                            "standalone package contains a mutable disk")
                if name == "rum-sdk.zip":
                    sdk = json.loads(archive.read("rum-sdk/sdk.json"))
                    require(sdk.get("source_commit") == report["commit"] and sdk.get("source_dirty") is False,
                            "SDK source provenance mismatch")
    validation = {"version": identity["version"], "tag": tag, "passed": True,
                  "code_commit": report["commit"], "documentation_commit": identity["commit"],
                  "documentation_changes": [], "suite_groups": len(suite["suites"]), "guest_workers": suite["jobs"],
                  "suite_seconds": suite["seconds"], "clean_check_seconds": report["elapsed_seconds"],
                  "compat_sdk_sha256": report["compat_sdk_sha256"], "linux": True, "windows": True,
                  "evidence_directory": release.as_posix(), "files": hashes}
    if os.environ.get("GITHUB_RUN_ID"):
        validation["workflow_run"] = (os.environ["GITHUB_SERVER_URL"] + "/" + os.environ["GITHUB_REPOSITORY"] +
                                      "/actions/runs/" + os.environ["GITHUB_RUN_ID"])
    output.mkdir(parents=True)
    for name, (relative, _) in PAYLOADS.items():
        shutil.copy2(release / relative, output / name)
    (output / "SHA256SUMS.txt").write_text("".join(hashes[name] + "  " + name + "\n" for name in PAYLOADS), encoding="utf-8")
    (output / "validation.json").write_text(json.dumps(validation, indent=2) + "\n", encoding="utf-8")
    verify(output, tag)
    return validation


def verify(directory, tag):
    require(re.fullmatch(r"v\d+\.\d+\.\d+", tag) is not None, "invalid release tag")
    require({p.name for p in directory.iterdir()} == ASSETS, "release must contain exactly six downloads")
    require(all(p.is_file() and not p.is_symlink() for p in directory.iterdir()), "downloads must be regular files")
    report = read_json(directory / "validation.json")
    require(report.get("passed") is True and report.get("linux") is True and report.get("windows") is True and
            report.get("tag") == tag and tag == "v" + report["version"], "release validation/tag mismatch")
    require(re.fullmatch(r"[0-9a-f]{40}", report["code_commit"]) is not None, "invalid source commit")
    require(set(report["files"]) == set(PAYLOADS), "incomplete download hashes")
    for name in PAYLOADS:
        require(digest(directory / name) == report["files"][name], "download hash mismatch: " + name)
    expected = "".join(report["files"][name] + "  " + name + "\n" for name in PAYLOADS)
    require((directory / "SHA256SUMS.txt").read_text() == expected, "checksum manifest mismatch")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("metadata", "stage", "verify"))
    parser.add_argument("--tag", required=True)
    parser.add_argument("--commit", help="require this exact validated source commit")
    parser.add_argument("--release", type=Path, default=Path("build/release-ci"))
    parser.add_argument("--output", type=Path, default=Path("build/release-downloads"))
    args = parser.parse_args()
    if args.operation == "metadata":
        result = metadata(args.tag)
        if os.environ.get("GITHUB_OUTPUT"):
            with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as stream:
                stream.write("".join(key + "=" + value + "\n" for key, value in result.items()))
    elif args.operation == "stage":
        result = stage(args.release, args.output, args.tag)
    else:
        result = verify(args.output, args.tag)
    if args.commit:
        require(result.get("code_commit", result.get("commit")) == args.commit, "download source commit mismatch")
    print("PASS: release " + args.operation + " for " + args.tag)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        raise SystemExit("release-assets: " + str(error))
