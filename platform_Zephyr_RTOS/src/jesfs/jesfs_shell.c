/*
 * JesFs shell commands for Zephyr.
 *
 * The shell intentionally exposes both the public JesFs API and a small set of
 * low-level flash diagnostics. The low-level commands are useful for board
 * bring-up; normal filesystem tests should use start/format/open/read/write/....
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/kernel.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include <zephyr/drivers/flash.h> // JEDEC/SFDP diagnostics.

#include <zephyr/sys/util.h>

#include "app.h"
#include "jesfs_shell.h"

#include "jesfs.h"

#include "jesfs_int.h" // Only for Flash Internals

//=========== Helper Functions ===============
//=== Platform specific ===
uint32_t jesfs_time_get(void) { return tb_unix_time_get(); }

// Mostly all modern CPUs have internal high-speed A/D. Ideal for checking power.
// Since V1.89 power is checked on jesfs_start() and all global write-functions on entry.
// Normal JesFs write/modify operations need only a few milliseconds.
int16_t jesfs_supply_voltage_check(void)
{
	return 0; // 0: Assume Power OK (Failure if <>0)
}

#define DBUF_SIZE 24
// Format timestamps for compact shell output.
void conv_secs_to_date_buffer(uint32_t secs, char *date_buffer, uint32_t date_buffer_size)
{
	struct jesfs_date fs_date;
	jesfs_sec1970_to_date(secs, &fs_date);
	if (secs < 1000000000) { // max 11k days
		snprintf(date_buffer, date_buffer_size, "PwrOn+%u.%02u:%02u:%02u", (secs / 86400),
			 fs_date.h, fs_date.min, fs_date.sec);
	} else {
		snprintf(date_buffer, date_buffer_size, "%02u.%02u.%04u %02u:%02u:%02u", fs_date.d,
			 fs_date.m, fs_date.a, fs_date.h, fs_date.min, fs_date.sec);
	}
}

//=========== JesFs Shell ===============

// Optional low-level JEDEC/SFDP diagnostics.
static int16_t js_subcmd_jedec_command(uint8_t flags, const struct device *flash_dev)
{

	uint32_t jedec_id = zephyr_get_flash_jedec_id();
	if (flash_dev == NULL) {
		tb_log(flags, "ERROR:Flash device not found\n");
		return -ENODEV;
	}
	// Flash was found; check the JEDEC ID (0xFFFFFFFF / 0x0 indicate errors).
	tb_log(flags, "Flash: '%s'\n", flash_dev->name);

	tb_log(flags, "JEDEC ID: 0x%08X\n", jedec_id);

	uint64_t flash_size;
	int rc = flash_get_size(flash_dev, &flash_size);
	if (rc != 0) {
		tb_log(flags, "ERROR:flash_get_size(): %d\n", rc);
		return rc;
	}
	tb_log(flags, "Flash size: %u kB\n", (uint32_t)(flash_size / 1024));

	struct flash_pages_info info;
	rc = flash_get_page_info_by_offs(flash_dev, 0, &info);
	if (rc != 0) {
		tb_log(flags, "ERROR:flash_get_page_info_by_offs(): %d\n", rc);
		return rc;
	}

	tb_log(flags, "Flash Page: %u Byte\n", (uint32_t)info.size);
	if (info.size != SF_SECTOR_PH) {
		tb_log(flags, "ERROR:Flash page size\n");
		return -EINVAL;
	}

#if defined(CONFIG_SPI_NOR_SFDP_RUNTIME)
/*
 * The nRF54L15-DK SPI NOR node exposes SFDP data through Zephyr when
 * CONFIG_SPI_NOR_SFDP_RUNTIME=y. See src/jesfs/sfdp_decode.md for details.
 */
