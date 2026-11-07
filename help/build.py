import figs, anim, sys

VERSION = sys.argv[1] if len(sys.argv) > 1 else '0.84'


def fig(svg, cap):
    return f'<figure>{svg}<figcaption>{cap}</figcaption></figure>'


def afig(svg, cap):
    # 움직이는 그림: 누르면 멈춤 / 다시 재생
    return f'<figure class="anim" title="누르면 멈춤 / 다시 재생">{svg}<figcaption><span class="badge">움직이는 그림</span> {cap}</figcaption></figure>'


def ol(items):
    return '<ol class="callouts">' + ''.join(f'<li>{i}</li>' for i in items) + '</ol>'


def ul(items):
    return '<ul>' + ''.join(f'<li>{i}</li>' for i in items) + '</ul>'


def tip(t):
    return f'<p class="tip"><b>팁</b> {t}</p>'


def note(t):
    return f'<p class="note"><b>주의</b> {t}</p>'


K = lambda k: f'<kbd>{k}</kbd>'

S = []   # (id, 제목, 본문, 목차 단계)

S.append(('start', '시작하기', f'''
<p>RuliManager 는 PC 에 있는 영상 파일을 배우 · 제작사 · 태그 · 컬렉션으로 정리해서 보는 프로그램입니다.
영상 파일은 그대로 두고, 정보(제목 · 배우 · 별점 등)는 프로그램 DB 에 따로 저장합니다.</p>
<h3>처음 쓸 때</h3>
<ol>
<li>위쪽 <b>[폴더 관리]</b> 에서 영상이 들어 있는 폴더를 추가합니다. 하위 폴더까지 모두 찾습니다
(mp4 · mkv · avi · wmv · mov · webm · ts 등). USB 메모리 · 외장 하드에 있는 폴더는 물어볼 때 <b>[예]</b> 를 고르면 이동식 디스크로 등록됩니다.</li>
<li>새로 찾은 영상은 카드에 <span class="temp">● 임시</span> 로 표시됩니다. 영상을 고르고 정보를 고친 뒤 <b>[저장]</b> 하거나 다른 영상을 고르면 정식으로 등록됩니다.</li>
<li>영상 폴더에 같은 이름의 이미지(<code>ABC-123.jpg</code> 등)가 있으면 카드 이미지로, 텍스트 파일(<code>ABC-123.txt</code>)이 있으면 영상 정보로 자동으로 읽습니다.</li>
<li>폴더 구조로 배우 · 제작사를 알아냅니다: <code>폴더\\배우\\영상\\영상.mp4</code> → 배우, <code>폴더\\제작사\\배우\\영상\\영상.mp4</code> → 제작사 · 배우.</li>
<li>파일 이름에서 품번(예: <code>ABC-123</code>)을 읽고, <a href="#series">품번 관리</a>에 등록된 접두어면 제작사 · 레이블도 자동으로 정해집니다.</li>
</ol>
{fig(figs.fig_code(), '파일 이름에서 품번 · 분할 파트 읽기')}
<h3>정보 저장과 DB 반영</h3>
<p>프로그램을 쓰는 동안 바꾼 정보는 작업용 사본에 저장되고, 아래쪽 초록색 <b>[DB 반영]</b> 을 누르거나 프로그램을 닫을 때 원본 DB 에 반영됩니다.
반영할 내용이 있을 때만 [DB 반영] 이 켜집니다. 자세한 내용은 <a href="#data">데이터와 백업</a>을 보세요.</p>
{tip(f'어느 화면에서든 {K("F1")} 을 누르면 그 화면의 설명이 열립니다.')}
''', 1))

S.append(('layout', '화면 구성', f'''
{fig(figs.fig_main(), '메인 화면 (영상 탭)')}
{ol([
 '<b>폴더 · 새로고침</b> - [폴더 관리] 로 등록 폴더 추가 · 제거, [새로고침] ' + K('F5') + ' 은 새 파일 찾기, [재 스캔] 은 저장된 영상에도 폴더 구조를 다시 적용해 빈 배우 · 제작사 · 발매일을 채웁니다.',
 '<b>[목록 관리 ▾] · [관리 도구 ▾] · 검색</b> - [목록 관리 ▾] 는 배우 / 제작사 / 태그 / 품번 관리 창, [관리 도구 ▾] 는 파일 관리 · 영상 검사를 엽니다. 검색 칸은 이름 · 품번 · 제목 · 배우 · 제작사 · 태그를 바로 찾습니다 (띄어 쓰면 모두 포함하는 것만).',
 '<b>설정 (톱니바퀴)</b> - <a href="#settings">설정 창</a>. 미리보기를 만드는 동안에는 톱니바퀴가 돌고, 오른쪽 클릭으로 만들기를 멈출 수 있습니다.',
 '<b>탭</b> - <a href="#video">영상</a> · <a href="#actor">배우</a> · <a href="#studio">제작사</a> · <a href="#tag">태그</a> · <a href="#collection">컬렉션</a>.',
 '<b>도구줄</b> - 정렬 · 방향, 보기 방식, 확대 슬라이더, 장르 칩, 페이지 단추. 카드와 함께 스크롤됩니다.',
 '<b>카드 격자</b> - 고르면 오른쪽에 상세 정보, 더블클릭 · ' + K('Enter') + ' · ' + K('Space') + ' 는 재생(영상) 또는 들어가기(<a href="#actor-drill">배우 출연작</a> · <a href="#studio-drill">제작사 영상 화면</a> · 태그 · 컬렉션).',
 '<b>상세 화면</b> - 고른 항목의 정보. <a href="#detail">영상 상세</a> 참고. 격자와 사이의 얇은 바를 누르면 접고 펼 수 있고, 설정에서 왼쪽 / 오른쪽 위치를 바꿀 수 있습니다.',
 '<b>아래 단추</b> - 재생, DB 에서 삭제 (파일은 지우지 않음), 탐색기에서 보기, DB 폴더 열기, DB 반영, 정보 txt 생성.'])}
<h3>영상 카드</h3>
{fig(figs.fig_card(), '영상 카드 각 부분')}
{ol(['<b>체크 칸</b> - 마우스를 올리면 나타남. 여러 영상을 체크해서 한 번에 삭제 · 미리보기 만들기 등을 합니다.',
     '<b>별점 리본</b> - 별점이 있는 영상만.',
     '<b>재생 시간</b>',
     '<b>제목</b> (최대 3줄)',
     '<b>품번</b>',
     '<b>해상도 · 등급</b> 표시 (예: 1080p | FHD)',
     '<b>배우 수 · 태그 수 · 물방울</b> - 숫자에 마우스를 올려 두면 출연 배우 카드 / 태그 칩이 팝업으로 나옵니다.'])}
''', 1))

