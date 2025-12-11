/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2020-2022 NXP
 *
 * @ gul_ipc_kern_h
 */

#ifndef _GUL_IPC_KERN_H_
#define _GUL_IPC_KERN_H_

#include <gul_ipc_ioctl.h>

typedef void *ipc_handle_t;

/*****************************************************************************
 * @gul_ipc_get_handle
 *
 * Provides IPC handle of Rattler device, required by Host to
 * exchange (send / receive) control message(s) to / from that
 * Rattler device.
 *
 * dev_name - [IN][M]  Name of the Rattler device visible to Host
 *                     For example: wlan_mon0
 *
 * Return Value -
 *	ipc_handle_t - Valid IPC handle, on success
 *	NULL value - on error.
 *
 * Note: Multiple kernel tasks can call this API for same device,
 *       for example "wlan_mon0" and IPC will return same handle
 *       to all kernel tasks / threads.
 *
 *       If more than one task / thread accesses same IPC channel then
 *       then they(task / thread) needs to make sure the synchronized
 *       access to the IPC channel.
 *
 *       IPC do not provide any synchronized access to channel
 *       among the Tasks / threads.
 *
 *****************************************************************************/
ipc_handle_t gul_ipc_get_handle(const char *dev_name);

/*****************************************************************************
 * @gul_ipc_create_hugepage_outbound
 *
 * Create huge page outbound window to pass data from modem to host
 *
 * gd   - [IN][M]  GUL device pointer
 * sys_map_user - [IN][M]

 * Return Value - void
 ****************************************************************************/

void ls_pcie_iatu_outbound_set(void __iomem *dbi, int idx, int type,
			       u64 cpu_addr, u64 pci_addr, u32 size);


#endif /* _GUL_IPC_KERN_H_ */
