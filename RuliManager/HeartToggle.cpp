#include "pch.h"
#include "HeartToggle.h"
#include "VectorIcons.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CHeartToggle, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_MESSAGE(WM_MOUSELEAVE, &CHeartToggle::OnMouseLeave)
	ON_WM_ENABLE()
END_MESSAGE_MAP()

bool CHeartToggle::Create(CWnd* parent, UINT id)
{
	const CString cls = AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_HAND), nullptr);
	return CWnd::Create(cls, L"", WS_CHILD, CRect(0, 0, 16, 16), parent, id) != FALSE;
}

void CHeartToggle::SetOn(bool on)
{
	if (m_on == on)
		return;
	m_on = on;
	if (GetSafeHwnd()) Invalidate(FALSE);
}

void CHeartToggle::SetColors(COLORREF back, COLORREF on, COLORREF off, COLORREF offHover)
{
	m_back = back; m_onColor = on; m_offColor = off; m_offHover = offHover;
	if (GetSafeHwnd()) Invalidate(FALSE);
}

void CHeartToggle::OnPaint()
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

	COLORREF col = m_on ? m_onColor : (m_hover ? m_offHover : m_offColor);
	if (!IsWindowEnabled())
		col = RGB(0x50, 0x5C, 0x66);
	VectorIcon::Heart(&mem, rc, col, 1.0);

	paint.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(old);
}

void CHeartToggle::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (m_onClick)
		m_onClick();
	CWnd::OnLButtonDown(nFlags, point);
}

void CHeartToggle::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_tracking)
	{
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, GetSafeHwnd(), 0 };
		m_tracking = ::TrackMouseEvent(&tme) != FALSE;
	}
	if (!m_hover)
	{
		m_hover = true;
		Invalidate(FALSE);
	}
	CWnd::OnMouseMove(nFlags, point);
}

LRESULT CHeartToggle::OnMouseLeave(WPARAM, LPARAM)
{
	m_tracking = false;
	if (m_hover)
	{
		m_hover = false;
		Invalidate(FALSE);
	}
	return 0;
}

void CHeartToggle::OnEnable(BOOL bEnable)
{
	Invalidate(FALSE);
	CWnd::OnEnable(bEnable);
}
