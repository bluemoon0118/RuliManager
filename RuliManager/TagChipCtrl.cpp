#include "pch.h"
#include "TagChipCtrl.h"
#include "TextDraw.h"
#include "VideoLibrary.h"   // 목록 구분 쉼표 (괄호 안 쉼표 제외)

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	const UINT kEditId = 100;
	const UINT_PTR kChipClickTimer = 0x7C1;   // 칩 한 번 클릭 (더블클릭 기다림)
	const int  kPad = 4;       // 바깥 여백
	const int  kGap = 4;       // 칩 사이
	const int  kChipPadL = 7;  // 칩 안 왼쪽 여백
	const int  kXW = 16;       // 칩 안 × 영역
	const int  kIconW = 22;    // 오른쪽 ×, ⌄ 버튼 폭
	const int  kMinEditW = 70;

	void DrawCross(CDC* dc, const CRect& rc, int size, COLORREF col, int width)
	{
		CPen pen(PS_SOLID, width, col);
		CPen* old = dc->SelectObject(&pen);
		const CPoint c = rc.CenterPoint();
		dc->MoveTo(c.x - size, c.y - size); dc->LineTo(c.x + size + 1, c.y + size + 1);
		dc->MoveTo(c.x + size, c.y - size); dc->LineTo(c.x - size - 1, c.y + size + 1);
		dc->SelectObject(old);
	}

	void DrawChevron(CDC* dc, const CRect& rc, int size, COLORREF col)
	{
		CPen pen(PS_SOLID, 2, col);
		CPen* old = dc->SelectObject(&pen);
		const CPoint c = rc.CenterPoint();
		POINT pts[3] = { { c.x - size, c.y - size / 2 }, { c.x, c.y + size / 2 + 1 }, { c.x + size, c.y - size / 2 } };
		dc->Polyline(pts, 3);
		dc->SelectObject(old);
	}
}

BEGIN_MESSAGE_MAP(CTagChipCtrl, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_TIMER()
	ON_WM_MOUSEMOVE()
	ON_MESSAGE(WM_MOUSELEAVE, &CTagChipCtrl::OnMouseLeave)
	ON_WM_SETCURSOR()
	ON_WM_SETFOCUS()
	ON_WM_ENABLE()
	ON_WM_CTLCOLOR()
	ON_EN_KILLFOCUS(kEditId, &CTagChipCtrl::OnEditKillFocus)
	ON_MESSAGE(WM_SETFONT, &CTagChipCtrl::OnSetFontMsg)
	ON_MESSAGE(WM_GETFONT, &CTagChipCtrl::OnGetFontMsg)
END_MESSAGE_MAP()

void CTagChipCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_trackLeave)
	{
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, GetSafeHwnd(), 0 };
		m_trackLeave = ::TrackMouseEvent(&tme) != FALSE;
	}
	int hit = -1;
	for (size_t i = 0; i < m_chipRects.size() && i < m_tags.size(); ++i)
		if (m_chipRects[i].PtInRect(point)) { hit = static_cast<int>(i); break; }
	SetHoverChip(hit);
	// 칩 × 위: 빨간 바탕 (바뀐 칩만 다시 그림)
	int hx = -1;
	if (hit >= 0 && hit < static_cast<int>(m_xRects.size()) && m_xRects[hit].PtInRect(point) && IsWindowEnabled())
		hx = hit;
	if (hx != m_hoverX)
	{
		if (m_hoverX >= 0 && m_hoverX < static_cast<int>(m_chipRects.size()))
			InvalidateRect(&m_chipRects[m_hoverX], FALSE);
		m_hoverX = hx;
		if (m_hoverX >= 0)
			InvalidateRect(&m_chipRects[m_hoverX], FALSE);
	}
	CWnd::OnMouseMove(nFlags, point);
}