#define SFDP_DUMP_LEN 128 // Includes headers; usually enough for the BFPT.
	uint8_t sfdp[SFDP_DUMP_LEN];
	memset(sfdp, 0, sizeof(sfdp));

	rc = flash_sfdp_read(flash_dev, 0, sfdp, sizeof(sfdp));
	if (rc != 0) {
		tb_log(flags, "ERROR:flash_sfdp_read(): %d\n", rc);
		return rc;
	}

	if ((sfdp[0] == 'S') && (sfdp[1] == 'F') && (sfdp[2] == 'D') && (sfdp[3] == 'P')) {
		tb_log(flags, "SFDP signature: OK\n");
	} else {
		tb_log(flags,
		       "WARNING:Expected SFDP signature 53 46 44 50, got %02X %02X %02X %02X\n",
		       sfdp[0], sfdp[1], sfdp[2], sfdp[3]);
	}

	tb_log(flags, "SFDP=[%u]:={\n ", sizeof(sfdp));
	for (uint16_t i = 0; i < sizeof(sfdp); i++) {
		tb_log(flags, " %02X", sfdp[i]);
		if ((i % 16) == 15) {
			tb_log(flags, "\n ");
		}
	}
	tb_log(flags, "\n");

#endif
	return 0;
}

static int16_t js_subcmd_read_command(uint8_t flags, char *args)
{
	uint8_t sbuf[16];
	char tline[65]; // aa .. bb : .abc..  3*16+16+1=65

	while (*args == ' ')
		args++;
	if (*args < '0' || *args > '9') {
		return -EINVAL;
	}
	uint32_t sadr = strtoul(args, &args, 0);
	while (*args == ' ')
		args++;
	uint32_t slen;
	if (*args) {
		char *endptr;
		slen = strtoul(args, &endptr, 0);
		if (endptr == args) {
			return -EINVAL;
		}
		while (*endptr == ' ')
			endptr++;
		if (*endptr) {
			return -EINVAL;
		}
	} else
		slen = 64; // Default: four hex dump lines.
	while (slen) {
		uint32_t rlen = slen;
		if (rlen > 16)
			rlen = 16;
		// Initialize buffers before each read so short reads are obvious.
		tline[sizeof(tline) - 1] = '\0';
		memset(sbuf, 0xe5, sizeof(sbuf));
		memset(tline, ' ', sizeof(tline) - 1);

		int16_t err = zephyr_flash_read(sadr, sbuf, rlen);
		if (err) {
			tb_log(flags, "ERROR:zephyr:flash_read(): %d (A:0x%08X, L:%u)\n", err, sadr,
			       rlen);
			return err;
		}

		for (int16_t i = 0; i < rlen; i++) {
			uint8_t val = sbuf[i];
			tline[3 * i] = "0123456789ABCDEF"[(val >> 4) & 0xF];
			tline[3 * i + 1] = "0123456789ABCDEF"[val & 0xF];
			if (val < ' ' || val > '~')
				val = '.';
			tline[48 + i] = val;
		}
		tb_log(flags, "%08X: %s\n", sadr, tline);
		slen -= rlen;
		sadr += rlen;
	}
	return 0;
}

int16_t js_subcmd_erase_command(uint8_t flags, char *args)
{

	// 0x / 0.-9. without WS
	while (*args == ' ')
		args++;
	if (*args < '0' || *args > '9') {
		return -EINVAL;
	}
	uint32_t sadr = strtoul(args, &args, 0);
	while (*args == ' ')
		args++;
	if (*args < '0' || *args > '9') {
		return -EINVAL;
	}
	char *endptr;
	uint32_t len = strtoul(args, &endptr, 0);
	if (endptr == args || len == 0) {
		return -EINVAL;
	}
	while (*endptr == ' ')
		endptr++;
	if (*endptr) {
		return -EINVAL;
	}
	int16_t err = zephyr_flash_erase(sadr, len);
	if (err) {
		tb_log(flags, "ERROR:zephyr:flash_erase(): %d (A:0x%08X L:%u)\n", err, sadr, len);
	}
	return err;
}

