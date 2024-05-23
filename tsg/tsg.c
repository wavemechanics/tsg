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
#define	REG_CONFIG		0x118	// Configuration Register #1
#define		TSG_PRESET_TIME_READY	0x04
#define		TSG_PRESET_POS_READY	0x80
#define	REG_TIMECODE_QUALITY	0x11d
#define		TSG_QUALITY_NOT_LOCKED	0x80
#define	REG_TZ_OFFSET		0x120
#define	REG_PHASE_COMP		0x124
#define	REG_SYNTH_FREQ		0x128
#define	REG_MISC_CONTROL	0x12c
#define	REG_SYNTH_CONTROL	0x12d
#define		TSG_SYNTH_LOAD	0x02

#define	UNUSED(x)	(x) __attribute__((unused))

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
#include "ushort2bcd.c"

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

	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_CONFIG, argp, 1);
	unlock(sc);

	*argp &= TSG_CLOCK_REF_MASK;
	return 0;
}

static int
tsg_set_clock_ref(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;

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
	bus_read_region_1(sc->registers_resource, REG_CONFIG, sc->buf, 1);
	sc->buf[0] &= ~TSG_CLOCK_REF_MASK;	// clear ref bits
	sc->buf[0] |= *argp;			// set new ref bits
	bus_write_region_1(sc->registers_resource, REG_CONFIG, sc->buf, 1);
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

// Return true if t holds a valid time for the current reference source.
// Only verify the year for GPS and timecode sources, but verify the
// whole time for the rest of the sources.
static int
is_valid_time(unsigned ref, struct tsg_time *t)
{
	if (t->year < 1969 || t->year > 2038)	// arbitrary
		return 0;
	if (ref == TSG_CLOCK_REF_GPS || ref == TSG_CLOCK_REF_TIMECODE)
		return 1;
	if (t->day < 1 || t->day > 366)
		return 0;
	if (t->hour < 0 || t->hour > 23)
		return 0;
	if (t->min < 0 || t->min > 59)
		return 0;
	if (t->sec < 0 || t->sec > 60)
		return 0;
	if (t->nsec < 0 || t->nsec > 999999999)
		return 0;
	return 1;
}

static int
tsg_set_clock_time(struct tsg_softc *sc, caddr_t arg)
{
	struct tsg_time t = *(struct tsg_time *)arg;
	uint8_t ref;
	uint16_t milli;
	uint8_t thousands_year, hundreds_year, tens_year, units_year;
	uint8_t hundreds_day, tens_day, units_day;
	uint8_t tens_hour, units_hour;
	uint8_t tens_min, units_min;
	uint8_t tens_sec, units_sec;
	uint8_t hundreds_milli, tens_milli, units_milli;
	char *fmt;
	uint8_t control;

	lock(sc);

	// get current clock reference so we know which parts of the time to set
	bus_read_region_1(sc->registers_resource, REG_CONFIG, &ref, 1);
	ref &= TSG_CLOCK_REF_MASK;

	if (!is_valid_time(ref, &t)) {
		unlock(sc);
		return EINVAL;
	}

	switch (ref) {
	case TSG_CLOCK_REF_GEN:
		milli = t.nsec / 1000000;
		if (sc->new_model && milli != 0) {
			// new models use the msec fields as a countdown to the second
			// so calculate how many milliseconds to go,
			milli = 1000 - milli;
		}
		ushort2bcd(milli, NULL, NULL, &hundreds_milli, &tens_milli, &units_milli);
		fmt = "nn";
		pack(sc->buf, fmt, units_milli, 0, hundreds_milli, tens_milli);
		bus_write_region_1(sc->registers_resource, 0x159, sc->buf, packlen(fmt));
		// fallthrough

	case TSG_CLOCK_REF_1PPS:
		ushort2bcd(t.sec, NULL, NULL, NULL, &tens_sec, &units_sec);
		fmt = "n";
		pack(sc->buf, fmt, tens_sec, units_sec);
		bus_write_region_1(sc->registers_resource, 0x15b, sc->buf, packlen(fmt));

		ushort2bcd(t.min, NULL, NULL, NULL, &tens_min, &units_min);
		pack(sc->buf, fmt, tens_min, units_min);
		bus_write_region_1(sc->registers_resource, 0x15c, sc->buf, packlen(fmt));

		ushort2bcd(t.hour, NULL, NULL, NULL, &tens_hour, &units_hour);
		pack(sc->buf, fmt, tens_hour, units_hour);
		bus_write_region_1(sc->registers_resource, 0x15d, sc->buf, packlen(fmt));

		ushort2bcd(t.day, NULL, NULL, &hundreds_day, &tens_day, &units_day);
		fmt = "nn";
		pack(sc->buf, fmt, tens_day, units_day, 0, hundreds_day);
		bus_write_region_1(sc->registers_resource, 0x15e, sc->buf, packlen(fmt));
		// fallthrough

	case TSG_CLOCK_REF_GPS:
	case TSG_CLOCK_REF_TIMECODE:
		ushort2bcd(t.year, NULL, &thousands_year, &hundreds_year, &tens_year, &units_year);
		fmt = "nn";
		pack(sc->buf, fmt, tens_year, units_year, thousands_year, hundreds_year);
		bus_write_region_1(sc->registers_resource, 0x160, sc->buf, packlen(fmt));
		break;

	default:
		unlock(sc);
		return EIO;	// problem; we don't expect any other values
		break;
	}

	bus_read_region_1(sc->registers_resource, REG_CONFIG, &control, 1);
	control &= ~TSG_PRESET_POS_READY;	// always clear; we're not setting pos here
	control |= TSG_PRESET_TIME_READY;
	bus_write_region_1(sc->registers_resource, REG_CONFIG, &control, 1);

	// Wait for up to a second for TIME_READY to clear/
	// I've seen it take between 19mS and 190mS, so poll every 10mS.
	struct timeval tv = {
		.tv_sec = 0,
		.tv_usec = 10 * 1000,	// 10mS
	};
	int timo = tvtohz(&tv);

	// wait for TIME_READY to clear
	int tries;
	for (tries = 0; tries < 100; ++tries) {
		bus_read_region_1(sc->registers_resource, REG_CONFIG, &control, 1);
		if ((control & TSG_PRESET_TIME_READY) == 0)
			break;
		pause("timrdy", timo);
	}
	unlock(sc);
	return tries >= 100 ? ETIMEDOUT : 0;
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
tsg_get_clock_leap(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *) arg;

	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_MISC_CONTROL, argp, 1);
	unlock(sc);
	*argp &= TSG_INSERT_LEAP;
	return 0;
}

