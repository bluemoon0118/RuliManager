#include "pch.h"
#include "StudioTreeView.h"
#include "TextDraw.h"
#include "DarkControls.h"   // WM_DARKSCROLL_SETPOS (직접 그린 스크롤바)
#include <algorithm>
#include <functional>
#include <climits>

namespace
{
	void EnsureGdiPlusTree()
	{
		static ULONG_PTR token = 0;
		if (!token)
		{
			Gdiplus::GdiplusStartupInput input;
			Gdiplus::GdiplusStartup(&token, &input, nullptr);
		}
	}
}

BEGIN_MESSAGE_MAP(CStudioTreeView, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_HSCROLL()
	ON_WM_VSCROLL()
	ON_WM_MOUSEWHEEL()
	ON_WM_MOUSEHWHEEL()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_RBUTTONDOWN()
	ON_WM_CONTEXTMENU()
	ON_WM_KEYDOWN()
	ON_WM_GETDLGCODE()
	ON_WM_MOUSEMOVE()
	ON_MESSAGE(WM_MOUSELEAVE, &CStudioTreeView::OnMouseLeave)
	ON_MESSAGE(WM_DARKSCROLL_SETPOS, &CStudioTreeView::OnDarkScrollSetPos)
END_MESSAGE_MAP()

bool CStudioTreeView::Create(CWnd* parent, UINT id)
{
	const CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), nullptr);
	if (!CWnd::CreateEx(0, cls, L"", WS_CHILD | WS_HSCROLL | WS_VSCROLL | WS_CLIPSIBLINGS | WS_TABSTOP, CRect(0, 0, 10, 10), parent, id))
		return false;
	return true;   // 스크롤바는 주인이 다른 목록과 같은 직접 그린 스크롤바(CDarkScrollBar)를 겹쳐 놓음
}

