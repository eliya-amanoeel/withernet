// Management web server, reachable on MGMT_IP only (the Ethernet side has no
// IP stack). Everything except /status.json needs basic auth: user "admin",
// password ADMIN_PASS_BOARD (the dongle's own one from secrets.h, picked in
// main/CMakeLists.txt). /status.json is open so Home Assistant's polling sends
// no password (HL-122); it holds nothing sensitive.
// Uploads must be signed with the key on the Mac; esp_ota_end() checks that.
//
//   GET  /             status page with a firmware upload form
//   GET  /status.json  the same status, for scripts (no login)
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

#ifdef ADMIN_PASS
#error "secrets.h: replace ADMIN_PASS with ADMIN_PASS_NIBE and ADMIN_PASS_CHECKWATT (see secrets.example.h)"
#endif
// At least 20 characters; at most 100 so "Basic " + base64 fits s_expected_auth.
_Static_assert(sizeof(ADMIN_PASS_BOARD) - 1 >= 20, "admin password too short: use 20+ characters");
_Static_assert(sizeof(ADMIN_PASS_BOARD) - 1 <= 100, "admin password too long: 100 characters max");

static const char *TAG = "web";

static char s_expected_auth[160]; // "Basic " + base64("admin:" password)

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
    // Favicon inline (no extra request); its colour is per dongle, see main/CMakeLists.txt.
    "<link rel=\"icon\" href=\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'>"
    "<rect width='32' height='32' rx='7' fill='%23" ICON_COLOR "'/>"
    "<g fill='none' stroke='%23fff' stroke-width='3' stroke-linecap='round'>"
    "<path d='M6 13a14 14 0 0 1 20 0'/><path d='M10.5 17.5a7.5 7.5 0 0 1 11 0'/></g>"
    "<circle cx='16' cy='22.5' r='2.3' fill='%23fff'/></svg>\">"
    "<style>"
    ":root{color-scheme:light dark;--fg:#1d1d1f;--mut:#6e6e73;--bg:#f5f5f7;--card:#fff;--line:#e5e5ea;--ok:#2e9d4f;--warn:#c98a00;--bad:#d1372e}"
    "@media(prefers-color-scheme:dark){:root{--fg:#f2f2f7;--mut:#98989f;--bg:#111113;--card:#1c1c1e;--line:#2c2c2e}}"
    "*{box-sizing:border-box}"
    "body{font:15px/1.4 system-ui,-apple-system,sans-serif;color:var(--fg);background:var(--bg);margin:0;padding:1.5em 1em}"
    "main{max-width:34em;margin:auto}"
    "h1{font-size:1.3em;margin:0}"
    ".sub{color:var(--mut);font-size:.9em;margin:.2em 0 1.2em}"
    "section{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:.4em 1em;margin-bottom:1em}"
    "h2{font-size:.8em;text-transform:uppercase;letter-spacing:.05em;color:var(--mut);margin:.8em 0 .3em}"
    "dl{display:grid;grid-template-columns:auto 1fr;margin:0}"
    "dt,dd{padding:.45em 0;border-top:1px solid var(--line);margin:0}"
    "dt{color:var(--mut)}dd{text-align:right;font-variant-numeric:tabular-nums}"
    "dt:first-of-type,dt:first-of-type+dd{border-top:0}"
    ".dot{display:inline-block;width:.6em;height:.6em;border-radius:50%;margin-right:.4em;background:var(--mut)}"
    ".ok{background:var(--ok)}.warn{background:var(--warn)}.bad{background:var(--bad)}"
    "details{margin:.6em 0}summary{cursor:pointer;color:var(--mut)}"
    "pre{font-size:.8em;overflow-x:auto;margin:.5em 0 0}"
    ".up{display:flex;gap:.5em;align-items:center;padding:.5em 0 .8em}"
    ".up input{flex:1;min-width:0}"
    "button{font:inherit;padding:.35em 1em;border-radius:6px;border:1px solid var(--line);background:var(--bg);color:var(--fg);cursor:pointer}"
    "#m{color:var(--mut);margin:0 0 .6em;min-height:1em}"
    "</style></head><body><main>"
    "<h1>withernet · " BOARD_NAME "</h1><p class=\"sub\" id=\"sub\">" MGMT_IP "</p>"
    "<section><h2>Status</h2><dl id=\"d\"><dt>loading…</dt><dd></dd></dl>"
    "<details><summary>Raw JSON</summary><pre id=\"s\"></pre></details></section>"
    "<section><h2>Firmware update</h2>"
    "<div class=\"up\"><input type=\"file\" id=\"f\" accept=\".bin\"><button onclick=\"up()\">Upload</button></div>"
    "<p id=\"m\"></p></section>"
    "</main><script>"
    "function $(i){return document.getElementById(i)}"
    "function dur(s){var d=s/86400|0,h=s/3600%24|0,m=s/60%60|0;return(d?d+'d ':'')+h+'h '+m+'m'}"
    "function row(k,v,c){return'<dt>'+k+'</dt><dd>'+(c?'<span class=\"dot '+c+'\"></span>':'')+v+'</dd>'}"
    "function load(){fetch('/status.json').then(r=>r.json()).then(j=>{"
    "var w=j.wifi,e=j.ethernet,f=j.frames,r=w.rssi;"
    "var q=!w.up?['not connected','bad']:r>=-60?['good','ok']:r>=-70?['ok','warn']:['weak','bad'];"
    "$('sub').textContent=j.ip+' · firmware '+j.firmware+' · up '+dur(j.uptime_s);"
    "$('d').innerHTML="
    "row('Wi-Fi',w.up?r+' dBm, '+q[0]:q[0],q[1])+"
    "row('Channel / AP',w.channel+' · '+w.ap)+"
    "row('Disconnects',w.disconnects+(w.disconnects?' (last reason '+w.last_disconnect_reason+')':''))+"
    "row('Ethernet',e.up?e.mbit+' Mbit '+(e.full_duplex?'full':'half')+' duplex':'down',e.up?'ok':'bad')+"
    "row('Device MAC',j.mac_learned?j.device_mac:'not learned yet',j.mac_learned?'ok':'warn')+"
    "row('Frames eth → wifi',f.eth_to_wifi.toLocaleString()+(f.eth_to_wifi_dropped?' ('+f.eth_to_wifi_dropped+' dropped)':''))+"
    "row('Frames wifi → eth',f.wifi_to_eth.toLocaleString())+"
    "row('Free heap',(j.free_heap/1024|0)+' KB')+"
    "row('Built',j.built);"
    "$('s').textContent=JSON.stringify(j,null,2)}).catch(e=>{$('d').innerHTML=row('offline',''+e,'bad')})}"
    "load();setInterval(load,5000);"
    "function up(){var f=$('f').files[0];if(!f)return;$('m').textContent='uploading…';"
    "fetch('/update',{method:'POST',body:f}).then(r=>r.text()).then(t=>{$('m').textContent=t}).catch(e=>{$('m').textContent='failed: '+e})}"
    "</script></body></html>";

static esp_err_t root_get(httpd_req_t *req)
{
    if (!authorized(req)) {
        return ESP_OK;
    }
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PAGE, HTTPD_RESP_USE_STRLEN);
}

// No login on purpose (HL-122). Don't add secrets or settings to this output.
static esp_err_t status_get(httpd_req_t *req)
{
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

    // Checks the image (checksum, chip, signature) before we boot into it.
    if (esp_ota_end(ota) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK) {
        ESP_LOGW(TAG, "update refused: image not valid or not signed with our key");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "refused: not a valid firmware signed with the withernet key\n");
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
    snprintf(userpass, sizeof(userpass), "admin:%s", ADMIN_PASS_BOARD);
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
