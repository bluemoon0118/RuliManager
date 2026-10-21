#include "pch.h"
#include "StarRatingCtrl.h"
#include <cmath>

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

	Gdiplus::Color ToColor(COLORREF c, BYTE a = 255)
	{
		return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
	}

	// (x, y) 를 왼쪽 위로 하는 size 크기의 다섯 꼭짓점 별
	void StarPath(Gdiplus::GraphicsPath& path, float x, float y, float size)
	{
		const float cx = x + size / 2.0f;
		const float cy = y + size * 0.53f;
		const float ro = size * 0.5f;
		const float ri = ro * 0.40f;
		Gdiplus::PointF pts[10];
		for (int i = 0; i < 10; ++i)
		{
			const double ang = -3.14159265358979 / 2 + i * 3.14159265358979 / 5;
			const float r = (i % 2 == 0) ? ro : ri;
			pts[i] = Gdiplus::PointF(cx + r * static_cast<float>(std::cos(ang)), cy + r * static_cast<float>(std::sin(ang)));
		}
		path.AddPolygon(pts, 10);
	}
}

int CStarRatingCtrl::s_mode = CStarRatingCtrl::MODE_ONE5;

CString CStarRatingCtrl::ScoreText(int halfUnits)
{
	CString t;
	if (halfUnits <= 0)
		return t;
	if (s_mode == MODE_ONE10)
		t.Format(L"%d", halfUnits);                 // 10점 만점: 반 별 = 1점
	else if (halfUnits % 2)
		t.Format(L"%d.5", halfUnits / 2);           // 5점 만점 (예전 0.5점 별점은 1점 방식에서도 그대로)
	else
		t.Format(L"%d", halfUnits / 2);
	return t;
}

CString CStarRatingCtrl::ScoreLabel(int halfUnits)
{
	CString t = ScoreText(halfUnits);
	if (!t.IsEmpty() && s_mode == MODE_ONE10)
		t += L"/10";
	return t;
}

BEGIN_MESSAGE_MAP(CStarRatingCtrl, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_MESSAGE(WM_MOUSELEAVE, &CStarRatingCtrl::OnMouseLeave)
	ON_WM_MOUSEWHEEL()
	ON_WM_KEYDOWN()
	ON_WM_GETDLGCODE()
	ON_WM_ENABLE()
	ON_WM_SETFOCUS()
	ON_WM_KILLFOCUS()
END_MESSAGE_MAP()

bool CStarRatingCtrl::Create(CWnd* parent, UINT id, int maxStars)
{
	m_max = (std::max)(1, maxStars);
	const CString cls = AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_HAND), nullptr);
	return CWnd::Create(cls, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, CRect(0, 0, 120, 20), parent, id) != FALSE;
}

void CStarRatingCtrl::SetRating(int rating)
{
	m_rating = (std::max)(0, (std::min)(m_max * 2, rating));
	if (GetSafeHwnd()) Invalidate(FALSE);
}

void CStarRatingCtrl::SetColors(COLORREF back, COLORREF star, COLORREF empty, COLORREF text)
{
	m_back = back; m_star = star; m_empty = empty; m_text = text;
	if (GetSafeHwnd()) Invalidate(FALSE);
}

int CStarRatingCtrl::StarSize() const
{
	CRect rc;
	GetClientRect(&rc);
	return (std::max)(8, rc.Height() - 4);
}

int CStarRatingCtrl::StarGap() const
{
	return (std::max)(2, StarSize() / 6);
}

int CStarRatingCtrl::HitStar(CPoint pt) const
{
	const int size = StarSize(), gap = StarGap();
	const int step = size + gap;
	if (pt.x < 0)
		return 0;
	const int i = (pt.x + gap / 2) / step;   // 별 사이 틈은 가까운 별로
	if (i >= m_max)
		return 0;
	if (HalfStep() && pt.x < i * step + size / 2)
		return i * 2 + 1;   // 별의 왼쪽 절반 (X 가운데 기준) = 반 별
	return i * 2 + 2;
}

void CStarRatingCtrl::ChangeTo(int rating)
{
	rating = (std::max)(0, (std::min)(m_max * 2, rating));
	if (rating == m_rating)
		return;
	m_rating = rating;
	Invalidate(FALSE);
	UpdateWindow();
	if (m_onChanged)
		m_onChanged(m_rating);
}

