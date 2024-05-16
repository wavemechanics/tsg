#include <stdio.h>
#include "node.h"
#include "pulse.h"
#include "synth.h"
#include "clock.h"
#include "gps.h"
#include "timecode.h"
#include "board.h"
#include "event.h"
#include "action.h"

static int
get(int fd)
{
	static struct node components[] = {
		{ "pulse", "pulse generator", get_pulse },
		{ "synth", "frequency synthesizer", get_synth },
		{ "clock", "clock and oscillator", get_clock },
		{ "gps", "GPS receiver and antenna", get_gps },
		{ "timecode", "input timecode", get_timecode },
		{ "board", "PCI board", get_board },
		{ "event", "event timestamper", get_event },
		{ NULL, NULL, NULL },
	};

	return walk(components, fd);
}

static int
set(int fd)
{
	static struct node components[] = {
		{ "pulse", "pulse generator", set_pulse },
		{ "synth", "frequency synthesizer", set_synth },
		{ "clock", "clock and oscillator", set_clock },
		{ "timecode", "input timecode", set_timecode },
		{ "board", "PCI board", set_board },
		{ "event", "event timestamper", set_event },
		{ NULL, NULL, NULL },
	};

	return walk(components, fd);
}

static int
load(int fd)
{
	static struct node components[] = {
		{ "synth", "frequency synthesizer", load_synth },
		{ NULL, NULL, NULL },
	};

	return walk(components, fd);
}

static int
restore(int fd)
{
	static struct node components[] = {
		{ "board", "PCI board", restore_board },
		{ NULL, NULL, NULL },
	};

	return walk(components, fd);
}

static int
reset(int fd)
{
	static struct node components[] = {
		{ "board", "PCI board", reset_board },
		{ NULL, NULL, NULL },
	};

	return walk(components, fd);
}

static int
save(int fd)
{
	static struct node components[] = {
		{ "board", "PCI board", save_board },
		{ NULL, NULL, NULL },
	};

	return walk(components, fd);
}

int
action(int fd)
{
	static struct node actions[] = {
		{ "get", "print board parameters", get },
		{ "set", "set board parameters", set },
		{ "load", "load parameters from eeprom", load },
		{ "reset", "reset factory parameters", reset },
		{ "restore", "restore eeprom parameters", restore },
		{ "save", "save eeprom parameters", save } ,
		{ NULL, NULL, NULL },
	};

	return walk(actions, fd);
}
