/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "driver/i2c.h"
#include <stdio.h>
#include <time.h>
#include "esp_http_server.h"
#include "ds3231.h"
#include "ui_scheduler.h"
#include "esp_wifi.h"


#define CHECK_ARG(ARG) do { if (!(ARG)) return ESP_ERR_INVALID_ARG; } while (0)
 i2c_dev_t dev;
 i2c_dev_t eeprom;
 
#define I2C_MASTER_SCL_IO 22        // Set GPIO for SCL
#define I2C_MASTER_SDA_IO 21        // Set GPIO for SDA
#define I2C_MASTER_NUM I2C_NUM_0    // I2C port number
#define I2C_MASTER_FREQ_HZ 100000   // Frequency
#define I2C_MASTER_TIMEOUT_MS 1000  // Timeout

#define DS3231_ADDRESS 0x68 // RTC I2C Address

#define EEPROM_I2C_ADDRESS 0x57 // Common I2C address for EEPROM chips
#define I2C_FREQ_HZ 400000

esp_err_t EEPROM_init_desc(i2c_dev_t *dev, i2c_port_t port, gpio_num_t sda_gpio, gpio_num_t scl_gpio)
{
    CHECK_ARG(dev);

    dev->port = port;
    dev->addr = EEPROM_I2C_ADDRESS;
    dev->cfg.sda_io_num = sda_gpio;
    dev->cfg.scl_io_num = scl_gpio;
#if HELPER_TARGET_IS_ESP32
    dev->cfg.master.clk_speed = I2C_FREQ_HZ;
#endif
    return i2c_dev_create_mutex(dev);
}





void app_main() {
   // i2c_master_init();
  ESP_ERROR_CHECK(i2cdev_init());

   
    memset(&dev, 0, sizeof(i2c_dev_t));
    memset(&eeprom, 0, sizeof(i2c_dev_t));

    ESP_ERROR_CHECK(ds3231_init_desc(&dev, 0, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO));
    ESP_ERROR_CHECK(EEPROM_init_desc(&eeprom, 0, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO));
/*
    // setup datetime: 2016-10-09 13:50:10
    struct tm time = {
        .tm_year = 125, //since 1900 (2016 - 1900)
        .tm_mon  = 0,  // 0-based
        .tm_mday = 16,
        .tm_hour = 23,
        .tm_min  = 59,
        .tm_sec  = 00
    };
    ESP_ERROR_CHECK(ds3231_set_time(&dev, &time));
    */
   /*
        xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) == ESP_OK) {
        register_endpoints(server);
    } else {
        printf("Failed to start HTTP server!\n");
    }
*/
    // Example: Get and print current time
    
   // xTaskCreate(sync_ui_with_rtc_task, "sync_ui_with_rtc_task", 2048, NULL, 10, NULL);
       // Initialize peripherals (e.g., I2C, RTC)
       
           // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
           // Initialize TCP/IP stack
    esp_netif_init();

    // Initialize Wi-Fi stack with default configurations
    esp_event_loop_create_default();

    // Add Wi-Fi network interface
    esp_netif_create_default_wifi_ap();
    printf("Starting application...\n");
    
     load_schedules_from_eeprom() ;
    init_leds();
   
   xTaskCreate(switch_monitor_task, "Switch Monitor Task", 4096, NULL, 10, NULL);
   xTaskCreate(scheduler_task, "Switch Monitor Task", 4096, NULL, 10, NULL);
    while (1) {
		
		//sync_ui_with_rtc_task(NULL);
		
      /*          if (ds3231_get_time(&dev, &time) != ESP_OK)
        {
            printf("Could not get time\n");
            continue;
        }*/
        //        printf("%04d-%02d-%02d %02d:%02d:%02d \n", time.tm_year + 1900 /*Add 1900 for better readability*/, time.tm_mon + 1,
          //  time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec );
            
        vTaskDelay(10000 / portTICK_PERIOD_MS); // Wait 1 second
    }

}