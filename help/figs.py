from svg import *


def fig_main():
    s = Svg(900, 540, '메인 화면 구성')
    s.window(0, 0, 900, 540, 'RuliManager v0.83')
    # 위 1줄: 폴더 · 새로고침 · 목록 관리 · 검색 · 설정
    x = 12
    for t, w in (('폴더 관리', 70), ('새로고침 (F5)', 84), ('재 스캔', 58), ('목록 관리 ▾', 78)):
        s.button(x, 36, w, t)
        x += w + 6
    s.text(x + 8, 50, '검색', 11, SUB)
    s.edit(x + 36, 36, 200, '이름 · 품번 · 배우 …')
    s.circle(876, 46, 9, COMBO)
    s.text(876, 50, '⚙', 11, TEXT, 'middle')
    # 탭
    x = 12
    for i, t in enumerate(('영상', '배우', '제작사', '태그', '컬렉션')):
        w = 54 if len(t) == 2 else 62
        s.button(x, 66, w, t, ACC if i == 0 else COMBO)
        x += w + 4
    # 격자 영역 + 안쪽 도구줄
    s.rect(12, 94, 600, 400, '#1D272E', r=2)
    s.edit(20, 102, 70, '정렬 ▾')
    s.button(92, 102, 22, '▲', COMBO)
    bx = 124
    for i, ic in enumerate(('▦', '☰', '▤', '▣')):
        s.button(bx + i * 24, 102, 22, ic, ACC if i == 0 else COMBO)
    s.text(232, 116, '[격자] [리스트] [패키지] [월 모드]', 9, SUB2)
    s.line(450, 112, 600, 112, COMBO, 3)
    s.circle(500, 112, 6, ACC)
    s.text(450, 104, '확대', 9, SUB2)
    cx = 20
    for t, f in (('전체 128건', SAVE), ('드라마', SAVE), ('VR', SAVE), ('코미디', SAVE)):
        cx = s.chip(cx, 128, t, f, x_mark=False)
    s.button(cx + 4, 128, 56, '장르숨김', COMBO, 18, 9)
    px = 20
    for t in ('«', '<', '1', '2', '3', '>', '»'):
        s.button(px, 152, 18, t, ACC if t == '1' else COMBO, 18, 9)
        px += 21
    s.text(px + 8, 165, '1 / 3 페이지 · 128편', 9, SUB2)
    covers = ['#3B5163', '#4A3F5C', '#3F5A4B', '#5C4A3B', '#3B4E5C', '#55435A', '#3F4F5A', '#4F5A3F']
    for i in range(8):
        r, c = divmod(i, 4)
        s.video_card(24 + c * 146, 180 + r * 150, 136, f'영상 제목 {i + 1}', f'ABC-{101 + i}',
                     stars=(4 if i == 1 else 0), actors=1 + i % 3, tags=3 + i, drops=(2 if i == 2 else 0),
                     cover=covers[i], check=(i == 5), temp=(i == 7))
    # 상세 패널
    s.rect(620, 94, 268, 400, '#1D272E', r=2)
    s.rect(628, 102, 252, 142, '#3B5163', r=3)
    s.play(754, 173, 22, op=0.35)
    s.text(628, 260, '123분 | fps: 29.97 | 1080p', 9, SUB)
    rows = [('품번', 'ABC-102'), ('제목', '영상 제목 2'), ('별점', None), ('발매일', '2024-05-17')]
    y = 270
    for lbl, val in rows:
        s.text(628, y + 13, lbl, 10, SUB)
        if val is None:
            s.stars(664, y + 14, 4, 13)
            s.path(f'M760,{y + 3} q6,8 0,12 q-6,-4 0,-12 Z', BLUE)
            s.text(770, y + 14, '2', 10, SUB)
        else:
            s.edit(664, y, 216, val, 18, 10)
        y += 24
    s.text(628, y + 13, '배우', 10, SUB)
    s.chip(664, y, '♀ 배우 이름')
    y += 24
    s.text(628, y + 13, '제작사', 10, SUB)
    s.rect(664, y, 104, 30, CARD, r=3)
    s.text(672, y + 19, '제작사', 10, TEXT)
    s.rect(774, y, 106, 30, '#2B3A46', r=3)
    s.text(782, y + 19, '레이블', 10, TEXT)
    y += 36
    s.text(628, y + 13, '태그', 10, SUB)
    cx = 664
    for t in ('드라마', '코미디'):
        cx = s.chip(cx, y, t)
    y += 24
    s.text(628, y + 13, '메모', 10, SUB)
    s.edit(664, y, 216, '', 40)
    s.button(820, 470, 60, '저장', SAVE)
    # 아래 버튼
    x = 12
    for t, w, f in (('재생 (기본 플레이어)', 116, ACC), ('DB 삭제 (Del)', 90, DANGER), ('탐색기에서 보기', 98, ACC),
                    ('DB 폴더 열기', 86, ACC), ('DB 반영', 70, SAVE), ('정보 txt 생성', 88, ACC)):
        s.button(x, 506, w, t, f)
        x += w + 6
    for n, (cx, cy) in enumerate([(12, 30), (250, 30), (885, 30), (12, 60), (12, 98), (24, 176), (620, 98), (12, 500)], 1):
        s.call(cx, cy, n)
    return s.render()


