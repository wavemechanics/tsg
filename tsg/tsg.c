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

#include <sys/timepps.h>

#include "tsg.h"

/* PCI identification */
#define	PCI_VENDOR_PLX		0x10b5
#define	PCI_DEVICE_TSG		0x9050
#define	PCI_SUBVENDOR_TRUETIME	0x12da

/* PCI BARs */
#define	BAR_LCR		0	/* BAR0: "Local Configuration Registers" */
#define BAR_REGISTERS	2	/* BAR2: regular card registers */

/* "Local Configuration Registers"; INTCSR is the only one */
#define	LCR_INTCSR	0x4c
#define		LCR_INTCSR_ENABLE	0x0048
#define		LCR_INTCSR_DISABLE	0x0008

/* register addresses */
#define	REG_HARDWARE_STATUS	0xfe
#define		TSG_INTR_SYNTH		0x08	// synth edge occurred
#define		TSG_INTR_PULSE		0x04	// pulse edge occurred
#define		TSG_INTR_COMPARE	0x02	// time comparison edge occurred
#define		TSG_INTR_EXT		0x01	// external event edge occurred
#define	REG_HARDWARE_CONTROL	0xf8
#define		TSG_CLEAR_SYNTH		0x40
#define		TSG_CLEAR_PULSE		0x04
#define		TSG_CLEAR_COMPARE	0x02
#define		TSG_CLEAR_EXT		0x01
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
#define	REG_TIME_COMPARE	0x138

#define	UNUSED(x)	(x) __attribute__((unused))

struct tsg_softc {
	device_t	device;

	struct cdev	*cdev;		// main device
	struct cdev	*cdev_compare;	// .compare device
	struct cdev	*cdev_ext;	// .ext device
	struct cdev	*cdev_pulse;	// .pulse device
	struct cdev	*cdev_synth;	// .synth device

	struct mtx	mtx;

	int		lcr_rid;
	struct resource	*lcr_resource;

	int		registers_rid;
	struct resource	*registers_resource;

	int		intr_rid;
	struct resource	*intr_resource;
	void		*cookiep;

	uint16_t	model;	/* from subdevice id */
	bool		new_model;
	bool		has_gps;

	uint8_t		buf[6*4];	// largest I/O is 6 32 bit words

	uint8_t		major;
	uint8_t		minor;
	uint8_t		test;

	// intmask determines which events should generate interrupts.
	// (TSG_INT_ENABLE_*)
	uint8_t 	intmask;

	// We manage four PPS API interfaces
	struct pps_state	pps_state_compare;
	struct mtx		pps_mtx_compare;
	struct tsg_time		pps_time_compare;

	struct pps_state	pps_state_ext;
	struct mtx		pps_mtx_ext;
	struct tsg_time		pps_time_ext;

	struct pps_state	pps_state_pulse;
	struct mtx		pps_mtx_pulse;
	struct tsg_time		pps_time_pulse;

	struct pps_state	pps_state_synth;
	struct mtx		pps_mtx_synth;
	struct tsg_time		pps_time_synth;
};

static d_open_t		tsg_open;
static d_close_t	tsg_close;
static d_ioctl_t	tsg_ioctl;

static d_open_t		tsg_compare_open;
static d_close_t	tsg_compare_close;
static d_ioctl_t	tsg_compare_ioctl;

static d_open_t		tsg_ext_open;
static d_close_t	tsg_ext_close;
static d_ioctl_t	tsg_ext_ioctl;

static d_open_t		tsg_pulse_open;
static d_close_t	tsg_pulse_close;
static d_ioctl_t	tsg_pulse_ioctl;

static d_open_t		tsg_synth_open;
static d_close_t	tsg_synth_close;
static d_ioctl_t	tsg_synth_ioctl;

static struct cdevsw tsg_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	tsg_open,
	.d_close =	tsg_close,
	.d_ioctl =	tsg_ioctl,
	.d_name = 	"tsg"
};

