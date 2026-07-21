/*
 * pbus.c -- 協定 task 主迴圈 + 公開 API(協定 §14 + 裁定 R2)。
 * 併發:recv 回呼只入佇列;所有 L1-L3 在此 task;publish/submit 經 txq 入列(任意 task 可呼叫)。
 */
#include "pbus_int.h"
#include "master_engine.h"
#include "game_state.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/task.h"

static const char *TAG = "pbus";
pbus_t g_pb;
uint8_t g_pbus_last_reject;   /* session 收 CMD_ACK(REJECT/STALE) 時寫;consume_reject 讀清 */

/* 裝置類別:各裝置 main 提供強符號覆寫(StickS3=4) */
__attribute__((weak)) uint8_t pk_board_device_class(void) { return 0; }

static bool mac_eq(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) == 0; }

/* ---------------- EVT/EVT_RTX 收包(epoch 仲裁 + 交付) ---------------- */
static void handle_evt(const pn_rx_item_t *rx)
{
    const pn_hdr_t *h = (const pn_hdr_t *)rx->pkt;

    if (g_pb.table_id == 0) return;   /* B5:未入桌不吃任何 EVT —— 掃描/自建桌期間
                                         吃到別桌廣播會被 epoch 仲裁降級成幽靈成員(真機踩中) */
    if (h->table_id != g_pb.table_id) return;   /* 別桌 */
    if (h->epoch < g_pb.epoch) return;                                /* 舊任期殘包 */
    if (h->epoch > g_pb.epoch) {                                      /* adopt epoch */
        g_pb.epoch = h->epoch;
        memcpy(g_pb.master_mac, rx->src, 6);
        g_pb.clock_freeze_hb = 2;
        if (g_pb.role == ROLE_MASTER || g_pb.role == ROLE_TEMP_MASTER ||
            g_pb.role == ROLE_TAKEOVER_CLAIM) {
            pn_set_role(ROLE_MEMBER);
            if (g_pb.cb.on_role) g_pb.cb.on_role(false);
        }
    }
    if (mac_eq(rx->src, g_pb.master_mac)) {
        pn_clock_ingest(h->table_ms);
        g_pb.t_last_hb_rx = pn_now_us();
    }
    pn_reliab_on_evt(rx->pkt, rx->len);
}

static void handle_rx(const pn_rx_item_t *rx)
{
    const pn_hdr_t *h = (const pn_hdr_t *)rx->pkt;
    switch (h->type) {
    case PN_PKT_EVT:
    case PN_PKT_EVT_RTX:
        handle_evt(rx);
        break;
    default:
        pn_session_on_pkt(rx);
        break;
    }
}

/* ---------------- txq 排空 ---------------- */
static void drain_tx(void)
{
    pn_txreq_t r;
    while (xQueueReceive(g_pb.txq, &r, 0) == pdTRUE) {
        if (r.kind == TXREQ_EVT) {
            if (g_pb.role == ROLE_MASTER || g_pb.role == ROLE_TEMP_MASTER)
                pn_publish_locked(r.code, r.body, r.len, r.play_at);
        } else { /* TXREQ_CMD */
            /* v1.1 真機修訂:Master 本人的命令直接同步進決策引擎 —— ESP-NOW 無自環,
               單播給 master_mac(=自己)永遠石沉大海(真機實測:Master 台按鍵全無效)。 */
            if (g_pb.role == ROLE_MASTER || g_pb.role == ROLE_TEMP_MASTER) {
                pbus_cmd_verdict_t v = { PN_CMD_REJECT, PN_RJ_WRONG_PHASE };
                if (g_pb.cmd_handler && g_pb.my_player_id != 0xFF)
                    v = g_pb.cmd_handler(g_pb.my_player_id, r.code, r.body, r.len);
                ESP_LOGI(TAG, "self cmd=%u -> result=%u reason=%u", r.code, v.result, v.reason);
                if (v.result == PN_CMD_REJECT || v.result == PN_CMD_STALE)
                    g_pbus_last_reject = v.reason ? v.reason : PN_RJ_WRONG_PHASE;
                game_view_publish();
                continue;
            }
            if (g_pb.cmd_inflight) continue;   /* 在途僅允許 1 筆(UI 已擋) */
            g_pb.cmd_inflight = true;
            g_pb.cmd_inflight_id = g_pb.cmd_id_next++;
            g_pb.cmd_inflight_code = r.code;
            g_pb.cmd_inflight_len = (uint8_t)r.len;
            memcpy(g_pb.cmd_inflight_body, r.body, r.len);
            g_pb.cmd_retries = 0;
            g_pb.cmd_last_send_us = 0;   /* 立即在 session_tick 送出 */
        }
    }
}