S.append(('video', '영상 탭', f'''
<p>등록된 모든 영상을 카드로 봅니다. 위 도구줄에서 정렬(이름 · 제목 · 크기 · 수정일 · 별점 · 발매일 · 배우 · 제작사 · 품번 · 재생 시간 · 해상도 등)과 방향 [▲▼] 을 고릅니다.</p>
<h3>보기 방식</h3>
{fig(figs.fig_views(), '[격자] [리스트] [패키지] [월 모드]')}
{afig(anim.an_hover_preview(), '카드에 마우스를 올리면 미리보기 재생')}
{ul(['<b>격자</b> - 기본 카드. 카드 이미지에 마우스를 올리면 미리보기 영상(만들어 둔 경우)이 그 자리에서 재생됩니다.',
     '<b>리스트</b> - 표 형태. 열 머리글을 누르면 정렬됩니다.',
     '<b>패키지</b> - 세로 커버만 촘촘히. 마우스를 올려 두면 격자 카드가 팝업으로 나옵니다.',
     '<b>월 모드</b> - 화면에 보이는 칸이 모두 동시에 미리보기를 재생합니다. 프로그램이 뒤로 가거나 최소화되면 멈춥니다.'])}
<h3>그 밖의 도구</h3>
{ul(['<b>확대 슬라이더</b> - 카드 크기. 끌기 · 클릭 · 휠 · ← → 로 바꾸고 탭마다 따로 기억합니다.',
     '<b>장르(태그) 칩</b> - 칩을 누르면 그 태그가 있는 영상만. [장르숨김] 은 칩 줄을 숨깁니다.',
     '<b>페이지</b> - 한 페이지 개수(전체 / 50 / 100 / 250 / 500 / 1000)는 탭마다 따로 저장됩니다.',
     '<b>[가로 | 세로]</b> - 카드 이미지 방향.',
     '<b>오른쪽 클릭 메뉴</b> - 재생, 이 영상 새로고침, 탐색기에서 보기, 텍스트로 정보 입력, 웹페이지 내용 붙여넣기, 스크린샷 추출, 미리보기 만들기, 컬렉션에 추가, DB 에서 삭제.'])}
<h3 id="split">분할 영상</h3>
{fig(figs.fig_split(), '분할 영상 합쳐 보기')}
{afig(anim.an_split_parts(), '파트 칸에서 원하는 파트 고르기')}
<p><code>ABC-123_1.mp4</code>, <code>ABC-123_2.mp4</code> 처럼 이름 끝의 <code>_숫자</code> 만 다른 파일은 한 작품의 파트로 봅니다.
설정에서 <b>분할 영상 합쳐서 보기</b> 를 켜면 첫 파트 카드 하나만 보이고, 카드 이미지를 파트 수만큼 나눠서 원하는 파트를 고르고 재생합니다.</p>
{note('연결되지 않은 이동식 디스크의 영상은 보기 전용입니다. 재생 · 수정은 디스크를 연결한 뒤에 할 수 있습니다.')}
''', 1))

