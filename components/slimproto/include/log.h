#ifndef __LOGS_H__
#define __LOGS_H__

#include "esp_log.h"

#if CONFIG_LOG_COLORS

#undef LOG_COLOR_W
#undef LOG_COLOR_I
#undef LOG_COLOR_D
#undef LOG_COLOR_V
#undef LOG_COLOR_E

//#define COLOR_LIGHTRED  "\033[31;1m"
#define LOG_COLOR_E       "\033[31m"
//#define COLOR_LIGHTBLUE "\033[34;1m"
#define LOG_COLOR_D       "\033[34m"
#define LOG_COLOR_I       "\033[32;1m"
#define LOG_COLOR_W       "\033[33;1m"
//#define COLOR_ORANGE    "\033[0;33m"
//#define COLOR_WHITE     "\033[37;1m"
//#define COLOR_LIGHTCYAN "\033[36;1m"
#define LOG_COLOR_V       "\033[36m"
//#define COLOR_RESET     "\033[0m"
//#define COLOR_HIGH      "\033[1m"

#endif

#define LOG_DOMAIN(dom) static const char * _log_domain = dom;

#define WRN(fmt,arg...)				\
  ESP_LOGW(_log_domain, fmt,##arg)

#define ERR(fmt,arg...)				\
  ESP_LOGE(_log_domain, fmt,##arg)

#define VER(fmt,arg...)				\
  ESP_LOGV(_log_domain, fmt,##arg)

#define DBG(fmt,arg...)				\
  ESP_LOGI(_log_domain, fmt,##arg)

#define INF(fmt,arg...)				\
  ESP_LOGI(_log_domain, fmt,##arg)

#endif /* __LOGS_H__ */
