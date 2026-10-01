"""Merge CTest profiles and report coverage for first-party production files."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--llvm-cov", default="llvm-cov")
    parser.add_argument("--llvm-profdata", default="llvm-profdata")
    args = parser.parse_args()
    build = args.build.resolve()
    root = Path(__file__).resolve().parents[1]
    output = build / "coverage"
    output.mkdir(parents=True, exist_ok=True)
    profiles = sorted((build / "profiles").glob("*.profraw"))
    if not profiles:
        raise SystemExit("No coverage profiles; run CTest with UCRAFT_COVERAGE=ON first")
    merged = output / "merged.profdata"
    subprocess.run([args.llvm_profdata, "merge", "-sparse", *map(str, profiles),
                    "-o", str(merged)], check=True)
    objects = [build / "src/UCraft", build / "tests/ucraft_source_tests",
               build / "tests/ucraft_game_tests"]
    common = [str(objects[0]), "-object=" + str(objects[1]), "-object=" + str(objects[2]),
              "-instr-profile=" + str(merged),
              "-ignore-filename-regex=(/3rdparty/|/tests/)"]
    sources = [str(path) for path in sorted((root / "src").glob("*.c"))]
    for mode, name in [("report", "summary.txt"), ("export", "coverage.json")]:
        with (output / name).open("w") as stream:
            subprocess.run([args.llvm_cov, mode, *common, *sources], stdout=stream, check=True)
    subprocess.run([args.llvm_cov, "show", *common, "-format=html",
                    "-show-branches=count", "-output-dir=" + str(output / "html"),
                    *sources], check=True)
    print((output / "summary.txt").read_text())


if __name__ == "__main__":
    main()