S.append(('detail', '영상 상세', f'''
<p>영상을 고르면 나오는 오른쪽 화면입니다 (설정에서 왼쪽으로 옮길 수 있음). 영상 탭, 배우 출연작, 제작사 · 태그 · 컬렉션 영상 화면에서 같은 모양입니다.</p>
{fig(figs.fig_detail(), '영상 상세 화면')}
<p>고친 내용은 <b>[저장]</b> 을 누르거나 다른 영상을 고를 때 저장됩니다. 별점과 물방울은 누르는 즉시 저장됩니다.</p>
{ul(['<b>이미지</b> - [이미지 변경] 으로 파일을 고르거나 탐색기에서 이미지를 끌어다 놓습니다. [이미지 검색] 은 품번으로 인터넷 이미지를 찾습니다.',
     '<b>제목</b> - 여러 줄로 보입니다.',
     '<b>별점</b> - 별 위에 마우스를 올리면 미리 보이고 누르면 확정, 같은 별을 다시 누르면 0점. 물방울은 누를 때마다 +1, 오른쪽 클릭 [초기화].',
     '<b>발매일</b> - <code>19980217</code> 처럼 숫자만 쳐도 됩니다. 오른쪽 콤보는 검열 정보(해당없음 · 검열됨 · 모자이크파괴 · 무수정 · 무수정-유출).',
     '<b>배우 · 태그 칩</b> - 글자를 치면 자동 완성 목록이 나옵니다. 칩의 × 로 빼고, ⌄ 로 목록 창에서 고릅니다. 목록에 없는 이름은 새로 추가됩니다. 태그 칩에 마우스를 올리면 태그 카드가 팝업으로 나옵니다.',
     '<b>제작사 · 레이블 카드</b> - 로고와 이름. 품번으로 자동으로 정해지고, 제작사 카드를 더블클릭하면 다른 제작사로 바꿉니다.',
     '<b>시리즈</b> - 작품의 시리즈 이름을 자유롭게 적습니다 (검색에 포함).',
     '<b>맨 아래 띠</b> - 출연 배우 카드(더블클릭 = 배우 출연작으로). 설정 [상세 화면] 에서 스크린샷(오른쪽 클릭 [스크린샷 추출] 로 만든 장면)으로 바꿀 수 있습니다.'])}
{afig(anim.an_rating(), '별점 · 물방울은 누르는 즉시 저장')}
{afig(anim.an_tag_popup(), '태그 칩 위에 마우스 → 태그 카드 팝업')}
''', 1))

S.append(('input', '정보 입력 (txt · 붙여넣기)', f'''
<p>영상 정보를 하나하나 치지 않고 한 번에 채우는 방법이 세 가지 있습니다.</p>
<h3>웹페이지 내용 붙여넣기</h3>
{afig(anim.an_paste(), '작품 사이트 내용으로 정보 채우기')}
<p>작품 사이트 화면을 드래그해서 복사한 뒤, 영상 오른쪽 클릭 <b>[웹페이지 내용 붙여넣기...]</b> 창에 붙여넣으면 품번 · 제목 · 발매일 · 배우 · 제작사 · 레이블 · 태그를 찾아 채웁니다.
처음 보는 태그나 제작사가 있으면 기존 항목에 연결할지, 새로 추가할지 묻습니다. [공백라인 제거] 로 빈 줄을 정리할 수 있습니다.</p>
<h3>텍스트로 정보 입력</h3>
<p>오른쪽 클릭 <b>[텍스트로 정보 입력...]</b> 은 지금 정보를 <code>항목: 값</code> 형식으로 보여 주는 창입니다. 고쳐서 [적용] 하면 반영됩니다.</p>
<pre>제목: 영상 제목
발매일: 2024-05-17
배우: 배우 A, 배우 B
제작사: 제작사 이름
태그: 드라마, 코미디
별점: 3.5</pre>
<h3>영상 폴더의 txt</h3>
<p>같은 형식의 txt 를 영상 옆에 두면 새로 찾을 때 자동으로 읽습니다. 반대로 아래 <b>[정보 txt 생성]</b> 은 저장된 정보를 영상마다 txt 로 써 줍니다 (배우 폴더에는 배우 정보 txt).</p>
{tip('.txt 파일을 영상 카드나 리스트 줄에 끌어다 놓으면 웹페이지 붙여넣기와 같이 처리됩니다.')}
{tip('날짜는 "2024-05-17", "2024.5.17", "2024年05月17日" 처럼 적혀도 읽습니다. 연도가 두 자리여도 됩니다 ("22.09.15" → 2022-09-15, "22년 09월" → 2022-09). 두 자리 연도는 올해 끝 두 자리 + 1 이하면 2000년대, 그보다 크면 1900년대로 봅니다 ("98" → 1998).')}
''', 1))

S.append(('preview', '미리보기 영상', f'''
<p>카드에 마우스를 올렸을 때 재생되는 짧은 미리보기 영상을 만듭니다. <b>ffmpeg.exe</b> 가 필요합니다
(프로그램 폴더, 프로그램 폴더\\ffmpeg\\bin, 또는 PATH 에 두면 됩니다).</p>
{ul(['<b>하나 만들기</b> - 영상 오른쪽 클릭 [미리보기 만들기]. 이미 있으면 [미리보기 다시 만들기] 로 나옵니다.',
     '<b>분할 영상</b> - 한 파트에서 만들면 같은 묶음의 모든 파트를 한 번에 만듭니다. 이미 있는 파트는 건너뛰고, 모든 파트에 있으면 모두 다시 만듭니다.',
     '<b>체크한 영상</b> - 카드를 체크한 뒤 오른쪽 클릭 [미리보기 만들기(n)].',
     '<b>모두</b> - 설정 [DB 관리] 의 [일괄 만들기](없는 것만) · [재생성](모두 다시) · [모두 삭제].',
     '<b>중단</b> - 만드는 동안 톱니바퀴가 돕니다. 톱니바퀴 오른쪽 클릭 또는 설정의 [만들기 중단].'])}
<p>만드는 진행은 아래 상태 줄에 표시되고, 다른 작업을 하는 동안 뒤에서 한 편씩 만듭니다.</p>
''', 1))

