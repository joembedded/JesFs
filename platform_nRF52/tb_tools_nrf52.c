/***************************************************************************************************************
* tb_tools_nrf52.c - Toolbox for UART, Unix-Time, .. 
*
* For platform nRF52 
* written on nRF52840-pca10056 and nRF52832-ACONNO(ADC52832) with nRF5_SDK_16.0.0 and SES 4.20a
*
* (C) joembedded.de
*
* Versions:   
* 1.0: 25.11.2019
* 1.1: 06.12.2019 added support for Low Power uninit, deep sleep current with RTC wakeup <= 2.7uA on nRF52840
* 1.1b: 09.12.2019 return empty string on timeout
* 1.2: 09.12.2019 changed init to presence of softdevice
* 1.3: 23.12.2019 USE_TBIO for LED
* 1.4: 05.01.2020 tb_isxxx_init()
* 1.5: 06.01.2020 Button added
* 1.5b: 09.01.2020 tb_watchdog_init return type 
* 1.6: 19.01.2020 WDT optimised and GUARD-functions and Framing Errors
***************************************************************************************************************/

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
#include "nrf_serial.h" // components/libraries/serial
#include "app_timer.h" // uses RTC1

#include "nrf_drv_wdt.h"

#include "app_error.h"
#include "app_util.h"

#include "boards.h"

#include "tb_tools.h"

#ifndef PLATFORM_NRF52
  #error "Define the Platform (PLATFORM_NRF52)" // in Project-Options on SES
#endif

/******* SES has a default section for Non-Volatile RAM ***********/
/* _tb_novo is valid if _tb_novo[0]==RAM_MAGIC and _tb_novo[1] = ~_tb_novo[2];  
*  _tb_novo[1] holds the RTC, _tb_novo[3] the GUARD-Value
*/

#define RAM_MAGIC    (0xBADC0FFEUL) 
uint32_t _tb_novo[4] __attribute__ ((section(".non_init")));
uint32_t _tb_bootcode_backup; // Holds initial Bootcode

// ---- local defines --------------
// Define only one USE_cxx:
//#define USE_BSP // if defined: Bord support package is init too (LEDs 0..x)
#define USE_TBIO  // if defined Own Handler (1 LED P0.13 + 1 BUTTON P0.11)

#ifdef USE_TBIO
  #define TB_LED0   NRF_GPIO_PIN_MAP(0,13) // Actice LOW
  #define TB_BUT0   NRF_GPIO_PIN_MAP(0,11) // Button
#endif


// ---------- locals uart --------------------
#ifdef NRF52840_XXAA
  #define TB_UART_NO  1 // use UARTE(1) 2 UARTS on 52840
#else
  #define TB_UART_NO  0 // use UARTE(0)
#endif

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

// Declare the handler
static void tb_uart_handler(struct nrf_serial_s const *p_serial, nrf_serial_event_t event);
NRF_SERIAL_CONFIG_DEF(tb_uart_config, 
                    NRF_SERIAL_MODE_DMA, //  Mode: Polling/IRQ/DMA (UART 1 requires DMA)
                    &tb_uart_queues, 
                    &tb_uart_buffs, 
                    tb_uart_handler, // No event handler
                    NULL); // No sleep handler

#define TB_SIZE_LINE_OUT  120  // 1 terminal line working buffer (opt. long filenames to print)
static char tb_uart_line_out[TB_SIZE_LINE_OUT];

// Higher Current peripherals, can be init/uninit
bool tb_highpower_peripheral_uart_init_flag=false; 

#if !APP_TIMER_KEEPS_RTC_ACTIVE
 #error "Need APP_TIMER_KEEPS_RTC_ACTIVE enabled"
#endif

// ---- locals Watcdog ----
static nrf_drv_wdt_channel_id tb_wd_m_channel_id;
static bool tb_wdt_enabled=false;
static bool tb_uart_error=false; // After UART-ERROR (Framing) tb_uninit() is required)

