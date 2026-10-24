from pathlib import Path
import base64
import json
import xml.etree.ElementTree as ET

import cairosvg
from PIL import Image

ROOT = Path(__file__).resolve().parent
ASSETS = ROOT / '素材'
DATA = ROOT / '购买清单.json'
DEFAULT = [
    dict(name='主控板', model=['XIAO ESP32-C3'], quantity=1, unit='块', image='01_XIAO_ESP32C3.png', crop=[283,85,752,1008]),
    dict(name='充电电池', model=['倍量 CR123A', '3.7V · 1850mWh', 'Type-C 充电款'], quantity=1, unit='节', image='05_倍量CR123A电池.png', crop=[375,60,510,1168]),
    dict(name='拨动开关', model=['SS12F44G8', '三脚 · 柄高 8mm'], quantity=1, unit='个', image='06_SS12F44G8拨动开关.png', crop=[185,57,887,1120]),
    dict(name='升压模块', model=['XL3608', '5V／9V／12V 可选'], quantity=1, unit='块', image='03_XL3608升压板.png', crop=[148,147,732,1235]),
    dict(name='电机驱动板', model=['Mini L298N（商品名）', '迷你双路 H 桥板', '芯片型号待确认'], quantity=1, unit='块', image='02_双路电机驱动.png', crop=[133,142,987,970]),
    dict(name='减速电机', model=['N20', '6V · 50 转／分钟'], quantity=2, unit='个', image='04_N20电机.png', crop=[38,388,1183,465]),
    dict(name='OLED 显示屏', model=['SSD1306 · 0.96 英寸', '128×64 · I²C 四针'], quantity=1, unit='块', image='07_SSD1306显示屏.png', crop=[100,115,1055,1025]),
    dict(name='螺丝', model=['M2×6', '十字圆头'], quantity=20, unit='颗', image='08_M2x6十字圆头螺丝.png', crop=[190,202,1000,780]),
    dict(name='杜邦线', model=['公对公／公对母／母对母', '26AWG 铜线'], quantity=20, unit='根', image='09_杜邦线.png', crop=[18,208,1220,810]),
]

if not DATA.exists():
    DATA.write_text(json.dumps(DEFAULT, ensure_ascii=False, indent=2), encoding='utf-8')
items = json.loads(DATA.read_text(encoding='utf-8'))
assert len(items) == 9
assert [v['quantity'] for v in items] == [1,1,1,1,1,2,1,20,20]

NS = 'http://www.w3.org/2000/svg'
ET.register_namespace('', NS)
def node(tag, attrs=None, parent=None, text=None):
    el = ET.SubElement(parent if parent is not None else svg, f'{{{NS}}}{tag}', {k:str(v) for k,v in (attrs or {}).items()})
    if text is not None:
        el.text = text
    return el

svg = ET.Element(f'{{{NS}}}svg', dict(width='1920', height='1080', viewBox='0 0 1920 1080'))
node('title', text='AI 小车购买清单')
node('desc', text='九项器件购买清单。图片、名称、型号、数量。文字和布局可编辑，器件图片内嵌。')
node('style', text="text{font-family:'Microsoft YaHei','Noto Sans CJK SC',sans-serif;} .name{fill:#172c3e;font-size:30px;font-weight:700} .model{fill:#506274;font-size:23px} .quantity{fill:#126c99;font-size:32px;font-weight:700}")
node('rect', dict(x=0,y=0,width=1920,height=1080,fill='white'))

margin_x, margin_y, gap = 56, 56, 22
cw = (1920-margin_x*2-gap*2)/3
ch = (1080-margin_y*2-gap*2)/3
for i, item in enumerate(items):
    x = margin_x + (i%3)*(cw+gap)
    y = margin_y + (i//3)*(ch+gap)
    group = node('g', dict(id=f'item-{i+1}', **{'data-name':item['name']}))
    node('rect', dict(x=x,y=y,width=cw,height=ch,rx=15,fill='white',stroke='#dfe6ec',**{'stroke-width':1.5}), group)

    path = ASSETS/item['image']
    with Image.open(path) as im:
        iw,ih = im.size
        im.verify()
    cx,cy,cropw,croph = item['crop']
    assert 0<=cx<iw and 0<=cy<ih and cx+cropw<=iw and cy+croph<=ih, (path,iw,ih,item['crop'])
    boxx, boxy, boxw, boxh = x+17,y+27,213,ch-54
    scale = min(boxw/cropw, boxh/croph)
    w,h = cropw*scale,croph*scale
    vp = node('svg', dict(x=boxx+(boxw-w)/2,y=boxy+(boxh-h)/2,width=w,height=h,viewBox=f'{cx} {cy} {cropw} {croph}',overflow='hidden'),group)
    node('image', dict(width=iw,height=ih,href='data:image/png;base64,'+base64.b64encode(path.read_bytes()).decode(),preserveAspectRatio='xMidYMid meet'),vp)

    tx = x+249
    node('text', dict(x=tx,y=y+65,**{'class':'name'}),group,item['name'])
    for j,line in enumerate(item['model']):
        attrs = dict(x=tx,y=y+113+j*35,**{'class':'model'})
        if i==8:
            attrs['font-size']='21'
            attrs['style']='font-size:21px'
        node('text', attrs, group, line)
    node('text', dict(x=tx,y=y+ch-39,**{'class':'quantity'}),group,f'× {item["quantity"]} {item["unit"]}')

out = ROOT/'AI小车_购买清单_16比9.svg'
ET.ElementTree(svg).write(out,encoding='utf-8',xml_declaration=True)
for scale,label in [(1,'1920x1080'),(2,'4K')]:
    target = ROOT/f'AI小车_购买清单_{label}.png'
    cairosvg.svg2png(url=str(out),write_to=str(target),output_width=1920*scale,output_height=1080*scale)
    with Image.open(target) as im:
        assert im.size == (1920*scale,1080*scale)
        im.verify()
    print(target)

check = ET.parse(out).getroot()
assert len(check.findall(f'.//{{{NS}}}image')) == 9
assert all(el.get('href','').startswith('data:image/png;base64,') for el in check.findall(f'.//{{{NS}}}image'))
print('Verified: 9 images, embedded assets, quantities, PNG dimensions, SVG XML.')
