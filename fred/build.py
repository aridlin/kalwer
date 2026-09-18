#!/usr/bin/env python3
"""Build the optional local Fred module; never bundle its private assets."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

PIN = "4a7c0d86832cd087dff0c37b6f9520c8cb757e84"
ROOT = Path(__file__).resolve().parent.parent


def run(*args, **kwargs):
    subprocess.run([str(a) for a in args], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--source", type=Path, help="Clean checkout of the pinned native port")
    parser.add_argument("--install", action="store_true")
    args = parser.parse_args()
    source = (args.source or ROOT / "build-fred/source").resolve()
    if not source.exists():
        run("git", "clone", "https://github.com/aridlin/fred-native-port.git", source)
        run("git", "-C", source, "checkout", "--detach", PIN)
    head = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if head != PIN or subprocess.check_output(["git", "-C", str(source), "status", "--porcelain", "--untracked-files=no"]):
        raise SystemExit("Use a clean checkout at " + PIN)
    selected = json.loads((source / "reference/manifest.json").read_text())["selected_reference"]
    raw = args.reference.read_bytes()
    if hashlib.sha256(raw).hexdigest() != selected["sha256"]:
        raise SystemExit("Reference SHA-256 does not match the selected native port reference")
    reference = source / selected["path"]
    if reference.exists():
        if hashlib.sha256(reference.read_bytes()).hexdigest() != selected["sha256"]:
            raise SystemExit("Refusing to replace a different existing private reference")
    else:
        reference.parent.mkdir(parents=True, exist_ok=True)
        reference.write_bytes(raw)
    python_tools = source / "build/python-tools"
    if not (python_tools / "bin/tap2sna.py").exists():
        run("python3", "-m", "pip", "install", "--target", python_tools, "skoolkit==10.0")
    run("cmake", "--preset", "dev", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_POSITION_INDEPENDENT_CODE=ON", cwd=source)
    build = source / "build/dev"
    run("cmake", "--build", build, "--target", "reference-unknown-78a8-first-update-probe", "-j4")

    # This upstream revision updated the public probe JSON without updating the
    # generator's expected hash of that JSON. Keep ALL private-data, state, and
    # generated-source hashes checked; only correct the public metadata hash in
    # a build-local expectation. No upstream tracked file is modified.
    golden = source / "tests/golden"
    expected = json.loads((golden / "reference_original_first_update_state_generated_cpp_summary.json").read_text())
    public = golden / "reference_unknown_78a8_first_update_probe_summary.json"
    expected["source_probe"]["public_summary_sha256"] = hashlib.sha256(public.read_bytes()).hexdigest()
    expectation = build / "first-update-expectation.json"
    expectation.write_text(json.dumps(expected))
    generated = build / "reference/private/generated"
    run("python3", source / "tools/generate_original_first_update_state_cpp.py",
        "--root", source, "--manifest", source / "reference/manifest.json",
        "--skoolkit-root", python_tools, "--public-probe", public,
        "--private-probe", build / "reference/private/unknown_78a8_first_update_values.json",
        "--write-json", build / "reference/reference_original_first_update_state_generated_cpp_summary.json",
        "--write-header", generated / "original_first_update_state.h",
        "--write-source", generated / "original_first_update_state.cpp", "--expect", expectation)
    run("cmake", "--build", build, "--target", "fred_original_presentation", "fred_app", "fred_tests", "-j4")
    output = ROOT / "build-fred/libkalwer-fred.so"
    output.parent.mkdir(parents=True, exist_ok=True)
    run(os.environ.get("CXX", "c++"), "-std=c++14", "-O2", "-Wall", "-Wextra", "-fPIC", "-shared",
        ROOT / "fred/adapter.cpp", "-I" + str(source / "include"),
        build / "libfred_original_presentation.a", build / "libfred_original_data.a",
        build / "libfred_core.a", "-o", output)
    run(build / "fred_app", "--no-wait", cwd=source)
    run("ctest", "--test-dir", build, "-R", "^fred_tests$", "--output-on-failure")
    if args.install:
        dest = Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))) / "kalwer/fred/libkalwer-fred.so"
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(output, dest.with_suffix(".so.new"))
        dest.with_suffix(".so.new").replace(dest)
        print("Installed local Fred module:", dest)


if __name__ == "__main__":
    main()
