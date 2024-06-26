#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
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
		"time: %d-%d-%02d:%02d:%02d.%09ld\n",
		t.year,
		t.day,
		t.hour,
		t.min,
		t.sec,
		(unsigned long)t.nsec
	);
	return 0;
}

/* get system time if s is "system"; returns -1 if any problem */
static int
parse_system_time(char *s, struct tsg_time *t)
{
	struct timeval tv;
	struct tm *tm;

	if (strcasecmp(s, "system") != 0)
		return -1;
	if (gettimeofday(&tv, NULL) == -1)
		return -1;
	tm = gmtime(&tv.tv_sec);
	t->year = tm->tm_year + 1900;
	t->day = tm->tm_yday + 1;
	t->hour = tm->tm_hour;
	t->min = tm->tm_min;
	t->sec = tm->tm_sec;
	t->nsec = tv.tv_usec * 1000;

	return 0;
}

/* parse time string that includes msec; returns -1 if wrong number of fields */
static int
parse_msec_time(char *s, struct tsg_time *t)
{
	int fields;
	unsigned year, doy, hour, min, sec, msec;

	fields = sscanf(
		s,
		"%u-%u-%u:%u:%u.%u",
		&year, &doy, &hour, &min, &sec, &msec
	);
	if (fields != 6)
		return -1;

	t->year = year;
	t->day = doy;
	t->hour = hour;
	t->min = min;
	t->sec = sec;
	t->nsec = msec * 1000000;

	return 0;
}

/* parse time string that only goes to seconds; returns -1 if wrong number of fields */
static int
parse_sec_time(char *s, struct tsg_time *t)
{
	int fields;
	unsigned year, doy, hour, min, sec;

	fields = sscanf(
		s,
		"%u-%u-%u:%u:%u",
		&year, &doy, &hour, &min, &sec
	);
	if (fields != 5)
		return -1;

	t->year = year;
	t->day = doy;
	t->hour = hour;
	t->min = min;
	t->sec = sec;
	t->nsec = 0;

	return 0;
}

/* parse time string that only has year; returns -1 if wrong number of fields */
static int
parse_year_time(char *s, struct tsg_time *t)
{
	unsigned year;

	if (sscanf(s, "%u", &year) != 1)
		return -1;

	t->year = year;
	t->day = 0;
	t->hour = 0;
	t->min = 0;
	t->sec = 0;
	t->nsec = 0;

	return 0;
}

static int
parse_time(char *s, struct tsg_time *t)
{
	if (parse_system_time(s, t) == 0)
		return 0;
	if (parse_msec_time(s, t) == 0)
		return 0;
	if (parse_sec_time(s, t) == 0)
		return 0;
	if (parse_year_time(s, t) == 0)
		return 0;
	return -1;
}

static int
set_time(int fd)
{
	char *tok = gettok();
	struct tsg_time t;
	char *msg = "clock time should be \"system\", YYYY, or YYYY-DDD-HH:MM:SS[.mmm]\n";

	if (tok == NULL) {
		puts(msg);
		return -2;
	}
	if (parse_time(tok, &t) == -1) {
		puts(msg);
		return -2;
	}

	return tsg_set_clock_time(fd, &t);
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
	uint16_t dac;

	if (tsg_get_clock_dac(fd, &dac) != 0)
		return -1;
	printf("DAC setting: 0x%04x\n", dac);
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
	uint8_t dst;

	if (tsg_get_clock_dst(fd, &dst) != 0)
		return -1;
	printf("DST: %s\n", dst ? "yes" : "no");
	return 0;
}

static int
set_dst(int fd)
{
	char *tok = gettok();
	uint8_t dst;
	int val;

	if ((val = truthy(tok)) == -1) {
		printf("dst argument must be yes or no\n");
		return -2;
	}
	dst = val ? TSG_CLOCK_DST_ENABLE : 0;

	return tsg_set_clock_dst(fd, &dst);
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
get_stop(int fd)
{
	uint8_t stop;

	if (tsg_get_clock_stop(fd, &stop) == -1)
		return -1;
	printf("clock stopped: %s\n", stop ? "yes" : "no");
	return 0;
}

static int
set_stop(int fd)
{
        char *tok = gettok();
        uint8_t stop;
        int val;

        if ((val = truthy(tok)) == -1) {
                printf("stop argument must be yes or no\n");
                return -2;
        }
        stop = val ? TSG_CLOCK_STOP : 0;

        return tsg_set_clock_stop(fd, &stop);
}

static int
get_tz_offset(int fd)
{
	struct tsg_tz_offset off;

	if (tsg_get_clock_tz_offset(fd, &off) == -1)
		return -1;
	printf("TZ offset: %c%d:%02d\n", off.sign, off.hour, off.min);
	return 0;
}

static int
set_tz_offset(int fd)
{
	char *tok = gettok();
	char sign;
	unsigned hour, min;
	char *msg = "offset argument must be like +|-HH:MM";

	if (tok == NULL) {
		puts(msg);
		return -2;
	}
	if (sscanf(tok, "%c%u:%u", &sign, &hour, &min) != 3) {
		puts(msg);
		return -2;
	}
	if (sign != '-' && sign != '+') {
		puts(msg);
		return -2;
	}
	if (hour > 12 || min > 59) {
		puts(msg);
		return -2;
	}

	struct tsg_tz_offset off = {
		.sign = sign,
		.hour = hour,
		.min = min,
	};
	if (tsg_set_clock_tz_offset(fd, &off) == -1)
		return -1;

	return 0;
}

static int
get_phase_compensation(int fd)
{
	int32_t offset;

	if (tsg_get_clock_phase_compensation(fd, &offset) == -1)
		return -1;
	printf("phase compensation: %ld nsec\n", (long)offset);
	return 0;
}

static int
set_phase_compensation(int fd)
{
	char *tok = gettok();
	long n;
	int32_t nsec;
	char *msg = "phase compensation argument must be number of nanoseconds";

	if (tok == NULL) {
		puts(msg);
		return -2;
	}
	if (sscanf(tok, "%ld", &n) != 1) {
		puts(msg);
		return -2;
	}
	nsec = n;

	return tsg_set_clock_phase_compensation(fd, &nsec);
}

static int
save_dac(int fd)
{
	return tsg_save_clock_dac(fd);
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
		{ "stop", "generator stop state", get_stop },
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
		{ "stop", "generator stop state", set_stop },
		{ "tz-offset", "timezone offset", set_tz_offset },
		{ "phase-compensation", "phase compensation", set_phase_compensation },
		{ NULL, NULL, NULL },
	};

	return walk(params, fd);
}

int
save_clock(int fd)
{
	static struct node params[] = {
		{ "dac", "DAC value", save_dac },
		{ NULL, NULL, NULL },
	};

	return walk(params, fd);
}