def fig_card():
    s = Svg(560, 220, '영상 카드 각 부분')
    s.add('<g transform="translate(190,16) scale(1.45)">')
    s.video_card(0, 0, 130, '영상 제목', 'ABC-123', stars=4, actors=2, tags=6, drops=3, check=True)
    s.add('</g>')
    pts = [((200, 30), (120, 30), ''), ((215, 40), (120, 70), ''), ((370, 116), (470, 112), ''),
           ((240, 140), (120, 136), ''), ((230, 162), (120, 166), ''), ((350, 160), (470, 160), ''),
           ((215, 197), (120, 200), '')]
    for i, ((ax, ay), (tx, ty), t) in enumerate(pts, 1):
        s.line(ax, ay, tx, ty, CALL, 1, '3 2')
        s.call(tx, ty, i)
    return s.render()


def fig_views():
    s = Svg(880, 190, '영상 보기 방식 네 가지')
    titles = ['격자', '리스트', '패키지', '월 모드']
    for k, t in enumerate(titles):
        ox = 10 + k * 218
        s.rect(ox, 10, 204, 140, '#1D272E', COMBO, r=4)
        if k == 0:
            for i in range(6):
                r, c = divmod(i, 3)
                x, y = ox + 10 + c * 63, 20 + r * 62
                s.rect(x, y, 58, 56, CARD, r=3)
                s.rect(x, y, 58, 32, '#3B5163', r=3)
                s.line(x + 5, y + 40, x + 45, y + 40, SUB, 3)
                s.line(x + 5, y + 48, x + 30, y + 48, SUB2, 2)
        elif k == 1:
            s.rect(ox + 8, 18, 188, 14, COMBO, r=1)
            for i in range(7):
                y = 36 + i * 15
                s.rect(ox + 8, y, 188, 13, ACC if i == 2 else CARD, r=1, op=1 if i == 2 else 0.6)
                s.line(ox + 14, y + 7, ox + 70, y + 7, SUB, 2)
                s.line(ox + 90, y + 7, ox + 140, y + 7, SUB2, 2)
        elif k == 2:
            for i in range(10):
                r, c = divmod(i, 5)
                x, y = ox + 10 + c * 38, 20 + r * 62
                s.rect(x, y, 34, 56, ['#3B5163', '#4A3F5C', '#3F5A4B', '#5C4A3B', '#55435A'][i % 5], r=2)
        else:
            for i in range(9):
                r, c = divmod(i, 3)
                x, y = ox + 6 + c * 64, 16 + r * 44
                s.rect(x, y, 62, 42, ['#3B5163', '#4A3F5C', '#3F5A4B'][(i + r) % 3], r=1)
                s.play(x + 31, y + 21, 7, op=0.6)
                s.line(x + 4, y + 37, x + 34, y + 37, TEXT, 2)
        s.text(ox + 102, 172, t, 13, TEXT, 'middle', 'bold')
    return s.render()


