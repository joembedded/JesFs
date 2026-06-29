# JesFs Quick Start - Zephyr and Bare Metal

JesFs, Jo's Embedded Serial File System, is a small file system for SPI NOR flash in IoT and low-power devices. It is intentionally not a POSIX file system: there are no directories and no generic OS-style abstraction. Instead, JesFs provides robust, flat file storage for configuration, measurement data, log files, OTA files, and cloud synchronization.

This file is the compact introduction for the **Zephyr** port in this project. The more detailed original documentation is located at [https://github.com/joembedded/JesFs](https://github.com/joembedded/JesFs), especially in `README.md` and `Documentation\Use_JesFs_en.md`.

## What JesFs Is For

- Small external or internal NOR flash memories, typically with 4096-byte sectors.
- Data loggers and blackbox logs that must survive power loss or reset.
- Parameter, configuration, language, or resource files.
- Firmware files for OTA workflows, optionally together with a bootloader.
- Mirroring selected files to a server or cloud twin.
- Optimised for maximum transfer speed: fast writes, fast reads.

JesFs prioritizes simplicity, deterministic structure, and low resource usage. Typical use needs only a few hundred bytes of RAM, a flat file system, and up to about 1000 files on 4-kB-sector flash. On smaller flash devices, the number of sectors naturally limits the number of files.

## Core Idea

NOR flash can directly program bits only from `1` to `0`. To set bits back to `1`, a whole sector must be erased. JesFs uses this property instead of hiding it.

The flash structure is deliberately simple:

- **Sector 0: Index**: Contains the JesFs magic value, flash ID, format timestamp, followed by a table of pointers to file start sectors.
- **HEAD sector**: Every file starts with a HEAD. It stores the file name, length, CRC, timestamp, flags, and the link to the next sector.
- **DATA sectors**: Additional sectors of a file form a singly linked list. Each DATA sector knows its owner and the next sector.
- **Freeing by marking**: Deleting first means only marking HEAD and DATA sectors as "to delete". When they are reused later, the sector is erased and written again. This gives simple wear leveling over the sector pool.

## Zephyr Port in This Project

In the **original bare-metal** version, the low-level driver must implement SPI commands, wakeup, deep power down, write enable, busy polling, reads, writes, and erases.

In **Zephyr**, the flash driver does this work. The port in this project is therefore smaller:

- `jesfs_hl.c`: High-level file API, platform-independent.
- `jesfs_ml.c`: Medium-level flash management, mostly platform-independent.
- `jesfs_ll_zephyr.c`: Wrapper around the Zephyr flash API, JEDEC ID, and runtime PM.
- `jesfs.h`: Public API, flags, error codes, and data structures.
- `jesfs_shell.c`: Test and diagnostic commands for the console.

## API Names

The Zephyr port uses the `jesfs_` API names, for example `jesfs_open()` and
`jesfs_read()`. This newer naming makes JesFs calls easy to distinguish from
Zephyr's own filesystem APIs, which also use the `fs_` prefix.

For legacy and bare-metal applications, the historic `fs_` names are still
available as compatibility macros in `jesfs.h`. For example, non-Zephyr builds
can still call `fs_open()`, which maps to `jesfs_open()`. Only Zephyr uses the
newer, more clearly distinguishable `jesfs_` names exclusively.

Important project options are in `prj.conf`:

```conf
CONFIG_JESFS_SHELL=y
CONFIG_FLASH=y
CONFIG_SPI_NOR=y
CONFIG_CRC=y
CONFIG_PM_DEVICE=y
CONFIG_PM_DEVICE_RUNTIME=y
CONFIG_SPI_NOR_FLASH_LAYOUT_PAGE_SIZE=4096
CONFIG_FLASH_JESD216_API=y
CONFIG_SPI_NOR_SFDP_RUNTIME=y
```

The SPI NOR flash must exist in devicetree as `jedec,spi-nor` and must be `okay`. `jesfs_ll_zephyr.c` uses that flash device directly.

## Minimal Code Usage

```c
#include "jesfs/jesfs.h"

int16_t res;
struct jesfs_desc desc;

res = jesfs_start(FS_START_NORMAL);
if (res == JESFS_ERR_BAD_MAGIC || res == JESFS_ERR_BAD_MAGIC_HEADER) {
	res = jesfs_format(FS_FORMAT_SOFT, NULL);
	if (res == 0) {
		res = jesfs_start(FS_START_NORMAL);
	}
}
if (res != 0) {
	return res;
}

res = jesfs_open(&desc, "hello.txt", SF_OPEN_CREATE | SF_OPEN_WRITE | SF_OPEN_CRC);
if (res == 0) {
	const uint8_t msg[] = "Hello JesFs\n";

	res = jesfs_write(&desc, msg, sizeof(msg) - 1);
	if (res == 0) {
		res = jesfs_close(&desc);
	}
}
```

Always check return values:

- `0`: success, or end of file for `jesfs_read()`.
- `> 0`: number of bytes read for `jesfs_read()`; status bits for `jesfs_info()`.
- `< 0`: error code, see `jesfs.h`.

## Lifecycle

```c
jesfs_start(FS_START_NORMAL);   /* full scan */
jesfs_start(FS_START_FAST);     /* faster scan with fewer checks */
jesfs_start(FS_START_RESTART);  /* very fast wakeup when state is already known */
```

In the Zephyr port, `jesfs_format()` has an optional progress callback:

```c
void progress_cb(uint32_t cur_sect, uint32_t total_sect);
jesfs_format(FS_FORMAT_SOFT, progress_cb);
```

`FS_FORMAT_SOFT` erases only non-empty sectors and is the sensible choice for normal use. `jesfs_deepsleep()` puts the file system into sleep state; before the next access, wake it with `jesfs_start(FS_START_RESTART)`.

## Reading and Writing Files

### Writing a File

```c
struct jesfs_desc desc;
int16_t res;

res = jesfs_open(&desc, "config.txt", SF_OPEN_CREATE | SF_OPEN_WRITE | SF_OPEN_CRC);
if (res == 0) {
	res = jesfs_write(&desc, data, data_len);
	if (res == 0) {
		res = jesfs_close(&desc);
	}
}
```

`SF_OPEN_CREATE` creates a new file. If the file already exists, the old file is marked as deleted and a new HEAD is used.

### Reading a File

```c
struct jesfs_desc desc;
uint8_t buf[128];
int32_t rd;

if (jesfs_open(&desc, "config.txt", SF_OPEN_READ | SF_OPEN_CRC) == 0) {
	do {
		rd = jesfs_read(&desc, buf, sizeof(buf));
		if (rd > 0) {
			/* process buf[0..rd-1] */
		}
	} while (rd > 0);

	(void)jesfs_close(&desc);
}
```

When `SF_OPEN_CRC` is set, physical reads update the running CRC in the descriptor.

Throughput note for this nRF54L15 DK project with an 8 MHz SPI clock:

- Write to flash is about 70 kB/s.
- Silent reads, for example `jesfs_read(desc, NULL, len)` while finding the end
  of an unclosed file, are about 20 MB/s because no payload is copied from the
  flash driver.
- Normal reads with data transfer and CRC tracking are about 500 kB/s.

### Deleting a File

```c
struct jesfs_desc desc;

if (jesfs_open(&desc, "old.txt", SF_OPEN_READ | SF_OPEN_RAW) == 0) {
	(void)jesfs_delete(&desc);
}
```

Deleting marks all sectors of the file as reusable. The descriptor is invalid afterwards.

### Renaming a File

`jesfs_rename()` does not take path strings. The source file and a temporary target file must both be open. The target descriptor must describe a newly created empty file and must not be opened with `SF_OPEN_READ` or `SF_OPEN_RAW`.

Closed file content and finalized metadata are immutable. JesFs cannot continue
writing file data, length, or CRC after `jesfs_close()`, because the affected
flash entries have already been programmed. To change content, create a
replacement file under a temporary or new name, verify and close it, then use the
application update flow to retire the old file and publish the replacement.
`jesfs_rename()` is only a metadata operation: the target descriptor must be an
empty staging file and supplies the new name and flags, while the source file
keeps its data, length, CRC, sector chain, and creation time.

```c
struct jesfs_desc old_desc;
struct jesfs_desc new_desc;
int16_t res;

res = jesfs_open(&old_desc, "old.txt", SF_OPEN_READ | SF_OPEN_CRC);
if (res == 0) {
	jesfs_set_static_secs(old_desc.file_ctime); /* keep the original creation time */
	res = jesfs_open(&new_desc, "new.txt", SF_OPEN_CREATE | SF_OPEN_CRC);
	jesfs_set_static_secs(0);
}
if (res == 0) {
	res = jesfs_rename(&old_desc, &new_desc);
}
```

After a successful rename, `old_desc` still points to the same file object and can continue to be used at its current file position. The target descriptor is consumed by `jesfs_rename()` and must not be used afterwards. Rename copies the new name and file flags from the target descriptor, but keeps the original file length, CRC, data, sector chain, and creation time.

## Unclosed Files / RAW Mode

Unclosed files are a central JesFs idea for loggers. A RAW file does not need to be closed. After reset or power loss, JesFs can find the end because unwritten flash is `0xFF`.

Do not call `jesfs_close()` just to clean up an intentionally unclosed RAW file.
For normal write files, `jesfs_close()` finalizes the HEAD entry by writing the
final length and CRC. That finalization write can happen only once per flash
entry. A later attempt to continue or finalize the same entry would need to
rewrite already-programmed metadata and is therefore rejected with a JesFs error.
Leaving a RAW logger unclosed is the intended API usage when the file must stay
appendable across resets.

Typical append pattern:

```c
struct jesfs_desc log_desc;
int16_t res;

res = jesfs_open(&log_desc, "log.txt", SF_OPEN_READ | SF_OPEN_RAW);
if (res == 0) {
	(void)jesfs_read(&log_desc, NULL, 0xFFFFFFFF); /* jump to the real end */
} else {
	res = jesfs_open(&log_desc, "log.txt", SF_OPEN_CREATE | SF_OPEN_RAW | SF_OPEN_EXT_SYNC);
}

if (res == 0) {
	(void)jesfs_write(&log_desc, line, line_len);
	/* RAW loggers normally stay open or are not finalized with jesfs_close(). */
}
```

Important: payload data in unclosed RAW files should not contain plain `0xFF` bytes. For binary data, use an escape rule or ASCII/Base64 encoding; otherwise the end search may stop too early.

Avoid `SF_OPEN_CRC` for files that are intended to remain unclosed. CRC is most
useful for fixed data that must be verified later, for example firmware images,
resource bundles, or configuration snapshots that are written once, closed, and
then only read. A CRC-protected file has a meaningful stored CRC only after
`jesfs_close()` has written the final file length and CRC to the HEAD sector.

## Flags

| Flag | Meaning |
|------|---------|
| `SF_OPEN_READ` | Read a file |
| `SF_OPEN_CREATE` | Create a new file, replacing an existing file |
| `SF_OPEN_WRITE` | Write a file and finalize it on `jesfs_close()` |
| `SF_OPEN_RAW` | Raw access, important for unclosed files and delete |
| `SF_OPEN_CRC` | Track CRC32 while reading/writing |
| `SF_OPEN_EXT_SYNC` | Application flag for external synchronization, not interpreted by the core |

`SF_XOPEN_UNCLOSED` is not an open flag. It is set informatively by `jesfs_open()` or `jesfs_info()` when a file was not finalized with `jesfs_close()`.

## File and Disk Information

```c
struct jesfs_stat stat;

for (uint16_t i = 0;; i++) {
	int16_t res = jesfs_info(&stat, i);

	if (res == 0 || res == FS_STAT_INDEX) {
		break;
	}
	if (res < 0) {
		/* structure or flash error */
		break;
	}
	if (res & FS_STAT_ACTIVE) {
		/* stat.fname, stat.file_len, stat.file_crc32, stat.disk_flags */
	}
}
```

Global disk data is stored in `sflash_info`, for example `total_flash_size`, `available_disk_size`, `files_active`, and `files_used`. This structure is useful for diagnostics; normal application logic should prefer the API functions.

## Integrity and Diagnostics

```c
int16_t res = jesfs_check_disk(my_printf_like_callback);
```

`jesfs_check_disk()` starts a normal scan and checks the index, sector lists, invalid sectors, unclosed files, and CRC files. It is suitable for development, service menus, or recovery decisions, not for every measurement cycle.

CRC32 uses ISO 3309. CRC is meaningful when files were written with
`SF_OPEN_CRC` and closed. Unclosed RAW files naturally cannot have a final CRC
stored in the header.

`jesfs_check_disk()` reports an unclosed file with `SF_OPEN_CRC` as an error. If
a valid CRC exists for a JesFs file, the file should also have been finalized by
`jesfs_close()`. Seeing the CRC flag on an unclosed file means the final length
and CRC could not be trusted; typical causes are power loss, reset, or another
interruption during the write/finalize sequence.

## Public API Overview

The application-facing JesFs functions in `jesfs.h` are intentionally small. Most of them also have a direct shell command in `jesfs_shell.c`; the remaining helpers are still part of the public API and can be used from application code.

| API | Purpose | Shell |
|-----|---------|-------|
| `jesfs_start(mode)` | Wake and scan the filesystem. Use `FS_START_NORMAL`, `FS_START_FAST`, or `FS_START_RESTART`. | `file start [fast\|restart]` |
| `jesfs_deepsleep()` | Put the flash/filesystem into low-power sleep state. | `file deepsleep` |
| `jesfs_format(fmode, cb)` | Format the filesystem. In the Zephyr port the callback may report progress. | `file format` |
| `jesfs_open(desc, name, flags)` | Open an existing file or create a new one, depending on flags. | `file open <name> [flags]` |
| `jesfs_close(desc)` | Finalize a write file and invalidate the descriptor. | `file close` |
| `jesfs_read(desc, dst, len)` | Read data or advance silently when `dst == NULL`. Returns byte count or error. | `file read [len]` |
| `jesfs_write(desc, data, len)` | Append/write data through the open descriptor. | `file write <text>`, `file chunkwrite <len> [chunk]` |
| `jesfs_delete(desc)` | Mark an opened file as deleted. The descriptor is invalid afterwards. | `file delete` |
| `jesfs_rename(old_desc, new_desc)` | Rename by using an opened source and a temporary opened target descriptor. | `file rename <new-name>` |
| `jesfs_info(stat, index)` | Enumerate files and disk metadata by index. | `file dir` |
| `jesfs_check_disk(cb)` | Run a structural and CRC diagnostic scan. | `file check` |
| `jesfs_rewind(desc)` | Reset an opened read descriptor to the beginning and reset its running CRC. | No shell command; available in the API. |
| `jesfs_notexists(name)` | Convenience existence check; returns `0` if the file exists, otherwise a negative JesFs error. | No shell command; available in the API. |
| `jesfs_get_crc32(desc)` | Read the stored CRC32 from an opened descriptor's HEAD sector. | No shell command; available in the API. |
| `jesfs_sec1970_to_date(sec, date)` | Convert Unix seconds to `struct jesfs_date`. | No shell command; available in the API. |
| `jesfs_date_to_sec1970(date)` | Convert `struct jesfs_date` to Unix seconds. | No shell command; available in the API. |
| `jesfs_set_static_secs(sec)` | Override JesFs creation timestamps, mainly for tests or metadata-preserving operations such as rename. Reset with `0`. | No direct shell command; `file rename` uses it internally. |

## Shell Commands in This Project

`jesfs_shell.c` provides a simple test interface. Commands are called through `file ...`:

```text
file start [fast|restart]
file format
file dir
file check
file open <name> [flags]
file write <text>
file chunkwrite <len> [chunk]
file read [len]
file close
file delete
file rename <new-name>
file deepsleep
file ll jedec
file ll read <addr> [len]
file ll write <addr> <byte>...
file ll erase <addr> <len>
```

Open flags in the shell are single letters:

| Shell | API flag |
|-------|----------|
| `r` | `SF_OPEN_READ` |
| `t` | `SF_OPEN_CREATE` |
| `w` | `SF_OPEN_WRITE` |
| `a` | `SF_OPEN_RAW` |
| `c` | `SF_OPEN_CRC` |
| `x` | `SF_OPEN_EXT_SYNC` |

Examples:

```text
file start
file format
file open cfg.txt twc
file write VERSION=1
file close
file open cfg.txt rc
file rename cfg2.txt
file read 64
file close
file dir
file check
file deepsleep
file start restart
```

## Limits and Rules

- No POSIX, no directories, no path semantics.
- File names are limited by `FNAMELEN`, currently 21 characters.
- The design expects 4096-byte sectors (`SF_SECTOR_PH`).
- Concurrent access from multiple threads needs external synchronization, for example a mutex.
- Every fallible API function must be checked.
- `jesfs_supply_voltage_check()` should be implemented meaningfully before write operations. In the test project it currently always returns "OK".
- Many small `jesfs_write()` calls work, but buffered write blocks are more efficient.
- `jesfs_start(FS_START_NORMAL)` and `jesfs_check_disk()` are startup/diagnostic operations, not hot-loop functions.

## Rule of Thumb

JesFs is strongest when files are treated as robust, sequential objects that match NOR flash behaviour: write, optionally finalise, later read, delete, or synchronise externally. For very fast continuous logs, RAW/unclosed mode is the intended special case: less POSIX-like, but highly effective for power-loss-safe IoT data with high throughput and low energy use.
