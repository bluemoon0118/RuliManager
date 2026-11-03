# 움직이는 그림 (SVG SMIL 애니메이션, 반복) - 설명서용
#  그림을 누르면 멈춤 / 다시 재생 (build.py 의 스크립트)
from svg import *

D = 7   # 한 번 도는 시간 (초) - 그림마다 바꿀 수 있음


def kt(*t):
    return ';'.join(f'{x:g}' for x in t)


def anim(attr, values, times, dur, discrete=False):
    cm = ' calcMode="discrete"' if discrete else ''
    return (f'<animate attributeName="{attr}" values="{values}" keyTimes="{times}" dur="{dur}s" '
            f'repeatCount="indefinite"{cm}/>')


def move(values, times, dur, discrete=False):
    cm = ' calcMode="discrete"' if discrete else ''
    return (f'<animateTransform attributeName="transform" type="translate" values="{values}" keyTimes="{times}" '
            f'dur="{dur}s" repeatCount="indefinite"{cm}/>')


def show_between(t0, t1, dur, fade=0.03):
    """t0 ~ t1 사이에만 보이는 그룹 여는 태그 (닫을 때 '</g>')"""
    pts = []
    if t0 <= 0:
        pts += [(0, 1)]
    else:
        a = max(0.0001, t0 - fade)
        pts += [(0, 0), (a, 0), (max(t0, a + 0.0001), 1)]
    if t1 >= 1:
        pts += [(1, 1)]
    else:
        b = min(0.9999, t1 + fade)
        pts += [(t1, 1), (max(b, t1 + 0.0001), 0), (1, 0)]
    out = []
    for t, v in pts:
        if out and t <= out[-1][0]:
            t = out[-1][0] + 0.0001
        out.append((min(t, 1), v))
    out[-1] = (1, out[-1][1])
    vals = ';'.join(str(v) for _, v in out)
    return f'<g opacity="{out[0][1]}">' + anim('opacity', vals, kt(*[t for t, _ in out]), dur)


def cursor(s, pts, times, dur):
    """마우스 화살표: pts = [(x, y), ...] 를 times 에 맞춰 이동"""
    vals = ';'.join(f'{x} {y}' for x, y in pts)
    s.add('<g>' + move(vals, kt(*times), dur))
    s.add('<path d="M0,0 L0,17 L4.5,13 L7.5,20 L10.5,18.8 L7.5,12 L13,12 Z" fill="#FFFFFF" stroke="#10161A" stroke-width="1.3"/>')
    s.add('</g>')


def click(s, x, y, t, dur, double=False):
    """누르는 표시 (퍼지는 원)"""
    for k in range(2 if double else 1):
        t0 = t + k * 0.04
        s.add(f'<circle cx="{x}" cy="{y}" r="4" fill="none" stroke="{CALL}" stroke-width="2.5" opacity="0">'
              + anim('r', '4;4;18;18', kt(0, t0, t0 + 0.06, 1), dur)
              + anim('opacity', '0;0;1;0;0', kt(0, t0, t0 + 0.005, t0 + 0.07, 1), dur) + '</circle>')


def step_text(s, x, y, steps, dur, size=12):
    """오른쪽 설명 줄: steps = [(t0, t1, 글자)] - 그 시간에만 강조"""
    for i, (t0, t1, txt) in enumerate(steps):
        yy = y + i * 24
        s.text(x, yy, f'{i + 1}. {txt}', size, SUB2)
        s.add(show_between(t0, t1, dur, 0.01))
        s.rect(x - 8, yy - size - 3, 300, size + 10, 'rgba(242,157,73,0.15)', r=3)
        s.text(x, yy, f'{i + 1}. {txt}', size, TEXT, weight='bold')
        s.add('</g>')


# ---------------------------------------------------------------------------

