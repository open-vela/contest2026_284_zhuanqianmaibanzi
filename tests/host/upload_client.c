/* SPDX-License-Identifier: Apache-2.0
 * Real storage/upload implementation; only board/FAT/ethernet are shimmed.
 */
#include <nuttx/config.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statfs.h>
#include <unistd.h>
#include <netutils/netlib.h>
#include "velapoka_storage.h"
#include "velapoka_upload.h"

char *vkp_test_mountpoint;
unsigned short vkp_test_port;
extern unsigned long vkp_test_sd_reads;

size_t strlcpy(char *dest, const char *src, size_t size)
{
  size_t length = strlen(src);
  if (size > 0)
    {
      size_t copy = length < size - 1 ? length : size - 1;
      memcpy(dest, src, copy);
      dest[copy] = 0;
    }
  return length;
}

int netlib_getifstatus(const char *name, uint8_t *flags)
{
  (void)name;
  *flags = IFF_RUNNING | IFF_UP;
  return 0;
}

int netlib_get_ipv4addr(const char *name, struct in_addr *address)
{
  (void)name;
  return inet_pton(AF_INET, "127.0.0.1", address) == 1 ? 0 : -1;
}

int netlib_get_ipv4netmask(const char *name, struct in_addr *address)
{
  (void)name;
  return inet_pton(AF_INET, "255.0.0.0", address) == 1 ? 0 : -1;
}

int netlib_getmacaddr(const char *name, uint8_t *mac)
{
  const uint8_t id[6] = {0xde, 0xad, 0xbe, 0xef, 0, 1};
  (void)name;
  memcpy(mac, id, 6);
  return 0;
}

int __real_statfs(const char *path, struct statfs *fs);
int __wrap_statfs(const char *path, struct statfs *fs)
{
  int ret = __real_statfs(path, fs);
  if (ret == 0) fs->f_type = MSDOS_SUPER_MAGIC;
  return ret;
}

static void wait_saved(struct velapoka_storage_s *storage)
{
  struct velapoka_storage_usage_s usage;
  for (int i = 0; i < 500; i++)
    {
      assert(velapoka_storage_get_usage(storage, &usage) == 0);
      if (usage.queued == 0) return;
      usleep(10000);
    }
  assert(!"Storage did not drain");
}

static void seed(struct velapoka_storage_s *storage)
{
  struct velapoka_result_s result = {0};
  struct velapoka_storage_usage_s usage;
  uint8_t gray[VELAPOKA_INSPECT_SIZE];
  uint32_t id;
  struct velapoka_export_s *snapshot;
  struct velapoka_export_file_s file;
  unsigned int files = 0;
  char prefix[128];
  memset(gray, 123, sizeof(gray));
  result.pass = false;
  result.sequence = 1;
  assert(velapoka_storage_save_result(storage, &result, gray,
                                      sizeof(gray), 18, &id) == 0);
  assert(id == 1);
  wait_saved(storage);
  assert(velapoka_storage_get_usage(storage, &usage) == 0);
  assert(usage.records == 1 && usage.fail_records == 1);
  assert(usage.total_bytes >= usage.free_bytes);
  assert(usage.free_bytes >= usage.available_bytes);
  assert(velapoka_storage_export_begin(storage, &snapshot) == 0);
  result.pass = true;
  result.sequence = 2;
  assert(velapoka_storage_save_result(storage, &result, NULL, 0, 18,
                                      &id) == 0);
  wait_saved(storage);
  assert(velapoka_storage_get_usage(storage, &usage) == 0);
  assert(usage.records == 2 && usage.fail_records == 1);
  while (velapoka_storage_export_next(snapshot, &file) == 1)
    {
      if (strcmp(file.name, "results.jsonl") == 0)
        {
          ssize_t n = velapoka_storage_export_read(snapshot, &file, 0,
                                                  prefix, sizeof(prefix));
          assert(n > 0);
          assert(strncmp(prefix, "{\"id\":1,", 8) == 0);
          char path[512];
          snprintf(path, sizeof(path), "%s/velapoka/results.jsonl", vkp_test_mountpoint);
          FILE *all = fopen(path, "r");
          assert(all != NULL);
          assert(fseek(all, 0, SEEK_END) == 0);
          assert((uint64_t)ftell(all) > file.size);
          fclose(all);
        }
      close(file.fd);
      files++;
    }
  assert(files == 2);
  velapoka_storage_export_end(snapshot);
}

int main(int argc, char **argv)
{
  struct velapoka_storage_s *storage;
  struct velapoka_upload_s *upload;
  struct velapoka_upload_status_s status;
  assert(argc == 5);
  vkp_test_mountpoint = argv[1];
  vkp_test_port = atoi(argv[2]);
  assert(velapoka_storage_start(&storage) == 0);
  if (strcmp(argv[3], "seed") == 0) seed(storage);
  assert(velapoka_upload_start(&upload, storage) == 0);
  assert(velapoka_upload_request(upload, true) == 0);
  assert(velapoka_upload_request(upload, true) == -EBUSY);
  if (strcmp(argv[4], "cancel") == 0)
    {
      usleep(200000);
      velapoka_upload_stop(upload);
      assert(velapoka_storage_stop(storage) == 0);
      puts("cancelled");
      return 0;
    }
  for (int i = 0; i < 3000; i++)
    {
      velapoka_upload_status(upload, &status);
      if (status.state == VELAPOKA_UPLOAD_DONE ||
          status.state == VELAPOKA_UPLOAD_ERROR ||
          status.state == VELAPOKA_UPLOAD_PARTIAL) break;
      usleep(10000);
    }
  printf("state=%d error=%d saved=%u skipped=%u failed=%u retries=%u "
         "withheld=%d log_error=%d last=%s bytes=%llu elapsed=%llu reads=%lu\n",
         status.state, status.error, status.sent_files, status.skipped_files,
         status.failed_files, status.retries, status.results_withheld,
         status.failure_log_error, status.last_failed,
         (unsigned long long)status.bytes_sent,
         (unsigned long long)status.elapsed_ms, vkp_test_sd_reads);
  if (strcmp(argv[4], "ok") == 0)
    {
      assert(status.state == VELAPOKA_UPLOAD_DONE);
      assert(status.error == 0);
    }
  else if (strcmp(argv[4], "partial") == 0)
    {
      assert(status.state == VELAPOKA_UPLOAD_PARTIAL);
      assert(status.error < 0);
    }
  else assert(status.state == VELAPOKA_UPLOAD_ERROR);
  velapoka_upload_stop(upload);
  assert(velapoka_storage_stop(storage) == 0);
  return 0;
}
