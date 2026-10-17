#include "pch.h"
#include "VideoGrid.h"
#include "DarkControls.h"

BEGIN_MESSAGE_MAP(CVideoGrid, CWnd)
	ON_WM_CREATE()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_VSCROLL()
	ON_WM_HSCROLL()
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
	return CWnd::CreateEx(WS_EX_CLIENTEDGE, cls, L"", WS_CHILD | WS_VSCROLL | WS_TABSTOP | WS_CLIPCHILDREN,   // 도구줄 자식 컨트롤 위는 그리지 않음
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
	if (m_tip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX))
	{
		m_tip.AddTool(this, L"");
		m_tip.SetMaxTipWidth(520);   // 긴 제목은 여러 줄로
		m_tip.SetDelayTime(TTDT_INITIAL, 400);
		m_tip.SetDelayTime(TTDT_AUTOPOP, 30000);
		m_tip.Activate(TRUE);
	}
	return 0;
}

BOOL CVideoGrid::PreTranslateMessage(MSG* pMsg)
{
	if (m_tip.GetSafeHwnd())
		m_tip.RelayEvent(pMsg);
	return CWnd::PreTranslateMessage(pMsg);
}

void CVideoGrid::UpdateTip(int row, CPoint pt)
{
	if (!m_tip.GetSafeHwnd())
		return;
	CString text;
	CRect card;
	if (row >= 0 && m_owner && m_fixedW > 0 && GetCardRect(row, card))
		text = m_owner->GridTipText(row, card, pt);
	if (text == m_tipText)
		return;
	m_tipText = text;
	m_tip.Pop();   // 다른 글자로 바뀌면 일단 닫고 새 글자로
	m_tip.UpdateTipText(text.IsEmpty() ? L"" : static_cast<LPCWSTR>(text), this);
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

void CVideoGrid::SetLines(const std::vector<GridLine>& lines)
{
	m_lines = lines;
	m_pos.clear();
	m_cellItem.clear();
	m_lineRows = 0;
	m_lineCols = 0;
}

void CVideoGrid::BuildLinePositions(int count)
{
	// 줄마다 새 화면 줄에서 시작: 첫 카드는 firstCol, 넘치면 다음 화면 줄의 wrapCol 부터
	m_pos.assign((std::max)(0, count), POINT{ 0, 0 });
	m_lineRows = 0;
	if (m_lines.empty() || count <= 0)
	{
		m_cellItem.clear();
		return;
	}
	int r = -1;
	for (size_t li = 0; li < m_lines.size(); ++li)
	{
		const int s = (std::max)(0, m_lines[li].start);
		const int e = (li + 1 < m_lines.size()) ? (std::min)(count, m_lines[li + 1].start) : count;
		if (s >= e)
			continue;
		++r;
		int c = (std::max)(0, m_lines[li].firstCol);
		for (int i = s; i < e; ++i)   // 한 화면 줄에 모두 (줄 바꿈 없음)
		{
			m_pos[i].x = c;
			m_pos[i].y = r;
			++c;
		}
	}
	if (r < 0)
		r = 0;
	// 첫 줄 앞에 빠진 카드(시작이 0 이 아닌 경우)는 맨 앞 줄로
	if (!m_lines.empty() && m_lines[0].start > 0)
		for (int i = 0; i < (std::min)(count, m_lines[0].start); ++i)
			m_pos[i] = POINT{ i, 0 };
	m_lineRows = r + 1;
	m_lineCols = 1;
	for (int i = 0; i < count; ++i)
		m_lineCols = (std::max)(m_lineCols, static_cast<int>(m_pos[i].x) + 1);
	m_cellItem.assign(static_cast<size_t>(m_lineRows) * m_lineCols, -1);
	for (int i = 0; i < count; ++i)
	{
		const size_t k = static_cast<size_t>(m_pos[i].y) * m_lineCols + m_pos[i].x;
		if (k < m_cellItem.size() && m_cellItem[k] < 0)
			m_cellItem[k] = i;
	}
}

int CVideoGrid::RowCount(int count) const
{
	if (LineReady(count))
		return m_lineRows;
	return (count + m_cols - 1) / m_cols;
}

bool CVideoGrid::PosOf(int row, int& r, int& c) const
{
	const int count = m_owner ? m_owner->GridGetCount() : 0;
	if (row < 0 || row >= count)
		return false;
	if (LineReady(count))
	{
		r = m_pos[row].y;
		c = m_pos[row].x;
		return true;
	}
	r = row / m_cols;
	c = row % m_cols;
	return true;
}

int CVideoGrid::ItemAt(int r, int c) const
{
	const int count = m_owner ? m_owner->GridGetCount() : 0;
	if (r < 0 || c < 0 || c >= Cols(count))
		return -1;
	if (LineReady(count))
	{
		const size_t k = static_cast<size_t>(r) * m_lineCols + c;
		return (k < m_cellItem.size()) ? m_cellItem[k] : -1;
	}
	const int row = r * m_cols + c;
	return (row < count) ? row : -1;
}

void CVideoGrid::ItemRangeOfRows(int r0, int r1, int& first, int& last) const
{
	// 카드 순서와 화면 줄 순서가 같으므로 범위로 구함
	first = 0;
	last = -1;
	const int count = m_owner ? m_owner->GridGetCount() : 0;
	if (count <= 0 || r1 < r0)
		return;
	if (!LineReady(count))
	{
		first = (std::max)(0, r0 * m_cols);
		last = (std::min)(count - 1, (r1 + 1) * m_cols - 1);
		return;
	}
	first = count;
	for (int i = 0; i < count; ++i)
	{
		if (m_pos[i].y < r0)
			continue;
		if (m_pos[i].y > r1)
			break;
		first = (std::min)(first, i);
		last = i;
	}
	if (last < 0)
		first = 0;
}

void CVideoGrid::MoveVert(int rows)
{
	if (!m_owner) return;
	const int count = m_owner->GridGetCount();
	if (count == 0) return;
	int sel = m_owner->GridGetSel();
	int r = 0, c = 0;
	if (sel < 0 || !PosOf(sel, r, c))
	{
		MoveSel(0);
		return;
	}
	const int total = RowCount(count);
	const int tr = (std::max)(0, (std::min)(total - 1, r + rows));
	if (tr == r)
		return;
	// 같은 칸에 카드가 없으면 그 줄에서 왼쪽으로 가장 가까운 카드 (없으면 오른쪽)
	int target = -1;
	for (int cc = c; cc >= 0 && target < 0; --cc)
		target = ItemAt(tr, cc);
	for (int cc = c + 1; cc < Cols(count) && target < 0; ++cc)
		target = ItemAt(tr, cc);
	if (target < 0)
		return;
	m_owner->GridSetSel(target);
	EnsureVisible(target);
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
	if (!m_lines.empty())
	{
		BuildLinePositions(count);   // 줄 배치: 칸 수가 바뀌면 다시 배치
		if (LineReady(count) && m_lineCols * m_cellW > rc.Width())
			m_offsetX = 0;           // 화면보다 긴 줄이 있으면 왼쪽부터 (좌우 스크롤)
	}
	UpdateHorzBar();
	const int rows = RowCount(count);
	const int total = m_headerH + rows * m_tileH;

	SCROLLINFO si = { sizeof(si) };
	si.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL;
	si.nMin = 0;
	si.nMax = (std::max)(0, total - 1);
	si.nPage = (std::max)(0, static_cast<int>(rc.Height()));
	SetScrollInfo(SB_VERT, &si, TRUE);
	SetScroll(m_scroll);   // 범위 안으로 보정
}

void CVideoGrid::UpdateHorzBar()
{
	// 줄 배치에서 가장 긴 줄이 화면보다 길면 가로 스크롤바 (아니면 숨김)
	CRect rc;
	GetClientRect(&rc);
	const int count = m_owner ? m_owner->GridGetCount() : 0;
	const int contentW = LineReady(count) ? m_offsetX + m_lineCols * m_cellW : 0;
	SCROLLINFO si = { sizeof(si) };
	si.fMask = SIF_RANGE | SIF_PAGE;
	si.nMin = 0;
	if (contentW > rc.Width())
	{
		si.nMax = contentW - 1;
		si.nPage = static_cast<UINT>((std::max)(1, static_cast<int>(rc.Width())));
	}
	else
	{
		si.nMax = 0;
		si.nPage = 1;   // 범위 없음 → 스크롤바 숨김
	}
	SetScrollInfo(SB_HORZ, &si, TRUE);
	SetScrollX(m_scrollX);   // 범위 안으로 보정
}

void CVideoGrid::SetScrollX(int pos)
{
	CRect rc;
	GetClientRect(&rc);
	const int count = m_owner ? m_owner->GridGetCount() : 0;
	const int contentW = LineReady(count) ? m_offsetX + m_lineCols * m_cellW : 0;
	const int maxPos = (std::max)(0, contentW - static_cast<int>(rc.Width()));
	pos = (std::max)(0, (std::min)(pos, maxPos));
	if (pos != m_scrollX)
	{
		m_scrollX = pos;
		Invalidate(FALSE);   // 머리 영역(도구줄)은 그대로 두고 카드만 옮김
		UpdateHot();
	}
	SetScrollPos(SB_HORZ, m_scrollX, TRUE);
}

void CVideoGrid::OnHScroll(UINT nSBCode, UINT /*nPos*/, CScrollBar* /*pScrollBar*/)
{
	CRect rc;
	GetClientRect(&rc);
	int pos = m_scrollX;
	switch (nSBCode)
	{
	case SB_LINELEFT:  pos -= m_cellW / 2; break;
	case SB_LINERIGHT: pos += m_cellW / 2; break;
	case SB_PAGELEFT:  pos -= rc.Width(); break;
	case SB_PAGERIGHT: pos += rc.Width(); break;
	case SB_LEFT:      pos = 0; break;
	case SB_RIGHT:     pos = INT_MAX / 2; break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION:
	{
		SCROLLINFO si = { sizeof(si), SIF_TRACKPOS };
		GetScrollInfo(SB_HORZ, &si);
		pos = si.nTrackPos;
		break;
	}
	default: return;
	}
	SetScrollX(pos);
}

void CVideoGrid::SetScroll(int pos)
{
	CRect rc;
	GetClientRect(&rc);
	const int count = m_owner ? m_owner->GridGetCount() : 0;
	const int rows = RowCount(count);
	const int maxPos = (std::max)(0, m_headerH + rows * m_tileH - rc.Height());
	pos = (std::max)(0, (std::min)(pos, maxPos));
	if (pos != m_scroll)
	{
		const int delta = m_scroll - pos;
		m_scroll = pos;
		// 이미 그려진 부분은 픽셀을 옮기고 새로 드러난 띠만 다시 그림 (스크롤 중 CPU 사용량 줄임)
		//  SW_SCROLLCHILDREN: 머리 영역의 도구줄 컨트롤도 같이 움직임
		ScrollWindowEx(0, delta, nullptr, nullptr, nullptr, nullptr, SW_INVALIDATE | SW_SCROLLCHILDREN);
		if (std::abs(delta) >= rc.Height())
			Invalidate(FALSE);
		UpdateHot();   // 스크롤하면 커서 아래 카드가 바뀜
	}
	SetScrollPos(SB_VERT, m_scroll, TRUE);
}

void CVideoGrid::SetHeaderHeight(int h)
{
	h = (std::max)(0, h);
	if (h == m_headerH)
		return;
	m_headerH = h;
	if (GetSafeHwnd())
	{
		UpdateLayout();
		Invalidate(FALSE);
	}
}

LRESULT CVideoGrid::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	// 머리 영역에 둔 도구줄 컨트롤(검색 · 콤보 · 버튼 등)의 알림은 원래 부모(메인 창)가 처리하던 것 → 그대로 넘김
	CWnd* parent = GetParent();
	if (parent && GetSafeHwnd())
	{
		auto mine = [this](HWND h) { return h && ::GetParent(h) == m_hWnd; };
		switch (message)
		{
		case WM_COMMAND:
			if (mine(reinterpret_cast<HWND>(lParam)))
				return parent->SendMessage(message, wParam, lParam);
			break;
		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLORBTN:
		case WM_CTLCOLORLISTBOX:
			if (mine(reinterpret_cast<HWND>(lParam)))
				return parent->SendMessage(message, wParam, lParam);
			break;
		case WM_DRAWITEM:
		{
			const DRAWITEMSTRUCT* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
			if (dis && dis->CtlType != ODT_MENU && mine(dis->hwndItem))
				return parent->SendMessage(message, wParam, lParam);
			break;
		}
		case WM_MEASUREITEM:
		{
			const MEASUREITEMSTRUCT* mis = reinterpret_cast<const MEASUREITEMSTRUCT*>(lParam);
			if (mis && mis->CtlType != ODT_MENU && wParam != 0 && ::GetDlgItem(m_hWnd, static_cast<int>(wParam)))
				return parent->SendMessage(message, wParam, lParam);
			break;
		}
		default:
			break;
		}
	}
	return CWnd::WindowProc(message, wParam, lParam);
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
	int part = 0, zone = 0;
	CRect card;
	if (row >= 0 && m_owner && m_fixedW > 0 && GetCardRect(row, card))
	{
		part = m_owner->GridHitPart(row, card, pt);
		zone = m_owner->GridHoverZone(row, card, pt);
	}
	if (row != m_hot || part != m_hotPart || zone != m_hotZone)
	{
		// 바뀐 카드(예전 / 새 마우스 오버)만 다시 그림
		CRect r;
		if (m_hot >= 0 && GetTileRect(m_hot, r))
			InvalidateRect(&r, FALSE);
		m_hot = row;
		m_hotPart = part;
		m_hotZone = zone;
		if (m_hot >= 0 && GetTileRect(m_hot, r))
			InvalidateRect(&r, FALSE);
	}
}

