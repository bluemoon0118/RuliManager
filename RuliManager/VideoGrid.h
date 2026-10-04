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
};

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
	int  HotRow() const { return m_hot; }   // 마우스가 올라가 있는 카드 (-1: 없음)
	int  HotPart() const { return m_hotPart; }   // 그 카드에서 마우스가 올라간 버튼 (GridHitPart 값)

	// 고정 카드의 카드 영역 (클라이언트 좌표)
	bool GetCardRect(int row, CRect& rc) const;

protected:
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
	int  m_lineH = 16;
	int  m_cols = 1;
	int  m_cellW = 176;
	int  m_tileH = 220;
	int  m_scroll = 0;
	int  m_fixedW = 0;       // 고정 카드 크기 (배우 카드)
	int  m_fixedH = 0;
	int  m_offsetX = 0;      // 고정 카드일 때 가운데 정렬 여백
	CFont m_fontBold;
	int  m_hot = -1;          // 마우스가 올라간 카드
	int  m_hotPart = 0;       // 마우스가 올라간 카드 위 버튼
	void SetHot(int row, CPoint pt);
	bool m_tracking = false;
	void UpdateHot();         // 커서 위치로 m_hot 다시 계산

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
	DECLARE_MESSAGE_MAP()
};
