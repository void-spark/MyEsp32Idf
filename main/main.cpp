#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_spi_flash.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp32_digital_led_lib.h"
extern "C" {
#include "mqtt_client.h"
}
#include "LedBlink2.h"
#include "RcReceiver.h"
#include "player.h"

#define LED_BUILTIN GPIO_NUM_2
#define LED1_EXT GPIO_NUM_27
#define LED2_EXT GPIO_NUM_32
#define LED3_EXT GPIO_NUM_25

#define WS2812_1 GPIO_NUM_26
#define NUM_PIXELS 8

#define RCV1_EXT GPIO_NUM_15

#define RC_BITS 25
#define TRIS (RC_BITS - 1) / 2

// WiFi credentials.
#define WIFI_SSID "Milkrun"
#define WIFI_PASS "55382636751623425906"

// MQTT details
const char* mqtt_server = "mqtt://raspberrypi.fritz.box";


/* FreeRTOS event group to signal app status changes*/
static EventGroupHandle_t app_event_group;

const int WIFI_CONNECTED_BIT = BIT0;
const int MQTT_CONNECTED_BIT = BIT1;

static const char *TAG = "MyEsp32";


long ledPatternPendingWiFi[] = {100,100};
long ledPatternPendingMqtt[] = {100,300};
long ledPatternConnected[] = {100,900};

long ledPatternRc[] = {150,150};

LedBlink2 blinkerInt(LED_BUILTIN);
LedBlink2 blinkerExt1(LED1_EXT);

RcReceiver rcReceiver(RC_BITS, 12);

esp_mqtt_client_handle_t mqttClient;

strand_t strands[] = { {.rmtChannel = 0, .gpioNum = WS2812_1, .ledType = LED_WS2812B_V3, .brightLimit = 32, .numPixels =  NUM_PIXELS,
   .pixels = nullptr, ._stateVars = nullptr} };

strand_t * strand = &strands[0];

void printAc();

IRAM_ATTR void myHandleInterrupt(void *) {
  const unsigned long time = esp_timer_get_time();
  //int state = digitalRead(RCV1_EXT);
  rcReceiver.handleInterrupt(time);
  //digitalWrite(TMP_OUT, state);
}


static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "got ip:%s",
                 ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        xEventGroupSetBits(app_event_group, WIFI_CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        esp_wifi_connect();
        xEventGroupClearBits(app_event_group, WIFI_CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            xEventGroupSetBits(app_event_group, MQTT_CONNECTED_BIT);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            xEventGroupClearBits(app_event_group, MQTT_CONNECTED_BIT);
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
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
    }
    return ESP_OK;
}

void wifi_init_sta() {
    app_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL) );

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, WIFI_PASS);

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    ESP_LOGI(TAG, "connect to ap SSID:%s password:%s", WIFI_SSID, WIFI_PASS);
}


static void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {};
    strcpy(mqtt_cfg.uri, mqtt_server);
    mqtt_cfg.event_handle = mqtt_event_handler;

    mqttClient = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(mqttClient);
}


