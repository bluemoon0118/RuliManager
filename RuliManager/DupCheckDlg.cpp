#include "pch.h"
#include "RuliManager.h"
#include "DupCheckDlg.h"
#include "VideoLibrary.h"
#include "MediaInfoLabel.h"
#include "ImagePreview.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	CString SizeText(ULONGLONG b)
	{
		CString s;
		if (b >= 1024ULL * 1024 * 1024)
			s.Format(L"%.2f GB", b / (1024.0 * 1024 * 1024));
		else if (b >= 1024ULL * 1024)
			s.Format(L"%.1f MB", b / (1024.0 * 1024));
		else
			s.Format(L"%llu B", b);
		return s;
	}

	CString WithComma(ULONGLONG n)
	{
		CString s;
		s.Format(L"%llu", n);
		for (int i = s.GetLength() - 3; i > 0; i -= 3)
			s.Insert(i, L',');
		return s;
	}

	CString TimeText(ULONGLONG d100ns)
	{
		if (d100ns == 0)
			return L"시간 ?";
		const ULONGLONG t = (d100ns + 5000000) / 10000000;
		CString s;
		s.Format(L"%llu:%02llu:%02llu", t / 3600, (t / 60) % 60, t % 60);
		return s;
	}

	CString DateText(ULONGLONG ft)
	{
		if (ft == 0)
			return CString();
		FILETIME f;
		f.dwLowDateTime = static_cast<DWORD>(ft & 0xFFFFFFFF);
		f.dwHighDateTime = static_cast<DWORD>(ft >> 32);
		FILETIME lf;
		SYSTEMTIME st;
		if (!::FileTimeToLocalFileTime(&f, &lf) || !::FileTimeToSystemTime(&lf, &st))
			return CString();
		CString s;
		s.Format(L"수정 %04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
		return s;
	}

	CString Lower(CString s)
	{
		s.MakeLower();
		return s;
	}

	int Dpi(CWnd* w)
	{
		CClientDC dc(w);
		return (std::max)(96, dc.GetDeviceCaps(LOGPIXELSY));
	}

	const UINT kCmdPlay = 1, kCmdExplore = 2, kCmdCheck = 3;
}

// ===========================================================================
// CDupGrid

BEGIN_MESSAGE_MAP(CDupGrid, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_VSCROLL()
	ON_WM_MOUSEWHEEL()
	ON_WM_MOUSEMOVE()
	ON_MESSAGE(WM_MOUSELEAVE, &CDupGrid::OnMouseLeave)
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_CONTEXTMENU()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

bool CDupGrid::Create(CWnd* parent, const CRect& rc, UINT id)
{
	const CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), nullptr);
	if (!CWnd::CreateEx(0, cls, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | WS_CLIPSIBLINGS, rc, parent, id))
		return false;
	::SetWindowTheme(GetSafeHwnd(), L"DarkMode_Explorer", nullptr);   // 어두운 스크롤바
	const int dpi = Dpi(this);
	auto px = [dpi](int v) { return ::MulDiv(v, dpi, 96); };
	m_cardW = px(230);
	m_imgH = m_cardW * 9 / 16;
	m_lineH = px(17);
	m_pad = px(7);
	m_gap = px(10);
	m_headH = px(30);
	NONCLIENTMETRICSW ncm = { sizeof(ncm) };
	::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
	LOGFONTW lf = ncm.lfMessageFont;
	m_font.CreateFontIndirect(&lf);
	lf.lfWeight = FW_BOLD;
	m_bold.CreateFontIndirect(&lf);
	lf.lfHeight = lf.lfHeight * 6 / 5;
	m_headFont.CreateFontIndirect(&lf);
	return true;
}

void CDupGrid::OnDestroy()
{
	ClearImages();
	CWnd::OnDestroy();
}

void CDupGrid::ClearImages()
{
	for (auto& kv : m_thumbs)
		if (kv.second)
			::DeleteObject(kv.second);
	m_thumbs.clear();
}

void CDupGrid::SetGroups(const std::vector<Group>& groups)
{
	m_groups = groups;
	// 없어진 파일의 체크는 지움
	std::set<CString> keep;
	for (const Group& g : m_groups)
		for (const Card& c : g.cards)
			if (m_checked.count(Lower(c.path)))
				keep.insert(Lower(c.path));
	m_checked.swap(keep);
	m_hot = m_hotCheck = m_focus = -1;
	m_scroll = 0;
	Relayout();
	Invalidate(FALSE);
}

bool CDupGrid::IsChecked(const CString& path) const
{
	return m_checked.count(Lower(path)) > 0;
}

void CDupGrid::SetChecked(const CString& path, bool on)
{
	if (on)
		m_checked.insert(Lower(path));
	else
		m_checked.erase(Lower(path));
}