LRESULT CTagChipCtrl::OnMouseLeave(WPARAM, LPARAM)
{
	m_trackLeave = false;
	SetHoverChip(-1);
	if (m_hoverX >= 0)
	{
		m_hoverX = -1;
		Invalidate(FALSE);
	}
	return 0;
}

void CTagChipCtrl::SetHoverChip(int index)
{
	if (index == m_hoverChip)
		return;
	m_hoverChip = index;
	if (!m_onChipHover)
		return;
	CRect r;
	if (index >= 0 && index < static_cast<int>(m_chipRects.size()))
	{
		r = m_chipRects[index];
		ClientToScreen(&r);
	}
	m_onChipHover(index, r);
}

LRESULT CTagChipCtrl::OnSetFontMsg(WPARAM wp, LPARAM lp)
{
	m_hFont = reinterpret_cast<HFONT>(wp);
	if (m_edit.GetSafeHwnd())
	{
		m_edit.SendMessage(WM_SETFONT, wp, lp);
		CRect rc;
		GetClientRect(&rc);
		DoLayout(rc.Width(), true);   // 글꼴이 바뀌면 칩 크기 · 입력 칸 위치 다시 계산
		if (m_onHeightChanged && m_lastHeight != 0)
			m_onHeightChanged();
	}
	if (lp)
		Invalidate(FALSE);
	return 0;
}

LRESULT CTagChipCtrl::OnGetFontMsg(WPARAM, LPARAM)
{
	return reinterpret_cast<LRESULT>(m_hFont);
}

CString CTagChipCtrl::DisplayOf(const CString& tag) const
{
	if (m_displayText)
	{
		const CString d = m_displayText(tag);
		if (!d.IsEmpty())
			return d;
	}
	return tag;
}

bool CTagChipCtrl::Create(CWnd* parent, UINT id)
{
	const CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_IBEAM), nullptr);
	if (!CWnd::Create(cls, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN, CRect(0, 0, 100, 24), parent, id))
		return false;
	SetFont(parent->GetFont());
	m_edit.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, CRect(0, 0, 10, 10), this, kEditId);
	m_edit.SetFont(parent->GetFont());
	m_edit.SetCueBanner(L"태그 입력 (↓ 목록)");
	m_edit.m_onAccept = [this](const SuggestItem&) { AddFromEdit(); };   // 목록에서 고르면 바로 칩으로
	m_backBrush.CreateSolidBrush(m_back);
	return true;
}

void CTagChipCtrl::SetColors(COLORREF back, COLORREF chipBack, COLORREF chipText, COLORREF icon, COLORREF text)
{
	m_back = back; m_chipBack = chipBack; m_chipText = chipText; m_icon = icon; m_text = text;
	if (m_backBrush.GetSafeHandle())
		m_backBrush.DeleteObject();
	m_backBrush.CreateSolidBrush(m_back);
	if (GetSafeHwnd())
		Invalidate();
}

void CTagChipCtrl::SetTags(const std::vector<CString>& tags)
{
	SetHoverChip(-1);   // 칩이 바뀌면 떠 있던 팝업은 닫음
	m_hoverX = -1;
	m_tags = tags;
	TagsChanged(false);
}

void CTagChipCtrl::TagsChanged(bool notify)
{
	if (notify && m_onChanged)
		m_onChanged();
	if (!GetSafeHwnd())
		return;
	CRect rc;
	GetClientRect(&rc);
	const int need = CalcHeight(rc.Width());
	DoLayout(rc.Width(), true);
	Invalidate();
	if (need != m_lastHeight && m_onHeightChanged)
	{
		m_lastHeight = need;
		m_onHeightChanged();   // 줄 수가 바뀌면 부모가 높이를 다시 정함
	}
}

void CTagChipCtrl::RemoveAt(int index)
{
	if (index < 0 || index >= static_cast<int>(m_tags.size()))
		return;
	m_tags.erase(m_tags.begin() + index);
	TagsChanged(true);
}

