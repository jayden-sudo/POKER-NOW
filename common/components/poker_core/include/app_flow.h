#pragma once
/*
 * app_flow.h -- 畫面狀態機入口。
 */
#ifdef __cplusplus
extern "C" {
#endif

void app_flow_start(const char *device_name);  /* 建 UI task;內部呼叫 game_init */

#ifdef __cplusplus
}
#endif
