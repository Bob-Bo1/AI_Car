#include "device_discovery.h"
#include "wifi_manager.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static char identity[13];
const char *car_device_id(void){return identity;}
static void discovery_task(void *arg){
    (void)arg;
    while(1){
        int fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if(fd<0){vTaskDelay(pdMS_TO_TICKS(2000));continue;}
        struct timeval timeout={.tv_sec=1};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
        struct sockaddr_in local={.sin_family=AF_INET,.sin_port=htons(37020),.sin_addr.s_addr=htonl(INADDR_ANY)};
        if(bind(fd,(struct sockaddr*)&local,sizeof(local))<0){close(fd);vTaskDelay(pdMS_TO_TICKS(2000));continue;}
        while(1){
            char query[64];struct sockaddr_in remote;socklen_t length=sizeof(remote);
            int n=recvfrom(fd,query,sizeof(query),0,(struct sockaddr*)&remote,&length);
            if(n<0)continue;
            static const char request[]="AI_CAR_DISCOVER_V1";
            if(n!=(int)sizeof(request)-1||memcmp(query,request,sizeof(request)-1)!=0)continue;
            car_wifi_status_t wifi;car_wifi_status(&wifi);if(!wifi.connected)continue;
            char reply[180];int size=snprintf(reply,sizeof(reply),"{\"type\":\"ai_car\",\"protocol\":1,\"device_id\":\"%s\",\"ip\":\"%s\",\"port\":80}",identity,wifi.ip);
            if(size>0&&size<(int)sizeof(reply))sendto(fd,reply,(size_t)size,0,(struct sockaddr*)&remote,length);
        }
    }
}
esp_err_t car_discovery_start(void){
    uint8_t mac[6];esp_err_t err=esp_read_mac(mac,ESP_MAC_WIFI_STA);if(err!=ESP_OK)return err;
    snprintf(identity,sizeof(identity),"%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    return xTaskCreate(discovery_task,"car_discovery",3072,NULL,1,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
