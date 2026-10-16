#include "pch.h"
#include "RuliManager.h"
#include "TagLinkDlg.h"
#include "TextDraw.h"
#include <cmath>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	// GDI+ 는 처음 쓸 때 한 번 시작 (화살표 부드럽게)
	void EnsureGdiPlusTL()
	{
		static ULONG_PTR token = 0;
		if (!token)
		{
			Gdiplus::GdiplusStartupInput input;
			Gdiplus::GdiplusStartup(&token, &input, nullptr);
		}
	}
}

// ---------------------------------------------------------------------------
// CTagLinkView: 왼쪽 기존 태그 칩 · 오른쪽 붙여넣은 태그 칩 · 연결 화살표

BEGIN_MESSAGE_MAP(CTagLinkView, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_RBUTTONDOWN()
	ON_WM_MOUSEWHEEL()
	ON_WM_CAPTURECHANGED()
	ON_WM_SETCURSOR()
	ON_MESSAGE(WM_MOUSELEAVE, &CTagLinkView::OnMouseLeave)
END_MESSAGE_MAP()

bool CTagLinkView::Create(CWnd* parent, const CRect& rc, UINT id)
{
	const CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), nullptr);
	if (!CWnd::CreateEx(WS_EX_CLIENTEDGE, cls, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, rc, parent, id))
		return false;
	// 글꼴 높이에 맞춘 칩 크기
	CClientDC dc(this);
	CFont* old = dc.SelectObject(Font());
	TEXTMETRIC tm = {};
	dc.GetTextMetrics(&tm);
	dc.SelectObject(old);
	const int th = static_cast<int>(tm.tmHeight);   // LONG → int (std::max 형 맞춤)
	m_chipH = th + 10;
	m_gap = (std::max)(4, th / 3);
	m_pad = th;
	m_headH = th + 10;
	Rebuild();
	return true;
}

CFont* CTagLinkView::Font()
{
	CWnd* parent = GetParent();
	return parent ? parent->GetFont() : nullptr;
}

void CTagLinkView::SetFilter(const CString& text)
{
	m_filter = text;
	m_filter.Trim();
	m_filter.MakeLower();
	m_scrollL = 0;
	Rebuild();
}

