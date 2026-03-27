#include <stdio.h>
#include <fcntl.h>

static FILE _stdin_f  = { .fd = STDIN_FILENO,  .flags = _FILE_READ  };
static FILE _stdout_f = { .fd = STDOUT_FILENO, .flags = _FILE_WRITE };
static FILE _stderr_f = { .fd = STDERR_FILENO, .flags = _FILE_WRITE };

FILE *stdin  = &_stdin_f;
FILE *stdout = &_stdout_f;
FILE *stderr = &_stderr_f;

#define MAX_FILES 16

static FILE _file_pool[MAX_FILES];
static bool _file_pool_used[MAX_FILES];

int atoi(const char *s)
{
    int result = 0, sign = 1;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { result = result * 10 + (*s - '0'); s++; }
    return sign * result;
}

int itoa(int value, char *buf)
{
    char tmp[12];
    int i = 0, j = 0;
    bool neg = false;

    if (value == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    if (value < 0)  { neg = true; value = -value; }

    while (value > 0) { tmp[i++] = '0' + (value % 10); value /= 10; }
    if (neg) tmp[i++] = '-';
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
    return j;
}

int fputc(int c, FILE *f)
{
    if (!f || !(f->flags & _FILE_WRITE)) { if (f) f->flags |= _FILE_ERR; return EOF; }
    char ch = (char)c;
    if (write(f->fd, &ch, 1) != 1) { f->flags |= _FILE_ERR; return EOF; }
    return (unsigned char)c;
}

int fgetc(FILE *f)
{
    if (!f || !(f->flags & _FILE_READ)) { if (f) f->flags |= _FILE_ERR; return EOF; }
    if (f->buf_pos >= f->buf_len) {
        int n = read(f->fd, f->buf, BUFSIZ);
        if (n == 0) { f->flags |= _FILE_EOF; return EOF; }
        if (n <  0) { f->flags |= _FILE_ERR; return EOF; }
        f->buf_len = n;
        f->buf_pos = 0;
    }
    return (unsigned char)f->buf[f->buf_pos++];
}

typedef struct {
    FILE *file;
    char *buf;
    int   pos;
    int   size;
    int   written;
} _fmt_ctx;

static void _ctx_putc(_fmt_ctx *ctx, char c)
{
    ctx->written++;
    if (ctx->file) {
        fputc(c, ctx->file);
    } else if (ctx->buf && ctx->pos < ctx->size - 1) {
        ctx->buf[ctx->pos++] = c;
    }
}

static void _fmt_str(_fmt_ctx *ctx, const char *s)
{
    while (*s) _ctx_putc(ctx, *s++);
}

static void _fmt_int(_fmt_ctx *ctx, int value)
{
    char buf[12];
    int len = itoa(value, buf);
    for (int i = 0; i < len; i++) _ctx_putc(ctx, buf[i]);
}

static void _fmt_hex(_fmt_ctx *ctx, unsigned int value)
{
    if (value == 0) { _ctx_putc(ctx, '0'); return; }
    char buf[8];
    int i = 0;
    while (value > 0) {
        int d = value % 16;
        buf[i++] = d < 10 ? '0' + d : 'a' + (d - 10);
        value /= 16;
    }
    while (i--) _ctx_putc(ctx, buf[i]);
}

static int _vformat(_fmt_ctx *ctx, const char *fmt, va_list args)
{
    ctx->written = 0;
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 's': _fmt_str(ctx, va_arg(args, const char *)); break;
                case 'c': _ctx_putc(ctx, (char)va_arg(args, int));   break;
                case 'd': _fmt_int(ctx, va_arg(args, int));           break;
                case 'x': _fmt_hex(ctx, va_arg(args, unsigned int));  break;
                case '%': _ctx_putc(ctx, '%');                        break;
                default:  _ctx_putc(ctx, '%'); _ctx_putc(ctx, *fmt); break;
            }
        } else {
            _ctx_putc(ctx, *fmt);
        }
        fmt++;
    }
    return ctx->written;
}

int fputs(const char *str, FILE *f)
{
    while (*str) { if (fputc(*str++, f) == EOF) return EOF; }
    return 0;
}

