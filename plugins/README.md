# RuliManager 플러그인 만드는 법

플러그인은 **외부 프로그램**입니다. RuliManager 가 플러그인을 실행하고, 표준 입력(stdin)으로 요청 JSON 을 보내고, 표준 출력(stdout)으로 진행 상황과 결과 JSON 을 받습니다.
언어는 상관없습니다 (Python, exe, bat, Node 등). 플러그인이 멈추거나 오류가 나도 RuliManager 에는 영향이 없습니다.

> **주의** - 플러그인은 일반 프로그램처럼 이 PC 에서 사용자 권한으로 그대로 실행됩니다 (파일 읽기 · 쓰기 · 인터넷 접속 모두 가능). 믿을 수 있는 곳에서 받은 플러그인만 넣고, 모르는 플러그인은 plugin.json 의 run 과 실행 파일 내용을 먼저 확인하세요.

## 1. 폴더 구성

```
plugins\
  내플러그인\          ← 폴더 이름 = 플러그인 id (켜기/끄기 기억)
    plugin.json        ← 필수
    plugin.py          ← 실행할 프로그램 (자유)
```

- `plugins` 폴더 위치: 실행 파일 폴더부터 위로 3단계 안의 `plugins` (개발 중에는 프로젝트 폴더의 `plugins`), 없으면 실행 파일 폴더\plugins
- 설정 → [플러그인] 탭에서 목록 확인 · 켜기/끄기 · [폴더 열기] · [새로 고침]

## 2. plugin.json (UTF-8)

```json
{
  "name": "AV 정보 가져오기",
  "version": "1.0",
  "author": "나",
  "description": "품번으로 사이트에서 제목 · 배우 · 발매일을 가져옵니다.",
  "api": 1,
  "run": "python plugin.py",
  "target": "video",
  "menu": "사이트에서 정보 가져오기",
  "timeout": 120
}
```

| 항목 | 설명 |
|---|---|
| `run` | 실행 명령 (필수). 플러그인 폴더가 작업 폴더. 첫 낱말이 플러그인 폴더 안 파일이면 그 파일을 실행 (`fetch.exe`, `"run.bat"` 등). `python` 은 PATH 에 있어야 함 |
| `target` | 필수. `video` = 영상 카드 오른쪽 클릭 [플러그인 ▸], `actor` = 배우 카드 오른쪽 클릭 [플러그인 ▸], `tool` = [관리 도구 ▾] → [플러그인 ▸] |
| `menu` | 메뉴에 보일 이름 (없으면 `name`) |
| `timeout` | 초 (기본 120, `0` = 제한 없음). 넘기면 하위 프로세스(크롬 드라이버 등)까지 종료 |
| `keepBehind` | 기본 `true` - 플러그인이 띄운 창(셀레늄 크롬 등)이 처음 보일 때 크기 · 최대화는 그대로 RuliManager 뒤로 보내고 포커스를 되돌림 (작업 표시줄에서 꺼내 보면 그대로 둠). `false` = 끔 |
| `api` | 이 문서의 형식 = `1` |

체크한 영상 · 배우에서 오른쪽 클릭하면 체크한 것 전부를 한 번에 보냅니다 ([플러그인(n) ▸]).

## 3. 요청 (RuliManager → stdin, UTF-8 JSON 한 덩어리, 끝나면 stdin 닫힘)

공통: `api`, `app` ("RuliManager"), `appVersion`, `action` (= target), `plugin` (폴더 이름), `dataDir` (DB 폴더), `tempDir` (이미지 등을 둘 임시 폴더 - 실행 전 · 끝난 뒤 RuliManager 가 비움)

**video**
```json
{ "api": 1, "action": "video", "items": [
  { "id": 0, "path": "E:\\영상\\ABC-123.mp4", "file": "ABC-123.mp4", "folder": "E:\\영상",
    "code": "ABC-123", "title": "", "release": "", "actors": ["홍길동"],
    "studio": "", "label": "", "series": "", "tags": [], "size": 1234567890 } ] }
```

**actor**
```json
{ "api": 1, "action": "actor", "items": [
  { "id": 0, "name": "홍길동", "aliases": [], "gender": "여성", "birth": "1998-02-17",
    "nationality": "", "height": "160", "bust": "", "waist": "", "hip": "", "cup": "",
    "debut": "2018-04", "retire": "", "urls": [], "photo": "" } ] }
```

**tool**
```json
{ "api": 1, "action": "tool", "library": { "videos": 1200, "actors": 300, "studios": 80, "tags": 150 } }
```

