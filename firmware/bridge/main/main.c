// withernet bridge: Wi-Fi station <-> Ethernet, by cloning the MAC of the one
// device on the Ethernet port. The router sees that device as a Wi-Fi client
// and assigns its IP; the dongle just forwards frames unchanged.
//
//   1. Ethernet starts in promiscuous mode; Wi-Fi is set up but not started.
//   2. The first frame from the device gives us its MAC.
//   3. That MAC goes on the Wi-Fi station interface, then Wi-Fi starts and joins.
//   4. Frames are forwarded both ways.
//
// Management IP: the dongle also has a small IP stack of its own on a fixed
// address (MGMT_IP), sharing the cloned MAC. Wi-Fi frames for that IP, and ARP
// asking for it, go to the dongle instead of the device. Nothing from the
// Ethernet side ever reaches it.
//
// Based on the ESP-IDF eth2ap example, with the station interface instead of
// the access point.
#include <string.h>

#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_defaults.h"
#include "esp_ota_ops.h"
#include "esp_private/wifi.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "nvs_flash.h"

#include "bridge.h"
#include "secrets.h"

// Set per dongle in main/CMakeLists.txt: build with tools/build-signed.sh <board>.
#if !defined(MGMT_IP) || !defined(BOARD_NAME)
#error "MGMT_IP/BOARD_NAME not set: build with -DBOARD=<nibe|checkwatt>"
#endif

// Rollback test only (BOARD=checkwatt-badwifi): a wrong Wi-Fi password, so the
// new firmware never gets Wi-Fi up and the 2-minute rollback fires.
#ifdef TEST_BAD_WIFI
#undef WIFI_PASS
#define WIFI_PASS "wrong-password-rollback-test"
#endif

static const char *TAG = "bridge";

// After an update over Wi-Fi, the new firmware must get Wi-Fi up within this
// time, or the dongle reboots into the previous one.
#define ROLLBACK_TIMEOUT_US (120 * 1000 * 1000LL)

// WT32-ETH01 Ethernet wiring, see docs/hardware-notes.md.
#define ETH_POWER_GPIO 16
#define ETH_MDC_GPIO   23
#define ETH_MDIO_GPIO  18
#define ETH_PHY_ADDR   1

#define QUEUE_LEN          40
#define QUEUE_TIMEOUT_MS   100
#define WIFI_TX_TIMEOUT_MS 20

typedef struct {
    uint8_t *packet;
    uint32_t length;
} eth_frame_t;

static esp_eth_handle_t s_eth_handle;
static QueueHandle_t s_eth2wifi_queue;
static uint8_t s_device_mac[6];
static bool s_mac_learned;
static bool s_eth_up;
static bool s_wifi_up;
static bool s_pending_verify; // running a new OTA firmware, not confirmed yet

// For the status page. Plain counters: a torn read now and then is fine.
static int s_eth_mbit;
static bool s_eth_full_duplex;
static uint32_t s_wifi_disconnects;
static int s_last_disconnect_reason;
static uint32_t s_eth_to_wifi;
static uint32_t s_wifi_to_eth;
static uint32_t s_eth_to_wifi_dropped;

// Management interface: an esp_netif (lwIP) whose "driver" is the Wi-Fi
// station, fed by wifi_rx below.
typedef struct {
    esp_netif_driver_base_t base;
} mgmt_driver_t;

static mgmt_driver_t s_mgmt_driver;
static esp_netif_t *s_mgmt_netif;
static bool s_mgmt_started;
static uint32_t s_mgmt_ip; // network byte order, as it appears in frames

#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_ARP  0x0806
#define ARP_TPA_OFFSET (14 + 24) // target protocol address in an ARP packet
#define IPV4_DST_OFFSET (14 + 16)

