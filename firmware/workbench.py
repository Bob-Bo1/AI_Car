"""Loopback workbench with identity-checked LAN discovery and transparent WS relay."""
import ipaddress,json,os,select,socket,subprocess,threading,time,urllib.request
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path

ASSETS=Path(__file__).resolve().parent
ROOT=Path(os.environ.get('AI_CAR_HOME',str(ASSETS)))
PORT=int(os.environ.get('AI_CAR_PORT','8876'))
QUERY=b'AI_CAR_DISCOVER_V1'
def normalize(value):return ''.join(c for c in str(value).lower() if c in '0123456789abcdef')
def fetch(ip,timeout=.9):
    ipaddress.IPv4Address(ip)
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(f'http://{ip}/api/status',timeout=timeout) as r:return json.load(r)

class Discovery:
    def __init__(self,settings_path=ROOT/'web-device.json'):
        self.path=settings_path;self.settings=json.loads(self.path.read_text(encoding='utf-8-sig'))
        self.identity=normalize(self.settings['device_mac']);self.lock=threading.Lock()
        self.address=None;self.checked=0.;self.channels=set();self.interfaces=[];self.interfaces_at=0.
        self.running=True;self.wake=threading.Event()
    def verify(self,ip):
        try:
            s=fetch(ip)
            return s.get('type')=='state' and s.get('motor_control') is True and s.get('ip')==ip and s.get('device_id')==self.identity
        except (OSError,ValueError,KeyError):return False
    def publish(self,ip):
        with self.lock:
            changed=ip!=self.address;self.address=ip;self.checked=time.monotonic()
            old=list(self.channels) if changed else []
        for channel in old:
            try:channel.shutdown(socket.SHUT_RDWR)
            except OSError:pass
        if ip and changed:
            self.settings.update(ip=ip,url=f'http://{ip}/',workbench_url=f'http://127.0.0.1:{PORT}/')
            temp=self.path.with_suffix('.tmp');temp.write_text(json.dumps(self.settings,ensure_ascii=False,indent=2),encoding='utf-8');os.replace(temp,self.path)
    def state(self):
        with self.lock:return {'app':'ai-car-workbench','device_id':self.identity,'online':self.address is not None,'ip':self.address,'port':PORT}
    def refresh_interfaces(self):
        if time.monotonic()-self.interfaces_at<15:return
        self.interfaces_at=time.monotonic()
        try:
            command='Get-NetIPAddress -AddressFamily IPv4 | Select-Object IPAddress,PrefixLength | ConvertTo-Json -Compress'
            r=subprocess.run(['powershell','-NoProfile','-Command',command],capture_output=True,text=True,timeout=8,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            records=json.loads(r.stdout);records=records if isinstance(records,list) else [records]
            interfaces=[]
            for row in records:
                item=ipaddress.IPv4Interface(f"{row['IPAddress']}/{row['PrefixLength']}")
                if item.ip.is_loopback or item.ip.is_link_local or item.network.prefixlen>30:continue
                interfaces.append((str(item.ip),str(item.network.broadcast_address)))
            self.interfaces=interfaces
        except (OSError,ValueError,KeyError,subprocess.TimeoutExpired):pass
    def probe(self):
        self.refresh_interfaces();sockets=[]
        try:
            for local,broadcast in self.interfaces:
                s=None
                try:
                    s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.setsockopt(socket.SOL_SOCKET,socket.SO_BROADCAST,1);s.bind((local,0));s.setblocking(False)
                    s.sendto(QUERY,(broadcast,37020));sockets.append(s)
                except OSError:
                    if s is not None:s.close()
            end=time.monotonic()+.65
            while sockets and time.monotonic()<end:
                readable,_,_=select.select(sockets,[],[],max(0,end-time.monotonic()))
                for s in readable:
                    try:
                        data,remote=s.recvfrom(512);reply=json.loads(data)
                        if reply.get('type')=='ai_car' and reply.get('protocol')==1 and reply.get('device_id')==self.identity and reply.get('port')==80 and reply.get('ip')==remote[0] and self.verify(remote[0]):return remote[0]
                    except (OSError,ValueError,KeyError):continue
        finally:
            for s in sockets:s.close()
        return None
    def run(self):
        while self.running:
            try:
                current=self.state()['ip'];candidate=current or self.settings.get('ip')
                if candidate and self.verify(candidate):self.publish(candidate)
                else:
                    self.publish(None);found=self.probe()
                    if found:self.publish(found)
            except (OSError,ValueError):self.publish(None)
            self.wake.wait(2);self.wake.clear()

class Handler(BaseHTTPRequestHandler):
    protocol_version='HTTP/1.1'
    def log_message(self,*args):pass
    def reply(self,code,data,kind='application/json; charset=utf-8'):
        self.send_response(code);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(data)));self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff');self.send_header('Content-Security-Policy',"default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self' ws:; img-src 'self' data:; frame-ancestors 'none'");self.end_headers();self.wfile.write(data)
    def do_GET(self):
        host=self.headers.get('Host','')
        if host not in (f'127.0.0.1:{PORT}',f'localhost:{PORT}'):
            self.reply(403,b'{}');return
        if self.path=='/':
            self.reply(200,(ASSETS/'main/console.html').read_bytes().replace('小车独立提供页面 · 无需电脑后台'.encode(),'电脑工作台 · 自动发现小车'.encode()),'text/html; charset=utf-8');return
        if self.path in ('/api/workbench/state','/api/workbench/meta'):
            self.reply(200,json.dumps(self.server.discovery.state()).encode());return
        if self.path=='/api/status':
            ip=self.server.discovery.state()['ip']
            try:self.reply(200,json.dumps(fetch(ip)).encode()) if ip else self.reply(503,b'{}')
            except (OSError,ValueError):self.reply(503,b'{}')
            return
        if self.path!='/ws':self.reply(404,b'{}');return
        origin=self.headers.get('Origin','')
        if origin!=f'http://{host}' or self.headers.get('Upgrade','').lower()!='websocket':self.reply(403,b'{}');return
        discovery=self.server.discovery;ip=discovery.state()['ip']
        if not ip or not discovery.verify(ip):
            discovery.wake.set();self.reply(503,b'{}');return
        upstream=None
        try:
            upstream=socket.create_connection((ip,80),timeout=2)
            request=f'GET /ws HTTP/1.1\r\nHost: {ip}\r\nOrigin: http://{ip}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
            for key in ('Sec-WebSocket-Key','Sec-WebSocket-Version'):
                value=self.headers.get(key,'')
                if '\r' in value or '\n' in value or len(value)>128:raise ValueError('Invalid WebSocket header')
                request+=f'{key}: {value}\r\n'
            upstream.sendall((request+'\r\n').encode('ascii'));answer=b''
            while b'\r\n\r\n' not in answer:
                block=upstream.recv(4096)
                if not block or len(answer)>8192:raise OSError('Handshake incomplete')
                answer+=block
            if b' 101 ' not in answer.split(b'\r\n',1)[0]:raise OSError('Device rejected handshake')
            self.connection.sendall(answer);self.close_connection=True
            upstream.settimeout(None);self.connection.settimeout(None)
            with discovery.lock:
                if discovery.address!=ip:return
                discovery.channels.add(upstream)
            while True:
                readable,_,_=select.select([upstream,self.connection],[],[],1)
                for source in readable:
                    data=source.recv(8192)
                    if not data:return
                    (self.connection if source is upstream else upstream).sendall(data)
        except (OSError,ValueError):
            self.close_connection=True;discovery.wake.set()
        finally:
            if upstream:
                with discovery.lock:discovery.channels.discard(upstream)
                upstream.close()

def main():
    discovery=Discovery();server=ThreadingHTTPServer(('127.0.0.1',PORT),Handler);server.daemon_threads=True;server.discovery=discovery
    threading.Thread(target=discovery.run,daemon=True).start()
    try:server.serve_forever()
    finally:discovery.running=False;discovery.wake.set();server.server_close()
if __name__=='__main__':main()
