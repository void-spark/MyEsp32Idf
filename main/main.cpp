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
#include "RcReceiver.h"
#include "doorbell_recv.h"
#include "sd_doorbell.h"
#include "my_mqtt.h"
#include "my_display.h"

#define BTN_BOOT GPIO_NUM_0
#define LED_BUILTIN GPIO_NUM_2
#define LED1_EXT GPIO_NUM_27
#define LED2_EXT GPIO_NUM_32

#define WS2812_1 GPIO_NUM_26
#define NUM_PIXELS 8

#define RCV1_EXT GPIO_NUM_33

#define RC_BITS 25
#define TRIS (RC_BITS - 1) / 2

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


RcReceiver rcReceiver(RC_BITS, 12);

strand_t strands[] = { {.rmtChannel = 0, .gpioNum = WS2812_1, .ledType = LED_WS2812B_V3, .numPixels =  NUM_PIXELS,
   .pixels = nullptr, ._stateVars = nullptr} };

strand_t * strand = &strands[0];

void printAc();
void printDoorbell();

IRAM_ATTR void myHandleInterrupt(void *) {
  const unsigned long time = esp_timer_get_time();
  //int state = digitalRead(RCV1_EXT);
  rcReceiver.handleInterrupt(time);
  handleDoorbell(time);
  //digitalWrite(TMP_OUT, state);
}

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
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
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

    while (true) {
        printAc();
        printf("rc: %d bytes\n", uxTaskGetStackHighWaterMark(NULL));
    }
}

static void taskDrb(void *pvParameters) {
    printf("drb start: %d bytes\n", uxTaskGetStackHighWaterMark(NULL));
    while (true) {
        printDoorbell();
        printf("drb: %d bytes\n", uxTaskGetStackHighWaterMark(NULL));
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

    mqttStart(subscribeTopics, handleMessage);

    ESP_LOGI(TAG, "Waiting for MQTT");
    updateHeader("Wait: MQTT");

    mqttWait();

    blinkerInt.setPattern(ledPatternConnected);

    setupDoorbell();

    gpio_config_t io_conf;
    io_conf.pin_bit_mask = 1ULL << RCV1_EXT;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(RCV1_EXT, myHandleInterrupt, NULL);

    sdDoorbellStart();

    // TimerHandle_t timer = xTimerCreate("infoTimer", pdMS_TO_TICKS(2000), pdTRUE, NULL, infoCallBack );
    // xTimerStart(timer, 0);
    
    if (xTaskCreatePinnedToCore(taskRc, "taskRc", configMINIMAL_STACK_SIZE + 2000, NULL, configMAX_PRIORITIES - 5, NULL, 1)!=pdPASS) {
        printf("ERROR creating taskRc! Out of memory?\n");
    };

    if (xTaskCreatePinnedToCore(taskDrb, "taskDrb", configMINIMAL_STACK_SIZE + 2000, NULL, configMAX_PRIORITIES - 5, NULL, 1)!=pdPASS) {
        printf("ERROR creating taskDrb! Out of memory?\n");
    };

    gpio_pad_select_gpio(BTN_BOOT);
    gpio_set_direction(BTN_BOOT, GPIO_MODE_INPUT);
    buttonTimer = xTimerCreate("ButtonTimer", (5 / portTICK_PERIOD_MS), pdTRUE, (void *) 0, buttonTimerCallback);

    xTimerStart(buttonTimer, 0);

    vTaskDelete(NULL);
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
        mqttPublish(topic2, msg2, 0, 0, 0);
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
            // if(address == 0) {
            //     go();
            // }
            strand->pixels[address] = pixelFromRGB(0, 0, 40);
        } else {
            strand->pixels[address] = pixelFromRGB(0, 0, 0);
        }
        digitalLeds_updatePixels(strand);


        static char topic[50];
        static char msg[50];
        snprintf (topic, 50, "devices/receiver/sw%d/on", address + 1);
        snprintf (msg, 50, "%s", stateOn ? "true" : "false"  );
        mqttPublish(topic, msg, 0, 0, 0);
    }
}


void printDoorbell() {
  static uint8_t triggered[DRB_SEQ_LENGTH] = {};
  static uint8_t last[DRB_SEQ_LENGTH] = {};

  uint8_t* result = getDrbReceivedPulses();

  bool sameAsTriggered = true;
  bool sameAsLast = true;
  for (int pos = 0; pos < DRB_SEQ_LENGTH; pos++) {
    if (result[pos] != last[pos]) {
      sameAsLast = false;
    }
    if (result[pos] != triggered[pos]) {
      sameAsTriggered = false;
    }
    last[pos] = result[pos];
  }
  if (sameAsLast && !sameAsTriggered ) {
    for (int pos = 0; pos < DRB_SEQ_LENGTH; pos++) {
      triggered[pos] = result[pos];
    }
    blinkerExt2.setPattern(ledPatternDrb, 10);
    printDoorbellWord(result, DRB_SEQ_LENGTH);

    //startMarch(13);
    // startSimple(13);

    static char topic[50];
    static char msg[50];
    snprintf (topic, 50, "devices/receiver/doorbell/pushed");
    snprintf (msg, 50, "%s", "true" );

    mqttPublish(topic, msg, 0, 0, 0);

    sdDoorbellGo();
  }
}