def fig_split():
    s = Svg(560, 230, '분할 영상 합쳐 보기 카드')
    s.add('<g transform="translate(30,14) scale(1.5)">')
    s.video_card(0, 0, 150, '영상 제목 (1/3)', 'ABC-123', time='3:12:40', actors=1, tags=4)
    s.add('</g>')
    ih = 150 * 9 / 16 * 1.5
    w = 150 * 1.5
    for i in range(3):
        x = 30 + i * w / 3
        if i:
            s.line(x, 14, x, 14 + ih, TEXT, 1.5)
        s.text(x + w / 6, 14 + ih / 2 + 8, str(i + 1), 26, TEXT, 'middle', 'bold')
    s.rect(30 + w / 3, 14, w / 3, ih, 'none', TEXT, r=0, sw=2)
    s.rect(30 + w / 3 + 4, 14 + ih - 6, w / 3 - 8, 3, ACC, r=1)
    tx = 300
    lines = ['ABC-123_1.mp4', 'ABC-123_2.mp4', 'ABC-123_3.mp4']
    for i, t in enumerate(lines):
        s.rect(tx, 30 + i * 30, 200, 22, EDIT, EDGE, r=2)
        s.text(tx + 8, 45 + i * 30, t, 11, TEXT if i == 1 else SUB)
    s.text(tx, 136, '파일 이름의 마지막 _숫자 가 같은 묶음 = 분할 영상', 10, SUB)
    s.text(tx, 156, '카드 이미지 위를 파트 수만큼 나눠 고름', 10, SUB)
    s.text(tx, 176, '흰 테두리 = 마우스 위치 · 파란 줄 = 고른 파트', 10, SUB)
    s.text(tx, 196, '재생 시간 = 파트 시간의 합', 10, SUB)
    return s.render()


def fig_detail():
    s = Svg(560, 470, '영상 상세 화면')
    s.rect(0, 0, 330, 470, '#1D272E', r=4)
    s.rect(10, 10, 310, 174, '#3B5163', r=3)
    s.play(165, 97, 26, op=0.35)
    s.button(150, 190, 80, '이미지 검색...', ACC, 18, 9)
    s.button(236, 190, 84, '이미지 변경...', ACC, 18, 9)
    s.text(10, 203, '123분 | fps 29.97 | 1080p', 9, SUB)
    y = 216
    s.text(10, y + 13, '품번', 10, SUB)
    s.edit(52, y, 242, 'ABC-123', 18, 10)
    s.button(298, y, 22, '⧉', COMBO, 18, 10)
    y += 24
    s.text(10, y + 13, '제목', 10, SUB)
    s.edit(52, y, 268, '영상 제목', 30, 10)
    y += 36
    s.text(10, y + 13, '별점', 10, SUB)
    s.stars(52, y + 14, 3, 14)
    s.path(f'M150,{y + 3} q6,8 0,12 q-6,-4 0,-12 Z', BLUE)
    s.text(160, y + 14, '0', 10, SUB)
    y += 24
    s.text(10, y + 13, '발매일', 10, SUB)
    s.edit(52, y, 110, '2024-05-17', 18, 10)
    s.edit(168, y, 90, '해당없음 ▾', 18, 10)
    y += 24
    s.text(10, y + 13, '배우', 10, SUB)
    cx = s.chip(52, y, '♀ 배우 A')
    s.chip(cx, y, '♀ 배우 B')
    y += 24
    s.text(10, y + 13, '제작사', 10, SUB)
    s.rect(52, y, 130, 32, CARD, r=3)
    s.text(60, y + 20, '제작사 이름', 10, TEXT)
    s.rect(188, y, 132, 32, '#2B3A46', r=3)
    s.text(196, y + 20, '레이블 이름', 10, TEXT)
    y += 38
    s.text(10, y + 13, '시리즈', 10, SUB)
    s.edit(52, y, 268, '', 18)
    y += 24
    s.text(10, y + 13, '태그', 10, SUB)
    cx = 52
    for t in ('드라마', '코미디', '+ 입력'):
        cx = s.chip(cx, y, t, COMBO if t[0] != '+' else EDIT, x_mark=(t[0] != '+'))
    y += 24
    s.text(10, y + 13, '메모', 10, SUB)
    s.edit(52, y, 268, '', 34)
    s.button(260, 440, 60, '저장', SAVE)
    notes = [(14, '이미지 영역 - 이미지 파일을 끌어다 놓으면 교체, 더블클릭 = 원본 보기'),
             (225, '품번 - 오른쪽 단추로 복사, 품번으로 제작사 · 레이블 자동 연결'),
             (285, '별점(클릭 즉시 저장) · 물방울(클릭마다 +1, 오른쪽 클릭 = 초기화)'),
             (333, '배우 칩 - 오른쪽 클릭 = 별칭 고르기, 더블클릭 = 배우로 이동'),
             (358, '제작사 · 레이블 카드 - 더블클릭 = 바꾸기'),
             (417, '태그 칩 - 입력하면 자동 완성, ⌄ = 목록에서 고르기'),
             (450, '저장 - 다른 영상을 고르면 자동 저장')]
    for i, (yy, t) in enumerate(notes, 1):
        s.line(326, yy, 344, yy, CALL, 1)
        s.call(354, yy, i)
        words, line, ly = t.split(' '), '', yy + 4
        out = []
        for w in words:
            if len(line) + len(w) > 17:
                out.append(line)
                line = w
            else:
                line = (line + ' ' + w).strip()
        out.append(line)
        for j, l in enumerate(out):
            s.text(370, ly + j * 13, l, 10, SUB)
    return s.render()


