/*
 * pbus_transport.c -- L0 傳輸:Wi-Fi/ESP-NOW init、recv 回呼(只入佇列)、發送、CRC。
 * 照 xingzhi intercom.c 已驗證模式(協定 §4/§17)。
 */
#include "pbus_int.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "nvs_flash.h"

static const char *TAG = "pbus_tx";
static const uint8_t BCAST[6] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

uint32_t pn_local_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
int64_t  pn_now_us(void)   { return esp_timer_get_time(); }

/* CRC32 (IEEE 802.3, reflected) —— 自帶實作,避免版本相依 */
uint32_t pn_crc32(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
    }
    return ~crc;
}

/* Wi-Fi task 上下文:協定 §4 —— 只驗最小限度 + 入佇列 */
static void pn_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len < (int)sizeof(pn_hdr_t) || len > 250) return;
    const pn_hdr_t *h = (const pn_hdr_t *)data;
    if (h->magic != PN_MAGIC || h->version != PN_VERSION) return;
    if ((int)(sizeof(pn_hdr_t) + h->len) > len) return;
    pn_rx_item_t it;
    memcpy(it.src, info->src_addr, 6);
    it.len = (uint16_t)len;
    memcpy(it.pkt, data, len);
    xQueueSend(g_pb.rxq, &it, 0);   /* 不阻塞,滿則丟(補洞兜底) */
}

esp_err_t pn_transport_init(void)
{
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    esp_err_t er = esp_event_loop_create_default();
    if (er != ESP_OK && er != ESP_ERR_INVALID_STATE) return er;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));           /* 含掃描期(協定 §4) */

    esp_wifi_get_mac(WIFI_IF_STA, g_pb.self_mac);

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(pn_recv_cb));

    esp_now_peer_info_t peer = { .channel = 0, .ifidx = WIFI_IF_STA, .encrypt = false };
    memcpy(peer.peer_addr, BCAST, 6);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
    ESP_LOGI(TAG, "transport up, self %02x:%02x:%02x:%02x:%02x:%02x",
             g_pb.self_mac[0], g_pb.self_mac[1], g_pb.self_mac[2],
             g_pb.self_mac[3], g_pb.self_mac[4], g_pb.self_mac[5]);
    return ESP_OK;
}

void pn_add_peer(const uint8_t mac[6])
{
    if (esp_now_is_peer_exist(mac)) return;
    esp_now_peer_info_t peer = { .channel = 0, .ifidx = WIFI_IF_STA, .encrypt = false };
    memcpy(peer.peer_addr, mac, 6);
    /* 對端表滿或 OOM 時 add 會失敗,之後對該 MAC 的單播全數落空;不可靜默(#18)。 */
    esp_err_t r = esp_now_add_peer(&peer);
    if (r != ESP_OK)
        ESP_LOGW(TAG, "add_peer %02x:%02x:%02x:%02x:%02x:%02x failed: %s",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], esp_err_to_name(r));
}

void pn_send(const uint8_t dst[6], const void *data, size_t len)
{
    esp_now_send(dst ? dst : BCAST, (const uint8_t *)data, len);
}

void pn_send_typed(const uint8_t dst[6], uint8_t type, const void *payload, uint16_t plen)
{
    uint8_t buf[250];
    if (sizeof(pn_hdr_t) + plen > sizeof(buf)) {   /* 超 ESP-NOW 上限:設計上不該發生,發生即程式錯誤(#16) */
        ESP_LOGE(TAG, "pn_send_typed: type=%u plen=%u exceeds MTU, dropped", type, plen);
        return;
    }
    pn_hdr_t *h = (pn_hdr_t *)buf;
    h->magic = PN_MAGIC;
    h->version = PN_VERSION;
    h->type = type;
    h->table_id = g_pb.table_id;
    h->epoch = g_pb.epoch;
    h->len = plen;
    h->table_ms = pn_clock_table_now();
    if (plen && payload) memcpy(buf + sizeof(pn_hdr_t), payload, plen);
    pn_send(dst, buf, sizeof(pn_hdr_t) + plen);
}
