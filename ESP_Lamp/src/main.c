#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_http_server.h"
#include "led_strip.h"
#include "mqtt_client.h"

#define BOX_WIFI_SSID      "ESP32_Setup_Device"
#define BOX_WIFI_PASS      "12345678"
#define LED_GPIO_NUM       8
#define LED_COUNT          1
#define MQTT_BROKER_URL    "mqtt://broker.hivemq.com"

static const char *TAG = "SMART_LAMP";
static led_strip_handle_t led_strip;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static httpd_handle_t server = NULL;
static char device_topic[64] = "secrethashlamp8126371726369213697/default/power";

// Режимы индикации статуса через встроенный светодиод
typedef enum {
    LED_MODE_SLOW_BLINK,   // Нет WiFi      — синий, 1 Гц
    LED_MODE_FAST_BLINK,   // Нет MQTT      — красный, 5 Гц
    LED_MODE_DOUBLE_BLINK  // Всё подключено — зелёный, двойная вспышка
} led_mode_t;

static led_mode_t current_led_mode = LED_MODE_SLOW_BLINK;
static bool lamp_state = false;

// --- NVS ---

void save_wifi_credentials(const char* ssid, const char* pass) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_str(my_handle, "wifi_ssid", ssid);
        nvs_set_str(my_handle, "wifi_pass", pass);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Credentials saved to NVS");
    }
}

bool load_wifi_credentials(char* ssid, char* pass) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READONLY, &my_handle) != ESP_OK) return false;
    size_t s_len = 32, p_len = 64;
    esp_err_t res = nvs_get_str(my_handle, "wifi_ssid", ssid, &s_len);
    res |= nvs_get_str(my_handle, "wifi_pass", pass, &p_len);
    nvs_close(my_handle);
    return (res == ESP_OK);
}

// --- LED ---

void set_led_color(uint32_t r, uint32_t g, uint32_t b) {
    if (led_strip) {
        led_strip_set_pixel(led_strip, 0, r, g, b);
        led_strip_refresh(led_strip);
    }
}

void led_task(void *pv) {
    while (1) {
        if (lamp_state) {
            // Лампа включена по MQTT — белый свет
            set_led_color(255, 255, 255);
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            // Лампа выключена — индикация статуса соединения
            switch (current_led_mode) {
                case LED_MODE_SLOW_BLINK:
                    set_led_color(0, 0, 255); vTaskDelay(pdMS_TO_TICKS(1000));
                    led_strip_clear(led_strip); vTaskDelay(pdMS_TO_TICKS(1000));
                    break;
                case LED_MODE_FAST_BLINK:
                    set_led_color(255, 0, 0); vTaskDelay(pdMS_TO_TICKS(200));
                    led_strip_clear(led_strip); vTaskDelay(pdMS_TO_TICKS(200));
                    break;
                case LED_MODE_DOUBLE_BLINK:
                    set_led_color(0, 255, 0); vTaskDelay(pdMS_TO_TICKS(150));
                    led_strip_clear(led_strip); vTaskDelay(pdMS_TO_TICKS(150));
                    set_led_color(0, 255, 0); vTaskDelay(pdMS_TO_TICKS(150));
                    led_strip_clear(led_strip); vTaskDelay(pdMS_TO_TICKS(1500));
                    break;
            }
        }
    }
}

// --- MQTT ---

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            current_led_mode = LED_MODE_DOUBLE_BLINK;
            esp_mqtt_client_subscribe(mqtt_client, device_topic, 0);
            ESP_LOGI(TAG, "MQTT Connected. Topic: %s", device_topic);
            break;
        case MQTT_EVENT_DISCONNECTED:
            current_led_mode = LED_MODE_FAST_BLINK;
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Data received: %.*s", event->data_len, event->data);
            if (strncmp(event->data, "ON", event->data_len) == 0)       lamp_state = true;
            else if (strncmp(event->data, "OFF", event->data_len) == 0) lamp_state = false;
            break;
        default: break;
    }
}

// --- Web Server ---

