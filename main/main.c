#include "storage.h"
#include "network.h"
#include "webserver.h"
#include "uart2.h"

void app_main(void)
{
    storage_start();
    network_start();
    webserver_start();
    uart2_start();
}
