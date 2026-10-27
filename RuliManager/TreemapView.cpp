#include "pch.h"
#include "TreemapView.h"
#include "TextDraw.h"

#include <algorithm>
#include <cmath>
#include <memory>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	COLORREF Mix(COLORREF a, COLORREF b, double t)   // a → b 로 t 만큼
	{
		auto ch = [t](int x, int y) { return static_cast<int>(x + (y - x) * t + 0.5); };
		return RGB(ch(GetRValue(a), GetRValue(b)), ch(GetGValue(a), GetGValue(b)), ch(GetBValue(a), GetBValue(b)));
	}

	CString Percent(double v, double total)
	{
		CString s;
		if (total <= 0)
			return s;
		const double p = v * 100.0 / total;
		if (p >= 10.0)      s.Format(L"%.0f%%", p);
		else if (p >= 0.1)  s.Format(L"%.1f%%", p);
		else                s = L"<0.1%";
		return s;
	}
}

BEGIN_MESSAGE_MAP(CTreemapView, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_MOUSEMOVE()
	ON_MESSAGE(WM_MOUSELEAVE, &CTreemapView::OnMouseLeave)
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_RBUTTONUP()
	ON_WM_SETCURSOR()
END_MESSAGE_MAP()

BOOL CTreemapView::Create(CWnd* parent, UINT id)
{
	const CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
	if (!CWnd::CreateEx(0, cls, L"", WS_CHILD | WS_CLIPSIBLINGS, CRect(0, 0, 10, 10), parent, id))
		return FALSE;
	if (m_tip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX))
	{
		m_tip.AddTool(this, L"");
		m_tip.SetMaxTipWidth(600);
		m_tip.SetDelayTime(TTDT_INITIAL, 300);
		m_tip.SetDelayTime(TTDT_AUTOPOP, 30000);
		m_tip.Activate(TRUE);
	}
	return TRUE;
}

void CTreemapView::SetColors(COLORREF back, COLORREF text)
{
	m_back = back;
	m_text = text;
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

void CTreemapView::SetItems(std::vector<Item> items, double total)
{
	items.erase(std::remove_if(items.begin(), items.end(), [](const Item& it) { return !(it.value > 0); }), items.end());
	std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b)
	{
		// 큰 값 먼저, "기타"(tag -1) 는 늘 맨 끝
		if ((a.tag == -1) != (b.tag == -1))
			return b.tag == -1;
		return a.value > b.value;
	});
	double sum = 0;
	for (const Item& it : items)
		sum += it.value;
	m_items = std::move(items);
	m_total = (total > 0) ? total : sum;
	m_hot = -1;
	Relayout();
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

