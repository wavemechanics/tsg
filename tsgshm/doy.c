#ifdef MAIN
#include <stdio.h>
#include <stdlib.h>
#endif

#include "doy.h"

void
doy2monthday(int year, int doy, int *monp, int *dayp)
{
	static int mdays[2][13] = {
		{ 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
		{ 0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
	};

	int isleap = ((year%4 == 0) && (year%100 != 0)) || (year%400 == 0);

	int left = doy;
	int mon;
	for (mon = 1; mon <= 12; ++mon) {
		if (left <= mdays[isleap][mon])
			break;
		left -= mdays[isleap][mon];
	}
	*monp = mon;
	*dayp = left;
}

#ifdef MAIN
int
main(int argc, char **argv)
{
	int mon, day;

	doy2monthday(2024, 151, &mon, &day);
	printf("%d %d\n", mon, day);
	exit(0);
}
#endif
