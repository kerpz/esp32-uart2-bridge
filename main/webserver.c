#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_chip_info.h"
#include "nvs_flash.h"
#include "esp_flash.h"

#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_spiffs.h"
#include "esp_event.h"

#include "esp_log.h"

#include "storage.h"
#include "network.h"

#include "html.h"
#include "webserver.h"

static const char *TAG = "webserver";

typedef struct
{
  httpd_handle_t server;
  int clients[MAX_WS_CLIENTS];
  int count;
  SemaphoreHandle_t lock;
} ws_manager_t;

static ws_manager_t ws_mgr;

static void add_text_element(cJSON *arr,
                             const char *label,
                             const char *name,
                             const char *value)
{
  cJSON *obj = cJSON_CreateObject();
  cJSON_AddStringToObject(obj, "type", "text");
  cJSON_AddStringToObject(obj, "label", label);
  cJSON_AddStringToObject(obj, "name", name);
  cJSON_AddStringToObject(obj, "value", value);
  cJSON_AddStringToObject(obj, "attrib", "disabled");
  cJSON_AddItemToArray(arr, obj);
}

static void json_add_select(cJSON *elements,
                            const char *name,
                            const char *label,
                            int value)
{
  cJSON *sel = cJSON_CreateObject();

  cJSON_AddStringToObject(sel, "type", "select");
  cJSON_AddStringToObject(sel, "label", label);
  cJSON_AddStringToObject(sel, "name", name);

  /* Convert value to string */
  cJSON_AddStringToObject(sel, "value", value ? "1" : "0");

  /* Options */
  cJSON *opts = cJSON_CreateArray();

  cJSON *opt0 = cJSON_CreateArray();
  cJSON_AddItemToArray(opt0, cJSON_CreateString("0"));
  cJSON_AddItemToArray(opt0, cJSON_CreateString("Disabled"));
  cJSON_AddItemToArray(opts, opt0);

  cJSON *opt1 = cJSON_CreateArray();
  cJSON_AddItemToArray(opt1, cJSON_CreateString("1"));
  cJSON_AddItemToArray(opt1, cJSON_CreateString("Enabled"));
  cJSON_AddItemToArray(opts, opt1);

  cJSON_AddItemToObject(sel, "options", opts);
  cJSON_AddItemToArray(elements, sel);
}

static void json_copy_str(cJSON *doc,
                          const char *key,
                          char *dst,
                          size_t maxlen)
{
  cJSON *it = cJSON_GetObjectItem(doc, key);
  if (cJSON_IsString(it) && it->valuestring)
  {
    strncpy(dst, it->valuestring, maxlen - 1);
    dst[maxlen - 1] = '\0';
  }
}

/***********************************************************
 index page handler
***********************************************************/
static esp_err_t index_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  httpd_resp_sendstr(req, INDEX_HTML);
  return ESP_OK;
}

