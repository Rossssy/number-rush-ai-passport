"""Generate an LVGL font subset and preview for the local AI Passport build."""
from pathlib import Path
import re
import sys
from PIL import Image, ImageDraw, ImageFont

root = Path(sys.argv[1])
source = (root / 'main/main.c').read_text(encoding='utf-8')
font_path = root / 'managed_components/lvgl__lvgl/tests/src/test_files/fonts/noto/NotoSansSC-Regular.ttf'
codes = set(range(32, 127))
for literal in re.findall(r'"([^"\n]*)"', source):
    codes.update(ord(c) for c in literal if ord(c) >= 32)
codes = sorted(codes)
font = ImageFont.truetype(str(font_path), 16)
missing_glyph = bytes(font.getmask('\u0378'))
assert all(bytes(font.getmask(chr(code))) != missing_glyph for code in codes), 'Source font is missing required characters'
bitmap = bytearray()
glyphs = ['{0}']
for code in codes:
    char = chr(code)
    left, top, right, bottom = font.getbbox(char, anchor='ls')
    width, height = right-left, bottom-top
    tile = Image.new('L', (max(1, width), max(1, height)))
    if width and height:
        ImageDraw.Draw(tile).text((-left, -top), char, font=font, anchor='ls', fill=255)
        pixels = [min(15, (p+8)//17) for p in tile.getdata()]
        assert tile.getbbox() is not None, f'Empty glyph: {char}'
    else:
        pixels = []
    start = len(bitmap)
    if len(pixels)%2: pixels.append(0)
    bitmap.extend((pixels[i]<<4)|pixels[i+1] for i in range(0,len(pixels),2))
    glyphs.append(f'{{.bitmap_index={start},.adv_w={round(font.getlength(char)*16)},.box_w={width},.box_h={height},.ofs_x={left},.ofs_y={-bottom}}}')
    assert top >= -15 and bottom <= 5, f'Glyph exceeds 20px line: {char}'
hex_data = ',\n'.join(','.join(str(b) for b in bitmap[i:i+32]) for i in range(0,len(bitmap),32))
offsets = ','.join(str(c-codes[0]) for c in codes)
output = '''#include "lvgl.h"
#include "src/font/fmt_txt/lv_font_fmt_txt.h"
/* Generated from Noto Sans SC (OFL); see build_passport_font.py. */
static const uint8_t bitmap[] = {BITMAP};
static const lv_font_fmt_txt_glyph_dsc_t glyphs[] = {GLYPHS};
static const uint16_t offsets[] = {OFFSETS};
static const lv_font_fmt_txt_cmap_t cmaps[] = {
{.range_start=32,.range_length=RANGE,.glyph_id_start=1,.unicode_list=offsets,
.list_length=COUNT,.type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}};
static const lv_font_fmt_txt_dsc_t descriptor = {
.glyph_bitmap=bitmap,.glyph_dsc=glyphs,.cmaps=cmaps,.cmap_num=1,.bpp=4};
const lv_font_t rush_font_16 = {
.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,
.line_height=20,.base_line=5,.subpx=LV_FONT_SUBPX_NONE,.dsc=&descriptor};
'''
for key, value in {'BITMAP':hex_data,'GLYPHS':',\n'.join(glyphs),'OFFSETS':offsets,'RANGE':str(codes[-1]-31),'COUNT':str(len(codes))}.items():
    output=output.replace(key,value)
(root/'main/rush_font_16.c').write_text(output,encoding='utf-8')
preview=Image.new('RGB',(240,132),'#071421')
draw=ImageDraw.Draw(preview)
for y,(status,hint) in enumerate([('游戏中','上/下换道  OK暂停'),('已暂停','OK 继续'),('被卡住了','OK 再玩一次')]):
    for offset,text in [(0,status),(22,hint)]:
        assert font.getlength(text)<=230
        draw.text(((240-font.getlength(text))/2,y*44+offset+15),text,font=font,anchor='ls',fill='white')
preview.resize((720,396)).save(root/'font-preview.png')
print(f'PASS: {len(codes)} glyphs covered; all glyphs fit 20px lines; preview saved.')
