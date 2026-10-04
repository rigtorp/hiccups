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
#include <system_error>
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

  enum class Start { waiting, running, cancelled };
  std::atomic<Start> start{Start::waiting};
  std::chrono::steady_clock::time_point deadline;
  std::atomic<size_t> active_threads = {0};

  auto pin = [](int cpu) {
    // pin current thread to assigned CPU
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set) == -1) {
      perror("sched_setaffinity");
      exit(1);
    }
  };

  auto func = [&](int cpu) {
    auto &s = samples[cpu];
    // The release/acquire handshake also publishes the measurement deadline.
    Start state;
    while ((state = start.load(std::memory_order_acquire)) == Start::waiting) {
      std::this_thread::yield();
    }
    if (state == Start::cancelled) {
      return;
    }

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
  threads.reserve(cpus.size() - 1);
  try {
    for (auto it = ++cpus.begin(); it != cpus.end(); ++it) {
      threads.emplace_back([&, cpu = *it] {
        pin(cpu);
        active_threads.fetch_add(1, std::memory_order_release);
        func(cpu);
      });
    }
  } catch (const std::system_error &e) {
    start.store(Start::cancelled, std::memory_order_release);
    for (auto &t : threads) {
      t.join();
    }
    std::cerr << "Failed to create measurement thread: " << e.what()
              << ". Check process/thread limits and available memory.\n";
    return 1;
  }

  pin(cpus.front());
  while (active_threads.load(std::memory_order_acquire) != threads.size()) {
    std::this_thread::yield();
  }

  // Allocate worker stacks before MCL_FUTURE can restrict new mappings.
  // Lock memory before starting measurements to avoid setup-induced jitter.
  if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
    perror("mlockall");
    std::cerr << "WARNING failed to lock memory, increase RLIMIT_MEMLOCK "
                 "or run with CAP_IPC_LOCK capability.\n";
  }

  deadline = std::chrono::steady_clock::now() + runtime;
  start.store(Start::running, std::memory_order_release);
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
              << (s.empty() ? 0 : s[s.size() * 0.99].count()) << " "
              << (s.empty() ? 0 : s[s.size() * 0.999].count()) << " "
              << (s.empty() ? 0 : s.back().count()) << std::endl;
  }

  return 0;
}
