/*
 * tsg.h
 */


#ifndef _TSG_H
#define	_TSG_H

#ifndef	_KERNEL
#include <sys/ioctl.h>
#include <sys/types.h>
#endif

/* these model numbers correspond to PCI subdevice ids; don't change */
#define	TSG_MODEL_PCI_SG	0x5900
#define	TSG_MODEL_GPS_PCI	0x5901	// probably
#define	TSG_MODEL_PCI_SG_2U	0x5907
#define	TSG_MODEL_GPS_PCI_2U	0x5908

#define	TSG_GET_BOARD_MODEL	_IOR('T', 5, uint16_t)

struct tsg_board_firmware {
	uint8_t major;
	uint8_t minor;
	uint8_t test;
};

#define	TSG_GET_BOARD_FIRMWARE	_IOR('T', 10, struct tsg_board_firmware)

#define	TSG_TEST_HARDWARE_FAIL	0x08
#define	TSG_TEST_DAC_LIMIT	0x04
#define	TSG_TEST_RAM_FAIL	0x02
#define	TSG_TEST_CLOCK_FAIL	0x01

#define	TSG_GET_BOARD_TEST_STATUS	_IOR('T', 15, uint8_t)

#define	TSG_J1_IRIG_B_AM	0x00
#define	TSG_J1_IRIG_B_DC	0x01
#define	TSG_J1_PULSE		0x02
#define	TSG_J1_SYNTH		0x03
#define	TSG_J1_STROBE		0x04
#define	TSG_J1_1PPS		0x05

#define	TSG_GET_BOARD_J1	_IOR('T', 20, uint8_t)
#define	TSG_SET_BOARD_J1	_IOW('T', 21, uint8_t)

#define	TSG_PULSE_FREQ_DISABLED	0x00
#define	TSG_PULSE_FREQ_1HZ	0x05
#define	TSG_PULSE_FREQ_10HZ	0x04
#define	TSG_PULSE_FREQ_100HZ	0x03
#define	TSG_PULSE_FREQ_1KHZ	0x02
#define	TSG_PULSE_FREQ_10KHZ	0x01
#define	TSG_PULSE_FREQ_100KHZ	0x06
#define	TSG_PULSE_FREQ_1MHZ	0x07
#define	TSG_PULSE_FREQ_5MHZ	0x08
#define	TSG_PULSE_FREQ_10MHZ	0x09

#define	TSG_GET_PULSE_FREQ	_IOR('T', 25, uint8_t)
#define	TSG_SET_PULSE_FREQ	_IOW('T', 26, uint8_t)

#define	TSG_GPS_ANTENNA_SHORTED	0x02
#define	TSG_GPS_ANTENNA_OPEN	0x01

#define	TSG_GET_GPS_ANTENNA_STATUS	_IOR('T', 30, uint8_t)

#define	TSG_CLOCK_PHASE_LOCK	0x04
#define	TSG_CLOCK_INPUT_VALID	0x02
#define	TSG_CLOCK_GPS_LOCK	0x01

#define	TSG_GET_CLOCK_LOCK	_IOR('T', 35, uint8_t)

#define	TSG_CLOCK_TIMECODE_IRIG_A_AM	0x01
#define	TSG_CLOCK_TIMECODE_IRIG_A_DC	0x81
#define	TSG_CLOCK_TIMECODE_IRIG_B_AM	0x00
#define	TSG_CLOCK_TIMECODE_IRIG_B_DC	0x80
#define	TSG_CLOCK_TIMECODE_MASK		0x83

#define	TSG_GET_CLOCK_TIMECODE	_IOR('T', 40, uint8_t)
#define	TSG_SET_CLOCK_TIMECODE	_IOW('T', 41, uint8_t)

#define	TSG_CLOCK_REF_GEN	0x00
#define	TSG_CLOCK_REF_1PPS	0x41
#define	TSG_CLOCK_REF_GPS	0x21
#define	TSG_CLOCK_REF_TIMECODE	0x11
#define	TSG_CLOCK_REF_MASK	0x71

#define	TSG_GET_CLOCK_REF	_IOR('T', 50, uint8_t)
#define	TSG_SET_CLOCK_REF	_IOW('T', 51, uint8_t)

struct tsg_time {
	uint16_t year;
	uint16_t day;
	uint8_t  hour;
	uint8_t  min;
	uint8_t  sec;
	uint32_t nsec;
};

#define	TSG_GET_CLOCK_TIME	_IOR('T', 60, struct tsg_time)
#define	TSG_SET_CLOCK_TIME	_IOW('T', 61, struct tsg_time)

struct tsg_position {
	uint16_t lat_deg;
	uint16_t lat_min;
	uint16_t lat_sec;
	uint8_t  lat_decisec;
	uint8_t  lat_dir;

	uint16_t lon_deg;
	uint16_t lon_min;
	uint16_t lon_sec;
	uint8_t  lon_decisec;
	uint8_t  lon_dir;

	uint16_t elev_meter;
	uint8_t  elev_decimeter;
	uint8_t  elev_sign;
};

#define TSG_GET_GPS_POSITION	_IOR('T', 70, struct tsg_position)
#define	TSG_SET_GPS_POSITION	_IOW('T', 71, struct tsg_position)

#define	TSG_MAX_SATELLITES	6
struct tsg_satellite {
	uint8_t sv;
	uint8_t level;
	uint8_t centilevel;
};
struct tsg_signal {
	struct tsg_satellite satellites[TSG_MAX_SATELLITES];
};

#define	TSG_GET_GPS_SIGNAL	_IOR('T', 80, struct tsg_signal)

struct tsg_agc_delays {
	uint32_t irig_a_nsec;
	uint32_t irig_b_nsec;
};

#define	TSG_GET_TIMECODE_AGC_DELAYS	_IOR('T', 90, struct tsg_agc_delays)

#endif