/***********************************************************
 system page handler
***********************************************************/
static esp_err_t system_handler(httpd_req_t *req)
{
  uint8_t expand_system = 0;
  uint8_t expand_wifiap = 1;
  uint8_t expand_wifista = 1;
  uint8_t expand_command = 1;

  // uint8_t timezone = 8;
  // time_t epoch = 1700000000; // fixed epoch for demo

  /* ---------- Receive POST body ---------- */
  int total_len = req->content_len;
  char *buf = malloc(total_len + 1);
  if (!buf)
    return ESP_FAIL;

  int recv_len = httpd_req_recv(req, buf, total_len);
  if (recv_len <= 0)
  {
    free(buf);
    return ESP_FAIL;
  }
  buf[recv_len] = 0;

  /* ---------- Parse JSON ---------- */
  if (strcmp(buf, "{}") != 0)
  {
    cJSON *doc = cJSON_Parse(buf);
    if (doc)
    {
      cJSON *item;

      item = cJSON_GetObjectItem(doc, "expand_system");
      if (item)
        expand_system = item->valueint;

      item = cJSON_GetObjectItem(doc, "expand_wifiap");
      if (item)
        expand_wifiap = item->valueint;

      item = cJSON_GetObjectItem(doc, "expand_wifista");
      if (item)
        expand_wifista = item->valueint;

      item = cJSON_GetObjectItem(doc, "expand_command");
      if (item)
        expand_command = item->valueint;

      item = cJSON_GetObjectItem(doc, "reboot");
      if (item)
        esp_restart();

      cJSON_Delete(doc);
    }
  }

  free(buf);

  /* ---------- Time ---------- */
  char datetime[20];
  struct tm timeinfo = {0};
  // time_t now = epoch + (timezone * 3600);
  // gmtime_r(&t_epoch, &now);
  time_t now = 0;
  time(&now);
  localtime_r(&now, &timeinfo);
  strftime(datetime, sizeof(datetime), "%Y-%m-%d %H:%M:%S", &timeinfo);

  /* ---------- System info ---------- */
  uint32_t free_heap = esp_get_free_heap_size();

  uint32_t flash_size = 0;
  esp_flash_get_size(NULL, &flash_size);

  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);

  char chip_str[32];
  sprintf(chip_str, "ESP32 rev%d", chip_info.revision);

  char heap_str[16];
  sprintf(heap_str, "%lu", free_heap);

  char flash_str[16];
  sprintf(flash_str, "%lu", flash_size);

  /* ---------- WiFi info ---------- */
  uint8_t mac_sta[6], mac_ap[6];
  esp_wifi_get_mac(WIFI_IF_STA, mac_sta);
  esp_wifi_get_mac(WIFI_IF_AP, mac_ap);

  char sta_mac[18], ap_mac[18];
  sprintf(sta_mac, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac_sta[0], mac_sta[1], mac_sta[2],
          mac_sta[3], mac_sta[4], mac_sta[5]);

  sprintf(ap_mac, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac_ap[0], mac_ap[1], mac_ap[2],
          mac_ap[3], mac_ap[4], mac_ap[5]);

  esp_netif_ip_info_t ip_sta, ip_ap;
  esp_netif_get_ip_info(sta_netif, &ip_sta);
  esp_netif_get_ip_info(ap_netif, &ip_ap);

  char sta_ip[16], ap_ip[16];
  sprintf(sta_ip, IPSTR, IP2STR(&ip_sta.ip));
  sprintf(ap_ip, IPSTR, IP2STR(&ip_ap.ip));

  wifi_sta_list_t sta_list;
  esp_wifi_ap_get_sta_list(&sta_list);

  char sta_count_str[8];
  sprintf(sta_count_str, "%d", sta_list.num);

  wifi_config_t wifi_cfg;

  /* ---------- Build JSON ---------- */
  cJSON *root = cJSON_CreateArray();

  /* ----- System ----- */
  cJSON *sys = cJSON_CreateObject();
  cJSON_AddStringToObject(sys, "label", "System");
  cJSON_AddStringToObject(sys, "name", "expand_system");
  cJSON_AddNumberToObject(sys, "value", expand_system);

  cJSON *sys_elements = cJSON_CreateArray();
  cJSON_AddItemToObject(sys, "elements", sys_elements);

  add_text_element(sys_elements, "Chip ID", "chip_id", chip_str);
  add_text_element(sys_elements, "Free Heap", "free_heap", heap_str);
  add_text_element(sys_elements, "Flash Size", "flash_size", flash_str);
  add_text_element(sys_elements, "App Code", "app_code", APPCODE);
  add_text_element(sys_elements, "System Date", "sys_date", datetime);

  cJSON_AddItemToArray(root, sys);

  /* ----- WiFi AP ----- */
  cJSON *ap = cJSON_CreateObject();
  cJSON_AddStringToObject(ap, "label", "Wifi AP");
  cJSON_AddStringToObject(ap, "name", "expand_wifiap");
  cJSON_AddNumberToObject(ap, "value", expand_wifiap);

  cJSON *ap_elements = cJSON_CreateArray();
  cJSON_AddItemToObject(ap, "elements", ap_elements);

  esp_wifi_get_config(WIFI_IF_AP, &wifi_cfg);
  add_text_element(ap_elements, "AP MAC", "ap_mac", ap_mac);
  add_text_element(ap_elements, "AP Address", "ap_address", ap_ip);
  add_text_element(ap_elements, "AP SSID", "ap_ssid", (char *)wifi_cfg.ap.ssid);
  add_text_element(ap_elements, "Connected Devices", "connected_devices", sta_count_str);

  cJSON_AddItemToArray(root, ap);

  /* ----- WiFi STA ----- */
  cJSON *sta = cJSON_CreateObject();
  cJSON_AddStringToObject(sta, "label", "Wifi Sta");
  cJSON_AddStringToObject(sta, "name", "expand_wifista");
  cJSON_AddNumberToObject(sta, "value", expand_wifista);

  cJSON *sta_elements = cJSON_CreateArray();
  cJSON_AddItemToObject(sta, "elements", sta_elements);

  esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);
  add_text_element(sta_elements, "Sta MAC", "sta_mac", sta_mac);
  add_text_element(sta_elements, "Sta Address", "sta_address", sta_ip);
  add_text_element(sta_elements, "Sta SSID", "sta_ssid", (char *)wifi_cfg.sta.ssid);

  cJSON_AddItemToArray(root, sta);

  /* ----- Command ----- */
  cJSON *cmd = cJSON_CreateObject();
  cJSON_AddStringToObject(cmd, "label", "Command");
  cJSON_AddStringToObject(cmd, "name", "expand_command");
  cJSON_AddNumberToObject(cmd, "value", expand_command);

  cJSON *cmd_elements = cJSON_CreateArray();
  cJSON_AddItemToObject(cmd, "elements", cmd_elements);

  cJSON *btn = cJSON_CreateObject();
  cJSON_AddStringToObject(btn, "type", "button");
  cJSON_AddStringToObject(btn, "label", "REBOOT");
  cJSON_AddStringToObject(btn, "name", "reboot");
  cJSON_AddStringToObject(btn, "value", "reboot");
  cJSON_AddStringToObject(btn, "confirm", "Are you sure you want to reboot?");
  cJSON_AddItemToArray(cmd_elements, btn);

  cJSON_AddItemToArray(root, cmd);

  char *json_out = cJSON_PrintUnformatted(root);

  /* ---------- Send response ---------- */
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_out, HTTPD_RESP_USE_STRLEN);

  cJSON_Delete(root);
  free(json_out);

  return ESP_OK;
}

