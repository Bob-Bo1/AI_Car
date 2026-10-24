#include "face_engine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "Run face tests with assertions enabled (-UNDEBUG)."
#endif
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static face_input_t input(uint32_t now){
    face_input_t in={.connected=true,.interaction_ms=now};strcpy(in.ip,"192.168.137.135");return in;
}
static void settle(face_engine_t *e,const face_input_t *in,uint32_t now){for(uint32_t t=now;t<now+1000;t+=125)face_engine_step(e,in,t);}
int main(int argc,char **argv){
    bool turns=argc>1&&strcmp(argv[1],"--turns")==0;
    if(argc>1&&(strcmp(argv[1],"--frames")==0||turns)){
#ifdef _WIN32
        _setmode(_fileno(stdout),_O_BINARY);
#endif
        for(int scene=0;scene<(turns?3:6);scene++){
            uint32_t base=90000;face_engine_t e;face_engine_init(&e,base-2000);face_input_t in=input(base);
            if(turns&&scene){in.active=true;in.throttle=70;in.steering=scene==1?-100:100;}
            else if(scene==1){in.active=true;in.throttle=100;}
            if(!turns&&scene==2){in.active=true;in.throttle=-100;}
            if(scene==3)in.interaction_ms=base-65000;
            if(scene==4){in.control_lost=true;in.reason_at_ms=base;}
            if(scene==5)in.locked=true;
            for(int frame=0;frame<32;frame++){
                uint32_t now=base+(uint32_t)frame*125;if(scene==4)in.reason_at_ms=now;
                face_engine_step(&e,&in,now);uint8_t pixels[1024];face_render(pixels,&e.pose,&in);fwrite(pixels,1,1024,stdout);
            }
        }
        return 0;
    }
    face_engine_t e;face_engine_init(&e,0);face_input_t in=input(0);
    face_engine_step(&e,&in,100);assert(e.pose.kind==FACE_BOOT);
    face_engine_step(&e,&in,2000);assert(e.pose.kind==FACE_IDLE);
    in.active=true;in.throttle=100;settle(&e,&in,3000);assert(e.pose.kind==FACE_FORWARD);
    in.throttle=-100;settle(&e,&in,4000);assert(e.pose.kind==FACE_BACKWARD&&e.pose.gaze_y>0);
    in.steering=-100;settle(&e,&in,5000);assert(e.pose.kind==FACE_LEFT&&e.pose.gaze_x==-7);
    in.steering=100;settle(&e,&in,6000);assert(e.pose.kind==FACE_RIGHT&&e.pose.gaze_x==7);
    in.testing=true;face_engine_step(&e,&in,7000);assert(e.pose.kind==FACE_TEST);
    in.locked=true;face_engine_step(&e,&in,7100);assert(e.pose.kind==FACE_ESTOP);
    in.fault=true;face_engine_step(&e,&in,7200);assert(e.pose.kind==FACE_FAULT);
    in.fault=false;in.locked=false;in.connected=false;face_engine_step(&e,&in,7300);assert(e.pose.kind==FACE_WIFI_LOST);
    in.connected=true;in.active=false;in.testing=false;in.control_lost=true;in.reason_at_ms=7400;
    face_engine_step(&e,&in,7500);assert(e.pose.kind==FACE_CONTROL_LOST);
    face_engine_step(&e,&in,9000);assert(e.pose.kind==FACE_IDLE);
    face_engine_step(&e,&in,60000);assert(e.pose.kind==FACE_SLEEPY);
    face_engine_step(&e,&in,90000);assert(e.pose.kind==FACE_ASLEEP&&e.pose.contrast==0x25);
    in.active=true;in.throttle=100;in.steering=0;face_engine_step(&e,&in,90125);assert(e.pose.kind==FACE_FORWARD&&e.pose.contrast==0x7f);
    // A fresh disconnected device shows connecting; a recovered Wi-Fi link cannot resume motion by itself.
    face_engine_init(&e,0);in=input(0);in.connected=false;face_engine_step(&e,&in,2000);assert(e.pose.kind==FACE_CONNECTING);
    in.connected=true;face_engine_step(&e,&in,2200);assert(e.pose.kind==FACE_IDLE);
    // Render every mode into a guarded buffer, and verify that only expression pixels are drawn.
    struct {uint8_t before[32],pixels[1024],after[32];} guarded;memset(&guarded,0xa5,sizeof(guarded));
    uint8_t empty[128]={0};in=input(100000);strcpy(in.ip,"255.255.255.255");
    for(int kind=FACE_BOOT;kind<=FACE_FAULT;kind++){
        for(int gaze=-7;gaze<=7;gaze++)for(int height=2;height<=30;height+=2){
            face_pose_t pose={.kind=(face_kind_t)kind,.eye_width=34,.eye_height=height,.gaze_x=gaze,.gaze_y=3};
            face_render(guarded.pixels,&pose,&in);
            for(int j=0;j<32;j++)assert(guarded.before[j]==0xa5&&guarded.after[j]==0xa5);
            assert(memcmp(empty,guarded.pixels,128)==0);
            assert(memcmp(empty,guarded.pixels+896,128)==0);
        }
    }
    // Monotonic millisecond wrap must not restart boot or lose idle-age calculation.
    face_engine_init(&e,UINT32_MAX-2000);in=input(UINT32_MAX-2000);
    face_engine_step(&e,&in,1000);assert(e.pose.kind==FACE_IDLE);
    puts("PASS: state priority, intent direction, test/lock/fault, Wi-Fi recovery, transient loss, idle sleep/wake, all-frame memory guards, expression-only screen, timestamp wrap.");
    return 0;
}
