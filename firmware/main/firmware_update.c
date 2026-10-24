#include "firmware_update.h"
#include "motor_bench.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static void reboot(void *arg){(void)arg;vTaskDelay(pdMS_TO_TICKS(1200));esp_restart();}
esp_err_t firmware_update_upload(httpd_req_t *req){
    const esp_partition_t *next=esp_ota_get_next_update_partition(NULL);
    if(!next||req->content_len<512||req->content_len>next->size)
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid firmware size");
    if(!motor_maintenance_begin())return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Stop motors and clear lock before updating");
    uint8_t *buffer=malloc(4096);esp_ota_handle_t handle=0;bool begun=false;esp_err_t err=ESP_ERR_NO_MEM;
    if(!buffer)goto fail;
    // Check the project descriptor before erasing the inactive slot.
    int first=0;const int header_size=sizeof(esp_image_header_t)+sizeof(esp_image_segment_header_t)+sizeof(esp_app_desc_t);
    while(first<header_size){int n=httpd_req_recv(req,(char*)buffer+first,header_size-first);if(n<=0){err=ESP_FAIL;goto fail;}first+=n;}
    esp_app_desc_t desc;memcpy(&desc,buffer+sizeof(esp_image_header_t)+sizeof(esp_image_segment_header_t),sizeof(desc));
    if(buffer[0]!=ESP_IMAGE_HEADER_MAGIC||desc.magic_word!=ESP_APP_DESC_MAGIC_WORD||strncmp(desc.project_name,"ai_car_bench",sizeof(desc.project_name))){err=ESP_ERR_INVALID_ARG;goto fail;}
    err=esp_ota_begin(next,req->content_len,&handle);if(err!=ESP_OK)goto fail;begun=true;
    err=esp_ota_write(handle,buffer,first);if(err!=ESP_OK)goto fail;
    int remaining=req->content_len-first;
    while(remaining>0){int n=httpd_req_recv(req,(char*)buffer,remaining>4096?4096:remaining);if(n<=0){err=ESP_FAIL;goto fail;}err=esp_ota_write(handle,buffer,n);if(err!=ESP_OK)goto fail;remaining-=n;}
    err=esp_ota_end(handle);begun=false;if(err!=ESP_OK)goto fail;
    err=esp_ota_set_boot_partition(next);if(err!=ESP_OK)goto fail;
    free(buffer);httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t sent=httpd_resp_sendstr(req,"{\"ok\":true,\"rebooting\":true}");
    if(xTaskCreate(reboot,"ota_reboot",2048,NULL,2,NULL)!=pdPASS)esp_restart();
    return sent;
fail:
    if(begun)esp_ota_abort(handle);
    free(buffer);motor_maintenance_end();
    return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Update rejected or interrupted; current firmware retained");
}
