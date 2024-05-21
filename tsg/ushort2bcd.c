#ifdef MAIN
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#endif

/* n has to be an int/unsigned because otherwise va_start moans.
 * But its value must fit within uint16_t, or results are undefined
 */
static void
ushort2bcd(unsigned n, ...)
{
	va_list args;
	uint16_t val = n;
	int decade;
	uint8_t bcd;
	uint8_t *p;

	va_start(args, n);
	for (decade = 10000; decade >= 1; decade /= 10) {
		bcd = val / decade;
		p = va_arg(args, uint8_t *);
		if (p)
			*p = bcd;
		val %= decade;
	}
	va_end(args);
}

#ifdef MAIN
int
main(int argc, char **argv)
{
	uint16_t tests[] = {
		0, 1, 32768, 65534, 65535, 10, 10000
	};
	uint8_t tenthousands, thousands, hundreds, tens, units;
	int i;
	uint16_t val;

	for (i = 0; i < sizeof(tests)/sizeof(uint16_t); ++i) {
		ushort2bcd(tests[i], &tenthousands, &thousands, &hundreds, &tens, &units);
		val = tenthousands * 10000 +
			thousands * 1000 +
			hundreds * 100 +
			tens * 10 +
			units;
		if (val != tests[i])
			printf("test %d failed: got %d, want %d\n", i, val, tests[i]);
	}
}
#endif
