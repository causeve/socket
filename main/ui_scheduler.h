/*
 * ui_scheduler.h
 *
 *  Created on: 16-Jan-2025
 *      Author: ilang
 */

#ifndef MAIN_UI_SCHEDULER_H_
#define MAIN_UI_SCHEDULER_H_
#include "nvs_flash.h"
#define MAX_EEPROM_SIZE 4096
#define EEPROM_BASE_ADDRESS 0x0000



void sync_ui_with_rtc_task(void *param) ;
void switch_monitor_task(void *param);
void start_http_server();
void load_schedules_from_eeprom() ;
void init_leds();
void scheduler_task(void *param);

#endif /* MAIN_UI_SCHEDULER_H_ */