/***********************************************************
 config page handler
***********************************************************/
esp_err_t config_handler(httpd_req_t *req)
{
  /* ---------- Receive POST body ---------- */
  int total = req->content_len;
  char *buf = malloc(total + 1);
  if (!buf)
    return ESP_ERR_NO_MEM;

  int received = httpd_req_recv(req, buf, total);
  if (received <= 0)
  {
    free(buf);
    return ESP_FAIL;
  }
  buf[received] = 0;

  if (strcmp(buf, "{}") != 0)
  {
    cJSON *doc = cJSON_Parse(buf);
    if (doc)
    {

      json_copy_str(doc, "ap_ssid", devcfg.ap_ssid, sizeof(devcfg.ap_ssid));
      json_copy_str(doc, "ap_key", devcfg.ap_key, sizeof(devcfg.ap_key));

      cJSON *v;
      if ((v = cJSON_GetObjectItem(doc, "sta_enable")))
        // devcfg.sta_enable = v->valueint;
        devcfg.sta_enable = (strcmp(v->valuestring, "1") == 0) ? 1 : 0;
      json_copy_str(doc, "sta_ssid", devcfg.sta_ssid, sizeof(devcfg.sta_ssid));
      json_copy_str(doc, "sta_key", devcfg.sta_key, sizeof(devcfg.sta_key));

      if ((v = cJSON_GetObjectItem(doc, "beep_enable")))
        devcfg.beep_enable = (strcmp(v->valuestring, "1") == 0) ? 1 : 0;
      if ((v = cJSON_GetObjectItem(doc, "analog_enable")))
        devcfg.analog_enable = (strcmp(v->valuestring, "1") == 0) ? 1 : 0;
      if ((v = cJSON_GetObjectItem(doc, "display_enable")))
        devcfg.display_enable = (strcmp(v->valuestring, "1") == 0) ? 1 : 0;
      if ((v = cJSON_GetObjectItem(doc, "ads1115_enable")))
        devcfg.ads1115_enable = (strcmp(v->valuestring, "1") == 0) ? 1 : 0;

      if ((v = cJSON_GetObjectItem(doc, "post_enable")))
        devcfg.post_enable = (strcmp(v->valuestring, "1") == 0) ? 1 : 0;
      json_copy_str(doc, "api_url", devcfg.api_url, sizeof(devcfg.api_url));
      json_copy_str(doc, "api_key", devcfg.api_key, sizeof(devcfg.api_key));

      // if ((v = cJSON_GetObjectItem(doc, "alarm_duration_limit")))
      //   alarm_duration_limit = v->valueint;

      config_save(&devcfg);
      cJSON_Delete(doc);
    }
  }
  free(buf);

  /* ---------- Build JSON response ---------- */
  cJSON *root = cJSON_CreateArray();

  /* ----- Wi-Fi AP section ----- */
  cJSON *ap = cJSON_CreateObject();
  cJSON_AddStringToObject(ap, "label", "Wifi AP");
  cJSON_AddStringToObject(ap, "name", "expand_wifiap");
  cJSON_AddNumberToObject(ap, "value", 1);
  cJSON *ap_elements = cJSON_CreateArray();
  cJSON_AddItemToObject(ap, "elements", ap_elements);

  cJSON *txt = cJSON_CreateObject();
  cJSON_AddStringToObject(txt, "type", "text");
  cJSON_AddStringToObject(txt, "label", "AP SSID");
  cJSON_AddStringToObject(txt, "name", "ap_ssid");
  cJSON_AddStringToObject(txt, "value", devcfg.ap_ssid);
  cJSON_AddItemToArray(ap_elements, txt);

  txt = cJSON_CreateObject();
  cJSON_AddStringToObject(txt, "type", "text");
  cJSON_AddStringToObject(txt, "label", "AP Key");
  cJSON_AddStringToObject(txt, "name", "ap_key");
  cJSON_AddStringToObject(txt, "value", devcfg.ap_key);
  cJSON_AddItemToArray(ap_elements, txt);

  cJSON_AddItemToArray(root, ap);

  /* ----- Wi-Fi STA section ----- */
  cJSON *sta = cJSON_CreateObject();
  cJSON_AddStringToObject(sta, "label", "Wifi Sta");
  cJSON_AddStringToObject(sta, "name", "expand_wifista");
  cJSON_AddNumberToObject(sta, "value", 1);

  cJSON *sta_elements = cJSON_CreateArray();
  cJSON_AddItemToObject(sta, "elements", sta_elements);

  /*
  cJSON *sel = cJSON_CreateObject();
  cJSON_AddStringToObject(sel, "type", "select");
  cJSON_AddStringToObject(sel, "label", "Wifi Sta");
  cJSON_AddStringToObject(sel, "name", "sta_enable");
  cJSON_AddNumberToObject(sel, "value", devcfg.sta_enable);

  cJSON *options = cJSON_CreateArray();
  cJSON *opt = cJSON_CreateArray();
  cJSON_AddItemToArray(opt, cJSON_CreateString("0"));
  cJSON_AddItemToArray(opt, cJSON_CreateString("Disabled"));
  cJSON_AddItemToArray(options, opt);

  opt = cJSON_CreateArray();
  cJSON_AddItemToArray(opt, cJSON_CreateString("1"));
  cJSON_AddItemToArray(opt, cJSON_CreateString("Enabled"));
  cJSON_AddItemToArray(options, opt);

  cJSON_AddItemToObject(sel, "options", options);
  cJSON_AddItemToArray(sta_elements, sel);
  */
  json_add_select(sta_elements, "sta_enable", "Wifi Sta", devcfg.sta_enable);

  /*
  uint16_t ap_count = 0;
  wifi_scan_config_t scan_config = {
      .ssid = 0,
      .bssid = 0,
      .channel = 0,
      .show_hidden = true};

  esp_wifi_scan_start(&scan_config, true);
  esp_wifi_scan_get_ap_num(&ap_count);
  wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
  esp_wifi_scan_get_ap_records(&ap_count, ap_records);
  */

  cJSON *sel_ssid = cJSON_CreateObject();
  cJSON_AddStringToObject(sel_ssid, "type", "select");
  cJSON_AddStringToObject(sel_ssid, "label", "Sta SSID");
  cJSON_AddStringToObject(sel_ssid, "name", "sta_ssid");
  cJSON_AddStringToObject(sel_ssid, "value", devcfg.sta_ssid);

  cJSON *options = cJSON_CreateArray();
  /*
   for (int i = 0; i < ap_count; i++)
   {
     int rssi = ap_records[i].rssi;

     int quality;
     if (rssi <= -100)
       quality = 0;
     else if (rssi >= -50)
       quality = 100;
     else
       quality = 2 * (rssi + 100);

     char label[96];

     if (ap_records[i].authmode != WIFI_AUTH_OPEN)
       snprintf(label, sizeof(label), "%s %d%% 🔒", ap_records[i].ssid, quality);
     else
       snprintf(label, sizeof(label), "%s %d%%", ap_records[i].ssid, quality);

     cJSON *opt = cJSON_CreateArray();
     cJSON_AddItemToArray(opt, cJSON_CreateString((char *)ap_records[i].ssid));
     cJSON_AddItemToArray(opt, cJSON_CreateString(label));
     cJSON_AddItemToArray(options, opt);
   }
  if (ap_count == 0)
  {
    cJSON *opt = cJSON_CreateArray();
    cJSON_AddItemToArray(opt, cJSON_CreateString(""));
    cJSON_AddItemToArray(opt, cJSON_CreateString(""));
    cJSON_AddItemToArray(options, opt);
  }

  cJSON_AddItemToObject(sel_ssid, "options", options);
   */
  cJSON *opt = cJSON_CreateArray();
  cJSON_AddItemToArray(opt, cJSON_CreateString(""));
  cJSON_AddItemToArray(opt, cJSON_CreateString(""));
  cJSON_AddItemToArray(options, opt);
  cJSON_AddItemToArray(sta_elements, sel_ssid);

  // txt = cJSON_CreateObject();
  // cJSON_AddStringToObject(txt, "type", "text");
  // cJSON_AddStringToObject(txt, "label", "Sta SSID");
  // cJSON_AddStringToObject(txt, "name", "sta_ssid");
  // cJSON_AddStringToObject(txt, "value", devcfg.sta_ssid);
  // cJSON_AddItemToArray(sta_elements, txt);

  txt = cJSON_CreateObject();
  cJSON_AddStringToObject(txt, "type", "text");
  cJSON_AddStringToObject(txt, "label", "Sta Key");
  cJSON_AddStringToObject(txt, "name", "sta_key");
  cJSON_AddStringToObject(txt, "value", devcfg.sta_key);
  cJSON_AddItemToArray(sta_elements, txt);

  cJSON_AddItemToArray(root, sta);

  /* ----- Components section ----- */
  cJSON *comp = cJSON_CreateObject();
  cJSON_AddStringToObject(comp, "label", "Components");
  cJSON_AddStringToObject(comp, "name", "expand_component");
  cJSON_AddNumberToObject(comp, "value", 1);

  cJSON *comp_elements = cJSON_CreateArray();
  cJSON_AddItemToObject(comp, "elements", comp_elements);

  json_add_select(comp_elements, "beep_enable", "Beep", devcfg.beep_enable);
  json_add_select(comp_elements, "analog_enable", "Analog", devcfg.analog_enable);
  json_add_select(comp_elements, "display_enable", "Display", devcfg.display_enable);
  json_add_select(comp_elements, "ads1115_enable", "ADS1115", devcfg.ads1115_enable);

  cJSON_AddItemToArray(root, comp);

  /* ----- API Post section ----- */
  cJSON *api = cJSON_CreateObject();
  cJSON_AddStringToObject(api, "label", "API Post");
  cJSON_AddStringToObject(api, "name", "expand_post");
  cJSON_AddNumberToObject(api, "value", 1);

  cJSON *api_elements = cJSON_CreateArray();
  cJSON_AddItemToObject(api, "elements", api_elements);

  json_add_select(api_elements, "post_enable", "API Post", devcfg.post_enable);

  txt = cJSON_CreateObject();
  cJSON_AddStringToObject(txt, "type", "text");
  cJSON_AddStringToObject(txt, "label", "API url");
  cJSON_AddStringToObject(txt, "name", "api_url");
  cJSON_AddStringToObject(txt, "value", devcfg.api_url);
  cJSON_AddItemToArray(api_elements, txt);

  txt = cJSON_CreateObject();
  cJSON_AddStringToObject(txt, "type", "text");
  cJSON_AddStringToObject(txt, "label", "API key");
  cJSON_AddStringToObject(txt, "name", "api_key");
  cJSON_AddStringToObject(txt, "value", devcfg.api_key);
  cJSON_AddItemToArray(api_elements, txt);

  cJSON_AddItemToArray(root, api);

  /* ----- Alarm section ----- */
  // cJSON *alarm = cJSON_CreateObject();
  // cJSON_AddStringToObject(alarm, "label", "Alarm");
  // cJSON_AddStringToObject(alarm, "name", "expand_alarm");
  // cJSON_AddNumberToObject(alarm, "value", 1);

  // cJSON *alarm_elements = cJSON_CreateArray();
  // cJSON_AddItemToObject(alarm, "elements", alarm_elements);

  // txt = cJSON_CreateObject();
  // cJSON_AddStringToObject(txt, "type", "text");
  // cJSON_AddStringToObject(txt, "label", "Alarm Duration");
  // cJSON_AddStringToObject(txt, "name", "alarm_duration_limit");
  // cJSON_AddNumberToObject(txt, "value", alarm_duration_limit);
  // cJSON_AddItemToArray(alarm_elements, txt);

  // cJSON_AddItemToArray(root, alarm);

  /* ----- Page section ----- */
  cJSON *page = cJSON_CreateObject();
  cJSON_AddStringToObject(page, "label", "Page");
  cJSON_AddStringToObject(page, "name", "expand_page");
  cJSON_AddNumberToObject(page, "value", 1);

  cJSON *page_elements = cJSON_CreateArray();
  cJSON_AddItemToObject(page, "elements", page_elements);

  txt = cJSON_CreateObject();
  cJSON_AddStringToObject(txt, "type", "button");
  cJSON_AddStringToObject(txt, "label", "UPDATE");
  cJSON_AddStringToObject(txt, "name", "update");
  cJSON_AddStringToObject(txt, "value", "update");
  cJSON_AddItemToArray(page_elements, txt);

  cJSON_AddItemToArray(root, page);

  /* ---------- Send JSON response ---------- */
  char *json_out = cJSON_PrintUnformatted(root);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_out, HTTPD_RESP_USE_STRLEN);

  free(json_out);
  cJSON_Delete(root);

  return ESP_OK;
}

