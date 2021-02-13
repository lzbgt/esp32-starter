#ifndef __APP_COMMON_H__
#define __APP_COMMON_H__
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <string.h>
#include <esp_http_server.h>
#include <freertos/event_groups.h>
#include <mqtt_client.h>

#define APP_WIFI_RETRY_MAX 5

#define APP_EBIT_WIFI_START_AP (1 << 0)
#define APP_EBIT_WIFI_START_STA (1 << 1)

#define DELAYMS(ms)                            \
    do                                         \
    {                                          \
        vTaskDelay((ms) / portTICK_PERIOD_MS); \
    } while (0);

typedef struct WIFIManagerConfig
{
    EventGroupHandle_t *pxEvtGroup;
    EventBits_t *pxBitsToWaitFor;
    char **ap_ssid;
    char **ap_passwd;
    char **sta_ssid;
    char **sta_passwd;
    BaseType_t xApChange;
} WIFIManagerConfig;

typedef int (*mqtt_ondata_callback)(esp_mqtt_event_handle_t event);
extern void mqtt_app_start(mqtt_ondata_callback cb);

typedef struct DeviceInfo
{
    const char *const model;
    const char *const sn;
    const char *const version;
    const char *const owner;
    const char *tags;
} DeviceInfo;

extern DeviceInfo *getDeviceInfo();
extern char *deviceInfoToJson();
extern void servoTask(void *args);
extern esp_netif_t *getNetif();
extern void setNetif(esp_netif_t *nif);

#endif