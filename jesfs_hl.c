/*******************************************************************************
 * JesFs_hl.c: JesFs high-level file API
 *
 * JesFs - Jo's Embedded Serial File System
 *
 * (C) joembedded@gmail.com - www.joembedded.de
 *
 * Version: see jesfs.h
 *
 *******************************************************************************/

#include <stddef.h>
#include <stdint.h>
#if defined(__ZEPHYR__)
#include <string.h>
#if defined(CONFIG_DEBUG_OPTIMIZATIONS)
#include <zephyr/kernel.h> /* printk() */
#endif
#include <zephyr/sys/crc.h>
#endif

/* ---------------------------------------------- JesFs start ---------------------- */
#include "jesfs.h"
#include "jesfs_int.h"

extern uint32_t jesfs_time_get(void);
extern int16_t jesfs_supply_voltage_check(void);

static uint32_t static_time;

/* JesFs assumes 4k physical sectors, the common erase granularity for SPI NOR. */
#if SF_SECTOR_PH != 4096
#error "Physical Sector Size SPI Flash must be 4096 Bytes"
#endif
#if FNAMELEN != 21
#error "FNAMELEN fixed to 21+Zero-Byte by Design"
#endif

/* ------------------- High-level filesystem helpers ------------------------ */
uint32_t jesfs_strlen(const char *s)
{
#if !defined(__ZEPHYR__)
	const char *p = s;
	while (*p) {
		p++;
	}
	return p - s;
#else
	return (uint32_t)strlen((const char *)s);
#endif
}
/* Not identical to strncpy(): always terminates and copies at most maxchar characters. */
void jesfs_strncpy(char *d, const char *s, uint8_t maxchar)
{
	char c;
	for (;;) {
		c = *s++;
		if (!c || !maxchar--) {
			break;
		}
		*d++ = c;
	}
	*d = 0;
}
void jesfs_memset(uint8_t *p, uint8_t v, uint32_t n)
{
#if !defined(__ZEPHYR__)
	while (n--) {
		*p++ = v;
	}
#else
	memset(p, v, n);
#endif
}
int16_t jesfs_strcmp(const char *s1, const char *s2)
{
#if !defined(__ZEPHYR__)
	for (;;) {
		if (*s1 == 0 || *s1 != *s2) {
			return *s1 - *s2;
		}
		s1++;
		s2++;
	}
#else
	return (int16_t)strcmp((const char *)s1, (const char *)s2);
#endif
}

/* Set a static timestamp for deterministic tests; pass 0 to use jesfs_time_get(). */
void jesfs_set_static_secs(uint32_t newsecs)
{
	static_time = newsecs;
}

uint32_t jesfs_get_secs(void)
{
	if (static_time) {
		return static_time;
	} else {
		return jesfs_time_get();
	}
}

/* ------ Date conversion routines --------------- */
#define SEC_DAY 86400L /* Seconds per day. */
static const uint32_t daylen[12] = {
	31 * SEC_DAY,  /* Jan */
	59 * SEC_DAY,  /* Feb */
	90 * SEC_DAY,  /* Mar */
	120 * SEC_DAY, /* Apr */
	151 * SEC_DAY, /* May */
	181 * SEC_DAY, /* June */
	212 * SEC_DAY, /* Jul */
	243 * SEC_DAY, /* Aug. */
	273 * SEC_DAY, /* Sept */
	304 * SEC_DAY, /* Oct */
	334 * SEC_DAY, /* Nov. */
	365 * SEC_DAY, /* Dec. */
};
/* Convert Unix seconds to struct jesfs_date (1970-01-01 00:00:00 = 0). */
void jesfs_sec1970_to_date(uint32_t asecs, struct jesfs_date *pd)
{
	uint32_t divs;
	uint8_t dlap = 0;
	divs = asecs / (1461 * SEC_DAY);
	pd->a = 1970 + divs * 4;
	asecs -= (divs * (1461 * SEC_DAY)); /* 3 normal + 1 leap year */
	if (asecs >= (789 * SEC_DAY)) {
		asecs -= SEC_DAY;
		dlap = 1;
	}
	divs = asecs / (365 * SEC_DAY);
	pd->a += divs;
	asecs -= divs * (365 * SEC_DAY);
	if (dlap && asecs < 59 * SEC_DAY && divs == 2) {
		divs = 1;
		asecs += SEC_DAY;
	} else {
		for (divs = 0; asecs >= daylen[divs]; divs++) {
			;
		}
	}
	pd->m = 1 + divs;
	if (divs) {
		asecs -= daylen[divs - 1];
	}
	divs = asecs / SEC_DAY;
	pd->d = 1 + divs;
	asecs -= SEC_DAY * divs;
	divs = asecs / 3600L;
	pd->h = divs;
	asecs -= divs * 3600L;
	divs = asecs / 60L;
	pd->min = divs;
	asecs -= divs * 60L;
	pd->sec = asecs;
}

static const uint8_t days_per_month[12] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
};
static const uint16_t days_summed[12] = {
	0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334,
};

/* Convert struct jesfs_date to Unix seconds. Returns 0 on invalid input. */
uint32_t jesfs_date_to_sec1970(const struct jesfs_date *pd)
{
	uint32_t nsec;
	uint16_t year_base;
	uint8_t year_idx;

	year_base = pd->a - 1970;
	year_idx = year_base % 4; /* In this 1970..2099 range, index 2 is the leap year. */
	if (year_base > 129) {
		return 0; /* Valid range is 1970..2099; 2100 is not a leap year. */
	}
	if (pd->m < 1 || pd->m > 12 || pd->d < 1) {
		return 0; /* Month: 1..12 Day: 1..x, */
	}
	if (pd->d > days_per_month[pd->m - 1]) { /* Check Day x ok for this month? */
		if (year_idx != 2 || pd->m != 2 || pd->d != 29) { /* Only Exception */
			return 0;
		}
	}
	if (pd->h > 23 || pd->min > 59 || pd->sec > 59) {
		return 0;
	}

	nsec = ((uint32_t)year_base / 4) * (1461 * SEC_DAY); /* Complete 4-years */
	nsec += ((uint32_t)year_idx) * (365 * SEC_DAY); /* */
	nsec += ((uint32_t)days_summed[pd->m - 1] + (pd->d - 1)) * SEC_DAY; /* plus days for 4-years */
	if (year_idx == 3 || (year_idx == 2 && pd->m > 2)) {
		nsec += SEC_DAY; /* Add leap day */
	}

	nsec += (pd->h * 3600);
	nsec += (pd->min * 60);
	nsec += pd->sec;

	return nsec;
}

