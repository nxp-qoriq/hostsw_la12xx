/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)*/
/*
 * Copyright 2019-2022 NXP
 */

#ifndef __MPIC_GB_TIMESTAMP__
#define __MPIC_GB_TIMESTAMP__

#define FATAL do { fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", \
				__LINE__, __FILE__, errno, strerror(errno)); return -1; } while (0)

#define MAP_SIZE 4096UL
#define MAP_MASK (MAP_SIZE - 1)

#if defined(LA1238RDB) || defined(LA1238CPE)
#define CCSR_BASE_ADDR		0x9848000000
#else
#define CCSR_BASE_ADDR		0xa048000000
#endif
#define MPIC_BASE_ADDR          (CCSR_BASE_ADDR + 0x2040000)
#define MPIC_REG_GTCCRB1        0x2140
#define MPIC_REG_GTCCRB0        0x2100

#define MPIC_TIMER_CLOCK               76800000
#define MPIC_TIMER_COUNT_TO_US(count)  (count / (MPIC_TIMER_CLOCK / 1000000))

/*
 * @details  Open MPIC Global B Timer device node
 * @return
 *	- On success return 0
 *	- On failure return negative error number
 */
int open_mpic_gbtimer(void);

/*
 * @details  MPIC Global B Timer current count
 * @return
 *	- On success return 64 bit timer counter in usec
 *	- On failure return negative error number
 */
uint64_t get_mpic_current_count(void);

/*
 * @details  Close MPIC Global B Timer device node
 * @return
 *	- On success return 0
 *	- On failure return negative error number
 */
int close_mpic_gbtimer(void);

#endif /* __MPIC_GB_TIMESTAMP__ */
