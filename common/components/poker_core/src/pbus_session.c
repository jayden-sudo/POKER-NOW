/*
 * pbus_session.c -- L2 成員/角色 + L1 發送:發現/建桌/加入/心跳/status/快照/接管/交接。
 * membership 事件(E_ROSTER/E_PLAYER_*)由此發;L3 遊戲事件由 master_engine 發(指南 §6.5)。
 */
#include "pbus_int.h"
#include "master_engine.h"
#include "game_state.h"
#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "pbus_ses";
extern uint8_t g_pbus_last_reject;
static const uint8_t BCAST[6] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

static bool mac_eq(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) == 0; }

void pn_set_role(pbus_role_t r)
{
    static const char *RN[] = { "SCAN", "JOINING", "PENDING", "TEMP_MASTER",
                                "MEMBER", "MASTER", "TAKEOVER_CLAIM" };
    if (g_pb.role != r)
        ESP_LOGI(TAG, "role %s -> %s", RN[g_pb.role], RN[r]);
    g_pb.role = r;
    g_pb.t_role_enter = pn_now_us();
}

/* v1.1 真機修訂:鎖定目標桌進入 JOINING(掃描聽到 ANNOUNCE / 讓桌合併 / dissolve 導引共用) */
static void join_table(uint16_t tid, const uint8_t *master)
{
    g_pb.table_id = tid;
    g_pb.epoch = 0;
    memcpy(g_pb.master_mac, master, 6);
    pn_add_peer(master);
    pn_set_role(ROLE_JOINING);
    g_pb.t_last_beacon_tx = 0;   /* 立即 HELLO */
    ESP_LOGI(TAG, "joining table 0x%04x via %02x:%02x:%02x:%02x:%02x:%02x",
             tid, master[0], master[1], master[2], master[3], master[4], master[5]);
}

static bool is_master_role(void)
{
    return g_pb.role == ROLE_MASTER || g_pb.role == ROLE_TEMP_MASTER;
}

/* mac → player_id(roster 查詢);未找到回 0xFF */
static uint8_t pid_of_mac(const uint8_t *mac)
{
    for (int i = 0; i < g_pb.st.n_players && i < 10; i++)
        if (mac_eq(g_pb.st.p[i].mac, mac)) return (uint8_t)i;
    return 0xFF;
}

/* B6:接管順位 = 距(死亡)Master 座位的環距,越小越優先(協定 §9.2/§9.5);
   排座前後備 player_id 環序 */
static uint8_t claim_rank_of(uint8_t player_id)
{
    uint8_t mpid = pid_of_mac(g_pb.master_mac);
    uint8_t ms = (mpid < 10) ? g_pb.st.p[mpid].seat : 0xFF;
    uint8_t ps = (player_id < 10) ? g_pb.st.p[player_id].seat : 0xFF;
    if (ms == 0xFF || ps == 0xFF) return player_id;
    uint8_t n = 0;
    for (int i = 0; i < g_pb.st.n_players && i < 10; i++)
        if (g_pb.st.p[i].seat != 0xFF && g_pb.st.p[i].seat >= n) n = g_pb.st.p[i].seat;
    n = (uint8_t)(n + 1);
    return (uint8_t)((ps + n - ms) % n);
}

/* ---------------- Master:編序 + 廣播(協定 §6.1) ---------------- */
void pn_publish_locked(uint8_t evt, const void *body, uint16_t len, uint32_t play_at)
{
    uint8_t buf[250];
    if (sizeof(pn_hdr_t) + sizeof(pn_evt_hdr_t) + len > sizeof(buf)) return;
    pn_hdr_t *h = (pn_hdr_t *)buf;
    pn_evt_hdr_t *e = (pn_evt_hdr_t *)(buf + sizeof(pn_hdr_t));
    uint16_t seq = g_pb.seq_alloc++;
    h->magic = PN_MAGIC; h->version = PN_VERSION; h->type = PN_PKT_EVT;
    h->table_id = g_pb.table_id; h->epoch = g_pb.epoch;
    h->len = (uint16_t)(sizeof(pn_evt_hdr_t) + len);
    h->table_ms = pn_clock_table_now();
    e->seq = seq; e->evt = evt; e->_pad = 0; e->play_at = play_at;
    if (len && body) memcpy(buf + sizeof(pn_hdr_t) + sizeof(pn_evt_hdr_t), body, len);
    uint16_t total = (uint16_t)(sizeof(pn_hdr_t) + sizeof(pn_evt_hdr_t) + len);

    pn_log_store(seq, buf, total);
    /* 本機為權威:直接有序交付(不走 OOO) */
    pn_deliver(buf, total);
    g_pb.next_expected_seq = (uint16_t)(seq + 1);

    /* 前向冗餘:一般 2 次,齊播 3 次(此處連發,不阻塞 task) */
    int reps = play_at ? 3 : 2;
    for (int i = 0; i < reps; i++) pn_send(NULL, buf, total);
}