void CTreemapView::SetSelectedTag(int tag)
{
	if (m_selTag == tag)
		return;
	m_selTag = tag;
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

void CTreemapView::Relayout()
{
	m_rects.assign(m_items.size(), CRect());
	if (!GetSafeHwnd() || m_items.empty())
		return;
	CRect client;
	GetClientRect(&client);
	if (client.Width() <= 2 || client.Height() <= 2)
		return;

	// squarified treemap: 짧은 변을 따라 한 줄씩, 줄의 가장 나쁜 가로세로 비율이 나빠지기 직전까지 담음
	double sum = 0;
	for (const Item& it : m_items)
		sum += it.value;
	if (!(sum > 0))
		return;
	const size_t n = m_items.size();
	const double scale = static_cast<double>(client.Width()) * client.Height() / sum;
	std::vector<double> a(n);
	for (size_t i = 0; i < n; ++i)
		a[i] = m_items[i].value * scale;   // 넓이 (픽셀²) - 큰 순서 ("기타" 는 끝이지만 계산에는 문제 없음)

	double rx = client.left, ry = client.top, rw = client.Width(), rh = client.Height();
	std::vector<double> fx(n), fy(n), fw(n), fh(n);
	size_t i = 0;
	while (i < n)
	{
		const double side = (std::max)(1.0, (std::min)(rw, rh));
		double rowSum = 0, best = 1e300, rmax = 0, rmin = 1e300;
		size_t j = i;
		while (j < n)
		{
			const double s = rowSum + a[j];
			const double mx = (std::max)(rmax, a[j]), mn = (std::min)(rmin, a[j]);
			const double worst = (std::max)(side * side * mx / (s * s), (s * s) / (side * side * (std::max)(1e-9, mn)));
			if (j > i && worst > best)
				break;
			best = worst;
			rowSum = s;
			rmax = mx;
			rmin = mn;
			++j;
		}
		if (rw >= rh)
		{
			// 왼쪽에 세로 한 줄
			const double colW = (j == n) ? rw : (std::min)(rw, rowSum / rh);
			double y = ry;
			for (size_t k = i; k < j; ++k)
			{
				const double h = (k + 1 == j) ? (ry + rh - y) : a[k] / colW;
				fx[k] = rx; fy[k] = y; fw[k] = colW; fh[k] = h;
				y += h;
			}
			rx += colW;
			rw -= colW;
		}
		else
		{
			// 위쪽에 가로 한 줄
			const double rowH = (j == n) ? rh : (std::min)(rh, rowSum / rw);
			double x = rx;
			for (size_t k = i; k < j; ++k)
			{
				const double w = (k + 1 == j) ? (rx + rw - x) : a[k] / rowH;
				fx[k] = x; fy[k] = ry; fw[k] = w; fh[k] = rowH;
				x += w;
			}
			ry += rowH;
			rh -= rowH;
		}
		i = j;
	}
	auto rnd = [](double v) { return static_cast<int>(std::floor(v + 0.5)); };
	for (size_t k = 0; k < n; ++k)
		m_rects[k] = CRect(rnd(fx[k]), rnd(fy[k]), rnd(fx[k] + fw[k]), rnd(fy[k] + fh[k]));   // 모서리를 반올림 → 이웃과 틈 · 겹침 없음
}

COLORREF CTreemapView::ColorFor(int rank, int tag)
{
	if (tag == -1)
		return RGB(72, 78, 86);   // 기타: 회색
	// 어두운 화면에 흰 글자가 잘 보이는 차분한 색 (순서대로 돌려 씀)
	static const COLORREF pal[] = {
		RGB(47, 98, 160),  RGB(54, 130, 92),  RGB(168, 96, 44),  RGB(122, 76, 152), RGB(38, 128, 138),
		RGB(160, 70, 92),  RGB(112, 116, 48), RGB(82, 88, 168),  RGB(140, 104, 56), RGB(58, 116, 150),
		RGB(150, 82, 132), RGB(76, 126, 70)
	};
	return pal[rank % _countof(pal)];
}

int CTreemapView::HitTest(CPoint pt) const
{
	for (size_t i = 0; i < m_rects.size(); ++i)
		if (m_rects[i].PtInRect(pt))
			return static_cast<int>(i);
	return -1;
}

CString CTreemapView::TipFor(int i) const
{
	if (i < 0 || i >= static_cast<int>(m_items.size()))
		return CString();
	const Item& it = m_items[i];
	CString v;
	if (m_formatValue)
		v = m_formatValue(it.value);
	else
		v.Format(L"%.0f", it.value);
	CString names = it.label;
	for (const CString* s : { &it.sub1, &it.sub2 })
		if (!s->IsEmpty())
			names += L"\r\n" + *s;
	return names + L"\r\n" + v + L"  (" + Percent(it.value, m_total) + L")";
}

int CTreemapView::BasePx(CFont* base)
{
	LOGFONT lf = {};
	if (!base || !base->GetLogFont(&lf))
		return 12;
	return (std::max)(6, static_cast<int>(lf.lfHeight < 0 ? -lf.lfHeight : lf.lfHeight));
}

CFont* CTreemapView::SizedFont(CFont* base, int px, bool bold)
{
	// 크기별 글꼴 (기본 글꼴과 같은 이름, 글자 높이 px) - 만든 것은 보관해서 다시 씀 (기본 글꼴이 바뀌면 비움)
	const HFONT hb = base ? static_cast<HFONT>(base->GetSafeHandle()) : nullptr;
	if (hb != m_sizedBase)
	{
		m_sizedFonts.clear();
		m_sizedBase = hb;
	}
	const int key = px * 2 + (bold ? 1 : 0);
	auto it = m_sizedFonts.find(key);
	if (it != m_sizedFonts.end())
		return it->second.get();
	LOGFONT lf = {};
	if (!base || !base->GetLogFont(&lf))
		return base;
	lf.lfHeight = -px;
	lf.lfWidth = 0;
	lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
	auto f = std::make_unique<CFont>();
	if (!f->CreateFontIndirect(&lf))
		return base;
	CFont* raw = f.get();
	m_sizedFonts[key] = std::move(f);
	return raw;
}

void CTreemapView::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	Relayout();
	Invalidate(FALSE);
}

