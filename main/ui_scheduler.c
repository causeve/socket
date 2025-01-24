#include "ui_scheduler.h"
#include <stdio.h>
#include <string.h>
#include "esp_http_server.h"
#include "ds3231.h"
#include "driver/i2c.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "soc/gpio_num.h"
#include "esp_log.h"

#define MAX_LEDS 2
#define MAX_SCHEDULES_PER_LED 3

#define HOTSPOT_SSID "ESP32_Hotspot"
#define HOTSPOT_PASSWORD "password123" // Set a secure password

#define SWITCH_GPIO GPIO_NUM_33  // GPIO connected to the switch
#define PRESS_DURATION 2000    // 2 seconds in milliseconds

// State tracking for hotspot mode
bool hotspot_active = false;

// Schedule structure
typedef struct {
    uint8_t days;         // Bitmask for days (e.g., 0b0111110 for Monday to Friday)
    uint8_t start_hour;   // Start time (hour, 0-23)
    uint8_t start_min;    // Start time (minute, 0-59)
    uint8_t end_hour;     // End time (hour, 0-23)
    uint8_t end_min;      // End time (minute, 0-59)
    bool enabled;         // Enable/Disable status
} schedule_t;
void test_eeprom_read_schedules(i2c_dev_t *eeprom_dev) ;
// LED structure
typedef struct {
    char name[10];                      // LED name (e.g., "LED1")
    uint8_t gpio_pin;                   // GPIO pin number for this LED
    schedule_t schedules[MAX_SCHEDULES_PER_LED]; // Array of schedules (max 3 per LED)
         }   led_t;

led_t leds[MAX_LEDS] = {
    {"LED1",GPIO_NUM_25, {} },  // LED1 with no schedules initially
    {"LED2",GPIO_NUM_26, {} },  // LED2 with no schedules initially
};

extern i2c_dev_t dev; // DS3231 device descriptor
extern i2c_dev_t eeprom; // eeprom device descriptor

const char *main_js =
    "console.log('JavaScript loaded from main.js');"
    "function getCurrentFormattedTime() {"
    "    const now = new Date();"
    "    return `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, '0')}-${String(now.getDate()).padStart(2, '0')} ${String(now.getHours()).padStart(2, '0')}:${String(now.getMinutes()).padStart(2, '0')}`;"
    "}"
    "function fetchRTC() {"
    "    const rtcTimeElement = document.getElementById('rtc-time');"
    "    fetch('/get-rtc')"
    "        .then(response => {"
    "            if (!response.ok) {"
    "                throw new Error('Failed to fetch RTC time');"
    "            }"
    "            return response.text();"
    "        })"
    "        .then(time => {"
    "            rtcTimeElement.innerText = `RTC - ${time}`;"
    "        })"
    "        .catch(err => {"
    "            console.error('Error fetching RTC time:', err);"
    "            rtcTimeElement.innerText = 'RTC - Error';"
    "        });"
    "}"
    "function updateRTC() {"
    "    const rtcTimeElement = document.getElementById('rtc-time');"
    "    const formattedTime = getCurrentFormattedTime();"
    "    fetch('/update-rtc', {"
    "        method: 'POST',"
    "        body: new URLSearchParams({ rtc_time: formattedTime }),"
    "        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },"
    "    })"
    "        .then(response => {"
    "            if (!response.ok) {"
    "                return response.text().then(errorMessage => {"
    "                    throw new Error(errorMessage);"
    "                });"
    "            }"
    "            return response.text();"
    "        })"
    "        .then(message => {"
    "            alert(message);"
    "            if (message === 'Success: RTC updated.') {"
    "                rtcTimeElement.innerText = `RTC - ${formattedTime.split(' ')[1]}`;"
    "            } else {"
    "                rtcTimeElement.innerText = 'RTC - Error';"
    "            }"
    "        })"
    "        .catch(err => {"
    "            console.error('Error updating RTC:', err);"
    "            alert(`Failed to update RTC: ${err.message}`);"
    "            rtcTimeElement.innerText = 'RTC - Error';"
    "        });"
    "}"
    "function fetchSchedules() {"
    "    const led = document.getElementById('led-select').value;"
    "    if (!led) {"
    "        alert('Please select an LED.');"
    "        return;"
    "    }"
    "    fetch('/get-schedules?led=' + led)"
    "        .then(response => {"
    "            if (!response.ok) {"
    "                throw new Error('Failed to fetch schedules');"
    "            }"
    "            return response.json();"
    "        })"
    "        .then(data => {"
    "            const tableBody = document.getElementById('schedule-table-body');"
    "            tableBody.innerHTML = '';"
    "            data.schedules.forEach((schedule, index) => {"
    "                const daysBitmask = parseInt(schedule.days, 16);"
    "                const dayLabels = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];"
    "                const dayCheckboxes = dayLabels.map((label, i) => {"
    "                    const isChecked = (daysBitmask & (1 << i)) !== 0;"
    "                    return `<label><input type='checkbox' data-day value='${1 << i}' ${isChecked ? 'checked' : ''} disabled>${label}</label>`;"
    "                }).join(' ');"
    "                const row = document.createElement('tr');"
    "                row.innerHTML = `"
    "                    <td>${dayCheckboxes}</td>"
    "                    <td><input type='time' value='${String(schedule.start_hour).padStart(2, '0')}:${String(schedule.start_min).padStart(2, '0')}' disabled></td>"
    "                    <td><input type='time' value='${String(schedule.end_hour).padStart(2, '0')}:${String(schedule.end_min).padStart(2, '0')}' disabled></td>"
    "                    <td><input type='checkbox' data-status ${schedule.enabled ? 'checked' : ''} disabled></td>"
    "                `;"
    "                tableBody.appendChild(row);"
    "            });"
    "            isEditing = false;"
    "            document.getElementById('edit-schedules-btn').disabled = false;"
    "            document.getElementById('submit-schedules-btn').disabled = true;"
    "        })"
    "        .catch(err => {"
    "            console.error('Error fetching schedules:', err.message);"
    "        });"
    "}"
    "function editSchedules() {"
    "    const tableBody = document.getElementById('schedule-table-body');"
    "    const rows = tableBody.querySelectorAll('tr');"
    "    rows.forEach((row) => {"
    "        const inputs = row.querySelectorAll('input');"
    "        inputs.forEach((input) => input.removeAttribute('disabled'));"
    "    });"
    "    isEditing = true;"
    "    document.getElementById('submit-schedules-btn').disabled = false;"
    "}"
    "function submitSchedules() {"
    "    if (!isEditing) return;"
    "    const led = document.getElementById('led-select').value;"
    "    const rows = document.querySelectorAll('#schedule-table-body tr');"
    "    const updatedSchedules = Array.from(rows).map((row, index) => {"
    "        const dayCheckboxes = row.querySelectorAll('input[type=\"checkbox\"][data-day]');"
    "        const days = Array.from(dayCheckboxes).reduce((bitmask, checkbox) => {"
    "            return bitmask | (checkbox.checked ? parseInt(checkbox.value, 10) : 0);"
    "        }, 0);"
    "        const timeInputs = row.querySelectorAll('input[type=\"time\"]');"
    "        const enabledCheckbox = row.querySelector('input[type=\"checkbox\"][data-status]');"
    "        const enabled = enabledCheckbox ? enabledCheckbox.checked : false;"
    "        return {"
    "            days: days,"
    "            start_hour: parseInt(timeInputs[0].value.split(':')[0], 10),"
    "            start_min: parseInt(timeInputs[0].value.split(':')[1], 10),"
    "            end_hour: parseInt(timeInputs[1].value.split(':')[0], 10),"
    "            end_min: parseInt(timeInputs[1].value.split(':')[1], 10),"
    "            enabled: enabled"
    "        };"
    "    });"
    "    fetch('/update-schedules', {"
    "        method: 'POST',"
    "        body: JSON.stringify({ led, schedules: updatedSchedules }),"
    "        headers: { 'Content-Type': 'application/json' }"
    "    })"
    "        .then(response => {"
    "            if (!response.ok) return response.text().then(err => { throw new Error(err); });"
    "            return response.text();"
    "        })"
    "        .then(message => {"
    "            alert(message);"
    "            fetchSchedules();"
    "        })"
    "        .catch(err => {"
    "            alert(`Error: ${err.message}`);"
    "        });"
    "}"
    "function fetchLEDs() {"
    "    const ledSelect = document.getElementById('led-select');"
    "    fetch('/get-leds')"
    "        .then(response => {"
    "            if (!response.ok) {"
    "                throw new Error('Failed to fetch LEDs');"
    "            }"
    "            return response.json();"
    "        })"
    "        .then(data => {"
    "            ledSelect.innerHTML = '';"
    "            data.leds.forEach(led => {"
    "                const option = document.createElement('option');"
    "                option.value = led;"
    "                option.textContent = led;"
    "                ledSelect.appendChild(option);"
    "            });"
    "        })"
    "        .catch(err => {"
    "            console.error('Error fetching LEDs:', err);"
    "        });"
    "}"
    "document.addEventListener('DOMContentLoaded', () => {"
    "    fetchRTC();"
    "    fetchLEDs();"
    "    const updateRTCButton = document.getElementById('update-rtc-btn');"
    "    if (updateRTCButton) {"
    "        updateRTCButton.addEventListener('click', updateRTC);"
    "    }"
    "});";