static void send_heartbeat(void)
{
    pn_heartbeat_t hb;
    hb.last_seq = g_pb.last_recv_seq;
    hb.state_crc = pn_crc32(&g_pb.st, sizeof(g_pb.st));
    hb.phase = g_pb.st.phase;
    hb.hand_no = g_pb.st.hand_no;
    hb.master_player = g_pb.my_player_id;
    hb.battery_pct = g_pb.cb.get_battery_pct ? (uint8_t)g_pb.cb.get_battery_pct() : 0xFF;
    pn_send_typed(NULL, PN_PKT_HEARTBEAT, &hb, sizeof(hb));
    g_pb.t_last_hb_tx = pn_now_us();
}

/* ---------------- 快照 ---------------- */
void pn_send_snapshot(const uint8_t dst[6])
{
    pn_snapshot_t snap;
    snap.as_of_seq = g_pb.last_recv_seq;
    snap.epoch = g_pb.epoch;
    snap.state = g_pb.st;
    pn_send_typed(dst, PN_PKT_SNAPSHOT, &snap, sizeof(snap));
}

/* 快照來自被接受 epoch 的任意發送者,整份權威狀態不可全盤盡信:座位/計數/階段若越界,
   後續會成為陣列索引或流入顯示。合法狀態必然通過(座位 <10 或 0xFF、n_players<=10、
   phase 合法),故僅擋畸形快照,不會誤拒正常 resync(#12)。 */
static bool state_sane(const pn_table_state_t *s)
{
    if (s->n_players > PN_MAX_PLAYERS) return false;
    if (s->phase > PN_PH_PAUSED) return false;
    if (s->button_seat != 0xFF && s->button_seat >= PN_MAX_PLAYERS) return false;
    if (s->to_act_seat != 0xFF && s->to_act_seat >= PN_MAX_PLAYERS) return false;
    for (int i = 0; i < PN_MAX_PLAYERS; i++)
        if (s->p[i].seat != 0xFF && s->p[i].seat >= PN_MAX_PLAYERS) return false;
    return true;
}

void pn_apply_snapshot(const pn_snapshot_t *snap)
{
    if (!state_sane(&snap->state)) {
        ESP_LOGW(TAG, "snapshot rejected: state failed sanity check");
        return;
    }
    g_pb.st = snap->state;
    g_pb.next_expected_seq = (uint16_t)(snap->as_of_seq + 1);
    g_pb.last_recv_seq = snap->as_of_seq;
    game_view_publish();
    if (g_pb.cb.on_link) g_pb.cb.on_link(PBUS_LINK_RESYNCED);
}

void pn_roster_add_peers(void)
{
    for (int i = 0; i < g_pb.st.n_players && i < 10; i++)
        if (!mac_eq(g_pb.st.p[i].mac, g_pb.self_mac))
            pn_add_peer(g_pb.st.p[i].mac);
}

/* ---------------- 建桌/加入 ---------------- */
static void become_temp_master(void)
{
    uint16_t tid = 0;
    while (tid == 0) tid = (uint16_t)(esp_random() & 0xFFFF);
    g_pb.table_id = tid;
    g_pb.epoch = 1;
    memcpy(g_pb.master_mac, g_pb.self_mac, 6);
    g_pb.my_player_id = 0;
    g_pb.clock_inited = true; g_pb.clock_offset_q8 = 0;
    g_pb.seq_alloc = 1; g_pb.next_expected_seq = 1; g_pb.last_recv_seq = 0;
    memset(g_pb.dedup, 0, sizeof(g_pb.dedup));   /* 新桌:清空命令去重表,勿沿用舊 (mac,cmd_id) (#17) */

    memset(&g_pb.st, 0, sizeof(g_pb.st));
    for (int i = 0; i < 10; i++) g_pb.st.p[i].seat = 0xFF;
    g_pb.st.table_id = tid;
    g_pb.st.sb = 1; g_pb.st.bb = 2; g_pb.st.bet_cap = 10;
    g_pb.st.phase = PN_PH_LOBBY;
    g_pb.st.button_seat = 0xFF; g_pb.st.to_act_seat = 0xFF;
    g_pb.st.n_players = 1;
    memcpy(g_pb.st.p[0].mac, g_pb.self_mac, 6);
    g_pb.st.p[0].flags = PN_PF_ONLINE;

    pn_set_role(ROLE_TEMP_MASTER);
    g_pb.t_join_open = pn_now_us();
    g_pb.join_extends = 0;
    game_view_publish();
    if (g_pb.cb.on_role) g_pb.cb.on_role(true);
    if (g_pb.cb.on_link) g_pb.cb.on_link(PBUS_LINK_JOINED);
    ESP_LOGI(TAG, "temp master, table_id=0x%04x", tid);
}

static void publish_roster(void)
{
    pn_evt_roster_t r;
    memset(&r, 0, sizeof(r));
    r.n = g_pb.st.n_players;
    for (int i = 0; i < g_pb.st.n_players && i < 10; i++) {
        r.m[i].player_id = (uint8_t)i;
        memcpy(r.m[i].mac, g_pb.st.p[i].mac, 6);
        r.m[i].device_class = 0;
        r.m[i].seat = g_pb.st.p[i].seat;
        r.m[i].flags = g_pb.st.p[i].flags;
        r.m[i].chips = g_pb.st.p[i].chips;
        snprintf(r.m[i].name, sizeof(r.m[i].name), "P%d", i);
    }
    pn_publish_locked(E_ROSTER, &r, sizeof(r), 0);
}

