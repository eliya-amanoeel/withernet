// Management web server, reachable on MGMT_IP only (the Ethernet side has no
// IP stack). Everything needs basic auth: user "admin", password ADMIN_PASS.
//
//   GET  /             status page with a firmware upload form
//   GET  /status.json  the same status, for scripts
//   POST /update       raw firmware .bin as the body; reboots into it
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"

#include "bridge.h"
#include "secrets.h"

static const char *TAG = "web";

static char s_expected_auth[160]; // "Basic " + base64("admin:" ADMIN_PASS)

static bool authorized(httpd_req_t *req)
{
    char got[sizeof(s_expected_auth)];
    if (httpd_req_get_hdr_value_str(req, "Authorization", got, sizeof(got)) == ESP_OK
        && strcmp(got, s_expected_auth) == 0) {
        return true;
    }
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"withernet\"");
    httpd_resp_sendstr(req, "login required\n");
    return false;
}

static const char PAGE[] =
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width\"><title>withernet " BOARD_NAME "</title>"
    "<style>body{font-family:sans-serif;margin:1em;max-width:40em}"
    "pre{background:#eee;padding:1em;overflow-x:auto}</style></head><body>"
    "<h1>withernet: " BOARD_NAME " (" MGMT_IP ")</h1><p id=\"sig\"></p><pre id=\"s\">loading...</pre>"
    "<h2>Firmware update</h2>"
    "<input type=\"file\" id=\"f\" accept=\".bin\"> <button onclick=\"up()\">Upload</button>"
    "<p id=\"m\"></p>"
    "<script>"
    "function $(i){return document.getElementById(i)}"
    "function load(){fetch('/status.json').then(r=>r.json()).then(j=>{"
    "var r=j.wifi.rssi,q=!j.wifi.up?'not connected':r>=-60?'good':r>=-70?'ok':'weak';"
    "$('sig').textContent='Wi-Fi signal: '+(j.wifi.up?r+' dBm, ':'')+q;"
    "$('s').textContent=JSON.stringify(j,null,2)}).catch(e=>{$('s').textContent='offline: '+e})}"
    "load();setInterval(load,5000);"
    "function up(){var f=$('f').files[0];if(!f)return;$('m').textContent='uploading...';"
    "fetch('/update',{method:'POST',body:f}).then(r=>r.text()).then(t=>{$('m').textContent=t})"
    ".catch(e=>{$('m').textContent='failed: '+e})}"
    "</script></body></html>";

static esp_err_t root_get(httpd_req_t *req)
{
    if (!authorized(req)) {
        return ESP_OK;
    }
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_get(httpd_req_t *req)
{
    if (!authorized(req)) {
        return ESP_OK;
    }
    bridge_status_t st;
    bridge_get_status(&st);
    wifi_ap_record_t ap = { 0 };
    if (!st.wifi_up || esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        memset(&ap, 0, sizeof(ap));
    }
    const esp_app_desc_t *app = esp_app_get_description();

    char buf[768];
    snprintf(buf, sizeof(buf),
             "{\"board\":\"" BOARD_NAME "\",\"ip\":\"" MGMT_IP "\",\"firmware\":\"%s\",\"built\":\"%s %s\",\"uptime_s\":%llu,"
             "\"device_mac\":\"" MACSTR "\",\"mac_learned\":%s,"
             "\"ethernet\":{\"up\":%s,\"mbit\":%d,\"full_duplex\":%s},"
             "\"wifi\":{\"up\":%s,\"rssi\":%d,\"channel\":%d,\"ap\":\"" MACSTR "\","
             "\"disconnects\":%" PRIu32 ",\"last_disconnect_reason\":%d},"
             "\"frames\":{\"eth_to_wifi\":%" PRIu32 ",\"wifi_to_eth\":%" PRIu32
             ",\"eth_to_wifi_dropped\":%" PRIu32 "},"
             "\"free_heap\":%u}\n",
             app->version, app->date, app->time,
             (unsigned long long)(esp_timer_get_time() / 1000000),
             MAC2STR(st.device_mac), st.mac_learned ? "true" : "false",
             st.eth_up ? "true" : "false", st.eth_mbit, st.eth_full_duplex ? "true" : "false",
             st.wifi_up ? "true" : "false", ap.rssi, ap.primary, MAC2STR(ap.bssid),
             st.wifi_disconnects, st.last_disconnect_reason,
             st.eth_to_wifi, st.wifi_to_eth, st.eth_to_wifi_dropped,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

// Offset of the app description inside a firmware .bin.
#define APP_DESC_OFFSET (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t))

static esp_err_t update_post(httpd_req_t *req)
{
    if (!authorized(req)) {
        return ESP_OK;
    }
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (part == NULL || req->content_len == 0 || req->content_len > part->size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing or too large\n");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "firmware upload, %u bytes, into %s", (unsigned)req->content_len, part->label);

    esp_ota_handle_t ota;
    if (esp_ota_begin(part, req->content_len, &ota) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "can't start update\n");
        return ESP_FAIL;
    }

    static char buf[1024]; // one upload at a time: the server has one task
    size_t left = req->content_len;
    bool first = true;
    int timeouts = 0;
    while (left > 0) {
        int n = httpd_req_recv(req, buf, left < sizeof(buf) ? left : sizeof(buf));
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) {
            continue;
        }
        if (n <= 0) {
            esp_ota_abort(ota);
            return ESP_FAIL; // connection gone, nobody to answer
        }
        if (first) {
            first = false;
            // Refuse a .bin from some other project before it gets anywhere.
            esp_app_desc_t desc;
            if ((size_t)n < APP_DESC_OFFSET + sizeof(desc)) {
                esp_ota_abort(ota);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "not a firmware file\n");
                return ESP_FAIL;
            }
            memcpy(&desc, buf + APP_DESC_OFFSET, sizeof(desc));
            if (strncmp(desc.project_name, esp_app_get_description()->project_name,
                        sizeof(desc.project_name)) != 0) {
                esp_ota_abort(ota);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "not a withernet bridge firmware\n");
                return ESP_FAIL;
            }
        }
        if (esp_ota_write(ota, buf, n) != ESP_OK) {
            esp_ota_abort(ota);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write failed\n");
            return ESP_FAIL;
        }
        left -= n;
    }

    // Checks the image (checksum, chip) before we boot into it.
    if (esp_ota_end(ota) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "firmware image is not valid\n");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "update done, rebooting");
    httpd_resp_sendstr(req, "ok, rebooting into the new firmware. Reload in ~30 s.\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

void web_start(void)
{
    char userpass[128];
    snprintf(userpass, sizeof(userpass), "admin:%s", ADMIN_PASS);
    size_t olen = 0;
    strcpy(s_expected_auth, "Basic ");
    ESP_ERROR_CHECK(mbedtls_base64_encode((unsigned char *)s_expected_auth + 6,
                                          sizeof(s_expected_auth) - 6, &olen,
                                          (const unsigned char *)userpass, strlen(userpass)));

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;
    httpd_handle_t server;
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));

    static const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = root_get },
        { .uri = "/status.json", .method = HTTP_GET, .handler = status_get },
        { .uri = "/update", .method = HTTP_POST, .handler = update_post },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uris[i]));
    }
    ESP_LOGI(TAG, "web server started");
}
