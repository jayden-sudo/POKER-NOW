/* hal_power_board.h -- 板內私有(不進 hal/) */
#pragma once
void power_latch_on(void);     /* app_main 第一行 */
void board_poweroff(void);     /* hal_input 中鍵長按呼叫:交接→斷電,不返回 */
