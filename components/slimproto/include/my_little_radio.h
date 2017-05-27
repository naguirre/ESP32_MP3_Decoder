#ifndef __MY_LITTLE_RADIO_H__
#define __MY_LITTLE_RADIO_H__

#define CONNECTED_BIT BIT0
#define PLAY_BIT BIT1

#define APP_VERSION "0.1"

#include <nvs.h>

extern EventGroupHandle_t mlr_wifi_event_group;
extern nvs_handle mlr_nvs_handle;

#endif /*__MY_LITTLE_RADIO_H__*/
