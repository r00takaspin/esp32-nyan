import re, numpy as np
from PIL import Image
def arr(src, name):
    m = re.search(r'%s\[[^\]]*\]\s*=\s*\{(.*?)\};' % name, src, re.S)
    return [int(x,0) for x in re.findall(r'0x[0-9A-Fa-f]+|\d+', m.group(1))]
def c565(v): return ((v>>11)&31)*255//31, ((v>>5)&63)*255//63, (v&31)*255//31
def dec(rle, a, b, pal, w=320, h=172):
    px=[]; 
    for i in range(a,b,2): px += [pal[rle[i+1]]]*rle[i]
    px = (px+[(0,0,0)]*(w*h))[:w*h]
    return np.array(px,dtype=np.uint8).reshape(h,w,3)
s=open('nyan_anims.h').read()
pal=[c565(v) for v in arr(s,'ANIM_PAL')]; rle=arr(s,'ANIM_RLE')
cnt=arr(s,'ANIM_COUNT'); off=re.search(r'ANIM_OFF\[5\]\[20\]\s*=\s*\{(.*?)\};',s,re.S).group(1)
rows=[[int(x) for x in r.split(',')] for r in re.findall(r'\{([^{}]*)\}',off)]
names=['idle','think','approve','reject','boot']
for a,n in enumerate(names):
    for i in range(cnt[a]):
        Image.fromarray(dec(rle,rows[a][i],rows[a][i+1],pal)).save(f'art/frames/{n}_{i:02d}.png')
s=open('nyancat_frames.h').read()
pal=[c565(v) for v in arr(s,'NYAN_PAL565')]; rle=arr(s,'NYAN_RLE'); off=arr(s,'NYAN_OFF')
for i in range(12): Image.fromarray(dec(rle,off[i],off[i+1],pal)).save(f'art/frames/nyan_{i:02d}.png')