bool CTagChipCtrl::AddFromEdit()
{
	CString text;
	m_edit.GetWindowText(text);
	bool added = false;
	int pos = 0;
	for (;;)   // 쉼표로 여러 개를 한 번에 넣어도 됨
	{
		const int p = CVideoLibrary::FindListComma(text, pos);
		CString t = (p < 0) ? text.Mid(pos) : text.Mid(pos, p - pos);
		t.Trim();
		if (!t.IsEmpty())
		{
			bool dup = false;
			for (const CString& x : m_tags)
				if (x.CompareNoCase(t) == 0) { dup = true; break; }
			if (!dup)
			{
				m_tags.push_back(t);
				added = true;
			}
		}
		if (p < 0) break;
		pos = p + 1;
	}
	m_edit.SetWindowText(L"");
	if (added)
		TagsChanged(true);
	return added;
}

int CTagChipCtrl::CalcHeight(int width)
{
	return DoLayout(width, false);
}

int CTagChipCtrl::DoLayout(int width, bool apply)
{
	CClientDC dc(this);
	CFont* old = dc.SelectObject(GetFont());
	TEXTMETRIC tm = {};
	dc.GetTextMetrics(&tm);
	const int chipH = tm.tmHeight + 6;
	const int right = (std::max)(kPad + kMinEditW, width - kPad - 2 * kIconW);   // 오른쪽 ×, ⌄ 자리 제외

	std::vector<CRect> chips, xs;
	int x = kPad, y = kPad;
	for (const CString& t : m_tags)
	{
		int tw = TextFB::Width(&dc, DisplayOf(t));   // 일본어 한자 등은 다른 글꼴로 대체해서 잼
		if (m_subText)
		{
			const CString sub = m_subText(t);
			if (!sub.IsEmpty())
				tw += TextFB::Width(&dc, L" (" + sub + L")");   // 회색 보조 글자 (넘치면 칩 폭에서 잘림)
		}
		int w = kChipPadL + tw + kXW;
		if (m_iconWidth && m_drawIcon)
		{
			const int iw = m_iconWidth(t, chipH - 4);   // 이름 왼쪽 아이콘 (스튜디오 이미지 등)
			if (iw > 0)
				w += iw + 4;
		}
		w = (std::min)(w, right - kPad);   // 한 줄보다 긴 태그는 줄임
		if (x + w > right && x > kPad)
		{
			x = kPad;
			y += chipH + kGap;
		}
		CRect r(x, y, x + w, y + chipH);
		chips.push_back(r);
		xs.push_back(CRect(r.right - kXW, r.top, r.right, r.bottom));
		x += w + kGap;
	}
	if (x + kMinEditW > right && x > kPad)
	{
		x = kPad;
		y += chipH + kGap;
	}
	const CRect editRc(x, y, right, y + chipH);
	const int height = y + chipH + kPad;
	dc.SelectObject(old);

	if (apply)
	{
		m_chipRects = chips;
		m_xRects = xs;
		CRect client;
		GetClientRect(&client);
		const int iconTop = (client.Height() - chipH) / 2;
		m_clearRect = CRect(width - kPad - 2 * kIconW, iconTop, width - kPad - kIconW, iconTop + chipH);
		m_dropRect = CRect(width - kPad - kIconW, iconTop, width - kPad, iconTop + chipH);
		if (m_edit.GetSafeHwnd())
		{
			// 입력 칸은 칩과 같은 줄에, 세로 가운데
			const int eh = tm.tmHeight + 2;
			m_edit.MoveWindow(editRc.left + 2, editRc.top + (chipH - eh) / 2, (std::max)(10, editRc.Width() - 2), eh);
		}
	}
	return height;
}

void CTagChipCtrl::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	DoLayout(cx, true);
	m_lastHeight = CalcHeight(cx);
	Invalidate();
}

