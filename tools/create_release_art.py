"""Reproducible cover and clearly labelled device UI illustrations."""
from pathlib import Path
import sys
from PIL import Image, ImageDraw, ImageFont

root=Path(sys.argv[1])
out=Path(sys.argv[2]); out.mkdir(parents=True,exist_ok=True)
font_path=root/'managed_components/lvgl__lvgl/tests/src/test_files/fonts/noto/NotoSansSC-Regular.ttf'
def font(size): return ImageFont.truetype(str(font_path),size)
def text(draw,xy,value,size=16,fill='white',anchor='mm'):
    draw.text(xy,value,font=font(size),fill=fill,anchor=anchor,stroke_width=0)

cover=Image.new('RGB',(240,320),'#071421'); d=ImageDraw.Draw(cover)
d.rectangle((0,0,239,5),fill='#38d9ff')
text(d,(120,33),'NUMBER RUSH',24,'#ffd447')
text(d,(120,65),'数字冲冲冲',23)
d.polygon([(84,95),(156,95),(234,258),(6,258)],fill='#162b4d')
for x in [36,78,120,162,204]: d.line([(120+(x-120)*.3,95),(x,258)],fill='#4b69a5',width=1)
for y in [112,145,186,234]: d.line((24,y,216,y),fill='#345175',width=1)
for x,y,v,color in [(105,113,'10','#ffd447'),(160,158,'−8','#ff4768'),(65,178,'+20','#38d9ff')]:
    d.rounded_rectangle((x-25,y-15,x+25,y+15),radius=4,fill=color)
    text(d,(x,y-1),v,16,'#071421')
d.rounded_rectangle((93,211,147,254),radius=6,fill='#66f2ff')
text(d,(120,231),'2',29,'#071421')
d.rounded_rectangle((20,268,220,308),radius=6,fill='#ffd447')
text(d,(120,288),'OK  开始游戏',16,'#071421')
cover.save(out/'cover-device.png')
cover.resize((960,1280),Image.Resampling.NEAREST).save(out/'cover.png')
rgb=cover.crop((0,0,240,260)); raw=bytearray()
for r,g,b in rgb.getdata():
    pixel=((r>>3)<<11)|((g>>2)<<5)|(b>>3)
    raw.extend((pixel&255,pixel>>8))
data=',\n'.join(','.join(str(v) for v in raw[i:i+32]) for i in range(0,len(raw),32))
(root/'main/rush_cover.c').write_text('#include "lvgl.h"\nstatic const uint8_t pixels[]={\n'+data+'\n};\nconst lv_image_dsc_t rush_cover={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=240,.h=260,.stride=480},.data_size=sizeof(pixels),.data=pixels};\n',encoding='utf-8')

for name,paused in [('gameplay',False),('paused',True)]:
    screen=Image.new('RGB',(240,320),'#071421'); d=ImageDraw.Draw(screen)
    text(d,(120,22),'NUMBER RUSH',20,'#ffe16a')
    text(d,(70,43),'NUM 128',14,'#66f2ff');text(d,(205,44),'85%',14)
    d.rounded_rectangle((7,58,233,272),radius=12,fill='#162b4d',outline='#4b69a5',width=2)
    for x in [48,96,144,192]:d.line((x,61,x,269),fill='#345175')
    for x,y,v,color in [(72,64,'+16','#38d9ff'),(168,145,'−9','#ff4768'),(24,191,'+22','#38d9ff')]:
        d.rounded_rectangle((x-24,y,x+23,y+27),radius=6,fill=color)
        text(d,(x,y+13),v,14,'#07324d' if color=='#38d9ff' else 'white')
    d.rounded_rectangle((28,100,212,127),radius=6,fill='#ffd447');text(d,(120,113),'100',14,'#2e2200')
    d.rounded_rectangle((98,235,142,269),radius=10,fill='#66f2ff');text(d,(120,250),'128',20,'#071421')
    text(d,(120,286),'已暂停' if paused else '游戏中',16)
    text(d,(120,308),'OK 继续' if paused else '上/下换道  OK暂停',16)
    screen.save(out/f'{name}-device-preview.png')
    poster=Image.new('RGB',(960,1360),'#071421')
    poster.paste(screen.resize((960,1280),Image.Resampling.NEAREST),(0,0))
    text(ImageDraw.Draw(poster),(480,1320),'界面示意图 · 非实机截图',24,'#afc3e8')
    poster.save(out/f'{name}-preview.png')
print('Created cover, RGB565 cover asset, and two labelled UI previews.')
