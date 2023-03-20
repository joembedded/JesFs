/*******************************************************************************
 * JesFs_hl.c: JesFs HighLevel(User) drivers
 *
 * JesFs - Jo's Embedded Serial File System
 *
 * (C) joembedded@gmail.com - www.joembedded.de
 * Version:
 * 1.5 / 25.11.2019
 * 1.6 / 22.12.2019 added fs_check_disk()
 * 1.7 / 12.03.2020 added fs_date2sec1970()
 * 1.8 / 25.09.2020 added fs_set_static_secs() to set a static time for JesFs
 * 1.81 / 19.12.2020 redundant code removed in fs_date2sec1970()
 * 1.82 / 21.03.2021 added comment fs_format() Timeouts
 * 1.83 / 11.07.2021 added Pin Definitions for NRF52
 * 1.84 / 15.08.2021 check in fs_date2sec1970
 * 1.85 / 17.03.2022 added check (Warren)
 * 1.86 / 18.03.2022 corrected bug in fs_date2sec1970()
 * 1.87 / 02.04.2022 fs_strcpy()->fsstrncpy() and some minor opts.
 * 1.88 / 17.03.2023 added feature _supply_voltage_check()
 *
 *******************************************************************************/

/* Required Std. headers */
#include <stddef.h>
#include <stdint.h>

//---------------------------------------------- JESFS-START ----------------------
#include "jesfs.h"
#include "jesfs_int.h"

extern uint32_t _time_get(void);      // We need Unix-Seconds, must be defined outside - UserProvided!
static uint32_t _static_time = 0; // If <>0: time used for fs_open() with Create or fs_format()
extern int16_t _supply_voltage_check(void);  // Return 0 if Power is OK, else  Error -147

// Driver designed for 4k-Flash (or larger) - JesFs
#if SF_SECTOR_PH != 4096
#error "Phyiscal Sector Size SPIFlash must be 4K"
#endif
#if FNAMELEN != 21
#error "FNAMELEN fixed to 21+Zero-Byte by Design"
#endif

//------------------- HighLevel FS Start ------------------------
// May omit unnecessary libs, slightly modified to standard
uint32_t fs_strlen(char *s) {
  char *p = s;
  while (*p)
    p++;
  return p - s;
}
void fs_strncpy(char *d, char *s, uint8_t maxchar) { // Added maxlen in V1.87
  char c;
  for (;;) {
    c = *s++;
    if (!c || !maxchar--)
      break;
    *d++ = c;
  }
  *d = 0;
}
void fs_memset(uint8_t *p, uint8_t v, uint32_t n) {
  while (n--)
    *p++ = v;
}
int16_t fs_strcmp(char *s1, char *s2) { // Only required for equal(0) or !equal(!0)
  for (;;) {
    if (*s1 == 0 || *s1 != *s2)
      return *s1 - *s2;
    s1++;
    s2++;
  }
}

// Set a static time / 0 for JesFs
void fs_set_static_secs(uint32_t newsecs) {
  _static_time = newsecs;
}

uint32_t fs_get_secs(void) { // Unix-Secs
  if (_static_time)
    return _static_time;
  else
    return _time_get(); // Macro from above
}

//------ Date-Routines (carefully tested!)---------------
#define SEC_DAY 86400L // Length of day in seconds for leap year
static const uint32_t daylen[12] = {
    31 * SEC_DAY,  /* Jan */
    59 * SEC_DAY,  /* Feb */
    90 * SEC_DAY,  /* Mar */
    120 * SEC_DAY, /* Apr */
    151 * SEC_DAY, /* Mai */
    181 * SEC_DAY, /* June */
    212 * SEC_DAY, /* Juli */
    243 * SEC_DAY, /* Aug. */
    273 * SEC_DAY, /* Sept */
    304 * SEC_DAY, /* Oct */
    334 * SEC_DAY, /* Nov. */
    365 * SEC_DAY, /* Dec. */
};
/* Convert seconds to date-struct (1.1.1970 00:00:00 = 0secs ) */
void fs_sec1970_to_date(uint32_t asecs, FS_DATE *pd) {
  uint32_t divs;
  uint8_t dlap = 0;
  divs = asecs / (1461 * SEC_DAY);
  pd->a = 1970 + divs * 4;
  asecs -= (divs * (1461 * SEC_DAY)); // 3 normal + 1 leap year
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
    for (divs = 0; asecs >= daylen[divs]; divs++)
      ;
  }
  pd->m = 1 + divs;
  if (divs)
    asecs -= daylen[divs - 1];
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

static const uint8_t days_per_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};      // Days per Month Normal
static const uint16_t days_summed[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334}; // Days summed

