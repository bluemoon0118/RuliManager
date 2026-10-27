#pragma once

// 격자 보기에서 표시할 정보를 제공하는 쪽 (메인 대화상자)이 구현
class IVideoGridOwner
{
public:
	virtual ~IVideoGridOwner() = default;
	virtual int  GridGetCount() = 0;
	virtual int  GridGetSel() = 0;
	virtual void GridSetSel(int row) = 0;
	// name: 굵은 첫 줄, release: 파란 둘째 줄, memo: 회색 2줄
	virtual void GridGetItem(int row, int& image, CString& name, CString& release, CString& memo) = 0;
	virtual void GridActivate(int row) = 0;       // 더블클릭 / Enter
	virtual void GridKey(UINT vk) = 0;            // Del, F2, F5, Space 등
	// 카드를 직접 그리려면 true 반환 (SetFixedTile 과 함께 사용, 기본은 격자가 그림)
	virtual bool GridDrawCard(CDC* /*dc*/, int /*row*/, const CRect& /*card*/, bool /*selected*/, bool /*focused*/) { return false; }
	// 카드 안 클릭 (고정 카드만): 카드 위의 버튼(즐겨찾기 등)을 눌렀으면 true (더블클릭 동작 안 함)
	virtual bool GridClick(int /*row*/, const CRect& /*card*/, CPoint /*pt*/) { return false; }
	// 마우스가 카드의 어느 부분 위에 있는지 (0: 버튼 아님, 1 이상: 버튼 번호) - 바뀔 때만 다시 그림
	virtual int  GridHitPart(int /*row*/, const CRect& /*card*/, CPoint /*pt*/) { return 0; }
	// 마우스가 카드의 어느 영역 위에 있는지 (그리기용, 클릭 · 더블클릭에는 영향 없음 - 예: 영상 카드 이미지 위) - 바뀔 때만 다시 그림
	virtual int  GridHoverZone(int /*row*/, const CRect& /*card*/, CPoint /*pt*/) { return 0; }
	// 카드 위 특정 영역 더블클릭을 주인이 처리했으면 true (열기 동작 안 함) - 예: 스크린샷 추출 진행 표시 → 멈춤
	virtual bool GridDblClick(int /*row*/, const CRect& /*card*/, CPoint /*pt*/) { return false; }
	// 마우스 위치의 풍선 도움말 글자 (비우면 표시 안 함 - 예: 영상 카드에서 잘린 제목 전체)
	virtual CString GridTipText(int /*row*/, const CRect& /*card*/, CPoint /*pt*/) { return CString(); }
	// 화면 바로 위 · 아래(한 화면씩)의 카드 범위 [first, last] - 이미지를 미리 읽어 두기용 (스크롤할 때 "불러오는 중" 깜빡임 줄임)
	virtual void GridPrefetch(int /*first*/, int /*last*/) {}
	// 세로 스크롤 위치가 바뀜 (카드 위에 띄운 미리보기 팝업을 따라 옮기기용)
	virtual void GridScrolled() {}
	// 묶음 머리줄 그리기 (GridLine::header 인 줄 위의 띠, SetGroupHeaderHeight 높이) - firstCard: 그 줄의 첫 카드
	virtual void GridDrawGroupHeader(CDC* /*dc*/, int /*firstCard*/, const CRect& /*rc*/) {}
};

// 줄 배치(리스트 보기)의 한 줄: start 번 카드부터 다음 줄 start 전까지 - 한 화면 줄에 모두 (넘치면 좌우 스크롤)
//  firstCol: 첫 카드의 칸 (예: 앞 페이지 제작사의 레이블이 이어지면 1 → 제작사 칸 비움), wrapCol: 사용 안 함 (예전 줄 바꿈용)
//  header: 이 줄 위에 묶음 머리줄 띠 (SetGroupHeaderHeight 높이, 주인이 GridDrawGroupHeader 로 그림)
//  headerH: 이 줄 머리줄 높이 (0 = SetGroupHeaderHeight 값)
struct GridLine { int start = 0; int firstCol = 0; int wrapCol = 0; bool header = false; int headerH = 0; };

// 썸네일 + 파일명 + 발매일 + 메모를 카드 형태로 바둑판 배열하는 컨트롤
class CVideoGrid : public CWnd
{
public:
	BOOL Create(CWnd* parent, UINT id, IVideoGridOwner* owner, CImageList* images, int thumbW, int thumbH);

