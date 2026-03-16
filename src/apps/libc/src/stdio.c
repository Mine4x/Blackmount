#include <stdio.h>

void putc(char c)
{
    write(STDOUT, &c, sizeof(c));
}

void puts(const char* str)
{
    while (*str)
    {
        putc(*str);
        str++;
    }
}

void print_int(int value)
{
    char buffer[12]; // Enough for 32-bit int
    int i = 0;
    bool negative = false;

    if (value == 0) {
        putc('0');
        return;
    }

    if (value < 0) {
        negative = true;
        value = -value;
    }

    while (value > 0) {
        buffer[i++] = '0' + (value % 10);
        value /= 10;
    }

    if (negative) {
        buffer[i++] = '-';
    }

    // Print in reverse
    while (i--) {
        putc(buffer[i]);
    }
}

void print_hex(unsigned int value)
{
    char buffer[9]; // 8 digits for 32-bit + null
    int i = 0;

    if (value == 0) {
        putc('0');
        return;
    }

    while (value > 0) {
        int digit = value % 16;
        if (digit < 10)
            buffer[i++] = '0' + digit;
        else
            buffer[i++] = 'a' + (digit - 10);
        value /= 16;
    }

    while (i--) {
        putc(buffer[i]);
    }
}

void printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 's': {
                    const char* str = va_arg(args, const char*);
                    puts(str);
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    putc(c);
                    break;
                }
                case 'd': {
                    int num = va_arg(args, int);
                    print_int(num);
                    break;
                }
                case 'x': {
                    unsigned int num = va_arg(args, unsigned int);
                    print_hex(num);
                    break;
                }
                case '%': {
                    putc('%');
                    break;
                }
                default: {
                    // Unknown specifier, print as-is
                    putc('%');
                    putc(*fmt);
                    break;
                }
            }
        } else {
            putc(*fmt);
        }
        fmt++;
    }

    va_end(args);
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
            }

            else if (*fmt == 's') {
                char* str = va_arg(args, char*);

                // skip whitespace
                while (*p == ' ' || *p == '\n' || *p == '\t') p++;

                // copy characters until whitespace
                while (*p && *p != ' ' && *p != '\n' && *p != '\t') {
                    *str++ = *p++;
                }

                *str = '\0'; // terminate string
            }
        }
        fmt++;
    }

    va_end(args);
}