void CVideoGrid::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_tracking)
	{
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, GetSafeHwnd(), 0 };
		m_tracking = ::TrackMouseEvent(&tme) != FALSE;
	}
	const int row = HitTest(point);
	SetHot(row, point);
	UpdateTip(row, point);   // 잘린 제목 등 풍선 도움말
	CWnd::OnMouseMove(nFlags, point);
}

LRESULT CVideoGrid::OnMouseLeave(WPARAM, LPARAM)
{
	m_tracking = false;
	SetHot(-1, CPoint(-1, -1));
	UpdateTip(-1, CPoint(-1, -1));
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
		m_selFill = TCOL(TC_SEL);
		m_selFillNoFocus = TCOL(TC_SELNOFOCUS);
		m_text    = TCOL(TC_TEXT);
		m_accent  = TCOL(TC_ACCENTTEXT);
		m_sub     = TCOL(TC_GRIDSUB);
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
	int r = 0, c = 0;
	if (!PosOf(row, r, c))
		return false;
	const int y0 = m_headerH + r * m_tileH - m_scroll;
	const int x0 = m_offsetX + c * m_cellW - m_scrollX;   // 줄 배치만 가로 스크롤 (보통은 0)
	rc.SetRect(x0, y0, x0 + m_cellW, y0 + m_tileH);
	return true;
}

