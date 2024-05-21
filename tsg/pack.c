/*
 * pack.c -- buffer packing and unpacking
 */

#ifdef MAIN
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <sys/types.h>
typedef uint32_t bus_size_t;
#endif

static bus_size_t
packlen(char *fmt)
{
	char *p;
	int len = 0;

	for (p = fmt; *p; ++p)
		switch (*p) {
		case ' ':
			break;
		case 'c':
		case 'C':
		case 'n':
			len++;
			break;
		case 's':
		case 'S':
			len += 2;
			break;
		case 'l':
		case 'L':
			len += 4;
			break;
		default:
			return -1;
		}
	return len;
}

static uint8_t *
pack(uint8_t *buf, char *fmt, ...)
{
	va_list args;
	char *p;
	uint8_t *bp = buf;
	uint8_t upper, lower;
	uint16_t s;
	int16_t S;
	uint32_t l;
	int32_t L;

	va_start(args, fmt);
	for (p = fmt; *p; ++p) {
		switch (*p) {
		case ' ':
			break;
		case 'n':
			upper = va_arg(args, unsigned int);
			lower = va_arg(args, unsigned int);
			*bp++ = (upper << 4) | lower;
			break;
		case 'c':
			*bp++ = va_arg(args, unsigned int);
			break;
		case 'C':
			*bp++ = va_arg(args, int);
			break;
		case 's':
			s = va_arg(args, unsigned int);
			*bp++ = s;
			*bp++ = s >> 8;
			break;
		case 'S':
			S = va_arg(args, int);
			*bp++ = S;
			*bp++ = S >> 8;
			break;
		case 'l':
			l = va_arg(args, uint32_t);
			*bp++ = l;
			*bp++ = l >> 8;
			*bp++ = l >> 16;
			*bp++ = l >> 24;
			break;
		case 'L':
			L = va_arg(args, int32_t);
			*bp++ = L;
			*bp++ = L >> 8;
			*bp++ = L >> 16;
			*bp++ = L >> 24;
			break;
		default:
			va_end(args);
			break;
		}
	}
	va_end(args);
	return bp;
}

static uint8_t *
unpack(uint8_t *buf, char *fmt, ...)
{
	va_list args;
	char *p;
	uint8_t *bp = buf;

	uint8_t *pc;
	int8_t *pC;
	uint8_t c;

	uint16_t *ps;
	int16_t *pS;
	uint16_t s;

	uint32_t *pl;
	int32_t *pL;
	uint32_t l;

	va_start(args, fmt);
	for (p = fmt; *p; ++p) {
		switch (*p) {
		case ' ':
			break;
		case 'n':
			c = *bp++;
			pc = va_arg(args, uint8_t *);
			if (pc)
				*pc = c >> 4;
			pc = va_arg(args, uint8_t *);
			if (pc)
				*pc = c & 0x0f;
			break;
		case 'c':
			pc = va_arg(args, uint8_t *);
			c = *bp++;
			if (pc)
				*pc = c;
			break;
		case 'C':
			pC = va_arg(args, int8_t *);
			c = *bp++;
			if (pC)
				*pC = (int8_t)c;
			break;
		case 's':
		case 'S':
			s = *bp++;
			s |= *bp++ << 8;
			if (*p == 's') {
				ps = va_arg(args, uint16_t *);
				if (ps)
					*ps = s;
			} else {
				pS = va_arg(args, int16_t *);
				if (pS)
					*pS = (int16_t)s;
			}
			break;
		case 'l':
		case 'L':
			l = *bp++;
			l |= *bp++ << 8;
			l |= *bp++ << 16;
			l |= *bp++ << 24;
			if (*p == 'l') {
				pl = va_arg(args, uint32_t *);
				if (pl)
					*pl = l;
			} else {
				pL = va_arg(args, int32_t *);
				if (pL)
					*pL = (int32_t)l;
			}
			break;
		default:
			va_end(args);
			return NULL;
		}
	}
	va_end(args);
	return bp;
}

#ifdef MAIN
int
main(int argc, char **argv)
{
	uint8_t buf[20];
	uint8_t		c1 = '-',		c2;
	int8_t		C1 = -9,		C2;
	uint16_t	s1 = 1234,		s2;
	int16_t		S1 = -1234,		S2;
	uint32_t	l1 = 123456789,		l2;
	int32_t		L1 = -123456789,	L2;
	uint8_t		high1 = 0xa,		high2;
	uint8_t		low1 = 0xb,		low2;
	char *fmt = "n c C s S l L";

	assert(packlen(fmt) == 15);

	pack(buf, fmt, high1, low1, c1, C1, s1, S1, l1, L1);
	unpack(buf, fmt, &high2, &low2, &c2, &C2, &s2, &S2, &l2, &L2);

	assert(high2 == high1);
	assert(low2 == low1);
	assert(c2 == c1);
	assert(C2 == C1);
	assert(s2 == s1);
	assert(S2 == S1);
	assert(l2 == l1);
	assert(L2 == L1);

	exit(0);
}
#endif
