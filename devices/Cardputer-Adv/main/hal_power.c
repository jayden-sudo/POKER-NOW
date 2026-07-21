/* hal_power.c -- 關機攔截(產品 §2.5)。
 * Cardputer-Adv 關機 = 側面電源開關物理切斷(P-MOS),軟體攔截不到;
 * app_prepare_poweroff() 永不被板級呼叫,協定 §9.5 故障接管兜底(產品層已知限制)。
 * R11:main 提供單一 no-op 定義,common 不定義以免重複符號。 */
#include "hal/hal_power.h"
void app_prepare_poweroff(void) { }