S.append(('actor', '배우 탭', f'''
{fig(figs.fig_actor(), '배우 카드와 트리맵')}
{ul(['<b>[격자 | 트리맵]</b> - 트리맵은 배우를 출연 수(기준 콤보로 바꿈)만큼의 칸 크기로 보여 줍니다.',
     '<b>성별 거르기</b> - [선택없음 | 여성만 | 남성만].',
     '<b>정렬</b> - [이름 | 작품수]. 즐겨찾기(♥)한 배우가 항상 먼저 나옵니다. ♥ 는 카드 오른쪽 위에 마우스를 올리고 누릅니다.',
     '<b>오른쪽 상세</b> - 사진 · 이름 · 별점 · 나이 · 국적 · 신체 · 데뷔 · 링크. 별점은 바로 누를 수 있습니다.',
     '<b>오른쪽 클릭</b> - 편집 ' + K('F2') + ', 텍스트로 정보 입력, 다른 배우와 합치기, 삭제 ' + K('Del') + '.',
     '빈 곳 오른쪽 클릭 → [새 배우 추가...].'])}
<p>배우 카드를 더블클릭하면 <a href="#actor-drill">배우 출연작 화면</a>으로 들어갑니다.</p>
''', 1))

S.append(('actor-drill', '배우 출연작 화면', f'''
<p>배우 카드를 더블클릭(또는 {K("Space")})하면 들어가는 화면입니다. 위에는 배우 정보, 아래에는 그 배우가 나온 영상, 오른쪽에는 고른 영상의 상세가 나옵니다.</p>
{afig(anim.an_drill(), '배우 카드 더블클릭으로 들어가고 [◀ 배우 목록] 으로 돌아오기')}
{fig(figs.fig_actor_drill(), '배우 출연작 화면 ([격자] 보기)')}
{ol(['<b>[◀ 배우 목록]</b> - 배우 목록으로 돌아갑니다 ({K("Backspace")} 도 됨). 오른쪽 글자는 배우 이름과 출연 영상 수.',
     '<b>[격자 | 연도별]</b> - 출연작 보기 방식.',
     '<b>배우 사진</b> - 더블클릭하면 원본 크기로 봅니다. 사진이 없으면 성별 실루엣.',
     '<b>배우 정보</b> - 배우 탭 상세와 같은 내용(이름 · ♥ · 별점 · 성별 · 출생 · 국적 · 신체 · 경력 · 링크). ♥ 와 별점은 여기서 바로 바꿀 수 있습니다.',
     '<b>출연작</b> - 영상 탭과 같은 격자 · 정렬 · 보기 방식. 정렬 · 페이지 개수는 영상 탭과 따로 기억합니다.',
     '<b>영상 상세</b> - 고른 영상의 <a href="#detail">영상 상세</a>. 이 화면에서는 아래 띠에 스크린샷만 나옵니다 (설정에서 스크린샷을 고른 경우).'])}
<h3>[연도별] 보기</h3>
{fig(figs.fig_year(), '[연도별] - 가로 1~12월, 세로 연도')}
<p>출연작을 발매일의 연도 · 월 칸에 늘어놓습니다. 영상이 있는 연도만, 오래된 연도부터 위에서 아래로 나옵니다.
위쪽 월 머리글과 왼쪽 연도는 스크롤해도 그대로 보입니다. 칸의 영상을 더블클릭하면 재생합니다.</p>
''', 2))

S.append(('studio', '제작사 탭', f'''
<p>제작사를 카드로 봅니다. 제작사는 상위 · 하위(예: 그룹 › 회사 › 브랜드)로 묶을 수 있고, 카드 배경색이 레벨을 나타냅니다.
보기 방식은 <b>[격자 | 리스트 | 계층]</b> 세 가지입니다.</p>
{fig(figs.fig_tree(), '[계층] 보기')}
{afig(anim.an_tree_fold(), '− / + 로 하위 제작사 접고 펼치기')}
{ul(['<b>격자</b> - 정렬([이름 | 영상수])과 검색 순서대로.',
     '<b>리스트</b> - 이름 · 영상 수 표. 열 머리글로 정렬.',
     '<b>계층</b> - 상위 → 하위 트리. − / + 로 접고 펼칩니다.',
     '빈 곳 오른쪽 클릭 → [새 제작사 추가...], 카드 오른쪽 클릭 → 편집.',
     '<b>체크</b> - 카드 · [계층] 노드 왼쪽 위에 마우스를 올리면 체크 칸. 여러 개 체크한 뒤 오른쪽 클릭으로 한꺼번에 삭제 · 병합합니다.',
     '<b>병합</b> - 카드 오른쪽 클릭 <b>[다른 제작사와 병합...]</b>, 또는 체크한 제작사에서 <b>[제작사 병합(n) - 이름 기준]</b> (오른쪽 클릭한 제작사로 합침). 영상 · 하위 제작사 · 레이블 · 품번 DB 항목이 대상으로 옮겨지고, 원래 이름은 대상의 서브이름이 되어 붙여넣기 · txt 에서도 대상으로 인식됩니다.'])}
<p>제작사 카드를 더블클릭하면 <a href="#studio-drill">제작사 영상 화면</a>으로 들어갑니다.</p>
''', 1))

