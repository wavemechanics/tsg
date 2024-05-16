#include <stdio.h>
#include <string.h>
#include "gettok.h"
#include "node.h"
#include "map.h"
#include "tsglib.h"
#include "board.h"

static int
get_model(int fd)
{
	uint16_t model;

	if (tsg_get_board_model(fd, &model) != 0)
		return -1;
	printf("model: %x\n", model);
	return 0;
}

static int
get_firmware(int fd)
{
	struct tsg_board_firmware fw;

	if (tsg_get_board_firmware(fd, &fw) != 0)
		return -1;
	printf("firmware: %d.%d.%d\n", fw.major, fw.minor, fw.test);

	return 0;
}

static int
get_pin6(int fd)
{
	printf("in get pin6\n");
	return 0;
}

static int
set_pin6(int fd)
{
	char *pin6 = gettok();

	printf("in set pin6\n");
	if (pin6 == NULL) {
		printf("expected pin6\n");
		return -2;
	}
	return 0;
}

static struct map j1_map[] = {
	{ TSG_J1_IRIG_B_AM, "IRIG-B-AM" },
	{ TSG_J1_IRIG_B_DC, "IRIG-B-DC" },
	{ TSG_J1_PULSE,     "pulse" },
	{ TSG_J1_SYNTH,     "synth" },
	{ TSG_J1_STROBE,    "strobe" },
	{ TSG_J1_1PPS,      "1pps" },
	{ 0,                NULL }
};

static int
get_j1(int fd)
{
	uint8_t j1;
	char *s;

	if (tsg_get_board_j1(fd, &j1) != 0)
		return -1;
	s = mapbyval(j1_map, j1, "unknown");
	printf("J1: %s\n", s);
	return 0;
}

static int
set_j1(int fd)
{
	char *s = gettok();
	uint8_t j1;

	if (s == NULL) {
		printmap(stdout, j1_map, 0);
		return -2;
	}

	j1 = mapbydesc(j1_map, s, 0xff);
	if (j1 == 0xff) {
		printmap(stdout, j1_map, 0);
		return -2;
	}

	if (tsg_set_board_j1(fd, &j1) != 0)
		return -1;
	return 0;
}

static int
get_self_test(int fd)
{
	uint8_t status;

	if (tsg_get_board_test_status(fd, &status) != 0)
		return -1;
	printf("hardware: %s\n", status & TSG_TEST_HARDWARE_FAIL ? "fail" : "ok");
	printf("dac limit: %s\n", status & TSG_TEST_DAC_LIMIT ? "fail" : "ok");
	printf("ram: %s\n", status & TSG_TEST_RAM_FAIL ? "fail" : "ok");
	printf("clock: %s\n", status & TSG_TEST_CLOCK_FAIL ? "fail" : "ok");
	return 0;
}

int
reset_board(int fd)
{
	printf("in reset\n");
	return 0;
}

int
restore_board(int fd)
{
	printf("in restore\n");
	return 0;
}

int
save_board(int fd)
{
	printf("in save\n");
	return 0;
}

int
get_board(int fd)
{
	static struct node params[] = {
		{ "model", "model", get_model },
		{ "firmware", "firmware version", get_firmware },
		{ "pin6", "pin6 output signal", get_pin6 },
		{ "j1", "J1 output signal", get_j1 },
		{ "self-test", "self test results", get_self_test },
		{ NULL, NULL, NULL },
	};

	return walk(params, fd);
}

int
set_board(int fd)
{
	static struct node params[] = {
		{ "pin6", "pin6 output signal", set_pin6 },
		{ "j1", "J1 output signal", set_j1 },
		{ NULL, NULL, NULL },
	};

	return walk(params, fd);
}

