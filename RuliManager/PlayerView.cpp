#include "pch.h"
#include "PlayerView.h"
#include <cmath>
#include <gdiplus.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// ---------------------------------------------------------------------------
// 영상 칸

BEGIN_MESSAGE_MAP(CPlayerVideoWnd, CWnd)
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_MOUSEMOVE()
	ON_WM_TIMER()
	ON_WM_SETCURSOR()
	ON_WM_RBUTTONUP()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

void CPlayerVideoWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
	CWnd::OnLButtonDown(nFlags, point);
	m_down = true;
}

void CPlayerVideoWnd::OnLButtonUp(UINT nFlags, CPoint point)
{
	CWnd::OnLButtonUp(nFlags, point);
	const bool down = m_down;
	m_down = false;
	if (m_skipUp)
	{
		m_skipUp = false;
		return;
	}
	if (!down)
		return;   // 이 창에서 누르지 않은 버튼 떼기 (영상 카드 더블클릭 → 플레이어가 열린 뒤 떼기가 이리로 옴): 무시
	SetTimer(1, ::GetDoubleClickTime(), nullptr);   // 더블클릭이 아니면 그때 한 번 클릭 (일시정지 전환)
}

void CPlayerVideoWnd::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	CWnd::OnLButtonDblClk(nFlags, point);
	KillTimer(1);
	m_down = false;
	m_skipUp = true;
	if (onDblClick) onDblClick();
}

void CPlayerVideoWnd::OnTimer(UINT_PTR id)
{
	if (id == 1)
	{
		KillTimer(1);
		if (onClick) onClick();
		return;
	}
	CWnd::OnTimer(id);
}

void CPlayerVideoWnd::OnMouseMove(UINT nFlags, CPoint point)
{
	CWnd::OnMouseMove(nFlags, point);
	if (onMouseMove) onMouseMove();
}

BOOL CPlayerVideoWnd::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (nHitTest == HTCLIENT && hideCursor && hideCursor())
	{
		::SetCursor(nullptr);
		return TRUE;
	}
	return CWnd::OnSetCursor(pWnd, nHitTest, message);
}

void CPlayerVideoWnd::OnRButtonUp(UINT nFlags, CPoint point)
{
	CWnd::OnRButtonUp(nFlags, point);
	if (onContextMenu)
	{
		CPoint s = point;
		ClientToScreen(&s);
		onContextMenu(s);
	}
}

BOOL CPlayerVideoWnd::OnEraseBkgnd(CDC* dc)
{
	CRect rc;
	GetClientRect(&rc);
	dc->FillSolidRect(rc, RGB(0, 0, 0));
	return TRUE;
}

BEGIN_MESSAGE_MAP(CPlayerFsWnd, CWnd)
	ON_WM_CLOSE()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

BOOL CPlayerFsWnd::OnEraseBkgnd(CDC* dc)
{
	CRect rc;
	GetClientRect(&rc);
	dc->FillSolidRect(rc, RGB(0, 0, 0));
	return TRUE;
}

// ---------------------------------------------------------------------------
// 플레이어 View

BEGIN_MESSAGE_MAP(CPlayerView, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_MESSAGE(WM_MOUSELEAVE, &CPlayerView::OnMouseLeave)
	ON_WM_MOUSEWHEEL()
	ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_WM_GETDLGCODE()
	ON_MESSAGE(WM_MPV_WAKE, &CPlayerView::OnMpvWake)
	ON_MESSAGE(WM_PLAYER_ENDED, &CPlayerView::OnPlayerEnded)
END_MESSAGE_MAP()

namespace
{
	const double kSpeeds[] = { 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 3.0 };
}

int CPlayerView::S(int px) const
{
	UINT dpi = GetSafeHwnd() ? ::GetDpiForWindow(m_hWnd) : 96;
	if (dpi == 0) dpi = 96;
	return ::MulDiv(px, static_cast<int>(dpi), 96);
}

bool CPlayerView::Create(CWnd* parent, UINT id)
{
	const CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), nullptr);
	if (!CWnd::CreateEx(0, cls, L"", WS_CHILD | WS_CLIPCHILDREN | WS_TABSTOP, CRect(0, 0, 10, 10), parent, id))
		return false;
	m_dockParent = parent;
	// 글꼴: 메인 창 글꼴 +1pt / 제목 굵게 +2pt
	LOGFONT lf = {};
	if (CFont* f = parent->GetFont())
		f->GetLogFont(&lf);
	else
		::GetObject(::GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf);
	const LONG base = std::abs(lf.lfHeight);
	lf.lfHeight = -(base + S(1));
	m_font.CreateFontIndirect(&lf);
	lf.lfHeight = -(base + S(3));
	lf.lfWeight = FW_BOLD;
	m_boldFont.CreateFontIndirect(&lf);

	const CString vcls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
	m_video.CreateEx(0, vcls, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, CRect(0, 0, 10, 10), this, 1);
	m_video.onClick = [this]() { TogglePauseUser(); };
	m_video.onDblClick = [this]() { SetFullscreen(!m_full); };
	m_video.onMouseMove = [this]() { Activity(); };
	m_video.hideCursor = [this]() { return m_full && !m_barsShown; };

	m_mpv.onChanged = [this]()
	{
		if (!m_rcSeek.IsRectEmpty())
		{
			InvalidateRect(m_rcSeek, FALSE);
			InvalidateRect(m_rcRow, FALSE);
		}
	};
	m_mpv.onVideoReady = [this]() { DisableMpvChildren(); };
	m_mpv.onFileLoaded = [this]() { DisableMpvChildren(); Invalidate(FALSE); };
	m_mpv.onEof = [this]()
	{
		if (m_endAction == 1)
			PostMessage(WM_PLAYER_ENDED);   // [목록으로 돌아가기]
	};

	m_fsWnd.onClose = [this]() { SetFullscreen(false); };
	return true;
}