// URL-декодирование строки запроса (замена %XX и '+')
void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a' - 'A';
            if (a >= 'A') a -= ('A' - 10); else a -= '0';
            if (b >= 'a') b -= 'a' - 'A';
            if (b >= 'A') b -= ('A' - 10); else b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' '; src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// Главная страница с формой ввода WiFi-данных
esp_err_t get_handler(httpd_req_t *req) {
    const char* html = "<html><meta charset='UTF-8'><body><h1>Setup WiFi</h1>"
                       "<form action='/config' method='GET'>SSID: <input name='s'><br>"
                       "Pass: <input name='p' type='password'><br><input type='submit' value='Connect'></form></body></html>";
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

// Сохранение новых WiFi-credentials и немедленное переподключение
esp_err_t wifi_cred_handler(httpd_req_t *req) {
    char buf[256], s_raw[32], p_raw[64], s_dec[32], p_dec[64];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        if (httpd_query_key_value(buf, "s", s_raw, sizeof(s_raw)) == ESP_OK &&
            httpd_query_key_value(buf, "p", p_raw, sizeof(p_raw)) == ESP_OK) {
            url_decode(s_dec, s_raw); url_decode(p_dec, p_raw);
            save_wifi_credentials(s_dec, p_dec);
            httpd_resp_send(req, "Credentials Saved. Connecting...", HTTPD_RESP_USE_STRLEN);

            wifi_config_t sta_config = {0};
            strncpy((char*)sta_config.sta.ssid, s_dec, sizeof(sta_config.sta.ssid));
            strncpy((char*)sta_config.sta.password, p_dec, sizeof(sta_config.sta.password));
            esp_wifi_set_config(WIFI_IF_STA, &sta_config);
            esp_wifi_connect();
        }
    }
    return ESP_OK;
}

// --- WiFi Events ---

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        current_led_mode = LED_MODE_FAST_BLINK;

        // После успешного подключения отключаем AP для экономии ресурсов
        esp_wifi_set_mode(WIFI_MODE_STA);

        const esp_mqtt_client_config_t mqtt_cfg = { .broker.address.uri = MQTT_BROKER_URL };
        if (mqtt_client) esp_mqtt_client_stop(mqtt_client);
        mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
        esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
        esp_mqtt_client_start(mqtt_client);

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        current_led_mode = LED_MODE_SLOW_BLINK;

        // При потере связи возвращаем APSTA, чтобы устройство оставалось настраиваемым
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        esp_wifi_connect();
    }
}

void start_webserver() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    static const httpd_uri_t uri_get    = { .uri = "/",       .method = HTTP_GET, .handler = get_handler      };
    static const httpd_uri_t uri_config = { .uri = "/config", .method = HTTP_GET, .handler = wifi_cred_handler };
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &uri_config);
    }
}

// --- Entry Point ---

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase(); nvs_flash_init();
    }

    // Инициализация WS2812 через RMT
    led_strip_config_t strip_config = { .strip_gpio_num = LED_GPIO_NUM, .max_leds = LED_COUNT, .led_model = LED_MODEL_WS2812 };
    led_strip_rmt_config_t rmt_config = { .clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = 10 * 1000 * 1000 };
    led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    xTaskCreate(led_task, "led_task", 2048, NULL, 5, NULL);

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,    &event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT,   IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL);

    // Стартуем в режиме APSTA: AP для настройки, STA для подключения к роутеру
    wifi_config_t ap_config = { .ap = { .ssid = BOX_WIFI_SSID, .password = BOX_WIFI_PASS,
                                        .authmode = WIFI_AUTH_WPA_WPA2_PSK, .max_connection = 4 } };
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);

    // Если есть сохранённые credentials — подключаемся автоматически
    char s[32], p[64];
    if (load_wifi_credentials(s, p)) {
        wifi_config_t sta_config = {0};
        strncpy((char*)sta_config.sta.ssid, s, 32);
        strncpy((char*)sta_config.sta.password, p, 64);
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    }

    esp_wifi_start();
    esp_wifi_connect();
    start_webserver();
}