S.append(('studio-drill', '제작사 영상 화면', f'''
<p>제작사 카드를 더블클릭(또는 {K("Space")})하면 들어가는 화면입니다. 위에는 제작사 정보, 아래에는 그 제작사의 영상이 나옵니다.</p>
{fig(figs.fig_studio_drill(), '제작사 영상 화면 ([레이블] 보기)')}
{ol(['<b>[◀ 제작사 목록]</b> - 제작사 목록으로 돌아갑니다 ({K("Backspace")} 도 됨). 오른쪽 글자는 제작사 이름과 영상 수.',
     '<b>로고 · 정보</b> - 로고, 이름, 상위 제작사 · 영상 수, 메모, 링크.',
     '<b>상위 · 하위 제작사 칸</b> - ▲ 는 바로 위 상위 제작사, └ 는 바로 아래 하위 제작사(영상이 많은 순). 칸 배경색은 제작사 레벨 색이고, 더블클릭하면 그 제작사의 영상 화면으로 넘어갑니다.',
     '<b>보기 방식</b> - 영상 탭의 [격자] [리스트] [패키지] [월 모드] 에 더해 <b>[레이블]</b> 보기가 있습니다.',
     '<b>[레이블] 보기</b> - 영상을 레이블 묶음으로 나눕니다. 묶음 순서는 <a href="#series">품번 관리</a>의 레이블 순서이고, 머리줄을 오른쪽 클릭하면 [품번 관리에서 보기] · [품번 관리에 등록] 을 고를 수 있습니다.',
     '<b>영상 상세</b> - 고른 영상의 <a href="#detail">영상 상세</a>. 아래 띠에는 그 영상의 출연 배우 카드가 나옵니다.'])}
<p>태그 카드나 컬렉션 카드를 더블클릭했을 때도 같은 모양의 영상 화면으로 들어갑니다 (태그는 위쪽 정보 없이, 컬렉션은 커버 · 메모와 함께).</p>
''', 2))

S.append(('tag', '태그 탭', f'''
{ul(['<b>정렬</b> - 위쪽 콤보 [이름 | 영상수] 와 [▲▼]. 바꾸면 맨 위부터 다시 보입니다.',
     '<b>즐겨찾기</b> - 카드 오른쪽 위 하트. 즐겨찾기한 태그가 먼저 나옵니다.',
     '<b>병합</b> - 카드 오른쪽 클릭 [다른 태그와 병합...] → 영상의 태그가 고른 태그로 바뀌고 원래 태그는 지워집니다.',
     '<b>여러 개 삭제</b> - 카드를 체크한 뒤 ' + K('Del') + '.',
     '카드를 더블클릭하면 그 태그가 붙은 영상 화면(오른쪽에 영상 상세)으로 들어갑니다.',
     '카드 이름은 "한글 이름 (영어, 일본어)" 로 표시됩니다 - <a href="#manage-tag">태그 관리</a>에서 입력.'])}
''', 1))

S.append(('collection', '컬렉션 탭', f'''
{fig(figs.fig_collection(), '컬렉션 영상 화면')}
<p>직접 고른 영상 묶음입니다. 빈 곳 오른쪽 클릭 [새 컬렉션 추가...] 로 만들고, 영상 오른쪽 클릭 <b>[컬렉션에 추가 ▸]</b> 로 넣고 뺍니다.
컬렉션마다 제목 · 커버 이미지 · 메모를 가지며, 카드를 더블클릭하면 위 그림처럼 들어갑니다.</p>
''', 1))

S.append(('series', '품번 관리', f'''
<p><b>[목록 관리 ▾] → [품번 관리...]</b>. 품번 접두어마다 제작사와 레이블을 정해 두면, 영상의 품번만으로 제작사 · 레이블이 자동으로 정해집니다.</p>
{fig(figs.fig_series(), '품번 관리 창')}
{ul(['<b>싱글</b> - 접두어 하나에 레이블 하나 (예: ABC → 레이블 1).',
     '<b>복합</b> - 같은 접두어를 뒷번호로 나눠 여러 레이블로. 레이블명을 고르고 [추가] · [삭제] · [▲▼] 로 순서를 정한 뒤, 뒷번호 칸에 그 레이블의 번호를 한 줄에 하나씩 적습니다. 표에는 "(*복합)" 으로 표시됩니다.',
     '같은 접두어를 여러 제작사가 쓰면 영상의 제작사를 보고 고릅니다. 고를 수 없으면 "(품번 중복 · 제작사 필요)" 로 표시됩니다.',
     '<b>[웹페이지 붙여넣기...]</b> - 사이트의 레이블 목록을 붙여넣어 한 번에 등록합니다.',
     '레이블명에 다른 이름을 적어 두면 붙여넣기에서 다른 표기도 같은 레이블로 알아봅니다.'])}
''', 1))

S.append(('manage-actor', '배우 관리', f'''
<p><b>[목록 관리 ▾] → [배우 관리...]</b> 또는 배우 카드 {K("F2")}. 왼쪽 표에서 배우를 고르고 오른쪽에서 사진 · 이름 · 성별 · 생년월일 · 국적 · 키 · 치수 · 데뷔 · 은퇴 · 별점 · 링크 · 메모를 고친 뒤 [저장] 합니다.</p>
{ul(['<b>이름 콤보</b> - 대표 이름과 별칭이 함께 들어 있습니다. 새 이름은 입력 후 ' + K('Enter') + ', 마지막으로 고른 이름이 대표 이름입니다.',
     '"한글 이름(English, 日本語)" 처럼 적으면 언어별 이름으로 나눠서 비교합니다 (붙여넣기 · txt 에서 다른 언어로 적혀도 같은 배우).',
     '<b>[일 모름]</b> - 데뷔일 · 은퇴일 옆 체크. 공개된 것이 연 · 월뿐이면 체크하세요. 날짜가 "2018-04" 처럼 연-월로만 표시 · 저장되고, 경력은 그 달 1일부터 셉니다. 붙여넣기 · txt 에서 일 없이 "2018년 4월" 로 적힌 데뷔 · 은퇴도 연-월로 들어갑니다.',
     '열 머리글을 누르면 그 열로 정렬합니다.'])}
''', 2))

