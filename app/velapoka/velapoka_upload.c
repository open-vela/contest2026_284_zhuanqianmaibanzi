/****************************************************************************
 * apps/app/velapoka/velapoka_upload.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <net/if.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <netutils/netlib.h>

#include "velapoka_upload.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define UPLOAD_CHUNK 4096
#define UPLOAD_MAX_FILE (UINT64_C(512) * 1024 * 1024)
#define UPLOAD_CONNECT_MS 3000
#define UPLOAD_IO_MS 30000
#ifndef UPLOAD_REPLY_MAX_MS
#  define UPLOAD_REPLY_MAX_MS 120000
#endif
#define UPLOAD_KEEPALIVE_MS 1000
#define UPLOAD_CACHE_SIZE 65536
#define UPLOAD_ATTEMPTS 3

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct velapoka_upload_s
{
  pthread_mutex_t lock;
  pthread_t thread;
  FAR struct velapoka_storage_s *storage;
  struct velapoka_upload_status_s status;
  uint64_t started_ms;
  bool stop;
  bool busy;
  bool cache_enabled;
  int request;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool upload_stopping(FAR struct velapoka_upload_s *u)
{
  bool stop;

  pthread_mutex_lock(&u->lock);
  stop = u->stop;
  pthread_mutex_unlock(&u->lock);
  return stop;
}

static uint64_t upload_now_ms(void)
{
  struct timespec now;

  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void upload_phase(FAR struct velapoka_upload_s *u,
                         FAR const char *phase)
{
  bool changed;

  pthread_mutex_lock(&u->lock);
  changed = strcmp(u->status.phase, phase) != 0;
  strlcpy(u->status.phase, phase, sizeof(u->status.phase));
  pthread_mutex_unlock(&u->lock);
  if (changed)
    {
      syslog(LOG_INFO, "velapoka upload: %s\n", phase);
    }
}

/* Nonblocking socket operations poll in 100 ms steps so Exit can cancel an
 * absent/slow PC without waiting for an entire file or an unbounded connect.
 */

static int upload_wait(FAR struct velapoka_upload_s *u, int fd,
                        short events, unsigned int timeout_ms)
{
  struct pollfd p;
  uint64_t deadline = upload_now_ms() + timeout_ms;

  memset(&p, 0, sizeof(p));
  p.fd = fd;
  p.events = events;
  while (upload_now_ms() < deadline)
    {
      int ret;

      if (upload_stopping(u))
        {
          return -ECANCELED;
        }

      ret = poll(&p, 1, 100);
      if (ret < 0 && errno != EINTR)
        {
          return -errno;
        }

      if (ret > 0)
        {
          return (p.revents & events) != 0 ? OK : -ECONNRESET;
        }
    }

  return -ETIMEDOUT;
}

static int upload_send(FAR struct velapoka_upload_s *u, int fd,
                        FAR const void *data, size_t length)
{
  FAR const uint8_t *bytes = data;
  uint64_t deadline = upload_now_ms() + UPLOAD_IO_MS;

  while (length > 0)
    {
      ssize_t count;
      int ret;

      if (upload_now_ms() >= deadline)
        {
          return -ETIMEDOUT;
        }

      ret = upload_wait(u, fd, POLLOUT, UPLOAD_IO_MS);

      if (ret < 0)
        {
          return ret;
        }

      count = send(fd, bytes, length, MSG_NOSIGNAL);
      if (count < 0 && (errno == EAGAIN || errno == EINTR))
        {
          continue;
        }

      if (count <= 0)
        {
          return count < 0 ? -errno : -EPIPE;
        }

      bytes += count;
      length -= count;
      deadline = upload_now_ms() + UPLOAD_IO_MS;
    }

  return OK;
}

static int upload_reply(FAR struct velapoka_upload_s *u, int fd,
                         FAR char *line, size_t size)
{
  size_t used;
  uint64_t deadline = upload_now_ms() + UPLOAD_REPLY_MAX_MS;

  used = 0;
  while (used + 1 < size)
    {
      ssize_t count;
      int ret;

      if (upload_now_ms() >= deadline)
        {
          return -ETIMEDOUT;
        }

      ret = upload_wait(u, fd, POLLIN, UPLOAD_IO_MS);

      if (ret < 0)
        {
          return ret;
        }

      count = recv(fd, line + used, 1, 0);
      if (count < 0 && (errno == EAGAIN || errno == EINTR))
        {
          continue;
        }

      if (count <= 0)
        {
          return count < 0 ? -errno : -ECONNRESET;
        }

      if (line[used++] == '\n')
        {
          line[used - 1] = '\0';
          if (strcmp(line, "WAIT") == 0)
            {
              used = 0;
              continue;
            }

          return OK;
        }
    }

  return -EPROTO;
}

