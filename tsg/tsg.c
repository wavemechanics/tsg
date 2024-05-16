/*
 * tsg.c -- Truetime/Symmetricom PCI timecode generator cards - 560-5900 series
 *
 *  560-5900 PCI-SG
 *  560-5901 GPS-PCI
 *  560-5907-U PCI-SG-2U
 *  560-5908-U GPS-PCI-2U
 */

#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <machine/stdarg.h>

#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "tsg.h"

/* PCI identification */
#define	PCI_VENDOR_PLX		0x10b5
#define	PCI_DEVICE_TSG		0x9050
#define	PCI_SUBVENDOR_TRUETIME	0x12da

/* PCI BARs */
#define	BAR_LCR		0	/* BAR0: "Local Configuration Registers" */
#define BAR_REGISTERS	2	/* BAR2: regular card registers */

/* register addresses */
#define	REG_HARDWARE_STATUS	0xfe
#define	REG_LOCK_STATUS		0x105

struct tsg_softc {
	device_t	device;
	struct cdev	*cdev;

	struct mtx	mtx;

	int		lcr_rid;
	struct resource	*lcr_resource;

	int		registers_rid;
	struct resource	*registers_resource;

	uint16_t	model;	/* from subdevice id */
	bool		new_model;
	bool		has_gps;

	uint8_t		buf[6*4];	// largest I/O is 6 32 bit words

	uint8_t		major;
	uint8_t		minor;
	uint8_t		test;
};

static d_open_t		tsg_open;
static d_close_t	tsg_close;
static d_ioctl_t	tsg_ioctl;

static struct cdevsw tsg_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	tsg_open,
	.d_close =	tsg_close,
	.d_ioctl =	tsg_ioctl,
	.d_name = 	"tsg"
};

static devclass_t tsg_devclass;

#define	lock(sc)	mtx_lock(&(sc)->mtx)
#define	unlock(sc)	mtx_unlock(&(sc)->mtx)

#include "pack.c"

static char *
model2desc(int model)
{
	switch (model) {
	case TSG_MODEL_PCI_SG:
		return "TrueTime 560-5900 PCI-SG";
	case TSG_MODEL_GPS_PCI:
		return "TrueTime 560-5901 GPS PCI";
	case TSG_MODEL_PCI_SG_2U:
		return "Symmetricom 560-5907-U PCI-SG-2U";
	case TSG_MODEL_GPS_PCI_2U:
		return "Symmetricom 560-5908-U GPS-PCI-2U";
	}
	return NULL;
}

static int
tsg_probe(device_t dev)
{
	char *desc;

	if (pci_get_vendor(dev) != PCI_VENDOR_PLX ||
	    pci_get_device(dev) != PCI_DEVICE_TSG ||
	    pci_get_subvendor(dev) != PCI_SUBVENDOR_TRUETIME)
		return ENXIO;

	desc = model2desc(pci_get_subdevice(dev));
	if (desc == NULL)
		return ENXIO;

	device_set_desc(dev, desc);

	/* We have to set the probe() priority a little higher than default to
	 * prevent the wi driver grabbing this device. But we're not the vendor, so
	 * don't claim vendor priority.
	 */
	return (BUS_PROBE_VENDOR+BUS_PROBE_DEFAULT)/2;
}

static int
tsg_modevent(module_t mod __unused, int event, void *arg __unused)
{
	int error = 0;

	switch (event) {
	case MOD_LOAD:
	case MOD_UNLOAD:
	case MOD_SHUTDOWN:
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}

	return error;
}

static void
release_resources(struct tsg_softc *sc)
{
	if (sc->lcr_resource) {
		bus_release_resource(
			sc->device,
			SYS_RES_MEMORY,
			sc->lcr_rid,
			sc->lcr_resource
		);
		sc->lcr_resource = NULL;
	}

	if (sc->registers_resource) {
		bus_release_resource(
			sc->device,
			SYS_RES_MEMORY,
			sc->registers_rid,
			sc->registers_resource
		);
		sc->registers_resource = NULL;
	}

	mtx_destroy(&sc->mtx);
}

