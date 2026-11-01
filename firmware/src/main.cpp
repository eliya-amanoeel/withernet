// Firmware for the WT32-ETH01 dongle: joins the home Wi-Fi and shares it over
// Ethernet, like a small home router (NAT). The device on the cable gets an
// address from our own DHCP server, on a separate small network.
// Updates go over the wire (the Nano); OTA is a future feature.
#include <Arduino.h>
#include <WiFi.h>
#include <ETH.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <dhcpserver/dhcpserver.h>
#include <dhcpserver/dhcpserver_options.h>

// WT32-ETH01 wiring, see docs/hardware-notes.md.
// GPIO16 powers the 50 MHz oscillator; ETH.begin drives it high.
#define PHY_ADDR  1
#define PHY_MDC   23
#define PHY_MDIO  18
#define PHY_POWER 16

// The cable side. The Deco uses 192.168.68.0/22, so stay well away from it.
const IPAddress LAN_IP(10, 10, 0, 1);
const IPAddress LAN_MASK(255, 255, 255, 0);

static dhcps_t *dhcpServer = nullptr;
static ip_addr_t lanDns;

// Called by the DHCP server when it hands out an address on the cable side.
static void onLease(void *, u8_t ip[4], u8_t mac[6]) {
  Serial.printf("Ethernet: gave %d.%d.%d.%d to %02x:%02x:%02x:%02x:%02x:%02x\n",
                ip[0], ip[1], ip[2], ip[3], mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// lwIP's DHCP server must be set up from the TCP/IP task.
static esp_err_t startDhcpServer(void *) {
  dhcpServer = dhcps_new();
  // Hand out the home network's DNS server, not our own address (we run no DNS).
  dhcps_offer_t offerDns = OFFER_DNS;
  dhcps_set_option_info(dhcpServer, DOMAIN_NAME_SERVER, &offerDns, sizeof(offerDns));
  dhcps_dns_setserver(dhcpServer, &lanDns);
  dhcps_set_new_lease_cb(dhcpServer, onLease, nullptr);
  ip4_addr_t ip;
  ip.addr = static_cast<uint32_t>(LAN_IP);
  struct netif *lan = (struct netif *)esp_netif_get_netif_impl(ETH.netif());
  return dhcps_start(dhcpServer, lan, ip) == ERR_OK ? ESP_OK : ESP_FAIL;
}

// Once Wi-Fi is up: serve addresses on the cable side. Done once; Wi-Fi
// reconnects keep the same setup. NAT is switched on later, in updateNat().
void startRouting(IPAddress dns) {
  if (dhcpServer) {
    return;
  }
  IP_ADDR4(&lanDns, dns[0], dns[1], dns[2], dns[3]);
  if (esp_netif_tcpip_exec(startDhcpServer, nullptr) != ESP_OK) {
    Serial.println("Router: DHCP server failed to start");
    return;
  }
  Serial.printf("Router: DHCP on, cable side %s, DNS %s\n",
                LAN_IP.toString().c_str(), dns.toString().c_str());
}

// NAT is switched on for the inside interface: traffic from the cable side
// goes out with the dongle's Wi-Fi address. lwIP only allows that while the
// cable is plugged in, so keep trying, and redo it after a replug.
void updateNat() {
  static bool natOn = false;
  static uint32_t lastTry = 0;
  if (!dhcpServer) {
    return;
  }
  if (!ETH.linkUp()) {
    natOn = false;
    return;
  }
  if (natOn || millis() - lastTry < 5000) {
    return;
  }
  lastTry = millis();
  esp_err_t err = esp_netif_napt_enable(ETH.netif());
  if (err == ESP_OK) {
    natOn = true;
    // Internet traffic must leave over Wi-Fi ("sta"), never the cable ("eth").
    const char *via = esp_netif_get_desc(esp_netif_get_default_netif());
    Serial.printf("Router: NAT on, internet via %s\n", via);
    if (strcmp(via, "sta") != 0) {
      WiFi.STA.setDefault();
      Serial.printf("Router: switched internet back to %s\n",
                    esp_netif_get_desc(esp_netif_get_default_netif()));
    }
  } else {
    Serial.printf("Router: NAT failed (%s), retrying\n", esp_err_to_name(err));
  }
}

void onNetworkEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
      // Again here, in case the call in setup() came too early to stick.
      WiFi.setSleep(false);
      wifi_ps_type_t ps;
      esp_wifi_get_ps(&ps);
      Serial.printf("Wi-Fi: connected, IP %s, signal %d dBm, power save %s\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                    ps == WIFI_PS_NONE ? "off" : "ON");
      // Internet traffic must leave over Wi-Fi, never the cable.
      WiFi.STA.setDefault();
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      wifi_err_reason_t reason = (wifi_err_reason_t)info.wifi_sta_disconnected.reason;
      Serial.printf("Wi-Fi: disconnected (%d %s)\n", reason, WiFi.disconnectReasonName(reason));
      break;
    }
    case ARDUINO_EVENT_ETH_START:
      Serial.println("Ethernet: started (PHY found)");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.printf("Ethernet: link up, %d Mbps\n", ETH.linkSpeed());
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("Ethernet: link down");
      break;
    default:
      break;
  }
}