void CTagLinkView::Rebuild()
{
	// 현재 영상 태그: 영상에 적힌 순서 그대로 (검색어가 있으면 그 글자가 들어간 것 + 연결된 것만)
	m_left.clear();
	std::vector<bool> linked(m_all.size(), false);
	for (const WebTag& w : m_web)
		if (w.link >= 0 && w.link < static_cast<int>(m_all.size()))
			linked[w.link] = true;
	for (size_t i = 0; i < m_all.size(); ++i)
	{
		if (!m_filter.IsEmpty() && !linked[i])
		{
			CString low = m_all[i];
			low.MakeLower();
			if (low.Find(m_filter) < 0)
				continue;
		}
		m_left.push_back(static_cast<int>(i));
	}
	ClampScroll();
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

CRect CTagLinkView::ColumnRect(int side) const
{
	CRect rc;
	GetClientRect(&rc);
	const int colW = (std::max)(60, (rc.Width() - m_pad * 2) * 36 / 100);
	const int top = m_headH;
	// side -1 = 기존 태그 → 오른쪽 열, side 1 = 붙여넣은(추가할) 태그 → 왼쪽 열
	if (side > 0)
		return CRect(m_pad, top, m_pad + colW, rc.bottom);
	return CRect(rc.right - m_pad - colW, top, rc.right - m_pad, rc.bottom);
}

int CTagLinkView::ContentH(size_t count) const
{
	return static_cast<int>(count) * (m_chipH + m_gap) + m_gap;
}

void CTagLinkView::ClampScroll()
{
	if (!GetSafeHwnd())
		return;
	const int h = ColumnRect(-1).Height();
	m_scrollL = (std::max)(0, (std::min)(m_scrollL, ContentH(m_left.size()) - h));
	m_scrollR = (std::max)(0, (std::min)(m_scrollR, ContentH(m_web.size()) - h));
}

CRect CTagLinkView::LeftChipRect(int pos) const
{
	const CRect col = ColumnRect(-1);
	const int y = col.top + m_gap + pos * (m_chipH + m_gap) - m_scrollL;
	return CRect(col.left, y, col.right, y + m_chipH);
}

CRect CTagLinkView::RightChipRect(int i) const
{
	const CRect col = ColumnRect(1);
	const int y = col.top + m_gap + i * (m_chipH + m_gap) - m_scrollR;
	return CRect(col.left, y, col.right, y + m_chipH);
}

int CTagLinkView::LeftAt(CPoint pt) const
{
	if (pt.y < m_headH)
		return -1;
	for (size_t p = 0; p < m_left.size(); ++p)
		if (LeftChipRect(static_cast<int>(p)).PtInRect(pt))
			return m_left[p];
	return -1;
}

int CTagLinkView::RightAt(CPoint pt) const
{
	if (pt.y < m_headH)
		return -1;
	for (size_t i = 0; i < m_web.size(); ++i)
		if (RightChipRect(static_cast<int>(i)).PtInRect(pt))
			return static_cast<int>(i);
	return -1;
}

int CTagLinkView::LeftPosOf(int tagIndex) const
{
	for (size_t p = 0; p < m_left.size(); ++p)
		if (m_left[p] == tagIndex)
			return static_cast<int>(p);
	return -1;
}

bool CTagLinkView::ArrowEnds(int webIndex, CPoint& a, CPoint& b) const
{
	if (webIndex < 0 || webIndex >= static_cast<int>(m_web.size()) || m_web[webIndex].link < 0)
		return false;
	const int pos = LeftPosOf(m_web[webIndex].link);
	if (pos < 0)
		return false;
	const CRect colL = ColumnRect(-1);
	const CRect l = LeftChipRect(pos), r = RightChipRect(webIndex);
	// 칩이 스크롤로 안 보이면 열의 위 / 아래 끝에서 시작
	auto clampY = [&colL](int y) { return (std::max)(static_cast<int>(colL.top), (std::min)(y, static_cast<int>(colL.bottom) - 1)); };
	a = CPoint(r.right, clampY(r.CenterPoint().y));      // 왼쪽 붙여넣은 태그 칩 오른쪽 끝에서
	b = CPoint(l.left - 1, clampY(l.CenterPoint().y));   // 오른쪽 기존 태그 칩으로 (화살촉)
	return true;
}

int CTagLinkView::ArrowAt(CPoint pt) const
{
	for (size_t i = 0; i < m_web.size(); ++i)
	{
		CPoint a, b;
		if (!ArrowEnds(static_cast<int>(i), a, b))
			continue;
		if (pt.x < a.x || pt.x > b.x)
			continue;
		// 선분까지 거리
		const double dx = b.x - a.x, dy = b.y - a.y;
		const double len2 = dx * dx + dy * dy;
		double t = len2 > 0 ? ((pt.x - a.x) * dx + (pt.y - a.y) * dy) / len2 : 0.0;
		t = (std::max)(0.0, (std::min)(1.0, t));
		const double px = a.x + t * dx - pt.x, py = a.y + t * dy - pt.y;
		if (std::sqrt(px * px + py * py) <= 5.0)
			return static_cast<int>(i);
	}
	return -1;
}

void CTagLinkView::Link(int tagIndex, int webIndex)
{
	if (tagIndex < 0 || webIndex < 0 || webIndex >= static_cast<int>(m_web.size()))
		return;
	m_web[webIndex].link = tagIndex;
	m_web[webIndex].addNew = false;
	Rebuild();
}

void CTagLinkView::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	ClampScroll();
	Invalidate(FALSE);
}

