/*******************************************************************************
* JesFs_main.C: JesFs Demo/Test
*
* JesFs - Jo's Embedded Serial File System
*
* Demo how to use JesFs on CC131x Launchpad.
* Can be used as standalone project or, better, together with JesFsBoot.
* Replacement for empty.c in emty TI-RTOS Project
*
* (C)2019 joembedded@gmail.com - www.joembedded.de
* Version: 1.1 / 2.1.2019
* --------------------------------------------
* Please regard: This is Copyrighted Software!
* --------------------------------------------
*
* It may be used for education or non-commercial use, and without any warranty!
* For commercial use, please read the included docu and the file 'license.txt'
*******************************************************************************/

#include <stdint.h>
#include <stddef.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#ifdef __WIN32__

 /******** On PC use stdio **************/
 #include <windows.h>
 #include <time.h>
 #define sleep(x) Sleep(x*1000)
 #define dooze(x) Sleep(x*1000)
 #define uart_printf printf
 #define uart_putc putchar
 #define uart_open()
  int uart_gets(uint8_t* buf, int16_t max, uint16_t wt, uint8_t echo){
	gets((char*)buf);
	return strlen((char*)buf);
 }
 #define mainThread_init()
 #define mainThread main
 #define GPIO_toggle(x)  // No LEDs on PC
 #define SysCtrlSystemReset() exit(-1)  // Exit
 extern int16_t ll_write_vdisk(char *fname);   // PC LowLevel LL-Driver
 extern int16_t ll_read_vdisk(char *fname);   // PC LowLevel LL-Driver
 extern void ll_setid_vdisk(uint32_t id); // PC LowLevel LL-Driver
#else

/* TI-RTOS */
/* Bunch of Std. Headers */
#include <unistd.h> // for sleep/usleep

/* Driver Header files */
#include <ti/drivers/GPIO.h>
//#include <ti/drivers/I2C.h>
#include <ti/drivers/SPI.h>
#include <ti/drivers/UART.h>

// Driverlibs for Hardware access - Here: SysCtrlSystemReset() (see 'q')
#include <ti/devices/DeviceFamily.h>
#include DeviceFamily_constructPath(driverlib/sys_ctrl.h)

#include <ti/sysbios/hal/Seconds.h>

// Hint: If defined, Watchdog is activated (only required for use with the SECURED Bootloader,
// because it starts with Watchdog enabled. For Development: may be undefined.
// Wathdog is running during sleep!

#define USE_WATCHDOG  // <- Undefine here *** BOOTLOADER ENABLES WATCHDOG ***
#ifdef USE_WATCHDOG
 #include <ti/drivers/Watchdog.h>
#endif

/* Board Header file */
#include "Board.h"

/* Project Header Files */
#include "uart_xl.h"
#endif // __WIN32__

#include "JesFs.h"

/* FTEMRM: Embedded Driver for 2G/3G/WiFi/LocalRadio/... Not used here
#include "fterm.h"
*/

/* ----------------------- Globals ---------------------------- */
uint8_t sbuffer[256]; // Test buffer
FS_DESC fs_desc, fs_desc_b; // _b for rename etc..
FS_STAT fs_stat;
FS_DATE fs_date;


#ifndef __WIN32__
#ifdef USE_WATCHDOG
// Will blink with the Red LED.
#define WDINTV  1500000 // 1 sec on cc13xx

Watchdog_Handle watchdogHandle;
void watchdogCallback(uintptr_t _unused){
	Watchdog_clear(watchdogHandle);
	GPIO_toggle(Board_GPIO_RLED);
}
#endif

#ifndef __WIN32__ // External Time-Helper function on CC1310 use either BIOS or AON_RTC
int32_t _time_get(void){
    return Seconds_get();   // use BIOS here
}
#endif

/* === mainThread inits === */
void mainThread_init(void){
#ifdef USE_WATCHDOG
	Watchdog_Params params;
#endif

	/* Init what we need */
	GPIO_init(); // LEDs (and JesFs too)
	/* Configure the LED pin */
	GPIO_setConfig(Board_GPIO_RLED, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW); // RED
	// GPIO_setConfig(Board_GPIO_GLED, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_HIGH); // GREEN

#ifdef USE_WATCHDOG
	Watchdog_init();
	/* Create and enable a Watchdog with resets disabled */
	Watchdog_Params_init(&params);
	params.callbackFxn = (Watchdog_Callback)watchdogCallback;
	params.resetMode = Watchdog_RESET_ON;
	//params.debugStallMode = Watchdog_DEBUG_STALL_OFF;   // (Default (OK for Debug) is STALL_ON)
	watchdogHandle = Watchdog_open(Board_WATCHDOG0, &params); // Assert it works..
	Watchdog_setReload(watchdogHandle, WDINTV);
#endif

	UART_init(); // required for HW uart
	//I2C_init();
	SPI_init();  // required for JesFs
}

