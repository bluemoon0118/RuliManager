#pragma once

// ---------------------------------------------------------------------------
// 프로그램 버전 (여기만 고치면 창 제목 · 실행 파일 속성(VERSIONINFO)에 함께 반영)
//  - 창 제목: "RuliManager v" + APP_VERSION_STR  (CRuliManagerDlg::OnInitDialog 에서 설정)
//  - 리소스(RuliManager.rc)의 VS_VERSION_INFO 도 이 값을 사용
//  - 숫자 4개와 문자열을 같이 맞춰 주세요 (예: 0.2 → MINOR 2, "0.2")
//  - 주의: 리소스 편집기에서 버전 정보를 직접 고치면 매크로가 숫자로 바뀌어 버릴 수 있음 → 이 파일에서만 수정

#define APP_NAME_STR        "RuliManager"

#define APP_VERSION_MAJOR   0
#define APP_VERSION_MINOR   2
#define APP_VERSION_PATCH   0
#define APP_VERSION_BUILD   0

#define APP_VERSION_STR     "0.2"
