#include "pch.h"
#include "AliasCombo.h"
#include "DarkTheme.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	const UINT kListId = 1;
	const UINT kSpinId = 2;
	const int  kMaxRows = 10;
}

// ---------------------------------------------------------------------------
// CAliasPopup

BEGIN_MESSAGE_MAP(CAliasPopup, CWnd)
	ON_WM_ACTIVATE()
	ON_WM_ACTIVATEAPP()
	ON_WM_SIZE()
	ON_WM_CONTEXTMENU()
	ON_WM_CTLCOLOR()
	ON_LBN_DBLCLK(kListId, &CAliasPopup::OnLbnDblclk)
	ON_NOTIFY(UDN_DELTAPOS, kSpinId, &CAliasPopup::OnDeltaPos)
END_MESSAGE_MAP()

bool CAliasPopup::CreatePopup(CWnd* owner)
{
	const CString cls = AfxRegisterWndClass(CS_DBLCLKS | CS_DROPSHADOW,
		::LoadCursor(nullptr, IDC_ARROW), nullptr);
	if (!CreateEx(WS_EX_TOOLWINDOW, cls, L"", WS_POPUP | WS_BORDER | WS_CLIPCHILDREN,
		CRect(0, 0, 100, 100), owner, 0))
		return false;
	m_list.Create(WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS,
		CRect(0, 0, 10, 10), this, kListId);
	m_list.SetFont(owner->GetFont());
	m_spin.Create(WS_CHILD | WS_VISIBLE, CRect(0, 0, 10, 10), this, kSpinId);
	m_spin.SetRange32(0, 1000);
	m_spin.SetPos32(500);
	m_listBrush.CreateSolidBrush(DarkColors::Edit);
	CDarkDialogTheme::ThemeChild(m_list.GetSafeHwnd());   // 어두운 스크롤바
	return true;
}

void CAliasPopup::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	if (!m_list.GetSafeHwnd())
		return;
	const int spinW = ::GetSystemMetrics(SM_CXVSCROLL) + 4;
	m_list.MoveWindow(0, 0, (std::max)(10, cx - spinW), cy);
	m_spin.MoveWindow(cx - spinW, 0, spinW, cy);   // 목록 오른쪽 전체 높이: 위쪽 ▲ / 아래쪽 ▼
}

void CAliasPopup::Open(const CRect& comboRect, int sel)
{
	m_list.ResetContent();
	for (int i = 0; i < m_combo->GetCount(); ++i)
	{
		CString s;
		m_combo->GetLBText(i, s);
		m_list.AddString(s);
	}
	m_list.SetCurSel(sel >= 0 ? sel : 0);

	const int itemH = (std::max)(12, m_list.GetItemHeight(0));
	const int rows = (std::min)((std::max)(m_list.GetCount(), 2), kMaxRows);
	const int spinW = ::GetSystemMetrics(SM_CXVSCROLL) + 4;
	CRect wr(0, 0, (std::max)(comboRect.Width(), 180) + spinW, rows * itemH);   // 목록 + 오른쪽 스핀
	::AdjustWindowRectEx(&wr, GetStyle(), FALSE, GetExStyle());
	const int w = wr.Width();
	const int h = wr.Height();

	MONITORINFO mi = { sizeof(mi) };
	::GetMonitorInfoW(::MonitorFromRect(&comboRect, MONITOR_DEFAULTTONEAREST), &mi);
	int x = comboRect.left;
	int y = comboRect.bottom;
	if (y + h > mi.rcWork.bottom)
		y = comboRect.top - h;            // 아래에 자리가 없으면 위로
	if (x + w > mi.rcWork.right)
		x = mi.rcWork.right - w;

	SetWindowPos(&CWnd::wndTop, x, y, w, h, SWP_SHOWWINDOW);
	m_list.SetFocus();
}

void CAliasPopup::Close(bool apply)
{
	if (m_closing || !IsOpen())
		return;
	m_closing = true;
	const int sel = m_list.GetCurSel();
	ShowWindow(SW_HIDE);
	if (m_combo)
	{
		m_combo->PopupClosed();
		if (apply && sel >= 0 && sel < m_combo->GetCount())
		{
			m_combo->SetCurSel(sel);       // 목록에서 고른 별칭을 콤보에 표시
			if (m_combo->m_onPicked)
				m_combo->m_onPicked();
		}
	}
	m_closing = false;
}