static void close_join_window(void)
{
    ESP_LOGI(TAG, "join window close: n=%u", g_pb.st.n_players);
    if (g_pb.st.n_players >= PK_MIN_PLAYERS) {
        pn_set_role(ROLE_MASTER);
        pn_roster_add_peers();
        publish_roster();
        master_engine_begin();     /* → E_DEALER_CALL 儀式 */
    } else if (g_pb.join_extends < PN_JOIN_EXTEND_MAX) {
        g_pb.join_extends++;
        g_pb.t_join_open = pn_now_us();   /* 延長 5s(下次比較用) */
        ESP_LOGI(TAG, "join window extended (%d)", g_pb.join_extends);
    } else {
        ESP_LOGW(TAG, "not enough players, dissolving");
        pn_send_typed(NULL, PN_PKT_TABLE_DISSOLVE, &(pn_table_dissolve_t){ g_pb.table_id, {0} }, sizeof(pn_table_dissolve_t));
        pbus_leave();
    }
}

/* Master 收 HELLO(LOBBY 期自動接受) */
static void on_hello(const pn_rx_item_t *rx)
{
    if (!is_master_role()) return;
    const pn_hello_t *hello = (const pn_hello_t *)(rx->pkt + sizeof(pn_hdr_t));
    (void)hello;
    uint8_t pid = pid_of_mac(rx->src);
    pn_join_ack_t ack = { .table_id = g_pb.table_id, .epoch = g_pb.epoch,
                          .table_ms = pn_clock_table_now() };

    if (pid != 0xFF) {                 /* 已在桌:回歸 */
        ack.result = (g_pb.st.phase == PN_PH_LOBBY) ? JA_ACCEPT : JA_ACCEPT_BACK;
        ack.player_id = pid;
        pn_add_peer(rx->src);
        pn_send_typed(rx->src, PN_PKT_JOIN_ACK, &ack, sizeof(ack));
        if (ack.result == JA_ACCEPT_BACK) pn_send_snapshot(rx->src);
        ESP_LOGI(TAG, "hello from known pid=%u -> %s", pid,
                 ack.result == JA_ACCEPT ? "ACCEPT" : "ACCEPT_BACK(+snapshot)");
        return;
    }
    /* v1.1 真機修訂:任何 JOIN_ACK(含 PENDING/REJECT)發送前必須先 add_peer ——
       ESP-NOW 對非 peer 的單播靜默失敗(真機實測:PENDING ack 永遠送不到,
       候選者無限 JOINING 超時循環)。 */
    pn_add_peer(rx->src);

    if (g_pb.st.n_players >= PN_MAX_PLAYERS) {
        ack.result = JA_REJECT_FULL;
        pn_send_typed(rx->src, PN_PKT_JOIN_ACK, &ack, sizeof(ack));
        return;
    }
    if (g_pb.st.phase == PN_PH_LOBBY ||
        g_pb.st.phase == PN_PH_DEALER_CALL) {   /* 自動接受(v1.4:莊家確認前晚到者
        仍直接入桌 —— 錯峰開機場景:先開機者停在莊家確認畫面等人到齊,晚到者無縫加入;
        莊家一經確認(排座起)才改走 PENDING 批准流程) */
        pid = g_pb.st.n_players;
        memcpy(g_pb.st.p[pid].mac, rx->src, 6);
        g_pb.st.p[pid].seat = 0xFF;
        g_pb.st.p[pid].flags = PN_PF_ONLINE;
        g_pb.st.n_players++;
        pn_add_peer(rx->src);
        ack.result = JA_ACCEPT; ack.player_id = pid;
        pn_send_typed(rx->src, PN_PKT_JOIN_ACK, &ack, sizeof(ack));
        pn_evt_player_joined_t j = { .player_id = pid, .device_class = 0 };
        memcpy(j.mac, rx->src, 6);
        snprintf(j.name, sizeof(j.name), "P%d", pid);
        pn_publish_locked(E_PLAYER_JOINED, &j, sizeof(j), 0);
        game_view_publish();
        ESP_LOGI(TAG, "hello accepted: new pid=%u (n=%u)", pid, g_pb.st.n_players);
    } else {                                  /* 遊戲中:PENDING */
        ack.result = JA_PENDING;
        pn_send_typed(rx->src, PN_PKT_JOIN_ACK, &ack, sizeof(ack));
        /* v1.1 真機修訂:E_JOIN_PENDING 以 MAC+TTL 去重 —— 候選者每 2s 重發 HELLO,
           不去重會以事件洗版(真機實測 3Hz 灌爆 64 筆事件日誌)。 */
        static struct { uint8_t mac[6]; int64_t t_us; } s_pend_cache[4];
        int64_t now = pn_now_us();
        bool fresh = true;
        int slot = 0;
        for (int i = 0; i < 4; i++) {
            if (mac_eq(s_pend_cache[i].mac, rx->src) &&
                now - s_pend_cache[i].t_us < (int64_t)PN_T_PEND_NOTIFY_MS * 1000) { fresh = false; break; }
            if (s_pend_cache[i].t_us <= s_pend_cache[slot].t_us) slot = i;
        }
        if (fresh) {
            memcpy(s_pend_cache[slot].mac, rx->src, 6);
            s_pend_cache[slot].t_us = now;
            pn_evt_join_pending_t jp = { .cand_id = 0xFF, .device_class = 0 };
            memcpy(jp.mac, rx->src, 6);
            snprintf(jp.name, sizeof(jp.name), "?");
            pn_publish_locked(E_JOIN_PENDING, &jp, sizeof(jp), 0);
            ESP_LOGI(TAG, "join pending: %02x:%02x:%02x:%02x:%02x:%02x",
                     rx->src[0], rx->src[1], rx->src[2], rx->src[3], rx->src[4], rx->src[5]);
        }
    }
}