static int
tsg_set_clock_leap(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;

	if (*argp != 0 && *argp != TSG_INSERT_LEAP)
		return ENODEV;

	lock(sc);

	if (!sc->new_model)
		sc->buf[0] = 0;
	else {
		/* New boards support 'use TQ' in REG_MISC_CONTROL register */
		bus_read_region_1(sc->registers_resource, REG_MISC_CONTROL, sc->buf, 1);
		sc->buf[0] &= TSG_USE_TQ;
	}
	sc->buf[0] |= *argp;

	bus_write_region_1(sc->registers_resource, REG_MISC_CONTROL, sc->buf, 1);
	unlock(sc);

	return 0;
}

static int
tsg_get_clock_dac(struct tsg_softc *sc, caddr_t arg)
{
	uint16_t *argp = (uint16_t *)arg;
	char *fmt = "s";

	lock(sc);
	/* 0x11e is the low byte, and 0x11f is the high byte */
	bus_read_region_1(sc->registers_resource, 0x11e, sc->buf, packlen(fmt));
	unpack(sc->buf, fmt, argp);
	unlock(sc);

	return 0;
}

static int
tsg_save_clock_dac(struct tsg_softc *sc, caddr_t UNUSED(arg))
{
	uint8_t buf;

	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_MISC_CONTROL, &buf, 1);
	buf &= (TSG_USE_TQ | TSG_INSERT_LEAP);	// clear all except use TQ and insert leap bits
	buf |= TSG_SAVE_DAC;
	bus_write_region_1(sc->registers_resource, REG_MISC_CONTROL, &buf, 1);
	unlock(sc);

	return 0;
}

static int
tsg_get_clock_dst(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;

	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_CONFIG, argp, 1);
	unlock(sc);
	*argp &= TSG_CLOCK_DST_ENABLE;

	return 0;
}

static int
tsg_set_clock_dst(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;
	uint8_t buf;

	if (*argp != 0 && *argp != TSG_CLOCK_DST_ENABLE)
		return ENODEV;

	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_CONFIG, &buf, 1);
	// clear the preset and dst bits
	buf &= ~(TSG_PRESET_TIME_READY | TSG_PRESET_POS_READY | TSG_CLOCK_DST_ENABLE);
	// set the dst bit
	buf |= *argp;
	bus_write_region_1(sc->registers_resource, REG_CONFIG, &buf, 1);
	unlock(sc);

	return 0;
}

