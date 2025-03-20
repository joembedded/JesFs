/*********************************************************************
* tb_tool.h - Toolbox for UART, Unix-Time, .. 
*
* (C) joembedded.de
*
*********************************************************************/

void tb_init(void);
void tb_uninit(void);

void tb_board_led_on(uint8_t idx); // LED mapper
void tb_board_led_off(uint8_t idx);
void tb_board_led_invert(uint8_t idx);
bool tb_board_button_state(uint8_t idx); // Button mapper

void tb_system_reset(void); // system reset

int16_t tb_tools_get_hex_byte(uint8_t **ppu);


uint32_t tb_watchdog_init(void);  // Init Watchdog
void tb_watchdog_feed(uint32_t feed_ticks); // feed_ticks: currently no function, but >0
bool tb_is_wd_init(void);

void tb_delay_ms(uint32_t msec); // --- Lower power delay than nrf_delay

uint32_t tb_get_runtime(void);   // This timer ALWAYS increments sec and is only set on Reset
uint32_t tb_get_ticks(void);    // System dependant for precise short periods (<1h)
uint32_t tb_deltaticks_to_ms(uint32_t t0, uint32_t t1);
uint32_t tb_runtime2time(uint32_t run_secs);
uint32_t tb_time2runtime(uint32_t ux_secs);

uint32_t tb_time_get(void); // ---- Unix-Timer. Must be called periodically to work, but at least twice per RTC-overflow ---
void tb_time_set(uint32_t new_secs); // Set time, regarding the timer

// --UART--
void tb_printf(char* fmt, ...); // tb_printf(): printf() to toolbox uart. Wait if busy
void tb_putsl(char* pc); // // tb_putsl - as puts, but without CR/NL
int16_t tb_putc(char c); // tb_putc(): Wait if not ready, may return -2
int16_t tb_kbhit(void); // ---- Input functions 0: Nothing available
int16_t tb_getc(void); // ---- get 1 char (0..255) (or -1 if nothing available)
// Get String with Timout (if >0) in msec of infinite (Timout 0), No echo (max_uart_in without trailing \0!)
int16_t tb_gets(char* input, int16_t max_uart_in, uint16_t max_wait_ms, uint8_t echo);
bool tb_is_uart_init(void);
bool tb_is_default_uart(void);
// pcomm_params: (const app_uart_comm_params_t*):
int16_t tb_uart_init(void *pcomm_params, uint8_t *prx_buf, uint16_t rx_buf_size, uint8_t *ptx_buf, uint16_t tx_buf_size, int32_t timout_ms);
int16_t tb_uart_uninit(void);


#if defined(NRF52) || defined(PLATFORM_NRF52)
// Debug-Fkt to show Pin Config
void tb_dbg_pinview(uint32_t pin_number);
// Novo contains 4 uint32 to keep time and track software for post-mortem
// GUARD: Each Sourcefile gets an uniqe ID (0..255), Guard write ID and LINE to bootcode
extern uint32_t _tb_novo[8]; //  __attribute__ ((section(".non_init")));

uint32_t tb_get_bootcode(bool ok);

#define  _tb_bootcode           _tb_novo[3] // Bootcode for post-mortem analysis
// 2 Type of Guards: Absolute and only for DEBUG see mcs_novo_init() for details
#define GUARD(x)    _tb_bootcode=(((x)<<16) | __LINE__ )
#ifdef DEBUG
 #define DBG_GUARD(x)  _tb_bootcode=( ((x)<<16)|__LINE__)
#else
 #define DBG_GUARD(x)    // Nothing
#endif
#endif

//***

