#pragma once

#include <functional>

// 별점 컨트롤: ★★★☆☆ 4
//  - 마우스를 올린 위치까지 별이 미리 표시되고, 클릭하면 별점 확정 (m_onChanged)
//  - 지금 별점과 같은 별을 다시 클릭하면 0점(없음)
//  - ←→ 키 / 0~5 숫자 키 / 휠로도 변경
class CStarRatingCtrl : public CWnd
{
public:
	std::function<void(int)> m_onChanged;

	bool Create(CWnd* parent, UINT id, int maxStars = 5);
	void SetRating(int rating);              // 알림 없이 값만 바꿈
	int  GetRating() const { return m_rating; }
	void SetColors(COLORREF back, COLORREF star, COLORREF empty, COLORREF text);

protected:
	int  m_max = 5;
	int  m_rating = 0;
	int  m_hover = -1;                       // 마우스가 올라간 별 수 (-1: 없음)
	bool m_tracking = false;
	COLORREF m_back = RGB(0x20, 0x2B, 0x33);
	COLORREF m_star = RGB(0xFF, 0xC8, 0x1E);
	COLORREF m_empty = RGB(0xE6, 0xEA, 0xEE);
	COLORREF m_text = RGB(0xFF, 0xFF, 0xFF);

	int  StarSize() const;
	int  StarGap() const;
	int  HitStar(CPoint pt) const;            // 1..max, 별 영역 밖이면 0
	void ChangeTo(int rating);

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg UINT OnGetDlgCode();
	afx_msg void OnEnable(BOOL bEnable);
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	DECLARE_MESSAGE_MAP()
};
