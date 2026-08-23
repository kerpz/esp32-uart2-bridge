#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "adc.h"

static const char *TAG = "adc";
static adc_oneshot_unit_handle_t adc_handle;
volatile int adc_voltage_mv;

int adc_read_voltage_mv(void)
{
  int raw;
  esp_err_t err = adc_oneshot_read(adc_handle, ADC_CHANNEL_0, &raw);
  if (err != ESP_OK)
    return -1;

  int adc_pin_voltage_mv = (raw * 3300) / 4095;
  return (adc_pin_voltage_mv * (ADC_DIVIDER_R1_OHM + ADC_DIVIDER_R2_OHM)) /
         ADC_DIVIDER_R2_OHM;
}

static void adc_task(void *arg)
{
  while (1)
  {
    int voltage_mv = adc_read_voltage_mv();
    if (voltage_mv >= 0)
      adc_voltage_mv = voltage_mv;
    else
      ESP_LOGW(TAG, "ADC read failed");

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void adc_start(void)
{
  adc_oneshot_unit_init_cfg_t init_config = {
      .unit_id = ADC_UNIT_1,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

  adc_oneshot_chan_cfg_t channel_config = {
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(
      adc_handle, ADC_CHANNEL_0, &channel_config));

  xTaskCreate(adc_task, "adc_task", 2048, NULL, 5, NULL);
  ESP_LOGI(TAG, "ADC started on GPIO%d", ADC_INPUT_GPIO);
}