/* 加入端收 JOIN_ACK */
static void on_join_ack(const pn_rx_item_t *rx)
{
    const pn_join_ack_t *ack = (const pn_join_ack_t *)(rx->pkt + sizeof(pn_hdr_t));
    ESP_LOGI(TAG, "join_ack result=%u pid=%u", ack->result, ack->player_id);
    switch (ack->result) {
    case JA_ACCEPT:
    case JA_ACCEPT_BACK:
        g_pb.table_id = ack->table_id;
        g_pb.epoch = ack->epoch;
        g_pb.my_player_id = ack->player_id;
        memcpy(g_pb.master_mac, rx->src, 6);
        pn_add_peer(rx->src);
        pn_clock_ingest(ack->table_ms);
        g_pb.clock_inited = true;
        g_pb.t_last_hb_rx = pn_now_us();
        pn_set_role(ROLE_MEMBER);
        if (g_pb.cb.on_link)
            g_pb.cb.on_link(ack->result == JA_ACCEPT_BACK ? PBUS_LINK_RESYNCED : PBUS_LINK_JOINED);
        break;
    case JA_PENDING:
        g_pb.table_id = ack->table_id;
        memcpy(g_pb.master_mac, rx->src, 6);
        pn_set_role(ROLE_PENDING);
        if (g_pb.cb.on_link) g_pb.cb.on_link(PBUS_LINK_PENDING);
        break;
    default: /* REJECT_* → 續掃描 */
        break;
    }
}

/* Master 收 CMD */
static void on_cmd(const pn_rx_item_t *rx)
{
    const pn_hdr_t *h = (const pn_hdr_t *)rx->pkt;
    const pn_cmd_hdr_t *ch = (const pn_cmd_hdr_t *)(rx->pkt + sizeof(pn_hdr_t));
    const void *arg = rx->pkt + sizeof(pn_hdr_t) + sizeof(pn_cmd_hdr_t);
    uint16_t arglen = (h->len >= sizeof(pn_cmd_hdr_t)) ? (h->len - sizeof(pn_cmd_hdr_t)) : 0;

    if (!is_master_role()) {                 /* 我不是 Master → 導向現任 */
        uint8_t body[sizeof(pn_cmd_ack_t) + 6];   /* ACK + 現任 Master MAC(§6.3) */
        pn_cmd_ack_t *a = (pn_cmd_ack_t *)body;
        a->cmd_id = ch->cmd_id; a->result = PN_CMD_NOT_MASTER; a->reason = 0;
        memcpy(body + sizeof(pn_cmd_ack_t), g_pb.master_mac, 6);
        pn_send_typed(rx->src, PN_PKT_CMD_ACK, body, sizeof(pn_cmd_ack_t) + 6);
        return;
    }

    uint8_t pid = pid_of_mac(rx->src);
    /* (MAC,cmd_id) 去重 */
    cmd_dedup_t *d = (pid < PN_MAX_PLAYERS) ? &g_pb.dedup[pid] : NULL;
    pn_cmd_ack_t ack = { .cmd_id = ch->cmd_id, .result = PN_CMD_OK, .reason = 0 };
    if (d && d->valid && d->last_cmd_id == ch->cmd_id) {
        ack.result = d->last_result; ack.reason = d->last_reason;   /* 重複:重播 ACK */
    } else {
        pbus_cmd_verdict_t v = { PN_CMD_OK, 0 };
        if (g_pb.cmd_handler && pid != 0xFF)
            v = g_pb.cmd_handler(pid, ch->cmd, arg, arglen);
        else
            v.result = PN_CMD_REJECT, v.reason = PN_RJ_WRONG_PHASE;
        ack.result = v.result; ack.reason = v.reason;
        ESP_LOGI(TAG, "cmd rx: pid=%u cmd=%u -> result=%u reason=%u",
                 pid, ch->cmd, v.result, v.reason);
        if (d) { d->valid = true; memcpy(d->mac, rx->src, 6);
                 d->last_cmd_id = ch->cmd_id; d->last_result = v.result; d->last_reason = v.reason; }
    }
    pn_send_typed(rx->src, PN_PKT_CMD_ACK, &ack, sizeof(ack));
    game_view_publish();
}