int16_t js_subcmd_write_command(uint8_t flags, char *args)
{
	uint8_t wbuf[16];
	memset(wbuf, 0x5e, sizeof(wbuf));
	int16_t wlen = 0;

	// First parse the address. strtoul accepts decimal and 0x-prefixed values.
	while (*args == ' ')
		args++;
	if (*args < '0' || *args > '9') {
		return -EINVAL;
	}
	uint32_t sadr = strtoul(args, &args, 0);
	// Then parse up to 16 data bytes. Extra input is intentionally ignored.
	while (*args && wlen < sizeof(wbuf)) {
		while (*args == ' ')
			args++;
		if (*args == '\0') {
			break;
		}
		if (*args < '0' || *args > '9') {
			return -EINVAL;
		}
		wbuf[wlen++] = (uint8_t)strtoul(args, &args, 0); // Limit to Byte is OK for Tests
	}
	// Allow zero-length writes for API testing.
	int16_t err = zephyr_flash_write(sadr, wbuf, wlen);
	if (err) {
		tb_log(flags, "ERROR:zephyr:flash_write(): %d (A:0x%08X L:%u)\n", err, sadr, wlen);
	}
	return err;
}

/* Low-level handler for commands that directly use Zephyr's flash API. */

//========== Low-level commands for flash, JEDEC, SFDP, read, write, erase =================
static int16_t js_handle_lowlevel_command(uint8_t flags, char *args)
{
	const struct device *flash_dev = zephyr_get_flash_device();
	if (flash_dev == NULL) {
		tb_log(flags, "ERROR:Flash device not found\n");
		return -ENODEV;
	}
	if (strcmp(args, "jedec") == 0) {
		return js_subcmd_jedec_command(flags, flash_dev);
	}

	char *nargs = tb_match_str_prefix("sread", args);
	if (nargs != NULL) {
		return js_subcmd_read_command(flags, nargs);
	}

	char *margs = tb_match_str_prefix("serase", args);
	if (margs != NULL) {
		return js_subcmd_erase_command(flags, margs);
	}

	char *wargs = tb_match_str_prefix("swrite", args);
	if (wargs != NULL) {
		return js_subcmd_write_command(flags, wargs);
	}

	else
		return -EINVAL;
}

// ==== User Commands start here =====
struct jesfs_desc js_file_desc; // Global for open file, only one file at a time for now

static int16_t js_handle_start_command(uint8_t flags, char *args)
{
	uint16_t cmd = FS_START_NORMAL;
	if (*args != '\0') {
		if (!strcmp(args, "normal")) {
			cmd = FS_START_NORMAL;
		} else if (!strcmp(args, "fast")) {
			cmd = FS_START_FAST;
		} else if (!strcmp(args, "restart")) {
			cmd = FS_START_RESTART;
		} else
			return -EINVAL;
	}
	int16_t res = jesfs_start(cmd); // Wake (an enable Flash)
	tb_log(flags, "jesfs_start(%d)=%d\n", cmd, res);
	return res;
}
static int16_t js_handle_deepsleep_command(uint8_t flags, char *args)
{
	if (*args)
		return -EINVAL; // Reject trailing characters such as "deepsleepx".

	int16_t res = jesfs_deepsleep(); // Wake (an enable Flash)
	tb_log(flags, "jesfs_deepsleep()=%d\n", res);
	return res;
}

// 8MB flash has 2048 sectors.
static uint8_t cb_prog_flags;
void cb_prog(uint32_t cur_sect, uint32_t total_sect)
{
	if (cur_sect & 0x1F)
		return;		    // Every 32nd sector: 64 progress marks for 8MB flash.
	tb_log(cb_prog_flags, "#"); // Mark work
}
static int16_t js_handle_format_command(uint8_t flags, char *args)
{
	if (*args)
		return -EINVAL; // Reject trailing characters such as "formatx".

	if (js_file_desc._head_sadr) {
		int16_t cres = jesfs_close(&js_file_desc);
		if (cres != 0) {
			return cres;
		}
	}
	tb_log(flags, "jesfs_format(%d) ...\n", FS_FORMAT_SOFT);
	cb_prog_flags = flags;
	int16_t res = jesfs_format(FS_FORMAT_SOFT, cb_prog);
	tb_log(flags, "\njesfs_format(%d)=%d\n", FS_FORMAT_SOFT, res);
	return res;
}

