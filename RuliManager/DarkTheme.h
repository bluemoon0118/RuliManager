#pragma once

#include <memory>
#include "DarkControls.h"

// 메인 창과 같은 어두운 색상 (서브 대화상자 공통)
namespace DarkColors
{
	const COLORREF Back         = RGB(0x20, 0x2B, 0x33);   // 배경
	const COLORREF Edit         = RGB(0x1B, 0x25, 0x2C);   // 에디트 / 날짜
	const COLORREF Text         = RGB(232, 238, 242);
	const COLORREF DisabledText = RGB(120, 135, 148);
	const COLORREF Combo        = RGB(0x39, 0x4B, 0x59);   // 콤보박스
	const COLORREF Button       = RGB(0x13, 0x7C, 0xBD);   // 버튼
	const COLORREF Danger       = RGB(0xDB, 0x37, 0x37);   // 삭제 버튼
	const COLORREF Save         = RGB(0x0F, 0x99, 0x60);   // 저장 버튼 (#0F9960)
	const COLORREF SelBack      = RGB(38, 79, 120);        // 목록 선택
	const COLORREF Border       = RGB(70, 85, 98);
	const COLORREF Preview      = RGB(0x18, 0x21, 0x28);   // 이미지 영역
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
};
