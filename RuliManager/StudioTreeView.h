#pragma once

#include <functional>
#include <vector>
#include <set>

// 제작사 탭 [계층] 보기: 상위 · 하위 제작사를 위 → 아래 트리로 (왼쪽 정렬)
//   [상위]
//     ├────────┐
//   [하위 A]  [하위 B]
//  - 묶음(맨 위 제작사 하나의 트리)마다 세로로 차례로, 상위 노드는 첫째 하위 바로 위 (왼쪽 맞춤), 형제는 가로로 나란히
//  - 상위 · 하위가 없는 제작사는 그 아래 구분선 밑에 격자로 (창 폭만큼 한 줄에)
//  - 노드(카드)는 주인이 그림 (m_onDrawNode), 노드 사이 꺾은 연결선은 이 창이 그림
//  - 하위가 있는 노드 아래 가운데 동그라미 (− / +) = 접기 · 펼치기 (접은 상태는 이름으로 기억, 접으면 옆에 하위 수)
//  - 클릭 = 선택, 더블클릭 · Enter = 열기, 오른쪽 클릭 = 메뉴, ←→ = 읽는 순서 앞 · 뒤, ↑ = 상위로, ↓ = 첫 하위로 (접혀 있으면 펼치기)
//  - 가로 · 세로 스크롤바 (주인이 직접 그린 스크롤바를 겹쳐 놓음, 휠 = 세로, Shift + 휠 = 가로)
class CStudioTreeView : public CWnd
{
public:
	struct Item
	{
		int     row = -1;      // 주인의 행 번호 (m_catRows)
		CString name;          // 제작사 이름 (접기 상태 기억용)
		int     parent = -1;   // 이 목록 안의 상위 항목 번호 (-1 = 맨 왼쪽)
	};

	std::function<void(CDC*, int row, const CRect&, bool selected, bool hot)> m_onDrawNode;
	std::function<void(int row)> m_onSelect;                  // 클릭
	std::function<void(int row)> m_onActivate;                // 더블클릭 · Enter
	std::function<void(int row, CPoint screenPt)> m_onContext;   // 오른쪽 클릭 (row = -1 이면 빈곳)
	std::function<void(UINT vk)> m_onKey;                     // 그 밖의 키 (F2 · Del · F5 등 - 격자와 같게)

	bool Create(CWnd* parent, UINT id);
	void SetColors(COLORREF back, COLORREF line, COLORREF text);
	void SetNodeSize(int w, int h);
	void SetMetrics(int toggleR, int lineW, int hGap, int vGap, int margin);
	void SetColumnGap(int gap);   // 카드 사이 좌우 간격 (트리 형제 · 아래 격자 모두, -1 = vGap 사용) - 제작사 [격자] 보기와 맞춤   // 접기 단추 반지름 · 연결선 굵기 · 단계 사이 · 노드 사이 · 바깥 여백 (고정, 픽셀)
	void SetItems(const std::vector<Item>& items);   // 표시 순서대로 (형제끼리 순서 = 이 순서)
	void SetSelectedRow(int row, bool ensureVisible = true);
	void SetEmptyText(const CString& text) { m_emptyText = text; }
	void ScrollToStart();
	void InvalidateRow(int row);

protected:
	struct Node
	{
		int row = -1;
		CString name;
		int parent = -1;
		std::vector<int> kids;
		int depth = 0;
		CRect rc;          // 내용 좌표
		bool visible = false;
	};
	std::vector<Node> m_nodes;
	std::vector<int>  m_order;          // 보이는 노드 (위 → 아래, 깊이 우선)
	std::set<CString> m_collapsed;      // 접은 제작사 (소문자 이름)
	int  m_nodeW = 180, m_nodeH = 48;
	int  m_hGap = 48, m_vGap = 10, m_margin = 12;
	bool m_fixedMetrics = false;        // SetMetrics 로 정한 값 사용 (노드 크기에 따라 바뀌지 않음)
	int  m_toggleR = 10, m_lineW = 2;
	int  m_colGap = -1;                 // 카드 사이 좌우 간격 (-1 = m_vGap)
	int  ColGap() const { return m_colGap >= 0 ? m_colGap : m_vGap; }
	int  m_contentW = 0, m_contentH = 0;
	int  m_sepY = -1;                   // 트리 · 아래 격자 구분선 (내용 좌표, -1 = 없음)
	int  m_gridCols = 0;                // 아래 격자 한 줄 칸 수 (0 = 격자 없음)
	bool m_inLayout = false;
	int  GridColumns() const;
	int  m_scrollX = 0, m_scrollY = 0;
	int  m_sel = -1;                    // 선택 노드 (m_nodes 번호)
	int  m_hot = -1, m_hotToggle = -1;
	bool m_tracking = false;
	COLORREF m_back = RGB(32, 43, 51), m_line = RGB(92, 112, 128), m_text = RGB(232, 238, 242);
	CString m_emptyText;
	CFont   m_countFont;                // 접힌 노드 옆 하위 수 (기본 글꼴 1.5배 굵게)

	bool IsCollapsed(int n) const;
	void Relayout();
	void UpdateScrollBars();
	void ScrollTo(int x, int y);
	void EnsureVisibleNode(int n);
	CRect ToggleRect(int n) const;      // 내용 좌표
	int  NodeAt(CPoint client) const;
	int  ToggleAt(CPoint client) const;
	void Toggle(int n);
	void SelectNode(int n, bool notify);
	int  NodeOfRow(int row) const;

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnMouseHWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg UINT OnGetDlgCode();
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
	afx_msg LRESULT OnDarkScrollSetPos(WPARAM wParam, LPARAM lParam);
	DECLARE_MESSAGE_MAP()
};
