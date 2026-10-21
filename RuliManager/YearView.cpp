#include "pch.h"
#include "YearView.h"
#include "TextDraw.h"
#include "DarkControls.h"   // WM_DARKSCROLL_SETPOS (직접 그린 스크롤바)

BEGIN_MESSAGE_MAP(CYearView, CWnd)
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
	ON_MESSAGE(WM_MOUSELEAVE, &CYearView::OnMouseLeave)
	ON_MESSAGE(WM_DARKSCROLL_SETPOS, &CYearView::OnDarkScrollSetPos)
END_MESSAGE_MAP()

bool CYearView::Create(CWnd* parent, UINT id)
{
	const CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), nullptr);
	// 격자 보기(CVideoGrid)와 같은 테두리 (WS_EX_CLIENTEDGE - 사각 영역 안에)
	if (!CWnd::CreateEx(WS_EX_CLIENTEDGE, cls, L"", WS_CHILD | WS_HSCROLL | WS_VSCROLL | WS_CLIPSIBLINGS | WS_TABSTOP, CRect(0, 0, 10, 10), parent, id))
		return false;
	return true;   // 스크롤바는 주인이 다른 목록과 같은 직접 그린 스크롤바(CDarkScrollBar)를 겹쳐 놓음
}

void CYearView::SetColors(COLORREF back, COLORREF title, COLORREF label, COLORREF line)
{
	m_back = back;
	m_title = title;
	m_label = label;
	m_line = line;
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

void CYearView::SetCardSize(int w, int h)
{
	if (w == m_cardW && h == m_cardH)
		return;
	m_cardW = (std::max)(20, w);
	m_cardH = (std::max)(20, h);
	Relayout();
}

void CYearView::SetData(std::vector<Column>&& columns, std::map<int, std::vector<int>>&& parts, bool resetScroll)
{
	m_cols = std::move(columns);
	m_parts = std::move(parts);
	m_partOwner.clear();
	for (const auto& kv : m_parts)
		for (int p : kv.second)
			m_partOwner[p] = kv.first;
	m_hotItem = m_hotPart = -1;
	if (resetScroll)
		m_scrollX = m_scrollY = 0;
	Relayout();
}

void CYearView::SetCardColors(std::map<int, COLORREF>&& colors)
{
	m_cardColors = std::move(colors);
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

void CYearView::SetLegend(std::vector<std::pair<CString, COLORREF>>&& legend)
{
	m_legend = std::move(legend);
	Relayout();   // 범례 줄 높이
}

void CYearView::SetSelected(int item)
{
	if (item == m_sel)
		return;
	const int old = m_sel;
	m_sel = item;
	InvalidateItem(old);
	InvalidateItem(item);
}

int CYearView::OwnerOf(int item) const
{
	auto it = m_partOwner.find(item);
	return it != m_partOwner.end() ? it->second : item;
}

void CYearView::InvalidateItem(int item)
{
	if (item < 0 || !GetSafeHwnd())
		return;
	item = OwnerOf(item);   // 파트면 그 카드
	for (const auto& c : m_cards)
	{
		if (c.second != item)
			continue;
		CRect r = c.first;
		r.OffsetRect(-m_scrollX, -m_scrollY);
		r.InflateRect(m_gap, m_gap);   // 카드 뒤 배경색까지
		InvalidateRect(&r, FALSE);
	}
}

void CYearView::ScrollToStart()
{
	ScrollTo(0, 0);
}

CFont* CYearView::BaseFont()
{
	CWnd* parent = GetParent();
	return parent ? parent->GetFont() : nullptr;
}

void CYearView::Relayout()
{
	m_colX.clear();
	m_colW.clear();
	m_rowY.clear();
	m_rowHs.clear();
	m_cards.clear();
	if (!GetSafeHwnd())
		return;
	// 글꼴 · 줄 높이
	if (!m_titleFont.GetSafeHandle())
	{
		LOGFONT lf = {};
		if (CFont* f = BaseFont())
			f->GetLogFont(&lf);
		lf.lfHeight = lf.lfHeight * 13 / 10;   // 연도: 조금 크게, 굵게
		lf.lfWeight = FW_BOLD;
		m_titleFont.CreateFontIndirect(&lf);
	}
	CClientDC dc(this);
	CFont* old = dc.SelectObject(BaseFont());
	TEXTMETRIC tm = {};
	dc.GetTextMetrics(&tm);
	m_legendH = m_legend.empty() ? 0 : tm.tmHeight + 10;   // 맨 위: 범례 (이름마다 색)
	m_headH = m_legendH + tm.tmHeight + 12;           // 위: (범례 +) 달 머리글
	dc.SelectObject(&m_titleFont);
	m_labelW = dc.GetTextExtent(L"0000년").cx + 18;   // 왼쪽: 연도 이름
	for (const Column& c : m_cols)
		m_labelW = (std::max)(m_labelW, static_cast<int>(dc.GetTextExtent(c.title).cx) + 18);   // "날짜 없음"
	dc.SelectObject(old);

	m_gap = (std::max)(4, m_cardW / 25);
	m_rowH = m_cardH + m_gap * 2;                     // 카드 한 단 (연도 줄 최소 높이 · 스크롤 단위)
	bool unknown = false;   // 발매월 모름 칸이 필요한지
	for (const Column& c : m_cols)
		if (!c.months[12].empty()) { unknown = true; break; }
	m_rows = unknown ? 13 : 12;                       // 달 칸 수 (가로)

	// 달 칸 폭 = 카드 하나 (같은 달 영상은 세로로 쌓음)
	int x = m_labelW;
	const int colW = m_cardW + m_gap * 2;
	for (int mo = 0; mo < m_rows; ++mo)
	{
		m_colX.push_back(x);
		m_colW.push_back(colW);
		x += colW;
	}
	// 연도 줄 높이 = 그 연도에서 영상이 가장 많은 달 기준
	int y = m_headH;
	for (size_t r = 0; r < m_cols.size(); ++r)
	{
		size_t maxCards = 1;
		for (int mo = 0; mo < m_rows; ++mo)
			maxCards = (std::max)(maxCards, m_cols[r].months[mo].size());
		const int h = static_cast<int>(maxCards) * (m_cardH + m_gap) + m_gap;
		m_rowY.push_back(y);
		m_rowHs.push_back(h);
		for (int mo = 0; mo < m_rows; ++mo)
		{
			const int cx = m_colX[mo] + m_gap;
			int cy = y + m_gap;
			for (int item : m_cols[r].months[mo])
			{
				m_cards.push_back({ CRect(cx, cy, cx + m_cardW, cy + m_cardH), item });
				cy += m_cardH + m_gap;
			}
		}
		y += h;
	}
	m_contentW = x + m_gap;
	m_contentH = y + m_gap;
	UpdateScrollBars();
	Invalidate(FALSE);
}

void CYearView::UpdateScrollBars()
{
	if (!GetSafeHwnd())
		return;
	CRect rc;
	GetClientRect(&rc);
	m_scrollX = (std::max)(0, (std::min)(m_scrollX, m_contentW - rc.Width()));
	m_scrollY = (std::max)(0, (std::min)(m_scrollY, m_contentH - rc.Height()));
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

void CYearView::ScrollTo(int x, int y)
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

int CYearView::ItemAt(CPoint client, int* part) const
{
	if (part)
		*part = -1;
	const int item = HitTest(client);
	if (item < 0)
		return -1;
	auto it = m_parts.find(item);
	if (it == m_parts.end() || it->second.size() < 2)
		return item;
	// 나눠진 영상: 카드를 파트 수만큼 좌우로 나눈 칸
	for (const auto& c : m_cards)
	{
		if (c.second != item)
			continue;
		const int n = static_cast<int>(it->second.size());
		const int x = client.x + m_scrollX - c.first.left;
		const int p = (std::max)(0, (std::min)(n - 1, x * n / (std::max)(1, c.first.Width())));
		if (part)
			*part = p;
		return it->second[p];
	}
	return item;
}

void CYearView::SetHot(int item, int part)
{
	if (item == m_hotItem && part == m_hotPart)
		return;
	const int old = m_hotItem;
	m_hotItem = item;
	m_hotPart = part;
	InvalidateItem(old);
	InvalidateItem(item);
}

void CYearView::OnMouseMove(UINT nFlags, CPoint point)
{
	int part = -1;
	const int item = ItemAt(point, &part);
	SetHot(part >= 0 ? OwnerOf(item) : -1, part);   // 파트 칸 표시는 나눠진 영상 카드에서만
	if (!m_tracking)
	{
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, GetSafeHwnd(), 0 };
		m_tracking = ::TrackMouseEvent(&tme) != FALSE;
	}
	CWnd::OnMouseMove(nFlags, point);
}

LRESULT CYearView::OnDarkScrollSetPos(WPARAM wParam, LPARAM lParam)
{
	// 직접 그린 스크롤바 끌기 / 트랙 클릭: lParam 1 = 가로, 0 = 세로
	if (lParam == 1)
		ScrollTo(static_cast<int>(wParam), m_scrollY);
	else
		ScrollTo(m_scrollX, static_cast<int>(wParam));
	return 0;
}

LRESULT CYearView::OnMouseLeave(WPARAM, LPARAM)
{
	m_tracking = false;
	SetHot(-1, -1);
	return 0;
}

int CYearView::HitTest(CPoint client) const
{
	const CPoint pt(client.x + m_scrollX, client.y + m_scrollY);
	if (client.y < m_headH || client.x < m_labelW)
		return -1;   // 달 머리글 · 연도 이름 (고정)
	for (const auto& c : m_cards)
		if (c.first.PtInRect(pt))
			return c.second;
	return -1;
}

namespace
{
	// 두 색 섞기 (t = 0 ~ 100, b 쪽 비율)
	COLORREF MixColor(COLORREF a, COLORREF b, int t)
	{
		return RGB((GetRValue(a) * (100 - t) + GetRValue(b) * t) / 100,
		           (GetGValue(a) * (100 - t) + GetGValue(b) * t) / 100,
		           (GetBValue(a) * (100 - t) + GetBValue(b) * t) / 100);
	}
}

void CYearView::OnPaint()
{
	CPaintDC paint(this);
	CRect rc;
	GetClientRect(&rc);
	if (rc.IsRectEmpty())
		return;
	CDC mem;
	mem.CreateCompatibleDC(&paint);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&paint, rc.Width(), rc.Height());
	CBitmap* oldBmp = mem.SelectObject(&bmp);
	mem.FillSolidRect(rc, m_back);
	mem.SetBkMode(TRANSPARENT);
	CFont* oldFont = mem.SelectObject(BaseFont());

	if (m_cols.empty())
	{
		mem.SetTextColor(m_label);
		CRect tr = rc;
		mem.DrawText(L"영상 없음", tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}
	else
	{
		// 연도 줄 배경: 한 줄 걸러 조금 밝은 띠 (연도 구분, 왼쪽 연도 이름 칸까지)
		const COLORREF band = MixColor(m_back, m_title, 7);
		auto fillBands = [&](int left, int right)
		{
			for (size_t r = 1; r < m_rowY.size(); r += 2)
			{
				const int y = m_rowY[r] - m_scrollY;
				const int top = (std::max)(y, m_headH), bottom = (std::min)(y + m_rowHs[r], static_cast<int>(rc.bottom));   // 머리글 아래만
				if (bottom > top)
					mem.FillSolidRect(left, top, right - left, bottom - top, band);
			}
		};
		fillBands(m_labelW, rc.right);
		// 칸 선: 달 칸 사이 세로선 (흐리게, 연도 구분선은 맨 마지막에 진하게)
		for (int mo = 1; mo < m_rows; ++mo)
		{
			const int x = m_colX[mo] - m_scrollX;
			if (x >= m_labelW && x <= rc.right)
				mem.FillSolidRect(x, m_headH, 1, rc.Height(), m_line);
		}
		// 카드
		for (const auto& card : m_cards)
		{
			CRect r = card.first;
			r.OffsetRect(-m_scrollX, -m_scrollY);
			if (r.right < m_labelW || r.left > rc.right || r.bottom < m_headH || r.top > rc.bottom)
				continue;
			if (m_onDrawCard)
			{
				const int saved = mem.SaveDC();
				mem.IntersectClipRect(m_labelW, m_headH, rc.right, rc.bottom);   // 머리글 아래 · 연도 이름 오른쪽만
				const bool sel = (m_sel >= 0 && OwnerOf(m_sel) == card.second);   // 파트 중 하나를 골라도 카드 선택 표시
				// 카드 뒤 배경색 (배우 이름마다 고유색): 카드 둘레 간격까지, 위아래 이웃 카드와는 1px 띄움
				auto cit = m_cardColors.find(card.second);
				if (cit != m_cardColors.end())
				{
					const int gx = m_gap - 1, gt = m_gap / 2, gb = m_gap - m_gap / 2 - 1;
					mem.FillSolidRect(r.left - gx, r.top - gt, r.Width() + gx * 2, r.Height() + gt + gb, cit->second);
				}
				m_onDrawCard(&mem, card.second, r, sel);
				// 나눠진 영상 카드에 마우스: 파트 수만큼 좌우 칸 + 반투명 번호 (올라간 칸은 밝게)
				auto pit = m_parts.find(card.second);
				if (card.second == m_hotItem && pit != m_parts.end() && pit->second.size() >= 2)
				{
					const int n = static_cast<int>(pit->second.size());
					Gdiplus::Graphics g(mem.GetSafeHdc());
					g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
					g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
					Gdiplus::SolidBrush dim(Gdiplus::Color(70, 0, 0, 0));
					Gdiplus::SolidBrush hot(Gdiplus::Color(60, 255, 255, 255));
					Gdiplus::Pen sep(Gdiplus::Color(110, 255, 255, 255), 1.0f);
					Gdiplus::FontFamily family(L"Segoe UI");
					const float fs = (std::max)(9.0f, (std::min)(r.Height() * 0.45f, r.Width() / static_cast<float>(n) * 0.6f));
					Gdiplus::Font font(&family, fs, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
					Gdiplus::StringFormat fmt;
					fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
					fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
					for (int p = 0; p < n; ++p)
					{
						const int x0 = r.left + r.Width() * p / n, x1 = r.left + r.Width() * (p + 1) / n;
						const Gdiplus::RectF cell(static_cast<float>(x0), static_cast<float>(r.top), static_cast<float>(x1 - x0), static_cast<float>(r.Height()));
						g.FillRectangle(p == m_hotPart ? static_cast<Gdiplus::Brush*>(&hot) : static_cast<Gdiplus::Brush*>(&dim), cell);
						if (p > 0)
							g.DrawLine(&sep, static_cast<float>(x0), static_cast<float>(r.top + 2), static_cast<float>(x0), static_cast<float>(r.bottom - 2));
						Gdiplus::SolidBrush text(Gdiplus::Color(p == m_hotPart ? 235 : 150, 255, 255, 255));
						wchar_t num[8];
						swprintf_s(num, L"%d", p + 1);
						g.DrawString(num, -1, &font, cell, &fmt, &text);
					}
				}
				mem.RestoreDC(saved);
			}
		}
		// 위: 달 머리글 (세로 스크롤해도 고정, 가로로는 같이 움직임)
		mem.FillSolidRect(0, 0, rc.Width(), m_headH, m_back);
		mem.SelectObject(BaseFont());
		for (int mo = 0; mo < m_rows; ++mo)
		{
			const int cx = m_colX[mo] - m_scrollX, cw = m_colW[mo];
			if (cx + cw < m_labelW || cx > rc.right)
				continue;
			bool any = false;   // 영상이 있는 달인지 (없으면 흐리게)
			for (const Column& c : m_cols)
				if (!c.months[mo].empty()) { any = true; break; }
			CString label;
			if (mo < 12)
				label.Format(L"%d월", mo + 1);
			else
				label = L"?";
			mem.SetTextColor(any ? m_label : m_line);
			const int saved = mem.SaveDC();
			mem.IntersectClipRect(m_labelW, 0, rc.right, m_headH);
			CRect tr(cx + m_gap, m_legendH, cx + cw, m_headH - 4);
			mem.DrawText(label, tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			mem.RestoreDC(saved);
		}
		mem.FillSolidRect(0, m_headH - 2, rc.Width(), 2, m_line);   // 머리글 밑줄
		// 맨 위 범례: ■ 이름 → ■ 이름 … (별칭 변경 이력 순서, 스크롤해도 고정)
		if (m_legendH > 0)
		{
			mem.SelectObject(BaseFont());
			const int sw = (std::max)(8, m_legendH - 12);   // 색 네모 크기
			int lx = 8;
			for (size_t i = 0; i < m_legend.size() && lx < rc.right; ++i)
			{
				if (i > 0)
				{
					mem.SetTextColor(m_label);
					CRect ar(lx, 0, lx + 200, m_legendH);
					mem.DrawText(L"→", ar, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
					const int aw = ar.Width();
					ar = CRect(lx, 0, lx + aw, m_legendH);
					mem.DrawText(L"→", ar, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
					lx += aw + 8;
				}
				const int sy = (m_legendH - sw) / 2;
				mem.FillSolidRect(lx, sy, sw, sw, m_legend[i].second);
				lx += sw + 5;
				const int tw = TextFB::Width(&mem, m_legend[i].first);
				mem.SetTextColor(m_title);
				TextFB::Draw(&mem, m_legend[i].first, CRect(lx, 0, lx + tw + 2, m_legendH), DT_LEFT, true);
				lx += tw + 8;
			}
		}
		// 왼쪽: 연도 이름 (가로 스크롤해도 고정, 세로로는 같이 움직임)
		mem.FillSolidRect(0, m_headH, m_labelW, rc.Height() - m_headH, m_back);
		fillBands(0, m_labelW);
		mem.SelectObject(&m_titleFont);
		for (size_t r = 0; r < m_rowY.size() && r < m_cols.size(); ++r)
		{
			const int y = m_rowY[r] - m_scrollY;
			if (y + m_rowHs[r] < m_headH || y > rc.bottom)
				continue;
			const int saved = mem.SaveDC();
			mem.IntersectClipRect(0, m_headH, m_labelW, rc.bottom);
			mem.SetTextColor(m_title);
			CRect tr(6, y, m_labelW - 6, y + m_rowH);   // 줄이 높아도 첫 단 카드 옆 (위쪽)
			TextFB::Draw(&mem, m_cols[r].title, tr, DT_LEFT, true);
			mem.RestoreDC(saved);
		}
		mem.FillSolidRect(m_labelW - 2, m_legendH, 2, rc.Height() - m_legendH, m_line);   // 연도 이름 오른쪽 선 (범례 줄 아래부터 — 범례 글자와 겹치지 않게)
		// 연도 구분선: 연도 줄 사이 (마지막 줄 아래 포함), 연도 이름 칸부터 오른쪽 끝까지 2px 진하게
		const COLORREF sepColor = MixColor(m_back, m_title, 35);
		for (size_t r = 1; r <= m_rowY.size(); ++r)
		{
			const int y = (r < m_rowY.size() ? m_rowY[r] : m_rowY.back() + m_rowHs.back()) - m_scrollY - 1;
			if (y >= m_headH && y <= rc.bottom)
				mem.FillSolidRect(0, y, rc.Width(), 2, sepColor);
		}
	}
	mem.SelectObject(oldFont);
	paint.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(oldBmp);
}

void CYearView::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	UpdateScrollBars();
	Invalidate(FALSE);
}

void CYearView::OnHScroll(UINT nSBCode, UINT /*nPos*/, CScrollBar* /*pScrollBar*/)
{
	CRect rc;
	GetClientRect(&rc);
	SCROLLINFO si = { sizeof(si), SIF_TRACKPOS };
	GetScrollInfo(SB_HORZ, &si);
	int x = m_scrollX;
	switch (nSBCode)
	{
	case SB_LINELEFT:  x -= m_cardW / 2; break;
	case SB_LINERIGHT: x += m_cardW / 2; break;
	case SB_PAGELEFT:  x -= rc.Width(); break;
	case SB_PAGERIGHT: x += rc.Width(); break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION: x = si.nTrackPos; break;
	case SB_LEFT:  x = 0; break;
	case SB_RIGHT: x = m_contentW; break;
	}
	ScrollTo(x, m_scrollY);
}

void CYearView::OnVScroll(UINT nSBCode, UINT /*nPos*/, CScrollBar* /*pScrollBar*/)
{
	CRect rc;
	GetClientRect(&rc);
	SCROLLINFO si = { sizeof(si), SIF_TRACKPOS };
	GetScrollInfo(SB_VERT, &si);
	int y = m_scrollY;
	switch (nSBCode)
	{
	case SB_LINEUP:   y -= m_rowH / 2; break;
	case SB_LINEDOWN: y += m_rowH / 2; break;
	case SB_PAGEUP:   y -= rc.Height(); break;
	case SB_PAGEDOWN: y += rc.Height(); break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION: y = si.nTrackPos; break;
	case SB_TOP:    y = 0; break;
	case SB_BOTTOM: y = m_contentH; break;
	}
	ScrollTo(m_scrollX, y);
}

BOOL CYearView::OnMouseWheel(UINT nFlags, short zDelta, CPoint /*pt*/)
{
	// 휠 = 세로 (한 칸에 반 줄), Shift + 휠 = 가로
	const int step = (std::max)(20, m_rowH / 2) * zDelta / WHEEL_DELTA;
	if (nFlags & MK_SHIFT)
		ScrollTo(m_scrollX - step, m_scrollY);
	else
		ScrollTo(m_scrollX, m_scrollY - step);
	return TRUE;
}

void CYearView::OnMouseHWheel(UINT /*nFlags*/, short zDelta, CPoint /*pt*/)
{
	ScrollTo(m_scrollX + (std::max)(20, m_cardW / 2) * zDelta / WHEEL_DELTA, m_scrollY);
}

void CYearView::OnLButtonDown(UINT /*nFlags*/, CPoint point)
{
	SetFocus();
	const int item = ItemAt(point);   // 나눠진 영상: 누른 칸의 파트
	if (item >= 0)
	{
		SetSelected(item);
		if (m_onSelect)
			m_onSelect(item);
	}
}

void CYearView::OnLButtonDblClk(UINT /*nFlags*/, CPoint point)
{
	// 카드 위 덧그림(스크린샷 추출 표시 등)을 주인이 먼저 처리
	if (m_onDblClickAt)
	{
		const int card = HitTest(point);
		if (card >= 0)
			for (const auto& c : m_cards)
				if (c.second == card)
				{
					CRect r = c.first;
					r.OffsetRect(-m_scrollX, -m_scrollY);
					if (m_onDblClickAt(card, r, point))
						return;
					break;
				}
	}
	const int item = ItemAt(point);
	if (item >= 0)
	{
		SetSelected(item);
		if (m_onSelect)
			m_onSelect(item);
		if (m_onActivate)
			m_onActivate(item);
	}
}

void CYearView::OnRButtonDown(UINT nFlags, CPoint point)
{
	SetFocus();
	const int item = ItemAt(point);
	if (item >= 0)
	{
		SetSelected(item);
		if (m_onSelect)
			m_onSelect(item);
	}
	CWnd::OnRButtonDown(nFlags, point);   // 이후 WM_CONTEXTMENU
}

void CYearView::OnContextMenu(CWnd* /*pWnd*/, CPoint point)
{
	int item = -1;
	if (point.x == -1 && point.y == -1)
	{
		// 키보드 (메뉴 키 / Shift+F10): 선택한 카드 가운데
		item = m_sel;
		CRect r;
		const int owner = OwnerOf(item);
		for (const auto& c : m_cards)
			if (c.second == owner) { r = c.first; break; }
		r.OffsetRect(-m_scrollX, -m_scrollY);
		point = r.CenterPoint();
		ClientToScreen(&point);
	}
	else
	{
		CPoint pt = point;
		ScreenToClient(&pt);
		item = ItemAt(pt);
	}
	if (m_onContext)
		m_onContext(item, point);
}

void CYearView::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	// 연도별 보기에 포커스가 있을 때도 격자와 같은 키 (F5 새로고침 등) - 주인에게 넘김
	if (m_onKey)
	{
		m_onKey(nChar);
		return;
	}
	CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}