/* Track CRC32 over a stream. Also useful for callers outside the filesystem. */
#define POLY32 0xEDB88320 /* ISO 3309 */
uint32_t jesfs_track_crc32(const uint8_t *pdata, uint32_t wlen, uint32_t crc_run)
{
#if !defined(__ZEPHYR__)
	uint8_t j;
	while (wlen--) {
		crc_run ^= *pdata++;
		for (j = 0; j < 8; j++) {
			if (crc_run & 1) {
				crc_run = (crc_run >> 1) ^ POLY32;
			} else {
				crc_run = crc_run >> 1;
			}
		}
	}
	return crc_run;
#else
	/* Zephyr has a built-in ISO 3309 CRC32 implementation; requires CONFIG_CRC=y. */
	return ~crc32_ieee_update(~crc_run, (const uint8_t *)pdata, wlen);
#endif
}

static int16_t sflash_sadr_invalid(uint32_t sadr)
{
	if (sadr == 0xFFFFFFFF) {
		return 0; /* OK */
	}
	if (!sadr) {
		return -1;
	}
	if (sadr & (SF_SECTOR_PH - 1)) {
		return -2; /* FATAL */
	}
	if (sadr >= sflash_info.total_flash_size) {
		return -3; /* FATAL */
	}
	return 0; /* Ok */
}

static int16_t flash_set2delete(uint32_t sadr)
{
	int16_t res;
	uint32_t thdr[3];
	uint32_t max_sect;
	uint32_t oadr;

	oadr = sadr;
	max_sect = (sflash_info.total_flash_size / SF_SECTOR_PH);
	while (--max_sect) {
		uint8_t is_head = 0;
		uint8_t is_data = 0;

		if (sflash_sadr_invalid(sadr)) {
			return JESFS_ERR_BAD_SECTOR_ADDR;
		}
		res = sflash_read(sadr, (uint8_t *)thdr, 12);
		if (res) {
			return res;
		}
		if (thdr[0] == SECTOR_MAGIC_HEAD_ACTIVE) {
			if (thdr[1] != 0xFFFFFFFF) {
				return JESFS_ERR_BAD_SECTOR_OWNER;
			}
			thdr[0] = SECTOR_MAGIC_HEAD_DELETED;
			is_head = 1;
		} else if (thdr[0] == SECTOR_MAGIC_DATA) {
			if (thdr[1] != oadr) {
				return JESFS_ERR_BAD_SECTOR_OWNER;
			}
			thdr[0] = SECTOR_MAGIC_TODELETE;
			is_data = 1;
		} else {
			return JESFS_ERR_BAD_SECTOR_TYPE; /* Illegal */
		}
		res = sflash_sector_write(sadr, (uint8_t *)thdr, 4);
		if (res) {
			return res;
		}
		if (is_head) {
			sflash_info.files_active--;
		}
		if (is_data) {
			sflash_info.available_disk_size += SF_SECTOR_PH;
		}
		sadr = thdr[2];
		if (sadr == 0xFFFFFFFF) {
			return 0;
		}
	}
	return JESFS_ERR_SECTOR_LIST_CYCLE;
}

/*
 * Find last used byte index in a sector.
 *
 * Returns 0 if the sector is empty.
 */
static int32_t sflash_find_mlen(uint32_t sadr, uint16_t max_sec_rd)
{
	uint16_t wlen;
	uint16_t used_len = max_sec_rd;
	sadr += max_sec_rd;
	while (max_sec_rd) {
		wlen = max_sec_rd;
		if (wlen > SF_BUFFER_SIZE_B) {
			wlen = SF_BUFFER_SIZE_B;
		}
		max_sec_rd -= wlen;
		sadr -= wlen;
		int16_t res = sflash_read(sadr, (uint8_t *)&sflash_info.databuf, wlen);
		if (res) {
			return res;
		}
		while (wlen--) {
			if (sflash_info.databuf.u8[wlen] != 0xFF) {
				return used_len;
			}
			used_len--;
		}
	}
	return 0;
}

/* Copy data inside one flash sector while respecting the page-write path. */
static int16_t flash_intrasec_copy(uint32_t sadr, uint32_t dadr, uint16_t clen)
{
	int16_t res;
	uint16_t blen;
	while (clen) {
		blen = clen;
		if (blen > SF_BUFFER_SIZE_B) {
			blen = SF_BUFFER_SIZE_B;
		}

		res = sflash_read(sadr, (uint8_t *)&sflash_info.databuf, blen);
		if (res) {
			return res;
		}
		res = sflash_sector_write(dadr, (uint8_t *)&sflash_info.databuf, blen);
		if (res) {
			return res;
		}
		sadr += blen;
		dadr += blen;
		clen -= blen;
	}
	return 0;
}

/* --------------------------- Public JesFs API ---------------------------------------- */

