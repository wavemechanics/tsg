/*
 * tsgsgm -- 
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/timepps.h>
#include <sys/shm.h>
#include "../tsg/tsg.h"
#include "doy.h"

#define	UNIT 0

struct shmTime {
        int    mode; /* 0 - if valid is set:
                      *       use values,
                      *       clear valid
                      * 1 - if valid is set:
                      *       if count before and after read of data is equal:
                      *         use values
                      *       clear valid
                      */
        volatile int    count;
        time_t          clockTimeStampSec;
        int             clockTimeStampUSec;
        time_t          receiveTimeStampSec;
        int             receiveTimeStampUSec;
        int             leap;
        int             precision;
        int             nsamples;
        volatile int    valid;
        unsigned        clockTimeStampNSec;     /* Unsigned ns timestamps */
        unsigned        receiveTimeStampNSec;   /* Unsigned ns timestamps */
        int             dummy[8];
};

void
usage(int status)
{
	fprintf(stderr, "usage: %s -d <pps-device> [-u <unit>] [-v]\n", getprogname());
	exit(status);
}

int
main(int argc, char **argv)
{
	int c;
	char *device = NULL;
	int fd;
	long n;
	int unit = 0;
	int verbose = 0;
	pps_handle_t handle;
	pps_params_t params;

	while ((c = getopt(argc, argv, "d:hu:v")) != -1) {
		switch (c) {
		case 'd':
			device = optarg;
			break;
		case 'u':
			n = strtol(optarg, NULL, 10);
			if (n == 0 && errno != 0) {
				perror("unit: ");
				exit(2);
			}
			if (n < 0 || n > 10)
				usage(2);
			unit = n;
			break;
		case 'v':
			verbose = 1;
			break;
		case '?':
		case 'h':
			usage(0);
		default:
			usage(2);
		}
	}

	if (device == NULL)
		usage(2);

	if ((fd = open(device, O_RDWR, 0)) == -1) {
		perror("open");
		exit(1);
	}
	if (time_pps_create(fd, &handle) != 0) {
		perror("time_pps_create");
		exit(1);
	}
	if (time_pps_getparams(handle, &params) != 0) {
		perror("time_pps_getparams");
		exit(1);
	}
	params.mode |= PPS_CAPTUREASSERT;
	if (time_pps_setparams(handle, &params) != 0) {
		perror("time_pps_params");
		exit(1);
	}

	int shmid = shmget(0x4e545030 + unit, sizeof(struct shmTime), 0);
	if (shmid == -1) {
		perror("shmget");
		exit(1);
	}

	struct shmTime *shmp = shmat(shmid, NULL, 0);
	if (shmp == (struct shmTime *)-1) {
		perror("shmat");
		exit(1);
	}

	shmp->mode = 1;
	shmp->count = 0;
	shmp->leap = 0;
	shmp->precision = -1;	// ???
	shmp->nsamples = 3;	// still used?
	shmp->valid = 0;

	for (;;) {
		pps_info_t info;
		struct tsg_time t;

		if (time_pps_fetch(handle, PPS_TSFMT_TSPEC, &info, NULL) != 0) {
			perror("time_pps_fetch");
			exit(1);
		}
		if (ioctl(fd, TSG_GET_LATCHED_TIME, &t) != 0) {
			perror("ioctl");
			exit(1);
		}

		// convert board day of month month and day
		int mon, day;
		doy2monthday(t.year, t.day, &mon, &day);

		// convert board time to seconds since epoch
		struct tm tm = {
			.tm_year = t.year - 1900,
			.tm_mon = mon - 1,
			.tm_mday = day,
			.tm_hour = t.hour,
			.tm_min = t.min,
			.tm_sec = t.sec,
			.tm_isdst = 0,
			.tm_zone = NULL,
			.tm_gmtoff = 0
		};
		
		struct timespec brd = {
			.tv_sec = timegm(&tm),
			.tv_nsec = t.nsec
		};

		// populate SHM for ntpd to pick up
		// need memory barriers?
		shmp->valid = 0;
		shmp->receiveTimeStampSec = info.assert_timestamp.tv_sec;
		shmp->receiveTimeStampUSec = info.assert_timestamp.tv_nsec / 1000;
		shmp->receiveTimeStampNSec = info.assert_timestamp.tv_nsec;

		shmp->clockTimeStampSec = brd.tv_sec;
		shmp->clockTimeStampUSec = brd.tv_nsec / 1000;
		shmp->clockTimeStampNSec = brd.tv_nsec;

		shmp->count++;
		shmp->valid = 1;

		if (verbose) {
			printf("assert %d count %d\n", info.assert_sequence, shmp->count);
			printf("\tsys: %d.%09ld\n", info.assert_timestamp.tv_sec, info.assert_timestamp.tv_nsec);
			printf("\tbrd: %d.%09ld\n", brd.tv_sec, brd.tv_nsec);
			struct timespec diff;
			timespecsub(&brd, &info.assert_timestamp, &diff);
			printf("\tdif: %02d.%09ld\n", diff.tv_sec, diff.tv_nsec);
		}
	}

	exit(0);
}
