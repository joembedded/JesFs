/*********************************************************************
* tb_tools_cc.c - Toolbox for UART, Unix-Time, ..
*
* For platform CC13XX / CC26XX
*
* 2019 (C) joembedded.de
* Version: 1.0 25.11.2019
*********************************************************************/

// TI-RTOS requires Bunch of Header Files
#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/Error.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Idle.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/hal/Seconds.h>
#include <ti/sysbios/family/arm/m3/Hwi.h>
#include <xdc/cfg/global.h>
/* Driver Header files */
#include <ti/drivers/GPIO.h>
//#include <ti/drivers/I2C.h>
#include <ti/drivers/SPI.h>
#include <ti/drivers/UART.h>
#include <ti/drivers/uart/UARTCC26XX.h>
#include <ti/drivers/Watchdog.h>
// Driverlib
#include <ti/devices/DeviceFamily.h>
#include DeviceFamily_constructPath(driverlib/sys_ctrl.h)
#include DeviceFamily_constructPath(driverlib/aon_rtc.h)

#include "Board.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h> // for var_args
#include <unistd.h> // for sleep/usleep

#include "tb_tools.h"

#ifndef CC13XX_CC26XX    // Define this Macro in ProjectOptions - PredefinedSymbols
  #error "Define the Platform (CC13XX_CC26XX)"
#endif

// ---- local defines --------------

// ---------- locals uart --------------------
#define TB_SIZE_LINE_OUT  80  // 1 terminal line working buffer
static char tb_uart_line_out[TB_SIZE_LINE_OUT];

#define UART_RX_BUFSIZE  128    // Any size OK
#define UART_TX_BUFSIZE  128    //
#define UART_TX_TIMEOUT  10000  // Semaphoren-Timeout in Task-Ticks (100 msec enough for 256 Bytes(22 msec))

static UART_Handle uart_handle; // Handle

// transfer buffers
static uint8_t uart_tx_buf[UART_TX_BUFSIZE];
static Semaphore_Handle uart_tx_sema;

static uint8_t uart_rx_buf[UART_RX_BUFSIZE];
static uint16_t uart_rxidx_in, uart_rxidx_out;
static int16_t uart_rxfrei_anz;

// ---- locals Watcdog ----
static bool tb_wdt_enabled=false;

static bool tb_init_flag=false;

//--------------- hardware drivers UART ----------------------
//---------uart_blockout---------
static int16_t uart_blkout(uint8_t* pu, uint16_t len){
    if(len>UART_TX_BUFSIZE) return -1;

    // TX-Puffer 256 Bytes => wait max 22 msec
    Semaphore_pend(uart_tx_sema, UART_TX_TIMEOUT); //

    memcpy(uart_tx_buf,pu, len); // D S n
    UART_write(uart_handle,uart_tx_buf,len);
    return 0;
}
// ---- callback Thread simply frees ressource -------------
static void _uart_handle_write_cb(UART_Handle handle, void *tmp_txBuf, size_t size){
    Semaphore_post(uart_tx_sema);  // Ready! Seems to fail in rare cases?
}