void CStudioTreeView::SetColors(COLORREF back, COLORREF line, COLORREF text)
{
	m_back = back;
	m_line = line;
	m_text = text;
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

void CStudioTreeView::SetNodeSize(int w, int h)
{
	w = (std::max)(60, w);
	h = (std::max)(24, h);
	if (w == m_nodeW && h == m_nodeH)
		return;
	m_nodeW = w;
	m_nodeH = h;
	if (!m_fixedMetrics)
	{
		m_hGap = (std::max)(24, w / 4);     // 단계 사이 (연결선 자리)
		m_vGap = (std::max)(6, h / 5);      // 노드 사이
		m_margin = (std::max)(8, h / 4);
	}
	Relayout();
}

void CStudioTreeView::SetItems(const std::vector<Item>& items)
{
	const int selRow = (m_sel >= 0 && m_sel < static_cast<int>(m_nodes.size())) ? m_nodes[m_sel].row : -1;
	m_nodes.clear();
	m_nodes.resize(items.size());
	for (size_t i = 0; i < items.size(); ++i)
	{
		m_nodes[i].row = items[i].row;
		m_nodes[i].name = items[i].name;
		const int p = items[i].parent;
		m_nodes[i].parent = (p >= 0 && p < static_cast<int>(items.size()) && p != static_cast<int>(i)) ? p : -1;
	}
	for (size_t i = 0; i < m_nodes.size(); ++i)
		if (m_nodes[i].parent >= 0)
			m_nodes[m_nodes[i].parent].kids.push_back(static_cast<int>(i));
	m_sel = -1;
	m_hot = m_hotToggle = -1;
	Relayout();
	if (selRow >= 0)
		SetSelectedRow(selRow, false);
}

bool CStudioTreeView::IsCollapsed(int n) const
{
	CString k = m_nodes[n].name;
	k.MakeLower();
	return m_collapsed.count(k) > 0;
}

void CStudioTreeView::Relayout()
{
	m_order.clear();
	for (Node& nd : m_nodes)
	{
		nd.visible = false;
		nd.depth = 0;
		nd.rc.SetRectEmpty();
	}
	// 위 → 아래 트리 (왼쪽 정렬): 단계마다 한 줄 아래, 형제는 가로로 나란히, 상위 노드는 첫째 하위 위 (왼쪽 맞춤)
	//  묶음(맨 위 제작사 하나의 트리)은 세로로 차례로 쌓음
	const int colW = m_nodeW + ColGap();  // 형제 사이 (가로 - 격자 보기 카드 간격과 같게)
	const int rowH = m_nodeH + m_hGap;     // 단계 사이 (세로 - 접기 단추 + 연결선 자리)
	int nextY = m_margin;
	int maxRight = 0;
	std::vector<bool> seen(m_nodes.size(), false);
	int nextX = m_margin, groupTop = m_margin, groupBottom = m_margin;

	std::function<int(int, int)> place = [&](int n, int depth) -> int
	{
		seen[n] = true;   // 순환 연결 방지
		Node& nd = m_nodes[n];
		nd.depth = depth;
		nd.visible = true;
		m_order.push_back(n);
		std::vector<int> kids;
		if (!IsCollapsed(n))
			for (int k : nd.kids)
				if (!seen[k]) kids.push_back(k);
		int x;
		if (kids.empty())
		{
			x = nextX;
			nextX += colW;
		}
		else
		{
			x = INT_MAX;
			for (int k : kids)
				x = (std::min)(x, place(k, depth + 1));   // 첫째 하위의 왼쪽
		}
		const int y = groupTop + depth * rowH;
		m_nodes[n].rc.SetRect(x, y, x + m_nodeW, y + m_nodeH);
		maxRight = (std::max)(maxRight, x + m_nodeW);
		groupBottom = (std::max)(groupBottom, y + m_nodeH);
		return x;
	};
	auto placeGroup = [&](int root)
	{
		nextX = m_margin;
		groupTop = nextY;
		groupBottom = nextY;
		place(root, 0);
		nextY = groupBottom + m_hGap;   // 다음 묶음은 아래로
	};

	// 하위가 있는 묶음 먼저, 그다음 순환 연결로 닿지 않은 노드
	bool anyGroup = false;
	for (size_t i = 0; i < m_nodes.size(); ++i)
		if (m_nodes[i].parent < 0 && !m_nodes[i].kids.empty() && !seen[i])
		{
			placeGroup(static_cast<int>(i));
			anyGroup = true;
		}
	for (size_t i = 0; i < m_nodes.size(); ++i)
		if (!seen[i] && !(m_nodes[i].parent < 0 && m_nodes[i].kids.empty()))
		{
			placeGroup(static_cast<int>(i));
			anyGroup = true;
		}
	const int rowGrid = m_nodeH + m_vGap;   // 아래 격자 줄 높이

	// 상위 · 하위가 없는 제작사: 트리 아래에 격자로 (창 폭에 들어가는 만큼 한 줄에, 구분선 아래)
	m_sepY = -1;
	std::vector<int> singles;
	for (size_t i = 0; i < m_nodes.size(); ++i)
		if (!seen[i] && m_nodes[i].parent < 0 && m_nodes[i].kids.empty())
			singles.push_back(static_cast<int>(i));
	if (!singles.empty())
	{
		if (anyGroup)
		{
			nextY += m_vGap;
			m_sepY = nextY - m_hGap / 2 - m_vGap / 2;   // 트리와 격자 사이 구분선
		}
		const int cols = GridColumns();
		const int cellW = m_nodeW + ColGap();   // 격자 보기 카드 간격과 같게
		for (size_t k = 0; k < singles.size(); ++k)
		{
			const int n = singles[k];
			const int col = static_cast<int>(k) % cols, row = static_cast<int>(k) / cols;
			const int x = m_margin + col * cellW, y = nextY + row * rowGrid;
			Node& nd = m_nodes[n];
			nd.depth = 0;
			nd.visible = true;
			nd.rc.SetRect(x, y, x + m_nodeW, y + m_nodeH);
			seen[n] = true;
			m_order.push_back(n);
			maxRight = (std::max)(maxRight, x + m_nodeW);
		}
		nextY += ((static_cast<int>(singles.size()) + cols - 1) / cols) * rowGrid;
		m_gridCols = cols;
	}
	else
		m_gridCols = 0;

	m_contentW = maxRight + m_margin;
	m_contentH = nextY + m_margin;
	UpdateScrollBars();
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

void CStudioTreeView::UpdateScrollBars()
{
	if (!GetSafeHwnd())
		return;
	CRect rc;
	GetClientRect(&rc);
	m_scrollX = (std::max)(0, (std::min)(m_scrollX, m_contentW - static_cast<int>(rc.Width())));
	m_scrollY = (std::max)(0, (std::min)(m_scrollY, m_contentH - static_cast<int>(rc.Height())));
	SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS };
	si.nMin = 0;
	si.nMax = (std::max)(0, m_contentW - 1);
	si.nPage = static_cast<UINT>((std::max)(1, static_cast<int>(rc.Width())));
	si.nPos = m_scrollX;
	SetScrollInfo(SB_HORZ, &si, TRUE);
	si.nMax = (std::max)(0, m_contentH - 1);
	si.nPage = static_cast<UINT>((std::max)(1, static_cast<int>(rc.Height())));
	si.nPos = m_scrollY;
	SetScrollInfo(SB_VERT, &si, TRUE);
}

void CStudioTreeView::ScrollTo(int x, int y)
{
	CRect rc;
	GetClientRect(&rc);
	x = (std::max)(0, (std::min)(x, m_contentW - static_cast<int>(rc.Width())));
	y = (std::max)(0, (std::min)(y, m_contentH - static_cast<int>(rc.Height())));
	if (x == m_scrollX && y == m_scrollY)
		return;
	m_scrollX = x;
	m_scrollY = y;
	SetScrollPos(SB_HORZ, m_scrollX, TRUE);
	SetScrollPos(SB_VERT, m_scrollY, TRUE);
	Invalidate(FALSE);
}

void CStudioTreeView::ScrollToStart()
{
	ScrollTo(0, 0);
}

void CStudioTreeView::EnsureVisibleNode(int n)
{
	if (n < 0 || n >= static_cast<int>(m_nodes.size()) || !m_nodes[n].visible || !GetSafeHwnd())
		return;
	CRect rc;
	GetClientRect(&rc);
	const CRect& r = m_nodes[n].rc;
	int x = m_scrollX, y = m_scrollY;
	if (r.left - m_margin < x) x = r.left - m_margin;
	else if (r.right + m_margin > x + rc.Width()) x = r.right + m_margin - rc.Width();
	if (r.top - m_margin < y) y = r.top - m_margin;
	else if (r.bottom + m_margin > y + rc.Height()) y = r.bottom + m_margin - rc.Height();
	ScrollTo(x, y);
}

CRect CStudioTreeView::ToggleRect(int n) const
{
	const Node& nd = m_nodes[n];
	if (nd.kids.empty())
		return CRect();
	const int r = m_fixedMetrics ? m_toggleR : (std::max)(6, m_nodeH / 7);
	const int cx = (nd.rc.left + nd.rc.right) / 2, cy = nd.rc.bottom + r + 3;   // 카드 아래 가운데
	return CRect(cx - r, cy - r, cx + r + 1, cy + r + 1);
}

int CStudioTreeView::NodeAt(CPoint client) const
{
	const CPoint p(client.x + m_scrollX, client.y + m_scrollY);
	for (int n : m_order)
		if (m_nodes[n].rc.PtInRect(p))
			return n;
	return -1;
}

int CStudioTreeView::ToggleAt(CPoint client) const
{
	const CPoint p(client.x + m_scrollX, client.y + m_scrollY);
	for (int n : m_order)
	{
		CRect t = ToggleRect(n);
		t.InflateRect(2, 2);
		if (!t.IsRectEmpty() && t.PtInRect(p))
			return n;
	}
	return -1;
}

int CStudioTreeView::NodeOfRow(int row) const
{
	for (size_t i = 0; i < m_nodes.size(); ++i)
		if (m_nodes[i].row == row)
			return static_cast<int>(i);
	return -1;
}

void CStudioTreeView::Toggle(int n)
{
	if (n < 0 || n >= static_cast<int>(m_nodes.size()) || m_nodes[n].kids.empty())
		return;
	CString k = m_nodes[n].name;
	k.MakeLower();
	if (m_collapsed.count(k))
		m_collapsed.erase(k);
	else
		m_collapsed.insert(k);
	// 접으면서 선택이 숨겨지면 접은 노드를 선택
	Relayout();
	if (m_sel >= 0 && !m_nodes[m_sel].visible)
		SelectNode(n, true);
	EnsureVisibleNode(n);
}

void CStudioTreeView::SelectNode(int n, bool notify)
{
	if (n < 0 || n >= static_cast<int>(m_nodes.size()))
		return;
	if (n != m_sel)
	{
		m_sel = n;
		Invalidate(FALSE);
	}
	EnsureVisibleNode(n);
	if (notify && m_onSelect)
		m_onSelect(m_nodes[n].row);
}

void CStudioTreeView::SetSelectedRow(int row, bool ensureVisible)
{
	int n = NodeOfRow(row);
	if (n >= 0 && !m_nodes[n].visible)
	{
		// 접힌 묶음 안: 상위들을 펼쳐서 보이게
		size_t guard = 0;   // 순환 연결 방지
		for (int p = m_nodes[n].parent; p >= 0 && guard < m_nodes.size(); p = m_nodes[p].parent, ++guard)
		{
			CString k = m_nodes[p].name;
			k.MakeLower();
			m_collapsed.erase(k);
		}
		Relayout();
	}
	if (n != m_sel)
	{
		m_sel = n;
		if (GetSafeHwnd())
			Invalidate(FALSE);
	}
	if (ensureVisible)
		EnsureVisibleNode(n);
}

void CStudioTreeView::InvalidateRow(int row)
{
	const int n = NodeOfRow(row);
	if (n < 0 || !m_nodes[n].visible || !GetSafeHwnd())
		return;
	CRect r = m_nodes[n].rc;
	r.OffsetRect(-m_scrollX, -m_scrollY);
	InvalidateRect(r, FALSE);
}

BOOL CStudioTreeView::OnEraseBkgnd(CDC* /*pDC*/)
{
	return TRUE;
}

void CStudioTreeView::SetMetrics(int toggleR, int lineW, int hGap, int vGap, int margin)
{
	// 접기 단추 · 연결선 굵기 · 간격을 노드 크기와 상관없이 고정 (카드가 커져도 그대로)
	m_fixedMetrics = true;
	m_toggleR = (std::max)(4, toggleR);
	m_lineW = (std::max)(1, lineW);
	m_hGap = (std::max)(m_toggleR * 2 + 12, hGap);   // 접기 단추 + 연결선이 들어갈 자리
	m_vGap = (std::max)(2, vGap);
	m_margin = (std::max)(4, margin);
	Relayout();
}

void CStudioTreeView::SetColumnGap(int gap)
{
	if (gap == m_colGap)
		return;
	m_colGap = gap;
	Relayout();
}

int CStudioTreeView::GridColumns() const
{
	// 아래 격자 한 줄 칸 수: 창 폭에 들어가는 만큼 (최소 1)
	CRect client(0, 0, 800, 600);
	if (GetSafeHwnd())
		GetClientRect(&client);
	const int cellW = m_nodeW + ColGap();
	const int avail = static_cast<int>(client.Width()) - m_margin * 2 + ColGap();
	return (std::max)(1, avail / (std::max)(1, cellW));
}

void CStudioTreeView::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	if (m_gridCols > 0 && GridColumns() != m_gridCols && !m_inLayout)
	{
		// 창 폭이 바뀌어 아래 격자 칸 수가 달라짐 → 다시 배치
		m_inLayout = true;
		Relayout();
		m_inLayout = false;
		return;
	}
	UpdateScrollBars();
	Invalidate(FALSE);
}