	void Refresh();                 // 항목 수 변경 후 호출
	void EnsureVisible(int row);
	void OnSelectionChanged();      // 선택이 바뀐 뒤 호출 (보이게 스크롤 + 다시 그림)
	bool GetTileRect(int row, CRect& rc) const;  // 클라이언트 좌표
	void SetShowImage(bool show);   // false: 이미지 없이 글자만 있는 카드
	void SetBackColor(COLORREF bg); // 배경색 (어두운 색이면 글자/카드 색도 어둡게 맞춤)
	int  HitTest(CPoint pt) const;
	// 카드 크기를 고정 (0 이면 기본: 썸네일 + 글자 4줄). 남는 폭은 양쪽으로 나눠 가운데 정렬
	void SetFixedTile(int cardW, int cardH);
	void SetTilePad(int pad);
	int  BasePad() const { return m_basePad; }   // 기본 카드 둘레 여백 (칸 = 카드 + 여백 × 2)   // 고정 카드 둘레 여백 (-1 = 기본 - 글꼴 높이 기준, 0 = 카드끼리 딱 붙임: 영상 [월 모드])
	int  HotRow() const { return m_hot; }   // 마우스가 올라가 있는 카드 (-1: 없음)
	int  HotPart() const { return m_hotPart; }   // 그 카드에서 마우스가 올라간 버튼 (GridHitPart 값)
	int  HotZone() const { return m_hotZone; }   // 그 카드에서 마우스가 올라간 영역 (GridHoverZone 값)

	// 고정 카드의 카드 영역 (클라이언트 좌표)
	bool GetCardRect(int row, CRect& rc) const;

	// 카드 위쪽 머리 영역 (목록 도구줄 자리): 카드는 이 높이 아래부터, 함께 스크롤됨
	//  - 머리 영역에 둔 자식 컨트롤(도구줄)은 스크롤할 때 같이 움직이고, 알림(WM_COMMAND · 그리기 · 색)은 부모 창으로 넘김
	void SetHeaderHeight(int h);
	int  HeaderHeight() const { return m_headerH; }
	int  ScrollY() const { return m_scroll; }
	void ScrollToTop() { if (GetSafeHwnd()) SetScroll(0); }   // 맨 위로 (탭 전환 · 카드로 들어갈 때)
	// 세로 스크롤바 숨김 (머리 영역(도구줄)만 쓸 때 - 영상 탭 [리스트] 보기)
	void SetScrollHidden(bool hidden) { if (m_scrollHidden == hidden) return; m_scrollHidden = hidden; if (GetSafeHwnd()) { UpdateLayout(); Invalidate(FALSE); } }

