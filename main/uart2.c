#include "driver/uart.h"
#include "driver/gpio.h"

/* TCP / sockets */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "esp_log.h"

#include "webserver.h"
#include "nmea_parser.h"
#include "uart2.h"

static const char *TAG = "uart2";

/* Active TCP client */
static int tcp_client_sock = -1;

/* -------------------------------------------------------
 * TCP bridge task (TCP -> UART)
 * -----------------------------------------------------*/
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

  while (1)
  {
    sock = accept(listen_sock, NULL, NULL);
    ESP_LOGI(TAG, "TCP client connected");

    tcp_client_sock = sock;

    while (1)
    {
      uint8_t buf[128];

      /* TCP -> UART */
      int len = recv(sock, buf, sizeof(buf), MSG_DONTWAIT);

      if (len > 0)
      {
        uart_write_bytes(UART_NUM_2, buf, len);
      }
      else if (len == 0)
      {
        /* client disconnected */
        break;
      }
      else if (errno != EAGAIN && errno != EWOULDBLOCK)
      {
        break;
      }

      vTaskDelay(pdMS_TO_TICKS(20));
    }

    close(sock);
    tcp_client_sock = -1;

    ESP_LOGI(TAG, "TCP client disconnected");
  }
}

/* -------------------------------------------------------
 * Unified UART reader task
 * UART -> LOG + WS + TCP + NMEA parsing + ESP32 time update
 * -----------------------------------------------------*/
void uart2_task(void *arg)
{
  uint8_t buf[256];

  static char linebuf[256];
  static int linepos = 0;

  while (1)
  {
    int len = uart_read_bytes(
        UART_NUM_2,
        buf,
        sizeof(buf),
        pdMS_TO_TICKS(100));

    if (len > 0)
    {
      /* Send raw data to WebSocket clients */
      ws_broadcast((char *)buf, len);

      /* Send raw data to TCP client if connected */
      if (tcp_client_sock >= 0)
      {
        send(tcp_client_sock, buf, len, 0);
      }

      /* Print raw GPS output for debugging */
      ESP_LOGI("GPS_RAW", "%.*s", len, buf);

      /* Build NMEA lines and parse them */
      for (int i = 0; i < len; i++)
      {
        char c = buf[i];

        if (c == '\n' || c == '\r')
        {
          if (linepos > 0)
          {
            linebuf[linepos] = 0; // terminate line

            /* Parse NMEA sentence */
            nmea_parse_line(linebuf);

            gps_data_t *g = gps_get_data();

            /* Update ESP32 system time from GPS */
            if (g->fix)
            {
              struct tm t = {0};

              // Parse hhmmss from utc_time
              if (strlen(g->utc_time) >= 6)
                sscanf(g->utc_time, "%2d%2d%2d",
                       &t.tm_hour,
                       &t.tm_min,
                       &t.tm_sec);

              // Parse ddmmyy from utc_date
              int day, mon, year;
              if (strlen(g->utc_date) >= 6)
              {
                sscanf(g->utc_date, "%2d%2d%2d",
                       &day, &mon, &year);
                t.tm_mday = day;
                t.tm_mon = mon - 1;
                t.tm_year = year + 100; // 2000+
              }

              time_t epoch = mktime(&t);
              struct timeval now = {
                  .tv_sec = epoch,
                  .tv_usec = 0};
              settimeofday(&now, NULL);
            }

            /* Print parsed GPS info */
            ESP_LOGI("GPS_PARSED",
                     "UTC:%s Fix:%d Lat:%.6f Lon:%.6f Sat:%d Alt:%.1f",
                     g->utc_time,
                     g->fix,
                     g->latitude,
                     g->longitude,
                     g->satellites,
                     g->altitude);

            linepos = 0; // reset line buffer
          }
        }
        else
        {
          if (linepos < sizeof(linebuf) - 1)
            linebuf[linepos++] = c;
        }
      }
    }
  }
}

/* -------------------------------------------------------
 * UART initialization
 * -----------------------------------------------------*/
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
  ESP_ERROR_CHECK(uart_set_pin(UART2_PORT, UART2_TXD, UART2_RXD,
                               UART2_RTS, UART2_CTS));

  /* Tasks */
  xTaskCreate(uart2_task, "uart2_task", 4096, NULL, 5, NULL);
  xTaskCreate(uart2_tcp_task, "uart2_tcp", 4096, NULL, 5, NULL);

  ESP_LOGI(TAG, "uart2 started");
}