void CTagLinkView::OnPaint()
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
	CFont* oldFont = mem.SelectObject(Font());
	mem.FillSolidRect(rc, TCOL(TC_BACK));
	mem.SetBkMode(TRANSPARENT);

	const CRect colL = ColumnRect(-1), colR = ColumnRect(1);
	// 머리글
	mem.SetTextColor(TCOL(TC_SUB2));
	CRect hl(colL.left, 0, colL.right, m_headH), hr(colR.left, 0, colR.right, m_headH);
	CString lt;
	lt.Format(L"%s (%d)", static_cast<LPCWSTR>(m_headCur), static_cast<int>(m_all.size()));
	CString rt;
	rt.Format(L"%s (%d)", static_cast<LPCWSTR>(m_headWeb), static_cast<int>(m_web.size()));
	mem.DrawText(lt, hl, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	mem.DrawText(rt, hr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	mem.FillSolidRect(m_pad, m_headH - 1, rc.Width() - m_pad * 2, 1, TCOL(TC_BORDER));

	std::vector<bool> linkedTag(m_all.size(), false);
	for (const WebTag& w : m_web)
		if (w.link >= 0 && w.link < static_cast<int>(linkedTag.size()))
			linkedTag[w.link] = true;

	const COLORREF accent = TCOL(TC_ACCENT);
	auto drawChip = [&](const CRect& r, const CString& text, COLORREF fill, COLORREF border, COLORREF textColor, int borderW, bool strike)
	{
		const int radius = (std::max)(6, m_chipH / 3);
		CBrush br(fill);
		CPen pen(PS_SOLID, borderW, border);
		CBrush* ob = mem.SelectObject(&br);
		CPen* op = mem.SelectObject(&pen);
		mem.RoundRect(r, CPoint(radius, radius));
		mem.SelectObject(ob);
		mem.SelectObject(op);
		mem.SetTextColor(textColor);
		CRect tr = r;
		tr.DeflateRect(m_gap + 2, 0);
		TextFB::Draw(&mem, text, tr, DT_CENTER, true);
		if (strike)
		{
			const int tw = (std::min)(static_cast<int>(tr.Width()), TextFB::Width(&mem, text));
			const int cx = tr.CenterPoint().x, cy = tr.CenterPoint().y;
			mem.FillSolidRect(cx - tw / 2, cy, tw, 1, textColor);
		}
	};

	// 왼쪽: 기존 태그
	{
		const int saved = mem.SaveDC();
		mem.IntersectClipRect(colL.left, colL.top, colL.right + 1, colL.bottom);
		for (size_t p = 0; p < m_left.size(); ++p)
		{
			const CRect r = LeftChipRect(static_cast<int>(p));
			if (r.bottom < colL.top || r.top > colL.bottom)
				continue;
			const int tag = m_left[p];
			const bool hot = (m_hotSide < 0 && m_hotIndex == tag) || (m_dragSide < 0 && m_dragIndex == tag);
			const bool linked = linkedTag[tag];
			const bool keep = tag < static_cast<int>(m_keep.size()) && m_keep[tag];
			if (linked)
				drawChip(r, m_all[tag], TCOL(TC_COMBO), hot ? TCOL(TC_LIGHT) : accent, TCOL(TC_TEXT), 2, false);         // 연결됨 = 유지
			else if (keep)
				drawChip(r, m_all[tag], TCOL(TC_EDIT), hot ? TCOL(TC_LIGHT) : TCOL(TC_BORDER), TCOL(TC_TEXT), hot ? 2 : 1, false);   // 유지
			else
				drawChip(r, m_all[tag], TCOL(TC_BACK), hot ? TCOL(TC_LIGHT) : TCOL(TC_DANGER), TCOL(TC_DISABLED), 1, true);   // 지움 (빨간 테두리 · 취소선)
		}
		mem.RestoreDC(saved);
	}
	// 오른쪽: 붙여넣은 태그 (연결됨 / 새 태그 / 무시)
	{
		const int saved = mem.SaveDC();
		mem.IntersectClipRect(colR.left - 1, colR.top, colR.right + 1, colR.bottom);
		for (size_t i = 0; i < m_web.size(); ++i)
		{
			const CRect r = RightChipRect(static_cast<int>(i));
			if (r.bottom < colR.top || r.top > colR.bottom)
				continue;
			const WebTag& w = m_web[i];
			const bool hot = (m_hotSide > 0 && m_hotIndex == static_cast<int>(i)) || (m_dragSide > 0 && m_dragIndex == static_cast<int>(i));
			if (w.link >= 0)
				drawChip(r, w.text, TCOL(TC_COMBO), hot ? TCOL(TC_LIGHT) : accent, TCOL(TC_TEXT), 2, false);
			else if (w.addNew)
				drawChip(r, L"+ " + w.text, TCOL(TC_EDIT), hot ? TCOL(TC_LIGHT) : TCOL(TC_SAVE), TCOL(TC_TEXT), 2, false);
			else
				drawChip(r, w.text, TCOL(TC_BACK), hot ? TCOL(TC_LIGHT) : TCOL(TC_BORDER), TCOL(TC_DISABLED), 1, true);
		}
		mem.RestoreDC(saved);
	}
	// 스크롤 표시 (내용이 열보다 길면 열 바깥쪽에 얇은 막대)
	auto drawScroll = [&](const CRect& col, int contentH, int scroll, int x)
	{
		const int h = col.Height();
		if (contentH <= h || h <= 0)
			return;
		const int barH = (std::max)(16, h * h / contentH);
		const int y = col.top + (h - barH) * scroll / (std::max)(1, contentH - h);
		mem.FillSolidRect(x, col.top, 3, h, TCOL(TC_SCROLL));
		mem.FillSolidRect(x, y, 3, barH, TCOL(TC_SCROLLTHUMB));
	};
	drawScroll(colL, ContentH(m_left.size()), m_scrollL, colL.right + 3);   // 기존 태그 (오른쪽 열) 바깥
	drawScroll(colR, ContentH(m_web.size()), m_scrollR, colR.left - 6);     // 붙여넣은 태그 (왼쪽 열) 바깥

	// 연결 화살표 + 끄는 중인 선 (GDI+ 부드럽게)
	{
		const int saved = mem.SaveDC();
		mem.IntersectClipRect(0, m_headH, rc.right, rc.bottom);
		EnsureGdiPlusTL();
		Gdiplus::Graphics g(mem.GetSafeHdc());
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		const Gdiplus::Color ac(255, GetRValue(accent), GetGValue(accent), GetBValue(accent));
		Gdiplus::Pen pen(ac, 1.6f);
		Gdiplus::SolidBrush head(ac);
		const float ah = static_cast<float>((std::max)(7, m_chipH / 3));   // 화살촉 길이
		auto arrow = [&](Gdiplus::Pen& p, Gdiplus::Brush& b, CPoint a, CPoint e)
		{
			const float dx = static_cast<float>(e.x - a.x), dy = static_cast<float>(e.y - a.y);
			const float len = std::sqrt(dx * dx + dy * dy);
			if (len < 1.0f)
				return;
			const float ux = dx / len, uy = dy / len;
			const Gdiplus::PointF tip(static_cast<float>(e.x), static_cast<float>(e.y));
			const Gdiplus::PointF base(tip.X - ux * ah, tip.Y - uy * ah);
			g.DrawLine(&p, static_cast<float>(a.x), static_cast<float>(a.y), base.X, base.Y);
			const Gdiplus::PointF pts[3] = { tip, Gdiplus::PointF(base.X - uy * ah * 0.5f, base.Y + ux * ah * 0.5f), Gdiplus::PointF(base.X + uy * ah * 0.5f, base.Y - ux * ah * 0.5f) };
			g.FillPolygon(&b, pts, 3);
		};
		for (size_t i = 0; i < m_web.size(); ++i)
		{
			CPoint a, b;
			if (ArrowEnds(static_cast<int>(i), a, b))
				arrow(pen, head, a, b);
		}
		if (m_dragging && m_dragSide != 0)
		{
			Gdiplus::Pen dash(Gdiplus::Color(200, 255, 255, 255), 1.4f);
			dash.SetDashStyle(Gdiplus::DashStyleDash);
			CPoint from;
			if (m_dragSide < 0)
			{
				const int pos = LeftPosOf(m_dragIndex);
				from = pos >= 0 ? CPoint(LeftChipRect(pos).left, LeftChipRect(pos).CenterPoint().y) : m_dragStart;
			}
			else
				from = CPoint(RightChipRect(m_dragIndex).right, RightChipRect(m_dragIndex).CenterPoint().y);
			g.DrawLine(&dash, static_cast<float>(from.x), static_cast<float>(from.y), static_cast<float>(m_dragPt.x), static_cast<float>(m_dragPt.y));
		}
		mem.RestoreDC(saved);
	}

	mem.SelectObject(oldFont);
	paint.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(oldBmp);
}

void CTagLinkView::OnLButtonDown(UINT /*nFlags*/, CPoint point)
{
	SetFocus();
	const int l = LeftAt(point);
	const int r = (l < 0) ? RightAt(point) : -1;
	if (l >= 0 || r >= 0)
	{
		m_dragSide = (l >= 0) ? -1 : 1;
		m_dragIndex = (l >= 0) ? l : r;
		m_dragStart = m_dragPt = point;
		m_dragging = false;
		SetCapture();
		Invalidate(FALSE);
		return;
	}
	// 화살표 클릭 = 연결 끊기
	const int a = ArrowAt(point);
	if (a >= 0)
	{
		m_web[a].link = -1;
		m_web[a].addNew = true;   // 연결을 끊으면 추가 상태로
		Rebuild();
	}
}

void CTagLinkView::OnMouseMove(UINT /*nFlags*/, CPoint point)
{
	if (!m_tracking)
	{
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, GetSafeHwnd(), 0 };
		m_tracking = ::TrackMouseEvent(&tme) != FALSE;
	}
	if (m_dragSide != 0)
	{
		if (!m_dragging && (std::abs(point.x - m_dragStart.x) > 4 || std::abs(point.y - m_dragStart.y) > 4))
			m_dragging = true;
		m_dragPt = point;
	}
	// 마우스가 올라간 칩 (끄는 중이면 놓을 곳)
	int side = 0, index = LeftAt(point);
	if (index >= 0)
		side = -1;
	else if ((index = RightAt(point)) >= 0)
		side = 1;
	if (side != m_hotSide || index != m_hotIndex || m_dragging)
	{
		m_hotSide = side;
		m_hotIndex = index;
		Invalidate(FALSE);
	}
}

