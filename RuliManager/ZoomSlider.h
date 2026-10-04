#pragma once

#include <functional>

// 단계 슬라이더 (격자 확대): 파란 트랙 위의 회색 동그라미를 끌거나 클릭해서 단계 선택
//  - 마우스 끌기 / 클릭 / 휠 / ←→ 키, 값이 바뀌면 m_onChanged(새 단계)
class CZoomSlider : public CWnd
{
public:
	std::function<void(int)> m_onChanged;

	bool Create(CWnd* parent, UINT id, int steps = 4);
	void SetPos(int pos);                 // 알림 없이 위치만 바꿈
	int  GetPos() const { return m_pos; }
	void SetColors(COLORREF back, COLORREF track, COLORREF thumb, COLORREF thumbBorder);

protected:
	int  m_steps = 4;
	int  m_pos = 0;
	bool m_dragging = false;
	COLORREF m_back = RGB(0x20, 0x2B, 0x33);
	COLORREF m_track = RGB(0x13, 0x7C, 0xBD);
	COLORREF m_thumb = RGB(0x5C, 0x70, 0x80);
	COLORREF m_thumbBorder = RGB(0x8A, 0x9B, 0xA8);

	int  ThumbRadius() const;
	int  PosToX(int pos) const;
	int  XToPos(int x) const;
	void ChangeTo(int pos);

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg UINT OnGetDlgCode();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	DECLARE_MESSAGE_MAP()
};
