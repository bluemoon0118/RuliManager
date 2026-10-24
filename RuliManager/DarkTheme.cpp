#include "pch.h"
#include "DarkTheme.h"

#include <uxtheme.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")   // SetWindowSubclass (에디트 어두운 테두리)
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
	if (m_bold && font)
	{
		// 굵은 글꼴: 머리글 글꼴과 같은 크기 · 모양으로 한 번 만들어 둠
		if (!m_boldFont.GetSafeHandle())
		{
			LOGFONT lf = {};
			font->GetLogFont(&lf);
			lf.lfWeight = FW_BOLD;
			m_boldFont.CreateFontIndirect(&lf);
		}
		if (m_boldFont.GetSafeHandle())
			font = &m_boldFont;
	}
	CFont* oldFont = font ? mem.SelectObject(font) : nullptr;

	const COLORREF back = TCOL(TC_THUMB);
	const COLORREF line = TCOL(TC_COMBO);
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
		if (item.fmt & (HDF_SORTUP | HDF_SORTDOWN))
		{
			// 정렬 표시: 오른쪽 끝 작은 삼각형 (▲ 오름차순 / ▼ 내림차순)
			const int s = (std::max)(3, rc.Height() / 6);
			const int cx = rc.right - 6 - s, cy = (rc.top + rc.bottom) / 2;
			const bool up = (item.fmt & HDF_SORTUP) != 0;
			POINT pts[3] = { { cx - s, up ? cy + s / 2 : cy - s / 2 }, { cx + s, up ? cy + s / 2 : cy - s / 2 }, { cx, up ? cy - s / 2 - 1 : cy + s / 2 + 1 } };
			CBrush ab(RGB(220, 226, 232));
			CPen ap(PS_SOLID, 1, RGB(220, 226, 232));
			CBrush* ob = mem.SelectObject(&ab);
			CPen* op = mem.SelectObject(&ap);
			mem.Polygon(pts, 3);
			mem.SelectObject(ob);
			mem.SelectObject(op);
			tr.right -= s * 2 + 6;
		}
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

namespace
{
	bool IsComboClass(HWND h)
	{
		wchar_t cls[16] = {};
		::GetClassNameW(h, cls, 16);
		return _wcsicmp(cls, L"ComboBox") == 0;
	}

	// 콤보박스: 테두리를 컨트롤이 직접(WM_PAINT, 클라이언트 영역 가장자리) 그리므로 그 바깥 1px 을 덧칠
	void PaintComboEdge(HWND h)
	{
		RECT rc = {};
		::GetClientRect(h, &rc);
		if (rc.right <= 0 || rc.bottom <= 0)
			return;
		// 선택 칸 높이만 (콤보를 옮길 때 잠깐 펼친 목록 높이만큼 창이 커지는 순간에 그리면 그 길이로 테두리 흔적이 남음)
		COMBOBOXINFO cbi = { sizeof(cbi) };
		if (::GetComboBoxInfo(h, &cbi) && cbi.rcButton.bottom > 0)
		{
			const LONG fieldH = cbi.rcButton.bottom + cbi.rcButton.top;   // 버튼은 위아래 같은 여백
			if (rc.bottom > fieldH + 2)
				return;   // 지금은 크기가 바뀌는 중 - 제 크기가 되면 다시 그려짐
		}
		HDC dc = ::GetDC(h);
		if (!dc)
			return;
		HBRUSH edge = ::CreateSolidBrush(TCOL(TC_EDGE));
		::FrameRect(dc, &rc, edge);
		::DeleteObject(edge);
		::ReleaseDC(h, dc);
	}

	// focus: 1 = 포커스 있음, 0 = 없음, -1 = 지금 포커스로 판단 (WM_KILLFOCUS 처리 중에는 GetFocus 가 아직 바뀌지 않았을 수 있어 직접 넘김)
	void PaintEditEdge(HWND h, int focus = -1)
	{
		if (IsComboClass(h))
		{
			PaintComboEdge(h);
			return;
		}
		const bool focused = (focus < 0) ? (::GetFocus() == h) : (focus != 0);
		const LONG style = ::GetWindowLongW(h, GWL_STYLE);
		const LONG ex = ::GetWindowLongW(h, GWL_EXSTYLE);
		const bool client = (ex & WS_EX_CLIENTEDGE) != 0;
		if (!client && !(style & WS_BORDER))
			return;
		RECT wr = {};
		::GetWindowRect(h, &wr);
		RECT rc = { 0, 0, wr.right - wr.left, wr.bottom - wr.top };
		HDC dc = ::GetWindowDC(h);
		if (!dc)
			return;
		// 포커스가 있으면 파란 강조 테두리 (버튼과 같은 강조색), 없으면 어두운 테두리
		const bool hot = focused && ::IsWindowEnabled(h) && !(style & ES_READONLY);
		HBRUSH edge = ::CreateSolidBrush(hot ? TCOL(TC_ACCENT) : TCOL(TC_EDGE));
		::FrameRect(dc, &rc, edge);
		::DeleteObject(edge);
		if (client)
		{
			// 2px 테두리의 안쪽 줄은 에디트 바탕색 (어두운 1px 테두리처럼 보이게)
			::InflateRect(&rc, -1, -1);
			HBRUSH inner = ::CreateSolidBrush(::IsWindowEnabled(h) ? TCOL(TC_EDIT) : TCOL(TC_BACK));
			::FrameRect(dc, &rc, inner);
			::DeleteObject(inner);
		}
		::ReleaseDC(h, dc);
	}

