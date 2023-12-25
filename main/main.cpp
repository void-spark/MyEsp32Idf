#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_https_ota.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_spi_flash.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/apps/sntp.h"
#include "LedBlink2.h"
#include "wifi_helper.h"
#include "mqtt_helper.h"
#include "sd_doorbell.h"
#include "my_display.h"

#define BTN_BOOT GPIO_NUM_0
#define LED_BUILTIN GPIO_NUM_2
#define LED1_EXT GPIO_NUM_27
#define LED2_EXT GPIO_NUM_32

// WiFi credentials.
#define WIFI_SSID "Milkrun"
#define WIFI_PASS "55382636751623425906"

static const char *TAG = "MyEsp32";

long ledPatternPendingWiFi[] = {100,100};
long ledPatternPendingMqtt[] = {100,300};
long ledPatternConnected[] = {5,2995};

long ledPatternRc[] = {150,150};

long ledPatternDrb[] = {500, 500};

LedBlink2 blinkerInt(LED_BUILTIN);
LedBlink2 blinkerExt1(LED1_EXT);
LedBlink2 blinkerExt2(LED2_EXT);

extern "C" {
    static void infoCallBack(TimerHandle_t xTimer) {
        uint32_t nowFree  = esp_get_free_heap_size();        
        uint32_t minimumFree = esp_get_minimum_free_heap_size();
        printf("Free: %lu (%luk), min: %lu(%luk)\n", nowFree, nowFree/1024, minimumFree, minimumFree/1024);
    }
}

static const char* ota_url = "http://raspberrypi.fritz.box:8032/esp32/MyEsp32Idf.bin";

static void ota_task(void * pvParameter) {
    ESP_LOGI(TAG, "Starting OTA update...");

    esp_http_client_config_t config = {};
    config.url = ota_url;

    esp_https_ota_config_t ota_config = {};
    ota_config.http_config = &config;

    ESP_LOGI(TAG, "Attempting to download update from %s", config.url);
    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA Succeed, Rebooting...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Firmware Upgrades Failed");
    }
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
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
    subscribeDevTopic("$update");

    subscribeTopic("devices/receiver/doorbell/pushed");
    subscribeTopic("devices/receiver/doorbell/reset");
}

static bool handleAnyMessage(const char* topic, const char* data) {

    if(strcmp(topic,"devices/receiver/doorbell/pushed") == 0) {
        blinkerExt2.setPattern(ledPatternDrb, 10);

        //startMarch(13);
        // startSimple(13);

        sdDoorbellRing();

        return true;
    }

    if(strcmp(topic,"devices/receiver/doorbell/reset") == 0) {
        sdDoorbellQuiet();

        return true;
    }

    return false;
}

static void handleMessage(const char* topic1, const char* topic2, const char* topic3, const char* data) {

    if(
        strcmp(topic1, "$update") == 0 && 
        topic2 == NULL && 
        topic3 == NULL
    ) {
        xTaskCreate(&ota_task, "ota_task", 8192, NULL, 5, NULL);
    }

    if(strcmp(topic1, "display") == 0 && strcmp(topic2, "header") == 0 && strcmp(topic3, "set") == 0) {
        updateHeader(data);
        publishNodeProp("display", "header", data);
    }
}

extern "C" void app_main() {
    sdDoorbellSetup();

    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
        return;
    }

    printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
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
    wifiStart();

    blinkerInt.setPattern(ledPatternPendingWiFi);
    
    ESP_LOGI(TAG, "Waiting for wifi");
    updateHeader("Wait: WiFi");
    wifiWait();

    esp_netif_ip_info_t ipInfo = {}; 
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");    
    esp_netif_get_ip_info(netif, &ipInfo);
	char ipValue[16];
    snprintf(ipValue, sizeof(ipValue), IPSTR, IP2STR(&ipInfo.ip));
    updateIp(ipValue);

    blinkerInt.setPattern(ledPatternPendingMqtt);

    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();

    mqttStart("Desk doorbell receiver", subscribeTopics, handleMessage, handleAnyMessage);

    ESP_LOGI(TAG, "Waiting for MQTT");
    updateHeader("Wait: MQTT");

    mqttWait();

    blinkerInt.setPattern(ledPatternConnected);

    sdDoorbellStart();

    // TimerHandle_t timer = xTimerCreate("infoTimer", pdMS_TO_TICKS(2000), pdTRUE, NULL, infoCallBack );
    // xTimerStart(timer, 0);

	ESP_ERROR_CHECK(gpio_reset_pin(BTN_BOOT));
	ESP_ERROR_CHECK(gpio_set_pull_mode(BTN_BOOT, GPIO_FLOATING));
	ESP_ERROR_CHECK(gpio_set_direction(BTN_BOOT, GPIO_MODE_INPUT));

    buttonTimer = xTimerCreate("ButtonTimer", (5 / portTICK_PERIOD_MS), pdTRUE, (void *) 0, buttonTimerCallback);

    xTimerStart(buttonTimer, 0);

    vTaskDelete(NULL);
}