/* Start the filesystem, identify flash, and scan basic on-flash structures. */
int16_t jesfs_start(uint8_t mode)
{
	int16_t res = 0;
	uint32_t id;
	uint32_t sadr;
	uint32_t idx_adr;
	uint32_t dir_typ;
	uint16_t err;

#if !defined(__ZEPHYR__)
	sflash_spi_init();
#endif

	if (jesfs_supply_voltage_check()) {
		sflash_info.creation_date = 0xFFFFFFFF; /* Invalidate Disk */
		sflash_info.total_flash_size = 0;
		sflash_info.identification = 0;
		sflash_info.state_flags |= STATE_POWERFAIL;
		return JESFS_ERR_VOLTAGE_TOO_LOW; /* Lock Flash Access if power is too low */
	}

	err = 3; /* Try 3 wakes before returning an Error */
	while (err--) {
/* Flash wakeup */
#if !defined(__ZEPHYR__)
		sflash_release_from_deep_power_down();
		sflash_wait_usec(45);
#else
		if (sflash_info.state_flags & (STATE_DEEPSLEEP)) {
			res = zephyr_flash_wake();
			if (res) {
				continue;
			}
		}
#endif
		sflash_info.state_flags &= ~(STATE_DEEPSLEEP_OR_POWERFAIL);

/* ID read and get setup */
		id = sflash_quick_scan_identification();
		/* Quickstart without structural checks: wake up and check ID only. */
		if (mode & FS_START_RESTART) {
			if (sflash_info.total_flash_size && id == sflash_info.identification) {
				return 0; /* Wake only */
			}
		}

		sflash_info.creation_date = 0xFFFFFFFF; /* Assume Invalid Disk */

		res = sflash_interpret_id(id);
		if (!res) {
			break;
		}
	}
	if (res) {
		return res;
	}

/* Flash is known. Read the 12-byte filesystem header. */
	res = sflash_read(0, (uint8_t *)&sflash_info.databuf, HEADER_SIZE_B);
	if (res) {
		return res;
	}

	if (sflash_info.databuf.u32[0] == 0xFFFFFFFF) {
		return JESFS_ERR_BAD_MAGIC;
	}
	if (sflash_info.databuf.u32[0] != HEADER_MAGIC) {
		return JESFS_ERR_BAD_MAGIC_HEADER;
	}
	if (sflash_info.databuf.u32[1] != sflash_info.identification) {
		return JESFS_ERR_FLASH_ID_MISMATCH;
	}

	sflash_info.creation_date = sflash_info.databuf.u32[2]; /* Must differ from 0xFFFFFFFF. */

	err = 0;
	sflash_info.available_disk_size = sflash_info.total_flash_size - SF_SECTOR_PH;

#ifdef JSTAT
	sflash_info.sectors_todelete = 0; /* Not really required, just for statistics */
	sflash_info.sectors_clear = 0;
	sflash_info.sectors_unknown = 0;
#endif

	sflash_info.files_used = 0;
	sflash_info.files_active = 0;

	sflash_info.lusect_adr = 0;
/* Scan Headers of all sectors (FAST or normal) */
/* Scan  Takes on 1M-Flash 12msec, 16M-Flash: 200msec (12 MHz SPI) */
	for (sadr = SF_SECTOR_PH; sadr < sflash_info.total_flash_size; sadr += SF_SECTOR_PH) {
		int16_t res = sflash_read(sadr, (uint8_t *)&sflash_info.databuf, (mode & FS_START_FAST) ? 4 : 12);
		if (res) {
			return res;
		}
		switch (sflash_info.databuf.u32[0]) {
		case 0xFFFFFFFF: /* Empty */
#ifdef JSTAT
			sflash_info.sectors_clear++;
#endif
			break;
		case SECTOR_MAGIC_TODELETE:
#ifdef JSTAT
			sflash_info.sectors_todelete++;
#endif
			sflash_info.lusect_adr = sadr;
			break;

/* Count 'used' and find last used sector */
		case SECTOR_MAGIC_HEAD_ACTIVE: /* Head of active file */
			sflash_info.files_active++;
		case SECTOR_MAGIC_HEAD_DELETED: /* Head of deleted file */
			sflash_info.files_used++;
		case SECTOR_MAGIC_DATA: /* Intermediate sector of any file */
			sflash_info.available_disk_size -= SF_SECTOR_PH;
			sflash_info.lusect_adr = sadr;
			break;

		default:
#ifdef JSTAT
			sflash_info.sectors_unknown++;
#endif
			err++;
		}

		if (!(mode & FS_START_FAST)) {
			switch (sflash_info.databuf.u32[0]) {
			case 0xFFFFFFFF:
				if (sflash_info.databuf.u32[1] != 0xFFFFFFFF || sflash_info.databuf.u32[2] != 0xFFFFFFFF) {
					err++;
				}
				break;
			case SECTOR_MAGIC_HEAD_ACTIVE:
			case SECTOR_MAGIC_HEAD_DELETED:
				if (sflash_info.databuf.u32[1] != 0xFFFFFFFF) {
					err++;
				}
				if (sflash_sadr_invalid(sflash_info.databuf.u32[2])) {
					err++;
				}
				break;
			case SECTOR_MAGIC_DATA:
			case SECTOR_MAGIC_TODELETE:
				idx_adr = sflash_info.databuf.u32[1];
				if (idx_adr == 0xFFFFFFFF || sflash_sadr_invalid(idx_adr)) {
					err++;
				}
				if (sflash_sadr_invalid(sflash_info.databuf.u32[2])) {
					err++;
				}
				break;
			}
		}
	}

	sadr = HEADER_SIZE_B;
	id = 0;
	while (sadr != SF_SECTOR_PH) {
		int16_t res = sflash_read(sadr, (uint8_t *)&idx_adr, 4);
		if (res) {
			return res;
		}
		if (idx_adr == 0xFFFFFFFF) {
			break;
		} else {
			if (sflash_sadr_invalid(idx_adr)) {
				err++;
			} else {
				res = sflash_read(idx_adr, (uint8_t *)&dir_typ, 4);
				if (res) {
					return res;
				}
				if (dir_typ == SECTOR_MAGIC_HEAD_ACTIVE || dir_typ == SECTOR_MAGIC_HEAD_DELETED) {
					id++;
				} else {
					err++;
				}
			}
		}
		sadr += 4;
	}

	if (err || (uint16_t)id != sflash_info.files_used) {
		return JESFS_ERR_FS_STRUCTURE_PROBLEM; /* Corrupt Data? */
	}
	return 0; /* OK */
}

/* Put the flash/filesystem into low-power mode. Use jesfs_start(FS_START_RESTART) to wake it. */
int16_t jesfs_deepsleep(void)
{
	if (sflash_info.state_flags & STATE_DEEPSLEEP) {
		return JESFS_ERR_DEEPSLEEP_ALREADY; /* Already sleeping, 2.nd command could wake FS again */
	}
#if !defined(__ZEPHYR__)
	sflash_info.state_flags |= STATE_DEEPSLEEP;
	sflash_deep_power_down();
	sflash_spi_close(); /* Added V1.51 */
	return 0; /* No Errors possible */
#else
	int16_t res = zephyr_flash_deepsleep();

	if (res) {
		return res;
	}

	sflash_info.state_flags |= STATE_DEEPSLEEP;
	return 0;
#endif
}

/*
 * Format the filesystem.
 *
 * FS_FORMAT_SOFT erases only non-empty sectors and is usually the practical
 * choice. Bare-metal
 * FS_FORMAT_FULL starts a bulk erase and may take minutes,
 * depending on the flash datasheet.
 */
