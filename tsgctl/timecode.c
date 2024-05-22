#include <stdio.h>
#include "gettok.h"
#include "node.h"
#include "map.h"
#include "tsglib.h"
#include "timecode.h"

static int
get_format(int fd)
{
	printf("in get format\n");
	return 0;
}

static int
set_format(int fd)
{
	char *format = gettok();

	printf("in set format\n");
	if (format == NULL) {
		printf("expected format\n");
		return -2;
	}
	return 0;
}

static int
get_quality(int fd)
{
	struct tsg_timecode_quality qual;

	if (tsg_get_timecode_quality(fd, &qual) == -1)
		return -1;
	printf("timecode source locked: %s\n", qual.locked ? "yes" : "no");
	printf("timecode source quality level: %d\n", qual.level);
	return 0;
}

static int
get_use_quality(int fd)
{
	uint8_t use;

	if (tsg_get_use_timecode_quality(fd, &use) == -1)
		return -1;
	printf("use timecode quality: %s\n", use ? "yes" : "no");
	return 0;
}

static int
set_use_quality(int fd)
{
	char *tok = gettok();
	int val;
	uint8_t use;

	if ((val = truthy(tok)) == -1) {
		puts("use-quality must be yes or no");
		return -2;
	}
	use = val ? TSG_USE_TQ : 0;

	return tsg_set_use_timecode_quality(fd, &use);
}

static int
get_calibration(int fd)
{
	printf("in get calibration\n");
	return 0;
}

static int
set_calibration(int fd)
{
	char *calibration = gettok();

	printf("in set calibration\n");
	if (calibration == NULL) {
		printf("expected calibration\n");
		return -2;
	}
	return 0;
}

static int
get_agc_delays(int fd)
{
	struct tsg_agc_delays delays;

	if (tsg_get_timecode_agc_delays(fd, &delays) != 0)
		return -1;
	printf("IRIG-A AGC delay: 0.%09ds\n", delays.irig_a_nsec);
	printf("IRIG-B AGC delay: 0.%09ds\n", delays.irig_b_nsec);
	return 0;
}

static int
set_agc_delays(int fd)
{
	char *agc_delays = gettok();

	printf("in set agc delays\n");
	if (agc_delays == NULL) {
		printf("expected agc delays\n");
		return -2;
	}
	return 0;
}

int
get_timecode(int fd)
{
	static struct node params[] = {
		{ "format", "timecode format", get_format },
		{ "quality", "timecode quality bits", get_quality },
		{ "use-quality", "use timecode quality bits", get_use_quality },
		{ "calibration", "calibration code", get_calibration },
		{ "agc-delays", "AGC delays", get_agc_delays },
		{ NULL, NULL, NULL },
	};

	return walk(params, fd);
}

int
set_timecode(int fd)
{
	static struct node params[] = {
		{ "format", "timecode format", set_format },
		{ "use-quality", "use timecode quality", set_use_quality },
		{ "calibration", "start calibration", set_calibration },
		{ "agc-delays", "AGC delays", set_agc_delays },
		{ NULL, NULL, NULL },
	};

	return walk(params, fd);
}
