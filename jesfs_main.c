/*******************************************************************************
* JesFs.C: JesFs Demo
*
* JesFs - Jo's Embedded Serial File System
*
* Demo how to use JesFs on CC1310 Launchpad.
* Can be used as standalone project or, better, together with JesFsBoot.
* Replacement for empty.c in emty TI-RTOS Project
*
* (C)2018 joembedded@gmail.com - www.joembedded.de
*
* --------------------------------------------
* Please regard: This is Copyrighted Software!
* --------------------------------------------
*
* It may be used for education or non-commercial use, and without any warranty!
* For commercial use, please read the included docu and the file 'license.txt'
*******************************************************************************/

/* Bunch of Std. Headers */
#include <unistd.h> // for sleep/usleep
#include <stdint.h>
#include <stddef.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/* Driver Header files */
#include <ti/drivers/GPIO.h>
//#include <ti/drivers/I2C.h>
//#include <ti/drivers/SPI.h>
#include <ti/drivers/UART.h>

// Driverlibs for Hardware access - Here: SysCtrlSystemReset() (see 'q')
#include <ti/devices/DeviceFamily.h>
#include DeviceFamily_constructPath(driverlib/sys_ctrl.h)

// Hint: If defined, Watchdog is activated (only required for use with the SECURED Bootloader,
// because it starts with Watchdog enabled. For Development: may be undefined.
#define USE_WATCHDOG
#ifdef USE_WATCHDOG
 #include <ti/drivers/Watchdog.h>
#endif

/* Board Header file */
#include "Board.h"

/* Project Header Files */
#include "my_uart.h"
#include "JesFs.h"

/* ----------------------- Globals ---------------------------- */
uint8_t sbuffer[256]; // Test buffer
FS_DESC fs_desc, fs_desc_b; // _b for rename etc..
FS_STAT fs_stat;

#ifdef USE_WATCHDOG
// Will blink with the Red LED.
#define WDINTV  1500000 // 1 sec on cc13xx

Watchdog_Handle watchdogHandle;
void watchdogCallback(uintptr_t _unused){
    Watchdog_clear(watchdogHandle);
    GPIO_toggle(Board_GPIO_LED0);
}
#endif

/* === mainThread inits === */
void mainThead_init(void){
#ifdef USE_WATCHDOG
    Watchdog_Params params;
#endif

    /* Init what we need */
    GPIO_init(); // LEDs (and JesFs too)
    /* Configure the LED pin */
    GPIO_setConfig(Board_GPIO_LED0, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW); // RED
    GPIO_setConfig(Board_GPIO_LED1, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_HIGH); // GREEN

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

    UART_init(); // required for my_uart
    //I2C_init();
    SPI_init();  // required for JesFs

}

/*  ======== mainThread ======== */
#define MAX_INPUT 80
char input[MAX_INPUT+1];

