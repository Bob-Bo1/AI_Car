from pathlib import Path
import base64
import json
import shutil
import xml.etree.ElementTree as E

ROOT=Path(__file__).parent
ASSETS=ROOT/'白底器件素材'
NS='http://www.w3.org/2000/svg'
INK='http://www.inkscape.org/namespaces/inkscape'
E.register_namespace('',NS)
E.register_namespace('inkscape',INK)
def node(name,attrs=None,text=None):
    n=E.Element('{'+NS+'}'+name,{k:str(v) for k,v in (attrs or {}).items()})
    n.text=text
    return n
root=node('svg',{'width':1920,'height':1080,'viewBox':'0 0 1920 1080','role':'img','aria-labelledby':'title desc'})
root.append(node('title',{'id':'title'},'AI 小车 · 逐引脚接线图'))
root.append(node('desc',{'id':'desc'},'16:9视频接线图。供电位于左上，驱动板与电机位于左下，主控和下方显示屏位于右侧。器件等比例缩放，驱动与电机转向适合接线的朝向，不相连的交叉用跨线弧表示。'))
root.append(node('style',text="text{font-family:'Microsoft YaHei','Noto Sans CJK SC',sans-serif}path{stroke-linecap:round;stroke-linejoin:round}.wire{fill:none;stroke-width:4}.back{fill:none;stroke:white;stroke-width:10}"))
def group(id,label):
    g=node('g',{'id':id,'{'+INK+'}groupmode':'layer','{'+INK+'}label':label});root.append(g);return g
def text(g,x,y,s,size=18,fill='#243244',anchor='start',weight='400'):
    g.append(node('text',{'x':x,'y':y,'font-size':size,'fill':fill,'text-anchor':anchor,'font-weight':weight},s))
def rect(g,x,y,w,h,fill,stroke='none',rx=0):g.append(node('rect',{'x':x,'y':y,'width':w,'height':h,'rx':rx,'fill':fill,'stroke':stroke}))
def dot(g,p,color,r=5):g.append(node('circle',{'cx':p[0],'cy':p[1],'r':r,'fill':color,'stroke':'white','stroke-width':1}))
P={};placements=[]
def photo(g,id,file,crop,left,top,width,pixels=(1254,1254),rotate=0):
    cx,cy,cw,ch=crop
    scale=width/cw
    height=ch*scale
    svg=node('svg',{'id':'photo-'+id,'x':left,'y':top,'width':width,'height':height,'viewBox':f'{cx} {cy} {cw} {ch}','overflow':'hidden','preserveAspectRatio':'xMidYMid meet'})
    data=base64.b64encode((ASSETS/file).read_bytes()).decode('ascii')
    svg.append(node('image',{'width':pixels[0],'height':pixels[1],'href':'data:image/png;base64,'+data,'preserveAspectRatio':'xMidYMid meet'}))
    if rotate==180:
        wrapper=node('g',{'transform':f'rotate(180 {left+width/2} {top+height/2})'})
        wrapper.append(svg);g.append(wrapper)
    else:g.append(svg)
    placements.append(dict(id=id,file=file,crop=crop,x=left,y=top,width=width,height=height,scale_x=scale,scale_y=scale,rotation=rotate))
    if rotate==180:return lambda x,y:(round(left+width-(x-cx)*scale,3),round(top+height-(y-cy)*scale,3))
    return lambda x,y:(round(left+(x-cx)*scale,3),round(top+(y-cy)*scale,3))
def pin(name,transform,x,y):P[name]=transform(x,y)

page=group('page','页面')
rect(page,0,0,1920,1080,'white')
text(page,48,65,'AI 小车 · 逐引脚接线图',32,weight='700')
for x,c,t in [(1000,'#d93636','电源正极'),(1230,'#253548','GND 共地'),(1460,'#1977b8','控制信号')]:
    page.append(node('path',{'d':f'M{x} 54 h35','stroke':c,'stroke-width':4}));text(page,x+48,61,t,18)
page.append(node('path',{'d':'M1690 54 L1700 54 Q1707 42 1714 54 L1725 54','stroke':'#253548','stroke-width':3,'fill':'none'}))
text(page,1738,61,'跨线不连接',18)

g=group('battery','01 电池')
f=photo(g,'battery','05_倍量CR123A电池.png',(375,60,510,1168),75,245,115)
pin('BAT_POS',f,627,85);pin('BAT_NEG',f,627,1188)
text(g,132,153,'CR123A 电池',21,anchor='middle',weight='700')
text(g,187,258,'+',24,'#d93636',weight='700');text(g,187,510,'−',24,weight='700')

