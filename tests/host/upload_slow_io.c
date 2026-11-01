/* SPDX-License-Identifier: Apache-2.0
 * Inject slow application SD reads, not TCP or libc's buffered log scanner.
 * Disabled unless VKP_TEST_READ_DELAY_US is set by an individual test.
 */
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>

ssize_t __real_read(int fd, void *buffer, size_t count);
unsigned long vkp_test_sd_reads;
void *__real_malloc(size_t size);

void *__wrap_malloc(size_t size)
{
  if (size == 65536 + 4096 && getenv("VKP_TEST_CACHE_OOM") != NULL)
    {
      errno = ENOMEM;
      return NULL;
    }

  return __real_malloc(size);
}

int __real_pthread_mutex_timedlock(pthread_mutex_t *mutex,
                                  const struct timespec *deadline);

int __wrap_pthread_mutex_timedlock(pthread_mutex_t *mutex,
                                  const struct timespec *deadline)
{
  static int injected;

  if (getenv("VKP_TEST_LOCK_BUSY") != NULL && !injected++)
    {
      int ret;

      pthread_mutex_lock(mutex);
      ret = __real_pthread_mutex_timedlock(mutex, deadline);
      pthread_mutex_unlock(mutex);
      return ret;
    }

  return __real_pthread_mutex_timedlock(mutex, deadline);
}

ssize_t __wrap_read(int fd, void *buffer, size_t count)
{
  const char *delay = getenv("VKP_TEST_READ_DELAY_US");
  const char *failures = getenv("VKP_TEST_READ_FAILS");
  static unsigned int failed;

  if (count == 4096) vkp_test_sd_reads++;

  if (count == 4096 && failures != NULL &&
      failed < strtoul(failures, NULL, 10))
    {
      failed++;
      errno = EIO;
      return -1;
    }

  if (delay != NULL && count == 4096)
    {
      usleep(strtoul(delay, NULL, 10));
    }

  return __real_read(fd, buffer, count);
}
