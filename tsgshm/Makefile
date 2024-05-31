tsgshm: tsgshm.o doy.o
	cc -o tsgshm tsgshm.o doy.o

tsgshm.o: doy.h ../tsg/tsg.h
	cc -Wall -c tsgshm.c

doy.o: doy.h
	cc -Wall -c doy.c
