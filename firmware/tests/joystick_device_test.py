"""Bounded integration checks of the flashed firmware. Wheels must be raised."""
import argparse,json,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from web_probe import Car,get_status
p=argparse.ArgumentParser();p.add_argument('--ip',required=True);p.add_argument('--wheels-raised',action='store_true');a=p.parse_args()
if not a.wheels_raised:p.error('Confirm raised wheels before motor output checks')
car=Car(a.ip);results=[]
def stopped():
    car.command('stop');time.sleep(.13);s=car.command('status')
    assert not s['active'] and s['output_a']==s['output_b']==0,s
def claim():
    s=car.command('claim');assert s['you_control'] and not s['locked'],s
try:
    assert car.state['version'] in ('0.3.1','0.4.0','0.4.1','0.4.2','0.5.0','0.6.0') and car.state['reverse_drive']
    assert (car.state['invert_a'],car.state['invert_b'],car.state['swap_ab'])==(True,False,False)
    claim()
    for fields in [dict(throttle=101,steering=0),dict(throttle=0,steering=-101),dict(throttle=.5,steering=0),dict(throttle=100),dict(throttle=True,steering=0)]:
        s=car.command('drive',speed=100,**fields);assert 'error' in s and not s['active'],s
    # Explicit fixtures exercise firmware parsing, normalization, mounting correction and PWM polarity.
    for throttle,steering,expected in [(100,0,(-255,-255)),(-100,0,(255,255)),(0,100,(255,-255)),(0,-100,(-255,255)),(71,71,(0,-255)),(71,-71,(-255,0)),(-71,71,(255,0)),(-71,-71,(0,255)),(100,50,(-85,-255))]:
        end=time.monotonic()+.65
        while time.monotonic()<end:
            s=car.command('drive',throttle=throttle,steering=steering,speed=100)
            assert 'error' not in s and (s['target_a'],s['target_b'])==expected,s
            time.sleep(.08)
        s=car.command('status');assert (s['output_a'],s['output_b'])==(-expected[0],expected[1]),s
        stopped();results.append({'axes':[throttle,steering],'targets':expected,'stop_outputs':[0,0]})
    s=car.command('drive',throttle=100,steering=0,speed=50);assert (s['target_a'],s['target_b'])==(-127,-127);stopped()
    car.command('drive',throttle=100,steering=30,speed=100);time.sleep(.15)
    car.command('drive',throttle=0,steering=0,speed=100);time.sleep(.05)
    s=car.command('status');assert not s['active'] and s['output_a']==s['output_b']==0,s
    # Ordinary status pings cannot keep an old drive vector alive.
    car.command('drive',throttle=100,steering=50,speed=100)
    for _ in range(7):time.sleep(.09);s=car.command('ping')
    assert not s['active'] and s['output_a']==s['output_b']==0 and 0<=s['stop_elapsed_ms']<500,s
    results.append({'check':'watchdog_with_pings','device_stop_elapsed_ms':s['stop_elapsed_ms']})
    claim();car.command('drive',throttle=-71,steering=-71,speed=100);time.sleep(.15);car.close();time.sleep(.2)
    s=get_status(a.ip);assert not s['active'] and s['output_a']==s['output_b']==0 and s['reason']=='controller_disconnected',s
    results.append({'check':'socket_disconnect','device_stop_elapsed_ms':s['stop_elapsed_ms']})
    print(json.dumps({'result':'PASS','checks':results},ensure_ascii=False,indent=2))
finally:
    try:car.command('stop');car.command('release')
    except (OSError,ConnectionError):pass
    car.close()