// --- locals timestamp ----
static uint32_t old_rtc_secs=0; // counts 0...511, Sec-fraction of RTC
static uint32_t cnt_secs=0;  // To compare

static bool tb_basic_init_flag=false; // Always ON, only init once

/* -------- init Toolbox -----------------
* Init consists of 2 blocks:
* ---------------------------
* Current consumption (tested with PCA10056 and module BT840(FANSTEL))
*
* --Minimum required Basic Block: Peripherals always needed (never shut down)--
* bsp_board_init_(BSP_INIT_LEDS): 2.7uA
* bsp_board_init(BSP_INIT_LEDS | BSP_INIT_BUTTONS): 2.7uA
*
* --And optional 'High-Power' Peripherals (shut down by tb_uninit()):
* bsp_board_init(BSP_INIT_LEDS | BSP_INIT_BUTTONS) + nrf_serial_init() (Legacy)POWER_CONFIG_DEFAULT_DCDCEN=0: 890uA
* bsp_board_init(BSP_INIT_LEDS | BSP_INIT_BUTTONS) + nrf_serial_init() (Legacy)POWER_CONFIG_DEFAULT_DCDCEN=1: 550uA
*
*
* 2 Routines for novo(): Keep RTC in non-volatile part of RAM. It is not
* important to be very precise, but better than total loss of time by Reset...
* The NOVO-Structure is stored in uninitialised 16 Bytes of the RAM
*
* novo.n[3] (aka '_tb_bootcode') is used to pass non-volatile status infos in the RAM
* here used for the guard: HH HM ML:LL 4 Bytes
*
* ML:LL is the Line Number of the last known guard point
* HM    is the Module ID (user defined)
*       0: ML:LL is Status code, not a Line Number, see docu
* HH    is a software specific ID.
*       Lower Nibble: t.b.d
*       Higher Nibble: Incremented by 1x by the watchdog.
*
* After Full-Power Reset Bootcode is 0
*******************************************************************************/
// uart-driver has problems with framing errors, see support 'Case ID: 216973'
static void tb_uart_handler(struct nrf_serial_s const *p_serial, nrf_serial_event_t event){
  ret_code_t ret;
  switch(event){
    case NRF_SERIAL_EVENT_DRV_ERR:
    case NRF_SERIAL_EVENT_FIFO_ERR:
        tb_uart_error=true;
        break;
    default:
        break;
    }
}