void CTagChipCtrl::OnPaint()
{
	CPaintDC paint(this);
	CRect rc;
	GetClientRect(&rc);
	CDC mem;
	mem.CreateCompatibleDC(&paint);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&paint, rc.Width(), rc.Height());
	CBitmap* oldBmp = mem.SelectObject(&bmp);
	CFont* oldFont = mem.SelectObject(GetFont());

	const bool enabled = IsWindowEnabled() != FALSE;
	mem.FillSolidRect(rc, m_back);
	mem.SetBkMode(TRANSPARENT);

	for (size_t i = 0; i < m_tags.size() && i < m_chipRects.size(); ++i)
	{
		const CRect& r = m_chipRects[i];
		CBrush br(enabled ? m_chipBack : TCOL(TC_BORDER));
		CPen pen(PS_SOLID, 1, enabled ? m_chipBack : TCOL(TC_BORDER));
		CBrush* ob = mem.SelectObject(&br);
		CPen* op = mem.SelectObject(&pen);
		mem.RoundRect(r, CPoint(6, 6));
		mem.SelectObject(ob);
		mem.SelectObject(op);

		CRect tr = r;
		tr.left += kChipPadL;
		tr.right -= kXW;
		if (m_iconWidth && m_drawIcon)
		{
			// 이름 왼쪽 아이콘 (칩 높이 - 4, 폭은 아이콘 비율대로)
			const int ih = r.Height() - 4;
			const int iw = m_iconWidth(m_tags[i], ih);
			if (iw > 0)
			{
				const int left = r.left + kChipPadL - 3;
				CRect ir(left, r.top + 2, (std::min)(left + iw, static_cast<int>(tr.right)), r.top + 2 + ih);
				m_drawIcon(&mem, m_tags[i], ir);
				tr.left = ir.right + 4;
			}
		}
		mem.SetTextColor(enabled ? m_chipText : TCOL(TC_EDIT));
		const CString shown = DisplayOf(m_tags[i]);
		TextFB::Draw(&mem, shown, tr, DT_LEFT, true);   // 글꼴 대체 (凪 등 일본어 한자도 표시)
		if (m_subText)
		{
			// 이름 뒤 회색 " (별칭…)" - 남은 폭만큼, 넘치면 … 없이 잘라냄
			const CString sub = m_subText(m_tags[i]);
			const int nameW = TextFB::Width(&mem, shown);
			if (!sub.IsEmpty() && tr.left + nameW < tr.right - 4)
			{
				CRect sr = tr;
				sr.left += nameW;
				mem.SetTextColor(enabled ? m_subColor : TCOL(TC_EDIT));
				TextFB::Draw(&mem, L" (" + sub + L")", sr, DT_LEFT, false);
			}
		}
		if (enabled)
		{
			if (static_cast<int>(i) == m_hoverX)
			{
				// 마우스가 올라간 ×: 빨간 둥근 바탕 + 흰 ×
				CRect xr = m_xRects[i];
				xr.DeflateRect(1, 2);
				const int d = (std::min)(xr.Width(), xr.Height());
				CRect cr(xr.CenterPoint().x - d / 2, xr.CenterPoint().y - d / 2, xr.CenterPoint().x - d / 2 + d, xr.CenterPoint().y - d / 2 + d);
				CBrush rb(RGB(0xE8, 0x3B, 0x3B));
				CPen rp(PS_SOLID, 1, RGB(0xE8, 0x3B, 0x3B));
				CBrush* ob2 = mem.SelectObject(&rb);
				CPen* op2 = mem.SelectObject(&rp);
				mem.RoundRect(cr, CPoint(d / 2, d / 2));
				mem.SelectObject(ob2);
				mem.SelectObject(op2);
				DrawCross(&mem, m_xRects[i], 3, RGB(255, 255, 255), 2);
			}
			else
				DrawCross(&mem, m_xRects[i], 3, m_chipText, 2);
		}
	}

	if (enabled)
	{
		if (!m_tags.empty())
			DrawCross(&mem, m_clearRect, 4, m_icon, 2);   // 모두 지우기
		DrawChevron(&mem, m_dropRect, 5, m_icon);         // 목록에서 선택
	}

	paint.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(oldFont);
	mem.SelectObject(oldBmp);
}

