/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#define FAR
#define OK 0
#define CONFIG_FAT_LFN 1
#define CONFIG_FAT_MAXFNAME 32
#define MSDOS_SUPER_MAGIC 0x4d44
#define CONFIG_EXAMPLES_VELAPOKA_STORAGE_PRIORITY 90
#define CONFIG_EXAMPLES_VELAPOKA_STORAGE_STACKSIZE 65536
#define CONFIG_EXAMPLES_VELAPOKA_STORAGE_DEVPATH "/dev/test-sd"
#define CONFIG_EXAMPLES_VELAPOKA_CAMERA_DEVPATH "/dev/video0"
#define CONFIG_EXAMPLES_VELAPOKA_FB_DEVPATH "/dev/fb0"
#define CONFIG_EXAMPLES_VELAPOKA_INPUT_DEVPATH "/dev/input0"
extern char *vkp_test_mountpoint;
extern unsigned short vkp_test_port;
#define CONFIG_EXAMPLES_VELAPOKA_STORAGE_MOUNTPOINT vkp_test_mountpoint
#define CONFIG_EXAMPLES_VELAPOKA_UPLOAD_HOST "127.0.0.1"
#define CONFIG_EXAMPLES_VELAPOKA_UPLOAD_PORT vkp_test_port
size_t strlcpy(char *dest, const char *src, size_t size);
