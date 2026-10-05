#include "pch.h"
#include "SuggestEdit.h"
#include "TextDraw.h"   // 글꼴 대체 (凪 등 일본어 한자)
#include "VideoLibrary.h"   // 목록 구분 쉼표 (괄호 안 쉼표 제외)

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	const int kMaxVisible = 8;     // 한 번에 보이는 후보 수
	const size_t kMaxItems = 100;  // 후보 최대 개수
}

// ---------------------------------------------------------------------------
// CSuggestList

BEGIN_MESSAGE_MAP(CSuggestList, CListBox)
	ON_WM_MOUSEACTIVATE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_MOUSEMOVE()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

int CSuggestList::OnMouseActivate(CWnd*, UINT, UINT)
{
	return MA_NOACTIVATE;   // 클릭해도 에디트의 포커스를 유지
}

void CSuggestList::OnLButtonDown(UINT, CPoint point)
{
	// 기본 처리(SetFocus)를 하지 않고 바로 선택 → 에디트가 포커스를 잃지 않음
	BOOL outside = TRUE;
	const UINT idx = ItemFromPoint(point, outside);
	if (!outside && m_edit)
		m_edit->Accept(static_cast<int>(idx));
}

void CSuggestList::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	OnLButtonDown(nFlags, point);
}

void CSuggestList::OnMouseMove(UINT, CPoint point)
{
	BOOL outside = TRUE;
	const UINT idx = ItemFromPoint(point, outside);
	if (!outside && static_cast<int>(idx) != GetCurSel())
		SetCurSel(static_cast<int>(idx));
}

BOOL CSuggestList::OnEraseBkgnd(CDC* pDC)
{
	CRect rc;
	GetClientRect(&rc);
	pDC->FillSolidRect(rc, m_back);
	return TRUE;
}

void CSuggestList::DrawItem(LPDRAWITEMSTRUCT lpDIS)
{
	CDC dc;
	dc.Attach(lpDIS->hDC);
	CRect rc(lpDIS->rcItem);
	const bool selected = (lpDIS->itemState & ODS_SELECTED) != 0;
	dc.FillSolidRect(rc, selected ? m_sel : m_back);

	if (lpDIS->itemID != static_cast<UINT>(-1) && lpDIS->itemID < m_items.size())
	{
		CString text = m_items[lpDIS->itemID].text;
		CString hint;
		const int tab = text.Find(L'\t');
		if (tab >= 0)
		{
			hint = text.Mid(tab + 1);
			text = text.Left(tab);
		}
		dc.SetBkMode(TRANSPARENT);
		CRect tr = rc;
		tr.DeflateRect(6, 0);
		if (!hint.IsEmpty())
		{
			dc.SetTextColor(selected ? m_text : m_hint);
			TextFB::Draw(&dc, hint, tr, DT_RIGHT, true);
			const int hw = TextFB::Width(&dc, hint);
			tr.right = (std::max)(tr.left, tr.right - hw - 12);
		}
		dc.SetTextColor(m_text);
		TextFB::Draw(&dc, text, tr, DT_LEFT, true);   // 글꼴 대체 (凪 등 일본어 한자도 표시)
	}
	dc.Detach();
}

// ---------------------------------------------------------------------------
// CSuggestEdit

BEGIN_MESSAGE_MAP(CSuggestEdit, CEdit)
	ON_CONTROL_REFLECT_EX(EN_CHANGE, &CSuggestEdit::OnEnChangeReflect)
	ON_WM_KILLFOCUS()
	ON_WM_MOUSEWHEEL()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

void CSuggestEdit::Setup(Provider provider, bool multi)
{
	m_provider = std::move(provider);
	m_multi = multi;
}

void CSuggestEdit::SetColors(COLORREF back, COLORREF text, COLORREF sel, COLORREF hint)
{
	m_popup.m_back = back;
	m_popup.m_text = text;
	m_popup.m_sel = sel;
	m_popup.m_hint = hint;
}

bool CSuggestEdit::IsPopupVisible() const
{
	return m_popup.GetSafeHwnd() && m_popup.IsWindowVisible();
}