def fig_paste():
    s = Svg(880, 200, '웹페이지 내용 붙여넣기 흐름')
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
            s.path(f'M{x + 196},{95} l18,0 m-6,-6 l6,6 l-6,6', 'none', CALL, 2.5)
    s.text(440, 192, '.txt 파일을 영상 카드에 끌어다 놓아도 ② 와 같이 처리됩니다', 11, SUB2, 'middle')
    return s.render()


def fig_actor():
    s = Svg(880, 230, '배우 카드와 트리맵 보기')
    for i in range(3):
        x = 10 + i * 120
        s.rect(x, 10, 110, 200, CARD, r=4)
        s.rect(x, 10, 110, 146, '#3E4F5E', r=4)
        s.person(x + 55, 80, 70, TEXT, 0.75)
        if i == 0:
            s.path(f'M{x},{34} L{x + 24},{10} L{x + 38},{10} L{x},{48} Z', GOLD)
            s.text(x + 10, 31, '5', 9, '#10161A', 'middle', 'bold')
        s.text(x + 100, 30, '♥' if i < 2 else '', 13, DANGER, 'end')
        s.rect(x + 82, 136, 20, 13, '#BC002D' if i else '#FFFFFF', r=1)
        if not i:
            s.circle(x + 92, 142.5, 4, '#BC002D')
        s.text(x + 8, 174, '♀', 12, PINK, weight='bold')
        s.text(x + 22, 174, f'배우 {chr(65 + i)}', 11, TEXT, weight='bold')
        s.text(x + 8, 192, f'{24 + i * 3}세', 9, SUB)
        s.text(x + 102, 192, f'▶ {40 - i * 11}', 9, SUB, 'end')
    s.text(185, 226, '배우 카드: 별점 리본 · 즐겨찾기 ♥ · 국기 · 성별 · 나이 · 출연 수', 10, SUB2, 'middle')
    # 트리맵
    tx, ty, tw, th = 400, 10, 470, 200
    blocks = [(0, 0, 0.45, 1, '#2B6C9E', '배우 A', '40편 · 31%'),
              (0.45, 0, 0.55, 0.55, '#7A4F8F', '배우 B', '29편 · 22%'),
              (0.45, 0.55, 0.3, 0.45, '#3F7A5B', '배우 C', '18편'),
              (0.75, 0.55, 0.25, 0.25, '#8F6A3F', '배우 D', ''),
              (0.75, 0.8, 0.25, 0.2, '#5C4A3B', '', '')]
    for bx, by, bw, bh, c, n, v in blocks:
        x, y, w, h = tx + bx * tw, ty + by * th, bw * tw, bh * th
        s.rect(x + 1, y + 1, w - 2, h - 2, c, r=2)
        if n:
            fs = max(10, min(22, h / 4))
            s.text(x + 8, y + fs + 6, n, fs, TEXT, weight='bold')
            if v:
                s.text(x + 8, y + fs * 2 + 10, v, max(9, fs * 0.6), TEXT)
    s.text(635, 226, '트리맵: 칸 크기 = 출연 수 (기준 콤보로 바꿈)', 10, SUB2, 'middle')
    return s.render()