/***********************************************************
 app page handler
***********************************************************/
esp_err_t app_handler(httpd_req_t *req)
{
  uint8_t refresh = 2;
  /* ---------- Receive POST body ---------- */
  int total = req->content_len;
  char *buf = malloc(total + 1);
  if (!buf)
    return ESP_ERR_NO_MEM;

  int received = httpd_req_recv(req, buf, total);
  if (received <= 0)
  {
    free(buf);
    return ESP_FAIL;
  }
  buf[received] = 0;

  if (strcmp(buf, "{}") != 0)
  {
    cJSON *doc = cJSON_Parse(buf);
    if (doc)
    {
      cJSON *item;

      item = cJSON_GetObjectItem(doc, "refresh");
      if (item)
        refresh = item->valueint;

      cJSON_Delete(doc);
    }
  }
  free(buf);

  /* ---------- Build JSON response ---------- */
  cJSON *root = cJSON_CreateArray();

  /* ----- WS section ----- */
  cJSON *ws = cJSON_CreateObject();
  cJSON_AddStringToObject(ws, "label", "WS");
  cJSON_AddStringToObject(ws, "name", "expand_ws");
  cJSON_AddNumberToObject(ws, "value", 1);
  cJSON *ws_elements = cJSON_CreateArray();
  cJSON_AddItemToObject(ws, "elements", ws_elements);

  cJSON *txt = cJSON_CreateObject();
  cJSON_AddStringToObject(txt, "type", "textarea");
  cJSON_AddStringToObject(txt, "label", "WS Debug");
  cJSON_AddStringToObject(txt, "name", "ws_debug");
  cJSON_AddStringToObject(txt, "value", "");
  cJSON_AddItemToArray(ws_elements, txt);

  cJSON_AddItemToArray(root, ws);

  /* ----- Page section ----- */
  cJSON *page = cJSON_CreateObject();
  cJSON_AddStringToObject(page, "label", "Page");
  cJSON_AddStringToObject(page, "name", "expand_page");
  cJSON_AddNumberToObject(page, "value", 1);

  cJSON *page_elements = cJSON_CreateArray();
  cJSON_AddItemToObject(page, "elements", page_elements);

  txt = cJSON_CreateObject();
  cJSON_AddStringToObject(txt, "type", "refresh");
  cJSON_AddStringToObject(txt, "label", "Refresh");
  cJSON_AddStringToObject(txt, "name", "refresh");
  cJSON_AddNumberToObject(txt, "value", refresh);
  cJSON_AddItemToArray(page_elements, txt);

  cJSON_AddItemToArray(root, page);

  /* ---------- Send JSON response ---------- */
  char *json_out = cJSON_PrintUnformatted(root);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_out, HTTPD_RESP_USE_STRLEN);

  free(json_out);
  cJSON_Delete(root);

  return ESP_OK;
}