// Is this Wi-Fi frame for the dongle itself rather than the device?
static bool is_for_mgmt(const uint8_t *frame, uint16_t len)
{
    if (len < 14) {
        return false;
    }
    uint16_t type = (frame[12] << 8) | frame[13];
    uint32_t ip;
    if (type == ETHERTYPE_ARP && len >= ARP_TPA_OFFSET + 4) {
        memcpy(&ip, frame + ARP_TPA_OFFSET, 4);
    } else if (type == ETHERTYPE_IPV4 && len >= IPV4_DST_OFFSET + 4) {
        memcpy(&ip, frame + IPV4_DST_OFFSET, 4);
    } else {
        return false;
    }
    return ip == s_mgmt_ip;
}

// Wi-Fi -> Ethernet. Runs in the Wi-Fi task; the driver already drops frames
// that aren't for the cloned MAC (or broadcast/multicast).
static esp_err_t wifi_rx(void *buffer, uint16_t len, void *eb)
{
    if (s_mgmt_started && is_for_mgmt(buffer, len)) {
        // lwIP now owns eb and frees it through mgmt_free_rx.
        esp_netif_receive(s_mgmt_netif, buffer, len, eb);
        return ESP_OK;
    }
    if (s_eth_up) {
        esp_eth_transmit(s_eth_handle, buffer, len);
        s_wifi_to_eth++;
    }
    esp_wifi_internal_free_rx_buffer(eb);
    return ESP_OK;
}

// Management -> Wi-Fi. esp_wifi_internal_tx copies the frame.
static esp_err_t mgmt_transmit(void *h, void *buffer, size_t len)
{
    if (!s_wifi_up) {
        return ESP_FAIL;
    }
    return esp_wifi_internal_tx(WIFI_IF_STA, buffer, len);
}

static esp_err_t mgmt_transmit_wrap(void *h, void *buffer, size_t len, void *netstack_buf)
{
    return mgmt_transmit(h, buffer, len);
}

static void mgmt_free_rx(void *h, void *buffer)
{
    esp_wifi_internal_free_rx_buffer(buffer);
}

static esp_err_t mgmt_post_attach(esp_netif_t *netif, void *args)
{
    mgmt_driver_t *driver = args;
    driver->base.netif = netif;
    esp_netif_driver_ifconfig_t cfg = {
        .handle = driver,
        .transmit = mgmt_transmit,
        .transmit_wrap = mgmt_transmit_wrap,
        .driver_free_rx_buffer = mgmt_free_rx,
    };
    return esp_netif_set_driver_config(netif, &cfg);
}

static void init_mgmt(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    s_mgmt_ip = esp_ip4addr_aton(MGMT_IP);

    esp_netif_ip_info_t ip_info = {
        .ip.addr = s_mgmt_ip,
        .netmask.addr = esp_ip4addr_aton(MGMT_NETMASK),
        .gw.addr = esp_ip4addr_aton(MGMT_GATEWAY),
    };
    esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    // Fixed IP: no DHCP client, no gratuitous ARP, no MLD (no IPv6 at all: it
    // would share the device's MAC and so its link-local address). The netif
    // goes up when Wi-Fi connects, see wifi_event_handler.
    base.flags = 0;
    base.ip_info = &ip_info;
    base.if_key = "MGMT";
    base.if_desc = "mgmt";

    esp_netif_config_t cfg = {
        .base = &base,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_WIFI_STA,
    };
    s_mgmt_netif = esp_netif_new(&cfg);
    s_mgmt_driver.base.post_attach = mgmt_post_attach;
    ESP_ERROR_CHECK(esp_netif_attach(s_mgmt_netif, &s_mgmt_driver));
}

// Called once the MAC is known: the netif needs it before starting.
static void start_mgmt(void)
{
    ESP_ERROR_CHECK(esp_netif_set_mac(s_mgmt_netif, s_device_mac));
    esp_netif_action_start(s_mgmt_netif, NULL, 0, NULL);
    s_mgmt_started = true;
    ESP_LOGI(TAG, "board " BOARD_NAME ", management IP " MGMT_IP);
}

