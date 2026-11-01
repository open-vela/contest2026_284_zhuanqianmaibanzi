/****************************************************************************
 * apps/app/velapoka/velapoka_upload.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_VELAPOKA_VELAPOKA_UPLOAD_H
#define __APPS_EXAMPLES_VELAPOKA_VELAPOKA_UPLOAD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stdint.h>

#include "velapoka_storage.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum velapoka_upload_state_e
{
  VELAPOKA_UPLOAD_IDLE = 0,
  VELAPOKA_UPLOAD_CHECKING,
  VELAPOKA_UPLOAD_SENDING,
  VELAPOKA_UPLOAD_DONE,
  VELAPOKA_UPLOAD_ERROR,
  VELAPOKA_UPLOAD_PARTIAL
};

struct velapoka_upload_status_s
{
  struct velapoka_storage_usage_s storage;
  enum velapoka_upload_state_e state;
  char ip[16];
  char netmask[16];
  char mac[18];
  char file[48];
  char phase[32];
  char last_failed[48];
  uint64_t file_size;
  uint64_t file_sent;
  uint64_t bytes_sent;
  uint64_t elapsed_ms;
  unsigned int sent_files;
  unsigned int skipped_files;
  unsigned int failed_files;
  unsigned int retries;
  int error;
  bool failure_log_error;
  bool results_withheld;
  bool link_known;
  bool link_up;
  bool storage_online;
  bool receiver_online;
};

struct velapoka_upload_s;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int velapoka_upload_start(FAR struct velapoka_upload_s **upload,
                          FAR struct velapoka_storage_s *storage);
int velapoka_upload_request(FAR struct velapoka_upload_s *upload,
                            bool send_files);
void velapoka_upload_status(FAR struct velapoka_upload_s *upload,
                            FAR struct velapoka_upload_status_s *status);
void velapoka_upload_stop(FAR struct velapoka_upload_s *upload);

#endif
