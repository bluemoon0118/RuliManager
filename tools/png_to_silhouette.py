# 투명 배경 PNG(흰 실루엣) → VectorIcons.cpp 실루엣 데이터 배열
# 사용: python png_to_silhouette.py kFemale1 image.png > out.inc
#   알파 > 127 을 모양으로 보고 윤곽선(바깥 + 구멍)을 찾아 단순화한 뒤 Catmull-Rom → 베지어로 부드럽게 변환
#   viewBox = 모양 경계 + 여백 (출력 첫 줄 주석에 크기)
import sys, cv2, numpy as np
from PIL import Image

def trace(path, eps=1.0, pad_ratio=0.02, min_area=40):
    a = np.array(Image.open(path).convert('RGBA'))[:, :, 3]
    m = (a > 127).astype(np.uint8) * 255
    m = cv2.morphologyEx(m, cv2.MORPH_OPEN, np.ones((2, 2), np.uint8))
    ys, xs = np.nonzero(m)
    x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    pad = int(round(max(x1 - x0, y1 - y0) * pad_ratio))
    ox, oy = x0 - pad, y0 - pad
    vw, vh = (x1 - x0) + 2 * pad + 1, (y1 - y0) + 2 * pad + 1
    contours, _ = cv2.findContours(m, cv2.RETR_CCOMP, cv2.CHAIN_APPROX_NONE)
    figs = []
    for c in contours:
        if abs(cv2.contourArea(c)) < min_area:
            continue
        s = cv2.approxPolyDP(c, eps, True).reshape(-1, 2).astype(float)
        if len(s) < 3:
            continue
        s[:, 0] -= ox; s[:, 1] -= oy
        s += 0.5
        figs.append(s)
    return figs, vw, vh

def to_bezier(poly, t=0.5):
    # 닫힌 Catmull-Rom (장력 t) → 3차 베지어: [시작점, (c1, c2, p) * n]
    n = len(poly)
    pts = [tuple(poly[0])]
    for i in range(n):
        p0, p1, p2, p3 = poly[(i - 1) % n], poly[i], poly[(i + 1) % n], poly[(i + 2) % n]
        c1 = p1 + (p2 - p0) * (t / 3.0)
        c2 = p2 - (p3 - p1) * (t / 3.0)
        pts += [tuple(c1), tuple(c2), tuple(p2)]
    return pts

def emit(name, path):
    figs, vw, vh = trace(path)
    out = [f'\t// [점 수(시작점 + 베지어 3점씩), x, y, ...] 반복, 0 = 끝  (viewBox {vw}x{vh}, PNG 알파 윤곽선 추적)',
           f'\tconst float {name}[] = {{']
    for f in figs:
        pts = to_bezier(f)
        vals = ', '.join(f'{x:.1f}f, {y:.1f}f' for x, y in pts)
        out.append(f'\t\t{len(pts)}, {vals},')
    out.append('\t\t0')
    out.append('\t};')
    return '\n'.join(out), vw, vh

if __name__ == '__main__':
    text, vw, vh = emit(sys.argv[1], sys.argv[2])
    print(f'// VIEWBOX {vw} {vh}')
    print(text)
