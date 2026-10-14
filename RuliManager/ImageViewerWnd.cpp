#include "pch.h"
#include "ImageViewerWnd.h"
#include "ImagePreview.h"
#include "Resource.h"
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

namespace
{
	const COLORREF& kViewerBack = TCOL(TC_VIEWER);
}

BEGIN_MESSAGE_MAP(CImageViewerWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_MBUTTONDOWN()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSEWHEEL()
	ON_WM_KEYDOWN()
	ON_WM_SETCURSOR()
	ON_WM_GETMINMAXINFO()
	ON_MESSAGE(WM_MOUSELEAVE, &CImageViewerWnd::OnMouseLeave)
END_MESSAGE_MAP()

bool CImageViewerWnd::Open(CWnd* owner, const CString& path, bool sfw, const CString& caption)
{
	CImage img;
	if (path.IsEmpty() || !(sfw ? LoadImageFileSfw(img, path) : LoadImageFile(img, path)))
		return false;
	if (!m_image.IsNull())
		m_image.Destroy();
	m_image.Attach(img.Detach());
	m_caption = caption;
	m_fit = true;
	m_dragging = false;
	m_listMode = m_listOpening;   // OpenList 가 부를 때만 여러 장 모드 (목록 유지)
	m_listOpening = false;
	if (!m_listMode)
		m_files.clear();
	m_navHover = 0;

	const int iw = m_image.GetWidth();
	const int ih = m_image.GetHeight();

	// 처음 창 크기: 원본 크기 (모니터 작업 영역의 90% 를 넘으면 그 안에 맞춤)
	CRect work;
	{
		MONITORINFO mi = { sizeof(mi) };
		HMONITOR mon = ::MonitorFromWindow(owner ? owner->GetSafeHwnd() : nullptr, MONITOR_DEFAULTTONEAREST);
		if (::GetMonitorInfoW(mon, &mi))
			work = mi.rcWork;
		else
			::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
	}
	const int maxW = work.Width() * 9 / 10;
	const int maxH = work.Height() * 9 / 10;
	const DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
	const DWORD exStyle = 0;
	auto clientToWindow = [&](int cw, int ch) -> CSize
	{
		CRect r(0, 0, cw, ch);
		::AdjustWindowRectEx(&r, style, FALSE, exStyle);
		return r.Size();
	};
	const CSize frame = clientToWindow(0, 0);
	double scale = 1.0;
	if (iw > 0 && ih > 0)
		scale = (std::min)(1.0, (std::min)(static_cast<double>(maxW - frame.cx) / iw, static_cast<double>(maxH - frame.cy) / ih));
	const int cw = (std::max)(200, static_cast<int>(iw * scale + 0.5));
	const int ch = (std::max)(150, static_cast<int>(ih * scale + 0.5));
	const CSize ws = clientToWindow(cw, ch);
	const CRect wr(CPoint(work.left + (work.Width() - ws.cx) / 2, work.top + (work.Height() - ws.cy) / 2), ws);

	if (!GetSafeHwnd())
	{
		const CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), nullptr,
			AfxGetApp() ? AfxGetApp()->LoadIcon(IDR_MAINFRAME) : nullptr);
		if (!CreateEx(exStyle, cls, L"", style, wr, owner, 0))
			return false;
		const BOOL dark = TRUE;
		::DwmSetWindowAttribute(GetSafeHwnd(), 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));   // 어두운 제목 표시줄 (지원하는 Windows 만)
	}
	else
	{
		if (IsIconic() || IsZoomed())
			ShowWindow(SW_RESTORE);
		MoveWindow(wr);
	}
	UpdateTitle();
	ShowWindow(SW_SHOW);
	SetForegroundWindow();
	SetFocus();
	Invalidate(FALSE);
	return true;
}

bool CImageViewerWnd::OpenList(CWnd* owner, const std::vector<CString>& files, int index, bool sfw)
{
	if (files.empty())
		return false;
	m_files = files;
	m_index = (std::max)(0, (std::min)(index, static_cast<int>(files.size()) - 1));
	m_sfw = sfw;
	m_listOpening = true;
	CString caption;
	caption.Format(L"%s  (%d / %d)", ::PathFindFileNameW(m_files[m_index]), m_index + 1, static_cast<int>(m_files.size()));
	const bool ok = Open(owner, m_files[m_index], sfw, caption);
	m_listOpening = false;   // 열지 못했을 때도 다음 Open 은 한 장 모드
	return ok;
}

