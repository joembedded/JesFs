/*
 * MAIN-JESFS-Shell-Skeleton
 * (C) JoEmbedded.de
 *
 */

#include "app.h"

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/uart.h>

#include <zephyr/kernel.h>

#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>

#include <zephyr/sys/util.h>

#if defined(CONFIG_JESFS_SHELL)
#include "jesfs/jesfs_shell.h"
#endif

#if defined(CONFIG_BLUETOOTH_UART)
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#endif

#define TB_LOG_MAX_LEN 100
#define TB_CMD_MAX_LEN 100

#define LED0_NODE DT_ALIAS(led0)

/* Public runtime state for later modules. - globales Herz der Zeit */
uint32_t tb_now_runtime_sec;
/* Public Device_ID (8-Bytes MAC)*/
tb_device_id_t tb_device_id;

/* Polling status: ':': idle. Later e.g. '.', 'o', '*' for BLE states. */
static char tb_status_char = ':';

enum tb_console_state {
	TB_CON_RESET = 0,
	TB_CON_POWERDOWN,
	TB_CON_READY_UNCHECKED,
	TB_CON_READY_CHECKED,
};

static uint8_t tb_con_state = TB_CON_RESET;
static int16_t tb_con_rx_lookahead = -1;

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec debug_led = GPIO_DT_SPEC_GET(DT_NODELABEL(led_dbg), gpios);
static const struct gpio_dt_spec rx_sense_pin =
	GPIO_DT_SPEC_GET(DT_NODELABEL(rx_detect_pin), gpios);
static const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart_con));

BUILD_ASSERT(DT_SAME_NODE(DT_CHOSEN(zephyr_console), DT_NODELABEL(uart_con)),
	     "zephyr,console must point to uart_con");

/******* A default section for Non-Volatile RAM ***********/
#define RAM_MAGIC (0xBADC0FFEUL)
struct tb_novo_state {
	uint32_t magic;		       // RAM_MAGIC
	uint32_t unix_delta_sec;       // offset zu Runtime fuer UNIX-Zeit bis 2099
	uint32_t last_runtime_sec;     // zusammen geben sie unix_sec
	uint64_t energy_usage_mA_mSec; // kumulierte Energie in mA * mSec
	uint32_t magic_inv;	       // ~RAM_MAGIC
};

/* Alignment is deliberate; some architectures dislike unaligned noinit objects. */
static struct tb_novo_state __noinit __aligned(4) tb_novo;

static void tb_fatal_error(const char *msg);
static void tb_init_device_id(void);
static void tb_init_system_peripherals(void);
static uint8_t tb_con_is_connected(void);
static void tb_con_powerdown(void);
static void tb_con_wake(void);
static int16_t tb_uart_poll_task(void);
static int16_t tb_handle_sys_command(uint8_t log_flags, char *args);
static int16_t tb_handle_help_command(uint8_t log_flags, char *args);

// Top-level command table for the debug console.
static const tb_command_entry_t system_commands[] = {
// Sub-shell commands.
#if defined(CONFIG_JESFS_SHELL)
	{"file", jesfs_shell, "<subcommand> (JESFS shell)"},
#endif

	// System commands.
	{"sys", tb_handle_sys_command, "time [unix_sec] | info"},
	{"help", tb_handle_help_command, NULL},
};

void tb_led_on(void) { gpio_pin_set_raw(led0.port, led0.pin, 1); }

void tb_led_off(void) { gpio_pin_set_raw(led0.port, led0.pin, 0); }

void tb_led_set(uint8_t state) { gpio_pin_set_raw(led0.port, led0.pin, state); }

void tb_debug_led_on(void) { gpio_pin_set_raw(debug_led.port, debug_led.pin, 1); }

void tb_debug_led_off(void) { gpio_pin_set_raw(debug_led.port, debug_led.pin, 0); }

void tb_debug_led_set(uint8_t state) { gpio_pin_set_raw(debug_led.port, debug_led.pin, state); }

