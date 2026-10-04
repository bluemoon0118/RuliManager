# SVG 경로(d 속성) → VectorIcons.cpp 의 실루엣 데이터 배열로 변환
# 사용: python svg_to_silhouette.py kFemale2 path.txt 850 1250  > out.inc
#   path.txt : <path d="..."> 의 d 값만 넣은 텍스트 파일 (M/L/H/V/C/S/Q/Z 지원, 대소문자 모두)
#              <path> 가 여러 개면 한 줄에 하나씩 (각각 따로 채움)
#   결과 배열을 VectorIcons.cpp 의 kFemale1 아래에 붙이고 kFemaleSet 에 { kFemale2, 폭, 높이 } 한 줄 추가
import re,sys
def tokens(d):
    for m in re.finditer(r'[MmLlHhVvCcSsQqTtZz]|-?(?:\d+\.?\d*|\.\d+)(?:e-?\d+)?',d):
        yield m.group()
def parse(d):
    toks=list(tokens(d)); i=0; cmd=None
    figs=[]; cur=None; x=y=0; sx=sy=0; lastc=None
    def num():
        nonlocal i; v=float(toks[i]); i+=1; return v
    while i<len(toks):
        t=toks[i]
        if re.match(r'[A-Za-z]',t): cmd=t; i+=1
        elif cmd in 'Mm': cmd='l' if cmd=='m' else 'L'
        c=cmd
        if c in 'Zz':
            if cur: cur['closed']=True
            x,y=sx,sy; lastc=None; continue
        if c in 'Mm':
            a,b=num(),num()
            if c=='m': a+=x;b+=y
            x,y=a,b; sx,sy=x,y; cur={'start':(x,y),'segs':[],'closed':False}; figs.append(cur); lastc=None
            cmd='l' if c=='m' else 'L'; continue
        if c in 'LlHhVv':
            if c in 'Ll': a,b=num(),num(); 
            if c=='l': a+=x;b+=y
            if c=='H': a,b=num(),y
            if c=='h': a,b=x+num(),y
            if c=='V': a,b=x,num()
            if c=='v': a,b=x,y+num()
            cur['segs'].append((x+(a-x)/3,y+(b-y)/3,x+2*(a-x)/3,y+2*(b-y)/3,a,b)); x,y=a,b; lastc=None; continue
        if c in 'Cc':
            v=[num() for _ in range(6)]
            if c=='c': v=[v[0]+x,v[1]+y,v[2]+x,v[3]+y,v[4]+x,v[5]+y]
            cur['segs'].append(tuple(v)); lastc=(v[2],v[3]); x,y=v[4],v[5]; continue
        if c in 'Ss':
            v=[num() for _ in range(4)]
            if c=='s': v=[v[0]+x,v[1]+y,v[2]+x,v[3]+y]
            c1=(2*x-lastc[0],2*y-lastc[1]) if lastc else (x,y)
            cur['segs'].append((c1[0],c1[1],v[0],v[1],v[2],v[3])); lastc=(v[0],v[1]); x,y=v[2],v[3]; continue
        if c in 'Qq':
            v=[num() for _ in range(4)]
            if c=='q': v=[v[0]+x,v[1]+y,v[2]+x,v[3]+y]
            qx,qy,ex,ey=v
            cur['segs'].append((x+2/3*(qx-x),y+2/3*(qy-y),ex+2/3*(qx-ex),ey+2/3*(qy-ey),ex,ey)); lastc=None; x,y=ex,ey; continue
        raise Exception('unsupported '+c)
    return figs
def emit(name,d,vw,vh):
    groups=[parse(line.strip()) for line in d.splitlines() if line.strip()]
    figs=[]
    for gi,g in enumerate(groups):
        if gi>0: figs.append(None)   # -1 = 다음 <path> (따로 채움)
        figs+=g
    out=[]
    out.append(f'	// [점 수(시작점 + 베지어 3점씩), x, y, ...] 반복, -1 = 다음 <path>(따로 채움), 0 = 끝  (viewBox {vw}x{vh})')
    out.append(f'	const float {name}[] = {{')
    for f in figs:
        if f is None:
            out.append('		-1,'); continue
        pts=[f['start']]+[p for s in f['segs'] for p in ((s[0],s[1]),(s[2],s[3]),(s[4],s[5]))]
        vals=', '.join(f'{p[0]:.2f}f, {p[1]:.2f}f' for p in pts)
        out.append(f'		{len(pts)}, {vals},')
    out.append('		0')
    out.append('	};')
    return '\n'.join(out)
if __name__=='__main__':
    print(emit(sys.argv[1],open(sys.argv[2]).read().strip(),sys.argv[3],sys.argv[4]))
