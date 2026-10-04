#pragma once

#include <vector>
#include <memory>
#include "DarkTheme.h"

// 검색 결과 썸네일 격자 (5 x 2): 클릭 = 선택, 더블클릭 = 확인
class CThumbPickGrid : public CWnd
{
public:
	struct Item { std::shared_ptr<CImage> thumb; CString link; CString thumbLink; CString info; };   // info: "1280×720 · JPG"   // CImage 는 복사 불가 → 포인터 (결과 목록과 공유)
	std::vector<Item> m_items;
	int  m_sel = -1;
	CString m_placeholder;

	bool Create(CWnd* parent, const CRect& rc, UINT id);
	void Reset(const CString& placeholder);

protected:
	int  m_hover = -1;
	bool m_tracking = false;
	CRect CellRect(int i) const;
	static int InfoHeight(CDC& dc);   // 칸 아래 정보 줄 높이
	int  HitTest(CPoint pt) const;

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
	DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------------
// 사진 검색 창: API 키 없이 검색 결과 페이지를 읽어서 10개씩 보여 주고 하나를 고름 (비공식 방식)
//  - 검색 엔진: Bing / Yandex (콤보), 세이프서치 끄기 (체크), [◀ 이전] [다음 10개 ▶]
//  - 엔진 페이지 구조가 바뀌면 해당 Fetch 함수의 파싱을 고쳐야 할 수 있음
class CImageSearchDlg : public CDialogEx
{
public:
	CImageSearchDlg(const CString& query, CWnd* pParent = nullptr);
	CString m_resultPath;   // 고른 이미지를 받아 둔 임시 파일 경로

	enum Engine { ENGINE_BING = 0, ENGINE_YANDEX };   // 콤보 순서와 같음

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();

	struct Result { CString link; CString thumbLink; std::shared_ptr<CImage> thumb; bool loaded = false; int w = 0, h = 0; CString ext; };

	CString m_query;
	CThumbPickGrid m_grid;
	CDarkDialogTheme m_theme;

	// 현재 검색 상태
	CString m_curQuery;
	int     m_curEngine = ENGINE_BING;
	bool    m_safeOff = false;
	std::vector<Result> m_results;   // 지금까지 받은 결과 전체
	int     m_offset = 0;            // 화면에 보이는 첫 결과
	int     m_page = 0;              // 엔진에서 받은 페이지 수
	bool    m_exhausted = false;     // 더 받을 결과 없음

	CString TempDir() const;
	void DoSearch();
	bool FetchMore(CString& err);    // 다음 페이지 결과를 m_results 에 덧붙임 (새 결과가 있으면 true)
	bool FetchBing(CString& err);
	bool FetchYandex(CString& err);
	void AddResult(const CString& link, const CString& thumb, int w = 0, int h = 0, const CString& ext = CString());
	static CString InfoText(const Result& r);   // "1280×720 · JPG" (크기를 모르면 확장자만)
	void ShowPage();                 // m_offset 부터 10개 표시 (썸네일은 처음 볼 때 받음)
	void SetStatus(const CString& s);
	void UpdateNavButtons();

	afx_msg void OnBnClickedSearch();
	afx_msg void OnBnClickedPrev();
	afx_msg void OnBnClickedNext();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()
};