void CDupGrid::Relayout()
{
	if (!GetSafeHwnd())
		return;
	CRect rc;
	GetClientRect(&rc);
	m_slots.clear();
	m_heads.clear();
	const int cardH = m_imgH + m_pad + m_lineH * 5 + m_pad;
	const int cols = (std::max)(1, static_cast<int>((rc.Width() - m_gap) / (m_cardW + m_gap)));
	int y = m_gap;
	for (size_t g = 0; g < m_groups.size(); ++g)
	{
		m_heads.push_back({ static_cast<int>(g), CRect(m_gap, y, (std::max)(m_gap + 10, static_cast<int>(rc.Width()) - m_gap), y + m_headH) });
		y += m_headH + m_gap / 2;
		const int n = static_cast<int>(m_groups[g].cards.size());
		for (int c = 0; c < n; ++c)
		{
			const int col = c % cols, row = c / cols;
			Slot s;
			s.g = static_cast<int>(g);
			s.c = c;
			s.rc = CRect(m_gap + col * (m_cardW + m_gap), y + row * (cardH + m_gap), 0, 0);
			s.rc.right = s.rc.left + m_cardW;
			s.rc.bottom = s.rc.top + cardH;
			m_slots.push_back(s);
		}
		const int rows = (n + cols - 1) / cols;
		y += rows * (cardH + m_gap) + m_gap;
	}
	m_contentH = y;
	UpdateScroll();
}

void CDupGrid::UpdateScroll()
{
	CRect rc;
	GetClientRect(&rc);
	SCROLLINFO si = { sizeof(si) };
	si.fMask = SIF_RANGE | SIF_PAGE;
	si.nMin = 0;
	si.nMax = (std::max)(0, m_contentH - 1);
	si.nPage = static_cast<UINT>((std::max)(1, static_cast<int>(rc.Height())));
	SetScrollInfo(SB_VERT, &si, TRUE);
	ScrollTo(m_scroll);
}

void CDupGrid::ScrollTo(int y)
{
	CRect rc;
	GetClientRect(&rc);
	const int maxY = (std::max)(0, m_contentH - static_cast<int>(rc.Height()));
	y = (std::max)(0, (std::min)(y, maxY));
	if (y != m_scroll)
	{
		m_scroll = y;
		Invalidate(FALSE);
	}
	SetScrollPos(SB_VERT, m_scroll, TRUE);
}

void CDupGrid::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	Relayout();
	Invalidate(FALSE);
}

void CDupGrid::OnVScroll(UINT nSBCode, UINT /*nPos*/, CScrollBar* /*pScrollBar*/)
{
	CRect rc;
	GetClientRect(&rc);
	SCROLLINFO si = { sizeof(si) };
	si.fMask = SIF_TRACKPOS;
	GetScrollInfo(SB_VERT, &si);
	int y = m_scroll;
	switch (nSBCode)
	{
	case SB_LINEUP:        y -= m_lineH * 3; break;
	case SB_LINEDOWN:      y += m_lineH * 3; break;
	case SB_PAGEUP:        y -= rc.Height(); break;
	case SB_PAGEDOWN:      y += rc.Height(); break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION: y = si.nTrackPos; break;
	case SB_TOP:           y = 0; break;
	case SB_BOTTOM:        y = m_contentH; break;
	default: break;
	}
	ScrollTo(y);
}

BOOL CDupGrid::OnMouseWheel(UINT /*nFlags*/, short zDelta, CPoint /*pt*/)
{
	ScrollTo(m_scroll - zDelta * (m_imgH / 2) / WHEEL_DELTA);
	return TRUE;
}

int CDupGrid::SlotAt(CPoint client) const
{
	const CPoint p(client.x, client.y + m_scroll);
	for (size_t i = 0; i < m_slots.size(); ++i)
		if (m_slots[i].rc.PtInRect(p))
			return static_cast<int>(i);
	return -1;
}

CRect CDupGrid::CheckRect(const CRect& card) const
{
	const int s = m_lineH + 2;
	return CRect(card.left + m_pad / 2 + 2, card.top + m_pad / 2 + 2, card.left + m_pad / 2 + 2 + s, card.top + m_pad / 2 + 2 + s);
}

void CDupGrid::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_tracking)
	{
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
		::TrackMouseEvent(&tme);
		m_tracking = true;
	}
	const int s = SlotAt(point);
	int hc = -1;
	if (s >= 0)
	{
		CRect cr = CheckRect(m_slots[s].rc);
		cr.OffsetRect(0, -m_scroll);
		cr.InflateRect(3, 3);
		if (cr.PtInRect(point))
			hc = s;
	}
	if (s != m_hot || hc != m_hotCheck)
	{
		m_hot = s;
		m_hotCheck = hc;
		Invalidate(FALSE);
	}
	CWnd::OnMouseMove(nFlags, point);
}

