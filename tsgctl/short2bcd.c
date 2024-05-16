#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>

/* n has to be an int/unsigned because otherwise va_start moans.
 * But its value must fit within uint16_t, or results are undefined
 */
void
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
		val -= bcd * decade;
		p = va_arg(args, uint8_t *);
		if (p)
			*p = bcd;
	}
	va_end(args);
}

int
main(int argc, char **argv)
{
	uint8_t thousands, hundreds, tens, units;

	ushort2bcd(65535, NULL, &thousands, &hundreds, &tens, &units);
	//printf("%d\n", tenthousands);
	printf("%d\n", thousands);
	printf("%d\n", hundreds);
	printf("%d\n", tens);
	printf("%d\n", units);
}