static struct cdevsw tsg_cdevsw_compare = {
	.d_version =	D_VERSION,
	.d_open =	tsg_compare_open,
	.d_close =	tsg_compare_close,
	.d_ioctl =	tsg_compare_ioctl,
	.d_name =	"tsg_compare"
};

static struct cdevsw tsg_cdevsw_ext = {
	.d_version =	D_VERSION,
	.d_open =	tsg_ext_open,
	.d_close =	tsg_ext_close,
	.d_ioctl =	tsg_ext_ioctl,
	.d_name	= 	"tsg_ext"
};

static struct cdevsw tsg_cdevsw_pulse = {
	.d_version =	D_VERSION,
	.d_open =	tsg_pulse_open,
	.d_close =	tsg_pulse_close,
	.d_ioctl =	tsg_pulse_ioctl,
	.d_name =	"tsg_pulse"
};

static struct cdevsw tsg_cdevsw_synth = {
	.d_version =	D_VERSION,
	.d_open =	tsg_synth_open,
	.d_close =	tsg_synth_close,
	.d_ioctl =	tsg_synth_ioctl,
	.d_name =	"tsg_synth"
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

static void
tsg_intcsr(struct tsg_softc *sc, int enable)
{
	uint32_t intcsr = enable ? LCR_INTCSR_ENABLE : LCR_INTCSR_DISABLE;
        bus_write_region_4(sc->lcr_resource, LCR_INTCSR, &intcsr, 1);
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

static char *fmt_bcd_time = "nnnn nnnn nnnn";

static void
read_bcd_time(struct tsg_softc *sc)
{
	bus_read_region_1(sc->registers_resource, 0xfc, sc->buf, packlen(fmt_bcd_time));
}

// bcd_time holds the BCD components of the time taken from the board
struct bcd_time {
	uint8_t thousands_year;
	uint8_t hundreds_year;
	uint8_t tens_year;
	uint8_t units_year;

	uint8_t hundreds_day;
	uint8_t tens_day;
	uint8_t units_day;

	uint8_t tens_hour;
	uint8_t units_hour;

	uint8_t tens_min;
	uint8_t units_min;

	uint8_t tens_sec;
	uint8_t units_sec;

	uint8_t hundreds_milli;
	uint8_t tens_milli;
	uint8_t units_milli;

	uint8_t	hundreds_micro;
	uint8_t tens_micro;
	uint8_t units_micro;

	uint8_t hundreds_nano;
};

static void
unpack_bcd_time(uint8_t *buf, struct bcd_time *b)
{
	unpack(
		buf,
		fmt_bcd_time,
		&b->tens_micro,		&b->units_micro,
		&b->units_milli,	&b->hundreds_micro,
		NULL,			NULL,
		&b->hundreds_nano,	NULL,
		&b->hundreds_milli,	&b->tens_milli,
		&b->tens_sec,		&b->units_sec,
		&b->tens_min,		&b->units_min,
		&b->tens_hour,		&b->units_hour,
		&b->tens_day,		&b->units_day,
		NULL,			&b->hundreds_day,
		&b->tens_year,		&b->units_year,
		&b->thousands_year,	&b->hundreds_year
	);
}

static void
bcd2time(struct bcd_time *b, struct tsg_time *t, int new)
{
	t->year = b->thousands_year * 1000 +
		  b->hundreds_year * 100 +
		  b->tens_year * 10 +
		  b->units_year;

	t->day = b->hundreds_day * 100 +
		 b->tens_day * 10 +
		 b->units_day;

	t->hour = b->tens_hour * 10 + b->units_hour;

	t->min = b->tens_min * 10 + b->units_min;

	t->sec = b->tens_sec * 10 + b->units_sec;

	t->nsec = b->hundreds_milli * 100000000 +
		  b->tens_milli  *     10000000 +
		  b->units_milli *      1000000 +
		  b->hundreds_micro *    100000 +
		  b->tens_micro *         10000 +
		  b->units_micro *         1000;

	if (new)
		t->nsec += b->hundreds_nano * 100;
}

static void
timestamp(struct pps_state *state, struct mtx *mtx)
{
	mtx_lock(mtx);
	pps_capture(state);
	pps_event(state, PPS_CAPTUREASSERT);
	mtx_unlock(mtx);
}

static void
tsg_ithrd(void *arg)
{
	struct tsg_softc *sc = arg;
	uint8_t intstat;
	unsigned intmask;
	uint8_t clearmask = 0;

	lock(sc);

	// latch board time so we can grab it later
	bus_write_region_1(sc->registers_resource, 0xfc, sc->buf, 1);

	// find out which events occurred
	bus_read_region_1(sc->registers_resource, REG_HARDWARE_STATUS, &intstat, 1);

	// which events are we interested in?
	intmask = sc->intmask;

	// capture PPS events when we are interested in them AND they have occurred
	if ((intmask & TSG_INT_ENABLE_EXT) && (intstat & TSG_INTR_EXT)) {
		timestamp(&sc->pps_state_ext, &sc->pps_mtx_ext);
		clearmask |= TSG_CLEAR_EXT;
	}
	if ((intmask & TSG_INT_ENABLE_PULSE) && (intstat & TSG_INTR_PULSE)) {
		timestamp(&sc->pps_state_pulse, &sc->pps_mtx_pulse);
		clearmask |= TSG_CLEAR_PULSE;
	}
	if ((intmask & TSG_INT_ENABLE_COMPARE) && (intstat & TSG_INTR_COMPARE)) {
		timestamp(&sc->pps_state_compare, &sc->pps_mtx_compare);
		clearmask |= TSG_CLEAR_COMPARE;
	}
	if ((intmask & TSG_INT_ENABLE_SYNTH) && (intstat & TSG_INTR_SYNTH)) {
		timestamp(&sc->pps_state_synth, &sc->pps_mtx_synth);
		clearmask |= TSG_CLEAR_SYNTH;
	}

	// save latched time for userland
	struct bcd_time b;
	struct tsg_time latched_time;

	read_bcd_time(sc);
	unpack_bcd_time(sc->buf, &b);
	bcd2time(&b, &latched_time, sc->new_model);
	if (clearmask & TSG_CLEAR_EXT)
		sc->pps_time_ext = latched_time;
	if (clearmask & TSG_CLEAR_PULSE)
		sc->pps_time_pulse = latched_time;
	if (clearmask & TSG_CLEAR_COMPARE)
		sc->pps_time_compare = latched_time;
	if (clearmask & TSG_CLEAR_SYNTH)
		sc->pps_time_synth = latched_time;

	// the control register holds the events we are interested in, and is used
	// to clear/acknowlege events
	clearmask |= sc->intmask;
	bus_write_region_1(sc->registers_resource, REG_HARDWARE_CONTROL, &clearmask, 1);

	unlock(sc);
}

static void
release_resources(struct tsg_softc *sc)
{
	tsg_intcsr(sc, 0);

	if (sc->cookiep != NULL) {
		bus_teardown_intr(
			sc->device,
			sc->intr_resource,
			sc->cookiep
		);
		sc->cookiep = NULL;
	}
	if (sc->intr_resource) {
		bus_release_resource(
			sc->device,
			SYS_RES_IRQ,
			sc->intr_rid,
			sc->intr_resource
		);
		sc->intr_resource = NULL;
	}
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

	mtx_destroy(&sc->pps_mtx_compare);
	mtx_destroy(&sc->pps_mtx_ext);
	mtx_destroy(&sc->pps_mtx_pulse);
	mtx_destroy(&sc->pps_mtx_synth);
	mtx_destroy(&sc->mtx);
}

static void
setup_pps_state(struct pps_state *state, struct mtx *mtx)
{
	state->ppscap = PPS_CAPTUREASSERT;
	state->driver_abi = PPS_ABI_VERSION;
	state->driver_mtx = mtx;
	pps_init_abi(state);
}

static int
tsg_attach(device_t dev)
{
	struct tsg_softc *sc = device_get_softc(dev);

	sc->device = dev;
	sc->lcr_resource = NULL;
	sc->registers_resource = NULL;

	mtx_init(&sc->mtx, "tsg", NULL, MTX_DEF);
	mtx_init(&sc->pps_mtx_compare, "tsg_compare", NULL, MTX_DEF);
	mtx_init(&sc->pps_mtx_ext, "tsg_ext", NULL, MTX_DEF);
	mtx_init(&sc->pps_mtx_pulse, "tsg_pulse", NULL, MTX_DEF);
	mtx_init(&sc->pps_mtx_synth, "tsg_synth", NULL, MTX_DEF);

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

	/* turn off board interrupts */
	tsg_intcsr(sc, 0);

	/* clear interrupt mask on board */
	sc->intmask = 0;
	bus_write_region_1(sc->registers_resource, REG_HARDWARE_CONTROL, &sc->intmask, 1);

	setup_pps_state(&sc->pps_state_compare, &sc->pps_mtx_compare);
	setup_pps_state(&sc->pps_state_ext, &sc->pps_mtx_ext);
	setup_pps_state(&sc->pps_state_pulse, &sc->pps_mtx_pulse);
	setup_pps_state(&sc->pps_state_synth, &sc->pps_mtx_synth);

	/* allocate interrupt */
	sc->intr_rid = 0;
	sc->intr_resource = bus_alloc_resource_any(
		dev,
		SYS_RES_IRQ,
		&sc->intr_rid,
		RF_ACTIVE | RF_SHAREABLE
	);
	if (!sc->intr_resource) {
		release_resources(sc);
		device_printf(dev, "cannot allocate interrupt\n");
		return ENXIO;
	}

	/* register interrupt handler */
	sc->cookiep = NULL;
	int err = bus_setup_intr(
		dev,
		sc->intr_resource,
		INTR_TYPE_CLK | INTR_MPSAFE,
		NULL,
		tsg_ithrd,
		sc,
		&sc->cookiep
	);
	if (err) {
		release_resources(sc);
		device_printf(dev, "cannot register interrupt handler\n");
		return err;
	}

	int unit = device_get_unit(dev);
	struct make_dev_args args;

	make_dev_args_init(&args);
	args.mda_devsw = &tsg_cdevsw;
	args.mda_uid = UID_ROOT;
	args.mda_gid = GID_WHEEL;
	args.mda_mode = 0600;
	args.mda_unit = unit;
	args.mda_si_drv1 = sc;
	int error = make_dev_s(&args, &sc->cdev, "tsg%d", unit);
	if (error != 0) {
		sc->cdev = NULL;
		release_resources(sc);
		device_printf(dev, "cannot create device node tsg%d\n", unit);
		return ENXIO;
	}

	args.mda_devsw = &tsg_cdevsw_compare;
	error = make_dev_s(&args, &sc->cdev_compare, "tsg%d.compare", unit);
	if (error != 0) {
		sc->cdev_compare = NULL;
		release_resources(sc);
		device_printf(dev, "cannot create device node tsg%d.compare", unit);
		return ENXIO;
	}

	args.mda_devsw = &tsg_cdevsw_ext;
	error = make_dev_s(&args, &sc->cdev_ext, "tsg%d.ext", unit);
	if (error != 0) {
		sc->cdev_ext = NULL;
		release_resources(sc);
		device_printf(dev, "cannot create device node tsg%d.ext", unit);
		return ENXIO;
	}

	args.mda_devsw = &tsg_cdevsw_pulse;
	error = make_dev_s(&args, &sc->cdev_pulse, "tsg%d.pulse", unit);
	if (error != 0) {
		sc->cdev_pulse = NULL;
		release_resources(sc);
		device_printf(dev, "cannot create device node tsg%d.pulse", unit);
		return ENXIO;
	}

	sc->cdev_synth = NULL;
	if (sc->new_model) {
		args.mda_devsw = &tsg_cdevsw_synth;
		error = make_dev_s(&args, &sc->cdev_synth, "tsg%d.synth", unit);
		if (error != 0) {
			sc->cdev_synth = NULL;
			release_resources(sc);
			device_printf(dev, "cannot create device node tsg%d.synth", unit);
			return ENXIO;
		}
	}

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

	if (sc->cdev_synth)
		destroy_dev(sc->cdev_synth);
	if (sc->cdev_pulse)
		destroy_dev(sc->cdev_pulse);
	if (sc->cdev_ext)
		destroy_dev(sc->cdev_ext);
	if (sc->cdev_compare)
		destroy_dev(sc->cdev_compare);
	if (sc->cdev)
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
tsg_compare_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	return 0;
}

static int
tsg_ext_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	return 0;
}

static int
tsg_pulse_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	return 0;
}

