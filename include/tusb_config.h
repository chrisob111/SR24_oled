#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

// 1. Vorherige Definitionen löschen (falls vom Framework gesetzt)
#ifdef CFG_TUD_ENABLED
  #undef CFG_TUD_ENABLED
#endif

// 2. Hardware-Einstellung (RP2040)
#define CFG_TUSB_MCU           OPT_MCU_RP2040
#define CFG_TUSB_RHPORT0_MODE  OPT_MODE_HOST

// 3. Host-Modus aktivieren
#define CFG_TUH_ENABLED        1
#define CFG_TUH_CDC            1  // CDC (Serial) Treiber
#define CFG_TUH_HUB            0  // Hub deaktivieren (spart Speicher)
#define CFG_TUH_DEVICE_MAX     1  // Max. 1 Gerät
#define CFG_TUH_ENUMERATION_BUFSIZE 256

// 4. Device-Modus deaktivieren
#define CFG_TUD_ENABLED        0

// 5. OS-Einstellung (Pico SDK)
#define CFG_TUSB_OS            OPT_OS_PICO

#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */