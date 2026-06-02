#include <stdio.h>
#include "devinfo.h"
#include "esp_mac.h"

devinfo_t device = {
    .serial     = "",
    .sw_major   = 1,
    .sw_minor   = 0,
    .sw_patch   = 1,
    .hw_rev     = 1,
    .build_time = __DATE__ " " __TIME__,
    .model      = "SX-70 Model Z",
};

void devinfo_init(void)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(device.serial, sizeof(device.serial),
            "%02X%02X%02X%02X%02X%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