static int
tsg_synth_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	return 0;
}

static int
tsg_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	//struct tsg_softc *sc = dev->si_drv1;

	return 0;
}

static int
tsg_compare_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	return 0;
}

static int
tsg_ext_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	return 0;
}

static int
tsg_pulse_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	return 0;
}

static int
tsg_synth_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
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
tsg_get_board_pin6(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;

	// pin6 on old boards is always comparator output
	if (!sc->new_model) {
		*argp = 0;
		return 0;
	}

	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_SYNTH_CONTROL, argp, 1);
	unlock(sc);
	*argp &= TSG_BOARD_PIN6_SYNTH;
	return 0;
}

static int
tsg_set_board_pin6(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;
	uint8_t buf;

	// Cannot change pin6 on old boards; it is always comparator output.
	if (!sc->new_model)
		return *argp == 0 ? 0 : EINVAL;
	if (*argp != 0 && *argp != TSG_BOARD_PIN6_SYNTH)
		return EINVAL;

	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_SYNTH_CONTROL, &buf, 1);
	buf &= ~(TSG_SYNTH_LOAD | TSG_BOARD_PIN6_SYNTH);
	buf |= *argp;
	bus_write_region_1(sc->registers_resource, REG_SYNTH_CONTROL, &buf, 1);
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

	lock(sc);
	bus_write_region_1(sc->registers_resource, 0xfc, sc->buf, 1);

	read_bcd_time(sc);
	struct bcd_time b;
	unpack_bcd_time(sc->buf, &b);

	unlock(sc);

	bcd2time(&b, argp, sc->new_model);

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
tsg_get_compare_time(struct tsg_softc *sc, caddr_t arg)
{
	struct tsg_compare_time *argp = (struct tsg_compare_time *)arg;
	char *fmt = "nnnn nnnn";
	uint8_t hundreds_day, tens_day, units_day;
	uint8_t tens_hour, units_hour;
	uint8_t tens_min, units_min;
	uint8_t tens_sec, units_sec;
	uint8_t hundreds_msec, tens_msec, units_msec;
	uint8_t hundreds_usec, tens_usec, units_usec;
	uint8_t mask;

	lock(sc);
	bus_read_region_1(sc->registers_resource, REG_TIME_COMPARE, sc->buf, packlen(fmt));
	unpack(sc->buf, fmt,
		&tens_usec,	&units_usec,
		&units_msec,	&hundreds_usec,
		&hundreds_msec,	&tens_msec,
		&tens_sec,	&units_sec,
		&tens_min,	&units_min,
		&tens_hour,	&units_hour,
		&tens_day,	&units_day,
		&mask,		&hundreds_day
	);
	unlock(sc);

	argp->day = hundreds_day * 100 + tens_day * 10 + units_day;
	argp->hour = tens_hour * 10 + units_hour;
	argp->min = tens_min * 10 + units_min;
	argp->sec = tens_sec * 10 + units_sec;
	argp->usec =
		hundreds_msec * 100000 +
		tens_msec *      10000 +
		units_msec *      1000 +
		hundreds_usec *    100 +
		tens_usec *         10 +
		units_usec;
	argp->mask = mask;

	return 0;
}