#if !defined(__ZEPHYR__)
int16_t jesfs_format(uint8_t fmode)
#else
/* Zephyr callers may pass a progress callback. */
int16_t jesfs_format(uint8_t fmode, void cb_prog(uint32_t cur_sect, uint32_t total_sect))
#endif
{
	uint32_t sbuf[3];
	int16_t res;
	uint32_t sadr;

	if (sflash_info.total_flash_size == 0 || sflash_info.identification == 0) {
		return JESFS_ERR_FLASH_ID_UNKNOWN; /* First jesfs_start() to identify Chip */
	}

	if (sflash_info.state_flags & STATE_DEEPSLEEP_OR_POWERFAIL) {
		return JESFS_ERR_FLASH_NOT_ACCESSIBLE;
	}

	if (jesfs_supply_voltage_check()) {
		sflash_info.state_flags |= STATE_POWERFAIL; /* Lock Flash until DEEPSLEEP */
		return JESFS_ERR_VOLTAGE_TOO_LOW; /* Lock Flash Access if power is too low */
	}

	if (fmode == FS_FORMAT_SOFT) {
#if defined(__ZEPHYR__)
		uint32_t total_sect = sflash_info.total_flash_size / SF_SECTOR_PH;
		uint32_t sect_cnt = 0;
#endif
		for (sadr = 0; sadr < sflash_info.total_flash_size; sadr += SF_SECTOR_PH) {
			int16_t res;

			res = sflash_read(sadr, (uint8_t *)&sbuf, 8);
			if (res) {
				return res;
			}
#if defined(__ZEPHYR__)
			if (cb_prog != NULL) {
				cb_prog(sect_cnt++, total_sect); /* Call user callback to show progress */
			}
#endif
			/* Header says empty; verify all bytes are really 0xFF. */
			if (sbuf[0] == 0xFFFFFFFF) {
				int32_t mlen = sflash_find_mlen(sadr, SF_SECTOR_PH);

				if (mlen < 0) {
					return (int16_t)mlen;
				}
				if (mlen == 0) {
					continue; /* yes: All empty! No Erase required */
				}
			} /* And all other sectors must be cleared by system */
			res = sflash_sector_erase(sadr);
			if (res) {
				return res;
			}
		}
#if !defined(__ZEPHYR__) /* Zephyr has own timing, so we do not need to wait here */
		sflash_wait_usec(1000); /* Wait 1 msec for last erase to finish */
#endif
#if !defined(__ZEPHYR__) /* No BulkErase in Zephyr */
	} else if (fmode == FS_FORMAT_FULL) {
		if (sflash_wait_write_enabled()) {
			return JESFS_ERR_WRITE_ENABLE_FAILED;
		}
		sflash_bulk_erase();
		if (sflash_wait_busy(240000)) {
			return JESFS_ERR_FLASH_TIMEOUT; /* */
		}
#endif
	} else {
		return JESFS_ERR_BAD_FORMAT_PARAM; /* Parameter */
	}

	sbuf[0] = HEADER_MAGIC;
	sbuf[1] = sflash_info.identification;
	sbuf[2] = jesfs_get_secs(); /* Creation Date of Disk is NOW */

	res = sflash_sector_write(0, (uint8_t *)sbuf, 12); /* Header V1.0 */
	if (res) {
		return res;
	}

	return jesfs_start(FS_START_NORMAL);
}

static uint32_t sflash_get_free_sector(void)
{
	uint32_t thdr;
	uint32_t max_sect;
/*
 * Some embedded compilers complain about the division. It will result in a
 * shift, so the warning can be ignored.
 */
	max_sect = (sflash_info.total_flash_size / SF_SECTOR_PH);
	while (--max_sect) {
		sflash_info.lusect_adr += SF_SECTOR_PH;
		if (sflash_info.lusect_adr >= sflash_info.total_flash_size) {
			sflash_info.lusect_adr = SF_SECTOR_PH; /* Set to Sector 1 (0: Header) */
		}
		int16_t res = sflash_read(sflash_info.lusect_adr, (uint8_t *)&thdr, 4);
		if (res) {
			return 0;
		}
/* This sector is free if it is marked as 'to delete' or if it is empty (0xFFFFFFFF) */
		if (thdr == SECTOR_MAGIC_TODELETE || thdr == 0xFFFFFFFF) {
			if (thdr == SECTOR_MAGIC_TODELETE) {
				if (sflash_sector_erase(sflash_info.lusect_adr)) {
					return 0;
				}
			}
			return sflash_info.lusect_adr;
		}
	}
	return 0;
}

/* --- jesfs_read() --- */
int32_t jesfs_read(struct jesfs_desc *pdesc, uint8_t *pdest, uint32_t anz)
{
	uint32_t h;
	uint32_t next_sect;
	int32_t total_rd = 0; /* max 2GB */
	uint16_t max_sec_rd;

	if (sflash_info.state_flags & STATE_DEEPSLEEP_OR_POWERFAIL) {
		return JESFS_ERR_FLASH_NOT_ACCESSIBLE;
	}

	if (!pdesc->_head_sadr) { /* Requires jesfs_start() before, even if no FS is present */
		return JESFS_ERR_BAD_DESCRIPTOR;
	}

	if (!(pdesc->open_flags & (SF_OPEN_READ | SF_OPEN_RAW))) {
		return JESFS_ERR_BAD_FILE_FLAGS;
	}

	while (anz) {
		int16_t res = sflash_read(pdesc->_wrk_sadr, (uint8_t *)&sflash_info.databuf,
					  HEADER_SIZE_B + FINFO_SIZE_B);
		if (res) {
			return res;
		}
		h = sflash_info.databuf.u32[0];
		if (h == SECTOR_MAGIC_HEAD_ACTIVE) {
			if (sflash_info.databuf.u32[1] != 0xFFFFFFFF) {
				return JESFS_ERR_SECTOR_HEADER_OWNER;
			}
		} else if (h == SECTOR_MAGIC_DATA) {
			if (sflash_info.databuf.u32[1] != pdesc->_head_sadr) {
				return JESFS_ERR_BAD_SECTOR_OWNER;
			}
		} else {
			return JESFS_ERR_BAD_SECTOR_TYPE;
		}

		next_sect = sflash_info.databuf.u32[2];
		if (sflash_sadr_invalid(next_sect)) {
			return JESFS_ERR_BAD_SECTOR_ADDR;
		}

		while (anz) {
			max_sec_rd = (SF_SECTOR_PH - pdesc->_sadr_rel);
			if (max_sec_rd > (SF_SECTOR_PH - HEADER_SIZE_B)) {
				return JESFS_ERR_FILE_DESC_CORRUPTED;
			}

			if (pdesc->file_len != 0xFFFFFFFF) {
				h = pdesc->file_len - pdesc->file_pos;
				if (anz > h) {
					anz = h;
				}

			} else if (next_sect == 0xFFFFFFFF) {
				int32_t mlen = sflash_find_mlen(pdesc->_wrk_sadr + pdesc->_sadr_rel, max_sec_rd);

				if (mlen < 0) {
					return mlen;
				}
				pdesc->file_len = pdesc->file_pos + (uint32_t)mlen; /* Now we know the End */
				if (anz > (uint32_t)mlen) {
					anz = (uint32_t)mlen;
				}
			}

/* Limit the block size of a single transfer, for example for SPI driver buffers. */
#ifdef SF_RD_TRANSFER_LIMIT
			if (pdest && max_sec_rd > SF_RD_TRANSFER_LIMIT) {
				max_sec_rd = SF_RD_TRANSFER_LIMIT;
			}
#endif

			if ((uint32_t)max_sec_rd > anz) {
				max_sec_rd = anz;
			}
			if (pdest) {
				int16_t res = sflash_read(pdesc->_wrk_sadr + pdesc->_sadr_rel, pdest, max_sec_rd);
				if (res) {
					return res;
				}
				if (pdesc->open_flags & SF_OPEN_CRC) {
					pdesc->file_crc32 = jesfs_track_crc32(pdest, max_sec_rd, pdesc->file_crc32);
				}
				pdest += max_sec_rd;
			}

			anz -= max_sec_rd;
			pdesc->file_pos += max_sec_rd;
			pdesc->_sadr_rel += max_sec_rd;
			total_rd += max_sec_rd;
			if (pdesc->_sadr_rel == SF_SECTOR_PH) {
				if (next_sect != 0xFFFFFFFF) {
					pdesc->_wrk_sadr = next_sect;
					pdesc->_sadr_rel = HEADER_SIZE_B;
				} else {
					if (anz) {
						return JESFS_ERR_BAD_FS_STRUCTURE;
					}
				}
				break;
			}
		}
	}
	return total_rd; /* max 2GB */
}

