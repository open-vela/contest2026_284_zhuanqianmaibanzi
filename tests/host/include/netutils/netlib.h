/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <arpa/inet.h>
#include <net/if.h>
#include <stdint.h>
#ifndef IFF_IS_RUNNING
#define IFF_IS_RUNNING(flags) (((flags) & IFF_RUNNING) != 0)
#endif
int netlib_getifstatus(const char *name, uint8_t *flags);
int netlib_get_ipv4addr(const char *name, struct in_addr *address);
int netlib_get_ipv4netmask(const char *name, struct in_addr *address);
int netlib_getmacaddr(const char *name, uint8_t *mac);
