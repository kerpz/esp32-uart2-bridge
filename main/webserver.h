#ifndef WEBSERVER_H
#define WEBSERVER_H

#define APPCODE "ESP32-UART2-BRIDGE"

#define MAX_WS_CLIENTS 4

void ws_broadcast(const char *data, size_t len);
void webserver_start(void);
#endif
