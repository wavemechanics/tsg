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

static struct map pin6_map[] = {
	{ 0,			"comparator" },
	{ TSG_BOARD_PIN6_SYNTH,	"synth" },
	{ 0,			NULL }
};

static int
get_pin6(int fd)
{
	uint8_t pin6;
	char *s;

	if (tsg_get_board_pin6(fd, &pin6) == -1)
		return -1;
	s = mapbyval(pin6_map, pin6, "unknown");
	printf("pin6: %s\n", s);
	return 0;
}

static int
set_pin6(int fd)
{
	char *s = gettok();
	uint8_t pin6;

	if (s == NULL) {
		printmap(stdout, pin6_map, 0);
		return -2;
	}
	pin6 = mapbydesc(pin6_map, s, 0xff);
	if (pin6 == 0xff) {
		printmap(stdout, pin6_map, 0);
		return -2;
	}

	return tsg_set_board_pin6(fd, &pin6);
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

	return tsg_set_board_j1(fd, &j1);
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
get_int_mask(int fd)
{
	uint8_t mask;

	if (tsg_get_int_mask(fd, &mask) == -1)
		return -1;
	printf("int-mask: ");
	if (mask == 0)
		putchar('-');
	if (mask & TSG_INT_ENABLE_COMPARE)
		putchar('c');
	if (mask & TSG_INT_ENABLE_EXT)
		putchar('e');
	if (mask & TSG_INT_ENABLE_PULSE)
		putchar('p');
	if (mask & TSG_INT_ENABLE_SYNTH)
		putchar('s');
	putchar('\n');
	return 0;
}

int
set_int_mask(int fd)
{
	char *tok = gettok();
	uint8_t mask;
	char *msg = "\"-\" or any combination of c, e, p, s (eg \"ep\")\n";

	if (tok == NULL) {
		puts(msg);
		return -2;
	}
	if (strcmp(tok, "-") == 0)
		mask = 0;
	else {
		mask = 0;
		for (char *cp = tok; *cp; ++cp) {
			switch (*cp) {
			case 'c':
				mask |= TSG_INT_ENABLE_COMPARE;
				break;
			case 'e':
				mask |= TSG_INT_ENABLE_EXT;
				break;
			case 'p':
				mask |= TSG_INT_ENABLE_PULSE;
				break;
			case 's':
				mask |= TSG_INT_ENABLE_SYNTH;
				break;
			default:
				puts(msg);
				return -2;
			}
		}
	}
	return tsg_set_int_mask(fd, &mask);
}

static int
get_latched_time(int fd)
{
        struct tsg_time t;
        if (tsg_get_latched_time(fd, &t) != 0)
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

int
get_board(int fd)
{
	static struct node params[] = {
		{ "model", "model", get_model },
		{ "firmware", "firmware version", get_firmware },
		{ "pin6", "pin6 output signal", get_pin6 },
		{ "j1", "J1 output signal", get_j1 },
		{ "self-test", "self test results", get_self_test },
		{ "int-mask", "interrupt mask ", get_int_mask },
		{ "latched-time", "time latched in last interrupt", get_latched_time },
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
		{ "int-mask", "interrupt mask", set_int_mask },
		{ NULL, NULL, NULL },
	};

	return walk(params, fd);
}

