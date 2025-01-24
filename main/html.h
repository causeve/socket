/*
 * html.h
 *
 *  Created on: 14-Jan-2025
 *      Author: ilang
 */

#ifndef MAIN_HTML_H_
#define MAIN_HTML_H_
#include "esp_http_server.h"
/*
const char *html_page =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<title>ESP32 Timer Setup</title>"
    "<style>"
    "body { font-family: Arial, sans-serif; }"
    "h1, h2 { text-align: center; }"
    "table { width: 100%; border-collapse: collapse; margin: 20px 0; }"
    "th, td { border: 1px solid #ddd; padding: 8px; text-align: center; }"
    "th { background-color: #f4f4f4; }"
    ".form-group { margin-bottom: 15px; }"
    "label { display: block; margin-bottom: 5px; }"
    "</style>"
    "</head>"
    "<body>"
    "<h1>ESP32 Timer Setup</h1>"
    "<h2>Set Current Time</h2>"
    "<form action=\"/set_time\" method=\"POST\">"
    "  <div class=\"form-group\">"
    "    <label>Time (HH:MM:SS):</label>"
    "    <input type=\"text\" name=\"time\" placeholder=\"e.g., 12:30:45\">"
    "  </div>"
    "  <div class=\"form-group\">"
    "    <label>Date (DD/MM/YYYY):</label>"
    "    <input type=\"text\" name=\"date\" placeholder=\"e.g., 15/01/2025\">"
    "  </div>"
    "  <button type=\"submit\">Set Time</button>"
    "</form>"
    "<h2>Current Schedules</h2>"
    "<table>"
    "  <thead>"
    "    <tr>"
    "      <th>ID</th>"
    "      <th>IO Pin</th>"
    "      <th>Days</th>"
    "      <th>Start Time</th>"
    "      <th>End Time</th>"
    "      <th>Actions</th>"
    "    </tr>"
    "  </thead>"
    "  <tbody id=\"schedule-table\">"
    "    <!-- Dynamically populated rows -->"
    "  </tbody>"
    "</table>"
    "<script>"
    "fetch('/get_schedules')"
    ".then(response => response.json())"
    ".then(data => {"
    "  document.getElementById('schedule-table').innerHTML = '';"
    "  data.schedules.forEach(schedule => {"
    "    document.getElementById('schedule-table').innerHTML += `"
    "      <tr>"
    "        <td>${schedule.id}</td>"
    "        <td>${schedule.pin}</td>"
    "        <td>${schedule.days}</td>"
    "        <td>${schedule.start_time}</td>"
    "        <td>${schedule.end_time}</td>"
    "        <td>"
    "          <button onclick=\"editSchedule(${schedule.id})\">Edit</button>"
    "          <button onclick=\"deleteSchedule(${schedule.id})\">Delete</button>"
    "        </td>"
    "      </tr>`;"
    "  });"
    "});"
    "</script>"
    "</body>"
    "</html>";
*/

void button_task(void *arg);
void register_endpoints(httpd_handle_t server);
void rtc_set_time(struct tm *timeinfo);
void rtc_get_time(struct tm *timeinfo);

#endif /* MAIN_HTML_H_ */