bool CPlayerView::Open(const CString& path, const CString& title, double startSec)
{
	m_title = title;
	CWinApp* cfg = AfxGetApp();
	m_seekStep = (std::max)(1, (std::min)(600, static_cast<int>(cfg->GetProfileInt(L"Settings", L"PlayerSeek", 5))));
	m_seekBig = (std::max)(1, (std::min)(600, static_cast<int>(cfg->GetProfileInt(L"Settings", L"PlayerSeekBig", 30))));
	m_hideMs = static_cast<ULONGLONG>((std::max)(1, (std::min)(10, static_cast<int>(cfg->GetProfileInt(L"Settings", L"PlayerHideSec", 3))))) * 1000;
	m_endAction = (std::max)(0, (std::min)(2, static_cast<int>(cfg->GetProfileInt(L"Settings", L"PlayerEndAction", 0))));
	if (!m_mpv.IsCreated())
	{
		if (!m_mpv.Create(m_video.GetSafeHwnd(), GetSafeHwnd(), WM_MPV_WAKE,
				cfg->GetProfileInt(L"Settings", L"PlayerHwdec", 1) != 0, cfg->GetProfileInt(L"Settings", L"PlayerSubAuto", 1) != 0))
			return false;
		// 볼륨 · 음소거 (마지막 값)
		CWinApp* app = AfxGetApp();
		m_mpv.SetVolume(app->GetProfileInt(L"Settings", L"PlayerVolume", 100));
		m_mpv.SetMute(app->GetProfileInt(L"Settings", L"PlayerMute", 0) != 0);
	}
	m_mpv.SetSpeed(1.0);
	if (!m_mpv.Load(path, startSec, m_endAction == 2, cfg->GetProfileInt(L"Settings", L"PlayerAutoPlay", 1) != 0))
		return false;
	m_open = true;
	m_hoverSec = m_dragSec = -1.0;
	m_playedSec = 0.0;
	m_lastPos = -1.0;
	m_counted = false;
	Invalidate(FALSE);
	return true;
}

void CPlayerView::Close()
{
	if (m_full)
		SetFullscreen(false);
	if (m_mpv.IsCreated())
	{
		CWinApp* app = AfxGetApp();
		app->WriteProfileInt(L"Settings", L"PlayerVolume", static_cast<int>(std::lround(m_mpv.volume)));
		app->WriteProfileInt(L"Settings", L"PlayerMute", m_mpv.mute ? 1 : 0);
		m_mpv.Destroy();   // 파일 · 디코더 · 오디오 장치를 모두 놓음 (다음 재생 때 다시 만듦)
	}
	m_open = false;
	m_dragSeek = m_dragVol = false;
	if (GetCapture() == this)
		ReleaseCapture();
}

void CPlayerView::OnDestroy()
{
	Close();
	if (m_fsWnd.GetSafeHwnd())
		m_fsWnd.DestroyWindow();
	CWnd::OnDestroy();
}

LRESULT CPlayerView::OnMpvWake(WPARAM, LPARAM)
{
	m_mpv.ProcessEvents();
	TrackPlayed();
	return 0;
}

void CPlayerView::TrackPlayed()
{
	if (!m_open || !m_mpv.loaded)
		return;
	const double pos = m_mpv.pos;
	// 재생 중 · 연속 탐색 아님 · 앞으로 조금씩 (0 ~ 1.5초) 움직인 것만 = 실제로 본 시간 (탐색 · 되감기는 건너뜀)
	if (!m_mpv.paused && m_seekTarget < 0.0 && m_lastPos >= 0.0)
	{
		const double d = pos - m_lastPos;
		if (d > 0.0 && d < 1.5)
			m_playedSec += d;
	}
	m_lastPos = pos;
	if (!m_counted && (m_playedSec >= 60.0 || (m_mpv.dur > 0.0 && m_playedSec >= m_mpv.dur * 0.1)))
	{
		m_counted = true;
		if (onPlayCounted) onPlayCounted();
	}
}

