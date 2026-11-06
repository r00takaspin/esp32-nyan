import random
from PIL import Image
W,H=43,18
g=[['.']*W for _ in range(H)]
def put(c,r,ch):
    if 0<=c<W and 0<=r<H: g[r][c]=ch
# rainbow: cols 0..7, stripes 2 rows, alternate 4-col segments offset by 1 row
for c in range(0,9):
    off = 1 if (c//4)%2==0 else 0
    for k,ch in enumerate('123456'):
        for d in range(2): put(c,1+off+k*2+d,ch)
# poptart cols 8..31 rows 0..14
P0,P1=8,31
for r in range(15):
    for c in range(P0,P1+1):
        x=c-P0; y=r
        ch='C'
        if y in (0,14) or x in (0,23): ch='K'
        if (y in (0,14) and x in (0,1,22,23)) or (y in (1,13) and x in (0,23)): ch='.'
        if (y in (1,13) and x in (1,22)): ch='K'
        if 2<=y<=12 and 3<=x<=20: ch='P'
        if (y in (2,12) and x in (3,20)): ch='C'
        put(c,r,ch)
random.seed(7)
spr=[(3,4),(5,2),(9,2),(14,2),(18,3),(20,6),(4,9),(3,12-1),(7,12),(12,12),(17,12),(19,10),(11,3),(16,11)]
for x,y in spr:
    if g[y][P0+x]=='P': put(P0+x,y,'M')
# tail rows 8..11 cols 3..9
tail=["kkk.....",
      "kggkk...",
      ".kkggkkk",
      "...kkggk",
      ".....kkk"]
for r,s in enumerate(tail):
    for c,ch in enumerate(s):
        if ch!='.': put(2+c,7+r,ch)
# legs rows 14..17
def leg(c0):
    for r,s in enumerate(["kggk","kggk",".kk."]):
        for c,ch in enumerate(s):
            if ch!='.': put(c0+c,15+r,ch)
for c0 in (10,15,30,36): leg(c0)
# head 14x11 at cols 29..42 rows 5..15
head=[".kk........kk.",
      "kGGk......kGGk",
      "kGGGk....kGGGk",
      "kGGGGkkkkGGGGk",
      "kGGGGGGGGGGGGk",
      "kGGWkGGGGGWkGk",
      "kGGkkGGGGGkkGk",
      "kRRGGGGGGGGRRk",
      "kRRGkGGkGGkRRk",
      ".kGGkkkkkkkGk.",
      "..kkkkkkkkkk.."]
for r,s in enumerate(head):
    for c,ch in enumerate(s):
        if ch!='.': put(29+c,5+r,ch)
open('grid_a.txt','w').write('\n'.join(''.join(r) for r in g)+'\n')
col={'.':(40,60,110),'K':(20,20,20),'k':(30,30,30),'C':(240,190,140),'P':(245,140,215),'M':(230,50,140),
'G':(140,142,150),'g':(140,142,150),'W':(245,245,240),'R':(240,145,145),
'1':(232,32,42),'2':(247,147,30),'3':(249,226,27),'4':(61,203,58),'5':(30,144,232),'6':(108,63,216)}
im=Image.new('RGB',(W,H))
for r in range(H):
    for c in range(W): im.putpixel((c,r),col[g[r][c]])
im=im.resize((W*12,H*12),Image.NEAREST); im.save('grid_a_preview.png')
print('\n'.join(''.join(r) for r in g))
