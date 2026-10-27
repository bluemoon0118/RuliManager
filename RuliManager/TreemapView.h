#pragma once

#include <vector>
#include <functional>
#include <map>
#include <memory>

// ---------------------------------------------------------------------------
// 트리맵 차트 (배우 탭 [트리맵] 보기)
//  - 항목 값에 비례한 넓이의 사각형으로 화면을 나눔 (squarified - 정사각형에 가깝게)
//  - 사각형 안: 이름 (굵게) / 둘째 · 셋째 이름 / 값 (m_formatValue) / 전체 대비 퍼센트 - 칸이 모자라면 이름 하나만 → 값 빼고 → 퍼센트만 → 글자 없음
//  - 마우스를 올리면 밝게 + 풍선 도움말 (이름 · 값 · 퍼센트), 클릭 = m_onClick, 더블클릭 = m_onActivate
class CTreemapView : public CWnd
{
public:
	struct Item
	{
		CString label;        // 첫 줄 이름 (굵게) - 예: 한글 이름
		CString sub1, sub2;   // 둘째 · 셋째 줄 이름 (예: 영어 · 일본어 이름, 비면 없음) - 칸이 모자라면 빼고 첫 줄 이름만
		double  value = 0;    // 0 이하는 그리지 않음
		int     tag = -1;     // 주인이 쓰는 번호 (예: 배우 번호, -1 = 기타 묶음)
	};

	BOOL Create(CWnd* parent, UINT id);
	// items: 값 큰 순서가 아니어도 됨 (안에서 정렬), total: 퍼센트 기준 (0 이하면 items 합)
	void SetItems(std::vector<Item> items, double total);
	void SetSelectedTag(int tag);
	void SetColors(COLORREF back, COLORREF text);
	void SetEmptyText(const CString& text) { m_emptyText = text; if (GetSafeHwnd()) Invalidate(FALSE); }
	void SetFonts(CFont* normal, CFont* bold) { m_font = normal; m_boldFont = bold; if (GetSafeHwnd()) Invalidate(FALSE); }

	std::function<void(int tag)> m_onClick;               // 사각형 클릭
	std::function<void(int tag)> m_onActivate;            // 사각형 더블클릭
	std::function<void(int tag, CPoint screen)> m_onContext;   // 오른쪽 클릭 (화면 좌표)
	std::function<CString(double)> m_formatValue;         // 칸 안 · 풍선 도움말의 값 글자 (없으면 칸에는 값 줄 없음, 도움말은 숫자)

protected:
	std::vector<Item>  m_items;     // 값 큰 순서
	std::vector<CRect> m_rects;     // m_items 와 같은 순서 (클라이언트 좌표)
	double   m_total = 0;
	int      m_hot = -1;
	int      m_selTag = -2;        // -2 = 선택 없음
	COLORREF m_back = RGB(30, 30, 30);
	COLORREF m_text = RGB(255, 255, 255);
	CString  m_emptyText;
	CFont*   m_font = nullptr;
	CFont*   m_boldFont = nullptr;
	CFont    m_ownFont, m_ownBold;   // 받은 글꼴이 없을 때 (대화상자 글꼴 + 1pt)
	std::map<int, std::unique_ptr<CFont>> m_sizedFonts;   // 칸 크기에 맞춘 글꼴 (키 = 글자 높이 × 2 + 굵게)
	HFONT    m_sizedBase = nullptr;                       // m_sizedFonts 를 만든 기본 글꼴
	static constexpr int kMaxPx = 64;                     // 이름 글자 최대 높이 (픽셀)
	static int BasePx(CFont* base);                       // 기본 글꼴의 글자 높이 (픽셀)
	CFont* SizedFont(CFont* base, int px, bool bold);
	CToolTipCtrl m_tip;
	CString  m_tipText;
	bool     m_tracking = false;

	void Relayout();
	int  HitTest(CPoint pt) const;
	CString TipFor(int i) const;
	static COLORREF ColorFor(int rank, int tag);

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	DECLARE_MESSAGE_MAP()
};
