/*
 * pack.c -- buffer packing and unpacking
 */

#ifdef MAIN
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
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
			*bp++ = s >> 8;
			*bp++ = s;
			break;
		case 'S':
			S = va_arg(args, int);
			*bp++ = S >> 8;
			*bp++ = S;
			break;
		case 'l':
			l = va_arg(args, uint32_t);
			*bp++ = l >> 24;
			*bp++ = l >> 16;
			*bp++ = l >> 8;
			*bp++ = l;
			break;
		case 'L':
			L = va_arg(args, int32_t);
			*bp = L >> 24;
			*bp++ = L >> 16;
			*bp++ = L >> 8;
			*bp++ = L;
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
	uint8_t buf[] = {'a', 'b', 0x10, 0x20, 0x30, 0x40, 0x12, 0x34, 0x56, 0x7a, 0x65};
	uint8_t a = '-';
	int8_t b = '-';
	uint16_t s = 0;
	int16_t S = 0;
	uint32_t l = 0;
	uint8_t high =0;
	uint8_t low =0;
	char *fmt = "cC sS l n";

	printf("%d\n", packlen(fmt));
	//unpack(buf, fmt, &a, &b, &s, &S, &l, &high, &low);
	unpack(buf, fmt, NULL, NULL, NULL, NULL, &l, NULL, NULL);

	printf("a=%c\n", a);
	printf("b=%c\n", b);
	printf("0x%x\n", s);
	printf("%d\n", S);
	printf("0x%lx\n", (unsigned long)l);
	printf("high=0x%x\n", high);
	printf("low=0x%x\n", low);
}
#endif