static int
tsg_attach(device_t dev)
{
	struct tsg_softc *sc = device_get_softc(dev);
	int unit;

	sc->device = dev;
	sc->lcr_resource = NULL;
	sc->registers_resource = NULL;

	mtx_init(&sc->mtx, "tsg", NULL, MTX_DEF);

	/* arrange to talk to LCR */
	sc->lcr_rid = PCIR_BAR(BAR_LCR);
	sc->lcr_resource = bus_alloc_resource_any(
		dev,
		SYS_RES_MEMORY,
		&sc->lcr_rid,
		RF_ACTIVE
	);
	if (!sc->lcr_resource) {
		release_resources(sc);
		device_printf(dev, "cannot allocate lcr resource\n");
		return ENXIO;
	}

	/* arrange to talk to normal card registers area */
	sc->registers_rid = PCIR_BAR(BAR_REGISTERS);
	sc->registers_resource = bus_alloc_resource_any(
		dev,
		SYS_RES_MEMORY,
		&sc->registers_rid,
		RF_ACTIVE
	);
	if (!sc->registers_resource) {
		release_resources(sc);
		device_printf(dev, "cannot allocate registers resource\n");
		return ENXIO;
	}

	sc->model = pci_get_subdevice(dev);
	sc->new_model = sc->model == TSG_MODEL_PCI_SG_2U || sc->model == TSG_MODEL_GPS_PCI_2U;
	sc->has_gps = sc->model == TSG_MODEL_GPS_PCI || sc->model == TSG_MODEL_GPS_PCI_2U;

	unit = device_get_unit(dev);
	sc->cdev = make_dev(&tsg_cdevsw, unit, UID_ROOT, GID_WHEEL, 0600, "tsg%d", unit);
	sc->cdev->si_drv1 = sc;

	/* Only the new board support the firmware registers.
	 * 0x1bc c major
	 * 0x1bd c minor
	 * 0x1be c test
	 */
	if (!sc->new_model)
		sc->major = sc->minor = sc->test = 0;
	else {
		char *fmt = "ccc";
		bus_read_region_1(sc->registers_resource, 0x1bc, sc->buf, packlen(fmt));
		unpack(sc->buf, fmt, &sc->major, &sc->minor, &sc->test);
		device_printf(sc->device, "firmware: %d.%d.%d\n", sc->major, sc->minor, sc->test);
	}

	return 0;
}

static int
tsg_detach(device_t dev)
{
	struct tsg_softc *sc = device_get_softc(dev);

	destroy_dev(sc->cdev);
	release_resources(sc);
	return 0;
}

static int
tsg_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	//struct tsg_softc *sc = dev->si_drv1;

	return 0;
}

static int
tsg_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	//struct tsg_softc *sc = dev->si_drv1;

	return 0;
}

#if 0
static void
print_buf(struct tsg_softc *sc, uint8_t *buf, int len)
{
	while (len-- > 0)
		device_printf(sc->device, "%d ", *buf++);
	device_printf(sc->device, "\n");
}
#endif


static int
tsg_get_board_model(struct tsg_softc *sc, caddr_t arg)
{
	uint16_t *argp = (uint16_t *)arg;

	*argp = sc->model;
	return 0;
}

static int
tsg_get_board_firmware(struct tsg_softc *sc, caddr_t arg)
{
	struct tsg_board_firmware *argp = (struct tsg_board_firmware *)arg;

	argp->major = sc->major;
	argp->minor = sc->minor;
	argp->test = sc->test;

	return 0;
}

static int
tsg_get_board_test_status(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;

	/* 0x11c Background Test Status register (lower nibble) */
	char *fmt = "n";

	lock(sc);
        bus_read_region_1(sc->registers_resource, 0x11c, sc->buf, packlen(fmt));
        unpack(sc->buf, fmt, NULL, argp);	
	unlock(sc);

	return 0;
}

static int
tsg_get_board_j1(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;

	/* On old boards J1 (CODE OUT) is fixed at IRIG-B AM. */
	/* On new boards 0x12f is the J1 source register. */
	if (!sc->new_model) {
		*argp = TSG_J1_IRIG_B_AM;
		return 0;
	}
	lock(sc);
	bus_read_region_1(sc->registers_resource, 0x12f, sc->buf, 1);
	*argp = sc->buf[0];
	unlock(sc);
	return 0;
}