LRESULT CPlayerView::OnPlayerEnded(WPARAM, LPARAM)
{
	if (m_open && m_mpv.eof && m_endAction == 1 && onBack)
		onBack();   // 끝까지 봄 → 목록으로 (보던 위치는 끝 근처라 지워짐 → 다음에 처음부터)
	return 0;
}

void CPlayerView::DisableMpvChildren()
{
	// mpv 가 만든 그림 창은 꺼 둠 → 마우스는 영상 칸(m_video)이 받음 (클릭 = 일시정지, 더블클릭 = 전체 화면)
	::EnumChildWindows(m_video.GetSafeHwnd(), [](HWND h, LPARAM) -> BOOL
	{
		if (::IsWindowEnabled(h))
			::EnableWindow(h, FALSE);
		return TRUE;
	}, 0);
}

// ---------------------------------------------------------------------------
// 배치

void CPlayerView::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	Layout();
}

void CPlayerView::Layout()
{
	if (!GetSafeHwnd())
		return;
	CRect rc;
	GetClientRect(&rc);
	const bool bars = !m_full || m_barsShown;
	const int topH = m_full ? 0 : S(34);
	const int seekH = bars ? S(16) : 0;
	const int rowH = bars ? S(38) : 0;
	m_rcTop.SetRect(rc.left, rc.top, rc.right, rc.top + topH);
	m_rcRow.SetRect(rc.left, rc.bottom - rowH, rc.right, rc.bottom);
	m_rcSeek.SetRect(rc.left, m_rcRow.top - seekH, rc.right, m_rcRow.top);
	m_rcVideo.SetRect(rc.left, m_rcTop.bottom, rc.right, (std::max)(static_cast<int>(m_rcTop.bottom), static_cast<int>(m_rcSeek.top)));

	// 위 줄: [◀ 목록] + 제목
	const int pad = S(8);
	m_rcBack.SetRectEmpty();
	m_rcTitle.SetRectEmpty();
	if (topH > 0)
	{
		m_rcBack.SetRect(rc.left + pad, m_rcTop.top + S(5), rc.left + pad + S(76), m_rcTop.bottom - S(5));
		m_rcTitle.SetRect(m_rcBack.right + S(12), m_rcTop.top, rc.right - pad, m_rcTop.bottom);
	}
	// 아래 줄: [재생] [-10] [+10] 시간 ……… [속도] [음소거] [볼륨] [전체 화면]
	m_rcPlay.SetRectEmpty(); m_rcRew.SetRectEmpty(); m_rcFwd.SetRectEmpty(); m_rcTime.SetRectEmpty();
	m_rcSpeed.SetRectEmpty(); m_rcMute.SetRectEmpty(); m_rcVol.SetRectEmpty(); m_rcFull.SetRectEmpty();
	if (rowH > 0)
	{
		const int b = rowH - S(8);   // 버튼 크기
		const int y = m_rcRow.top + (rowH - b) / 2;
		int x = rc.left + pad;
		m_rcPlay.SetRect(x, y, x + b, y + b);  x += b + S(4);
		m_rcRew.SetRect(x, y, x + b + S(8), y + b);  x += b + S(8) + S(2);
		m_rcFwd.SetRect(x, y, x + b + S(8), y + b);  x += b + S(8) + S(12);
		int r = rc.right - pad;
		m_rcFull.SetRect(r - b, y, r, y + b);  r -= b + S(10);
		m_rcVol.SetRect(r - S(90), y, r, y + b);  r -= S(90) + S(4);
		m_rcMute.SetRect(r - b, y, r, y + b);  r -= b + S(8);
		m_rcSpeed.SetRect(r - S(48), y, r, y + b);  r -= S(48) + S(8);
		m_rcTime.SetRect(x, m_rcRow.top, (std::max)(x, r), m_rcRow.bottom);
	}
	if (m_video.GetSafeHwnd())
		m_video.MoveWindow(m_rcVideo);
	Invalidate(FALSE);
}

CPlayerView::Part CPlayerView::HitTest(CPoint pt) const
{
	if (m_rcBack.PtInRect(pt)) return P_BACK;
	if (m_rcSeek.PtInRect(pt)) return P_SEEK;
	if (m_rcPlay.PtInRect(pt)) return P_PLAY;
	if (m_rcRew.PtInRect(pt)) return P_REW;
	if (m_rcFwd.PtInRect(pt)) return P_FWD;
	if (m_rcSpeed.PtInRect(pt)) return P_SPEED;
	if (m_rcMute.PtInRect(pt)) return P_MUTE;
	if (m_rcVol.PtInRect(pt)) return P_VOL;
	if (m_rcFull.PtInRect(pt)) return P_FULL;
	return P_NONE;
}

double CPlayerView::SeekSecAt(int x) const
{
	const int l = m_rcSeek.left + S(8), r = m_rcSeek.right - S(8);
	if (r <= l || m_mpv.dur <= 0.0)
		return -1.0;
	const double f = (std::max)(0.0, (std::min)(1.0, static_cast<double>(x - l) / (r - l)));
	return f * m_mpv.dur;
}