S.append(('manage-studio', '제작사 관리', f'''
<p><b>[목록 관리 ▾] → [제작사 관리...]</b>. 왼쪽 트리에서 제작사를 고르고 이름 · 서브이름 · 이미지(SVG 도 됨) · 상위 제작사 · 하위 제작사 · 링크 · 메모를 정합니다.
트리에서 끌어서 다른 제작사 아래로 옮기면 상위 제작사가 바뀝니다.</p>
''', 2))

S.append(('manage-tag', '태그 관리', f'''
<p><b>[목록 관리 ▾] → [태그 관리...]</b>. 이름 · 다른 이름(줄마다) · 영어 · 일본어 이름을 정합니다. 다른 이름이나 영어 · 일본어로 적힌 태그도 붙여넣기 · txt 에서 이 태그로 바뀝니다.
[일괄 등록] 으로 여러 태그를 한 번에 추가할 수 있습니다 (한 줄에 하나 또는 쉼표).</p>
''', 2))

S.append(('manage-file', '파일 관리', f'''
<p><b>[관리 도구 ▾] → [파일 관리...]</b>. 등록 폴더의 실제 폴더 · 파일을 트리로 보여 주고, DB 정보와 폴더 구조(제작사 \\ 배우 \\ 영상)가 맞지 않는 영상을 찾아 고칩니다.
항목을 오른쪽 클릭하면 폴더 열기 등의 메뉴가 나옵니다.</p>
{note('파일 · 폴더 이름을 실제로 바꾸는 기능입니다. 고치기 전에 목록을 잘 확인하세요.')}
''', 2))

S.append(('video-check', '영상 검사', f'''
<p><b>[관리 도구 ▾] → [영상 검사...]</b>. 영상 파일이 손상되었는지 ffmpeg 로 실제로 풀어 보며 검사합니다. 미리보기가 만들어지지 않는 영상의 원인을 찾을 때도 씁니다.
검사 창은 열어 둔 채 다른 작업을 할 수 있고(모달리스), 닫아도 검사는 뒤에서 계속됩니다.</p>
{fig(figs.fig_video_check(), '영상 검사 창')}
{ul(['<b>목록에 넣기</b> - 영상 카드 오른쪽 클릭 <b>[영상 검사]</b> (분할 영상은 모든 파트), 카드를 체크했으면 <b>[영상 검사(n)]</b>. 여러 번 넣어도 되고, 넣은 순서대로 한 편씩 검사합니다.',
     '<b>검사 방식</b> - <b>빠른 검사</b>: 머리 정보를 읽고 영상 곳곳(9곳)을 1초씩 풀어 봄, 1편에 몇 초. <b>정밀 검사</b>: 처음부터 끝까지 영상 · 소리를 모두 풀어 봄, 오래 걸림. 의심되는 영상만 정밀 검사하는 것을 권합니다.',
     '<b>[중단]</b> - 지금 검사를 취소하고 남은 항목은 멈춤. <b>[시작]</b> 을 누르면 이어서 검사 (취소된 항목도 다시).',
     '<b>결과</b> - <span style="color:#3DCC91">정상</span> · <span style="color:#F29D49">의심</span>(일부 구간 실패, 오류 몇 줄, 길이 정보 없음, 끝까지 풀리지 않음) · <span style="color:#FF7373">손상</span>(열리지 않음, 영상 스트림 없음, 절반 이상 구간 실패, 오류가 많음).',
     '목록에서 고르면 아래 칸에 자세한 결과(길이 · 스트림, 실패한 위치, ffmpeg 오류 줄)가 나옵니다. 더블클릭하면 탐색기에서 그 파일을 보여 줍니다.',
     '[선택 삭제] · [끝난 항목 지우기] 로 목록을 정리합니다. 결과는 프로그램을 닫으면 지워집니다.'])}
{note('ffmpeg.exe 가 필요합니다 (미리보기 영상과 같은 위치). 연결되지 않은 이동식 디스크의 영상은 검사할 수 없습니다.')}
''', 2))

