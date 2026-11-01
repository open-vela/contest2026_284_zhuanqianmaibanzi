/****************************************************************************
 * apps/app/velapoka/velapoka_storage.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_VELAPOKA_VELAPOKA_STORAGE_H
#define __APPS_EXAMPLES_VELAPOKA_VELAPOKA_STORAGE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "velapoka_model.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VELAPOKA_HISTORY_COUNT 3

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct velapoka_storage_s;
struct velapoka_export_s;

struct velapoka_storage_usage_s
{
  uint64_t total_bytes;
  uint64_t free_bytes;
  uint64_t available_bytes;
  uint32_t records;
  uint32_t fail_records;
  unsigned int queued;
};

struct velapoka_export_file_s
{
  char name[48];
  uint64_t size;
  int fd;
};

struct velapoka_history_s
{
  uint32_t id;
  uint16_t score_tenths;
  bool pass;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int velapoka_storage_start(FAR struct velapoka_storage_s **storage);
int velapoka_storage_stop(FAR struct velapoka_storage_s *storage);
bool velapoka_storage_online(FAR struct velapoka_storage_s *storage);
int velapoka_storage_last_error(FAR struct velapoka_storage_s *storage);
int velapoka_storage_load_reference(
  FAR struct velapoka_storage_s *storage, FAR uint8_t *reference,
  size_t size, FAR unsigned int *threshold_percent);
int velapoka_storage_save_reference(
  FAR struct velapoka_storage_s *storage, FAR const uint8_t *reference,
  size_t size, unsigned int threshold_percent);
int velapoka_storage_save_result(
  FAR struct velapoka_storage_s *storage,
  FAR const struct velapoka_result_s *result,
  FAR const uint8_t *gray, size_t size, unsigned int threshold_percent,
  FAR uint32_t *record_id);
int velapoka_storage_read_export(
  FAR struct velapoka_storage_s *storage, FAR const char *relative_path,
  FAR uint8_t **data, FAR size_t *size);
unsigned int velapoka_storage_get_history(
  FAR struct velapoka_storage_s *storage,
  FAR struct velapoka_history_s *history, unsigned int capacity);
int velapoka_storage_get_usage(FAR struct velapoka_storage_s *storage,
                               FAR struct velapoka_storage_usage_s *usage);
int velapoka_storage_export_begin(FAR struct velapoka_storage_s *storage,
                                  FAR struct velapoka_export_s **session);
/* next: 1 = opened file (caller closes fd), 0 = end, -EAGAIN = scan yielded.
 * The caller must end the session before stopping/unmounting storage.
 */

int velapoka_storage_export_next(FAR struct velapoka_export_s *session,
                                 FAR struct velapoka_export_file_s *file);
int velapoka_storage_export_reopen(FAR struct velapoka_export_s *session,
                                   FAR struct velapoka_export_file_s *file);
int velapoka_storage_upload_failure(FAR struct velapoka_storage_s *storage,
                                    FAR const char *name, int error);
ssize_t velapoka_storage_export_read(
  FAR struct velapoka_export_s *session,
  FAR struct velapoka_export_file_s *file, uint64_t offset,
  FAR void *buffer, size_t size);
void velapoka_storage_export_end(FAR struct velapoka_export_s *session);

#endif /* __APPS_EXAMPLES_VELAPOKA_VELAPOKA_STORAGE_H */
