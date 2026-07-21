/*
 * number_speech.c -- 0..9999 → voice_id 序列(產品 §6.2,指南 §10 照抄)。
 * 純函式,無 esp 依賴。
 */
#include "voice.h"
#include "pk_config.h"

static int push_00_99(voice_id_t *o, int k, unsigned v)
{
    if (v == 0) return k;
    if (v <= 20) { o[k++] = (voice_id_t)(V_N0 + v); return k; }
    unsigned t = v / 10, u = v % 10;
    o[k++] = (t == 2) ? V_N20 : (voice_id_t)(V_N30 + (t - 3));
    if (u) o[k++] = (voice_id_t)(V_N0 + u);
    return k;
}

int voice_expand_number(uint16_t n, voice_id_t out[8])
{
    int k = 0;
    if (n > PK_CHIPS_MAX) n = PK_CHIPS_MAX;
    if (n == 0) { out[0] = V_N0; return 1; }
    if (n >= 1000) { out[k++] = (voice_id_t)(V_N0 + n / 1000); out[k++] = V_THOUSAND; n %= 1000; }
    if (n >= 100)  { out[k++] = (voice_id_t)(V_N0 + n / 100);  out[k++] = V_HUNDRED;  n %= 100; }
    return push_00_99(out, k, n);
}
