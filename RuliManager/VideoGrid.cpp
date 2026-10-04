#include "pch.h"
#include "VideoGrid.h"
#include "DarkControls.h"

BEGIN_MESSAGE_MAP(CVideoGrid, CWnd)
	ON_WM_CREATE()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_VSCROLL()
	ON_WM_MOUSEWHEEL()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_RBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_MESSAGE(WM_MOUSELEAVE, &CVideoGrid::OnMouseLeave)
	ON_WM_KEYDOWN()
	ON_WM_GETDLGCODE()
	ON_WM_SETFOCUS()
	ON_WM_KILLFOCUS()
	ON_MESSAGE(WM_DARKSCROLL_SETPOS, &CVideoGrid::OnDarkScrollSetPos)
END_MESSAGE_MAP()

BOOL CVideoGrid::Create(CWnd* parent, UINT id, IVideoGridOwner* owner, CImageList* images, int thumbW, int thumbH)
{
	m_owner = owner;
	m_images = images;
	m_thumbW = thumbW;
	m_thumbH = thumbH;

	const CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
	return CWnd::CreateEx(WS_EX_CLIENTEDGE, cls, L"", WS_CHILD | WS_VSCROLL | WS_TABSTOP,
		CRect(0, 0, 10, 10), parent, id);
}

int CVideoGrid::OnCreate(LPCREATESTRUCT lpcs)
{
	if (CWnd::OnCreate(lpcs) == -1)
		return -1;

	// 부모(대화상자) 글꼴을 기준으로 굵은 글꼴과 줄 높이 계산
	CFont* font = GetParent()->GetFont();
	LOGFONT lf = {};
	if (font && font->GetLogFont(&lf))
	{
		lf.lfWeight = FW_BOLD;
		m_fontBold.CreateFontIndirect(&lf);
	}

	CClientDC dc(this);
	CFont* old = dc.SelectObject(font);
	TEXTMETRIC tm = {};
	dc.GetTextMetrics(&tm);
	dc.SelectObject(old);
	m_lineH = tm.tmHeight + tm.tmExternalLeading + 1;
	m_pad = (std::max)(6, m_lineH / 2);
	return 0;
}

void CVideoGrid::SetFixedTile(int cardW, int cardH)
{
	m_fixedW = cardW;
	m_fixedH = cardH;
	if (GetSafeHwnd())
	{
		UpdateLayout();
		Invalidate(FALSE);
	}
}

void CVideoGrid::UpdateLayout()
{
	CRect rc;
	GetClientRect(&rc);
	if (m_fixedW > 0 && m_fixedH > 0)
	{
		// 고정 크기 카드: 칸 = 카드 + 여백, 남는 폭은 가운데 정렬
		m_cellW = m_fixedW + m_pad * 2;
		m_cols = (std::max)(1, rc.Width() / m_cellW);
		m_offsetX = (std::max)(0, (static_cast<int>(rc.Width()) - m_cols * m_cellW) / 2);
		m_tileH = m_fixedH + m_pad * 2;
	}
	else
	{
		const int minCell = m_thumbW + m_pad * 2;
		m_cols = (std::max)(1, rc.Width() / minCell);
		m_cellW = (std::max)(minCell, rc.Width() / m_cols);
		m_offsetX = 0;
		// 썸네일 + 파일명 1줄 + 발매일 1줄 + 메모 2줄
		m_tileH = m_pad + ImageAreaH() + m_lineH * 4 + m_pad;
	}

	const int count = m_owner ? m_owner->GridGetCount() : 0;
	const int rows = (count + m_cols - 1) / m_cols;
	const int total = rows * m_tileH;

	SCROLLINFO si = { sizeof(si) };
	si.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL;
	si.nMin = 0;
	si.nMax = (std::max)(0, total - 1);
	si.nPage = (std::max)(0, static_cast<int>(rc.Height()));
	SetScrollInfo(SB_VERT, &si, TRUE);
	SetScroll(m_scroll);   // 범위 안으로 보정
}

void CVideoGrid::SetScroll(int pos)
{
	CRect rc;
	GetClientRect(&rc);
	const int count = m_owner ? m_owner->GridGetCount() : 0;
	const int rows = (count + m_cols - 1) / m_cols;
	const int maxPos = (std::max)(0, rows * m_tileH - rc.Height());
	pos = (std::max)(0, (std::min)(pos, maxPos));
	if (pos != m_scroll)
	{
		m_scroll = pos;
		Invalidate(FALSE);
		UpdateHot();   // 스크롤하면 커서 아래 카드가 바뀜
	}
	SetScrollPos(SB_VERT, m_scroll, TRUE);
}

