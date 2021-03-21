/*******************************************************************************
* JesFs_main.C: JesFs Demo/Test
*
* JesFs - Jo's Embedded Serial File System
*
* Demo how to use JesFs on
* - TI CC13xx/CC26xx Launchpad
* - Nordic nRF52840 DK_PCA10056 (nRF52)
* - Nordic nRF52832 DK_PCA10040 (nRF52)
* - Windows (Compilers: "Embarcadero(R) C++ Builder Community Edition" (for PC)
*           and "Microsoft Visual Studio Community 2019")
*
* Can be used as standalone project or,
* in combination with secure JesFsBoot Bootloader
*
* Docu in 'JesFs.pdf'
*
* (C) joembedded@gmail.com - www.joembedded.de
* Version: 
* 1.5: 25.11.2019 
* 1.51: 07.12.2019 (nRF52) added deep sleep functions in Toolbox (nrF52840<3uA) (see cmd 's')
* 1.6: 22.12.2019  added fs_check_disk() for detailed checks
* 1.61: 05.01.2020 source cosmetics and (nRF52) SPIM 16MHz as default
* 1.62: 19.01.2020 Changed WD behavior in tb_tools
* 1.7: 25.02.2020 (nRF52) Added Defines for u-Blox NINA-B3 
* 1.8: 20.03.2020 Added Time set with '!' and UART-RX-Error
* 2.0: 06.09.2020 (nRF52) Changed UART Driver to APP_UART  for Multi-Use in tb_tools
* 2.01: 08.09.2020 (nRF52) Fixed Error in SDK17 (see tb_tools_nrf52.c-> 'SDK17')
* 2.02: 23.09.2020 (nRF52) Adapted to SDK17.0.2 (still Problem in 'nrf_drv_clock.c' -> see 'SDK17')
* 2.03: 22.11.2020 Corrected small error in JesFs_main.c 'r' command.
* 2.04: 11.02.2021 Corrected small error in JesFs_main.c 'X' command.
* 2.05: 12.03.2021 Added print of Flash ID for analysis 
* 2.06: 21.03.2021 Added hint about 'Bulk Erase'/Soft format (see case 'F'))
*******************************************************************************/

#define VERSION "2.06 / 21.03.2021"

#ifdef WIN32		// Visual Studio Code defines WIN32
 #define _CRT_SECURE_NO_WARNINGS	// VS somtimes complains traditional C
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


//======== Platform headers ==========
#ifdef PLATFORM_NRF52    // Define this Macro in ProjectOptions - PredefinedSymbols
  #include "boards.h"    // Settings div.

  #define GID 1   // Guard-ID

#endif

#ifdef CC13XX_CC26XX    // Define this Macro in ProjectOptions - PredefinedSymbols
	#define main     mainThread  // main_start is a Task
#endif

#ifdef WIN32		// Visual Studio Code defines WIN32
	#define __WIN32__	// Embarcadero defines __WIN32__
#endif
#ifdef __WIN32__
	extern int16_t ll_read_vdisk(char* fname);	// Helpers to write/read virtual Disk to fFile on PC
	extern int16_t ll_setid_vdisk(uint32_t id);
	extern int16_t ll_get_info_vdisk(uint32_t * pid_used, uint8_t * *pmem, uint32_t * psize);
#endif

//======= Toolbox =======
#include "tb_tools.h"

//===== JesFs =====
#include "jesfs.h"

//====================== Globals ===============
#define MAX_INPUT 80
char input[MAX_INPUT+1];  // 0 at end

#define SBUF_SIZE 10000  // Big buffer for BULK write test (Remark for CCxxy0: only 20k available)
uint8_t sbuffer[SBUF_SIZE]; // Test buffer

//==== JesFs globals =====
// two global descriptors and helpers (global to save RAM memory and allow reuse)
// (JesFs was originally desigend for very small CPUs)
FS_DESC fs_desc, fs_desc_b; // _b: A second descriptor for rename etc..
FS_STAT fs_stat;
FS_DATE fs_date;  // Structe holding date-time


//=========== Helper Functions ===============
//=== Platform specific ===
uint32_t _time_get(void) {
	return tb_time_get();
}

