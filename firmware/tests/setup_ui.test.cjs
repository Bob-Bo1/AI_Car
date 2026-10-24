const fs=require('fs'),vm=require('vm'),assert=require('node:assert/strict');
const html=fs.readFileSync(require('path').join(__dirname,'../main/setup.html'),'utf8');
assert(!html.includes('id="joystick"'));assert(!html.includes('id="phoneQr"'));
const elements=new Map(),timers=[],sockets=[],events={};let redirected='',viaHotspot=true;
function el(id){if(!elements.has(id))elements.set(id,{value:'',textContent:'',hidden:false,disabled:false,required:false,checked:false,dataset:{},children:[],classList:{toggle(){}},replaceChildren(){this.children=[];},append(c){this.children.push(c);},get selectedOptions(){return [this.children.find(c=>c.value===this.value)||this.children[0]];}});return elements.get(id);}
el('switchStep').hidden=true;el('connectingStep').hidden=true;
class WS{constructor(){this.readyState=1;this.sent=[];sockets.push(this);}send(text){this.sent.push(JSON.parse(text));}}
vm.runInNewContext(html.match(/<script>([\s\S]*)<\/script>/)[1],{
 document:{getElementById:el,createElement:()=>({value:'',textContent:'',dataset:{}})},
 location:{host:'192.168.4.1',replace(url){redirected=url;}},window:{addEventListener(k,fn){events[k]=fn;}},
 sessionStorage:{setItem(){},removeItem(){}},WebSocket:WS,AbortController,Date,JSON,Promise,
 setTimeout,clearTimeout,setInterval(fn,ms){timers.push({fn,ms});},
 fetch:async url=>({ok:true,json:async()=>url.includes('/api/wifi/scan')?{ready:true,scanning:false,networks:[{ssid:'Test-2G',rssi:-45,secure:true}]}:{device_id:'example-car',connected:true,via_hotspot:viaHotspot}})
});
(async()=>{
 await new Promise(resolve=>setImmediate(resolve));
 const ws=sockets[0],base={token:'test-token',device_id:'example-car',connected:true,ssid:'Old-SSID',ip:'192.0.2.20',active:false,locked:false,fault:false,you_control:false};
 const feed=s=>ws.onmessage({data:JSON.stringify({...base,...s})});ws.onopen();feed({});
 assert.equal(el('chooseStep').hidden,false);assert.equal(el('switchStep').hidden,true);
 assert.equal(el('networks').children[1].value,'Test-2G');
 el('networks').value='Test-2G';el('password').value='example-password';el('setupForm').onsubmit({preventDefault(){}});
 assert.equal(ws.sent.at(-1).type,'claim');assert.equal(ws.sent.at(-1).takeover,true);
 feed({you_control:true});assert.equal(ws.sent.at(-1).type,'wifi');assert.equal(ws.sent.at(-1).ssid,'Test-2G');assert.equal(el('password').value,'');
 assert.equal(el('connectingStep').hidden,false);
 feed({connected:false,ssid:'Test-2G',ip:''});assert.equal(el('switchStep').hidden,true);
 feed({connected:true,ssid:'Test-2G'});assert.equal(el('switchStep').hidden,false);assert.equal(el('enter').href,'http://192.0.2.20/');
 await timers.find(t=>t.ms===1800).fn();assert.equal(redirected,'','AP client must not auto enter before switching network');
 viaHotspot=false;await timers.find(t=>t.ms===1800).fn();assert.equal(redirected,'http://192.0.2.20/');
 console.log('PASS: isolated setup, scanned selection, credentials cleared, wait for acquired IP, require home-network source, then redirect.');
})().catch(e=>{console.error(e);process.exitCode=1;});