def an_hover_preview():
    dur = 7
    s = Svg(760, 230, '카드에 마우스를 올리면 미리보기 재생')
    x, y, w = 40, 20, 220
    s.video_card(x, y, w, '영상 제목', 'ABC-123', actors=2, tags=5)
    ih = w * 9 / 16
    # 재생 중 화면 (색 띠가 흐르는 모양)
    s.add(show_between(0.25, 0.85, dur))
    s.add(f'<clipPath id="clipHv"><rect x="{x}" y="{y}" width="{w}" height="{ih}" rx="4"/></clipPath>')
    s.add(f'<g clip-path="url(#clipHv)">')
    s.add('<g>' + move(f'0 0;{-w} 0', kt(0, 1), 2.4))
    cols = ['#5C7A99', '#7A5C99', '#5C997A', '#99805C']
    for k in range(8):
        s.rect(x + k * w / 4, y, w / 4 + 1, ih, cols[k % 4], r=0)
        s.person(x + k * w / 4 + w / 8, y + ih / 2 + 6, 40, TEXT, 0.5)
    s.add('</g>')
    s.rect(x, y + ih - 4, w, 4, '#000000', r=0, op=0.4)
    s.add(f'<rect x="{x}" y="{y + ih - 4}" width="0" height="4" fill="{ACC}">'
          + anim('width', f'0;0;{w};{w}', kt(0, 0.25, 0.85, 1), dur) + '</rect>')
    s.add('</g></g>')
    cursor(s, [(320, 180), (150, 80), (150, 80), (320, 190), (320, 180)], [0, 0.2, 0.85, 0.95, 1], dur)
    step_text(s, 360, 60, [(0.0, 0.22, '카드 이미지에 마우스를 올림'),
                           (0.25, 0.85, '미리보기 영상이 그 자리에서 재생'),
                           (0.86, 1.0, '마우스를 빼면 멈춤')], dur)
    s.text(360, 150, '미리보기 영상은 오른쪽 클릭 [미리보기 만들기] 로 만든 것만', 10, SUB2)
    return s.render()


def an_split_parts():
    dur = 8
    s = Svg(760, 240, '분할 영상 파트 고르기')
    x, y, w = 40, 20, 240
    s.video_card(x, y, w, '영상 제목 (1/3)', 'ABC-123', time='3:12:40', actors=1, tags=4)
    ih = w * 9 / 16
    zw = w / 3
    # 마우스가 올라가 있는 동안 파트 칸 · 번호
    s.add(show_between(0.12, 0.92, dur))
    for i in range(3):
        if i:
            s.line(x + i * zw, y, x + i * zw, y + ih, TEXT, 1.5)
        s.text(x + i * zw + zw / 2, y + ih / 2 + 9, str(i + 1), 26, TEXT, 'middle', 'bold')
    # 올라간 칸 테두리 (마우스를 따라)
    s.add('<g>' + move(f'0 0;0 0;{zw} 0;{2 * zw} 0;{zw} 0;{zw} 0', kt(0, 0.12, 0.35, 0.55, 0.72, 1), dur, True))
    s.rect(x, y, zw, ih, 'none', TEXT, r=0, sw=2.5)
    s.add('</g></g>')
    # 고른 파트 파란 줄: 처음 1 → 누르면 2
    s.add('<g>' + move(f'0 0;0 0;{zw} 0;{zw} 0', kt(0, 0.78, 0.785, 1), dur, True))
    s.rect(x + 5, y + ih - 6, zw - 10, 3, ACC, r=1)
    s.add('</g>')
    cy = y + ih / 2 + 20
    cursor(s, [(330, 200), (x + zw / 2, cy), (x + zw / 2, cy), (x + zw * 1.5, cy), (x + zw * 2.5, cy),
               (x + zw * 1.5, cy), (x + zw * 1.5, cy), (330, 200), (330, 200)],
           [0, 0.12, 0.3, 0.4, 0.55, 0.72, 0.92, 0.98, 1], dur)
    click(s, x + zw * 1.5, cy, 0.78, dur)
    step_text(s, 340, 60, [(0.12, 0.7, '이미지 위를 좌우로 움직이면 파트 칸이 나옴'),
                           (0.76, 0.9, '원하는 파트 칸을 누르면 그 파트를 고름'),
                           (0.9, 1.0, '파란 줄 = 고른 파트 (더블클릭 = 그 파트 재생)')], dur)
    return s.render()


