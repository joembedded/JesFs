/* my_uart.c
* Simple printf/gets-replacement for CC13xx etc.. 
* TI-RTOS
*
* (C)2018 www.joembedded.de
*/

/* For usleep() */
#include <unistd.h>
#include <stdint.h>
#include <stddef.h>

/* Fuer my_printf() */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/* Driver Header files */
//#include <ti/drivers/GPIO.h>
// #include <ti/drivers/I2C.h>
//#include <ti/drivers/SPI.h>
#include <ti/drivers/UART.h>
// #include <ti/drivers/Watchdog.h>

/* Board Header file */
#include "Board.h"

/* My Headers */
#include "my_uart.h"

// Globals/Setup
//#define UART_TIMEOUT_MSEC 10000	// If defined, gets() returns after Idle Time - Here: 10000 = 10 sec

/**** my_printf: printf-Wrapper ****/
static UART_Handle def_uart; // Handler

void my_putchar(char c){
    UART_write(def_uart,&c,1);
}

/* Dont know if the 'outbuf' needs to be static, but I guess TI-RTOS copies it */
void my_printf(char* fmt, ...){
	/*static*/ char outbuf[MAX_UART_OUT+1]; // Auf maximale Laenge aufpassen!
    size_t ulen;
    va_list argptr;
    va_start(argptr, fmt);
    ulen=vsnprintf(outbuf, MAX_UART_OUT, fmt, argptr);	// vsn: begrenzt!
    va_end(argptr);
    UART_write(def_uart,outbuf,ulen);
}

// String holen mit Timeout vom init()
int16_t my_gets(char* input, int16_t max_uart_in){ 
    int16_t idx=0;
    int32_t res;
    char c;
    for(;;){
        res=UART_read(def_uart, &c,1);
        if(res>0){
            if(c=='\n' || c=='\r') break;    // NL CR oder was auch immer
            else if(c==8){	// Backspace
               if(idx>0){
                 idx--;
#ifdef ECHO     // If defined Echo Input, DEL needs 3 chars
				 UART_write(def_uart, &c,1);
				 c=' ';
                 UART_write(def_uart, &c,1);
				 c=8;
				 UART_write(def_uart, &c,1);
#endif
               }
            }else if(c>=' ' && c<128 && idx<max_uart_in){
				input[idx++]=c;
#ifdef ECHO   // If defined Echo Input
                UART_write(def_uart, &c,1);
#endif
				}
        }else{
            idx=0;  // No Input..
            break;
        }
    }
    input[idx]=0;   // Terminate String
    return idx;
}

/* My_uart OPEN, Requires external UART_init(), Return 0: OK, <0: Error */
int16_t my_uart_open(void){
    UART_Params uartParams;

    if(def_uart) return -2;   // Already open!

    /* Create a UART with data processing off. */
    UART_Params_init(&uartParams);
    uartParams.writeDataMode = UART_DATA_BINARY;
    uartParams.readDataMode = UART_DATA_BINARY;
    uartParams.readReturnMode = UART_RETURN_FULL;
    uartParams.readEcho = UART_ECHO_OFF;
    uartParams.baudRate = 115200;   // <--- 115kBd

/*** Enable for Callback
    uartParams.readMode = UART_MODE_CALLBACK;
    uartParams.readCallback = _my_uart_read_cb;
****/
#if UART_TIMEOUT_MSEC
	uartParams.readTimeout = UART_TIMEOUT_MSEC*100; // Systicks, means 10usec/Tick
#endif
    def_uart = UART_open(Board_UART0, &uartParams); // Closed never...
    if(!def_uart) return -1;   // Watchdog will kill
    return 0; // OK
}

/* Close */
void my_uart_close(void){
	UART_close(def_uart);
	def_uart=NULL;
}

//
