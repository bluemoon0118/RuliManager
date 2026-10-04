#pragma once

#include <functional>

// 자동 완성 후보 한 개: text = 목록에 보이는 글자("값\t오른쪽 설명" 가능), value = 입력될 값
struct SuggestItem
{
	CString text;
	CString value;
};

class CSuggestEdit;

// 에디트 아래에 뜨는 후보 목록 (포커스를 가져가지 않는 팝업 리스트박스, 직접 그림)
class CSuggestList : public CListBox
{
public:
	CSuggestEdit* m_edit = nullptr;
	std::vector<SuggestItem> m_items;
	COLORREF m_back = RGB(0x1B, 0x25, 0x2C);
	COLORREF m_text = RGB(232, 238, 242);
	COLORREF m_sel = RGB(0x13, 0x7C, 0xBD);
	COLORREF m_hint = RGB(140, 155, 168);

protected:
	void DrawItem(LPDRAWITEMSTRUCT lpDIS) override;
	void MeasureItem(LPMEASUREITEMSTRUCT) override {}
	afx_msg int  OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	DECLARE_MESSAGE_MAP()
};

// 글자를 입력하면 후보를 실시간으로 찾아 콤보박스처럼 아래에 목록으로 보여 주는 에디트
//  - multi = true 이면 쉼표로 구분된 여러 값 중 커서가 있는 값만 완성
//  - ↓ : 목록 열기/다음 항목, ↑ : 이전 항목, Enter/Tab : 선택, Esc : 닫기, 마우스 클릭 : 선택
class CSuggestEdit : public CEdit
{
public:
	// 전체 후보를 돌려주는 함수 (입력값으로 거르는 것은 에디트가 함)
	using Provider = std::function<void(std::vector<SuggestItem>& out)>;

	void Setup(Provider provider, bool multi);
	void SetColors(COLORREF back, COLORREF text, COLORREF sel, COLORREF hint);
	std::function<void(const SuggestItem&)> m_onAccept;   // 후보를 고른 뒤 호출 (선택 사항)

	void HidePopup();
	bool IsPopupVisible() const;
	void Accept(int index);
	BOOL PreTranslateMessage(MSG* pMsg) override;

protected:
	Provider m_provider;
	bool m_multi = false;
	bool m_accepting = false;
	CSuggestList m_popup;

	CString CurrentToken(int& start, int& end) const;
	void UpdatePopup(bool showAll);
	void MoveSelection(int delta);

	afx_msg BOOL OnEnChangeReflect();
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnDestroy();
	DECLARE_MESSAGE_MAP()
};