g=group('power-switch','02 拨动开关')
f=photo(g,'switch','06_SS12F44G8拨动开关.png',(185,57,887,1120),285,185,140)
pin('SW_LEFT',f,419,1159);pin('SW_CENTER',f,626,1159);pin('SW_RIGHT',f,833,1159)
text(g,355,153,'拨动开关',21,anchor='middle',weight='700')
text(g,P['SW_RIGHT'][0]+4,P['SW_RIGHT'][1]+25,'未接',15,'#697789','middle')

g=group('boost','03 XL3608 升压板')
f=photo(g,'boost','03_XL3608升压板.png',(148,147,732,1235),525,185,185,(1024,1536))
for name,coords in [('BOOST_VIN',[(220,219),(333,219)]),('BOOST_GND_IN',[(690,219),(803,219)]),('BOOST_VOUT',[(220,1306),(333,1306)]),('BOOST_GND_OUT',[(690,1306),(803,1306)])]:
    for i,(x,y) in enumerate(coords,1):pin(f'{name}_{i}',f,x,y)
text(g,617,153,'XL3608 升压板',21,anchor='middle',weight='700')
text(g,617,527,'输出 5V',18,'#c83232','middle','600')

g=group('controller','04 XIAO ESP32-C3')
f=photo(g,'xiao','01_XIAO_ESP32C3.png',(283,85,752,1008),1500,250,210,(1322,1190))
for i,y in enumerate([295,403,511,619,728,836,945]):
    pin(f'XIAO_D{i}',f,335,y)
    x,yy=P[f'XIAO_D{i}'];text(g,x-17,yy-10,f'D{i}',18,anchor='end',weight='600')
for label,y in zip(['5V','GND','3V3','D10','D9','D8','D7'],[295,403,511,619,728,836,945]):
    pin('XIAO_'+label,f,991,y)
    x,yy=P['XIAO_'+label];text(g,x+17,yy-10,label,18,weight='600')
text(g,1605,210,'XIAO ESP32-C3',23,anchor='middle',weight='700')

g=group('display','05 SSD1306 显示屏')
f=photo(g,'oled','07_SSD1306显示屏.png',(100,115,1055,1025),1455,625,300,rotate=180)
for name,x in [('GND',477),('VCC',574),('SCL',671),('SDA',769)]:pin('OLED_'+name,f,x,172)
text(g,1605,596,'SSD1306 显示屏',21,anchor='middle',weight='700')

g=group('driver','06 双路电机驱动')
f=photo(g,'driver','02_双路电机驱动.png',(133,142,987,970),500,690,260,rotate=180)
for name,x,y in [('DRV_VCC',230,223),('DRV_GND',230,351),('DRV_IN1',345,638),('DRV_IN2',346,764),('DRV_IN3',345,890),('DRV_IN4',345,1016),('DRV_A1',923,601),('DRV_A2',924,734),('DRV_B1',923,887),('DRV_B2',924,1016)]:pin(name,f,x,y)
text(g,630,655,'双路电机驱动板',22,anchor='middle',weight='700')

for id,y,title in [('MOTOR_A',850,'电机 A'),('MOTOR_B',650,'电机 B')]:
    g=group(id,title)
    f=photo(g,id,'04_N20电机.png',(38,388,1183,465),125,y,225,rotate=180)
    pin(id+'_T1',f,73,739);pin(id+'_T2',f,73,501)
    text(g,237,y-28,title,23,anchor='middle',weight='700')
    text(g,237,y+121,'N20 · 6V / 50转',16,'#697789','middle')
    x,yy=P[id+'_T1'];text(g,x+11,yy-9,'＋',29,'#d93636','middle','700')

g=group('wires','07 导线')
wires=[]
def wire(id,net,points,c):
    points=[tuple(p) for p in points]
    d='M '+' L '.join(f'{x},{y}' for x,y in points)
    w=node('g',{'id':'wire-'+id,'data-net':net,'{'+INK+'}label':id})
    w.append(node('title',text=id));w.append(node('path',{'class':'back','d':d}));w.append(node('path',{'class':'wire','d':d,'stroke':c}))
    g.append(w);wires.append(dict(id=id,net=net,points=points,color=c))