// Print the directory of the Flash
int16_t js_handle_dir_command(uint8_t flags, char *args)
{
	if (*args)
		return -EINVAL; // Reject trailing characters such as "dirx".

	tb_log(flags, "Directory:\n");
	tb_log(flags, "Disk size: %d Bytes\n", sflash_info.total_flash_size);
	if (sflash_info.state_flags) {
		tb_log(flags, "Error: VoltageLow/FlashSleep!\n");
		return -EAGAIN;
	}
	if (sflash_info.creation_date == 0xFFFFFFFF) { // Severe Error
		tb_log(flags, "Error: Invalid/Unformatted Disk!\n");
		return -EINVAL;
	}
	tb_log(flags, "Disk available: %d Bytes / %d Sectors\n", sflash_info.available_disk_size,
	       sflash_info.available_disk_size / SF_SECTOR_PH);

	int16_t res = 0;

	char date_buffer[DBUF_SIZE];
	conv_secs_to_date_buffer(sflash_info.creation_date, date_buffer, DBUF_SIZE);
	tb_log(flags, "Disk formatted [%s]\n", date_buffer);

	for (int16_t i = 0; i < sflash_info.files_used + 1;
	     i++) { // Include one spare entry as a sanity check.
		struct jesfs_stat fs_stat;
		res = jesfs_info(&fs_stat, i);

		if (res <= 0)
			break;
		if (res & FS_STAT_INACTIVE)
			tb_log(flags, "(- '%s'   (deleted))\n", fs_stat.fname);
		else if (res & FS_STAT_ACTIVE) {
			tb_log(flags, "- '%s'   ", fs_stat.fname); // Active
			if (res & FS_STAT_UNCLOSED) {
				// Read to EOF to discover the real length of an unclosed file.
				struct jesfs_desc fs_desc;
				int32_t lres = jesfs_open(
					&fs_desc, fs_stat.fname,
					SF_OPEN_READ | SF_OPEN_RAW); // Find out len by DummyRead
				if (!lres)
					lres = jesfs_read(&fs_desc, NULL,
							  0xFFFFFFFF); // Read as much as possible
				if (lres >= 0) {
					fs_stat.file_len = fs_desc.file_pos; // Update Length
					lres = jesfs_close(
						&fs_desc); // Prevent descriptor from Reuse
				}
				if (lres >= 0) {
					tb_log(flags, "(Unclosed: %u Bytes)", fs_stat.file_len);
				} else {
					tb_log(flags, "(Unclosed: ERROR %d)", lres);
				}

			} else {
				tb_log(flags, "%u Bytes", fs_stat.file_len);
			}
			// The creation Flags
			if (fs_stat.disk_flags & SF_OPEN_CRC)
				tb_log(flags, " CRC32:%x", fs_stat.file_crc32);
			if (fs_stat.disk_flags & SF_OPEN_EXT_SYNC)
				tb_log(flags, " ExtSync");
			// if(fs_stat.disk_flags & _SF_OPEN_RES) tb_log(flags, " Reserved");
			conv_secs_to_date_buffer(fs_stat.file_ctime, date_buffer, DBUF_SIZE);
			tb_log(flags, " [%s]\n", date_buffer);
		}
	}
	tb_log(flags, "Disk Nr. of files active: %d\n", sflash_info.files_active);
	tb_log(flags, "Disk Nr. of files used: %d\n", sflash_info.files_used);
#ifdef JSTAT
	if (sflash_info.sectors_unknown)
		tb_log(flags, "WARNING - Found %d Unknown Sectors\n", sflash_info.sectors_unknown);
#endif
	return res;
}

// Run a careful Disk Check with opt. Output, requires a temp. buffer
static uint8_t cb_check_flags;
static void cb_check(const char *fmt, ...)
{
#define CHECK_LINE_SIZE 80
	char cb_check_line[CHECK_LINE_SIZE];
	va_list args;
	va_start(args, fmt);
	vsnprintk(cb_check_line, sizeof(cb_check_line), fmt, args);
	va_end(args);

	tb_log(cb_check_flags, "%s", cb_check_line);
}
int16_t js_handle_check_command(uint8_t flags, char *args)
{
	if (*args)
		return -EINVAL; // Reject trailing characters such as "checkx".

	int16_t res = 0;
	if (js_file_desc._head_sadr) {
		res = jesfs_close(&js_file_desc); // Close what is open..
		if (res != 0) {
			return res;
		}
	}
	cb_check_flags = flags;
	res = jesfs_check_disk(cb_check);
	return res;
}