void CTagChipCtrl::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	if (!IsWindowEnabled())
		return;
	if (m_onChipDblClick)
	{
		bool onX = false;
		for (const CRect& r : m_xRects)
			if (r.PtInRect(point)) { onX = true; break; }
		const int chip = onX ? -1 : ChipAt(point);
		if (chip >= 0)
		{
			KillTimer(kChipClickTimer);
			m_pendingClick = -1;
			m_onChipDblClick(chip);
			return;
		}
	}
	OnLButtonDown(nFlags, point);   // 칩이 아닌 곳: 두 번째 클릭도 보통 클릭으로 (× · 지우기 등)
}

void CTagChipCtrl::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != kChipClickTimer)
	{
		CWnd::OnTimer(nIDEvent);
		return;
	}
	KillTimer(kChipClickTimer);
	const int chip = m_pendingClick;
	m_pendingClick = -1;
	if (chip >= 0 && chip < static_cast<int>(m_tags.size()) && m_onChipClick)
		m_onChipClick(chip, m_pendingPt);
}

int CTagChipCtrl::ChipAt(CPoint client) const
{
	for (size_t i = 0; i < m_chipRects.size() && i < m_tags.size(); ++i)
		if (m_chipRects[i].PtInRect(client))
			return static_cast<int>(i);
	return -1;
}

void CTagChipCtrl::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (!IsWindowEnabled())
		return;
	for (size_t i = 0; i < m_xRects.size(); ++i)
	{
		if (m_xRects[i].PtInRect(point))
		{
			RemoveAt(static_cast<int>(i));
			return;
		}
	}
	if (m_clearRect.PtInRect(point) && !m_tags.empty())
	{
		m_tags.clear();
		TagsChanged(true);
		return;
	}
	if (m_dropRect.PtInRect(point))
	{
		// ⌄ 버튼: 목록이 떠 있으면 닫기 (다시 누르면 닫히는 콤보처럼), 아니면 열기
		if (m_edit.GetSafeHwnd() && m_edit.IsPopupVisible())
		{
			m_edit.HidePopup();
			return;
		}
		if (m_onDropDown)
			m_onDropDown();
		return;
	}
	if (m_onChipClick)
	{
		for (size_t i = 0; i < m_chipRects.size() && i < m_tags.size(); ++i)
		{
			if (m_chipRects[i].PtInRect(point))
			{
				CPoint screen(m_chipRects[i].left, m_chipRects[i].bottom);
				ClientToScreen(&screen);
				if (m_onChipDblClick)
				{
					// 더블클릭일 수 있으니 기다렸다가 (그 사이 더블클릭이 오면 취소)
					m_pendingClick = static_cast<int>(i);
					m_pendingPt = screen;
					SetTimer(kChipClickTimer, ::GetDoubleClickTime(), nullptr);
					return;
				}
				m_onChipClick(static_cast<int>(i), screen);
				return;
			}
		}
	}
	m_edit.SetFocus();
	CWnd::OnLButtonDown(nFlags, point);
}

BOOL CTagChipCtrl::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (pWnd == this && nHitTest == HTCLIENT)
	{
		CPoint pt;
		::GetCursorPos(&pt);
		ScreenToClient(&pt);
		bool hand = m_clearRect.PtInRect(pt) || m_dropRect.PtInRect(pt);
		for (const CRect& r : m_xRects)
			if (r.PtInRect(pt)) { hand = true; break; }
		if (m_onChipClick || m_onChipDblClick)
			for (const CRect& r : m_chipRects)
				if (r.PtInRect(pt)) { hand = true; break; }
		::SetCursor(::LoadCursor(nullptr, hand ? IDC_HAND : IDC_ARROW));
		return TRUE;
	}
	return CWnd::OnSetCursor(pWnd, nHitTest, message);
}

