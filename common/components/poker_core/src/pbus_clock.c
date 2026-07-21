/*
 * pbus_clock.c -- 桌面時鐘 EWMA(協定 §7)。offset = table_ms - local_ms,α=1/4(定點 Q8)。
 */
#include "pbus_int.h"

void pn_clock_reset(void)
{
    g_pb.clock_inited = false;
    g_pb.clock_offset_q8 = 0;
    g_pb.clock_freeze_hb = 0;
}

void pn_clock_ingest(uint32_t table_ms)
{
    int32_t sample = (int32_t)(table_ms - pn_local_ms());
    int64_t sample_q8 = (int64_t)sample << 8;
    if (!g_pb.clock_inited) {
        g_pb.clock_offset_q8 = sample_q8;
        g_pb.clock_inited = true;
    } else if (g_pb.clock_freeze_hb > 0) {
        g_pb.clock_freeze_hb--;               /* 新任前 2 心跳只驗證不更新 */
    } else {
        g_pb.clock_offset_q8 += (sample_q8 - g_pb.clock_offset_q8) / 4;  /* α=0.25 */
    }
}

uint32_t pn_clock_table_now(void)
{
    if (!g_pb.clock_inited) return pn_local_ms();
    return (uint32_t)((int32_t)pn_local_ms() + (int32_t)(g_pb.clock_offset_q8 >> 8));
}

uint32_t pn_clock_local_for(uint32_t table_ms)
{
    if (!g_pb.clock_inited) return table_ms;
    return (uint32_t)((int32_t)table_ms - (int32_t)(g_pb.clock_offset_q8 >> 8));
}
