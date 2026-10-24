#include "web_console.h"
#include "motor_bench.h"
#include "wifi_manager.h"
#include "face_display.h"
#include "device_discovery.h"
#include "firmware_update.h"
#include "esp_ota_ops.h"
#include "lwip/sockets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "cJSON.h"

extern bool car_oled_present(void);
extern const unsigned char setup_start[] asm("_binary_setup_html_start");
extern const unsigned char setup_end[] asm("_binary_setup_html_end");
extern const unsigned char console_start[] asm("_binary_console_html_start");
extern const unsigned char console_end[] asm("_binary_console_html_end");
typedef struct {char token[17];uint32_t seq;} session_t;
static int owner=-1;
static uint32_t owner_generation;
static int64_t owner_seen;
static httpd_handle_t server;

static void expire_owner(void){
    motor_status_t s;motor_get_status(&s);
    if(owner!=-1&&(owner_generation!=s.generation||esp_timer_get_time()-owner_seen>2000000)){
        motor_stop_reason(owner_generation!=s.generation?s.reason:"controller_timeout",true);owner=-1;
    }
}
bool web_console_owned(void){return owner!=-1;}
static cJSON *status_json(int fd){
    expire_owner();motor_status_t m;motor_get_status(&m);car_wifi_status_t w;car_wifi_status(&w);
    cJSON *j=cJSON_CreateObject();
    cJSON_AddStringToObject(j,"type","state");cJSON_AddStringToObject(j,"version","0.7.0");
    cJSON_AddStringToObject(j,"device_id",car_device_id());
    cJSON_AddBoolToObject(j,"ota_supported",true);
    cJSON_AddStringToObject(j,"running_partition",esp_ota_get_running_partition()->label);
    cJSON_AddNumberToObject(j,"reset_reason",esp_reset_reason());
    cJSON_AddNumberToObject(j,"heap_free",esp_get_free_heap_size());
    cJSON_AddNumberToObject(j,"ap_clients",w.ap_clients);cJSON_AddStringToObject(j,"hostname",car_hostname());cJSON_AddStringToObject(j,"ap_ssid",w.ap_ssid);cJSON_AddStringToObject(j,"ap_ip","192.168.4.1");
    cJSON_AddNumberToObject(j,"wifi_revision",w.wifi_revision);cJSON_AddBoolToObject(j,"connected",w.connected);cJSON_AddStringToObject(j,"ssid",w.ssid);cJSON_AddStringToObject(j,"ip",w.ip);cJSON_AddNumberToObject(j,"rssi",w.rssi);cJSON_AddNumberToObject(j,"wifi_reason",w.reason);
    cJSON_AddBoolToObject(j,"oled_detected",car_oled_present());cJSON_AddBoolToObject(j,"motor_control",true);
    face_display_status_t face;face_display_status(&face);
    cJSON_AddStringToObject(j,"face_state",face_kind_name(face.kind));
    cJSON_AddBoolToObject(j,"oled_running",face.running);cJSON_AddBoolToObject(j,"oled_healthy",face.healthy);
    cJSON_AddNumberToObject(j,"oled_frames",face.frames);cJSON_AddNumberToObject(j,"oled_errors",face.total_errors);
    cJSON_AddNumberToObject(j,"oled_frame_ms",face.frame_ms);cJSON_AddNumberToObject(j,"oled_stack_free",face.stack_free);
    cJSON_AddStringToObject(j,"oled_last_error",esp_err_to_name(face.last_error));
    cJSON_AddNumberToObject(j,"oled_last_success_age_ms",face.frames?(double)((uint32_t)(esp_timer_get_time()/1000)-face.last_success_ms):-1);
    cJSON_AddNumberToObject(j,"throttle",m.throttle);cJSON_AddNumberToObject(j,"steering",m.steering);
    cJSON_AddNumberToObject(j,"idle_ms",(uint32_t)(esp_timer_get_time()/1000)-m.interaction_ms);
    cJSON_AddBoolToObject(j,"active",m.active);cJSON_AddBoolToObject(j,"locked",m.locked);cJSON_AddBoolToObject(j,"fault",m.fault);
    cJSON_AddBoolToObject(j,"claimed",owner!=-1);cJSON_AddBoolToObject(j,"you_control",fd>=0&&owner==fd);
    cJSON_AddNumberToObject(j,"target_a",m.target_a);cJSON_AddNumberToObject(j,"target_b",m.target_b);cJSON_AddNumberToObject(j,"output_a",m.output_a);cJSON_AddNumberToObject(j,"output_b",m.output_b);
    cJSON_AddBoolToObject(j,"invert_a",m.invert_a);cJSON_AddBoolToObject(j,"invert_b",m.invert_b);cJSON_AddBoolToObject(j,"swap_ab",m.swap_ab);
    cJSON_AddBoolToObject(j,"reverse_drive",m.reverse_drive);
    cJSON_AddNumberToObject(j,"generation",m.generation);cJSON_AddNumberToObject(j,"command_id",m.command_id);cJSON_AddStringToObject(j,"mode",m.mode);cJSON_AddStringToObject(j,"reason",m.reason);
    cJSON_AddNumberToObject(j,"stop_elapsed_ms",m.stop_elapsed_ms);cJSON_AddNumberToObject(j,"watchdog_ms",450);cJSON_AddNumberToObject(j,"uptime_ms",esp_timer_get_time()/1000);return j;
}
static esp_err_t send_state(httpd_req_t *req,const char *error,bool hello){
    cJSON *j=status_json(httpd_req_to_sockfd(req));if(!j)return ESP_ERR_NO_MEM;
    if(error)cJSON_AddStringToObject(j,"error",error);
    if(hello){session_t *s=httpd_sess_get_ctx(req->handle,httpd_req_to_sockfd(req));if(s)cJSON_AddStringToObject(j,"token",s->token);}
    char *text=cJSON_PrintUnformatted(j);cJSON_Delete(j);if(!text)return ESP_ERR_NO_MEM;
    httpd_ws_frame_t frame={.type=HTTPD_WS_TYPE_TEXT,.payload=(uint8_t*)text,.len=strlen(text)};
    esp_err_t e=httpd_ws_send_frame(req,&frame);free(text);return e;
}
static bool origin_ok(httpd_req_t *req){
    size_t len=httpd_req_get_hdr_value_len(req,"Origin");if(!len)return true;
    if(len>95)return false;
    char origin[96],host[72],expected[96];
    if(httpd_req_get_hdr_value_str(req,"Origin",origin,sizeof(origin))!=ESP_OK||httpd_req_get_hdr_value_str(req,"Host",host,sizeof(host))!=ESP_OK)return false;
    snprintf(expected,sizeof(expected),"http://%s",host);return strcmp(expected,origin)==0;
}
static bool integer(cJSON *j,const char *key,int low,int high,int *out){
    cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<low||v->valuedouble>high||floor(v->valuedouble)!=v->valuedouble)return false;
    *out=(int)v->valuedouble;return true;
}
static esp_err_t websocket(httpd_req_t *req){
    int fd=httpd_req_to_sockfd(req);
    if(req->method==HTTP_GET){
        if(!origin_ok(req))return ESP_FAIL;
        session_t *session=calloc(1,sizeof(*session));if(!session)return ESP_ERR_NO_MEM;
        snprintf(session->token,sizeof(session->token),"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());
        httpd_sess_set_ctx(req->handle,fd,session,free);return ESP_OK;
    }
    httpd_ws_frame_t frame={0};esp_err_t e=httpd_ws_recv_frame(req,&frame,0);if(e!=ESP_OK)return e;
    if(frame.len>512||frame.type!=HTTPD_WS_TYPE_TEXT)return ESP_FAIL;
    uint8_t payload[513]={0};frame.payload=payload;e=httpd_ws_recv_frame(req,&frame,frame.len);if(e!=ESP_OK)return e;
    if(memchr(payload,0,frame.len))return ESP_FAIL;
    cJSON *j=cJSON_ParseWithLengthOpts((char*)payload,frame.len+1,NULL,true);if(!j)return send_state(req,"指令格式错误",false);
    const char *error=NULL;cJSON *t=cJSON_GetObjectItemCaseSensitive(j,"type");const char *type=cJSON_IsString(t)?t->valuestring:"";
    if(strcmp(type,"hello")==0){cJSON_Delete(j);return send_state(req,NULL,true);}
    session_t *session=httpd_sess_get_ctx(req->handle,fd);cJSON *token=cJSON_GetObjectItemCaseSensitive(j,"token");int seq=0;
    if(!session||!cJSON_IsString(token)||strcmp(token->valuestring,session->token)!=0||!integer(j,"seq",1,2147483647,&seq)||(uint32_t)seq<=session->seq){cJSON_Delete(j);return send_state(req,"指令已过期或会话无效",false);}
    session->seq=(uint32_t)seq;expire_owner();
    if(strcmp(type,"claim")==0){
        if(owner!=-1&&owner!=fd&&!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j,"takeover")))error="另一台设备正在控制，请重新操作接管";
        else {if(owner!=fd){motor_stop_reason("control_taken_over",true);owner=-1;}motor_status_t s;motor_get_status(&s);owner=fd;owner_generation=s.generation;owner_seen=esp_timer_get_time();motor_note_interaction();}
    }else if(strcmp(type,"estop")==0){motor_emergency_stop();owner=-1;}
    else if(strcmp(type,"status")==0||strcmp(type,"ping")==0){if(owner==fd)owner_seen=esp_timer_get_time();}
    else if(owner!=fd)error="请先取得控制权";
    else{
        owner_seen=esp_timer_get_time();
        if(strcmp(type,"release")==0){motor_stop_reason("released",true);owner=-1;}
        else if(strcmp(type,"stop")==0)motor_stop_reason("released_control",false);
        else if(strcmp(type,"unlock")==0){if(!motor_unlock())error="故障未解除或电机尚未停止";}
        else if(strcmp(type,"drive")==0){
            cJSON *a=cJSON_GetObjectItemCaseSensitive(j,"action");int speed,throttle,steering;bool ok=false;
            if(integer(j,"speed",50,100,&speed)){
                if(cJSON_HasObjectItem(j,"throttle")||cJSON_HasObjectItem(j,"steering"))
                    ok=integer(j,"throttle",-100,100,&throttle)&&integer(j,"steering",-100,100,&steering)&&motor_drive_axes(throttle,steering,(unsigned)speed);
                else if(cJSON_IsString(a))ok=motor_drive(a->valuestring,(unsigned)speed);
            }
            if(!ok)error="行驶指令无效、正在测试或已急停";
        }else if(strcmp(type,"test")==0){
            int motor,dir,ms;if(!integer(j,"motor",0,1,&motor)||!integer(j,"direction",-1,1,&dir)||!integer(j,"duration_ms",300,2000,&ms)||!motor_network_test(motor,dir,(unsigned)ms))error="测试未启动：请停车、解除急停并检查参数";
        }else if(strcmp(type,"keep")==0){int id;if(integer(j,"command_id",1,2147483647,&id))motor_network_keep((uint32_t)id);else error="测试编号无效";}
        else if(strcmp(type,"config")==0){
            cJSON *a=cJSON_GetObjectItemCaseSensitive(j,"invert_a"),*b=cJSON_GetObjectItemCaseSensitive(j,"invert_b"),*swap=cJSON_GetObjectItemCaseSensitive(j,"swap_ab");
            cJSON *reverse=cJSON_GetObjectItemCaseSensitive(j,"reverse_drive");motor_status_t current;motor_get_status(&current);
            if(!cJSON_IsBool(a)||!cJSON_IsBool(b)||!cJSON_IsBool(swap)||(reverse&&!cJSON_IsBool(reverse))||!motor_config_save(cJSON_IsTrue(a),cJSON_IsTrue(b),cJSON_IsTrue(swap),reverse?cJSON_IsTrue(reverse):current.reverse_drive))error="设置未保存，请先停车";
        }else if(strcmp(type,"wifi")==0){
            cJSON *ssid=cJSON_GetObjectItemCaseSensitive(j,"ssid"),*password=cJSON_GetObjectItemCaseSensitive(j,"password");
            if(motor_bench_active()||!cJSON_IsString(ssid)||!cJSON_IsString(password)||car_wifi_configure(ssid->valuestring,password->valuestring)!=ESP_OK)error="Wi-Fi未保存，请先停车并检查输入";
        }else error="不支持的指令";
    }
    cJSON_Delete(j);return send_state(req,error,false);
}
static void close_session(httpd_handle_t handle,int fd){if(owner==fd){motor_stop_reason("controller_disconnected",true);owner=-1;}close(fd);}
static esp_err_t upload(httpd_req_t *req){
    if(!httpd_req_get_hdr_value_len(req,"Origin")||!origin_ok(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Origin rejected");
    expire_owner();char token[32];session_t *session=owner<0?NULL:httpd_sess_get_ctx(server,owner);
    if(!session||httpd_req_get_hdr_value_str(req,"X-Car-Token",token,sizeof(token))!=ESP_OK||strcmp(token,session->token))
        return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Claim control before updating");
    esp_err_t result=firmware_update_upload(req);owner=-1;return result;
}
static esp_err_t scan_wifi(httpd_req_t *req){
    if(motor_bench_active())return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Stop motors before scanning");
    char query[64]={0};httpd_req_get_url_query_str(req,query,sizeof(query));
    if(strstr(query,"refresh=1"))car_wifi_scan_start();
    car_wifi_network_t items[24];bool scanning,ready;int error;
    unsigned count=car_wifi_scan_result(items,24,&scanning,&ready,&error);
    cJSON *j=cJSON_CreateObject(),*list=cJSON_AddArrayToObject(j,"networks");
    cJSON_AddBoolToObject(j,"scanning",scanning);cJSON_AddBoolToObject(j,"ready",ready);cJSON_AddNumberToObject(j,"error",error);
    for(unsigned i=0;i<count;i++){cJSON *n=cJSON_CreateObject();cJSON_AddStringToObject(n,"ssid",items[i].ssid);cJSON_AddNumberToObject(n,"rssi",items[i].rssi);cJSON_AddBoolToObject(n,"secure",items[i].secure);cJSON_AddItemToArray(list,n);}
    char *body=cJSON_PrintUnformatted(j);cJSON_Delete(j);if(!body)return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");esp_err_t e=httpd_resp_sendstr(req,body);free(body);return e;
}
static esp_err_t ready_page(httpd_req_t *req){
    char origin[96]={0};httpd_req_get_hdr_value_str(req,"Origin",origin,sizeof(origin));
    if(origin[0]&&strcmp(origin,"http://192.168.4.1")!=0&&!origin_ok(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Origin rejected");
    if(origin[0])httpd_resp_set_hdr(req,"Access-Control-Allow-Origin",origin);
    httpd_resp_set_hdr(req,"Vary","Origin");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    if(req->method==HTTP_OPTIONS){httpd_resp_set_hdr(req,"Access-Control-Allow-Methods","GET, OPTIONS");httpd_resp_set_hdr(req,"Access-Control-Allow-Private-Network","true");return httpd_resp_send(req,NULL,0);}
    struct sockaddr_storage peer={0};socklen_t size=sizeof(peer);uint32_t peer4=0;bool known=false;
    if(getpeername(httpd_req_to_sockfd(req),(struct sockaddr*)&peer,&size)==0){
        if(peer.ss_family==AF_INET){peer4=((struct sockaddr_in*)&peer)->sin_addr.s_addr;known=true;}
        else if(peer.ss_family==AF_INET6){
            static const unsigned char prefix[12]={0,0,0,0,0,0,0,0,0,0,255,255};
            const unsigned char *bytes=(const unsigned char*)&((struct sockaddr_in6*)&peer)->sin6_addr;
            if(memcmp(bytes,prefix,sizeof(prefix))==0){memcpy(&peer4,bytes+12,4);known=true;}
        }
    }
    bool via_ap=!known||(ntohl(peer4)&0xffffff00U)==0xc0a80400U;
    car_wifi_status_t w;car_wifi_status(&w);cJSON *j=cJSON_CreateObject();
    cJSON_AddStringToObject(j,"device_id",car_device_id());cJSON_AddBoolToObject(j,"connected",w.connected);cJSON_AddBoolToObject(j,"via_hotspot",via_ap);cJSON_AddStringToObject(j,"ip",w.ip);
    char *body=cJSON_PrintUnformatted(j);cJSON_Delete(j);if(!body)return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req,"application/json");esp_err_t e=httpd_resp_sendstr(req,body);free(body);return e;
}
static esp_err_t page(httpd_req_t *req){
    httpd_resp_set_type(req,"text/html; charset=utf-8");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    char host[80]={0},query[64]={0};httpd_req_get_hdr_value_str(req,"Host",host,sizeof(host));httpd_req_get_url_query_str(req,query,sizeof(query));
    bool setup=strncmp(req->uri,"/setup",6)==0||strstr(query,"setup=1")!=NULL||((strcmp(host,"192.168.4.1")==0||strcmp(host,"192.168.4.1:80")==0)&&strncmp(req->uri,"/control",8)!=0);
    httpd_resp_set_hdr(req,"Content-Security-Policy",setup?"default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self' ws: http:; img-src 'self' data:; frame-ancestors 'none'":"default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self' ws:; img-src 'self' data:; frame-ancestors 'none'");
    return setup?httpd_resp_send(req,(const char*)setup_start,setup_end-setup_start-1):httpd_resp_send(req,(const char*)console_start,console_end-console_start-1);
}
static esp_err_t status(httpd_req_t *req){cJSON *j=status_json(-1);char *text=cJSON_PrintUnformatted(j);cJSON_Delete(j);if(!text)return ESP_ERR_NO_MEM;httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");esp_err_t e=httpd_resp_sendstr(req,text);free(text);return e;}
esp_err_t web_console_start(void){
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();cfg.stack_size=8192;cfg.max_uri_handlers=12;cfg.max_open_sockets=6;cfg.lru_purge_enable=true;cfg.recv_wait_timeout=2;cfg.send_wait_timeout=2;cfg.close_fn=close_session;
    esp_err_t e=httpd_start(&server,&cfg);if(e!=ESP_OK)return e;
    httpd_uri_t setup={.uri="/setup",.method=HTTP_GET,.handler=page},control={.uri="/control",.method=HTTP_GET,.handler=page};
    httpd_uri_t scan={.uri="/api/wifi/scan",.method=HTTP_GET,.handler=scan_wifi},ready={.uri="/api/ready",.method=HTTP_GET,.handler=ready_page},options={.uri="/api/ready",.method=HTTP_OPTIONS,.handler=ready_page};
    if((e=httpd_register_uri_handler(server,&setup))!=ESP_OK)return e;
    if((e=httpd_register_uri_handler(server,&control))!=ESP_OK)return e;
    if((e=httpd_register_uri_handler(server,&scan))!=ESP_OK)return e;
    if((e=httpd_register_uri_handler(server,&ready))!=ESP_OK)return e;
    if((e=httpd_register_uri_handler(server,&options))!=ESP_OK)return e;
    httpd_uri_t ota={.uri="/api/firmware",.method=HTTP_POST,.handler=upload};
    if((e=httpd_register_uri_handler(server,&ota))!=ESP_OK)return e;
    httpd_uri_t root={.uri="/",.method=HTTP_GET,.handler=page},api={.uri="/api/status",.method=HTTP_GET,.handler=status},ws={.uri="/ws",.method=HTTP_GET,.handler=websocket,.is_websocket=true};
    if((e=httpd_register_uri_handler(server,&root))!=ESP_OK)return e;
    if((e=httpd_register_uri_handler(server,&api))!=ESP_OK)return e;
    if((e=httpd_register_uri_handler(server,&ws))!=ESP_OK)return e;
    printf("BENCH WEB_READY version=0.7.0 port=80 websocket=/ws\n");return ESP_OK;
}