void tb_init(void){
    ret_code_t ret;

    if(tb_basic_init_flag==false){  // init ony once
      /* Minimum Required Basic Block ---START--- */
    // Init _tb_novo-nonvolatile RAM
    // Check RAM and init counter 
    if(_tb_novo[0]!= RAM_MAGIC || (_tb_novo[1] != ~_tb_novo[2] )){
        _tb_novo[0] = RAM_MAGIC;  // Init Non-Volatile Vars
        _tb_novo[1] = 0;  // Time, Times <(01.01.2020 are "unknown")
        _tb_novo[2] = ~0; // ~Time
        // _tb_novo[3] === _tb_bootcode
        _tb_bootcode_backup=0;  // Invalid
    }else{  // Valid Time
      _tb_bootcode_backup = _tb_bootcode;   // Save Bootcode
      tb_time_set(_tb_novo[1]);
    }
    _tb_bootcode = 0;                     // Bootcode frei fuer neues

#ifdef USE_BSP
      // Initialize Board Support Package (LEDs (and buttons)).
      bsp_board_init(BSP_INIT_LEDS | BSP_INIT_BUTTONS);
#endif
#ifdef USE_TBIO
    nrf_gpio_cfg_output(TB_LED0);  // We use Software CS - Uses PIN Decode
    nrf_gpio_pin_set(TB_LED0);    // LED OFF Active LOW

    nrf_gpio_cfg_input(TB_BUT0,GPIO_PIN_CNF_PULL_Pullup); // Std. Button on all Boards
#endif
    //Normalerweise RX-Port OFF
    nrf_gpio_cfg_input(RX_PIN_NUMBER,GPIO_PIN_CNF_PULL_Pulldown); 

#ifndef SOFTDEVICE_PRESENT
      // Not required for Soft-Device
      ret = nrf_drv_clock_init();
      APP_ERROR_CHECK(ret);
      ret = nrf_drv_power_init(NULL);
      APP_ERROR_CHECK(ret);
      nrf_drv_clock_lfclk_request(NULL);
#endif

      ret = app_timer_init(); // Baut sich eine Event-FIFO, Timer wird APP_TIMER_CONFIG auf 32k..1kHz gesetzt
      APP_ERROR_CHECK(ret);
      tb_basic_init_flag=true;
      /* Minimum Required Basic Block ---END--- */
    }

    if(tb_highpower_peripheral_uart_init_flag==false){  // Higher Power Peripherals, can be powered off by uninit
      ret = nrf_serial_init(&tb_uart, &m_tbuart_drv_config, &tb_uart_config);
      APP_ERROR_CHECK(ret);
      tb_highpower_peripheral_uart_init_flag=true;
      tb_uart_error=false;
    }

}
// ------ uninit all higher power peripherals  --------------------
void tb_uninit(void){
    ret_code_t ret;
    
    // Never uninit basic blocks 

    // uninit higher power blocks
    if(tb_highpower_peripheral_uart_init_flag==true){    
      nrf_drv_uart_rx_abort(&tb_uart.instance);
      ret=nrf_serial_flush(&tb_uart, NRF_SERIAL_MAX_TIMEOUT); 
      APP_ERROR_CHECK(ret);
      ret = nrf_serial_uninit(&tb_uart);
      APP_ERROR_CHECK(ret);

      //Normalerweise RX-Port OFF
      nrf_gpio_cfg_input(RX_PIN_NUMBER,GPIO_PIN_CNF_PULL_Pulldown); 

      // Strange Error, UART needs Power-Cycle for DeepSleep (nrf52840)
      // ( https://devzone.nordicsemi.com/f/nordic-q-a/54696/increased-current-consumption-after-nrf_serial_uninit )
#if TB_UART_NO==0
      #define TB_UART_BASE NRF_UARTE0_BASE  // 0x40002000
#elif TB_UART_NO==1
      #define TB_UART_BASE  NRF_UARTE1_BASE  // 0x40028000
#endif
     *(volatile uint32_t *)(TB_UART_BASE + 0xFFC) = 0;
     *(volatile uint32_t *)(TB_UART_BASE + 0xFFC);
     *(volatile uint32_t *)(TB_UART_BASE + 0xFFC) = 1;
      tb_highpower_peripheral_uart_init_flag=false;
    }
}
// ------ is HighPower Peripheral part init? ----------------------
inline bool tb_is_hpp_init(void){
    return tb_highpower_peripheral_uart_init_flag;
}

// Get Startup-Bootcode
inline uint32_t tb_get_bootcode(bool ok){
    uint32_t res = _tb_bootcode_backup;
    if(ok) _tb_bootcode_backup=1; // Aufgezeichnet (aus MIC1)
    return res;
}


// ------ board support pakage or direct I/O -----
inline void tb_board_led_on(uint8_t idx){
#ifdef USE_BSP
  bsp_board_led_on(idx);
#endif
#ifdef USE_TBIO
    nrf_gpio_pin_clear(TB_LED0);    // LED OFF Active LOW
#endif

}
inline void tb_board_led_off(uint8_t idx){
#ifdef USE_BSP
  bsp_board_led_off(idx);
#endif
#ifdef USE_TBIO
    nrf_gpio_pin_set(TB_LED0);    // LED OFF Active LOW
#endif
}
inline void tb_board_led_invert(uint8_t idx){
#ifdef USE_BSP
  bsp_board_led_invert(idx);
#endif
#ifdef USE_TBIO
    nrf_gpio_pin_toggle(TB_LED0);    // LED OFF Active LOW
#endif
}
// Get the Button State
inline bool tb_board_button_state(uint8_t idx){
#ifdef USE_BSP
    return bsp_board_button_state_get(idx);
#endif
#ifdef USE_TBIO
    return nrf_gpio_pin_read(TB_BUT0);    // LED OFF Active LOW
#endif
}

