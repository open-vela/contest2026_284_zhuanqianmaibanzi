/* SPDX-License-Identifier: Apache-2.0
 * Render the real product UI using the workspace LVGL software renderer.
 */
#include <nuttx/config.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lvgl/lvgl.h>
#include "velapoka_ui.h"

char *vkp_test_mountpoint;
unsigned short vkp_test_port = 8899;
static uint16_t pixels[1024 * 600];

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

const char *velapoka_state_name(enum velapoka_state_e state)
{
  (void)state;
  return "READY";
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
  uint16_t *source = (uint16_t *)data;
  for (int y = area->y1; y <= area->y2; y++)
    for (int x = area->x1; x <= area->x2; x++)
      pixels[y * 1024 + x] = *source++;
  lv_display_flush_ready(display);
}

static lv_obj_t *find_label(lv_obj_t *obj, const char *text)
{
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++)
    {
      lv_obj_t *child = lv_obj_get_child(obj, i);
      if (lv_obj_check_type(child, &lv_label_class) &&
          strcmp(lv_label_get_text(child), text) == 0)
        {
          return child;
        }
      lv_obj_t *found = find_label(child, text);
      if (found != NULL) return found;
    }
  return NULL;
}

static void click(const char *text, enum velapoka_ui_action_e expected)
{
  lv_obj_t *label = find_label(lv_screen_active(), text);
  assert(label != NULL);
  lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, NULL);
  assert(velapoka_ui_take_action() == expected);
}

static lv_obj_t *find_image(lv_obj_t *obj)
{
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++)
    {
      lv_obj_t *child = lv_obj_get_child(obj, i);
      if (lv_obj_check_type(child, &lv_image_class)) return child;
      lv_obj_t *found = find_image(child);
      if (found != NULL) return found;
    }
  return NULL;
}

static unsigned int check_boxes(lv_obj_t *obj)
{
  unsigned int count = 0;
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++)
    {
      lv_obj_t *child = lv_obj_get_child(obj, i);
      if (!lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN) &&
          lv_obj_get_style_border_width(child, 0) == 3)
        {
          lv_area_t box;
          lv_area_t roi;
          lv_obj_get_coords(child, &box);
          lv_obj_get_coords(obj, &roi);
          assert(box.x1 >= roi.x1 && box.y1 >= roi.y1);
          assert(box.x2 <= roi.x2 && box.y2 <= roi.y2);
          assert(box.x2 >= box.x1 && box.y2 >= box.y1);
          count++;
        }
      count += check_boxes(child);
    }
  return count;
}