def an_rating():
    dur = 8
    s = Svg(760, 150, '별점 · 물방울 누르기')
    x0, y0 = 40, 60
    s.text(x0, y0 + 6, '별점', 13, SUB)
    sx = x0 + 50
    for i in range(5):
        s.text(sx + i * 26, y0 + 10, '☆', 24, GOLD)
    # 마우스를 따라 채워지는 별 (1 → 4), 누르면 확정
    for i in range(5):
        t_in = 0.08 + i * 0.08
        if i < 4:
            s.add(f'<g opacity="0">' + anim('opacity', '0;0;1;1;0;0', kt(0, t_in, t_in + 0.01, 0.94, 0.95, 1), dur))
        else:
            s.add(f'<g opacity="0">' + anim('opacity', '0;0;0.5;0;0', kt(0, 0.38, 0.39, 0.42, 1), dur))
        s.text(sx + i * 26, y0 + 10, '★', 24, GOLD)
        s.add('</g>')
    s.add(show_between(0.5, 0.92, dur, 0.01))
    s.text(sx + 140, y0 + 6, '4  저장됨', 12, SAVE, weight='bold')
    s.add('</g>')
    # 물방울
    dx = 330
    s.path(f'M{dx},{y0 - 10} q8,11 0,17 q-8,-6 0,-17 Z', BLUE)
    s.add(show_between(0, 0.66, dur, 0.001))
    s.text(dx + 14, y0 + 6, '0', 13, SUB)
    s.add('</g>')
    s.add(show_between(0.67, 1, dur, 0.001))
    s.text(dx + 14, y0 + 6, '1', 13, TEXT, weight='bold')
    s.add('</g>')
    pts = [(sx + 5, y0 + 30), (sx + 6, y0), (sx + 26 * 3 + 6, y0), (sx + 26 * 4 + 6, y0), (sx + 26 * 3 + 6, y0),
           (sx + 26 * 3 + 6, y0), (dx, y0 - 2), (dx, y0 - 2), (sx + 5, y0 + 30)]
    cursor(s, pts, [0, 0.08, 0.32, 0.38, 0.44, 0.55, 0.62, 0.9, 1], dur)
    click(s, sx + 26 * 3 + 10, y0 - 4, 0.48, dur)
    click(s, dx + 2, y0 - 4, 0.665, dur)
    step_text(s, 440, 40, [(0.05, 0.46, '별 위로 움직이면 미리 채워짐'),
                           (0.47, 0.6, '누르면 바로 저장 (같은 별 다시 = 0점)'),
                           (0.62, 0.92, '물방울은 누를 때마다 +1')], dur)
    return s.render()


def an_tag_popup():
    dur = 6
    s = Svg(760, 190, '태그 칩에 마우스를 올리면 태그 카드')
    x0, y0 = 40, 140
    s.text(x0, y0 + 13, '태그', 12, SUB)
    cx = x0 + 44
    xs = []
    for t in ('드라마', '코미디', 'VR'):
        xs.append(cx)
        cx = s.chip(cx, y0, t, size=11, h=20)
    # 팝업 카드 (두 번째 칩 위)
    px, py = xs[1] - 30, 18
    s.add(show_between(0.3, 0.8, dur))
    s.rect(px, py, 140, 110, CARD, COMBO, r=5)
    s.path(f'M{px + 20},{py + 22} l22,0 l14,14 l-17,17 l-14,-14 Z', TEXT)
    s.circle(px + 28, py + 30, 3, CARD)
    s.text(px + 66, py + 40, '코미디', 13, TEXT, weight='bold')
    s.text(px + 66, py + 58, '(Comedy, コメディ)', 9, SUB)
    s.line(px, py + 80, px + 140, py + 80, COMBO)
    s.play(px + 58, py + 95, 6, SUB, 1)
    s.text(px + 70, py + 99, '42', 11, SUB)
    s.text(px + 128, py + 22, '♥', 12, DANGER, 'end')
    s.add('</g>')
    cursor(s, [(xs[1] + 20, y0 + 50), (xs[1] + 20, y0 + 8), (xs[1] + 20, y0 + 8), (xs[1] + 20, y0 + 50), (xs[1] + 20, y0 + 50)],
           [0, 0.15, 0.8, 0.9, 1], dur)
    step_text(s, 360, 60, [(0.15, 0.3, '태그 칩 위에 마우스를 잠깐 올려 둠'),
                           (0.3, 0.8, '태그 탭 카드와 같은 모양으로 팝업'),
                           (0.8, 1.0, '마우스를 빼면 사라짐')], dur)
    return s.render()