BOOL CTagLinkView::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (pWnd == this && nHitTest == HTCLIENT)
	{
		CPoint pt;
		::GetCursorPos(&pt);
		ScreenToClient(&pt);
		const bool hand = LeftAt(pt) >= 0 || RightAt(pt) >= 0 || ArrowAt(pt) >= 0;   // 칩 · 화살표 위는 손 모양
		::SetCursor(::LoadCursor(nullptr, hand ? IDC_HAND : IDC_ARROW));
		return TRUE;
	}
	return CWnd::OnSetCursor(pWnd, nHitTest, message);
}

void CTagLinkView::OnLButtonUp(UINT /*nFlags*/, CPoint point)
{
	if (m_dragSide == 0)
		return;
	const int side = m_dragSide, index = m_dragIndex;
	const bool dragged = m_dragging;
	m_dragSide = 0;
	m_dragIndex = -1;
	m_dragging = false;
	if (GetCapture() == this)
		ReleaseCapture();
	if (dragged)
	{
		// 반대쪽 칩 위에 놓으면 연결
		if (side < 0)
		{
			const int r = RightAt(point);
			if (r >= 0)
				Link(index, r);
		}
		else
		{
			const int l = LeftAt(point);
			if (l >= 0)
				Link(l, index);
		}
	}
	else if (side > 0 && index >= 0 && index < static_cast<int>(m_web.size()) && m_web[index].link < 0)
		m_web[index].addNew = !m_web[index].addNew;   // 연결 안 된 붙여넣은 태그 클릭: 추가 ↔ 무시
	else if (side < 0 && index >= 0 && index < static_cast<int>(m_keep.size()))
	{
		bool linked = false;
		for (const WebTag& w : m_web)
			if (w.link == index) { linked = true; break; }
		if (!linked)
			m_keep[index] = !m_keep[index];   // 연결 안 된 현재 영상 태그 클릭: 지움 ↔ 유지
	}
	Invalidate(FALSE);
}

