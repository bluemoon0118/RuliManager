#pragma once

// 직접 그린 스크롤바가 대상 창에 스크롤 위치를 지정할 때 보내는 메시지 (wParam = 위치)
const UINT WM_DARKSCROLL_SETPOS = WM_APP + 50;

// ---------------------------------------------------------------------------
// 직접 그리는 스크롤바
// 대상 창(목록/에디트/격자)의 기본 스크롤바 위에 겹쳐 놓고, 같은 위치/크기/상태를 유지하면서
// 정해진 색으로 그립니다. 클릭/드래그/휠은 대상 창의 스크롤로 전달합니다.
class CDarkScrollBar : public CWnd
{
public:
	enum TargetType { TARGET_LISTVIEW, TARGET_EDIT, TARGET_GRID };

	BOOL Create(CWnd* parent, CWnd* target, bool vertical, TargetType type);
	void SetColors(COLORREF track, COLORREF thumb, COLORREF thumbHot);
	void Sync();   // 대상 스크롤바의 위치/상태를 다시 읽어 맞춤

protected:
	CWnd*      m_target = nullptr;
	bool       m_vert = true;
	TargetType m_type = TARGET_LISTVIEW;
	COLORREF   m_track    = RGB(0x6C, 0x74, 0x78);
	COLORREF   m_thumb    = RGB(0xB5, 0xBC, 0xC0);
	COLORREF   m_thumbHot = RGB(0xDD, 0xE2, 0xE5);

	SCROLLINFO m_si = {};
	bool  m_enabled = true;
	CRect m_barRect;              // 실제 스크롤바 영역 (이 창의 클라이언트 좌표, 모서리 칸 제외)
	bool  m_dragging = false;
	int   m_dragStartMouse = 0;
	int   m_dragStartPos = 0;
	bool  m_hot = false;
	int   m_repeatCode = -1;      // 트랙을 누르고 있을 때 반복할 SB_PAGEUP/SB_PAGEDOWN

	bool GetThumbRect(CRect& rc) const;
	int  TrackLength() const { return m_vert ? m_barRect.Height() : m_barRect.Width(); }
	int  MinThumb() const;
	void ScrollTargetTo(int pos);
	void SendScroll(int code);

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnCaptureChanged(CWnd* pWnd);
	afx_msg int  OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message);
	DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------------
// 배경색을 지정할 수 있는 콤보박스 (CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS)
class CDarkCombo : public CComboBox
{
public:
	void SetColors(COLORREF bg, COLORREF text, COLORREF selBg);
	void SetHeights(int fieldHeight, int itemHeight);

	virtual void DrawItem(LPDRAWITEMSTRUCT lpDIS);
	virtual void MeasureItem(LPMEASUREITEMSTRUCT lpMIS);

protected:
	COLORREF m_bg    = RGB(0x39, 0x4B, 0x59);
	COLORREF m_text  = RGB(232, 238, 242);
	COLORREF m_selBg = RGB(38, 79, 120);
	CBrush   m_brush;

	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------------
// 배경색을 지정할 수 있는 버튼 (BS_OWNERDRAW 로 바꿔서 직접 그림)
class CDarkButton : public CButton
{
public:
	// 대화상자의 버튼을 연결하고 직접 그리기 방식으로 바꿉니다.
	BOOL Attach(UINT id, CWnd* parent);
	void SetColors(COLORREF bg, COLORREF text, COLORREF parentBg);

	// 토글 버튼: 선택(체크)되면 bg 색, 아니면 offBg 색으로 그림
	void SetToggle(COLORREF offBg, COLORREF offText);
	void SetChecked(bool checked);
	bool IsChecked() const { return m_checked; }

	// 글자 왼쪽 아이콘
	enum Icon { ICON_NONE = 0, ICON_PLAY, ICON_PERSON, ICON_CAMERA, ICON_TAG, ICON_GEAR };   // GEAR: 글자 없이 가운데 톱니바퀴   // PLAY: 원 안의 재생(▶), PERSON: 사람, CAMERA: 비디오 카메라, TAG: 꼬리표
	void SetIcon(int icon) { m_icon = icon; if (GetSafeHwnd()) Invalidate(FALSE); }

	virtual void DrawItem(LPDRAWITEMSTRUCT lpDIS);

protected:
	bool     m_toggle   = false;
	bool     m_checked  = false;
	COLORREF m_offBg    = RGB(0x39, 0x4B, 0x59);
	COLORREF m_offText  = RGB(190, 200, 208);
	COLORREF m_bg       = RGB(0x13, 0x7C, 0xBD);
	COLORREF m_text     = RGB(255, 255, 255);
	COLORREF m_parentBg = RGB(0x20, 0x2B, 0x33);
	bool     m_hover    = false;
	int      m_icon     = ICON_NONE;

	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnEnable(BOOL bEnable);
	DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------------
// 배경색을 지정할 수 있는 날짜 선택 컨트롤 (직접 그림)
// 날짜 칸: 숫자를 바로 입력할 수 있음 - "19980217" / "1998-02-17" / "1998.2.17" 처럼 치고 Enter (또는 8자리 / 포커스 이동 시 확정)
//   Backspace 로 입력 중 글자 지우기, Esc 취소, Delete 는 날짜 지우기(없음), 달력(▼)도 그대로 사용
class CDarkDateTime : public CDateTimeCtrl
{
public:
	void SetColors(COLORREF bg, COLORREF text, COLORREF border);

protected:
	COLORREF m_bg     = RGB(0x1B, 0x25, 0x2C);
	COLORREF m_text   = RGB(232, 238, 242);
	COLORREF m_border = RGB(70, 85, 98);
	bool     m_typing = false;   // 직접 입력 중
	CString  m_typed;            // 입력한 글자 (숫자와 구분자)

	bool CommitTyped();          // 입력한 글자를 날짜로 (성공하면 true)
	void CancelTyped();
	void NotifyChanged(bool valid, const SYSTEMTIME& st);
	static bool ParseTyped(const CString& text, SYSTEMTIME& st);

	afx_msg void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg UINT OnGetDlgCode();

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnEnable(BOOL bEnable);
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	DECLARE_MESSAGE_MAP()
};
