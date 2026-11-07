# RuliManager 예제 플러그인 (target: tool)
#  - [관리 도구 ▾] → [플러그인 ▸] 에서 실행, 결과의 "message" 를 메시지 창으로 보여 줌
import json
import sys


def send(obj):
    print(json.dumps(obj, ensure_ascii=False), flush=True)


req = json.load(sys.stdin)
lib = req.get("library", {})
send({"progress": 50, "message": "세는 중"})
msg = "영상 {0}개 · 배우 {1}명 · 제작사 {2}개 · 태그 {3}개\n데이터 폴더: {4}".format(
    lib.get("videos", 0), lib.get("actors", 0), lib.get("studios", 0), lib.get("tags", 0), req.get("dataDir", ""))
print("이 줄은 JSON 이 아니므로 실행 창의 기록에만 남습니다.", flush=True)
send({"ok": True, "message": msg})