static int
tsg_get_clock_stop(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;

	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_CONFIG, argp, 1);
	unlock(sc);
	*argp &= TSG_CLOCK_STOP;

	return 0;
}

static int
tsg_set_clock_stop(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;
	uint8_t buf;

	if (*argp != 0 && *argp != TSG_CLOCK_STOP)
		return ENODEV;

	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_CONFIG, &buf, 1);
	// clear the preset and clock stop bits
	buf &= ~(TSG_PRESET_TIME_READY | TSG_PRESET_POS_READY | TSG_CLOCK_STOP);
	// set the generator stop bit
	buf |= *argp;
	bus_write_region_1(sc->registers_resource, REG_CONFIG, &buf, 1);
	unlock(sc);

	return 0;
}

static int
tsg_get_clock_tz_offset(struct tsg_softc *sc, caddr_t arg)
{
	struct tsg_tz_offset *argp = (struct tsg_tz_offset *)arg;
	char *fmt = "n n c c";
	uint8_t tens_min, units_min;
	uint8_t tens_hour, units_hour;
	uint8_t sign, dummy;

	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_TZ_OFFSET, sc->buf, packlen(fmt));
	unpack(sc->buf, fmt,
		&tens_min,	&units_min,
		&tens_hour,	&units_hour,
		&sign,
		&dummy
	);
	unlock(sc);

	argp->sign = sign;
	argp->hour = tens_hour * 10 + units_hour;
	argp->min = tens_min * 10 + units_min;

	return 0;
}

static int
tsg_set_clock_tz_offset(struct tsg_softc *sc, caddr_t arg)
{
	struct tsg_tz_offset *argp = (struct tsg_tz_offset *)arg;
	char *fmt = "n n c c";
	uint8_t tens_min, units_min;
	uint8_t tens_hour, units_hour;

	if (argp->sign != '-' && argp->sign != '+')
		return ENODEV;
	if (argp->hour > 12)
		return ENODEV;
	if (argp->min > 59)
		return ENODEV;

	ushort2bcd(argp->hour, NULL, NULL, NULL, &tens_hour, &units_hour);
	ushort2bcd(argp->min, NULL, NULL, NULL, &tens_min, &units_min);

	lock(sc);
	pack(sc->buf, fmt,
		tens_min,	units_min,
		tens_hour,	units_hour,
		argp->sign,
		0
	);
	bus_write_region_1(sc->registers_resource, REG_TZ_OFFSET, sc->buf, packlen(fmt));
	unlock(sc);

	return 0;
}

static int
tsg_get_clock_phase_compensation(struct tsg_softc *sc, caddr_t arg)
{
	int32_t *argp = (int32_t *)arg;
	char *fmt = "S c";
	int16_t usec;
	uint8_t hnsec;

	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_PHASE_COMP, sc->buf, packlen(fmt));
	unpack(sc->buf, fmt, &usec, &hnsec);
	unlock(sc);

	uprintf("usec=%d nsec=%d\n", usec, hnsec);
	*argp = usec * 1000;
	if (!sc->new_model)
		return 0;

	if (usec < 0)
		*argp -= hnsec * 100;
	else
		*argp += hnsec * 100;
	return 0;
}

static int
tsg_set_clock_phase_compensation_old(struct tsg_softc *sc, caddr_t arg)
{
	int32_t *argp = (int32_t *)arg;
	int16_t usec;
	char *fmt = "S";

	usec = *argp / 1000;	// argp is in nanoseconds
	if (usec < -1000 || usec > 1000)
		return ENODEV;

	lock(sc);
	pack(sc->buf, fmt, usec);
	bus_write_region_1(sc->registers_resource, REG_PHASE_COMP, sc->buf, packlen(fmt));
	unlock(sc);

	return 0;
}

static int
tsg_set_clock_phase_compensation_new(struct tsg_softc *sc, caddr_t arg)
{
	int32_t *argp = (int32_t *)arg;
	int16_t usec;
	int8_t hnsec;
	char *fmt = "S c";

	usec = *argp / 1000;	// argp is in nanoseconds
	if (usec < -800 || usec > 800)
		return ENODEV;

	hnsec = (*argp % 1000) / 100;
	if (hnsec < 0)
		hnsec = -hnsec;

	lock(sc);
	pack(sc->buf, fmt, usec, hnsec);
	bus_write_region_1(sc->registers_resource, REG_PHASE_COMP, sc->buf, packlen(fmt));
	unlock(sc);

	return 0;
}