def fig_tree():
    s = Svg(880, 280, '제작사 계층 보기')
    def node(x, y, name, cnt, lvl):
        col = ['#30404D', '#2D4458', '#2F4A3E'][lvl]
        s.rect(x, y, 120, 64, col, r=4)
        s.rect(x + 30, y + 6, 60, 30, '#4A5C6B', r=2)
        s.text(x + 60, y + 26, 'LOGO', 9, SUB, 'middle')
        s.text(x + 8, y + 52, name, 10, TEXT, weight='bold')
        s.text(x + 112, y + 52, f'▶ {cnt}', 9, SUB, 'end')
    def btn(x, y, plus=False):
        s.circle(x, y, 7, BACK, SUB, 1.2)
        s.line(x - 3.5, y, x + 3.5, y, TEXT, 1.4)
        if plus:
            s.line(x, y - 3.5, x, y + 3.5, TEXT, 1.4)
    node(20, 10, '그룹', 320, 0)
    btn(80, 82)
    s.line(80, 89, 80, 102, SUB, 1.5)
    s.line(80, 102, 380, 102, SUB, 1.5)
    for i, (n, c, plus) in enumerate((('회사 A', 180, False), ('회사 B', 95, True), ('회사 C', 45, False))):
        x = 20 + i * 150
        s.line(x + 60, 102, x + 60, 114, SUB, 1.5)
        node(x, 114, n, c, 1)
        if i == 0:
            btn(x + 60, 186)
            s.line(80, 193, 80, 206, SUB, 1.5)
            s.line(80, 206, 230, 206, SUB, 1.5)
            for j, (m, d) in enumerate((('브랜드 1', 120), ('브랜드 2', 60))):
                s.line(20 + j * 150 + 60, 206, 20 + j * 150 + 60, 212, SUB, 1.5)
                node(20 + j * 150, 212, m, d, 2)
        if plus:
            btn(x + 60, 186, True)
            s.text(x + 72, 191, '3', 13, TEXT, weight='bold')
    tx = 500
    s.text(tx, 30, '[계층] 보기', 13, TEXT, weight='bold')
    for i, t in enumerate(['위 → 아래로 상위 · 하위 제작사', '− / + = 접기 · 펼치기 (접힌 하위 수 표시)',
                           '카드 배경색 = 레벨 (그룹 › 회사 › 브랜드)', '상위 · 하위가 없는 제작사는 아래 격자로',
                           '← → 읽는 순서, ↑ 상위로, ↓ 첫 하위로', '더블클릭 = 제작사 영상 화면']):
        s.text(tx, 58 + i * 22, '· ' + t, 11, SUB)
    return s.render()