char *fgets(char *buf, int size, FILE *f)
{
    if (!buf || size <= 0) return NULL;
    int i = 0;
    while (i < size - 1) {
        int c = fgetc(f);
        if (c == EOF) { if (i == 0) return NULL; break; }
        buf[i++] = (char)c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return buf;
}

int fread(void *ptr, int size, int count, FILE *f)
{
    char *p = (char *)ptr;
    int total = size * count, n = 0;
    while (n < total) {
        int c = fgetc(f);
        if (c == EOF) break;
        p[n++] = (char)c;
    }
    return n / size;
}

int fwrite(const void *ptr, int size, int count, FILE *f)
{
    const char *p = (const char *)ptr;
    int total = size * count;
    for (int i = 0; i < total; i++) {
        if (fputc(p[i], f) == EOF) return i / size;
    }
    return count;
}

int  feof(FILE *f)    { return f && (f->flags & _FILE_EOF); }
int  ferror(FILE *f)  { return f && (f->flags & _FILE_ERR); }
void clearerr(FILE *f){ if (f) f->flags &= ~(_FILE_EOF | _FILE_ERR); }

FILE *fopen(const char *path, const char *mode)
{
    int oflags = 0, fflags = 0;

    switch (mode[0]) {
        case 'r':
            oflags = (mode[1] == '+') ? O_RDWR              : O_RDONLY;
            fflags = (mode[1] == '+') ? _FILE_READ | _FILE_WRITE : _FILE_READ;
            break;
        case 'w':
            oflags = (mode[1] == '+') ? O_RDWR   | O_CREAT | O_TRUNC
                                      : O_WRONLY  | O_CREAT | O_TRUNC;
            fflags = (mode[1] == '+') ? _FILE_READ | _FILE_WRITE : _FILE_WRITE;
            break;
        case 'a':
            oflags = (mode[1] == '+') ? O_RDWR   | O_CREAT | O_APPEND
                                      : O_WRONLY  | O_CREAT | O_APPEND;
            fflags = (mode[1] == '+') ? _FILE_READ | _FILE_WRITE : _FILE_WRITE;
            break;
        default:
            return NULL;
    }

    int fd = open(path, oflags, 0644);
    if (fd < 0) return NULL;

    for (int i = 0; i < MAX_FILES; i++) {
        if (!_file_pool_used[i]) {
            _file_pool_used[i]    = true;
            _file_pool[i].fd      = fd;
            _file_pool[i].flags   = fflags;
            _file_pool[i].buf_pos = 0;
            _file_pool[i].buf_len = 0;
            return &_file_pool[i];
        }
    }

    close(fd);
    return NULL;
}

int fclose(FILE *f)
{
    if (!f) return EOF;
    int ret = close(f->fd);
    for (int i = 0; i < MAX_FILES; i++) {
        if (&_file_pool[i] == f) { _file_pool_used[i] = false; break; }
    }
    return ret == 0 ? 0 : EOF;
}

int vfprintf(FILE *f, const char *fmt, va_list args)
{
    _fmt_ctx ctx = { .file = f, .buf = NULL, .pos = 0, .size = 0, .written = 0 };
    return _vformat(&ctx, fmt, args);
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list args; va_start(args, fmt);
    int n = vfprintf(f, fmt, args);
    va_end(args);
    return n;
}

int vprintf(const char *fmt, va_list args) { return vfprintf(stdout, fmt, args); }

int printf(const char *fmt, ...)
{
    va_list args; va_start(args, fmt);
    int n = vprintf(fmt, args);
    va_end(args);
    return n;
}

int vsnprintf(char *str, int size, const char *fmt, va_list args)
{
    _fmt_ctx ctx = { .file = NULL, .buf = str, .pos = 0, .size = size, .written = 0 };
    int n = _vformat(&ctx, fmt, args);
    if (str && size > 0) str[ctx.pos] = '\0';
    return n;
}

int snprintf(char *str, int size, const char *fmt, ...)
{
    va_list args; va_start(args, fmt);
    int n = vsnprintf(str, size, fmt, args);
    va_end(args);
    return n;
}

int vsprintf(char *str, const char *fmt, va_list args)
{
    return vsnprintf(str, 0x7fffffff, fmt, args);
}

int sprintf(char *str, const char *fmt, ...)
{
    va_list args; va_start(args, fmt);
    int n = vsprintf(str, fmt, args);
    va_end(args);
    return n;
}

void putc(char c)          { fputc(c, stdout); }
void puts(const char *str) { fputs(str, stdout); }

void scanf(const char *fmt, ...)
{
    char buf[104];
    int n = read(STDIN_FILENO, buf, sizeof(buf) - 1);
    if (n <= 0) return;
    buf[n] = '\0';

    char *p = buf;
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == 'd') {
                int *iptr = va_arg(args, int *);
                int value = 0; bool neg = false;
                while (*p == ' ' || *p == '\n' || *p == '\t') p++;
                if (*p == '-') { neg = true; p++; }
                while (*p >= '0' && *p <= '9') { value = value * 10 + (*p - '0'); p++; }
                *iptr = neg ? -value : value;
            } else if (*fmt == 's') {
                char *str = va_arg(args, char *);
                while (*p == ' ' || *p == '\n' || *p == '\t') p++;
                while (*p && *p != ' ' && *p != '\n' && *p != '\t') *str++ = *p++;
                *str = '\0';
            }
        }
        fmt++;
    }

    va_end(args);
}