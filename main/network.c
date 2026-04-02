#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_sntp.h"
#include "esp_event.h"
#include "esp_log.h"

#include "storage.h"
#include "network.h"

static const char *TAG = "network";

esp_netif_t *ap_netif = NULL;
esp_netif_t *sta_netif = NULL;

/* ===== State नियंत्रण ===== */
static int retry_count = 0;
static bool sta_enabled = true;
static bool sntp_started = false;

/* ===== Backoff function ===== */
static int get_retry_delay_ms(int retry)
{
  if (retry < 5)
    return 2000; // fast
  else if (retry < 10)
    return 5000; // medium
  else
    return 15000; // slow forever
}

/* ===== WiFi Event Handler ===== */
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
  if (event_base == WIFI_EVENT)
  {
    switch (event_id)
    {
    case WIFI_EVENT_STA_START:
      ESP_LOGI(TAG, "STA start → connecting...");
      if (sta_enabled)
        esp_wifi_connect();
      break;

    case WIFI_EVENT_STA_DISCONNECTED:
    {
      if (!sta_enabled)
      {
        ESP_LOGW(TAG, "STA disabled, not reconnecting");
        break;
      }

      retry_count++;

      int delay_ms = get_retry_delay_ms(retry_count);

      ESP_LOGW(TAG, "STA disconnected → retry #%d in %d ms",
               retry_count, delay_ms);

      vTaskDelay(pdMS_TO_TICKS(delay_ms));
      esp_wifi_connect();

      break;
    }

    case WIFI_EVENT_AP_STACONNECTED:
      ESP_LOGI(TAG, "AP client connected");
      break;

    case WIFI_EVENT_AP_STADISCONNECTED:
      ESP_LOGI(TAG, "AP client disconnected");
      break;
    }
  }

  else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
  {
    retry_count = 0; // reset retries

    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "STA got IP: " IPSTR, IP2STR(&event->ip_info.ip));

    /* ===== SNTP (only once) ===== */
    if (!sntp_started)
    {
      sntp_started = true;

      ESP_LOGI(TAG, "Starting SNTP...");
      esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
      esp_sntp_setservername(0, "pool.ntp.org");
      esp_sntp_init();
    }

    /* ===== Wait for time sync ===== */
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;

    while (timeinfo.tm_year < (2020 - 1900) && retry < 10)
    {
      ESP_LOGI(TAG, "Waiting for time sync...");
      vTaskDelay(pdMS_TO_TICKS(2000));
      time(&now);
      localtime_r(&now, &timeinfo);
      retry++;
    }

    /* ===== Philippines timezone ===== */
    setenv("TZ", "PHT-8", 1);
    tzset();

    char datetime[32];
    strftime(datetime, sizeof(datetime), "%Y-%m-%d %H:%M:%S", &timeinfo);

    ESP_LOGI(TAG, "Current time: %s", datetime);
  }
}

/* ===== Public: Start Network ===== */
void network_start(void)
{
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  sta_netif = esp_netif_create_default_wifi_sta();
  ap_netif = esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  /* Register events */
  ESP_ERROR_CHECK(
      esp_event_handler_instance_register(
          WIFI_EVENT,
          ESP_EVENT_ANY_ID,
          &wifi_event_handler,
          NULL,
          NULL));

  ESP_ERROR_CHECK(
      esp_event_handler_instance_register(
          IP_EVENT,
          IP_EVENT_STA_GOT_IP,
          &wifi_event_handler,
          NULL,
          NULL));

  /* Always AP + STA */
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

  /* ===== STA Config ===== */
  wifi_config_t sta_config = {0};
  strncpy((char *)sta_config.sta.ssid,
          devcfg.sta_ssid,
          sizeof(sta_config.sta.ssid));

  strncpy((char *)sta_config.sta.password,
          devcfg.sta_key,
          sizeof(sta_config.sta.password));

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

  /* ===== AP Config ===== */
  wifi_config_t ap_config = {0};

  strncpy((char *)ap_config.ap.ssid,
          devcfg.ap_ssid,
          sizeof(ap_config.ap.ssid));

  strncpy((char *)ap_config.ap.password,
          devcfg.ap_key,
          sizeof(ap_config.ap.password));

  ap_config.ap.ssid_len = strlen(devcfg.ap_ssid);
  ap_config.ap.channel = 1;
  ap_config.ap.max_connection = 4;

  if (strlen(devcfg.ap_key) == 0)
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
  else
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

  /* Start WiFi */
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "Network started (AP always ON, STA auto-retry)");
}

/* ===== Public: Disable STA ===== */
void wifi_sta_disable(void)
{
  ESP_LOGW(TAG, "Disabling STA...");

  sta_enabled = false;
  esp_wifi_disconnect();
}

/* ===== Public: Enable STA ===== */
void wifi_sta_enable(void)
{
  ESP_LOGI(TAG, "Enabling STA...");

  retry_count = 0;
  sta_enabled = true;

  esp_wifi_connect();
}