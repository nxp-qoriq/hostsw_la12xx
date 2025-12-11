/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
 * Copyright 2020-2023 NXP
 */

#ifndef __GUL_TVD_IOCTL__
#define __GUL_TVD_IOCTL__

#include <linux/ioctl.h>

#define GUL_TVD_MAGIC   'S'

/**
 * TVD IOCTLs
 *
 */
#define IOCTL_GUL_TVD_GET_THERMAL_EVENT		_IOWR(GUL_TVD_MAGIC, 1, struct tvd *)
#define IOCTL_GUL_TVD_MTD_GET_TEMP		_IOWR(GUL_TVD_MAGIC, 2, struct tvd *)
#define IOCTL_GUL_TVD_CTD_GET_TEMP		_IOWR(GUL_TVD_MAGIC, 3, struct tvd *)
#define IOCTL_GUL_TVD_PROGRAM_CTD_THRESHOLD	_IOWR(GUL_TVD_MAGIC, 4, struct tvd *)
#define IOCTL_GUL_TVD_PROGRAM_MTD_THRESHOLD	_IOWR(GUL_TVD_MAGIC, 5, struct tvd *)
#define IOCTL_GUL_TVD_PROGRAM_CTD_HYSTERESIS	_IOWR(GUL_TVD_MAGIC, 6, struct tvd *)
#define IOCTL_GUL_TVD_PROGRAM_MTD_HYSTERESIS	_IOWR(GUL_TVD_MAGIC, 7, struct tvd *)
#define IOCTL_GUL_TVD_REGISTER_EVEFD		_IOWR(GUL_TVD_MAGIC, 8, struct tvd *)
#define IOCTL_GUL_TVD_DEREGISTER_EVEFD		_IOWR(GUL_TVD_MAGIC, 9, struct tvd *)
#define IOCTL_GUL_TVD_RTD_GET_TEMP		_IOWR(GUL_TVD_MAGIC, 10, struct tvd *)
#define IOCTL_GUL_TVD_PROGRAM_RTD_THRESHOLD	_IOWR(GUL_TVD_MAGIC, 11, struct tvd *)
#define IOCTL_GUL_TVD_MTD_GET_POWER_INFO _IOWR(GUL_TVD_MAGIC, 12, struct tvd *)
/*
 * Default Minimum CTD, RTD and MTD threshold to be 1
 *
 */
#define DEFAULT_MIN_MTD_THRESHOLD		1
#define DEFAULT_MIN_RTD_THRESHOLD		1
#define DEFAULT_MIN_CTD_THRESHOLD       1

/*
 * Default Maximum CTD, RTD and MTD threshold to be 2
 *
 */
#define DEFAULT_MAX_MTD_THRESHOLD		2
#define DEFAULT_MAX_RTD_THRESHOLD		2
#define DEFAULT_MAX_CTD_THRESHOLD		2

/*
 * DEFAULT MTD threshold configure values
 *
 */
#define DEFAULT_MTD_THRESHOLD_1			60
#define DEFAULT_MTD_THRESHOLD_2			80

/*
 * DEFAULT RTD threshold configure values
 *
 */
#define DEFAULT_RTD_THRESHOLD_1			60
#define DEFAULT_RTD_THRESHOLD_2			80

/*
 * DEFAULT CTD threshold configure values
 *
 */
#define DEFAULT_CTD_THRESHOLD_1                 60
#define DEFAULT_CTD_THRESHOLD_2                 80

/*
 * Maximum CTD and MTD threshold to be 2
 *
 */
#define	MAX_CTD_THRESHOLD			2
#define	MAX_MTD_THRESHOLD			2
#define	MAX_RTD_THRESHOLD			2

/*
 * Minimum CTD and MTD threshold to be 1
 *
 */
#define MIN_CTD_THRESHOLD                       1
#define MIN_MTD_THRESHOLD                       1
#define MIN_RTD_THRESHOLD                       1

/**
 * Default hysteresis values for MTD and CTD
 * Hysteresis value added to Threshold value
 * So for Temp rising if temp crosses (Threshold + Hystereis)
 * then will get thermal event.
 * For Temp falling, if temp comes below (Threshold - Hystereis),
 * then will get thermal event.
 */
