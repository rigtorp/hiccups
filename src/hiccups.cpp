// © 2020 Erik Rigtorp <erik@rigtorp.se>
// SPDX-License-Identifier: MIT

#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <thread>
#include <vector>

int main(int argc, char *argv[]) {

  auto runtime = std::chrono::seconds(5);
  auto threshold = std::chrono::nanoseconds::max();
  size_t nsamples = runtime.count() * 1 << 16;

  int opt;
  while ((opt = getopt(argc, argv, "r:s:t:")) != -1) {
    switch (opt) {
    case 'r':
      runtime = std::chrono::seconds(std::stoul(optarg));
      break;
    case 't':
      threshold = std::chrono::nanoseconds(std::stoul(optarg));
      break;
    case 's':
      nsamples = std::stoul(optarg);
      break;
    default:
      goto usage;
    }
  }

  if (optind != argc) {
  usage:
    std::cerr
        << "hiccups 1.0.0 © 2020 Erik Rigtorp <erik@rigtorp.se> "
           "https://github.com/rigtorp/hiccups\n"
           "usage: hiccups [-r runtime_seconds] [-t threshold_nanoseconds] "
           "[-s number_of_samples]\n";
    exit(1);
  }

  cpu_set_t set;
  CPU_ZERO(&set);
  if (sched_getaffinity(0, sizeof(set), &set) == -1) {
    perror("sched_getaffinity");
    exit(1);
  }

  // enumerate available CPUs
  std::vector<int> cpus;
  for (int i = 0; i < CPU_SETSIZE; ++i) {
    if (CPU_ISSET(i, &set)) {
      cpus.push_back(i);
    }
  }

  // calculate threshold as minimum timestamp delta * 8
  if (threshold == std::chrono::nanoseconds::max()) {
    auto ts1 = std::chrono::steady_clock::now();
    for (int i = 0; i < 10000; ++i) {
      auto ts2 = std::chrono::steady_clock::now();
      if (ts2 - ts1 < threshold) {
        threshold = ts2 - ts1;
      }
      ts1 = ts2;
    }
    threshold *= 8;
  }

  // reserve memory for samples
  std::map<int, std::vector<std::chrono::nanoseconds>> samples;
  for (int cpu : cpus) {
    samples[cpu].reserve(nsamples);
  }

  // avoid page faults and TLB shootdowns when saving samples
  if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
    perror("mlockall");
    std::cerr << "WARNING failed to lock memory, increase RLIMIT_MEMLOCK "
                 "or run with CAP_IPC_LOC capability.\n";
  }

  const auto deadline = std::chrono::steady_clock::now() + runtime;
  std::atomic<size_t> active_threads = {0};

  auto func = [&](int cpu) {
    // pin current thread to assigned CPU
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set) == -1) {
      perror("sched_setaffinity");
      exit(1);
    }

    auto &s = samples[cpu];
    active_threads.fetch_add(1, std::memory_order_release);

    // wait for all threads to be ready
    while (active_threads.load(std::memory_order_relaxed) != cpus.size())
      ;

    // run jitter measurement loop
    auto ts1 = std::chrono::steady_clock::now();
    while (ts1 < deadline) {
      auto ts2 = std::chrono::steady_clock::now();
      if (ts2 - ts1 < threshold) {
        ts1 = ts2;
        continue;
      }
      if (s.size() == s.capacity()) {
        std::cerr << "WARNING preallocated sample space exceeded, increase "
                     "threshold or number of samples.\n";
      }
      s.push_back(ts2 - ts1);
      // ts1 = ts2;
      ts1 = std::chrono::steady_clock::now();
    }
  };

  // start measurements threads
  std::vector<std::thread> threads;
  for (auto it = ++cpus.begin(); it != cpus.end(); ++it) {
    threads.emplace_back([&, cpu = *it] { func(cpu); });
  }
  func(cpus.front());

  // wait for all threads to finish
  for (auto &t : threads) {
    t.join();
  }

  // print statistics
  std::cout << "cpu threshold_ns hiccups pct99_ns pct999_ns max_ns\n";
  for (auto &[cpu, s] : samples) {
    std::sort(s.begin(), s.end());
    std::cout << cpu << " " << threshold.count() << " " << s.size() << " "
              << s[s.size() * 0.99].count() << " "
              << s[s.size() * 0.999].count() << " "
              << (!s.empty() ? s.back().count() : 0)
              << std::endl;
  }

  return 0;
}