/* Rewind File to Start */
int16_t jesfs_rewind(struct jesfs_desc *pdesc)
{
	if (!pdesc->_head_sadr) {
		return JESFS_ERR_BAD_DESCRIPTOR;
	}
	if (pdesc->open_flags & SF_OPEN_WRITE) {
		return JESFS_ERR_NOT_OPEN_FOR_WRITE;
	}
	pdesc->_wrk_sadr = pdesc->_head_sadr;
	pdesc->file_pos = 0;
	pdesc->_sadr_rel = HEADER_SIZE_B + FINFO_SIZE_B;
	pdesc->file_crc32 = 0xFFFFFFFF; /* Reset CRC */
	return 0;
}

/*
 * Open a file. With SF_OPEN_CREATE, create a new file and delete any existing
 * file unless SF_OPEN_RAW is also set.
 */
int16_t jesfs_open(struct jesfs_desc *pdesc, const char *pname, uint8_t flags)
{
	int16_t res;
	uint16_t i;
	uint32_t sadr = 0;
	uint32_t sfun_adr = 0;
	uint8_t new_index_entry = 0;

	if (sflash_info.state_flags & STATE_DEEPSLEEP_OR_POWERFAIL) {
		return JESFS_ERR_FLASH_NOT_ACCESSIBLE;
	}
	pdesc->_head_sadr = 0;
	pdesc->file_crc32 = 0xFFFFFFFF;
	if (sflash_info.creation_date == 0xFFFFFFFF) {
		return JESFS_ERR_BAD_MAGIC; /* Disk not formatted */
	}
	if (!pname) {
		return JESFS_ERR_BAD_FILENAME;
	}
	for (i = 0; i <= FNAMELEN; i++) {
		if (!pname[i]) {
			break;
		}
	}
	if (!*pname || i > FNAMELEN) {
		return JESFS_ERR_BAD_FILENAME;
	}

	for (i = 0; i < sflash_info.files_used; i++) {
		int16_t res;
		res = sflash_read(HEADER_SIZE_B + i * 4, (uint8_t *)&sadr, 4);
		if (res) {
			return res;
		}
		res = sflash_read(sadr, (uint8_t *)&sflash_info.databuf, HEADER_SIZE_B + FINFO_SIZE_B);
		if (res) {
			return res;
		}
		if (sflash_info.databuf.u32[0] == SECTOR_MAGIC_HEAD_DELETED) {
			sfun_adr = sadr;
		} else if (sflash_info.databuf.u32[0] != SECTOR_MAGIC_HEAD_ACTIVE) {
			return JESFS_ERR_INDEX_CORRUPTED;
		} else if (!jesfs_strcmp(pname, (char *)&sflash_info.databuf.u8[HEADER_SIZE_B + 12])) {
			break;
		}
		sadr = 0;
	}
	pdesc->open_flags = flags;
	pdesc->_sadr_rel = HEADER_SIZE_B + FINFO_SIZE_B;
	pdesc->file_pos = 0;
	pdesc->file_len = 0;

	if (sadr) {
		pdesc->_head_sadr = sadr;
		pdesc->_wrk_sadr = sadr;
		/* Reject CRC-open requests for files that were created without on-disk CRC flag. */
		if ((flags & SF_OPEN_CRC) && !(sflash_info.databuf.u8[HEADER_SIZE_B + 34] & SF_OPEN_CRC)) {
			return JESFS_ERR_BAD_FILE_FLAGS;
		}
		if (flags & (SF_OPEN_READ | SF_OPEN_RAW)) {
			/* Informative data for existing files. */
			pdesc->file_len = sflash_info.databuf.u32[HEADER_SIZE_L + 0];
			if (pdesc->file_len == 0xFFFFFFFF) {
				pdesc->open_flags |= SF_XOPEN_UNCLOSED;
			}
			pdesc->open_flags |=
				(sflash_info.databuf.u8[HEADER_SIZE_B + 34] &
				 (SF_OPEN_EXT_SYNC | _SF_OPEN_RES));
			pdesc->file_ctime = sflash_info.databuf.u32[HEADER_SIZE_L + 2]; /* get file creation time */
			return 0;
		}
		if (!(flags & SF_OPEN_CREATE)) {
			return JESFS_ERR_BAD_FILE_FLAGS;
		}
		res = flash_set2delete(sadr);
		if (res) {
			return res;
		}
		sfun_adr = sadr;
	} else {
		if (!(flags & SF_OPEN_CREATE)) {
			return JESFS_ERR_FILE_NOT_FOUND;
		}
	}

	if (jesfs_supply_voltage_check()) {
		sflash_info.state_flags |= STATE_POWERFAIL; /* Lock Flash until DEEPSLEEP */
		return JESFS_ERR_VOLTAGE_TOO_LOW; /* Lock Flash Access if power is too low */
	}

	if (!sfun_adr) {
		sfun_adr = sflash_get_free_sector();
		if (!sfun_adr) {
			return JESFS_ERR_NO_FREE_SECTOR;
		}
		if (HEADER_SIZE_B + sflash_info.files_used * 4 >= (SF_SECTOR_PH - 4)) {
			return JESFS_ERR_INDEX_FULL;
		}
		res = sflash_sector_write(HEADER_SIZE_B + sflash_info.files_used * 4, (uint8_t *)&sfun_adr, 4);
		if (res) {
			return res;
		}
		new_index_entry = 1;
	} else {
		res = sflash_sector_erase(sfun_adr);
		if (res) {
			return res;
		}
	}

	jesfs_memset((uint8_t *)&sflash_info.databuf, 0xFF, HEADER_SIZE_B + FINFO_SIZE_B);
	sflash_info.databuf.u32[0] = SECTOR_MAGIC_HEAD_ACTIVE;
	jesfs_strncpy((char *)&sflash_info.databuf.u8[HEADER_SIZE_B + 12], pname, FNAMELEN);
	pdesc->file_ctime = jesfs_get_secs();
	sflash_info.databuf.u32[HEADER_SIZE_L + 2] = pdesc->file_ctime;
	sflash_info.databuf.u8[HEADER_SIZE_B + 34] = flags;
	res = sflash_sector_write(sfun_adr, (uint8_t *)&sflash_info.databuf, HEADER_SIZE_B + FINFO_SIZE_B);
	if (res) {
		return res;
	}

	pdesc->_head_sadr = sfun_adr;
	pdesc->_wrk_sadr = sfun_adr;

	if (new_index_entry) {
		sflash_info.available_disk_size -= SF_SECTOR_PH;
		sflash_info.files_used++;
	}
	sflash_info.files_active++;
	return 0;
}

