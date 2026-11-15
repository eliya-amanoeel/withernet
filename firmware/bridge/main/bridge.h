// Shared between the bridge (main.c) and the management web server (web.c).
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool mac_learned;
    uint8_t device_mac[6];
    bool eth_up;
    int eth_mbit;
    bool eth_full_duplex;
    bool wifi_up;
    uint32_t wifi_disconnects;
    int last_disconnect_reason;
    uint32_t eth_to_wifi;          // frames forwarded
    uint32_t wifi_to_eth;
    uint32_t eth_to_wifi_dropped;  // queue full or Wi-Fi TX busy too long
} bridge_status_t;

void bridge_get_status(bridge_status_t *out);

// Status page, /status.json and firmware upload on the management IP.
void web_start(void);
