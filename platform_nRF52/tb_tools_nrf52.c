/***************************************************************************************************************
* tb_tools_nrf52.c - Toolbox for UART, Unix-Time, .. 
*
* For platform nRF52 
*
* (C) joembedded.de
*
* Versions:   
* 1.0: 25.11.2019
* 1.1: 06.12.2019 added support for Low Power uninit, deep sleep current with RTC wakeup <= 2.7uA on nRF52840
* 1.1b: 09.12.2019 return empty string on timeout
* 1.2: 09.12.2019 changed init to presence of softdevice
* 1.3: 23.12.2019 USE_TBIO for LED (removed in 2.50)
* 1.4: 05.01.2020 tb_isxxx_init()
* 1.5: 06.01.2020 Button added
* 1.5b: 09.01.2020 tb_watchdog_init return type 
* 1.6: 19.01.2020 WDT optimised and GUARD-functions and Framing Errors
* 1.7: 25.02.2020 Added Defines for u-Blox NINA-B3 
* 2.0: 06.09.2020 Changed UART Driver to APP_UART for Multi-Use
* 2.01: 08.09.2020 Fixed Error in SDK17 (see tb_tools_nrf52.c-> 'SDK17')
* 2.02: 23.09.2020 Adapted to SDK17.0.2 (still Problem in 'nrf_drv_clock.c' -> search in this file 'SDK17')
* 2.11: 16.05.2021 removed 'board.h', small changes in PIN-Names
* 2.50: 02.07.2021 changed Platform PIN Setup
* 2.51: 10.07.2021 added 'tb_pins_nrf52.h'
* 2.54: 06.10.2021 added 'tb_get_runtime()' and 'tb_runtime2time()'
* 2.55: 06.10.2021 INFO: SDK17.1.0: There is still an Error on nrf_drv_clk.c ( -> search in this file 'SDK17')
* 2.56: 14.11.2021 added 'tb_putsl(char* pc)'
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

#include "app_timer.h" // uses RTC1

#include "nrf_drv_wdt.h"

#include "app_uart.h" // uses Driver Instanz from sdk_config.h (1 on 52840, 0 on 52832)
#include "app_error.h"
#include "app_util.h"

#include "tb_tools.h"

#if defined (UART_PRESENT)
#include "nrf_uart.h"
#endif
#if defined (UARTE_PRESENT)
#include "nrf_uarte.h"
#endif

// ------ Custom IO Setup - Pin Definitions ------------
#include "tb_pins_nrf52.h"

/******* SES has a default section for Non-Volatile RAM ***********/
/* _tb_novo is valid if _tb_novo[0]==RAM_MAGIC and _tb_novo[1] = ~_tb_novo[2];  
*  _tb_novo[1] holds the RTC, _tb_novo[3] the GUARD-Value
*  _tb_novo[4,5,6,7 frei] (4,5 for Energy-Management)
*/

#define RAM_MAGIC    (0xBADC0FFEUL) 
uint32_t _tb_novo[8] __attribute__ ((section(".non_init")));
uint32_t _tb_bootcode_backup; // Holds initial Bootcode

// ---- local defines --------------

// ---------- locals uart --------------------
#define UART_TX_BUF_SIZE 256      /* Also as default buffers for UART */
#define UART_RX_BUF_SIZE 256    /* Also as default buffers for UART */

static uint8_t  _tb_app_uart_def_rx_buf[UART_RX_BUF_SIZE];     
static uint8_t  _tb_app_uart_def_tx_buf[UART_TX_BUF_SIZE];     

static const app_uart_comm_params_t _tb_app_uart_def_comm_params = {
          TB_RX_PIN,
          TB_TX_PIN,
          UART_PIN_DISCONNECTED  /*RTS_PIN_NUMBER*/,   
          UART_PIN_DISCONNECTED  /*CTS_PIN_NUMBER*/,
          /*APP_UART_FLOW_CONTROL_ENABLED */ APP_UART_FLOW_CONTROL_DISABLED,
          false,      // Not Use Parity
#if defined (UARTE_PRESENT)
          NRF_UARTE_BAUDRATE_115200
#else
          NRF_UART_BAUDRATE_115200
#endif
};
static app_uart_buffers_t _tb_app_uart_buffers;
static int32_t _tb_app_timout_ms;

