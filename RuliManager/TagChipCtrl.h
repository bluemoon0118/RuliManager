#pragma once

#include <functional>
#include "SuggestEdit.h"

// 태그 칩 입력: [태그 ×] [태그 ×] ... [입력 칸]          (×: 모두 지우기, ⌄: 목록에서 선택)
//  - 칩의 × 를 누르면 그 태그 삭제, 입력 칸에 글자 입력 → Enter / 쉼표 / 목록 선택으로 추가
//  - 빈 입력 칸에서 Backspace → 마지막 태그 삭제, 칩이 넘치면 다음 줄로 (높이는 CalcHeight)
class CTagChipCtrl : public CWnd
{
public:
	CSuggestEdit m_edit;                         // 새 태그 입력 (자동 완성)
	std::function<void()> m_onChanged;           // 사용자가 태그를 추가/삭제함
	std::function<void()> m_onDropDown;          // ⌄ 버튼
	std::function<void()> m_onHeightChanged;     // 필요한 높이가 바뀜 (부모가 다시 배치)
	// 칩 이름 뒤에 회색으로 붙일 글자 (예: 배우의 별칭). 비어 있으면 이름만
	std::function<CString(const CString&)> m_subText;
	// 칩에 보일 글자 (값과 다르게 보여 줄 때, 예: 배우 칩에 이 작품의 별칭). 비어 있으면 값 그대로
	std::function<CString(const CString&)> m_displayText;
	// 칩(× 제외)을 클릭함: (칩 번호, 화면 좌표)
	std::function<void(int, CPoint)> m_onChipClick;
	CString DisplayOf(const CString& tag) const;

	bool Create(CWnd* parent, UINT id);
	void SetColors(COLORREF back, COLORREF chipBack, COLORREF chipText, COLORREF icon, COLORREF text);
	void SetTags(const std::vector<CString>& tags);   // 알림 없이 표시만 바꿈
	const std::vector<CString>& Tags() const { return m_tags; }
	int  CalcHeight(int width);                       // 이 폭에서 필요한 높이 (픽셀)
	bool AddFromEdit();                               // 입력 칸의 글자를 태그로 추가

protected:
	std::vector<CString> m_tags;
	std::vector<CRect>   m_chipRects;
	std::vector<CRect>   m_xRects;
	CRect m_clearRect, m_dropRect;
	int   m_lastHeight = 0;
	COLORREF m_back = RGB(0x1B, 0x25, 0x2C);
	COLORREF m_chipBack = RGB(0xCE, 0xD9, 0xE0);
	COLORREF m_chipText = RGB(0x18, 0x20, 0x26);
	COLORREF m_icon = RGB(0xA7, 0xB6, 0xC2);
	COLORREF m_text = RGB(232, 238, 242);
	COLORREF m_subColor = RGB(0x5C, 0x70, 0x80);   // 칩 안 회색 글자
	CBrush   m_backBrush;

	int  DoLayout(int width, bool apply);
	void TagsChanged(bool notify);
	void RemoveAt(int index);

	BOOL PreTranslateMessage(MSG* pMsg) override;
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnEnable(BOOL bEnable);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnEditKillFocus();
	DECLARE_MESSAGE_MAP()
};