#include <ctype.h>



void url_decode(const char *src, char *dest, size_t dest_len) {
    size_t i = 0, j = 0;

    while (src[i] && j < dest_len - 1) {
        if (src[i] == '%') {
            if (isxdigit((unsigned char)src[i + 1]) && isxdigit((unsigned char)src[i + 2])) {
                char hex[3] = {src[i + 1], src[i + 2], '\0'};
                dest[j++] = (char)strtol(hex, NULL, 16); // Convert %xx to a character
                i += 3;
            } else {
                dest[j++] = src[i++];
            }
        } else if (src[i] == '+') {
            dest[j++] = ' '; // Convert + to space
            i++;
        } else {
            dest[j++] = src[i++];
        }
    }
    dest[j] = '\0'; // Null-terminate the decoded string
}


// Custom EEPROM read function
// EEPROM Write Function
esp_err_t eeprom_write(const i2c_dev_t *dev, uint16_t address, const uint8_t *data, size_t len) {
    size_t page_size = 32; // 32 bytes per page
    size_t written = 0;

    while (written < len) {
        // Calculate remaining space in the current page
        size_t page_offset = address % page_size;
        size_t space_in_page = page_size - page_offset;

        // Calculate how much to write in this iteration
        size_t to_write = (len - written > space_in_page) ? space_in_page : (len - written);

        // Address buffer (16-bit addressing)
        uint8_t addr_buf[2] = {
            (uint8_t)((address >> 8) & 0xFF),
            (uint8_t)(address & 0xFF)
        };

        // Write the data to the EEPROM
        esp_err_t ret = i2c_dev_write(dev, addr_buf, sizeof(addr_buf), data + written, to_write);
        if (ret != ESP_OK) {
            ESP_LOGE("eeprom_write", "Failed to write to EEPROM at Address: %04x", address);
            return ret;
        }

        // Log the write operation
        ESP_LOGI("eeprom_write", "Wrote to EEPROM Address: %04x, Bytes: %d", address, to_write);

        // Wait for the write cycle to complete
        vTaskDelay(10 / portTICK_PERIOD_MS);

        // Move to the next address and data chunk
        address += to_write;
        written += to_write;
    }

    return ESP_OK;
}




