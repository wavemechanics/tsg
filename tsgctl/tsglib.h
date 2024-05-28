/*
 * tsglib.c
 */

#ifndef	_TSGLIB_H
#define	_TSGLIB_H

#include <sys/types.h>
#include <tsg.h>

int tsg_get_board_model(int fd, uint16_t *model);
int tsg_get_board_firmware(int fd, struct tsg_board_firmware *p);
int tsg_get_board_test_status(int fd, uint8_t *p);
int tsg_get_board_j1(int fd, uint8_t *p);
int tsg_set_board_j1(int fd, uint8_t *p);
int tsg_get_board_pin6(int fd, uint8_t *p);
int tsg_set_board_pin6(int fd, uint8_t *p);
int tsg_get_pulse_freq(int fd, uint8_t *p);
int tsg_set_pulse_freq(int fd, uint8_t *p);
int tsg_get_gps_antenna_status(int fd, uint8_t *p);
int tsg_get_clock_lock(int fd, uint8_t *p);
int tsg_get_clock_timecode(int fd, uint8_t *p);
int tsg_set_clock_timecode(int fd, uint8_t *p);
int tsg_get_clock_ref(int fd, uint8_t *p);
int tsg_set_clock_ref(int fd, uint8_t *p);
int tsg_get_clock_time(int fd, struct tsg_time *p);
int tsg_set_clock_time(int fd, struct tsg_time *p);
int tsg_get_gps_position(int fd, struct tsg_position *p);
int tsg_get_gps_signal(int fd, struct tsg_signal *p);
int tsg_get_timecode_agc_delays(int fd, struct tsg_agc_delays *p);
int tsg_get_clock_leap(int fd, uint8_t *p);
int tsg_set_clock_leap(int fd, uint8_t *p);
int tsg_get_clock_dac(int fd, uint16_t *p);
int tsg_save_clock_dac(int fd);
int tsg_get_clock_dst(int fd, uint8_t *p);
int tsg_set_clock_dst(int fd, uint8_t *p);
int tsg_get_clock_stop(int fd, uint8_t *p);
int tsg_set_clock_stop(int fd, uint8_t *p);
int tsg_get_clock_tz_offset(int fd, struct tsg_tz_offset *p);
int tsg_set_clock_tz_offset(int fd, struct tsg_tz_offset *p);
int tsg_get_clock_phase_compensation(int fd, int32_t *p);
int tsg_set_clock_phase_compensation(int fd, int32_t *p);
int tsg_get_timecode_quality(int fd, struct tsg_timecode_quality *p);
int tsg_get_use_timecode_quality(int fd, uint8_t *p);
int tsg_set_use_timecode_quality(int fd, uint8_t *p);
int tsg_get_synth_freq(int fd, uint32_t *p);
int tsg_set_synth_freq(int fd, uint32_t *p);
int tsg_get_synth_edge(int fd, uint8_t *p);
int tsg_set_synth_edge(int fd, uint8_t *p);
int tsg_get_synth_enable(int fd, uint8_t *p);
int tsg_set_synth_enable(int fd, uint8_t *p);
int tsg_get_compare_time(int fd, struct tsg_compare_time *p);
int tsg_set_compare_time(int fd, struct tsg_compare_time *p);
int tsg_get_int_mask(int fd, uint8_t *p);
int tsg_set_int_mask(int fd, uint8_t *p);
int tsg_get_board_p_count(int fd, struct tsg_counts *p);

#endif
