/*********************************************************************
* tb_tools_nrf52.c - Toolbox for UART, Unix-Time, .. 
*
* For platform nRF52 
*
* 2019 (C) joembedded.de
* Version: 1.0 25.11.2019
*********************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h> // for var_args

#include "nrf.h"
#include "nrf_drv_clock.h"
#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_drv_power.h"
#include "nrf_serial.h"
#include "app_timer.h" // uses RTC1

#include "nrf_drv_wdt.h"

#include "app_error.h"
#include "app_util.h"
#include "boards.h"

#include "tb_tools.h"

#ifndef PLATFORM_NRF52
  #error "Define the Platform (PLATFORM_NRF52)" // in Project-Options on SES
#endif

// ---- local defines --------------
#define USE_BSP // if defined: Bord support package is init too

// ---------- locals uart --------------------
#define TB_UART_NO  1 // use UARTE(1)
NRF_SERIAL_UART_DEF(tb_uart,TB_UART_NO); // reserve Name+InterfaceNumber
NRF_SERIAL_DRV_UART_CONFIG_DEF(m_tbuart_drv_config,
                      RX_PIN_NUMBER, TX_PIN_NUMBER,
                      NRF_UART_PSEL_DISCONNECTED /*RTS_PIN_NUMBER*/, NRF_UART_PSEL_DISCONNECTED /*CTS_PIN_NUMBER*/,
                      NRF_UART_HWFC_ENABLED, NRF_UART_PARITY_EXCLUDED,
                      NRF_UART_BAUDRATE_115200,
                      UART_DEFAULT_CONFIG_IRQ_PRIORITY);
// UART requires FIFOs and working Buffers
#define SERIAL_FIFO_TX_SIZE 128 // Ok for 1.5 lines...
#define SERIAL_FIFO_RX_SIZE 80 /*16*/ //more than necessary
NRF_SERIAL_QUEUES_DEF(tb_uart_queues, SERIAL_FIFO_TX_SIZE, SERIAL_FIFO_RX_SIZE);

#define SERIAL_BUFF_TX_SIZE 20 // TX Junks: Blocks: >=1 
#define SERIAL_BUFF_RX_SIZE 1 // RX in single Chars: 1
NRF_SERIAL_BUFFERS_DEF(tb_uart_buffs, SERIAL_BUFF_TX_SIZE, SERIAL_BUFF_RX_SIZE);

NRF_SERIAL_CONFIG_DEF(tb_uart_config, 
                    NRF_SERIAL_MODE_DMA, //  Mode: Polling/IRQ/DMA (UART 1 requires DMA)
                    &tb_uart_queues, 
                    &tb_uart_buffs, 
                    NULL, // No event handler
                    NULL); // No sleep handler

#define TB_SIZE_LINE_OUT  80  // 1 terminal line working buffer
static char tb_uart_line_out[TB_SIZE_LINE_OUT];

#if !APP_TIMER_KEEPS_RTC_ACTIVE
 #error "Need APP_TIMER_KEEPS_RTC_ACTIVE enabled"
#endif

// ---- locals Watcdog ----
static nrf_drv_wdt_channel_id tb_wd_m_channel_id;
static bool tb_wdt_enabled=false;


// --- locals timestamp ----
static uint32_t old_rtc_secs=0;
static uint32_t cnt_secs=0;
static bool tb_init_flag=false;

// -------- init Toolbox -----------------
void tb_init(void){
    ret_code_t ret;

    if(tb_init_flag==true) return;  // Already init

#ifdef USE_BSP
    // Initialize Board Support Package (LEDs (and buttons)).
    bsp_board_init(BSP_INIT_LEDS | BSP_INIT_BUTTONS);
#endif

    ret = nrf_drv_clock_init();
    APP_ERROR_CHECK(ret);
    ret = nrf_drv_power_init(NULL);
    APP_ERROR_CHECK(ret);

    nrf_drv_clock_lfclk_request(NULL);
    ret = app_timer_init(); // Baut sich eine Event-FIFO, Timer wird APP_TIMER_CONFIG auf 32k..1kHz gesetzt
    APP_ERROR_CHECK(ret);

    ret = nrf_serial_init(&tb_uart, &m_tbuart_drv_config, &tb_uart_config);
    APP_ERROR_CHECK(ret);
    tb_init_flag=true;

}

// ------ uninit all --------------------
void tb_uninit(void){

    // ToDo...
    APP_ERROR_CHECK(!NRF_SUCCESS);

    tb_init_flag=false;
}

// ------ board support pakage -----
void tb_board_led_on(uint8_t idx){
#ifdef USE_BSP
  bsp_board_led_on(idx);
#endif
}
void tb_board_led_off(uint8_t idx){
#ifdef USE_BSP
  bsp_board_led_off(idx);
#endif
}
void tb_board_led_invert(uint8_t idx){
#ifdef USE_BSP
  bsp_board_led_invert(idx);
#endif
}


// ------- System Reset -------------
void tb_system_reset(void){
     NVIC_SystemReset();
}


// ------Watchdog, triggered befor reset -----
static void tb_wdt_event_handler(void){
   // ToDo...
  for(;;); // Wait2Die
}