def fig_series():
    s = Svg(880, 330, '품번 관리 창')
    s.window(0, 0, 880, 330, '품번 관리')
    s.edit(12, 36, 200, '검색')
    s.rect(12, 62, 520, 22, COMBO, r=0)
    for x, t in ((20, '제작사'), (140, '품번 접두어'), (300, '레이블')):
        s.text(x, 77, t, 10, TEXT, weight='bold')
    rows = [('제작사 A', 'ABC', '레이블 1', 0), ('', 'ABD', '레이블 2', 0), ('제작사 B', 'XYZ', '(*복합)', 1),
            ('제작사 C', 'T28', '레이블 3', 0)]
    y = 84
    for i, (st, pre, lb, comp) in enumerate(rows):
        bg = CARD if (i < 2 or i == 3) else '#2A3743'
        if i == 2:
            bg = ACC
        s.rect(12, y, 520, 26, bg, r=0, op=1 if i != 2 else 0.85)
        s.text(20, y + 17, st, 10, TEXT)
        s.text(140, y + 17, pre, 10, TEXT)
        s.text(300, y + 17, lb, 10, '#FFC940' if comp else TEXT)
        y += 26
    s.button(12, 300, 70, '새 품번', ACC)
    s.button(88, 300, 54, '삭제', DANGER)
    s.button(148, 300, 132, '웹페이지 붙여넣기...', COMBO)
    rx = 548
    for lbl, val in (('제작사', '제작사 B'), ('품번 접두어', 'XYZ'), ('구분', '복합 ▾')):
        s.text(rx, 50 + 0, '', 1)
    y = 40
    for lbl, val in (('제작사', '제작사 B'), ('품번 접두어', 'XYZ'), ('구분', '복합 ▾')):
        s.text(rx, y + 14, lbl, 10, SUB)
        s.edit(rx + 70, y, 250, val, 20, 10)
        y += 28
    s.text(rx, y + 14, '레이블명', 10, SUB)
    s.edit(rx + 70, y, 120, '레이블 X ▾', 20, 10)
    bx = rx + 194
    for t, w in (('추가', 34), ('삭제', 34), ('▲', 20), ('▼', 20)):
        s.button(bx, y, w, t, COMBO, 20, 9)
        bx += w + 3
    y += 28
    s.text(rx, y + 14, '뒷번호', 10, SUB)
    s.rect(rx + 70, y, 250, 80, EDIT, EDGE, r=2)
    for i, t in enumerate(('001', '002', '015')):
        s.text(rx + 78, y + 16 + i * 16, t, 10, SUB)
    s.text(rx, y + 104, '복합 = 같은 접두어를 뒷번호로 나눠 여러 레이블로', 9, SUB2)
    s.button(800, 300, 68, '저장', SAVE)
    return s.render()


def fig_code():
    s = Svg(700, 150, '파일 이름에서 품번 읽기')
    x = 30
    parts = [('ABC', ACC, '접두어'), ('-', COMBO, ''), ('123', SAVE, '번호'), ('_2', CALL, '파트'),
             ('.mp4', COMBO, '')]
    for t, c, lbl in parts:
        w = len(t) * 26 + 20
        s.rect(x, 30, w, 50, c, r=4)
        s.text(x + w / 2, 64, t, 26, TEXT, 'middle', 'bold')
        if lbl:
            s.text(x + w / 2, 104, lbl, 12, SUB, 'middle')
        x += w + 6
    s.text(x + 20, 50, '→ 품번 ABC-123', 13, TEXT, weight='bold')
    s.text(x + 20, 72, '→ 품번 관리에서 ABC 로', 12, SUB)
    s.text(x + 20, 90, '   제작사 · 레이블 찾기', 12, SUB)
    s.text(30, 136, '영문 2~6자 + 숫자 2~6자 (예: HODV-021348), 영문 · 숫자가 섞인 접두어는 - 나 _ 로 구분 (예: T28-498)', 10, SUB2)
    return s.render()


def fig_collection():
    s = Svg(880, 172, '컬렉션 영상 화면')
    s.button(10, 10, 100, '◀ 컬렉션 목록', COMBO)
    s.rect(10, 40, 220, 124, '#4A3F5C', r=4)
    s.path('M60,70 h120 v64 h-120 Z M60,70 v64 M180,70 v64', 'none', TEXT, 2)
    for i in range(4):
        s.rect(64, 76 + i * 15, 8, 8, TEXT, r=1, op=0.7)
        s.rect(168, 76 + i * 15, 8, 8, TEXT, r=1, op=0.7)
    s.text(244, 62, '컬렉션 이름', 16, TEXT, weight='bold')
    s.text(244, 86, '영상 12편 · 18시간 40분', 11, SUB)
    s.text(244, 108, '메모: 직접 고른 영상 묶음', 11, SUB)
    for i in range(4):
        s.add(f'<g transform="translate({450 + i * 106},40) scale(0.72)">')
        s.video_card(0, 0, 136, f'영상 {i + 1}', f'ABC-{200 + i}', actors=1, tags=2)
        s.add('</g>')
    return s.render()
