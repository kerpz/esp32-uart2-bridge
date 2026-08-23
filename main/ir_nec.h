#pragma once

#include <stdint.h>

#define IR_NEC_RXD 18
#define IR_NEC_TXD 19

void ir_nec_start(void);
int ir_nec_send(uint16_t address, uint8_t command);