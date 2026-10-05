/* vsnprintf: %d %i %u %x %X %p %s %c %%, with '-', '0', width, precision for %s, and the 'l' modifier. */
#include <stdarg.h>
#include <stdint.h>
#include <console.h>

struct out {
    char *buf;
    size_t size, len;
};

static void put(struct out *o, char c)
{
    if (o->len + 1 < o->size)
        o->buf[o->len] = c;
    o->len++;
}

static void put_padded(struct out *o, const char *s, int n, int width, int left, char pad)
{
    if (!left)
        for (int i = n; i < width; i++)
            put(o, pad);
    for (int i = 0; i < n; i++)
        put(o, s[i]);
    if (left)
        for (int i = n; i < width; i++)
            put(o, ' ');
}

static int utoa(uint32_t v, unsigned base, int upper, char *tmp)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    do {
        tmp[n++] = digits[v % base];
        v /= base;
    } while (v);
    for (int i = 0; i < n / 2; i++) {
        char c = tmp[i];
        tmp[i] = tmp[n - 1 - i];
        tmp[n - 1 - i] = c;
    }
    return n;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    struct out o = { buf, size, 0 };
    char tmp[16];

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            put(&o, *fmt);
            continue;
        }
        int left = 0, width = 0, prec = -1;
        char pad = ' ';
        fmt++;
        for (;; fmt++) {
            if (*fmt == '-')
                left = 1;
            else if (*fmt == '0')
                pad = '0';
            else
                break;
        }
        for (; *fmt >= '0' && *fmt <= '9'; fmt++)
            width = width * 10 + (*fmt - '0');
        if (*fmt == '.')
            for (prec = 0, fmt++; *fmt >= '0' && *fmt <= '9'; fmt++)
                prec = prec * 10 + (*fmt - '0');
        while (*fmt == 'l')
            fmt++;

        switch (*fmt) {
        case 'd':
        case 'i': {
            int32_t v = va_arg(ap, int32_t);
            int n = 0;
            if (v < 0) {
                tmp[n++] = '-';
                n += utoa(-(uint32_t)v, 10, 0, tmp + 1);
            } else {
                n = utoa(v, 10, 0, tmp);
            }
            put_padded(&o, tmp, n, width, left, pad);
            break;
        }
        case 'u':
            put_padded(&o, tmp, utoa(va_arg(ap, uint32_t), 10, 0, tmp), width, left, pad);
            break;
        case 'x':
        case 'X':
            put_padded(&o, tmp, utoa(va_arg(ap, uint32_t), 16, *fmt == 'X', tmp), width, left, pad);
            break;
        case 'p':
            put(&o, '0');
            put(&o, 'x');
            put_padded(&o, tmp, utoa((uint32_t)va_arg(ap, void *), 16, 0, tmp), 8, 0, '0');
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            int n = 0;
            while (s[n] && (prec < 0 || n < prec))
                n++;
            put_padded(&o, s, n, width, left, ' ');
            break;
        }
        case 'c':
            tmp[0] = (char)va_arg(ap, int);
            put_padded(&o, tmp, 1, width, left, ' ');
            break;
        case '%':
            put(&o, '%');
            break;
        case '\0':
            fmt--;
            break;
        default:
            put(&o, '%');
            put(&o, *fmt);
        }
    }
    if (size)
        buf[o.len < size ? o.len : size - 1] = '\0';
    return (int)o.len;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}
