#pragma once
/*
 * pbus.h -- 協定層對遊戲邏輯的完整介面(協定 §14 + 裁定 R2 補遺)。
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "pbus_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PBUS_LINK_JOINED, PBUS_LINK_PENDING, PBUS_LINK_LOST,
    PBUS_LINK_RESYNCED, PBUS_LINK_DISSOLVED,
} pn_link_state_t;

typedef struct {
    void (*on_event)(const pn_evt_hdr_t *e, const void *body, size_t len);
    void (*on_role)(bool i_am_master);
    void (*on_link)(pn_link_state_t s);
    uint32_t (*get_battery_pct)(void);   /* 0xFF=未知(裁定 R3) */
} pbus_callbacks_t;

esp_err_t pbus_start(const pbus_callbacks_t *cb, const char *name);
esp_err_t pbus_submit_cmd(uint8_t cmd, const void *arg, size_t len);
esp_err_t pbus_publish_evt(uint8_t evt, const void *body, size_t len, uint32_t play_at);
uint32_t  pbus_table_now(void);
uint32_t  pbus_local_time_for(uint32_t table_ms);
const pn_table_state_t *pbus_state(void);

/* ---- 介面補遺(裁定 R2)---- */
typedef struct { uint8_t result; uint8_t reason; } pbus_cmd_verdict_t;
typedef pbus_cmd_verdict_t (*pbus_cmd_handler_t)(uint8_t player_id, uint8_t cmd,
                                                 const void *arg, size_t len);
void    pbus_set_cmd_handler(pbus_cmd_handler_t h);
void    pbus_set_idle_hook(void (*hook)(void));
uint8_t pbus_my_player_id(void);         /* 0xFF=未入桌 */
bool    pbus_is_master(void);
void    pbus_leave(void);

/* ---- 供 master_engine 使用的內部 session 操作(src 私有標頭不宜暴露的最小集合) ---- */
uint16_t pbus_device_class(void);        /* 本機 device_class,供發 roster 用 */

/* ---- 客戶端命令回饋(裁定 R2 延伸:協定 §14 未給 client 側 ACK 路徑) ---- */
bool    pbus_cmd_inflight(void);         /* 有命令在途等 ACK */
uint8_t pbus_consume_reject(void);       /* 最近 REJECT/STALE reason;讀取即清零 */

#ifdef __cplusplus
}
#endif