void CStarRatingCtrl::OnPaint()
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

	const bool enabled = IsWindowEnabled() != FALSE;
	const int shown = (enabled && m_hover >= 0) ? m_hover : m_rating;   // 마우스 오버 중이면 미리 보기
	const int size = StarSize(), gap = StarGap();
	const float top = (rc.Height() - size) / 2.0f;
	const BYTE alpha = enabled ? 255 : 110;

	EnsureGdiPlus();
	{
		Gdiplus::Graphics g(mem.GetSafeHdc());
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		Gdiplus::SolidBrush fill(ToColor(m_star, alpha));
		Gdiplus::Pen outline(ToColor(m_empty, alpha), (std::max)(1.0f, size / 12.0f));
		outline.SetLineJoin(Gdiplus::LineJoinRound);
		for (int i = 0; i < m_max; ++i)
		{
			Gdiplus::GraphicsPath path;
			const float pad = size * 0.06f;
			const float sx = static_cast<float>(i * (size + gap));
			StarPath(path, sx + pad, top + pad, size - pad * 2);
			if (shown >= i * 2 + 2)
				g.FillPath(&fill, &path);          // 별 하나
			else if (shown == i * 2 + 1)
			{
				// 반 별: 별 영역을 X 가운데로 나눠 왼쪽 절반만 채우고 테두리는 전체
				g.DrawPath(&outline, &path);
				g.SetClip(Gdiplus::RectF(sx, 0.0f, size / 2.0f, static_cast<Gdiplus::REAL>(rc.Height())));
				g.FillPath(&fill, &path);
				g.ResetClip();
			}
			else
				g.DrawPath(&outline, &path);
		}
	}

	// 오른쪽 숫자
	if (shown > 0)
	{
		const CString num = ScoreText(shown);   // 5점 만점 "3.5" / 10점 만점 "7"
		CFont* oldFont = mem.SelectObject(GetParent() ? GetParent()->GetFont() : nullptr);
		mem.SetBkMode(TRANSPARENT);
		mem.SetTextColor(enabled ? m_text : RGB(0x80, 0x8C, 0x96));
		CRect tr(m_max * (size + gap) + gap, 0, rc.right, rc.bottom);
		mem.DrawText(num, tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		if (oldFont) mem.SelectObject(oldFont);
	}
	if (GetFocus() == this)
	{
		CRect fr(0, 0, (std::min)(static_cast<int>(rc.right), m_max * (size + gap) + gap + size), rc.bottom);
		mem.DrawFocusRect(fr);
	}

	paint.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(old);
}

void CStarRatingCtrl::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetFocus();
	const int hit = HitStar(point);
	if (hit <= 0)
		return;
	ChangeTo(hit == m_rating ? 0 : hit);   // 같은 별을 다시 누르면 없음
	m_hover = -1;                         // 확정 후에는 확정 값 표시 (다시 움직이면 미리 보기)
	Invalidate(FALSE);
	CWnd::OnLButtonDown(nFlags, point);
}

void CStarRatingCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_tracking)
	{
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, GetSafeHwnd(), 0 };
		m_tracking = ::TrackMouseEvent(&tme) != FALSE;
	}
	const int hit = HitStar(point);
	const int hover = (hit > 0) ? hit : -1;
	if (hover != m_hover)
	{
		m_hover = hover;
		Invalidate(FALSE);
	}
	CWnd::OnMouseMove(nFlags, point);
}

LRESULT CStarRatingCtrl::OnMouseLeave(WPARAM, LPARAM)
{
	m_tracking = false;
	if (m_hover != -1)
	{
		m_hover = -1;   // 마우스가 나가면 확정된 별점으로 복원
		Invalidate(FALSE);
	}
	return 0;
}

BOOL CStarRatingCtrl::OnMouseWheel(UINT, short zDelta, CPoint)
{
	const int step = HalfStep() ? 1 : 2;
	ChangeTo(m_rating + (zDelta > 0 ? step : -step));
	return TRUE;
}

void CStarRatingCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	const int step = HalfStep() ? 1 : 2;   // 0.5점 · 10점 만점 방식이면 반 별씩
	if (nChar == VK_LEFT || nChar == VK_DOWN)
		ChangeTo(m_rating - step);
	else if (nChar == VK_RIGHT || nChar == VK_UP)
		ChangeTo(m_rating + step);
	else if (nChar == VK_HOME || nChar == VK_DELETE || nChar == VK_BACK)
		ChangeTo(0);
	else if (nChar == VK_END)
		ChangeTo(m_max * 2);
	else if (nChar >= L'0' && nChar <= L'9')
		ChangeTo(static_cast<int>(nChar - L'0') * (s_mode == MODE_ONE10 ? 1 : 2));   // 숫자 = 별 수 (10점 만점이면 점수)
	else if (nChar >= VK_NUMPAD0 && nChar <= VK_NUMPAD9)
		ChangeTo(static_cast<int>(nChar - VK_NUMPAD0) * (s_mode == MODE_ONE10 ? 1 : 2));
	else
		CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

UINT CStarRatingCtrl::OnGetDlgCode()
{
	return DLGC_WANTARROWS | DLGC_WANTCHARS;
}

void CStarRatingCtrl::OnEnable(BOOL bEnable)
{
	if (!bEnable)
		m_hover = -1;
	Invalidate(FALSE);
	CWnd::OnEnable(bEnable);
}

void CStarRatingCtrl::OnSetFocus(CWnd* pOldWnd)
{
	CWnd::OnSetFocus(pOldWnd);
	Invalidate(FALSE);
}

void CStarRatingCtrl::OnKillFocus(CWnd* pNewWnd)
{
	CWnd::OnKillFocus(pNewWnd);
	Invalidate(FALSE);
}