int16_t js_handle_open_command(uint8_t flags, char *args)
{
	while (*args == ' ')
		args++;

	char fname[FNAMELEN + 1];
	int16_t fncnt = 0;
	while (*args && *args > ' ' && fncnt < FNAMELEN) {
		fname[fncnt++] = *args++;
	}
	fname[fncnt] = '\0';
	if (!fname[0] || (*args && *args > ' ')) {
		return JESFS_ERR_BAD_FILENAME;
	}
	while (*args && *args == ' ')
		args++;

	uint8_t open_flags =
		SF_OPEN_READ | SF_OPEN_RAW | SF_OPEN_CRC; // Default Open-Mode, (RAW allows Delete)
	if (*args) {
		open_flags = 0;
		while (true) {
			switch (*args++) {
			case 'r':
				open_flags |= SF_OPEN_READ;
				break; // 1
			case 't':
				open_flags |= SF_OPEN_CREATE;
				break; // 2
			case 'w':
				open_flags |= SF_OPEN_WRITE;
				break; // 4
			case 'a':
				open_flags |= SF_OPEN_RAW;
				break; // 8
			case 'c':
				open_flags |= SF_OPEN_CRC;
				break; // 16
			case 'x':
				open_flags |= SF_OPEN_EXT_SYNC;
				break; // 64
			default:
				return -EINVAL;
			}
			if (*args == '\0')
				break;
		}
	}
	if (js_file_desc._head_sadr) {
		// return -EBUSY;
		tb_log(flags, "WARNING: A file was still open\n");
	}
	struct jesfs_desc new_desc;
	int16_t res = jesfs_open(&new_desc, fname, open_flags);
	if (!res) {
		js_file_desc = new_desc;
	}
	tb_log(flags, "jesfs_open('%s',%u)=%d\n", fname, open_flags, res);

	return res;
}

int16_t js_handle_close_command(uint8_t flags, char *args)
{
	if (*args)
		return -EINVAL; // Reject trailing characters such as "closex".

	int16_t res = jesfs_close(&js_file_desc);
	tb_log(flags, "jesfs_close()=%d\n", res);
	return res;
}

// At first only ASCII read implemented
int16_t js_handle_read_command(uint8_t flags, char *args)
{

	char line_buf[64 + 1]; // 64 Bytes + Null-Terminator
	int32_t ranz;
	if (!*args) {
		ranz = (sizeof(line_buf) - 1); // Default: Read 1 Line
	} else {
		char *endptr;

		ranz = strtol(args, &endptr, 0);
		while (*endptr == ' ') {
			endptr++;
		}
		if (*endptr != '\0' || ranz == 0) {
			return -EINVAL;
		}
	}

	int32_t rlen_total = 0;
	while (true) {
		if (ranz > 0) {
			// Pos Number: Read and show the data in ASCII
			int32_t remaining = ranz - rlen_total;
			uint32_t maxread = (sizeof(line_buf) - 1);

			if (remaining <= 0) {
				break;
			}
			if (maxread > (uint32_t)remaining)
				maxread = (uint32_t)remaining;

			int32_t rlen = jesfs_read(&js_file_desc, line_buf, maxread);
			if (rlen < 0 || rlen > (sizeof(line_buf) - 1)) {
				tb_log(flags, "ERROR: jesfs_read()=%d\n", rlen);
				return (int16_t)rlen;
			}
			if (rlen == 0) {
				if (js_file_desc.open_flags & SF_OPEN_CRC) {
					uint32_t crcRun = js_file_desc.file_crc32;
					uint32_t crcDisk = jesfs_get_crc32(&js_file_desc);
					if (crcDisk != crcRun) {
						tb_log(flags,
						       "ERROR: CRC mismatch: stored: %08X, read: "
						       "%08X\n",
						       crcDisk, crcRun);
						return -EIO;
					} else {
						tb_log(flags, "CRC OK: %08X\n", crcRun);
					}
				} else {
					tb_log(flags, "CRC not enabled\n");
				}
				break; // EOF
			}
			line_buf[rlen] = '\0';		     // Null-Terminator
			for (int16_t i = 0; i < rlen; i++) { // Only Visibles
				if (line_buf[i] < ' ' || line_buf[i] > '~') {
					line_buf[i] = '.';
				}
			}
			tb_log(flags, "%u:'%s'\n", rlen, line_buf); // Show in ASCII
			rlen_total += rlen;
			if (rlen_total >= ranz)
				break; // Read enough
		} else {
			// Neg Number: Silent Reading, only count the number of bytes read.
			uint32_t remaining = (uint32_t)-ranz;
			int32_t rlen = jesfs_read(&js_file_desc, NULL, remaining);
			if (rlen < 0) {
				tb_log(flags, "ERROR: jesfs_read()=%d\n", rlen);
				return (int16_t)rlen;
			}
			if (rlen == 0) {
				break;
			}
			if ((uint32_t)rlen > remaining) {
				return JESFS_ERR_FILE_DESC_CORRUPTED;
			}
			rlen_total += rlen;
			if ((uint32_t)rlen >= remaining) {
				break;
			}
		}
	}
	tb_log(flags, "jesfs_read(): %d Chars\n", rlen_total);
	return 0;
}