static uint32_t upload_crc(uint32_t crc, FAR const uint8_t *data,
                             size_t length)
{
  size_t i;

  for (i = 0; i < length; i++)
    {
      unsigned int bit;

      crc ^= data[i];
      for (bit = 0; bit < 8; bit++)
        {
          crc = (crc >> 1) ^
                (UINT32_C(0xedb88320) & (0 - (crc & 1)));
        }
    }

  return crc;
}

static int upload_ping(FAR struct velapoka_upload_s *u, int fd)
{
  char reply[32];
  int ret = upload_send(u, fd, "PING\n", 5);

  if (ret == OK)
    {
      ret = upload_reply(u, fd, reply, sizeof(reply));
      if (ret == OK && strcmp(reply, "OK") != 0)
        {
          ret = -EPROTO;
        }
    }

  return ret;
}

/* Only use between commands, never inside a FILE payload.  A byte threshold
 * alone leaves small BMP files silent for their entire CRC scan on slow SD.
 */

static int upload_keepalive(FAR struct velapoka_upload_s *u, int fd,
                             FAR uint64_t *last)
{
  int ret = OK;

  if (upload_stopping(u))
    {
      return -ECANCELED;
    }

  if (upload_now_ms() - *last >= UPLOAD_KEEPALIVE_MS)
    {
      ret = upload_ping(u, fd);
      *last = upload_now_ms();
    }

  return ret;
}

static void upload_telemetry(FAR struct velapoka_upload_s *u)
{
  struct velapoka_storage_usage_s usage;
  struct in_addr address;
  char ip[16] = "unavailable";
  char mask[16] = "unavailable";
  char mac_text[18] = "unavailable";
  uint8_t mac[6];
  uint8_t flags = 0;
  bool known;
  bool storage_ok;

  known = netlib_getifstatus("eth0", &flags) == OK;
  if (netlib_get_ipv4addr("eth0", &address) == OK)
    {
      inet_ntop(AF_INET, &address, ip, sizeof(ip));
    }

  if (netlib_get_ipv4netmask("eth0", &address) == OK)
    {
      inet_ntop(AF_INET, &address, mask, sizeof(mask));
    }

  if (netlib_getmacaddr("eth0", mac) == OK)
    {
      snprintf(mac_text, sizeof(mac_text),
               "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2],
               mac[3], mac[4], mac[5]);
    }

  memset(&usage, 0, sizeof(usage));
  storage_ok = velapoka_storage_get_usage(u->storage, &usage) == OK;
  pthread_mutex_lock(&u->lock);
  strlcpy(u->status.ip, ip, sizeof(u->status.ip));
  strlcpy(u->status.netmask, mask, sizeof(u->status.netmask));
  strlcpy(u->status.mac, mac_text, sizeof(u->status.mac));
  u->status.link_known = known;
  u->status.link_up = known && IFF_IS_RUNNING(flags);
  u->status.storage_online = storage_ok;
  u->status.storage = usage;
  if (!u->status.link_up)
    {
      u->status.receiver_online = false;
    }

  pthread_mutex_unlock(&u->lock);
}

