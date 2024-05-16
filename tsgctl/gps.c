#include <stdio.h>
#include "node.h"
#include "tsglib.h"
#include "gps.h"

static int
get_antenna(int fd)
{
	uint8_t status;

	if (tsg_get_gps_antenna_status(fd, &status) != 0)
		return -1;
	printf("antenna shorted: %s\n", status & TSG_GPS_ANTENNA_SHORTED ? "yes" : "no");
	printf("antenna open: %s\n", status & TSG_GPS_ANTENNA_OPEN ? "yes" : "no");
	return 0;
}

static int
get_position(int fd)
{
	struct tsg_position pos;

	if (tsg_get_gps_position(fd, &pos) != 0)
		return -1;
	printf(
		"gps position: %do%d'%d.%d\"%c %do%d'%d.%d\"%c %c%d.%dm\n",
		pos.lat_deg,
		pos.lat_min,
		pos.lat_sec,
		pos.lat_decisec,
		pos.lat_dir,
		pos.lon_deg,
		pos.lon_min,
		pos.lon_sec,
		pos.lon_decisec,
		pos.lon_dir,
		pos.elev_sign,
		pos.elev_meter,
		pos.elev_decimeter
        );
	return 0;
}

static int
get_signal(int fd)
{
	struct tsg_signal sig;
	struct tsg_satellite *sat;
	int i;

	if (tsg_get_gps_signal(fd, &sig) != 0)
		return -1;

	printf("SV signal\n");
	printf("-- ------\n");
	for (i = 0; i < TSG_MAX_SATELLITES; ++i) {
		sat = &sig.satellites[i];
		printf("%02d %2d.%02d\n", sat->sv, sat->level, sat->centilevel);
	}
	return 0;
}

int
get_gps(int fd)
{
	static struct node params[] = {
		{ "antenna", "antenna cable status", get_antenna},
		{ "position", "antenna position", get_position },
		{ "signal", "satellite signal strengths", get_signal},
		{ NULL, NULL, NULL },
	};

	return walk(params, fd);
}