// EEPROM Read Function
esp_err_t eeprom_read(i2c_dev_t *dev, uint16_t address, uint8_t *data, size_t len) {
    if (!dev || !data || len == 0) {
        ESP_LOGE("eeprom_read", "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t addr_buf[2] = {
        (uint8_t)((address >> 8) & 0xFF),
        (uint8_t)(address & 0xFF)
    };

    esp_err_t ret = i2c_dev_read(dev, addr_buf, sizeof(addr_buf), data, len);
    if (ret != ESP_OK) {
        ESP_LOGE("eeprom_read", "Failed to read from EEPROM at Address: %04x, Length: %d", address, len);
    } else {
        ESP_LOGI("eeprom_read", "Read from EEPROM Address: %04x, Length: %d", address, len);
    }
    return ret;
}

void init_leds() {
    for (int i = 0; i < MAX_LEDS; i++) {
        gpio_set_direction(leds[i].gpio_pin, GPIO_MODE_OUTPUT);
        gpio_set_level(leds[i].gpio_pin, 0); // Initially turn off LEDs
    }
}



// Function to save a schedule to EEPROM
void save_schedules_to_eeprom() {
    for (uint8_t i = 0; i < MAX_LEDS; i++) {
        for (uint8_t j = 0; j < MAX_SCHEDULES_PER_LED; j++) {
            schedule_t *schedule = &leds[i].schedules[j];

        /*    // Skip invalid or disabled schedules
            if (schedule->days == 0 && !schedule->enabled) {
                ESP_LOGI("save_schedules_to_eeprom", "Skipping invalid or disabled schedule for LED %d, Slot %d", i, j);
                continue;
            }*/

            uint16_t eeprom_address = (i * MAX_SCHEDULES_PER_LED + j) * 6 + EEPROM_BASE_ADDRESS; // Calculate EEPROM address
            uint8_t data[6] = {
                schedule->days,
                schedule->start_hour,
                schedule->start_min,
                schedule->end_hour,
                schedule->end_min,
                schedule->enabled
            };

            // Log the schedule being saved
         //   ESP_LOGI("save_schedules_to_eeprom", "Saving schedule to EEPROM: LED %d, Slot %d, Days: %02x, Start: %02d:%02d, End: %02d:%02d, Enabled: %s, Address: %04x",
         //            i, j, schedule->days, schedule->start_hour, schedule->start_min,
         //            schedule->end_hour, schedule->end_min, schedule->enabled ? "true" : "false", eeprom_address);
                     
                     ESP_LOGI("save_schedules_to_eeprom", "Writing to EEPROM: LED %d, Slot %d, Address: %04x, Data: %02x %02x %02x %02x %02x %02x",
         i, j, eeprom_address,
         data[0], data[1], data[2], data[3], data[4], data[5]);


            esp_err_t ret =  eeprom_write(&eeprom, eeprom_address, data, sizeof(data)); // Replace with your EEPROM write function
            
             if (ret != ESP_OK) {
                ESP_LOGE("save_schedules_to_eeprom", "Failed to write schedule for LED %d, Slot %d", i, j);
            }

         
             
        }
    }
    
    //test_eeprom_read_schedules(&eeprom);
}




// Function to serve the UI page 
esp_err_t render_ui(httpd_req_t *req) {
    const char *html =
        "<!DOCTYPE html>"
        "<html lang=\"en\">"
        "<head>"
        "    <meta charset=\"UTF-8\">"
        "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
        "    <title>LED Scheduler</title>"
        "    <script src=\"/main.js\"></script>"
        "</head>"
        "<body>"
        "    <h1>LED Scheduler</h1>"
        "    <h2>Current Time</h2>"
        "    <span id=\"rtc-time\">RTC - HH:MM</span>"
        "    <button id=\"update-rtc-btn\">Update RTC</button>"
        "    <br><br>"
        "    <label for=\"led-select\">Select LED:</label>"
        "    <select id=\"led-select\"></select>"
        "    <button onclick=\"fetchSchedules()\">View Schedules</button>"
        "    <br><br>"
        "    <table id=\"schedule-table\" border=\"1\">"
        "        <thead>"
        "            <tr>"
        "                <th>Days</th>"
        "                <th>ON Time</th>"
        "                <th>OFF Time</th>"
        "                <th>Status</th>"
        "            </tr>"
        "        </thead>"
        "        <tbody id=\"schedule-table-body\"></tbody>"
        "    </table>"
        "    <br>"
        "    <button onclick=\"editSchedules()\">Edit</button>"
        "    <button onclick=\"submitSchedules()\">Submit</button>"
        "</body>"
        "</html>";

    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}







bool add_schedule(uint8_t led_index, schedule_t schedule) {
    if (led_index >= MAX_LEDS) {
        return false; // Invalid LED index
    }

    for (uint8_t i = 0; i < MAX_SCHEDULES_PER_LED; i++) {
        if (leds[led_index].schedules[i].days == 0 && !leds[led_index].schedules[i].enabled) {
            leds[led_index].schedules[i] = schedule; // Add the schedule to the first free slot
            return true;
        }
    }

    return false; // No available slot
}


void clear_schedules(uint8_t led_index) {
    if (led_index >= MAX_LEDS) {
        return; // Invalid LED index
    }

    for (uint8_t i = 0; i < MAX_SCHEDULES_PER_LED; i++) {
        leds[led_index].schedules[i] = (schedule_t){0}; // Reset each schedule
    }
}


schedule_t *get_schedule(uint8_t led_index, uint8_t schedule_index) {
    if (led_index >= MAX_LEDS || schedule_index >= MAX_SCHEDULES_PER_LED) {
        return NULL; // Invalid LED or schedule index
    }

    schedule_t *schedule = &leds[led_index].schedules[schedule_index];

    // Check if the schedule is valid
    if (schedule->days == 0 && !schedule->enabled) {
        return NULL; // Empty or invalid schedule
    }

    return schedule; // Return a pointer to the schedule
}


void print_schedule(uint8_t led_index, uint8_t schedule_index) {
    schedule_t *schedule = get_schedule(led_index, schedule_index);
    if (schedule) {
        printf("Days: %02x, Start: %02d:%02d, End: %02d:%02d, Enabled: %s\n",
               schedule->days, schedule->start_hour, schedule->start_min,
               schedule->end_hour, schedule->end_min,
               schedule->enabled ? "true" : "false");
    } else {
        printf("No valid schedule found for LED %d, Schedule %d\n", led_index, schedule_index);
    }
}


// Function to load schedules from EEPROM
void load_schedules_from_eeprom() {
    ESP_LOGI("load_schedules_from_eeprom", "Loading schedules from EEPROM");

    for (uint8_t i = 0; i < MAX_LEDS; i++) {
        for (uint8_t j = 0; j < MAX_SCHEDULES_PER_LED; j++) {
            uint16_t eeprom_address = (i * MAX_SCHEDULES_PER_LED + j) * 6 + EEPROM_BASE_ADDRESS; // Start from 0x0000
            uint8_t data[6];
            esp_err_t result = eeprom_read(&eeprom, eeprom_address, data, sizeof(data));

            if (result != ESP_OK) {
                ESP_LOGE("load_schedules_from_eeprom", "Failed to read EEPROM at Address: %04x", eeprom_address);
                continue;
            }

            // Log raw data
            ESP_LOGI("load_schedules_from_eeprom", "Raw data from EEPROM: LED %d, Slot %d, Address: %04x, Data: %02x %02x %02x %02x %02x %02x",
                     i, j, eeprom_address, data[0], data[1], data[2], data[3], data[4], data[5]);

            // Validate the schedule
            if (data[0] == 0xFF || data[5] == 0) {
                ESP_LOGI("load_schedules_from_eeprom", "Empty or disabled schedule: LED %d, Slot %d, Address: %04x", i, j, eeprom_address);
                leds[i].schedules[j] = (schedule_t){0,0,0,0,0,0};
                continue;
            }

            // Load valid schedule
            leds[i].schedules[j] = (schedule_t){
                .days = data[0],
                .start_hour = data[1],
                .start_min = data[2],
                .end_hour = data[3],
                .end_min = data[4],
                .enabled = data[5]
            };

            // Log loaded schedule
            ESP_LOGI("load_schedules_from_eeprom", "Loaded schedule: LED %d, Slot %d, Days: %02x, Start: %02d:%02d, End: %02d:%02d, Enabled: %s, Address: %04x",
                     i, j, leds[i].schedules[j].days, leds[i].schedules[j].start_hour, leds[i].schedules[j].start_min,
                     leds[i].schedules[j].end_hour, leds[i].schedules[j].end_min,
                     leds[i].schedules[j].enabled ? "true" : "false", eeprom_address);
        }
    }

    ESP_LOGI("load_schedules_from_eeprom", "Finished loading schedules from EEPROM");
}







int get_led_index(const char *led_name) {
    for (int i = 0; i < MAX_LEDS; i++) {
        if (strcmp(leds[i].name, led_name) == 0) {
            return i; // Found matching LED, return its index
        }
    }
    return -1; // Return -1 if the LED name is not found
}

// Function to handle fetching schedules as JSON
// Fetch schedules for a specific LED
esp_err_t get_schedules_handler(httpd_req_t *req) {
    ESP_LOGI("get_schedules_handler", "Handler invoked");

    // Parse query string
    char query[256]; // Increased buffer size
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        ESP_LOGE("get_schedules_handler", "Failed to get query string");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Query string missing");
        return ESP_FAIL;
    }
    ESP_LOGI("get_schedules_handler", "Query string: %s", query);

    // Extract LED name
    char led_name[16]; // Increased buffer size for LED name
    if (httpd_query_key_value(query, "led", led_name, sizeof(led_name)) != ESP_OK) {
        ESP_LOGE("get_schedules_handler", "LED name missing in query string");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "LED name missing");
        return ESP_FAIL;
    }
    ESP_LOGI("get_schedules_handler", "LED name: %s", led_name);

    // Get LED index
    int led_index = get_led_index(led_name);
    if (led_index == -1) {
        ESP_LOGE("get_schedules_handler", "LED not found: %s", led_name);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "LED not found");
        return ESP_FAIL;
    }
    ESP_LOGI("get_schedules_handler", "LED index: %d", led_index);

    // Construct JSON response
    size_t buffer_size = 1024; // Larger buffer size
    char *json_response = malloc(buffer_size);
    if (!json_response) {
        ESP_LOGE("get_schedules_handler", "Failed to allocate memory");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Internal error");
        return ESP_FAIL;
    }

    snprintf(json_response, buffer_size, "{\"schedules\": [");
    size_t used_size = strlen(json_response);
    for (int i = 0; i < MAX_SCHEDULES_PER_LED; i++) {
        schedule_t *schedule = &leds[led_index].schedules[i];
        int written = snprintf(json_response + used_size, buffer_size - used_size,
            "{\"days\":\"%02x\",\"start_hour\":%d,\"start_min\":%d,\"end_hour\":%d,\"end_min\":%d,\"enabled\":%s}%s",
            schedule->days, schedule->start_hour, schedule->start_min,
            schedule->end_hour, schedule->end_min,
            schedule->enabled ? "true" : "false",
            (i < MAX_SCHEDULES_PER_LED - 1) ? "," : "");
        if (written < 0 || written >= (int)(buffer_size - used_size)) {
            ESP_LOGE("get_schedules_handler", "Buffer overflow while constructing JSON");
            //free(json_response);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Response too large");
            return ESP_FAIL;
        }
        used_size += written;
    }
    snprintf(json_response + used_size, buffer_size - used_size, "]}");
    ESP_LOGE("get_schedules_handler", "Constructed JSON response: %s", json_response);

    // Send the response
    httpd_resp_send(req, json_response, HTTPD_RESP_USE_STRLEN);
    free(json_response);
    return ESP_OK;
}