void CTagChipCtrl::OnSetFocus(CWnd* pOldWnd)
{
	CWnd::OnSetFocus(pOldWnd);
	if (m_edit.GetSafeHwnd())
		m_edit.SetFocus();
}

void CTagChipCtrl::OnEnable(BOOL bEnable)
{
	CWnd::OnEnable(bEnable);
	if (m_edit.GetSafeHwnd())
	{
		m_edit.EnableWindow(bEnable);
		if (!bEnable)
			m_edit.SetWindowText(L"");
	}
	Invalidate();
}

HBRUSH CTagChipCtrl::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (pWnd == &m_edit)
	{
		pDC->SetTextColor(m_text);
		pDC->SetBkColor(m_back);
		return m_backBrush;
	}
	return CWnd::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CTagChipCtrl::OnEditKillFocus()
{
	// 입력하다 다른 곳을 누르면 적어 둔 태그를 추가
	AddFromEdit();
}

BOOL CTagChipCtrl::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->hwnd == m_edit.GetSafeHwnd())
	{
		if (pMsg->message == WM_KEYDOWN)
		{
			if (pMsg->wParam == VK_RETURN)
			{
				CString text;
				m_edit.GetWindowText(text);
				text.Trim();
				if (!text.IsEmpty())
				{
					AddFromEdit();   // Enter: 태그 추가 (비어 있으면 대화상자의 Enter 처리로)
					return TRUE;
				}
			}
			else if (pMsg->wParam == VK_BACK)
			{
				int s = 0, e = 0;
				m_edit.GetSel(s, e);
				if (m_edit.GetWindowTextLength() == 0 && !m_tags.empty())
				{
					RemoveAt(static_cast<int>(m_tags.size()) - 1);   // 빈 칸에서 Backspace: 마지막 태그 삭제
					return TRUE;
				}
			}
		}
		else if (pMsg->message == WM_CHAR && pMsg->wParam == L',')
		{
			// 쉼표: 태그 추가 (단, 괄호 안에서 친 쉼표는 이름의 일부로 그대로 입력)
			CString text;
			m_edit.GetWindowText(text);
			int selS = 0, selE = 0;
			m_edit.GetSel(selS, selE);
			const CString probe = text.Left(selS) + L",";
			if (CVideoLibrary::FindListComma(probe, selS) == selS)
			{
				AddFromEdit();
				return TRUE;
			}
		}
	}
	return CWnd::PreTranslateMessage(pMsg);
}

// ---------------------------------------------------------------------------
// CGenreChipBar

namespace
{
	const int kGenreGapX = 6, kGenreGapY = 6, kGenrePadX = 10, kGenreCountGap = 6;
}

BEGIN_MESSAGE_MAP(CGenreChipBar, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_LBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_MESSAGE(WM_MOUSELEAVE, &CGenreChipBar::OnMouseLeave)
	ON_WM_SETCURSOR()
END_MESSAGE_MAP()

bool CGenreChipBar::Create(CWnd* parent, UINT id)
{
	const CString cls = AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);   // 더블클릭 없음 (빠르게 두 번 = 두 번 클릭)
	return CWnd::CreateEx(0, cls, L"", WS_CHILD, CRect(0, 0, 10, 10), parent, id) != FALSE;
}

void CGenreChipBar::SetItems(const std::vector<Item>& items, int total, const CString& selected)
{
	m_items = items;
	m_total = total;
	m_sel = selected;
	m_hot = -1;
	if (GetSafeHwnd())
	{
		CRect rc;
		GetClientRect(&rc);
		CClientDC dc(this);
		CFont* old = m_font ? dc.SelectObject(m_font) : nullptr;
		DoLayout(&dc, rc.Width());
		if (old) dc.SelectObject(old);
		Invalidate(FALSE);
	}
}