void CTagLinkView::OnRButtonDown(UINT /*nFlags*/, CPoint point)
{
	// 붙여넣은 태그 칩(왼쪽 열) 오른쪽 클릭 = 연결 끊기 (기존 태그 칩(오른쪽 열)이면 그 태그에 연결된 것 모두)
	const int r = RightAt(point);
	if (r >= 0)
	{
		if (m_web[r].link >= 0)
		{
			m_web[r].link = -1;
			m_web[r].addNew = true;   // 연결을 끊으면 추가 상태로
			Rebuild();
		}
		return;
	}
	const int l = LeftAt(point);
	if (l >= 0)
	{
		bool any = false;
		for (WebTag& w : m_web)
			if (w.link == l) { w.link = -1; w.addNew = true; any = true; }
		if (any)
			Rebuild();
	}
}

BOOL CTagLinkView::OnMouseWheel(UINT /*nFlags*/, short zDelta, CPoint pt)
{
	// 마우스가 있는 열만 스크롤 (가운데면 왼쪽)
	ScreenToClient(&pt);
	const int step = (m_chipH + m_gap) * 2 * zDelta / WHEEL_DELTA;
	if (pt.x <= ColumnRect(1).right + m_pad)   // 붙여넣은 태그 열 (왼쪽)
		m_scrollR -= step;
	else
		m_scrollL -= step;
	ClampScroll();
	Invalidate(FALSE);
	return TRUE;
}

