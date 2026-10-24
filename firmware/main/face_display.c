#include "face_display.h"
#include "motor_bench.h"
#include "wifi_manager.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static i2c_master_dev_handle_t panel;
static uint8_t frame[1024];
static face_display_status_t health;
static portMUX_TYPE health_lock=portMUX_INITIALIZER_UNLOCKED;
void face_display_status(face_display_status_t *out){portENTER_CRITICAL(&health_lock);*out=health;portEXIT_CRITICAL(&health_lock);}
static esp_err_t send(uint8_t control,const uint8_t *data,size_t len){
    uint8_t packet[129];if(len>128)return ESP_ERR_INVALID_SIZE;
    packet[0]=control;memcpy(packet+1,data,len);
    return i2c_master_transmit(panel,packet,len+1,150);
}
static esp_err_t flush(uint8_t contrast,bool set_contrast){
    esp_err_t err=ESP_OK;
    if(set_contrast){const uint8_t command[]={0x81,contrast};err=send(0,command,sizeof(command));}
    const uint8_t range[]={0x21,0,127,0x22,0,7};
    if(err==ESP_OK)err=send(0,range,sizeof(range));
    for(int page=0;page<8&&err==ESP_OK;page++)err=send(0x40,frame+page*128,128);
    return err;
}
static bool is_control_loss(const char *reason){return strcmp(reason,"heartbeat_timeout")==0||strcmp(reason,"controller_disconnected")==0||strcmp(reason,"controller_timeout")==0;}
static void display_task(void *arg){
    (void)arg;
    face_engine_t engine;uint32_t now=(uint32_t)(esp_timer_get_time()/1000);face_engine_init(&engine,now);
    TickType_t wake=xTaskGetTickCount();uint8_t contrast=0;bool first=true;uint32_t failures=0;
    while(1){
        int64_t started=esp_timer_get_time();now=(uint32_t)(started/1000);
        motor_status_t m;motor_get_status(&m);car_wifi_status_t w;car_wifi_status(&w);
        face_input_t in={.connected=w.connected||w.ap_clients>0,.active=m.active,.testing=strcmp(m.mode,"test")==0,
            .locked=m.locked,.fault=m.fault,.control_lost=is_control_loss(m.reason),
            .throttle=m.throttle,.steering=m.steering,.interaction_ms=m.interaction_ms,.reason_at_ms=m.reason_at_ms};
        snprintf(in.ip,sizeof(in.ip),"%s",w.ip);
        face_engine_step(&engine,&in,now);face_render(frame,&engine.pose,&in);
        // No motor/state lock is held while the display driver waits for I2C.
        esp_err_t err=flush(engine.pose.contrast,first||contrast!=engine.pose.contrast);
        if(err==ESP_OK){contrast=engine.pose.contrast;first=false;failures=0;}
        else {failures++;first=true;}
        portENTER_CRITICAL(&health_lock);
        health.kind=engine.pose.kind;health.last_error=err;health.healthy=err==ESP_OK;health.consecutive_errors=failures;
        health.frame_ms=(uint32_t)((esp_timer_get_time()-started)/1000);
        if(err==ESP_OK){health.frames++;health.last_success_ms=(uint32_t)(esp_timer_get_time()/1000);}else health.total_errors++;
        portEXIT_CRITICAL(&health_lock);
        uint32_t stack_free=(uint32_t)uxTaskGetStackHighWaterMark(NULL);
        portENTER_CRITICAL(&health_lock);health.stack_free=stack_free;portEXIT_CRITICAL(&health_lock);
        if(err!=ESP_OK&&(failures==1||failures==3||failures%30==0))printf("BENCH FACE_ERROR result=%s consecutive=%lu\n",esp_err_to_name(err),(unsigned long)failures);
        if(failures>=3){vTaskDelay(pdMS_TO_TICKS(2000));wake=xTaskGetTickCount();}
        else {
            if(xTaskGetTickCount()-wake>=pdMS_TO_TICKS(125))wake=xTaskGetTickCount();
            vTaskDelayUntil(&wake,pdMS_TO_TICKS(125));
        }
    }
}
esp_err_t face_display_start(i2c_master_dev_handle_t oled){
    if(!oled)return ESP_ERR_INVALID_ARG;
    panel=oled;
    const uint8_t init[]={0xae,0xd5,0x80,0xa8,0x3f,0xd3,0,0x40,0x8d,0x14,0x20,0,0xa1,0xc8,0xda,0x12,0x81,0x7f,0xd9,0xf1,0xdb,0x40,0xa4,0xa6,0xaf};
    esp_err_t err=send(0,init,sizeof(init));
    if(err==ESP_OK){
        portENTER_CRITICAL(&health_lock);health.running=true;portEXIT_CRITICAL(&health_lock);
        if(xTaskCreate(display_task,"cartoon_face",4096,NULL,2,NULL)!=pdPASS)err=ESP_ERR_NO_MEM;
    }
    if(err!=ESP_OK){portENTER_CRITICAL(&health_lock);health.running=false;health.last_error=err;portEXIT_CRITICAL(&health_lock);}
    return err;
}