void CAliasPopup::Move(int delta)
{
	const int sel = m_list.GetCurSel();
	if (sel == LB_ERR)
		return;
	const int to = sel + delta;
	if (to < 0 || to >= m_list.GetCount())
		return;   // 맨 위 / 맨 아래
	CString text;
	m_list.GetText(sel, text);
	m_list.DeleteString(sel);
	m_list.InsertString(to, text);
	m_list.SetCurSel(to);
	if (m_combo)
	{
		m_combo->MoveItem(sel, to);        // 콤보 항목 순서도 같이
		if (m_combo->m_onReordered)
			m_combo->m_onReordered();
	}
}

void CAliasPopup::DeleteSelected()
{
	const int sel = m_list.GetCurSel();
	if (sel == LB_ERR)
		return;
	m_list.DeleteString(sel);
	if (m_combo)
		m_combo->DeleteItem(sel);
	if (m_list.GetCount() == 0)
	{
		Close(false);   // 별칭이 하나도 남지 않으면 닫음
		if (m_combo) m_combo->SetFocus();
		return;
	}
	m_list.SetCurSel((std::min)(sel, m_list.GetCount() - 1));
	m_list.SetFocus();
}

HBRUSH CAliasPopup::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (nCtlColor == CTLCOLOR_LISTBOX && m_listBrush.GetSafeHandle())
	{
		pDC->SetTextColor(DarkColors::Text);
		pDC->SetBkColor(DarkColors::Edit);
		return m_listBrush;
	}
	return CWnd::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CAliasPopup::OnContextMenu(CWnd*, CPoint point)
{
	// 우클릭한 항목을 고르고 메뉴 표시 (키보드 메뉴 키면 고른 항목 위치)
	if (point.x == -1 && point.y == -1)
	{
		CRect rc;
		const int sel = m_list.GetCurSel();
		if (sel == LB_ERR || m_list.GetItemRect(sel, &rc) == LB_ERR)
			return;
		point = CPoint(rc.left + 8, rc.bottom);
		m_list.ClientToScreen(&point);
	}
	else
	{
		CPoint pt = point;
		m_list.ScreenToClient(&pt);
		BOOL outside = TRUE;
		const UINT idx = m_list.ItemFromPoint(pt, outside);
		if (outside)
			return;
		m_list.SetCurSel(static_cast<int>(idx));
	}
	const int sel = m_list.GetCurSel();
	if (sel == LB_ERR)
		return;

	CString text;
	m_list.GetText(sel, text);
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, 1, L"'" + text + L"' 삭제\tDel");
	if (menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
		point.x, point.y, this) == 1)
		DeleteSelected();
}

void CAliasPopup::OnDeltaPos(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMUPDOWN* ud = reinterpret_cast<NMUPDOWN*>(pNMHDR);
	Move(ud->iDelta > 0 ? -1 : 1);     // ▲(값 증가) = 위로, ▼ = 아래로
	*pResult = 1;                        // 스핀 위치는 바꾸지 않음
	m_list.SetFocus();
}

void CAliasPopup::OnLbnDblclk()
{
	Close(true);
	if (m_combo)
		m_combo->SetFocus();
}

BOOL CAliasPopup::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN)
	{
		const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
		switch (pMsg->wParam)
		{
		case VK_RETURN:
			Close(true);
			if (m_combo) m_combo->SetFocus();
			return TRUE;
		case VK_ESCAPE:
		case VK_F4:
			Close(false);
			if (m_combo) m_combo->SetFocus();
			return TRUE;
		case VK_DELETE:
			DeleteSelected();                      // Del : 고른 별칭 삭제
			return TRUE;
		case VK_UP:
			if (ctrl) { Move(-1); return TRUE; }   // Ctrl+↑ : 위로
			break;
		case VK_DOWN:
			if (ctrl) { Move(1); return TRUE; }    // Ctrl+↓ : 아래로
			break;
		}
	}
	return CWnd::PreTranslateMessage(pMsg);
}