static int
tsg_set_board_j1(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;

	/* Cannot change J1 on old boards; it is always IRIG-B AM. */
	/* 0x12f is the J1 source register on new boards. */
	if (!sc->new_model)
		return *argp == TSG_J1_IRIG_B_AM ? 0 : ENODEV;
	switch (*argp) {
	case TSG_J1_IRIG_B_AM:
	case TSG_J1_IRIG_B_DC:
	case TSG_J1_PULSE:
	case TSG_J1_SYNTH:
	case TSG_J1_STROBE:
	case TSG_J1_1PPS:
		break;
	default:
		return EINVAL;
	}

	lock(sc);
	bus_write_region_1(sc->registers_resource, 0x12f, argp, 1);
	unlock(sc);
	return 0;
}

static int
tsg_get_pulse_freq(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;
	char *fmt = "n";

	/* the upper nibble of 0x11b is the pulse frequency register */
	lock(sc);
	bus_read_region_1(sc->registers_resource, 0x11b, sc->buf, packlen(fmt));
	unpack(sc->buf, fmt, argp, NULL);
	unlock(sc);
	return 0;
}

static int
tsg_set_pulse_freq(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;
	char *fmt = "n";

	/* old boards do not support higher than 10kHz */
	/* 0x11b is the pulse frequency register */
	switch (*argp) {
	case TSG_PULSE_FREQ_DISABLED:
	case TSG_PULSE_FREQ_1HZ:
	case TSG_PULSE_FREQ_10HZ:
	case TSG_PULSE_FREQ_100HZ:
	case TSG_PULSE_FREQ_1KHZ:
	case TSG_PULSE_FREQ_10KHZ:
		break;
	case TSG_PULSE_FREQ_100KHZ:
	case TSG_PULSE_FREQ_1MHZ:
	case TSG_PULSE_FREQ_5MHZ:
	case TSG_PULSE_FREQ_10MHZ:
		if (!sc->new_model)
			return ENODEV;
		break;
	default:
		return EINVAL;
	}

	lock(sc);
	pack(sc->buf, fmt, *argp, 0);
	bus_write_region_1(sc->registers_resource, 0x11b, sc->buf, packlen(fmt));
	unlock(sc);
	return 0;
}

static int
tsg_get_gps_antenna_status(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;
	char *fmt = "n";

	if (!sc->has_gps)
		return EOPNOTSUPP;

	/* antenna status is upper nibble of Hardware Status register */
	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_HARDWARE_STATUS, sc->buf, packlen(fmt));
	unpack(sc->buf, fmt, argp, NULL);
	unlock(sc);

	/* the antenna status bits are the opposite of what we expect
	 * (eg 0 means shorted)
	 * so here we flip all the bits, and then only keep the antenna status ones.
	 */
	*argp = ~(*argp) & (TSG_GPS_ANTENNA_SHORTED|TSG_GPS_ANTENNA_OPEN);
	return 0;
}

static int
tsg_get_clock_lock(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;
	char *fmt = "n";

	/* lock status is upper nibble of lock status register */
	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_LOCK_STATUS, sc->buf, packlen(fmt));
	unpack(sc->buf, fmt, argp, NULL);
	unlock(sc);

	*argp &= (TSG_CLOCK_PHASE_LOCK|TSG_CLOCK_INPUT_VALID|TSG_CLOCK_GPS_LOCK);
	return 0;
}

static int
tsg_get_clock_timecode(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;
	char *fmt = "c";

	/* timecode is in register 0x119 */
	lock(sc);
	bus_read_region_1(sc->registers_resource, 0x119, sc->buf, packlen(fmt));
	unpack(sc->buf, fmt, argp);
	unlock(sc);

	*argp &= TSG_CLOCK_TIMECODE_MASK;
	return 0;
}

static int
tsg_set_clock_timecode(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;

	switch (*argp) {
	case TSG_CLOCK_TIMECODE_IRIG_A_AM:
	case TSG_CLOCK_TIMECODE_IRIG_A_DC:
	case TSG_CLOCK_TIMECODE_IRIG_B_AM:
	case TSG_CLOCK_TIMECODE_IRIG_B_DC:
		break;
	default:
		return ENODEV;
	}

	lock(sc);
	bus_write_region_1(sc->registers_resource, 0x119, argp, 1);
	unlock(sc);
	return 0;
}

