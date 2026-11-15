// Copy this file to secrets.h (ignored by git) and fill in your Wi-Fi.
#pragma once

#define WIFI_SSID "your-wifi-name"
#define WIFI_PASS "your-wifi-password"

// The dongle's own fixed IP, outside the router's DHCP pool. One per dongle:
// Nibe 192.168.68.40, Checkwatt 192.168.68.41 (see handoff.md).
#define MGMT_IP      "192.168.68.40"
#define MGMT_NETMASK "255.255.252.0"
#define MGMT_GATEWAY "192.168.68.1"

// Password for the status page and firmware upload (user "admin").
#define ADMIN_PASS "choose-a-password"
