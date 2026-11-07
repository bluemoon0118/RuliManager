# RuliManager 예제 플러그인 (target: video)
#  - stdin 으로 요청 JSON 을 받고, stdout 으로 진행 / 결과를 한 줄에 JSON 하나씩 보냅니다.
#  - 파일 이름에서 품번을 뺀 나머지를 제목으로 돌려줍니다 (네트워크 사용 안 함).
import json
import os
import re
import sys
import time


def send(obj):
    print(json.dumps(obj, ensure_ascii=False), flush=True)


def main():
    req = json.load(sys.stdin)
    items = req.get("items", [])
    out = []
    for n, it in enumerate(items):
        send({"progress": int(n * 100 / max(1, len(items))), "message": it.get("file", "")})
        stem = os.path.splitext(it.get("file", ""))[0]
        code = it.get("code", "")
        title = stem
        if code:
            title = re.sub(re.escape(code), "", title, flags=re.IGNORECASE)
        title = re.sub(r"[_\-\.\s\[\]\(\)]+", " ", title).strip()
        result = {"id": it["id"]}
        if title:
            result["title"] = title
        out.append(result)
        send({"item": result})   # 한 편씩 바로 넘김 (RuliManager 가 바로 적용 - 끝에 "items" 로 한꺼번에 보내도 됨)
        time.sleep(0.2)   # 진행 막대가 보이도록 (실제 플러그인에는 필요 없음)
    send({"ok": True})


if __name__ == "__main__":
    try:
        main()
    except Exception as e:   # 실패는 ok:false 로 알림
        send({"ok": False, "error": str(e)})
        sys.exit(1)
