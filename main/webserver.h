#pragma once

#define APPCODE "ESP32-WEBPANEL-DUAL"

#define MAX_WS_CLIENTS 4

void ws_broadcast(const char *data, size_t len);
void webserver_start(void);