S.append(('dup-check', '중복 파일 검사', f'''
<p><b>[관리 도구 ▾] → [중복 파일 검사...]</b>. 같은 영상이 여러 곳에 있는지 찾아 묶음별로 보여 주고, 필요 없는 파일을 골라 지웁니다.</p>
{fig(figs.fig_dup_check(), '중복 파일 검사 창')}
{ul(['<b>검사 기준</b> - <b>같은 크기</b>: 파일 크기(바이트)가 똑같은 영상. <b>같은 품번</b>: 품번이 같은 영상 (분할 영상 ABC-123_1 · _2 처럼 파트가 다르면 중복으로 보지 않음). 둘 다 같은 묶음은 하나로 보여 줍니다. 기준을 바꾸면 바로 다시 검사합니다.',
     '<b>카드</b> - 커버, 파일 이름, 크기 · 재생 시간, 해상도 · fps, 수정일, 위치(폴더). 묶음 안에서는 해상도가 높고 큰 파일이 앞에 옵니다.',
     '<b>재생</b> - 카드 더블클릭 (오른쪽 클릭 메뉴에서 재생 · 탐색기에서 보기).',
     '<b>체크</b> - 카드 왼쪽 위 칸을 누르면 삭제할 파일로 체크 (빨간 테두리). <b>[자동 선택]</b> 은 묶음마다 해상도가 높은 것 → 큰 것 하나만 남기고 나머지를 체크합니다.',
     '<b>[선택 삭제 (휴지통)]</b> - 체크한 파일을 Windows 휴지통으로 보내고 DB 에서도 지웁니다 (휴지통에서 되살릴 수 있음). 한 묶음의 파일이 모두 체크돼 있으면 확인 창에서 알려 줍니다. 커버 이미지 · 정보 txt 는 그대로 둡니다.'])}
{note('연결되지 않은 이동식 디스크의 영상은 카드에 표시만 되고 체크 · 삭제할 수 없습니다.')}
''', 2))

S.append(('settings', '설정', f'''
<p>오른쪽 위 톱니바퀴. 탭 네 개로 나뉩니다.</p>
{ul(['<b>표시</b> - 카드에 해상도 · 재생 시간 표시, SFW(배우 · 영상 이미지를 모자이크로), 분할 영상 합쳐서 보기, 별점 단위(1점 / 0.5점).',
     '<b>상세 화면</b> - 상세 화면 위치(왼쪽 / 오른쪽), 영상 상세 맨 아래 띠(출연 배우 카드 / 스크린샷), 스크린샷 가로 배치 수.',
     '<b>테마</b> - 색상표(Stash · Dark · Midnight). 미리보기로 확인하고, 다시 시작하면 적용됩니다.',
     '<b>DB 관리</b> - DB 삭제(영상 / 배우 / 제작사 / 태그), 텍스트 내보내기 · 일괄 등록, 미리보기 영상 일괄 만들기 · 재생성 · 모두 삭제.'])}
''', 1))

S.append(('data', '데이터와 백업', f'''
{ul(['DB 는 실행 파일 폴더의 <code>library.vmdb</code>(정보)와 <code>images.vmdb</code>(배우 사진 · 제작사 이미지) 두 파일입니다. 두 파일만 옮기면 다른 PC 에서도 그대로 쓸 수 있습니다. 아래 [DB 폴더 열기] 로 폴더를 엽니다.',
     '실행 중에는 작업용 사본에 저장하고, <b>[DB 반영]</b> 이나 종료할 때 원본에 반영합니다.',
     '프로그램을 켤 때마다 DB 폴더의 <code>backup</code> 에 자동 백업합니다 (파일마다 최근 10개). 되돌리려면 프로그램을 끄고 백업 파일을 원래 이름으로 바꿔 덮어씁니다.',
     '프로그램 버전이 바뀌어 DB 형식을 고칠 때는 <code>DbBackup</code> 폴더에 원본을 먼저 백업합니다.',
     '이동식 디스크 영상의 커버는 캐시에 보관해서 디스크를 빼도 카드 이미지가 보입니다.'])}
''', 1))

S.append(('keys', '단축키', f'''
<table>
<tr><th>키</th><th>동작</th></tr>
<tr><td>{K("F1")}</td><td>지금 화면의 설명서</td></tr>
<tr><td>{K("F5")}</td><td>새로고침 (새 파일 찾기)</td></tr>
<tr><td>{K("F2")}</td><td>고른 항목 편집 (배우 · 제작사 · 태그 관리 창)</td></tr>
<tr><td>{K("Enter")} / {K("Space")} / 더블클릭</td><td>영상 재생, 배우 · 제작사 · 태그 · 컬렉션은 들어가기</td></tr>
<tr><td>{K("Backspace")}</td><td>들어간 화면에서 목록으로 돌아가기</td></tr>
<tr><td>{K("Del")}</td><td>DB 에서 삭제 (파일은 그대로), 체크한 카드가 있으면 모두</td></tr>
<tr><td>{K("Ctrl")}+{K("F")}</td><td>검색 칸으로</td></tr>
<tr><td>{K("←")} {K("→")} {K("↑")} {K("↓")}</td><td>카드 이동 (제작사 [계층] 보기: ↑ 상위, ↓ 첫 하위)</td></tr>
<tr><td>마우스 휠 / {K("←")} {K("→")} (확대 슬라이더에서)</td><td>카드 크기</td></tr>
</table>
''', 1))


nav = ''.join(f'<a class="lv{lv}" href="#{i}">{t}</a>' for i, t, _, lv in S)
body = ''.join(f'<section id="{i}"><h2>{t}</h2>{b}</section>' for i, t, b, _ in S)