// ---- enable the watchdog only once! -----------
void tb_watchdog_init(void){
    ret_code_t ret;
    //Configure WDT. RELAOD_VALUE set in sdk_config.h
    // max. 131000 sec...
    ret = nrf_drv_wdt_init(NULL, tb_wdt_event_handler);
    APP_ERROR_CHECK(ret);
    ret = nrf_drv_wdt_channel_alloc(&tb_wd_m_channel_id);
    APP_ERROR_CHECK(ret);
    nrf_drv_wdt_enable();
    tb_wdt_enabled=true;
}

// ---- Feed the watchdog, No function if WD not initialised ------------
// feed_ticks currently ignored, but >0
void tb_watchdog_feed(uint32_t feed_ticks){
  if(feed_ticks && tb_wdt_enabled) {
    nrf_drv_wdt_channel_feed(tb_wd_m_channel_id);
  }
}


// --- A low Power delay ---
APP_TIMER_DEF(tb_delaytimer);
static bool tb_expired_flag;
static void tb_timeout_handler(void * p_context){
      tb_expired_flag=true;
}
void tb_delay_ms(uint32_t msec){   
      uint32_t ticks = APP_TIMER_TICKS(msec);
      if(ticks<APP_TIMER_MIN_TIMEOUT_TICKS) ticks=APP_TIMER_MIN_TIMEOUT_TICKS;  // Minimum 5 ticks 
      ret_code_t ret = app_timer_create(&tb_delaytimer, APP_TIMER_MODE_SINGLE_SHOT,tb_timeout_handler);
      if (ret != NRF_SUCCESS) return;
      tb_expired_flag=false;
      app_timer_start(tb_delaytimer, ticks, NULL);
      while(!tb_expired_flag){
        __WFE();
        __SEV();
        __WFE();
      }
}
 
// ---- Unix-Timer. Must be called periodically to work, but at least twice per RTC-overflow (512..xx secs) ---
uint32_t tb_time_get(void){
   uint32_t rtc_secs;
   // RTC will overflow after 512..xx secs - Scale Ticks to seconds
   rtc_secs=app_timer_cnt_get() / (APP_TIMER_CLOCK_FREQ / (APP_TIMER_CONFIG_RTC_FREQUENCY+1)) ;
   if(rtc_secs<old_rtc_secs)  cnt_secs+=(((RTC_COUNTER_COUNTER_Msk)+1)/ (APP_TIMER_CLOCK_FREQ / (APP_TIMER_CONFIG_RTC_FREQUENCY+1))) ;
   old_rtc_secs=rtc_secs; // Save last seen rtc_secs
   return cnt_secs+rtc_secs;
}

// Set time, regarding the timer
void tb_time_set(uint32_t new_secs){
  tb_time_get(); // update static vars
  cnt_secs=new_secs-old_rtc_secs;
}

// ----- fine clocl ticks functions ---------------------
// Use the difference of 2 timestamps to calclualte msec Time
uint32_t tb_get_ticks(void){
  return app_timer_cnt_get();
}
// Warning: might overflow for very long ticks, maximum delta is 24 Bit
uint32_t tb_deltaticks_to_ms(uint32_t t0, uint32_t t1){
  uint32_t delta_ticks = app_timer_cnt_diff_compute(t1,t0);
  // prevent 32 bit overflow, count in 4 msec steps
  return ( delta_ticks * 250 ) / ((APP_TIMER_CLOCK_FREQ / (APP_TIMER_CONFIG_RTC_FREQUENCY+1) / 4));
}

// tb_printf(): printf() to toolbox uart. Wait if busy
void tb_printf(char* fmt, ...){
    ret_code_t ret;
    size_t ulen;
    size_t uavail;
    va_list argptr;
    va_start(argptr, fmt);

    ulen=vsnprintf((char*)tb_uart_line_out, TB_SIZE_LINE_OUT, fmt, argptr);  // vsn: limit!
    va_end(argptr);
    // vsnprintf() limits output to given size, but might return more.
    if(ulen>TB_SIZE_LINE_OUT-1) ulen=TB_SIZE_LINE_OUT-1;

    for(;;){
      // get available space in TX queue
      uavail=nrf_queue_available_get(&tb_uart_queues_txq);
      if(uavail>=ulen) break;  // enough space: OK to send
      tb_delay_ms(1);
    }

    ret = nrf_serial_write(&tb_uart,tb_uart_line_out,ulen, NULL,0); 
    APP_ERROR_CHECK(ret);  // NRF_SUCCESS if OK
}

// tb_putc(): Wait if not ready
void tb_putc(char c){
  tb_printf("%c",c);
}

// ---- Input functions 0: Nothing available (tb_kbhit is faster than tb_getc) ---------
int16_t tb_kbhit(void){
    int16_t res;
    res=!nrf_queue_is_empty(&tb_uart_queues_rxq);
    return res;
}

// ---- get 1 char (0..255) (or -1 if nothing available)
int16_t tb_getc(void){
    ret_code_t ret;
    uint8_t c;
    ret=nrf_queue_generic_pop(&tb_uart_queues_rxq,&c,false);
    if(ret!=NRF_SUCCESS) return -1;
    return (int16_t)c;  
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