/* Return 0 if the file exists, otherwise a negative JesFs error. */
int16_t jesfs_notexists(const char *pname)
{
	struct jesfs_desc fs_desc_test;
	return jesfs_open(&fs_desc_test, pname, SF_OPEN_READ);
}

/* Append/write data to an opened file descriptor. */
int16_t jesfs_write(struct jesfs_desc *pdesc, const uint8_t *pdata, uint32_t len)
{
	int16_t res;
	uint32_t maxwrite;
	uint32_t wlen;
	uint32_t newsect;

	if (sflash_info.state_flags & STATE_DEEPSLEEP_OR_POWERFAIL) {
		return JESFS_ERR_FLASH_NOT_ACCESSIBLE;
	}
	if (!pdesc->_head_sadr) {
		return JESFS_ERR_BAD_DESCRIPTOR;
	}
	if (pdesc->open_flags & SF_OPEN_RAW) {
		if (pdesc->file_pos != pdesc->file_len) {
			return JESFS_ERR_RAW_WRITE_UNKNOWN_END;
		}
	} else if (!(pdesc->open_flags & SF_OPEN_WRITE)) {
		return JESFS_ERR_NOT_OPEN_FOR_WRITE;
	}

	if (jesfs_supply_voltage_check()) {
		sflash_info.state_flags |= STATE_POWERFAIL; /* Lock Flash until DEEPSLEEP */
		return JESFS_ERR_VOLTAGE_TOO_LOW; /* Lock Flash Access if power is too low */
	}

	while (len) {
		maxwrite = SF_SECTOR_PH - pdesc->_sadr_rel;
		if (maxwrite > SF_SECTOR_PH) {
			return JESFS_ERR_SECTOR_BORDER_VIOLATED;
		}
		if (!maxwrite) {
			newsect = sflash_get_free_sector();
			if (!newsect) {
				return JESFS_ERR_NO_FREE_SECTOR;
			}

			res = sflash_sector_write(pdesc->_wrk_sadr + 8, (uint8_t *)&newsect, 4);
			if (res) {
				return res;
			}

			pdesc->_wrk_sadr = newsect;
			pdesc->_sadr_rel = HEADER_SIZE_B;
			maxwrite = SF_SECTOR_PH - HEADER_SIZE_B;
			sflash_info.databuf.u32[0] = SECTOR_MAGIC_DATA;
			sflash_info.databuf.u32[1] = pdesc->_head_sadr;
			res = sflash_sector_write(pdesc->_wrk_sadr, (uint8_t *)&sflash_info.databuf, 8);
			if (res) {
				return res;
			}
			sflash_info.available_disk_size -= SF_SECTOR_PH;
		}

		wlen = len;
		if (wlen > maxwrite) {
			wlen = maxwrite;
		}
		res = sflash_sector_write(pdesc->_wrk_sadr + pdesc->_sadr_rel, pdata, wlen);
		if (res) {
			return res;
		}
		if (pdesc->open_flags & SF_OPEN_CRC) {
			pdesc->file_crc32 = jesfs_track_crc32(pdata, wlen, pdesc->file_crc32);
		}
		len -= wlen;
		pdata += wlen;
		pdesc->_sadr_rel += wlen;
		pdesc->file_pos += wlen;
		if (pdesc->file_len != 0xFFFFFFFF) {
			pdesc->file_len = pdesc->file_pos;
		}
	}
	return 0;
}

int16_t jesfs_close(struct jesfs_desc *pdesc)
{
	int16_t res;
	uint32_t s0adr;
	uint32_t hinfo[2];

	if (sflash_info.state_flags & STATE_DEEPSLEEP_OR_POWERFAIL) {
		return JESFS_ERR_FLASH_NOT_ACCESSIBLE;
	}
	if (!pdesc->_head_sadr) {
		return JESFS_ERR_BAD_DESCRIPTOR;
	}
	s0adr = pdesc->_head_sadr;
	if ((pdesc->open_flags & SF_OPEN_WRITE) ||
	    ((pdesc->open_flags & SF_OPEN_CREATE) && !(pdesc->open_flags & SF_OPEN_RAW))) {
		if (sflash_sadr_invalid(s0adr)) {
			return JESFS_ERR_BAD_SECTOR_ADDR;
		}

		if (jesfs_supply_voltage_check()) {
			sflash_info.state_flags |= STATE_POWERFAIL; /* Lock Flash until DEEPSLEEP */
			return JESFS_ERR_VOLTAGE_TOO_LOW; /* Lock Flash Access if power is too low */
		}

		hinfo[0] = pdesc->file_pos;
		hinfo[1] = pdesc->file_crc32;
		res = sflash_sector_write(s0adr + HEADER_SIZE_B + 0, (uint8_t *)hinfo, 8);
		if (res) {
			return res;
		}
	}
	pdesc->_head_sadr = 0; /* Invalidate descriptor */
	return 0; /* OK */
}

