import re
from PIL import Image, ImageDraw
src=open('/Users/voldemar/Documents/deepseek-harness/default-workspace/Arduino/libraries/GFX_Library_for_Arduino/src/font/glcdfont.h').read()
body=src[src.index('{')+1:src.index('};')]
body=re.sub(r'//.*','',body); body=re.sub(r'/\*.*?\*/','',body,flags=re.S)
F=[int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]+',body)]
def C(r,g,b): return (r&0xF8|r>>5, g&0xFC|g>>6, b&0xF8|b>>5)
def ch(d,x,y,c,col,sz):
    for i in range(5):
        line=F[ord(c)*5+i]
        for j in range(8):
            if line>>j&1: d.rectangle([x+i*sz,y+j*sz,x+i*sz+sz-1,y+j*sz+sz-1],fill=col)
def txt(d,x,y,s,col,sz=1):
    for k,c in enumerate(s): ch(d,x+k*6*sz,y,c,col,sz)
def wrapped(d,x,y,maxW,s,col,sz,lh):
    cw=6*sz; cx,cy=x,y
    for w in s.split():
        if cx>x and cx+len(w)*cw>x+maxW: cx=x; cy+=lh
        txt(d,cx,cy,w,col,sz); cx+=(len(w)+1)*cw
def approve(text,blink,out):
    im=Image.new('RGB',(320,172),C(18,20,34)); d=ImageDraw.Draw(im)
    e=C(60,220,90) if blink else C(240,60,60)
    for r in [(2,2,316,6),(2,164,316,6),(2,2,6,168),(312,2,6,168)]:
        d.rectangle([r[0],r[1],r[0]+r[2]-1,r[1]+r[3]-1],fill=e)
    txt(d,12,11,"APPROVE TOOL CALL?",C(255,190,40))
    wrapped(d,12,23,296,text,(255,255,255),1,10)
    d.rounded_rectangle([14,100,153,161],12,fill=C(60,220,90))
    d.rounded_rectangle([166,100,305,161],12,fill=C(240,60,60))
    txt(d,57,121,"YES",(0,0,0),3); txt(d,218,121,"NO",(0,0,0),3)
    im.save(out)
approve("bash: git push origin main  (DeepSeek Harness wants to run a shell command in ~/Arduino/nyancat)",True,'frames/ui_approve.png')
