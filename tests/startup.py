#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Linux integration checks: python3 tests/startup.py path/to/hiccups."""
import argparse
import os
from pathlib import Path
import resource
import subprocess
import sys
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--hooks", type=Path, help="Use prebuilt test hooks")
    args = parser.parse_args()
    binary = str(args.binary.resolve())
    cpus = sorted(os.sched_getaffinity(0))
    if len(cpus) < 2:
        print("SKIP: startup regression checks require at least two allowed CPUs")
        return 77
    hard_lock = resource.getrlimit(resource.RLIMIT_MEMLOCK)[1]
    lock_limit = 8 * 1024 * 1024
    if hard_lock != resource.RLIM_INFINITY:
        lock_limit = min(lock_limit, hard_lock)

    with tempfile.TemporaryDirectory(prefix="hiccups-startup-") as directory:
        hooks = args.hooks.resolve() if args.hooks else Path(directory) / "hooks.so"
        if not args.hooks:
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

        for fail in range(1, min(len(selected), 3)):
            result, _ = run(selected, fail=fail)
            assert result.returncode == 1, result.stderr
            assert "Failed to create measurement thread:" in result.stderr, result.stderr
            assert not result.stdout, result.stdout
            assert "TEST mlockall" not in result.stderr, result.stderr
            print(f"PASS: thread creation {fail} fails cleanly, with no deadlock or abort")
        if len(selected) < 3:
            print("SKIP: partial-startup cancellation requires three allowed CPUs")

        result, elapsed = run(selected, delay=2)
        check_rows(result, selected)
        assert elapsed >= 3, elapsed
        print("PASS: one-second measurement starts after two-second setup delay")

        result, _ = run(cpus[:1])
        check_rows(result, cpus[:1])
        print("PASS: single CPU startup")

        if lock_limit == 8 * 1024 * 1024:
            lock_cpus = cpus[::2][:2] if len(cpus) >= 3 else cpus[:2]
            result, _ = run(lock_cpus, stack=256 * 1024)
            check_rows(result, lock_cpus)
            if "TEST mlockall result=0" in result.stderr:
                assert "WARNING failed to lock memory" not in result.stderr, result.stderr
                print("PASS: successful memory locking with small worker stacks")
            else:
                assert "WARNING failed to lock memory" in result.stderr, result.stderr
                print("SKIP: runner could not lock memory even with an 8 MiB limit")
        else:
            print("SKIP: successful locking requires an 8 MiB memory-lock limit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