def an_tree_fold():
    dur = 8
    s = Svg(760, 230, '제작사 계층 접기 · 펼치기')
    def node(x, y, name, cnt, col):
        s.rect(x, y, 110, 54, col, r=4)
        s.rect(x + 28, y + 5, 54, 24, '#4A5C6B', r=2)
        s.text(x + 8, y + 45, name, 10, TEXT, weight='bold')
        s.text(x + 102, y + 45, f'▶ {cnt}', 9, SUB, 'end')
    node(150, 10, '회사 A', 180, '#2D4458')
    bx, by = 205, 74
    # 펼친 상태: 연결선 + 하위 3개
    s.add('<g>' + anim('opacity', '1;1;0;0;1;1', kt(0, 0.3, 0.36, 0.7, 0.76, 1), dur))
    s.line(bx, by + 7, bx, 94, SUB, 1.5)
    s.line(65, 94, 345, 94, SUB, 1.5)
    for i, (n, c) in enumerate((('브랜드 1', 120), ('브랜드 2', 40), ('브랜드 3', 20))):
        x = 10 + i * 140
        s.line(x + 55, 94, x + 55, 104, SUB, 1.5)
        node(x, 104, n, c, '#2F4A3E')
    s.add('</g>')
    # 단추 − / +
    s.circle(bx, by, 8, BACK, SUB, 1.3)
    s.line(bx - 4, by, bx + 4, by, TEXT, 1.6)
    s.add('<g>' + anim('opacity', '0;0;1;1;0;0', kt(0, 0.33, 0.34, 0.73, 0.74, 1), dur))
    s.line(bx, by - 4, bx, by + 4, TEXT, 1.6)
    s.text(bx + 14, by + 6, '3', 16, TEXT, weight='bold')
    s.add('</g>')
    cursor(s, [(330, 200), (bx - 2, by - 2), (bx - 2, by - 2), (300, 60), (bx - 2, by - 2), (bx - 2, by - 2), (330, 200), (330, 200)],
           [0, 0.22, 0.4, 0.55, 0.66, 0.8, 0.92, 1], dur)
    click(s, bx, by, 0.32, dur)
    click(s, bx, by, 0.72, dur)
    step_text(s, 470, 60, [(0.2, 0.36, '카드 아래 − 단추를 누름'),
                           (0.36, 0.7, '하위가 접히고 접힌 수(3)가 보임'),
                           (0.7, 1.0, '+ 를 다시 누르면 펼침 (↓ 키도 됨)')], dur)
    return s.render()