// --- read Callback: Es wurden size_bytes gelesen, wieviel weiter
static void _uart_handle_read_cb(UART_Handle handle, void *tmp_txBuf, size_t size){
    uint16_t frei;
    int16_t mxf;
    uint32_t key=Hwi_disable();
    mxf=uart_rxfrei_anz-size;  // Soviel ist JETZT noch frei (int16)

    uart_rxidx_in+=size;
    if(uart_rxidx_in>=UART_RX_BUFSIZE) uart_rxidx_in=0;  // Overflow
    if(uart_rxidx_in>=uart_rxidx_out)  frei=UART_RX_BUFSIZE-uart_rxidx_in;
    else frei=uart_rxidx_out-uart_rxidx_in;
    // Bei FLOW-Ctrl. evtl. kleinere Bloecke als 1/4 lesen fuer feinere Granularitaet
    if(frei>(UART_RX_BUFSIZE/4)) frei=(UART_RX_BUFSIZE/4);

    uart_rxfrei_anz=mxf;   // merken
    Hwi_restore(key);

    UART_read(uart_handle, uart_rx_buf+uart_rxidx_in , frei);  // Empfangen was geht
}
// ----------- uart_open---------------------------
// Manual Flow!
int16_t uart_open(void){
    Semaphore_Params semp;

    // Zuerst die Semaphoren erzeugen!
    // Semaphore fuer TX erzeugen
    Semaphore_Params_init(&semp);
    semp.mode = Semaphore_Mode_BINARY;
    uart_tx_sema=Semaphore_create(1,&semp, NULL); // Geht auch ohne EB

    uart_rxidx_in=0;   // In-Pointer
    uart_rxidx_out=0; // Out-Pointer
    uart_rxfrei_anz=UART_RX_BUFSIZE; // Wieviel verfuegbar insgesamt

    UART_Params uartParams;
    UART_Params_init(&uartParams);
    uartParams.writeDataMode = UART_DATA_BINARY; // TEXT nicht auf CC26xx
    uartParams.readDataMode = UART_DATA_BINARY; // TEXT nicht auf CC26xx
    uartParams.readReturnMode = UART_RETURN_FULL;
    uartParams.readEcho = UART_ECHO_OFF; // ECHO ON nicht auf CC26xx
    uartParams.baudRate = 115200;   // <--- 115kBd

    uartParams.readMode = UART_MODE_CALLBACK;
    uartParams.readCallback = _uart_handle_read_cb;
    uartParams.writeMode = UART_MODE_CALLBACK;
    uartParams.writeCallback = _uart_handle_write_cb;

    // Alwasys UART No 0
    uart_handle = UART_open(0, &uartParams); // MUSS klappen!
    if(!uart_handle) return -1004;   // Kann nicht oeffnen!

    UART_control(uart_handle, UARTCC26XX_CMD_RETURN_PARTIAL_ENABLE, NULL); // Nicht noetig bei Einzelzeichen
    UART_read(uart_handle, uart_rx_buf , (UART_RX_BUFSIZE/4));  // Maximal 1/4 Puffer lesen

    return 0;
}

// Kann bedenkenlos jederzeit aufgerufen werden
void uart_close(void){
    if(uart_handle) {
        UART_readCancel(uart_handle);
        // UART_control(uart_handle, UART_CMD_RXDISABLE, 0); // Suggested by TI because of I_q, but no results
        UART_writeCancel(uart_handle);
        UART_close(uart_handle);

        /** Dummy open/close with no callbacks, because of too high I_q */
        UART_Params uartParams;
        UART_Params_init(&uartParams);
        // Alwasys UART No 0
        uart_handle = UART_open(0, &uartParams);
        UART_close(uart_handle);

        uart_handle=NULL;
    }
    Semaphore_delete(&uart_tx_sema);

}
// ----------- UART drivers end -------------


// -------- init Toolbox -----------------
// Init UART, SPI and optionally Watchdog
void tb_init(void){
    /* Init what we need */
    GPIO_init(); // LEDs (and JesFs too)
    /* Configure the LED pin */
    GPIO_setConfig(Board_GPIO_RLED, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW); // RED

    UART_init(); // required for HW uart
    //I2C_init();
    SPI_init();  // normally required required (several instances available)

    uart_open();

    tb_init_flag=true;

}

// ------ uninit all --------------------
void tb_uninit(void){
    if(tb_init_flag){
        uart_close();

        // Rest: ToDo...
        tb_init_flag=false;
    }
}

// ------ Only LED 0 (RED) available on CCxxyy -----
void tb_board_led_on(uint8_t idx){
    GPIO_write(Board_GPIO_RLED, 1);
}
void tb_board_led_off(uint8_t idx){
    GPIO_write(Board_GPIO_RLED, 0);
}
void tb_board_led_invert(uint8_t idx){
    GPIO_toggle(Board_GPIO_RLED);
}


// ------- System Reset -------------
void tb_system_reset(void){
    SysCtrlSystemReset(); // Up to (TI-RTOS+Debugger) what happens: Reset, Stall, Nirvana, .. See TI docu.
}


// ------Watchdog, triggered befor reset -----
// ---- enable the watchdog only once! -----------
// #define USE_WATCHDOG
#define WDINTV 1500000 // 1 sec on cc13xx
#define WD_INTERVAL 250 // 250 secs for large Flash
static uint16_t wd_cnt=WD_INTERVAL;

Watchdog_Handle watchdogHandle;
void watchdogCallback(uintptr_t _unused){
    Watchdog_clear(watchdogHandle);
    if(wd_cnt--==0) for(;;);  // Die
}

void tb_watchdog_init(void){
#ifdef USE_WATCHDOG
    Watchdog_init();
    /* Create and enable a Watchdog with resets disabled */
    Watchdog_Params_init(&params);
    params.callbackFxn = (Watchdog_Callback)watchdogCallback;
    params.resetMode = Watchdog_RESET_ON;
    //params.debugStallMode = Watchdog_DEBUG_STALL_OFF;   // (Default (OK for Debug) is STALL_ON)
    watchdogHandle = Watchdog_open(Board_WATCHDOG0, &params); // Assert it works..
    Watchdog_setReload(watchdogHandle, WDINTV);
    tb_wdt_enabled=true;
#endif
}