double CPlayerView::VolAt(int x) const
{
	const int l = m_rcVol.left + S(4), r = m_rcVol.right - S(4);
	if (r <= l)
		return m_mpv.volume;
	const double f = (std::max)(0.0, (std::min)(1.0, static_cast<double>(x - l) / (r - l)));
	return f * 100.0;
}

CString CPlayerView::FormatTime(double sec)
{
	if (sec < 0.0) sec = 0.0;
	const long long t = static_cast<long long>(sec);
	CString s;
	if (t >= 3600)
		s.Format(L"%lld:%02lld:%02lld", t / 3600, (t / 60) % 60, t % 60);
	else
		s.Format(L"%lld:%02lld", t / 60, t % 60);
	return s;
}

// ---------------------------------------------------------------------------
// 그리기

void CPlayerView::DrawIcon(CDC* dc, Part p, const CRect& rc, COLORREF col)
{
	Gdiplus::Graphics g(dc->GetSafeHdc());
	g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	const Gdiplus::Color c(255, GetRValue(col), GetGValue(col), GetBValue(col));
	Gdiplus::SolidBrush br(c);
	const float cx = rc.left + rc.Width() / 2.0f, cy = rc.top + rc.Height() / 2.0f;
	const float u = (std::min)(rc.Width(), rc.Height()) / 2.0f * 0.55f;   // 아이콘 반 크기
	switch (p)
	{
	case P_PLAY:
		if (m_open && (!m_mpv.paused || m_seekPaused))   // 연속 탐색의 임시 정지는 재생 중으로 표시
		{
			// 일시정지 ‖
			const float w = u * 0.55f, gap = u * 0.35f;
			g.FillRectangle(&br, cx - gap - w, cy - u, w, u * 2);
			g.FillRectangle(&br, cx + gap, cy - u, w, u * 2);
		}
		else
		{
			const Gdiplus::PointF pts[] = { { cx - u * 0.7f, cy - u }, { cx + u, cy }, { cx - u * 0.7f, cy + u } };
			g.FillPolygon(&br, pts, 3);
		}
		break;
	case P_MUTE:
	{
		// 스피커 + (음소거면 ×, 아니면 소리 물결)
		const Gdiplus::PointF pts[] = {
			{ cx - u, cy - u * 0.4f }, { cx - u * 0.45f, cy - u * 0.4f }, { cx + u * 0.1f, cy - u },
			{ cx + u * 0.1f, cy + u }, { cx - u * 0.45f, cy + u * 0.4f }, { cx - u, cy + u * 0.4f } };
		g.FillPolygon(&br, pts, 6);
		Gdiplus::Pen pen(c, (std::max)(1.5f, u * 0.18f));
		if (m_mpv.mute)
		{
			g.DrawLine(&pen, cx + u * 0.4f, cy - u * 0.4f, cx + u, cy + u * 0.4f);
			g.DrawLine(&pen, cx + u, cy - u * 0.4f, cx + u * 0.4f, cy + u * 0.4f);
		}
		else
		{
			g.DrawArc(&pen, cx - u * 0.2f, cy - u * 0.5f, u * 0.8f, u * 1.0f, -50.0f, 100.0f);
			if (m_mpv.volume > 50.0)
				g.DrawArc(&pen, cx - u * 0.45f, cy - u * 0.9f, u * 1.4f, u * 1.8f, -50.0f, 100.0f);
		}
		break;
	}
	case P_FULL:
	{
		// 네 모서리 꺾쇠 (전체 화면이면 안쪽으로)
		Gdiplus::Pen pen(c, (std::max)(1.5f, u * 0.2f));
		const float a = u, k = u * 0.45f;
		const float sx[] = { -1, 1, 1, -1 }, sy[] = { -1, -1, 1, 1 };
		for (int i = 0; i < 4; ++i)
		{
			const float px = cx + sx[i] * a, py = cy + sy[i] * a;
			if (!m_full)
			{
				g.DrawLine(&pen, px, py, px - sx[i] * k, py);
				g.DrawLine(&pen, px, py, px, py - sy[i] * k);
			}
			else
			{
				const float qx = px - sx[i] * k, qy = py - sy[i] * k;
				g.DrawLine(&pen, qx, qy, qx + sx[i] * k, qy);
				g.DrawLine(&pen, qx, qy, qx, qy + sy[i] * k);
			}
		}
		break;
	}
	default:
		break;
	}
}

