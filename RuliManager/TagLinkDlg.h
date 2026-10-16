#pragma once

#include <vector>
#include "DarkTheme.h"

// ---------------------------------------------------------------------------
// 태그 연결 창 (웹페이지 내용 붙여넣기 → 읽어낸 태그 중 현재 영상 태그에 없는 것이 있을 때)
//   [붙여넣은 태그]              [현재 영상 태그]
//   [String1] ───────────────▶ [문자열1]
//   [String2] ───────────────▶ [문자열2]
//   [String5]                   [문자열6]   (왼쪽 연결 안 됨: 누르면 새 태그 / 무시 전환)
//  - 칩을 반대쪽 칩으로 끌어다 놓으면 연결 (붙여넣은 태그 하나 = 기존 태그 하나, 기존 태그 하나에 여러 개 연결 가능)
//  - 왼쪽(붙여넣은) 칩 오른쪽 클릭 / 화살표 클릭 = 연결 끊기, 연결 안 된 왼쪽 칩 클릭 = 새 태그로 추가 ↔ 무시
//  - 내부 이름: m_left / LeftChipRect = 기존 태그 (화면 오른쪽 열), m_web / RightChipRect = 붙여넣은 태그 (화면 왼쪽 열)
//  - 기존 태그 = 현재 영상에 설정된 태그 (DB 전체가 아님). 같은 태그(이름 · 다른 이름 · 영어 · 일본어 이름)면 처음부터 연결
//  - 연결 안 된 붙여넣은 태그(왼쪽): 처음에는 무조건 "추가" (연결을 끊어도 "추가"), 누르면 "무시" 와 전환
//  - 연결 안 된 현재 영상 태그(오른쪽): 처음에는 무조건 "지움" (취소선), 누르면 "유지" 와 전환 / 연결된 것은 유지
//  - 확인: 영상 태그 = 현재 태그 + 추가로 고른 것 (병합), 연결한 웹 이름은 그 태그의 "다른 이름"에 추가 (다음부터 자동 연결)
class CTagLinkView : public CWnd
{
public:
	struct WebTag
	{
		CString text;        // 붙여넣은 태그 이름
		int     link = -1;   // 연결된 기존 태그 (m_all 번호, -1 = 연결 안 됨)
		bool    addNew = true;    // 연결 안 됨: true = 영상에 추가 (기본), false = 무시
	};

	std::vector<CString> m_all;      // 기존 태그 = 현재 영상 태그 (영상에 적힌 순서)
	std::vector<WebTag>  m_web;      // 붙여넣은 태그
	CString m_headCur = L"현재 영상 태그";   // 오른쪽 열 머리글 (제작사 · 레이블 연결 창은 바꿔 씀)
	CString m_headWeb = L"붙여넣은 태그";   // 왼쪽 열 머리글
	std::vector<bool>    m_keep;     // 현재 영상 태그(m_all)마다: 연결 안 됐을 때 유지 (기본 false = 지움, 누르면 전환)

	bool Create(CWnd* parent, const CRect& rc, UINT id);
	void SetFilter(const CString& text);   // 왼쪽 목록 검색 (연결된 태그는 항상 표시)

protected:
	std::vector<int> m_left;         // 지금 보이는 왼쪽 목록 (m_all 번호)
	CString m_filter;
	int  m_scrollL = 0, m_scrollR = 0;   // 열마다 세로 스크롤
	int  m_chipH = 24, m_gap = 6, m_pad = 10, m_headH = 24;
	// 끌기
	int  m_dragSide = 0;             // 0 = 없음, -1 = 왼쪽 칩에서, 1 = 오른쪽 칩에서
	int  m_dragIndex = -1;           // 왼쪽: m_all 번호, 오른쪽: m_web 번호
	CPoint m_dragStart, m_dragPt;
	bool m_dragging = false;         // 몇 픽셀 이상 움직였음 (아니면 클릭)
	int  m_hotSide = 0, m_hotIndex = -1;
	bool m_tracking = false;

	void   Rebuild();                // m_left 다시 만들기
	CRect  ColumnRect(int side) const;            // 칩이 놓이는 열 영역 (머리글 아래)
	CRect  LeftChipRect(int pos) const;           // m_left[pos] 칩 (스크롤 반영)
	CRect  RightChipRect(int i) const;            // m_web[i] 칩
	int    LeftAt(CPoint pt) const;               // m_all 번호 (-1)
	int    RightAt(CPoint pt) const;              // m_web 번호 (-1)
	int    LeftPosOf(int tagIndex) const;         // m_left 안 위치 (-1)
	bool   ArrowEnds(int webIndex, CPoint& a, CPoint& b) const;   // 연결 화살표 양 끝 (왼쪽 칩이 안 보이면 열 위 / 아래 끝)
	int    ArrowAt(CPoint pt) const;              // 화살표 근처면 m_web 번호
	int    ContentH(size_t count) const;
	void   ClampScroll();
	void   Link(int tagIndex, int webIndex);
	CFont* Font();

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
	afx_msg void OnCaptureChanged(CWnd* pWnd);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	DECLARE_MESSAGE_MAP()
};

class CTagLinkDlg : public CDialogEx
{
public:
	// curTags: 현재 영상 태그, webTags: 붙여넣은 태그, autoLink[i]: 처음 연결할 현재 태그 (빈 문자열 = 연결 안 됨), addDefault[i]: 연결 안 됐을 때 처음부터 추가
	CTagLinkDlg(const std::vector<CString>& curTags, const std::vector<CString>& webTags, const std::vector<CString>& autoLink, const std::vector<bool>& addDefault, CWnd* pParent = nullptr);

	// 확인 후 결과: 붙여넣은 태그마다 (연결된 현재 태그 이름 - 없으면 빈 문자열, 새 태그로 추가)

	struct Result { CString web; CString linked; bool addNew = false; };
	std::vector<Result> m_result;
	std::vector<CString> m_keepTags;   // 남길 현재 영상 태그 (연결된 것 + 유지로 바꾼 것), 나머지 현재 태그는 지움
	// 다른 용도(제작사 · 레이블 연결)로 쓸 때 바꾸는 글자 (DoModal 전에, 비우면 기본 글자)
	CString m_caption, m_guide, m_headCur, m_headWeb;
	std::vector<bool> m_keepDefault;   // 현재 항목마다 처음 "유지" 상태 (비우면 모두 지움)

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();

	CTagLinkView m_view;
	CDarkDialogTheme m_theme;
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()
};