void CStudioTreeView::OnPaint()
{
	CPaintDC paintDC(this);
	CRect client;
	GetClientRect(&client);
	CDC mem;
	mem.CreateCompatibleDC(&paintDC);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&paintDC, (std::max)(1, client.Width()), (std::max)(1, client.Height()));
	CBitmap* oldBmp = mem.SelectObject(&bmp);
	mem.FillSolidRect(client, m_back);
	CWnd* top = GetParentOwner();   // 대화상자 글꼴
	CFont* oldFont = mem.SelectObject(top ? top->GetFont() : nullptr);
	mem.SetBkMode(TRANSPARENT);

	if (m_order.empty())
	{
		mem.SetTextColor(m_text);
		TextFB::DrawFmt(&mem, m_emptyText, client, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}
	else
	{
		const CRect view(m_scrollX, m_scrollY, m_scrollX + client.Width(), m_scrollY + client.Height());
		auto toClient = [this](CRect r) { r.OffsetRect(-m_scrollX, -m_scrollY); return r; };

		// 연결선: 상위 아래 가운데 → (접기 단추 아래) 가로줄 → 하위 위 가운데 (꺾은 선)
		const int lw = m_fixedMetrics ? m_lineW : (std::max)(1, m_nodeH / 30);
		CPen pen(PS_SOLID, lw, m_line);
		CPen* oldPen = mem.SelectObject(&pen);
		for (int n : m_order)
		{
			const Node& nd = m_nodes[n];
			if (nd.kids.empty() || IsCollapsed(n))
				continue;
			int left = INT_MAX, right = INT_MIN, kidTop = INT_MAX;
			for (int k : nd.kids)
				if (m_nodes[k].visible)
				{
					const int kx = (m_nodes[k].rc.left + m_nodes[k].rc.right) / 2;
					left = (std::min)(left, kx);
					right = (std::max)(right, kx);
					kidTop = (std::min)(kidTop, static_cast<int>(m_nodes[k].rc.top));
				}
			if (left == INT_MAX)
				continue;
			const int px = (nd.rc.left + nd.rc.right) / 2;
			const int r = m_fixedMetrics ? m_toggleR : (std::max)(6, m_nodeH / 7);
			const int midY = (std::min)(static_cast<int>(nd.rc.bottom) + r * 2 + 8, kidTop - 4);   // 접기 단추 아래
			left = (std::min)(left, px);
			right = (std::max)(right, px);
			// 화면 밖이면 건너뜀
			if (right < view.left - 2 || left > view.right + 2 || kidTop < view.top - 2 || nd.rc.bottom > view.bottom + 2)
				continue;
			mem.MoveTo(px - m_scrollX, nd.rc.bottom - m_scrollY);
			mem.LineTo(px - m_scrollX, midY - m_scrollY);
			mem.MoveTo(left - m_scrollX, midY - m_scrollY);
			mem.LineTo(right - m_scrollX + 1, midY - m_scrollY);
			for (int k : nd.kids)
				if (m_nodes[k].visible)
				{
					const int kx = (m_nodes[k].rc.left + m_nodes[k].rc.right) / 2;
					mem.MoveTo(kx - m_scrollX, midY - m_scrollY);
					mem.LineTo(kx - m_scrollX, m_nodes[k].rc.top - m_scrollY);
				}
		}
		mem.SelectObject(oldPen);

		// 트리와 아래 격자(상위 · 하위가 없는 제작사) 사이 구분선
		if (m_sepY >= 0)
			mem.FillSolidRect(m_margin - m_scrollX, m_sepY - m_scrollY, (std::max)(static_cast<int>(client.Width()), m_contentW) - m_margin * 2, 1, m_line);

		// 노드 (주인이 그림)
		for (int n : m_order)
		{
			const Node& nd = m_nodes[n];
			CRect inter;
			if (!inter.IntersectRect(nd.rc, view))
				continue;
			const CRect r = toClient(nd.rc);
			if (m_onDrawNode)
				m_onDrawNode(&mem, nd.row, r, n == m_sel, n == m_hot);
			else
			{
				mem.FillSolidRect(r, RGB(48, 64, 77));
				mem.SetTextColor(m_text);
				CRect tr = r;
				TextFB::DrawFmt(&mem, nd.name, tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
			}
		}

		// 접기 · 펼치기 동그라미 (− 펼침 / + 접힘, 접힘이면 옆에 하위 수)
		for (int n : m_order)
		{
			const CRect t0 = ToggleRect(n);
			if (t0.IsRectEmpty())
				continue;
			const CRect t = toClient(t0);
			if (t.right < 0 || t.left > client.right || t.bottom < 0 || t.top > client.bottom)
				continue;
			const bool collapsed = IsCollapsed(n);
			const bool hot = (n == m_hotToggle);
			const COLORREF sc = hot ? m_back : m_text;
			{
				// 원 · 기호를 안티앨리어싱으로 (작은 크기에서 GDI 로 그리면 깨져 보였음) - 중심은 정수 픽셀 가운데
				const float r = (t.Width() - 1) / 2.0f;
				const float cxf = t.left + r + 0.5f, cyf = t.top + r + 0.5f;
				EnsureGdiPlusTree();
				Gdiplus::Graphics g(mem.GetSafeHdc());
				g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
				g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
				auto gc = [](COLORREF c) { return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)); };
				Gdiplus::SolidBrush fill(gc(hot ? m_line : m_back));
				g.FillEllipse(&fill, cxf - r, cyf - r, r * 2, r * 2);
				Gdiplus::Pen ring(gc(m_line), 1.0f);
				g.DrawEllipse(&ring, cxf - r + 0.5f, cyf - r + 0.5f, r * 2 - 1.0f, r * 2 - 1.0f);
				const float s = r * 0.55f;                            // 기호 반 길이
				const float pw = (std::max)(1.0f, r / 3.5f);           // 기호 굵기
				Gdiplus::Pen sign(gc(sc), pw);
				sign.SetStartCap(Gdiplus::LineCapRound);
				sign.SetEndCap(Gdiplus::LineCapRound);
				g.DrawLine(&sign, cxf - s, cyf, cxf + s, cyf);         // −
				if (collapsed)
					g.DrawLine(&sign, cxf, cyf - s, cxf, cyf + s);     // + (세로)
			}
			const int cy = (t.top + t.bottom) / 2;
			if (collapsed)
			{
				CString cnt;
				cnt.Format(L"%d", static_cast<int>(m_nodes[n].kids.size()));
				// 접힌 하위 수: 기본 글꼴의 1.5배 · 굵게 · 밝은 글자 (단추 오른쪽)
				if (!m_countFont.GetSafeHandle())
				{
					LOGFONT lf = {};
					if (CFont* f = top ? top->GetFont() : nullptr)
						f->GetLogFont(&lf);
					else
						lf.lfHeight = -12;
					lf.lfHeight = lf.lfHeight * 3 / 2;
					lf.lfWeight = FW_BOLD;
					m_countFont.CreateFontIndirect(&lf);
				}
				CFont* of = mem.SelectObject(&m_countFont);
				mem.SetTextColor(m_text);
				TEXTMETRIC tm = {};
				mem.GetTextMetrics(&tm);
				const int ch = tm.tmHeight;
				CRect cr(t.right + 4, cy - ch / 2 - 1, t.right + 4 + (std::max)(m_hGap, ch * 3), cy - ch / 2 - 1 + ch + 2);
				TextFB::DrawFmt(&mem, cnt, cr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
				mem.SelectObject(of);
			}
		}
	}
	mem.SelectObject(oldFont);
	paintDC.BitBlt(0, 0, client.Width(), client.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(oldBmp);
}

LRESULT CStudioTreeView::OnDarkScrollSetPos(WPARAM wParam, LPARAM lParam)
{
	// 직접 그린 스크롤바 끌기 / 트랙 클릭: lParam 1 = 가로, 0 = 세로
	if (lParam == 1)
		ScrollTo(static_cast<int>(wParam), m_scrollY);
	else
		ScrollTo(m_scrollX, static_cast<int>(wParam));
	return 0;
}

void CStudioTreeView::OnHScroll(UINT nSBCode, UINT /*nPos*/, CScrollBar* /*pScrollBar*/)
{
	CRect rc;
	GetClientRect(&rc);
	SCROLLINFO si = { sizeof(si), SIF_TRACKPOS };
	GetScrollInfo(SB_HORZ, &si);
	int x = m_scrollX;
	switch (nSBCode)
	{
	case SB_LINELEFT:  x -= m_nodeW / 2; break;
	case SB_LINERIGHT: x += m_nodeW / 2; break;
	case SB_PAGELEFT:  x -= rc.Width(); break;
	case SB_PAGERIGHT: x += rc.Width(); break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION: x = si.nTrackPos; break;
	case SB_LEFT:  x = 0; break;
	case SB_RIGHT: x = m_contentW; break;
	}
	ScrollTo(x, m_scrollY);
}

void CStudioTreeView::OnVScroll(UINT nSBCode, UINT /*nPos*/, CScrollBar* /*pScrollBar*/)
{
	CRect rc;
	GetClientRect(&rc);
	SCROLLINFO si = { sizeof(si), SIF_TRACKPOS };
	GetScrollInfo(SB_VERT, &si);
	int y = m_scrollY;
	const int rowH = m_nodeH + m_vGap;
	switch (nSBCode)
	{
	case SB_LINEUP:   y -= rowH; break;
	case SB_LINEDOWN: y += rowH; break;
	case SB_PAGEUP:   y -= rc.Height(); break;
	case SB_PAGEDOWN: y += rc.Height(); break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION: y = si.nTrackPos; break;
	case SB_TOP:    y = 0; break;
	case SB_BOTTOM: y = m_contentH; break;
	}
	ScrollTo(m_scrollX, y);
}

BOOL CStudioTreeView::OnMouseWheel(UINT nFlags, short zDelta, CPoint /*pt*/)
{
	// 휠 = 세로 (한 칸에 노드 한 줄), Shift + 휠 = 가로
	const int step = (m_nodeH + m_vGap) * zDelta / WHEEL_DELTA;
	if (nFlags & MK_SHIFT)
		ScrollTo(m_scrollX - step * 2, m_scrollY);
	else
		ScrollTo(m_scrollX, m_scrollY - step);
	return TRUE;
}

void CStudioTreeView::OnMouseHWheel(UINT /*nFlags*/, short zDelta, CPoint /*pt*/)
{
	ScrollTo(m_scrollX + (m_nodeW / 2) * zDelta / WHEEL_DELTA, m_scrollY);
}

void CStudioTreeView::OnLButtonDown(UINT /*nFlags*/, CPoint point)
{
	SetFocus();
	const int t = ToggleAt(point);
	if (t >= 0)
	{
		Toggle(t);
		return;
	}
	const int n = NodeAt(point);
	if (n >= 0)
		SelectNode(n, true);
}

void CStudioTreeView::OnLButtonDblClk(UINT /*nFlags*/, CPoint point)
{
	if (ToggleAt(point) >= 0)
	{
		Toggle(ToggleAt(point));
		return;
	}
	const int n = NodeAt(point);
	if (n >= 0 && m_onActivate)
		m_onActivate(m_nodes[n].row);
}

void CStudioTreeView::OnRButtonDown(UINT nFlags, CPoint point)
{
	SetFocus();
	const int n = NodeAt(point);
	if (n >= 0)
		SelectNode(n, true);
	CWnd::OnRButtonDown(nFlags, point);   // 이후 WM_CONTEXTMENU
}

void CStudioTreeView::OnContextMenu(CWnd* /*pWnd*/, CPoint point)
{
	if (!m_onContext)
		return;
	int row = -1;
	if (point.x == -1 && point.y == -1)
	{
		// 키보드(Shift+F10 · 메뉴 키): 선택 노드 가운데
		if (m_sel < 0 || !m_nodes[m_sel].visible)
			return;
		CRect r = m_nodes[m_sel].rc;
		r.OffsetRect(-m_scrollX, -m_scrollY);
		point = r.CenterPoint();
		ClientToScreen(&point);
		row = m_nodes[m_sel].row;
	}
	else
	{
		CPoint pt = point;
		ScreenToClient(&pt);
		const int n = NodeAt(pt);
		row = (n >= 0) ? m_nodes[n].row : -1;
	}
	m_onContext(row, point);
}

UINT CStudioTreeView::OnGetDlgCode()
{
	return DLGC_WANTARROWS | DLGC_WANTCHARS;
}

void CStudioTreeView::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (m_order.empty())
	{
		if (m_onKey) m_onKey(nChar);
		return;
	}
	int pos = -1;
	for (size_t i = 0; i < m_order.size(); ++i)
		if (m_order[i] == m_sel) { pos = static_cast<int>(i); break; }
	// 보이는 순서(깊이 우선)를 화면 위 → 아래 순서로
	std::vector<int> byY = m_order;
	std::stable_sort(byY.begin(), byY.end(), [this](int a, int b)   // 읽는 순서: 위 → 아래, 같은 줄은 왼쪽 → 오른쪽
	{
		if (m_nodes[a].rc.top != m_nodes[b].rc.top) return m_nodes[a].rc.top < m_nodes[b].rc.top;
		return m_nodes[a].rc.left < m_nodes[b].rc.left;
	});
	int yPos = -1;
	for (size_t i = 0; i < byY.size(); ++i)
		if (byY[i] == m_sel) { yPos = static_cast<int>(i); break; }
	switch (nChar)
	{
	case VK_LEFT:    // 읽는 순서 앞
		SelectNode(byY[yPos <= 0 ? 0 : yPos - 1], true);
		return;
	case VK_RIGHT:   // 읽는 순서 뒤
		SelectNode(byY[yPos < 0 ? 0 : (std::min)(static_cast<int>(byY.size()) - 1, yPos + 1)], true);
		return;
	case VK_HOME:
		SelectNode(byY.front(), true);
		return;
	case VK_END:
		SelectNode(byY.back(), true);
		return;
	case VK_UP:      // 상위로 (맨 위 노드면 펼친 하위 접기)
		if (m_sel >= 0)
		{
			if (m_nodes[m_sel].parent >= 0)
				SelectNode(m_nodes[m_sel].parent, true);
			else if (!m_nodes[m_sel].kids.empty() && !IsCollapsed(m_sel))
				Toggle(m_sel);
		}
		else
			SelectNode(byY.front(), true);
		return;
	case VK_DOWN:    // 첫 하위로 (접혀 있으면 펼치기)
		if (m_sel >= 0 && !m_nodes[m_sel].kids.empty())
		{
			if (IsCollapsed(m_sel))
				Toggle(m_sel);
			else
				SelectNode(m_nodes[m_sel].kids.front(), true);
		}
		else if (m_sel < 0)
			SelectNode(byY.front(), true);
		return;
	case VK_RETURN:
		if (m_sel >= 0 && m_onActivate)
			m_onActivate(m_nodes[m_sel].row);
		return;
	}
	(void)pos;
	if (m_onKey)
		m_onKey(nChar);
	else
		CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CStudioTreeView::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_tracking)
	{
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
		::TrackMouseEvent(&tme);
		m_tracking = true;
	}
	const int t = ToggleAt(point);
	const int n = (t >= 0) ? -1 : NodeAt(point);
	if (t != m_hotToggle || n != m_hot)
	{
		m_hotToggle = t;
		m_hot = n;
		Invalidate(FALSE);
	}
	CWnd::OnMouseMove(nFlags, point);
}

LRESULT CStudioTreeView::OnMouseLeave(WPARAM, LPARAM)
{
	m_tracking = false;
	if (m_hot >= 0 || m_hotToggle >= 0)
	{
		m_hot = m_hotToggle = -1;
		Invalidate(FALSE);
	}
	return 0;
}