/***********************************************************
 firmware update handler
***********************************************************/
static esp_err_t ota_update_handler(httpd_req_t *req)
{
  esp_ota_handle_t ota_handle;
  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
  ESP_ERROR_CHECK(esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle));
  char buf[1024];
  int remaining = req->content_len;
  while (remaining > 0)
  {
    int recv_len = httpd_req_recv(req, buf, remaining > sizeof(buf) ? sizeof(buf) : remaining);
    if (recv_len <= 0)
      return ESP_FAIL;
    esp_ota_write(ota_handle, buf, recv_len);
    remaining -= recv_len;
  }
  ESP_ERROR_CHECK(esp_ota_end(ota_handle));
  ESP_ERROR_CHECK(esp_ota_set_boot_partition(update_partition));
  httpd_resp_sendstr(req, "Update complete. Rebooting...");
  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
  return ESP_OK;
}

/***********************************************************
 firmware upgrade page handler
***********************************************************/
static esp_err_t firmware_handler(httpd_req_t *req)
{
  /*
  const char *html =
      "<html><body><h2>ESP32 OTA Update</h2>"
      "<form method='POST' action='/update' enctype='application/octet-stream'>"
      "<input type='file' name='firmware'><br><br>"
      "<input type='submit' value='Upload'>"
      "</form></body></html>";
  httpd_resp_sendstr(req, html);
  */
  /* -------- Build JSON -------- */
  cJSON *root = cJSON_CreateArray();

  cJSON *section = cJSON_CreateObject();
  cJSON_AddStringToObject(section, "label", "Firmware Upgrade");
  cJSON_AddStringToObject(section, "name", "firmware_upgrade");
  cJSON_AddStringToObject(section, "value", "1");

  cJSON *elements = cJSON_CreateArray();
  cJSON_AddItemToObject(section, "elements", elements);

  /* File input */
  cJSON *file = cJSON_CreateObject();
  cJSON_AddStringToObject(file, "type", "file");
  cJSON_AddStringToObject(file, "label", "File");
  cJSON_AddStringToObject(file, "name", "file");
  cJSON_AddStringToObject(file, "value", "");
  cJSON_AddStringToObject(file, "accept", ".bin");
  cJSON_AddItemToArray(elements, file);

  /* Upload button */
  cJSON *btn = cJSON_CreateObject();
  cJSON_AddStringToObject(btn, "type", "button");
  cJSON_AddStringToObject(btn, "label", "UPLOAD");
  cJSON_AddStringToObject(btn, "name", "upload");
  cJSON_AddStringToObject(btn, "value", "upload");
  cJSON_AddItemToArray(elements, btn);

  cJSON_AddItemToArray(root, section);

  char *json_out = cJSON_PrintUnformatted(root);

  /* -------- Send response -------- */
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_out, HTTPD_RESP_USE_STRLEN);

  free(json_out);
  cJSON_Delete(root);

  return ESP_OK;
}

