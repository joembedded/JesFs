/*******************************************************************************
* JesFs_ll_pca100xx.c: LowLevelDriver for nRF52840 on PCA10056 Board
*                      Changes added to work also on nRF52832
*
* JesFs - Jo's Embedded Serial File System
* 
* The PCA10056 contains a Ultra-Low-Power MX25R6435 Flash (8 Mbyte)
* Sleeping current is typically <0.5uA. By design the Flash is connected in QSPI mode.
* Unfortunetally QSPI is only available on nRF52840 and transfer is limited to multiples of 4.
* So here the more common SPIM-driver is used instead of QSPI, but using high speed 
* 32MHz Clock ( = up to 4 MByte/sec Transfer)
*
* Tested on:
* - nRF52840 on PCA10056 DK Board, using the on board 8MB Flash
* - nRF52840 on NINA-B302 DK Boarf using the on board 1,2,8MB Flash
* - nRF52832 on ANNA-B112 DK Board using the external 8MB Flash
*
* Speical notes:
* - There is a blocking waiting function used (nrf_delay_us() in sflash_wait_usec()).
*   It my be replaced by a low_power_wait(), etc..
* - During SPI select active, the FPIN_LED (if defined) is on. While waiting, it is toggeling,
*   so with a small Piezo you can 'hear' and 'see' the file system working ;-)
*
* (C) joembedded@gmail.com - www.joembedded.de
* Version: 
* 1.5 / 25.11.2019
* 1.51 / 07.12.2019 (LED polarity PCA10056)
* 1.6 / 05.01.2020 Seems 32MHz is too fast with Softdevice in parallel, with 16 Mhz OK
* 1.7 / 06.01.2020 added Defines for 52840/etc
* 1.71 / 12.11.2020 LED only on EVAL-board
* 2.00 / 23.11.2020 nRF52832 (limited SPI DMA-Size <256)
* 2.10 / 13.02.2021 nRF52832 SPI Pins mapped to optimal position for ANNA-B112 module
* 2.11 / 16.05.2021 removed 'board.h'
*******************************************************************************/

// ---------------- required for JesFs ----------------------------
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "jesfs.h"
#include "jesfs_int.h"

/* Driver Header files */
#include "nrfx_spim.h"
#include "app_util_platform.h"
#include "nrf_gpio.h"
#include "nrf_delay.h"
// #include "boards.h" // Pin names
#include "app_error.h"

#ifdef NRF52840_XXAA    // 52840: SPI-Flash on Board

/* Driver Defines und Locals */
// Use DK PC10056 on board FLASH - Pins are fixed
// Unused PINS: BSP_QSPI_IO2_PIN  (22) and BSP_QSPI_IO3_PIN (23)
#define FPIN_SCK_PIN  NRF_GPIO_PIN_MAP(0,19) // 19 Clock (BSP_QSPI_SCK_PIN)
#define FPIN_MOSI_PIN NRF_GPIO_PIN_MAP(0,20) // 20 SI/DIO0 (BSP_QSPI_IO0_PIN)
#define FPIN_MISO_PIN NRF_GPIO_PIN_MAP(0,21) //21 SO/DIO1 (BSP_QSPI_IO1_PIN)
#define FPIN_SS_PIN   NRF_GPIO_PIN_MAP(0,17) //17 Select, (extern, BSP_QSPI_CSN_PIN)
// Enable or define in Project: #define FPIN_LED      BSP_LED_0   //  13 (optional, used if defined)

#else // 52832 - I/Os flexible, but this makes sense
// Pins mapped to optimal positions for ANNA-B112 module.
#define SPI_CHUNKS         128 // If defined: SPI in smaller Chunks (nRF52832 requires <=255 Bytes)
#define FPIN_SCK_PIN  NRF_GPIO_PIN_MAP(0,15)   //  Clock
#define FPIN_MOSI_PIN NRF_GPIO_PIN_MAP(0,16)   //  SI/DIO0
#define FPIN_MISO_PIN NRF_GPIO_PIN_MAP(0,18)   //  SO/DIO1
#define FPIN_SS_PIN   NRF_GPIO_PIN_MAP(0,14)   //  Select, (extern)
//#define FPIN_LED      NRF_GPIO_PIN_MAP(0,27)   //  (optional, used if defined)

#endif



#ifdef BOARD_PCA10056
  #define SPI_INSTANCE  3     // M                                  /**< SPI instance index. */
#else
  #define SPI_INSTANCE  2     // Assign highest available SPIM instabce for Driver..
#endif

static const nrfx_spim_t spi = NRFX_SPIM_INSTANCE(SPI_INSTANCE);  /**< SPI instance. */
static bool spi_init_flag=false;
static nrfx_spim_config_t spi_config = NRFX_SPIM_DEFAULT_CONFIG;