#define DEFAULT_MTD_HYSTERESIS			2
#define DEFAULT_CTD_HYSTERESIS			2

/**
 * Structure for MTD threshold
 *  threshold_count	No.of MTD thresholds
 *  threshold[MAX_MTD_THRESHOLD]
 *	Array of MAX_MTD_THRESHOLD thresholds, MAX_MTD_THRESHOLD be 2.
 */
struct mtd_tvd_thermal_threshold {
	int threshold_count;
	int threshold[MAX_MTD_THRESHOLD];
};

/**
 * Structure for RTD threshold
 *  threshold_count	No.of RTD thresholds
 *  threshold[MAX_RTD_THRESHOLD]
 *	Array of MAX_RTD_THRESHOLD thresholds, MAX_RTD_THRESHOLD be 2.
 */
struct rtd_tvd_thermal_threshold {
	int threshold_count;
	int threshold[MAX_RTD_THRESHOLD];
};

/**
 * Structure for CTD threshold
 *  threshold_count	No.of CTD thresholds
 *  threshold[MAX_CTD_THRESHOLD]
 *	Array of MAX_CTD_THRESHOLD thresholds, MAX_CTD_THRESHOLD be 2
 */
struct ctd_tvd_thermal_threshold {
	int threshold_count;
	int threshold[MAX_CTD_THRESHOLD];
};

/**
 * Enum for type of Threshold
 */
enum thermal_threshold_type {
	CTD_THERMAL_THRESHOLD = 1,	/**< enum for CTD Threshold */
	MTD_TERMAL_THRESHOLD,	/**< enum for MTD Threshold */
	RTD_TERMAL_THRESHOLD,	/**< enum for RTD Threshold */
	MAX_THRESHOLD_EVENT		/**< Max Threshold */
};

/**
 * Structure for Threshold
 */
struct thermal_threshold {
	enum thermal_threshold_type threshold_type;	/**< Type of threshold(MTD, RTD or CTD) */
	struct ctd_tvd_thermal_threshold ctd_threshold;	/**< ctd thrshold */
	struct mtd_tvd_thermal_threshold mtd_threshold;	/**< mtd thrshold */
	struct rtd_tvd_thermal_threshold rtd_threshold;	/**< rtd thrshold */
};

/**
 * Enum for type of Hysteresis
 */
enum thermal_hysteresis_type {
	CTD_THERMAL_HYSTERESIS = 1,	/**< enum for CTD Hysteresis */
	MTD_THERMAL_HYSTERESIS,	/**< enum for MTD Hysteresis */
	MAX_HYSTERSIS			/**< Max Hysteresis */
};

/**
 * Structure for Hysteresis
 */
struct thermal_hysteresis {
	enum thermal_hysteresis_type hysteresis_type;	/**< Type of hysteresis(MTD or CTD) */
	int  mtd_hysteresis_val;			/**< MTD hysteresis value */
	int  ctd_hysteresis_val;			/**< CTD hysteresis value */
};

#define CTD_TEMP_INVALID        (0xffff)

/**
 * Enum for CTD events
 */
enum ctd_event_type {
	CTD_HIGH_CRITIC_TEMP_EVENT = 1,	/**<Temp crossed the Critical temp threshold*/
	CTD_HIGH_AVRG_TEMP_EVENT,	/**<Temp crossed the Average temp threshold*/
	CTD_LOW_AVRG_TEMP_EVENT,	/**<Temp comes to below Avg temp threshold*/
	CTD_LOW_CRITIC_TEMP_EVENT,	/**<Temp comes to below Critical temp threshold*/
	CTD_MAX_THERMAL_EVENT
};

#define RTD_TEMP_INVALID        (0xffff)

/**
 * Enum for MTD events
 */
enum mtd_event_type {
	MTD_HIGH_CRITIC_TEMP_EVENT = 1,	/**<Temp crossed the Critical temp threshold*/
	MTD_HIGH_TEMP_EVENT,		/**<Temp crossed the High temp threshold*/
	MTD_LOW_TEMP_EVENT,		/**<Temp comes to below Low temp threshold*/
	MTD_LOW_CRITIC_TEMP_EVENT,	/**<Temp comes to below Critical temp threshold*/
	MTD_MAX_EVENT
};