/***********************************************************
 wifi scan handler
***********************************************************/
static esp_err_t wifi_scan_handler(httpd_req_t *req)
{
  wifi_scan_config_t scan_config = {
      .ssid = 0,
      .bssid = 0,
      .channel = 0,
      .show_hidden = true};

  /* blocking scan */
  ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

  uint16_t ap_count = 0;
  esp_wifi_scan_get_ap_num(&ap_count);

  wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);

  if (!ap_records)
    return ESP_ERR_NO_MEM;

  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));

  cJSON *root = cJSON_CreateArray();

  for (int i = 0; i < ap_count; i++)
  {
    cJSON *obj = cJSON_CreateObject();

    int rssi = ap_records[i].rssi;

    /* RSSI quality */
    int quality;
    if (rssi <= -100)
      quality = 0;
    else if (rssi >= -50)
      quality = 100;
    else
      quality = 2 * (rssi + 100);

    char bssid[18];
    sprintf(bssid,
            "%02X:%02X:%02X:%02X:%02X:%02X",
            ap_records[i].bssid[0],
            ap_records[i].bssid[1],
            ap_records[i].bssid[2],
            ap_records[i].bssid[3],
            ap_records[i].bssid[4],
            ap_records[i].bssid[5]);

    cJSON_AddNumberToObject(obj, "rssi", rssi);
    cJSON_AddNumberToObject(obj, "signal", quality);
    cJSON_AddStringToObject(obj, "ssid", (char *)ap_records[i].ssid);
    cJSON_AddStringToObject(obj, "bssid", bssid);
    cJSON_AddNumberToObject(obj, "channel", ap_records[i].primary);
    cJSON_AddNumberToObject(obj, "secure", ap_records[i].authmode);
    cJSON_AddBoolToObject(obj, "hidden", strlen((char *)ap_records[i].ssid) == 0);

    cJSON_AddItemToArray(root, obj);
  }

  char *json = cJSON_PrintUnformatted(root);

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);

  free(json);
  cJSON_Delete(root);
  free(ap_records);

  return ESP_OK;
}

