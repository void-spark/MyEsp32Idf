#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include "my_display.h"

static const char *TAG = "MyMqtt";

static const int MQTT_CONNECTED_BIT = BIT0;

// MQTT details
static const char* mqtt_server = "mqtt://raspberrypi.fritz.box";

static EventGroupHandle_t mqttEventGroup;

static esp_mqtt_client_handle_t mqttClient;

static esp_err_t mqttEventHandler(esp_mqtt_event_handle_t event) {
    esp_mqtt_client_handle_t client = event->client;
    switch (event->event_id) {
        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT");
            break;
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

            esp_mqtt_client_subscribe(client, "doorbell/header", 2);

            xEventGroupSetBits(mqttEventGroup, MQTT_CONNECTED_BIT);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            xEventGroupClearBits(mqttEventGroup, MQTT_CONNECTED_BIT);
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\n", event->topic_len, event->topic);
            printf("DATA=%.*s\n", event->data_len, event->data);

            if(strncmp("doorbell/header", event->topic, event->topic_len) == 0) {
                updateHeader(event->data_len, event->data);
            }

            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
    }
    return ESP_OK;
}

void mqttStart() {
    mqttEventGroup = xEventGroupCreate();

    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.uri = mqtt_server;
    mqtt_cfg.event_handle = mqttEventHandler;

    mqttClient = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(mqttClient);
}

void mqttWait() {
    xEventGroupWaitBits(mqttEventGroup, MQTT_CONNECTED_BIT, false, true, portMAX_DELAY);
}

void mqttPublish(const char *topic, const char *data, int len, int qos, int retain) {
    int msg_id = esp_mqtt_client_publish(mqttClient, topic, data, len, qos, retain);
}
