#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run an unpreloaded measurement, suitable for sanitizers and installed builds."""
import os
from pathlib import Path
import resource
import subprocess
import sys


def main():
    binary = str(Path(sys.argv[1]).resolve())
    cpus = sorted(os.sched_getaffinity(0))[:3]

    def setup():
        os.sched_setaffinity(0, cpus)
        # Sanitizers reserve large mappings and may intercept memory locking.
        resource.setrlimit(resource.RLIMIT_MEMLOCK, (0, 0))
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

    result = subprocess.run([binary, "-r", "1", "-s", "65536", "-t", "100000"],
                            preexec_fn=setup, capture_output=True, text=True,
                            timeout=10)
    assert result.returncode == 0, result.stderr
    rows = result.stdout.splitlines()
    assert rows[0] == "cpu threshold_ns hiccups pct99_ns pct999_ns max_ns", result.stdout
    assert [int(row.split()[0]) for row in rows[1:]] == cpus, result.stdout
    for row in rows[1:]:
        values = [int(value) for value in row.split()]
        assert len(values) == 6 and all(value >= 0 for value in values), row
        assert values[1] == 100000 and values[3] <= values[4] <= values[5], row
    print(result.stdout, end="")
    print(result.stderr, end="", file=sys.stderr)
    print("PASS: measurement completed on all selected CPUs")


if __name__ == "__main__":
    main()
