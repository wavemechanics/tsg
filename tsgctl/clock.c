#include <stdio.h>
#include <string.h>
#include "gettok.h"
#include "node.h"
#include "map.h"
#include "tsglib.h"
#include "clock.h"

static int
get_time(int fd)
{
	struct tsg_time t;
	if (tsg_get_clock_time(fd, &t) != 0)
		return -1;
	printf(
		"time: %d-%d %02d:%02d:%02d.%09ld\n",
		t.year,
		t.day,
		t.hour,
		t.min,
		t.sec,
		(unsigned long)t.nsec
	);
	return 0;
}

static int
set_time(int fd)
{
	char *freq = gettok();

	printf("in set clock time\n");
	if (freq == NULL) {
		printf("expected time\n");
		return -2;
	}
	return 0;
}

static int
get_lock(int fd)
{
	uint8_t lock;

	if (tsg_get_clock_lock(fd, &lock) != 0)
		return -1;
	printf("phase lock: %s\n", lock & TSG_CLOCK_PHASE_LOCK ? "yes" : "no");
	printf("input valid: %s\n", lock & TSG_CLOCK_INPUT_VALID ? "yes" : "no" );
	printf("gps lock: %s\n", lock & TSG_CLOCK_GPS_LOCK ? "yes" : "no" );
	return 0;
}

static int
get_dac(int fd)
{
	printf("in get dac\n");
	return 0;
}

static int
set_dac(int fd)
{
	char *dac = gettok();

	printf("in set dac\n");
	if (dac == NULL) {
		printf("expected dac\n");
		return -2;
	}
	return 0;
}

static int
get_leap(int fd)
{
	uint8_t leap;

	if (tsg_get_clock_leap(fd, &leap) != 0)
		return -1;
	printf("leap scheduled: %s\n", leap & TSG_INSERT_LEAP ? "yes" : "no");
	return 0;
}

static int
set_leap(int fd)
{
	char *tok = gettok();
	uint8_t leap;
	int val;

	if ((val = truthy(tok)) == -1) {
		printf("leap argument must be yes or no\n");
		return -2;
	}
	leap = val ? TSG_INSERT_LEAP : 0;

	return tsg_set_clock_leap(fd, &leap);
}

static int
get_dst(int fd)
{
	printf("in get dst\n");
	return 0;
}

static int
set_dst(int fd)
{
	char *freq = gettok();

	printf("in set dst\n");
	if (freq == NULL) {
		printf("expected dst\n");
		return -2;
	}
	return 0;
}

static struct map timecode_map[] = {
	{ TSG_CLOCK_TIMECODE_IRIG_A_AM, "IRIG-A-AM" },
	{ TSG_CLOCK_TIMECODE_IRIG_A_DC, "IRIG-A-DC" },
	{ TSG_CLOCK_TIMECODE_IRIG_B_AM, "IRIG-B-AM" },
	{ TSG_CLOCK_TIMECODE_IRIG_B_DC, "IRIG-B-DC" },
	{ 0,				NULL }
};

static int
get_timecode(int fd)
{
	uint8_t timecode;
	char *s;

	if (tsg_get_clock_timecode(fd, &timecode) != 0)
		return -1;
	s = mapbyval(timecode_map, timecode, "unknown");
	printf("input timecode: %s\n", s);
	return 0;
}

static int
set_timecode(int fd)
{
	char *s = gettok();
	uint8_t timecode;

	if (s == NULL) {
		printmap(stdout, timecode_map, 0);
		return -2;
	}
	timecode = mapbydesc(timecode_map, s, 0xff);
	if (timecode == 0xff) {
		printmap(stdout, timecode_map, 0);
		return -2;
	}
	return tsg_set_clock_timecode(fd, &timecode);
}

static struct map ref_map[] = {
	{ TSG_CLOCK_REF_GEN,		"generator" },
	{ TSG_CLOCK_REF_1PPS,		"1pps" },
	{ TSG_CLOCK_REF_GPS,		"gps" },
	{ TSG_CLOCK_REF_TIMECODE,	"timecode" },
	{ 0,				NULL }
};

static int
get_reference(int fd)
{
	uint8_t ref;
	char *s;

	if (tsg_get_clock_ref(fd, &ref) != 0)
		return -1;
	s = mapbyval(ref_map, ref, "unknown");
	printf("reference: %s\n", s);
	return 0;
}

static int
set_reference(int fd)
{
	char *s = gettok();
	uint8_t ref;

	if (s == NULL) {
		printmap(stdout, ref_map, 0);
		return -2;
	}
	ref = mapbydesc(ref_map, s, 0xff);
	if (ref == 0xff) {
		printmap(stdout, ref_map, 0);
		return -2;
	}
	return tsg_set_clock_ref(fd, &ref);
}

static int
get_run(int fd)
{
	printf("in get run state\n");
	return 0;
}

static int
set_run(int fd)
{
	char *freq = gettok();

	printf("in set run state\n");
	if (freq == NULL) {
		printf("expected run state\n");
		return -2;
	}
	return 0;
}

static int
get_tz_offset(int fd)
{
	printf("in get tz offset\n");
	return 0;
}

static int
set_tz_offset(int fd)
{
	char *freq = gettok();

	printf("in set tz offset\n");
	if (freq == NULL) {
		printf("expected tz offset\n");
		return -2;
	}
	return 0;
}

static int
get_phase_compensation(int fd)
{
	printf("in get phase compensation\n");
	return 0;
}

static int
set_phase_compensation(int fd)
{
	char *freq = gettok();

	printf("in set phase compensation\n");
	if (freq == NULL) {
		printf("expected phase compensation\n");
		return -2;
	}
	return 0;
}

int
get_clock(int fd)
{
	static struct node params[] = {
		{ "time", "board clock time", get_time },
		{ "lock", "lock status", get_lock },
		{ "dac", "DAC value", get_dac },
		{ "leap", "leap second scheduled", get_leap },
		{ "dst", "DST status", get_dst },
		{ "timecode", "input timecode format", get_timecode },
		{ "reference", "sync source", get_reference },
		{ "run", "generator run state", get_run },
		{ "tz-offset", "timezone offset", get_tz_offset },
		{ "phase-compensation", "phase compensation", get_phase_compensation },
		{ NULL, NULL, NULL },
	};

	return walk(params, fd);
}

int
set_clock(int fd)
{
	static struct node params[] = {
		{ "time", "board clock time", set_time },
		{ "dac", "DAC value", set_dac },
		{ "leap", "schedule leap second", set_leap },
		{ "dst", "DST status", set_dst },
		{ "timecode", "input timecode format", set_timecode },
		{ "reference", "sync source", set_reference },
		{ "run", "generator run state", set_run },
		{ "tz-offset", "timezone offset", set_tz_offset },
		{ "phase-compensation", "phase compensation", set_phase_compensation },
		{ NULL, NULL, NULL },
	};

	return walk(params, fd);
}