static int upload_connect(FAR struct velapoka_upload_s *u)
{
  struct sockaddr_in address;
  struct timeval timeout;
  uint8_t mac[6];
  char hello[40];
  char reply[32];
  int error;
  socklen_t error_size = sizeof(error);
  int fd;
  int ret;

  upload_phase(u, "Connecting");
  timeout.tv_sec = 3;
  timeout.tv_usec = 0;
  if (netlib_getmacaddr("eth0", mac) < 0)
    {
      return -ENODEV;
    }

  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(CONFIG_EXAMPLES_VELAPOKA_UPLOAD_PORT);
  if (inet_pton(AF_INET, CONFIG_EXAMPLES_VELAPOKA_UPLOAD_HOST,
                &address.sin_addr) != 1)
    {
      return -EINVAL;
    }

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    {
      return -errno;
    }

  /* NuttX unbuffered TCP send can wait for ACKs even on a nonblocking fd.
   * Bound each send as well as the poll/reply waits.
   */

  if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                 sizeof(timeout)) < 0)
    {
      ret = -errno;
      goto fail;
    }

  ret = fcntl(fd, F_GETFL, 0);
  if (ret < 0 || fcntl(fd, F_SETFL, ret | O_NONBLOCK) < 0)
    {
      ret = -errno;
      goto fail;
    }

  ret = connect(fd, (FAR struct sockaddr *)&address, sizeof(address));
  if (ret < 0)
    {
      ret = errno == EINPROGRESS ?
            upload_wait(u, fd, POLLOUT, UPLOAD_CONNECT_MS) : -errno;
      if (ret < 0)
        {
          goto fail;
        }
    }

  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_size) < 0)
    {
      ret = -errno;
      goto fail;
    }

  if (error != 0)
    {
      ret = -error;
      goto fail;
    }

  upload_phase(u, "Handshake");
  snprintf(hello, sizeof(hello), "VKP2 %02x%02x%02x%02x%02x%02x\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  ret = upload_send(u, fd, hello, strlen(hello));
  if (ret == OK)
    {
      ret = upload_reply(u, fd, reply, sizeof(reply));
      if (ret == OK && strcmp(reply, "OK2") != 0)
        {
          ret = -EPROTO;
        }
    }

  if (ret == OK)
    {
      return fd;
    }

fail:
  close(fd);
  return ret;
}

static int upload_file(FAR struct velapoka_upload_s *u, int fd,
                        FAR struct velapoka_export_s *session,
                        FAR struct velapoka_export_file_s *file,
                        FAR uint8_t *buffer)
{
  char header[128];
  char reply[32];
  uint64_t offset = 0;
  uint32_t crc = UINT32_MAX;
  uint64_t last = upload_now_ms();
  FAR uint8_t *cache = u->cache_enabled && file->size <= UPLOAD_CACHE_SIZE ?
                       buffer + UPLOAD_CHUNK : NULL;
  int ret;

  if (file->size > UPLOAD_MAX_FILE)
    {
      return -EFBIG;
    }

  pthread_mutex_lock(&u->lock);
  strlcpy(u->status.file, file->name, sizeof(u->status.file));
  u->status.file_size = file->size;
  u->status.file_sent = 0;
  pthread_mutex_unlock(&u->lock);
  upload_phase(u, "Checking file CRC");
  syslog(LOG_INFO, "velapoka upload: file=%s size=%" PRIu64 "\n",
         file->name, file->size);

  while (offset < file->size)
    {
      ssize_t count;
      FAR uint8_t *data = cache != NULL ? cache + offset : buffer;

      if (upload_stopping(u))
        {
          return -ECANCELED;
        }

      count = velapoka_storage_export_read(session, file, offset, data,
                                           UPLOAD_CHUNK);
      if (count <= 0)
        {
          return count < 0 ? count : -EIO;
        }

      crc = upload_crc(crc, data, count);
      offset += count;
      ret = upload_keepalive(u, fd, &last);
      if (ret < 0)
        {
          return ret;
        }
    }

  upload_phase(u, "Negotiating file");
  snprintf(header, sizeof(header), "FILE %s %" PRIu64 " %08" PRIx32 "\n",
           file->name, file->size, ~crc);
  ret = upload_send(u, fd, header, strlen(header));
  if (ret == OK)
    {
      ret = upload_reply(u, fd, reply, sizeof(reply));
    }

  if (ret < 0)
    {
      return ret;
    }

  if (strcmp(reply, "HAVE") == 0)
    {
      pthread_mutex_lock(&u->lock);
      u->status.skipped_files++;
      u->status.file_sent = file->size;
      pthread_mutex_unlock(&u->lock);
      return OK;
    }

  if (strcmp(reply, "SEND") != 0)
    {
      return -EPROTO;
    }

  upload_phase(u, "Sending file");
  offset = 0;
  while (offset < file->size)
    {
      ssize_t count;

      if (upload_stopping(u))
        {
          return -ECANCELED;
        }

      if (cache != NULL)
        {
          count = file->size - offset < UPLOAD_CHUNK ?
                  file->size - offset : UPLOAD_CHUNK;
        }
      else
        {
          count = velapoka_storage_export_read(session, file, offset, buffer,
                                               UPLOAD_CHUNK);
        }

      if (count <= 0)
        {
          return count < 0 ? count : -EIO;
        }

      ret = upload_send(u, fd, cache != NULL ? cache + offset : buffer,
                         count);
      if (ret < 0)
        {
          return ret;
        }

      offset += count;
      pthread_mutex_lock(&u->lock);
      u->status.file_sent = offset;
      u->status.bytes_sent += count;
      pthread_mutex_unlock(&u->lock);
    }

  upload_phase(u, "Waiting for PC save");
  ret = upload_reply(u, fd, reply, sizeof(reply));
  if (ret == OK && strcmp(reply, "OK") != 0)
    {
      ret = -EBADMSG;
    }

  if (ret == OK)
    {
      pthread_mutex_lock(&u->lock);
      u->status.sent_files++;
      pthread_mutex_unlock(&u->lock);
    }

  return ret;
}

