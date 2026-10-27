"""Apply reviewed runtime patches, validating layered patches in an E: scratch copy."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

PATCHES = (
    "msvc-countl-zero.patch",
    "runtime-irq-return-stack.patch",
    "runtime-sdl3-host.patch",
    "runtime-debug-stop.patch",
    "runtime-live-remap.patch",
    "runtime-cpufastset-rounding.patch",
    "runtime-native-frame-driver.patch",
    "runtime-object-presentation.patch",
    "runtime-training-checkpoints.patch",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", required=True, type=Path)
    args = parser.parse_args()
    engine = args.engine.resolve()
    project = Path(__file__).resolve().parent.parent
    patches = [project / name for name in PATCHES]
    paths = sorted({name for patch in patches for name in re.findall(r"^\+\+\+ b/(.+)$", patch.read_text(encoding="utf-8"), re.M)})
    for name in paths:
        if not (engine / name).resolve().is_relative_to(engine):
            raise RuntimeError("Runtime patch path escapes dependency checkout")
    # New patches may intentionally touch an older patch's context. Reverse
    # the stack in a private copy, then reapply it in order. The dependency
    # checkout and its Git index stay untouched until the whole stack passes.
    with tempfile.TemporaryDirectory(prefix="seed-runtime-patches-", dir=engine.parent) as temporary:
        scratch = Path(temporary).resolve()
        if not scratch.is_relative_to(engine.parent):
            raise RuntimeError("Invalid runtime patch scratch directory")
        for name in paths:
            source, target = engine / name, scratch / name
            if source.is_file():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, target)
        def apply(patch, *options):
            return subprocess.run(["git", "apply", *options, str(patch)], cwd=scratch, capture_output=True, text=True)
        for patch in reversed(patches):
            if apply(patch, "--reverse", "--check").returncode == 0:
                result = apply(patch, "--reverse")
                if result.returncode:
                    raise RuntimeError(result.stderr)
        for patch in patches:
            result = apply(patch, "--check")
            if result.returncode:
                raise RuntimeError(f"Cannot safely prepare {patch.name}; dependency changes need review:\n{result.stderr}")
            result = apply(patch)
            if result.returncode:
                raise RuntimeError(result.stderr)
        changed = 0
        for name in paths:
            source, target = scratch / name, engine / name
            if not source.is_file():
                raise RuntimeError("Runtime patch stack unexpectedly removed a file")
            expected = source.read_bytes()
            if target.is_file() and target.read_bytes() == expected:
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            pending = target.with_name(target.name + ".seed-pending")
            pending.write_bytes(expected)
            pending.replace(target)
            changed += 1
        print(f"Validated {len(patches)} runtime patches; updated {changed} files.")


if __name__ == "__main__":
    main()