//=== common helpers ===
// Helper Function for readable timestamps
void conv_secs_to_date_sbuffer(uint32_t secs) {
    fs_sec1970_to_date(secs,&fs_date);
    sprintf((char*)sbuffer,"%02u.%02u.%04u %02u:%02u:%02u",fs_date.d,fs_date.m,fs_date.a,fs_date.h,fs_date.min,fs_date.sec);
}
// Convert a String "day.month.year4 hr:min[.sec]" to unix-seconds  // Return 0 on error
uint32_t conv_tstr_to_secs(char* pc){
  memset(&fs_date,0,sizeof(fs_date));
  fs_date.d=(uint8_t)strtoul(pc,&pc,0);
  if(*pc++!='.') return 0;
  fs_date.m=(uint8_t)strtoul(pc,&pc,0);
  if(*pc++!='.') return 0;
  fs_date.a=(uint16_t)strtoul(pc,&pc,0);
  if(*pc++!=' ') return 0;
  fs_date.h=(uint8_t)strtoul(pc,&pc,0);
  if(*pc++!=':') return 0;
  fs_date.min=(uint8_t)strtoul(pc,&pc,0);
  if(*pc++==':') fs_date.sec=(uint8_t)strtoul(pc,0,0);  // Seconds optional
  return fs_date2sec1970(&fs_date);
}