bool CVideoGrid::GetCardRect(int row, CRect& rc) const
{
	if (!GetTileRect(row, rc))
		return false;
	if (m_fixedW > 0)
		rc.DeflateRect(m_pad, m_pad);   // OnPaint 의 고정 카드와 같은 영역
	else
		rc.DeflateRect(m_pad / 2, m_pad / 2);
	return true;
}

void CVideoGrid::UpdateHot()
{
	if (!GetSafeHwnd())
		return;
	CPoint pt;
	::GetCursorPos(&pt);
	ScreenToClient(&pt);
	CRect client;
	GetClientRect(&client);
	SetHot(client.PtInRect(pt) ? HitTest(pt) : -1, pt);
}

void CVideoGrid::SetHot(int row, CPoint pt)
{
	int part = 0;
	CRect card;
	if (row >= 0 && m_owner && m_fixedW > 0 && GetCardRect(row, card))
		part = m_owner->GridHitPart(row, card, pt);
	if (row != m_hot || part != m_hotPart)
	{
		m_hot = row;
		m_hotPart = part;
		Invalidate(FALSE);
	}
}

void CVideoGrid::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_tracking)
	{
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, GetSafeHwnd(), 0 };
		m_tracking = ::TrackMouseEvent(&tme) != FALSE;
	}
	SetHot(HitTest(point), point);
	CWnd::OnMouseMove(nFlags, point);
}

LRESULT CVideoGrid::OnMouseLeave(WPARAM, LPARAM)
{
	m_tracking = false;
	SetHot(-1, CPoint(-1, -1));
	return 0;
}

int CVideoGrid::VisibleRows() const
{
	CRect rc;
	GetClientRect(&rc);
	return (std::max)(1, rc.Height() / (std::max)(1, m_tileH));
}

void CVideoGrid::SetBackColor(COLORREF bg)
{
	m_bg = bg;
	const int lum = (GetRValue(bg) * 299 + GetGValue(bg) * 587 + GetBValue(bg) * 114) / 1000;
	if (lum < 128)
	{
		// 어두운 배경
		auto lighten = [&](int d) { return RGB((std::min)(255, GetRValue(bg) + d), (std::min)(255, GetGValue(bg) + d), (std::min)(255, GetBValue(bg) + d)); };
		m_card    = lighten(10);
		m_border  = lighten(30);
		m_selFill = RGB(38, 79, 120);
		m_selFillNoFocus = RGB(45, 62, 78);
		m_text    = RGB(232, 238, 242);
		m_accent  = RGB(110, 180, 255);
		m_sub     = RGB(150, 165, 178);
	}
	else
	{
		m_card    = bg;
		m_border  = RGB(225, 225, 225);
		m_selFill = RGB(204, 232, 255);
		m_selFillNoFocus = RGB(229, 243, 255);
		m_text    = RGB(0, 0, 0);
		m_accent  = RGB(0, 102, 204);
		m_sub     = RGB(110, 110, 110);
	}
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

void CVideoGrid::SetShowImage(bool show)
{
	if (m_showImage == show)
		return;
	m_showImage = show;
	if (GetSafeHwnd())
	{
		UpdateLayout();
		Invalidate(FALSE);
	}
}

void CVideoGrid::Refresh()
{
	if (!GetSafeHwnd()) return;
	UpdateLayout();
	Invalidate(FALSE);
}

bool CVideoGrid::GetTileRect(int row, CRect& rc) const
{
	const int count = m_owner ? m_owner->GridGetCount() : 0;
	if (row < 0 || row >= count)
		return false;
	const int r = row / m_cols;
	const int c = row % m_cols;
	rc.SetRect(m_offsetX + c * m_cellW, r * m_tileH - m_scroll, m_offsetX + (c + 1) * m_cellW, (r + 1) * m_tileH - m_scroll);
	return true;
}

int CVideoGrid::HitTest(CPoint pt) const
{
	if (pt.x < m_offsetX || pt.y < 0)
		return -1;
	const int c = (pt.x - m_offsetX) / m_cellW;
	const int r = (pt.y + m_scroll) / m_tileH;
	if (c >= m_cols)
		return -1;
	const int row = r * m_cols + c;
	const int count = m_owner ? m_owner->GridGetCount() : 0;
	return (row >= 0 && row < count) ? row : -1;
}

void CVideoGrid::EnsureVisible(int row)
{
	if (!GetSafeHwnd() || row < 0)
		return;
	CRect client;
	GetClientRect(&client);
	const int top = (row / m_cols) * m_tileH;
	const int bottom = top + m_tileH;
	if (top < m_scroll)
		SetScroll(top);
	else if (bottom > m_scroll + client.Height())
		SetScroll(bottom - client.Height());
}

void CVideoGrid::OnSelectionChanged()
{
	if (!GetSafeHwnd()) return;
	if (m_owner)
		EnsureVisible(m_owner->GridGetSel());
	Invalidate(FALSE);
}

BOOL CVideoGrid::OnEraseBkgnd(CDC* /*pDC*/)
{
	return TRUE;
}

void CVideoGrid::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	UpdateLayout();
	Invalidate(FALSE);
}

