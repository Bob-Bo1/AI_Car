"""Real OTA checks; uploading same build exercises slot switching without motor motion."""
import sys,time,json,urllib.request,urllib.error,http.client
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from web_probe import Car,get_status
ip=sys.argv[1];image=Path('firmware/build/ai_car_bench.bin').read_bytes();checks=[]
op=urllib.request.build_opener(urllib.request.ProxyHandler({}))
def post(body,token='',origin=None):
 req=urllib.request.Request('http://'+ip+'/api/firmware',data=body,headers={'Origin':origin or 'http://'+ip,'X-Car-Token':token,'Content-Type':'application/octet-stream'})
 try:
  with op.open(req,timeout=35) as r:return r.status,r.read().decode()
 except urllib.error.HTTPError as e:return e.code,e.read().decode()
assert post(bytes(512),origin='http://untrusted.invalid')[0]==403;checks.append('untrusted origin rejected')
assert post(bytes(512))[0]==403;checks.append('missing session token rejected')
c=Car(ip);c.command('claim',takeover=True);before=c.command('status')['running_partition']
assert post(bytes(512),c.token)[0]==400;c.close();r=get_status(ip);assert r['running_partition']==before and not r['locked'] and not r['active'];checks.append('invalid image retained current slot')
c=Car(ip);c.command('claim',takeover=True)
h=http.client.HTTPConnection(ip,timeout=3);h.putrequest('POST','/api/firmware');h.putheader('Origin','http://'+ip);h.putheader('X-Car-Token',c.token);h.putheader('Content-Length',str(len(image)));h.endheaders();h.send(image[:512]);h.close();time.sleep(3);c.close()
r=get_status(ip);assert r['running_partition']==before and not r['locked'] and not r['active'];checks.append('interrupted upload retained current slot')
c=Car(ip);c.command('claim',takeover=True);code,body=post(image,c.token);assert code==200,(code,body);c.close()
end=time.monotonic()+30;result=None
while time.monotonic()<end:
 time.sleep(1)
 try:
  r=get_status(ip)
  if r['running_partition']!=before and r['uptime_ms']>12000:result=r;break
 except Exception:pass
assert result and not result['active'] and result['output_a']==result['output_b']==0,result
checks.append('wireless upload switched slot and rebooted healthy')
Path('firmware/ota-device-result.json').write_text(json.dumps({'result':'PASS','checks':checks,'before_slot':before,'after':result},indent=2));print(checks);print(result['running_partition'])