static int
tsg_get_clock_ref(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;

	/* 0x118 is the Configration Register */
	lock(sc);
	bus_read_region_1(sc->registers_resource, 0x118, argp, 1);
	unlock(sc);

	*argp &= TSG_CLOCK_REF_MASK;
	return 0;
}

static int
tsg_set_clock_ref(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;

	/* 0x118 is the Configuration Register */
	switch (*argp) {
	case TSG_CLOCK_REF_GEN:
	case TSG_CLOCK_REF_1PPS:
	case TSG_CLOCK_REF_TIMECODE:
		break;
	case TSG_CLOCK_REF_GPS:
		if (!sc->has_gps)
			return EOPNOTSUPP;
		break;
	}

	lock(sc);
	bus_read_region_1(sc->registers_resource, 0x118, sc->buf, 1);
	sc->buf[0] &= ~TSG_CLOCK_REF_MASK;	// clear ref bits
	sc->buf[0] |= *argp;			// set new ref bits
	bus_write_region_1(sc->registers_resource, 0x118, sc->buf, 1);
	unlock(sc);
	return 0;
}

static int
tsg_get_clock_time(struct tsg_softc *sc, caddr_t arg)
{
	struct tsg_time *argp = (struct tsg_time *)arg;
	char *fmt = "nnnn nnnn nnnn";
	uint8_t thousands_year, hundreds_year, tens_year, units_year;
	uint8_t hundreds_day, tens_day, units_day;
	uint8_t tens_hour, units_hour;
	uint8_t tens_min, units_min;
	uint8_t tens_sec, units_sec;
	uint8_t hundreds_milli, tens_milli, units_milli;
	uint8_t hundreds_micro, tens_micro, units_micro;
	uint8_t hundreds_nano;

	lock(sc);
	bus_write_region_1(sc->registers_resource, 0xfc, sc->buf, 1);
	//bus_barrier(sc->registers_resource, 0xfc, packlen(fmt),
	//	BUS_SPACE_BARRIER_READ|BUS_SPACE_BARRIER_WRITE);
	bus_read_region_1(sc->registers_resource, 0xfc, sc->buf, packlen(fmt));
	unpack(sc->buf, fmt,
		&tens_micro,     &units_micro,
		&units_milli,    &hundreds_micro,
		NULL,            NULL,
		&hundreds_nano,  NULL,
		&hundreds_milli, &tens_milli,
		&tens_sec,       &units_sec,
		&tens_min,       &units_min,
		&tens_hour,      &units_hour,
		&tens_day,       &units_day,
		NULL,            &hundreds_day,
		&tens_year,      &units_year,
		&thousands_year, &hundreds_year
	);
	unlock(sc);

	argp->year = thousands_year * 1000 +
		     hundreds_year * 100 +
		     tens_year * 10 +
		     units_year;
	argp->day = hundreds_day * 100 +
		    tens_day * 10 +
		    units_day;
	argp->hour = tens_hour * 10 + units_hour;
	argp->min = tens_min * 10 + units_min;
	argp->sec = tens_sec * 10 + units_sec;
	argp->nsec = hundreds_milli * 100000000 +
		     tens_milli *      10000000 +
		     units_milli *      1000000 +
		     hundreds_micro *    100000 +
		     tens_micro *         10000 +
		     units_micro *         1000;
	if (sc->new_model)
		argp->nsec += hundreds_nano * 100;
	return 0;
}

