/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF AaNY KIND, either express or implied.
*/
#include "app_common.h"
#include "app_wifi.h"
#include "app_httpsrv.h"
#include "app_spiffs.h"
#include <freertos/task.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <esp_spi_flash.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <lwip/err.h>
#include <lwip/sys.h>
#include <cJSON.h>
#include <sntp.h>
#include <time.h>

static const char *TAG = "hello";

// wifi manager globals
EventGroupHandle_t xAppEventGroup;
EventBits_t xBitsToWaitFor = APP_EBIT_WIFI_START_AP | APP_EBIT_WIFI_START_STA;

static esp_netif_t *_netif = NULL;

esp_netif_t *getNetif()
{
    return _netif;
}

void setNetif(esp_netif_t *nif)
{
    _netif = nif;
}

#define DEV_MODEL "ESP32"
#define DEV_SN "HS00001"

DeviceInfo devInfo = {
    .model = DEV_MODEL,
    .sn = DEV_SN,
    .owner = "bruce.lu",
    .tags = "home;dev",
    .version = "0.0.1",
};

char _constPasswd[] = "hubstack.cn";
char *ap_ssid = DEV_SN;
char *ap_passwd = _constPasswd;
char *wifi_ssid = NULL, *wifi_passwd = NULL;

WIFIManagerConfig xWifiMgrCfg = {
    .ap_ssid = &ap_ssid,
    .ap_passwd = &ap_passwd,
    .sta_ssid = &wifi_ssid,
    .sta_passwd = &wifi_passwd,
    .pxEvtGroup = &xAppEventGroup,
    .pxBitsToWaitFor = &xBitsToWaitFor,
    .xApChange = 0,
};

DeviceInfo *getDeviceInfo()
{
    return &devInfo;
}

char *getNowTimeStr()
{
    time_t now;
    char *strftime_buf = (char *)malloc(64);
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, 64, "%F %T", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in Shanghai is: %s", strftime_buf);

    return strftime_buf;
}

char *deviceInfoToJson()
{
    DeviceInfo *info = &devInfo;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "model", cJSON_CreateString(info->model));
    cJSON_AddItemToObject(root, "sn", cJSON_CreateString(info->sn));
    cJSON_AddItemToObject(root, "version", cJSON_CreateString(info->version));
    cJSON_AddItemToObject(root, "owner", cJSON_CreateString(info->owner));
    cJSON_AddItemToObject(root, "tags", cJSON_CreateString(info->tags));

    char *now = getNowTimeStr();
    cJSON_AddItemToObject(root, "time", cJSON_CreateString(now));
    char *tmp = cJSON_Print(root);
    char *ret = (char *)malloc(strlen(tmp) + 1);
    memcpy(ret, tmp, strlen(tmp) + 1);
    cJSON_Delete(root);
    free(now);

    return ret;
}

// http server globals
httpd_handle_t httpsrv = NULL;

void syncNtpTime()
{
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "cn.pool.ntp.org");
    sntp_init();
    // Set timezone to China Standard Time
    setenv("TZ", "CST-8", 1);
    tzset();

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 30;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count)
    {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(6000 / portTICK_PERIOD_MS);
    }
    if (retry >= 30)
    {
        ESP_LOGE(TAG, "failed sync time");
    }
    else
    {
        time(&now);
        localtime_r(&now, &timeinfo);
    }
}

int mqttCallback(esp_mqtt_event_handle_t event)
{
    return 0;
}

void onStaOK()
{
    syncNtpTime();

    if (!httpsrv)
        start_http_srv(&httpsrv, &xWifiMgrCfg);
    mqtt_app_start(mqttCallback);
}

void onApOk()
{
    if (!httpsrv)
        start_http_srv(&httpsrv, &xWifiMgrCfg);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    static int s_retry_num = 0;
    ESP_LOGE(TAG, "event got %s, %d", event_base, event_id);
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGE(TAG, "disconnected from AP");
            if (s_retry_num < APP_WIFI_RETRY_MAX)
            {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "retry connect AP");
            }
            else
            {
                ESP_LOGI(TAG, "swith to ap mode");
                xEventGroupSetBits(xAppEventGroup, APP_EBIT_WIFI_START_AP);
                s_retry_num = 0;
            }
            break;
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_CONNECTED:
            break;

        default:
            break;
        }

        return;
    }

    if (event_base == IP_EVENT)
    {
        switch (event_id)
        {
        case IP_EVENT_STA_GOT_IP:
        {
            s_retry_num = 0;
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "esp got ip:" IPSTR, IP2STR(&event->ip_info.ip));
            onStaOK();
        }
        break;
        case IP_EVENT_AP_STAIPASSIGNED:
        {
            ip_event_ap_staipassigned_t *event = (ip_event_ap_staipassigned_t *)event_data;
            ESP_LOGI(TAG, "client got ip:" IPSTR, IP2STR(&event->ip));
            onApOk();
        }
        break;
        }

        return;
    }
}

