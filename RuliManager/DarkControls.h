#pragma once

// 직접 그린 스크롤바가 대상 창에 스크롤 위치를 지정할 때 보내는 메시지 (wParam = 위치, lParam = 1 이면 가로 스크롤바)
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
	COLORREF   m_track    = TCOL(TC_SCROLL);
	COLORREF   m_thumb    = TCOL(TC_SCROLLTHUMB);
	COLORREF   m_thumbHot = TCOL(TC_SCROLLTHUMBHOT);

	SCROLLINFO m_si = {};
	bool  m_enabled = true;
	CRect m_barRect;              // 실제 스크롤바 영역 (이 창의 클라이언트 좌표, 모서리 칸 제외)
	bool  m_dragging = false;
	int   m_dragStartMouse = 0;
	int   m_dragStartPos = 0;
	bool  m_hot = false;
	int   m_repeatCode = -1;      // 누르고 있을 때 반복할 SB_PAGEUP/SB_PAGEDOWN (트랙) · SB_LINEUP/SB_LINEDOWN (양 끝 버튼)
	int   m_hotArrow = 0;         // 마우스가 올라간 끝 버튼 (0 없음, 1 위/왼쪽, 2 아래/오른쪽)

	// 양 끝 화살표 버튼 (▲ / ▼): 길이 = 스크롤바 두께 (스크롤바가 짧으면 없음)
	int   ArrowLen() const;
	CRect ArrowRect(int which) const;   // 1 = 위/왼쪽, 2 = 아래/오른쪽
	CRect TrackRect() const;            // 화살표 버튼을 뺀 트랙 영역 (썸이 움직이는 곳)
	bool GetThumbRect(CRect& rc) const;
	int  TrackLength() const { const CRect t = TrackRect(); return m_vert ? t.Height() : t.Width(); }
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
	// 항목별 글자색 (항목 번호 순서, CLR_DEFAULT = 기본 글자색) - 예: 영상 상세 검열 콤보
	void SetItemColors(const std::vector<COLORREF>& colors) { m_itemColors = colors; if (GetSafeHwnd()) Invalidate(FALSE); }

	virtual void DrawItem(LPDRAWITEMSTRUCT lpDIS);
	virtual void MeasureItem(LPMEASUREITEMSTRUCT lpMIS);

protected:
	COLORREF m_bg    = TCOL(TC_COMBO);
	COLORREF m_text  = TCOL(TC_TEXT);
	COLORREF m_selBg = TCOL(TC_SEL);
	CBrush   m_brush;
	std::vector<COLORREF> m_itemColors;

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
	enum Icon { ICON_NONE = 0, ICON_PLAY, ICON_PERSON, ICON_CAMERA, ICON_TAG, ICON_GEAR, ICON_COLLECTION };   // COLLECTION: 겹친 화면 (컬렉션 탭)   // GEAR: 글자 없이 가운데 톱니바퀴   // PLAY: 원 안의 재생(▶), PERSON: 사람, CAMERA: 비디오 카메라, TAG: 꼬리표
	void SetIcon(int icon) { m_icon = icon; if (GetSafeHwnd()) Invalidate(FALSE); }

	// 붙은 버튼 묶음(세그먼트 컨트롤)의 한 칸: 안쪽 모서리는 각지게, 칸 사이에 가는 구분선, 묶음 전체에 테두리
	enum Segment { SEG_NONE = 0, SEG_LEFT, SEG_MIDDLE, SEG_RIGHT };
	void SetSegment(int seg) { m_segment = seg; if (GetSafeHwnd()) Invalidate(FALSE); }

	virtual void DrawItem(LPDRAWITEMSTRUCT lpDIS);

protected:
	bool     m_toggle   = false;
	bool     m_checked  = false;
	COLORREF m_offBg    = TCOL(TC_COMBO);
	COLORREF m_offText  = TCOL(TC_TOGGLETEXT);
	COLORREF m_bg       = TCOL(TC_ACCENT);
	COLORREF m_text     = RGB(255, 255, 255);
	COLORREF m_parentBg = TCOL(TC_BACK);
	bool     m_hover    = false;
	int      m_icon     = ICON_NONE;
	int      m_segment  = SEG_NONE;

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
	COLORREF m_bg     = TCOL(TC_EDIT);
	COLORREF m_text   = TCOL(TC_TEXT);
	COLORREF m_border = TCOL(TC_EDGE);
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

// ---------------------------------------------------------------------------
// 체크박스 · 라디오 버튼을 Windows 11 모양으로 직접 그림 (기존 Button 컨트롤을 서브클래싱)
//  - 체크: 둥근 사각형 (꺼짐 = 어두운 칸 + 회색 테두리, 켜짐 = 강조색 칸 + 흰 체크 표시, 3상태 = 흰 가로줄)
//  - 라디오: 원 (꺼짐 = 어두운 원 + 회색 테두리, 켜짐 = 강조색 원 + 가운데 흰 원)
//  - 마우스 오버 = 테두리 밝게 / 누름 = 조금 어둡게, 비활성 = 흐리게, 키보드 포커스 = 글자 둘레 점선
//  - 클릭 · 그룹 동작은 원래 컨트롤 그대로 (그리기만 바꿈, 상태가 바뀌는 메시지 뒤에 다시 그림)
class CDarkCheck : public CWnd
{
public:
	bool Subclass(HWND hwnd);

protected:
	bool m_hot = false;
	bool m_tracking = false;
	bool IsRadio() const;
	void PaintTo(CDC* dc);
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;
};