// Ethernet -> queue. Runs in the EMAC task, so hand the frame off instead of
// waiting on Wi-Fi here. We own the buffer and must free it.
static esp_err_t eth_rx(esp_eth_handle_t eth_handle, uint8_t *buffer, uint32_t len, void *priv)
{
    eth_frame_t frame = { .packet = buffer, .length = len };
    if (xQueueSend(s_eth2wifi_queue, &frame, pdMS_TO_TICKS(QUEUE_TIMEOUT_MS)) != pdTRUE) {
        s_eth_to_wifi_dropped++;
        free(buffer);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void learn_mac(const uint8_t *src)
{
    // Group addresses can't be a station MAC.
    if (src[0] & 0x01) {
        return;
    }
    memcpy(s_device_mac, src, 6);
    s_mac_learned = true;
    ESP_LOGI(TAG, "device MAC " MACSTR ", joining Wi-Fi as it", MAC2STR(s_device_mac));
    ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_STA, s_device_mac));
    start_mgmt();
    ESP_ERROR_CHECK(esp_wifi_start());
    // Power save makes the radio doze between beacons and delays frames by
    // hundreds of ms. A bridge has to stay awake.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
}

// Ethernet -> Wi-Fi, from the queue. Retries briefly when the Wi-Fi TX buffers
// are full.
static void eth2wifi_task(void *arg)
{
    eth_frame_t frame;
    bool warned_other_mac = false;

    while (true) {
        if (xQueueReceive(s_eth2wifi_queue, &frame, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        const uint8_t *src = frame.packet + 6;

        if (!s_mac_learned) {
            if (frame.length >= 14) {
                learn_mac(src);
            }
        } else if (memcmp(src, s_device_mac, 6) != 0) {
            // Only the cloned MAC may talk on Wi-Fi: one device per dongle.
            if (!warned_other_mac && !(src[0] & 0x01)) {
                warned_other_mac = true;
                ESP_LOGW(TAG, "ignoring frames from " MACSTR " (one device per dongle; reboot to relearn)",
                         MAC2STR(src));
            }
        } else if (s_wifi_up) {
            esp_err_t err;
            int waited = 0;
            while ((err = esp_wifi_internal_tx(WIFI_IF_STA, frame.packet, frame.length)) != ESP_OK
                   && waited < WIFI_TX_TIMEOUT_MS) {
                vTaskDelay(pdMS_TO_TICKS(2));
                waited += 2;
            }
            if (err == ESP_OK) {
                s_eth_to_wifi++;
            } else {
                s_eth_to_wifi_dropped++;
            }
        }
        free(frame.packet);
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case WIFI_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "Wi-Fi connected");
        // Register here: the driver only delivers frames to us once associated.
        esp_wifi_internal_reg_rxcb(WIFI_IF_STA, wifi_rx);
        s_wifi_up = true;
        esp_netif_action_connected(s_mgmt_netif, NULL, 0, NULL);
        if (s_pending_verify) {
            s_pending_verify = false;
            esp_ota_mark_app_valid_cancel_rollback();
            ESP_LOGI(TAG, "new firmware confirmed");
        }
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t *event = data;
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason %d), retrying", event->reason);
        if (s_wifi_up) {
            s_wifi_disconnects++;
        }
        s_last_disconnect_reason = event->reason;
        s_wifi_up = false;
        esp_wifi_internal_reg_rxcb(WIFI_IF_STA, NULL);
        if (s_mgmt_started) {
            esp_netif_action_disconnected(s_mgmt_netif, NULL, 0, NULL);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_wifi_connect();
        break;
    }
    default:
        break;
    }
}

static void eth_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case ETHERNET_EVENT_CONNECTED: {
        eth_speed_t speed;
        eth_duplex_t duplex;
        esp_eth_ioctl(s_eth_handle, ETH_CMD_G_SPEED, &speed);
        esp_eth_ioctl(s_eth_handle, ETH_CMD_G_DUPLEX_MODE, &duplex);
        ESP_LOGI(TAG, "Ethernet link up, %s Mbit %s duplex",
                 speed == ETH_SPEED_100M ? "100" : "10",
                 duplex == ETH_DUPLEX_FULL ? "full" : "half");
        s_eth_mbit = speed == ETH_SPEED_100M ? 100 : 10;
        s_eth_full_duplex = duplex == ETH_DUPLEX_FULL;
        s_eth_up = true;
        break;
    }
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Ethernet link down");
        s_eth_up = false;
        break;
    default:
        break;
    }
}

