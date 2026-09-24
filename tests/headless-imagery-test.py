"""Imagery tests must report skips, not crash, when no display is available."""
import os
import subprocess
import sys

env = dict(os.environ, DISPLAY="", WAYLAND_DISPLAY="", GDK_BACKEND="x11",
           GTK_A11Y="none")
result = subprocess.run([sys.argv[1]], env=env, text=True,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        timeout=15)
print(result.stdout, end="")
assert result.returncode == 0, result.returncode
cases = [line for line in result.stdout.splitlines() if line.startswith("ok ")]
assert len(cases) == 3, cases
assert all("# SKIP" in line for line in cases), cases