static int
tsg_set_clock_phase_compensation(struct tsg_softc *sc, caddr_t arg)
{
	if (sc->new_model)
		return tsg_set_clock_phase_compensation_new(sc, arg);
	else
		return tsg_set_clock_phase_compensation_old(sc, arg);
}

static int
tsg_get_timecode_quality(struct tsg_softc *sc, caddr_t arg)
{
	struct tsg_timecode_quality *argp = (struct tsg_timecode_quality *)arg;
	char *fmt = "n";
	uint8_t buf;

	if (!sc->new_model)
		return EOPNOTSUPP;

	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_TIMECODE_QUALITY, &buf, packlen(fmt));
	unlock(sc);

	unpack(&buf, fmt, &argp->level, &argp->locked);
	argp->locked &= TSG_QUALITY_NOT_LOCKED;		// clear unrelated bits
	argp->locked |= ~TSG_QUALITY_NOT_LOCKED;	// flip not locked bit
	switch (argp->level) {
	case 0x01:
		argp->level = 1;
		break;
	case 0x02:
		argp->level = 2;
		break;
	case 0x04:
		argp->level = 3;
		break;
	case 0x08:
		argp->level = 4;
		break;
	default:
		argp->level = 0;
		break;
	}
	return 0;
}

static int
tsg_get_use_timecode_quality(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;

	if (!sc->new_model)
		return EOPNOTSUPP;

	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_MISC_CONTROL, argp, 1);
	unlock(sc);

	*argp &= TSG_USE_TQ;
	return 0;
}

static int
tsg_set_use_timecode_quality(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;
	uint8_t buf;

	if (!sc->new_model)
		return EOPNOTSUPP;
	if (*argp != 0 && *argp != TSG_USE_TQ)
		return ENODEV;

	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_MISC_CONTROL, &buf, 1);
	buf &= TSG_INSERT_LEAP;	// clear all but the leap flag
	buf |= *argp;		// set use TQ value
	bus_write_region_1(sc->registers_resource, REG_MISC_CONTROL, &buf, 1);
	unlock(sc);

	return 0;
}

static int
tsg_get_synth_freq(struct tsg_softc *sc, caddr_t arg)
{
	uint32_t *argp = (uint32_t *)arg;
	char *fmt = "l";

	if (!sc->new_model)
		return EOPNOTSUPP;

	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_SYNTH_FREQ, sc->buf, packlen(fmt));
	unpack(sc->buf, fmt, argp);
	unlock(sc);

	return 0;
}

static int
tsg_set_synth_freq(struct tsg_softc *sc, caddr_t arg)
{
	uint32_t *argp = (uint32_t *)arg;
	char *fmt = "l";

	if (!sc->new_model)
		return EOPNOTSUPP;
	if (*argp < 1 || *argp > 1000000)
		return EINVAL;

	lock(sc);
	pack(sc->buf, fmt, *argp);
	bus_write_region_1(sc->registers_resource, REG_SYNTH_FREQ, sc->buf, packlen(fmt));

	bus_read_region_1(sc->registers_resource, REG_SYNTH_CONTROL, sc->buf, 1);
	sc->buf[0] |= TSG_SYNTH_LOAD;
	bus_write_region_1(sc->registers_resource, REG_SYNTH_CONTROL, sc->buf, 1);
	unlock(sc);
	return 0;
}

static int
tsg_get_synth_edge(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;
	uint8_t buf;

	if (!sc->new_model)
		return EOPNOTSUPP;

	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_SYNTH_CONTROL, &buf, 1);
	unlock(sc);
	*argp = buf & TSG_SYNTH_EDGE_RISING;
	return 0;
}

static int
tsg_set_synth_edge(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;
	uint8_t buf;

	if (!sc->new_model)
		return EOPNOTSUPP;
	if (*argp != 0 && *argp != TSG_SYNTH_EDGE_RISING)
		return EINVAL;

	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_SYNTH_CONTROL, &buf, 1);
	buf &= ~TSG_SYNTH_EDGE_RISING;
	buf |= *argp;
	bus_write_region_1(sc->registers_resource, REG_SYNTH_CONTROL, &buf, 1);
	unlock(sc);
	return 0;
}

static int
tsg_get_synth_enable(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;

	if (!sc->new_model)
		return EOPNOTSUPP;

	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_SYNTH_CONTROL, argp, 1);
	unlock(sc);
	*argp &= TSG_SYNTH_ENABLE;
	return 0;
}