int16_t js_handle_write_command(uint8_t flags, char *args)
{
	if (*args == ' ')
		args++; // Skip max. 1 WS
	uint32_t wlen = strlen(args);
	int16_t res = jesfs_write(&js_file_desc, (uint8_t *)args, wlen);
	tb_log(flags, "jesfs_write(%d)=%d\n", wlen, res);
	return res;
}

// Write a big Block of Test-Data to generate a stress test
int16_t js_handle_chunkwrite_command(uint8_t flags, char *args)
{
	while (*args == ' ')
		args++;
	if (!js_file_desc._head_sadr)
		return JESFS_ERR_BAD_DESCRIPTOR;

	char js_chunk[128 + 1];
	char *endptr;
	uint32_t wlen = strtoul(args, &endptr, 0);
	if (endptr == args || wlen == 0)
		return -EINVAL;
	args = endptr;
	while (*args == ' ')
		args++;

	uint32_t chunklen;
	if (*args) {
		chunklen = strtoul(args, &endptr, 0);
		if (endptr == args)
			return -EINVAL;
		while (*endptr == ' ')
			endptr++;
		if (*endptr)
			return -EINVAL;
	} else {
		chunklen = sizeof(js_chunk) - 1;
	}
	if (chunklen == 0 || chunklen > sizeof(js_chunk) - 1)
		return -EINVAL;
	tb_log(flags, "ChunkWrite: %u Bytes in %u Byte chunks\n", wlen, chunklen);
	uint32_t wpos = 0;
	int16_t res = 0;
	while (wlen) {
		uint32_t thislen = wlen;
		if (thislen > chunklen)
			thislen = chunklen;

		int marker_len =
			snprintk(js_chunk, sizeof(js_chunk), "+((%u))-", wpos); // Null-Terminator
		if (marker_len < 0)
			return -EINVAL;
		if ((uint32_t)marker_len > thislen)
			marker_len = (int)thislen;
		for (uint32_t i = (uint32_t)marker_len; i < thislen; i++)
			js_chunk[i] = (uint8_t)((rand() & 63) + 58); // Random printable ASCII
		js_chunk[thislen] = '\0';

		res = jesfs_write(&js_file_desc, (uint8_t *)js_chunk, thislen);
		if (res < 0) {
			tb_log(flags, "ERROR: jesfs_write()=%d at Pos. %u\n", res, wpos);
			return res;
		}
		wlen -= thislen;
		wpos += thislen;
	}
	tb_log(flags, "ChunkWrite()=%d\n", res);
	return res;
}

// The file must be opened before it can be deleted.
int16_t js_handle_delete_command(uint8_t flags, char *args)
{
	if (*args)
		return -EINVAL; // Reject trailing arguments.

	int16_t res = jesfs_delete(&js_file_desc);
	tb_log(flags, "jesfs_delete()=%d\n", res);
	return res;
}

