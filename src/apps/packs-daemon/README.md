# Blackmount "Packs" Package Manager

`packs` is the system package manager daemon for Blackmount OS.  It runs as a
background process and listens on a Unix domain socket.  Any program can send
requests to install, remove, list, or query packages.

---

## Package Layout

An unpacked package is a directory containing:

```
myapp/
├── package.conf          # required metadata
├── myapp                 # any files that should be installed system-wide
└── myapp.conf
```

### package.conf fields

| Key     | Required | Description                                    |
|---------|----------|------------------------------------------------|
| `name`  | yes      | Package identifier, e.g. `myapp`               |
| `type`  | yes      | `0` = executable, `1` = service                |
| `strv`  | yes      | Human-readable version string, e.g. `1.0.2`    |
| `intv`  | yes      | Numeric version, e.g. `102`                    |
| `files` | no       | Files to install to system paths (see below)   |

#### files= format

```
files=<srcname>:<destpath>[;<srcname>:<destpath>...]
```

`srcname` is the filename inside the package directory.  `destpath` is the
absolute path where it should be installed on the system.

**Example:**

```conf
name=myapp
type=0
strv=1.2.0
intv=120
files=myapp:/bin/myapp;myapp.conf:/etc/myapp.conf
```

---

## Socket Protocol

The daemon listens on `/tmp/packs.sock` (AF_UNIX, SOCK_STREAM).

### Request packet — `request_t` (fixed size)

| Field  | Type             | Size | Notes                                           |
|--------|------------------|------|-------------------------------------------------|
| `t`    | `request_type_t` | 4 B  | See request types below                         |
| `data` | `char[124]`      | 124 B| Path (INSTALL) or package name (REMOVE / INFO)  |

### Request types

| Value | Name      | `data` field          |
|-------|-----------|-----------------------|
| `0`   | `INSTALL` | Path to package dir   |
| `1`   | `REMOVE`  | Package name          |
| `2`   | `LIST`    | Unused                |
| `3`   | `INFO`    | Package name          |

### Response — `response_t` header (fixed size) + optional payload

| Field     | Type       | Size   | Notes                                       |
|-----------|------------|--------|---------------------------------------------|
| `status`  | `int`      | 4 B    | `0` = success, negative = error             |
| `data_len`| `int`      | 4 B    | Byte length of payload that follows         |
| `message` | `char[240]`| 240 B  | NUL-terminated human-readable status string |

If `data_len > 0`, read exactly that many additional bytes from the socket
after the response header.

---

## Installation paths

| Path                        | Purpose                                    |
|-----------------------------|--------------------------------------------|
| `/etc/packs/`               | packs root directory                       |
| `/etc/packs/installed/<n>/` | Permanent copy of each installed package   |
| `/etc/packs/packages.db`    | Binary database of installed packages      |

---

## Example client

```c
int connect_to_packs()
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/tmp/packs.sock", sizeof(addr.sun_path) - 1);
    if (connect(fd, &addr, sizeof(addr)) < 0) { close(fd); return -1; }
    return fd;
}

void packs_install(const char* pakpath)
{
    int fd = connect_to_packs();

    request_t req = {0};
    req.t = INSTALL;
    strncpy(req.data, pakpath, sizeof(req.data) - 1);
    send(fd, &req, sizeof(req), 0);

    response_t resp = {0};
    recv(fd, &resp, sizeof(resp), MSG_WAITALL);
    printf("status=%d  %s\n", resp.status, resp.message);

    close(fd);
}
```