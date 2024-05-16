#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
//#include <sys/ioctl.h>
#include "gettok.h"
#include "action.h"

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-d <device>] <action> <component> [<value>]\n", getprogname());
	exit(2);
}

int
main(int argc, char **argv)
{
	char *dev = "/dev/tsg0";
	int c;
	int fd;
	int status;

	while ((c = getopt(argc, argv, "d:")) != -1) {
		switch (c) {
		case 'd':
			dev = optarg;
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if ((fd = open(dev, O_RDONLY)) == -1) {
		perror("open");
		exit(1);
	}

	init_gettok(argc, argv);

	status = action(fd);
	if (status != 0) {
		perror("");
		exit(1);
	}
	exit(0);
}

