"""Apply this project's reviewed patches without resetting the dependency checkout."""

import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", required=True, type=Path)
    args = parser.parse_args()
    project = Path(__file__).resolve().parent.parent
    for name in (
        "msvc-countl-zero.patch",
        "runtime-irq-return-stack.patch",
        "runtime-sdl3-host.patch",
        "runtime-debug-stop.patch",
        "runtime-live-remap.patch",
        "runtime-cpufastset-rounding.patch",
        "runtime-native-frame-driver.patch",
    ):
        patch = str(project / name)
        command = ["git", "-C", str(args.engine.resolve()), "apply"]
        if (
            subprocess.run(
                command + ["--reverse", "--check", patch], capture_output=True
            ).returncode
            == 0
        ):
            print(f"Runtime patch already applied: {name}")
            continue
        checked = subprocess.run(command + ["--check", patch], capture_output=True, text=True)
        if checked.returncode:
            raise RuntimeError(
                f"Cannot safely apply {name}; dependency changes need review:\n{checked.stderr}"
            )
        subprocess.run(command + [patch], check=True)
        print(f"Applied runtime patch: {name}")


if __name__ == "__main__":
    main()