/* 命令端收 CMD_ACK */
static void on_cmd_ack(const pn_rx_item_t *rx)
{
    const pn_hdr_t *h = (const pn_hdr_t *)rx->pkt;
    const pn_cmd_ack_t *ack = (const pn_cmd_ack_t *)(rx->pkt + sizeof(pn_hdr_t));
    if (!g_pb.cmd_inflight || ack->cmd_id != g_pb.cmd_inflight_id) return;

    if (ack->result == PN_CMD_NOT_MASTER) {
        if (h->len >= sizeof(pn_cmd_ack_t) + 6)
            memcpy(g_pb.master_mac, (const uint8_t *)ack + sizeof(pn_cmd_ack_t), 6);
        g_pb.cmd_last_send_us = 0;   /* 立即重投(cmd_id 不變,冪等) */
        return;
    }
    g_pb.cmd_inflight = false;
    if (ack->result == PN_CMD_REJECT || ack->result == PN_CMD_STALE)
        g_pbus_last_reject = ack->reason ? ack->reason : PN_RJ_WRONG_PHASE;
    game_view_publish();
}

/* ---------------- 控制包分派 ---------------- */
void pn_session_on_pkt(const pn_rx_item_t *rx)
{
    const pn_hdr_t *h = (const pn_hdr_t *)rx->pkt;

    switch (h->type) {
    case PN_PKT_ANNOUNCE: {
        const pn_announce_t *an = (const pn_announce_t *)(rx->pkt + sizeof(pn_hdr_t));
        if (g_pb.role == ROLE_SCAN) {
            /* v1.1 真機修訂:LOBBY 桌與遊戲中的桌都可鎖定 —— 遊戲中 HELLO 會被回
               ACCEPT_BACK(回歸)或 PENDING(等莊家批准),對應協定 §8.2 */
            join_table(h->table_id, rx->src);
        } else if (g_pb.role == ROLE_TEMP_MASTER && an->phase != PN_PH_LOBBY &&
                   g_pb.st.n_players <= 1) {
            /* B4 硬化:孤家寡人的臨時桌聽到「遊戲中」的桌 → 棄桌改投
               (重啟裝置錯過掃描窗時的第二回歸路徑;HELLO 會被回 ACCEPT_BACK/PENDING) */
            ESP_LOGW(TAG, "lonely lobby yields to in-game table 0x%04x", h->table_id);
            pbus_leave();
            join_table(h->table_id, rx->src);
        } else if (g_pb.role == ROLE_TEMP_MASTER && an->phase == PN_PH_LOBBY &&
                   g_pb.st.phase == PN_PH_LOBBY &&
                   memcmp(rx->src, g_pb.self_mac, 6) < 0) {
            /* v1.1 真機修訂:建桌衝突消解(協定 §8.1.3)—— 對方 MAC 較小,我讓桌:
               廣播解散(附導引目標)後改投對方 */
            ESP_LOGW(TAG, "lobby merge: yielding my table 0x%04x to 0x%04x",
                     g_pb.table_id, h->table_id);
            pn_table_dissolve_t td;
            td.target_table_id = h->table_id;
            memcpy(td.target_mac, rx->src, 6);
            uint16_t my_tid = g_pb.table_id;
            for (int i = 0; i < 3; i++) {
                g_pb.table_id = my_tid;   /* dissolve 包須帶「被解散桌」的 table_id */
                pn_send_typed(NULL, PN_PKT_TABLE_DISSOLVE, &td, sizeof(td));
            }
            pbus_leave();
            join_table(h->table_id, rx->src);
        }
        break;
    }
    case PN_PKT_HELLO:      on_hello(rx); break;
    case PN_PKT_JOIN_ACK:   on_join_ack(rx); break;
    case PN_PKT_CMD:        on_cmd(rx); break;
    case PN_PKT_CMD_ACK:    on_cmd_ack(rx); break;
    case PN_PKT_HEARTBEAT: {
        if (h->table_id != g_pb.table_id) break;
        /* B6 防禦:同桌雙 Master 消解 —— 高 epoch 勝,同 epoch MAC 小者勝(協定 §9.5) */
        if (is_master_role() && h->epoch >= g_pb.epoch) {
            bool peer_wins = (h->epoch > g_pb.epoch) ||
                             (memcmp(rx->src, g_pb.self_mac, 6) < 0);
            if (peer_wins) {
                ESP_LOGW(TAG, "dual-master detected: demoting self (epoch %u vs %u)",
                         g_pb.epoch, h->epoch);
                g_pb.epoch = h->epoch;
                memcpy(g_pb.master_mac, rx->src, 6);
                pn_set_role(ROLE_MEMBER);
                if (g_pb.cb.on_role) g_pb.cb.on_role(false);
                g_pb.t_last_hb_rx = pn_now_us();
                pn_send_typed(rx->src, PN_PKT_SNAP_REQ, NULL, 0);
            }
            break;
        }
        const pn_heartbeat_t *hb = (const pn_heartbeat_t *)(rx->pkt + sizeof(pn_hdr_t));
        if (h->epoch >= g_pb.epoch) {
            g_pb.epoch = h->epoch;
            memcpy(g_pb.master_mac, rx->src, 6);
            g_pb.t_last_hb_rx = pn_now_us();
            pn_clock_ingest(h->table_ms);
            if (g_pb.role == ROLE_TAKEOVER_CLAIM) { pn_set_role(ROLE_MEMBER); }
            /* 落後?請求補洞 */
            if ((int16_t)(hb->last_seq - g_pb.last_recv_seq) > 0) pn_gap_arm_if_needed();
        }
        break;
    }
    case PN_PKT_STATUS:
        /* Master:成員存活/對齊(簡化:僅收下) */
        break;
    case PN_PKT_GAP_REQ: {
        if (!is_master_role()) break;
        const pn_gap_req_t *gr = (const pn_gap_req_t *)(rx->pkt + sizeof(pn_hdr_t));
        for (uint16_t s = gr->from_seq; (int16_t)(s - gr->to_seq) <= 0; s++) {
            const uint8_t *pkt; uint16_t len;
            if (pn_log_get(s, &pkt, &len)) {
                uint8_t tmp[250]; memcpy(tmp, pkt, len);
                ((pn_hdr_t *)tmp)->type = PN_PKT_EVT_RTX;
                pn_send(rx->src, tmp, len);
            }
            if (s == gr->to_seq) break;
        }
        break;
    }
    case PN_PKT_SNAP_REQ:
        if (is_master_role()) pn_send_snapshot(rx->src);
        break;
    case PN_PKT_SNAPSHOT: {
        const pn_snapshot_t *snap = (const pn_snapshot_t *)(rx->pkt + sizeof(pn_hdr_t));
        pn_apply_snapshot(snap);
        break;
    }
    case PN_PKT_MASTER_CLAIM: {
        const pn_master_claim_t *mc = (const pn_master_claim_t *)(rx->pkt + sizeof(pn_hdr_t));
        if (g_pb.role == ROLE_TAKEOVER_CLAIM) {
            /* B6 修:退讓判準 = 更高 last_seq > 更近順位 > 更小 player_id(全序,必有唯一勝者) */
            uint8_t peer_rank = claim_rank_of(mc->player_id);
            bool peer_wins =
                ((int16_t)(mc->last_seq - g_pb.last_recv_seq) > 0) ||
                (mc->last_seq == g_pb.last_recv_seq &&
                 (peer_rank < g_pb.claim_my_rank ||
                  (peer_rank == g_pb.claim_my_rank && mc->player_id < g_pb.my_player_id)));
            if (peer_wins) {
                ESP_LOGW(TAG, "yield takeover to pid=%u (rank %u vs %u)",
                         mc->player_id, peer_rank, g_pb.claim_my_rank);
                pn_set_role(ROLE_MEMBER);
                memcpy(g_pb.master_mac, rx->src, 6);
                g_pb.t_last_hb_rx = pn_now_us();   /* 給勝者心跳緩衝,防立即再判死 */
                pn_claim_info_t ci = { .last_seq = g_pb.last_recv_seq };
                pn_send_typed(rx->src, PN_PKT_CLAIM_INFO, &ci, sizeof(ci));
            }
        } else if (g_pb.role == ROLE_MEMBER) {
            g_pb.t_last_hb_rx = pn_now_us();       /* 選舉進行中,暫緩自己的死亡偵測 */
            pn_claim_info_t ci = { .last_seq = g_pb.last_recv_seq };
            pn_send_typed(rx->src, PN_PKT_CLAIM_INFO, &ci, sizeof(ci));
        }
        break;
    }
    case PN_PKT_CLAIM_INFO:
        /* 接管候選收集(簡化:忽略,以 settle 逾時封頂) */
        break;
    case PN_PKT_TABLE_DISSOLVE: {
        /* v1.1 真機修訂:只解散「指名我這一桌」的 dissolve —— 不檢查 table_id 會被
           同場域任何過期 lobby 的廣播誤殺(真機實測踩中:三人儀式桌被孤桌解散包炸掉)。 */
        if (g_pb.role == ROLE_SCAN || g_pb.table_id == 0) break;
        if (h->table_id != g_pb.table_id) break;
        const pn_table_dissolve_t *td = (const pn_table_dissolve_t *)(rx->pkt + sizeof(pn_hdr_t));
        uint16_t redirect = (h->len >= sizeof(pn_table_dissolve_t) &&
                             td->target_table_id != 0 &&
                             td->target_table_id != g_pb.table_id) ? td->target_table_id : 0;
        uint8_t redirect_mac[6];
        if (redirect) memcpy(redirect_mac, td->target_mac, 6);
        ESP_LOGW(TAG, "table 0x%04x dissolved%s", g_pb.table_id, redirect ? " (redirected)" : "");
        pbus_leave();
        if (redirect) join_table(redirect, redirect_mac);
        break;
    }
    default: break;
    }
}

