#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_spi_flash.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp32_digital_led_lib.h"
#include "lwip/apps/sntp.h"
#include "LedBlink2.h"
#include "sd_doorbell.h"
#include "my_mqtt.h"
#include "my_display.h"

#define BTN_BOOT GPIO_NUM_0
#define LED_BUILTIN GPIO_NUM_2
#define LED1_EXT GPIO_NUM_27
#define LED2_EXT GPIO_NUM_32

#define WS2812_1 GPIO_NUM_26
#define NUM_PIXELS 8

// WiFi credentials.
#define WIFI_SSID "Milkrun"
#define WIFI_PASS "55382636751623425906"

/* FreeRTOS event group to signal app status changes*/
static EventGroupHandle_t app_event_group;

const int WIFI_CONNECTED_BIT = BIT0;

static const char *TAG = "MyEsp32";

long ledPatternPendingWiFi[] = {100,100};
long ledPatternPendingMqtt[] = {100,300};
long ledPatternConnected[] = {5,2995};

long ledPatternRc[] = {150,150};

long ledPatternDrb[] = {500, 500};

LedBlink2 blinkerInt(LED_BUILTIN);
LedBlink2 blinkerExt1(LED1_EXT);
LedBlink2 blinkerExt2(LED2_EXT);


strand_t strands[] = { {.rmtChannel = 0, .gpioNum = WS2812_1, .ledType = LED_WS2812B_V3, .numPixels =  NUM_PIXELS,
   .pixels = nullptr, ._stateVars = nullptr} };

strand_t * strand = &strands[0];

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "retry to connect to the AP");
        xEventGroupClearBits(app_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        {
            char ip[16] = "";
            snprintf(ip, 16, IPSTR, IP2STR(&event->ip_info.ip));
            updateIp(ip);
        }
        xEventGroupSetBits(app_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta() {
    app_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, WIFI_PASS);
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    ESP_LOGI(TAG, "connect to ap SSID:%s password:%s", WIFI_SSID, WIFI_PASS);
}

extern "C" {
    static void infoCallBack(TimerHandle_t xTimer) {
        uint32_t nowFree  = esp_get_free_heap_size();        
        uint32_t minimumFree = esp_get_minimum_free_heap_size();
        printf("Free: %d (%dk), min: %d(%dk)\n", nowFree, nowFree/1024, minimumFree, minimumFree/1024);
    }
}

static void taskRc(void *pvParameters) {
    printf("rc start: %d bytes\n", uxTaskGetStackHighWaterMark(NULL));

    if (digitalLeds_initStrands(strands, 1)) {
        printf("Failed to initialize ws2812\n");
        while (true) {};
    }
    digitalLeds_resetPixels(strand);

    for (int i = 0; i < strand->numPixels; i++) {
        if(i % 3 == 0) {
            strand->pixels[i] = pixelFromRGB(1, 0, 0);
        } else if(i % 3 == 1) {
            strand->pixels[i] = pixelFromRGB(0, 1, 0);
        } else {
            strand->pixels[i] = pixelFromRGB(0, 0, 1);
        }
    }
    digitalLeds_updatePixels(strand);

    vTaskDelete(NULL);
}

static TimerHandle_t buttonTimer;

static void buttonTimerCallback(TimerHandle_t xTimer) { 
    int level = gpio_get_level(BTN_BOOT);

    // https://www.embedded.com/electronics-blogs/break-points/4024981/My-favorite-software-debouncers
    static uint16_t state = 0; // Current debounce status
    state=(state<<1) | !level | 0xe000;
    if(state==0xf000) {
        nextScreen();
    }
}

static void subscribeTopics() {
    // TODO: Send node/properties details, rename to setup or so?
    subscribeDevTopic("display/header/set");

    subscribeTopic("devices/receiver/doorbell/pushed");
}

static bool handleAnyMessage(const char* topic, const char* data) {

    if(strcmp(topic,"devices/receiver/doorbell/pushed") == 0) {
        blinkerExt2.setPattern(ledPatternDrb, 10);

        //startMarch(13);
        // startSimple(13);

        sdDoorbellGo();

        return true;
    }

    return false;
}

static void handleMessage(const char* topic1, const char* topic2, const char* topic3, const char* data) {

    if(strcmp(topic1, "display") == 0 && strcmp(topic2, "header") == 0 && strcmp(topic3, "set") == 0) {
        updateHeader(data);
        publishNodeProp("display", "header", data);
    }
}

extern "C" void app_main() {

    gpio_pad_select_gpio(WS2812_1);
    gpio_set_direction(WS2812_1, GPIO_MODE_OUTPUT);
    gpio_set_level(WS2812_1, 0);

    sdDoorbellSetup();

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

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    myInitDisplay();

    // Initialize WiFi
    wifi_init_sta();

    blinkerInt.setPattern(ledPatternPendingWiFi);
    
    ESP_LOGI(TAG, "Waiting for wifi");
    updateHeader("Wait: WiFi");
    xEventGroupWaitBits(app_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);

    blinkerInt.setPattern(ledPatternPendingMqtt);

    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();

    mqttStart(subscribeTopics, handleMessage, handleAnyMessage);

    ESP_LOGI(TAG, "Waiting for MQTT");
    updateHeader("Wait: MQTT");

    mqttWait();

    blinkerInt.setPattern(ledPatternConnected);

    sdDoorbellStart();

    // TimerHandle_t timer = xTimerCreate("infoTimer", pdMS_TO_TICKS(2000), pdTRUE, NULL, infoCallBack );
    // xTimerStart(timer, 0);

   if (xTaskCreatePinnedToCore(taskRc, "taskRc", configMINIMAL_STACK_SIZE + 2000, NULL, configMAX_PRIORITIES - 5, NULL, 1)!=pdPASS) {
        printf("ERROR creating taskRc! Out of memory?\n");
    };

    gpio_pad_select_gpio(BTN_BOOT);
    gpio_set_direction(BTN_BOOT, GPIO_MODE_INPUT);
    buttonTimer = xTimerCreate("ButtonTimer", (5 / portTICK_PERIOD_MS), pdTRUE, (void *) 0, buttonTimerCallback);

    xTimerStart(buttonTimer, 0);

    vTaskDelete(NULL);
}
