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
	ON_WM_SETCURSOR()
	ON_WM_SETFOCUS()
	ON_WM_ENABLE()
	ON_WM_CTLCOLOR()
	ON_EN_KILLFOCUS(kEditId, &CTagChipCtrl::OnEditKillFocus)
	ON_MESSAGE(WM_SETFONT, &CTagChipCtrl::OnSetFontMsg)
	ON_MESSAGE(WM_GETFONT, &CTagChipCtrl::OnGetFontMsg)
END_MESSAGE_MAP()

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
		CBrush br(enabled ? m_chipBack : RGB(0x5C, 0x70, 0x80));
		CPen pen(PS_SOLID, 1, enabled ? m_chipBack : RGB(0x5C, 0x70, 0x80));
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
		mem.SetTextColor(enabled ? m_chipText : RGB(0x1B, 0x25, 0x2C));
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
				mem.SetTextColor(enabled ? m_subColor : RGB(0x1B, 0x25, 0x2C));
				TextFB::Draw(&mem, L" (" + sub + L")", sr, DT_LEFT, false);
			}
		}
		if (enabled)
			DrawCross(&mem, m_xRects[i], 3, m_chipText, 2);
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
		if (m_onChipClick)
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