void CPlayerView::OnPaint()
{
	CPaintDC pdc(this);
	CRect client;
	GetClientRect(&client);
	CDC mem;
	mem.CreateCompatibleDC(&pdc);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&pdc, (std::max)(1, client.Width()), (std::max)(1, client.Height()));
	CBitmap* oldBmp = mem.SelectObject(&bmp);
	CDC* dc = &mem;

	const COLORREF back = TCOL(TC_BACK), text = TCOL(TC_TEXT), sub = TCOL(TC_SUB), accent = TCOL(TC_ACCENT), hotBack = TCOL(TC_COMBO);
	dc->FillSolidRect(client, back);
	dc->SetBkMode(TRANSPARENT);
	CFont* oldFont = dc->SelectObject(&m_font);

	auto hotFill = [&](Part p, const CRect& r)
	{
		if (m_hot == p || m_press == p)
		{
			CBrush br(hotBack);
			CPen pen(PS_SOLID, 1, hotBack);
			CBrush* ob = dc->SelectObject(&br);
			CPen* op = dc->SelectObject(&pen);
			dc->RoundRect(r, CPoint(S(6), S(6)));
			dc->SelectObject(ob);
			dc->SelectObject(op);
		}
	};

	// 위 줄
	if (!m_rcTop.IsRectEmpty())
	{
		{
			const COLORREF bb = (m_hot == P_BACK || m_press == P_BACK) ? accent : hotBack;
			CBrush br(bb);
			CPen pen(PS_SOLID, 1, bb);
			CBrush* ob = dc->SelectObject(&br);
			CPen* op = dc->SelectObject(&pen);
			dc->RoundRect(m_rcBack, CPoint(S(6), S(6)));
			dc->SelectObject(ob);
			dc->SelectObject(op);
			dc->SetTextColor(RGB(255, 255, 255));
			CRect tr = m_rcBack;
			dc->DrawText(L"◀ 목록", tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		dc->SelectObject(&m_boldFont);
		dc->SetTextColor(text);
		CRect tr = m_rcTitle;
		dc->DrawText(m_title, tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
		dc->SelectObject(&m_font);
	}

	// 탐색 바
	if (!m_rcSeek.IsRectEmpty())
	{
		const int l = m_rcSeek.left + S(8), r = m_rcSeek.right - S(8);
		const int cy = m_rcSeek.top + m_rcSeek.Height() / 2;
		const bool big = (m_hot == P_SEEK || m_dragSeek);
		const int th = big ? S(6) : S(4);
		dc->FillSolidRect(l, cy - th / 2, (std::max)(0, r - l), th, TCOL(TC_BORDER));
		const double shown = (m_dragSeek && m_dragSec >= 0.0) ? m_dragSec : (m_seekTarget >= 0.0 ? m_seekTarget : m_mpv.pos);
		if (m_mpv.dur > 0.0 && r > l)
		{
			const int px = l + static_cast<int>((r - l) * (std::max)(0.0, (std::min)(1.0, shown / m_mpv.dur)));
			dc->FillSolidRect(l, cy - th / 2, px - l, th, accent);
			if (big)
			{
				const int kr = S(7);
				CBrush br(RGB(255, 255, 255));
				CPen pen(PS_SOLID, 1, RGB(255, 255, 255));
				CBrush* ob = dc->SelectObject(&br);
				CPen* op = dc->SelectObject(&pen);
				dc->Ellipse(px - kr, cy - kr, px + kr, cy + kr);
				dc->SelectObject(ob);
				dc->SelectObject(op);
			}
			if (m_hoverSec >= 0.0 && !m_dragSeek)
			{
				const int hx = l + static_cast<int>((r - l) * (std::min)(1.0, m_hoverSec / m_mpv.dur));
				dc->FillSolidRect(hx - 1, cy - th, 2, th * 2, RGB(255, 255, 255));
			}
		}
	}

	// 아래 컨트롤 줄
	if (!m_rcRow.IsRectEmpty())
	{
		hotFill(P_PLAY, m_rcPlay);
		DrawIcon(dc, P_PLAY, m_rcPlay, text);
		hotFill(P_REW, m_rcRew);
		hotFill(P_FWD, m_rcFwd);
		dc->SetTextColor(text);
		{
			CRect t = m_rcRew;
			dc->DrawText(L"◀◀ 10", t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			t = m_rcFwd;
			dc->DrawText(L"10 ▶▶", t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		// 시간: 지금 / 전체 (탐색 바에 마우스가 있으면 그 위치를 강조색으로)
		{
			CString t = FormatTime((m_dragSeek && m_dragSec >= 0.0) ? m_dragSec : (m_seekTarget >= 0.0 ? m_seekTarget : m_mpv.pos)) + L" / " + FormatTime(m_mpv.dur);
			CRect tr = m_rcTime;
			dc->SetTextColor(text);
			dc->DrawText(t, tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			if (m_hoverSec >= 0.0 && !m_dragSeek)
			{
				CRect calc = m_rcTime;
				dc->DrawText(t, calc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
				CRect hr(calc.right + S(12), m_rcTime.top, m_rcTime.right, m_rcTime.bottom);
				dc->SetTextColor(accent);
				dc->DrawText(L"▸ " + FormatTime(m_hoverSec), hr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			}
			if (m_open && m_mpv.eof)
			{
				CRect er = m_rcTime;
				dc->SetTextColor(sub);
				dc->DrawText(L"끝", er, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			}
		}
		hotFill(P_SPEED, m_rcSpeed);
		{
			CString s;
			s.Format(L"%.2gx", m_mpv.speed);
			CRect sr = m_rcSpeed;
			dc->SetTextColor(std::fabs(m_mpv.speed - 1.0) < 0.001 ? text : accent);
			dc->DrawText(s, sr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		hotFill(P_MUTE, m_rcMute);
		DrawIcon(dc, P_MUTE, m_rcMute, m_mpv.mute ? sub : text);
		{
			// 볼륨 막대 (0 ~ 100)
			const int l = m_rcVol.left + S(4), r = m_rcVol.right - S(4);
			const int cy = m_rcVol.top + m_rcVol.Height() / 2;
			const int th = S(4);
			dc->FillSolidRect(l, cy - th / 2, r - l, th, TCOL(TC_BORDER));
			const double v = (std::max)(0.0, (std::min)(100.0, m_mpv.volume));
			const int px = l + static_cast<int>((r - l) * v / 100.0);
			dc->FillSolidRect(l, cy - th / 2, px - l, th, m_mpv.mute ? sub : accent);
			if (m_hot == P_VOL || m_dragVol)
			{
				const int kr = S(6);
				CBrush br(RGB(255, 255, 255));
				CPen pen(PS_SOLID, 1, RGB(255, 255, 255));
				CBrush* ob = dc->SelectObject(&br);
				CPen* op = dc->SelectObject(&pen);
				dc->Ellipse(px - kr, cy - kr, px + kr, cy + kr);
				dc->SelectObject(ob);
				dc->SelectObject(op);
			}
		}
		hotFill(P_FULL, m_rcFull);
		DrawIcon(dc, P_FULL, m_rcFull, text);
	}

	dc->SelectObject(oldFont);
	// 영상 칸은 자식 창이 그림 (WS_CLIPCHILDREN) - 나머지만 옮김
	pdc.BitBlt(0, 0, client.Width(), client.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(oldBmp);
}

// ---------------------------------------------------------------------------
// 마우스 · 키

void CPlayerView::Action(Part p)
{
	switch (p)
	{
	case P_BACK:  if (onBack) onBack(); break;
	case P_PLAY:  TogglePauseUser(); break;
	case P_REW:   SeekStep(-10.0); break;
	case P_FWD:   SeekStep(10.0); break;
	case P_SPEED: CycleSpeed(+1); break;
	case P_MUTE:  m_mpv.SetMute(!m_mpv.mute); break;
	case P_FULL:  SetFullscreen(!m_full); break;
	default: break;
	}
}

void CPlayerView::SeekStep(double delta)
{
	// 연속으로 누르면 (다이얼 · 키 반복) mpv 의 지금 위치는 아직 앞 탐색을 끝내지 못한 옛 위치
	//  → 목표 위치를 직접 쌓아서 늘 정확 탐색 (mpv 는 처리 전 탐색을 새 탐색으로 바꿔치기 - 줄줄이 밀리지 않음)
	//  - 키프레임 탐색을 섞으면 목표 앞 키프레임으로 뒤로 갔다가 정확 탐색으로 앞으로 와서 화면이 앞뒤로 떨림 → 쓰지 않음
	//  - 반복 중(0.25초 안)에는 잠깐 일시정지: 탐색 사이에 몇 프레임씩 재생되며 덜덜 떠는 것을 막음 → 멈추고 0.3초 뒤 다시 재생
	if (!m_open || !m_mpv.loaded)
		return;
	const ULONGLONG now = ::GetTickCount64();
	const bool rapid = (m_seekTarget >= 0.0 && now < m_lastSeekTick + 250);
	const double base = (m_seekTarget >= 0.0 && now < m_lastSeekTick + 1000) ? m_seekTarget : m_mpv.pos;
	double target = (std::max)(0.0, base + delta);
	if (m_mpv.dur > 0.0)
		target = (std::min)(target, (std::max)(0.0, m_mpv.dur - 0.5));
	m_seekTarget = target;
	m_lastSeekTick = now;
	if (rapid && !m_mpv.paused && !m_seekPaused)
	{
		m_mpv.SetPause(true);
		m_seekPaused = true;
	}
	m_mpv.SeekAbs(target, true);
	SetTimer(T_SEEKFIX, 100, nullptr);
	InvalidateRect(m_rcSeek, FALSE);
	InvalidateRect(m_rcRow, FALSE);
}

void CPlayerView::TogglePauseUser()
{
	if (!m_open)
		return;
	if (m_seekPaused)
	{
		// 연속 탐색 중 잠깐 멈춘 상태에서 사용자가 누름 = 재생 중이었던 것으로 보고 멈춤 유지
		m_seekPaused = false;
		return;
	}
	m_mpv.TogglePause();
}

void CPlayerView::CycleSpeed(int dir)
{
	// 0.25 ~ 3.0 배 사이에서 다음 / 이전 단계 (맨 끝에서 [속도] 를 누르면 1.0 으로)
	int cur = -1;
	for (int i = 0; i < static_cast<int>(_countof(kSpeeds)); ++i)
		if (std::fabs(kSpeeds[i] - m_mpv.speed) < 0.001)
			cur = i;
	if (cur < 0)
		cur = 3;
	int next = cur + dir;
	if (next >= static_cast<int>(_countof(kSpeeds)))
		next = 3;
	if (next < 0)
		next = 0;
	m_mpv.SetSpeed(kSpeeds[next]);
}

void CPlayerView::OnLButtonDown(UINT nFlags, CPoint point)
{
	CWnd::OnLButtonDown(nFlags, point);
	SetFocus();
	Activity();
	m_press = HitTest(point);
	if (m_press == P_SEEK && m_open)
	{
		m_dragSeek = true;
		m_dragSec = SeekSecAt(point.x);
		if (m_dragSec >= 0.0)
			m_mpv.SeekAbs(m_dragSec, false);
		SetCapture();
	}
	else if (m_press == P_VOL)
	{
		m_dragVol = true;
		m_mpv.SetVolume(VolAt(point.x));
		SetCapture();
	}
	else if (m_press != P_NONE)
		SetCapture();
	Invalidate(FALSE);
}

void CPlayerView::OnLButtonUp(UINT nFlags, CPoint point)
{
	CWnd::OnLButtonUp(nFlags, point);
	const Part pressed = m_press;
	m_press = P_NONE;
	if (GetCapture() == this)
		ReleaseCapture();
	if (m_dragSeek)
	{
		m_dragSeek = false;
		const double sec = SeekSecAt(point.x);
		if (sec >= 0.0)
			m_mpv.SeekAbs(sec, true);   // 놓은 위치로 정확히
		m_dragSec = -1.0;
	}
	else if (m_dragVol)
		m_dragVol = false;
	else if (pressed != P_NONE && HitTest(point) == pressed)
		Action(pressed);
	Invalidate(FALSE);
}

void CPlayerView::OnMouseMove(UINT nFlags, CPoint point)
{
	CWnd::OnMouseMove(nFlags, point);
	if (!m_trackLeave)
	{
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
		m_trackLeave = ::TrackMouseEvent(&tme) != FALSE;
	}
	Activity();
	if (m_dragSeek)
	{
		const double sec = SeekSecAt(point.x);
		if (sec >= 0.0 && std::fabs(sec - m_dragSec) > 0.05)
		{
			m_dragSec = sec;
			m_mpv.SeekAbs(sec, false);   // 끄는 동안은 키프레임으로 빠르게
		}
		InvalidateRect(m_rcSeek, FALSE);
		InvalidateRect(m_rcRow, FALSE);
		return;
	}
	if (m_dragVol)
	{
		m_mpv.SetVolume(VolAt(point.x));
		return;
	}
	const Part hot = HitTest(point);
	const double hs = (hot == P_SEEK) ? SeekSecAt(point.x) : -1.0;
	if (hot != m_hot || std::fabs(hs - m_hoverSec) > 0.01)
	{
		m_hot = hot;
		m_hoverSec = hs;
		Invalidate(FALSE);
	}
}

LRESULT CPlayerView::OnMouseLeave(WPARAM, LPARAM)
{
	m_trackLeave = false;
	if (m_hot != P_NONE || m_hoverSec >= 0.0)
	{
		m_hot = P_NONE;
		m_hoverSec = -1.0;
		Invalidate(FALSE);
	}
	return 0;
}

BOOL CPlayerView::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	(void)nFlags; (void)pt;
	Activity();
	m_mpv.SetVolume(m_mpv.volume + (zDelta > 0 ? 5.0 : -5.0));   // 휠 = 볼륨
	return TRUE;
}

bool CPlayerView::HandleKey(UINT vk)
{
	if (!m_open)
		return false;
	const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
	const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
	const bool alt = (::GetKeyState(VK_MENU) & 0x8000) != 0;
	if (alt || (ctrl && vk != VK_LEFT && vk != VK_RIGHT))
		return false;   // Ctrl + F (검색) 등 메인 창 단축키는 그대로 (Ctrl + ← → = 1초 이동만)
	Activity();
	switch (vk)
	{
	case VK_SPACE: case 'K':  TogglePauseUser(); return true;
	case VK_LEFT:   SeekStep(shift ? -m_seekBig : (ctrl ? -1.0 : -m_seekStep)); return true;
	case VK_RIGHT:  SeekStep(shift ? m_seekBig : (ctrl ? 1.0 : m_seekStep)); return true;
	case 'J':       SeekStep(-10.0); return true;
	case 'L':       SeekStep(10.0); return true;
	case VK_UP:     m_mpv.SetVolume(m_mpv.volume + 5.0); return true;
	case VK_DOWN:   m_mpv.SetVolume(m_mpv.volume - 5.0); return true;
	case 'M':       m_mpv.SetMute(!m_mpv.mute); return true;
	case 'D':       m_mpv.FrameStep(true); return true;            // D = 이전 프레임
	case 'F':       m_mpv.FrameStep(false); return true;           // F = 다음 프레임
	case VK_ESCAPE:
		if (m_full)
			SetFullscreen(false);
		else if (onBack)
			onBack();
		return true;
	case VK_OEM_PERIOD: m_mpv.FrameStep(false); return true;   // . = 다음 프레임 (일시정지)
	case VK_OEM_COMMA:  m_mpv.FrameStep(true); return true;    // , = 이전 프레임
	case VK_OEM_4:      CycleSpeed(-1); return true;            // [ = 느리게
	case VK_OEM_6:      CycleSpeed(+1); return true;            // ] = 빠르게
	case VK_BACK:       m_mpv.SetSpeed(1.0); return true;       // Backspace = 1배속
	case VK_HOME:       m_mpv.SeekAbs(0.0, true); return true;
	default:
		break;
	}
	if (vk >= '0' && vk <= '9' && !ctrl && m_mpv.dur > 0.0)
	{
		m_mpv.SeekAbs(m_mpv.dur * (vk - '0') / 10.0, true);   // 숫자 = 그 비율 위치 (0 = 처음, 5 = 가운데)
		return true;
	}
	return false;
}

BOOL CPlayerView::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN && HandleKey(static_cast<UINT>(pMsg->wParam)))
		return TRUE;
	return CWnd::PreTranslateMessage(pMsg);
}

// ---------------------------------------------------------------------------
// 전체 화면

void CPlayerView::SetFullscreen(bool on)
{
	if (on == m_full || !GetSafeHwnd())
		return;
	if (on)
	{
		CWnd* top = m_dockParent ? m_dockParent->GetTopLevelParent() : nullptr;
		const HMONITOR mon = ::MonitorFromWindow(top ? top->GetSafeHwnd() : m_hWnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { sizeof(mi) };
		::GetMonitorInfoW(mon, &mi);
		const CRect mr(mi.rcMonitor);
		if (!m_fsWnd.GetSafeHwnd())
		{
			const CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
			m_fsWnd.CreateEx(0, cls, L"RuliManager", WS_POPUP | WS_CLIPCHILDREN, mr, top, 0);
		}
		m_fsWnd.SetWindowPos(&CWnd::wndTop, mr.left, mr.top, mr.Width(), mr.Height(), SWP_SHOWWINDOW);
		m_full = true;
		m_barsShown = true;
		SetParent(&m_fsWnd);
		MoveWindow(0, 0, mr.Width(), mr.Height());
		ShowWindow(SW_SHOW);
		m_fsWnd.SetForegroundWindow();
		SetFocus();
		m_lastActivity = ::GetTickCount64();
		SetTimer(T_HIDE, 300, nullptr);
	}
	else
	{
		KillTimer(T_HIDE);
		m_full = false;
		m_barsShown = true;
		SetParent(m_dockParent);
		if (m_fsWnd.GetSafeHwnd())
			m_fsWnd.ShowWindow(SW_HIDE);
		if (onDocked) onDocked();   // 메인 창이 원래 자리로 다시 배치
		if (CWnd* top = m_dockParent ? m_dockParent->GetTopLevelParent() : nullptr)
			top->SetForegroundWindow();
		SetFocus();
	}
	Layout();
}

void CPlayerView::Activity()
{
	CPoint cur;
	::GetCursorPos(&cur);
	if (cur == m_lastMouse)
		return;   // 배치가 바뀌며 생긴 가짜 마우스 이동은 무시
	m_lastMouse = cur;
	m_lastActivity = ::GetTickCount64();
	if (m_full && !m_barsShown)
		ShowBars(true);
}

void CPlayerView::ShowBars(bool show)
{
	if (m_barsShown == show)
		return;
	m_barsShown = show;
	Layout();
	if (!show)
		::SetCursor(nullptr);
}

void CPlayerView::OnTimer(UINT_PTR id)
{
	if (id == T_SEEKFIX)
	{
		// 연속 탐색이 멈춤 (0.3초): 잠깐 멈췄던 재생을 다시
		if (::GetTickCount64() < m_lastSeekTick + 300)
			return;
		KillTimer(T_SEEKFIX);
		if (m_seekPaused)
		{
			m_seekPaused = false;
			m_mpv.SetPause(false);
		}
		m_seekTarget = -1.0;
		InvalidateRect(m_rcSeek, FALSE);
		InvalidateRect(m_rcRow, FALSE);
		return;
	}
	if (id == T_HIDE)
	{
		// 전체 화면: 마우스가 설정한 시간(기본 3초) 멈추면 컨트롤 숨김 (끄는 중 · 컨트롤 위에 마우스가 있으면 유지)
		if (!m_full)
		{
			KillTimer(T_HIDE);
			return;
		}
		CPoint cur;
		::GetCursorPos(&cur);
		ScreenToClient(&cur);
		const bool overBars = m_barsShown && (m_rcSeek.PtInRect(cur) || m_rcRow.PtInRect(cur));
		if (m_barsShown && !m_dragSeek && !m_dragVol && !overBars && ::GetTickCount64() > m_lastActivity + m_hideMs)
			ShowBars(false);
		return;
	}
	CWnd::OnTimer(id);
}
