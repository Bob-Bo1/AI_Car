"""Render the actual C engine's frames at native OLED resolution and as a review sheet."""
from pathlib import Path
import subprocess
from PIL import Image,ImageDraw,ImageFont

root=Path(__file__).resolve().parents[2]
raw=subprocess.check_output([str(root/'tmp/face_engine_test.exe'),'--frames'])
assert len(raw)==6*32*1024,len(raw)
out=root/'Docs/机器人眼睛预览';out.mkdir(exist_ok=True)
labels=['待机 · 圆角方眼','驾驶 · 精神睁眼','后退 · 收小眼睛','困倦 · 半闭眼','疑惑 · 轻微歪头','急停 · 暂停提示']
font=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',21)
small=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',16)
sheet=Image.new('RGB',(1248,576),'#edf3f8');draw=ImageDraw.Draw(sheet)
draw.text((24,17),'小车机器人眼睛 · 六种基础表情',font=font,fill='#13283c')
draw.text((24,52),'同一套C语言绘图代码生成；屏幕原图128×64，以下按整倍数放大。',font=small,fill='#64758a')
for scene,label in enumerate(labels):
    images=[]
    for frame in range(32):
        buf=raw[(scene*32+frame)*1024:(scene*32+frame+1)*1024]
        image=Image.new('L',(128,64));pix=image.load()
        for y in range(64):
            for x in range(128):pix[x,y]=255 if buf[x+(y//8)*128]&(1<<(y%8)) else 0
        images.append(image)
    # Representative frame before the deterministic blink.
    images[8].save(out/f'{scene+1:02d}.png')
    images[0].resize((512,256),Image.Resampling.NEAREST).save(out/f'{scene+1:02d}.gif',save_all=True,append_images=[im.resize((512,256),Image.Resampling.NEAREST) for im in images[1:]],duration=125,loop=0)
    x=24+(scene%3)*408;y=100+(scene//3)*234
    draw.text((x,y),label,font=font,fill='#13283c')
    sheet.paste(images[8].convert('RGB').resize((384,192),Image.Resampling.NEAREST),(x,y+34))
sheet.save(out/'六种基础表情.png')
print(out/'六种基础表情.png')

turn_raw=subprocess.check_output([str(root/'tmp/face_engine_test.exe'),'--turns'])
assert len(turn_raw)==3*32*1024
turn_sheet=Image.new('RGB',(1248,330),'#edf3f8');turn_draw=ImageDraw.Draw(turn_sheet)
turn_draw.text((24,15),'左右看幅度 · 最大位移由5像素增至7像素',font=font,fill='#13283c')
for scene,label in enumerate(['居中','左看 · 7像素','右看 · 7像素']):
    buf=turn_raw[(scene*32+8)*1024:(scene*32+9)*1024]
    im=Image.new('RGB',(128,64));pix=im.load()
    for y in range(64):
        for x in range(128):pix[x,y]=(255,255,255) if buf[x+(y//8)*128]&(1<<(y%8)) else (0,0,0)
    x=24+scene*408;turn_draw.text((x,62),label,font=font,fill='#13283c')
    turn_sheet.paste(im.resize((384,192),Image.Resampling.NEAREST),(x,99))
turn_sheet.save(out/'左右看幅度.png')