void CAliasPopup::OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized)
{
	CWnd::OnActivate(nState, pWndOther, bMinimized);
	if (nState == WA_INACTIVE)
		Close(true);   // 바깥을 클릭하면 닫힘 (그때 고른 별칭 반영)
}

void CAliasPopup::OnActivateApp(BOOL bActive, DWORD dwThreadID)
{
	CWnd::OnActivateApp(bActive, dwThreadID);
	if (!bActive)
		Close(true);
}

// ---------------------------------------------------------------------------
// CAliasCombo

BEGIN_MESSAGE_MAP(CAliasCombo, CComboBox)
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

int CAliasCombo::CurrentIndex() const
{
	int sel = GetCurSel();
	if (sel == CB_ERR)
	{
		CString typed;
		GetWindowText(typed);
		typed.Trim();
		if (!typed.IsEmpty())
			sel = FindStringExact(-1, typed);
	}
	return sel;
}

void CAliasCombo::ShowPopup()
{
	if (GetCount() <= 0 || !IsWindowEnabled())
	{
		::MessageBeep(MB_OK);
		return;
	}
	if (!m_popup.GetSafeHwnd())
	{
		m_popup.m_combo = this;
		if (!m_popup.CreatePopup(GetParent()))
			return;
	}
	CRect rc;
	GetWindowRect(&rc);
	m_popup.Open(rc, CurrentIndex());
}

void CAliasCombo::MoveItem(int from, int to)
{
	if (from < 0 || from >= GetCount() || to < 0 || to >= GetCount())
		return;
	CString text;
	GetLBText(from, text);
	DeleteString(from);
	InsertString(to, text);
	SetCurSel(to);
}

void CAliasCombo::DeleteItem(int index)
{
	if (index < 0 || index >= GetCount())
		return;
	CString text;
	GetLBText(index, text);
	DeleteString(index);
	if (GetCount() > 0)
		SetCurSel((std::min)(index, GetCount() - 1));
	else
		SetWindowText(L"");
	if (m_onDeleted)
		m_onDeleted(text);
}

void CAliasCombo::OnLButtonDown(UINT nFlags, CPoint point)
{
	COMBOBOXINFO info = { sizeof(info) };
	if (GetComboBoxInfo(&info) && CRect(info.rcButton).PtInRect(point))
	{
		// 방금 펼침 목록이 닫혔다면(▾ 를 다시 눌러 닫은 경우) 다시 열지 않음
		if (::GetTickCount64() - m_closedTick > 250)
			ShowPopup();
		return;   // 기본 펼침 목록은 쓰지 않음
	}
	CComboBox::OnLButtonDown(nFlags, point);
}

void CAliasCombo::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	COMBOBOXINFO info = { sizeof(info) };
	if (GetComboBoxInfo(&info) && CRect(info.rcButton).PtInRect(point))
		return;
	CComboBox::OnLButtonDblClk(nFlags, point);
}

BOOL CAliasCombo::PreTranslateMessage(MSG* pMsg)
{
	// 입력 칸 더블클릭 → 글자 전체 선택 (기본 동작은 단어 하나만 선택)
	if (pMsg->message == WM_LBUTTONDBLCLK && pMsg->hwnd != m_hWnd && ::GetParent(pMsg->hwnd) == m_hWnd)
	{
		SetFocus();
		SetEditSel(0, -1);
		return TRUE;
	}

	// F4 / Alt+↓ / Alt+↑ : 펼침 목록 (입력 칸에 포커스가 있을 때도)
	if ((pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_F4) ||
		(pMsg->message == WM_SYSKEYDOWN && (pMsg->wParam == VK_DOWN || pMsg->wParam == VK_UP)))
	{
		ShowPopup();
		return TRUE;
	}
	return CComboBox::PreTranslateMessage(pMsg);
}

void CAliasCombo::OnDestroy()
{
	if (m_popup.GetSafeHwnd())
		m_popup.DestroyWindow();
	CComboBox::OnDestroy();
}
