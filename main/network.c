#include <time.h>
#include <sys/time.h>

#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_sntp.h"

#include "esp_log.h"

#include "storage.h"
#include "network.h"

static const char *TAG = "network";

esp_netif_t *ap_netif = NULL;
esp_netif_t *sta_netif = NULL;

// static bool mdns_started = false;

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
      esp_wifi_connect();
      break;

    case WIFI_EVENT_STA_DISCONNECTED:
      ESP_LOGI(TAG, "STA disconnected, retrying...");
      esp_wifi_connect();
      break;

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
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "STA got IP: " IPSTR, IP2STR(&event->ip_info.ip));

    /* Start mDNS once */
    /*
    if (!mdns_started)
    {
      mdns_started = true;
      ESP_ERROR_CHECK(mdns_init());
      mdns_hostname_set("esp32");
      mdns_instance_name_set("ESP32 Device");
      mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
      ESP_LOGI(TAG, "mDNS started: http://esp32.local");
    }
      */

    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

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

    setenv("TZ", "PST-8", 1);
    tzset();

    // time(&now);
    // localtime_r(&now, &timeinfo);

    char datetime[20];
    strftime(datetime, sizeof(datetime), "%Y-%m-%d %H:%M:%S", &timeinfo);

    ESP_LOGI(TAG, "Current time: %s", datetime);

    // ws_broadcast_text(timebuf); // your function
  }
}

void network_start(void)
{
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  sta_netif = esp_netif_create_default_wifi_sta();
  ap_netif = esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

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

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

  /* ---------- STA ---------- */
  wifi_config_t sta_config = {0};
  strncpy((char *)sta_config.sta.ssid, devcfg.sta_ssid, sizeof(sta_config.sta.ssid));
  strncpy((char *)sta_config.sta.password, devcfg.sta_key, sizeof(sta_config.sta.password));

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

  /* ---------- AP ---------- */
  wifi_config_t ap_config = {0};
  strncpy((char *)ap_config.ap.ssid, devcfg.ap_ssid, sizeof(ap_config.ap.ssid));
  strncpy((char *)ap_config.ap.password, devcfg.ap_key, sizeof(ap_config.ap.password));

  ap_config.ap.ssid_len = strlen(devcfg.ap_ssid);
  ap_config.ap.channel = 1;
  ap_config.ap.max_connection = 4;

  if (strlen(devcfg.ap_key) == 0)
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
  else
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "network started");
}