	LRESULT CALLBACK EditEdgeProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR)
	{
		switch (msg)
		{
		case WM_NCPAINT:
		{
			const LRESULT r = ::DefSubclassProc(h, msg, wp, lp);   // 기본 테두리 · 스크롤바 먼저
			PaintEditEdge(h);
			return r;
		}
		case WM_SETFOCUS:
		case WM_KILLFOCUS:
		case WM_ENABLE:
		case WM_MOUSELEAVE:
		{
			// 테마 에디트는 포커스 · 마우스 오버 때 테두리를 다시 그리므로 그 뒤에 덧그림 (포커스 = 파란 테두리)
			const LRESULT r = ::DefSubclassProc(h, msg, wp, lp);
			PaintEditEdge(h, msg == WM_SETFOCUS ? 1 : (msg == WM_KILLFOCUS ? 0 : -1));
			return r;
		}
		case WM_NCDESTROY:
			::RemoveWindowSubclass(h, EditEdgeProc, id);
			break;
		}
		const LRESULT r = ::DefSubclassProc(h, msg, wp, lp);
		if (msg == WM_MOUSEMOVE || msg == WM_PAINT || msg == WM_LBUTTONUP || msg == WM_TIMER ||
			msg == CB_SHOWDROPDOWN || msg == WM_CAPTURECHANGED)
			PaintEditEdge(h);   // 마우스 오버 · 눌림 · 펼침 · 테마 애니메이션 뒤에도 (콤보)
		return r;
	}

	BOOL CALLBACK AttachEdgeEnum(HWND h, LPARAM)
	{
		DarkEdge::Attach(h);
		return TRUE;
	}
}

namespace DarkEdge
{
	void Attach(HWND edit)
	{
		if (!edit || !::IsWindow(edit))
			return;
		wchar_t cls[16] = {};
		::GetClassNameW(edit, cls, 16);
		if (_wcsicmp(cls, L"Edit") != 0 && _wcsicmp(cls, L"ComboBox") != 0)
			return;   // 에디트 · 콤보박스만
		::SetWindowSubclass(edit, EditEdgeProc, 0x45444745 /* 'EDGE' */, 0);   // 이미 있으면 그대로
		::SetWindowPos(edit, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);   // 테두리 다시 그리기
		::InvalidateRect(edit, nullptr, FALSE);   // 콤보는 클라이언트 영역에 테두리
	}

	void AttachAll(HWND root)
	{
		if (root && ::IsWindow(root))
			::EnumChildWindows(root, AttachEdgeEnum, 0);
	}
}

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
		DarkEdge::Attach(hwnd);                                         // 어두운 테두리
	}
	else if (_wcsicmp(cls, L"ListBox") == 0)
	{
		::SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
	}
	else if (_wcsicmp(cls, L"ComboBox") == 0)
	{
		::SetWindowTheme(hwnd, L"DarkMode_CFD", nullptr);                // 어두운 콤보 테두리/버튼
		DarkEdge::Attach(hwnd);                                         // 테두리를 에디트와 같은 어두운 색으로
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
				// 체크박스 · 라디오는 Windows 11 모양으로 직접 그림 (그룹 상자 · 누름 모양 체크는 제외)
				const bool checkLike = (type == BS_CHECKBOX || type == BS_AUTOCHECKBOX || type == BS_3STATE || type == BS_AUTO3STATE ||
				                        type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON);
				if (checkLike && !(::GetWindowLongW(h, GWL_STYLE) & BS_PUSHLIKE) && !CWnd::FromHandlePermanent(h))
				{
					auto chk = std::make_unique<CDarkCheck>();
					if (chk->Subclass(h))
						m_checks.push_back(std::move(chk));
				}
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
