#include "pch.h"
#include "DarkTheme.h"

#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// ---------------------------------------------------------------------------
// CDarkHeader

BEGIN_MESSAGE_MAP(CDarkHeader, CHeaderCtrl)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

void CDarkHeader::OnPaint()
{
	CPaintDC paint(this);
	CRect client;
	GetClientRect(&client);
	CDC mem;
	mem.CreateCompatibleDC(&paint);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&paint, client.Width(), client.Height());
	CBitmap* oldBmp = mem.SelectObject(&bmp);
	CFont* font = GetFont();
	CFont* oldFont = font ? mem.SelectObject(font) : nullptr;

	const COLORREF back = RGB(0x2A, 0x35, 0x3D);
	const COLORREF line = RGB(0x39, 0x4B, 0x59);
	mem.FillSolidRect(client, back);
	mem.SetBkMode(TRANSPARENT);
	mem.SetTextColor(RGB(255, 255, 255));   // 머리글 글자: 흰색

	const int count = GetItemCount();
	for (int i = 0; i < count; ++i)
	{
		CRect rc;
		if (!GetItemRect(i, &rc))
			continue;
		wchar_t text[128] = {};
		HDITEMW item = {};
		item.mask = HDI_TEXT | HDI_FORMAT;
		item.pszText = text;
		item.cchTextMax = _countof(text);
		GetItem(i, &item);

		UINT align = DT_LEFT;
		if ((item.fmt & HDF_JUSTIFYMASK) == HDF_RIGHT)  align = DT_RIGHT;
		if ((item.fmt & HDF_JUSTIFYMASK) == HDF_CENTER) align = DT_CENTER;
		CRect tr = rc;
		tr.DeflateRect(6, 0);
		mem.DrawText(text, tr, align | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
		mem.FillSolidRect(rc.right - 1, rc.top + 3, 1, rc.Height() - 6, line);   // 열 구분선
	}
	mem.FillSolidRect(client.left, client.bottom - 1, client.Width(), 1, line);    // 아래 경계선

	paint.BitBlt(0, 0, client.Width(), client.Height(), &mem, 0, 0, SRCCOPY);
	if (oldFont)
		mem.SelectObject(oldFont);
	mem.SelectObject(oldBmp);
}

// ---------------------------------------------------------------------------

void CDarkDialogTheme::ThemeChild(HWND hwnd)
{
	wchar_t cls[32] = {};
	::GetClassNameW(hwnd, cls, 32);
	const LONG style = ::GetWindowLongW(hwnd, GWL_STYLE);

	if (_wcsicmp(cls, WC_LISTVIEWW) == 0)
	{
		ListView_SetBkColor(hwnd, DarkColors::Back);
		ListView_SetTextBkColor(hwnd, DarkColors::Back);
		ListView_SetTextColor(hwnd, DarkColors::Text);
		::SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);           // 어두운 스크롤바·선택 표시
		if (HWND header = ListView_GetHeader(hwnd))
			::SetWindowTheme(header, L"DarkMode_ItemsView", nullptr);    // 어두운 열 머리글
	}
	else if (_wcsicmp(cls, L"Edit") == 0)
	{
		if (style & ES_MULTILINE)
			::SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);       // 메모 칸 스크롤바
	}
	else if (_wcsicmp(cls, L"ListBox") == 0)
	{
		::SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
	}
	else if (_wcsicmp(cls, L"ComboBox") == 0)
	{
		::SetWindowTheme(hwnd, L"DarkMode_CFD", nullptr);                // 어두운 콤보 테두리/버튼
	}
}

void CDarkDialogTheme::Apply(CWnd* dlg, std::initializer_list<UINT> dangerIds)
{
	if (!m_back.GetSafeHandle())
		m_back.CreateSolidBrush(DarkColors::Back);
	if (!m_edit.GetSafeHandle())
		m_edit.CreateSolidBrush(DarkColors::Edit);

	for (CWnd* w = dlg->GetWindow(GW_CHILD); w; w = w->GetWindow(GW_HWNDNEXT))
	{
		const HWND h = w->GetSafeHwnd();
		wchar_t cls[32] = {};
		::GetClassNameW(h, cls, 32);
		if (_wcsicmp(cls, L"Button") == 0)
		{
			const LONG type = ::GetWindowLongW(h, GWL_STYLE) & BS_TYPEMASK;
			if (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON)
			{
				if (CWnd::FromHandlePermanent(h))
					continue;   // 이미 다른 클래스로 연결된 버튼은 건드리지 않음
				const UINT id = static_cast<UINT>(::GetDlgCtrlID(h));
				bool danger = false;
				for (UINT d : dangerIds)
					if (d == id) { danger = true; break; }
				// "저장" / "등록" 버튼은 초록색
				wchar_t caption[64] = {};
				::GetWindowTextW(h, caption, 64);
				const bool isSave = (wcscmp(caption, L"저장") == 0 || wcscmp(caption, L"등록") == 0);
				auto btn = std::make_unique<CDarkButton>();
				if (btn->Attach(id, dlg))
				{
					btn->SetColors(danger ? DarkColors::Danger : (isSave ? DarkColors::Save : DarkColors::Button), RGB(255, 255, 255), DarkColors::Back);
					m_buttons.push_back(std::move(btn));
				}
			}
			else
			{
				::SetWindowTheme(h, L"", L"");   // 체크박스/라디오: 테마를 끄면 글자색 지정 가능
			}
		}
		else
		{
			ThemeChild(h);
			// 목록의 열 머리글(첫 줄)은 직접 그려서 흰 글자로
			if (_wcsicmp(cls, WC_LISTVIEWW) == 0)
			{
				HWND header = ListView_GetHeader(h);
				if (header && !(::GetWindowLongW(h, GWL_STYLE) & LVS_NOCOLUMNHEADER) && !CWnd::FromHandlePermanent(header))
				{
					auto hdr = std::make_unique<CDarkHeader>();
					if (hdr->SubclassWindow(header))
					{
						hdr->Invalidate();
						m_headers.push_back(std::move(hdr));
					}
				}
			}
		}
	}
	dlg->Invalidate();
}

HBRUSH CDarkDialogTheme::CtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (!m_back.GetSafeHandle())
		return nullptr;
	switch (nCtlColor)
	{
	case CTLCOLOR_DLG:
		return m_back;

	case CTLCOLOR_EDIT:
	case CTLCOLOR_LISTBOX:
		pDC->SetTextColor(DarkColors::Text);
		pDC->SetBkColor(DarkColors::Edit);
		return m_edit;

	case CTLCOLOR_STATIC:
	{
		// 읽기 전용/비활성 에디트도 CTLCOLOR_STATIC 을 보냄 → 에디트 배경색
		wchar_t cls[16] = {};
		::GetClassNameW(pWnd->GetSafeHwnd(), cls, 16);
		if (_wcsicmp(cls, L"Edit") == 0)
		{
			pDC->SetTextColor(pWnd->IsWindowEnabled() ? DarkColors::Text : DarkColors::DisabledText);
			pDC->SetBkColor(DarkColors::Edit);
			return m_edit;
		}
		pDC->SetTextColor(pWnd->IsWindowEnabled() ? DarkColors::Text : DarkColors::DisabledText);
		pDC->SetBkColor(DarkColors::Back);
		pDC->SetBkMode(TRANSPARENT);
		return m_back;
	}

	case CTLCOLOR_BTN:
		pDC->SetTextColor(DarkColors::Text);
		pDC->SetBkColor(DarkColors::Back);
		return m_back;
	}
	return nullptr;
}