uint32_t jesfs_get_crc32(struct jesfs_desc *pdesc)
{
	uint32_t rd_crc;

	if (sflash_info.state_flags & STATE_DEEPSLEEP_OR_POWERFAIL) {
		return 0; /* Be. values not possible */
	}
	if (!pdesc->_head_sadr) {
		return 0;
	}
	int16_t res = sflash_read(pdesc->_head_sadr + HEADER_SIZE_B + 4, (uint8_t *)&rd_crc, 4);
	if (res) {
		return 0;
	}
	return rd_crc;
}

int16_t jesfs_delete(struct jesfs_desc *pdesc)
{
	int16_t res;

	if (sflash_info.state_flags & STATE_DEEPSLEEP_OR_POWERFAIL) {
		return JESFS_ERR_FLASH_NOT_ACCESSIBLE;
	}
	if (!pdesc->_head_sadr) {
		return JESFS_ERR_BAD_DESCRIPTOR;
	}
	if (pdesc->open_flags & SF_OPEN_WRITE) {
		return JESFS_ERR_BAD_FILE_FLAGS;
	}
	res = flash_set2delete(pdesc->_head_sadr);
	if (res) {
		return res;
	}
	pdesc->_head_sadr = (uint32_t)0; /* No Close! */
	return 0;
}

/*
 * Rename a file by copying the first-sector metadata from the new descriptor.
 *
 * This can also change management flags such as
 * hidden/sync, but not length,
 * CRC, or creation date. A different target name must be used.
 * Tip: with jesfs_set_static_secs(secs) you can set a static creation date for testing purposes,
 * reset with jesfs_set_static_secs(0) to use the real time again.
 */
int16_t jesfs_rename(struct jesfs_desc *pd_odesc, struct jesfs_desc *pd_ndesc)
{
	uint16_t mlen;
	uint32_t thdr[6];
	int16_t res;

	if (sflash_info.state_flags & STATE_DEEPSLEEP_OR_POWERFAIL) {
		return JESFS_ERR_FLASH_NOT_ACCESSIBLE;
	}

	if (jesfs_supply_voltage_check()) {
		sflash_info.state_flags |= STATE_POWERFAIL; /* Lock Flash until DEEPSLEEP */
		return JESFS_ERR_VOLTAGE_TOO_LOW; /* Lock Flash Access if power is too low */
	}

	if (!pd_odesc->_head_sadr || !pd_ndesc->_head_sadr) {
		return JESFS_ERR_RENAME_FILES_NOT_OPEN;
	}
	if (pd_ndesc->open_flags & (SF_OPEN_READ | SF_OPEN_RAW)) {
		return JESFS_ERR_RENAME_OPEN_FOR_READ_OR_RAW;
	}
	if (pd_ndesc->file_len) {
		return JESFS_ERR_RENAME_TARGET_NOT_EMPTY;
	}

	if (pd_odesc->file_len == 0xFFFFFFFF) {
		int32_t found_len =
			sflash_find_mlen(pd_odesc->_head_sadr + HEADER_SIZE_B + FINFO_SIZE_B,
					 SF_SECTOR_PH - HEADER_SIZE_B - FINFO_SIZE_B);

		if (found_len < 0) {
			return (int16_t)found_len;
		}
		mlen = (uint16_t)found_len;
	} else if (pd_odesc->file_len > SF_SECTOR_PH - HEADER_SIZE_B - FINFO_SIZE_B) {
		mlen = SF_SECTOR_PH - HEADER_SIZE_B - FINFO_SIZE_B;
	} else {
		mlen = pd_odesc->file_len;
	}

	thdr[0] = SECTOR_MAGIC_HEAD_ACTIVE;
	thdr[1] = 0xFFFFFFFF;
	res = sflash_read(pd_odesc->_head_sadr + 8, (uint8_t *)&thdr[2], 16); /* Nx Ln CRC Dt */
	if (res) {
		return res;
	}
	res = flash_intrasec_copy(pd_odesc->_head_sadr + HEADER_SIZE_B + FINFO_SIZE_B,
				  pd_ndesc->_head_sadr + HEADER_SIZE_B + FINFO_SIZE_B,
				  mlen);
	if (res) {
		return res;
	}
	res = sflash_sector_erase(pd_odesc->_head_sadr);
	if (res) {
		return res;
	}
	res = flash_intrasec_copy(pd_ndesc->_head_sadr + HEADER_SIZE_B + 12,
				  pd_odesc->_head_sadr + HEADER_SIZE_B + 12,
				  mlen + FINFO_SIZE_B - 12);
	if (res) {
		return res;
	}

	res = sflash_sector_write(pd_odesc->_head_sadr, (uint8_t *)thdr, 24);
	if (res) {
		return res;
	}

	pd_ndesc->open_flags = 0;
	res = jesfs_delete(pd_ndesc);
	if (res) {
		return res;
	}

	pd_ndesc->_head_sadr = 0;
	return 0;
}