CSS = '''
:root{--back:#202B33;--panel:#293742;--card:#30404D;--edit:#1B252C;--text:#F5F8FA;--sub:#A7B6C2;--acc:#137CBD;--acc2:#48AFF0;--save:#0F9960;--warn:#D9822B}
*{box-sizing:border-box}
html{scroll-behavior:smooth;scroll-padding-top:16px}
body{margin:0;background:var(--back);color:var(--text);font:15px/1.7 'Malgun Gothic','맑은 고딕',sans-serif}
nav{position:fixed;top:0;left:0;bottom:0;width:230px;overflow-y:auto;background:#182026;padding:18px 0;border-right:1px solid #10161A}
nav .brand{padding:0 20px 14px;font-weight:bold;font-size:18px}
nav .brand small{display:block;color:var(--sub);font-weight:normal;font-size:12px}
nav a{display:block;padding:5px 20px;color:var(--sub);text-decoration:none;font-size:14px;border-left:3px solid transparent}
nav a.lv2{padding-left:36px;font-size:13px}
nav a:hover{color:var(--text);background:#202B33}
nav a.on{color:var(--text);border-left-color:var(--acc2);background:#202B33}
main{margin-left:230px;max-width:1000px;padding:24px 48px 80px}
h1{font-size:28px;margin:8px 0 4px}
h2{font-size:22px;margin:0 0 12px;padding-bottom:8px;border-bottom:1px solid var(--card)}
h3{font-size:17px;margin:26px 0 8px;color:var(--acc2)}
section{padding:28px 0 12px}
section:target h2{color:var(--acc2)}
a{color:var(--acc2)}
code,kbd{font-family:Consolas,monospace;font-size:13px}
code{background:var(--edit);padding:1px 5px;border-radius:3px}
kbd{display:inline-block;min-width:22px;text-align:center;background:var(--card);border:1px solid #10161A;border-bottom-width:2px;border-radius:4px;padding:0 6px}
pre{background:var(--edit);padding:12px 16px;border-radius:4px;overflow:auto;font-size:13px}
figure{margin:16px 0 20px;background:var(--panel);border-radius:6px;padding:14px}
figure svg{display:block;width:100%;height:auto}
figcaption{color:var(--sub);font-size:13px;text-align:center;margin-top:8px}
ol.callouts{counter-reset:c;list-style:none;padding-left:0}
ol.callouts li{counter-increment:c;position:relative;padding-left:34px;margin:6px 0}
ol.callouts li::before{content:counter(c);position:absolute;left:0;top:2px;width:22px;height:22px;border-radius:50%;background:#F29D49;color:#10161A;font-weight:bold;font-size:12px;text-align:center;line-height:22px}
.tip,.note{padding:10px 14px;border-radius:4px;margin:14px 0}
.tip{background:rgba(15,153,96,.15);border-left:4px solid var(--save)}
.note{background:rgba(217,130,43,.15);border-left:4px solid var(--warn)}
.temp{color:#F29D49}
table{border-collapse:collapse;width:100%}
th,td{text-align:left;padding:7px 12px;border-bottom:1px solid var(--card)}
th{background:var(--panel)}
.hint{color:var(--sub);font-size:13px}
figure.anim{cursor:pointer;outline:1px solid rgba(242,157,73,.35)}
figure.anim.paused{outline-color:var(--sub)}
figure.anim.paused figcaption::after{content:'  (멈춤 - 누르면 다시 재생)';color:#F29D49}
.badge{display:inline-block;background:#F29D49;color:#10161A;font-weight:bold;font-size:11px;border-radius:3px;padding:0 6px;margin-right:4px}
@media (prefers-reduced-motion: reduce){figure.anim figcaption::before{content:'시스템 설정이 움직임 줄이기면 처음에 멈춰 있습니다 - 눌러서 재생  ';color:var(--sub)}}
'''

JS = '''
// 지금 보는 장을 목차에서 강조
const links=[...document.querySelectorAll('nav a')];
const secs=links.map(a=>document.querySelector(a.getAttribute('href')));
function mark(){let cur=0;const y=window.scrollY+80;secs.forEach((s,i)=>{if(s&&s.offsetTop<=y)cur=i;});
 links.forEach((a,i)=>a.classList.toggle('on',i===cur));}
window.addEventListener('scroll',mark);window.addEventListener('hashchange',mark);mark();
// 움직이는 그림: 누르면 멈춤 / 다시 재생, 화면 밖에서는 멈춤 (움직임 줄이기 설정이면 처음엔 멈춤)
const reduce=window.matchMedia('(prefers-reduced-motion: reduce)').matches;
document.querySelectorAll('figure.anim').forEach(f=>{
 const svg=f.querySelector('svg');
 if(reduce){svg.pauseAnimations();f.classList.add('paused');}
 f.addEventListener('click',()=>{if(f.classList.toggle('paused'))svg.pauseAnimations();else svg.unpauseAnimations();});
});
const io=new IntersectionObserver(es=>es.forEach(e=>{const f=e.target,svg=f.querySelector('svg');
 if(f.classList.contains('paused'))return; if(e.isIntersecting)svg.unpauseAnimations();else svg.pauseAnimations();}));
document.querySelectorAll('figure.anim').forEach(f=>io.observe(f));
'''

html = f'''<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RuliManager 설명서</title>
<style>{CSS}</style>
</head>
<body>
<nav><div class="brand">RuliManager<small>설명서 · v{VERSION}</small></div>{nav}</nav>
<main>
<h1>RuliManager 설명서</h1>
<p class="hint">v{VERSION} 기준 · 그림은 실제 화면을 단순하게 그린 것이라 세부 모양은 다를 수 있습니다 · 어느 화면에서든 <kbd>F1</kbd> 을 누르면 그 화면의 장이 열립니다.</p>
{body}
</main>
<script>{JS}</script>
</body>
</html>
'''
open('index.html', 'w', encoding='utf-8', newline='\r\n').write(html)
print(len(html))