// ---- Feed the watchdog, No function if WD not initialised ------------
// feed_ticks currently ignored, but >0
void tb_watchdog_feed(uint32_t feed_ticks){
    if(feed_ticks && tb_wdt_enabled) {
        wd_cnt=WD_INTERVAL;
    }
}


// --- A low Power delay ---
void tb_delay_ms(uint32_t msec){   
    while(msec){
        if(msec>=1000) {
            sleep(1);
            msec -=1000;
        }else{
            usleep(msec*1000);  // < 1e6
            break;
        }
    }
}
 
// ---- Unix-Timer. Must be called periodically to work, but at least twice per RTC-overflow (512..xx secs) ---
uint32_t tb_time_get(void){
    return Seconds_get();   // use BIOS here
 }

// Set time, regarding the timer
void tb_time_set(uint32_t new_secs){
    return Seconds_set(new_secs);   // use BIOS here
}

// ----- fine clocl ticks functions ---------------------
// Use the difference of 2 timestamps to calclualte msec Time
// CCxxyy: RTC runns with 65535 ticks/sec
uint32_t tb_get_ticks(void){
     return AONRTCCurrentCompareValueGet();
}

// Warning: might overflow for very long ticks, maximum delta is 24 Bit
uint32_t tb_deltaticks_to_ms(uint32_t t0, uint32_t t1){
  uint32_t delta_ticks = t1-t0;
  return (delta_ticks * 125)/8192;  // OK for ranges 0..524 secs
}


// tb_printf(): printf() to toolbox uart. Wait if busy
void tb_printf(char* fmt, ...){
    size_t ulen;
    va_list argptr;
    va_start(argptr, fmt);

    ulen=vsnprintf(tb_uart_line_out, TB_SIZE_LINE_OUT, fmt, argptr);  // vsn: limit!
    va_end(argptr);
    // vsnprintf() limits output to given size, but might return more.
    if(ulen>TB_SIZE_LINE_OUT-1) ulen=TB_SIZE_LINE_OUT-1;

    uart_blkout((uint8_t*)tb_uart_line_out, ulen);
}

// tb_putc(): Wait if not ready
void tb_putc(char c){
  tb_printf("%c",c);
}

// ---- Input functions 0: Nothing available (tb_kbhit is faster than tb_getc) ---------
int16_t tb_kbhit(void){
    int16_t res;
    uint32_t key=Hwi_disable();
    res=UART_RX_BUFSIZE-uart_rxfrei_anz;
    Hwi_restore(key);
    return res;
}

// ---- get 1 char (0..255) (or -1 if nothing available)
int16_t tb_getc(void){
    int16_t res;
    int16_t mxf;
    uint32_t key=Hwi_disable();
    if(uart_rxidx_in == uart_rxidx_out){
        mxf=UART_RX_BUFSIZE;  // UART-Puffer ist leer-> Maximaler Fuellstand!
        res=-1;
    }else{
        res=uart_rx_buf[uart_rxidx_out++];    // Clip
        if(uart_rxidx_out==UART_RX_BUFSIZE) uart_rxidx_out=0;
        mxf=uart_rxfrei_anz+1;
    }
    uart_rxfrei_anz=mxf;
    Hwi_restore(key);
    return res;
}

// Get String with Timout (if >0) in msec of infinite (Timout 0), No echo (max_uart_in without trailing \0!)
int16_t tb_gets(char* input, int16_t max_uart_in, uint16_t max_wait_ms, uint8_t echo){
    int16_t idx=0;
    int16_t res;
    char c;
    max_uart_in--;  // Reserve 1 Byte for End-0
    for(;;){
        res=tb_kbhit();
        if(res>0){
            res=tb_getc();
            if(res<0) break;
            c=res;
            if(c=='\n' || c=='\r') {
                break;    // NL CR or whatever (no Echo for NL CR)
            }else if(c==8){  // Backspace
               if(idx>0){
                   idx--;
                   if(echo){
                       tb_putc(8);
                       tb_putc(' ');
                       tb_putc(8);
                   }
               }
            }else if(c>=' ' && c<128 && idx<max_uart_in){
                input[idx++]=c;
                if(echo) tb_putc(c);
            }
        }else{
            if(max_wait_ms){
                if(!--max_wait_ms) break;
            }
            tb_delay_ms(1);
        }
    }
    input[idx]=0;   // Terminate String
    return idx;
}

//**