static void check_result_frames(lv_display_t *display)
{
  static uint8_t gray[512 * 288];
  struct velapoka_result_s result = {0};
  lv_obj_t *image = find_image(lv_screen_active());
  assert(image != NULL);
  const lv_image_dsc_t *source = lv_image_get_src(image);
  memset(gray, 30, sizeof(gray));
  assert(velapoka_ui_update_preview(gray, sizeof(gray), 1) == 0);
  uint16_t before = ((const uint16_t *)source->data)[0];
  velapoka_ui_set_preview_frozen(true);
  memset(gray, 220, sizeof(gray));
  assert(velapoka_ui_update_preview(gray, sizeof(gray), 2) == 0);
  assert(((const uint16_t *)source->data)[0] == before);

  result.box_count = 1;
  result.boxes[0] = (struct velapoka_box_s){256, 128, 64, 64};
  velapoka_ui_set_result(&result);
  lv_obj_update_layout(lv_screen_active());
  assert(check_boxes(lv_screen_active()) == 1);
  result.pass = true;
  velapoka_ui_set_result(&result);
  assert(check_boxes(lv_screen_active()) == 0);
  result.pass = false;
  result.boxes[0] = (struct velapoka_box_s){0, 0, 16, 16};
  velapoka_ui_set_result(&result);
  assert(check_boxes(lv_screen_active()) == 0);
  result.boxes[0] = (struct velapoka_box_s){60, 60, 0, 16};
  velapoka_ui_set_result(&result);
  assert(check_boxes(lv_screen_active()) == 0);
  result.box_count = 255; /* Corrupt count must not index beyond boxes[8]. */
  for (unsigned int i = 0; i < VELAPOKA_INSPECT_MAX_BOXES; i++)
    result.boxes[i] = (struct velapoka_box_s){48, 32, 224, 112};
  velapoka_ui_set_result(&result);
  lv_obj_update_layout(lv_screen_active());
  assert(check_boxes(lv_screen_active()) == VELAPOKA_INSPECT_MAX_BOXES);
  lv_tick_inc(1999);
  lv_timer_handler();
  assert(find_label(lv_screen_active(), "CAPTURED") != NULL);
  lv_tick_inc(1);
  lv_timer_handler();
  assert(find_label(lv_screen_active(), "LIVE") != NULL);
  assert(check_boxes(lv_screen_active()) == 0);
  assert(find_label(lv_screen_active(), "FAIL") != NULL);
  puts("PASS: result resumes live preview after 2 seconds, keeps metrics");
  velapoka_ui_set_result(&result);
  velapoka_ui_set_preview_frozen(true); /* Finish / safe shutdown cancels. */
  lv_tick_inc(2001);
  lv_timer_handler();
  assert(find_label(lv_screen_active(), "CAPTURED") != NULL);
  velapoka_ui_clear_boxes();

  velapoka_ui_set_preview_frozen(false);
  assert(velapoka_ui_update_preview(gray, sizeof(gray), 3) == 0);
  assert(((const uint16_t *)source->data)[0] != before);
  before = ((const uint16_t *)source->data)[0];
  click("Finish", VELAPOKA_UI_ACTION_FINISH);
  click("Data / Upload", VELAPOKA_UI_ACTION_TEST_PC);
  memset(gray, 10, sizeof(gray));
  assert(velapoka_ui_update_preview(gray, sizeof(gray), 4) == 0);
  assert(((const uint16_t *)source->data)[0] == before);
  click("Back", VELAPOKA_UI_ACTION_NONE);
  assert(velapoka_ui_update_preview(gray, sizeof(gray), 5) == 0);
  assert(((const uint16_t *)source->data)[0] != before);
  lv_refr_now(display);
  puts("PASS: frozen result frame, PASS suppression, ROI clipping, data redraw");
}