//========= MAIN ====================
int main(void) { // renamed to mainThread() on CCxxyy
    char *pc;
    uint32_t uval;
    int32_t res;
    int32_t i,h;
    int32_t anz;
    uint32_t t0;
    uint32_t asecs;
    uint8_t open_flag;

#ifdef PLATFORM_NRF52    // Find out why restared
    uval = (NRF_POWER->RESETREAS);
    (NRF_POWER->RESETREAS)=0xFFFFFFFF;  // Clear with '1'
#endif

    tb_init(); // Init the Toolbox (without Watchdog)
    tb_watchdog_init();   // Watchdog separate init

    tb_printf("\n*** JesFs *Demo* " VERSION " (C)2020 JoEmbedded.de\n\n");

#ifdef PLATFORM_NRF52    // Find out why restared
    tb_printf("Reset-Reason: 0x%X ",uval);
    if(uval&1) tb_printf("(Pin-Reset)");  // The low Nibble Reasons
    if(uval&2) tb_printf("(Watchdog)");
    if(uval&4) tb_printf("(Soft-Reset)");
    if(uval&8) tb_printf("(CPU Lockup)");
    tb_printf(" Bootcode: 0x%x\n",tb_get_bootcode(true));
    GUARD(GID); // GUARD: Save THIS line as last visited line in Module GID
#endif

    res=fs_start(FS_START_NORMAL);
    tb_printf("Filesystem Init:%d\n",res);
    tb_printf("Disk size: %d Bytes\n",  sflash_info.total_flash_size);
	tb_printf("Flash ID: %x\n",sflash_info.identification); // For Analysis, see 'jesfs.h' for definitions

    for(;;) {
        tb_board_led_on(0);

        tb_time_get();      // Dummy call to update Unix-Timer
        tb_printf("> ");   // Show prompt
        res=tb_gets(input,MAX_INPUT, 60000, 1);   // 60 seconds time with echo
        tb_putc('\n');
        tb_watchdog_feed(1);  // Now 250 secs time
        tb_board_led_off(0);

#ifdef PLATFORM_NRF52   // Save Software Position
    GUARD(GID); 
#endif
        if(res<0){
          tb_printf("ERROR: UART-RX\n");   // Show prompt
          tb_uninit();
          tb_delay_ms(1000);
          tb_init();
        }else if(res>0) { // ignore empty lines
            pc=&input[1];             // point to 1.st argument
            while(*pc==' ') pc++;     // Remove Whitspaces from Input
            uval=strtoul(pc,NULL,0);  // Or 0

            t0=tb_get_ticks();
            switch(input[0]) {

            case 's':	// V1.51: With deep sleep, onn nRF52840: <3uA
                tb_printf("'s' Flash DeepSleep and CPU sleep %d secs...\n",uval);
                res=fs_start(FS_START_RESTART);   // Restart Flash to be sure it is awake, else it can not be sent to sleep..
                if(res) tb_printf("(FS_start(FS_RESTART) return ERROR: Res:%d)\n",res);
                fs_deepsleep(); // ...because if Flash is already sleeping, this will wake it up!, now SPI closed, Flash in Deep-Sleep
                tb_delay_ms(10);  // Wait for Text Out
                tb_uninit();	//  Disable toolbox-HighPower peripherals (e.g. UART)
                tb_delay_ms(uval*1000);
                tb_init();      //  Re-Enable again
                // no 'break', wake Filesystem fall-through

            case 'i':
                res=fs_start(FS_START_FAST);  // Wake (an enable Flash)
                tb_printf("'i' Filesystem Init Fast: Res:%d\n",res);
                if(!res) break; // All OK

            case 'I':
                res=fs_start(FS_START_NORMAL);
                tb_printf("'I' Filesystem Init Normal: Res: %d\n",res);
                break;

            case 'S':
                tb_printf("'S' Only and CPU sleep %d secs (Wake Sflash with 'i'/'I')...\n",uval);
                tb_delay_ms(uval*1000);
                break;

            case 'F':
                // Warning: F1 ('Bulk Erase') might need VERY long on some (larger) Chips (> 240 secs, 
                // which is Default Timeout). Better to use F2 (which erases only non-empty 4k sectors).
                // F1: Hardware Chip Erase ('Bulk Erase') F2: Soft Erase :2 (faster if not full)
                i=atoi(pc); 
                pc="???";
                if(i==1) pc="Chip Erase";
                else if(i==2) pc="Soft Erase";
                tb_printf("'F' Format Serial Flash (Mode:%d(%s)) (may take up to 240 secs!)...\n",i,pc);
                tb_printf("FS format: Res:%d\n",fs_format(i));
                break;

            case 'o':
                // Open an existing file and set file position to first byte or create it, if not existing.
                // This file can not be closed, but appended if file position points behind the last existing byte
                // of the file
                tb_printf("'o' Open File for Reading or Raw '%s' (incl. Writing)\n",pc);
                open_flag=SF_OPEN_READ |SF_OPEN_RAW|SF_OPEN_CRC; // flags:  Read only (Raw required for Delete) with CRC
                if(*pc=='~'){ open_flag &= ~SF_OPEN_CRC; pc++; tb_printf("CRC disabled\n");} // ~FILENAME = no CRC
                res=fs_open(&fs_desc,pc, open_flag );
                tb_printf("Res:%d, Len:%d\n",res,fs_desc.file_len); // (Len: if already exists)
                break;

            case 'O':
                // Open the file for writing and create it, if not existing. If existing: truncate
                tb_printf("'O' Open File for Writing '%s'\n",pc);
                open_flag=SF_OPEN_CREATE|SF_OPEN_WRITE |SF_OPEN_CRC; // flags: Create if not there, in any case: open for writing  with CRC
                if(*pc=='~'){ open_flag &= ~SF_OPEN_CRC; pc++; tb_printf("CRC disabled\n");} // ~FILENAME = no CRC
                res=fs_open(&fs_desc,pc,open_flag);
                tb_printf("Res:%d\n",res);
                break;

            case 'c':
                // Write CRC/Len if file was opened for writing, RAW files can not be closed
                // In any case: invalidate the descriptor
                tb_printf("'c' Close File, Res:%d\n",fs_close(&fs_desc));
                break;

            case 'd':
                tb_printf("'d' Delete (RAW opened) File, Res:%d\n",fs_delete(&fs_desc));
                break;


            case 'v': // Listing on virtual Disk - Only basic infos. No checks, Version with **File Health-Check** for all files with CRC in JesFs_cmd.c
                tb_printf("'v' Directory:\n");
                tb_printf("Disk size: %d Bytes\n",   sflash_info.total_flash_size);
                if(sflash_info.creation_date==0xFFFFFFFF) { // Severe Error
                    tb_printf("Error: Invalid/Unformated Disk!\n");
                    break;
                }
                tb_printf("Disk available: %d Bytes / %d Sectors\n",sflash_info.available_disk_size,sflash_info.available_disk_size/SF_SECTOR_PH);
                conv_secs_to_date_sbuffer(sflash_info.creation_date);
                tb_printf("Disk formated [%s]\n",sbuffer);
                for(i=0; i<sflash_info.files_used+1; i++) { // Mit Testreserve
                    res=fs_info(&fs_stat,i);

                    if(res<=0) break;
                    if(res&FS_STAT_INACTIVE) tb_printf("(- '%s'   (deleted))\n",fs_stat.fname); // Inaktive/Deleted
                    else if(res&FS_STAT_ACTIVE) {
                        tb_printf("- '%s'   ",fs_stat.fname); // Active
                        if(res&FS_STAT_UNCLOSED) {
                            fs_open(&fs_desc,fs_stat.fname,SF_OPEN_READ|SF_OPEN_RAW); // Find out len by DummyRead
                            fs_read(&fs_desc, NULL , 0xFFFFFFFF);   // Read as much as possible
                            fs_close(&fs_desc); // Prevent descriptor from Reuse
                            tb_printf("(Unclosed: %u Bytes)",fs_desc.file_len);
                        } else {
                            tb_printf("%u Bytes",fs_stat.file_len);
                        }
                        // The creation Flags
                        if(fs_stat.disk_flags & SF_OPEN_CRC) tb_printf(" CRC32:%x",fs_stat.file_crc32);
                        if(fs_stat.disk_flags & SF_OPEN_EXT_SYNC) tb_printf(" ExtSync");
                        //if(fs_stat.disk_flags & _SF_OPEN_RES) tb_printf(" Reserved");
                        conv_secs_to_date_sbuffer(fs_stat.file_ctime);
                        tb_printf(" [%s]\n",sbuffer);
                    }
                }
                tb_printf("Disk Nr. of files active: %d\n",sflash_info.files_active);
                tb_printf("Disk Nr. of files used: %d\n",sflash_info.files_used);
#ifdef JSTAT
                if(sflash_info.sectors_unknown) tb_printf("WARNING - Found %d Unknown Sectors\n",sflash_info.sectors_unknown);
#endif
                tb_printf("Res:%d\n",res);
                break;

            case 'V': // Run Careful Disk Check with opt. Output, requires a temp. buffer
                fs_check_disk(tb_printf,sbuffer,SBUF_SIZE);
                break;


            case 'W':
                // Write a number of BULK Data...
                // The theoretical maximum speed of the Mx25R6435F is typically 80kB/sec (due to page programming)
                // My measures (nRF52) show ca. 60-70kB/sec on an empty Flash (including computed CRC32), so writing speed is quite OK for JesFs
                // If old sectors must be erased before writing, speed goes down to ca. 30-40kB/sec
                tb_printf("'W' BULK Write %d Bytes to file in Junks of %d\n",uval,SBUF_SIZE);
                for(i=0; i<SBUF_SIZE; i++) {
                    sbuffer[i]=' '+i%93;
                }
                anz=uval;
                while(anz>0) {
                    uval=anz;
                    if(uval>SBUF_SIZE) uval=SBUF_SIZE;
                    res=fs_write(&fs_desc,sbuffer,uval);
                    tb_printf("Write %u/%u Bytes - Res:%d\n",uval,anz,res);
                    if(res<0) break;
                    anz-=uval;
                }
                break;

            case 'w': // Write Text Data
                tb_printf("'w' Write '%s' to file\n",pc);
                res=fs_write(&fs_desc,(uint8_t*)pc,strlen(pc));
                tb_printf("Res:%d\n",res);
                break;

            case 'r':
                tb_printf("'r' Read (max.) %d Bytes from File:\n",uval);
				anz = uval;	
                // Read the complete sbuffer, but show only max. first 60 bytes
                if(anz<(int)sizeof(sbuffer)) {
                    res=fs_read(&fs_desc, sbuffer , anz);
                    // Show max. the first 60 Bytes...
                    if(res>0) {
                        for(i=0; i<res; i++) {
                            tb_printf("%c",sbuffer[i]);
                            if(i>60) {
                                tb_printf("...");
                                break; // Don't show all
                            }
                        }
                        tb_printf("\n");
                    }
                } else {
                    // This is e.g. tp place the file position to find the end of the file
                    // Silent read is VERY fast, my measures show ca. 120-180 MB/sec (nRF52)
                    // Warning: silent read will not update computed CRC32
                    tb_printf("Read %d silent...\n",anz);
                    res=fs_read(&fs_desc, NULL , anz);
                }
                tb_printf("Read %d Bytes: Res:%d\n",anz,res);
                break;

            case 'R':
                // Read BULK data
                // Reading Files with computed CRC32 is about 550-600 kB/sec (nRF52) and
                // without computed CRC ca. 3.75MB/sec what is very close to the 
                // theoretical absolute maximum limit of 4MB/sec
                tb_printf("'R' BULK Read %d Bytes from file in Junks of %d\n",uval,SBUF_SIZE);
                h=0;
                anz=uval;
                while(anz>0) {
                    uval=anz;
                    if(uval>SBUF_SIZE) uval=SBUF_SIZE;
                    res=fs_read(&fs_desc,sbuffer,uval);
                    if(res<=0) break; // 0: Nothing more
                    anz-=uval;
                    h+=res;
                }
                tb_printf("Read %u Bytes - Res:%d\n",h,res);

                break;

            case 'e':
                // Rewind file position
                res=fs_rewind(&fs_desc);
                tb_printf("'e' Rewind File - Res:%d\n",res);
                break;

            case 't':
                // Show file position an disk len (not for unlcosed files) and the computed CRC32
                tb_printf("'t' Test/Info: file_pos:%d  file_len:%d CRC32:%x\n",fs_desc.file_pos,fs_desc.file_len, fs_desc.file_crc32);
                break;

            case 'z':
                // Display the CRC of a file (shows 0xFFFFFFFF for unclosed files or if not enabled)
                // and the computed CRC32 (which is only updated for physical reads, not for silent reads)
                tb_printf("'z' CRC32: Disk:%x, Run:%x\n",fs_get_crc32(&fs_desc),fs_desc.file_crc32);
                break;

            case 'n': // Rename (Open) File to
                tb_printf("'n' Rename File to '%s'\n",pc); // rename also allows changing Flags (eg. Hidden or Sync)
                i=SF_OPEN_CREATE|SF_OPEN_WRITE;  // ensures new File is avail. and empty
                if(fs_desc.open_flags&SF_OPEN_CRC) i|= SF_OPEN_CRC; // Optionally Take CRC to new File
                res=fs_open(&fs_desc_b,pc,i); // Must be a new name (not the same)
                if(!res) res=fs_rename(&fs_desc,&fs_desc_b);
                tb_printf("Rename - Res:%d\n",res);
                break;


            case 'q':    // Quit: Exit / System Reset.
                tb_printf("'q' Exit in 1 sec...\n");
                tb_delay_ms(1000);
                tb_printf("Bye!\n");

                // Winddows exit()
                // TI-RTOS Up to (TI-RTOS+Debugger) what happens: Reset, Stall, Nirvana, .. See TI docu.
                // nRF52: Reset
                tb_system_reset();

            case 'X': // Watchdog Test
              tb_printf("Counting %d secs, Watchdog Test...\n",uval);
#ifdef PLATFORM_NRF52   // Save Software Position
    GUARD(GID); 
#endif
              for(i=0;i<uval;i++){
                tb_printf("(%u)",i);
                tb_delay_ms(1000);
              }
              break;

            case '!':   // Time Management - Set the embedded Timer (in unix-Seconds or dd.mm.yyyy hh:MM:ss )
                asecs=uval;
                if(asecs) {
                  if(asecs<32) asecs=conv_tstr_to_secs(pc); // might return 0
                  if(asecs) tb_time_set(asecs);
                }
                asecs=tb_time_get();  
                conv_secs_to_date_sbuffer(asecs);
                tb_printf("'!': Time: [%s] (%u secs)\n",sbuffer,asecs);
                break;

            /****************** TESTFUNCTION Development only********************************/
            case 'm':   // mADR Examine Mem Adr. in Hex!
                // Read 1 page of the serial flash (internal function)
                {
                extern void sflash_read(uint32_t sadr, uint8_t* sbuf, uint16_t len);
                int adr,j;
                adr=strtol(pc,0,16);  // Hex
                tb_printf("Disk Adr. 0x04%x:\n",adr);
                sflash_read(adr,sbuffer,256); // Read 1 page of the flash
                for(i=0; i<16; i++) {
                    tb_printf("%04X: ",adr+i*16);
                    for(j=0; j<16; j++) tb_printf("%02X ",sbuffer[i*16+j]);
                    for(j=0; j<16; j++) {
                        res=sbuffer[i*16+j];
                        if(res<' ' || res>127) res='.';
                        tb_printf("%c",res);
                    }
                    tb_printf("\n");
                }
            }
            break;

#ifdef __WIN32__
            // On Windows the virtual Disk/Flash (in RAM) can be saved and reloaded
            case '+':
                tb_printf("'+': Write VirtualDisk to File: '%s'\n",pc);
                res=ll_write_vdisk(pc);
                tb_printf("Res: %d\n",res);
                break;

            case '#':
                tb_printf("'#': Read VirtualDisk from File: '%s'\n",pc);
                res=ll_read_vdisk(pc);
                tb_printf("Res: %d\n",res);
                tb_printf("FS Init Normal:%d\n",fs_start(FS_START_NORMAL));
                break;

            case '$':
                // Manually select size of virtual Flash
                anz=strtoul(pc,NULL,0);
                tb_printf("'$': Set vdisk ID: $%x/%d\n",anz,anz);
                ll_setid_vdisk(anz);
                break;
#endif

            default:
                tb_printf("???\n");
                break;
            }

            // Measure runtime
            tb_printf("(Run: %u msec)\n",tb_deltaticks_to_ms(t0,tb_get_ticks()));
        }
    }
}

// **