static uint8_t _tb_app_uart_errorcode; // 1 res. for data, 16: Fifo OVF, ...
static int16_t _tb_app_uart_peekchar; // -1: nothing or 0..255
static bool _tb_uart_init_flag=false; 

#define TB_SIZE_LINE_OUT  120  // 1 terminal line working buffer (opt. long filenames to print)
static char _tb_app_uart_line_out[TB_SIZE_LINE_OUT];


#if !APP_TIMER_KEEPS_RTC_ACTIVE
 #error "Need APP_TIMER_KEEPS_RTC_ACTIVE enabled"
#endif

// ---- locals Watcdog ----
static nrf_drv_wdt_channel_id tb_wd_m_channel_id;
static bool tb_wdt_enabled=false;


// --- locals timestamp ----
static uint32_t old_rtc_secs=0; // counts 0...511, Sec-fraction of RTC
static uint32_t cnt_secs=0;  // To compare
static uint32_t ux_run_delta=0; // Difference Runtime to Unixtime

static bool tb_basic_init_flag=false; // Always ON, only init once

//----------- UART helpers ------------
static void _tb_app_uart_error_handle(app_uart_evt_t * p_event){
/* Required, but Dummy is OK too, BREAK gibt 8+4, also ERR 28.d -> 0001 1100 */
    if (p_event->evt_type == APP_UART_COMMUNICATION_ERROR){
        // ref. NRF52840 Datasheeet: Bits: OVERRUN:1 PARITY:2 FRAMING:4: BREAK:8
        // APP_ERROR_HANDLER(p_event->data.error_communication);
        _tb_app_uart_errorcode = 16+(p_event->data.error_communication&0xF);
    }else if (p_event->evt_type == APP_UART_FIFO_ERROR){
        // APP_ERROR_HANDLER(p_event->data.error_code);
        _tb_app_uart_errorcode = 2;  // 2: Comm Error
    }
}

// tb_putc(): Wait if not ready. With Timout from init
int16_t tb_putc(char c){
    int32_t h=_tb_app_timout_ms;
    if(_tb_uart_init_flag==false) return -1; // Not init...

    for(;;){
        if(app_uart_put(c) != NRF_ERROR_NO_MEM) return 0; // NRF_ERROR_NO_MEM (4) -> No ERROR, just wait.
        if(!(h--)) return -2; 
        tb_delay_ms(1);  
    }
}

// ---- Test: return 0:Nothing available, -1: ERROR, 1: Char available
int16_t tb_kbhit(void){
  uint8_t cr;
  if(_tb_uart_init_flag==false) return 0; // Not init = nothing
  if(_tb_app_uart_errorcode) return -1; // Error
  if(app_uart_get(&cr) == NRF_SUCCESS){ // nothing: NRF_ERROR_NOT_FOUND
    _tb_app_uart_peekchar=(int16_t)cr;
    return 1; // Ready
  }
  return 0; // nothing
}
// ---- get 1 char (0..255) (or -1 if nothing available, or < -1 on Framing Errors)
// Framing Errors (-2...-xx) see _tb_app_uart_error_handle()
int16_t tb_getc(void){
  uint8_t cr;
  int16_t ret;
  if(_tb_uart_init_flag==false) return -1; // Not init...
  if(_tb_app_uart_errorcode){
    cr=_tb_app_uart_errorcode;   // Error: Report one time
    _tb_app_uart_errorcode=0;
    return -(int16_t)cr;  // -2..
  }else if(_tb_app_uart_peekchar>=0){
    ret = _tb_app_uart_peekchar;
    _tb_app_uart_peekchar=-1;
    return ret;
  }else if(app_uart_get(&cr) == NRF_SUCCESS){ // nothing: NRF_ERROR_NOT_FOUND
      return (int16_t) cr;
  }else return -1;  // Probably NOT_FOUND or sth. else
}

