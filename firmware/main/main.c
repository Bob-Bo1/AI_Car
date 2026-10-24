#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "motor_bench.h"
#include "wifi_manager.h"
#include "web_console.h"
#include "face_display.h"
#include "device_discovery.h"

static i2c_master_dev_handle_t oled;
bool car_oled_present(void){return oled!=NULL;}
static void console(void *arg) {
    char line[512]; int pos=0;
    while(1) {
        int ch=getchar();
        if(ch==EOF){clearerr(stdin);vTaskDelay(pdMS_TO_TICKS(20));continue;}
        if(ch=='\r') continue;
        if(ch!='\n'){if(pos<(int)sizeof(line)-1) line[pos++]=(char)ch;else pos=0;continue;}
        line[pos]=0;pos=0;
        if(strcmp(line,"STATUS")==0) { car_wifi_status_t w;car_wifi_status(&w);printf("BENCH STATUS wifi=%s ip=%s oled=%s motor=%s version=0.7.0 reset=%d\n",w.connected?"connected":"offline",w.ip,oled?"present":"missing",motor_bench_active()?"running":"stopped",esp_reset_reason());continue; }
        if(strcmp(line,"STOP")==0){motor_bench_stop();continue;}
        if(strncmp(line,"MOTOR_START_TEST ",17)==0 &&
           (strcmp(line,"MOTOR_START_TEST A FWD")==0 || strcmp(line,"MOTOR_START_TEST A REV")==0 ||
            strcmp(line,"MOTOR_START_TEST B FWD")==0 || strcmp(line,"MOTOR_START_TEST B REV")==0)){
            int motor=line[17]=='A'?0:1;
            int direction=strstr(line," FWD")?1:-1;
            if(!motor_bench_start_test_motor(motor,direction,300))printf("BENCH MOTOR_REJECT reason=start_test_queue_or_fault\n");
            continue;
        }
        if(strncmp(line,"MOTOR_START_TEST ",17)==0 && (line[17]=='A' || line[17]=='B')){
            char motor,dir[4]={0},extra;unsigned ms;
            int n=sscanf(line,"MOTOR_START_TEST %c %3s %u %c",&motor,dir,&ms,&extra);
            int direction=strcmp(dir,"FWD")==0?1:strcmp(dir,"REV")==0?-1:0;
            if(n!=3 || !motor_bench_start_test_motor(motor-'A',direction,ms))printf("BENCH MOTOR_REJECT reason=invalid_start_test\n");
            continue;
        }
        if(strcmp(line,"MOTOR_START_TEST FWD")==0 || strcmp(line,"MOTOR_START_TEST REV")==0){
            int direction=strstr(line," FWD")?1:-1;
            if(!motor_bench_start_test(direction))printf("BENCH MOTOR_REJECT reason=start_test_queue_or_fault\n");
            continue;
        }
        if(strcmp(line,"MEASURE_MOTOR_DISCONNECTED FWD")==0 || strcmp(line,"MEASURE_MOTOR_DISCONNECTED REV")==0){
            int direction=strstr(line," FWD")?1:-1;
            if(!motor_bench_measure(direction))printf("BENCH MOTOR_REJECT reason=measurement_queue_or_fault\n");
            continue;
        }
        if(strncmp(line,"MOTOR ",6)==0){
            char dir[4]={0},extra;unsigned duty,ms;
            int n=sscanf(line,"MOTOR %3s %u %u %c",dir,&duty,&ms,&extra);
            int sign=strcmp(dir,"FWD")==0?1:strcmp(dir,"REV")==0?-1:0;
            if(n!=3 || !motor_bench_run(sign,duty,ms))printf("BENCH MOTOR_REJECT reason=invalid_or_queue_full\n");
            continue;
        }
        if(strcmp(line,"SCAN")==0) {
            if(esp_wifi_scan_start(NULL,true)==ESP_OK){
                wifi_ap_record_t aps[16];uint16_t n=16;
                if(esp_wifi_scan_get_ap_records(&n,aps)==ESP_OK)
                    for(int i=0;i<n;i++)printf("BENCH NETWORK ssid=%s rssi=%d channel=%u\n",aps[i].ssid,aps[i].rssi,aps[i].primary);
            }else printf("BENCH SCAN_BUSY\n");
            continue;
        }
        if(strcmp(line,"REBOOT")==0) {motor_bench_stop();fflush(stdout);vTaskDelay(pdMS_TO_TICKS(100));esp_restart();}
        cJSON *root=cJSON_Parse(line);
        cJSON *ssid=cJSON_GetObjectItem(root,"ssid"),*pass=cJSON_GetObjectItem(root,"password");
        if(cJSON_IsString(ssid)&&cJSON_IsString(pass)&&strlen(ssid->valuestring)>0&&strlen(ssid->valuestring)<=32&&strlen(pass->valuestring)<=63) {
            esp_err_t err=car_wifi_configure(ssid->valuestring,pass->valuestring);
            printf(err==ESP_OK?"BENCH CONFIG_ACCEPTED\n":"BENCH CONFIG_ERROR\n");
        } else printf("BENCH COMMAND_ERROR\n");
        cJSON_Delete(root);memset(line,0,sizeof(line));
    }
}
void app_main(void) {
    const int motor[]={4,5,20,21};
    for(int i=0;i<4;i++){gpio_set_level(motor[i],0);gpio_set_direction(motor[i],GPIO_MODE_OUTPUT);}
    printf("BENCH BOOT version=0.7.0 reset=%d motors=idle\n",esp_reset_reason());
    ESP_ERROR_CHECK(motor_bench_init());
    esp_err_t err=nvs_flash_init();
    if(err!=ESP_OK){printf("BENCH NVS_ERROR code=%d\n",err);return;}
    ESP_ERROR_CHECK(motor_config_load());
    ESP_ERROR_CHECK(car_wifi_init());
    i2c_master_bus_config_t buscfg={.i2c_port=I2C_NUM_0,.sda_io_num=6,.scl_io_num=7,.clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=true};
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&buscfg,&bus));
    for(int a=0x08;a<0x78;a++) if(i2c_master_probe(bus,a,30)==ESP_OK){
        printf("BENCH I2C_FOUND address=0x%02X\n",a);
        if((a==0x3c||a==0x3d)&&!oled){
            i2c_device_config_t cfg={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=a,.scl_speed_hz=100000};
            ESP_ERROR_CHECK(i2c_master_bus_add_device(bus,&cfg,&oled));
        }
    }
    if(oled){
        err=face_display_start(oled);
        printf("BENCH OLED_INIT result=%s\n",esp_err_to_name(err));
    }else printf("BENCH OLED_MISSING\n");
    ESP_ERROR_CHECK(car_discovery_start());
    ESP_ERROR_CHECK(web_console_start());
    esp_ota_img_states_t ota_state;
    bool ota_pending=esp_ota_get_state_partition(esp_ota_get_running_partition(),&ota_state)==ESP_OK&&ota_state==ESP_OTA_IMG_PENDING_VERIFY;
    xTaskCreate(console,"console",6144,NULL,5,NULL);
    int count=0;
    while(1){
        car_wifi_tick();car_wifi_status_t w;car_wifi_status(&w);
        if(count%5==0){
            face_display_status_t f;face_display_status(&f);
            printf("BENCH HEALTH uptime=%d wifi=%s ip=%s oled_write=%s heap=%lu face=%s frames=%lu frame_ms=%lu stack_free=%lu errors=%lu\n",count,w.connected?"connected":"offline",w.ip,esp_err_to_name(f.last_error),(unsigned long)esp_get_free_heap_size(),face_kind_name(f.kind),(unsigned long)f.frames,(unsigned long)f.frame_ms,(unsigned long)f.stack_free,(unsigned long)f.total_errors);
        }
        if(ota_pending&&count>=10){ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());ota_pending=false;printf("BENCH OTA_VALIDATED\n");}
        count++;vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

