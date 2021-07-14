# hiccups

[![License](https://img.shields.io/github/license/rigtorp/hiccups)](https://github.com/rigtorp/hiccups/blob/master/LICENSE)

*hiccups* measures the system induced jitter ("hiccups") a CPU bound thread
experiences.

It runs a thread on each processor core that loops for a fixed interval (by
default 5 seconds). Each loop iteration acquires a timestamp and if the
difference between the last two successive timestamps exceeds a threshold, the
thread is assumed to have been interrupted and the difference is recorded. At
the end of the run the number of interruptions, 99th percentile, 99.9th
percentile and maximum interruption per processor core is output.

By default the threshold is calculated as 8 times the smallest difference
between two consecutive timestamps out of 10000 runs. Difference between two
successive timestamps exceeding the threshold is indicative of jitter introduced
by thread context switching, interrupt processing, TLB shootdowns etc.

*hiccups* was inspired by [David Riddoch's](mailto:david@riddoch.org.uk)
[sysjitter](https://www.openonload.org/download/sysjitter/sysjitter-1.4.tgz).

Linux kernel version 5.14 introduces the [*osnoise
tracer*](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/Documentation/trace/osnoise-tracer.rst)
that also measures the system jitter / noise. It additionally shows you the
sources of the jitter.

## Example

Measure jitter on CPUs 0 through 3:
```
$ taskset -c 0-3 hiccups | column -t -R 1,2,3,4,5,6
cpu  threshold_ns  hiccups  pct99_ns  pct999_ns    max_ns
  0           168    17110     83697    6590444  17010845
  1           168     9929    169555    5787333   9517076
  2           168    20728     73359    6008866  16008460
  3           168    28336      1354       4870     17869
```

In this example CPU 3 is a quiet CPU with low jitter as indicated by the 99.9th
percentile being much lower then the other CPUs.

## Building & Installing

*hiccups* requires [CMake](https://cmake.org/) 3.2 or higher and a C++17
compiler.

Building on Debian/Ubuntu:

```
sudo apt install cmake g++
cd hiccups
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

Building on RHEL/CentOS:

```
sudo yum install cmake3 g++
cd hiccups
mkdir build && cd build
cmake3 .. -DCMAKE_BUILD_TYPE=Release
make
```

Installing:

```
$ sudo make install
```

## About

This project was created by [Erik Rigtorp](https://rigtorp.se)
<[erik@rigtorp.se](mailto:erik@rigtorp.se)>.
