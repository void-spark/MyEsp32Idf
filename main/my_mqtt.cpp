#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG = "MyMqtt";

static const int MQTT_CONNECTED_BIT = BIT0;

// MQTT details
static const char* mqtt_server = "mqtt://raspberrypi.fritz.box";

static EventGroupHandle_t mqttEventGroup;

static char deviceTopic[30] = {};

static esp_mqtt_client_handle_t mqttClient;

static void (*messageHandler)(char* topic, char* data);

void publishDevProp(char *deviceProperty, char *value) {
	char topic[50];
	snprintf(topic, sizeof(topic), "%s/$%s", deviceTopic, deviceProperty);
    int msg_id = esp_mqtt_client_publish(mqttClient, topic, value, 0, 1, 1);
}

void publishNodeProp(char *nodeId, char *property, char *value) {
	char topic[60];
	snprintf(topic, sizeof(topic), "%s/%s/%s", deviceTopic, nodeId, property);
    int msg_id = esp_mqtt_client_publish(mqttClient, topic, value, 0, 1, 1);
}

void subscribeDevTopic(char *subTopic) {
	char topic[50];
	snprintf(topic, sizeof(topic), "%s/%s", deviceTopic, subTopic);
    int msg_id = esp_mqtt_client_subscribe(mqttClient, topic, 2);
}

static void handleMessage(char* topic, char* data) {
    // if(strncmp(topic, deviceTopic, strlen(deviceTopic)) != 0) {
    //     ESP_LOGE(TAG, "MQTT Topic '%s' does not start with our root topic '%s'", topic, deviceTopic);
    //     return;
    // }

    messageHandler(topic, data);
}

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

            {
                char topic[50] = {};
                if(event->topic_len >= sizeof(topic)) {
                    ESP_LOGE(TAG, "MQTT Topic length %d exceeds maximum %d", event->topic_len, sizeof(topic));
                    break;
                }
                snprintf(topic, sizeof(topic), "%.*s", event->topic_len, event->topic);

                char data[50] = {};
                if(event->data_len >= sizeof(data)) {
                    ESP_LOGE(TAG, "MQTT Data length %d exceeds maximum %d", event->data_len, sizeof(data));
                    break;
                }
                snprintf(data, sizeof(data), "%.*s", event->data_len, event->data);

                handleMessage(topic, data);
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
    }
    return ESP_OK;
}

void mqttStart(void (*messageHandlerArg)(char* topic, char* data)) {
    messageHandler = messageHandlerArg;

    mqttEventGroup = xEventGroupCreate();

    // Get the default MAC of this ESP32
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_efuse_mac_get_default(mac));

    // Create a topic for this device
    snprintf(deviceTopic, sizeof(deviceTopic), "%s/%02x%02x%02x%02x%02x%02x", "devices", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    char lwt_topic[40];
	snprintf(lwt_topic, sizeof(lwt_topic), "%s/$online", deviceTopic);

    // esp_mqtt_client_init will make copies of all the provided config data
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.uri = mqtt_server;
    mqtt_cfg.event_handle = mqttEventHandler;
    mqtt_cfg.lwt_topic = lwt_topic;
    mqtt_cfg.lwt_msg = "false";
    mqtt_cfg.lwt_qos = 2;
    mqtt_cfg.lwt_retain = 1;

    mqttClient = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(mqttClient);
}

void mqttWait() {
    xEventGroupWaitBits(mqttEventGroup, MQTT_CONNECTED_BIT, false, true, portMAX_DELAY);
}

void mqttPublish(const char *topic, const char *data, int len, int qos, int retain) {
    int msg_id = esp_mqtt_client_publish(mqttClient, topic, data, len, qos, retain);
}
