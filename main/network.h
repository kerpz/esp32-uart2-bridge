#pragma once

#include "esp_netif.h"

extern esp_netif_t *ap_netif;
extern esp_netif_t *sta_netif;

void network_start(void);