CString CGenreChipBar::ChipText(int i, CString& count) const
{
	if (i == 0)
	{
		CString n;
		n.Format(L"%d", m_total);
		// 천 단위 쉼표
		for (int p = n.GetLength() - 3; p > 0; p -= 3)
			n.Insert(p, L',');
		count.Empty();
		return L"전체 " + n + L"건";
	}
	const Item& it = m_items[i - 1];
	count.Format(L"%d", it.count);
	return it.name;
}

int CGenreChipBar::DoLayout(CDC* dc, int width)
{
	m_rects.clear();
	const int lineH = dc->GetTextExtent(L"Ag가").cy;
	const int chipH = lineH + 10;
	int x = 0, y = 0;
	const int n = static_cast<int>(m_items.size()) + 1;
	for (int i = 0; i < n; ++i)
	{
		CString count;
		const CString name = ChipText(i, count);
		int w = kGenrePadX * 2 + dc->GetTextExtent(name).cx;
		if (!count.IsEmpty())
			w += kGenreCountGap + dc->GetTextExtent(count).cx;
		w = (std::min)(w, (std::max)(40, width));
		if (x > 0 && x + w > width)
		{
			x = 0;
			y += chipH + kGenreGapY;
		}
		m_rects.push_back(CRect(x, y, x + w, y + chipH));
		x += w + kGenreGapX;
	}
	// 줄마다 가운데 정렬
	for (size_t a = 0; a < m_rects.size();)
	{
		size_t b = a;
		while (b < m_rects.size() && m_rects[b].top == m_rects[a].top)
			++b;
		const int rowW = m_rects[b - 1].right - m_rects[a].left;
		const int shift = (std::max)(0, (width - rowW) / 2);
		for (size_t k = a; k < b; ++k)
			m_rects[k].OffsetRect(shift, 0);
		a = b;
	}
	return y + chipH;
}

int CGenreChipBar::CalcHeight(int width)
{
	if (!GetSafeHwnd())
		return 0;
	CClientDC dc(this);
	CFont* old = m_font ? dc.SelectObject(m_font) : nullptr;
	const int h = DoLayout(&dc, (std::max)(1, width));
	if (old) dc.SelectObject(old);
	return h;
}

int CGenreChipBar::HitTest(CPoint pt) const
{
	for (size_t i = 0; i < m_rects.size(); ++i)
		if (m_rects[i].PtInRect(pt))
			return static_cast<int>(i);
	return -1;
}

void CGenreChipBar::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	if (cx > 0)
	{
		CClientDC dc(this);
		CFont* old = m_font ? dc.SelectObject(m_font) : nullptr;
		DoLayout(&dc, cx);
		if (old) dc.SelectObject(old);
		Invalidate(FALSE);
	}
}