// ------- System Reset (Will keep Watchdog active, if enabled) ------------
void tb_system_reset(void){
     NVIC_SystemReset();
}

// ShowPin - Debug-Function!: pin_number=NRF_GPIO_PIN_MAP(port, pin) // == ((pin_0_31)+port_0_1*32)
void tb_dbg_pinview(uint32_t pin_number){
    nrf_gpio_pin_dir_t   dir;
    nrf_gpio_pin_input_t input;
    nrf_gpio_pin_pull_t  pull;
    nrf_gpio_pin_drive_t drive;
    nrf_gpio_pin_sense_t sense;

    NRF_GPIO_Type * reg = nrf_gpio_pin_port_decode(&pin_number);
    uint32_t pr=reg->PIN_CNF[pin_number];

    dir=(pr>>GPIO_PIN_CNF_DIR_Pos)&1; // 0: Input, 1: Output
    input=(pr>>GPIO_PIN_CNF_INPUT_Pos)&1; // 0: Connect, 1: Disconnect
    pull=(pr>>GPIO_PIN_CNF_PULL_Pos)&3; // 0: Disabled, 1: Pulldown, 3(!): Pullup
    drive=(pr>>GPIO_PIN_CNF_DRIVE_Pos)&7; // "S0S1","H0S1","S0H1","H0H1","D0S1","D0H1","S0D1","H0D1"
    sense=(pr>>GPIO_PIN_CNF_SENSE_Pos)&3; // 0: Disabled, 2(!):Sense_High 3(!):Sens_low
    tb_printf("P:%u.%02u: ",pin_number>>5,pin_number&31);

    tb_printf("I:%u ",nrf_gpio_pin_read(pin_number));
    tb_printf("O:%u ",nrf_gpio_pin_out_read(pin_number));

    tb_printf(dir?"Output ":"Input  "); // 7
    tb_printf(input?"Disconn.  ":"Connected "); // 10
    const char* drp[]={"NoPull  ","PullDown","?Pull2? ","PullUp  "}; // 8
    tb_printf("%s ",drp[pull]);
    const char* drv[]={"S0S1","H0S1","S0H1","H0H1","D0S1","D0H1","S0D1","H0D1"};
    tb_printf("%s ",drv[drive]);
    const char* drs[]={"Sens.Disbl.","?Sense1?","SenseHigh","SenseLow"};
    tb_printf("%s\n",drs[sense]);
}


//----- toolfunktion tools_get_hex_byte(): Holt ein HexByte aus einem HEX String, Ret: <0 ERR oder 0..255 -----
int16_t tb_tools_get_hex_byte(uint8_t **ppu){
	uint8_t val = 0,n, *pu;
        pu=*ppu;
	for (uint8_t i = 0; i < 2; i++) {
		n = *pu++;
		if (n >= '0' && n <= '9') n -= '0';
		else if (n >= 'a' && n <= 'f') n = n- 'a' + 10;
		else if (n >= 'A' && n <= 'F') n = n- 'A' + 10;
		else return -1;	// Unknown
		val <<= 4;
		val += n;
	}
        *ppu=pu;
	return val;
}


//---------Watchdog, designed to run in unist of 10 sec-----------
#ifdef PLATFORM_NRF52
 #if NRFX_WDT_CONFIG_RELOAD_VALUE < 250000 // if used with JesFs
  #warning "Watchdog Interval <250 seconds not recommended"
 #endif
