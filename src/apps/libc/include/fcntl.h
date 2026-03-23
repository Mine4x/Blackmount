#ifndef _FCNTL_H
#define _FCNTL_H

#include <unistd.h>
#include <stdint.h>
#include <errno.h>

#ifndef O_RDONLY
#define O_RDONLY    0x0000
#endif
#ifndef O_WRONLY
#define O_WRONLY    0x0001
#endif
#ifndef O_RDWR
#define O_RDWR      0x0002
#endif
#ifndef O_ACCMODE
#define O_ACCMODE   0x0003
#endif
#ifndef O_CREAT
#define O_CREAT     0x0040
#endif
#ifndef O_EXCL
#define O_EXCL      0x0080
#endif
#ifndef O_NOCTTY
#define O_NOCTTY    0x0100
#endif
#ifndef O_TRUNC
#define O_TRUNC     0x0200
#endif
#ifndef O_APPEND
#define O_APPEND    0x0400
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK  0x0800
#endif
#ifndef O_DSYNC
#define O_DSYNC     0x1000
#endif
#ifndef O_SYNC
#define O_SYNC      0x101000
#endif
#ifndef O_RSYNC
#define O_RSYNC     0x101000
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY 0x10000
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW  0x20000
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC   0x80000
#endif
#ifndef O_LARGEFILE
#define O_LARGEFILE 0x8000
#endif

#ifndef F_DUPFD
#define F_DUPFD         0
#endif
#ifndef F_GETFD
#define F_GETFD         1
#endif
#ifndef F_SETFD
#define F_SETFD         2
#endif
#ifndef F_GETFL
#define F_GETFL         3
#endif
#ifndef F_SETFL
#define F_SETFL         4
#endif
#ifndef F_GETLK
#define F_GETLK         5
#endif
#ifndef F_SETLK
#define F_SETLK         6
#endif
#ifndef F_SETLKW
#define F_SETLKW        7
#endif
#ifndef F_DUPFD_CLOEXEC
#define F_DUPFD_CLOEXEC 1030
#endif

#ifndef FD_CLOEXEC
#define FD_CLOEXEC 1
#endif

#ifndef AT_FDCWD
#define AT_FDCWD -100
#endif

#endif