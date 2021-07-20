/***************************************************************************************************************
* tb_pins_nrf52.c - Pin definitins for my used HW platforms
*
* For platform nRF52 
*
* (C) joembedded.de
***************************************************************************************************************/


//-------- NRF52840-CPUS -------------
#ifdef NINA_B3_EVK
  //#warning "INFO: TB_TOOLS for NINA_B3_EVK" // Just as Info
  #ifndef NRF52840_XXAA
    #error "WRONG CPU"
  #endif
  #define TB_LED0   NRF_GPIO_PIN_MAP(0,13) // Active LOW
  #define TB_BUT0   NRF_GPIO_PIN_MAP(0,25) // Button and GREEN Led ..
  #define TB_RX_PIN NRF_GPIO_PIN_MAP(0,29) 
  #define TB_TX_PIN NRF_GPIO_PIN_MAP(1,13) 
#endif

#if defined(NINA_B3_LTX) || defined(NINA_B3_EPA)
  //#warning "INFO: TB_TOOLS for NINA_B3_LTX/_EPA" // Just as Info
  #ifndef NRF52840_XXAA
    #error "WRONG CPU"
  #endif
  #define TB_LED0   NRF_GPIO_PIN_MAP(0,13) // Actice LOW
  //#define TB_BUT0   NRF_GPIO_PIN_MAP(xx) //  No Button
  #define TB_RX_PIN NRF_GPIO_PIN_MAP(0,15) 
  #define TB_TX_PIN NRF_GPIO_PIN_MAP(0,14) 
#endif

//-------- NRF52832-CPUS -------------
#ifdef ANNA_B112_EVK
  //#warning "INFO: TB_TOOLS for NINA_B112_EVK"  // Just as Info
  #ifndef NRF52832_XXAA
    #error "WRONG CPU"
  #endif
  #define TB_BUT0   NRF_GPIO_PIN_MAP(0,25) // Button and GREEN Led ..
  #define TB_LED0   NRF_GPIO_PIN_MAP(0,27) // Active LOW
  #define TB_RX_PIN NRF_GPIO_PIN_MAP(0,2) 
  #define TB_TX_PIN NRF_GPIO_PIN_MAP(0,3) 
#endif

#ifdef YJ_NRF52832  // YJ_16048 from HolyIot (NO CE/FCC uncertified Low-Cost module)
  //#warning "INFO: TB_TOOLS for YJ_16048_NRF52832 (NO CE/FCC)"  // Just as Info
  #ifndef NRF52832_XXAA
    #error "WRONG CPU"
  #endif
  #define TB_LED0   NRF_GPIO_PIN_MAP(0,19) // Active LOW
  //#define TB_BUT0   NRF_GPIO_PIN_MAP(xx) //  No Button
  #define TB_RX_PIN NRF_GPIO_PIN_MAP(0,2) 
  #define TB_TX_PIN NRF_GPIO_PIN_MAP(0,3) 
#endif

#ifdef ANNA_SDI12  // ANNA-SDI12 Sensor platform with ANNA-B112
  //#warning "INFO: TB_TOOLS for YJ_16048_NRF52832 (NO CE/FCC)"  // Just as Info
  #ifndef NRF52832_XXAA
    #error "WRONG CPU"
  #endif
  #define TB_LED0   NRF_GPIO_PIN_MAP(0,19) // Active LOW
  //#define TB_BUT0   NRF_GPIO_PIN_MAP(xx) //  No Button
  #define TB_RX_PIN NRF_GPIO_PIN_MAP(0,2) 
  #define TB_TX_PIN NRF_GPIO_PIN_MAP(0,3) 
#endif

//---- Check Platform
#if !defined(TB_RX_PIN) || !defined(TB_TX_PIN)
 #error "No Platform set: can not define I/O Pins"
#endif

//***