void bridge_get_status(bridge_status_t *out)
{
    *out = (bridge_status_t){
        .mac_learned = s_mac_learned,
        .eth_up = s_eth_up,
        .eth_mbit = s_eth_mbit,
        .eth_full_duplex = s_eth_full_duplex,
        .wifi_up = s_wifi_up,
        .wifi_disconnects = s_wifi_disconnects,
        .last_disconnect_reason = s_last_disconnect_reason,
        .eth_to_wifi = s_eth_to_wifi,
        .wifi_to_eth = s_wifi_to_eth,
        .eth_to_wifi_dropped = s_eth_to_wifi_dropped,
    };
    memcpy(out->device_mac, s_device_mac, 6);
}

static void rollback_timeout(void *arg)
{
    if (s_pending_verify) {
        ESP_LOGE(TAG, "new firmware didn't get Wi-Fi up, rolling back");
        esp_ota_mark_app_invalid_rollback_and_reboot();
        // Only returns if it refused: it checks the previous firmware's
        // signature, and one from before signing has none. A reboot still rolls
        // back: the bootloader aborts a firmware that was never confirmed and
        // boots the previous one (it doesn't check signatures).
        ESP_LOGW(TAG, "previous firmware is unsigned, rebooting so the bootloader rolls back");
        esp_restart();
    }
}

// A firmware installed over Wi-Fi boots "pending verify". It gets confirmed
// when Wi-Fi connects; until then a timer stands ready to roll back.
static void check_new_firmware(void)
{
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) != ESP_OK
        || state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }
    ESP_LOGW(TAG, "running new firmware, confirming once Wi-Fi is up");
    s_pending_verify = true;
    esp_timer_handle_t timer;
    const esp_timer_create_args_t args = { .callback = rollback_timeout, .name = "rollback" };
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_once(timer, ROLLBACK_TIMEOUT_US));
}

static void init_wifi(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            // Three Decos share the SSID: scan them all and join the strongest.
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = { .capable = true, .required = false },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    // Not started yet: the MAC can only change while Wi-Fi is stopped.
}

static void init_ethernet(void)
{
    // GPIO16 powers the 50 MHz oscillator that clocks the PHY via GPIO0.
    gpio_reset_pin(ETH_POWER_GPIO);
    gpio_set_direction(ETH_POWER_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(ETH_POWER_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_config.smi_gpio.mdc_num = ETH_MDC_GPIO;
    emac_config.smi_gpio.mdio_num = ETH_MDIO_GPIO;
    emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac_config.clock_config.rmii.clock_gpio = EMAC_CLK_IN_GPIO;
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = ETH_PHY_ADDR;
    phy_config.reset_gpio_num = -1;
    esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config);

    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    config.stack_input = eth_rx;
    ESP_ERROR_CHECK(esp_eth_driver_install(&config, &s_eth_handle));

    // Accept frames for any MAC, not just the ESP's own Ethernet MAC.
    bool promiscuous = true;
    ESP_ERROR_CHECK(esp_eth_ioctl(s_eth_handle, ETH_CMD_S_PROMISCUOUS, &promiscuous));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_eth_start(s_eth_handle));
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "withernet bridge starting, waiting for a device on Ethernet");
    s_eth2wifi_queue = xQueueCreate(QUEUE_LEN, sizeof(eth_frame_t));
    xTaskCreate(eth2wifi_task, "eth2wifi", 4096, NULL, configMAX_PRIORITIES - 3, NULL);

    check_new_firmware();
    init_mgmt();
    web_start();
    init_wifi();
    init_ethernet();
}