static int
tsg_set_compare_time(struct tsg_softc *sc, caddr_t arg)
{
	struct tsg_compare_time *argp = (struct tsg_compare_time *)arg;
	char *fmt = "nnnn nnnn";
	uint8_t hundreds_day, tens_day, units_day;
	uint8_t tens_hour, units_hour;
	uint8_t tens_min, units_min;
	uint8_t tens_sec, units_sec;
	uint8_t hundreds_msec, tens_msec, units_msec;
	uint8_t hundreds_usec, tens_usec, units_usec;

	if (argp->day < 1 || argp->day > 366)
		return EINVAL;
	if (argp->hour > 23)
		return EINVAL;
	if (argp->min > 59)
		return EINVAL;
	if (argp->sec > 59)
		return EINVAL;
	if (argp->usec > 999999)
		return EINVAL;
	switch (argp->mask) {
	case TSG_COMPARE_MASK_HDAY:
	case TSG_COMPARE_MASK_TDAY:
	case TSG_COMPARE_MASK_UDAY:
	case TSG_COMPARE_MASK_THOUR:
	case TSG_COMPARE_MASK_UHOUR:
	case TSG_COMPARE_MASK_TMIN:
	case TSG_COMPARE_MASK_UMIN:
	case TSG_COMPARE_MASK_TSEC:
	case TSG_COMPARE_MASK_USEC:
	case TSG_COMPARE_MASK_HMSEC:
	case TSG_COMPARE_MASK_TMSEC:
	case TSG_COMPARE_MASK_UMSEC:
		break;
	case TSG_COMPARE_MASK_DISABLE:
		if (!sc->new_model)
			return EINVAL;
		break;
	default:
		return EINVAL;
	}

	ushort2bcd(argp->day, NULL, NULL, &hundreds_day, &tens_day, &units_day);
	ushort2bcd(argp->hour, NULL, NULL, NULL, &tens_hour, &units_hour);
	ushort2bcd(argp->min, NULL, NULL, NULL, &tens_min, &units_min);
	ushort2bcd(argp->sec, NULL, NULL, NULL, &tens_sec, &units_sec);
	ushort2bcd(argp->usec / 1000, NULL, NULL, &hundreds_msec, &tens_msec, &units_msec);
	ushort2bcd(argp->usec % 1000, NULL, NULL, &hundreds_usec, &tens_usec, &units_usec);

	lock(sc);
	pack(sc->buf, fmt,
		tens_usec,	units_usec,
		units_msec,	hundreds_usec,
		hundreds_msec,	tens_msec,
		tens_sec,	units_sec,
		tens_min,	units_min,
		tens_hour,	units_hour,
		tens_day,	units_day,
		argp->mask,	hundreds_day
	);
	bus_write_region_1(sc->registers_resource, REG_TIME_COMPARE, sc->buf, packlen(fmt));
	unlock(sc);

	return 0;
}