// Scans for every mesh node broadcasting our network and joins the strongest,
// locked to its address. Left to itself, the ESP (or the Deco's steering) kept
// picking a weak node at -70 dBm over one at -42 dBm, and then lost it.
void joinStrongestNode() {
  int n = WiFi.scanNetworks();
  int best = -1;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) != WIFI_SSID) {
      continue;
    }
    Serial.printf("  node %s, channel %d, signal %d dBm\n",
                  WiFi.BSSIDstr(i).c_str(), WiFi.channel(i), WiFi.RSSI(i));
    if (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best)) {
      best = i;
    }
  }
  if (best < 0) {
    Serial.println("Wi-Fi: our network not found");
  } else {
    Serial.printf("Wi-Fi: joining node %s\n", WiFi.BSSIDstr(best).c_str());
    WiFi.begin(WIFI_SSID, WIFI_PASS, WiFi.channel(best), WiFi.BSSID(best));
    // No modem sleep: we're mains-powered, and its stalls cause packet loss.
    WiFi.setSleep(false);
  }
  WiFi.scanDelete();
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nwithernet starting");

  Network.onEvent(onNetworkEvent);

  WiFi.setHostname("withernet");
  WiFi.mode(WIFI_STA);
  // We reconnect ourselves (see loop), so a lost node means a fresh pick.
  WiFi.setAutoReconnect(false);
}

// While Wi-Fi is down, pick the strongest node again every 15 seconds.
void keepWiFiUp() {
  static uint32_t lastTry = 0;
  static bool tried = false;
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  if (tried && millis() - lastTry < 15000) {
    return;
  }
  tried = true;
  lastTry = millis();
  WiFi.disconnect();
  joinStrongestNode();
}

bool startEthernet(IPAddress dns) {
  if (!ETH.begin(ETH_PHY_LAN8720, PHY_ADDR, PHY_MDC, PHY_MDIO, PHY_POWER, ETH_CLOCK_GPIO0_IN)) {
    Serial.println("Ethernet: failed to start");
    return false;
  }
  // Fixed address on the cable side, with ourselves as gateway: the DHCP server
  // only offers a gateway to the device if this one is set.
  // lwIP has one DNS list for all interfaces, so pass Wi-Fi's DNS on, or this
  // would wipe it.
  if (!ETH.config(LAN_IP, LAN_IP, LAN_MASK, dns)) {
    Serial.println("Ethernet: could not set the cable-side address");
    return false;
  }
  // Now that the cable side has a gateway too, make sure our own traffic
  // still leaves over Wi-Fi, and that switching back kept the DNS server.
  WiFi.STA.setDefault();
  WiFi.STA.dnsIP(0, dns);
  return true;
}

// Checks the ESP itself can reach the internet (DNS lookup plus a connection),
// apart from forwarding traffic for the cable side.
void checkInternet() {
  NetworkClient client;
  if (client.connect("example.com", 80, 5000)) {
    Serial.println("Internet: ESP itself reaches example.com");
    client.stop();
  } else {
    Serial.println("Internet: ESP itself can NOT reach example.com");
  }
}

void loop() {
  keepWiFiUp();
  // Ethernet only starts once Wi-Fi is up: with the cable up during the WPA3
  // login, the ESP crashed inside the Wi-Fi driver (2026-11-01).
  static bool started = false;
  if (!started && WiFi.status() == WL_CONNECTED) {
    started = true;
    // Read before Ethernet starts: setting up the cable side can clear it.
    IPAddress dns = WiFi.dnsIP();
    if (startEthernet(dns)) {
      startRouting(dns);
    }
    checkInternet();
  }
  updateNat();
}