static bool upload_transport_error(int error)
{
  return error == -ENOTCONN || error == -ECONNRESET ||
         error == -ECONNREFUSED || error == -EPIPE ||
         error == -ETIMEDOUT || error == -ENETUNREACH ||
         error == -ENETDOWN || error == -EHOSTUNREACH ||
         error == -EPROTO;
}

static int upload_retry_file(FAR struct velapoka_upload_s *u, FAR int *fd,
                              FAR struct velapoka_export_s *session,
                              FAR struct velapoka_export_file_s *file,
                              FAR uint8_t *buffer)
{
  unsigned int attempt;
  int ret = -EIO;

  for (attempt = 0; attempt < UPLOAD_ATTEMPTS; attempt++)
    {
      unsigned int delay;

      if (attempt > 0)
        {
          upload_phase(u, "Retrying file");
          pthread_mutex_lock(&u->lock);
          u->status.retries++;
          pthread_mutex_unlock(&u->lock);
        }

      for (delay = 0; delay < attempt * 3; delay++)
        {
          if (upload_stopping(u))
            {
              return -ECANCELED;
            }

          usleep(100000);
        }

      if (upload_stopping(u))
        {
          return -ECANCELED;
        }

      if (file->fd < 0)
        {
          upload_phase(u, "Opening file");
          ret = velapoka_storage_export_reopen(session, file);
          if (ret < 0)
            {
              goto failed;
            }
        }

      if (*fd < 0)
        {
          *fd = upload_connect(u);
          if (*fd < 0)
            {
              ret = *fd;
              goto failed;
            }
        }

      ret = upload_file(u, *fd, session, file, buffer);
      if (ret == OK)
        {
          return OK;
        }

failed:
      /* Any error might leave the peer in a raw FILE body. Never inject a
       * new command into that connection; reconnect and reopen the file.
       */

      if (*fd >= 0)
        {
          close(*fd);
          *fd = -1;
        }

      if (file->fd >= 0)
        {
          close(file->fd);
          file->fd = -1;
        }

      if (ret == -ECANCELED || ret == -ENOENT || ret == -EINVAL ||
          ret == -EFBIG || ret == -EPROTO)
        {
          break;
        }
    }

  return ret;
}

static int upload_failure(FAR struct velapoka_upload_s *u, FAR int *fd,
                           FAR const char *name, int error)
{
  char command[96];
  char reply[32];
  int ret;

  pthread_mutex_lock(&u->lock);
  u->status.failed_files++;
  strlcpy(u->status.last_failed, name, sizeof(u->status.last_failed));
  if (u->status.error == OK)
    {
      u->status.error = error;
    }

  pthread_mutex_unlock(&u->lock);
  syslog(LOG_ERR, "velapoka upload: failed file=%s error=%d\n", name,
         error);
  ret = velapoka_storage_upload_failure(u->storage, name, error);
  if (ret < 0)
    {
      pthread_mutex_lock(&u->lock);
      u->status.failure_log_error = true;
      pthread_mutex_unlock(&u->lock);
    }

  if (upload_transport_error(error) || upload_stopping(u))
    {
      return error;
    }

  if (*fd < 0)
    {
      *fd = upload_connect(u);
      if (*fd < 0)
        {
          return *fd;
        }
    }

  upload_phase(u, "Recording file failure");
  snprintf(command, sizeof(command), "FAIL %s %d\n", name, error);
  ret = upload_send(u, *fd, command, strlen(command));
  if (ret == OK)
    {
      ret = upload_reply(u, *fd, reply, sizeof(reply));
      if (ret == OK && strcmp(reply, "OK") != 0)
        {
          ret = -EPROTO;
        }
    }

  return ret;
}