static int
tsg_get_int_mask(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;

	lock(sc);
	*argp = sc->intmask;
	unlock(sc);

	return 0;
}

static int
tsg_set_int_mask(struct tsg_softc *sc, caddr_t arg)
{
	uint8_t *argp = (uint8_t *)arg;

	if (*argp & ~TSG_INT_ENABLE_MASK)
		return EINVAL;	// non-int bits are set
	if (!sc->new_model && (*argp & TSG_INT_ENABLE_SYNTH))
		return ENODEV;	// old boards don't support synth interrupts

	lock(sc);
	sc->intmask = *argp;
	bus_write_region_1(sc->registers_resource, REG_HARDWARE_CONTROL, &sc->intmask, 1);
	tsg_intcsr(sc, sc->intmask != 0);
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
		{ TSG_GET_BOARD_PIN6,		tsg_get_board_pin6 },
		{ TSG_SET_BOARD_PIN6,		tsg_set_board_pin6 },
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
		{ TSG_GET_COMPARE_TIME,		tsg_get_compare_time },
		{ TSG_SET_COMPARE_TIME,		tsg_set_compare_time },
		{ TSG_GET_INT_MASK,		tsg_get_int_mask },
		{ TSG_SET_INT_MASK,		tsg_set_int_mask },
		{ 0,				NULL },
	};

	for (struct dispatcher *p = tab; p->fcn; ++p)
		if (p->cmd == cmd)
			return (*p->fcn)(sc, arg);
	return EOPNOTSUPP;
}