	// 줄 배치 (리스트 보기): 비우면 보통 바둑판, 주면 줄마다 새 화면 줄에서 시작 (Refresh 전에 호출)
	void SetLines(const std::vector<GridLine>& lines);
	bool LineMode() const { return !m_lines.empty(); }
	// 묶음 머리줄 높이 (줄 배치에서 header 인 줄 위) - 0 = 없음
	void SetGroupHeaderHeight(int h);
	// 고정 카드 폭을 격자 폭에 맞춤 (한 줄에 카드 하나 - 표 형태 보기), 높이는 SetFixedTile 값
	void SetFillWidth(bool fill);

protected:
	bool m_scrollHidden = false;
	IVideoGridOwner* m_owner = nullptr;
	CImageList* m_images = nullptr;
	int  m_thumbW = 160;
	int  m_thumbH = 120;
	bool m_showImage = true;
	COLORREF m_bg      = RGB(255, 255, 255);
	COLORREF m_card    = RGB(255, 255, 255);
	COLORREF m_border  = RGB(225, 225, 225);
	COLORREF m_selFill = RGB(204, 232, 255);
	COLORREF m_selFillNoFocus = RGB(229, 243, 255);
	COLORREF m_text    = RGB(0, 0, 0);
	COLORREF m_accent  = RGB(0, 102, 204);
	COLORREF m_sub     = RGB(110, 110, 110);
	int  ImageAreaH() const { return m_showImage ? m_thumbH + m_pad / 2 : 0; }
	int  m_pad = 8;
	int  m_basePad = 8;      // 기본 여백 (글꼴 기준)
	int  m_padOverride = -1; // SetTilePad (-1 = 기본)
	int  m_lineH = 16;
	int  m_cols = 1;
	int  m_cellW = 176;
	int  m_tileH = 220;
	int  m_scroll = 0;
	int  m_headerH = 0;      // 카드 위 머리 영역 높이 (도구줄)
	int  m_fixedW = 0;       // 고정 카드 크기 (배우 카드)
	int  m_fixedH = 0;
	int  m_offsetX = 0;      // 고정 카드일 때 가운데 정렬 여백
	// 줄 배치 (리스트 보기)
	std::vector<GridLine> m_lines;
	std::vector<POINT> m_pos;        // 카드 → (x = 칸, y = 화면 줄)
	std::vector<int>   m_cellItem;   // 화면 줄 * m_cols + 칸 → 카드 (-1: 빈 칸)
	int  m_lineRows = 0;             // 줄 배치의 화면 줄 수
	int  m_lineCols = 0;             // 줄 배치의 가장 긴 줄 칸 수 (화면보다 길면 좌우 스크롤)
	int  m_scrollX = 0;              // 줄 배치 가로 스크롤 위치
	// 묶음 머리줄 (줄 배치 + header): 화면 줄마다 높이가 달라짐
	int  m_groupH = 0;
	bool m_fillWidth = false;
	std::vector<int>  m_bandTop;     // 화면 줄 r 의 띠(머리줄 포함) 위쪽 (내용 좌표, 머리 영역 제외) - 크기 = 줄 수 + 1 (마지막 = 전체 높이)
	std::vector<int>  m_rowHdr;      // 화면 줄 r 위 머리줄 높이 (0 = 없음)
	std::vector<int>  m_rowHdrCard;  // 그 머리줄의 첫 카드
	bool Variable() const;           // 화면 줄 높이가 달라지는 배치인지
	int  RowTop(int r) const;        // 화면 줄 r 카드 위쪽 (내용 좌표)
	int  BandTop(int r) const;       // 화면 줄 r 띠 위쪽 (머리줄 포함)
	int  ContentH(int rows) const;   // 카드 전체 높이
	int  BandAt(int y) const;        // 내용 좌표 y 의 화면 줄 (-1 = 위쪽 밖)
	int  Cols(int count) const { return LineReady(count) ? m_lineCols : m_cols; }
	void SetScrollX(int pos);
	void UpdateHorzBar();
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	void BuildLinePositions(int count);
	bool LineReady(int count) const { return !m_lines.empty() && static_cast<int>(m_pos.size()) == count; }
	int  RowCount(int count) const;                 // 화면 줄 수
	bool PosOf(int row, int& r, int& c) const;      // 카드의 화면 줄 / 칸
	int  ItemAt(int r, int c) const;                // 화면 줄 / 칸의 카드 (-1: 없음)
	void ItemRangeOfRows(int r0, int r1, int& first, int& last) const;   // 화면 줄 [r0, r1] 의 카드 범위
	void MoveVert(int rows);                        // 위 · 아래 줄로 (같은 칸 근처)
	CFont m_fontBold;
	int  m_hot = -1;          // 마우스가 올라간 카드
	int  m_hotPart = 0;       // 마우스가 올라간 카드 위 버튼
	int  m_hotZone = 0;
	void SetHot(int row, CPoint pt);
	bool m_tracking = false;
	void UpdateHot();         // 커서 위치로 m_hot 다시 계산
	CToolTipCtrl m_tip;       // GridTipText 풍선 도움말
	CString m_tipText;        // 지금 풍선 도움말 글자
	void UpdateTip(int row, CPoint pt);
	virtual BOOL PreTranslateMessage(MSG* pMsg);

	void UpdateLayout();
	void SetScroll(int pos);
	int  VisibleRows() const;
	void MoveSel(int delta);

	afx_msg int  OnCreate(LPCREATESTRUCT lpcs);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg UINT OnGetDlgCode();
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	afx_msg LRESULT OnDarkScrollSetPos(WPARAM wParam, LPARAM lParam);
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);   // 도구줄 자식 컨트롤 알림 → 부모 창
	DECLARE_MESSAGE_MAP()
};
