#pragma once

#include <functional>

// 별점 컨트롤: ★★★☆☆ 4
//  - 마우스를 올린 위치까지 별이 미리 표시되고, 클릭하면 별점 확정 (m_onChanged)
//  - 지금 별점과 같은 별을 다시 클릭하면 0점(없음)
//  - ←→ 키 / 0~5 숫자 키 / 휠로도 변경
class CStarRatingCtrl : public CWnd
{
public:
	// 값 단위 = 반 별 (0 ~ 별 수 x 2, 예: 7 = 별 3.5개)
	std::function<void(int)> m_onChanged;

	bool Create(CWnd* parent, UINT id, int maxStars = 5);
	void SetRating(int rating);              // 알림 없이 값만 바꿈 (반 별 단위)
	int  GetRating() const { return m_rating; }
	// 별점 체계 (설정 창) - 별은 5개 기준, 값은 반 별 단위(0 ~ 10)로 같음
	//  0 = 1점 단위 5점 만점 (별 하나씩, "3"), 1 = 0.5점 단위 5점 만점 (별 반쪽 - 별 왼쪽 절반 = .5, "3.5"), 2 = 1점 단위 10점 만점 (별 반쪽 = 1점, "7")
	enum { MODE_ONE5 = 0, MODE_HALF5 = 1, MODE_ONE10 = 2 };
	static void SetRatingMode(int mode) { s_mode = (mode >= MODE_ONE5 && mode <= MODE_ONE10) ? mode : MODE_ONE5; }
	static int  RatingMode() { return s_mode; }
	static bool HalfStep() { return s_mode != MODE_ONE5; }   // 반 별 단위로 입력 (0.5점 · 10점 만점)
	static CString ScoreText(int halfUnits);                 // 표시용 점수: "3" / "3.5" (5점 만점) · "7" (10점 만점)
	static CString ScoreLabel(int halfUnits);                // 카드 리본 · 정보용: "3.5" / "7/10"
	void SetColors(COLORREF back, COLORREF star, COLORREF empty, COLORREF text);
	// 이 컨트롤과 같은 모양으로 다른 곳(영상 [리스트] 별점 열 등)에 그리기 / 클릭 위치 → 값
	//  band: 별 줄 영역 (별 크기 = 높이 - 4, 왼쪽부터), 배경은 칠하지 않음, shown = 반 별 단위 값, 오른쪽에 점수 글자
	static void DrawStars(CDC* dc, const CRect& band, int shown, int maxStars, COLORREF star, COLORREF empty, COLORREF text, CFont* font, bool enabled = true);
	static int  HitStarIn(const CRect& band, CPoint pt, int maxStars);   // 반 별 단위 값 (별 밖이면 0)
	static int  StarsWidth(int bandH, int maxStars);                     // 별 줄 폭 (점수 글자 제외)

protected:
	static int  s_mode;
	int  m_max = 5;
	int  m_rating = 0;
	int  m_hover = -1;                       // 마우스가 올라간 별 수 (-1: 없음)
	bool m_tracking = false;
	COLORREF m_back = TCOL(TC_BACK);
	COLORREF m_star = RGB(0xFF, 0xC8, 0x1E);
	COLORREF m_empty = RGB(0xE6, 0xEA, 0xEE);
	COLORREF m_text = RGB(0xFF, 0xFF, 0xFF);

	int  StarSize() const;
	int  StarGap() const;
	int  HitStar(CPoint pt) const;            // 반 별 단위 값 (0.5점 방식: 별 왼쪽 절반 = .5), 별 영역 밖이면 0
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