static void vTaskWIFIManager(void *pvParameters)
{
    WIFIManagerConfig *pxCfg = (WIFIManagerConfig *)pvParameters;
    EventBits_t xEventGroupValue = 0;
    while (true)
    {
        xEventGroupValue = xEventGroupWaitBits(*pxCfg->pxEvtGroup,
                                               *pxCfg->pxBitsToWaitFor, pdTRUE, pdFALSE, portMAX_DELAY); //1000/portTICK_PERIOD_MS);
        //
        if ((xEventGroupValue & APP_EBIT_WIFI_START_AP) != 0)
        {
            pxCfg->xApChange = 1;
            start_wifi_ap(*pxCfg->ap_ssid, *pxCfg->ap_passwd);
            pxCfg->xApChange = 0;
        }
        else if ((xEventGroupValue & APP_EBIT_WIFI_START_STA) != 0)
        {
            pxCfg->xApChange = 2;
            ESP_LOGI(TAG, "WILL START STA with %s, %s", *pxCfg->sta_ssid, *pxCfg->sta_passwd);
            start_wifi_sta(*pxCfg->sta_ssid, *pxCfg->sta_passwd);
            pxCfg->xApChange = 0;
        }
        else
        {
            // // not needed
            // DELAYMS(500);
        }

        ESP_LOGI(TAG, "FINISHED WIFIManager");
    }
}

static void vTaskStats(void *pvParam)
{
    int i = 0;
    for (;; i++)
    {
        if (i % 10 == 0)
        {
            ESP_LOGD(TAG, "i lived %d secs.", i * 100);
        }

        DELAYMS(10000)
    }
}

void app_main()
{
    xAppEventGroup = xEventGroupCreate();

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    esp_netif_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    setNetif(esp_netif_create_default_wifi_sta()); // important!! or IP event handler will not be trigger

    // init wifi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    xTaskCreate(vTaskWIFIManager, "WIFI Mgr", 1024 * 8, &xWifiMgrCfg, 8, NULL); //configMAX_PRIORITIES
    // xTaskCreate(vTaskStats, "stats", 1000, NULL, configMAX_CO_ROUTINE_PRIORITIES, NULL);

    esp_event_handler_instance_t wifi_event_instance;
    esp_event_handler_instance_t ip_event_instance;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &wifi_event_instance));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &ip_event_instance));
    // get previous stored wifi configuration
    wifi_config_t wifi_config;
    ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_config));

    if (strlen((const char *)wifi_config.sta.ssid))
    {
        ESP_LOGI(TAG, "Found ssid %s", (const char *)wifi_config.sta.ssid);
        ESP_LOGI(TAG, "Found password %s", (const char *)wifi_config.sta.password);
        *xWifiMgrCfg.sta_ssid = (char *)malloc(strlen((char *)wifi_config.sta.ssid) + 1);
        strcpy(*xWifiMgrCfg.sta_ssid, (char *)wifi_config.sta.ssid);
        *xWifiMgrCfg.sta_passwd = (char *)malloc(strlen((char *)wifi_config.sta.password) + 1);
        strcpy(*xWifiMgrCfg.sta_passwd, (char *)wifi_config.sta.password);
        xEventGroupSetBits(xAppEventGroup, APP_EBIT_WIFI_START_STA);
        //start_wifi_sta(*xWifiMgrCfg.sta_ssid, *xWifiMgrCfg.sta_passwd);
    }
    else
    {
        xEventGroupSetBits(xAppEventGroup, APP_EBIT_WIFI_START_AP);
    }

    //xTaskCreate(servoTask, "servo task", 2048, NULL, 5, NULL);

    // vTaskStartScheduler();
    // esp_event_loop_delete(system_event_handler);
}