#if 0
esp_err_t get_schedules_handler_old(httpd_req_t *req) {
    ESP_LOGI("get_schedules_handler", "Handler invoked");

    // Parse query string
    char query[128];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        ESP_LOGE("get_schedules_handler", "Failed to get query string");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Query string missing");
        return ESP_FAIL;
    }
    ESP_LOGI("get_schedules_handler", "Query string: %s", query);

    // Extract LED name
    char led_name[10];
    if (httpd_query_key_value(query, "led", led_name, sizeof(led_name)) != ESP_OK) {
        ESP_LOGE("get_schedules_handler", "LED name missing in query string");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "LED name missing");
        return ESP_FAIL;
    }
    ESP_LOGI("get_schedules_handler", "LED name: %s", led_name);

    // Get LED index
    int led_index = get_led_index(led_name);
    if (led_index == -1) {
        ESP_LOGE("get_schedules_handler", "LED not found: %s", led_name);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "LED not found");
        return ESP_FAIL;
    }
    ESP_LOGI("get_schedules_handler", "LED index: %d", led_index);

    // Construct JSON response
    char json_response[512] = "{\"schedules\": [";
    size_t used_size = strlen(json_response);
    for (int i = 0; i < MAX_SCHEDULES_PER_LED; i++) {
        schedule_t *schedule = &leds[led_index].schedules[i];
        int written = snprintf(json_response + used_size, sizeof(json_response) - used_size,
            "{\"days\":\"%02x\",\"start_hour\":%d,\"start_min\":%d,\"end_hour\":%d,\"end_min\":%d,\"enabled\":%s}%s",
            schedule->days, schedule->start_hour, schedule->start_min,
            schedule->end_hour, schedule->end_min,
            schedule->enabled ? "true" : "false",
            (i < MAX_SCHEDULES_PER_LED - 1) ? "," : "");
        if (written < 0 || written >= (int)(sizeof(json_response) - used_size)) {
            ESP_LOGE("get_schedules_handler", "Buffer overflow while constructing JSON");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Response too large");
            return ESP_FAIL;
        }
        used_size += written;
    }
    snprintf(json_response + used_size, sizeof(json_response) - used_size, "]}");
    ESP_LOGI("get_schedules_handler", "Constructed JSON response: %s", json_response);

    // Send the response
    httpd_resp_send(req, json_response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

#endif

// Helper function to convert 12-hour time to 24-hour format
void parse_time_12hr_to_24hr(const char *time_str, uint8_t *hour, uint8_t *minute) {
    int temp_hour, temp_min;
    char period[3] = ""; // To store AM/PM

    // Parse the time string (e.g., "05:30 PM")
    if (sscanf(time_str, "%d:%d %2s", &temp_hour, &temp_min, period) == 3) {
        if (strcasecmp(period, "PM") == 0 && temp_hour != 12) {
            temp_hour += 12; // Add 12 for PM, except for 12 PM
        } else if (strcasecmp(period, "AM") == 0 && temp_hour == 12) {
            temp_hour = 0; // Set to 0 for 12 AM
        }
        *hour = (uint8_t)temp_hour;
        *minute = (uint8_t)temp_min;
    } else {
        ESP_LOGW("parse_time_12hr_to_24hr", "Invalid time string: %s", time_str);
        *hour = 0; // Default to 00:00 on parse failure
        *minute = 0;
    }
}



void update_led_schedules(uint8_t led_index, schedule_t *new_schedules, uint8_t schedule_count) {
    ESP_LOGI("update_led_schedules", "Updating local copy for LED %d", led_index);

    // Validate LED index
    if (led_index >= MAX_LEDS) {
        ESP_LOGE("update_led_schedules", "Invalid LED index: %d", led_index);
        return;
    }

    // Validate schedule count
    if (schedule_count > MAX_SCHEDULES_PER_LED) {
        ESP_LOGW("update_led_schedules", "Too many schedules provided (%d). Truncating to %d.", schedule_count, MAX_SCHEDULES_PER_LED);
        schedule_count = MAX_SCHEDULES_PER_LED; // Limit to max allowed schedules
    }

    // Clear existing schedules
    ESP_LOGI("update_led_schedules", "Clearing existing schedules for LED %d", led_index);
    for (uint8_t i = 0; i < MAX_SCHEDULES_PER_LED; i++) {
        leds[led_index].schedules[i] = (schedule_t){0}; // Reset each schedule
    }

    // Add new schedules sequentially
    for (uint8_t i = 0; i < schedule_count; i++) {
        leds[led_index].schedules[i] = new_schedules[i];

        // Log each updated schedule
        ESP_LOGI("update_led_schedules", "Updated schedule: Slot %d, Days: %02x, Start: %02d:%02d, End: %02d:%02d, Enabled: %s",
                 i, new_schedules[i].days, new_schedules[i].start_hour, new_schedules[i].start_min,
                 new_schedules[i].end_hour, new_schedules[i].end_min, new_schedules[i].enabled ? "true" : "false");
    }

    // Log completion
    ESP_LOGI("update_led_schedules", "Finished updating schedules for LED %d", led_index);
}

esp_err_t update_schedules_handler(httpd_req_t *req) {
    ESP_LOGI("update_schedules_handler", "Handler invoked");

    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        ESP_LOGE("update_schedules_handler", "Failed to receive data");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        return ESP_FAIL;
    }
    buf[ret] = '\0'; // Null-terminate the received data
    ESP_LOGI("update_schedules_handler", "Received JSON: %s", buf);

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        ESP_LOGE("update_schedules_handler", "Invalid JSON");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *led_name_json = cJSON_GetObjectItem(json, "led");
    if (!cJSON_IsString(led_name_json)) {
        ESP_LOGE("update_schedules_handler", "LED name missing or invalid in JSON");
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "LED name missing or invalid");
        return ESP_FAIL;
    }
    const char *led_name = cJSON_GetStringValue(led_name_json);
    ESP_LOGI("update_schedules_handler", "LED name: %s", led_name);

    int led_index = get_led_index(led_name);
    if (led_index == -1) {
        ESP_LOGE("update_schedules_handler", "LED not found: %s", led_name);
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "LED not found");
        return ESP_FAIL;
    }
    ESP_LOGI("update_schedules_handler", "LED index: %d", led_index);

    cJSON *schedules_json = cJSON_GetObjectItem(json, "schedules");
    if (!cJSON_IsArray(schedules_json)) {
        ESP_LOGE("update_schedules_handler", "Schedules missing or invalid in JSON");
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Schedules missing or invalid");
        return ESP_FAIL;
    }

    schedule_t new_schedules[MAX_SCHEDULES_PER_LED] = {0};
    uint8_t schedule_count = 0;

    cJSON *schedule_json;
    cJSON_ArrayForEach(schedule_json, schedules_json) {
        if (schedule_count >= MAX_SCHEDULES_PER_LED) {
            ESP_LOGW("update_schedules_handler", "Ignoring extra schedules beyond limit (%d)", MAX_SCHEDULES_PER_LED);
            break;
        }

        schedule_t new_schedule = {0};
        // Parse enabled status
        new_schedule.enabled = cJSON_IsTrue(cJSON_GetObjectItem(schedule_json, "enabled"));
        // Parse enabled status
       // cJSON *enabled_json = cJSON_GetObjectItem(schedule_json, "enabled");
        //new_schedule.enabled = cJSON_IsTrue(enabled_json);

        if (new_schedule.enabled) {
            // Validate days bitmask
            cJSON *days_json = cJSON_GetObjectItem(schedule_json, "days");
            if (!cJSON_IsNumber(days_json) || days_json->valueint == 0) {
                ESP_LOGE("update_schedules_handler", "Invalid days bitmask in enabled schedule %d", schedule_count);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid days bitmask in enabled schedule");
                cJSON_Delete(json);
                return ESP_FAIL;
            }
            new_schedule.days = (uint8_t)days_json->valueint;

            // Validate start and end times
            cJSON *start_hour_json = cJSON_GetObjectItem(schedule_json, "start_hour");
            cJSON *start_min_json = cJSON_GetObjectItem(schedule_json, "start_min");
            cJSON *end_hour_json = cJSON_GetObjectItem(schedule_json, "end_hour");
            cJSON *end_min_json = cJSON_GetObjectItem(schedule_json, "end_min");
            if (!cJSON_IsNumber(start_hour_json) || !cJSON_IsNumber(start_min_json) ||
                !cJSON_IsNumber(end_hour_json) || !cJSON_IsNumber(end_min_json)) {
                ESP_LOGE("update_schedules_handler", "Invalid time fields in enabled schedule %d", schedule_count);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid time fields in enabled schedule");
                cJSON_Delete(json);
                return ESP_FAIL;
            }

            new_schedule.start_hour = (uint8_t)start_hour_json->valueint;
            new_schedule.start_min = (uint8_t)start_min_json->valueint;
            new_schedule.end_hour = (uint8_t)end_hour_json->valueint;
            new_schedule.end_min = (uint8_t)end_min_json->valueint;

            // Ensure start time is earlier than end time
            if (new_schedule.start_hour > new_schedule.end_hour ||
                (new_schedule.start_hour == new_schedule.end_hour && new_schedule.start_min >= new_schedule.end_min)) {
                ESP_LOGE("update_schedules_handler", "Invalid time range in enabled schedule %d", schedule_count);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid time range in enabled schedule");
                cJSON_Delete(json);
                return ESP_FAIL;
            }
        }

        new_schedules[schedule_count++] = new_schedule;

        ESP_LOGI("update_schedules_handler", "Parsed schedule %d: Days: %02x, Start: %02d:%02d, End: %02d:%02d, Enabled: %s",
                 schedule_count - 1, new_schedule.days, new_schedule.start_hour, new_schedule.start_min,
                 new_schedule.end_hour, new_schedule.end_min, new_schedule.enabled ? "true" : "false");
    }

    update_led_schedules(led_index, new_schedules, schedule_count);
    save_schedules_to_eeprom();

    cJSON_Delete(json);
    httpd_resp_sendstr(req, "Success: Schedules updated.");
    return ESP_OK;
}