static void pbus_task(void *arg)
{
    (void)arg;
    pn_rx_item_t rx;
    for (;;) {
        if (xQueueReceive(g_pb.rxq, &rx, pdMS_TO_TICKS(20)) == pdTRUE)
            handle_rx(&rx);
        drain_tx();
        pn_session_tick();
        pn_gap_tick();
        if (g_pb.idle_hook) g_pb.idle_hook();
    }
}

/* ---------------- 公開 API ---------------- */
esp_err_t pbus_start(const pbus_callbacks_t *cb, const char *name)
{
    if (g_pb.started) return ESP_ERR_INVALID_STATE;
    memset(&g_pb, 0, sizeof(g_pb));
    if (cb) g_pb.cb = *cb;
    if (name) { strncpy(g_pb.name, name, sizeof(g_pb.name) - 1); }
    g_pb.device_class = pk_board_device_class();
    g_pb.my_player_id = 0xFF;
    g_pb.cmd_id_next = 1;
    g_pb.st.button_seat = 0xFF;
    g_pb.st.to_act_seat = 0xFF;
    for (int i = 0; i < 10; i++) g_pb.st.p[i].seat = 0xFF;

    g_pb.rxq = xQueueCreate(24, sizeof(pn_rx_item_t));
    g_pb.txq = xQueueCreate(12, sizeof(pn_txreq_t));
    if (!g_pb.rxq || !g_pb.txq) return ESP_ERR_NO_MEM;

    pn_clock_reset();
    pn_reliab_reset();
    ESP_ERROR_CHECK(pn_transport_init());

    pn_set_role(ROLE_SCAN);
    g_pb.t_scan_start = pn_now_us();
    g_pb.started = true;

    xTaskCreatePinnedToCore(pbus_task, "pbus", 6144, NULL, 12, NULL, 0);
    ESP_LOGI(TAG, "started as \"%s\" (dc=%u)", g_pb.name, g_pb.device_class);
    return ESP_OK;
}

esp_err_t pbus_submit_cmd(uint8_t cmd, const void *arg, size_t len)
{
    if (len > sizeof(((pn_txreq_t *)0)->body)) return ESP_ERR_INVALID_SIZE;
    pn_txreq_t r = { .kind = TXREQ_CMD, .code = cmd, .play_at = 0, .len = (uint16_t)len };
    if (len && arg) memcpy(r.body, arg, len);
    return xQueueSend(g_pb.txq, &r, 0) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t pbus_publish_evt(uint8_t evt, const void *body, size_t len, uint32_t play_at)
{
    if (len > sizeof(((pn_txreq_t *)0)->body)) return ESP_ERR_INVALID_SIZE;
    pn_txreq_t r = { .kind = TXREQ_EVT, .code = evt, .play_at = play_at, .len = (uint16_t)len };
    if (len && body) memcpy(r.body, body, len);
    return xQueueSend(g_pb.txq, &r, 0) == pdTRUE ? ESP_OK : ESP_FAIL;
}

uint32_t pbus_table_now(void) { return pn_clock_table_now(); }
uint32_t pbus_local_time_for(uint32_t t) { return pn_clock_local_for(t); }
const pn_table_state_t *pbus_state(void) { return &g_pb.st; }

void pbus_set_cmd_handler(pbus_cmd_handler_t h) { g_pb.cmd_handler = h; }
void pbus_set_idle_hook(void (*hook)(void)) { g_pb.idle_hook = hook; }
uint8_t pbus_my_player_id(void) { return g_pb.my_player_id; }
bool pbus_is_master(void) { return g_pb.role == ROLE_MASTER || g_pb.role == ROLE_TEMP_MASTER; }
uint16_t pbus_device_class(void) { return g_pb.device_class; }
bool pbus_cmd_inflight(void) { return g_pb.cmd_inflight; }

uint8_t pbus_consume_reject(void)
{
    uint8_t v = g_pbus_last_reject;
    g_pbus_last_reject = 0;
    return v;
}

void pbus_leave(void)
{
    /* 本地清會話 → 回掃描 */
    g_pb.table_id = 0; g_pb.epoch = 0; g_pb.my_player_id = 0xFF;
    pn_clock_reset(); pn_reliab_reset();
    memset(&g_pb.st, 0, sizeof(g_pb.st));
    g_pb.st.button_seat = 0xFF; g_pb.st.to_act_seat = 0xFF;
    for (int i = 0; i < 10; i++) g_pb.st.p[i].seat = 0xFF;
    pn_set_role(ROLE_SCAN);
    g_pb.t_scan_start = pn_now_us();
    if (g_pb.cb.on_link) g_pb.cb.on_link(PBUS_LINK_DISSOLVED);
}