void tb_log(uint8_t flags, const char *format, ...)
{
	if (flags == TB_LOG_NONE) {
		return;
	}

	char buf[TB_LOG_MAX_LEN];
	va_list args;

	va_start(args, format);
	vsnprintk(buf, sizeof(buf), format, args);
	va_end(args);

	if ((flags & TB_LOG_CON) && (tb_con_state >= TB_CON_READY_UNCHECKED)) {
		printk("%s", buf);
	}

#if defined(CONFIG_BLUETOOTH_UART)
	if ((flags & TB_LOG_BLE) && notifications_enabled && current_conn) {
		tb_blecon(buf);
	}
#endif
}

int16_t tb_con_kbhit(void)
{
	unsigned char c;

	if (tb_con_state <= TB_CON_POWERDOWN) {
		return -2;
	}

	if (tb_con_rx_lookahead >= 0) {
		return tb_con_rx_lookahead;
	}

	if (uart_poll_in(uart_dev, &c) == 0) {
		tb_con_rx_lookahead = (int16_t)c;
		return tb_con_rx_lookahead;
	}

	return 0;
}

int16_t tb_con_getc(void)
{
	unsigned char c;

	if (tb_con_state <= TB_CON_POWERDOWN) {
		return -ESHUTDOWN;
	}

	if (tb_con_rx_lookahead >= 0) {
		c = (unsigned char)tb_con_rx_lookahead;
		tb_con_rx_lookahead = -1;
		return (int16_t)c;
	}

	if (uart_poll_in(uart_dev, &c) == 0) {
		return (int16_t)c;
	}

	return -ENODATA;
}

/*
 * Liest eine ASCII-Zeile von der Debug-Konsole.
 *
 * timeout_ms == 0 wartet unbegrenzt. max_len enthaelt das abschliessende '\0'.
 */
int16_t tb_con_gets(char *input, int16_t max_len, uint16_t timeout_ms, uint8_t echo)
{
	int16_t idx = 0;
	int16_t res;
	uint16_t wait_ticks = timeout_ms * 2U;

	max_len--;

	while (true) {
		// theoretisch sinnig, sonst Zeit evtl. timeout_ms falsch bei Reset, aber +/-20 sec
		// sind OK tb_time_checkpoint();
		res = tb_con_getc();

		if (res >= 0) {
			char c = (char)res;

			if (c == '\n' || c == '\r') {
				printk("\n");
				res = idx;

				if (tb_con_state < TB_CON_READY_CHECKED) {
					tb_con_state = TB_CON_READY_CHECKED;
				}

				break;
			}

			if (c == '\b') {
				if (idx > 0) {
					idx--;
					if (echo) {
						printk("%c %c", '\b', '\b');
					}
				}
			} else if (c >= ' ' && c < 128 && idx < max_len) {
				input[idx++] = c;
				if (echo) {
					printk("%c", c);
				}
			}
		} else if (res == -ENODATA) {
			if (wait_ticks != 0U && --wait_ticks == 0U) {
				idx = 0;
				res = TB_CMD_TIMEOUT;
				break;
			}

			if ((wait_ticks & 511U) == 0U) {
				tb_led_on();
			} else if ((wait_ticks & 63U) == 0U) {
				tb_led_off();
			}

			k_usleep(500);
		} else {
			break;
		}
	}

	input[idx] = '\0';
	tb_led_off();

	return res;
}

int16_t tb_dispatch_command(uint8_t log_flags, char *cmd, const tb_command_entry_t *commands,
			    size_t num_commands)
{
	int16_t res = TB_CMD_UNKNOWN;
	for (size_t i = 0; i < num_commands; i++) {
		char *args = tb_match_str_prefix(commands[i].prefix, cmd);

		if (args == NULL) {
			continue;
		}

		while (*args == ' ') {
			args++;
		}

		res = commands[i].handler(log_flags, args);
		break;
	}
	return res;
}

/* Generelle Einsprungroutine fuer Textbasierte Kommandos, von UART, BLE, ...*/
int16_t tb_sys_command(uint8_t log_flags, char *cmd)
{
	uint32_t start_ms = (uint32_t)k_uptime_get();
	int16_t res;
	if (*cmd != '\0') {
		res = tb_dispatch_command(log_flags, cmd, system_commands,
					  ARRAY_SIZE(system_commands));
	} else {
		res = 0; // Leerzeile
	}
	uint32_t dur_ms = (uint32_t)(k_uptime_get() - start_ms);

	if (res == 0) {
		if (dur_ms > 1U) {
			tb_log(log_flags, "OK (Run: %u ms)\n", dur_ms);
		} else {
			tb_log(log_flags, "OK\n");
		}

		return 0;
	}

	if (dur_ms > 1U) {
		tb_log(log_flags, "ERROR:%d (Run: %u ms)\n", res, dur_ms);
	} else {
		tb_log(log_flags, "ERROR:%d\n", res);
	}

	return res;
}

