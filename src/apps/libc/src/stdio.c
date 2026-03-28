#include <stdio.h>

void putc(char c)
{
    write(STDOUT, &c, sizeof(c));
}

void puts(const char* str)
{
    while (*str)
    {
        putc(*str++);
    }
}

int format(char *out, int size, const char *fmt, va_list args)
{
    int pos = 0;

    for (const char *p = fmt; *p && pos < size - 1; p++) {
        if (*p != '%') {
            out[pos++] = *p;
            continue;
        }

        p++;

        if (*p == 's') {
            const char *s = va_arg(args, const char *);
            while (*s && pos < size - 1)
                out[pos++] = *s++;
        } else if (*p == 'd') {
            int value = va_arg(args, int);
            char buf[32];
            int i = 0, neg = 0;

            if (value == 0) {
                buf[i++] = '0';
            } else {
                if (value < 0) {
                    neg = 1;
                    value = -value;
                }
                while (value > 0) {
                    buf[i++] = '0' + (value % 10);
                    value /= 10;
                }
                if (neg)
                    buf[i++] = '-';
            }

            while (i > 0 && pos < size - 1)
                out[pos++] = buf[--i];
        } else if (*p == 'x') {
            unsigned int value = va_arg(args, unsigned int);
            char buf[32];
            int i = 0;

            if (value == 0) {
                buf[i++] = '0';
            } else {
                while (value > 0) {
                    int d = value % 16;
                    buf[i++] = d < 10 ? '0' + d : 'a' + (d - 10);
                    value /= 16;
                }
            }

            while (i > 0 && pos < size - 1)
                out[pos++] = buf[--i];
        } else if (*p == 'c') {
            out[pos++] = (char)va_arg(args, int);
        } else if (*p == '%') {
            out[pos++] = '%';
        }
    }

    out[pos] = '\0';
    return pos;
}

int vfdprintf(int fd, const char* fmt, va_list args)
{
    char buf[1024];
    int len = format(buf, sizeof(buf), fmt, args);
    write(fd, buf, len);
    return len;
}

int fdprintf(int fd, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int len = vfdprintf(fd, fmt, args);
    va_end(args);
    return len;
}

void printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfdprintf(STDOUT, fmt, args);
    va_end(args);
}

void errorf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfdprintf(STDOUT, fmt, args);
    va_end(args);
}

int snprintf(char *out, int size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int len = format(out, size, fmt, args);
    va_end(args);
    return len;
}

int sprintf(char *str, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int len = format(str, 1 << 30, fmt, args);
    va_end(args);
    return len;
}

void scanf(const char* fmt, ...)
{
    char buf[104];
    int n = read(STDIN, buf, sizeof(buf) - 1);
    if (n <= 0) return;

    buf[n] = '\0';
    char* p = buf;

    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;

            if (*fmt == 'd') {
                int* iptr = va_arg(args, int*);
                int value = 0;
                bool neg = false;

                while (*p == ' ' || *p == '\n' || *p == '\t') p++;

                if (*p == '-') {
                    neg = true;
                    p++;
                }

                while (*p >= '0' && *p <= '9') {
                    value = value * 10 + (*p - '0');
                    p++;
                }

                *iptr = neg ? -value : value;
            } else if (*fmt == 's') {
                char* str = va_arg(args, char*);

                while (*p == ' ' || *p == '\n' || *p == '\t') p++;

                while (*p && *p != ' ' && *p != '\n' && *p != '\t') {
                    *str++ = *p++;
                }

                *str = '\0';
            }
        }
        fmt++;
    }

    va_end(args);
}

char *fgets(char *buf, int size, int fd)
{
    if (!buf || size <= 0)
        return NULL;

    int n = read(fd, buf, size - 1);
    if (n <= 0)
        return NULL;

    buf[n] = '\0';

    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n') {
            buf[i + 1] = '\0';
            break;
        }
    }

    return buf;
}

int atoi(const char *s)
{
    int result = 0;
    int sign = 1;

    while (*s == ' ' || *s == '\t')
        s++;

    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }

    return sign * result;
}