int main(int argc, char **argv)
{
  struct velapoka_upload_status_s status = {0};
  assert(argc == 2);
  lv_init();
  lv_display_t *display = lv_display_create(1024, 600);
  void *buffer = malloc(1024 * 64 * 2);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, buffer, NULL, 1024 * 64 * 2,
                          LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush);
  assert(velapoka_ui_create(true, true, true) == 0);
  check_result_frames(display);
  click("Data / Upload", VELAPOKA_UI_ACTION_TEST_PC);
  status.link_known = status.link_up = status.storage_online = true;
  status.receiver_online = true;
  strcpy(status.ip, "10.0.0.2");
  strcpy(status.netmask, "255.255.255.0");
  strcpy(status.mac, "00:11:22:33:44:55");
  status.storage.total_bytes = UINT64_C(15931539456);
  status.storage.free_bytes = UINT64_C(15000000000);
  status.storage.available_bytes = UINT64_C(14900000000);
  status.storage.records = 123;
  status.storage.fail_records = 24;
  status.state = VELAPOKA_UPLOAD_DONE;
  strcpy(status.phase, "Complete");
  status.sent_files = 25;
  status.skipped_files = 12;
  strcpy(status.file, "results.jsonl");
  status.file_size = status.file_sent = 123456;
  velapoka_ui_update_data(&status, true);
  lv_refr_now(display);
  FILE *image = fopen(argv[1], "wb");
  assert(image != NULL);
  fprintf(image, "P6\n1024 600\n255\n");
  for (unsigned int i = 0; i < 1024 * 600; i++)
    {
      unsigned int p = pixels[i];
      fputc(((p >> 11) & 31) * 255 / 31, image);
      fputc(((p >> 5) & 63) * 255 / 63, image);
      fputc((p & 31) * 255 / 31, image);
    }
  fclose(image);

  status.state = VELAPOKA_UPLOAD_ERROR;
  status.error = -107;
  strcpy(status.phase, "Checking file CRC");
  strcpy(status.file, "fail/fail_00000001.bmp");
  status.file_sent = 0;
  velapoka_ui_update_data(&status, true);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t *progress = find_label(lv_screen_active(),
    "UPLOAD / CONNECTION FAILED\nStage: Checking file CRC   0 s   0 KiB/s\n"
    "fail/fail_00000001.bmp 0%   Saved: 25   Skipped: 12   Failed: 0   Retry: 0\n"
    "Last failed:    Error: -107\nSee PC log and stage; tap Retry.");
  assert(progress != NULL);
  lv_area_t progress_area;
  lv_area_t button_area;
  lv_obj_get_coords(progress, &progress_area);
  lv_obj_get_coords(lv_obj_get_parent(find_label(lv_screen_active(),
                                               "Safe exit")), &button_area);
  assert(progress_area.y2 < button_area.y1);
  puts("PASS: upload error stage is visible without overlapping buttons");
  status.state = VELAPOKA_UPLOAD_PARTIAL;
  status.error = -74;
  status.results_withheld = true;
  status.failed_files = 1;
  status.retries = 2;
  status.sent_files = 1;
  status.skipped_files = 0;
  status.elapsed_ms = 12000;
  status.bytes_sent = 58678;
  status.file_size = status.file_sent = 58678;
  strcpy(status.phase, "Incomplete: see failure log");
  strcpy(status.file, "fail/fail_00000002.bmp");
  strcpy(status.last_failed, "fail/fail_00000001.bmp");
  velapoka_ui_update_data(&status, true);
  lv_obj_update_layout(lv_screen_active());
  progress = find_label(lv_screen_active(),
    "INCOMPLETE - not all files confirmed\n"
    "Stage: Incomplete: see failure log   12 s   4 KiB/s\n"
    "fail/fail_00000002.bmp 100%   Saved: 1   Skipped: 0   Failed: 1   Retry: 2\n"
    "Last failed: fail/fail_00000001.bmp   Error: -74\n"
    "New results.jsonl withheld; see upload-errors.jsonl.");
  assert(progress != NULL);
  lv_obj_get_coords(progress, &progress_area);
  assert(progress_area.y2 < button_area.y1);
  click("Upload / Retry", VELAPOKA_UI_ACTION_UPLOAD);
  puts("PASS: incomplete count, retry, speed, manifest warning and retry button");
  click("Safe exit", VELAPOKA_UI_ACTION_EXIT);
  velapoka_ui_begin_shutdown();
  assert(velapoka_ui_take_action() == VELAPOKA_UI_ACTION_NONE);
  velapoka_ui_shutdown_complete(false);
  assert(find_label(lv_screen_active(), "SAFE EXIT COMPLETE") != NULL);
  assert(find_label(lv_screen_active(), "Inspect") == NULL);
  click("Open data", VELAPOKA_UI_ACTION_REOPEN_DATA);
  click("Restart", VELAPOKA_UI_ACTION_RESTART);
  lv_refr_now(display);
  lv_deinit();
  velapoka_ui_release();

  /* Repeated application reinitialization must not retain deleted widgets. */
  for (int i = 0; i < 3; i++)
    {
      lv_init();
      display = lv_display_create(1024, 600);
      lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
      lv_display_set_buffers(display, buffer, NULL, 1024 * 64 * 2,
                            LV_DISPLAY_RENDER_MODE_PARTIAL);
      lv_display_set_flush_cb(display, flush);
      assert(velapoka_ui_create(true, true, true) == 0);
      click("Data / Upload", VELAPOKA_UI_ACTION_TEST_PC);
      velapoka_ui_shutdown_complete(true);
      assert(find_label(lv_screen_active(), "SHUTDOWN ERROR") != NULL);
      click("Open data", VELAPOKA_UI_ACTION_REOPEN_DATA);
      lv_deinit();
      velapoka_ui_release();
    }
  free(buffer);
  puts("PASS: shutdown page, restart/data actions, three UI reinitializations");
  printf("UI rendered using LVGL %d.%d.%d\n", LVGL_VERSION_MAJOR,
         LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
  return 0;
}
