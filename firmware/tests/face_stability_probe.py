"""Read-only bounded soak: does not claim control or renew motion."""
import argparse,json,re,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from web_probe import get_status

p=argparse.ArgumentParser();p.add_argument('--ip',required=True);p.add_argument('--seconds',type=int,default=900);p.add_argument('--port');p.add_argument('--out',type=Path,required=True);a=p.parse_args()
assert 1<=a.seconds<=900
serial_port=None
if a.port:
    import serial
    serial_port=serial.Serial();serial_port.port=a.port;serial_port.baudrate=115200;serial_port.timeout=0;serial_port.dtr=False;serial_port.rts=False;serial_port.open()
a.out.parent.mkdir(parents=True,exist_ok=True)
start=time.monotonic();last_uptime=-1;last_frames=-1;failures=[];serial_errors=[];samples=0;heap=[];buffer='';first=None;last=None
try:
    with a.out.with_suffix('.jsonl').open('w',encoding='utf-8') as log:
        while time.monotonic()-start<a.seconds:
            try:
                s=get_status(a.ip);elapsed=round(time.monotonic()-start,2);samples+=1
                if first is None:first=s
                if s['uptime_ms']<last_uptime:failures.append('device_restarted')
                if s['oled_frames']<last_frames:failures.append('frame_counter_restarted')
                if not s['oled_healthy'] or s['oled_errors'] or s['oled_last_success_age_ms']>1000:failures.append('display_unhealthy')
                last_uptime=s['uptime_ms'];last_frames=s['oled_frames'];last=s
                if 'heap_free' in s:heap.append(s['heap_free'])
                log.write(json.dumps({'elapsed_s':elapsed,**s},ensure_ascii=False)+'\n');log.flush()
            except (OSError,ValueError,KeyError) as e:
                failures.append(type(e).__name__);log.write(json.dumps({'elapsed_s':round(time.monotonic()-start,2),'error':type(e).__name__})+'\n');log.flush()
            if serial_port:
                try:text=serial_port.read(serial_port.in_waiting).decode('utf-8','replace')
                except OSError as e:
                    serial_errors.append(type(e).__name__);log.write(json.dumps({'observer_serial_error':type(e).__name__})+'\n')
                    serial_port.close();serial_port=None;text=''
                buffer+=text
                while '\n' in buffer:
                    line,buffer=buffer.split('\n',1)
                    if 'BENCH HEALTH' in line:
                        values=re.search(r'heap=(\d+)',line)
                        if values:heap.append(int(values.group(1)))
                        log.write(json.dumps({'serial':line.strip()},ensure_ascii=False)+'\n')
            time.sleep(min(1,max(0,a.seconds-(time.monotonic()-start))))
finally:
    if serial_port:serial_port.close()
    elapsed=time.monotonic()-start
    result={'result':'PASS' if not failures and samples and elapsed>=a.seconds-.1 else 'FAIL','duration_s':round(elapsed,2),'samples':samples,'failures':failures,'observer_serial_errors':serial_errors,'first':first,'last':last,'heap_samples':len(heap),'heap_min':min(heap) if heap else None,'heap_max':max(heap) if heap else None,'heap_first':heap[0] if heap else None,'heap_last':heap[-1] if heap else None}
    a.out.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k not in ('first','last')},ensure_ascii=False))