#if 0
esp_err_t update_schedules_handler_new(httpd_req_t *req) {
    ESP_LOGI("update_schedules_handler", "Handler invoked");

    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        ESP_LOGE("update_schedules_handler", "Failed to receive data");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        return ESP_FAIL;
    }
    buf[ret] = '\0'; // Null-terminate the received data
    ESP_LOGI("update_schedules_handler", "Received JSON: %s", buf);

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        ESP_LOGE("update_schedules_handler", "Invalid JSON");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *led_name_json = cJSON_GetObjectItem(json, "led");
    if (!cJSON_IsString(led_name_json)) {
        ESP_LOGE("update_schedules_handler", "LED name missing or invalid in JSON");
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "LED name missing or invalid");
        return ESP_FAIL;
    }
    const char *led_name = cJSON_GetStringValue(led_name_json);
    ESP_LOGI("update_schedules_handler", "LED name: %s", led_name);

    int led_index = get_led_index(led_name);
    if (led_index == -1) {
        ESP_LOGE("update_schedules_handler", "LED not found: %s", led_name);
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "LED not found");
        return ESP_FAIL;
    }
    ESP_LOGI("update_schedules_handler", "LED index: %d", led_index);

    cJSON *schedules_json = cJSON_GetObjectItem(json, "schedules");
    if (!cJSON_IsArray(schedules_json)) {
        ESP_LOGE("update_schedules_handler", "Schedules missing or invalid in JSON");
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Schedules missing or invalid");
        return ESP_FAIL;
    }

    schedule_t new_schedules[MAX_SCHEDULES_PER_LED] = {0};
    uint8_t schedule_count = 0;

    cJSON *schedule_json;
    cJSON_ArrayForEach(schedule_json, schedules_json) {
        if (schedule_count >= MAX_SCHEDULES_PER_LED) {
            ESP_LOGW("update_schedules_handler", "Ignoring extra schedules beyond limit (%d)", MAX_SCHEDULES_PER_LED);
            break;
        }

        schedule_t new_schedule = {0};

        // Parse enabled status
        cJSON *enabled_json = cJSON_GetObjectItem(schedule_json, "enabled");
        new_schedule.enabled = cJSON_IsTrue(enabled_json);

        if (new_schedule.enabled) {
            // Validate days bitmask
            cJSON *days_json = cJSON_GetObjectItem(schedule_json, "days");
            if (!cJSON_IsNumber(days_json) || days_json->valueint == 0) {
                ESP_LOGE("update_schedules_handler", "Invalid days bitmask in enabled schedule %d", schedule_count);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid days bitmask in enabled schedule");
                cJSON_Delete(json);
                return ESP_FAIL;
            }
            new_schedule.days = (uint8_t)days_json->valueint;

            // Validate start and end times
            cJSON *start_hour_json = cJSON_GetObjectItem(schedule_json, "start_hour");
            cJSON *start_min_json = cJSON_GetObjectItem(schedule_json, "start_min");
            cJSON *end_hour_json = cJSON_GetObjectItem(schedule_json, "end_hour");
            cJSON *end_min_json = cJSON_GetObjectItem(schedule_json, "end_min");
            if (!cJSON_IsNumber(start_hour_json) || !cJSON_IsNumber(start_min_json) ||
                !cJSON_IsNumber(end_hour_json) || !cJSON_IsNumber(end_min_json)) {
                ESP_LOGE("update_schedules_handler", "Invalid time fields in enabled schedule %d", schedule_count);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid time fields in enabled schedule");
                cJSON_Delete(json);
                return ESP_FAIL;
            }

            new_schedule.start_hour = (uint8_t)start_hour_json->valueint;
            new_schedule.start_min = (uint8_t)start_min_json->valueint;
            new_schedule.end_hour = (uint8_t)end_hour_json->valueint;
            new_schedule.end_min = (uint8_t)end_min_json->valueint;

            // Ensure start time is earlier than end time
            if (new_schedule.start_hour > new_schedule.end_hour ||
                (new_schedule.start_hour == new_schedule.end_hour && new_schedule.start_min >= new_schedule.end_min)) {
                ESP_LOGE("update_schedules_handler", "Invalid time range in enabled schedule %d", schedule_count);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid time range in enabled schedule");
                cJSON_Delete(json);
                return ESP_FAIL;
            }
        }

        new_schedules[schedule_count++] = new_schedule;

        ESP_LOGI("update_schedules_handler", "Parsed schedule %d: Days: %02x, Start: %02d:%02d, End: %02d:%02d, Enabled: %s",
                 schedule_count - 1, new_schedule.days, new_schedule.start_hour, new_schedule.start_min,
                 new_schedule.end_hour, new_schedule.end_min, new_schedule.enabled ? "true" : "false");
    }

    update_led_schedules(led_index, new_schedules, schedule_count);
    save_schedules_to_eeprom();

    cJSON_Delete(json);
    httpd_resp_sendstr(req, "Success: Schedules updated.");
    return ESP_OK;
}


