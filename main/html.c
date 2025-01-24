/*
 * html.c
 *
 *  Created on: 14-Jan-2025
 *      Author: ilang
 */




#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_http_server.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_wifi.h"
#include "esp_netif.h"

// GPIO and Button Configuration
#define BUTTON_GPIO GPIO_NUM_33 // Adjust to your button GPIO
#define LED_GPIO 2    // Optional LED for hotspot indicator

// I2C and RTC Configuration
#define I2C_MASTER_SCL_IO 22
#define I2C_MASTER_SDA_IO 21
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000
#define DS3231_ADDRESS 0x68
#define I2C_MASTER_TIMEOUT_MS 1000

// Schedule Management
#define MAX_SCHEDULES 10

typedef struct {
    int start_hour, start_min;
    int end_hour, end_min;
    char days[20]; // Days of the week, e.g., "Mon, Tue"
    int pin;       // GPIO pin
    bool enabled;  // Whether the schedule is active
} Schedule;

Schedule schedules[MAX_SCHEDULES];
int schedule_count = 0;

// HTML Page as a C String
const char *html_page =
    "<!DOCTYPE html>"
    "<html>"
    "<head><title>ESP32 Timer Setup</title></head>"
    "<body>"
    "<h1>ESP32 Timer Setup</h1>"
    "<form action=\"/set_time\" method=\"POST\">"
    "  <label>Time (HH:MM:SS):</label>"
    "  <input type=\"text\" name=\"time\" placeholder=\"HH:MM:SS\"><br>"
    "  <label>Date (DD/MM/YYYY):</label>"
    "  <input type=\"text\" name=\"date\" placeholder=\"DD/MM/YYYY\"><br>"
    "  <button type=\"submit\">Set Time</button>"
    "</form>"
    "<h2>Schedules</h2>"
    "<form action=\"/set_schedule\" method=\"POST\">"
    "  <label>GPIO Pin:</label>"
    "  <input type=\"text\" name=\"pin\" placeholder=\"e.g., 2\"><br>"
    "  <label>Days:</label>"
    "  <input type=\"text\" name=\"days\" placeholder=\"e.g., Mon, Tue\"><br>"
    "  <label>Start Time (HH:MM):</label>"
    "  <input type=\"text\" name=\"start_time\" placeholder=\"HH:MM\"><br>"
    "  <label>End Time (HH:MM):</label>"
    "  <input type=\"text\" name=\"end_time\" placeholder=\"HH:MM\"><br>"
    "  <button type=\"submit\">Add Schedule</button>"
    "</form>"
    "<table border=\"1\">"
    "  <thead><tr><th>ID</th><th>Pin</th><th>Days</th><th>Start</th><th>End</th><th>Actions</th></tr></thead>"
    "  <tbody id=\"schedule-table\"></tbody>"
    "</table>"
    "<script>"
    "fetch('/get_schedules')"
    ".then(response => response.json())"
    ".then(data => {"
    "  let table = document.getElementById('schedule-table');"
    "  table.innerHTML = '';"
    "  data.schedules.forEach(s => {"
    "    table.innerHTML += `<tr>"
    "      <td>${s.id}</td><td>${s.pin}</td><td>${s.days}</td>"
    "      <td>${s.start_time}</td><td>${s.end_time}</td>"
    "      <td><button onclick=\"deleteSchedule(${s.id})\">Delete</button></td>"
    "    </tr>`;"
    "  });"
    "});"
    "</script>"
    "</body>"
    "</html>";

