#ifndef ADC_H
#define ADC_H

#include <stdint.h>

#define ADC_INPUT_GPIO 36
#define ADC_DIVIDER_R1_OHM 100000
#define ADC_DIVIDER_R2_OHM 10000

extern volatile int adc_voltage_mv;

void adc_start(void);
int adc_read_voltage_mv(void);
#endif
