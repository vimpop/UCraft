"""Run actionable Clang analyzer checks on every production translation unit."""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--clang-tidy", default="clang-tidy")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.build.resolve() / "analysis"
    output.mkdir(parents=True, exist_ok=True)
    entries = json.loads((args.build / "compile_commands.json").read_text())
    selected = {}
    for entry in entries:
        path = Path(entry["directory"], entry["file"]).resolve()
        command = entry.get("command", " ".join(entry.get("arguments", [])))
        if path.parent == root / "src" and "UCRAFT_TEST_HOOKS" not in command:
            selected[str(path)] = entry
    expected = {str(path.resolve()) for path in (root / "src").glob("*.c")}
    if set(selected) != expected:
        raise SystemExit("compile database must contain every production src/*.c file")
    (output / "compile_commands.json").write_text(json.dumps(list(selected.values()), indent=2))
    checks = "-*,clang-analyzer-core.*,clang-analyzer-unix.*,clang-analyzer-deadcode.*"
    failures = []
    for path in sorted(selected):
        result = subprocess.run(
            [args.clang_tidy, path, "-p", str(output), "--checks=" + checks,
             "--warnings-as-errors=clang-analyzer-*"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
        (output / (Path(path).name + ".log")).write_text(result.stdout)
        print(f"{Path(path).name}: {'FAIL' if result.returncode else 'PASS'}", flush=True)
        if result.returncode:
            failures.append(path)
            print(result.stdout)
    raise SystemExit(bool(failures))


if __name__ == "__main__":
    main()