int CVideoGrid::HitTest(CPoint pt) const
{
	if (pt.x + m_scrollX < m_offsetX || pt.y < 0 || pt.y + m_scroll < m_headerH)
		return -1;   // 머리 영역 (도구줄)
	const int c = (pt.x + m_scrollX - m_offsetX) / m_cellW;
	const int r = (pt.y + m_scroll - m_headerH) / m_tileH;
	return ItemAt(r, c);
}

void CVideoGrid::EnsureVisible(int row)
{
	if (!GetSafeHwnd() || row < 0)
		return;
	CRect client;
	GetClientRect(&client);
	int r = 0, c = 0;
	if (!PosOf(row, r, c))
		return;
	{
		// 줄 배치: 가로로도 보이게
		const int left = m_offsetX + c * m_cellW;
		if (left < m_scrollX)
			SetScrollX(left);
		else if (left + m_cellW > m_scrollX + client.Width())
			SetScrollX(left + m_cellW - client.Width());
	}
	const int top = m_headerH + r * m_tileH;
	const int bottom = top + m_tileH;
	if (r == 0)
		SetScroll(0);   // 첫 줄이면 머리 영역(도구줄)까지 보이게
	else if (top < m_scroll)
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
		CRect emptyRc = client;
		emptyRc.top = (std::max)(emptyRc.top, static_cast<LONG>(m_headerH - m_scroll));
		mem.DrawText(L"표시할 동영상이 없습니다.", -1, &emptyRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}

	// 다시 그릴 영역(스크롤로 새로 드러난 띠 등)에 걸친 줄만 그림
	CRect paintRc = dc.m_ps.rcPaint;
	paintRc.IntersectRect(&paintRc, &client);
	if (paintRc.IsRectEmpty())
		paintRc = client;
	const int firstRow = (std::max)(0, m_scroll + static_cast<int>(paintRc.top) - m_headerH) / m_tileH;   // LONG → int (std::max 형 맞춤)
	const int lastRow = (m_scroll + static_cast<int>(paintRc.bottom) - m_headerH < 0) ? -1
		: (m_scroll + static_cast<int>(paintRc.bottom) - m_headerH) / m_tileH;   // 머리 영역만 다시 그리면 카드 없음
	for (int r = firstRow; r <= lastRow; ++r)
	{
		const int cols = Cols(count);
		for (int c = 0; c < cols; ++c)
		{
			{
				// 줄 배치: 화면 밖(좌우) 칸은 건너뜀
				const int x0 = m_offsetX + c * m_cellW - m_scrollX;
				if (x0 + m_cellW <= paintRc.left || x0 >= paintRc.right)
					continue;
			}
			const int row = ItemAt(r, c);
			if (row < 0)
			{
				if (LineReady(count))
					continue;   // 줄 배치: 빈 칸
				break;
			}

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
	dc.BitBlt(paintRc.left, paintRc.top, paintRc.Width(), paintRc.Height(), &mem, paintRc.left, paintRc.top, SRCCOPY);
	mem.SelectObject(oldBmp);

	// 보이는 줄 바로 아래 · 위 한 화면씩 미리 읽기 요청 (아래 먼저 - 보통 아래로 스크롤)
	if (m_owner && count > 0 && m_tileH > 0)
	{
		const int visFirst = (std::max)(0, m_scroll - m_headerH) / m_tileH;
		const int visLast = (std::max)(0, m_scroll + static_cast<int>(client.Height()) - m_headerH) / m_tileH;
		const int screenRows = (std::max)(1, visLast - visFirst + 1);
		int below0 = 0, below1 = -1, above0 = 0, above1 = -1;
		ItemRangeOfRows(visLast + 1, visLast + screenRows, below0, below1);
		if (below0 <= below1)
			m_owner->GridPrefetch(below0, below1);
		ItemRangeOfRows((std::max)(0, visFirst - screenRows), visFirst - 1, above0, above1);
		if (above0 <= above1)
			m_owner->GridPrefetch(above0, above1);
	}
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

BOOL CVideoGrid::OnMouseWheel(UINT nFlags, short zDelta, CPoint /*pt*/)
{
	if ((nFlags & MK_SHIFT) && LineReady(m_owner ? m_owner->GridGetCount() : 0))
	{
		SetScrollX(m_scrollX - zDelta * m_cellW / WHEEL_DELTA);   // Shift + 휠 = 좌우 (줄 배치)
		return TRUE;
	}
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
		if (m_fixedW > 0 && GetCardRect(row, card) && m_owner->GridDblClick(row, card, point))
			return;   // 주인이 처리함 (예: 스크린샷 추출 멈춤)
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
	const bool lines = LineReady(m_owner ? m_owner->GridGetCount() : 0);   // 줄 배치: 위 · 아래는 화면 줄 단위
	switch (nChar)
	{
	case VK_LEFT:  MoveSel(-1); return;
	case VK_RIGHT: MoveSel(+1); return;
	case VK_UP:    if (lines) MoveVert(-1); else MoveSel(-m_cols); return;
	case VK_DOWN:  if (lines) MoveVert(+1); else MoveSel(+m_cols); return;
	case VK_PRIOR: if (lines) MoveVert(-VisibleRows()); else MoveSel(-page); return;
	case VK_NEXT:  if (lines) MoveVert(+VisibleRows()); else MoveSel(+page); return;
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

LRESULT CVideoGrid::OnDarkScrollSetPos(WPARAM wParam, LPARAM lParam)
{
	if (lParam == 1)
	{
		SetScrollX(static_cast<int>(wParam));   // 가로 스크롤바 (줄 배치)
		return 0;
	}
	SetScroll(static_cast<int>(wParam));   // 직접 그린 스크롤바에서 끌기
	return 0;
}