static int
tsg_compare_ioctl(struct cdev *dev, u_long cmd, caddr_t arg, int fflag, struct thread *td)
{
	struct tsg_softc *sc = dev->si_drv1;
	int err;

	if (cmd == TSG_GET_LATCHED_TIME) {
		struct tsg_time *argp = (struct tsg_time *)arg;
		lock(sc);
		*argp = sc->pps_time_compare;
		unlock(sc);
		return 0;
	} else if (cmd == TSG_GET_CLOCK_REF)
		return tsg_get_clock_ref(sc, arg);
	else if (cmd == TSG_GET_CLOCK_LOCK)
		return tsg_get_clock_ref(sc, arg);

	mtx_lock(&sc->pps_mtx_compare);
	err = pps_ioctl(cmd, arg, &sc->pps_state_compare);
	mtx_unlock(&sc->pps_mtx_compare);
	return err;
}

static int
tsg_ext_ioctl(struct cdev *dev, u_long cmd, caddr_t arg, int fflag, struct thread *td)
{
	struct tsg_softc *sc = dev->si_drv1;
	int err;

	if (cmd == TSG_GET_LATCHED_TIME) {
		struct tsg_time *argp = (struct tsg_time *)arg;
		lock(sc);
		*argp = sc->pps_time_ext;
		unlock(sc);
		return 0;
	} else if (cmd == TSG_GET_CLOCK_REF)
		return tsg_get_clock_ref(sc, arg);
	else if (cmd == TSG_GET_CLOCK_LOCK)
		return tsg_get_clock_ref(sc, arg);

	mtx_lock(&sc->pps_mtx_ext);
	err = pps_ioctl(cmd, arg, &sc->pps_state_ext);
	mtx_unlock(&sc->pps_mtx_ext);
	return err;
}

