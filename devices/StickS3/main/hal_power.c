/* hal_power.c -- 關機攔截(產品 §2.5,指南 §13.5)。
 * StickS3 關機 = PM1 雙擊,硬體攔截不到;app_prepare_poweroff() 永不被板級呼叫。
 * 協定 §9.5 故障接管兜底。此處僅提供空實作供介面完整。 */
#include "hal/hal_power.h"

void app_prepare_poweroff(void)
{
    /* 若本機為 Master 應觸發 §9.3 交接;StickS3 無法攔截雙擊關機,故為 no-op。
     * 保留符號讓其他裝置的板級關機路徑可呼叫共用實作。 */
}
