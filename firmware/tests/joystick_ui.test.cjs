// Run the actual embedded page script against a small DOM/socket harness.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const html=fs.readFileSync(require('node:path').join(__dirname,'../main/console.html'),'utf8');
const code=html.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements=new Map(),windowEvents={},documentEvents={},timers=[],sockets=[];let now=1000;
function element(id){if(!elements.has(id)){const classes=new Set();elements.set(id,{id,style:{},children:[],checked:id==='safe',value:id==='speed'?'100':'',clientWidth:280,attrs:{},capture:null,classList:{toggle(k,v){if(v)classes.add(k);else classes.delete(k);},remove(k){classes.delete(k);}},focus(){},setAttribute(k,v){this.attrs[k]=v;},setPointerCapture(id){this.capture=id;},releasePointerCapture(id){this.capture=null;},hasPointerCapture(id){return this.capture===id;},getBoundingClientRect(){return {left:0,top:0,width:280,height:280};},get lastChild(){return {remove:()=>this.children.pop()};},prepend(li){this.children.unshift(li);},matches(){return false;}});}return elements.get(id);}
class Socket{constructor(){this.readyState=1;this.sent=[];sockets.push(this);}send(raw){this.sent.push(JSON.parse(raw));}close(){this.readyState=3;this.onclose?.();}}
vm.runInNewContext(code,{document:{hidden:false,getElementById:element,querySelectorAll(){return [];},createElement(){return element('li'+Math.random());},addEventListener(k,fn){documentEvents[k]=fn;}},window:{addEventListener(k,fn){windowEvents[k]=fn;}},location:{host:'car.test'},WebSocket:Socket,Date:class extends Date{static now(){return now;}},setInterval(fn,ms){timers.push({fn,ms});},setTimeout(){},console});
const socket=sockets[0],stick=element('joystick');socket.onopen();
const idle={type:'state',token:'test',you_control:true,claimed:true,active:false,locked:false,fault:false,mode:'idle',reason:'boot',output_a:0,output_b:0,ssid:'test',invert_a:true,invert_b:false,swap_ab:false,reverse_drive:true};
let snapshot=idle;const feed=(s={})=>{snapshot={...idle,...s};socket.onmessage({data:JSON.stringify(snapshot)});};feed();
feed({you_control:false,claimed:false});assert.equal(socket.sent.some(s=>s.type==='claim'),false,'viewing must not claim');feed();
const event=(x=0,y=0)=>({button:0,pointerId:1,clientX:140+x*95.2,clientY:140-y*95.2,preventDefault(){}});
const tick=()=>{now+=100;timers.find(t=>t.ms===100).fn();if(socket.readyState===1)socket.onmessage({data:JSON.stringify(snapshot)});};
const last=()=>socket.sent.at(-1),drive=()=>socket.sent.filter(s=>s.type==='drive').at(-1);
stick.onpointerdown(event(.02,.02));assert.equal(last().type,'stop','center deadzone must not move');
stick.onpointermove(event(1,1));tick();assert.deepEqual([drive().throttle,drive().steering],[71,71]);
stick.onpointermove(event(50,0));tick();assert.deepEqual([drive().throttle,drive().steering],[0,100]);
stick.onpointermove(event(0,-.54));tick();assert.deepEqual([drive().throttle,drive().steering],[-50,0]);
stick.onpointerup(event());assert.equal(last().type,'stop');const afterRelease=socket.sent.length;tick();assert.equal(socket.sent.length,afterRelease,'released pointer cannot resend motion');assert.equal(element('knob').style.transform,'translate(0px,0px)');
for(const method of ['onpointercancel','onlostpointercapture']){stick.onpointerdown(event(0,1));tick();stick[method](event());assert.equal(last().type,'stop');const n=socket.sent.length;tick();assert.equal(socket.sent.length,n);}
const key=(k)=>({key:k,repeat:false,target:{matches(){return false;}},preventDefault(){}});
windowEvents.keydown(key('w'));tick();assert.deepEqual([drive().throttle,drive().steering],[100,0]);
windowEvents.keydown(key('d'));tick();assert.deepEqual([drive().throttle,drive().steering],[71,71]);
windowEvents.keyup(key('w'));tick();assert.deepEqual([drive().throttle,drive().steering],[0,100]);
windowEvents.keyup(key('d'));assert.equal(last().type,'stop');
windowEvents.keydown(key('s'));tick();feed({you_control:false,claimed:true,reason:'heartbeat_timeout'});const revoked=socket.sent.length;tick();assert.equal(socket.sent.length,revoked,'ownership loss must clear old movement');
feed();stick.onpointerdown(event(0,1));tick();feed({locked:true,reason:'emergency_stop'});const emergency=socket.sent.length;tick();assert.equal(socket.sent.length,emergency);
// A new deliberate gesture requests takeover once; periodic updates never reclaim.
feed({you_control:false,claimed:true});
const beforeTake=socket.sent.length;stick.onpointerdown(event(0,1));
assert.equal(last().type,'claim');assert.equal(last().takeover,true);
assert.equal(socket.sent.length,beforeTake+1,'wait for ownership before motion');
feed({you_control:true,claimed:true,reason:'control_taken_over'});assert.equal(last().type,'drive');
feed({you_control:false,claimed:true});const lost=socket.sent.length;tick();
assert.equal(socket.sent.length,lost,'old held gesture must not reclaim or replay');
stick.onpointermove(event(1,0));assert.equal(socket.sent.length,lost);
stick.onpointerdown(event(0,1));assert.equal(last().type,'claim');
stick.onpointerup(event());feed();assert.equal(last().type,'claim','released gesture must not drive after delayed claim ack');
// A lock can only be explicitly released; takeover does not unlock it.
feed({you_control:false,claimed:true,locked:true});element('unlock').onclick();assert.equal(last().type,'claim');
feed({you_control:true,claimed:true,locked:true});assert.equal(last().type,'unlock');feed();
feed();element('reverseDrive').checked=true;element('saveDirection').onclick();assert.equal(last().reverse_drive,true);
element('wifiSsid').value='test';element('wifiPassword').value='example-only';element('wifiForm').onsubmit({preventDefault(){}});assert.equal(last().type,'wifi');feed({connected:true,ssid:'test',hostname:'ai-car-demo',ip:'192.0.2.20'});assert.equal(element('settings').open,false);assert.equal(element('homeLink').hidden,false);assert.equal(element('homeLink').href,'http://192.0.2.20/');
assert.equal(element('phoneUrl').href,'http://192.0.2.20/');assert.ok(element('phoneQr').innerHTML.includes('<svg'));feed({connected:false,ip:'',ap_ssid:'AI-Car-test'});assert.equal(element('phoneUrl').href,'http://192.168.4.1/control');feed();
stick.onpointerdown(event(-1,.5));tick();windowEvents.blur();assert.equal(socket.readyState,3);assert.equal(socket.sent.at(-2).type,'stop');assert.equal(last().type,'release');
console.log('PASS: actual page script: deadzone, radial bounds, proportional axes, pointer release/cancel/capture loss, diagonal keys, partial key release, ownership loss, estop, blur and reverse setting.');
