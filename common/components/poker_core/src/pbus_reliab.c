/*
 * pbus_reliab.c -- L1 可靠層:事件日誌、亂序緩衝、補洞、嚴格有序交付(協定 §6)。
 * 上層永遠看不到亂序/缺洞/重複。
 */
#include "pbus_int.h"
#include "game_state.h"
#include "esp_random.h"
#include "esp_log.h"

static inline int16_t seq_dist(uint16_t a, uint16_t b) { return (int16_t)(a - b); }

void pn_reliab_reset(void)
{
    for (int i = 0; i < PK_OOO_BUF_SLOTS; i++) g_pb.ooo[i].used = false;
    g_pb.log_head = 0; g_pb.log_count = 0;
    g_pb.t_gap_arm = 0; g_pb.gap_retries = 0;
    memset(g_pb.crc_ring, 0, sizeof(g_pb.crc_ring));
}

void pn_log_store(uint16_t seq, const uint8_t *pkt, uint16_t len)
{
    if (len > PK_EVT_SLOT_BYTES) return;
    int i = g_pb.log_head;
    g_pb.log[i].seq = seq;
    g_pb.log[i].len = len;
    memcpy(g_pb.log[i].pkt, pkt, len);
    g_pb.log_head = (i + 1) % PK_EVT_LOG_MASTER;
    if (g_pb.log_count < PK_EVT_LOG_MASTER) g_pb.log_count++;
}

bool pn_log_get(uint16_t seq, const uint8_t **pkt, uint16_t *len)
{
    for (int i = 0; i < g_pb.log_count; i++) {
        if (g_pb.log[i].seq == seq) {
            *pkt = g_pb.log[i].pkt;
            *len = g_pb.log[i].len;
            return true;
        }
    }
    return false;
}

/* 交付一個已排序 EVT 包(pkt = 完整空中包) */
void pn_deliver(const uint8_t *pkt, uint16_t len)
{
    if (len < sizeof(pn_hdr_t) + sizeof(pn_evt_hdr_t)) return;
    const pn_hdr_t *h = (const pn_hdr_t *)pkt;
    const pn_evt_hdr_t *e = (const pn_evt_hdr_t *)(pkt + sizeof(pn_hdr_t));
    const void *body = pkt + sizeof(pn_hdr_t) + sizeof(pn_evt_hdr_t);
    uint16_t body_len = (h->len >= sizeof(pn_evt_hdr_t)) ? (h->len - sizeof(pn_evt_hdr_t)) : 0;

    game_state_apply(&g_pb.st, e, body, body_len);   /* 先更新鏡像 */
    g_pb.last_recv_seq = e->seq;
    g_pb.crc_ring[e->seq & 7] = pn_crc32(&g_pb.st, sizeof(g_pb.st));
    game_view_publish();
    ESP_LOGI("pbus_rel", "evt seq=%u id=%u phase=%u n=%u",
             e->seq, e->evt, g_pb.st.phase, g_pb.st.n_players);  /* v1.1 真機診斷 */
    if (g_pb.cb.on_event) g_pb.cb.on_event(e, body, body_len);
}

static void ooo_insert(const uint8_t *pkt, uint16_t len, uint16_t seq)
{
    /* 已存在?(去重) */
    for (int i = 0; i < PK_OOO_BUF_SLOTS; i++)
        if (g_pb.ooo[i].used && g_pb.ooo[i].seq == seq) return;
    /* 找空槽 */
    for (int i = 0; i < PK_OOO_BUF_SLOTS; i++) {
        if (!g_pb.ooo[i].used) {
            g_pb.ooo[i].used = true; g_pb.ooo[i].seq = seq; g_pb.ooo[i].len = len;
            memcpy(g_pb.ooo[i].pkt, pkt, len);
            return;
        }
    }
    /* 滿:丟 seq 最大者(離交付點最遠) */
    int far = 0;
    for (int i = 1; i < PK_OOO_BUF_SLOTS; i++)
        if (seq_dist(g_pb.ooo[i].seq, g_pb.ooo[far].seq) > 0) far = i;
    if (seq_dist(seq, g_pb.ooo[far].seq) < 0) {   /* 新包更近才替換 */
        g_pb.ooo[far].seq = seq; g_pb.ooo[far].len = len;
        memcpy(g_pb.ooo[far].pkt, pkt, len);
    }
}

