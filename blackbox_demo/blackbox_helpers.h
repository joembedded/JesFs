/*****************************************************************
* blackbox_helpers.h
*
* Include-File with some less important stuff for the blackbox
*******************************************************************************/


#include <stdint.h>
#include <stddef.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/********************* WINDOWS inits *************************/
#ifdef __WIN32__

 /******** On PC use stdio **************/
 #include <windows.h>
 #include <time.h>
 #include <conio.h> // kbhit/getc
 #define sleep(x) Sleep(x*1000)
 #define uart_printf printf
 #define uart_putc putchar
 #define uart_open()
 #define uart_kbhit() kbhit()
 #define uart_getc() getch()
 extern int uart_gets(uint8_t* buf, int16_t max, uint16_t wt, uint8_t echo);
 #define mainThread_init()
 #define mainThread main
 #define GPIO_toggle(x)  // No LEDs on PC
 #define SysCtrlSystemReset() exit(-1)  // Exit

 // external Low-Level-Helper functions for WIN32 to save RAM as JesFs-File in JesFs_ll_pc.c
 extern int16_t ll_write_vdisk(char *fname);   // PC LowLevel LL-Driver
 extern int16_t ll_read_vdisk(char *fname);   // PC LowLevel LL-Driver
 extern void ll_setid_vdisk(uint32_t id); // PC LowLevel LL-Driver

 void helper_save_disk_to_file(void);
 void helper_load_disk_from_file(void);

#else

/********************* TI-RTOS inits *************************/
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

/* Board Header file */
#include "Board.h"

/* Project Header Files */
#include "uart_xl.h"

void mainThread_init(void);


#endif // __WIN32__
/********************* END inits *************************/

// Common Helper Functions
void helper_show_board(void);
void helper_conv_secs_to_date(uint32_t asecs, uint8_t *sbuffer);
void helper_show_directory(void);

// END