R='#d93636';G='#253548'
wire('电池正极到开关左脚','VBAT',[P['BAT_POS'],(P['BAT_POS'][0],175),(240,175),(240,407),(P['SW_LEFT'][0],407),P['SW_LEFT']],R)
wire('开关中脚到升压VIN','SW_BAT',[P['SW_CENTER'],(P['SW_CENTER'][0],440),(465,440),(465,175),(P['BOOST_VIN_1'][0],175),P['BOOST_VIN_1']],R)
wire('公共地母线','GND',[(45,110),(1810,110)],G)
wire('电池负极到共地','GND',[P['BAT_NEG'],(45,P['BAT_NEG'][1]),(45,110)],G)
wire('升压输入GND双孔共地','GND',[P['BOOST_GND_IN_1'],P['BOOST_GND_IN_2'],(735,P['BOOST_GND_IN_2'][1])],G)
wire('升压输出GND双孔共地','GND',[P['BOOST_GND_OUT_1'],P['BOOST_GND_OUT_2'],(735,P['BOOST_GND_OUT_2'][1]),(735,110)],G)
wire('XIAO共地','GND',[P['XIAO_GND'],(1810,P['XIAO_GND'][1]),(1810,110)],G)
wire('驱动共地','GND',[P['DRV_GND'],(780,P['DRV_GND'][1]),(780,1040),(45,1040),(45,P['BAT_NEG'][1])],G)
wire('升压5V输出','5V',[P['BOOST_VOUT_1'],(P['BOOST_VOUT_1'][0],520),(70,520),(70,1010),(P['DRV_VCC'][0],1010),P['DRV_VCC']],R)
wire('5V到XIAO','5V',[P['BOOST_VOUT_2'],(P['BOOST_VOUT_2'][0],540),(765,540),(765,125),(1770,125),(1770,P['XIAO_5V'][1]),P['XIAO_5V']],R)
control=[('D2','DRV_IN1',870,'#1977b8'),('D3','DRV_IN2',900,'#cf8117'),('D6','DRV_IN3',930,'#7756b3')]
for label,target,lane,c in control:
    sx,sy=P['XIAO_'+label];tx,ty=P[target]
    wire(label+'到'+target[4:],target[4:],[(sx,sy),(lane,sy),(lane,ty-18),(tx,ty-18),(tx,ty)],c)
sx,sy=P['XIAO_D7'];tx,ty=P['DRV_IN4']
wire('D7到IN4','IN4',[(sx,sy),(1850,sy),(1850,165),(840,165),(840,ty-18),(tx,ty-18),(tx,ty)],'#24825f')
wire('OLED共地','GND',[(1810,P['XIAO_GND'][1]),(1810,940),(P['OLED_GND'][0],940),P['OLED_GND']],G)
wire('3V3到OLED_VCC','3V3',[P['XIAO_3V3'],(1790,P['XIAO_3V3'][1]),(1790,990),(P['OLED_VCC'][0],990),P['OLED_VCC']],'#bc6226')
wire('D4到OLED_SDA','SDA',[P['XIAO_D4'],(1405,P['XIAO_D4'][1]),(1405,970),(P['OLED_SDA'][0],970),P['OLED_SDA']],'#168d93')
wire('D5到OLED_SCL','SCL',[P['XIAO_D5'],(1430,P['XIAO_D5'][1]),(1430,950),(P['OLED_SCL'][0],950),P['OLED_SCL']],'#b45386')
wire('A输出1到电机A端子1','A1',[P['DRV_A1'],(430,P['DRV_A1'][1]),(430,P['MOTOR_A_T1'][1]),P['MOTOR_A_T1']],'#15879b')
wire('A输出2到电机A端子2','A2',[P['DRV_A2'],(400,P['DRV_A2'][1]),(400,P['MOTOR_A_T2'][1]),P['MOTOR_A_T2']],'#15879b')
wire('B输出1到电机B端子1','B1',[P['DRV_B1'],(430,P['DRV_B1'][1]),(430,P['MOTOR_B_T1'][1]),P['MOTOR_B_T1']],'#937041')
wire('B输出2到电机B端子2','B2',[P['DRV_B2'],(400,P['DRV_B2'][1]),(400,P['MOTOR_B_T2'][1]),P['MOTOR_B_T2']],'#937041')

# Give each unavoidable crossing an actual raised arc rather than a straight gap.
def crossing(a,b,c,d):
    if a[1]==b[1] and c[0]==d[0]:
        p=(c[0],a[1])
        if min(a[0],b[0])<p[0]<max(a[0],b[0]) and min(c[1],d[1])<p[1]<max(c[1],d[1]):return p
    if a[0]==b[0] and c[1]==d[1]:return crossing(c,d,a,b)
    return None
def crossing_count(items):
    hits=set()
    for i,w in enumerate(items):
        for v in items[:i]:
            if w['net']==v['net']:continue
            for a,b in zip(w['points'],w['points'][1:]):
                for c,d in zip(v['points'],v['points'][1:]):
                    p=crossing(a,b,c,d)
                    if p:hits.add(tuple(round(z,3) for z in p))
    return len(hits)
