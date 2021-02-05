#include "esp_log.h"
#include "app_common.h"

static const char *TAG = "MQTT";
static mqtt_ondata_callback callback = NULL;

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "error %s: 0x%x", message, error_code);
    }
}

char *concatStr(const char *a, const char *b)
{
    int la = strlen(a);
    int lb = strlen(b);
    char *buf = (char *)malloc(la + lb + 1);
    memcpy(buf, a, la);
    memcpy(buf + la, b, lb);
    buf[la + lb] = 0;
    return buf;
}

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    // your_context_t *context = event->context;
    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
    {
        ESP_LOGI(TAG, "MQTT CONNECTED");
        char *devInfo = deviceInfoToJson();
        char sbase[] = "/hubstack/dev/stat/";
        char dbase[] = "/hubstackcn/dev/dl/";
        char *topic = concatStr(sbase, getDeviceInfo()->sn);
        msg_id = esp_mqtt_client_publish(client, topic, devInfo, 0, 1, 1);
        ESP_LOGI(TAG, "sent state to %s, %d", topic, msg_id);
        free(devInfo);
        free(topic);

        topic = concatStr(dbase, getDeviceInfo()->sn);
        esp_mqtt_client_subscribe(client, topic, 0);
        ESP_LOGI(TAG, "subscribed to %s", topic);
        free(topic);
    }

    // msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
    // ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

    // msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
    // ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
    break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGE(TAG, "mqtt disconn");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        // ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        // msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        // ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        //ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        //ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        if (callback != NULL)
        {
            callback(event);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    mqtt_event_handler_cb(event_data);
}

void mqtt_app_setHandler(mqtt_ondata_callback cb)
{
    callback = cb;
}
void mqtt_app_start(mqtt_ondata_callback cb)
{
    callback = cb;
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = "mqtt://etsme:etSme0704@hubstack.cn:1883",
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
}