/*
 * Copyright 2016 International Business Machines
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _CAPI_FLASH_H_
#define _CAPI_FLASH_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <termios.h>
#include <endian.h>
#include <time.h>
#include <assert.h>

#define MAX_STRING_SIZE 1024
#define CXL_SYSFS_PATH "/sys/class/cxl/card"
#define CXL_CONFIG "/device/config"

#define IBM_PCIID           0x1014
#define CAPI_PCIID          0x0477
#define CAPI_LEGACY0        0x04cf
#define CAPI_LEGACY1        0x0601

#define PCI_ID              0x0
#define PCI_DEVICEID(X)     (((X) >> 16) & 0xFFFF)
#define PCI_VENDORID(X)     ((X) & 0xFFFF)
#define PCI_ECAP            0x100
#define ECAP_ID(X)          ((X) & 0xFFFF)
#define ECAP_NEXT(X)        (((X) >> 20) & 0xFFF)
#define ECAP_VSEC           0x000B
#define CAPI_VSECID         0x1280
#define VSEC_ID(X)          ((X) & 0xFFFF)
#define VSEC_REV(X)         (((X) >> 16) & 0xF)
#define VSEC_LENGTH(X)      (((X) >> 20) & 0xFFF)
#define FLASH_ADDR_OFFSET   0x50
#define FLASH_SIZE_OFFSET   0x54
#define FLASH_CNTL_OFFSET   0x58
#define FLASH_READY         (1 << 31)
#define FLASH_OP_DONE       (1 << 30)
#define FLASH_READ_REQ      (1 << 27)
#define FLASH_PROG_REQ      (1 << 26)
#define FLASH_ERASE_STATUS  (1 << 15)
#define FLASH_PROG_STATUS   (1 << 14)
#define FLASH_READ_STATUS   (1 << 13)
#define FLASH_PORT_READY    (1 << 12)
#define FLASH_DATA_OFFSET   0x5C
#define FLASH_CHECK_BIT(X,Y,Z)  (((X) & (Y)) == (Z)) 
// Flash error codes
#define FLASH_READY_TIMEOUT 1
#define FLASH_ERASE_TIMEOUT 2
#define FLASH_PROG_TIMEOUT  4
#define FLASH_PORT_TIMEOUT  99
#define FLASH_ERR_CFG_WRITE 100
#define FLASH_ERR_CFG_READ  200

#define FLASH_READ_SIZE     0x200               /* 512 Words */

#endif
