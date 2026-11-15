// Copy this file to secrets.h (ignored by git) and fill in your Wi-Fi.
#pragma once

#define WIFI_SSID "your-wifi-name"
#define WIFI_PASS "your-wifi-password"

// Network for the dongle's own fixed IP. The IP itself is per dongle and set in
// main/CMakeLists.txt (BOARD=nibe, BOARD=checkwatt).
#define MGMT_NETMASK "255.255.252.0"
#define MGMT_GATEWAY "192.168.68.1"

// Password for the status page and firmware upload (user "admin"), one per
// dongle, 20-100 characters. Generate each with: openssl rand -base64 24
#define ADMIN_PASS_NIBE      "choose-a-password-for-the-nibe"
#define ADMIN_PASS_CHECKWATT "choose-a-password-for-the-checkwatt"
