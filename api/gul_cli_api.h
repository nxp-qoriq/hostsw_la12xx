/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2022-2023 NXP
 */

#ifndef __GUL_CLIAPP_API__
#define __GUL_CLIAPP_API__

#include "gul_cli_ioctl.h"

int libcli_register(struct cli *cli_t, int modem_id, int core_id);
int libcli_deregister(struct cli *cli_t);
int libcli_blockforEvent_read(int fd, struct cli *cli_t, int count);
int libcli_send_msi_sync(int fd, struct cli *cli_t);
int libcli_send_msi_async(int fd, struct cli *cli_t);
int libcli_send_msi_wait(int fd, struct cli *cli_t);
int libcli_dump_core_log(int fd, struct cli *cli_t);

#endif /* __GUL_CLIAPP_API__ */