LRESULT CDupGrid::OnMouseLeave(WPARAM, LPARAM)
{
	m_tracking = false;
	if (m_hot >= 0 || m_hotCheck >= 0)
	{
		m_hot = m_hotCheck = -1;
		Invalidate(FALSE);
	}
	return 0;
}

void CDupGrid::ToggleSlot(int s)
{
	if (s < 0 || s >= static_cast<int>(m_slots.size()))
		return;
	const Card& c = m_groups[m_slots[s].g].cards[m_slots[s].c];
	if (c.offline)
		return;   // 연결 안 된 디스크: 지울 수 없음
	SetChecked(c.path, !IsChecked(c.path));
	Invalidate(FALSE);   // 같은 파일이 다른 묶음에도 있을 수 있음
	if (m_onCheckChanged)
		m_onCheckChanged();
}

void CDupGrid::OnLButtonDown(UINT /*nFlags*/, CPoint point)
{
	SetFocus();
	const int s = SlotAt(point);
	if (s < 0)
		return;
	m_focus = s;
	CRect cr = CheckRect(m_slots[s].rc);
	cr.OffsetRect(0, -m_scroll);
	cr.InflateRect(3, 3);
	if (cr.PtInRect(point))
		ToggleSlot(s);
	else
		Invalidate(FALSE);
}

void CDupGrid::OnLButtonDblClk(UINT /*nFlags*/, CPoint point)
{
	const int s = SlotAt(point);
	if (s < 0)
		return;
	CRect cr = CheckRect(m_slots[s].rc);
	cr.OffsetRect(0, -m_scroll);
	cr.InflateRect(3, 3);
	if (cr.PtInRect(point))
	{
		ToggleSlot(s);   // 체크 칸을 빠르게 두 번: 두 번째도 체크 전환 (재생하지 않음)
		return;
	}
	if (m_onPlay)
		m_onPlay(m_groups[m_slots[s].g].cards[m_slots[s].c].path);
}

void CDupGrid::OnContextMenu(CWnd* /*pWnd*/, CPoint point)
{
	CPoint cp = point;
	ScreenToClient(&cp);
	const int s = SlotAt(cp);
	if (s < 0)
		return;
	m_focus = s;
	Invalidate(FALSE);
	const Card& c = m_groups[m_slots[s].g].cards[m_slots[s].c];
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING | (c.offline ? MF_GRAYED : 0), kCmdPlay, L"재생 (기본 플레이어)\t더블클릭");
	menu.AppendMenu(MF_STRING | (c.offline ? MF_GRAYED : 0), kCmdExplore, L"탐색기에서 보기");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | (c.offline ? MF_GRAYED : 0) | (IsChecked(c.path) ? MF_CHECKED : 0), kCmdCheck, L"삭제할 파일로 체크");
	const UINT cmd = static_cast<UINT>(DarkMenu::Track(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, this));
	if (cmd == kCmdPlay && m_onPlay)
		m_onPlay(c.path);
	else if (cmd == kCmdExplore && m_onExplore)
		m_onExplore(c.path);
	else if (cmd == kCmdCheck)
		ToggleSlot(s);
}

HBITMAP CDupGrid::Thumb(const Card& c, int w, int h)
{
	const CString key = Lower(c.path);
	auto it = m_thumbs.find(key);
	if (it != m_thumbs.end())
		return it->second;
	HBITMAP bmp = nullptr;
	CImage img;
	if (!c.image.IsEmpty() && LoadImageFileSfw(img, c.image) && !img.IsNull())
	{
		// 비율 유지 · 가운데 (남는 곳은 어두운 바탕)
		CClientDC sdc(this);
		CDC mem;
		mem.CreateCompatibleDC(&sdc);
		bmp = ::CreateCompatibleBitmap(sdc.GetSafeHdc(), w, h);
		HGDIOBJ old = ::SelectObject(mem.GetSafeHdc(), bmp);
		mem.FillSolidRect(0, 0, w, h, TCOL(TC_THUMB));
		const double iw = img.GetWidth(), ih = img.GetHeight();
		const double sc = (std::min)(w / iw, h / ih);
		const int dw = (std::max)(1, static_cast<int>(iw * sc + 0.5)), dh = (std::max)(1, static_cast<int>(ih * sc + 0.5));
		::SetStretchBltMode(mem.GetSafeHdc(), HALFTONE);
		::SetBrushOrgEx(mem.GetSafeHdc(), 0, 0, nullptr);
		img.StretchBlt(mem.GetSafeHdc(), (w - dw) / 2, (h - dh) / 2, dw, dh, SRCCOPY);
		::SelectObject(mem.GetSafeHdc(), old);
	}
	m_thumbs[key] = bmp;
	return bmp;
}

