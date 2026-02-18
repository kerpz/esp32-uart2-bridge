#pragma once

// #define UART2_BAUD_RATE 115200
#define UART2_BAUD_RATE 9600

#define UART2_PORT UART_NUM_2
#define UART2_TXD 17 // change as needed
#define UART2_RXD 16 // change as needed
#define UART2_RTS UART_PIN_NO_CHANGE
#define UART2_CTS UART_PIN_NO_CHANGE

#define UART2_TCP_BRIDGE_PORT 5000

#define BUF_SIZE 1024

void uart2_start(void);