/* Do nothing for x secs, on Launchpad: Blink with LEDs */
void dooze(int32_t anz_sec){
	uart_close();
	while(anz_sec-->0){
		GPIO_write(Board_GPIO_RLED, 1);
		usleep(300);
		GPIO_write(Board_GPIO_RLED, 0);   // Lights out...
		sleep(1);
	}
	uart_open();
}
#endif

// Helper Function for good view
void conv_secs_to_date_sbuffer(uint32_t secs){
	fs_sec1970_to_date(secs,&fs_date);
	sprintf((char*)sbuffer,"%02u.%02u.%04u %02u:%02u:%02u",fs_date.d,fs_date.m,fs_date.a,fs_date.h,fs_date.min,fs_date.sec);
}


/*  ======== mainThread ======== */
#define MAX_INPUT 80
char input[MAX_INPUT+1];

void *mainThread(void *arg0){
	int32_t res;
	int32_t i;
	int32_t anz;
	char *pc;   // Points to argument
#ifndef __WIN32__
	uint32_t asecs;
#endif

	mainThread_init();

	uart_open();
	uart_printf("\n*** JesFs *Demo* V1.1 "__TIME__ " " __DATE__ " ***\n\n");
	uart_printf("(C)2019 joembedded@gmail.com - www.joembedded.de\n\n");

#ifdef USE_WATCHDOG
	uart_printf("Watchdog: ON\n");
#else
    uart_printf("Watchdog: OFF\n");
#endif

#ifdef CC1310_LAUNCHXL
    uart_printf("Board: CC1310_LAUNCHXL\n");
#elif defined(BTRACK_PCB)
    uart_printf("Board: BTRACK\n");
#elif defined(LTRAX_PCB)
    uart_printf("Board: LTRAX\n");
#elif defined(__WIN32__)
	uart_printf("Board: WIN32-PC\n");
#else
 #error "Unknown Board"
#endif

	uart_printf("Filesystem Init:%d\n",fs_start(FS_START_NORMAL));

#ifdef __WIN32__
 // #define USE_DEFAULT_DISK   // Enable for persitance
 #ifdef USE_DEFAULT_DISK
	// For tests: save a default-disk with some files...
	// There may be an old image available
	uart_printf("__WIN32__ Read VirtualDisk from File: 'default.disk'\n");
	res=ll_read_vdisk("default.disk",0);  // Erstmal unformatiert
	uart_printf("Res: %d, Init:%d\n",res, fs_start(FS_START_NORMAL));
 #endif

#endif

	while (1) {
		GPIO_toggle(Board_GPIO_RLED);
		uart_printf("> ");
		res=uart_gets(input,MAX_INPUT, 60000, 1);   // 60 seconds time with echo (on CC1310)
		uart_putc('\n');

		if(res>0){
			pc=&input[1];
			while(*pc==' ') pc++;   // Remove Whitspaces from Input
			switch(input[0]){

			case 's':
                anz=atoi(pc);
                uart_printf("'s' Flash DeepSleep and CPU sleep %d secs (Wake Sflash with 'i'/'I')...\n",anz);
                res=fs_start(FS_START_RESTART);   // Restart Flash to be sure it is awake, else it can not be sent to sleep..
                if(res) uart_printf("(FS_start(FS_RESTART):%d???)\n",res);
                fs_deepsleep(); // because if Flash is already sleeping, this will wake it up!
                dooze(anz);
                // no 'break', wake Filesystem

			case 'i':
				res=fs_start(FS_START_FAST);
				uart_printf("'i' Filesystem Init Fast:%d\n",res);
				if(!res) break; // All OK

			case 'I':
				res=fs_start(FS_START_NORMAL);
				uart_printf("'I' Filesystem Init Normal:%d\n",res);
				break;

			case 'S':
				anz=atoi(pc);
				uart_printf("'S' Only and CPU sleep %d secs (Wake Sflash with 'i'/'I')...\n",anz);
				dooze(anz);
				break;


			case 'F':
			    i=atoi(pc); // Full:1 Soft:2
			    uart_printf("'F' Format Serial Flash (Mode:%d) (may take up to 120 secs!)...\n",i);
			    uart_printf("FS format:%d\n",fs_format(i));

				break;

			case 'o':
				uart_printf("'o' Open File for Reading or Raw '%s'\n",pc);
				res=fs_open(&fs_desc,pc,SF_OPEN_READ |SF_OPEN_RAW|SF_OPEN_CRC );    // Read only (Raw required for Delete)
				uart_printf("res:%d, Len:%d\n",res,fs_desc.file_len); // (Len: if already exists)

				break;

			case 'O':
				uart_printf("'O' Open File for Writing '%s'\n",pc);
				res=fs_open(&fs_desc,pc,SF_OPEN_CREATE|SF_OPEN_WRITE |SF_OPEN_CRC);    // Create if not there, in any case: open for writing
				uart_printf("res:%d\n",res);
				break;

			case 'c':
				uart_printf("'c' Close File, Res:%d\n",fs_close(&fs_desc));
				break;

			case 'd':
				uart_printf("'d' Delete (RAW opened) File, Res:%d\n",fs_delete(&fs_desc));
				break;


			case 'v': // Listing on virtual Disk - Only basic infos. No checks, Version with **File Health-Check** for all files with CRC in JesFs_cmd.c
				uart_printf("'v' Directory:\n");
				uart_printf("Disk size: %d Bytes\n",   sflash_info.total_flash_size);
                if(sflash_info.creation_date==0xFFFFFFFF){  // Severe Error
                    uart_printf("Error: Invalid/Unformated Disk!\n");
                    break;
                }
				uart_printf("Disk available: %d Bytes / %d Sectors\n",sflash_info.available_disk_size,sflash_info.available_disk_size/SF_SECTOR_PH);
				conv_secs_to_date_sbuffer(sflash_info.creation_date);
				uart_printf("Disk formated [%s]\n",sbuffer);
				for(i=0;i<sflash_info.files_used+1;i++){ // Mit Testreserve
					res=fs_info(&fs_stat,i);

					if(res<=0) break;
					if(res&FS_STAT_INACTIVE) uart_printf("(- '%s'   (deleted))\n",fs_stat.fname); // Inaktive/Deleted
					else if(res&FS_STAT_ACTIVE) {
						uart_printf("- '%s'   ",fs_stat.fname); // Active
						if(res&FS_STAT_UNCLOSED) {
							fs_open(&fs_desc,fs_stat.fname,SF_OPEN_READ|SF_OPEN_RAW); // Find out len by DummyRead
							fs_read(&fs_desc, NULL , 0xFFFFFFFF);   // Read as much as possible
							uart_printf("(Unclosed: %u Bytes)",fs_desc.file_len);
						}else{
							uart_printf("%u Bytes",fs_stat.file_len);
						}
						// The creation Flags
						if(fs_stat.disk_flags & SF_OPEN_CRC) uart_printf(" CRC32:%x",fs_stat.file_crc32);
						if(fs_stat.disk_flags & SF_OPEN_EXT_SYNC) uart_printf(" ExtSync");
						//if(fs_stat.disk_flags & _SF_OPEN_RES) uart_printf(" Reserved");
						conv_secs_to_date_sbuffer(fs_stat.file_ctime);
						uart_printf(" [%s]\n",sbuffer);
					}
				}
				uart_printf("Disk Nr. of files active: %d\n",sflash_info.files_active);
				uart_printf("Disk Nr. of files used: %d\n",sflash_info.files_used);
#ifdef JSTAT
				if(sflash_info.sectors_unknown) uart_printf("WARNING - Disk Error: Nr. of unknown sectors: %d\n",sflash_info.sectors_unknown);
#endif
				uart_printf("Res:%d\n",res);
				break;

			case 'W': // Write BULK Data...
				anz=atoi(pc);
				uart_printf("'W' BULK Write %d * '[_012_xx_abc]' to file\n",anz);
				for(i=0;i<anz;i++) {
					sprintf((char*)sbuffer,"[_012_%u_abc]",i);
					res=fs_write(&fs_desc,sbuffer,strlen((char*)sbuffer));
					if(res<0) break;
				}
				uart_printf("Write %d: Res:%d\n",anz,res);
				break;

			case 'w': // Write Text Data
				uart_printf("'w' Write '%s' to file\n",pc);
				res=fs_write(&fs_desc,(uint8_t*)pc,strlen(pc));
				uart_printf("Res:%d\n",res);
				break;

			case 'r':
				anz=atoi(pc);
				uart_printf("'r' Read (max.) %d Bytes from File:\n",anz);

				if(anz<(int)sizeof(sbuffer)) {
					res=fs_read(&fs_desc, sbuffer , anz);
					// Show max. the first 60 Bytes...
					if(res>0){
					   for(i=0;i<res;i++){
						   uart_printf("%c",sbuffer[i]);
						   if(i>60) {
							   uart_printf("..."); break; // Don't show all
						   }
					   }
					   uart_printf("\n");
					}
				}else{
					uart_printf("Read %d silent...\n",anz);
					res=fs_read(&fs_desc, NULL , anz);
				}
				uart_printf("Read %d Bytes: Res:%d\n",anz,res);
				break;

			case 'e':
				res=fs_rewind(&fs_desc);
				uart_printf("'e' Rewind File Res:%d\n",res);
				break;

            case 't':
                uart_printf("'t' Test/Info: disk_cnt:%d  file_len:%d CRC32:%x\n",fs_desc.file_pos,fs_desc.file_len, fs_desc.file_crc32);
                break;

            case 'z':
               uart_printf("'z' CRC32: Disk:%x, Run:%x\n",fs_get_crc32(&fs_desc),fs_desc.file_crc32);
               break;

			case 'n': // Rename (Open) File to
				uart_printf("'n' Rename File to '%s'\n",pc); // rename also allows changing Flags (eg. Hidden or Sync)
				i=SF_OPEN_CREATE|SF_OPEN_WRITE;  // ensures new File is avail. and empty
				if(fs_desc.open_flags&SF_OPEN_CRC) i|= SF_OPEN_CRC; // Optionally Take CRC to new File
				res=fs_open(&fs_desc_b,pc,i); // Must be a new name (not the same)
				if(!res) res=fs_rename(&fs_desc,&fs_desc_b);
				uart_printf("Rename Res:%d\n",res);
				break;

			case 'q':    // Quit: System Reset.
				uart_printf("'q' Exit in 1 sec...\n");
				sleep(1);
				uart_printf("Bye!\n");
				SysCtrlSystemReset(); // Up to (TI-RTOS+Debugger) what happens: Reset, Stall, Nirvana, .. See TI docu.

/****************** TESTFUNCTION Development only********************************/
			case 'm':   // mADR Examine Mem Adr. in HEX
				{
				  extern void sflash_read(uint32_t sadr, uint8_t* sbuf, uint16_t len);
				  int adr,j;
				  adr=strtoul(pc,0,16);
				  sflash_read(adr,sbuffer,256); // Einzelne Eintraege lesen (OK, ist langsam!)

				  for(i=0;i<16;i++){
					uart_printf("%04X: ",adr+i*16);
					for(j=0;j<16;j++) uart_printf("%02X ",sbuffer[i*16+j]);
					for(j=0;j<16;j++) {
						res=sbuffer[i*16+j];
						if(res<' ' || res>127) res='.';
						uart_printf("%c",res);
					}
					uart_printf("\n");
				  }
				}
				break;
#ifdef __WIN32__
			case '+':
				uart_printf("'+': Write VirtualDisk to File: '%s'\n",pc);
				res=ll_write_vdisk(pc);
				uart_printf("Res: %d\n",res);
				break;

			case '#':
				uart_printf("'#': Read VirtualDisk from File: '%s'\n",pc);
				res=ll_read_vdisk(pc);
				uart_printf("Res: %d\n",res);
				uart_printf("FS Init Normal:%d\n",fs_start(FS_START_NORMAL));
				break;

			case '$':
				anz=strtoul(pc,NULL,0);
				uart_printf("'$': Set vdisk ID: $%x/%d\n",anz,anz);
				ll_setid_vdisk(anz);
				break;
#else
			case '!':   // Time Management - Set the embedded Timer (in unix-Seconds)
				asecs=strtoul(pc,NULL,0);
				if(asecs) Seconds_set(asecs);
				asecs=Seconds_get();    // TI-RTOS
				conv_secs_to_date_sbuffer(asecs);
				uart_printf("'!': Time: [%s] (%u secs)\n",sbuffer,asecs);
				break;

 #ifdef USE_WATCHDOG
			case '#':   // #RESET is sent by JesFsCon to reset the Board
				if(strcmp(input,"#RESET")) break;
				SysCtrlSystemReset(); // Up to (TI-RTOS+Debugger) what happens: Reset, Stall, Nirvana, .. See TI docu.
 #endif
#endif
/******************* ONLY TEST **************************************************/
			default:
				uart_printf("???\n");
		   }
		}
	}
}

// END