char *tb_match_str_prefix(const char *prefix, char *cmd)
{
	while (*prefix != '\0') {
		if (*prefix++ != *cmd++) {
			return NULL;
		}
	}

	return cmd;
}

void tb_list_commands(uint8_t log_flags, const tb_command_entry_t *commands, int16_t num_commands)
{
	for (int16_t i = 0; i < num_commands; i++) {
		if (commands[i].hint != NULL) {
			tb_log(log_flags, "  %s  -  %s\n", commands[i].prefix, commands[i].hint);
		} else {
			tb_log(log_flags, "  %s\n", commands[i].prefix);
		}
	}
}

// Listet alle Kommandos auf
static int16_t tb_handle_help_command(uint8_t log_flags, char *args)
{
	(void)args;

	tb_log(log_flags, "Commands:\n");
	tb_list_commands(log_flags, system_commands, ARRAY_SIZE(system_commands));

	return 0;
}

// "sys"
static int16_t tb_handle_sys_command(uint8_t log_flags, char *args)
{
	char *args2 = tb_match_str_prefix("time", args);
	if (args2 != NULL) {
		while (*args2 == ' ') {
			args2++;
		}
		// Unix-Timestamp, e.g. 1782563079 = 2025-02-24 15:04:39 UTC
		uint32_t nsec = strtoul(args2, NULL, 10);
		if (nsec) {
			/* Optional: Plausibilitaetscheck auf Datum/Uhrzeit. */
			tb_unix_time_set(nsec);
			tb_log(log_flags, "Unix Time set to:%u sec\n", nsec);
		}

		uint32_t usec = tb_unix_time_get();
		tb_log(log_flags, "Unix Time:%u sec\n", usec);
		tb_log(log_flags, "Runtime:%u sec\n", tb_now_runtime_sec);
		return 0;
	}

	if (strcmp(args, "info") == 0) {
		tb_log(log_flags, "MAC:%016llX\n", tb_device_id.value);
		return 0;
	}

	return TB_CMD_INVALID_PARAM;
}

static uint8_t tb_con_is_connected(void) { return (uint8_t)gpio_pin_get_dt(&rx_sense_pin); }

static void tb_con_powerdown(void)
{
	if (tb_con_state < TB_CON_READY_UNCHECKED) {
		return;
	}

	printk("[UART OFF]\n");
	k_msleep(10);

	int err = pm_device_runtime_put(uart_dev);

	if (err == 0 || err == -EALREADY) {
		tb_con_state = TB_CON_POWERDOWN;
	}
}

static void tb_con_wake(void)
{
	if (tb_con_state > TB_CON_POWERDOWN) {
		return;
	}

	int err = pm_device_runtime_get(uart_dev);

	if (err == 0 && device_is_ready(uart_dev)) {
		tb_con_rx_lookahead = -1;

		if (tb_con_state != TB_CON_RESET) {
			printk("[UART ON]\n");
		}

		tb_con_state = TB_CON_READY_UNCHECKED;
	}
}

/*
 * Im unchecked Zustand wird der Prompt erst nach CR/LF aktiv. Danach startet
 * jedes druckbare Zeichen direkt den Prompt und bleibt im Lookahead-Puffer.
 */
static int16_t tb_uart_poll_task(void)
{
	printk("%c", tb_status_char);

	if (tb_con_state < TB_CON_READY_CHECKED) {
		int16_t last_ch = -1;

		while (true) {
			int16_t ch = tb_con_kbhit();

			if (ch <= 0) {
				break;
			}

			last_ch = ch;
			(void)tb_con_getc();
		}

		if (last_ch != '\n' && last_ch != '\r') {
			return 0;
		}
	} else {
		int16_t ch = tb_con_kbhit();

		if (ch != '\n' && ch != '\r' && (ch < ' ' || ch > '~')) {
			return 0;
		}
	}

	char cmdbuf[TB_CMD_MAX_LEN];
	uint16_t timeout_ms;

	if (tb_con_state == TB_CON_READY_CHECKED) {
		printk("\n>");
		timeout_ms = 20000;
	} else {
		printk("\n!>");
		timeout_ms = 1000;
	}

	int16_t cmd_len = tb_con_gets(cmdbuf, sizeof(cmdbuf), timeout_ms, 1);

	if (cmd_len >= 0) {
		return tb_sys_command(TB_LOG_CON, cmdbuf);
	}

	tb_log(TB_LOG_CON, "\nERROR:%d\n", cmd_len);

	return cmd_len;
}

