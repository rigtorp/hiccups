// SPDX-License-Identifier: MIT
// Test-only fault injection and observation of memory locking.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*routine)(void *), void *arg) {
  static unsigned calls;
  const char *fail = getenv("HICCUPS_TEST_FAIL_THREAD");
  if (fail && ++calls == strtoul(fail, NULL, 10)) {
    return EAGAIN;
  }
  int (*create)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *) =
      dlsym(RTLD_NEXT, "pthread_create");
  return create(thread, attr, routine, arg);
}

int mlockall(int flags) {
  const char *delay = getenv("HICCUPS_TEST_LOCK_DELAY");
  if (delay) {
    struct timespec remaining = {strtol(delay, NULL, 10), 0};
    while (nanosleep(&remaining, &remaining) == -1 && errno == EINTR) {
    }
  }
  int (*lock)(int) = dlsym(RTLD_NEXT, "mlockall");
  int result = lock(flags);
  int saved_errno = errno;
  char message[80];
  int length = snprintf(message, sizeof(message), "TEST mlockall result=%d\n",
                        result);
  write(STDERR_FILENO, message, (size_t)length);
  errno = saved_errno;
  return result;
}
