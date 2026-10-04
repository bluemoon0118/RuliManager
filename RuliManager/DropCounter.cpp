#include "pch.h"
#include "DropCounter.h"
#include "VectorIcons.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace { const UINT kCmdReset = 1; }

BEGIN_MESSAGE_MAP(CDropCounter, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_RBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_MESSAGE(WM_MOUSELEAVE, &CDropCounter::OnMouseLeave)
	ON_WM_ENABLE()
END_MESSAGE_MAP()

bool CDropCounter::Create(CWnd* parent, UINT id)
{
	const CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_HAND), nullptr);
	if (!CWnd::Create(cls, L"", WS_CHILD | WS_TABSTOP, CRect(0, 0, 60, 20), parent, id))
		return false;
	if (m_tip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX))
	{
		m_tip.AddTool(this, L"클릭: 1 증가 / 오른쪽 클릭: 초기화");
		m_tip.Activate(TRUE);
	}
	return true;
}

void CDropCounter::SetColors(COLORREF back, COLORREF normal, COLORREF hover)
{
	m_back = back;
	m_normal = normal;
	m_hoverColor = hover;
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

void CDropCounter::SetCount(int count)
{
	m_count = (std::max)(0, count);
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

void CDropCounter::Change(int count)
{
	count = (std::max)(0, count);
	if (count == m_count)
		return;
	m_count = count;
	Invalidate(FALSE);
	if (m_onChanged)
		m_onChanged(m_count);
}

void CDropCounter::OnPaint()
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

	const bool enabled = IsWindowEnabled() != FALSE;
	const COLORREF col = !enabled ? RGB(0x6C, 0x74, 0x78) : (m_hover ? m_hoverColor : m_normal);

	// 아이콘: 왼쪽 정사각형 (높이에 맞춤)
	const int side = rc.Height();
	VectorIcon::Drops(&mem, CRect(rc.left, rc.top, rc.left + side, rc.top + side), col, 0.9);

	// 숫자
	CFont* old = mem.SelectObject(GetParent()->GetFont());
	mem.SetBkMode(TRANSPARENT);
	mem.SetTextColor(col);
	CString t;
	t.Format(L"%d", m_count);
	CRect tr(rc.left + side + 3, rc.top, rc.right, rc.bottom);
	mem.DrawText(t, tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	mem.SelectObject(old);

	paint.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(oldBmp);
}

void CDropCounter::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (IsWindowEnabled())
		Change(m_count + 1);
	CWnd::OnLButtonDown(nFlags, point);
}

void CDropCounter::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	// 빠르게 두 번 눌러도 두 번 증가
	if (IsWindowEnabled())
		Change(m_count + 1);
	CWnd::OnLButtonDblClk(nFlags, point);
}

void CDropCounter::OnRButtonUp(UINT nFlags, CPoint point)
{
	if (!IsWindowEnabled())
		return;
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING | (m_count > 0 ? 0 : MF_GRAYED), kCmdReset, L"초기화");
	CPoint sp = point;
	ClientToScreen(&sp);
	const UINT cmd = menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY, sp.x, sp.y, this);
	if (cmd == kCmdReset)
		Change(0);
	(void)nFlags;
}

void CDropCounter::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_tracking)
	{
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
		m_tracking = ::TrackMouseEvent(&tme) != FALSE;
	}
	if (!m_hover)
	{
		m_hover = true;
		Invalidate(FALSE);
	}
	CWnd::OnMouseMove(nFlags, point);
}

LRESULT CDropCounter::OnMouseLeave(WPARAM, LPARAM)
{
	m_tracking = false;
	if (m_hover)
	{
		m_hover = false;
		Invalidate(FALSE);
	}
	return 0;
}

void CDropCounter::OnEnable(BOOL bEnable)
{
	CWnd::OnEnable(bEnable);
	Invalidate(FALSE);
}

BOOL CDropCounter::PreTranslateMessage(MSG* pMsg)
{
	if (m_tip.GetSafeHwnd())
		m_tip.RelayEvent(pMsg);
	return CWnd::PreTranslateMessage(pMsg);
}
