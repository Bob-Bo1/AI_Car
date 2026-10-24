#pragma once
#include <stdbool.h>
#include "esp_err.h"
typedef struct {bool connected;char ssid[33],ip[24],ap_ssid[33];int rssi,reason,ap_clients;unsigned wifi_revision;} car_wifi_status_t;
esp_err_t car_wifi_init(void);
void car_wifi_tick(void);
void car_wifi_status(car_wifi_status_t *out);
esp_err_t car_wifi_configure(const char *ssid,const char *password);

const char *car_hostname(void);

typedef struct {char ssid[33];int rssi;bool secure;} car_wifi_network_t;
esp_err_t car_wifi_scan_start(void);
unsigned car_wifi_scan_result(car_wifi_network_t *out,unsigned capacity,bool *scanning,bool *ready,int *error);