// Convert date to unix seconds - Return 0 (== 1.1.1970 00:00:00) on Failure
uint32_t fs_date2sec1970(FS_DATE *pd) {
  uint32_t nsec;
  uint16_t year_base;
  uint8_t year_idx;

  year_base = pd->a - 1970;
  year_idx = year_base % 4; // 0,1,2:Schaltjahr,3
  if (year_base > 129)
    return 0; // OK von 1970-2099 (2100 is NO leap year)
  if (pd->m < 1 || pd->m > 12 || pd->d < 1)
    return 0;                                         // Month: 1..12 Day: 1..x,
  if (pd->d > days_per_month[pd->m - 1]) {            // Check Day x ok for this month?
    if (year_idx != 2 || pd->m != 2 || pd->d != 29) { // Only Exception
      return 0;
    }
  }
  if (pd->h > 23 || pd->m > 59 || pd->sec > 59)
    return 0;

  nsec = ((uint32_t)year_base / 4) * (1461 * SEC_DAY);                // Complete 4-years
  nsec += ((uint32_t)year_idx) * (365 * SEC_DAY);                     //
  nsec += ((uint32_t)days_summed[pd->m - 1] + (pd->d - 1)) * SEC_DAY; // plus days for 4-years
  if (year_idx == 3 || (year_idx == 2 && pd->m > 2))
    nsec += SEC_DAY; // Add leap day

  nsec += (pd->h * 3600);
  nsec += (pd->min * 60);
  nsec += pd->sec;

  return nsec;
}

/* Calculating a CRC32: Also useful for external use */
#define POLY32 0xEDB88320 // ISO 3309
uint32_t fs_track_crc32(uint8_t *pdata, uint32_t wlen, uint32_t crc_run) {
  uint8_t j;
  while (wlen--) {
    crc_run ^= *pdata++;
    for (j = 0; j < 8; j++) {
      if (crc_run & 1)
        crc_run = (crc_run >> 1) ^ POLY32;
      else
        crc_run = crc_run >> 1;
    }
  }
  return crc_run;
}

static int16_t sflash_sadr_invalid(uint32_t sadr) {
  if (sadr == 0xFFFFFFFF)
    return 0; // OK
  if (!sadr)
    return -1;
  if (sadr & (SF_SECTOR_PH - 1))
    return -2; // FATAL
  if (sadr >= sflash_info.total_flash_size)
    return -3; // FATAL
  return 0;    // Ok
}

static int16_t flash_set2delete(uint32_t sadr) {
  int16_t res;
  uint32_t thdr[3];
  uint32_t max_sect;
  uint32_t oadr;
  oadr = sadr;
  max_sect = (sflash_info.total_flash_size / SF_SECTOR_PH);
  while (--max_sect) {
    if (sflash_sadr_invalid(sadr))
      return -120;
    sflash_read(sadr, (uint8_t *)thdr, 12);
    if (thdr[0] == SECTOR_MAGIC_HEAD_ACTIVE) {
      if (thdr[1] != 0xFFFFFFFF)
        return -122;
      thdr[0] = SECTOR_MAGIC_HEAD_DELETED;
      sflash_info.files_active--;
    } else if (thdr[0] == SECTOR_MAGIC_DATA) {
      if (thdr[1] != oadr)
        return -122;
      thdr[0] = SECTOR_MAGIC_TODELETE;
      sflash_info.available_disk_size += SF_SECTOR_PH;
    } else
      return -123; // Illegal
    res = sflash_SectorWrite(sadr, (uint8_t *)thdr, 4);
    if (res)
      return res;
    sadr = thdr[2];
    if (sadr == 0xFFFFFFFF)
      return 0;
  }
  return -121;
}
// Find last used byte index in a sector (max_sec_rd<=SF_SECTOR_PH). Returns 0 is sector is totaly empty (all bytes FF)
static uint16_t sflash_find_mlen(uint32_t sadr, uint16_t max_sec_rd) {
  uint16_t wlen;
  uint16_t used_len = max_sec_rd;
  sadr += max_sec_rd;
  while (max_sec_rd) {
    wlen = max_sec_rd;
    if (wlen > SF_BUFFER_SIZE_B)
      wlen = SF_BUFFER_SIZE_B;
    max_sec_rd -= wlen;
    sadr -= wlen;
    sflash_read(sadr, (uint8_t *)&sflash_info.databuf, wlen);
    while (wlen--) {
      if (sflash_info.databuf.u8[wlen] != 0xFF)
        return used_len;
      used_len--;
    }
  }
  return 0;
}

// Copy IntraFlash and Page-Safe
static int16_t flash_intrasec_copy(uint32_t sadr, uint32_t dadr, uint16_t clen) {
  int16_t res;
  uint16_t blen;
  while (clen) {
    blen = clen;
    if (blen > SF_BUFFER_SIZE_B)
      blen = SF_BUFFER_SIZE_B;

    sflash_read(sadr, (uint8_t *)&sflash_info.databuf, blen);
    res = sflash_SectorWrite(dadr, (uint8_t *)&sflash_info.databuf, blen);
    if (res)
      return res;
    sadr += blen;
    dadr += blen;
    clen -= blen;
  }
  return 0;
}

//--------------------------- Frm here User Functions ----------------------------------------

