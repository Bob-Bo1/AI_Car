"""Bounded real-device expression/control checks; explicit raised wheels required."""
import argparse,json,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from web_probe import Car,get_status
p=argparse.ArgumentParser();p.add_argument('--ip',required=True);p.add_argument('--wheels-raised',action='store_true');a=p.parse_args()
if not a.wheels_raised:p.error('Confirm wheels are raised before these motor tests')
car=Car(a.ip);results=[]
def health(s):assert s['oled_healthy'] and s['oled_errors']==0 and s['oled_last_success_age_ms']<1000,s
def claim():assert car.command('claim')['you_control']
def wait_face(expected):
    end=time.monotonic()+.7
    while time.monotonic()<end:
        s=car.command('status');health(s)
        if s['face_state']==expected:return s
        time.sleep(.04)
    raise AssertionError((expected,s))
def stop():
    car.command('stop');time.sleep(.05);s=car.command('status')
    assert not s['active'] and s['output_a']==s['output_b']==s['throttle']==s['steering']==0,s
try:
    assert car.state['version']=='0.6.0' and car.state['reverse_drive'];health(car.state);claim();wait_face('idle')
    initial_frames=car.state['oled_frames']
    for throttle,steering,face in [(100,0,'forward'),(-100,0,'backward'),(0,-100,'left'),(0,100,'right'),(71,-71,'left'),(-71,71,'right')]:
        end=time.monotonic()+.8;seen=False
        while time.monotonic()<end:
            s=car.command('drive',throttle=throttle,steering=steering,speed=100);health(s)
            assert (s['throttle'],s['steering'])==(throttle,steering),s
            seen|=s['face_state']==face;time.sleep(.08)
        assert seen,(face,s);stop();wait_face('idle');results.append({'axes':[throttle,steering],'face':face,'stopped':True})
    for motor,direction in [(0,1),(1,-1)]:
        s=car.command('test',motor=motor,direction=direction,duration_ms=2000);assert s['active'],s
        command=s['command_id'];end=time.monotonic()+2.2;seen=False
        while time.monotonic()<end:
            time.sleep(.08);s=car.command('keep',command_id=command);health(s);seen|=s['face_state']=='test'
            assert s['throttle']==s['steering']==0,s
            if not s['active']:break
        assert seen and not s['active'] and s['reason']=='test_complete',s
        time.sleep(.03);s=car.command('status');assert s['output_a']==s['output_b']==0,s
        wait_face('idle');results.append({'test_motor':motor,'direction':direction,'face':'test','automatic_stop':True})
    car.command('drive',throttle=100,steering=0,speed=100);time.sleep(.12);car.command('estop')
    s=wait_face('estop');assert s['locked'] and not s['active'] and s['output_a']==s['output_b']==0,s
    claim();car.command('unlock');wait_face('idle');results.append({'check':'estop_and_unlock','passed':True})
    car.command('drive',throttle=-100,steering=-50,speed=100)
    for _ in range(8):time.sleep(.08);s=car.command('ping')
    assert not s['active'] and s['output_a']==s['output_b']==0 and 0<=s['stop_elapsed_ms']<500,s
    wait_face('control_lost');results.append({'check':'watchdog_with_animation','device_stop_elapsed_ms':s['stop_elapsed_ms']})
    assert s['oled_frames']>initial_frames+30,s
    claim();car.command('release');time.sleep(.05);s=get_status(a.ip);health(s)
    assert not s['active'] and s['output_a']==s['output_b']==0,s
    print(json.dumps({'result':'PASS','checks':results,'final':s},ensure_ascii=False,indent=2))
finally:
    try:car.command('stop');car.command('release')
    except (OSError,ConnectionError):pass
    car.close()
