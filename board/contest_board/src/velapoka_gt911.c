/****************************************************************************
 * vendor/openvela/boards/contest2026_284_board/src/velapoka_gt911.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/i2c/i2c_master.h>
#include <nuttx/input/touchscreen.h>
#include <nuttx/wqueue.h>

#include <arch/board/board.h>

#include <arch/board/velapoka_bsp.h>

#define GT911_STATUS_REG       0x814e
#define GT911_POINT_REG        0x814f
#define GT911_PRODUCT_ID_REG   0x8140
#define GT911_MAX_POINTS       5
#define GT911_POINT_BYTES      8
#define GT911_BUFFER_SIZE      (1 + GT911_MAX_POINTS * GT911_POINT_BYTES)

struct gt911_dev_s
{
  struct touch_lowerhalf_s lower;
  struct i2c_master_s *i2c;
  struct work_s work;
  bool contact;
  int16_t last_x;
  int16_t last_y;
  uint8_t last_id;
  uint8_t buffer[GT911_BUFFER_SIZE];
};

static struct gt911_dev_s g_gt911;

static int gt911_read(FAR struct gt911_dev_s *dev, uint16_t reg,
                      FAR uint8_t *buffer, size_t buflen)
{
  uint8_t regbuf[2] = {reg >> 8, reg & 0xff};
  struct i2c_msg_s msgs[2] =
  {
    {
      .frequency = BOARD_VELAPOKA_TOUCH_FREQUENCY,
      .addr = BOARD_VELAPOKA_TOUCH_ADDR,
      .flags = 0,
      .buffer = regbuf,
      .length = sizeof(regbuf),
    },
    {
      .frequency = BOARD_VELAPOKA_TOUCH_FREQUENCY,
      .addr = BOARD_VELAPOKA_TOUCH_ADDR,
      .flags = I2C_M_READ,
      .buffer = buffer,
      .length = buflen,
    }
  };

  return I2C_TRANSFER(dev->i2c, msgs, 2);
}

static int gt911_write_u8(FAR struct gt911_dev_s *dev, uint16_t reg,
                          uint8_t value)
{
  uint8_t buffer[3] = {reg >> 8, reg & 0xff, value};
  struct i2c_msg_s msg =
  {
    .frequency = BOARD_VELAPOKA_TOUCH_FREQUENCY,
    .addr = BOARD_VELAPOKA_TOUCH_ADDR,
    .flags = 0,
    .buffer = buffer,
    .length = sizeof(buffer),
  };

  return I2C_TRANSFER(dev->i2c, &msg, 1);
}

static uint16_t gt911_get_le16(FAR const uint8_t *value)
{
  return (uint16_t)value[0] | ((uint16_t)value[1] << 8);
}

static void gt911_report(FAR struct gt911_dev_s *dev, bool down,
                         FAR const uint8_t *point_data)
{
  struct touch_sample_s sample;
  FAR struct touch_point_s *point = &sample.point[0];
  uint16_t raw_x;
  uint16_t raw_y;

  memset(&sample, 0, sizeof(sample));
  sample.npoints = 1;

  if (down)
    {
      raw_x = gt911_get_le16(point_data + 1);
      raw_y = gt911_get_le16(point_data + 3);
      dev->last_id = point_data[0];

      /* The 7-inch adapter is mounted in the orientation used by the
       * reference BSP, which mirrors both axes.
       */

      if (raw_x >= BOARD_VELAPOKA_LCD_WIDTH)
        {
          raw_x = BOARD_VELAPOKA_LCD_WIDTH - 1;
        }

      if (raw_y >= BOARD_VELAPOKA_LCD_HEIGHT)
        {
          raw_y = BOARD_VELAPOKA_LCD_HEIGHT - 1;
        }

      dev->last_x = BOARD_VELAPOKA_LCD_WIDTH - 1 - raw_x;
      dev->last_y = BOARD_VELAPOKA_LCD_HEIGHT - 1 - raw_y;
    }

  point->id = dev->last_id;
  point->x = dev->last_x;
  point->y = dev->last_y;
  point->pressure = down ? 1 : 0;
  point->flags = TOUCH_ID_VALID | TOUCH_POS_VALID |
                 TOUCH_PRESSURE_VALID;
  point->flags |= down ? (dev->contact ? TOUCH_MOVE : TOUCH_DOWN) : TOUCH_UP;
  dev->contact = down;
  touch_event(dev->lower.priv, &sample);
}

static void gt911_worker(FAR void *arg)
{
  FAR struct gt911_dev_s *dev = arg;
  uint8_t points;
  bool valid;
  int ret;

  ret = gt911_read(dev, GT911_STATUS_REG, dev->buffer, 1);
  if (ret < 0)
    {
      ierr("GT911 status read failed: %d\n", ret);
      goto queue_again;
    }

  valid = (dev->buffer[0] & 0x80) != 0;
  points = dev->buffer[0] & 0x0f;

  if (valid && points > 0 && points <= GT911_MAX_POINTS)
    {
      ret = gt911_read(dev, GT911_POINT_REG, &dev->buffer[1],
                       points * GT911_POINT_BYTES);
      if (ret >= 0)
        {
          gt911_report(dev, true, &dev->buffer[1]);
        }
    }
  else if (dev->contact)
    {
      gt911_report(dev, false, NULL);
    }

  if (valid)
    {
      ret = gt911_write_u8(dev, GT911_STATUS_REG, 0);
      if (ret < 0)
        {
          ierr("GT911 status clear failed: %d\n", ret);
        }
    }

queue_again:
  ret = work_queue(LPWORK, &dev->work, gt911_worker, dev,
                   CONFIG_VELAPOKA_TOUCHSCREEN_SAMPLE_DELAY);
  if (ret < 0)
    {
      ierr("GT911 work queue failed: %d\n", ret);
    }
}

int velapoka_touchscreen_initialize(void)
{
  FAR struct gt911_dev_s *dev = &g_gt911;
  uint8_t product_id[4];
  int ret;

  memset(dev, 0, sizeof(*dev));
  dev->i2c = velapoka_i2c_initialize();
  if (dev->i2c == NULL)
    {
      return -ENODEV;
    }

  ret = gt911_read(dev, GT911_PRODUCT_ID_REG, product_id,
                   sizeof(product_id));
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: GT911 not found at I2C address 0x%02x\n",
             BOARD_VELAPOKA_TOUCH_ADDR);
      return ret;
    }

  ret = touch_register(&dev->lower, CONFIG_VELAPOKA_TOUCHSCREEN_PATH,
                       CONFIG_VELAPOKA_TOUCHSCREEN_SAMPLE_CACHES);
  if (ret < 0)
    {
      return ret;
    }

  ret = work_queue(LPWORK, &dev->work, gt911_worker, dev,
                   CONFIG_VELAPOKA_TOUCHSCREEN_SAMPLE_DELAY);
  if (ret < 0)
    {
      return ret;
    }

  velapoka_bsp_mark_ready(VELAPOKA_CAP_TOUCH);
  syslog(LOG_INFO, "GT911: product %.4s registered at %s\n",
         product_id, CONFIG_VELAPOKA_TOUCHSCREEN_PATH);
  return OK;
}