LRESULT CTagLinkView::OnMouseLeave(WPARAM, LPARAM)
{
	m_tracking = false;
	if (m_hotSide != 0)
	{
		m_hotSide = 0;
		m_hotIndex = -1;
		Invalidate(FALSE);
	}
	return 0;
}

void CTagLinkView::OnCaptureChanged(CWnd* pWnd)
{
	if (pWnd != this && m_dragSide != 0)
	{
		m_dragSide = 0;
		m_dragIndex = -1;
		m_dragging = false;
		Invalidate(FALSE);
	}
	CWnd::OnCaptureChanged(pWnd);
}

// ---------------------------------------------------------------------------
// CTagLinkDlg

BEGIN_MESSAGE_MAP(CTagLinkDlg, CDialogEx)
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

CTagLinkDlg::CTagLinkDlg(const std::vector<CString>& curTags, const std::vector<CString>& webTags, const std::vector<CString>& autoLink, const std::vector<bool>& addDefault, CWnd* pParent)
	: CDialogEx(IDD_TAG_LINK, pParent)
{
	m_view.m_all = curTags;   // 현재 영상 태그 (영상에 적힌 순서)
	m_view.m_keep.assign(curTags.size(), false);   // 연결 안 된 현재 태그는 처음에 무조건 지움
	for (size_t i = 0; i < webTags.size(); ++i)
	{
		CTagLinkView::WebTag w;
		w.text = webTags[i];
		w.addNew = (i < addDefault.size()) && addDefault[i];   // 연결 안 된 태그는 처음부터 추가
		const CString target = (i < autoLink.size()) ? autoLink[i] : CString();
		if (!target.IsEmpty())
			for (size_t t = 0; t < m_view.m_all.size(); ++t)
				if (m_view.m_all[t].CompareNoCase(target) == 0) { w.link = static_cast<int>(t); break; }
		m_view.m_web.push_back(w);
	}
}

BOOL CTagLinkDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	m_theme.Apply(this);
	// 다른 용도로 쓸 때의 제목 · 안내 · 열 머리글
	if (!m_caption.IsEmpty())
		SetWindowText(m_caption);
	if (!m_guide.IsEmpty())
		SetDlgItemText(IDC_TL_GUIDE, m_guide);
	if (!m_headCur.IsEmpty())
		m_view.m_headCur = m_headCur;
	if (!m_headWeb.IsEmpty())
		m_view.m_headWeb = m_headWeb;
	if (m_keepDefault.size() == m_view.m_keep.size())
		m_view.m_keep = m_keepDefault;
	// 자리 표시 칸을 연결 화면으로 교체
	CRect rc(7, 40, 413, 272);
	if (CWnd* holder = GetDlgItem(IDC_TL_VIEW))
	{
		holder->GetWindowRect(&rc);
		ScreenToClient(&rc);
		holder->DestroyWindow();
	}
	m_view.Create(this, rc, IDC_TL_VIEW);
	return TRUE;
}

void CTagLinkDlg::OnOK()
{
	m_result.clear();
	for (const CTagLinkView::WebTag& w : m_view.m_web)
	{
		Result r;
		r.web = w.text;
		if (w.link >= 0 && w.link < static_cast<int>(m_view.m_all.size()))
			r.linked = m_view.m_all[w.link];
		r.addNew = (w.link < 0) && w.addNew;
		m_result.push_back(r);
	}
	// 남길 현재 영상 태그: 연결된 것 + 유지로 바꾼 것
	m_keepTags.clear();
	for (size_t i = 0; i < m_view.m_all.size(); ++i)
	{
		bool keep = i < m_view.m_keep.size() && m_view.m_keep[i];
		for (const CTagLinkView::WebTag& w : m_view.m_web)
			if (w.link == static_cast<int>(i)) { keep = true; break; }
		if (keep)
			m_keepTags.push_back(m_view.m_all[i]);
	}
	CDialogEx::OnOK();
}

HBRUSH CTagLinkDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
		return br;
	return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}