esp_err_t update_schedules_handler_old(httpd_req_t *req) {
    ESP_LOGI("update_schedules_handler", "Handler invoked");

    // Receive JSON data
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        ESP_LOGE("update_schedules_handler", "Failed to receive data");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        return ESP_FAIL;
    }
    buf[ret] = '\0'; // Null-terminate the received data
    ESP_LOGI("update_schedules_handler", "Received JSON: %s", buf);

    // Parse JSON
    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        ESP_LOGE("update_schedules_handler", "Invalid JSON");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    // Extract LED name
    cJSON *led_name_json = cJSON_GetObjectItem(json, "led");
    if (!cJSON_IsString(led_name_json)) {
        ESP_LOGE("update_schedules_handler", "LED name missing or invalid in JSON");
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "LED name missing or invalid");
        return ESP_FAIL;
    }
    const char *led_name = cJSON_GetStringValue(led_name_json);
    ESP_LOGI("update_schedules_handler", "LED name: %s", led_name);

    // Get LED index
    int led_index = get_led_index(led_name);
    if (led_index == -1) {
        ESP_LOGE("update_schedules_handler", "LED not found: %s", led_name);
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "LED not found");
        return ESP_FAIL;
    }
    ESP_LOGI("update_schedules_handler", "LED index: %d", led_index);

    // Extract schedules
    cJSON *schedules_json = cJSON_GetObjectItem(json, "schedules");
    if (!cJSON_IsArray(schedules_json)) {
        ESP_LOGE("update_schedules_handler", "Schedules missing or invalid in JSON");
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Schedules missing or invalid");
        return ESP_FAIL;
    }

    // Temporary array to store new schedules
    schedule_t new_schedules[MAX_SCHEDULES_PER_LED] = {0};
    uint8_t schedule_count = 0;

    // Parse schedules from JSON
    cJSON *schedule_json;
    cJSON_ArrayForEach(schedule_json, schedules_json) {
        if (schedule_count >= MAX_SCHEDULES_PER_LED) {
            ESP_LOGW("update_schedules_handler", "Ignoring extra schedules beyond limit (%d)", MAX_SCHEDULES_PER_LED);
            break;
        }

        schedule_t new_schedule = {0};
        // Parse days bitmask
        cJSON *days_json = cJSON_GetObjectItem(schedule_json, "days");
        if (!cJSON_IsNumber(days_json)) {
            ESP_LOGE("update_schedules_handler", "Invalid days bitmask in schedule %d", schedule_count);
            continue;
        }
        new_schedule.days = (uint8_t)days_json->valueint;
        // Parse start time
        cJSON *start_hour_json = cJSON_GetObjectItem(schedule_json, "start_hour");
        cJSON *start_min_json = cJSON_GetObjectItem(schedule_json, "start_min");
        if (cJSON_IsNumber(start_hour_json) && cJSON_IsNumber(start_min_json)) {
            new_schedule.start_hour = (uint8_t)start_hour_json->valueint;
            new_schedule.start_min = (uint8_t)start_min_json->valueint;
        }

        // Parse end time
        cJSON *end_hour_json = cJSON_GetObjectItem(schedule_json, "end_hour");
        cJSON *end_min_json = cJSON_GetObjectItem(schedule_json, "end_min");
        if (cJSON_IsNumber(end_hour_json) && cJSON_IsNumber(end_min_json)) {
            new_schedule.end_hour = (uint8_t)end_hour_json->valueint;
            new_schedule.end_min = (uint8_t)end_min_json->valueint;
        }

        // Parse enabled status
        new_schedule.enabled = cJSON_IsTrue(cJSON_GetObjectItem(schedule_json, "enabled"));

        // Add to the temporary array
        new_schedules[schedule_count++] = new_schedule;

        // Log the parsed schedule
        ESP_LOGI("update_schedules_handler", "Parsed schedule: Days: %02x, Start: %02d:%02d, End: %02d:%02d, Enabled: %s",
                 new_schedule.days, new_schedule.start_hour, new_schedule.start_min,
                 new_schedule.end_hour, new_schedule.end_min,
                 new_schedule.enabled ? "true" : "false");
    }

    // Update schedules for the LED
    ESP_LOGI("update_schedules_handler", "Updating schedules for LED %d", led_index);
    update_led_schedules(led_index, new_schedules, schedule_count);

    // Save updated schedules to EEPROM
    ESP_LOGI("update_schedules_handler", "Saving schedules for LED %d to EEPROM", led_index);
    save_schedules_to_eeprom();

    // Clean up JSON object
    cJSON_Delete(json);

    // Send success response
    ESP_LOGI("update_schedules_handler", "Schedules updated successfully for LED %s", led_name);
    httpd_resp_sendstr(req, "Success: Schedules updated.");
    return ESP_OK;
}

#endif

