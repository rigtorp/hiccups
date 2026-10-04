#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Linux integration checks: python3 tests/startup.py path/to/hiccups."""
import os
from pathlib import Path
import resource
import subprocess
import sys
import tempfile
import time


def main():
    binary = str(Path(sys.argv[1]).resolve())
    cpus = sorted(os.sched_getaffinity(0))
    if len(cpus) < 3:
        raise SystemExit("These tests require at least three allowed CPUs")
    hard_lock = resource.getrlimit(resource.RLIMIT_MEMLOCK)[1]
    lock_limit = 8 * 1024 * 1024
    if hard_lock != resource.RLIM_INFINITY:
        lock_limit = min(lock_limit, hard_lock)

    with tempfile.TemporaryDirectory(prefix="hiccups-startup-") as directory:
        hooks = Path(directory) / "hooks.so"
        subprocess.run([os.environ.get("CC", "cc"), "-shared", "-fPIC",
                        str(Path(__file__).with_name("startup_hooks.c")),
                        "-o", str(hooks), "-ldl"], check=True)

        def run(selected, *, stack=16 * 1024 * 1024, fail=None, delay=None):
            def setup():
                os.sched_setaffinity(0, selected)
                resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
                resource.setrlimit(resource.RLIMIT_MEMLOCK,
                                   (lock_limit, hard_lock))
                _, hard_stack = resource.getrlimit(resource.RLIMIT_STACK)
                resource.setrlimit(resource.RLIMIT_STACK, (stack, hard_stack))

            env = os.environ.copy()
            env["LD_PRELOAD"] = str(hooks)
            for key in ["HICCUPS_TEST_FAIL_THREAD", "HICCUPS_TEST_LOCK_DELAY"]:
                env.pop(key, None)
            if fail is not None:
                env["HICCUPS_TEST_FAIL_THREAD"] = str(fail)
            if delay is not None:
                env["HICCUPS_TEST_LOCK_DELAY"] = str(delay)
            begin = time.monotonic()
            result = subprocess.run([binary, "-r", "1", "-s", "1", "-t",
                                     "1000000000"], env=env, preexec_fn=setup,
                                    capture_output=True, text=True, timeout=10)
            return result, time.monotonic() - begin

        def check_rows(result, selected):
            assert result.returncode == 0, result.stderr
            rows = result.stdout.splitlines()[1:]
            assert [int(row.split()[0]) for row in rows] == selected, result.stdout
            assert all(len(row.split()) == 6 for row in rows), result.stdout

        selected = cpus[:3]
        result, _ = run(selected)
        check_rows(result, selected)
        assert "TEST mlockall result=-1" in result.stderr, result.stderr
        assert "WARNING failed to lock memory" in result.stderr, result.stderr
        print("PASS: worker stacks allocated before memory locking; lock failure continues")

        for fail in [1, 2]:
            result, _ = run(selected, fail=fail)
            assert result.returncode == 1, result.stderr
            assert "Failed to create measurement thread:" in result.stderr, result.stderr
            assert not result.stdout, result.stdout
            assert "TEST mlockall" not in result.stderr, result.stderr
            print(f"PASS: thread creation {fail} fails cleanly, with no deadlock or abort")

        result, elapsed = run(selected, delay=2)
        check_rows(result, selected)
        assert elapsed >= 3, elapsed
        print("PASS: one-second measurement starts after two-second setup delay")

        result, _ = run(cpus[:1])
        check_rows(result, cpus[:1])
        print("PASS: single CPU startup")

        if lock_limit == 8 * 1024 * 1024:
            result, _ = run(cpus[::2][:2], stack=256 * 1024)
            check_rows(result, cpus[::2][:2])
            assert "TEST mlockall result=0" in result.stderr, result.stderr
            assert "WARNING failed to lock memory" not in result.stderr, result.stderr
            print("PASS: successful memory locking with small worker stacks and nonconsecutive CPUs")
        else:
            print("SKIP: successful locking requires an 8 MiB memory-lock limit")


if __name__ == "__main__":
    main()