def an_drill():
    dur = 9
    s = Svg(760, 280, '배우 카드 더블클릭 → 출연작 화면')
    W, H = 440, 260
    s.add(f'<clipPath id="clipDr"><rect x="10" y="10" width="{W}" height="{H}" rx="4"/></clipPath>')
    s.rect(10, 10, W, H, '#1D272E', r=4)
    s.add('<g clip-path="url(#clipDr)">')
    # 배우 목록
    s.add('<g>' + anim('opacity', '1;1;0;0;1;1', kt(0, 0.33, 0.4, 0.82, 0.88, 1), dur))
    for i in range(3):
        x = 30 + i * 140
        s.rect(x, 30, 120, 200, CARD, r=4)
        s.rect(x, 30, 120, 150, '#3E4F5E', r=4)
        s.person(x + 60, 100, 70, TEXT, 0.75)
        s.text(x + 8, 200, '♀', 12, PINK, weight='bold')
        s.text(x + 22, 200, f'배우 {chr(65 + i)}', 11, TEXT, weight='bold')
        s.text(x + 112, 218, f'▶ {40 - i * 11}', 9, SUB, 'end')
    s.add('</g>')
    # 출연작 화면 (오른쪽에서 들어옴)
    s.add('<g opacity="0">' + anim('opacity', '0;0;1;1;0;0', kt(0, 0.36, 0.42, 0.82, 0.86, 1), dur)
          + move(f'{W} 0;{W} 0;0 0;0 0;0 0', kt(0, 0.36, 0.45, 0.85, 1), dur))
    s.button(20, 20, 92, '◀ 배우 목록', COMBO)
    s.text(122, 34, '배우: 배우 B  (영상 29편)', 11, TEXT, weight='bold')
    s.rect(20, 48, 90, 120, '#3E4F5E', r=3)
    s.person(65, 102, 64, TEXT, 0.75)
    s.text(124, 70, '배우 B', 18, TEXT, weight='bold')
    s.stars(124, 92, 4, 14)
    for k, t in enumerate(('♀ 여성 · 27세', '일본 · 160cm', '데뷔 2019')):
        s.text(124, 114 + k * 16, t, 10, SUB)
    for i in range(4):
        s.add(f'<g transform="translate({20 + i * 106},178) scale(0.7)">')
        s.video_card(0, 0, 140, f'출연작 {i + 1}', f'ABC-{300 + i}', actors=1, tags=3)
        s.add('</g>')
    s.add('</g>')
    s.add('</g>')
    cursor(s, [(420, 250), (230, 110), (230, 110), (60, 32), (60, 32), (420, 250), (420, 250)],
           [0, 0.2, 0.42, 0.66, 0.84, 0.95, 1], dur)
    click(s, 232, 112, 0.28, dur, double=True)
    click(s, 62, 34, 0.78, dur)
    step_text(s, 480, 60, [(0.2, 0.34, '배우 카드를 더블클릭 (또는 Space)'),
                           (0.36, 0.75, '배우 정보 + 출연작 화면으로'),
                           (0.76, 1.0, '[◀ 배우 목록] 이나 Backspace 로 돌아감')], dur, 11)
    s.text(480, 150, '제작사 · 태그 · 컬렉션 카드도 같은 방식', 10, SUB2)
    return s.render()


def an_paste():
    dur = 10
    s = Svg(880, 210, '웹페이지 내용 붙여넣기 흐름')
    boxes = [('① 작품 사이트', ['화면을 드래그해서', '복사 (Ctrl+C)']),
             ('② 붙여넣기 창', ['영상 오른쪽 클릭 →', '[웹페이지 내용 붙여넣기]', '→ Ctrl+V']),
             ('③ 연결 확인', ['처음 보는 제작사 · 레이블', '· 태그를 기존 항목에', '연결하거나 새로 추가']),
             ('④ 영상 상세', ['품번 · 제목 · 발매일 ·', '배우 · 제작사 · 태그가', '채워짐 → 저장'])]
    for i, (t, ls) in enumerate(boxes):
        x = 10 + i * 220
        s.rect(x, 20, 190, 150, CARD, COMBO, r=6)
        s.rect(x, 20, 190, 30, ACC if i != 3 else SAVE, r=6)
        s.rect(x, 40, 190, 10, ACC if i != 3 else SAVE, r=0)
        s.text(x + 95, 40, t, 13, TEXT, 'middle', 'bold')
        for j, l in enumerate(ls):
            s.text(x + 95, 80 + j * 20, l, 11, SUB, 'middle')
        if i < 3:
            s.path(f'M{x + 196},{95} l18,0 m-6,-6 l6,6 l-6,6', 'none', COMBO, 2.5)
    # 지금 단계 테두리 (단계마다 옮겨 감)
    s.add('<g>' + move('0 0;220 0;440 0;660 0;660 0', kt(0, 0.25, 0.5, 0.75, 1), dur, True))
    s.rect(7, 17, 196, 156, 'none', CALL, r=8, sw=3)
    s.add('</g>')
    # 문서가 화살표를 따라 이동
    s.add('<g>' + move('0 0;0 0;220 0;220 0;440 0;440 0;660 0;660 0',
                       kt(0, 0.18, 0.25, 0.43, 0.5, 0.68, 0.75, 1), dur))
    s.rect(160, 140, 22, 26, TEXT, r=2, op=0.9)
    for k in range(3):
        s.line(164, 147 + k * 6, 178, 147 + k * 6, COMBO, 2)
    s.add('</g>')
    s.text(440, 200, '.txt 파일을 영상 카드에 끌어다 놓아도 ② 와 같이 처리됩니다', 11, SUB2, 'middle')
    return s.render()