bool CImageViewerWnd::LoadCurrent()
{
	if (m_index < 0 || m_index >= static_cast<int>(m_files.size()))
		return false;
	CImage img;
	const CString& path = m_files[m_index];
	if (!(m_sfw ? LoadImageFileSfw(img, path) : LoadImageFile(img, path)))
		return false;
	if (!m_image.IsNull())
		m_image.Destroy();
	m_image.Attach(img.Detach());
	m_caption.Format(L"%s  (%d / %d)", ::PathFindFileNameW(path), m_index + 1, static_cast<int>(m_files.size()));
	return true;
}

void CImageViewerWnd::ShowIndex(int index)
{
	const int n = static_cast<int>(m_files.size());
	if (n <= 0)
		return;
	index = (std::max)(0, (std::min)(index, n - 1));   // 처음 / 마지막에서는 그대로
	if (index == m_index && !m_image.IsNull())
		return;
	m_index = index;
	if (!LoadCurrent())
	{
		if (!m_image.IsNull())
			m_image.Destroy();
		m_caption.Format(L"%s  (%d / %d) - 읽을 수 없음", ::PathFindFileNameW(m_files[m_index]), m_index + 1, n);
	}
	m_fit = true;
	m_dragging = false;
	// 처음 / 마지막에 닿으면 그쪽 < / > 를 바로 지움 (마우스 위치 기준으로 다시)
	CPoint pt;
	::GetCursorPos(&pt);
	ScreenToClient(&pt);
	CRect rc;
	GetClientRect(&rc);
	m_navHover = rc.PtInRect(pt) ? NavZoneAt(pt) : 0;
	UpdateTitle();
	Invalidate(FALSE);
}

CRect CImageViewerWnd::NavZoneRect(int side) const
{
	CRect rc;
	GetClientRect(&rc);
	const int w = (std::max)(48, (std::min)(120, rc.Width() / 8));   // 좌우 끝 영역 폭
	return side < 0 ? CRect(rc.left, rc.top, rc.left + w, rc.bottom) : CRect(rc.right - w, rc.top, rc.right, rc.bottom);
}

int CImageViewerWnd::NavZoneAt(CPoint pt) const
{
	if (!Gallery())
		return 0;
	if (m_index > 0 && NavZoneRect(-1).PtInRect(pt))   // 처음 이미지에서는 < 없음
		return -1;
	if (m_index < static_cast<int>(m_files.size()) - 1 && NavZoneRect(1).PtInRect(pt))   // 마지막 이미지에서는 > 없음
		return 1;
	return 0;
}

LRESULT CImageViewerWnd::OnMouseLeave(WPARAM, LPARAM)
{
	m_trackLeave = false;
	if (m_navHover != 0)
	{
		m_navHover = 0;
		Invalidate(FALSE);
	}
	return 0;
}

void CImageViewerWnd::UpdateTitle()
{
	CString t;
	t.Format(L"%s  -  %d x %d%s", static_cast<LPCWSTR>(m_caption), m_image.IsNull() ? 0 : m_image.GetWidth(), m_image.IsNull() ? 0 : m_image.GetHeight(),
		m_fit ? L"" : L"  (원본 크기)");
	SetWindowText(t);
}

CRect CImageViewerWnd::ImageRect() const
{
	CRect rc;
	GetClientRect(&rc);
	if (m_image.IsNull())
		return CRect();
	const int iw = m_image.GetWidth();
	const int ih = m_image.GetHeight();
	if (iw <= 0 || ih <= 0)
		return CRect();
	if (m_fit)
	{
		double scale = (std::min)(static_cast<double>(rc.Width()) / iw, static_cast<double>(rc.Height()) / ih);
		scale = (std::min)(scale, 1.0);   // 작은 이미지는 확대하지 않음
		const int w = (std::max)(1, static_cast<int>(iw * scale + 0.5));
		const int h = (std::max)(1, static_cast<int>(ih * scale + 0.5));
		const int x = rc.left + (rc.Width() - w) / 2;
		const int y = rc.top + (rc.Height() - h) / 2;
		return CRect(x, y, x + w, y + h);
	}
	return CRect(m_offset, CSize(iw, ih));
}

void CImageViewerWnd::ClampOffset()
{
	if (m_image.IsNull())
		return;
	CRect rc;
	GetClientRect(&rc);
	const int iw = m_image.GetWidth();
	const int ih = m_image.GetHeight();
	// 창보다 작으면 가운데, 크면 빈 곳이 생기지 않게
	if (iw <= rc.Width())
		m_offset.x = (rc.Width() - iw) / 2;
	else
		m_offset.x = (std::min)(0L, (std::max)(static_cast<LONG>(rc.Width() - iw), m_offset.x));
	if (ih <= rc.Height())
		m_offset.y = (rc.Height() - ih) / 2;
	else
		m_offset.y = (std::min)(0L, (std::max)(static_cast<LONG>(rc.Height() - ih), m_offset.y));
}