/* Start Filesystem - Fill structurs and check basic parameters */
int16_t fs_start(uint8_t mode) {
  int16_t res;
  uint32_t id;
  uint32_t sadr;
  uint32_t idx_adr;
  uint32_t dir_typ;
  uint16_t err;

  sflash_spi_init();
  
  if (_supply_voltage_check()) {
    sflash_info.creation_date = 0xFFFFFFFF; // Invalidate Disk
    sflash_info.total_flash_size = 0;
    sflash_info.identification = 0;
    sflash_info.state_flags |= STATE_POWERFAIL;
    return -147; // Lock Flash Access if power is too low
  }

  err=3;    // Try 3 wakes before returning an Error
  while(err--){
    // Flash wakeup
    sflash_ReleaseFromDeepPowerDown();
    sflash_wait_usec(45);
    sflash_info.state_flags &= ~(STATE_DEEPSLEEP_OR_POWERFAIL );

    // ID read and get setup
    id = sflash_QuickScanIdentification();
    if (mode & FS_START_RESTART) {
      if (sflash_info.total_flash_size && id == sflash_info.identification) {
        return 0; // Wake only
      }
    }
    sflash_info.creation_date = 0xFFFFFFFF; // Assume Invalid Disk

    res = sflash_interpret_id(id);
    if(!res) break;
  }
  if (res)
      return res;


  // OK, Flash is known - Read 12 Bytes (3 Longs) of the Header
  sflash_read(0, (uint8_t *)&sflash_info.databuf, HEADER_SIZE_B);

  if (sflash_info.databuf.u32[0] == 0xFFFFFFFF)
    return -108;
  if (sflash_info.databuf.u32[0] != HEADER_MAGIC)
    return -146;
  if (sflash_info.databuf.u32[1] != sflash_info.identification)
    return -109;

  sflash_info.creation_date = sflash_info.databuf.u32[2]; // Creation date must be anyting different from 0xFFFFFFFF

  err = 0;
  sflash_info.available_disk_size = sflash_info.total_flash_size - SF_SECTOR_PH;

#ifdef JSTAT
  sflash_info.sectors_todelete = 0; // Not really required, just for statistics
  sflash_info.sectors_clear = 0;
  sflash_info.sectors_unknown = 0;
#endif

  sflash_info.files_used = 0;
  sflash_info.files_active = 0;

  sflash_info.lusect_adr = 0;
  // Scan Headers of all sectors (FAST or normal)
  // Scan  Takes on 1M-Flash 12msec, 16M-Flash: 200msec (12 MHz SPI)
  for (sadr = SF_SECTOR_PH; sadr < sflash_info.total_flash_size; sadr += SF_SECTOR_PH) {
    sflash_read(sadr, (uint8_t *)&sflash_info.databuf, (mode & FS_START_FAST) ? 4 : 12);
    switch (sflash_info.databuf.u32[0]) {
    case 0xFFFFFFFF:
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

    case SECTOR_MAGIC_HEAD_ACTIVE:
      sflash_info.files_active++;
      sflash_info.lusect_adr = sadr;
    case SECTOR_MAGIC_HEAD_DELETED:
      sflash_info.files_used++;
      sflash_info.lusect_adr = sadr;

    case SECTOR_MAGIC_DATA:
      sflash_info.available_disk_size -= SF_SECTOR_PH;
      sflash_info.lusect_adr = sadr;
      break;

    default:
#ifdef JSTAT
      sflash_info.sectors_unknown++; // !!! Big Failure
#endif
      err++;
    }

    if (!(mode & FS_START_FAST)) {
      switch (sflash_info.databuf.u32[0]) {
      case 0xFFFFFFFF:
        if (sflash_info.databuf.u32[1] != 0xFFFFFFFF || sflash_info.databuf.u32[2] != 0xFFFFFFFF)
          err++;
        break;
      case SECTOR_MAGIC_HEAD_ACTIVE:
      case SECTOR_MAGIC_HEAD_DELETED:
        if (sflash_info.databuf.u32[1] != 0xFFFFFFFF)
          err++;
        if (sflash_sadr_invalid(sflash_info.databuf.u32[2]))
          err++;
        break;
      case SECTOR_MAGIC_DATA:
      case SECTOR_MAGIC_TODELETE:
        idx_adr = sflash_info.databuf.u32[1];
        if (idx_adr == 0xFFFFFFFF || sflash_sadr_invalid(idx_adr))
          err++;
        if (sflash_sadr_invalid(sflash_info.databuf.u32[2]))
          err++;
        break;
      }
    }
  }

  sadr = HEADER_SIZE_B;
  id = 0;
  while (sadr != SF_SECTOR_PH) {
    sflash_read(sadr, (uint8_t *)&idx_adr, 4);
    if (idx_adr == 0xFFFFFFFF)
      break;
    else {
      if (sflash_sadr_invalid(idx_adr))
        err++;
      else {
        sflash_read(idx_adr, (uint8_t *)&dir_typ, 4);
        if (dir_typ == SECTOR_MAGIC_HEAD_ACTIVE || dir_typ == SECTOR_MAGIC_HEAD_DELETED)
          id++;
        else
          err++;
      }
    }
    sadr += 4;
  }

  if (err || (uint16_t)id != sflash_info.files_used)
    return -107; // Corrupt Data?
  return 0;      // OK
}