static void tb_fatal_error(const char *msg)
{
	printk("FATAL:%s\n", msg);

	while (true) {
		k_sleep(K_SECONDS(1));
	}
}

static void tb_init_device_id(void)
{
	hwinfo_get_device_id(tb_device_id.bytes, sizeof(tb_device_id.bytes));
}

static void tb_init_system_peripherals(void)
{
	if (!gpio_is_ready_dt(&led0) || gpio_pin_configure_dt(&led0, GPIO_OUTPUT_HIGH) < 0) {
		tb_fatal_error("LED_GPIO");
	}

	if (!gpio_is_ready_dt(&debug_led) ||
	    gpio_pin_configure_dt(&debug_led, GPIO_OUTPUT_HIGH) < 0) {
		tb_fatal_error("DEBUG_LED_GPIO");
	}

	if (!device_is_ready(uart_dev) || !gpio_is_ready_dt(&rx_sense_pin)) {
		tb_fatal_error("UART_INIT");
	}

	gpio_pin_configure_dt(&rx_sense_pin, GPIO_INPUT);

	if (tb_con_is_connected()) {
		tb_con_wake();
	} else {
		tb_con_state = TB_CON_POWERDOWN;
	}

	tb_init_device_id();
}

uint32_t tb_runtime_now_get(void)
{
	uint32_t rts_sec = (uint32_t)(k_uptime_get() / 1000);
	tb_novo.last_runtime_sec = rts_sec;
	return rts_sec;
}

void tb_unix_time_set(uint32_t unix_sec)
{
	uint32_t rts_sec = tb_runtime_now_get();

	tb_novo.unix_delta_sec = unix_sec - rts_sec;
	tb_now_runtime_sec = rts_sec;
	tb_novo.last_runtime_sec = rts_sec;
}

uint32_t tb_unix_time_get(void)
{
	uint32_t runtime_sec = tb_runtime_now_get();

	tb_now_runtime_sec = runtime_sec;
	tb_novo.last_runtime_sec = runtime_sec;

	return runtime_sec + tb_novo.unix_delta_sec;
}

static void tb_init_intern(void)
{
	if (tb_novo.magic != RAM_MAGIC || tb_novo.magic_inv != ~RAM_MAGIC) {
		tb_novo.magic = RAM_MAGIC;
		tb_novo.magic_inv = ~RAM_MAGIC;
		tb_novo.unix_delta_sec = 0;
		tb_novo.last_runtime_sec = 0;
		tb_novo.energy_usage_mA_mSec = 0;
	} else {
		tb_unix_time_set(tb_novo.unix_delta_sec + tb_novo.last_runtime_sec);
	}
}

int main(void)
{
	printk("\n\n=== JoEmbedded.de - Z-Shell " __DATE__ " " __TIME__ " ===\n");
	tb_init_intern(); // Time-NV etc
	tb_init_system_peripherals();
	printk("Device MAC:%016llX\n", tb_device_id.value);

#if defined(CONFIG_BLUETOOTH_UART)
	if (bt_uart_init()) {
		tb_fatal_error("BT_ENABLE");
	}
#endif

	while (true) {
		tb_now_runtime_sec = tb_runtime_now_get();

		if (tb_con_is_connected()) {
			if (tb_con_state <= TB_CON_POWERDOWN) {
				tb_con_wake();
			}

			tb_led_on();
			(void)tb_uart_poll_task();
		} else if (tb_con_state >= TB_CON_READY_UNCHECKED) {
			tb_con_powerdown();
		}

		tb_led_off();

		/* VDD 3V: BLE on ~12 uA bei 1 s Zyklus, BLE off <5 uA. */
		k_sleep(K_MSEC(1000));
	}

	return 0;
}