void CImageViewerWnd::OnPaint()
{
	CPaintDC dc(this);
	CRect rc;
	GetClientRect(&rc);
	if (rc.IsRectEmpty())
		return;
	CDC mem;
	mem.CreateCompatibleDC(&dc);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
	CBitmap* old = mem.SelectObject(&bmp);
	mem.FillSolidRect(&rc, kViewerBack);
	if (!m_image.IsNull())
	{
		const CRect ir = ImageRect();
		if (m_fit && (ir.Width() != m_image.GetWidth() || ir.Height() != m_image.GetHeight()))
		{
			mem.SetStretchBltMode(HALFTONE);
			::SetBrushOrgEx(mem.GetSafeHdc(), 0, 0, nullptr);
		}
		m_image.Draw(mem.GetSafeHdc(), ir);
	}
	if (Gallery() && m_navHover != 0)
	{
		// 마우스가 올라간 끝 영역: 반투명 어두운 띠 + 흰 < / >
		const CRect zr = NavZoneRect(m_navHover);
		CDC shade;
		shade.CreateCompatibleDC(&mem);
		CBitmap sb;
		sb.CreateCompatibleBitmap(&mem, 1, 1);
		CBitmap* os = shade.SelectObject(&sb);
		shade.SetPixel(0, 0, RGB(0, 0, 0));
		BLENDFUNCTION bf = { AC_SRC_OVER, 0, 110, 0 };
		::AlphaBlend(mem.GetSafeHdc(), zr.left, zr.top, zr.Width(), zr.Height(), shade.GetSafeHdc(), 0, 0, 1, 1, bf);
		shade.SelectObject(os);
		Gdiplus::Graphics g(mem.GetSafeHdc());
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		const float cx = (zr.left + zr.right) / 2.0f, cy = (zr.top + zr.bottom) / 2.0f;
		const float s = (std::max)(10.0f, (std::min)(28.0f, zr.Width() / 4.0f));   // 꺾쇠 크기
		Gdiplus::Pen pen(Gdiplus::Color(235, 255, 255, 255), (std::max)(3.0f, s / 4.0f));
		pen.SetStartCap(Gdiplus::LineCapRound);
		pen.SetEndCap(Gdiplus::LineCapRound);
		pen.SetLineJoin(Gdiplus::LineJoinRound);
		const float dx = s / 2.0f * (m_navHover < 0 ? 1.0f : -1.0f);   // < 는 오른쪽에서 왼쪽으로 꺾임
		Gdiplus::PointF pts[3] = { { cx + dx, cy - s }, { cx - dx, cy }, { cx + dx, cy + s } };
		g.DrawLines(&pen, pts, 3);
	}
	dc.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(old);
}

void CImageViewerWnd::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	if (!m_fit)
		ClampOffset();
	Invalidate(FALSE);
}

void CImageViewerWnd::OnLButtonDblClk(UINT /*nFlags*/, CPoint point)
{
	if (m_listMode)
	{
		// 여러 장 모드: 확대 전환 없음, 끝 영역을 빠르게 두 번 누르면 두 장 넘김 (두 번째 누름이 더블클릭으로 옴)
		if (const int side = NavZoneAt(point))
			ShowIndex(m_index + side);
		return;
	}
	// 창에 맞춤 ↔ 원본 크기: 원본 크기로 바꿀 때는 더블클릭한 곳이 그 자리에 오도록
	if (m_image.IsNull())
		return;
	if (m_fit)
	{
		const CRect ir = ImageRect();
		double fx = 0.5, fy = 0.5;
		if (ir.Width() > 0 && ir.Height() > 0 && ir.PtInRect(point))
		{
			fx = static_cast<double>(point.x - ir.left) / ir.Width();
			fy = static_cast<double>(point.y - ir.top) / ir.Height();
		}
		m_fit = false;
		m_offset.x = point.x - static_cast<LONG>(fx * m_image.GetWidth());
		m_offset.y = point.y - static_cast<LONG>(fy * m_image.GetHeight());
		ClampOffset();
	}
	else
		m_fit = true;
	m_dragging = false;
	if (GetCapture() == this)
		ReleaseCapture();
	UpdateTitle();
	Invalidate(FALSE);
}

void CImageViewerWnd::OnMButtonDown(UINT nFlags, CPoint point)
{
	if (m_listMode)
		return;   // 여러 장 모드: 확대 전환 없음
	OnLButtonDblClk(nFlags, point);
}