/* Set Flash to Ultra-Low-Power mode. Call fs_start(FS_RESTART) to continue/wake */
int16_t fs_deepsleep(void) {
  if (sflash_info.state_flags & STATE_DEEPSLEEP)
    return -140; // Already sleeping, 2.nd command could wake FS again
  sflash_info.state_flags |= (STATE_DEEPSLEEP);
  sflash_DeepPowerDown();
  sflash_spi_close(); // Added V1.51
  return 0;
}

/* Format Filesystem. May require between 30-240 seconds (even more, see Datasheet) for a 512k-16 MB Flash) (changed in V1.1)
 * Warning: fmode=FS_FORMAT_FULL ('Bulk Erase') might need VERY long on some (larger) Chips (> 240 secs,  which is Default Timeout).
 * Better to use fmode=FS_FORMAT_SOFT (which erases only non-empty 4k sectors). */
int16_t fs_format(uint8_t fmode) {
  uint32_t sbuf[3];
  int16_t res;
  uint32_t sadr;

  if (sflash_info.state_flags & STATE_DEEPSLEEP_OR_POWERFAIL)
    return -148;
  if (fmode == FS_FORMAT_SOFT) {
    for (sadr = 0; sadr < sflash_info.total_flash_size; sadr += SF_SECTOR_PH) {
      sflash_read(sadr, (uint8_t *)&sbuf, 8); // Read the 8 byte header: Magic and owner (owner for later...)
      if (sbuf[0] == 0xFFFFFFFF) {            // Header says: Empty
        res = sflash_find_mlen(sadr, SF_SECTOR_PH);
        if (!res)
          continue; // yes.
      }             // else if(sbuf[0]==SECTOR_MAGIC_DATA || sbuf[0]==SECTOR_MAGIC_HEAD_ACTIVE){... // reserved for JesFs V1.2, e.g. keep System Files
      res = sflash_SectorErase(sadr);
      if (res)
        return res;
    }
  } else if (fmode == FS_FORMAT_FULL) {
    if (sflash_WaitWriteEnabled())
      return -102; // Wait enabled until OK, Fehler 1:1
    sflash_BulkErase();
    if (sflash_WaitBusy(240000))
      return -101; //
  } else
    return -139; // Parameter

  sbuf[0] = HEADER_MAGIC;
  sbuf[1] = sflash_info.identification;
  sbuf[2] = fs_get_secs(); // Creation Date of Disk is NOW

  res = sflash_SectorWrite(0, (uint8_t *)sbuf, 12); // Header V1.0
  if (res)
    return res;

  return fs_start(FS_START_NORMAL);
}

static uint32_t sflash_get_free_sector(void) {
  uint32_t thdr;
  uint32_t max_sect;
  // Some embedded compilers complain about the Division. In fact, it will result in a shift. So it might be ignored
  max_sect = (sflash_info.total_flash_size / SF_SECTOR_PH);
  while (--max_sect) {
    sflash_info.lusect_adr += SF_SECTOR_PH;
    if (sflash_info.lusect_adr >= sflash_info.total_flash_size)
      sflash_info.lusect_adr = SF_SECTOR_PH;
    sflash_read(sflash_info.lusect_adr, (uint8_t *)&thdr, 4);

    if (thdr == SECTOR_MAGIC_TODELETE || thdr == 0xFFFFFFFF) {
      if (thdr == SECTOR_MAGIC_TODELETE) {
        if (sflash_SectorErase(sflash_info.lusect_adr))
          return 0;
      }
      return sflash_info.lusect_adr;
    }
  }
  return 0;
}