void CVideoGrid::OnPaint()
{
	CPaintDC dc(this);
	CRect client;
	GetClientRect(&client);
	if (client.IsRectEmpty())
		return;

	CDC mem;
	mem.CreateCompatibleDC(&dc);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&dc, client.Width(), client.Height());
	CBitmap* oldBmp = mem.SelectObject(&bmp);

	mem.FillSolidRect(&client, m_bg);
	mem.SetBkMode(TRANSPARENT);

	CFont* normal = GetParent()->GetFont();
	CFont* bold = m_fontBold.GetSafeHandle() ? &m_fontBold : normal;
	CFont* oldFont = mem.SelectObject(normal);

	const int count = m_owner ? m_owner->GridGetCount() : 0;
	const int sel = m_owner ? m_owner->GridGetSel() : -1;
	const bool focused = (GetFocus() == this);

	if (count == 0)
	{
		mem.SetTextColor(m_sub);
		mem.DrawText(L"표시할 동영상이 없습니다.", -1, &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}

	const int firstRow = m_scroll / m_tileH;
	const int lastRow = (m_scroll + client.Height()) / m_tileH;
	for (int r = firstRow; r <= lastRow; ++r)
	{
		for (int c = 0; c < m_cols; ++c)
		{
			const int row = r * m_cols + c;
			if (row >= count)
				break;

			CRect tile;
			GetTileRect(row, tile);
			CRect card = tile;
			card.DeflateRect(m_pad / 2, m_pad / 2);

			// 고정 카드: 주인이 직접 그림 (배우 카드)
			if (m_fixedW > 0)
			{
				CRect fixedCard = tile;
				fixedCard.DeflateRect(m_pad, m_pad);
				if (m_owner->GridDrawCard(&mem, row, fixedCard, row == sel, focused))
					continue;
			}

			// 카드 배경 / 선택 표시
			if (row == sel)
			{
				const COLORREF fill = focused ? m_selFill : m_selFillNoFocus;
				mem.FillSolidRect(&card, fill);
				CBrush border(RGB(0, 120, 215));
				mem.FrameRect(&card, &border);
			}
			else
			{
				mem.FillSolidRect(&card, m_card);
				CBrush border(m_border);
				mem.FrameRect(&card, &border);
			}

			int image = 0;
			CString name, release, memo;
			m_owner->GridGetItem(row, image, name, release, memo);

			// 썸네일
			const int ix = tile.left + (tile.Width() - m_thumbW) / 2;
			const int iy = tile.top + m_pad;
			if (m_showImage && m_images && image >= 0)
				m_images->Draw(&mem, image, CPoint(ix, iy), ILD_NORMAL);

			// 텍스트 영역
			const int ty = iy + ImageAreaH();
			CRect text(card.left + m_pad / 2, ty, card.right - m_pad / 2, ty + m_lineH);

			mem.SelectObject(bold);
			mem.SetTextColor(m_text);
			mem.DrawText(name, &text, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

			mem.SelectObject(normal);
			text.OffsetRect(0, m_lineH);
			mem.SetTextColor(m_accent);
			mem.DrawText(release, &text, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

			text.OffsetRect(0, m_lineH);
			text.bottom = text.top + m_lineH * 2;
			memo.Replace(L"\r\n", L" ");
			memo.Replace(L'\n', L' ');
			mem.SetTextColor(m_sub);
			mem.DrawText(memo, &text, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS | DT_NOPREFIX);
		}
	}

	mem.SelectObject(oldFont);
	dc.BitBlt(0, 0, client.Width(), client.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(oldBmp);
}

void CVideoGrid::OnVScroll(UINT nSBCode, UINT /*nPos*/, CScrollBar* /*pScrollBar*/)
{
	CRect rc;
	GetClientRect(&rc);
	int pos = m_scroll;
	switch (nSBCode)
	{
	case SB_LINEUP:     pos -= m_lineH * 3; break;
	case SB_LINEDOWN:   pos += m_lineH * 3; break;
	case SB_PAGEUP:     pos -= rc.Height(); break;
	case SB_PAGEDOWN:   pos += rc.Height(); break;
	case SB_TOP:        pos = 0; break;
	case SB_BOTTOM:     pos = INT_MAX / 2; break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION:
	{
		SCROLLINFO si = { sizeof(si), SIF_TRACKPOS };
		GetScrollInfo(SB_VERT, &si);
		pos = si.nTrackPos;
		break;
	}
	default: return;
	}
	SetScroll(pos);
}

BOOL CVideoGrid::OnMouseWheel(UINT /*nFlags*/, short zDelta, CPoint /*pt*/)
{
	SetScroll(m_scroll - zDelta * m_tileH / (WHEEL_DELTA * 2));   // 한 칸에 반 줄
	return TRUE;
}

void CVideoGrid::OnLButtonDown(UINT /*nFlags*/, CPoint point)
{
	SetFocus();
	const int row = HitTest(point);
	if (row >= 0 && m_owner)
	{
		m_owner->GridSetSel(row);
		CRect card;
		if (m_fixedW > 0 && GetCardRect(row, card))
			m_owner->GridClick(row, card, point);   // 카드 위 버튼 (즐겨찾기 하트 등)
	}
}

void CVideoGrid::OnLButtonDblClk(UINT /*nFlags*/, CPoint point)
{
	const int row = HitTest(point);
	if (row >= 0 && m_owner)
	{
		m_owner->GridSetSel(row);
		CRect card;
		if (m_fixedW > 0 && GetCardRect(row, card) && m_owner->GridHitPart(row, card, point) != 0)
			return;   // 카드 위 버튼을 빠르게 두 번 누름 → 첫 클릭만 처리, 열지 않음 (정렬이 바뀌어 다른 카드가 와도 안전)
		m_owner->GridActivate(row);
	}
}

void CVideoGrid::OnRButtonDown(UINT nFlags, CPoint point)
{
	SetFocus();
	const int row = HitTest(point);
	if (row >= 0 && m_owner)
		m_owner->GridSetSel(row);
	CWnd::OnRButtonDown(nFlags, point);   // 이후 WM_CONTEXTMENU 가 부모로 전달됨
}

void CVideoGrid::MoveSel(int delta)
{
	if (!m_owner) return;
	const int count = m_owner->GridGetCount();
	if (count == 0) return;
	int sel = m_owner->GridGetSel();
	sel = (sel < 0) ? 0 : (std::max)(0, (std::min)(count - 1, sel + delta));
	m_owner->GridSetSel(sel);
	EnsureVisible(sel);
}

void CVideoGrid::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	const int page = VisibleRows() * m_cols;
	switch (nChar)
	{
	case VK_LEFT:  MoveSel(-1); return;
	case VK_RIGHT: MoveSel(+1); return;
	case VK_UP:    MoveSel(-m_cols); return;
	case VK_DOWN:  MoveSel(+m_cols); return;
	case VK_PRIOR: MoveSel(-page); return;
	case VK_NEXT:  MoveSel(+page); return;
	case VK_HOME:  MoveSel(-INT_MAX / 2); return;
	case VK_END:   MoveSel(+INT_MAX / 2); return;
	default:
		if (m_owner)
			m_owner->GridKey(nChar);
		break;
	}
	CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

UINT CVideoGrid::OnGetDlgCode()
{
	return DLGC_WANTARROWS | DLGC_WANTCHARS;
}

void CVideoGrid::OnSetFocus(CWnd* pOldWnd)
{
	CWnd::OnSetFocus(pOldWnd);
	Invalidate(FALSE);
}

void CVideoGrid::OnKillFocus(CWnd* pNewWnd)
{
	CWnd::OnKillFocus(pNewWnd);
	Invalidate(FALSE);
}

LRESULT CVideoGrid::OnDarkScrollSetPos(WPARAM wParam, LPARAM /*lParam*/)
{
	SetScroll(static_cast<int>(wParam));   // 직접 그린 스크롤바에서 끌기
	return 0;
}
