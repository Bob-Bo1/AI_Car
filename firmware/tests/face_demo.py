"""One visible, bounded direction demonstration, followed by stop and release."""
import argparse,json,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from web_probe import Car
p=argparse.ArgumentParser();p.add_argument('direction',choices=['forward','backward','left','right']);p.add_argument('--ip');p.add_argument('--seconds',type=float,default=2);p.add_argument('--wheels-raised',action='store_true');a=p.parse_args()
if not a.wheels_raised or not .1<=a.seconds<=2:p.error('Raised wheels required; duration must not exceed 2 seconds')
ip=a.ip or json.loads((Path(__file__).resolve().parents[1]/'web-device.json').read_text())['ip']
c=Car(ip);owned=False;seen=set();before=c.state['uptime_ms'];result={}
try:
    s=c.command('claim');assert s['you_control'] and not s['locked'],s;owned=True
    end=time.monotonic()+a.seconds
    while time.monotonic()<end:
        s=c.command('drive',action=a.direction,speed=100);assert 'error' not in s and s['active'],s
        seen.add(s['face_state']);time.sleep(.08)
    c.command('stop');time.sleep(.06);s=c.command('status')
    assert not s['active'] and s['output_a']==s['output_b']==0,s
    assert a.direction in seen,seen
    assert s['uptime_ms']>before,s
    result={'direction':a.direction,'seconds':a.seconds,'faces_seen':sorted(seen),'outputs_after_stop':[s['output_a'],s['output_b']],'uptime_ms':s['uptime_ms'],'reset_reason':s.get('reset_reason'),'oled_errors':s['oled_errors']}
    print(json.dumps(result,ensure_ascii=False))
    with (Path(__file__).resolve().parents[1]/'face-demo-output.jsonl').open('a',encoding='utf-8') as log:log.write(json.dumps(result,ensure_ascii=False)+'\n')
finally:
    if owned:
        try:c.command('stop');c.command('release')
        except OSError:pass
    c.close()
