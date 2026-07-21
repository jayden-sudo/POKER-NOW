/* hal_power.c -- 關機攔截(產品 §2.5;S3 §21.3 R11)。
 * xingzhi 關機 = 第 4 顆硬體電源鍵長按 ≈2s,硬體層直接斷電,韌體不可見、不可攔截;
 * 拉低 GPIO21 亦無法可靠關機(插 USB 時 5V 旁路自鎖,HARDWARE.md §2)。
 * app_prepare_poweroff() 永不被本板呼叫;協定 §9.5 故障接管兜底。空殼供介面完整。 */
#include "hal/hal_power.h"

void app_prepare_poweroff(void)
{
    /* no-op(同 StickS3 慣例;唯一真實作在 zuowei,其 GPIO13 自鎖可攔截) */
}
