# SVG 도식 도우미 (RuliManager 설명서용) - 프로그램 Stash 테마 색
BACK = '#202B33'; EDIT = '#1B252C'; COMBO = '#394B59'; CARD = '#30404D'
TEXT = '#F5F8FA'; SUB = '#A7B6C2'; SUB2 = '#738694'; ACC = '#137CBD'
SAVE = '#0F9960'; DANGER = '#DB3737'; EDGE = '#10161A'; HOT = '#48AFF0'
PINK = '#FF66A1'; BLUE = '#48AFF0'; GOLD = '#F2B824'; CALL = '#F29D49'
FONT = "'Malgun Gothic','맑은 고딕',sans-serif"


def esc(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


class Svg:
    def __init__(self, w, h, label):
        self.w, self.h, self.label, self.parts = w, h, label, []

    def add(self, s):
        self.parts.append(s)
        return self

    def rect(self, x, y, w, h, fill=CARD, stroke=None, r=3, sw=1, op=None, dash=None):
        a = f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}"'
        if stroke:
            a += f' stroke="{stroke}" stroke-width="{sw}"'
        if op is not None:
            a += f' opacity="{op}"'
        if dash:
            a += f' stroke-dasharray="{dash}"'
        return self.add(a + '/>')

    def text(self, x, y, s, size=12, fill=TEXT, anchor='start', weight='normal'):
        return self.add(f'<text x="{x}" y="{y}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" '
                        f'font-weight="{weight}">{esc(s)}</text>')

    def line(self, x1, y1, x2, y2, stroke=COMBO, sw=1, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ''
        return self.add(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{stroke}" stroke-width="{sw}"{d}/>')

    def path(self, d, fill='none', stroke=None, sw=1):
        s = f' stroke="{stroke}" stroke-width="{sw}"' if stroke else ''
        return self.add(f'<path d="{d}" fill="{fill}"{s}/>')

    def circle(self, cx, cy, r, fill, stroke=None, sw=1):
        s = f' stroke="{stroke}" stroke-width="{sw}"' if stroke else ''
        return self.add(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{fill}"{s}/>')

    def button(self, x, y, w, s, fill=ACC, h=20, size=11, fg=TEXT):
        self.rect(x, y, w, h, fill, r=3)
        return self.text(x + w / 2, y + h / 2 + size * 0.36, s, size, fg, 'middle')

    def edit(self, x, y, w, s='', h=20, size=11):
        self.rect(x, y, w, h, EDIT, EDGE, r=2)
        if s:
            self.text(x + 6, y + h / 2 + size * 0.36, s, size, SUB)
        return self

    def chip(self, x, y, s, fill=COMBO, size=10, h=18, fg=TEXT, x_mark=True):
        w = len(s) * size * 0.95 + (26 if x_mark else 14)
        self.rect(x, y, w, h, fill, r=h / 2)
        self.text(x + 8, y + h / 2 + size * 0.36, s + (' ×' if x_mark else ''), size, fg)
        return x + w + 5

    def call(self, x, y, n):
        """번호 풍선 (설명 목록의 번호와 맞춤)"""
        self.circle(x, y, 10, CALL, EDGE, 1.5)
        return self.text(x, y + 4, str(n), 11, '#10161A', 'middle', 'bold')

    def play(self, cx, cy, r, fill=TEXT, op=0.85):
        self.add(f'<g opacity="{op}">')
        self.circle(cx, cy, r, 'none', fill, max(1.5, r / 7))
        self.path(f'M{cx - r * 0.3},{cy - r * 0.45} L{cx + r * 0.5},{cy} L{cx - r * 0.3},{cy + r * 0.45} Z', fill)
        return self.add('</g>')

    def person(self, cx, cy, s, fill=TEXT, op=0.8):
        # 사람 실루엣 (머리 + 어깨)
        self.add(f'<g opacity="{op}">')
        self.circle(cx, cy - s * 0.35, s * 0.28, fill)
        self.path(f'M{cx - s * 0.55},{cy + s * 0.55} Q{cx - s * 0.55},{cy + s * 0.02} {cx},{cy + s * 0.02} '
                  f'Q{cx + s * 0.55},{cy + s * 0.02} {cx + s * 0.55},{cy + s * 0.55} Z', fill)
        return self.add('</g>')

    def stars(self, x, y, n, size=11):
        s = ''.join('★' if i < n else '☆' for i in range(5))
        return self.text(x, y, s, size, GOLD)

    def window(self, x, y, w, h, title):
        self.rect(x, y, w, h, BACK, EDGE, r=6, sw=1.5)
        self.add(f'<path d="M{x},{y + 26} L{x},{y + 6} Q{x},{y} {x + 6},{y} L{x + w - 6},{y} Q{x + w},{y} {x + w},{y + 6} '
                 f'L{x + w},{y + 26} Z" fill="#182026"/>')
        self.text(x + 12, y + 17, title, 11, SUB)
        for i, c in enumerate(('—', '☐', '✕')):
            self.text(x + w - 60 + i * 20, y + 17, c, 10, SUB2, 'middle')
        return self

    def video_card(self, x, y, w, title='제목', code='ABC-123', res='1080p', grade='FHD', time='2:03:15',
                   stars=0, actors=2, tags=5, drops=0, cover=None, check=False, temp=False):
        ih = w * 9 / 16
        self.rect(x, y, w, ih + 58, CARD, r=4)
        self.rect(x, y, w, ih, cover or '#3B5163', r=4)
        self.play(x + w / 2, y + ih / 2, ih * 0.18, op=0.35)
        self.text(x + w - 5, y + ih - 5, time, 9, TEXT, 'end')
        if stars:
            self.path(f'M{x},{y + 24} L{x + 24},{y} L{x + 38},{y} L{x},{y + 38} Z', GOLD)
            self.text(x + 10, y + 21, str(stars), 9, '#10161A', 'middle', 'bold')
        if check:
            self.rect(x + 5, y + 5, 13, 13, ACC, r=3)
            self.path(f'M{x + 8},{y + 11.5} l3,3 l5,-6', 'none', TEXT, 1.6)
        ty = y + ih + 14
        self.text(x + 6, ty, title, 10, TEXT, weight='bold')
        if temp:
            self.text(x + w - 6, ty, '● 임시', 9, CALL, 'end')
        self.text(x + 6, ty + 15, code, 9, SUB)
        bw = 58
        self.rect(x + w - bw - 6, ty + 5, bw, 13, CARD, SUB, r=1)
        self.text(x + w - bw - 3, ty + 15, res, 8, SUB, weight='bold')
        self.rect(x + w - 26, ty + 5, 20, 13, SUB, r=0)
        self.text(x + w - 16, ty + 15, grade, 7, CARD, 'middle', 'bold')
        self.line(x, ty + 24, x + w, ty + 24, COMBO)
        cx = x + 8
        self.person(cx + 4, ty + 35, 9, SUB, 1)
        self.text(cx + 12, ty + 38, str(actors), 9, SUB)
        cx += 30
        self.path(f'M{cx},{ty + 31} l6,0 l5,5 l-6,6 l-5,-5 Z', SUB)
        self.text(cx + 14, ty + 38, str(tags), 9, SUB)
        if drops:
            cx += 30
            self.path(f'M{cx + 4},{ty + 29} q5,7 0,11 q-5,-4 0,-11 Z', BLUE)
            self.text(cx + 12, ty + 38, str(drops), 9, SUB)
        return ih + 58

    def render(self):
        body = '\n'.join(self.parts)
        return (f'<svg viewBox="0 0 {self.w} {self.h}" role="img" aria-label="{esc(self.label)}" '
                f'xmlns="http://www.w3.org/2000/svg" font-family="{FONT}">\n{body}\n</svg>')