#define MTD_TEMP_INVALID        (-70)

/**
 * Enum for RTD events
 */
enum rtd_event_type {
	RTD_HIGH_CRITIC_TEMP_EVENT = 1,/**<Temp crossed the Critical temp threshold*/
	RTD_HIGH_TEMP_EVENT,		/**<Temp crossed the High temp threshold*/
	RTD_LOW_TEMP_EVENT,		/**<Temp comes to below Low temp threshold*/
	RTD_LOW_CRITIC_TEMP_EVENT,	/**<Temp comes to below Critical temp threshold*/
	RTD_INVALID_TEMP_EVENT,	/**<Temp is invalid */
	RTD_MAX_EVENT
};

/**
 * Enum for CTD Thermal event
 */
struct ctd_tmu_event {
	enum ctd_event_type event_type;	/**<CTD event type */
	int curr_temp;			/**<CTD temp at event time */
};

/**
 * Enum for MTD Thermal event
 */
struct mtd_tmu_event {
	enum mtd_event_type event_type;	/**<MTD event type */
	int curr_temp;			/**<MTD temp at event time */
};

/**
 * Enum for RTD Thermal event
 */
struct rtd_tmu_event {
	enum rtd_event_type event_type;	/**<RTD event type */
	int curr_temp;			/**<RTD temp at event time */
};

/**
 * Enum for Thermal event
 */
enum thermal_event_id {
	CTD_EVENT = 1,		/**<Thermal Event from CTD */
	MTD_EVENT = 2,		/**<Thermal Event from MTD */
	BOTH_CTD_MTD_EVENT = 3,	/**<Thermal event from both MTD and CTD */
	RTD_EVENT = 4,		/**<Thermal Event from RTD */
	MAX_EVENT = 8
};

/**
 * Structure for Thermal Event
 */
struct thermal_event {
	enum thermal_event_id thermal_id;	/**<Type of Event(MTD,CTD or Both) */
	struct ctd_tmu_event ctd;		/**<ctd event structure */
	struct mtd_tmu_event mtd;		/**<mtd event structure */
	struct rtd_tmu_event rtd;		/**<rtd event structure */
};

/**
 * Enum for mtd available sites.
 */
enum mtd_temp_sites {
    VSPA_TEMP 	= 0,		/**<MTD VSPA Temperature site at 0th position. >**/
    FECA_TEMP 	= 1,		/**<MTD FECA Temperature site at 1st position. >**/
    PCI_TEMP	= 2,		/**<MTD PCI Temperature site at 2nd position. >**/
    DIODE_TEMP	= 3,		/**<MTD Diode Temperature site via i2c read.  >**/
    COMBINED_TEMP = 4         /**<COMBINED_TEMP is combined value of all sites. >**/
};

/**
 * Structure for tvd
 */

struct tvd {
	uint32_t			tvdid;		/**<TVD id*/
	int				dev_tvd_handle;	/**<TVD handle for fd*/
	enum mtd_temp_sites		mtd_site;
					/**<MTD site to get MTD current temp */
	int64_t				get_mtd_power_info;
					/**<MTD power info from i2c sensor*/
	int				get_mtd_curr_temp;
					/**<Get MTD current temp*/
	int				get_ctd_curr_temp;
					/**<Get CTD current temp*/
	int				get_rtd_curr_temp;
					/**<Get RTD current temp*/
	int				tvd_eventfd;
					/**<eventfd for tvd events*/
	struct thermal_event		thermal_event_source;
					/**<Thermal Events structure*/
	struct thermal_threshold	thermal_threshold_source;
					/**<Threshold  structure*/
	struct thermal_hysteresis	hysteresis;
					/**<Hysteresis structure*/
};

/*
 * TVD Lib error codes
 */
#define TVD_OK				0
#define TVD_OPEN_FAIL			1
#define TVD_WRITE_FAIL			2

/* 
 * Adjust temp in host and modem context.
 * */
#define TMU_ADJUST_TEMP_HOST_CTXT(x) (x + TEMP_ADJUST)
#define TMU_ADJUST_TEMP_MDM_CTXT(x) (x - TEMP_ADJUST)

#endif /*__GUL_TVD_IOCTL__*/