static int
tsg_get_gps_position(struct tsg_softc *sc, caddr_t arg)
{
	struct tsg_position *argp = (struct tsg_position *)arg;
	char *fmt = "nnncnn nnncnn ncnn";
	uint8_t lat_hundreds_deg, lat_tens_deg, lat_units_deg;
	uint8_t lat_tens_min, lat_units_min;
	uint8_t lat_tens_sec, lat_units_sec, lat_decisec;
	uint8_t lat_dir;
	uint8_t lon_hundreds_deg, lon_tens_deg, lon_units_deg;
	uint8_t lon_tens_min, lon_units_min;
	uint8_t lon_tens_sec, lon_units_sec, lon_decisec;
	uint8_t lon_dir;
	uint8_t elev_tens_km, elev_unit_km;
	uint8_t elev_hundreds_meter, elev_tens_meter, elev_units_meter, elev_decimeter;
	uint8_t elev_sign;

	uint8_t prev_buf[packlen(fmt)];
	memset(prev_buf, 0, packlen(fmt));

	lock(sc);
	/* The old model requires writing something to 0xfc to freeze the position
	 * registers.
	 * For the new model, the position registers can be read any time, but should
	 * be read twice and compared to make sure the values aren't changing.
	 */
	if (!sc->new_model) {
		bus_write_region_1(sc->registers_resource, 0xfc, sc->buf, 1);
		bus_read_region_1(sc->registers_resource, 0x108, sc->buf, packlen(fmt));
	} else {
		int tries = 0;
		while (tries++ < 10) {
			bus_read_region_1(sc->registers_resource, 0x108, sc->buf, packlen(fmt));
			if (memcmp(sc->buf, prev_buf, packlen(fmt)) == 0)
				break;
			memcpy(prev_buf, sc->buf, packlen(fmt));
		}
		if (tries == 10) {
			unlock(sc);
			return EIO;
		}
	}

	unpack(sc->buf, fmt,
		&lat_tens_deg,        &lat_units_deg,
		NULL,                 &lat_hundreds_deg,
		&lat_tens_min,        &lat_units_min,
		&lat_dir,
		NULL,                 &lat_decisec,
		&lat_tens_sec,        &lat_units_sec,
		&lon_tens_deg,        &lon_units_deg,
		NULL,                 &lon_hundreds_deg,
		&lon_tens_min,        &lon_units_min,
		&lon_dir,
		NULL,                 &lon_decisec,
		&lon_tens_sec,        &lon_units_sec,
		&elev_tens_km,        &elev_unit_km,
		&elev_sign,
		&elev_units_meter,    &elev_decimeter,
		&elev_hundreds_meter, &elev_tens_meter
	);

	unlock(sc);

	argp->lat_deg = lat_hundreds_deg * 100 +
		        lat_tens_deg * 10 +
			lat_units_deg;
	argp->lat_min = lat_tens_min * 10 +
		        lat_units_min;
	argp->lat_sec = lat_tens_sec * 10 +
	                lat_units_sec;
	argp->lat_decisec = lat_decisec;
	argp->lat_dir = lat_dir;

	argp->lon_deg = lon_hundreds_deg * 100 +
		        lon_tens_deg * 10 +
			lon_units_deg;
	argp->lon_min = lon_tens_min * 10 +
		        lon_units_min;
	argp->lon_sec = lon_tens_sec * 10 +
	                lon_units_sec;
	argp->lon_decisec = lon_decisec;
	argp->lon_dir = lon_dir;

	argp->elev_meter = elev_tens_km * 10000 +
		           elev_unit_km * 1000 +
			   elev_hundreds_meter * 100 +
			   elev_tens_meter * 10 +
			   elev_units_meter;
	argp->elev_decimeter = elev_decimeter;
	argp->elev_sign = elev_sign;

	return 0;
}

static int
tsg_get_gps_signal(struct tsg_softc *sc, caddr_t arg)
{
	struct tsg_signal *argp = (struct tsg_signal *)arg;
	uint8_t updating;
	char *fmt = "ncnn";

	lock(sc);
	int tries = 0;
	while (tries++ < 10) {
		bus_read_region_1(sc->registers_resource, 0x1b0, &updating, 1);
		if (updating)
			continue;
		bus_read_region_1(
			sc->registers_resource,
			0x198,
			sc->buf,
			packlen(fmt) * TSG_MAX_SATELLITES
		);
		bus_read_region_1(sc->registers_resource, 0x1b0, &updating, 1);
		if (updating)
			continue;
		break;
	}
	if (tries == 10) {
		unlock(sc);
		return EIO;
	}

	int i;
	uint8_t *buf = sc->buf;
	struct tsg_satellite *sat;
	uint8_t tens_sv, unit_sv;
	uint8_t tens_signal, units_signal;
	uint8_t tenth_signal, hundredth_signal;

	for (i = 0; i < TSG_MAX_SATELLITES; ++i) {
		buf = unpack(buf, fmt, 
			&tens_sv, &unit_sv,
			NULL,
			&tenth_signal, &hundredth_signal,
			&tens_signal, &units_signal
		);

		sat = argp->satellites + i;
		sat->sv = tens_sv * 10 + unit_sv;
		sat->level = tens_signal * 10 + units_signal;
		sat->centilevel = tenth_signal * 10 + hundredth_signal;
	}

	unlock(sc);
	return 0;
}