static int
tsg_set_synth_enable(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;
	uint8_t buf;

	if (!sc->new_model)
		return EOPNOTSUPP;

	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_SYNTH_CONTROL, &buf, 1);
	buf &= ~TSG_SYNTH_ENABLE;
	buf |= *argp;
	bus_write_region_1(sc->registers_resource, REG_SYNTH_CONTROL, &buf, 1);
	unlock(sc);
	return 0;
}

static int
tsg_ioctl(struct cdev *dev, u_long cmd, caddr_t arg, int fflag, struct thread *td)
{
	struct tsg_softc *sc = dev->si_drv1;

	typedef int (*handler)(struct tsg_softc *, caddr_t);

	static struct dispatcher {
		u_long	cmd;
		handler	fcn;
	} tab[] = {
		{ TSG_GET_BOARD_MODEL,		tsg_get_board_model },
		{ TSG_GET_BOARD_FIRMWARE,	tsg_get_board_firmware },
		{ TSG_GET_BOARD_TEST_STATUS,	tsg_get_board_test_status },
		{ TSG_GET_BOARD_J1,		tsg_get_board_j1 },
		{ TSG_SET_BOARD_J1,		tsg_set_board_j1 },
		{ TSG_GET_PULSE_FREQ,		tsg_get_pulse_freq },
		{ TSG_SET_PULSE_FREQ,		tsg_set_pulse_freq },
		{ TSG_GET_GPS_ANTENNA_STATUS,	tsg_get_gps_antenna_status },
		{ TSG_GET_CLOCK_LOCK,		tsg_get_clock_lock },
		{ TSG_GET_CLOCK_TIMECODE,	tsg_get_clock_timecode },
		{ TSG_SET_CLOCK_TIMECODE,	tsg_set_clock_timecode },
		{ TSG_GET_CLOCK_REF,		tsg_get_clock_ref },
		{ TSG_SET_CLOCK_REF,		tsg_set_clock_ref },
		{ TSG_GET_CLOCK_TIME,		tsg_get_clock_time },
		{ TSG_SET_CLOCK_TIME,		tsg_set_clock_time },
		{ TSG_GET_GPS_POSITION,		tsg_get_gps_position },
		{ TSG_GET_GPS_SIGNAL,		tsg_get_gps_signal },
		{ TSG_GET_TIMECODE_AGC_DELAYS,	tsg_get_timecode_agc_delays },
		{ TSG_GET_CLOCK_LEAP,		tsg_get_clock_leap },
		{ TSG_SET_CLOCK_LEAP,		tsg_set_clock_leap },
		{ TSG_GET_CLOCK_DAC,		tsg_get_clock_dac },
		{ TSG_SAVE_CLOCK_DAC,		tsg_save_clock_dac },
		{ TSG_GET_CLOCK_DST,		tsg_get_clock_dst },
		{ TSG_SET_CLOCK_DST,		tsg_set_clock_dst },
		{ TSG_GET_CLOCK_STOP,		tsg_get_clock_stop },
		{ TSG_SET_CLOCK_STOP,		tsg_set_clock_stop },
		{ TSG_GET_CLOCK_TZ_OFFSET,	tsg_get_clock_tz_offset },
		{ TSG_SET_CLOCK_TZ_OFFSET,	tsg_set_clock_tz_offset },
		{ TSG_GET_CLOCK_PHASE_COMP,	tsg_get_clock_phase_compensation },
		{ TSG_SET_CLOCK_PHASE_COMP,	tsg_set_clock_phase_compensation },
		{ TSG_GET_TIMECODE_QUALITY,	tsg_get_timecode_quality },
		{ TSG_GET_USE_TIMECODE_QUALITY,	tsg_get_use_timecode_quality },
		{ TSG_SET_USE_TIMECODE_QUALITY,	tsg_set_use_timecode_quality },
		{ TSG_GET_SYNTH_FREQ,		tsg_get_synth_freq },
		{ TSG_SET_SYNTH_FREQ,		tsg_set_synth_freq },
		{ TSG_GET_SYNTH_EDGE,		tsg_get_synth_edge },
		{ TSG_SET_SYNTH_EDGE,		tsg_set_synth_edge },
		{ TSG_GET_SYNTH_ENABLE,		tsg_get_synth_enable },
		{ TSG_SET_SYNTH_ENABLE,		tsg_set_synth_enable },
		{ 0,				NULL },
	};

	for (struct dispatcher *p = tab; p->fcn; ++p)
		if (p->cmd == cmd)
			return (*p->fcn)(sc, arg);
	return EOPNOTSUPP;
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