void CSuggestEdit::HidePopup()
{
	if (IsPopupVisible())
		m_popup.ShowWindow(SW_HIDE);
}

CString CSuggestEdit::CurrentToken(int& start, int& end) const
{
	CString text;
	GetWindowText(text);
	if (!m_multi)
	{
		start = 0;
		end = text.GetLength();
	}
	else
	{
		int selStart = 0, selEnd = 0;
		GetSel(selStart, selEnd);
		const int caret = (std::min)((std::max)(0, selEnd), text.GetLength());
		start = CVideoLibrary::ReverseFindListComma(text, caret) + 1;
		end = CVideoLibrary::FindListComma(text, caret);
		if (end < 0)
			end = text.GetLength();
	}
	CString token = text.Mid(start, end - start);
	token.Trim();
	return token;
}

BOOL CSuggestEdit::OnEnChangeReflect()
{
	if (!m_accepting)
		UpdatePopup(false);
	return FALSE;   // 부모 창도 EN_CHANGE 를 받도록
}

void CSuggestEdit::UpdatePopup(bool showAll)
{
	if (!m_provider || GetFocus() != this || (GetStyle() & ES_READONLY) || !IsWindowEnabled())
	{
		HidePopup();
		return;
	}

	int start = 0, end = 0;
	const CString token = CurrentToken(start, end);
	if (token.IsEmpty() && !showAll)
	{
		HidePopup();
		return;
	}

	// 같은 칸에 이미 입력된 다른 값은 후보에서 뺌
	std::vector<CString> existing;
	if (m_multi)
	{
		CString text;
		GetWindowText(text);
		const CString others = text.Left(start) + L"," + text.Mid(end);
		int pos = 0;
		for (;;)
		{
			const int p = CVideoLibrary::FindListComma(others, pos);
			CString t = (p < 0) ? others.Mid(pos) : others.Mid(pos, p - pos);
			t.Trim();
			if (!t.IsEmpty())
				existing.push_back(t);
			if (p < 0) break;
			pos = p + 1;
		}
	}

	std::vector<SuggestItem> all;
	m_provider(all);

	CString lower = token;
	lower.MakeLower();
	std::vector<SuggestItem> prefix, contains;
	std::set<CString> seen;
	for (const SuggestItem& it : all)
	{
		if (it.value.IsEmpty())
			continue;
		bool dup = false;
		for (const CString& e : existing)
			if (e.CompareNoCase(it.value) == 0) { dup = true; break; }
		if (dup)
			continue;
		CString key = it.text;
		key.MakeLower();
		if (!seen.insert(key).second)
			continue;
		CString val = it.value;
		val.MakeLower();
		if (lower.IsEmpty() || val.Find(lower) == 0)
			prefix.push_back(it);                 // 앞부분이 같은 것 먼저
		else if (key.Find(lower) >= 0)
			contains.push_back(it);
	}
	prefix.insert(prefix.end(), contains.begin(), contains.end());
	if (prefix.size() > kMaxItems)
		prefix.resize(kMaxItems);

	// 입력한 값과 정확히 같은 후보 하나뿐이면 목록을 띄우지 않음
	if (prefix.empty() || (prefix.size() == 1 && !showAll && prefix[0].value.CompareNoCase(token) == 0))
	{
		HidePopup();
		return;
	}

	if (!m_popup.GetSafeHwnd())
	{
		m_popup.m_edit = this;
		if (!m_popup.CreateEx(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"LISTBOX", nullptr,
			WS_POPUP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
			CRect(0, 0, 10, 10), GetParent(), 0))
			return;
		m_popup.SetFont(GetFont());
	}

	// 항목 높이 = 글자 높이 + 여백
	int itemH = 18;
	{
		CClientDC dc(this);
		CFont* old = dc.SelectObject(GetFont());
		TEXTMETRIC tm = {};
		dc.GetTextMetrics(&tm);
		dc.SelectObject(old);
		itemH = tm.tmHeight + 6;
	}

	m_popup.m_items = prefix;
	m_popup.SetRedraw(FALSE);
	m_popup.ResetContent();
	for (const SuggestItem& it : prefix)
		m_popup.AddString(it.text);
	m_popup.SetItemHeight(0, itemH);
	m_popup.SetCurSel(-1);
	m_popup.SetRedraw(TRUE);

	CRect rc;
	GetWindowRect(&rc);
	const int n = (std::min)(static_cast<int>(prefix.size()), kMaxVisible);
	const int h = n * itemH + 2;
	MONITORINFO mi = { sizeof(mi) };
	::GetMonitorInfoW(::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &mi);
	int y = rc.bottom;
	if (y + h > mi.rcWork.bottom)
		y = rc.top - h;   // 아래에 자리가 없으면 위로
	m_popup.SetWindowPos(&CWnd::wndTop, rc.left, y, rc.Width(), h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	m_popup.Invalidate();
}

void CSuggestEdit::MoveSelection(int delta)
{
	const int count = m_popup.GetCount();
	if (count <= 0)
		return;
	int cur = m_popup.GetCurSel();
	if (cur < 0)
		cur = (delta > 0) ? 0 : count - 1;
	else
		cur = (std::min)(count - 1, (std::max)(0, cur + delta));
	m_popup.SetCurSel(cur);
}

void CSuggestEdit::Accept(int index)
{
	if (index < 0 || index >= static_cast<int>(m_popup.m_items.size()))
		return;
	const SuggestItem item = m_popup.m_items[index];

	int start = 0, end = 0;
	CurrentToken(start, end);
	CString text;
	GetWindowText(text);

	CString newText;
	int caret = 0;
	if (m_multi)
	{
		CString before = text.Left(start);
		before.TrimRight();
		const CString prefix = before.IsEmpty() ? CString() : before + L" ";
		CString after = text.Mid(end);
		after.TrimLeft();
		if (after.IsEmpty())
		{
			newText = prefix + item.value + L", ";   // 이어서 다음 값을 입력할 수 있게
			caret = newText.GetLength();
		}
		else
		{
			newText = prefix + item.value + after;   // after 는 쉼표로 시작
			caret = prefix.GetLength() + item.value.GetLength();
		}
	}
	else
	{
		newText = item.value;
		caret = newText.GetLength();
	}

	HidePopup();
	m_accepting = true;
	SetWindowText(newText);   // EN_CHANGE → 부모의 변경 처리
	SetSel(caret, caret);
	m_accepting = false;

	if (m_onAccept)
		m_onAccept(item);
}

BOOL CSuggestEdit::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN && pMsg->hwnd == m_hWnd)
	{
		if (IsPopupVisible())
		{
			switch (pMsg->wParam)
			{
			case VK_DOWN:  MoveSelection(1);  return TRUE;
			case VK_UP:    MoveSelection(-1); return TRUE;
			case VK_NEXT:  MoveSelection(kMaxVisible);  return TRUE;
			case VK_PRIOR: MoveSelection(-kMaxVisible); return TRUE;
			case VK_ESCAPE:
				HidePopup();
				return TRUE;
			case VK_RETURN:
			case VK_TAB:
				if (m_popup.GetCurSel() >= 0)
				{
					Accept(m_popup.GetCurSel());
					return TRUE;
				}
				HidePopup();
				break;
			}
		}
		else if (pMsg->wParam == VK_DOWN)
		{
			UpdatePopup(true);   // ↓ : 지금 값으로 후보 목록 열기 (비어 있으면 전체)
			return TRUE;
		}
	}
	return CEdit::PreTranslateMessage(pMsg);
}

void CSuggestEdit::OnKillFocus(CWnd* pNewWnd)
{
	CEdit::OnKillFocus(pNewWnd);
	HidePopup();
}

BOOL CSuggestEdit::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	if (IsPopupVisible())
	{
		// 휠은 포커스가 있는 에디트로 오므로 목록을 대신 스크롤
		const int lines = (zDelta > 0) ? -3 : 3;
		m_popup.SetTopIndex((std::max)(0, m_popup.GetTopIndex() + lines));
		return TRUE;
	}
	return CEdit::OnMouseWheel(nFlags, zDelta, pt);
}

void CSuggestEdit::OnDestroy()
{
	if (m_popup.GetSafeHwnd())
		m_popup.DestroyWindow();
	CEdit::OnDestroy();
}