/***********************************************************
 websocket handler and manager
***********************************************************/
static void ws_add_client(httpd_req_t *req)
{
  int fd = httpd_req_to_sockfd(req);
  ws_mgr.server = req->handle;

  xSemaphoreTake(ws_mgr.lock, portMAX_DELAY);

  if (ws_mgr.count < MAX_WS_CLIENTS)
  {
    ws_mgr.clients[ws_mgr.count++] = fd;
    ESP_LOGI(TAG, "WS client added fd=%d total=%d",
             fd, ws_mgr.count);
  }

  xSemaphoreGive(ws_mgr.lock);
}

static void ws_remove_client(int fd)
{
  xSemaphoreTake(ws_mgr.lock, portMAX_DELAY);

  for (int i = 0; i < ws_mgr.count; i++)
  {
    if (ws_mgr.clients[i] == fd)
    {
      for (int j = i; j < ws_mgr.count - 1; j++)
        ws_mgr.clients[j] = ws_mgr.clients[j + 1];

      ws_mgr.count--;
      break;
    }
  }

  xSemaphoreGive(ws_mgr.lock);
}

void ws_broadcast(const char *data, size_t len)
{
  if (!ws_mgr.server)
    return;

  httpd_ws_frame_t ws_pkt = {
      .payload = (uint8_t *)data,
      .len = len,
      .type = HTTPD_WS_TYPE_TEXT};

  xSemaphoreTake(ws_mgr.lock, portMAX_DELAY);

  for (int i = 0; i < ws_mgr.count;)
  {
    int fd = ws_mgr.clients[i];

    esp_err_t err = httpd_ws_send_frame_async(
        ws_mgr.server,
        fd,
        &ws_pkt);

    if (err != ESP_OK)
    {
      ESP_LOGW(TAG, "Removing dead WS client fd=%d", fd);

      /* remove client */
      for (int j = i; j < ws_mgr.count - 1; j++)
        ws_mgr.clients[j] = ws_mgr.clients[j + 1];

      ws_mgr.count--;
      continue; // don't increment i
    }

    i++;
  }

  xSemaphoreGive(ws_mgr.lock);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
  if (req->method == HTTP_GET)
  {
    ESP_LOGI(TAG, "WebSocket handshake done");
    ws_add_client(req);
    return ESP_OK;
  }

  httpd_ws_frame_t ws_pkt = {0};
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;

  /* Get frame length */
  esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
  if (ret != ESP_OK)
    return ret;

  if (ws_pkt.len)
  {
    ws_pkt.payload = malloc(ws_pkt.len + 1);
    httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    ws_pkt.payload[ws_pkt.len] = 0;
  }

  if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE)
  {
    ws_remove_client(httpd_req_to_sockfd(req));
  }

  if (ws_pkt.len)
  {
    ESP_LOGI(TAG, "Received: %s", ws_pkt.payload);
    /* Echo back */
    httpd_ws_send_frame(req, &ws_pkt);
    free(ws_pkt.payload);
  }

  return ESP_OK;
}

/***********************************************************
 main webserver start function
***********************************************************/
void webserver_start(void)
{
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  httpd_handle_t server = NULL;
  if (httpd_start(&server, &config) == ESP_OK)
  {
    httpd_uri_t uris[] = {
        {"/", HTTP_GET, index_handler, NULL, false, false, NULL},
        {"/system", HTTP_POST, system_handler, NULL, false, false, NULL},
        {"/config", HTTP_POST, config_handler, NULL, false, false, NULL},
        {"/app", HTTP_POST, app_handler, NULL, false, false, NULL},
        {"/firmware", HTTP_POST, firmware_handler, NULL, false, false, NULL},
        {"/upload", HTTP_POST, ota_update_handler, NULL, false, false, NULL},
        {"/scan", HTTP_GET, wifi_scan_handler, NULL, false, false, NULL},
        {"/ws", HTTP_GET, ws_handler, NULL, true, true, NULL}};
    for (int i = 0; i < 8; i++)
      httpd_register_uri_handler(server, &uris[i]);

    ws_mgr.lock = xSemaphoreCreateMutex();
  }
  ESP_LOGI(TAG, "webserver started");
}