//------------------- LowLevel SPI START ------------------------
// INIT Hardware SPI
int16_t sflash_spi_init(void){
    if(spi_init_flag==true) return 0;  // Already init

    //spi_config.frequency      = NRF_SPIM_FREQ_1M; // for tests, Default: 4M
#ifdef NRF52840_XXAA    // 52840: max. 32MHz
    spi_config.frequency      = NRF_SPIM_FREQ_16M; 
     //spi_config.frequency      = NRF_SPIM_FREQ_32M; // Maybe too fast with Softdevice in || (V1.6) 
#else // older: max 8MHz
    spi_config.frequency      = NRF_SPIM_FREQ_8M; // for tests, Default: 4M
#endif

    spi_config.miso_pin       = FPIN_MISO_PIN;
    spi_config.mosi_pin       = FPIN_MOSI_PIN;
    spi_config.sck_pin        = FPIN_SCK_PIN;
    APP_ERROR_CHECK(nrfx_spim_init(&spi, &spi_config, NULL, NULL)); // Use Blocking transfer

    nrf_gpio_cfg_output(FPIN_SS_PIN);  // We use Software CS
    nrf_gpio_pin_set(FPIN_SS_PIN); // De-Select Peripheral
    // Better use strong drivers for high Speed
    nrf_gpio_cfg(FPIN_SCK_PIN,NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_CONNECT, NRF_GPIO_PIN_NOPULL,
        NRF_GPIO_PIN_H0H1, NRF_GPIO_PIN_NOSENSE);

    nrf_gpio_cfg(FPIN_MOSI_PIN, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_DISCONNECT,
               NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_H0H1,  NRF_GPIO_PIN_NOSENSE);
#ifdef FPIN_LED
    nrf_gpio_cfg_output(FPIN_LED);  // Might be already init from bsp-drivers
#endif
    spi_init_flag=true;
    return 0;
}

void sflash_spi_close(void){
    if(spi_init_flag==false) return;  // Already uninit
    nrfx_spim_uninit(&spi);
    spi_init_flag=false;
}

void sflash_wait_usec(uint32_t usec){
#ifdef FPIN_LED
    nrf_gpio_pin_toggle(FPIN_LED);  // Hear Me Working! Led Toggle
#endif
    nrf_delay_us(usec);
}

void sflash_select(void){
        // nrf_delay_us(1);  // (?) from Community: SPI may need a small delay to recover (?)
#ifdef FPIN_LED
        nrf_gpio_pin_clear(FPIN_LED);  // Hear Me Working! Led ON (0 on PCA10056)
#endif
        nrf_gpio_pin_clear(FPIN_SS_PIN); // Select Peripheral
}
void sflash_deselect(void){
#ifdef FPIN_LED
        nrf_gpio_pin_set(FPIN_LED);  // Hear Me Working! Led Off (1 on PCA10056)
#endif
        nrf_gpio_pin_set(FPIN_SS_PIN); // De-Select Peripheral
}

void sflash_spi_read(uint8_t *buf, uint16_t len){
#ifdef SPI_CHUNKS 
    uint16_t rlen;
    while(len){
      rlen=len;
      if(rlen>SPI_CHUNKS) rlen=SPI_CHUNKS;
      nrfx_spim_xfer_desc_t xfer_desc = NRFX_SPIM_XFER_TRX(NULL, 0, buf, rlen); 
      APP_ERROR_CHECK(nrfx_spim_xfer(&spi, &xfer_desc, 0));
      len-=rlen;
      buf+=rlen;
    }
#else
    nrfx_spim_xfer_desc_t xfer_desc = NRFX_SPIM_XFER_TRX(NULL, 0, buf, len); 
    APP_ERROR_CHECK(nrfx_spim_xfer(&spi, &xfer_desc, 0));
#endif
}

/* Because of SPI page-Size (def. 256 Bytes) len is no problem. */
void  sflash_spi_write(const uint8_t *buf, uint16_t len){
#ifdef SPI_CHUNKS 
    uint16_t wlen;
    while(len){
      wlen=len;
      if(wlen>SPI_CHUNKS) wlen=SPI_CHUNKS;
      nrfx_spim_xfer_desc_t xfer_desc = NRFX_SPIM_XFER_TRX(buf, wlen, NULL, 0); 
      APP_ERROR_CHECK(nrfx_spim_xfer(&spi, &xfer_desc, 0));
      len-=wlen;
      buf+=wlen;
    }
#else
    nrfx_spim_xfer_desc_t xfer_desc = NRFX_SPIM_XFER_TRX(buf, len, NULL, 0); 
    APP_ERROR_CHECK(nrfx_spim_xfer(&spi, &xfer_desc, 0));
#endif
}
//------------------- LowLevel SPI End ------------------------