/* ---------------- 接管上任 ---------------- */
static void takeover_finish(void)
{
    g_pb.epoch++;
    memcpy(g_pb.master_mac, g_pb.self_mac, 6);
    g_pb.seq_alloc = (uint16_t)(g_pb.last_recv_seq + 1);
    /* 新 epoch 上任:清空繼承自舊角色的命令去重表,否則可能對 cmd_id 撞號的新命令
       回放舊 ACK(#17)。 */
    memset(g_pb.dedup, 0, sizeof(g_pb.dedup));
    pn_set_role(ROLE_MASTER);
    pn_roster_add_peers();
    pn_evt_takeover_t tk = { .new_epoch = g_pb.epoch,
                             .new_master_player = g_pb.my_player_id,
                             .dead_master_player = 0xFF };
    pn_publish_locked(E_MASTER_TAKEOVER, &tk, sizeof(tk), 0);
    if (g_pb.cb.on_role) g_pb.cb.on_role(true);   /* engine 依 phase 處置(HAND→abort) */
    send_heartbeat();
    ESP_LOGW(TAG, "took over as master, epoch=%u", g_pb.epoch);
}

/* ---------------- 定時器輪 ---------------- */
void pn_session_tick(void)
{
    int64_t now = pn_now_us();

    switch (g_pb.role) {
    case ROLE_SCAN: {
        int64_t jitter = (int64_t)(esp_random() % 500) * 1000;
        if (now - g_pb.t_scan_start > (int64_t)PN_T_SCAN_MS * 1000 + jitter)
            become_temp_master();
        break;
    }
    case ROLE_JOINING:
        /* v1.1 真機修訂:JOINING 超時回掃描 —— 目標桌消失(對方重啟/斷電)時
           不再永久卡死(真機實測踩中:除錯中反覆重啟的裝置留下幽靈 ANNOUNCE)。 */
        if (now - g_pb.t_role_enter > (int64_t)PN_T_JOIN_GIVEUP_MS * 1000) {
            ESP_LOGW(TAG, "join timeout on table 0x%04x, back to scan", g_pb.table_id);
            pbus_leave();
            break;
        }
        if (now - g_pb.t_last_beacon_tx > (int64_t)PN_T_HELLO_MS * 1000) {
            pn_hello_t hi = { .device_class = (uint8_t)g_pb.device_class,
                              .battery_pct = g_pb.cb.get_battery_pct ? (uint8_t)g_pb.cb.get_battery_pct() : 0xFF,
                              .fw_ver = 1 };
            strncpy(hi.name, g_pb.name, sizeof(hi.name));
            pn_send_typed(g_pb.master_mac, PN_PKT_HELLO, &hi, sizeof(hi));
            g_pb.t_last_beacon_tx = now;
        }
        break;
    case ROLE_PENDING:
        /* v1.1 真機修訂:等待批准期間 Master 失聯(心跳斷 10s)→ 回掃描 */
        if (g_pb.t_last_hb_rx &&
            now - g_pb.t_last_hb_rx > (int64_t)PN_T_PEND_MASTER_LOST_MS * 1000) {
            ESP_LOGW(TAG, "pending master lost, back to scan");
            pbus_leave();
            break;
        }
        if (now - g_pb.t_last_beacon_tx > (int64_t)PN_T_HELLO_PENDING_MS * 1000) {
            pn_hello_t hi = { .device_class = (uint8_t)g_pb.device_class,
                              .battery_pct = 0xFF, .fw_ver = 1 };
            strncpy(hi.name, g_pb.name, sizeof(hi.name));
            pn_send_typed(g_pb.master_mac, PN_PKT_HELLO, &hi, sizeof(hi));
            g_pb.t_last_beacon_tx = now;
        }
        break;
    case ROLE_TEMP_MASTER:
        if (now - g_pb.t_last_beacon_tx > (int64_t)PN_T_ANNOUNCE_MS * 1000) {
            uint16_t rem = 0;
            int64_t elapsed = (now - g_pb.t_join_open) / 1000;
            if (elapsed < PN_T_JOIN_WINDOW_MS) rem = (uint16_t)(PN_T_JOIN_WINDOW_MS - elapsed);
            pn_announce_t an = { .phase = PN_PH_LOBBY, .n_players = g_pb.st.n_players, .join_close_ms = rem };
            pn_send_typed(NULL, PN_PKT_ANNOUNCE, &an, sizeof(an));
            g_pb.t_last_beacon_tx = now;
        }
        if (now - g_pb.t_last_hb_tx > (int64_t)PN_T_HEARTBEAT_MS * 1000) send_heartbeat();
        if (now - g_pb.t_join_open > (int64_t)PN_T_JOIN_WINDOW_MS * 1000) close_join_window();
        break;
    case ROLE_MASTER:
        if (now - g_pb.t_last_hb_tx > (int64_t)PN_T_HEARTBEAT_MS * 1000) send_heartbeat();
        /* v1.1 真機修訂:成桌後續發 ANNOUNCE(1Hz)—— 中途加入(PENDING)與
           重啟回歸(ACCEPT_BACK)靠它找到桌(協定 §8.2;先前實作漏掉,晚開機
           的裝置永遠找不到已成之桌)。 */
        if (now - g_pb.t_last_beacon_tx > (int64_t)PN_T_ANNOUNCE_INGAME_MS * 1000) {
            pn_announce_t an = { .phase = g_pb.st.phase,
                                 .n_players = g_pb.st.n_players,
                                 .join_close_ms = 0xFFFF };
            pn_send_typed(NULL, PN_PKT_ANNOUNCE, &an, sizeof(an));
            g_pb.t_last_beacon_tx = now;
            static uint32_t s_ann_cnt;                     /* B4 診斷:確認 TX 真的在發 */
            if ((++s_ann_cnt % 15) == 1)
                ESP_LOGI(TAG, "in-game announce #%lu (phase=%u)",
                         (unsigned long)s_ann_cnt, g_pb.st.phase);
        }
        break;
    case ROLE_MEMBER:
        if (now - g_pb.t_last_status_tx > (int64_t)PN_T_STATUS_MS * 1000) {
            pn_status_t st = { .last_recv_seq = g_pb.last_recv_seq,
                               .state_crc = pn_crc32(&g_pb.st, sizeof(g_pb.st)),
                               .battery_pct = g_pb.cb.get_battery_pct ? (uint8_t)g_pb.cb.get_battery_pct() : 0xFF };
            pn_send_typed(g_pb.master_mac, PN_PKT_STATUS, &st, sizeof(st));
            g_pb.t_last_status_tx = now;
        }
        /* v1.1 真機修訂:LOBBY 期 Master 失聯 → 不接管、回掃描(§8.1.3 後半;
           先前實作只做了「不接管」,漏了「回掃描」,成員會永久卡在死桌)。 */
        if (g_pb.st.phase == PN_PH_LOBBY &&
            now - g_pb.t_last_hb_rx > (int64_t)PN_T_MASTER_DEAD_MS * 1000) {
            ESP_LOGW(TAG, "lobby master lost, back to scan");
            pbus_leave();
            break;
        }
        /* Master 死亡偵測(LOBBY 期不接管,§8.1.3)。
           B6 修:宣告按順位錯峰(§9.5),否則多成員同時判死 → 同時登基 → 腦裂(真機踩中三 Master)。 */
        if (g_pb.st.phase != PN_PH_LOBBY &&
            now - g_pb.t_last_hb_rx > (int64_t)PN_T_MASTER_DEAD_MS * 1000) {
            pn_set_role(ROLE_TAKEOVER_CLAIM);
            g_pb.claim_my_rank = claim_rank_of(g_pb.my_player_id);
            g_pb.t_claim_start = now +
                (int64_t)g_pb.claim_my_rank * PN_T_CLAIM_STAGGER_MS * 1000;
            g_pb.claim_sent = false;
            g_pb.claim_best_seq = g_pb.last_recv_seq;
            ESP_LOGW(TAG, "master dead, claim rank=%u (stagger %ums)",
                     g_pb.claim_my_rank, g_pb.claim_my_rank * PN_T_CLAIM_STAGGER_MS);
            if (g_pb.cb.on_link) g_pb.cb.on_link(PBUS_LINK_LOST);
        }
        break;
    case ROLE_TAKEOVER_CLAIM:
        if (!g_pb.claim_sent && now >= g_pb.t_claim_start) {
            pn_master_claim_t mc = { .epoch = (uint16_t)(g_pb.epoch + 1),
                                     .last_seq = g_pb.last_recv_seq,
                                     .player_id = g_pb.my_player_id };
            for (int i = 0; i < 3; i++)
                pn_send_typed(NULL, PN_PKT_MASTER_CLAIM, &mc, sizeof(mc));
            g_pb.claim_sent = true;
        }
        if (g_pb.claim_sent &&
            now - g_pb.t_claim_start > (int64_t)PN_T_CLAIM_SETTLE_MS * 1000)
            takeover_finish();
        break;
    }

    /* CMD 客戶端重試(§6.3) */
    if (g_pb.cmd_inflight) {
        if (now - g_pb.cmd_last_send_us > (int64_t)PN_T_CMD_RETRY_MS * 1000) {
            if (g_pb.cmd_retries > PN_CMD_RETRY) {
                g_pb.cmd_inflight = false;
                if (g_pb.cb.on_link) g_pb.cb.on_link(PBUS_LINK_LOST);
            } else {
                uint8_t buf[sizeof(pn_cmd_hdr_t) + 210];
                pn_cmd_hdr_t *ch = (pn_cmd_hdr_t *)buf;
                ch->cmd_id = g_pb.cmd_inflight_id;
                ch->cmd = g_pb.cmd_inflight_code;
                ch->_pad = 0;
                memcpy(buf + sizeof(pn_cmd_hdr_t), g_pb.cmd_inflight_body, g_pb.cmd_inflight_len);
                pn_send_typed(g_pb.master_mac, PN_PKT_CMD, buf, (uint16_t)(sizeof(pn_cmd_hdr_t) + g_pb.cmd_inflight_len));
                g_pb.cmd_last_send_us = now;
                g_pb.cmd_retries++;
            }
        }
    }

    (void)BCAST;
}