// RTC Functions
void rtc_get_time(struct tm *timeinfo) {
    uint8_t data[7];
    i2c_master_write_read_device(I2C_MASTER_NUM, DS3231_ADDRESS, NULL, 0, data, 7, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    timeinfo->tm_sec = data[0] & 0x7F;
    timeinfo->tm_min = data[1] & 0x7F;
    timeinfo->tm_hour = data[2] & 0x3F;
    timeinfo->tm_mday = data[4] & 0x3F;
    timeinfo->tm_mon = (data[5] & 0x1F) - 1;
    timeinfo->tm_year = (data[6] & 0xFF) + 100;
}

void rtc_set_time(struct tm *timeinfo) {
    uint8_t data[7];
    data[0] = timeinfo->tm_sec;
    data[1] = timeinfo->tm_min;
    data[2] = timeinfo->tm_hour;
    data[3] = 0;
    data[4] = timeinfo->tm_mday;
    data[5] = timeinfo->tm_mon + 1;
    data[6] = timeinfo->tm_year - 100;
    i2c_master_write_to_device(I2C_MASTER_NUM, DS3231_ADDRESS, data, sizeof(data), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

// HTTP Handlers
esp_err_t html_handler(httpd_req_t *req) {
    httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t get_schedules_handler(httpd_req_t *req) {
    cJSON *response = cJSON_CreateObject();
    cJSON *array = cJSON_CreateArray();
    for (int i = 0; i < schedule_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", i);
        cJSON_AddNumberToObject(item, "pin", schedules[i].pin);
        cJSON_AddStringToObject(item, "days", schedules[i].days);
        cJSON_AddStringToObject(item, "start_time", "10:00");
        cJSON_AddStringToObject(item, "end_time", "12:00");
        cJSON_AddItemToArray(array, item);
    }
    cJSON_AddItemToObject(response, "schedules", array);
    char *json = cJSON_Print(response);
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_Delete(response);
    free(json);
    return ESP_OK;
}

esp_err_t set_schedule_handler(httpd_req_t *req) {
    char buf[200];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    buf[ret] = '\0';
    if (schedule_count >= MAX_SCHEDULES) {
        httpd_resp_send(req, "Schedule limit reached!", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    Schedule new_schedule;
    sscanf(buf, "pin=%d&days=%[^&]&start_time=%d:%d&end_time=%d:%d",
           &new_schedule.pin, new_schedule.days,
           &new_schedule.start_hour, &new_schedule.start_min,
           &new_schedule.end_hour, &new_schedule.end_min);
    schedules[schedule_count++] = new_schedule;
    httpd_resp_send(req, "Schedule added successfully!", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t delete_schedule_handler(httpd_req_t *req) {
    char buf[50];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    buf[ret] = '\0';
    int id;
    sscanf(buf, "id=%d", &id);
    if (id < 0 || id >= schedule_count) {
        httpd_resp_send(req, "Invalid schedule ID!", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    for (int i = id; i < schedule_count - 1; i++) {
        schedules[i] = schedules[i + 1];
    }
    schedule_count--;
    httpd_resp_send(req, "Schedule deleted successfully!", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t set_time_handler(httpd_req_t *req) {
    char buf[100];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    buf[ret] = '\0';
    struct tm timeinfo = {0};
    sscanf(buf, "time=%d:%d:%d&date=%d/%d/%d",
           &timeinfo.tm_hour, &timeinfo.tm_min, &timeinfo.tm_sec,
           &timeinfo.tm_mday, &timeinfo.tm_mon, &timeinfo.tm_year);
    timeinfo.tm_mon -= 1;
    timeinfo.tm_year -= 1900;
    rtc_set_time(&timeinfo);
    httpd_resp_send(req, "Time updated successfully!", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void register_endpoints(httpd_handle_t server) {
    httpd_uri_t html_uri = { .uri = "/", .method = HTTP_GET, .handler = html_handler, .user_ctx = NULL };
    httpd_uri_t get_schedules_uri = { .uri = "/get_schedules", .method = HTTP_GET, .handler = get_schedules_handler, .user_ctx = NULL };
    httpd_uri_t set_schedule_uri = { .uri = "/set_schedule", .method = HTTP_POST, .handler = set_schedule_handler, .user_ctx = NULL };
    httpd_uri_t delete_schedule_uri = { .uri = "/delete_schedule", .method = HTTP_POST, .handler = delete_schedule_handler, .user_ctx = NULL };
    httpd_uri_t set_time_uri = { .uri = "/set_time", .method = HTTP_POST, .handler = set_time_handler, .user_ctx = NULL };

    httpd_register_uri_handler(server, &html_uri);
    httpd_register_uri_handler(server, &get_schedules_uri);
    httpd_register_uri_handler(server, &set_schedule_uri);
    httpd_register_uri_handler(server, &delete_schedule_uri);
    httpd_register_uri_handler(server, &set_time_uri);
}



void start_wifi_hotspot() {
    // Initialize TCP/IP stack
    esp_netif_init();

    // Create default Wi-Fi AP configuration
    esp_netif_create_default_wifi_ap();

    // Initialize the Wi-Fi driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    // Set Wi-Fi mode to AP (Access Point)
    esp_wifi_set_mode(WIFI_MODE_AP);

    // Configure the Access Point
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "ESP32_Timer",              // Wi-Fi SSID
            .ssid_len = 0,
            .password = "password123",         // Wi-Fi Password
            .max_connection = 4,              // Max clients
            .authmode = WIFI_AUTH_WPA_WPA2_PSK // Security
        }
    };

    // If no password is provided, set the AP to open
    if (strlen((char *)ap_config.ap.password) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    esp_wifi_set_config(WIFI_IF_AP, &ap_config);

    // Start Wi-Fi
    esp_wifi_start();

    printf("Hotspot started! SSID: %s, Password: %s\n", ap_config.ap.ssid, ap_config.ap.password);
}


void button_task(void *arg) {
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_pullup_en(BUTTON_GPIO);

    bool hotspot_started = false;

    while (1) {
        // Detect button press (low level)
        if (!gpio_get_level(BUTTON_GPIO)) {
            if (!hotspot_started) {
                printf("Button pressed! Starting hotspot...\n");
                start_wifi_hotspot();
                hotspot_started = true;
            }
        } else {
            hotspot_started = false; // Reset when button is released
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}



