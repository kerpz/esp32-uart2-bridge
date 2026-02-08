#include "driver/uart.h"
#include "driver/gpio.h"

/* TCP / sockets */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "esp_log.h"

#include "webserver.h"
#include "uart2.h"

static const char *TAG = "uart2";

void uart2_tcp_task(void *arg)
{
  int listen_sock, sock;
  struct sockaddr_in addr;

  listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

  addr.sin_family = AF_INET;
  addr.sin_port = htons(UART2_TCP_BRIDGE_PORT);
  addr.sin_addr.s_addr = INADDR_ANY;

  bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr));
  listen(listen_sock, 1);

  uint8_t rxbuf[256];

  while (1)
  {
    sock = accept(listen_sock, NULL, NULL);
    ESP_LOGI(TAG, "Client connected");

    while (1)
    {
      /* TCP -> UART */
      int len = recv(sock, rxbuf, sizeof(rxbuf), MSG_DONTWAIT);
      if (len > 0)
      {
        uart_write_bytes(UART_NUM_2, rxbuf, len);
      }

      /* UART -> TCP */
      len = uart_read_bytes(UART_NUM_2, rxbuf, sizeof(rxbuf), pdMS_TO_TICKS(20));

      if (len > 0)
      {
        send(sock, rxbuf, len, 0);
      }

      vTaskDelay(pdMS_TO_TICKS(10));
    }

    close(sock);
  }
}

void uart2_ws_task(void *arg)
{
  uint8_t buf[256];

  while (1)
  {
    int len = uart_read_bytes(UART_NUM_2, buf, sizeof(buf) - 1, pdMS_TO_TICKS(100));

    if (len > 0)
    {
      buf[len] = 0;
      ws_broadcast((char *)buf, len);
    }
  }
}

void uart2_start(void)
{
  uart_config_t uart_config = {
      .baud_rate = UART2_BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  ESP_ERROR_CHECK(uart_driver_install(UART2_PORT, BUF_SIZE, BUF_SIZE, 0, NULL, 0));
  ESP_ERROR_CHECK(uart_param_config(UART2_PORT, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(UART2_PORT, UART2_TXD, UART2_RXD, UART2_RTS, UART2_CTS));

  xTaskCreate(uart2_ws_task, "uart2_ws", 4096, NULL, 5, NULL);
  xTaskCreate(uart2_tcp_task, "uart2_tcp", 4096, NULL, 5, NULL);

  ESP_LOGI(TAG, "uart2 started");
}