static int
tsg_get_timecode_agc_delays(struct tsg_softc *sc, caddr_t arg)
{
	struct tsg_agc_delays *argp = (struct tsg_agc_delays *)arg;
	char *fmt = "cccc";
	uint8_t irig_a_usec, irig_a_nsec;
	uint8_t irig_b_usec, irig_b_nsec;

	/* New boards support retrieving AGC delays in register 0x1b4 */
	if (!sc->new_model)
		return EOPNOTSUPP;

	lock(sc);
	bus_read_region_1(sc->registers_resource, 0x1b4, sc->buf, packlen(fmt));
	unpack(sc->buf, fmt, &irig_b_usec, &irig_b_nsec, &irig_a_usec, &irig_a_nsec);
	unlock(sc);

	argp->irig_a_nsec = irig_a_usec * 1000 + irig_a_nsec;
	argp->irig_b_nsec = irig_b_usec * 1000 + irig_b_nsec;

	return 0;
}

static int
tsg_ioctl(struct cdev *dev, u_long cmd, caddr_t arg, int fflag, struct thread *td)
{
	struct tsg_softc *sc = dev->si_drv1;
	int error = EOPNOTSUPP;

	switch (cmd) {
	case TSG_GET_BOARD_MODEL:
		error = tsg_get_board_model(sc, arg);
		break;
	case TSG_GET_BOARD_FIRMWARE:
		error = tsg_get_board_firmware(sc, arg);
		break;
	case TSG_GET_BOARD_TEST_STATUS:
		error = tsg_get_board_test_status(sc, arg);
		break;
	case TSG_GET_BOARD_J1:
		error = tsg_get_board_j1(sc, arg);
		break;
	case TSG_SET_BOARD_J1:
		error = tsg_set_board_j1(sc, arg);
		break;
	case TSG_GET_PULSE_FREQ:
		error = tsg_get_pulse_freq(sc, arg);
		break;
	case TSG_SET_PULSE_FREQ:
		error = tsg_set_pulse_freq(sc, arg);
		break;
	case TSG_GET_GPS_ANTENNA_STATUS:
		error = tsg_get_gps_antenna_status(sc, arg);
		break;
	case TSG_GET_CLOCK_LOCK:
		error = tsg_get_clock_lock(sc, arg);
		break;
	case TSG_GET_CLOCK_TIMECODE:
		error = tsg_get_clock_timecode(sc, arg);
		break;
	case TSG_SET_CLOCK_TIMECODE:
		error = tsg_set_clock_timecode(sc, arg);
		break;
	case TSG_GET_CLOCK_REF:
		error = tsg_get_clock_ref(sc, arg);
		break;
	case TSG_SET_CLOCK_REF:
		error = tsg_set_clock_ref(sc, arg);
		break;
	case TSG_GET_CLOCK_TIME:
		error = tsg_get_clock_time(sc, arg);
		break;
	case TSG_GET_GPS_POSITION:
		error = tsg_get_gps_position(sc, arg);
		break;
	case TSG_GET_GPS_SIGNAL:
		error = tsg_get_gps_signal(sc, arg);
		break;
	case TSG_GET_TIMECODE_AGC_DELAYS:
		error = tsg_get_timecode_agc_delays(sc, arg);
		break;
	}

	return error;
}

static device_method_t tsg_methods[] = {
	DEVMETHOD(device_probe, tsg_probe),
	DEVMETHOD(device_attach, tsg_attach),
	DEVMETHOD(device_detach, tsg_detach),
	{0, 0}
};

static driver_t tsg_driver = {
	"tsg",
	tsg_methods,
	sizeof(struct tsg_softc)
};

DEV_MODULE(tsg, tsg_modevent, NULL);

DRIVER_MODULE(
	tsg,
	pci,
	tsg_driver,
	tsg_devclass,
	NULL,
	NULL
);
