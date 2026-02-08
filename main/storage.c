#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "storage.h"

static const char *TAG = "storage";

/* Global config instance */
device_config_t devcfg = {0};

/* ---------- Defaults ---------- */
static void config_apply_defaults(device_config_t *cfg)
{
    ESP_LOGI(TAG, "applying default config");

    strcpy(cfg->ap_ssid, "ESP32");
    strcpy(cfg->ap_key, "12345678");

    cfg->sta_enable = 1;
    strcpy(cfg->sta_ssid, "KERPZ-AP2");
    strcpy(cfg->sta_key, "yourpassword");

    cfg->beep_enable = 1;
    cfg->analog_enable = 1;
    cfg->display_enable = 0;
    cfg->ads1115_enable = 0;

    cfg->post_enable = 0;
    strcpy(cfg->api_url, "https://192.168.2.1:8001/cgi-bin/custom-full.cgi?a=iot");
    strcpy(cfg->api_key, "NIJCG7UI28O9CAYD");

    cfg->http_timeout = 0;
}

/* ---------- Load ---------- */
void config_load(device_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    nvs_handle_t nvs;
    size_t len;

    if (nvs_open("cfg", NVS_READONLY, &nvs) != ESP_OK)
    {
        ESP_LOGW(TAG, "no NVS config found");
        return;
    }

    /* magic */
    nvs_get_u8(nvs, "magic", &cfg->magic);

    len = sizeof(cfg->ap_ssid);
    nvs_get_str(nvs, "ap_ssid", cfg->ap_ssid, &len);

    len = sizeof(cfg->ap_key);
    nvs_get_str(nvs, "ap_key", cfg->ap_key, &len);

    nvs_get_u8(nvs, "sta_enable", &cfg->sta_enable);

    len = sizeof(cfg->sta_ssid);
    nvs_get_str(nvs, "sta_ssid", cfg->sta_ssid, &len);

    len = sizeof(cfg->sta_key);
    nvs_get_str(nvs, "sta_key", cfg->sta_key, &len);

    nvs_get_u8(nvs, "beep_enable", &cfg->beep_enable);
    nvs_get_u8(nvs, "analog_enable", &cfg->analog_enable);
    nvs_get_u8(nvs, "display_enable", &cfg->display_enable);
    nvs_get_u8(nvs, "ads1115_enable", &cfg->ads1115_enable);

    nvs_get_u8(nvs, "post_enable", &cfg->post_enable);

    len = sizeof(cfg->api_url);
    nvs_get_str(nvs, "api_url", cfg->api_url, &len);

    len = sizeof(cfg->api_key);
    nvs_get_str(nvs, "api_key", cfg->api_key, &len);

    nvs_get_u16(nvs, "http_timeout", &cfg->http_timeout);

    nvs_close(nvs);
}

/* ---------- Save ---------- */
void config_save(device_config_t *cfg)
{
    nvs_handle_t nvs;

    if (nvs_open("cfg", NVS_READWRITE, &nvs) != ESP_OK)
        return;

    cfg->magic = CONFIG_MAGIC;

    nvs_set_u8(nvs, "magic", cfg->magic);

    nvs_set_str(nvs, "ap_ssid", cfg->ap_ssid);
    nvs_set_str(nvs, "ap_key", cfg->ap_key);

    nvs_set_u8(nvs, "sta_enable", cfg->sta_enable);
    nvs_set_str(nvs, "sta_ssid", cfg->sta_ssid);
    nvs_set_str(nvs, "sta_key", cfg->sta_key);

    nvs_set_u8(nvs, "beep_enable", cfg->beep_enable);
    nvs_set_u8(nvs, "analog_enable", cfg->analog_enable);
    nvs_set_u8(nvs, "display_enable", cfg->display_enable);
    nvs_set_u8(nvs, "ads1115_enable", cfg->ads1115_enable);

    nvs_set_u8(nvs, "post_enable", cfg->post_enable);
    nvs_set_str(nvs, "api_url", cfg->api_url);
    nvs_set_str(nvs, "api_key", cfg->api_key);

    nvs_set_u16(nvs, "http_timeout", cfg->http_timeout);

    nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGI(TAG, "config saved");
}

/* ---------- Storage start ---------- */
void storage_start(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }

    config_load(&devcfg);

    if (devcfg.magic != CONFIG_MAGIC)
    {
        ESP_LOGI(TAG, "first boot detected");

        config_apply_defaults(&devcfg);
        config_save(&devcfg);
    }
}