static bool ooo_take(uint16_t seq, uint8_t *out, uint16_t *out_len)
{
    for (int i = 0; i < PK_OOO_BUF_SLOTS; i++) {
        if (g_pb.ooo[i].used && g_pb.ooo[i].seq == seq) {
            *out_len = g_pb.ooo[i].len;
            memcpy(out, g_pb.ooo[i].pkt, g_pb.ooo[i].len);
            g_pb.ooo[i].used = false;
            return true;
        }
    }
    return false;
}

static bool ooo_has_holes(void)
{
    for (int i = 0; i < PK_OOO_BUF_SLOTS; i++) if (g_pb.ooo[i].used) return true;
    return false;
}

void pn_gap_arm_if_needed(void)
{
    if (g_pb.t_gap_arm == 0 && ooo_has_holes()) {
        int jitter = (int)(esp_random() % (PN_T_GAP_JITTER_MS + 1));
        g_pb.t_gap_arm = pn_now_us() + (int64_t)(PN_T_GAP_DELAY_MS + jitter) * 1000;
        g_pb.gap_retries = 0;
    }
}

void pn_reliab_on_evt(const uint8_t *pkt, uint16_t len)
{
    const pn_evt_hdr_t *e = (const pn_evt_hdr_t *)(pkt + sizeof(pn_hdr_t));
    uint16_t seq = e->seq;
    int16_t d = seq_dist(seq, g_pb.next_expected_seq);

    if (d < 0) return;                        /* 重複/舊包 */
    if (d == 0) {
        pn_deliver(pkt, len);
        g_pb.next_expected_seq++;
        /* 連續交付亂序緩衝 */
        uint8_t buf[250]; uint16_t blen;
        while (ooo_take(g_pb.next_expected_seq, buf, &blen)) {
            pn_deliver(buf, blen);
            g_pb.next_expected_seq++;
        }
        g_pb.t_gap_arm = 0;                 /* 交付推進 → 重評 gap timer */
        pn_gap_arm_if_needed();             /* 仍有更遠的洞才重新武裝 */
    } else {
        ooo_insert(pkt, len, seq);
        pn_gap_arm_if_needed();
    }
}

/* gap timer 到期:向 Master 單播 GAP_REQ */
void pn_gap_tick(void)
{
    if (g_pb.t_gap_arm == 0) return;
    if (pn_now_us() < g_pb.t_gap_arm) return;

    /* 找亂序緩衝最小 seq → to = min-1 */
    uint16_t min_seq = 0; bool have = false;
    for (int i = 0; i < PK_OOO_BUF_SLOTS; i++) {
        if (g_pb.ooo[i].used && (!have || seq_dist(g_pb.ooo[i].seq, min_seq) < 0)) {
            min_seq = g_pb.ooo[i].seq; have = true;
        }
    }
    if (!have) { g_pb.t_gap_arm = 0; return; }

    pn_gap_req_t req = { .from_seq = g_pb.next_expected_seq, .to_seq = (uint16_t)(min_seq - 1) };
    pn_send_typed(g_pb.master_mac, PN_PKT_GAP_REQ, &req, sizeof(req));

    if (++g_pb.gap_retries > PN_GAP_RETRY) {
        /* 轉快照請求 */
        pn_send_typed(g_pb.master_mac, PN_PKT_SNAP_REQ, NULL, 0);
        g_pb.gap_retries = 0;
        g_pb.t_gap_arm = pn_now_us() + (int64_t)PN_T_SNAP_REQ_MS * 1000;
    } else {
        g_pb.t_gap_arm = pn_now_us() + (int64_t)PN_T_GAP_RETRY_MS * 1000;
    }
}