void CTreemapView::OnPaint()
{
	CPaintDC pdc(this);
	CRect client;
	GetClientRect(&client);
	if (client.IsRectEmpty())
		return;
	CDC mem;
	mem.CreateCompatibleDC(&pdc);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&pdc, client.Width(), client.Height());
	CBitmap* oldBmp = mem.SelectObject(&bmp);
	mem.FillSolidRect(client, m_back);
	mem.SetBkMode(TRANSPARENT);
	if (!m_font && !m_ownFont.GetSafeHandle())
	{
		// 글꼴을 받지 못했으면 대화상자 글꼴 + 1pt (이름은 굵게)
		CFont* base = GetTopLevelParent() ? GetTopLevelParent()->GetFont() : nullptr;
		LOGFONT lf = {};
		if (base && base->GetLogFont(&lf))
		{
			lf.lfHeight = (lf.lfHeight < 0) ? lf.lfHeight - 2 : lf.lfHeight + 2;
			m_ownFont.CreateFontIndirect(&lf);
			lf.lfWeight = FW_BOLD;
			m_ownBold.CreateFontIndirect(&lf);
		}
	}
	CFont* normal = m_font ? m_font : (m_ownFont.GetSafeHandle() ? &m_ownFont : GetTopLevelParent()->GetFont());
	CFont* bold = m_boldFont ? m_boldFont : (m_ownBold.GetSafeHandle() ? &m_ownBold : normal);
	CFont* oldFont = mem.SelectObject(normal);

	if (m_items.empty())
	{
		mem.SetTextColor(Mix(m_text, m_back, 0.45));
		mem.DrawText(m_emptyText, -1, &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}

	TEXTMETRIC tm = {};
	mem.SelectObject(bold);
	mem.GetTextMetrics(&tm);
	const int lineH = tm.tmHeight;
	mem.SelectObject(normal);
	const int pad = (std::max)(3, lineH / 4);

	int rank = 0;
	for (size_t i = 0; i < m_items.size(); ++i)
	{
		const Item& it = m_items[i];
		CRect r = m_rects[i];
		r.DeflateRect(0, 0, 1, 1);   // 오른쪽 · 아래 1px = 칸 사이 틈 (배경색)
		if (r.Width() <= 0 || r.Height() <= 0)
		{
			if (it.tag != -1) ++rank;
			continue;
		}
		COLORREF col = ColorFor(rank, it.tag);
		if (it.tag != -1)
			++rank;
		if (static_cast<int>(i) == m_hot)
			col = Mix(col, RGB(255, 255, 255), 0.18);
		mem.FillSolidRect(r, col);
		if (it.tag == m_selTag && it.tag != -1)
		{
			// 선택한 배우: 흰 테두리 2px
			CRect s = r;
			for (int k = 0; k < 2 && s.Width() > 2 && s.Height() > 2; ++k)
			{
				mem.Draw3dRect(s, RGB(255, 255, 255), RGB(255, 255, 255));
				s.DeflateRect(1, 1);
			}
		}

		// 글자 (칸에 들어가는 만큼, 위에서부터 차례로 줄임)
		//  ① 이름(굵게) / 둘째 이름 / 셋째 이름 / 값 / 퍼센트
		//  ② 이름 / 값 / 퍼센트          (둘째 · 셋째 이름은 빼고)
		//  ③ 이름 / 퍼센트
		//  ④ 퍼센트만 → 그것도 안 들어가면 없음
		//  칸이 크면 글자도 크게: 들어가는 가장 큰 이름 크기 (최대 kMaxPx), 나머지 줄은 이름의 0.65배 (기본 크기보다 작아지지 않음)
		CRect tr = r;
		tr.DeflateRect(pad, pad);
		if (tr.Width() < lineH || tr.Height() < lineH)
			continue;
		const CString pct = Percent(it.value, m_total);
		const CString val = m_formatValue ? m_formatValue(it.value) : CString();
		const int basePx = BasePx(normal);
		const int maxPx = (std::max)(basePx, (std::min)({ static_cast<int>(tr.Width() / 2.2), static_cast<int>(tr.Height() / 2), static_cast<int>(kMaxPx) }));
		struct Line { CString text; bool name; double dim; };
		auto build = [&](int level) -> std::vector<Line>
		{
			std::vector<Line> v;
			v.push_back({ it.label, true, 0.0 });
			if (level == 0)
			{
				if (!it.sub1.IsEmpty()) v.push_back({ it.sub1, false, 0.3 });
				if (!it.sub2.IsEmpty()) v.push_back({ it.sub2, false, 0.3 });
			}
			if (level <= 1 && !val.IsEmpty())
				v.push_back({ val, false, 0.08 });
			v.push_back({ pct, false, 0.25 });
			return v;
		};
		// 이 크기로 들어가는지 (allowCut: 이름이 넘치면 "…" 허용 - 마지막 단계의 기본 크기에서만)
		auto fits = [&](const std::vector<Line>& ls, int px, bool allowCut, int& outH) -> bool
		{
			const int subPx = (std::max)(basePx, px * 65 / 100);
			outH = 0;
			for (size_t k = 0; k < ls.size(); ++k)
			{
				const Line& ln = ls[k];
				mem.SelectObject(SizedFont(normal, ln.name ? px : subPx, ln.name));
				TEXTMETRIC lt = {};
				mem.GetTextMetrics(&lt);
				outH += lt.tmHeight;
				if (outH > tr.Height())
					return false;
				if (TextFB::Width(&mem, ln.text) > tr.Width() && !(allowCut && ln.name))
					return false;
			}
			return true;
		};
		std::vector<Line> lines;
		int namePx = 0, totalH = 0;
		const bool hasSub = !it.sub1.IsEmpty() || !it.sub2.IsEmpty();
		for (int level = hasSub ? 0 : 1; level <= 2 && namePx == 0; ++level)
		{
			if (level == 1 && val.IsEmpty())
				continue;
			std::vector<Line> cand = build(level);
			std::vector<int> sizes;   // 큰 크기부터 2px 씩, 마지막은 기본 크기
			for (int px = maxPx; px > basePx; px -= 2)
				sizes.push_back(px);
			sizes.push_back(basePx);
			for (int px : sizes)
			{
				const bool cut = (level == 2 && px == basePx);   // 마지막 단계의 기본 크기: 이름이 넘치면 "…"
				if (fits(cand, px, cut, totalH))
				{
					namePx = px;
					lines = std::move(cand);
					break;
				}
			}
		}
		if (namePx > 0)
		{
			const int subPx = (std::max)(basePx, namePx * 65 / 100);
			int y = tr.top + (tr.Height() - totalH) / 2;
			for (const Line& ln : lines)
			{
				mem.SelectObject(SizedFont(normal, ln.name ? namePx : subPx, ln.name));
				TEXTMETRIC lt = {};
				mem.GetTextMetrics(&lt);
				mem.SetTextColor(ln.dim > 0 ? Mix(m_text, col, ln.dim) : m_text);
				TextFB::Draw(&mem, ln.text, CRect(tr.left, y, tr.right, y + lt.tmHeight), DT_CENTER, ln.name);
				y += lt.tmHeight;
			}
		}
		else
		{
			// 퍼센트만
			mem.SelectObject(SizedFont(normal, basePx, false));
			if (TextFB::Width(&mem, pct) <= tr.Width())
			{
				mem.SetTextColor(m_text);
				TextFB::Draw(&mem, pct, tr, DT_CENTER, false);
			}
		}
	}

	mem.SelectObject(oldFont);
	pdc.BitBlt(0, 0, client.Width(), client.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(oldBmp);
}

void CTreemapView::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_tracking)
	{
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
		m_tracking = ::TrackMouseEvent(&tme) != FALSE;
	}
	const int hit = HitTest(point);
	if (hit != m_hot)
	{
		m_hot = hit;
		Invalidate(FALSE);
		if (m_tip.GetSafeHwnd())
		{
			m_tip.Pop();
			m_tipText = TipFor(hit);
			m_tip.UpdateTipText(m_tipText.IsEmpty() ? L"" : static_cast<LPCWSTR>(m_tipText), this);
		}
	}
	CWnd::OnMouseMove(nFlags, point);
}

