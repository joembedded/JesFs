# JesFs - Jo's Embedded Serial File System 
**for Standard (Serial) NOR-Flash**

Just think of very simple things like language data: on a “very small” 
Embedded Device (not something “big” like an Embedded Linux, but something 
that can run with small batteries for years): it is commonly integrated 
“somewhere in the code”.  
Difficult to change! But if the language data is in files, 
changes are easy. Same for graphics, setups, everything…
It even allows to change the firmware on the Embedded Device from many different sources!

Think of Embedded Devices, that even can get their latest firmware by themselves! E.g. over 
WiFi, Mobile Internet, Bluetooth, Uart, Radio-Link, …  
Suddenly all options are open! And if you have concerns about 
the security: don’t worry: problem already solved, as you’ll see later..

The main problem for “very small devices” – until now – was the “File System”: 
everybody knows “FAT”, “NTFS”, … but have you ever thought of a file system on a small chip? 
Or even inside of a CPU? No problem, with the right Software. This is why I wrote 

**“JesFs – Jo’s Embedded Serial File System“**

This first part covers only the Open Source part of JesFs. 
The JesFsBoot - Secured Bootloader will follow soon.

My daily work ist the IoT. Because I did not find any really practical solution, 
I decided to create my own one. “Robustness”, “Security” and “Small Footprint” 
were my design constraints.

JesFs was designed for use in the “Real World”. For use with standard Serial NOR-Flash memories, 
like the M25R-Series, used on TI’s CC1310-Launchpad , which is available up to 16 MB, or even more…

Some Basics about JesFs:

- Ultra-Small RAM and code footprint: can be used on the smallest MCUs with only 8kB program memory or less (like the famous MSP430-series, almost all kind of 32-Bit ARM cores (M0, M3, M4, ….)). Only 200 bytes of RAM are sufficient!
- Completely “Open Source” and written in Standard C.
- Works with Serial NOR-Flash from **8kB to 16MB** (opt. up to 2GB), but could also be used with CPU-internal NOR-Flash.
- Works hand-in-hand with the Ultra-Small JesFsBoot Secure bootloader (requires less than 8kB on standard ARM cores, including an AES-128 encryption engine for reliable Over-the-Air-Updates (“OTA”)).
- Includes optimised Wear Leveling (for maximum life of the memory).
- A special mode was added to allow millions of write cycles, especially for data collection event reports and journaling aplications.
- JesFs is quasi persistent: no data loss on power loss.
- Designed for (almost) all situations, where NOR memories could be used (the ones where only blocks can be deleted (0->1) and only 0 written.
- Strictly taylored to Ultra-Low-Power Embedded Systems.
- Designed to use the advantage of an underlying RTOS, but can also be used standalone (JesFs was originally developed on a CC1310 with TI-RTOS).
- Sample applications für the TI-Launchpad CC13xx/26xx and others (see [JesFs_Test.pdf](https://github.com/joembedded/JesFs/blob/master/JesFs_Test.pdf))
- Easy to use with an intuitive API:

```
  int16_t fs_open(FS_DESC *pdesc, char* pname, uint8_t flags);
  int32_t fs_read(FS_DESC *pdesc, uint8_t *pdest, uint32_t anz);
  int16_t fs_write(FS_DESC *pdesc, uint8_t *pdata, uint32_t len);
  int16_t fs_close(FS_DESC *pdesc);
  int16_t fs_delete(FS_DESC *pdesc);
  int16_t fs_rewind(FS_DESC *pdesc);
  int16_t fs_rename(FS_DESC *pd_odesc, FS_DESC *pd_ndesc);
  uint32_t fs_get_crc32(FS_DESC *pdesc);

  int16_t fs_format(uint32_t f_id);
  int16_t fs_start(uint8_t mode);
  void fs_deepsleep(void);

  int16_t fs_info(FS_STAT *pstat, uint16_t fno);
  ```