## 4. 출력 (stdout, 한 줄에 JSON 하나, UTF-8)

- 진행 (선택): `{"progress": 40, "message": "ABC-123 검색 중"}` - progress 0 ~ 100 (없으면 막대가 계속 움직임)
- 결과 한 편 (추천): `{"item": {"id": 0, "title": "...", ...}}` - 한 편이 끝날 때마다 보내면 RuliManager 가 바로 적용합니다 (첫 결과에서 채울 항목을 정하면 다음 것부터 그대로). RuliManager 가 그 결과를 처리하는 동안(고르기 창 등)은 플러그인을 일시 정지했다가 다시 실행합니다 - 시간을 재는 코드는 멈춘 시간만큼 흘러간 것으로 보일 수 있음
- 끝 (필수, 마지막에 한 번): `{"ok": true, ...}` - 결과를 한 편씩 보냈으면 `{"ok": true}` 만, 예전처럼 여기에 `"items": [...]` 로 한꺼번에 넣어도 됨 (한 편씩 보낸 id 는 건너뜀)
- 실패: `{"ok": false, "error": "사이트에 접속하지 못했습니다"}`
- JSON 이 아닌 줄 (print 디버그, 오류 출력 stderr) 은 실행 창의 기록 칸에만 보임
- 출력할 때마다 flush 하세요 (Python: `print(..., flush=True)`)

**결과 한 편 - video** (`{"item": {...}}` 의 안쪽, 또는 끝 결과의 `items` 배열 항목)
```json
{ "ok": true, "items": [
  { "id": 0, "code": "ABC-123", "title": "제목", "release": "2022-09-15",
    "actors": ["홍길동", "김철수"], "studio": "제작사", "label": "레이블", "series": "", "tags": ["태그1", "태그2"],
    "text": "사이트 화면 글 그대로 (선택 - 붙여넣기처럼 해석)",
    "cover": "C:\\...\\Temp\\RuliManager\\plugin\\abc.jpg" } ],
  "message": "끝나고 보여 줄 메시지 (선택)" }
```

**결과 - actor**
```json
{ "ok": true, "items": [
  { "id": 0, "name": "홍길동(Hong Gildong, 洪吉童)", "aliases": ["예전 이름"], "gender": "여성",
    "birth": "1998-02-17", "nationality": "일본", "height": 160, "bust": 88, "waist": 58, "hip": 86, "cup": "E",
    "debut": "2018-04", "retire": "", "urls": ["https://..."], "text": "(선택)",
    "photo": "https://example.com/photo.jpg" } ] }
```

**결과 - tool**: `{"ok": true, "message": "보여 줄 내용"}`

- `id` 는 요청의 `id` 를 그대로 돌려주세요 (영상은 `path` 로도 찾음)
- 이미지: 영상 `cover` (커버) / 배우 `photo` (사진) - 로컬 파일 경로(상대 경로 = 플러그인 폴더 기준, 보통 `tempDir` 에 받아 둔 파일) 또는 http(s) 주소 (RuliManager 가 받음 - Referer 가 필요한 사이트는 플러그인이 직접 받아 경로로 주세요)
  - 채울 항목 고르기 창 맨 아래 이미지 줄에 [가져온 이미지] → [지금 이미지] 썸네일 (더블클릭 = 원본 보기), 체크하면 커버는 영상 옆 "영상이름.확장자" (예전 이미지는 휴지통), 사진은 DB 폴더 images\actors 로 복사
- 비어 있거나 없는 항목은 바꾸지 않습니다
- 결과는 **[웹페이지 내용 붙여넣기] 와 같은 과정**으로 적용됩니다: 배우 · 제작사 연결 창 → 확인 창 → 적용 (항목마다)
- 날짜는 "2022-09-15", "2022.9.15", "22년 09월" 모두 됨, 데뷔 · 은퇴는 연-월만 ("2018-04") 도 됨
- `aliases` (배우 다른 이름) 는 오래된 이름부터

## 5. 환경 변수

RuliManager 가 실행할 때 넣어 줌: `PYTHONIOENCODING=utf-8`, `PYTHONUTF8=1`, `RULIMANAGER_API=1`, `RULIMANAGER_PLUGIN_DIR=<플러그인 폴더>`, `RULIMANAGER_TEMP_DIR=<임시 폴더>` (= 요청의 tempDir)

## 6. 예제

- `sample_title` (video): 파일 이름에서 품번을 뺀 나머지를 제목으로
- `sample_stats` (tool): 라이브러리 개수를 메시지로

직접 시험: `echo {"api":1,"action":"tool","library":{"videos":1}} | python plugin.py`
