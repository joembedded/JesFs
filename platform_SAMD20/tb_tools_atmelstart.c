/*********************************************************************
* tb_tools_atmelstart.c - Toolbox for UART, Unix-Time, ..
*
* For platform ATSAMD20eXX
*
* 2019 (C) joembedded.de
* Version: 
* 1.0 / 13.1.2022
*********************************************************************/

#include <atmel_start.h> // Atmel start headers

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h> // for var_args

#include "tb_tools.h"

#ifndef SAMD20    // Define this Macro in ProjectOptions - PredefinedSymbols
  #error "Define the Platform (SAMD20)"
#endif

// ---- local defines --------------

// ---------- locals uart --------------------
#define TB_SIZE_LINE_OUT  80  // 1 terminal line working buffer
static char tb_uart_line_out[TB_SIZE_LINE_OUT];

#define UART_RX_BUFSIZE  128    // Any size OK
#define UART_TX_BUFSIZE  128    //
#define UART_TX_TIMEOUT  10000  // Semaphoren-Timeout in Task-Ticks (100 msec enough for 256 Bytes(22 msec))

struct io_descriptor *serial_io; // Handle

// ---- locals Watcdog ----
static bool tb_wdt_enabled=false;

static bool tb_init_flag=false;

// ---- locals RTC ----
static volatile unsigned int tb_rtc_overflows=0;
static struct timer_task RTC_task1;

//--------------- hardware drivers UART ----------------------
//---------uart_blockout---------
static int16_t uart_blkout(uint8_t* pu, uint16_t len){
    io_write(serial_io, pu, len);
    return 0;
}
// ----------- uart_open---------------------------
// Manual Flow!
int16_t uart_open(void){
    usart_sync_get_io_descriptor(&serial, &serial_io);
    usart_sync_enable(&serial);
    return 0;
}

// Kann bedenkenlos jederzeit aufgerufen werden
void uart_close(void){
    usart_sync_disable(&serial);
}
// ----------- UART drivers end -------------

// RTC callback
static void rtc_task1_cb(const struct timer_task *const timer_task)
{
    tb_rtc_overflows++;
}

void rtc_init()
{
    RTC_task1.interval = 33;
    RTC_task1.cb       = rtc_task1_cb;
    RTC_task1.mode     = TIMER_TASK_REPEAT;

    timer_add_task(&RTC_0, &RTC_task1);
    timer_start(&RTC_0);
}

// -------- init Toolbox -----------------
// Init UART, SPI and optionally Watchdog - Power opt. missing
void tb_init(void){
    if(tb_init_flag==false){
	    atmel_start_init();

		uart_open();
        
        rtc_init();
        
        // TODO: SPI init

		tb_init_flag=true;
	}
}

// ------ uninit all --------------------
void tb_uninit(void){
    if(tb_init_flag){
        // LowPower ToDo...
        // uart_close(); etc..
        tb_init_flag=false;
    }
}

// ------ Only LED 0 (RED) available on SAMD20 -----
void tb_board_led_on(uint8_t idx){
	gpio_set_pin_level(LED_RED, false);
}
void tb_board_led_off(uint8_t idx){
    gpio_set_pin_level(LED_RED, true);
}
void tb_board_led_invert(uint8_t idx){
    gpio_toggle_pin_level(LED_RED);
}


// ------- System Reset -------------
void tb_system_reset(void){
    while(1);
}


// ------Watchdog, triggered befor reset -----
// ---- enable the watchdog only once! -----------
// #define USE_WATCHDOG

uint32_t tb_watchdog_init(void){
#ifdef USE_WATCHDOG
    wdt_enable(&WDT_0);
    tb_wdt_enabled=true;
    return 0;
#endif
    return -1;
}

// ---- Feed the watchdog, No function if WD not initialized ------------
// feed_ticks currently ignored, but >0
void tb_watchdog_feed(uint32_t feed_ticks){
    if(feed_ticks && tb_wdt_enabled) {
        wdt_feed(&WDT_0);
    }
}

// --- A low Power delay ---
void tb_delay_ms(uint32_t msec){
    delay_ms(msec);
} 
 
// ---- Unix-Timer. Must be called periodically to work, but at least twice per RTC-overflow (512..xx secs) ---
uint32_t tb_time_get(void){
    return (tb_rtc_overflows >> 10);
 }

// Set time, regarding the timer
void tb_time_set(uint32_t new_secs){
    tb_rtc_overflows = (new_secs << 10);
}

// ----- fine clock ticks functions ---------------------
// Use the difference of 2 timestamps to calculalte msec Time
uint32_t tb_get_ticks(void){
     return tb_rtc_overflows;
}

// Warning: might overflow for very long ticks, maximum delta is 24 Bit
uint32_t tb_deltaticks_to_ms(uint32_t t0, uint32_t t1){
    return t1-t0;
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
int16_t tb_putc(char c){
  tb_printf("%c",c);
  return 0;
}

// ---- Input functions 0: Nothing available (tb_kbhit is faster than tb_getc) ---------
int16_t tb_kbhit(void){
    return (int16_t)usart_sync_is_rx_not_empty(&serial);
}

// ---- get 1 char (0..255) (or -1 if nothing available)
int16_t tb_getc(void){
    int16_t res = -1;
    io_read(serial_io, (uint8_t*)&res, 1);
    return res;
}

// Get String with Timout (if >0) in msec of infinite (Timout 0), No echo (max_uart_in without trailing \0!)
int16_t tb_gets(char* input, int16_t max_uart_in, uint16_t max_wait_ms, uint8_t echo){
    int16_t idx=0;
    int16_t res;
    char c;
    max_uart_in--;  // Reserve 1 Byte for End-0
    max_wait_ms /= 1000;
    uint32_t s_time = tb_time_get();
    for(;;){
        res=tb_kbhit();
        if(res>0){
            res=tb_getc();
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
            uint32_t d_time = tb_time_get() - s_time;
            if( d_time > max_wait_ms ) break;
        }
    }
    input[idx]=0;   // Terminate String
    return idx;
}

//**
