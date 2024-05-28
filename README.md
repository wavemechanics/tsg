FreeBSD driver and control program for TrueTime/Symmetricom 560-5900 series cards.

WORK IN PROGRESS

These cards have an onboard clock that can free-run, or can be synchronised to
an external 1PPS signal or IRIG A/B timecode.
Some cards can be synced to an onboard GPS receiver.

The card can generate 1PPS, IRIG timecode, decade pulses, and arbitrary frequences.
It can also be configured to do things when the onboard clock time matches a given
pattern.

Certain events can generate PCI interrupts.
This driver handles these interrupts using the RFC 2783 Pulse Per Second API.
You can use this API to capture timestamps on external events, or to pass 1PPS
signals to NTP.

This driver supports most board parameters using `ioctl`s or the `tsgctl` cli.

Next steps:

* finish the last few board control ioctls and commands
* further testing, and polish up the code
* write docs and examples
* maybe implement `read` to generate an NMEA stream

## How to build and load

Directory layout:

    tsg/	driver sources

    tsgctl/	control program sources

Each source directory has its own `Makefile`, so you can just change to each directory
and run `make`.

To load the driver:

    cd tsg
    make load

And then `make unload` to unload the driver.

You can check `dmesg` to see if it detected your card.
You should see something like:

    tsg0: <Symmetricom 560-5907-U PCI-SG-2U> port 0x9400-0x947f mem 0xe1040000-0xe104007f,0xe1041000-0xe10411ff irq 18 at device 5.0 on pci3
    tsg0: firmware: 6.0.0
    tsg1: <Symmetricom 560-5908-U GPS-PCI-2U> port 0x8000-0x807f mem 0xc0001000-0xc000107f,0xc0000000-0xc00001ff irq 16 at device 0.0 on pci4
    tsg1: firmware: 6.0.0

I am using two different cards, so both devices show up.


## How to test

Once the drivers are loaded and the cards are detected, you can use the cli to control
them.

Here is an example setup I am using to test a few functions:

    +-+ GPS antenna
     +
     |
     |
    +------+   IRIG  +------+  IRIG    +-----------------+
    | tsg1 +---------+ tsg0 +----------+ timecode reader |
    +------+         +------+          +-----------------+

A GPS antenna is connected to tsg1's antenna input.

Coax connects tsg1's CODE OUT (J1) to tsg0's CODE IN.

Coax connects tsg0's CODE OUT (J1) to the timecode reader input.


To configure this:

Configure tsg1 to sync to GPS:

	# ./tsgctl -d /dev/tsg1 get clock reference
	reference: timecode

	# ./tsgctl -d /dev/tsg1 set clock reference gps

	# ./tsgctl -d /dev/tsg1 get clock lock
	phase lock: no
	input valid: yes
	gps lock: no

After about a minute:

	# ./tsgctl -d /dev/tsg1 get clock lock
	phase lock: yes
	input valid: yes
	gps lock: yes

Make sure tsg1 CODE OUT (J1) is sending IRIG-B AM:

	# ./tsgctl -d /dev/tsg1 get board j1
	J1: IRIG-B-AM

Configure tsg0 to sync to the timecode from tsg1 (in this case it already is):

	# ./tsgctl -d /dev/tsg0 get clock reference
	reference: timecode

Verify tsg0 is happy with the incoming timecode:

	# ./tsgctl -d /dev/tsg0 get clock lock
	phase lock: yes
	input valid: yes
	gps lock: no

Now configure tsg0 to send a timecode to the timecode reader:

	# ./tsgctl -d /dev/tsg0 set board j1


Another example to generate a 10MHz square waves instead of a timecode on tsg0's
CODE OUT (J1):

    # ./tsgctl -d /dev/tsg0 set board j1 pulse
    # ./tsgctl -d /dev/tsg0 set pulse freq 10MHz

You can use a frequency counter to verify the J1 output frequency.