static int upload_batch(FAR struct velapoka_upload_s *u, FAR int *fd)
{
  FAR struct velapoka_export_s *session;
  struct velapoka_export_file_s file;
  FAR uint8_t *buffer;
  char reply[32];
  char command[96];
  uint64_t last = upload_now_ms();
  int ret;

  upload_phase(u, "Opening SD snapshot");
  ret = velapoka_storage_export_begin(u->storage, &session);
  if (ret < 0)
    {
      return ret;
    }

  buffer = malloc(UPLOAD_CHUNK + UPLOAD_CACHE_SIZE);
  u->cache_enabled = buffer != NULL;
  if (buffer == NULL)
    {
      buffer = malloc(UPLOAD_CHUNK);
    }

  if (buffer == NULL)
    {
      velapoka_storage_export_end(session);
      return -ENOMEM;
    }

  for (; ; )
    {
      if (upload_stopping(u))
        {
          ret = -ECANCELED;
          break;
        }

      upload_phase(u, "Scanning SD records");
      ret = upload_keepalive(u, *fd, &last);
      if (ret < 0)
        {
          break;
        }

      ret = velapoka_storage_export_next(session, &file);
      if (ret == -EAGAIN)
        {
          ret = upload_ping(u, *fd);
          if (ret == OK)
            {
              continue;
            }
        }

      if (ret == 0)
        {
          break;
        }

      if (ret < 0 && file.name[0] == '\0')
        {
          upload_failure(u, fd, "results.jsonl", ret);
          break;
        }

      /* Publish the JSONL snapshot only after every referenced image is
       * confirmed. A partial batch must not replace a complete manifest.
       */

      if (strcmp(file.name, "results.jsonl") == 0 &&
          u->status.failed_files > 0)
        {
          if (file.fd >= 0)
            {
              close(file.fd);
            }

          ret = OK;
          break;
        }

      ret = upload_retry_file(u, fd, session, &file, buffer);
      if (file.fd >= 0)
        {
          close(file.fd);
        }

      if (ret < 0)
        {
          int failure_ret;

          if (ret == -ECANCELED)
            {
              break;
            }

          failure_ret = upload_failure(u, fd, file.name, ret);
          if (failure_ret < 0)
            {
              ret = failure_ret;
              break;
            }
        }

      last = upload_now_ms();
    }

  if (ret < 0 && ret != -ECANCELED && u->status.failed_files == 0)
    {
      upload_failure(u, fd, "results.jsonl", ret);
    }

  free(buffer);
  velapoka_storage_export_end(session);
  pthread_mutex_lock(&u->lock);
  u->status.results_withheld = ret < 0 || u->status.failed_files > 0;
  pthread_mutex_unlock(&u->lock);
  if (ret == OK && *fd >= 0)
    {
      upload_phase(u, "Confirming batch");
      snprintf(command, sizeof(command), "END %u %u %u\n",
               u->status.sent_files, u->status.skipped_files,
               u->status.failed_files);
      ret = upload_send(u, *fd, command, strlen(command));
      if (ret == OK)
        {
          ret = upload_reply(u, *fd, reply, sizeof(reply));
          if (ret == OK && strcmp(reply, "OK") != 0)
            {
              ret = -EPROTO;
            }
        }
    }

  return ret == OK && u->status.failed_files > 0 ? u->status.error : ret;
}

