#include "wifi_manager.h"
#include "motor_bench.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "mdns.h"
static car_wifi_status_t state;
static char hostname[32];
const char *car_hostname(void){return hostname;}
static SemaphoreHandle_t mutex;
static int64_t last_attempt;
static bool started,has_credentials;
static bool scan_running,scan_ready;static int scan_error;
static unsigned scan_count;static car_wifi_network_t scan_networks[24];
static void connect_now(void){last_attempt=esp_timer_get_time();esp_err_t e=esp_wifi_connect();if(e!=ESP_OK&&e!=ESP_ERR_WIFI_CONN)printf("BENCH WIFI_CONNECT_ERROR code=%d\n",e);}
static void event(void *arg,esp_event_base_t base,int32_t id,void *data){
    if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_START){started=true;if(has_credentials)connect_now();}
    if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED){
        wifi_event_sta_disconnected_t *d=data;car_wifi_status_t previous;car_wifi_status(&previous);if(previous.connected)motor_stop_reason("wifi_lost",true);
        xSemaphoreTake(mutex,portMAX_DELAY);state.connected=false;state.reason=d->reason;state.ip[0]=0;xSemaphoreGive(mutex);
        last_attempt=esp_timer_get_time();printf("BENCH WIFI_DISCONNECTED reason=%d retry_in_ms=3000\n",d->reason);
    }
    if(base==WIFI_EVENT&&id==WIFI_EVENT_SCAN_DONE){
        wifi_event_sta_scan_done_t *done=data;uint16_t count=24;
        wifi_ap_record_t *records=calloc(count,sizeof(*records));
        esp_err_t e=records?esp_wifi_scan_get_ap_records(&count,records):ESP_ERR_NO_MEM;
        xSemaphoreTake(mutex,portMAX_DELAY);scan_count=0;
        if(e==ESP_OK&&done->status==0){for(unsigned i=0;i<count;i++){
            if(!records[i].ssid[0])continue;
            bool duplicate=false;
            for(unsigned j=0;j<scan_count;j++)if(strcmp(scan_networks[j].ssid,(char*)records[i].ssid)==0)duplicate=true;
            if(duplicate)continue;
            car_wifi_network_t *item=&scan_networks[scan_count++];snprintf(item->ssid,sizeof(item->ssid),"%.32s",records[i].ssid);
            item->rssi=records[i].rssi;item->secure=records[i].authmode!=WIFI_AUTH_OPEN;
        }}
        scan_error=e!=ESP_OK?e:done->status;scan_ready=scan_error==0;scan_running=false;
        xSemaphoreGive(mutex);free(records);
    }
    if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP){
        ip_event_got_ip_t *d=data;xSemaphoreTake(mutex,portMAX_DELAY);state.connected=true;state.reason=0;snprintf(state.ip,sizeof(state.ip),IPSTR,IP2STR(&d->ip_info.ip));xSemaphoreGive(mutex);
        printf("BENCH WIFI_CONNECTED ip=" IPSTR "\n",IP2STR(&d->ip_info.ip));
    }
}
esp_err_t car_wifi_init(void){
    mutex=xSemaphoreCreateMutex();if(!mutex)return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif=esp_netif_create_default_wifi_sta();if(!netif)return ESP_FAIL;esp_netif_set_hostname(netif,"ai-car");if(!esp_netif_create_default_wifi_ap())return ESP_FAIL;
    wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();esp_err_t e=esp_wifi_init(&cfg);if(e!=ESP_OK)return e;
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,NULL));ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event,NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    wifi_config_t saved={0};if(esp_wifi_get_config(WIFI_IF_STA,&saved)==ESP_OK){snprintf(state.ssid,sizeof(state.ssid),"%.32s",saved.sta.ssid);has_credentials=saved.sta.ssid[0]!=0;}memset(&saved,0,sizeof(saved));
    uint8_t mac[6];ESP_ERROR_CHECK(esp_read_mac(mac,ESP_MAC_WIFI_STA));wifi_config_t ap={0};
    snprintf((char*)ap.ap.ssid,sizeof(ap.ap.ssid),"AI-Car-%02X%02X%02X",mac[3],mac[4],mac[5]);
    snprintf((char*)ap.ap.password,sizeof(ap.ap.password),"car-%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    ap.ap.ssid_len=strlen((char*)ap.ap.ssid);ap.ap.channel=1;ap.ap.max_connection=4;ap.ap.authmode=WIFI_AUTH_WPA2_PSK;
    snprintf(state.ap_ssid,sizeof(state.ap_ssid),"%s",ap.ap.ssid);ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,&ap));memset(&ap,0,sizeof(ap));
    printf("BENCH AP_READY ssid=%s ip=192.168.4.1\n",state.ap_ssid);
    snprintf(hostname,sizeof(hostname),"ai-car-%02x%02x%02x",mac[3],mac[4],mac[5]);
    ESP_ERROR_CHECK(esp_netif_set_hostname(netif,hostname));ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(hostname));ESP_ERROR_CHECK(mdns_instance_name_set("AI Car"));
    ESP_ERROR_CHECK(mdns_service_add(NULL,"_http","_tcp",80,NULL,0));
    ESP_ERROR_CHECK(esp_wifi_start());return esp_wifi_set_ps(WIFI_PS_NONE);
}
void car_wifi_tick(void){car_wifi_status_t s;car_wifi_status(&s);if(started&&has_credentials&&!scan_running&&!s.connected&&esp_timer_get_time()-last_attempt>3000000)connect_now();}
void car_wifi_status(car_wifi_status_t *out){xSemaphoreTake(mutex,portMAX_DELAY);*out=state;xSemaphoreGive(mutex);wifi_sta_list_t clients={0};if(esp_wifi_ap_get_sta_list(&clients)==ESP_OK)out->ap_clients=clients.num;if(out->connected){wifi_ap_record_t ap;if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK)out->rssi=ap.rssi;}}
esp_err_t car_wifi_configure(const char *ssid,const char *password){
    if(!ssid||!password||!strlen(ssid)||strlen(ssid)>32||strlen(password)>63)return ESP_ERR_INVALID_ARG;
    motor_stop_reason("wifi_change",true);wifi_config_t cfg={0};memcpy(cfg.sta.ssid,ssid,strlen(ssid));memcpy(cfg.sta.password,password,strlen(password));
    xSemaphoreTake(mutex,portMAX_DELAY);state.connected=false;state.ip[0]=0;state.reason=0;xSemaphoreGive(mutex);
    esp_wifi_disconnect();esp_err_t e=esp_wifi_set_config(WIFI_IF_STA,&cfg);memset(&cfg,0,sizeof(cfg));
    if(e==ESP_OK){has_credentials=true;xSemaphoreTake(mutex,portMAX_DELAY);snprintf(state.ssid,sizeof(state.ssid),"%s",ssid);state.wifi_revision++;xSemaphoreGive(mutex);last_attempt=esp_timer_get_time()-2500000;}return e;
}

esp_err_t car_wifi_scan_start(void){
    xSemaphoreTake(mutex,portMAX_DELAY);if(scan_running){xSemaphoreGive(mutex);return ESP_OK;}
    scan_running=true;scan_ready=false;scan_error=0;xSemaphoreGive(mutex);
    wifi_scan_config_t config={.show_hidden=false,.scan_type=WIFI_SCAN_TYPE_ACTIVE};
    esp_err_t e=esp_wifi_scan_start(&config,false);
    if(e!=ESP_OK){xSemaphoreTake(mutex,portMAX_DELAY);scan_running=false;scan_error=e;xSemaphoreGive(mutex);}return e;
}
unsigned car_wifi_scan_result(car_wifi_network_t *out,unsigned capacity,bool *scanning,bool *ready,int *error){
    xSemaphoreTake(mutex,portMAX_DELAY);unsigned count=scan_count<capacity?scan_count:capacity;
    memcpy(out,scan_networks,count*sizeof(*out));*scanning=scan_running;*ready=scan_ready;*error=scan_error;xSemaphoreGive(mutex);return count;
}
