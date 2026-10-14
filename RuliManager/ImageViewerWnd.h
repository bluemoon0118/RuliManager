#pragma once

// 원본 이미지 보기 창 (영상 상세 이미지 영역 더블클릭)
//  - 처음에는 원본 크기 (화면보다 크면 화면 작업 영역의 90% 안에 맞춤), 창 크기를 바꾸면 비율 유지 맞춤 (확대하지 않음)
//  - 더블클릭 / 가운데 클릭: 창에 맞춤 ↔ 원본 크기(100%) 전환, 원본 크기에서 드래그 / 휠로 이동
//  - Esc 로 닫음, 창은 하나만 (다른 이미지를 열면 같은 창에 표시)
//  - 여러 장 모드(OpenList, 영상 스크린샷): 좌우 끝 영역에 마우스를 올리면 < / > 표시, 누르면 이전 / 다음 이미지 (← → 키 · 마우스 휠도),
//    처음 / 마지막 이미지에서는 더 넘기지 않음 (그쪽 < / > 도 표시 안 함), 이 모드에서는 더블클릭 / 가운데 클릭 / Space 확대 전환 없음 (항상 창에 맞춤)
class CImageViewerWnd : public CWnd
{
public:
	bool Open(CWnd* owner, const CString& path, bool sfw, const CString& caption);   // sfw: SFW 옵션이 켜져 있으면 모자이크
	bool OpenList(CWnd* owner, const std::vector<CString>& files, int index, bool sfw);   // 여러 장 (제목 = 파일 이름 (번호 / 전체))

protected:
	CImage  m_image;
	CString m_caption;
	bool    m_fit = true;      // true = 창에 맞춤, false = 원본 크기 (드래그로 이동)
	// 여러 장 모드
	std::vector<CString> m_files;
	int     m_index = 0;
	bool    m_sfw = false;
	int     m_navHover = 0;    // 마우스가 올라간 쪽: -1 = 왼쪽(<), 1 = 오른쪽(>), 0 = 없음
	bool    m_trackLeave = false;
	bool    m_listMode = false;     // OpenList 로 열었음 (한 장이어도 확대 전환 없음)
	bool    m_listOpening = false;
	bool    Gallery() const { return m_files.size() > 1; }   // 넘길 이미지가 있음 (< / >)
	int     NavZoneAt(CPoint pt) const;   // -1 / 1 / 0
	CRect   NavZoneRect(int side) const;
	void    ShowIndex(int index);         // 창 크기는 그대로, 그 이미지를 창에 맞춤
	bool    LoadCurrent();
	afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
	CPoint  m_offset;          // 원본 크기일 때 이미지 왼쪽 위의 화면 위치 (클라이언트 좌표, 이미지가 작으면 가운데)
	bool    m_dragging = false;
	CPoint  m_dragStart;
	CPoint  m_dragOffset;

	void   UpdateTitle();
	void   ClampOffset();
	CRect  ImageRect() const;  // 지금 이미지를 그리는 영역 (클라이언트 좌표)

	virtual void PostNcDestroy() override {}   // 대화상자 멤버 (delete this 안 함)
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnMButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	DECLARE_MESSAGE_MAP()
};
