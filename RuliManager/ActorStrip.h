#pragma once

#include <functional>

// 가로 한 줄 카드 띠 (영상 상세 정보 아래의 출연 배우 카드)
//  - 카드는 주인이 그림 (m_onDraw), 넘치면 아래에 얇은 가로 스크롤바 (휠 / 끌기 / 트랙 클릭)
//  - 카드 클릭 = 선택 표시, 더블클릭 = m_onActivate
class CActorStrip : public CWnd
{
public:
	std::function<void(CDC*, int, const CRect&, bool)> m_onDraw;   // (dc, 카드 번호, 카드 영역, 마우스 오버)
	std::function<void(int)> m_onActivate;                          // 더블클릭
	std::function<CString(int)> m_onTip;                            // 마우스 오버 툴팁 (전체 이름)
	std::function<int(int, const CRect&, CPoint)> m_onHitPart;      // 카드 위 버튼 번호 (0 = 없음, 1 = 하트 …)
	std::function<void(int, int)> m_onPartClick;                    // 버튼 클릭 (카드 번호, 버튼 번호)
	int  HotPart() const { return m_hotPart; }                      // 마우스가 올라간 카드의 버튼 번호

	bool Create(CWnd* parent, UINT id);
	void SetColors(COLORREF back, COLORREF text, COLORREF bar, COLORREF thumb);
	void SetCardSize(int w, int h, int gap);
	void SetCount(int count);            // 카드 수 (바뀌면 맨 앞으로 스크롤)
	void SetEmptyText(const CString& text) { m_empty = text; Invalidate(FALSE); }
	int  BarHeight() const { return m_barH; }
	int  CalcHeight() const { return m_cardH + m_barH + 2; }   // 카드 + 스크롤바 자리
	// 여러 줄 모드: 카드가 폭을 넘으면 다음 줄로 (가로 스크롤 없음) - 제작사 상세의 하위 레이블 카드
	void SetWrap(bool wrap) { m_wrap = wrap; m_scroll = 0; m_vscroll = 0; if (GetSafeHwnd()) Invalidate(FALSE); }
	int  CalcWrapHeight(int width) const;   // 이 폭에서 모든 카드가 들어가는 높이
	int  RowsHeight(int rows) const { return rows <= 0 ? 0 : rows * m_cardH + (rows - 1) * m_gap; }   // 카드 n줄 높이
	//  여러 줄 모드에서 창 높이보다 카드가 많으면 오른쪽에 얇은 세로 스크롤바 (휠 / 끌기 / 트랙 클릭)

protected:
	int m_count = 0;
	bool m_wrap = false;
	int  PerRow(int width) const { return (std::max)(1, (width + m_gap) / (std::max)(1, m_cardW + m_gap)); }
	int m_cardW = 80, m_cardH = 120, m_gap = 6;
	int m_barH = 7;
	int m_scroll = 0;     // 가로 스크롤 (픽셀)
	int m_vscroll = 0;    // 여러 줄 모드: 세로 스크롤 (픽셀)
	bool m_dragVBar = false;
	int  m_dragStartY = 0, m_dragStartVScroll = 0;
	bool NeedVBar() const;          // 여러 줄 모드에서 세로 스크롤이 필요한지
	int  WrapWidth() const;         // 카드를 놓는 폭 (세로 스크롤바 자리 제외)
	int  MaxVScroll() const;
	void SetVScroll(int pos);
	bool VThumbRect(CRect& rc) const;
	int m_hot = -1;
	int m_hotPart = 0;
	bool m_tracking = false;
	bool m_dragBar = false;
	int  m_dragStartX = 0, m_dragStartScroll = 0;
	COLORREF m_back = RGB(0x20, 0x2B, 0x33);
	COLORREF m_text = RGB(0x8A, 0x9B, 0xA8);
	COLORREF m_bar = RGB(0x2A, 0x35, 0x3D);
	COLORREF m_thumb = RGB(0x6C, 0x74, 0x78);
	CString m_empty;
	CToolTipCtrl m_tip;
	int m_tipRow = -1;

	int  ContentWidth() const;
	int  MaxScroll() const;
	void SetScroll(int pos);
	bool ThumbRect(CRect& rc) const;
	int  HitTest(CPoint pt) const;
	int  PartAt(int i, CPoint pt) const;
	CRect CardRect(int i) const;

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnMouseHWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnMouseMove(UINT nFlags, CPoint pt);
	afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint pt);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint pt);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint pt);
	afx_msg void OnCaptureChanged(CWnd* pWnd);
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	DECLARE_MESSAGE_MAP()
};

// 마우스를 올린 항목 옆에 잠깐 띄우는 카드 팝업 (예: 태그 칩 위 → 태그 카드)
//  - 포커스를 가져가지 않고 마우스도 통과 (아래 컨트롤 그대로 사용), 내용은 주인이 그림 (m_onDraw)
class CCardPopup : public CWnd
{
public:
	std::function<void(CDC*, const CRect&)> m_onDraw;
	bool CreatePopup(CWnd* owner);
	void ShowNear(const CRect& anchorScreen, CSize size);   // 기준 영역 위(공간이 없으면 아래)에 표시
	void Hide();
	COLORREF m_back = RGB(0x20, 0x2B, 0x33);

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg LRESULT OnNcHitTest(CPoint) { return HTTRANSPARENT; }
	afx_msg int OnMouseActivate(CWnd*, UINT, UINT) { return MA_NOACTIVATE; }
	DECLARE_MESSAGE_MAP()
};

