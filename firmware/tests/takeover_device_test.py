"""Bounded real-device takeover regression. Requires raised wheels."""
import sys,time,json,urllib.request
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from web_probe import Car,get_status
ip=sys.argv[1]
assert '--wheels-raised' in sys.argv
checks=[]
a=Car(ip);b=Car(ip)
try:
 assert a.command('claim',takeover=True)['you_control']
 assert b.command('claim',takeover=True)['you_control']
 r=a.command('drive',throttle=100,steering=0,speed=100);assert 'error' in r and not r['active'];checks.append('old owner motion rejected')
 assert not a.command('ping')['you_control'];assert b.command('ping')['you_control'];checks.append('heartbeat cannot steal')
 assert a.command('claim',takeover=True)['you_control']
 assert a.command('drive',throttle=100,steering=0,speed=100)['active'];time.sleep(.12)
 r=b.command('claim',takeover=True);assert r['you_control'] and not r['active'];time.sleep(.04)
 r=b.command('status');assert r['output_a']==r['output_b']==0;checks.append('takeover cancels prior motion')
 r=a.command('stop');assert 'error' in r;assert b.command('ping')['you_control'];checks.append('old release cannot stop new owner')
 assert b.command('drive',throttle=100,steering=0,speed=100)['active'];time.sleep(.12);b.command('stop');time.sleep(.04)
 r=b.command('status');assert not r['active'] and r['output_a']==r['output_b']==0;checks.append('new controller drive and stop')
 b.command('release')
finally:
 try:b.command('stop')
 except Exception:pass
 a.close();b.close()
Path('firmware/takeover-device-result.json').write_text(json.dumps({'result':'PASS','checks':checks,'state':get_status(ip)},indent=2))
print(checks)