/* The file must be open before it can be renamed.
 * the old descriptor is still valid afterwards */
int16_t js_handle_rename_command(uint8_t flags, char *args)
{
	if (!js_file_desc._head_sadr)
		return JESFS_ERR_BAD_DESCRIPTOR;

	char newfname[FNAMELEN + 1];
	int16_t fncnt = 0;
	while (*args && *args > ' ' && fncnt < FNAMELEN) {
		newfname[fncnt++] = *args++;
	}
	newfname[fncnt] = '\0';
	if (!newfname[0] || (*args && *args > ' '))
		return JESFS_ERR_BAD_FILENAME;
	while (*args == ' ')
		args++;
	if (*args)
		return -EINVAL;

	struct jesfs_desc js_new_desc_test;
	uint8_t old_disk_flags;
	int16_t res = sflash_read(js_file_desc._head_sadr + HEADER_SIZE_B + 34, &old_disk_flags,
				  sizeof(old_disk_flags));
	if (res)
		return res;
	// The target descriptor is only a staging HEAD; jesfs_rename() rejects READ/RAW there.
	uint8_t new_flags = SF_OPEN_CREATE |
			    (old_disk_flags & ~(SF_OPEN_READ | SF_OPEN_RAW | SF_XOPEN_UNCLOSED));

	jesfs_set_static_secs(
		js_file_desc.file_ctime); // Use OLD timestamp for NEW file (= keep timestamp)
	res = jesfs_open(&js_new_desc_test, newfname, new_flags);
	if (!res) {
		res = jesfs_rename(&js_file_desc, &js_new_desc_test);
		jesfs_close(&js_new_desc_test);
	}
	jesfs_set_static_secs(0);
	tb_log(flags, "jesfs_rename(NewName:'%s')=%d\n", newfname, res);
	return res;
}

static int16_t js_handle_help_command(uint8_t flags, char *args);
// Defined below to keep the command table close to the handlers.
static const tb_command_entry_t js_commands[] = {
	// Low-level flash diagnostics via Zephyr flash API.
	{"ll", js_handle_lowlevel_command,
	 "jedec | sread <addr> [len] | swrite <addr> <b0..b15> | serase <addr> <len>"},

	// Filesystem lifecycle commands.
	{"start", js_handle_start_command, "[normal|fast|restart] (Default: normal)"},
	{"deepsleep", js_handle_deepsleep_command, NULL},
	{"format", js_handle_format_command, NULL},
	{"dir", js_handle_dir_command, NULL},
	{"check", js_handle_check_command, NULL},

	// File operation commands (open file descriptor required where noted).
	{"open", js_handle_open_command,
	 "<FILENAME> [r|t|w|a|c|x] (r:READ t:CREATE w:WRITE a:RAW c:CRC x:EXT_SYNC, Default: rac)"},
	{"close", js_handle_close_command, NULL},
	{"read", js_handle_read_command, "<NUMBER2READ> (negative = silent read)"},
	{"write", js_handle_write_command, "<DATA> (Text string)"},
	{"delete", js_handle_delete_command, "(File must be open)"},
	{"rename", js_handle_rename_command, "<NEWFILENAME> (File must be open)"},

	// Helper/test commands.
	{"chunkwrite", js_handle_chunkwrite_command, "<TOTALBYTES> [CHUNKSIZE] (Default: 128)"},

	{"help", js_handle_help_command, NULL},

};

static int16_t js_handle_help_command(uint8_t flags, char *args)
{
	(void)args;

	tb_log(flags, "'file' commands:\n");
	tb_list_commands(flags, js_commands, ARRAY_SIZE(js_commands));
	return 0;
}

// Entry point called after the outer shell has stripped the "file" prefix.
int16_t jesfs_shell(uint8_t flags, char *cmd)
{
	int16_t res;
	if (*cmd != '\0') {
		res = tb_dispatch_command(flags, cmd, js_commands, ARRAY_SIZE(js_commands));
	} else {
		tb_log(flags, "JESFS ready\n");
		res = 0; // Empty command.
	}
	return res;
}
