/* Minimal C library for DrakeOS programs. */
#include <drake.h>

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}

void *memset(void *d, int c, size_t n)
{
    unsigned char *p = d;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    while (n--)
        *p++ = *q++;
    return d;
}

int atoi(const char *s)
{
    int v = 0, neg = *s == '-';
    if (neg)
        s++;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

int puts(const char *s)
{
    write(STDOUT, s, strlen(s));
    return write(STDOUT, "\n", 1);
}

static int utoa(unsigned v, unsigned base, char *out)
{
    char tmp[12];
    int n = 0, i = 0;
    do {
        tmp[n++] = "0123456789abcdef"[v % base];
        v /= base;
    } while (v);
    while (n)
        out[i++] = tmp[--n];
    return i;
}

/* Supports %d %u %x %s %c %% with an optional width. */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    size_t len = 0;
#define PUT(ch) do { if (len + 1 < size) buf[len] = (ch); len++; } while (0)
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            PUT(*fmt);
            continue;
        }
        int width = 0;
        for (fmt++; *fmt >= '0' && *fmt <= '9'; fmt++)
            width = width * 10 + (*fmt - '0');
        char num[16];
        const char *s = num;
        int n;
        switch (*fmt) {
        case 'd': {
            int v = va_arg(ap, int);
            n = 0;
            if (v < 0)
                num[n++] = '-', v = -v;
            n += utoa((unsigned)v, 10, num + n);
            break;
        }
        case 'u': n = utoa(va_arg(ap, unsigned), 10, num); break;
        case 'x': n = utoa(va_arg(ap, unsigned), 16, num); break;
        case 'c': num[0] = (char)va_arg(ap, int); n = 1; break;
        case 's': s = va_arg(ap, const char *); n = (int)strlen(s); break;
        case '%': num[0] = '%'; n = 1; break;
        default: num[0] = '?'; n = 1;
        }
        for (int i = n; i < width; i++)
            PUT(' ');
        for (int i = 0; i < n; i++)
            PUT(s[i]);
    }
    if (size)
        buf[len < size ? len : size - 1] = '\0';
#undef PUT
    return (int)len;
}

int printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    write(STDOUT, buf, n < (int)sizeof buf ? (size_t)n : sizeof buf - 1);
    return n;
}
