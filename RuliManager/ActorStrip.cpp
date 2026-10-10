#include "pch.h"
#include "ActorStrip.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CActorStrip, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_MOUSEWHEEL()
	ON_WM_MOUSEHWHEEL()
	ON_WM_MOUSEMOVE()
	ON_MESSAGE(WM_MOUSELEAVE, &CActorStrip::OnMouseLeave)
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_CAPTURECHANGED()
END_MESSAGE_MAP()

bool CActorStrip::Create(CWnd* parent, UINT id)
{
	const CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), nullptr);
	if (!CWnd::Create(cls, L"", WS_CHILD, CRect(0, 0, 100, 100), parent, id))
		return false;
	if (m_tip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX))
	{
		m_tip.AddTool(this, L"");
		m_tip.SetMaxTipWidth(600);
		m_tip.Activate(TRUE);
	}
	return true;
}

void CActorStrip::SetColors(COLORREF back, COLORREF text, COLORREF bar, COLORREF thumb)
{
	m_back = back;
	m_text = text;
	m_bar = bar;
	m_thumb = thumb;
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

void CActorStrip::SetCardSize(int w, int h, int gap)
{
	m_cardW = (std::max)(10, w);
	m_cardH = (std::max)(10, h);
	m_gap = (std::max)(0, gap);
	m_barH = (std::max)(6, gap + 1);
	SetScroll(m_scroll);
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

void CActorStrip::SetCount(int count)
{
	if (count != m_count)
	{
		m_scroll = 0;
		m_vscroll = 0;
	}
	m_count = (std::max)(0, count);
	if (m_hot >= m_count)
	{
		m_hot = -1;
		m_hotPart = 0;
	}
	m_tipRow = -1;
	SetScroll(m_scroll);
	SetVScroll(m_vscroll);
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

bool CActorStrip::NeedVBar() const
{
	if (!m_wrap || !GetSafeHwnd() || m_count <= 0)
		return false;
	CRect rc;
	GetClientRect(&rc);
	return CalcWrapHeight(rc.Width()) > rc.Height() + 1;
}

int CActorStrip::WrapWidth() const
{
	CRect rc;
	if (GetSafeHwnd()) GetClientRect(&rc);
	return (std::max)(1, rc.Width() - (NeedVBar() ? m_barH + 3 : 0));
}

int CActorStrip::MaxVScroll() const
{
	if (!NeedVBar())
		return 0;
	CRect rc;
	GetClientRect(&rc);
	return (std::max)(0, CalcWrapHeight(WrapWidth()) - rc.Height());
}

void CActorStrip::SetVScroll(int pos)
{
	pos = (std::max)(0, (std::min)(pos, MaxVScroll()));
	if (pos != m_vscroll)
	{
		m_vscroll = pos;
		if (GetSafeHwnd())
			Invalidate(FALSE);
	}
}

bool CActorStrip::VThumbRect(CRect& thumb) const
{
	if (!NeedVBar())
		return false;
	CRect rc;
	GetClientRect(&rc);
	const int content = CalcWrapHeight(WrapWidth());
	if (content <= rc.Height() || rc.Height() <= 0)
		return false;
	const int track = rc.Height();
	const int th = (std::max)(m_barH * 3, track * rc.Height() / content);
	const int maxS = content - rc.Height();
	const int ty = (track - th) * m_vscroll / (std::max)(1, maxS);
	thumb.SetRect(rc.right - m_barH, ty, rc.right, ty + th);
	return true;
}

int CActorStrip::CalcWrapHeight(int width) const
{
	if (m_count <= 0)
		return 0;
	const int rows = (m_count + PerRow(width) - 1) / PerRow(width);
	return rows * m_cardH + (rows - 1) * m_gap;
}

int CActorStrip::ContentWidth() const
{
	if (m_wrap)
		return 0;   // 여러 줄 모드: 가로 스크롤 없음
	return m_count > 0 ? m_count * m_cardW + (m_count - 1) * m_gap : 0;
}

int CActorStrip::MaxScroll() const
{
	if (!GetSafeHwnd())
		return 0;
	CRect rc;
	GetClientRect(&rc);
	return (std::max)(0, ContentWidth() - rc.Width());
}

void CActorStrip::SetScroll(int pos)
{
	pos = (std::max)(0, (std::min)(pos, MaxScroll()));
	if (pos != m_scroll)
	{
		m_scroll = pos;
		if (GetSafeHwnd())
			Invalidate(FALSE);
	}
}

CRect CActorStrip::CardRect(int i) const
{
	if (m_wrap)
	{
		const int per = PerRow(WrapWidth());   // 세로 스크롤바 자리 제외
		const int x = (i % per) * (m_cardW + m_gap);
		const int y = (i / per) * (m_cardH + m_gap) - m_vscroll;
		return CRect(x, y, x + m_cardW, y + m_cardH);
	}
	const int x = i * (m_cardW + m_gap) - m_scroll;
	return CRect(x, 0, x + m_cardW, m_cardH);
}

bool CActorStrip::ThumbRect(CRect& thumb) const
{
	CRect rc;
	GetClientRect(&rc);
	const int content = ContentWidth();
	if (content <= rc.Width() || rc.Width() <= 0)
		return false;
	const int track = rc.Width();
	const int tw = (std::max)(m_barH * 3, track * rc.Width() / content);
	const int maxS = content - rc.Width();
	const int tx = (track - tw) * m_scroll / (std::max)(1, maxS);
	thumb.SetRect(tx, rc.bottom - m_barH, tx + tw, rc.bottom);
	return true;
}

int CActorStrip::HitTest(CPoint pt) const
{
	if (!m_wrap && (pt.y < 0 || pt.y >= m_cardH))
		return -1;
	for (int i = 0; i < m_count; ++i)
	{
		if (CardRect(i).PtInRect(pt))
			return i;
	}
	return -1;
}

int CActorStrip::PartAt(int i, CPoint pt) const
{
	if (i < 0 || !m_onHitPart)
		return 0;
	return m_onHitPart(i, CardRect(i), pt);
}

void CActorStrip::OnPaint()
{
	CPaintDC paint(this);
	CRect rc;
	GetClientRect(&rc);

	CDC mem;
	mem.CreateCompatibleDC(&paint);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&paint, (std::max)(1, rc.Width()), (std::max)(1, rc.Height()));
	CBitmap* oldBmp = mem.SelectObject(&bmp);
	mem.FillSolidRect(rc, m_back);

	if (m_count == 0)
	{
		if (!m_empty.IsEmpty())
		{
			CFont* old = mem.SelectObject(GetParent()->GetFont());
			mem.SetBkMode(TRANSPARENT);
			mem.SetTextColor(m_text);
			CRect tr(0, 0, rc.right, m_cardH);
			mem.DrawText(m_empty, tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			mem.SelectObject(old);
		}
	}
	else if (m_onDraw)
	{
		for (int i = 0; i < m_count; ++i)
		{
			const CRect card = CardRect(i);
			if (card.right < 0 || card.left > rc.right || card.bottom < 0 || card.top > rc.bottom)
				continue;
			m_onDraw(&mem, i, card, i == m_hot);
		}
	}

	CRect vthumb;
	if (VThumbRect(vthumb))
	{
		// 여러 줄 모드: 오른쪽 세로 스크롤바
		mem.FillSolidRect(CRect(rc.right - m_barH, 0, rc.right, rc.bottom), m_bar);
		vthumb.DeflateRect(1, 0);
		CBrush br(m_thumb);
		CPen pen(PS_SOLID, 1, m_thumb);
		CBrush* ob = mem.SelectObject(&br);
		CPen* op = mem.SelectObject(&pen);
		mem.RoundRect(vthumb, CPoint(vthumb.Width(), vthumb.Width()));
		mem.SelectObject(ob);
		mem.SelectObject(op);
	}

	CRect thumb;
	if (ThumbRect(thumb))
	{
		mem.FillSolidRect(CRect(0, rc.bottom - m_barH, rc.right, rc.bottom), m_bar);
		thumb.DeflateRect(0, 1);
		CBrush br(m_thumb);
		CPen pen(PS_SOLID, 1, m_thumb);
		CBrush* ob = mem.SelectObject(&br);
		CPen* op = mem.SelectObject(&pen);
		mem.RoundRect(thumb, CPoint(thumb.Height(), thumb.Height()));
		mem.SelectObject(ob);
		mem.SelectObject(op);
	}

	paint.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(oldBmp);
}

void CActorStrip::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	SetScroll(m_scroll);
	SetVScroll(m_vscroll);
	Invalidate(FALSE);
}

BOOL CActorStrip::OnMouseWheel(UINT, short zDelta, CPoint)
{
	if (m_wrap)
	{
		// 여러 줄 모드: 휠 한 칸 = 카드 한 줄
		if (MaxVScroll() <= 0)
			return FALSE;
		SetVScroll(m_vscroll - zDelta * (m_cardH + m_gap) / WHEEL_DELTA);
		return TRUE;
	}
	if (MaxScroll() <= 0)
		return FALSE;
	SetScroll(m_scroll - zDelta * (m_cardW + m_gap) / WHEEL_DELTA);
	return TRUE;
}

void CActorStrip::OnMouseHWheel(UINT, short zDelta, CPoint)
{
	SetScroll(m_scroll + zDelta * (m_cardW + m_gap) / WHEEL_DELTA);
}

void CActorStrip::OnMouseMove(UINT nFlags, CPoint pt)
{
	if (m_dragVBar)
	{
		CRect rc, thumb;
		GetClientRect(&rc);
		if (VThumbRect(thumb))
		{
			const int track = rc.Height() - thumb.Height();
			if (track > 0)
				SetVScroll(m_dragStartVScroll + (pt.y - m_dragStartY) * MaxVScroll() / track);
		}
		return;
	}
	if (m_dragBar)
	{
		CRect rc, thumb;
		GetClientRect(&rc);
		if (ThumbRect(thumb))
		{
			const int track = rc.Width() - thumb.Width();
			if (track > 0)
				SetScroll(m_dragStartScroll + (pt.x - m_dragStartX) * MaxScroll() / track);
		}
		return;
	}
	if (!m_tracking)
	{
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
		m_tracking = ::TrackMouseEvent(&tme) != FALSE;
	}
	const int hot = HitTest(pt);
	const int part = PartAt(hot, pt);
	if (hot != m_hot || part != m_hotPart)
	{
		m_hot = hot;
		m_hotPart = part;
		Invalidate(FALSE);
	}
	if (m_tip.GetSafeHwnd() && hot != m_tipRow)
	{
		m_tipRow = hot;
		const CString text = (hot >= 0 && m_onTip) ? m_onTip(hot) : CString();
		m_tip.UpdateTipText(text.IsEmpty() ? L"" : static_cast<LPCWSTR>(text), this);
		if (text.IsEmpty())
			m_tip.Pop();
	}
	CWnd::OnMouseMove(nFlags, pt);
}

LRESULT CActorStrip::OnMouseLeave(WPARAM, LPARAM)
{
	m_tracking = false;
	m_tipRow = -1;
	if (m_hot >= 0)
	{
		m_hot = -1;
		m_hotPart = 0;
		Invalidate(FALSE);
	}
	return 0;
}

void CActorStrip::OnLButtonDown(UINT nFlags, CPoint pt)
{
	CRect rc, thumb;
	GetClientRect(&rc);
	if (VThumbRect(thumb) && pt.x >= rc.right - m_barH)
	{
		// 세로 스크롤바: 끌기 / 트랙 클릭 (한 화면씩)
		if (pt.y >= thumb.top && pt.y < thumb.bottom)
		{
			m_dragVBar = true;
			m_dragStartY = pt.y;
			m_dragStartVScroll = m_vscroll;
			SetCapture();
		}
		else
			SetVScroll(m_vscroll + (pt.y < thumb.top ? -rc.Height() : rc.Height()));
		return;
	}
	if (ThumbRect(thumb) && pt.y >= rc.bottom - m_barH)
	{
		if (thumb.PtInRect(pt) || (pt.x >= thumb.left && pt.x < thumb.right))
		{
			m_dragBar = true;
			m_dragStartX = pt.x;
			m_dragStartScroll = m_scroll;
			SetCapture();
		}
		else
		{
			// 트랙 클릭: 한 화면씩 이동
			SetScroll(m_scroll + (pt.x < thumb.left ? -rc.Width() : rc.Width()));
		}
		return;
	}
	// 카드 위 버튼 (즐겨찾기 하트 등)
	const int i = HitTest(pt);
	const int part = PartAt(i, pt);
	if (part > 0 && m_onPartClick)
	{
		m_onPartClick(i, part);
		return;
	}
	CWnd::OnLButtonDown(nFlags, pt);
}

void CActorStrip::OnLButtonUp(UINT nFlags, CPoint pt)
{
	if (m_dragBar || m_dragVBar)
	{
		m_dragBar = false;
		m_dragVBar = false;
		ReleaseCapture();
	}
	CWnd::OnLButtonUp(nFlags, pt);
}

void CActorStrip::OnCaptureChanged(CWnd* pWnd)
{
	m_dragBar = false;
	m_dragVBar = false;
	CWnd::OnCaptureChanged(pWnd);
}

void CActorStrip::OnLButtonDblClk(UINT nFlags, CPoint pt)
{
	const int i = HitTest(pt);
	const int part = PartAt(i, pt);
	if (part > 0 && m_onPartClick)
	{
		m_onPartClick(i, part);   // 버튼을 빠르게 두 번 누름 = 두 번 클릭 (더블클릭 동작 안 함)
		return;
	}
	if (i >= 0 && m_onActivate)
	{
		m_onActivate(i);
		return;
	}
	CWnd::OnLButtonDblClk(nFlags, pt);
}

BOOL CActorStrip::PreTranslateMessage(MSG* pMsg)
{
	if (m_tip.GetSafeHwnd())
		m_tip.RelayEvent(pMsg);
	return CWnd::PreTranslateMessage(pMsg);
}

// ---------------------------------------------------------------------------
// CCardPopup

BEGIN_MESSAGE_MAP(CCardPopup, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_NCHITTEST()
	ON_WM_MOUSEACTIVATE()
END_MESSAGE_MAP()

bool CCardPopup::CreatePopup(CWnd* owner)
{
	const CString cls = AfxRegisterWndClass(CS_DROPSHADOW, ::LoadCursor(nullptr, IDC_ARROW), nullptr);
	return CreateEx(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, cls, L"", WS_POPUP,
		CRect(0, 0, 10, 10), owner, 0) != FALSE;
}

void CCardPopup::ShowNear(const CRect& anchor, CSize size)
{
	if (!GetSafeHwnd())
		return;
	// 모니터 작업 영역 안에서: 기준 영역 위쪽(가운데 정렬), 공간이 없으면 아래쪽
	MONITORINFO mi = { sizeof(mi) };
	CRect work(0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN));
	if (::GetMonitorInfoW(::MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &mi))
		work = mi.rcWork;
	const int gap = 6;
	int x = anchor.left + (anchor.Width() - size.cx) / 2;
	int y = anchor.top - gap - size.cy;
	if (y < work.top)
		y = anchor.bottom + gap;
	const int cw = static_cast<int>(size.cx), ch = static_cast<int>(size.cy);   // LONG → int (std::min / max 형 맞춤)
	x = (std::max)(static_cast<int>(work.left), (std::min)(x, static_cast<int>(work.right) - cw));
	y = (std::max)(static_cast<int>(work.top), (std::min)(y, static_cast<int>(work.bottom) - ch));
	SetWindowPos(&CWnd::wndTopMost, x, y, size.cx, size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	Invalidate(FALSE);
}

void CCardPopup::Hide()
{
	if (GetSafeHwnd() && IsWindowVisible())
		ShowWindow(SW_HIDE);
}

void CCardPopup::OnPaint()
{
	CPaintDC paint(this);
	CRect rc;
	GetClientRect(&rc);
	CDC mem;
	mem.CreateCompatibleDC(&paint);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&paint, (std::max)(1, rc.Width()), (std::max)(1, rc.Height()));
	CBitmap* old = mem.SelectObject(&bmp);
	mem.FillSolidRect(rc, m_back);
	if (m_onDraw)
		m_onDraw(&mem, rc);
	paint.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(old);
}

