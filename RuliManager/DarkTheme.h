#pragma once

#include <memory>
#include "DarkControls.h"

// 메인 창과 같은 어두운 색상 (서브 대화상자 공통)
namespace DarkColors
{
	inline const COLORREF& Back        = TCOL(TC_BACK);   // 배경
	inline const COLORREF& Edit        = TCOL(TC_EDIT);   // 에디트 / 날짜
	inline const COLORREF& Text        = TCOL(TC_TEXT);
	inline const COLORREF& DisabledText = TCOL(TC_DISABLED);
	inline const COLORREF& Combo       = TCOL(TC_COMBO);   // 콤보박스
	inline const COLORREF& Button      = TCOL(TC_ACCENT);   // 버튼
	inline const COLORREF& Danger      = TCOL(TC_DANGER);   // 삭제 버튼
	inline const COLORREF& Save        = TCOL(TC_SAVE);   // 저장 버튼 (#0F9960)
	inline const COLORREF& SelBack     = TCOL(TC_SEL);        // 목록 선택
	inline const COLORREF& Border      = TCOL(TC_EDGE);
	inline const COLORREF& Preview     = TCOL(TC_PREVIEW);   // 이미지 영역
}

// 목록(그리드)의 열 머리글 - 어두운 배경에 흰 글자로 직접 그림
class CDarkHeader : public CHeaderCtrl
{
protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	DECLARE_MESSAGE_MAP()
};

// 대화상자 하나를 메인 창과 같은 색으로 맞추는 도우미
//  - OnInitDialog 에서 Apply(this, { 삭제 버튼 ID... }) 호출
//  - OnCtlColor 에서 CtlColor() 가 nullptr 이 아니면 그 브러시를 반환
//  - 푸시 버튼은 CDarkButton 으로 바꾸고, 목록/에디트/콤보는 어두운 테마(스크롤바·머리글 포함)를 적용
class CDarkDialogTheme
{
public:
	void   Apply(CWnd* dlg, std::initializer_list<UINT> dangerIds = {});
	HBRUSH CtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	static void ThemeChild(HWND hwnd);   // 목록·에디트·콤보에 Windows 어두운 테마 적용

private:
	CBrush m_back;
	CBrush m_edit;
	std::vector<std::unique_ptr<CDarkButton>> m_buttons;
	std::vector<std::unique_ptr<CDarkHeader>> m_headers;
	std::vector<std::unique_ptr<CDarkCheck>>  m_checks;    // 체크박스 · 라디오 (Windows 11 모양으로 직접 그림)
};
