#include "pch.h"
#include "ZoomSlider.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	void EnsureGdiPlus()
	{
		static ULONG_PTR token = 0;
		if (!token)
		{
			Gdiplus::GdiplusStartupInput input;
			Gdiplus::GdiplusStartup(&token, &input, nullptr);
		}
	}
}

BEGIN_MESSAGE_MAP(CZoomSlider, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEWHEEL()
	ON_WM_KEYDOWN()
	ON_WM_GETDLGCODE()
	ON_WM_SIZE()
END_MESSAGE_MAP()

bool CZoomSlider::Create(CWnd* parent, UINT id, int steps)
{
	m_steps = (std::max)(2, steps);
	const CString cls = AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_HAND), nullptr);
	return CWnd::Create(cls, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, CRect(0, 0, 80, 20), parent, id) != FALSE;
}

void CZoomSlider::SetColors(COLORREF back, COLORREF track, COLORREF thumb, COLORREF thumbBorder)
{
	m_back = back; m_track = track; m_thumb = thumb; m_thumbBorder = thumbBorder;
	if (GetSafeHwnd()) Invalidate(FALSE);
}

void CZoomSlider::SetPos(int pos)
{
	m_pos = (std::max)(0, (std::min)(m_steps - 1, pos));
	if (GetSafeHwnd()) Invalidate(FALSE);
}

int CZoomSlider::ThumbRadius() const
{
	CRect rc;
	GetClientRect(&rc);
	return (std::max)(4, rc.Height() * 36 / 100);
}

int CZoomSlider::PosToX(int pos) const
{
	CRect rc;
	GetClientRect(&rc);
	const int r = ThumbRadius();
	const int span = (std::max)(1, rc.Width() - r * 2 - 2);
	return rc.left + r + 1 + span * pos / (m_steps - 1);
}

int CZoomSlider::XToPos(int x) const
{
	CRect rc;
	GetClientRect(&rc);
	const int r = ThumbRadius();
	const int span = (std::max)(1, rc.Width() - r * 2 - 2);
	const double t = static_cast<double>(x - (rc.left + r + 1)) / span;
	const int pos = static_cast<int>(t * (m_steps - 1) + 0.5);   // 가까운 단계로
	return (std::max)(0, (std::min)(m_steps - 1, pos));
}

void CZoomSlider::ChangeTo(int pos)
{
	pos = (std::max)(0, (std::min)(m_steps - 1, pos));
	if (pos == m_pos)
		return;
	m_pos = pos;
	Invalidate(FALSE);
	UpdateWindow();
	if (m_onChanged)
		m_onChanged(m_pos);
}

void CZoomSlider::OnPaint()
{
	CPaintDC paint(this);
	CRect rc;
	GetClientRect(&rc);
	CDC mem;
	mem.CreateCompatibleDC(&paint);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&paint, rc.Width(), rc.Height());
	CBitmap* old = mem.SelectObject(&bmp);
	mem.FillSolidRect(rc, m_back);

	EnsureGdiPlus();
	{
		Gdiplus::Graphics g(mem.GetSafeHdc());
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		const int r = ThumbRadius();
		const float cy = rc.Height() / 2.0f;
		const float th = (std::max)(4.0f, rc.Height() * 0.28f);   // 트랙 두께

		// 트랙 (둥근 막대)
		Gdiplus::GraphicsPath track;
		const float x0 = static_cast<float>(r / 2), x1 = static_cast<float>(rc.Width() - r / 2 - 1);
		track.AddArc(x0, cy - th / 2, th, th, 90.0f, 180.0f);
		track.AddArc(x1 - th, cy - th / 2, th, th, 270.0f, 180.0f);
		track.CloseFigure();
		Gdiplus::SolidBrush trackBrush(Gdiplus::Color(255, GetRValue(m_track), GetGValue(m_track), GetBValue(m_track)));
		g.FillPath(&trackBrush, &track);

		// 단계 눈금 (작은 점)
		Gdiplus::SolidBrush tick(Gdiplus::Color(110, 255, 255, 255));
		for (int i = 0; i < m_steps; ++i)
		{
			const float tx = static_cast<float>(PosToX(i));
			g.FillEllipse(&tick, tx - 1.5f, cy - 1.5f, 3.0f, 3.0f);
		}

		// 손잡이 (회색 동그라미)
		const float cx = static_cast<float>(PosToX(m_pos));
		Gdiplus::SolidBrush thumb(Gdiplus::Color(255, GetRValue(m_thumb), GetGValue(m_thumb), GetBValue(m_thumb)));
		Gdiplus::Pen border(Gdiplus::Color(255, GetRValue(m_thumbBorder), GetGValue(m_thumbBorder), GetBValue(m_thumbBorder)), 1.0f);
		g.FillEllipse(&thumb, cx - r, cy - r, r * 2.0f, r * 2.0f);
		g.DrawEllipse(&border, cx - r, cy - r, r * 2.0f, r * 2.0f);
	}
	if (GetFocus() == this)
	{
		CRect fr = rc;
		mem.DrawFocusRect(fr);
	}

	paint.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(old);
}

void CZoomSlider::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetFocus();
	m_dragging = true;
	SetCapture();
	ChangeTo(XToPos(point.x));
	CWnd::OnLButtonDown(nFlags, point);
}

void CZoomSlider::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_dragging)
		ChangeTo(XToPos(point.x));
	CWnd::OnMouseMove(nFlags, point);
}

void CZoomSlider::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_dragging)
	{
		m_dragging = false;
		ReleaseCapture();
	}
	CWnd::OnLButtonUp(nFlags, point);
}

BOOL CZoomSlider::OnMouseWheel(UINT, short zDelta, CPoint)
{
	ChangeTo(m_pos + (zDelta > 0 ? 1 : -1));
	return TRUE;
}

void CZoomSlider::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	switch (nChar)
	{
	case VK_LEFT:  case VK_DOWN: ChangeTo(m_pos - 1); return;
	case VK_RIGHT: case VK_UP:   ChangeTo(m_pos + 1); return;
	case VK_HOME:  ChangeTo(0); return;
	case VK_END:   ChangeTo(m_steps - 1); return;
	}
	CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

UINT CZoomSlider::OnGetDlgCode()
{
	return DLGC_WANTARROWS;
}

void CZoomSlider::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	Invalidate(FALSE);
}