#endif

// ------Watchdog, triggeres 60usec before reset -----
static uint32_t tb_wd_dcounter=0;  // Max. Interval 2560 secs

static void tb_wdt_event_handler(void){
   _tb_bootcode+=0x10000000;        // WD increments Hx from HH-Guard - Special
   for(;;); // Wait2Die - No chance to prevent this
}

// ---- enable the watchdog only once! -----------
// on nRF52: WD has 8 channels, here only 1 used
uint32_t tb_watchdog_init(void){  // Will trigger after 10 sec
    ret_code_t ret;
    //Configure WDT. RELAOD_VALUE set in sdk_config.h
    // max. 131000 sec...
    ret = nrf_drv_wdt_init(NULL, tb_wdt_event_handler);
    APP_ERROR_CHECK(ret);
    ret = nrf_drv_wdt_channel_alloc(&tb_wd_m_channel_id);
    APP_ERROR_CHECK(ret);
    nrf_drv_wdt_enable();
    tb_wdt_enabled=true;
    return tb_wd_m_channel_id; // 0..7 on nRF52
}

// ------ is Watchdog init? ----------------------
inline bool tb_is_wd_init(void){
    return tb_wdt_enabled;
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
        __SEV();  // SetEvent
        __WFE();  // Clea at least last Event set by SEV
        __WFE();  // Sleep safe
      }
}
 
// ---- Unix-Timer. Must be called periodically to work, but at least twice per RTC-overflow (512..xx secs) ---
uint32_t tb_time_get(void){
   uint32_t rtc_secs, ux_secs;
   // RTC will overflow after 512..xx secs - Scale Ticks to seconds
   rtc_secs=app_timer_cnt_get() / (APP_TIMER_CLOCK_FREQ / (APP_TIMER_CONFIG_RTC_FREQUENCY+1)) ;
   if(rtc_secs<old_rtc_secs)  cnt_secs+=(((RTC_COUNTER_COUNTER_Msk)+1)/ (APP_TIMER_CLOCK_FREQ / (APP_TIMER_CONFIG_RTC_FREQUENCY+1))) ;
   old_rtc_secs=rtc_secs; // Save last seen rtc_secs
   ux_secs=cnt_secs+rtc_secs;
   // Store alsoto non-init RAM 
   _tb_novo[1]=ux_secs;
   _tb_novo[2]=~ux_secs;
   return ux_secs;
}

// Set time, regarding the timer
void tb_time_set(uint32_t new_secs){
  tb_time_get(); // update static vars
  cnt_secs=new_secs-old_rtc_secs;
   // Store alsoto non-init RAM 
   _tb_novo[1]=cnt_secs;
   _tb_novo[2]=~cnt_secs;
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

    if(tb_highpower_peripheral_uart_init_flag==false) return; // Not init...

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

    if(tb_highpower_peripheral_uart_init_flag==false) return 0; // Not init...

    res=!nrf_queue_is_empty(&tb_uart_queues_rxq);
    if(!res && tb_uart_error) return -1;
    return res;
}

// ---- get 1 char (0..255) (or -1 if nothing available)
int16_t tb_getc(void){
    ret_code_t ret;
    uint8_t c;

    if(tb_highpower_peripheral_uart_init_flag==false) return -1; // Not init...
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

    if(tb_highpower_peripheral_uart_init_flag==false) return 0; // Not init...
    if(tb_uart_error) return -1;

    for(;;){
        res=tb_kbhit();
        if(res>0){
            res=tb_getc();
            if(res<0) return res; // ERROR
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
        }else if(!res){
            if(max_wait_ms){
                if(!--max_wait_ms){
                  idx=0;  // Return empty String
                  break;
                }
            }
            tb_delay_ms(1);
        }else return res; // ERROR
    }
    input[idx]=0;   // Terminate String
    return idx;
}

//**