esp_err_t get_rtc_handler(httpd_req_t *req) {
	 ESP_LOGI("get_rtc_handler", "Handler invoked.");
    struct tm rtc_time;
    if (ds3231_get_time(&dev, &rtc_time) == ESP_OK) {
        char time_str[16];
        snprintf(time_str, sizeof(time_str), "%02d:%02d", rtc_time.tm_hour, rtc_time.tm_min);
        httpd_resp_send(req, time_str, HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_send(req, "Error: Failed to read RTC.", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t get_leds_handler(httpd_req_t *req) {
	 ESP_LOGI("get_leds_handler", "Handler invoked.");
    char json_response[256] = "{\"leds\": ["; // Start the JSON response
    size_t used_size = strlen(json_response); // Track the used space in the buffer

    for (int i = 0; i < MAX_LEDS; i++) {
        // Append the LED name
        int written = snprintf(
            json_response + used_size,
            sizeof(json_response) - used_size,
            "\"%s\"%s",
            leds[i].name,
            (i < MAX_LEDS - 1) ? "," : "" // Add a comma if it's not the last item
        );

        // Check for snprintf errors or buffer overflows
        if (written < 0 || written >= (int)(sizeof(json_response) - used_size)) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Response too large");
            return ESP_FAIL;
        }

        used_size += written; // Update the used size
    }

    // Append the closing bracket for the JSON response
    int final_written = snprintf(json_response + used_size, sizeof(json_response) - used_size, "]}");
    if (final_written < 0 || final_written >= (int)(sizeof(json_response) - used_size)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Response too large");
        return ESP_FAIL;
    }

    // Send the response
    httpd_resp_send(req, json_response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Function to calculate the day of the week using Zeller's Congruence
static int calculate_weekday(int year, int month, int day) {
    if (month < 3) {
        month += 12;
        year -= 1;
    }
    int K = year % 100; // Year within the century
    int J = year / 100; // Century
    int weekday = (day + (13 * (month + 1)) / 5 + K + (K / 4) + (J / 4) - (2 * J)) % 7;
    return ((weekday + 6) % 7)+1; // Convert to 1 = Sunday, 7 = Saturday
}

// Function to handle RTC updates
esp_err_t update_rtc_handler(httpd_req_t *req) {
    ESP_LOGI("update_rtc_handler", "Handler invoked.");

    char buf[128];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        ESP_LOGW("update_rtc_handler", "Failed to receive data.");
        httpd_resp_send(req, "Error: Failed to receive data.", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    buf[received] = '\0';
    ESP_LOGI("update_rtc_handler", "Received data: %s", buf);

    char decoded_buf[128];
    url_decode(buf, decoded_buf, sizeof(decoded_buf)); // Decode URL-encoded data

    struct tm new_time;
    if (sscanf(decoded_buf, "rtc_time=%4d-%2d-%2d %2d:%2d",
               &new_time.tm_year, &new_time.tm_mon, &new_time.tm_mday,
               &new_time.tm_hour, &new_time.tm_min) != 5) {
        ESP_LOGW("update_rtc_handler", "Invalid time format: %s", decoded_buf);
        httpd_resp_send(req, "Error: Invalid time format.", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    ESP_LOGI("update_rtc_handler", "Parsed time: %04d-%02d-%02d %02d:%02d",
             new_time.tm_year, new_time.tm_mon, new_time.tm_mday, new_time.tm_hour, new_time.tm_min);
             
                 // Compute wday if not provided
    
       new_time .tm_wday = calculate_weekday(new_time.tm_year, new_time.tm_mon, new_time.tm_mday);
        ESP_LOGI("update_rtc_handler", "Calculated wday: %d", new_time .tm_wday);

    // Adjust the time for the RTC format
    new_time.tm_year -= 1900; // Adjust year
    new_time.tm_mon -= 1;     // Adjust month

    // Update the RTC hardware
    if (ds3231_set_time(&dev, &new_time) == ESP_OK) {
        ESP_LOGI("update_rtc_handler", "RTC updated successfully.");
        httpd_resp_send(req, "Success: RTC updated.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    } else {
        ESP_LOGE("update_rtc_handler", "Failed to update RTC.");
        httpd_resp_send(req, "Error: Failed to update RTC.", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
}






esp_err_t serve_main_js(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, main_js, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}


// Initialization function
void ui_scheduler_init(httpd_handle_t server) {
    //load_schedules_from_eeprom(); // Load schedules into memory
    
        httpd_uri_t js_handler = {
        .uri = "/main.js",
        .method = HTTP_GET,
        .handler = serve_main_js,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &js_handler);

    httpd_uri_t ui_page = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = render_ui
    };
    httpd_register_uri_handler(server, &ui_page);


    httpd_uri_t get_schedules_uri  = {
        .uri = "/get-schedules",
        .method = HTTP_GET,
        .handler = get_schedules_handler
    };
    
        if (httpd_register_uri_handler(server, &get_schedules_uri) != ESP_OK) {
        ESP_LOGE("HTTP", "Failed to register /get-schedules endpoint");
    }
    
    
	    httpd_uri_t get_leds_uri = {
	    .uri = "/get-leds",
	    .method = HTTP_GET,
	    .handler = get_leds_handler,
	    .user_ctx = NULL
	};
	httpd_register_uri_handler(server, &get_leds_uri);
        // Define the update-schedules endpoint
    httpd_uri_t update_schedules_uri  = {
        .uri = "/update-schedules",
        .method = HTTP_POST,
        .handler = update_schedules_handler,
        .user_ctx = NULL
    };
    
        if (httpd_register_uri_handler(server, &update_schedules_uri) != ESP_OK) {
        ESP_LOGE("HTTP", "Failed to register /update-schedules endpoint");
    }
    
	    httpd_uri_t rtc_handler = {
	    .uri = "/get-rtc",
	    .method = HTTP_GET,
	    .handler = get_rtc_handler,
	    .user_ctx = NULL
	};
	httpd_register_uri_handler(server, &rtc_handler);

    httpd_uri_t update_rtc = {
        .uri = "/update-rtc",
        .method = HTTP_POST,
        .handler = update_rtc_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &update_rtc);
    


}


void sync_ui_with_rtc_task(void *param) {
    struct tm rtc_time;
#if 0 // EEPROM Write Test code
    uint8_t eeprom_test_data[] = {0x12, 0x34, 0x56, 0x78, 0x9A}; // Test data
    uint8_t eeprom_read_data[sizeof(eeprom_test_data)];          // Buffer for reading

    // EEPROM Write Test
    if (eeprom_write(&eeprom, 0x0000, eeprom_test_data, sizeof(eeprom_test_data)) == ESP_OK) {
        printf("EEPROM write successful.\n");
    } else {
        printf("EEPROM write failed.\n");
    }
    
    vTaskDelay(10 / portTICK_PERIOD_MS); // 10ms delay


    // EEPROM Read Test
    if (eeprom_read(&eeprom, 0x0000, eeprom_read_data, sizeof(eeprom_read_data)) == ESP_OK) {
        printf("EEPROM read data: ");
        for (size_t i = 0; i < sizeof(eeprom_read_data); i++) {
            printf("0x%02X ", eeprom_read_data[i]);
        }
        printf("\n");

        // Verify data
        bool match = true;
        for (size_t i = 0; i < sizeof(eeprom_test_data); i++) {
            if (eeprom_read_data[i] != eeprom_test_data[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            printf("EEPROM test passed.\n");
        } else {
            printf("EEPROM test failed: Data mismatch.\n");
        }
    } else {
        printf("EEPROM read failed.\n");
    }
#endif
    // Sync RTC in the loop
    while (1) {
		vTaskDelay(5000 / portTICK_PERIOD_MS);
		
        if (ds3231_get_time(&dev, &rtc_time) == ESP_OK) {
            printf("RTC Time: %04d-%02d-%02d %02d:%02d:%02d weak day: %d\n",
                   rtc_time.tm_year + 1900, rtc_time.tm_mon + 1, rtc_time.tm_mday,
                   rtc_time.tm_hour, rtc_time.tm_min, rtc_time.tm_sec, rtc_time.tm_wday);
        } else {
            printf("Failed to sync RTC time.\n");
        }
        
    }
}

void start_http_server() {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) == ESP_OK) {
        ui_scheduler_init(server); // Register all endpoints and load schedules
        printf("HTTP server started successfully.\n");
    } else {
        printf("Failed to start HTTP server!\n");
    }
}

void start_hotspot() {
    // Ensure NVS is initialized
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Stop and deinitialize Wi-Fi if already running
    esp_wifi_stop();
    esp_wifi_deinit();

    // Initialize Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Configure AP mode
    esp_wifi_set_mode(WIFI_MODE_AP);
    wifi_config_t ap_config = {
        .ap = {
            .ssid = HOTSPOT_SSID,
            .ssid_len = strlen(HOTSPOT_SSID),
            .password = HOTSPOT_PASSWORD,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    // Start Wi-Fi
    if (esp_wifi_start() == ESP_OK) {
        printf("Hotspot started. SSID: %s, Password: %s\n", HOTSPOT_SSID, HOTSPOT_PASSWORD);
        start_http_server();
    } else {
        printf("Failed to start hotspot.\n");
    }
}

void stop_hotspot() {
    esp_wifi_stop();
    printf("Hotspot deactivated. Returning to normal mode.\n");
}


void test_eeprom_read_schedules(i2c_dev_t *eeprom_dev) {
    ESP_LOGI("EEPROM Test", "Starting EEPROM read test");

    for (uint8_t led_index = 0; led_index < MAX_LEDS; led_index++) {
        ESP_LOGI("EEPROM Test", "Reading schedules for LED %d", led_index);

        for (uint8_t schedule_index = 0; schedule_index < MAX_SCHEDULES_PER_LED; schedule_index++) {
            uint16_t eeprom_address = EEPROM_BASE_ADDRESS  + 
                                       (led_index * MAX_SCHEDULES_PER_LED + schedule_index) * sizeof(schedule_t);
            uint8_t data[sizeof(schedule_t)] = {0};

            esp_err_t result = eeprom_read(eeprom_dev, eeprom_address, data, sizeof(schedule_t));
            if (result != ESP_OK) {
                ESP_LOGE("EEPROM Test", "Failed to read schedule at Address: %04x", eeprom_address);
                continue;
            }

            // Parse the data
            schedule_t schedule = {
                .days = data[0],
                .start_hour = data[1],
                .start_min = data[2],
                .end_hour = data[3],
                .end_min = data[4],
                .enabled = data[5]
            };

            // Print the schedule
            ESP_LOGI("EEPROM Test", "LED %d, Schedule %d: Days: %02x, Start: %02d:%02d, End: %02d:%02d, Enabled: %s",
                     led_index, schedule_index, schedule.days, schedule.start_hour, schedule.start_min,
                     schedule.end_hour, schedule.end_min, schedule.enabled ? "true" : "false");
        }
    }

    ESP_LOGI("EEPROM Test", "EEPROM read test completed");
}

void switch_monitor_task(void *param) {
    gpio_set_direction(SWITCH_GPIO, GPIO_MODE_INPUT);
    gpio_pullup_en(SWITCH_GPIO); // Enable pull-up resistor for active-low input

    while (1) {
        if (gpio_get_level(SWITCH_GPIO) == 0) { // Switch active low
            vTaskDelay(PRESS_DURATION / portTICK_PERIOD_MS);
            if (gpio_get_level(SWITCH_GPIO) == 0) { // Still active low after 2 seconds
                if (!hotspot_active) {
                    printf("Switch active for 2 seconds. Activating hotspot...\n");
                    start_hotspot();
                    hotspot_active = true;
                }
            }
        } else if (hotspot_active) { // Switch returned to high
            printf("Switch moved to high. Deactivating hotspot...\n");
            stop_hotspot();
            hotspot_active = false;
        }

        vTaskDelay(100 / portTICK_PERIOD_MS); // Check every 100ms
    }
}






#define TAG "LED Scheduler"

// Function to update LED states based on schedules
void update_led_schedule(bool wifi_mode_enabled) {
    // Check if Wi-Fi mode is enabled
    if (wifi_mode_enabled) {
        ESP_LOGI(TAG, "Wi-Fi mode is enabled. Scheduler is paused.");
        return;
    }

    // Get the current time from the RTC
    struct tm rtc_time;
    if (ds3231_get_time(&dev, &rtc_time) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get RTC time.");
        return;
    }

    ESP_LOGI(TAG, "Current time: %04d-%02d-%02d %02d:%02d:%02d, Weekday: %d",
             rtc_time.tm_year + 1900, rtc_time.tm_mon + 1, rtc_time.tm_mday,
             rtc_time.tm_hour, rtc_time.tm_min, rtc_time.tm_sec, rtc_time.tm_wday);

    // Iterate over all LEDs
    for (int i = 0; i < MAX_LEDS; i++) {
        led_t *led = &leds[i];

        // Iterate over all schedules for the current LED
        for (int j = 0; j < MAX_SCHEDULES_PER_LED; j++) {
            schedule_t *schedule = &led->schedules[j];

            // Check if the schedule is enabled
            if (!schedule->enabled) {
                ESP_LOGD(TAG, "Schedule %d for LED %s is disabled.", j, led->name);
                continue;
            }

            // Check if the current weekday matches the schedule's days bitmask
            if (!(schedule->days & (1 << (rtc_time.tm_wday - 1)))) {
                ESP_LOGD(TAG, "Schedule %d for LED %s does not match the current weekday.", j, led->name);
                continue;
            }

            // Check if the current time falls within the schedule's time range
            bool within_time_range = 
                (rtc_time.tm_hour > schedule->start_hour || 
                (rtc_time.tm_hour == schedule->start_hour && rtc_time.tm_min >= schedule->start_min)) &&
                (rtc_time.tm_hour < schedule->end_hour || 
                (rtc_time.tm_hour == schedule->end_hour && rtc_time.tm_min <= schedule->end_min));

            // Update the LED state based on the schedule
            int new_state = within_time_range ? 1 : 0;
            gpio_set_level(led->gpio_pin, new_state);
            ESP_LOGI(TAG, "LED %s (GPIO %d) turned %s based on schedule %d.",
                     led->name, led->gpio_pin, new_state ? "ON" : "OFF", j);
        }
    }
}


void scheduler_task(void *param) {
    while (true) {
        update_led_schedule(hotspot_active); // Pass Wi-Fi mode status
        vTaskDelay(pdMS_TO_TICKS(60000)); // Run every minute
    }
}