// --- fs_read() ---
int32_t fs_read(FS_DESC *pdesc, uint8_t *pdest, uint32_t anz) {
  uint32_t h;
  uint32_t next_sect;
  int32_t total_rd = 0; // max 2GB
  uint16_t max_sec_rd;
  uint16_t uc_mlen;

  if (sflash_info.state_flags & STATE_DEEPSLEEP_OR_POWERFAIL)
    return -148;
  if (!pdesc->_head_sadr)
    return -117;
  if (!(pdesc->open_flags & (SF_OPEN_READ | SF_OPEN_RAW))) // Warren mod. 17.03.2022
    return -125;

  while (anz) {
    sflash_read(pdesc->_wrk_sadr, (uint8_t *)&sflash_info.databuf, HEADER_SIZE_B + FINFO_SIZE_B);
    h = sflash_info.databuf.u32[0];
    if (h == SECTOR_MAGIC_HEAD_ACTIVE) {
      if (sflash_info.databuf.u32[1] != 0xFFFFFFFF)
        return -128;
    } else if (h == SECTOR_MAGIC_DATA) {
      if (sflash_info.databuf.u32[1] != pdesc->_head_sadr)
        return -122;
    } else
      return -123;

    next_sect = sflash_info.databuf.u32[2];
    if (sflash_sadr_invalid(next_sect))
      return -120;

    while (anz) {
      max_sec_rd = (SF_SECTOR_PH - pdesc->_sadr_rel);
      if (max_sec_rd > (SF_SECTOR_PH - HEADER_SIZE_B))
        return -129;

      if (pdesc->file_len != 0xFFFFFFFF) {
        h = pdesc->file_len - pdesc->file_pos;
        if (anz > h)
          anz = h;

      } else if (next_sect == 0xFFFFFFFF) {
        uc_mlen = sflash_find_mlen(pdesc->_wrk_sadr + pdesc->_sadr_rel, max_sec_rd);
        pdesc->file_len = pdesc->file_pos + uc_mlen; // Now we know the End
        if (anz > (uint32_t)uc_mlen)
          anz = uc_mlen;
      }

#ifdef SF_RD_TRANSFER_LIMIT // If defined: limit blocksize of single transfer (e.g. spi-driver buffer limits)
      if (pdest && max_sec_rd > SF_RD_TRANSFER_LIMIT)
        max_sec_rd = SF_RD_TRANSFER_LIMIT;
#endif

      if ((uint32_t)max_sec_rd > anz)
        max_sec_rd = anz;
      if (pdest) {
        sflash_read(pdesc->_wrk_sadr + pdesc->_sadr_rel, pdest, max_sec_rd);
        if (pdesc->open_flags & SF_OPEN_CRC)
          pdesc->file_crc32 = fs_track_crc32(pdest, max_sec_rd, pdesc->file_crc32);
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
        }
        break;
      }
    }
  }
  return total_rd; // max 2GB
}

/* Rewind File to Start */
int16_t fs_rewind(FS_DESC *pdesc) {
  if (!pdesc->_head_sadr)
    return -117;
  if (pdesc->open_flags & SF_OPEN_WRITE)
    return -118;
  pdesc->_wrk_sadr = pdesc->_head_sadr;
  pdesc->file_pos = 0;
  pdesc->_sadr_rel = HEADER_SIZE_B + FINFO_SIZE_B;
  pdesc->file_crc32 = 0xFFFFFFFF; // Reset CRC
  return 0;
}

/* Open File, if flag OPEN_CREATE is set, it will be generated, if already exists, it will be deleted
 * Flag OPEN_RAW will  not delete existing files, even if OPEN_CREATE is set */
