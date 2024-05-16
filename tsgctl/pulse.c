#include <stdio.h>
#include "gettok.h"
#include "node.h"
#include "map.h"
#include "tsglib.h"
#include "pulse.h"

static struct map freq_map[] = {
	{ TSG_PULSE_FREQ_DISABLED, "disabled" },
	{ TSG_PULSE_FREQ_1HZ, "1Hz" },
	{ TSG_PULSE_FREQ_10HZ, "10Hz" },
	{ TSG_PULSE_FREQ_100HZ, "100Hz" },
	{ TSG_PULSE_FREQ_1KHZ, "1kHz" },
	{ TSG_PULSE_FREQ_10KHZ, "10kHz" },
	{ TSG_PULSE_FREQ_100KHZ, "100kHz" },
	{ TSG_PULSE_FREQ_1MHZ, "1MHz" },
	{ TSG_PULSE_FREQ_5MHZ, "5MHz" },
	{ TSG_PULSE_FREQ_10MHZ, "10MHz" },
	{ 0, NULL }
};

static int
get_pulse_freq(int fd)
{
	uint8_t freq;
	char *s;

	if (tsg_get_pulse_freq(fd, &freq) != 0)
		return -1;
	//printf("freq=0x%x\n", freq);
	s = mapbyval(freq_map, freq, "unknown");
	printf("pulse freq: %s\n", s);
	return 0;
}

static int
set_pulse_freq(int fd)
{
	char *s = gettok();
	uint8_t freq;

	if (s == NULL) {
		printmap(stdout, freq_map, 0);
		return -2;
	}
	freq = mapbydesc(freq_map, s, 0xff);
	if (freq == 0xff) {
		printmap(stdout, freq_map, 0);
		return -2;
	}

	if (tsg_set_pulse_freq(fd, &freq) != 0)
		return -1;
	return 0;
}

int
get_pulse(int fd)
{
	static struct node params[] = {
		{ "freq", "pulse generator frequency", get_pulse_freq },
		{ NULL, NULL, NULL },
	};

	return walk(params, fd);
}

int
set_pulse(int fd)
{
	static struct node params[] = {
		{ "freq", "pulse generator frequency", set_pulse_freq },
		{ NULL, NULL, NULL },
	};

	return walk(params, fd);
}