void *mainThread(void *arg0){
    int32_t res;
    int32_t i;
    int32_t anz;
    char *pc;   // Points to argument

    mainThead_init();

    my_uart_open();
    my_printf("*** JesFs *Demo* V1.0 "__TIME__ " " __DATE__ " ***\n\n");
    my_printf("(C)2018 joembedded@gmail.com - www.joembedded.de\n\n");

    my_printf("Filesystem Init:%d\n",fs_start(FS_START_NORMAL));

    while (1) {
        GPIO_toggle(Board_GPIO_LED1);
        my_printf("> ");
        res=my_gets(input,MAX_INPUT);
        my_putchar('\n');

        if(res>0){
            pc=&input[1];
            while(*pc==' ') pc++;   // Remove Whitspaces from Input
            switch(input[0]){
            case 'i':
                my_printf("'i' Filesystem Init Fast:%d\n",fs_start(FS_START_FAST));
                break;
            case 'I':
                my_printf("'I' Filesystem Init Normal:%d\n",fs_start(FS_START_NORMAL));
                break;

            case 's':
                my_printf("'s' Sent Serial Flash to DeepSleep. Wake with 'i'/'I'\n");
                fs_deepsleep();
                break;

            case 'F':
                my_printf("'F' Format Serial Flash (may take up to 120 secs!)...\n");
                my_printf("FS Init:%d\n",fs_format(0xFFFFFFFF));    // ID currently not used
                break;

            case 'o':
                my_printf("'o' Open File for Reading or Raw '%s'\n",pc);
                res=fs_open(&fs_desc,pc,SF_OPEN_READ|SF_OPEN_RAW|SF_OPEN_CRC);    // Read only
                my_printf("res:%d, Len:%d\n",res,fs_desc.file_len); // (Len: if already exists)
                break;

            case 'O':
                my_printf("'O' Open File for Writing '%s'\n",pc);
                res=fs_open(&fs_desc,pc,SF_OPEN_CREATE|SF_OPEN_WRITE |SF_OPEN_CRC);    // Create if not there, in any case: open for writing
                my_printf("res:%d\n",res);
                break;

            case 'c':
                my_printf("'c' Close File, Res:%d\n",fs_close(&fs_desc));
                break;

            case 'd':
                my_printf("'d' Delete (RAW opened) File, Res:%d\n",fs_delete(&fs_desc));
                break;


            case 'v': // Listing on virtual Disk
                my_printf("'v' Directory:\n");
                my_printf("Disk size: %d Bytes\n",   sflash_info.total_flash_size);
                my_printf("Disk available: %d Bytes / %d Sectors\n",sflash_info.available_disk_size,sflash_info.available_disk_size/SF_SECTOR_PH);
                for(i=0;i<sflash_info.files_used+1;i++){ // Mit Testreserve
                    res=fs_info(&fs_stat,i);

                    if(res<=0) break;
                    if(res&FS_STAT_INACTIVE) my_printf("(- '%s'   (deleted))\n",fs_stat.fname); // Inaktive/Deleted
                    else if(res&FS_STAT_ACTIVE) {
                        my_printf("- '%s'   ",fs_stat.fname); // Active
                        if(res&FS_STAT_UNCLOSED) my_printf("(Unclosed)");
                        else my_printf("%u Bytes",fs_stat.file_len);
                        // The creation Flags
                        if(fs_stat.disk_flags & SF_OPEN_CRC) my_printf(" CRC32:%x",fs_stat.file_crc32);
                        if(fs_stat.disk_flags & SF_OPEN_EXT_SYNC) my_printf(" ExtSync");
                        if(fs_stat.disk_flags & SF_OPEN_EXT_HIDDEN) my_printf(" ExtHidden");
                        my_printf("\n");
                    }
                }
                my_printf("Disk Nr. of files active: %d\n",sflash_info.files_active);
                my_printf("Disk Nr. of files used: %d\n",sflash_info.files_used);
                my_printf("Res:%d\n",res);
                break;

            case 'W': // Write BULK Data...
                anz=atoi(pc);
                my_printf("'W' BULK Write %d * '[_012_xx_abc]' to file\n",anz);
                for(i=0;i<anz;i++) {
                    sprintf((char*)sbuffer,"[_012_%u_abc]",i);
                    res=fs_write(&fs_desc,sbuffer,strlen((char*)sbuffer));
                    if(res<0) break;
                }
                my_printf("Write %d: Res:%d\n",anz,res);
                break;

            case 'w': // Write Text Data
                my_printf("'w' Write '%s' to file\n",pc);
                res=fs_write(&fs_desc,(uint8_t*)pc,strlen(pc));
                my_printf("Res:%d\n",res);
                break;

            case 'r':
                anz=atoi(pc);
                my_printf("'r' Read (max.) %d Bytes from File:\n",anz);

                if(anz<sizeof(sbuffer)) {
                    res=fs_read(&fs_desc, sbuffer , anz);
                    // Show max. the first 60 Bytes...
                    if(res>0){
                       for(i=0;i<res;i++){
                           my_printf("%c",sbuffer[i]);
                           if(i>60) {
                               my_printf("..."); break; // Don't show all
                           }
                       }
                       my_printf("\n");
                    }
                }else{
                    my_printf("Read %d silent...\n",anz);
                    res=res=fs_read(&fs_desc, NULL , anz);
                }
                my_printf("Read %d Bytes: Res:%d\n",anz,res);
                break;

            case 'e':
                res=fs_rewind(&fs_desc);
                my_printf("'e' Rewind File Res:%d\n",res);
                break;

            case 't':
                my_printf("'t' Test/Info: disk_cnt:%d  file_len:%d CRC32:%x\n",fs_desc.file_pos,fs_desc.file_len, fs_desc.file_crc32);
                break;

            case 'z':
               my_printf("'z' CRC32: Disk:%x, Run:%x\n",fs_get_crc32(&fs_desc),fs_desc.file_crc32);
               break;

            case 'n': // Rename (Open) File to
                my_printf("'n' Rename File to '%s'\n",pc); // rename also allows changing Flags (eg. Hidden or Sync)
                i=SF_OPEN_CREATE|SF_OPEN_WRITE;  // ensures new File is avail. and empty
                if(fs_desc.open_flags&SF_OPEN_CRC) i|= SF_OPEN_CRC; // Optionally Take CRC to new File
                res=fs_open(&fs_desc_b,pc,i); // Must be a new name (not the same)
                if(!res) res=fs_rename(&fs_desc,&fs_desc_b);
                my_printf("Rename Res:%d\n",res);
                break;

            case 'q':    // Quit: System Reset.
                my_printf("'q' Exit in 3 secs...\n");
                sleep(3);
                my_printf("Bye!\n");
                SysCtrlSystemReset(); // Up to (TI-RTOS+Debugger) what happens: Reset, Stall, Nirvana, .. See TI docu.

            default:
                my_printf("???\n");
           }
        }
    }
}

// END