static int
tsg_pulse_ioctl(struct cdev *dev, u_long cmd, caddr_t arg, int fflag, struct thread *td)
{
	struct tsg_softc *sc = dev->si_drv1;
	int err;

	if (cmd == TSG_GET_LATCHED_TIME) {
		struct tsg_time *argp = (struct tsg_time *)arg;
		lock(sc);
		*argp = sc->pps_time_pulse;
		unlock(sc);
		return 0;
	} else if (cmd == TSG_GET_CLOCK_REF)
		return tsg_get_clock_ref(sc, arg);
	else if (cmd == TSG_GET_CLOCK_LOCK)
		return tsg_get_clock_ref(sc, arg);

	mtx_lock(&sc->pps_mtx_pulse);
	err = pps_ioctl(cmd, arg, &sc->pps_state_pulse);
	mtx_unlock(&sc->pps_mtx_pulse);
	return err;
}

static int
tsg_synth_ioctl(struct cdev *dev, u_long cmd, caddr_t arg, int fflag, struct thread *td)
{
	struct tsg_softc *sc = dev->si_drv1;
	int err;

	if (cmd == TSG_GET_LATCHED_TIME) {
		struct tsg_time *argp = (struct tsg_time *)arg;
		lock(sc);
		*argp = sc->pps_time_synth;
		unlock(sc);
		return 0;
	} else if (cmd == TSG_GET_CLOCK_REF)
		return tsg_get_clock_ref(sc, arg);
	else if (cmd == TSG_GET_CLOCK_LOCK)
		return tsg_get_clock_ref(sc, arg);

	mtx_lock(&sc->pps_mtx_synth);
	err = pps_ioctl(cmd, arg, &sc->pps_state_synth);
	mtx_unlock(&sc->pps_mtx_synth);
	return err;
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