bridge_count=0
for i,w in enumerate(wires):
    parts=[f'M{w["points"][0][0]},{w["points"][0][1]}']
    for a,b in zip(w['points'],w['points'][1:]):
        horizontal=a[1]==b[1];axis=0 if horizontal else 1
        direction=1 if b[axis]>a[axis] else -1
        hits=set()
        for v in wires[:i]:
            if w['net']==v['net']:continue
            for c,d in zip(v['points'],v['points'][1:]):
                p=crossing(a,b,c,d)
                if p and min(abs(p[axis]-a[axis]),abs(b[axis]-p[axis]))>9:hits.add(p)
        last=None
        for p in sorted(hits,key=lambda p:direction*p[axis]):
            if last is not None and abs(p[axis]-last)<19:continue
            last=p[axis];bridge_count+=1
            if horizontal:parts.append(f'L{p[0]-direction*7},{p[1]} Q{p[0]},{p[1]-12} {p[0]+direction*7},{p[1]}')
            else:parts.append(f'L{p[0]},{p[1]-direction*7} Q{p[0]+12},{p[1]} {p[0]},{p[1]+direction*7}')
        parts.append(f'L{b[0]},{b[1]}')
    wire_group=g.find('{'+NS+'}g[@id="wire-'+w['id']+'"]')
    for path in wire_group.findall('{'+NS+'}path'):path.set('d',' '.join(parts))

g=group('junctions','08 电气接点')
for p in [(45,110),(45,P['BAT_NEG'][1]),(735,110),(735,P['BOOST_GND_IN_2'][1]),(1810,110),(1810,P['XIAO_GND'][1])]:dot(g,p,G)
used=set()
for w in wires:
    for p in w['points']:
        if p in P.values():dot(g,p,w['color'],2.6);used.add(p)

g=group('labels','09 线路标注')
def caption(x,y,s,c,width):
    rect(g,x-width/2,y-19,width,28,'white','#e1e7ed',5)
    text(g,x,y,s,17,c,'middle','600')
caption(380,110,'GND · 全部共地',G,200)
caption(1030,125,'+5V',R,65)
for label,c in [('D2','#1977b8'),('D3','#cf8117'),('D6','#7756b3')]:caption(1060,P['XIAO_'+label][1],label+' → '+{'D2':'IN1','D3':'IN2','D6':'IN3'}[label],c,136)
caption(1060,165,'D7 → IN4','#24825f',136)
caption(1670,990,'3V3','#bc6226',58)
for x,y,s,c in [(1500,970,'D4 → SDA','#168d93'),(1500,950,'D5 → SCL','#b45386')]:
    rect(g,x-56,y-15,112,19,'white','none',3)
    text(g,x,y,s,16,c,'middle','600')
for name in ['IN1','IN2','IN3','IN4']:
    x,y=P['DRV_'+name]
    rect(g,x+7,y-10,39,20,'white','none',3)
    text(g,x+26.5,y+5,name,14,'#243244','middle','700')
for name in ['SDA','SCL','VCC','GND']:
    x,y=P['OLED_'+name]
    rect(g,x-12,y-27,24,18,'white','none',2)
    text(g,x,y-12,name,12,'#243244','middle','700')

# Preserve the latest tall layout as a recoverable version before replacing the current artifact.
old=ROOT/'AI小车_接线图_含显示屏原排版.svg'
if not old.exists():shutil.copy2(ROOT/'AI小车_接线图.svg',old)
out=ROOT/'AI小车_接线图_16比9.svg'
E.ElementTree(root).write(out,encoding='utf-8',xml_declaration=True)
for name in ['AI小车_接线图.svg','AI小车_接线图_照片版.svg']:shutil.copy2(out,ROOT/name)
data=json.loads((ROOT/'接线数据.json').read_text(encoding='utf-8'))
before=json.loads((ROOT/'接线数据_交叉优化前.json').read_text(encoding='utf-8'))
data.update(version='V7 减少交叉版',canvas={'width':1920,'height':1080},pins=P,wires=wires,placements=placements,unused_pins=[k for k,v in P.items() if v not in used],crossings={'before':crossing_count(before['wires']),'after':crossing_count(wires),'bridges':bridge_count})
data['internal_connections']=[{'net':net,'pins':[prefix+'_1',prefix+'_2'],'reason':'同一焊盘的两个孔'} for prefix,net in [('BOOST_VIN','SW_BAT'),('BOOST_VOUT','5V'),('BOOST_GND_IN','GND'),('BOOST_GND_OUT','GND')]]
data['notes']=[s for s in data['notes'] if not s.startswith('16:9')]
data['notes'].append('16:9布局：1920×1080，器件只进行等比例缩放，保持原始宽高比。')
data['notes']=[s for s in data['notes'] if not s.startswith('当前图中')]
data['notes'].append('当前图中驱动板、两台电机与OLED旋转180度，未镜像或拉伸。OLED下边一排引脚从左到右为SDA、SCL、VCC、GND。跨线弧表示不连接。')
(ROOT/'接线数据.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
print('1920x1080 SVG; photos='+str(len(placements))+'; wires='+str(len(wires)))
print('crossings:',data['crossings'])
