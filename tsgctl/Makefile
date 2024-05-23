OBJS=ctl.o gettok.o node.o action.o pulse.o synth.o clock.o gps.o timecode.o board.o event.o map.o tsglib.o

CFLAGS+=-Wall -I../tsg
tsgctl: $(OBJS)
	cc -Wall -o tsgctl $(OBJS)
clean::
	rm -f tsgctl

ctl.o: gettok.h action.h

gettok.o: gettok.h

node.o: gettok.h node.h

action.o: node.h pulse.h synth.h clock.h gps.h action.h timecode.h

pulse.o: gettok.h node.h pulse.h tsglib.h map.h

synth.o: gettok.h node.h synth.h tsglib.h map.h

clock.o: gettok.h node.h tsglib.h clock.h map.h

gps.o: node.h tsglib.h gps.h

timecode.o: gettok.h node.h timecode.h tsglib.h map.h

board.o: gettok.h node.h board.h tsglib.h map.h

event.o: gettok.h node.h event.h

map.o: map.h

tsglib.o: ../tsg/tsg.h tsglib.h

clean::
	rm -f $(OBJS)