void CDupGrid::DrawCard(CDC* dc, const Card& c, const CRect& rc, bool hot, bool hotCheck, bool focus)
{
	const bool checked = IsChecked(c.path);
	const COLORREF back = TCOL(TC_CARD);
	dc->FillSolidRect(rc, back);
	// 커버
	const CRect img(rc.left, rc.top, rc.right, rc.top + m_imgH);
	if (HBITMAP bmp = Thumb(c, img.Width(), img.Height()))
	{
		CDC mem;
		mem.CreateCompatibleDC(dc);
		HGDIOBJ old = ::SelectObject(mem.GetSafeHdc(), bmp);
		dc->BitBlt(img.left, img.top, img.Width(), img.Height(), &mem, 0, 0, SRCCOPY);
		::SelectObject(mem.GetSafeHdc(), old);
	}
	else
	{
		dc->FillSolidRect(img, TCOL(TC_THUMB));
		dc->SetTextColor(TCOL(TC_SUB));
		dc->SelectObject(&m_font);
		CRect t = img;
		dc->DrawText(L"커버 없음", &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	if (c.offline)
	{
		CRect band(img.left, img.bottom - m_lineH - 4, img.right, img.bottom);
		dc->FillSolidRect(band, RGB(0, 0, 0));
		dc->SetTextColor(TCOL(TC_SUB));
		dc->SelectObject(&m_font);
		dc->DrawText(L"연결 안 된 디스크 (삭제 불가)", &band, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	// 글자 줄
	dc->SetBkMode(TRANSPARENT);
	CRect line(rc.left + m_pad, img.bottom + m_pad / 2, rc.right - m_pad, img.bottom + m_pad / 2 + m_lineH);
	auto put = [&](const CString& t, COLORREF col, CFont* f, UINT extra)
	{
		dc->SelectObject(f);
		dc->SetTextColor(col);
		CRect r = line;
		dc->DrawText(t, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | extra);
		line.OffsetRect(0, m_lineH);
	};
	put(c.name, TCOL(TC_TEXT), &m_bold, DT_END_ELLIPSIS);
	put(c.sizeText + L"   " + c.timeText, TCOL(TC_TEXT), &m_font, DT_END_ELLIPSIS);
	put(c.resText, TCOL(TC_SUB), &m_font, DT_END_ELLIPSIS);
	put(c.dateText, TCOL(TC_SUB), &m_font, DT_END_ELLIPSIS);
	put(c.folder, TCOL(TC_SUB2), &m_font, DT_PATH_ELLIPSIS);
	// 테두리: 체크 = 빨강, 포커스 · 마우스 = 밝게
	if (checked || hot || focus)
	{
		const COLORREF col = checked ? TCOL(TC_DANGER) : (focus ? TCOL(TC_ACCENT) : TCOL(TC_CARDHOT));
		CPen pen(PS_SOLID, checked ? 3 : 2, col);
		CPen* op = dc->SelectObject(&pen);
		CGdiObject* ob = dc->SelectStockObject(NULL_BRUSH);
		dc->Rectangle(rc.left + 1, rc.top + 1, rc.right, rc.bottom);
		dc->SelectObject(ob);
		dc->SelectObject(op);
	}
	// 체크 칸 (왼쪽 위): 체크했으면 항상, 아니면 마우스가 카드 위에 있을 때
	if (!c.offline && (checked || hot))
	{
		const CRect cr = CheckRect(rc);
		dc->FillSolidRect(cr, checked ? TCOL(TC_DANGER) : RGB(20, 26, 31));
		CPen pen(PS_SOLID, 1, hotCheck || checked ? RGB(255, 255, 255) : TCOL(TC_SUB));
		CPen* op = dc->SelectObject(&pen);
		CGdiObject* ob = dc->SelectStockObject(NULL_BRUSH);
		dc->Rectangle(cr);
		dc->SelectObject(ob);
		dc->SelectObject(op);
		if (checked)
		{
			CPen tick(PS_SOLID, (std::max)(2, cr.Width() / 7), RGB(255, 255, 255));
			op = dc->SelectObject(&tick);
			dc->MoveTo(cr.left + cr.Width() / 5, cr.top + cr.Height() / 2);
			dc->LineTo(cr.left + cr.Width() * 2 / 5, cr.bottom - cr.Height() / 4);
			dc->LineTo(cr.right - cr.Width() / 5, cr.top + cr.Height() / 4);
			dc->SelectObject(op);
		}
	}
}

void CDupGrid::OnPaint()
{
	CPaintDC paint(this);
	CRect rc;
	GetClientRect(&rc);
	CDC mem;
	mem.CreateCompatibleDC(&paint);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&paint, (std::max)(1, rc.Width()), (std::max)(1, rc.Height()));
	CBitmap* oldBmp = mem.SelectObject(&bmp);
	CFont* oldFont = mem.SelectObject(&m_font);
	mem.FillSolidRect(rc, TCOL(TC_BACK));
	mem.SetBkMode(TRANSPARENT);
	if (m_groups.empty())
	{
		const CString msg = L"중복 파일이 없습니다.\n\n위에서 검사 기준(같은 크기 · 같은 품번)을 고르고 [검사] 를 누르세요.";
		mem.SetTextColor(TCOL(TC_SUB));
		CRect m = rc;
		mem.DrawText(msg, &m, DT_CENTER | DT_WORDBREAK | DT_CALCRECT);
		CRect t(rc.left, (rc.Height() - m.Height()) / 2, rc.right, (rc.Height() - m.Height()) / 2 + m.Height());
		mem.DrawText(msg, &t, DT_CENTER | DT_WORDBREAK);
	}
	for (const auto& h : m_heads)
	{
		CRect r = h.second;
		r.OffsetRect(0, -m_scroll);
		if (r.bottom < 0 || r.top > rc.bottom)
			continue;
		mem.FillSolidRect(r, TCOL(TC_COMBO));
		mem.SelectObject(&m_headFont);
		mem.SetTextColor(TCOL(TC_TEXT));
		CRect t = r;
		t.DeflateRect(m_pad, 0);
		mem.DrawText(m_groups[h.first].title, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	}
	for (size_t i = 0; i < m_slots.size(); ++i)
	{
		CRect r = m_slots[i].rc;
		r.OffsetRect(0, -m_scroll);
		if (r.bottom < 0 || r.top > rc.bottom)
			continue;
		DrawCard(&mem, m_groups[m_slots[i].g].cards[m_slots[i].c], r, static_cast<int>(i) == m_hot, static_cast<int>(i) == m_hotCheck, static_cast<int>(i) == m_focus);
	}
	paint.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(oldFont);
	mem.SelectObject(oldBmp);
}

// ===========================================================================
// CDupCheckDlg

CDupCheckDlg::CDupCheckDlg(CVideoLibrary& lib, CWnd* pParent)
	: CDialogEx(IDD_DUP_CHECK, pParent), m_lib(lib)
{
}

void CDupCheckDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CDupCheckDlg, CDialogEx)
	ON_WM_CTLCOLOR()
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_BN_CLICKED(IDC_DUP_SCAN, &CDupCheckDlg::OnScan)
	ON_BN_CLICKED(IDC_DUP_AUTO, &CDupCheckDlg::OnAuto)
	ON_BN_CLICKED(IDC_DUP_CLEAR, &CDupCheckDlg::OnClearChecks)
	ON_BN_CLICKED(IDC_DUP_REMOVE, &CDupCheckDlg::OnRemove)
	ON_BN_CLICKED(IDC_DUP_SIZE, &CDupCheckDlg::OnScan)
	ON_BN_CLICKED(IDC_DUP_CODE, &CDupCheckDlg::OnScan)
END_MESSAGE_MAP()

BOOL CDupCheckDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	CheckDlgButton(IDC_DUP_SIZE, AfxGetApp()->GetProfileInt(L"Settings", L"DupBySize", 1) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_DUP_CODE, AfxGetApp()->GetProfileInt(L"Settings", L"DupByCode", 1) ? BST_CHECKED : BST_UNCHECKED);
	m_theme.Apply(this, { IDC_DUP_REMOVE });

	CRect frame;
	GetDlgItem(IDC_DUP_GRID)->GetWindowRect(&frame);
	ScreenToClient(&frame);
	GetDlgItem(IDC_DUP_GRID)->ShowWindow(SW_HIDE);   // 자리 표시용
	m_grid.Create(this, frame, IDC_DUP_GRID + 1000);
	m_grid.m_onPlay = [this](const CString& path)
	{
		if (!::PathFileExistsW(path))
		{
			MessageBox(L"파일을 찾을 수 없습니다.\n" + path, L"중복 파일 검사", MB_ICONINFORMATION);
			return;
		}
		::ShellExecuteW(GetSafeHwnd(), L"open", path, nullptr, nullptr, SW_SHOWNORMAL);
	};
	m_grid.m_onExplore = [this](const CString& path)
	{
		::ShellExecuteW(GetSafeHwnd(), L"open", L"explorer.exe", L"/select,\"" + path + L"\"", nullptr, SW_SHOWNORMAL);
	};
	m_grid.m_onCheckChanged = [this]() { UpdateStatus(); };

	CRect wr;
	GetWindowRect(&wr);
	m_minSize = wr.Size();
	Scan();
	return TRUE;
}

HBRUSH CDupCheckDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
		return br;
	return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CDupCheckDlg::OnGetMinMaxInfo(MINMAXINFO* mmi)
{
	if (m_minSize.cx > 0)
	{
		mmi->ptMinTrackSize.x = m_minSize.cx * 3 / 4;
		mmi->ptMinTrackSize.y = m_minSize.cy * 2 / 3;
	}
	CDialogEx::OnGetMinMaxInfo(mmi);
}

void CDupCheckDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialogEx::OnSize(nType, cx, cy);
	if (m_grid.GetSafeHwnd() && nType != SIZE_MINIMIZED)
		LayoutControls(cx, cy);
}

void CDupCheckDlg::LayoutControls(int cx, int cy)
{
	CRect m(7, 7, 7, 7);
	MapDialogRect(&m);
	auto rectOf = [this](UINT id) { CRect r; GetDlgItem(id)->GetWindowRect(&r); ScreenToClient(&r); return r; };
	if (CWnd* close = GetDlgItem(IDCANCEL))
	{
		const CRect r = rectOf(IDCANCEL);
		close->SetWindowPos(nullptr, cx - m.left - r.Width(), r.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
	const CRect st = rectOf(IDC_DUP_STATUS);
	const int stY = cy - m.top - st.Height();
	GetDlgItem(IDC_DUP_STATUS)->SetWindowPos(nullptr, m.left, stY, cx - m.left * 2, st.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
	CRect g;
	m_grid.GetWindowRect(&g);
	ScreenToClient(&g);
	const int mx = static_cast<int>(m.left), my = static_cast<int>(m.top), gTop = static_cast<int>(g.top);
	const int gridH = (std::max)(40, stY - my / 2 - gTop);
	m_grid.SetWindowPos(nullptr, mx, gTop, cx - mx * 2, gridH, SWP_NOZORDER | SWP_NOACTIVATE);
	Invalidate();
}

void CDupCheckDlg::OnScan()
{
	AfxGetApp()->WriteProfileInt(L"Settings", L"DupBySize", IsDlgButtonChecked(IDC_DUP_SIZE) ? 1 : 0);
	AfxGetApp()->WriteProfileInt(L"Settings", L"DupByCode", IsDlgButtonChecked(IDC_DUP_CODE) ? 1 : 0);
	Scan();
}

void CDupCheckDlg::Scan()
{
	CWaitCursor wait;
	const bool bySize = IsDlgButtonChecked(IDC_DUP_SIZE) == BST_CHECKED;
	const bool byCode = IsDlgButtonChecked(IDC_DUP_CODE) == BST_CHECKED;
	const auto& items = m_lib.items;

	// 묶음 찾기 (항목 번호 목록)
	struct Raw { int kind; CString key; std::vector<int> members; };   // kind: 0 = 크기, 1 = 품번, 2 = 둘 다
	std::vector<Raw> raws;
	if (bySize)
	{
		std::map<ULONGLONG, std::vector<int>> bySz;
		for (size_t i = 0; i < items.size(); ++i)
			if (items[i].size > 0)
				bySz[items[i].size].push_back(static_cast<int>(i));
		for (const auto& kv : bySz)
			if (kv.second.size() >= 2)
			{
				CString k;
				k.Format(L"%llu", kv.first);
				raws.push_back({ 0, k, kv.second });
			}
	}
	if (byCode)
	{
		// 같은 품번 + 같은 파트 번호 (분할 영상 ABC-123_1 · _2 는 중복이 아님)
		std::map<CString, std::vector<int>> byCd;
		std::map<CString, CString> shown;   // 키 → 표시 품번
		for (size_t i = 0; i < items.size(); ++i)
		{
			CString code = items[i].code;
			code.Trim();
			if (code.IsEmpty())
				code = CVideoLibrary::ExtractCode(items[i].FileName());
			code.Trim();
			if (code.IsEmpty())
				continue;
			CString pk;
			int part = 0;
			CVideoLibrary::PartGroupKey(items[i].path, pk, part);
			CString key = code;
			key.MakeUpper();
			CString k2;
			k2.Format(L"%s#%d", static_cast<LPCWSTR>(key), part);
			byCd[k2].push_back(static_cast<int>(i));
			CString disp = key;
			if (part > 0)
			{
				CString ps;
				ps.Format(L" (파트 %d)", part);
				disp += ps;
			}
			shown[k2] = disp;
		}
		for (const auto& kv : byCd)
		{
			if (kv.second.size() < 2)
				continue;
			// 같은 크기 묶음과 구성이 똑같으면 하나로 ("품번 · 크기 같음")
			std::vector<int> a = kv.second;
			std::sort(a.begin(), a.end());
			bool merged = false;
			for (Raw& r : raws)
			{
				if (r.kind != 0)
					continue;
				std::vector<int> b = r.members;
				std::sort(b.begin(), b.end());
				if (a == b)
				{
					r.kind = 2;
					r.key = shown[kv.first] + L"|" + r.key;
					merged = true;
					break;
				}
			}
			if (!merged)
				raws.push_back({ 1, shown[kv.first], kv.second });
		}
	}

	// 카드 정보 (재생 시간 · 해상도는 캐시에서)
	std::map<int, CDupGrid::Card> cardOf;
	auto cardFor = [&](int i) -> CDupGrid::Card
	{
		auto it = cardOf.find(i);
		if (it != cardOf.end())
			return it->second;
		const VideoItem& v = items[i];
		CDupGrid::Card c;
		c.item = i;
		c.path = v.path;
		c.name = v.FileName();
		c.folder = v.path.Left(static_cast<int>(::PathFindFileNameW(v.path) - static_cast<LPCWSTR>(v.path)));
		c.folder.TrimRight(L'\\');
		c.code = v.code;
		c.size = v.size;
		c.sizeText = SizeText(v.size);
		c.offline = m_isOffline ? m_isOffline(i) : false;
		c.dateText = DateText(v.modified);
		if (!c.offline)
		{
			const CMediaInfoLabel::Info info = CMediaInfoLabel::ReadInfoCached(v.path);
			c.timeText = TimeText(info.duration100ns);
			if (info.width > 0 && info.height > 0)
			{
				c.resText.Format(L"%u × %u", info.width, info.height);
				if (info.fps > 0)
				{
					CString f;
					f.Format(L" · %.2f fps", info.fps);
					c.resText += f;
				}
			}
			else
				c.resText = L"해상도 ?";
			c.height = info.height;
			if (m_findImage)
				c.image = m_findImage(v.path);
		}
		else
		{
			c.timeText = L"";
			c.resText = L"(연결 안 된 디스크)";
		}
		cardOf[i] = c;
		return c;
	};

	std::vector<CDupGrid::Group> groups;
	for (const Raw& r : raws)
	{
		CDupGrid::Group g;
		ULONGLONG total = 0;
		for (int i : r.members)
		{
			g.cards.push_back(cardFor(i));
			total += items[i].size;
		}
		// 큰 파일 · 해상도 높은 것 먼저
		std::stable_sort(g.cards.begin(), g.cards.end(), [](const CDupGrid::Card& a, const CDupGrid::Card& b)
		{
			if (a.height != b.height) return a.height > b.height;
			return a.size > b.size;
		});
		CString cnt;
		cnt.Format(L"%d개", static_cast<int>(r.members.size()));
		if (r.kind == 0)
		{
			const ULONGLONG sz = _wcstoui64(r.key, nullptr, 10);
			g.title = L"같은 크기 · " + SizeText(sz) + L" (" + WithComma(sz) + L" 바이트) · " + cnt;
		}
		else if (r.kind == 1)
			g.title = L"같은 품번 · " + r.key + L" · " + cnt + L" (합계 " + SizeText(total) + L")";
		else
		{
			const int bar = r.key.Find(L'|');
			const ULONGLONG sz = _wcstoui64(r.key.Mid(bar + 1), nullptr, 10);
			g.title = L"같은 품번 · 같은 크기 · " + r.key.Left(bar) + L" · " + SizeText(sz) + L" · " + cnt;
		}
		groups.push_back(g);
	}
	// 같은 품번 · 크기 → 품번 → 크기 순, 같은 종류 안에서는 제목 순
	std::stable_sort(groups.begin(), groups.end(), [](const CDupGrid::Group& a, const CDupGrid::Group& b)
	{
		auto rank = [](const CString& t) { return t.Find(L"같은 품번 · 같은 크기") == 0 ? 0 : (t.Find(L"같은 품번") == 0 ? 1 : 2); };
		const int ra = rank(a.title), rb = rank(b.title);
		if (ra != rb) return ra < rb;
		return ::StrCmpLogicalW(a.title, b.title) < 0;
	});
	m_grid.ClearImages();
	m_grid.SetGroups(groups);
	UpdateStatus();
}

void CDupCheckDlg::UpdateStatus()
{
	std::set<CString> files;
	for (const CDupGrid::Group& g : m_grid.Groups())
		for (const CDupGrid::Card& c : g.cards)
			files.insert(CString(c.path).MakeLower());
	ULONGLONG checkedBytes = 0;
	int checked = 0;
	std::set<CString> counted;
	for (const CDupGrid::Group& g : m_grid.Groups())
		for (const CDupGrid::Card& c : g.cards)
			if (m_grid.IsChecked(c.path) && counted.insert(CString(c.path).MakeLower()).second)
			{
				++checked;
				checkedBytes += c.size;
			}
	CString s;
	s.Format(L"중복 묶음 %d개 · 파일 %d개 · 삭제할 파일로 체크 %d개 (%s)   카드 더블클릭 = 재생, 왼쪽 위 칸 = 체크",
		static_cast<int>(m_grid.Groups().size()), static_cast<int>(files.size()), checked, static_cast<LPCWSTR>(SizeText(checkedBytes)));
	SetDlgItemText(IDC_DUP_STATUS, s);
	GetDlgItem(IDC_DUP_REMOVE)->EnableWindow(checked > 0);
	GetDlgItem(IDC_DUP_CLEAR)->EnableWindow(checked > 0);
	GetDlgItem(IDC_DUP_AUTO)->EnableWindow(!m_grid.Groups().empty());
}

void CDupCheckDlg::OnAuto()
{
	// 묶음마다 하나만 남기고 나머지를 체크: 해상도가 높은 것 → 크기가 큰 것을 남김 (연결 안 된 디스크 영상은 체크하지 않음)
	for (const CDupGrid::Group& g : m_grid.Groups())
	{
		int keep = -1;
		for (size_t k = 0; k < g.cards.size(); ++k)
		{
			if (m_grid.IsChecked(g.cards[k].path))
				continue;
			if (keep < 0 || g.cards[k].height > g.cards[keep].height ||
				(g.cards[k].height == g.cards[keep].height && g.cards[k].size > g.cards[keep].size))
				keep = static_cast<int>(k);
		}
		for (size_t k = 0; k < g.cards.size(); ++k)
			if (static_cast<int>(k) != keep && !g.cards[k].offline)
				m_grid.SetChecked(g.cards[k].path, true);
	}
	m_grid.Invalidate(FALSE);
	UpdateStatus();
}

void CDupCheckDlg::OnClearChecks()
{
	m_grid.Checked().clear();
	m_grid.Invalidate(FALSE);
	UpdateStatus();
}

void CDupCheckDlg::OnRemove()
{
	std::vector<CString> paths;
	std::set<CString> seen;
	ULONGLONG bytes = 0;
	for (const CDupGrid::Group& g : m_grid.Groups())
		for (const CDupGrid::Card& c : g.cards)
			if (m_grid.IsChecked(c.path) && !c.offline && seen.insert(CString(c.path).MakeLower()).second)
			{
				paths.push_back(c.path);
				bytes += c.size;
			}
	if (paths.empty() || !m_remove)
		return;
	// 한 묶음의 파일이 모두 체크됐으면 경고 (모두 지우면 남는 것이 없음)
	int allGone = 0;
	for (const CDupGrid::Group& g : m_grid.Groups())
	{
		bool all = true;
		for (const CDupGrid::Card& c : g.cards)
			if (!m_grid.IsChecked(c.path)) { all = false; break; }
		if (all)
			++allGone;
	}
	CString msg;
	msg.Format(L"체크한 파일 %d개 (%s) 를 휴지통으로 보내고 DB 에서도 삭제할까요?\n\n", static_cast<int>(paths.size()), static_cast<LPCWSTR>(SizeText(bytes)));
	for (size_t k = 0; k < paths.size() && k < 12; ++k)
		msg += L"  " + paths[k] + L"\n";
	if (paths.size() > 12)
	{
		CString more;
		more.Format(L"  … 외 %d개\n", static_cast<int>(paths.size() - 12));
		msg += more;
	}
	if (allGone > 0)
	{
		CString w;
		w.Format(L"\n주의: 묶음 %d개는 모든 파일이 체크되어 있어 하나도 남지 않습니다.\n", allGone);
		msg += w;
	}
	msg += L"\n(휴지통에서 되살릴 수 있습니다. 커버 이미지 · 정보 txt 는 그대로 둡니다)";
	if (MessageBox(msg, L"중복 파일 삭제", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
		return;
	m_grid.ClearImages();
	const std::vector<CString> failed = m_remove(paths);
	for (const CString& p : paths)
		m_grid.SetChecked(p, false);
	for (const CString& p : failed)
		m_grid.SetChecked(p, true);   // 지우지 못한 파일은 체크 그대로
	Scan();
	CString done;
	if (failed.empty())
		done.Format(L"파일 %d개를 휴지통으로 보내고 DB 에서 삭제했습니다.", static_cast<int>(paths.size()));
	else
	{
		done.Format(L"파일 %d개를 삭제했습니다. %d개는 지우지 못했습니다 (사용 중이거나 권한이 없을 수 있음):\n\n",
			static_cast<int>(paths.size() - failed.size()), static_cast<int>(failed.size()));
		for (size_t k = 0; k < failed.size() && k < 10; ++k)
			done += L"  " + failed[k] + L"\n";
	}
	MessageBox(done, L"중복 파일 삭제", failed.empty() ? MB_ICONINFORMATION : MB_ICONWARNING);
}
