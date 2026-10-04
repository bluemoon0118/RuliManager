#pragma once

#include <functional>

// 즐겨찾기 하트 버튼: 켜짐 = 빨간 하트, 꺼짐 = 회색 하트 (마우스를 올리면 밝게), 클릭하면 m_onClick
class CHeartToggle : public CWnd
{
public:
	std::function<void()> m_onClick;

	bool Create(CWnd* parent, UINT id);
	void SetOn(bool on);
	bool IsOn() const { return m_on; }
	void SetColors(COLORREF back, COLORREF on, COLORREF off, COLORREF offHover);

protected:
	bool m_on = false;
	bool m_hover = false;
	bool m_tracking = false;
	COLORREF m_back = RGB(0x20, 0x2B, 0x33);
	COLORREF m_onColor = RGB(0xF2, 0x5C, 0x54);
	COLORREF m_offColor = RGB(0x95, 0x98, 0x9D);
	COLORREF m_offHover = RGB(0xC8, 0xCC, 0xD0);

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
	afx_msg void OnEnable(BOOL bEnable);
	DECLARE_MESSAGE_MAP()
};