void CImageViewerWnd::OnLButtonDown(UINT /*nFlags*/, CPoint point)
{
	if (const int side = NavZoneAt(point))
	{
		ShowIndex(m_index + side);   // 좌우 끝 영역: 이전 / 다음 이미지
		return;
	}
	if (m_fit || m_image.IsNull())
		return;
	m_dragging = true;
	m_dragStart = point;
	m_dragOffset = m_offset;
	SetCapture();
}

void CImageViewerWnd::OnLButtonUp(UINT /*nFlags*/, CPoint /*point*/)
{
	if (m_dragging)
	{
		m_dragging = false;
		if (GetCapture() == this)
			ReleaseCapture();
	}
}

void CImageViewerWnd::OnMouseMove(UINT /*nFlags*/, CPoint point)
{
	if (Gallery())
	{
		// 좌우 끝 영역 위에서만 < / > 표시
		const int side = NavZoneAt(point);
		if (side != m_navHover)
		{
			m_navHover = side;
			Invalidate(FALSE);
		}
		if (!m_trackLeave)
		{
			TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, GetSafeHwnd(), 0 };
			m_trackLeave = ::TrackMouseEvent(&tme) != FALSE;
		}
	}
	if (!m_dragging)
		return;
	m_offset = m_dragOffset + (point - m_dragStart);
	ClampOffset();
	Invalidate(FALSE);
}

BOOL CImageViewerWnd::OnMouseWheel(UINT nFlags, short zDelta, CPoint /*pt*/)
{
	if (m_listMode)
	{
		// 여러 장 모드: 휠 위 = 이전, 휠 아래 = 다음 (처음 / 마지막에서는 그대로)
		if (zDelta > 0)
			ShowIndex(m_index - 1);
		else if (zDelta < 0)
			ShowIndex(m_index + 1);
		return TRUE;
	}
	// 원본 크기에서: 휠 = 위아래, Shift + 휠 = 좌우
	if (m_fit || m_image.IsNull())
		return TRUE;
	CRect rc;
	GetClientRect(&rc);
	const int step = (std::max)(40, rc.Height() / 8) * zDelta / WHEEL_DELTA;
	if (nFlags & MK_SHIFT)
		m_offset.x += step;
	else
		m_offset.y += step;
	ClampOffset();
	Invalidate(FALSE);
	return TRUE;
}

void CImageViewerWnd::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (nChar == VK_ESCAPE)
	{
		ShowWindow(SW_HIDE);   // 창은 남겨 두고 숨김 (다음에 다시 씀)
		if (CWnd* owner = GetOwner())
			owner->SetForegroundWindow();
		if (!m_image.IsNull())
			m_image.Destroy();   // 메모리 정리
		return;
	}
	if (m_listMode)
	{
		// 여러 장 모드: ← → (PageUp / PageDown) 로 이전 / 다음, Home / End 로 처음 / 끝, 확대 전환 키 없음
		if (nChar == VK_LEFT || nChar == VK_PRIOR) { ShowIndex(m_index - 1); return; }
		if (nChar == VK_RIGHT || nChar == VK_NEXT) { ShowIndex(m_index + 1); return; }
		if (nChar == VK_HOME) { ShowIndex(0); return; }
		if (nChar == VK_END) { ShowIndex(static_cast<int>(m_files.size()) - 1); return; }
		if (nChar == VK_SPACE || nChar == VK_RETURN)
			return;
	}
	if (nChar == VK_SPACE || nChar == VK_RETURN)
	{
		CRect rc;
		GetClientRect(&rc);
		OnLButtonDblClk(0, rc.CenterPoint());
		return;
	}
	CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

BOOL CImageViewerWnd::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (nHitTest == HTCLIENT && Gallery())
	{
		CPoint pt;
		::GetCursorPos(&pt);
		ScreenToClient(&pt);
		if (NavZoneAt(pt) != 0)
		{
			::SetCursor(::LoadCursor(nullptr, IDC_HAND));   // 좌우 끝 영역: 누르면 넘김
			return TRUE;
		}
	}
	if (nHitTest == HTCLIENT && !m_fit && !m_image.IsNull())
	{
		::SetCursor(::LoadCursor(nullptr, IDC_SIZEALL));   // 원본 크기: 드래그로 이동
		return TRUE;
	}
	return CWnd::OnSetCursor(pWnd, nHitTest, message);
}

void CImageViewerWnd::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	lpMMI->ptMinTrackSize.x = 200;
	lpMMI->ptMinTrackSize.y = 150;
	CWnd::OnGetMinMaxInfo(lpMMI);
}