extern "C" void app_main() {

    gpio_pad_select_gpio(WS2812_1);
    gpio_set_direction(WS2812_1, GPIO_MODE_OUTPUT);
    gpio_set_level(WS2812_1, 0);

    playerSetup();

    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ",
        chip_info.cores,
        (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
        (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d, ", chip_info.revision);

    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
        (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");


    if (digitalLeds_initStrands(strands, 1)) {
        printf("Failed to initialize ws2812\n");
        while (true) {};
    }
    digitalLeds_resetPixels(strand);


    for (int i = 0; i < strand->numPixels; i++) {
        strand->pixels[i] = pixelFromRGB(5, 0, 0);
    }
    digitalLeds_updatePixels(strand);

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);


    // Initialize WiFi
    wifi_init_sta();

    blinkerInt.setPattern(ledPatternPendingWiFi);
    
    ESP_LOGI(TAG, "Waiting for wifi");
    xEventGroupWaitBits(app_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);

    blinkerInt.setPattern(ledPatternPendingMqtt);

    mqtt_app_start();

    ESP_LOGI(TAG, "Waiting for MQTT");
    xEventGroupWaitBits(app_event_group, MQTT_CONNECTED_BIT, false, true, portMAX_DELAY);

    blinkerInt.setPattern(ledPatternConnected);

    gpio_config_t io_conf;
    io_conf.pin_bit_mask = 1ULL << RCV1_EXT;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(RCV1_EXT, myHandleInterrupt, NULL);

    playerStart();

    while(true) {
        printAc();
    }
}


//TODO: 'Move' this to ac_recv, tweak timings for more reliability??, print timings..,
//check at least 2/ like doorbell. (+ timeout/last????) Reset last on..??? change MAX/CODE_BIT_PULSES?
// USE ESP TIMERS??
// Check in/after  hasAcReceived, if it's all valid tri's!! And if address bits are correct?!
//   AGoal: no weird ones between two identical, which would prevent compare.
//    Use the longer buffer we have now ????
// boolean isBit/isSyncBit (isTri??) < array with 'pattern' of this?!
// Document: shouting on my 'superhetrodyne' cause high pulses to be longer.
// Could the signal getting to the pin be too low, or otherwise bad?
// short pulses do seem one of the issues

bool isCorrectCode(uint8_t* result) {
  return result[0] == TRI_0 && // A0 = GND
         ( result[1] == TRI_0 || result[1] == TRI_1 ) && // A1 = pin1 (1=UP)
         ( result[2] == TRI_0 || result[2] == TRI_1 ) && // A2 = pin2 (1=UP)
         ( result[3] == TRI_0 || result[3] == TRI_1 ) && // A3 = pin3 (1=UP)
         result[4] == TRI_0 && // A4 = GND
         result[5] == TRI_0 && // A5 = GND
         result[6] == TRI_0 && // A6 = GND
         result[7] == TRI_0 && // A7 = GND
         ( result[8] == TRI_0 || result[8] == TRI_1 ) && // D0 = switch
         result[9] == TRI_0 && // D1 = GND
         result[10] == TRI_0 && // D2 = GND
         result[11] == TRI_0 ;// D3 = GND
}

void printAc() {

    static uint8_t triggered[TRIS] = {};
    static uint8_t last[TRIS] = {};

    struct BitData *bitData = rcReceiver.getReceivedBits();
    uint8_t *result = rcReceiver.getTris(bitData);

    bool correctCode = isCorrectCode(result);

    rcReceiver.printDetails(bitData);
    if (!correctCode) {
        rcReceiver.printAcTimings(bitData);
    }
    rcReceiver.printAcTriState(result);
    if (!correctCode) {
        return;
    }

    bool sameAsTriggered = true;
    bool sameAsLast = true;
    for (int pos = 0; pos < TRIS; pos++) {
        if (result[pos] != last[pos]) {
            sameAsLast = false;
        }
        if (result[pos] != triggered[pos]) {
            sameAsTriggered = false;
        }
        last[pos] = result[pos];
    }
    if (sameAsLast) {
        bool stateOn = result[8] == TRI_1;

        uint8_t address = 0;
        address += result[1] == TRI_1 ? 0 : 1;
        address += result[2] == TRI_1 ? 0 : 2;
        address += result[3] == TRI_1 ? 0 : 4;

        static char topic2[50];
        static char msg2[50];
        
        snprintf (topic2, 50, "devices/receiver/sw%d/pulse_%s", address + 1, stateOn ? "on" : "off" );
        snprintf (msg2, 50, "%s", "true" );
        int msg_id = esp_mqtt_client_publish(mqttClient, topic2, msg2, 0, 0, 0);
    }

    if (sameAsLast && !sameAsTriggered) {
        for (int pos = 0; pos < TRIS; pos++) {
            triggered[pos] = result[pos];
        }

        bool stateOn = result[8] == TRI_1;

        uint8_t address = 0;
        address += result[1] == TRI_1 ? 0 : 1;
        address += result[2] == TRI_1 ? 0 : 2;
        address += result[3] == TRI_1 ? 0 : 4;

        blinkerExt1.setPattern(ledPatternRc, address + 1);


        printf("Switch %d: %s\n", address + 1, stateOn ? "ON" : "OFF");

        if(stateOn) {
            strand->pixels[address] = pixelFromRGB(0, 0, 40);
        } else {
            strand->pixels[address] = pixelFromRGB(0, 0, 0);
        }
        digitalLeds_updatePixels(strand);


        static char topic[50];
        static char msg[50];
        snprintf (topic, 50, "devices/receiver/sw%d/on", address + 1);
        snprintf (msg, 50, "%s", stateOn ? "true" : "false"  );
        int msg_id = esp_mqtt_client_publish(mqttClient, topic, msg, 0, 0, 0);
    }
}
