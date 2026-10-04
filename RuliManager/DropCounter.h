#pragma once

#include <functional>

// 물방울 아이콘 + 숫자 카운터 (영상 상세 정보 별점 오른쪽)
//  - 클릭: +1, 오른쪽 클릭: [초기화] 메뉴 → 0
//  - 값이 바뀌면 m_onChanged(새 값)
class CDropCounter : public CWnd
{
public:
	std::function<void(int)> m_onChanged;

	bool Create(CWnd* parent, UINT id);
	void SetColors(COLORREF back, COLORREF normal, COLORREF hover);
	void SetCount(int count);
	int  GetCount() const { return m_count; }

protected:
	int  m_count = 0;
	bool m_hover = false;
	bool m_tracking = false;
	COLORREF m_back = RGB(0x20, 0x2B, 0x33);
	COLORREF m_normal = RGB(0xE8, 0xEE, 0xF2);
	COLORREF m_hoverColor = RGB(0x48, 0xAF, 0xF0);
	CToolTipCtrl m_tip;

	void Change(int count);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
	afx_msg void OnEnable(BOOL bEnable);
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	DECLARE_MESSAGE_MAP()
};