void CGenreChipBar::OnPaint()
{
	CPaintDC paint(this);
	CRect rc;
	GetClientRect(&rc);
	CDC mem;
	mem.CreateCompatibleDC(&paint);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&paint, (std::max)(1, rc.Width()), (std::max)(1, rc.Height()));
	CBitmap* oldBmp = mem.SelectObject(&bmp);
	CFont* oldFont = m_font ? mem.SelectObject(m_font) : nullptr;
	mem.FillSolidRect(rc, TCOL(TC_BACK));
	mem.SetBkMode(TRANSPARENT);
	for (int i = 0; i < static_cast<int>(m_rects.size()); ++i)
	{
		const CRect& r = m_rects[i];
		const bool sel = (i == 0) ? m_sel.IsEmpty() : (m_sel.CompareNoCase(m_items[i - 1].name) == 0);
		const bool hot = (i == m_hot);
		// 선택 = 빨간 계열, 마우스 오버 = 바탕에 빨간 기운 (선택 칩은 조금 밝게)
		auto mix = [](COLORREF a, COLORREF b, int pct)
		{
			return RGB((GetRValue(a) * (100 - pct) + GetRValue(b) * pct) / 100,
				(GetGValue(a) * (100 - pct) + GetGValue(b) * pct) / 100,
				(GetBValue(a) * (100 - pct) + GetBValue(b) * pct) / 100);
		};
		const COLORREF red = RGB(0xD9, 0x3B, 0x3B);
		// 칩은 모두 초록 바탕 ([전체] 포함, 저장 버튼 초록을 바탕색 쪽으로 조금 눌러 차분하게)
		const bool tagChip = true;
		const COLORREF green = mix(TCOL(TC_SAVE), TCOL(TC_BACK), 25);
		const COLORREF base = tagChip ? green : TCOL(TC_COMBO);
		COLORREF fill = sel ? red : base;
		if (hot)
			fill = sel ? mix(red, RGB(255, 255, 255), 15) : (tagChip ? mix(green, RGB(255, 255, 255), 15) : mix(TCOL(TC_COMBO), red, 35));
		const COLORREF edge = hot ? mix(fill, red, 60) : fill;
		CBrush br(fill);
		CPen pen(PS_SOLID, 1, edge);
		CBrush* ob = mem.SelectObject(&br);
		CPen* op = mem.SelectObject(&pen);
		mem.RoundRect(r, CPoint(8, 8));
		mem.SelectObject(ob);
		mem.SelectObject(op);

		CString count;
		const CString name = ChipText(i, count);
		const int cw = count.IsEmpty() ? 0 : mem.GetTextExtent(count).cx;
		CRect tr(r.left + kGenrePadX, r.top, r.right - kGenrePadX - (cw > 0 ? cw + kGenreCountGap : 0), r.bottom);
		mem.SetTextColor((sel || hot || tagChip) ? RGB(255, 255, 255) : TCOL(TC_TEXT));
		mem.DrawText(name, tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
		if (cw > 0)
		{
			CRect cr(r.right - kGenrePadX - cw, r.top, r.right - kGenrePadX, r.bottom);
			mem.SetTextColor((sel || hot) ? RGB(255, 225, 225) : (tagChip ? RGB(205, 240, 220) : TCOL(TC_SUB2)));
			mem.DrawText(count, cr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		}
	}
	paint.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	if (oldFont) mem.SelectObject(oldFont);
	mem.SelectObject(oldBmp);
}

void CGenreChipBar::OnLButtonDown(UINT nFlags, CPoint point)
{
	const int i = HitTest(point);
	if (i >= 0 && m_onSelect)
	{
		const CString pick = (i == 0) ? CString() : m_items[i - 1].name;
		if (pick.CompareNoCase(m_sel) != 0)
			m_onSelect(pick);   // 부모가 거르기 · SetItems 로 다시 표시
		return;
	}
	CWnd::OnLButtonDown(nFlags, point);
}

void CGenreChipBar::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_tracking)
	{
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
		m_tracking = ::TrackMouseEvent(&tme) != FALSE;
	}
	const int hot = HitTest(point);
	if (hot != m_hot)
	{
		m_hot = hot;
		Invalidate(FALSE);
	}
	CWnd::OnMouseMove(nFlags, point);
}

LRESULT CGenreChipBar::OnMouseLeave(WPARAM, LPARAM)
{
	m_tracking = false;
	if (m_hot != -1)
	{
		m_hot = -1;
		Invalidate(FALSE);
	}
	return 0;
}

BOOL CGenreChipBar::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	CPoint pt;
	::GetCursorPos(&pt);
	ScreenToClient(&pt);
	if (HitTest(pt) >= 0)
	{
		::SetCursor(::LoadCursor(nullptr, IDC_HAND));
		return TRUE;
	}
	return CWnd::OnSetCursor(pWnd, nHitTest, message);
}
