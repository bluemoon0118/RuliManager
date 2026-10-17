#pragma once

// 테마 (색상표) - 설정 창 [테마] 에서 고름 (레지스트리 Settings\Theme, 다시 시작하면 적용)
//  - 화면 곳곳의 색은 역할(TC_...)로 나눠 g_themeColors 에서 읽음: TCOL(TC_BACK)
//  - 프로그램 시작 때(대화상자를 만들기 전) Theme::LoadFromSettings 로 한 번 정함 → 실행 중에는 바뀌지 않음
//  - "Stash" = 원래 색 (기본값)

enum ThemeColor
{
	TC_BACK,             // 창 배경
	TC_TEXT,             // 기본 글자
	TC_EDIT,             // 에디트 / 날짜 배경
	TC_COMBO,            // 콤보 배경 · 카드 구분선
	TC_ACCENT,           // 버튼 · 강조
	TC_SCROLL,           // 스크롤바 배경
	TC_THUMB,            // 이미지 없는 칸 배경
	TC_CARD,             // 카드 배경
	TC_BORDER,           // 테두리 · 비활성 칩
	TC_SUB2,             // 회색 글자 (진함)
	TC_SUB,              // 회색 글자
	TC_LIGHT,            // 밝은 글자 · 칩 배경 · 아이콘
	TC_DARK,             // 칩 글자
	TC_HINT,             // 안내 글자
	TC_DISABLED,         // 비활성 글자
	TC_LABELCARD,        // 레이블 카드 배경
	TC_LABELHOT,         // 레이블 카드 마우스 오버 테두리
	TC_CARDHOT,          // 제작사 카드 마우스 오버 테두리
	TC_VIEWER,           // 원본 이미지 보기 창 배경
	TC_PREVIEW,          // 이미지 영역
	TC_SAVE,             // 저장 버튼
	TC_DANGER,           // 삭제 버튼
	TC_SEL,              // 목록 선택
	TC_SELNOFOCUS,       // 목록 선택 (포커스 없음)
	TC_EDGE,             // 에디트 테두리
	TC_TOGGLETEXT,       // 토글 버튼 꺼짐 글자
	TC_GRIDSUB,          // 목록 회색 글자
	TC_ACCENTTEXT,       // 강조 글자
	TC_SCROLLTHUMB,      // 스크롤바 막대
	TC_SCROLLTHUMBHOT,   // 스크롤바 막대 (마우스 오버)
	TC_HILITE,           // 목록 마우스 오버
	TC_BRIGHT,           // 아주 밝은 글자
	TC_COUNT
};

extern COLORREF g_themeColors[TC_COUNT];
#define TCOL(role) (g_themeColors[role])

namespace Theme
{
	int            Count();                    // 테마 수
	const wchar_t* Name(int index);            // 테마 이름 ("Stash" ...)
	int            Find(const CString& name);  // 이름 → 번호 (없으면 -1)
	int            Current();                  // 지금 적용된 테마 번호
	void           Apply(int index);           // g_themeColors 를 그 테마 색으로 (시작할 때만)
	void           LoadFromSettings();         // 레지스트리 Settings\Theme → Apply (없으면 Stash)
	void           SaveToSettings(int index);  // 레지스트리에 저장 (다시 시작하면 적용)
	COLORREF       Color(int index, int role); // 그 테마의 색 (적용하지 않고 읽기만 - 설정 창 미리보기)
}
