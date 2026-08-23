#include "ir_nec.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"

#include "driver/rmt.h"
#include "esp_err.h"
#include "esp_log.h"

#include "webserver.h"

#define IR_NEC_RX_CHANNEL RMT_CHANNEL_0
#define IR_NEC_TX_CHANNEL RMT_CHANNEL_1
#define IR_NEC_TICK_US 1
#define IR_NEC_TOLERANCE_US 700
#define IR_NEC_DATA_ITEMS 34

static const char *TAG = "ir_nec";
static RingbufHandle_t rx_ringbuf;

static bool timing_matches(uint16_t actual, uint16_t expected)
{
  return actual >= expected - IR_NEC_TOLERANCE_US &&
         actual <= expected + IR_NEC_TOLERANCE_US;
}

static bool parse_nec(const rmt_item32_t *items, size_t item_count,
                      uint16_t *address, uint8_t *command)
{
  if (item_count < IR_NEC_DATA_ITEMS || items[0].level0 != 0 || items[0].level1 != 1 ||
      !timing_matches(items[0].duration0, 9000) ||
      !timing_matches(items[0].duration1, 4500))
    return false;

  uint32_t data = 0;
  for (int bit = 0; bit < 32; bit++)
  {
    const rmt_item32_t item = items[bit + 1];
    if (item.level0 != 0 || item.level1 != 1 || !timing_matches(item.duration0, 560))
      return false;

    uint16_t expected_space = item.duration1 > 1000 ? 1690 : 560;
    if (!timing_matches(item.duration1, expected_space))
      return false;
    data >>= 1;
    if (item.duration1 > 1000)
      data |= 0x80000000;
  }

  uint8_t address_low = data & 0xff;
  uint8_t address_high = (data >> 8) & 0xff;
  uint8_t command_value = (data >> 16) & 0xff;
  uint8_t command_inverse = (data >> 24) & 0xff;
  if ((uint8_t)~command_value != command_inverse)
    return false;

  *address = (uint16_t)address_low | ((uint16_t)address_high << 8);
  *command = command_value;
  return true;
}

static void ir_nec_rx_task(void *arg)
{
  (void)arg;
  while (true)
  {
    size_t item_size = 0;
    rmt_item32_t *items = (rmt_item32_t *)xRingbufferReceive(rx_ringbuf, &item_size,
                                                             pdMS_TO_TICKS(1000));
    if (!items)
      continue;

    uint16_t address;
    uint8_t command;
    if (parse_nec(items, item_size / sizeof(rmt_item32_t), &address, &command))
    {
      char message[80];
      int length = snprintf(message, sizeof(message),
                            "IR RX: address=0x%04X command=0x%02X\n",
                            address, command);
      ws_broadcast(message, length);
      ESP_LOGI(TAG, "received address=0x%04X command=0x%02X", address, command);
    }
    vRingbufferReturnItem(rx_ringbuf, (void *)items);
  }
}

int ir_nec_send(uint16_t address, uint8_t command)
{
  rmt_item32_t items[34] = {0};
  int item = 0;
  items[item].duration0 = 9000;
  items[item].level0 = 1;
  items[item].duration1 = 4500;
  items[item++].level1 = 0;
  uint32_t data = address | ((uint32_t)(uint8_t)~address << 8) |
                  ((uint32_t)command << 16) | ((uint32_t)(uint8_t)~command << 24);

  for (int bit = 0; bit < 32; bit++)
  {
    items[item].duration0 = 560;
    items[item].level0 = 1;
    items[item].duration1 = (data & (1u << bit)) ? 1690 : 560;
    items[item++].level1 = 0;
  }
  items[item].duration0 = 560;
  items[item].level0 = 1;
  items[item].duration1 = 0;
  items[item++].level1 = 0;
  esp_err_t err = rmt_write_items(IR_NEC_TX_CHANNEL, items, item, true);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "send failed: 0x%x", err);
    return -1;
  }
  ESP_LOGI(TAG, "sent address=0x%04X command=0x%02X", address, command);
  return 0;
}

void ir_nec_start(void)
{
  rmt_config_t rx_config = RMT_DEFAULT_CONFIG_RX(IR_NEC_RXD, IR_NEC_RX_CHANNEL);
  rx_config.clk_div = 80;
  rx_config.rx_config.filter_en = true;
  rx_config.rx_config.filter_ticks_thresh = 100;
  rx_config.rx_config.idle_threshold = 12000;
  ESP_ERROR_CHECK(rmt_config(&rx_config));
  ESP_ERROR_CHECK(rmt_driver_install(IR_NEC_RX_CHANNEL, 4096, 0));
  ESP_ERROR_CHECK(rmt_get_ringbuf_handle(IR_NEC_RX_CHANNEL, &rx_ringbuf));
  ESP_ERROR_CHECK(rmt_rx_start(IR_NEC_RX_CHANNEL, true));

  rmt_config_t tx_config = RMT_DEFAULT_CONFIG_TX(IR_NEC_TXD, IR_NEC_TX_CHANNEL);
  tx_config.clk_div = 80;
  tx_config.tx_config.carrier_freq_hz = 38000;
  tx_config.tx_config.carrier_duty_percent = 33;
  tx_config.tx_config.carrier_en = true;
  ESP_ERROR_CHECK(rmt_config(&tx_config));
  ESP_ERROR_CHECK(rmt_driver_install(IR_NEC_TX_CHANNEL, 0, 0));
  xTaskCreate(ir_nec_rx_task, "ir_nec_rx", 3072, NULL, 5, NULL);
  ESP_LOGI(TAG, "NEC RX GPIO%d / TX GPIO%d ready", IR_NEC_RXD, IR_NEC_TXD);
}