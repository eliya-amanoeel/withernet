// withernet bridge: Wi-Fi station <-> Ethernet, by cloning the MAC of the one
// device on the Ethernet port. The router sees that device as a Wi-Fi client
// and assigns its IP; the dongle just forwards frames unchanged.
//
//   1. Ethernet starts in promiscuous mode; Wi-Fi is set up but not started.
//   2. The first frame from the device gives us its MAC.
//   3. That MAC goes on the Wi-Fi station interface, then Wi-Fi starts and joins.
//   4. Frames are forwarded both ways.
//
// Based on the ESP-IDF eth2ap example, with the station interface instead of
// the access point.
#include <string.h>

#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_private/wifi.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "nvs_flash.h"

#include "secrets.h"

static const char *TAG = "bridge";

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

// Wi-Fi -> Ethernet. Runs in the Wi-Fi task; the driver already drops frames
// that aren't for the cloned MAC (or broadcast/multicast).
static esp_err_t wifi_rx(void *buffer, uint16_t len, void *eb)
{
    if (s_eth_up) {
        esp_eth_transmit(s_eth_handle, buffer, len);
    }
    esp_wifi_internal_free_rx_buffer(eb);
    return ESP_OK;
}

// Ethernet -> queue. Runs in the EMAC task, so hand the frame off instead of
// waiting on Wi-Fi here. We own the buffer and must free it.
static esp_err_t eth_rx(esp_eth_handle_t eth_handle, uint8_t *buffer, uint32_t len, void *priv)
{
    eth_frame_t frame = { .packet = buffer, .length = len };
    if (xQueueSend(s_eth2wifi_queue, &frame, pdMS_TO_TICKS(QUEUE_TIMEOUT_MS)) != pdTRUE) {
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
    ESP_ERROR_CHECK(esp_wifi_start());
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
            int waited = 0;
            while (esp_wifi_internal_tx(WIFI_IF_STA, frame.packet, frame.length) != ESP_OK
                   && waited < WIFI_TX_TIMEOUT_MS) {
                vTaskDelay(pdMS_TO_TICKS(2));
                waited += 2;
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
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t *event = data;
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason %d), retrying", event->reason);
        s_wifi_up = false;
        esp_wifi_internal_reg_rxcb(WIFI_IF_STA, NULL);
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
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Ethernet link up");
        s_eth_up = true;
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Ethernet link down");
        s_eth_up = false;
        break;
    default:
        break;
    }
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

    init_wifi();
    init_ethernet();
}
