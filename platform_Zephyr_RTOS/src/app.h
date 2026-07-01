/*
 * Public application interface for the MAIN-Shell skeleton.
 *
 * External modules should use this header for shared runtime state,
 * text command dispatch, logging, LED helpers and console access.
 */

#ifndef APP_H
#define APP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Multiplexer for tb_log(). */
#define TB_LOG_NONE 0x00U
#define TB_LOG_CON 0x01U

/* Console always present, BLE and others if possible*/
#if defined(CONFIG_BLUETOOTH_UART)
#define TB_LOG_BLE 0x02U
#endif

/* App-specific errors are below the negative errno range
 * used by Zephyr (__ELASTERROR in errno.h). */
#define TB_ERR_BASE 2000
#define TB_CMD_UNKNOWN (-TB_ERR_BASE)
#define TB_CMD_INVALID_PARAM (-TB_ERR_BASE - 1)
#define TB_CMD_TIMEOUT (-TB_ERR_BASE - 2)

typedef union {
	uint8_t bytes[8];
	uint64_t value;
} tb_device_id_t;

typedef int16_t (*tb_command_handler_t)(uint8_t log_flags, char *args);

typedef struct {
	const char *const prefix;
	tb_command_handler_t handler;
	const char *const hint;
} tb_command_entry_t;

extern tb_device_id_t tb_device_id;

// periodically updated for global consistent time values per periodic slot
extern uint32_t tb_now_runtime_sec;

void tb_led_on(void);
void tb_led_off(void);
void tb_led_set(uint8_t state);

void tb_debug_led_on(void);
void tb_debug_led_off(void);
void tb_debug_led_set(uint8_t state);

void tb_log(uint8_t flags, const char *format, ...);

int16_t tb_con_kbhit(void);
int16_t tb_con_getc(void);
int16_t tb_con_gets(char *input, int16_t max_len, uint16_t timeout_ms, uint8_t echo);

void tb_unix_time_set(uint32_t unix_sec); // valid until year 2099
uint32_t tb_unix_time_get(void);
uint32_t tb_runtime_now_get(void);

int16_t tb_sys_command(uint8_t log_flags, char *cmd);
char *tb_match_str_prefix(const char *prefix, char *cmd);
int16_t tb_dispatch_command(uint8_t log_flags, char *cmd, const tb_command_entry_t *commands,
			    size_t num_commands);
void tb_list_commands(uint8_t log_flags, const tb_command_entry_t *commands, int16_t num_commands);

#ifdef __cplusplus
}
#endif

#endif /* APP_H */
