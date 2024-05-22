/*
 * tsg.c
 *
 * This extra layer might not seem helpful now, but I'm thinking
 * something like this might make it easier to integrate TSG access
 * into other languages like Go or Tcl later. We'll see.
 */

#include "../tsg/tsg.h"

int
tsg_get_board_model(int fd, uint16_t *model)
{
	return ioctl(fd, TSG_GET_BOARD_MODEL, model);
}

int
tsg_get_board_firmware(int fd, struct tsg_board_firmware *p)
{
	return ioctl(fd, TSG_GET_BOARD_FIRMWARE, p);
}

int
tsg_get_board_test_status(int fd, uint8_t *p)
{
	return ioctl(fd, TSG_GET_BOARD_TEST_STATUS, p);
}

int
tsg_get_board_j1(int fd, uint8_t *p)
{
	return ioctl(fd, TSG_GET_BOARD_J1, p);
}

int
tsg_set_board_j1(int fd, uint8_t *p)
{
	return ioctl(fd, TSG_SET_BOARD_J1, p);
}

int
tsg_get_pulse_freq(int fd, uint8_t *p)
{
	return ioctl(fd, TSG_GET_PULSE_FREQ, p);
}

int
tsg_set_pulse_freq(int fd, uint8_t *p)
{
	return ioctl(fd, TSG_SET_PULSE_FREQ, p);
}

int
tsg_get_gps_antenna_status(int fd, uint8_t *p)
{
	return ioctl(fd, TSG_GET_GPS_ANTENNA_STATUS, p);
}

int
tsg_get_clock_lock(int fd, uint8_t *p)
{
	return ioctl(fd, TSG_GET_CLOCK_LOCK, p);
}

int
tsg_get_clock_timecode(int fd, uint8_t *p)
{
	return ioctl(fd, TSG_GET_CLOCK_TIMECODE, p);
}

int
tsg_set_clock_timecode(int fd, uint8_t *p)
{
	return ioctl(fd, TSG_SET_CLOCK_TIMECODE, p);
}

int
tsg_get_clock_ref(int fd, uint8_t *p)
{
	return ioctl(fd, TSG_GET_CLOCK_REF, p);
}

int
tsg_set_clock_ref(int fd, uint8_t *p)
{
	return ioctl(fd, TSG_SET_CLOCK_REF, p);
}

int
tsg_get_clock_time(int fd, struct tsg_time *p)
{
	return ioctl(fd, TSG_GET_CLOCK_TIME, p);
}

int
tsg_get_gps_position(int fd, struct tsg_position *p)
{
	return ioctl(fd, TSG_GET_GPS_POSITION, p);
}

int
tsg_get_gps_signal(int fd, struct tsg_signal *p)
{
	return ioctl(fd, TSG_GET_GPS_SIGNAL, p);
}

int
tsg_get_timecode_agc_delays(int fd, struct tsg_agc_delays *p)
{
	return ioctl(fd, TSG_GET_TIMECODE_AGC_DELAYS, p);
}

int
tsg_get_clock_leap(int fd, uint8_t *p)
{
	return ioctl(fd, TSG_GET_CLOCK_LEAP, p);
}

int
tsg_set_clock_leap(int fd, uint8_t *p)
{
	return ioctl(fd, TSG_SET_CLOCK_LEAP, p);
}

int
tsg_get_clock_dac(int fd, uint16_t *p)
{
	return ioctl(fd, TSG_GET_CLOCK_DAC, p);
}

int
tsg_save_clock_dac(int fd)
{
	return ioctl(fd, TSG_SAVE_CLOCK_DAC);
}

int
tsg_get_clock_dst(int fd, uint8_t *p)
{
	return ioctl(fd, TSG_GET_CLOCK_DST, p);
}

int
tsg_set_clock_dst(int fd, uint8_t *p)
{
	return ioctl(fd, TSG_SET_CLOCK_DST, p);
}

int
tsg_get_clock_stop(int fd, uint8_t *p)
{
	return ioctl(fd, TSG_GET_CLOCK_STOP, p);
}

int
tsg_set_clock_stop(int fd, uint8_t *p)
{
	return ioctl(fd, TSG_SET_CLOCK_STOP, p);
}

int
tsg_get_clock_tz_offset(int fd, struct tsg_tz_offset *p)
{
	return ioctl(fd, TSG_GET_CLOCK_TZ_OFFSET, p);
}

int
tsg_set_clock_tz_offset(int fd, struct tsg_tz_offset *p)
{
	return ioctl(fd, TSG_SET_CLOCK_TZ_OFFSET, p);
}

int
tsg_get_clock_phase_compensation(int fd, int32_t *p)
{
	return ioctl(fd, TSG_GET_CLOCK_PHASE_COMP, p);
}

int
tsg_set_clock_phase_compensation(int fd, int32_t *p)
{
	return ioctl(fd, TSG_SET_CLOCK_PHASE_COMP, p);
}

int
tsg_get_timecode_quality(int fd, struct tsg_timecode_quality *p)
{
	return ioctl(fd, TSG_GET_TIMECODE_QUALITY, p);
}

int
tsg_get_use_timecode_quality(int fd, uint8_t *p)
{
	return ioctl(fd, TSG_GET_USE_TIMECODE_QUALITY, p);
}

int
tsg_set_use_timecode_quality(int fd, uint8_t *p)
{
	return ioctl(fd, TSG_SET_USE_TIMECODE_QUALITY, p);
}
