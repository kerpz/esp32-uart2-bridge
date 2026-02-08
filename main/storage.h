#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include "esp_err.h"

/* Config magic for first boot detection */
#define CONFIG_MAGIC 0xA5

typedef struct
{
    uint8_t magic;

    /* WiFi AP */
    char ap_ssid[32];
    char ap_key[16];

    /* WiFi STA */
    uint8_t sta_enable;
    char sta_ssid[32];
    char sta_key[16];

    /* Components */
    uint8_t beep_enable;
    uint8_t analog_enable;
    uint8_t display_enable;
    uint8_t ads1115_enable;

    /* API */
    uint8_t post_enable;
    char api_url[256];
    char api_key[32];

    uint16_t http_timeout;

} device_config_t;

/* Global config */
extern device_config_t devcfg;

/* Storage API */
void storage_start(void);
void config_load(device_config_t *cfg);
void config_save(device_config_t *cfg);

#endif