// tb_putsl - as puts, but without CR/NL
void tb_putsl(char* pc){
    if(_tb_uart_init_flag==false) return; // Not init...
    while(*pc){
      if(tb_putc(*pc++)) break; 
    }
}

// tb_printf(): printf() to toolbox uart. Wait if busy
void tb_printf(char* fmt, ...){
    ret_code_t ret;
    size_t ulen;
    uint8_t *pl;
    va_list argptr;
    va_start(argptr, fmt);

    if(_tb_uart_init_flag==false) return; // Not init...

    ulen=vsnprintf((char*)_tb_app_uart_line_out, TB_SIZE_LINE_OUT, fmt, argptr);  // vsn: limit!
    va_end(argptr);
    // vsnprintf() limits output to given size, but might return more.
    if(ulen>TB_SIZE_LINE_OUT-1) ulen=TB_SIZE_LINE_OUT-1;
    pl=_tb_app_uart_line_out;
    while(ulen--){
      if(tb_putc(*pl++)) break; 
    }
}

// Get String with Timeout (if >0) in msec of infinite (Timout 0), No echo (max_uart_in without trailing \0!)
int16_t tb_gets(char* input, int16_t max_uart_in, uint16_t max_wait_ms, uint8_t echo){
    int16_t idx=0;
    int16_t res;
    char c;
    max_uart_in--;  // Reserve 1 Byte for End-0

    if(_tb_uart_init_flag==false) return 0; // Not init...

    for(;;){
        res=tb_getc();
        if(res>=0){ 
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
            } // else ignore
        }else if(res==-1){  // No Data
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

/* if pcomm_params == NULL: Use default UART
* if prx/tx_buf== NULL: use default Buffers
* timeout: -1: ca. 50 days...
*/
int16_t tb_uart_init(void *pcomm_params, 
        uint8_t *prx_buf, uint16_t rx_buf_size,
        uint8_t *ptx_buf, uint16_t tx_buf_size,
        int32_t timeout_ms){

    if(_tb_uart_init_flag==true) return -1; // Already in USE

    uint32_t err_code;
    _tb_app_uart_errorcode=0;
    _tb_app_uart_peekchar=-1;
                                                    
    // Check for Defaults
    if(pcomm_params==NULL) pcomm_params=(void*)&_tb_app_uart_def_comm_params;
    if(prx_buf==NULL){
      prx_buf = _tb_app_uart_def_rx_buf;
      rx_buf_size = sizeof(_tb_app_uart_def_rx_buf);
    }
    if(ptx_buf==NULL){
      ptx_buf = _tb_app_uart_def_tx_buf;
      tx_buf_size = sizeof(_tb_app_uart_def_tx_buf);
    }

    _tb_app_uart_buffers.rx_buf      = prx_buf;      
    _tb_app_uart_buffers.rx_buf_size = rx_buf_size;  
    _tb_app_uart_buffers.tx_buf      = ptx_buf;      
    _tb_app_uart_buffers.tx_buf_size = tx_buf_size; 
    
    _tb_app_timout_ms = timeout_ms; // 0: Wait "quasi forever"

    _tb_uart_init_flag=true;

    err_code = app_uart_init((const app_uart_comm_params_t*)pcomm_params, &_tb_app_uart_buffers, _tb_app_uart_error_handle, APP_IRQ_PRIORITY_LOWEST); 
    if(err_code == NRF_SUCCESS) return 0;
    else return -2;
}

int16_t tb_uart_uninit(void){
     uint32_t err_code;
     if(_tb_uart_init_flag == false) return -1; // Not init
     _tb_uart_init_flag = false;
     err_code = app_uart_close();
     if(err_code == NRF_SUCCESS) return 0;
     else return -2;
}

bool tb_is_uart_init(void){
  return _tb_uart_init_flag;
}

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

        _tb_novo[4]=0;  // Reserviert fuer User Energycounter H (measure.c)
        _tb_novo[5]=0;  // Reserviert fuer User Energycounter L (measure.c)
        _tb_novo[6]=0;  // noch FREI
        _tb_novo[7]=0;  // noch FREI

        _tb_bootcode_backup=0;  // Invalid
    }else{  // Valid Time
      _tb_bootcode_backup = _tb_bootcode;   // Save Bootcode
      tb_time_set(_tb_novo[1]);
    }
    _tb_bootcode = 0;                     // Bootcode frei fuer neues

    nrf_gpio_cfg_output(TB_LED0);  // We use Software CS - Uses PIN Decode
    nrf_gpio_pin_set(TB_LED0);    // LED OFF Active LOW
#ifdef TB_BUT0
    nrf_gpio_cfg_input(TB_BUT0,GPIO_PIN_CNF_PULL_Pullup); // Std. Button on all Boards
#endif
    //Normalerweise RX-Port OFF
    nrf_gpio_cfg_input(TB_RX_PIN,GPIO_PIN_CNF_PULL_Pulldown); 

#ifndef SOFTDEVICE_PRESENT
      // Not required for Soft-Device
      ret = nrf_drv_power_init(NULL);
      APP_ERROR_CHECK(ret);
/* SDK17: ERROR in in "nrf_drv_clock.c -> nrf_drv_clock_init()": 
*  Reported to NORDIC as 'Case ID: 277657'
*  Remove:   "if (nrf_wdt_started()) m_clock_cb.lfclk_on = true;" 
   
   Remove or disable THIS part: nrf_drv_clock.c (arround line 196-202 in SDK17):
   // if (nrf_wdt_started())
   // {
   //     m_clock_cb.lfclk_on = true;
   // }


*/
      ret = nrf_drv_clock_init();
      APP_ERROR_CHECK(ret);
      nrf_drv_clock_lfclk_request(NULL);
#endif

      ret = app_timer_init(); // Baut sich eine Event-FIFO, Timer wird APP_TIMER_CONFIG auf 32k..1kHz gesetzt
      APP_ERROR_CHECK(ret);
      tb_basic_init_flag=true;
      /* Minimum Required Basic Block ---END--- */
    }

    ret = tb_uart_init(NULL, NULL, 0, NULL, 0, -1);  // Use default UART
    APP_ERROR_CHECK(ret);
}
// ------ uninit all higher power peripherals, currently only UART  --------------------
void tb_uninit(void){
    tb_uart_uninit();
    //Special: Default UART to LOW if OFF
    nrf_gpio_cfg_input(TB_RX_PIN,GPIO_PIN_CNF_PULL_Pulldown); 
}


// Get Startup-Bootcode
inline uint32_t tb_get_bootcode(bool ok){
    uint32_t res = _tb_bootcode_backup;
    if(ok) _tb_bootcode_backup=1; // Reset Bootcode to known state Starts with 1
    return res;
}


// ------ board support pakage or direct I/O -----
inline void tb_board_led_on(uint8_t idx){
    nrf_gpio_pin_clear(TB_LED0);    // LED OFF Active LOW

}
inline void tb_board_led_off(uint8_t idx){
    nrf_gpio_pin_set(TB_LED0);    // LED OFF Active LOW
}
inline void tb_board_led_invert(uint8_t idx){
    nrf_gpio_pin_toggle(TB_LED0);    // LED OFF Active LOW
}
// Get the Button State (optionally present)
inline bool tb_board_button_state(uint8_t idx){
#ifdef TB_BUT0
    return nrf_gpio_pin_read(TB_BUT0);
#else
    return 1; // Assume NOT PRESSED if not pressent
#endif
}

// ------- System Reset (Will keep Watchdog active, if enabled) ------------
void tb_system_reset(void){
     NVIC_SystemReset();
}

// ShowPin - Debug-Function!: pin_number=NRF_GPIO_PIN_MAP(port, pin) // == ((pin_0_31)+port_0_1*32)
// Will raise FATAL, if pin not existing...
void tb_dbg_pinview(uint32_t pin_number){
    nrf_gpio_pin_dir_t   dir;
    nrf_gpio_pin_input_t input;
    nrf_gpio_pin_pull_t  pull;
    nrf_gpio_pin_drive_t drive;
    nrf_gpio_pin_sense_t sense;

    tb_printf("P:%u.%02u: ",pin_number>>5,pin_number&31); 
    NRF_GPIO_Type * reg = nrf_gpio_pin_port_decode(&pin_number); // Modifies PIN-Number
    uint32_t pr=reg->PIN_CNF[pin_number];

    dir=(pr>>GPIO_PIN_CNF_DIR_Pos)&1; // 0: Input, 1: Output
    input=(pr>>GPIO_PIN_CNF_INPUT_Pos)&1; // 0: Connect, 1: Disconnect
    pull=(pr>>GPIO_PIN_CNF_PULL_Pos)&3; // 0: Disabled, 1: Pulldown, 3(!): Pullup
    drive=(pr>>GPIO_PIN_CNF_DRIVE_Pos)&7; // "S0S1","H0S1","S0H1","H0H1","D0S1","D0H1","S0D1","H0D1"
    sense=(pr>>GPIO_PIN_CNF_SENSE_Pos)&3; // 0: Disabled, 2(!):Sense_High 3(!):Sens_low

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
static bool _tb_expired_flag;
static void tb_timeout_handler(void * p_context){
      _tb_expired_flag=true;
}

void tb_delay_ms(uint32_t msec){   

#if 0
#warning "NRF_DELAY SDK16/SDK17-Problem: LFtimer not init! (-> search in this file 'SDK17')"
nrf_delay_ms(msec); 
return;
#endif

      uint32_t ticks = APP_TIMER_TICKS(msec);
      if(ticks<APP_TIMER_MIN_TIMEOUT_TICKS) ticks=APP_TIMER_MIN_TIMEOUT_TICKS;  // Minimum 5 ticks 
      ret_code_t ret = app_timer_create(&tb_delaytimer, APP_TIMER_MODE_SINGLE_SHOT,tb_timeout_handler);
      if (ret != NRF_SUCCESS) return;
      _tb_expired_flag=false;
      app_timer_start(tb_delaytimer, ticks, NULL);
      while(!_tb_expired_flag){
        __SEV();  // SetEvent
        __WFE();  // Clear at least last Event set by SEV
        __WFE();  // Sleep safe
      }
}
 
// ---- Unix-Timer. Must be called periodically to work, but at least twice per RTC-overflow (512..xx secs) ---
// New in V2.5: 2 Systemtimers: RUNTIME(sec) and UNIX(secs)
// Idea: runtime always starts at 0 and altime increases, whereas UNIX time could be set by user.
uint32_t tb_get_runtime(void){  // This timer ALWAYS increments an is only set on Reset to 0
   uint32_t rtc_secs, run_secs;
   // RTC will overflow after 512..xx secs - Scale Ticks to seconds
   rtc_secs=app_timer_cnt_get() / (APP_TIMER_CLOCK_FREQ / (APP_TIMER_CONFIG_RTC_FREQUENCY+1)) ;
   if(rtc_secs<old_rtc_secs)  cnt_secs+=(((RTC_COUNTER_COUNTER_Msk)+1)/ (APP_TIMER_CLOCK_FREQ / (APP_TIMER_CONFIG_RTC_FREQUENCY+1))) ;
   old_rtc_secs=rtc_secs; // Save last seen rtc_secs
   run_secs=cnt_secs+rtc_secs;
   return run_secs;
}
// Runtime seconds Timestamp to Unix Timestamp
uint32_t tb_runtime2time(uint32_t run_secs){
   uint32_t ux_secs;
   ux_secs = run_secs + ux_run_delta;
   return ux_secs;
}

uint32_t tb_time_get(void){ // Last Unix Timestamp is saved in NV memory
   uint32_t ux_secs;
   ux_secs = tb_get_runtime() + ux_run_delta;
   // Store absolute time also to non-init RAM 
   _tb_novo[1]=ux_secs;
   _tb_novo[2]=~ux_secs;
   return ux_secs;
}

// Set time, regarding the timer
void tb_time_set(uint32_t new_secs){
   ux_run_delta = new_secs - tb_get_runtime();
   // Store also to non-init RAM 
   _tb_novo[1]=new_secs;
   _tb_novo[2]=~new_secs;
}

// ----- clock ticks functions ---------------------
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
//**