static FAR void *upload_thread(FAR void *arg)
{
  FAR struct velapoka_upload_s *u = arg;
  unsigned int ticks = 0;

  while (!upload_stopping(u))
    {
      int request;

      if (ticks++ % 20 == 0)
        {
          upload_telemetry(u);
        }

      pthread_mutex_lock(&u->lock);
      request = u->request;
      u->request = 0;
      pthread_mutex_unlock(&u->lock);
      if (request != 0)
        {
          int fd;
          int ret;

          upload_telemetry(u);
          fd = upload_connect(u);
          pthread_mutex_lock(&u->lock);
          u->status.receiver_online = fd >= 0;
          pthread_mutex_unlock(&u->lock);
          ret = fd < 0 ? fd : OK;
          if (fd >= 0 && request == 2)
            {
              pthread_mutex_lock(&u->lock);
              u->status.state = VELAPOKA_UPLOAD_SENDING;
              pthread_mutex_unlock(&u->lock);
              ret = upload_batch(u, &fd);
            }

          if (fd >= 0)
            {
              close(fd);
            }

          if (ret == OK)
            {
              upload_phase(u, request == 2 ? "Complete" : "PC check OK");
            }
          else
            {
              syslog(LOG_ERR, "velapoka upload: phase=%s error=%d\n",
                     u->status.phase, ret);
              if (u->status.failed_files > 0)
                {
                  upload_phase(u, "Incomplete: see failure log");
                }
            }

          pthread_mutex_lock(&u->lock);
          u->status.error = ret;
          u->status.elapsed_ms = upload_now_ms() - u->started_ms;
          u->status.state = ret < 0 &&
                            (u->status.failed_files > 0 ||
                             u->status.sent_files > 0 ||
                             u->status.skipped_files > 0) ?
                            VELAPOKA_UPLOAD_PARTIAL :
                            ret < 0 ? VELAPOKA_UPLOAD_ERROR :
                            request == 2 ? VELAPOKA_UPLOAD_DONE :
                            VELAPOKA_UPLOAD_IDLE;
          u->busy = false;
          pthread_mutex_unlock(&u->lock);
          upload_telemetry(u);
        }

      usleep(100000);
    }

  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int velapoka_upload_start(FAR struct velapoka_upload_s **upload,
                          FAR struct velapoka_storage_s *storage)
{
  FAR struct velapoka_upload_s *u;
  struct sched_param priority;
  pthread_attr_t attr;
  int ret;

  *upload = NULL;
  u = calloc(1, sizeof(*u));
  if (u == NULL)
    {
      return -ENOMEM;
    }

  ret = pthread_mutex_init(&u->lock, NULL);
  if (ret != 0)
    {
      free(u);
      return -ret;
    }

  u->storage = storage;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 8192);
  memset(&priority, 0, sizeof(priority));
  priority.sched_priority = 80;
  pthread_attr_setschedparam(&attr, &priority);
  ret = pthread_create(&u->thread, &attr, upload_thread, u);
  pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      pthread_mutex_destroy(&u->lock);
      free(u);
      return -ret;
    }

  *upload = u;
  return OK;
}

int velapoka_upload_request(FAR struct velapoka_upload_s *u,
                            bool send_files)
{
  if (u == NULL)
    {
      return -ENODEV;
    }

  pthread_mutex_lock(&u->lock);
  if (u->busy || u->stop)
    {
      pthread_mutex_unlock(&u->lock);
      return -EBUSY;
    }

  u->busy = true;
  u->request = send_files ? 2 : 1;
  u->status.state = VELAPOKA_UPLOAD_CHECKING;
  u->status.error = OK;
  u->status.file[0] = '\0';
  strlcpy(u->status.phase, "Queued", sizeof(u->status.phase));
  u->status.sent_files = 0;
  u->status.skipped_files = 0;
  u->status.file_size = 0;
  u->status.file_sent = 0;
  u->status.bytes_sent = 0;
  u->status.elapsed_ms = 0;
  u->status.failed_files = 0;
  u->status.retries = 0;
  u->status.last_failed[0] = '\0';
  u->status.failure_log_error = false;
  u->status.results_withheld = false;
  u->started_ms = upload_now_ms();
  pthread_mutex_unlock(&u->lock);
  return OK;
}

void velapoka_upload_status(FAR struct velapoka_upload_s *u,
                            FAR struct velapoka_upload_status_s *status)
{
  memset(status, 0, sizeof(*status));
  if (u != NULL)
    {
      pthread_mutex_lock(&u->lock);
      *status = u->status;
      if (u->busy)
        {
          status->elapsed_ms = upload_now_ms() - u->started_ms;
        }

      pthread_mutex_unlock(&u->lock);
    }
  else
    {
      status->state = VELAPOKA_UPLOAD_ERROR;
      status->error = -ENODEV;
    }
}

void velapoka_upload_stop(FAR struct velapoka_upload_s *u)
{
  if (u != NULL)
    {
      pthread_mutex_lock(&u->lock);
      u->stop = true;
      pthread_mutex_unlock(&u->lock);
      pthread_join(u->thread, NULL);
      pthread_mutex_destroy(&u->lock);
      free(u);
    }
}