int16_t fs_open(FS_DESC *pdesc, char *pname, uint8_t flags) {
  int16_t res;
  uint16_t i;
  uint32_t sadr = 0;
  uint32_t sfun_adr = 0;

  if (sflash_info.state_flags & STATE_DEEPSLEEP_OR_POWERFAIL)
    return -148;
  pdesc->_head_sadr = 0;
  pdesc->file_crc32 = 0xFFFFFFFF;
  if (sflash_info.creation_date == 0xFFFFFFFF)
    return -108; // Disk not formatted
  if (!*pname || fs_strlen(pname) > FNAMELEN)
    return -110;

  for (i = 0; i < sflash_info.files_used; i++) {
    sflash_read(HEADER_SIZE_B + i * 4, (uint8_t *)&sadr, 4);
    sflash_read(sadr, (uint8_t *)&sflash_info.databuf, HEADER_SIZE_B + FINFO_SIZE_B);
    if (sflash_info.databuf.u32[0] == SECTOR_MAGIC_HEAD_DELETED) {
      sfun_adr = sadr;
    } else if (sflash_info.databuf.u32[0] != SECTOR_MAGIC_HEAD_ACTIVE)
      return -114;
    else if (!fs_strcmp(pname, (char *)&sflash_info.databuf.u8[HEADER_SIZE_B + 12])) {
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
    if (flags & (SF_OPEN_READ | SF_OPEN_RAW)) {
      pdesc->file_len = sflash_info.databuf.u32[HEADER_SIZE_L + 0]; // informative data (only for existing files)
      if (pdesc->file_len == 0xFFFFFFFF)
        pdesc->open_flags |= SF_XOPEN_UNCLOSED;
      pdesc->open_flags |= (sflash_info.databuf.u8[HEADER_SIZE_B + 34] & (SF_OPEN_EXT_SYNC | _SF_OPEN_RES));
      pdesc->file_ctime = sflash_info.databuf.u32[HEADER_SIZE_L + 2]; // get file creation time
      return 0;
    }
    res = flash_set2delete(sadr);
    if (res)
      return res;
    sfun_adr = sadr;
  } else {
    if (!(flags & SF_OPEN_CREATE))
      return -124;
  }

  if (!sfun_adr) {
    sfun_adr = sflash_get_free_sector();
    if (!sfun_adr)
      return -113;
    if (HEADER_SIZE_B + sflash_info.files_used * 4 >= (SF_SECTOR_PH - 4))
      return -111;
    res = sflash_SectorWrite(HEADER_SIZE_B + sflash_info.files_used * 4, (uint8_t *)&sfun_adr, 4);
    if (res)
      return res;
    sflash_info.available_disk_size -= SF_SECTOR_PH;
    sflash_info.files_used++;
  } else {
    res = sflash_SectorErase(sfun_adr);
    if (res)
      return res;
  }

  fs_memset((uint8_t *)&sflash_info.databuf, 0xFF, HEADER_SIZE_B + FINFO_SIZE_B);
  sflash_info.databuf.u32[0] = SECTOR_MAGIC_HEAD_ACTIVE;
  fs_strncpy((char *)&sflash_info.databuf.u8[HEADER_SIZE_B + 12], pname, FNAMELEN);
  pdesc->file_ctime = fs_get_secs();
  sflash_info.databuf.u32[HEADER_SIZE_L + 2] = pdesc->file_ctime;
  sflash_info.databuf.u8[HEADER_SIZE_B + 34] = flags;
  res = sflash_SectorWrite(sfun_adr, (uint8_t *)&sflash_info.databuf, HEADER_SIZE_B + FINFO_SIZE_B);
  if (res)
    return res;

  pdesc->_head_sadr = sfun_adr;
  pdesc->_wrk_sadr = sfun_adr;

  sflash_info.files_active++;
  return 0;
}

/* Write to File */
int16_t fs_write(FS_DESC *pdesc, uint8_t *pdata, uint32_t len) {
  int16_t res;
  uint32_t maxwrite;
  uint32_t wlen;
  uint32_t newsect;

  if (sflash_info.state_flags & STATE_DEEPSLEEP_OR_POWERFAIL)
    return -148;
  if (!pdesc->_head_sadr)
    return -117;
  if (pdesc->open_flags & SF_OPEN_RAW) {
    if (pdesc->file_pos != pdesc->file_len)
      return -130;
  } else if (!(pdesc->open_flags & SF_OPEN_WRITE))
    return -118;

  while (len) {
    maxwrite = SF_SECTOR_PH - pdesc->_sadr_rel;
    if (maxwrite > SF_SECTOR_PH)
      return -112;
    if (!maxwrite) {
      newsect = sflash_get_free_sector();
      if (!newsect)
        return -113;

      res = sflash_SectorWrite(pdesc->_wrk_sadr + 8, (uint8_t *)&newsect, 4);
      if (res)
        return res;

      pdesc->_wrk_sadr = newsect;
      pdesc->_sadr_rel = HEADER_SIZE_B;
      maxwrite = SF_SECTOR_PH - HEADER_SIZE_B;
      sflash_info.databuf.u32[0] = SECTOR_MAGIC_DATA;
      sflash_info.databuf.u32[1] = pdesc->_head_sadr;
      res = sflash_SectorWrite(pdesc->_wrk_sadr, (uint8_t *)&sflash_info.databuf, 8);
      if (res)
        return res;
      sflash_info.available_disk_size -= SF_SECTOR_PH;
    }

    wlen = len;
    if (wlen > maxwrite)
      wlen = maxwrite;
    if (pdesc->open_flags & SF_OPEN_CRC)
      pdesc->file_crc32 = fs_track_crc32(pdata, wlen, pdesc->file_crc32);
    res = sflash_SectorWrite(pdesc->_wrk_sadr + pdesc->_sadr_rel, pdata, wlen);
    if (res)
      return res;
    len -= wlen;
    pdata += wlen;
    pdesc->_sadr_rel += wlen;
    pdesc->file_pos += wlen;
    if (pdesc->file_len != 0xFFFFFFFF)
      pdesc->file_len = pdesc->file_pos;
  }
  return 0;
}

int16_t fs_close(FS_DESC *pdesc) {
  int16_t res;
  uint32_t s0adr;
  uint32_t hinfo[2];

  if (sflash_info.state_flags & STATE_DEEPSLEEP_OR_POWERFAIL)
    return -148;
  if (!pdesc->_head_sadr)
    return -117;
  s0adr = pdesc->_head_sadr;
  pdesc->_head_sadr = 0; // Invalidate descriptor
  if (pdesc->open_flags & SF_OPEN_WRITE) {
    if (sflash_sadr_invalid(s0adr))
      return -120;
    hinfo[0] = pdesc->file_pos;
    hinfo[1] = pdesc->file_crc32;
    res = sflash_SectorWrite(s0adr + HEADER_SIZE_B + 0, (uint8_t *)hinfo, 8);
    if (res)
      return res;
  }
  return 0; // OK
}

uint32_t fs_get_crc32(FS_DESC *pdesc) {
  uint32_t rd_crc;

  if (sflash_info.state_flags & STATE_DEEPSLEEP_OR_POWERFAIL)
    return 0; // Be. values not possible
  if (!pdesc->_head_sadr)
    return 0;
  sflash_read(pdesc->_head_sadr + HEADER_SIZE_B + 4, (uint8_t *)&rd_crc, 4);
  return rd_crc;
}

int16_t fs_delete(FS_DESC *pdesc) {
  int16_t res;

  if (sflash_info.state_flags & STATE_DEEPSLEEP_OR_POWERFAIL)
    return -148;
  if (!pdesc->_head_sadr)
    return -117;
  if (pdesc->open_flags & SF_OPEN_WRITE)
    return -125;
  res = flash_set2delete(pdesc->_head_sadr);
  if (res)
    return res;
  pdesc->_head_sadr = (uint32_t)0; // No Close!
  return 0;
}

/* Rename a File. Can also be used to change the File's Management Flags (e.g Hidden, Sync), but not CRC LEN or DATE
 * But always a new name must be used, see docu */
int16_t fs_rename(FS_DESC *pd_odesc, FS_DESC *pd_ndesc) {
  uint16_t mlen;
  uint32_t thdr[6];
  int16_t res;

  if (sflash_info.state_flags & STATE_DEEPSLEEP_OR_POWERFAIL)
    return -148;
  if (!pd_odesc->_head_sadr || !pd_ndesc->_head_sadr)
    return -135;
  if (pd_ndesc->open_flags & (SF_OPEN_READ | SF_OPEN_RAW))
    return -133;
  if (pd_ndesc->file_len)
    return -134;

  if (pd_odesc->file_len == 0xFFFFFFFF)
    mlen = sflash_find_mlen(pd_odesc->_head_sadr + HEADER_SIZE_B + FINFO_SIZE_B, SF_SECTOR_PH - HEADER_SIZE_B - FINFO_SIZE_B);
  else if (pd_odesc->file_len > SF_SECTOR_PH - HEADER_SIZE_B - FINFO_SIZE_B)
    mlen = SF_SECTOR_PH - HEADER_SIZE_B - FINFO_SIZE_B;
  else
    mlen = pd_odesc->file_len;

  thdr[0] = SECTOR_MAGIC_HEAD_ACTIVE;
  thdr[1] = 0xFFFFFFFF;
  sflash_read(pd_odesc->_head_sadr + 8, (uint8_t *)&thdr[2], 16); // Nx Ln CRC Dt

  res = flash_intrasec_copy(pd_odesc->_head_sadr + HEADER_SIZE_B + FINFO_SIZE_B, pd_ndesc->_head_sadr + HEADER_SIZE_B + FINFO_SIZE_B, mlen); // S D
  if (res)
    return res;
  res = sflash_SectorErase(pd_odesc->_head_sadr);
  if (res)
    return res;
  res = flash_intrasec_copy(pd_ndesc->_head_sadr + HEADER_SIZE_B + 12, pd_odesc->_head_sadr + HEADER_SIZE_B + 12, mlen + FINFO_SIZE_B - 12); // S D
  if (res)
    return res;

  res = sflash_SectorWrite(pd_odesc->_head_sadr, (uint8_t *)thdr, 24);
  if (res)
    return res;

  pd_ndesc->open_flags = 0;
  res = fs_delete(pd_ndesc);
  if (res)
    return res;

  pd_ndesc->_head_sadr = 0;
  return 0;
}

int16_t fs_info(FS_STAT *pstat, uint16_t fno) {
  uint32_t sadr, idx_adr;
  int16_t ret;

  if (sflash_info.state_flags & STATE_DEEPSLEEP_OR_POWERFAIL)
    return -148;
  idx_adr = HEADER_SIZE_B + fno * 4;
  if (idx_adr > SF_SECTOR_PH - 4)
    return FS_STAT_INDEX;
  sflash_read(idx_adr, (uint8_t *)&sadr, 4); // Read Sector, where Index(fno) points to
  if (sadr == 0xFFFFFFFF)  return 0;  // This Index-Entry is unused
  if( !sadr) return -143; // Index is taboo!
  if (sadr >= sflash_info.total_flash_size) // Points to outside? Severe Error
    return -115;

  sflash_read(sadr, (uint8_t *)&sflash_info.databuf, HEADER_SIZE_B + FINFO_SIZE_B);

  // Each Index-Entry points to a HEAD: Either ACTIVE or DELETED. All other is an Error
  switch (sflash_info.databuf.u32[0]) {
  case SECTOR_MAGIC_HEAD_ACTIVE:
    ret = FS_STAT_ACTIVE;
    break;
  case SECTOR_MAGIC_HEAD_DELETED:
    ret = FS_STAT_INACTIVE;
    break;

    // Entry points to unidentified Head. Possible Reason: 
    // PowerLoss on fs_open() for Write/Create or on nRF52: J-TAG (which can access the serial flash!)
  case 0xFFFFFFFF:
    return -126; 
  default:
    return -142; 
  }

  pstat->_head_sadr = sadr;
  fs_strncpy(pstat->fname, (char *)&sflash_info.databuf.u8[HEADER_SIZE_B + 12], FNAMELEN);
  pstat->file_crc32 = sflash_info.databuf.u32[HEADER_SIZE_L + 1];
  pstat->file_ctime = sflash_info.databuf.u32[HEADER_SIZE_L + 2];
  pstat->disk_flags = sflash_info.databuf.u8[HEADER_SIZE_B + 34];
  sadr = sflash_info.databuf.u32[HEADER_SIZE_L];
  if (sadr == 0xFFFFFFFF) { // Unclosed File
    ret |= FS_STAT_UNCLOSED;
    pstat->disk_flags |= SF_XOPEN_UNCLOSED; // informativ
  }
  pstat->file_len = sadr;
  return ret;
}

/* Careful Disk Check
 * Returns:  0: No error <0: Critical Error, See JesFS  >0: Non-Critical Errors
 * If cb_printf() <> NULL: Output Diagnostics, pline[line_size) is a temp bffer */
int16_t fs_check_disk(void cb_printf(char *fmt, ...), uint8_t *pline, uint32_t line_size) {
  int16_t res;
  uint16_t i;
  int32_t lres;
  uint32_t aval;
  int16_t err = 0;

  // Might consume some stack space:
  FS_STAT lfs_stat;
  FS_DESC lfs_desc;

  if (cb_printf)
    cb_printf("Check Disk...\n");

  res = fs_start(FS_START_NORMAL);
  if (res) {
    if (cb_printf)
      cb_printf("ERROR: Disc Error:%d\n", res); // -107 .. -109
    if(res == -147) return -147; // Power Loss!
    err++;
  }
#ifdef JSTAT
  if (sflash_info.sectors_unknown) {
    if (cb_printf)
      cb_printf("ERROR: Unknown Sectors: %d\n", sflash_info.sectors_unknown);
    err++;
  }
#endif

  for (i = 0;; i++) {
    res = fs_info(&lfs_stat, i);

    if (i >= sflash_info.files_used) { 
      if (res == FS_STAT_INDEX)
        break;
      if (!res)
        continue; // Check unused entries also
    }
    if (cb_printf) if(res>0) {
      if(res&FS_STAT_INACTIVE) cb_printf("Check Index(%u): ('%s')\n", i,lfs_stat.fname);
      else cb_printf("Check Index(%u): '%s'\n", i,lfs_stat.fname);
    }
  

    if (res < 0 || (res & ~(FS_STAT_ACTIVE | FS_STAT_INACTIVE | FS_STAT_UNCLOSED))) {
      if (cb_printf)
        cb_printf("ERROR: Index(%u):%d\n", i, res);
      err++;
    }

    if (res & FS_STAT_ACTIVE) {
      if (res & FS_STAT_UNCLOSED) {
        res = fs_open(&lfs_desc, lfs_stat.fname, SF_OPEN_READ | SF_OPEN_RAW);
        if (res < 0) {
          if (cb_printf)
            cb_printf("ERROR: Open '%s':%d\n", lfs_stat.fname, res);
          err++;
        } else {
          if (lfs_stat.disk_flags & SF_OPEN_CRC) {
            // does not make sense
            if (cb_printf)
              cb_printf("ERROR: Unclosed File with CRC '%s'\n", lfs_stat.fname);
            err++;
          }
          lres = fs_read(&lfs_desc, NULL, 0xFFFFFFFF);
          if (lres < 0) {
            if (cb_printf)
              cb_printf("ERROR: Unclosed Read '%s':%d\n", lfs_stat.fname, lres);
            err++;
          }
        }
      } else if (lfs_stat.disk_flags & SF_OPEN_CRC) {
        res = fs_open(&lfs_desc, lfs_stat.fname, SF_OPEN_READ | SF_OPEN_CRC);
        if (res < 0) {
          if (cb_printf)
            cb_printf("ERROR: Open '%s':%d\n", lfs_stat.fname, res);
          err++;
        } else {
          aval = lfs_stat.file_len;
          if (aval > sflash_info.total_flash_size) {
            if (cb_printf)
              cb_printf("ERROR: Illegal File Size '%s':%u Bytes\n", lfs_stat.fname, aval);
            err++;
          } else {
            while (aval) {
              res = fs_read(&lfs_desc, pline, line_size);
              if (res < 0 || res > line_size || res > aval) {
                if (cb_printf)
                  cb_printf("ERROR: Read Data '%s':%d\n", lfs_stat.fname, res);
                err++;
                break;
              }
              aval -= res;
            }
            if (lfs_stat.file_crc32 != lfs_desc.file_crc32) {
              if (cb_printf)
                cb_printf("ERROR: CRC false '%s'\n", lfs_stat.fname);
              err++;
            }
          }
        }
      }
    }
  }
  if (cb_printf) {
    if (err)
      cb_printf("ERROR(s): %d\n", err);
    else
      cb_printf("Disk OK\n");
  }
  return err;
}

//------------------- HighLevel FS OK ------------------------

//----------------------------------------------- JESFS-End ----------------------