LRESULT CTreemapView::OnMouseLeave(WPARAM, LPARAM)
{
	m_tracking = false;
	if (m_hot >= 0)
	{
		m_hot = -1;
		Invalidate(FALSE);
	}
	return 0;
}

void CTreemapView::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetFocus();
	const int hit = HitTest(point);
	if (hit >= 0 && m_onClick)
		m_onClick(m_items[hit].tag);
	CWnd::OnLButtonDown(nFlags, point);
}

void CTreemapView::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	const int hit = HitTest(point);
	if (hit >= 0 && m_onActivate)
		m_onActivate(m_items[hit].tag);
	CWnd::OnLButtonDblClk(nFlags, point);
}

void CTreemapView::OnRButtonUp(UINT nFlags, CPoint point)
{
	const int hit = HitTest(point);
	if (hit >= 0 && m_onContext)
	{
		CPoint sp = point;
		ClientToScreen(&sp);
		m_onContext(m_items[hit].tag, sp);
		return;
	}
	CWnd::OnRButtonUp(nFlags, point);
}

BOOL CTreemapView::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (nHitTest == HTCLIENT)
	{
		::SetCursor(::LoadCursor(nullptr, IDC_ARROW));
		return TRUE;
	}
	return CWnd::OnSetCursor(pWnd, nHitTest, message);
}

BOOL CTreemapView::PreTranslateMessage(MSG* pMsg)
{
	if (m_tip.GetSafeHwnd() && pMsg->hwnd == m_hWnd && pMsg->message >= WM_MOUSEFIRST && pMsg->message <= WM_MOUSELAST)
		m_tip.RelayEvent(pMsg);
	return CWnd::PreTranslateMessage(pMsg);
}