int16_t jesfs_info(struct jesfs_stat *pstat, uint16_t fno)
{
	uint32_t sadr, idx_adr;
	int16_t ret;

	if (sflash_info.state_flags & STATE_DEEPSLEEP_OR_POWERFAIL) {
		return JESFS_ERR_FLASH_NOT_ACCESSIBLE;
	}
	idx_adr = HEADER_SIZE_B + fno * 4;
	if (idx_adr > SF_SECTOR_PH - 4) {
		return FS_STAT_INDEX;
	}
	ret = sflash_read(idx_adr, (uint8_t *)&sadr, 4); /* Read Sector, where Index(fno) points to */
	if (ret) {
		return ret;
	}
	if (sadr == 0xFFFFFFFF) {
		return 0; /* This Index-Entry is unused */
	}
	if (!sadr) {
		return JESFS_ERR_BAD_INDEX_ENTRY; /* Index is taboo! */
	}
	if (sadr >= sflash_info.total_flash_size) { /* Points to outside? Severe Error */
		return JESFS_ERR_STAT_INDEX_RANGE;
	}

	ret = sflash_read(sadr, (uint8_t *)&sflash_info.databuf, HEADER_SIZE_B + FINFO_SIZE_B);
	if (ret) {
		return ret;
	}

/* Each Index-Entry points to a HEAD: Either ACTIVE or DELETED. All other is an Error */
	switch (sflash_info.databuf.u32[0]) {
	case SECTOR_MAGIC_HEAD_ACTIVE:
		ret = FS_STAT_ACTIVE;
		break;
	case SECTOR_MAGIC_HEAD_DELETED:
		ret = FS_STAT_INACTIVE;
		break;

/* Entry points to unidentified Head. Possible Reason: */
		/*
		 * Power loss on jesfs_open() for write/create or on nRF52 JTAG,
		 * which can access the serial flash.
		 */
	case 0xFFFFFFFF:
		return JESFS_ERR_BAD_FS_STRUCTURE;
	default:
		return JESFS_ERR_BAD_INDEX_HEAD;
	}

	pstat->_head_sadr = sadr;
	jesfs_strncpy(pstat->fname, (char *)&sflash_info.databuf.u8[HEADER_SIZE_B + 12], FNAMELEN);
	pstat->file_crc32 = sflash_info.databuf.u32[HEADER_SIZE_L + 1];
	pstat->file_ctime = sflash_info.databuf.u32[HEADER_SIZE_L + 2];
	pstat->disk_flags = sflash_info.databuf.u8[HEADER_SIZE_B + 34];
	sadr = sflash_info.databuf.u32[HEADER_SIZE_L];
	if (sadr == 0xFFFFFFFF) { /* Unclosed File */
		ret |= FS_STAT_UNCLOSED;
		pstat->disk_flags |= SF_XOPEN_UNCLOSED; /* Informational flag for callers. */
	}
	pstat->file_len = sadr;
	return ret;
}

/*
 * Run a careful disk check.
 *
 * Returns 0 if no error was found, a negative JesFs error for critical errors,
 * or a positive count of
 * non-critical errors. If cb_printf is not NULL,
 * diagnostics are emitted through that callback.
 */
int16_t jesfs_check_disk(void cb_printf(const char *fmt, ...))
{
	int16_t res;
	uint16_t i;
	int32_t lres;
	uint32_t aval;
	int16_t err = 0;

/* Might consume some stack space: */
	struct jesfs_stat lfs_stat;
	struct jesfs_desc lfs_desc;

	if (cb_printf) {
		cb_printf("Check Disk...\n");
	}

	res = jesfs_start(FS_START_NORMAL);
	if (res) {
		if (cb_printf) {
			cb_printf("ERROR: Disc Error:%d\n", res);
		}
		if (res == JESFS_ERR_VOLTAGE_TOO_LOW) {
			return JESFS_ERR_VOLTAGE_TOO_LOW; /* Power Loss! */
		}
		err++;
	}
#ifdef JSTAT
	if (cb_printf) {
		cb_printf("Sectors Empty/Recyclable: %d/%d\n", sflash_info.sectors_clear,
			  sflash_info.sectors_todelete);
		if (sflash_info.sectors_unknown) {
			cb_printf("ERROR: Unknown Sectors: %d\n", sflash_info.sectors_unknown);
			err++;
		}
	}

#endif

	for (i = 0;; i++) {
		res = jesfs_info(&lfs_stat, i);

		if (i >= sflash_info.files_used) {
			if (res == FS_STAT_INDEX) {
				break;
			}
			if (!res) {
				continue; /* Check unused entries also */
			}
		}
		if (cb_printf && res > 0) {
			if (res & FS_STAT_INACTIVE) {
				cb_printf("Check Index(%u): ('%s' (deleted))\n", i, lfs_stat.fname);
			} else {
				cb_printf("Check Index(%u): '%s'\n", i, lfs_stat.fname);
			}
		}

		if (res < 0 || (res & ~(FS_STAT_ACTIVE | FS_STAT_INACTIVE | FS_STAT_UNCLOSED))) {
			if (cb_printf) {
				cb_printf("ERROR: Index(%u):%d\n", i, res);
			}
			err++;
			continue;
		}

		if (res > 0 && (res & FS_STAT_ACTIVE)) {
			if (res & FS_STAT_UNCLOSED) {
				res = jesfs_open(&lfs_desc, lfs_stat.fname, SF_OPEN_READ | SF_OPEN_RAW);
				if (res < 0) {
					if (cb_printf) {
						cb_printf("ERROR: Open '%s':%d\n", lfs_stat.fname, res);
					}
					err++;
				} else {
					if (lfs_stat.disk_flags & SF_OPEN_CRC) {
/* CRC cannot be verified before an unclosed file has a final length. */
						if (cb_printf) {
							cb_printf("ERROR: Unclosed File with CRC '%s'\n", lfs_stat.fname);
						}
						err++;
					}
					lres = jesfs_read(&lfs_desc, NULL, 0xFFFFFFFF);
					if (lres < 0) {
						if (cb_printf) {
							cb_printf("ERROR: Unclosed Read '%s':%d\n", lfs_stat.fname, lres);
						}
						err++;
					}
					(void)jesfs_close(&lfs_desc);
				}
			} else if (lfs_stat.disk_flags & SF_OPEN_CRC) {
				res = jesfs_open(&lfs_desc, lfs_stat.fname, SF_OPEN_READ | SF_OPEN_CRC);
				if (res < 0) {
					if (cb_printf) {
						cb_printf("ERROR: Open '%s':%d\n", lfs_stat.fname, res);
					}
					err++;
				} else {
					aval = lfs_stat.file_len;
					if (aval > sflash_info.total_flash_size) {
						if (cb_printf) {
							cb_printf("ERROR: Illegal File Size '%s':%u Bytes\n", lfs_stat.fname, aval);
						}
						err++;
					} else {
						while (aval) {
							res = jesfs_read(&lfs_desc, (uint8_t *)&sflash_info.databuf, SF_BUFFER_SIZE_B);
							if (res <= 0 || res > SF_BUFFER_SIZE_B || res > aval) {
								if (cb_printf) {
									cb_printf("ERROR: Read Data '%s':%d\n", lfs_stat.fname, res);
								}
								err++;
								break;
							}
							aval -= res;
						}
						if (lfs_stat.file_crc32 != lfs_desc.file_crc32) {
							if (cb_printf) {
								cb_printf("ERROR: CRC false '%s'\n", lfs_stat.fname);
							}
							err++;
						}
					}
					(void)jesfs_close(&lfs_desc);
				}
			}
		}
	}
	if (cb_printf) {
		if (err) {
			cb_printf("ERROR(s): %d\n", err);
		} else {
			cb_printf("Disk OK\n");
		}
	}
	return err;
}

/* ------------------- High-level FS OK ------------------------ */

/* ----------------------------------------------- JESFS-End ---------------------- */
