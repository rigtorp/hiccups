# hiccups

[![License](https://img.shields.io/github/license/rigtorp/hiccups)](https://github.com/rigtorp/hiccups/blob/master/LICENSE)

*hiccups* measures jitter experienced by CPU-bound threads on Linux. It runs
one busy-loop thread per allowed logical CPU and records gaps between successive
timestamps above a threshold. Output includes the number of recorded gaps,
99th and 99.9th percentiles, and maximum gap, in nanoseconds.

The default run lasts 5 seconds. The default threshold is 8 times the smallest
of 10,000 consecutive timestamp differences.

## Usage

Use `taskset` to select CPUs:

```sh
taskset -c 0-3 hiccups | column -t -R 1,2,3,4,5,6
```

Example output:

```text
cpu  threshold_ns  hiccups  pct99_ns  pct999_ns    max_ns
  0           168    17110     83697    6590444  17010845
  1           168     9929    169555    5787333   9517076
  2           168    20728     73359    6008866  16008460
  3           168    28336      1354       4870     17869
```

Options: `-r` runtime in seconds, `-t` threshold in nanoseconds, and `-s`
preallocated samples per CPU. `taskset` cannot override container or cgroup CPU
restrictions.

If memory locking fails, hiccups warns and continues. Increase `RLIMIT_MEMLOCK`
or grant `CAP_IPC_LOCK` if locked memory is required.

## Build and install

Requires Linux, CMake 3.20+, and a C++17 compiler. Tests also require Python 3
and a C compiler; disable them with `-DBUILD_TESTING=OFF`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$HOME/.local"
```

## About

Created by [Erik Rigtorp](https://rigtorp.se)
<[erik@rigtorp.se](mailto:erik@rigtorp.se)>, inspired by David Riddoch's
[sysjitter](https://www.openonload.org/download/sysjitter/sysjitter-1.4.tgz).
