#include "pch.h"
#include "DarkControls.h"
#include "VectorIcons.h"
#include <memory>

namespace
{
	COLORREF Dim(COLORREF c)
	{
		return RGB(GetRValue(c) * 55 / 100, GetGValue(c) * 55 / 100, GetBValue(c) * 55 / 100);
	}
}

// ===========================================================================
// CDarkCombo

BEGIN_MESSAGE_MAP(CDarkCombo, CComboBox)
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

void CDarkCombo::SetColors(COLORREF bg, COLORREF text, COLORREF selBg)
{
	m_bg = bg;
	m_text = text;
	m_selBg = selBg;
	if (m_brush.GetSafeHandle())
		m_brush.DeleteObject();
	m_brush.CreateSolidBrush(m_bg);
	if (GetSafeHwnd())
		Invalidate();
}

void CDarkCombo::SetHeights(int fieldHeight, int itemHeight)
{
	if (fieldHeight > 0) SetItemHeight(-1, fieldHeight);   // 선택된 값이 보이는 칸
	if (itemHeight > 0)  SetItemHeight(0, itemHeight);     // 펼친 목록의 항목
}

void CDarkCombo::MeasureItem(LPMEASUREITEMSTRUCT /*lpMIS*/)
{
	// 높이는 SetHeights 로 지정
}

void CDarkCombo::DrawItem(LPDRAWITEMSTRUCT lpDIS)
{
	CDC dc;
	dc.Attach(lpDIS->hDC);

	CRect rc = lpDIS->rcItem;
	const bool isField = (lpDIS->itemState & ODS_COMBOBOXEDIT) != 0;
	const bool selected = (lpDIS->itemState & ODS_SELECTED) != 0;

	// 펼친 목록에서 선택된 항목만 강조, 위쪽 값 칸은 항상 배경색
	dc.FillSolidRect(&rc, (selected && !isField) ? m_selBg : m_bg);

	if (lpDIS->itemID != static_cast<UINT>(-1))
	{
		CString text;
		GetLBText(static_cast<int>(lpDIS->itemID), text);
		dc.SetBkMode(TRANSPARENT);
		COLORREF textColor = m_text;
		if (lpDIS->itemID < m_itemColors.size() && m_itemColors[lpDIS->itemID] != CLR_DEFAULT)
			textColor = m_itemColors[lpDIS->itemID];   // 항목별 글자색
		dc.SetTextColor(IsWindowEnabled() ? textColor : Dim(textColor));
		CFont* old = dc.SelectObject(GetFont());
		rc.DeflateRect(4, 0);
		dc.DrawText(text, &rc, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
		dc.SelectObject(old);
	}

	dc.Detach();
}

HBRUSH CDarkCombo::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	// 펼친 목록(리스트박스)의 빈 영역 배경
	if (m_brush.GetSafeHandle() && (nCtlColor == CTLCOLOR_LISTBOX || nCtlColor == CTLCOLOR_EDIT))
	{
		pDC->SetTextColor(m_text);
		pDC->SetBkColor(m_bg);
		return m_brush;
	}
	return CComboBox::OnCtlColor(pDC, pWnd, nCtlColor);
}

// ===========================================================================
// CDarkButton

namespace
{
	COLORREF Shade(COLORREF c, int percent)   // percent > 100: 밝게, < 100: 어둡게
	{
		auto ch = [&](int v)
		{
			int r = v * percent / 100;
			if (percent > 100) r = v + (255 - v) * (percent - 100) / 100;
			return (std::max)(0, (std::min)(255, r));
		};
		return RGB(ch(GetRValue(c)), ch(GetGValue(c)), ch(GetBValue(c)));
	}
}

BEGIN_MESSAGE_MAP(CDarkButton, CButton)
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_ENABLE()
END_MESSAGE_MAP()

BOOL CDarkButton::Attach(UINT id, CWnd* parent)
{
	if (!SubclassDlgItem(id, parent))
		return FALSE;
	ModifyStyle(BS_TYPEMASK, BS_OWNERDRAW);
	Invalidate();
	return TRUE;
}

void CDarkButton::SetColors(COLORREF bg, COLORREF text, COLORREF parentBg)
{
	m_bg = bg;
	m_text = text;
	m_parentBg = parentBg;
	if (GetSafeHwnd())
		Invalidate();
}

void CDarkButton::SetToggle(COLORREF offBg, COLORREF offText)
{
	m_toggle = true;
	m_offBg = offBg;
	m_offText = offText;
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

void CDarkButton::SetChecked(bool checked)
{
	if (m_checked == checked)
		return;
	m_checked = checked;
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

void CDarkButton::SetTooltip(const CString& text)
{
	if (!GetSafeHwnd())
		return;
	if (!m_tip.GetSafeHwnd())
	{
		if (!m_tip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX))
			return;
		m_tip.AddTool(this, text);
		m_tip.Activate(TRUE);
	}
	else
		m_tip.UpdateTipText(text, this);
}

BOOL CDarkButton::PreTranslateMessage(MSG* pMsg)
{
	if (m_tip.GetSafeHwnd())
		m_tip.RelayEvent(pMsg);
	return CButton::PreTranslateMessage(pMsg);
}

void CDarkButton::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_hover)
	{
		m_hover = true;
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
		::TrackMouseEvent(&tme);
		Invalidate(FALSE);
		if (m_onHover)
			m_onHover(true);
	}
	CButton::OnMouseMove(nFlags, point);
}

void CDarkButton::OnMouseLeave()
{
	m_hover = false;
	Invalidate(FALSE);
	if (m_onHover)
		m_onHover(false);
	CButton::OnMouseLeave();
}

void CDarkButton::OnEnable(BOOL bEnable)
{
	CButton::OnEnable(bEnable);
	Invalidate(FALSE);
}

void CDarkButton::DrawItem(LPDRAWITEMSTRUCT lpDIS)
{
	// 메모리 DC 에 그린 뒤 한 번에 옮김 (톱니바퀴 회전처럼 자주 다시 그려도 깜빡이지 않게)
	CDC screen;
	screen.Attach(lpDIS->hDC);
	CRect rc = lpDIS->rcItem;
	CDC dc;
	dc.CreateCompatibleDC(&screen);
	CBitmap memBmp;
	memBmp.CreateCompatibleBitmap(&screen, (std::max)(1, static_cast<int>(rc.right)), (std::max)(1, static_cast<int>(rc.bottom)));   // 0,0 부터 (좌표 그대로)
	CBitmap* oldMemBmp = dc.SelectObject(&memBmp);

	const bool disabled = (lpDIS->itemState & ODS_DISABLED) != 0;
	const bool pressed  = (lpDIS->itemState & ODS_SELECTED) != 0;
	const bool focused  = (lpDIS->itemState & ODS_FOCUS) != 0;

	// 토글 버튼이 꺼져 있으면 꺼짐 색
	const COLORREF base = (m_toggle && !m_checked) ? m_offBg : m_bg;
	const COLORREF textColor = (m_toggle && !m_checked) ? m_offText : m_text;

	COLORREF fill = base;
	if (disabled)      fill = RGB((GetRValue(base) + GetRValue(m_parentBg) * 2) / 3,
	                              (GetGValue(base) + GetGValue(m_parentBg) * 2) / 3,
	                              (GetBValue(base) + GetBValue(m_parentBg) * 2) / 3);   // 배경 쪽으로 흐리게
	else if (pressed)  fill = Shade(base, 75);    // 눌림: 어둡게
	else if (m_hover)  fill = Shade(base, 115);   // 마우스 올림: 밝게

	// 모서리는 부모 배경색, 버튼은 둥근 사각형
	dc.FillSolidRect(&rc, m_parentBg);
	CBrush brush(fill);
	CPen pen(PS_SOLID, 1, focused && !disabled ? Shade(base, 160) : fill);
	CBrush* oldBrush = dc.SelectObject(&brush);
	CPen* oldPen = dc.SelectObject(&pen);
	if (m_segment == SEG_NONE)
		dc.RoundRect(&rc, CPoint(6, 6));
	else
	{
		// 세그먼트: 바깥쪽 모서리만 둥글게 (안쪽은 옆 칸과 붙도록 각지게), 묶음 테두리 + 칸 사이 구분선
		const COLORREF edge = Shade(m_toggle ? m_offBg : base, 150);
		CPen edgePen(PS_SOLID, 1, edge);
		dc.SelectObject(&edgePen);
		CRect shape = rc;
		if (m_segment == SEG_LEFT)        shape.right += 8;    // 오른쪽 둥근 모서리는 밖으로 (잘림)
		else if (m_segment == SEG_RIGHT)  shape.left -= 8;
		else                              shape.InflateRect(8, 0);
		CRgn clip;
		clip.CreateRectRgnIndirect(&rc);
		dc.SelectClipRgn(&clip);
		dc.RoundRect(&shape, CPoint(6, 6));
		if (m_segment != SEG_LEFT)
		{
			// 왼쪽 칸과의 구분선 (왼쪽 테두리)
			dc.MoveTo(rc.left, rc.top);
			dc.LineTo(rc.left, rc.bottom);
		}
		dc.SelectClipRgn(nullptr);
		dc.SelectObject(&pen);
	}
	dc.SelectObject(oldBrush);
	dc.SelectObject(oldPen);

	CString text;
	GetWindowText(text);
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(disabled ? Dim(textColor) : textColor);
	CFont* oldFont = dc.SelectObject(GetFont());
	CRect tr = rc;
	if (pressed) tr.OffsetRect(1, 1);

	if (m_icon == ICON_GEAR)
	{
		// 설정 버튼: 글자 없이 가운데 톱니바퀴 (벡터)
		VectorIcon::Gear(&dc, tr, disabled ? Dim(textColor) : textColor, 0.72, m_iconAngle);
	}
	else if (m_icon == ICON_VIEW_GRID || m_icon == ICON_VIEW_LIST || m_icon == ICON_VIEW_PACKAGE || m_icon == ICON_VIEW_YEAR || m_icon == ICON_VIEW_WALL || m_icon == ICON_VIEW_TREEMAP || m_icon == ICON_VIEW_LABEL)
	{
		// 보기 방식 버튼: 글자 없이 가운데 아이콘 (벡터)
		const COLORREF ic = disabled ? Dim(textColor) : textColor;
		if (m_icon == ICON_VIEW_GRID)      VectorIcon::ViewGrid(&dc, tr, ic, 0.66);
		else if (m_icon == ICON_VIEW_LIST) VectorIcon::ViewList(&dc, tr, ic, 0.66);
		else if (m_icon == ICON_VIEW_YEAR) VectorIcon::ViewYear(&dc, tr, ic, 0.66);
		else if (m_icon == ICON_VIEW_WALL) VectorIcon::ViewWall(&dc, tr, ic, 0.66);
		else if (m_icon == ICON_VIEW_TREEMAP) VectorIcon::ViewTreemap(&dc, tr, ic, 0.66);
		else if (m_icon == ICON_VIEW_LABEL) VectorIcon::ViewLabel(&dc, tr, ic, 0.66);
		else                               VectorIcon::ViewPackage(&dc, tr, ic, 0.66);
	}
	else if (m_icon == ICON_GENRE_OFF || m_icon == ICON_GENRE_ON)
	{
		// 장르 칩 [장르숨김|전체] 버튼: 글자 없이 가운데 빗금 꼬리표 / 꼬리표 (이름은 툴팁)
		const COLORREF ic = disabled ? Dim(textColor) : textColor;
		if (m_icon == ICON_GENRE_OFF) VectorIcon::GenreTagOff(&dc, tr, ic, 0.66);
		else                          VectorIcon::GenreTag(&dc, tr, ic, 0.66);
	}
	else if (m_icon == ICON_SORT_ASC || m_icon == ICON_SORT_DESC)
	{
		// 정렬 방향 버튼: 글자 없이 가운데 화살표 + 막대 (이름은 툴팁)
		const COLORREF ic = disabled ? Dim(textColor) : textColor;
		if (m_icon == ICON_SORT_ASC) VectorIcon::SortAsc(&dc, tr, ic, 0.70);
		else                         VectorIcon::SortDesc(&dc, tr, ic, 0.70);
	}
	else if (m_icon == ICON_SYMBOL_ALL || m_icon == ICON_SYMBOL_F || m_icon == ICON_SYMBOL_M)
	{
		// 성별 거르기 버튼: 글자 없이 가운데 성별 기호 (고른 상태도 분홍 / 파랑 그대로 - 고른 칸은 바탕색으로 구분)
		const COLORREF kF = RGB(0xF5, 0x49, 0x8B), kM = RGB(0x48, 0xAF, 0xF0);
		const COLORREF f = disabled ? Dim(textColor) : kF;
		const COLORREF m = disabled ? Dim(textColor) : kM;
		if (m_icon == ICON_SYMBOL_ALL)    VectorIcon::GenderPair(&dc, tr, f, m, 0.72);
		else if (m_icon == ICON_SYMBOL_F) VectorIcon::GenderFemale(&dc, tr, f, 0.66);
		else                              VectorIcon::GenderMale(&dc, tr, m, 0.66);
	}
	else if (m_icon == ICON_COPY)
	{
		// 복사 버튼: 글자 없이 가운데 겹친 두 장 (벡터)
		VectorIcon::Copy(&dc, tr, disabled ? Dim(textColor) : textColor, 0.62);
	}
	else if (m_icon != ICON_NONE)
	{
		// [아이콘] 글자 : 아이콘+글자를 가운데 정렬
		COLORREF iconColor = disabled ? Dim(textColor) : textColor;
		if (!disabled && !(m_toggle && m_checked))
		{
			// 성별 기호: 꺼진(고르지 않은) 버튼에서는 배우 카드의 성별 기호 색 (켜지면 버튼 글자색)
			if (m_icon == ICON_FEMALE) iconColor = RGB(0xF5, 0x49, 0x8B);
			else if (m_icon == ICON_MALE) iconColor = RGB(0x48, 0xAF, 0xF0);
		}
		// 아이콘 크기는 버튼 높이가 아니라 글자 높이에 맞춰 네 버튼 모두 같게
		TEXTMETRIC tm = {};
		dc.GetTextMetrics(&tm);
		const int d = (std::max)(8, (std::min)(tr.Height() - 4, static_cast<int>(tm.tmAscent) * 95 / 100));   // 아이콘 높이
		const int iconW = ((m_icon == ICON_CAMERA) ? d * 125 / 100 : d) * 120 / 100;   // 카메라만 가로로 넓게 (24x24 설계에서 세로가 낮은 모양), 아이콘 20% 크게
		const int gap = (std::max)(4, d * 45 / 100);
		const int textW = dc.GetTextExtent(text).cx;
		const int total = iconW + gap + textW;
		const int left = tr.left + (tr.Width() - total) / 2;
		const int top = tr.top + (tr.Height() - d) / 2;

		// 각 탭 카드의 기본 이미지와 같은 벡터 아이콘 (구멍은 뚫려 있어 버튼 색이 비침)
		const int cy = top + d / 2;
		const CRect sq(left, cy - iconW / 2, left + iconW, cy - iconW / 2 + iconW);   // 아이콘 칸 (정사각형, 세로 가운데)
		if (m_icon == ICON_PLAY)
			VectorIcon::Movie(&dc, sq, iconColor, 1.0);    // 영상: 원 + 재생 (영상 기본 이미지)
		else if (m_icon == ICON_PERSON)
			VectorIcon::User(&dc, sq, iconColor, 1.0);     // 배우: 사람 (성별 미지정 배우 기본 이미지)
		else if (m_icon == ICON_CAMERA)
			VectorIcon::Camera(&dc, sq, iconColor, 1.0);   // 제작사: 비디오 카메라 (제작사 기본 이미지)
		else if (m_icon == ICON_TAG)
			VectorIcon::Tag(&dc, sq, iconColor, 1.0);      // 태그: 꼬리표 (태그 기본 이미지)
		else if (m_icon == ICON_COLLECTION)
			VectorIcon::Collection(&dc, sq, iconColor, 1.0);   // 컬렉션: 겹친 화면 + 재생
		else if (m_icon == ICON_GENDER_ALL)
			VectorIcon::GenderBoth(&dc, sq, iconColor, 0.95);
		else if (m_icon == ICON_FEMALE)
			VectorIcon::GenderFemale(&dc, sq, iconColor, 0.95);
		else if (m_icon == ICON_MALE)
			VectorIcon::GenderMale(&dc, sq, iconColor, 0.95);

		CRect textRc(left + iconW + gap, tr.top, tr.right, tr.bottom);
		dc.DrawText(text, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	}
	else
	{
		dc.DrawText(text, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	}
	dc.SelectObject(oldFont);

	screen.BitBlt(rc.left, rc.top, rc.Width(), rc.Height(), &dc, rc.left, rc.top, SRCCOPY);
	dc.SelectObject(oldMemBmp);
	screen.Detach();
}

// ===========================================================================
// CDarkDateTime

BEGIN_MESSAGE_MAP(CDarkDateTime, CDateTimeCtrl)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_ENABLE()
	ON_WM_SETFOCUS()
	ON_WM_KILLFOCUS()
	ON_WM_CHAR()
	ON_WM_KEYDOWN()
	ON_WM_GETDLGCODE()
END_MESSAGE_MAP()

bool CDarkDateTime::ParseTyped(const CString& text, SYSTEMTIME& st)
{
	// 숫자 묶음으로 나눔: "1998-2-17" → 1998 / 2 / 17, "19980217" → 1998 / 02 / 17
	std::vector<CString> nums;
	CString cur;
	for (int i = 0; i < text.GetLength(); ++i)
	{
		const wchar_t c = text[i];
		if (c >= L'0' && c <= L'9')
			cur += c;
		else if (!cur.IsEmpty())
		{
			nums.push_back(cur);
			cur.Empty();
		}
	}
	if (!cur.IsEmpty())
		nums.push_back(cur);

	int y = 0, m = 0, d = 0;
	if (nums.size() == 1 && nums[0].GetLength() == 8)
	{
		y = _wtoi(nums[0].Left(4));
		m = _wtoi(nums[0].Mid(4, 2));
		d = _wtoi(nums[0].Mid(6, 2));
	}
	else if (nums.size() == 3 && nums[0].GetLength() == 4)
	{
		y = _wtoi(nums[0]);
		m = _wtoi(nums[1]);
		d = _wtoi(nums[2]);
	}
	else
		return false;

	if (y < 1601 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31)
		return false;
	SYSTEMTIME t = {};
	t.wYear = static_cast<WORD>(y);
	t.wMonth = static_cast<WORD>(m);
	t.wDay = static_cast<WORD>(d);
	FILETIME ft = {};
	if (!::SystemTimeToFileTime(&t, &ft))   // 2월 30일 같은 날짜 거르기
		return false;
	::FileTimeToSystemTime(&ft, &t);        // 요일 채우기
	st = t;
	return true;
}

void CDarkDateTime::NotifyChanged(bool valid, const SYSTEMTIME& st)
{
	// 사용자가 바꾼 것처럼 부모에게 DTN_DATETIMECHANGE 알림 (변경 표시 / 저장 대상)
	NMDATETIMECHANGE nm = {};
	nm.nmhdr.hwndFrom = GetSafeHwnd();
	nm.nmhdr.idFrom = static_cast<UINT_PTR>(GetDlgCtrlID());
	nm.nmhdr.code = DTN_DATETIMECHANGE;
	nm.dwFlags = valid ? GDT_VALID : GDT_NONE;
	nm.st = st;
	if (CWnd* parent = GetParent())
		parent->SendMessage(WM_NOTIFY, nm.nmhdr.idFrom, reinterpret_cast<LPARAM>(&nm));
}

bool CDarkDateTime::CommitTyped()
{
	if (!m_typing)
		return false;
	SYSTEMTIME st = {};
	const bool ok = ParseTyped(m_typed, st);
	m_typing = false;
	m_typed.Empty();
	if (ok)
	{
		SetTime(&st);
		NotifyChanged(true, st);
	}
	else
		::MessageBeep(MB_ICONWARNING);   // 잘못된 날짜: 원래 값 유지
	Invalidate();
	return ok;
}

void CDarkDateTime::CancelTyped()
{
	m_typing = false;
	m_typed.Empty();
	Invalidate();
}

UINT CDarkDateTime::OnGetDlgCode()
{
	UINT code = CDateTimeCtrl::OnGetDlgCode() | DLGC_WANTCHARS;
	if (m_typing)
		code |= DLGC_WANTALLKEYS;   // 입력 중 Enter / Esc 는 이 칸에서 처리 (대화상자 확인/닫기 안 함)
	return code;
}

void CDarkDateTime::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	const wchar_t c = static_cast<wchar_t>(nChar);
	if (c >= L'0' && c <= L'9')
	{
		if (!m_typing)
		{
			m_typing = true;
			m_typed.Empty();
		}
		if (m_typed.GetLength() < 10)
			m_typed += c;
		// 구분자 없이 8자리를 다 치면 바로 확정
		if (m_typed.GetLength() == 8 && m_typed.SpanIncluding(L"0123456789").GetLength() == 8)
			CommitTyped();
		else
			Invalidate();
		return;
	}
	if (c == L'-' || c == L'.' || c == L'/' || c == L' ')
	{
		if (m_typing && m_typed.GetLength() < 10)
		{
			m_typed += L'-';
			Invalidate();
		}
		return;
	}
	if (c == L'\b' || c == L'\r' || c == 27)
		return;   // OnKeyDown 에서 처리
	CDateTimeCtrl::OnChar(nChar, nRepCnt, nFlags);
}

void CDarkDateTime::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (m_typing)
	{
		if (nChar == VK_RETURN) { CommitTyped(); return; }
		if (nChar == VK_ESCAPE) { CancelTyped(); return; }
		if (nChar == VK_BACK)
		{
			if (!m_typed.IsEmpty())
				m_typed.Delete(m_typed.GetLength() - 1);
			if (m_typed.IsEmpty())
				m_typing = false;
			Invalidate();
			return;
		}
	}
	else if ((nChar == VK_DELETE || nChar == VK_BACK) && (GetStyle() & DTS_SHOWNONE))
	{
		// 날짜 지우기 (없음)
		SYSTEMTIME st = {};
		if (GetTime(&st) == GDT_VALID)
		{
			SetTime(static_cast<LPSYSTEMTIME>(nullptr));
			NotifyChanged(false, st);
			Invalidate();
		}
		return;
	}
	CDateTimeCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CDarkDateTime::SetColors(COLORREF bg, COLORREF text, COLORREF border)
{
	m_bg = bg;
	m_text = text;
	m_border = border;
	if (GetSafeHwnd())
		Invalidate();
}

BOOL CDarkDateTime::OnEraseBkgnd(CDC* /*pDC*/)
{
	return TRUE;
}

void CDarkDateTime::OnEnable(BOOL bEnable)
{
	CDateTimeCtrl::OnEnable(bEnable);
	Invalidate();
}

void CDarkDateTime::OnSetFocus(CWnd* pOldWnd)
{
	CDateTimeCtrl::OnSetFocus(pOldWnd);
	Invalidate();
}

void CDarkDateTime::OnKillFocus(CWnd* pNewWnd)
{
	if (m_typing)
		CommitTyped();   // 입력하던 날짜는 포커스가 나갈 때 확정 (잘못되면 원래 값)
	CDateTimeCtrl::OnKillFocus(pNewWnd);
	Invalidate();
}

void CDarkDateTime::OnPaint()
{
	CPaintDC dc(this);
	CRect rc;
	GetClientRect(&rc);
	if (rc.IsRectEmpty())
		return;

	CDC mem;
	mem.CreateCompatibleDC(&dc);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
	CBitmap* oldBmp = mem.SelectObject(&bmp);

	const bool enabled = IsWindowEnabled() != FALSE;
	const bool focused = (GetFocus() == this);

	mem.FillSolidRect(&rc, m_bg);
	CBrush border(focused ? RGB(0, 120, 215) : m_border);
	mem.FrameRect(&rc, &border);

	// 컨트롤이 알려주는 체크박스 / 드롭다운 버튼 위치
	DATETIMEPICKERINFO info = {};
	info.cbSize = sizeof(info);
	SendMessage(DTM_GETDATETIMEPICKERINFO, 0, reinterpret_cast<LPARAM>(&info));

	SYSTEMTIME st = {};
	const bool valid = (GetTime(&st) == GDT_VALID);
	const COLORREF textColor = (enabled && valid) ? m_text : Dim(m_text);

	CFont* oldFont = mem.SelectObject(GetFont());
	mem.SetBkMode(TRANSPARENT);

	int textLeft = 5;
	if (GetStyle() & DTS_SHOWNONE)
	{
		CRect cb = info.rcCheck;
		const int box = (std::min)(rc.Height() - 6, 13);
		if (cb.IsRectEmpty())
			cb.SetRect(3, 0, 3 + box + 4, rc.bottom);
		CRect sq(cb.left + (cb.Width() - box) / 2, (rc.Height() - box) / 2, 0, 0);
		sq.right = sq.left + box;
		sq.bottom = sq.top + box;

		CBrush boxBorder(enabled ? m_text : Dim(m_text));
		mem.FrameRect(&sq, &boxBorder);
		if (valid)
		{
			// 체크 표시
			CPen pen(PS_SOLID, (std::max)(1, box / 7), enabled ? m_text : Dim(m_text));
			CPen* oldPen = mem.SelectObject(&pen);
			mem.MoveTo(sq.left + box * 2 / 10, sq.top + box * 5 / 10);
			mem.LineTo(sq.left + box * 4 / 10, sq.top + box * 7 / 10);
			mem.LineTo(sq.left + box * 8 / 10, sq.top + box * 3 / 10);
			mem.SelectObject(oldPen);
		}
		textLeft = (std::max)(static_cast<int>(cb.right), static_cast<int>(sq.right)) + 4;
	}

	// 드롭다운 버튼 (▼)
	CRect btn = info.rcButton;
	if (btn.IsRectEmpty())
		btn.SetRect(rc.right - rc.Height(), 0, rc.right, rc.bottom);
	{
		const int cx = btn.left + btn.Width() / 2;
		const int cy = rc.Height() / 2;
		const int a = (std::max)(3, rc.Height() / 6);
		POINT tri[3] = { { cx - a, cy - a / 2 }, { cx + a, cy - a / 2 }, { cx, cy + a / 2 + 1 } };
		CBrush arrow(enabled ? m_text : Dim(m_text));
		CPen nullPen(PS_NULL, 0, RGB(0, 0, 0));
		CBrush* oldBrush = mem.SelectObject(&arrow);
		CPen* oldPen = mem.SelectObject(&nullPen);
		mem.Polygon(tri, 3);
		mem.SelectObject(oldBrush);
		mem.SelectObject(oldPen);
	}

	// 날짜 텍스트 (직접 입력 중이면 입력한 글자 + 커서)
	CString text;
	COLORREF drawColor = textColor;
	if (m_typing)
	{
		CString shown = m_typed;
		if (shown.SpanIncluding(L"0123456789").GetLength() == shown.GetLength())
		{
			// 숫자만 칠 때는 YYYY-MM-DD 모양으로 보여 줌
			CString f;
			for (int i = 0; i < shown.GetLength(); ++i)
			{
				if (i == 4 || i == 6) f += L'-';
				f += shown[i];
			}
			shown = f;
		}
		text = shown + L"_";
		drawColor = m_text;
	}
	else if (valid)
		text.Format(L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
	mem.SetTextColor(drawColor);
	CRect tr(textLeft, 0, btn.left - 2, rc.bottom);
	// 선택(포커스)되면 날짜 글자 뒤를 밝은 회색 블록으로 칠함 (입력하면 통째로 바뀐다는 표시)
	if (focused && enabled && valid && !m_typing && !text.IsEmpty())
	{
		const int tw = mem.GetTextExtent(text).cx;
		CRect hl(tr.left - 2, 3, (std::min)(static_cast<int>(tr.left) + tw + 2, static_cast<int>(tr.right)), rc.bottom - 3);   // CRect 는 LONG
		mem.FillSolidRect(&hl, TCOL(TC_HILITE));
	}
	mem.DrawText(text, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

	mem.SelectObject(oldFont);
	dc.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(oldBmp);
}

// ===========================================================================
// CDarkScrollBar

BEGIN_MESSAGE_MAP(CDarkScrollBar, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_TIMER()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_MOUSEWHEEL()
	ON_WM_CAPTURECHANGED()
	ON_WM_MOUSEACTIVATE()
END_MESSAGE_MAP()

namespace
{
	const UINT_PTR kSyncTimer = 1;
	const UINT_PTR kRepeatTimer = 2;
}

BOOL CDarkScrollBar::Create(CWnd* parent, CWnd* target, bool vertical, TargetType type)
{
	m_target = target;
	m_vert = vertical;
	m_type = type;

	// 대상 창이 겹친 이 창을 덮어 그리지 않도록
	target->ModifyStyle(0, WS_CLIPSIBLINGS);

	const CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
	if (!CWnd::CreateEx(0, cls, L"", WS_CHILD | WS_CLIPSIBLINGS, CRect(0, 0, 1, 1), parent, 0))
		return FALSE;

	SetTimer(kSyncTimer, 50, nullptr);   // 대상 스크롤 상태를 주기적으로 맞춤
	Sync();
	return TRUE;
}

void CDarkScrollBar::SetColors(COLORREF track, COLORREF thumb, COLORREF thumbHot)
{
	m_track = track;
	m_thumb = thumb;
	m_thumbHot = thumbHot;
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

int CDarkScrollBar::MinThumb() const
{
	return (std::max)(16, ::GetSystemMetrics(m_vert ? SM_CYVTHUMB : SM_CXHTHUMB));
}

void CDarkScrollBar::Sync()
{
	if (!GetSafeHwnd() || !m_target || !m_target->GetSafeHwnd())
		return;

	const HWND target = m_target->GetSafeHwnd();
	auto barInfo = [&](LONG obj, CRect& rc) -> bool
	{
		SCROLLBARINFO sbi = {};
		sbi.cbSize = sizeof(sbi);
		if (!::GetScrollBarInfo(target, obj, &sbi))
			return false;
		if (sbi.rgstate[0] & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN))
			return false;
		rc = sbi.rcScrollBar;
		if (rc.IsRectEmpty())
			return false;
		if (obj == (m_vert ? OBJID_VSCROLL : OBJID_HSCROLL))
			m_enabled = !(sbi.rgstate[0] & STATE_SYSTEM_UNAVAILABLE);
		return true;
	};

	CRect bar;   // 화면 좌표
	const bool visible = m_target->IsWindowVisible() && barInfo(m_vert ? OBJID_VSCROLL : OBJID_HSCROLL, bar);
	if (!visible)
	{
		if (IsWindowVisible())
			ShowWindow(SW_HIDE);
		return;
	}

	// 세로 스크롤바는 가로 스크롤바와 만나는 오른쪽 아래 모서리 칸까지 덮음
	CRect win = bar;
	if (m_vert)
	{
		CRect h;
		if (barInfo(OBJID_HSCROLL, h))
			win.bottom = (std::max)(win.bottom, h.bottom);
	}

	CRect barClient = bar;
	barClient.OffsetRect(-win.left, -win.top);

	CWnd* parent = GetParent();
	CRect winInParent = win;
	parent->ScreenToClient(&winInParent);

	CRect cur;
	GetWindowRect(&cur);
	parent->ScreenToClient(&cur);
	if (cur != winInParent || !IsWindowVisible())
	{
		SetWindowPos(&CWnd::wndTop, winInParent.left, winInParent.top, winInParent.Width(), winInParent.Height(),
			SWP_NOACTIVATE | SWP_SHOWWINDOW);
		Invalidate(FALSE);
	}

	SCROLLINFO si = {};
	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL;
	::GetScrollInfo(target, m_vert ? SB_VERT : SB_HORZ, &si);
	if (si.nMin != m_si.nMin || si.nMax != m_si.nMax || si.nPage != m_si.nPage || si.nPos != m_si.nPos ||
		barClient != m_barRect)
	{
		m_si = si;
		m_barRect = barClient;
		Invalidate(FALSE);
	}
}

int CDarkScrollBar::ArrowLen() const
{
	const int thick = m_vert ? m_barRect.Width() : m_barRect.Height();
	const int len = m_vert ? m_barRect.Height() : m_barRect.Width();
	if (thick <= 0 || len < thick * 3)
		return 0;   // 너무 짧으면 버튼 없이 트랙만
	return thick;
}

CRect CDarkScrollBar::ArrowRect(int which) const
{
	const int a = ArrowLen();
	if (a <= 0)
		return CRect();
	if (m_vert)
		return (which == 1) ? CRect(m_barRect.left, m_barRect.top, m_barRect.right, m_barRect.top + a)
		                    : CRect(m_barRect.left, m_barRect.bottom - a, m_barRect.right, m_barRect.bottom);
	return (which == 1) ? CRect(m_barRect.left, m_barRect.top, m_barRect.left + a, m_barRect.bottom)
	                    : CRect(m_barRect.right - a, m_barRect.top, m_barRect.right, m_barRect.bottom);
}

CRect CDarkScrollBar::TrackRect() const
{
	CRect t = m_barRect;
	const int a = ArrowLen();
	if (m_vert) { t.top += a; t.bottom -= a; }
	else        { t.left += a; t.right -= a; }
	return t;
}

bool CDarkScrollBar::GetThumbRect(CRect& rc) const
{
	if (!m_enabled)
		return false;
	const int range = m_si.nMax - m_si.nMin + 1;
	const int page = static_cast<int>(m_si.nPage);
	const int trackLen = TrackLength();
	if (page <= 0 || range <= page || trackLen <= 0)
		return false;

	int thumbLen = (std::max)(MinThumb(), MulDiv(trackLen, page, range));
	thumbLen = (std::min)(thumbLen, trackLen);
	const int maxPos = range - page;
	const int pos = (std::max)(0, (std::min)(maxPos, m_si.nPos - m_si.nMin));
	const int off = MulDiv(trackLen - thumbLen, pos, maxPos);

	const CRect tr = TrackRect();
	if (m_vert)
		rc.SetRect(tr.left, tr.top + off, tr.right, tr.top + off + thumbLen);
	else
		rc.SetRect(tr.left + off, tr.top, tr.left + off + thumbLen, tr.bottom);
	return true;
}

void CDarkScrollBar::ScrollTargetTo(int pos)
{
	const int maxPos = m_si.nMax - static_cast<int>(m_si.nPage) + 1;
	pos = (std::max)(m_si.nMin, (std::min)(pos, maxPos));
	const int cur = m_si.nPos;
	if (pos == cur)
		return;

	const HWND target = m_target->GetSafeHwnd();
	switch (m_type)
	{
	case TARGET_GRID:
		::SendMessage(target, WM_DARKSCROLL_SETPOS, static_cast<WPARAM>(pos), m_vert ? 0 : 1);   // lParam 1 = 가로 스크롤바
		break;
	case TARGET_EDIT:
		if (m_vert)
			::SendMessage(target, EM_LINESCROLL, 0, pos - cur);
		else
			::SendMessage(target, EM_LINESCROLL, pos - cur, 0);
		break;
	case TARGET_LISTVIEW:
		if (m_vert)
		{
			// 보고서 보기: 세로 단위는 항목 → 픽셀로 바꿔서 스크롤
			RECT r = {};
			int itemH = 16;
			if (ListView_GetItemRect(target, 0, &r, LVIR_BOUNDS))
				itemH = (std::max)(1, static_cast<int>(r.bottom - r.top));
			ListView_Scroll(target, 0, (pos - cur) * itemH);
		}
		else
		{
			ListView_Scroll(target, pos - cur, 0);
		}
		break;
	}
	Sync();
}

void CDarkScrollBar::SendScroll(int code)
{
	m_target->SendMessage(m_vert ? WM_VSCROLL : WM_HSCROLL, MAKEWPARAM(code, 0), 0);
	Sync();
}

BOOL CDarkScrollBar::OnEraseBkgnd(CDC* /*pDC*/)
{
	return TRUE;
}

void CDarkScrollBar::OnPaint()
{
	CPaintDC paintDC(this);
	CRect rc;
	GetClientRect(&rc);
	// 메모리에 그린 뒤 한 번에 복사 (끌 때 깜빡임 방지)
	CDC dc;
	dc.CreateCompatibleDC(&paintDC);
	CBitmap buf;
	buf.CreateCompatibleBitmap(&paintDC, (std::max)(1, rc.Width()), (std::max)(1, rc.Height()));
	CBitmap* oldBuf = dc.SelectObject(&buf);

	dc.FillSolidRect(&rc, m_track);

	CRect thumb;
	if (GetThumbRect(thumb))
	{
		thumb.DeflateRect(m_vert ? 3 : 2, m_vert ? 2 : 3);
		const COLORREF c = (m_dragging || m_hot) ? m_thumbHot : m_thumb;
		CBrush brush(c);
		CPen pen(PS_SOLID, 1, c);
		CBrush* oldBrush = dc.SelectObject(&brush);
		CPen* oldPen = dc.SelectObject(&pen);
		const int radius = m_vert ? thumb.Width() : thumb.Height();
		dc.RoundRect(&thumb, CPoint(radius, radius));
		dc.SelectObject(oldBrush);
		dc.SelectObject(oldPen);
	}
	// 양 끝 화살표 버튼 (▲ ▼ / ◀ ▶): 마우스를 올리거나 누르면 밝게
	for (int which = 1; which <= 2; ++which)
	{
		const CRect ar = ArrowRect(which);
		if (ar.IsRectEmpty())
			continue;
		const bool pressed = (which == 1 && m_repeatCode == SB_LINEUP) || (which == 2 && m_repeatCode == SB_LINEDOWN);
		const COLORREF c = !m_enabled ? m_track : ((pressed || m_hotArrow == which) ? m_thumbHot : m_thumb);
		const int s = (std::max)(3, (std::min)(ar.Width(), ar.Height()) / 4);   // 삼각형 반 너비
		const CPoint mid = ar.CenterPoint();
		POINT pts[3];
		if (m_vert)
		{
			const int dir = (which == 1) ? -1 : 1;   // 위 / 아래
			pts[0] = { mid.x - s, mid.y - dir * s / 2 };
			pts[1] = { mid.x + s, mid.y - dir * s / 2 };
			pts[2] = { mid.x, mid.y + dir * (s - s / 2) + dir };
		}
		else
		{
			const int dir = (which == 1) ? -1 : 1;   // 왼쪽 / 오른쪽
			pts[0] = { mid.x - dir * s / 2, mid.y - s };
			pts[1] = { mid.x - dir * s / 2, mid.y + s };
			pts[2] = { mid.x + dir * (s - s / 2) + dir, mid.y };
		}
		CBrush brush(c);
		CPen pen(PS_SOLID, 1, c);
		CBrush* ob = dc.SelectObject(&brush);
		CPen* op = dc.SelectObject(&pen);
		dc.Polygon(pts, 3);
		dc.SelectObject(ob);
		dc.SelectObject(op);
	}
	paintDC.BitBlt(0, 0, rc.Width(), rc.Height(), &dc, 0, 0, SRCCOPY);
	dc.SelectObject(oldBuf);
}

void CDarkScrollBar::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kSyncTimer)
	{
		Sync();
		return;
	}
	if (nIDEvent == kRepeatTimer)
	{
		KillTimer(kRepeatTimer);
		if (m_repeatCode < 0 || !(::GetKeyState(VK_LBUTTON) & 0x8000))
			return;

		// 마우스가 아직 썸의 해당 방향에 있으면 계속 페이지 이동
		CPoint pt;
		::GetCursorPos(&pt);
		ScreenToClient(&pt);
		if (m_repeatCode == SB_LINEUP || m_repeatCode == SB_LINEDOWN)
		{
			// 화살표 버튼 위에 있는 동안 계속 한 줄씩
			if (ArrowRect(m_repeatCode == SB_LINEUP ? 1 : 2).PtInRect(pt))
				SendScroll(m_repeatCode);
			SetTimer(kRepeatTimer, 50, nullptr);
			return;
		}
		CRect thumb;
		if (GetThumbRect(thumb) && m_barRect.PtInRect(pt))
		{
			const int p = m_vert ? pt.y : pt.x;
			const int a = m_vert ? thumb.top : thumb.left;
			const int b = m_vert ? thumb.bottom : thumb.right;
			if ((m_repeatCode == SB_PAGEUP && p < a) || (m_repeatCode == SB_PAGEDOWN && p >= b))
				SendScroll(m_repeatCode);
		}
		SetTimer(kRepeatTimer, 60, nullptr);
		return;
	}
	CWnd::OnTimer(nIDEvent);
}

void CDarkScrollBar::OnLButtonDown(UINT /*nFlags*/, CPoint point)
{
	if (!m_barRect.PtInRect(point))
		return;   // 모서리 칸

	// 양 끝 화살표 버튼: 한 줄씩, 누르고 있으면 반복
	for (int which = 1; which <= 2; ++which)
	{
		if (ArrowRect(which).PtInRect(point))
		{
			if (!m_enabled)
				return;
			m_repeatCode = (which == 1) ? SB_LINEUP : SB_LINEDOWN;
			SendScroll(m_repeatCode);
			SetCapture();
			SetTimer(kRepeatTimer, 300, nullptr);
			Invalidate(FALSE);
			return;
		}
	}

	CRect thumb;
	if (!GetThumbRect(thumb))
		return;

	if (thumb.PtInRect(point))
	{
		m_dragging = true;
		m_dragStartMouse = m_vert ? point.y : point.x;
		m_dragStartPos = m_si.nPos;
		SetCapture();
		Invalidate(FALSE);
		return;
	}

	const int p = m_vert ? point.y : point.x;
	m_repeatCode = (p < (m_vert ? thumb.top : thumb.left)) ? SB_PAGEUP : SB_PAGEDOWN;
	SendScroll(m_repeatCode);
	SetCapture();
	SetTimer(kRepeatTimer, 300, nullptr);
}

void CDarkScrollBar::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	OnLButtonDown(nFlags, point);
}

void CDarkScrollBar::OnLButtonUp(UINT /*nFlags*/, CPoint /*point*/)
{
	const bool wasActive = m_dragging || m_repeatCode >= 0;
	m_dragging = false;
	m_repeatCode = -1;
	KillTimer(kRepeatTimer);
	if (GetCapture() == this)
		ReleaseCapture();
	if (wasActive)
		SendScroll(SB_ENDSCROLL);
	Invalidate(FALSE);
}

void CDarkScrollBar::OnCaptureChanged(CWnd* pWnd)
{
	if (pWnd != this)
	{
		m_dragging = false;
		m_repeatCode = -1;
		KillTimer(kRepeatTimer);
		Invalidate(FALSE);
	}
	CWnd::OnCaptureChanged(pWnd);
}

void CDarkScrollBar::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_dragging)
	{
		CRect thumb;
		if (GetThumbRect(thumb))
		{
			const int thumbLen = m_vert ? thumb.Height() : thumb.Width();
			const int room = TrackLength() - thumbLen;
			const int maxPos = (m_si.nMax - m_si.nMin + 1) - static_cast<int>(m_si.nPage);
			if (room > 0 && maxPos > 0)
			{
				const int delta = (m_vert ? point.y : point.x) - m_dragStartMouse;
				ScrollTargetTo(m_dragStartPos + MulDiv(delta, maxPos, room));
			}
		}
		return;
	}

	// 마우스가 썸 위에 있으면 밝게
	CRect thumb;
	const bool hot = GetThumbRect(thumb) && thumb.PtInRect(point);
	if (hot != m_hot)
	{
		m_hot = hot;
		Invalidate(FALSE);
	}
	// 화살표 버튼 위
	const int hotArrow = ArrowRect(1).PtInRect(point) ? 1 : (ArrowRect(2).PtInRect(point) ? 2 : 0);
	if (hotArrow != m_hotArrow)
	{
		m_hotArrow = hotArrow;
		Invalidate(FALSE);
	}
	TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
	::TrackMouseEvent(&tme);
	CWnd::OnMouseMove(nFlags, point);
}

void CDarkScrollBar::OnMouseLeave()
{
	if (m_hot || m_hotArrow)
	{
		m_hot = false;
		m_hotArrow = 0;
		Invalidate(FALSE);
	}
	CWnd::OnMouseLeave();
}

BOOL CDarkScrollBar::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	// 휠은 대상 창으로 전달
	m_target->SendMessage(WM_MOUSEWHEEL, MAKEWPARAM(nFlags, zDelta), MAKELPARAM(pt.x, pt.y));
	Sync();
	return TRUE;
}

int CDarkScrollBar::OnMouseActivate(CWnd* /*pDesktopWnd*/, UINT /*nHitTest*/, UINT /*message*/)
{
	return MA_NOACTIVATE;   // 클릭해도 포커스를 가져가지 않음
}

// ---------------------------------------------------------------------------
// CDarkCheck: Windows 11 모양 체크박스 · 라디오 버튼

namespace
{
	void EnsureGdiPlusCheck()
	{
		static ULONG_PTR token = 0;
		if (!token)
		{
			Gdiplus::GdiplusStartupInput input;
			Gdiplus::GdiplusStartup(&token, &input, nullptr);
		}
	}
	Gdiplus::Color GdipColor(COLORREF c, BYTE a = 255)
	{
		return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
	}
	COLORREF ShadeCheck(COLORREF c, int pct)   // pct < 0 어둡게, > 0 밝게
	{
		auto ch = [pct](int v) { return pct < 0 ? v * (100 + pct) / 100 : v + (255 - v) * pct / 100; };
		return RGB(ch(GetRValue(c)), ch(GetGValue(c)), ch(GetBValue(c)));
	}
	void AddRoundRect(Gdiplus::GraphicsPath& p, const Gdiplus::RectF& r, float rad)
	{
		const float d = rad * 2.0f;
		p.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
		p.AddArc(r.GetRight() - d, r.Y, d, d, 270.0f, 90.0f);
		p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0.0f, 90.0f);
		p.AddArc(r.X, r.GetBottom() - d, d, d, 90.0f, 90.0f);
		p.CloseFigure();
	}
}

bool CDarkCheck::Subclass(HWND hwnd)
{
	if (!SubclassWindow(hwnd))
		return false;
	Invalidate();
	return true;
}

bool CDarkCheck::IsRadio() const
{
	const LONG type = GetStyle() & BS_TYPEMASK;
	return type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON;
}

void CDarkCheck::PaintTo(CDC* dc)
{
	CRect rc;
	GetClientRect(&rc);
	dc->FillSolidRect(rc, TCOL(TC_BACK));
	CFont* font = GetFont();
	CFont* oldFont = font ? dc->SelectObject(font) : nullptr;
	TEXTMETRIC tm = {};
	dc->GetTextMetrics(&tm);

	const bool enabled = IsWindowEnabled() != FALSE;
	const LRESULT state = ::SendMessageW(m_hWnd, BM_GETSTATE, 0, 0);
	const int check = static_cast<int>(::SendMessageW(m_hWnd, BM_GETCHECK, 0, 0));
	const bool pushed = (state & BST_PUSHED) != 0;
	const bool on = (check != BST_UNCHECKED);
	const bool radio = IsRadio();

	// 칸 크기: 글자 높이에 맞춤 (9pt 맑은 고딕 · 100% 에서 약 14px)
	const int size = (std::max)(12, static_cast<int>(tm.tmHeight) * 7 / 8);
	const bool rightButton = (GetStyle() & BS_LEFTTEXT) != 0;
	const int boxX = rightButton ? rc.right - size - 1 : rc.left + 1;
	const int boxY = rc.top + (rc.Height() - size) / 2;
	const COLORREF accent = TCOL(TC_ACCENT);

	EnsureGdiPlusCheck();
	{
		Gdiplus::Graphics g(dc->GetSafeHdc());
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		const Gdiplus::RectF box(boxX + 0.5f, boxY + 0.5f, size - 1.0f, size - 1.0f);
		COLORREF fill, border;
		if (on)
		{
			fill = enabled ? (pushed ? ShadeCheck(accent, -15) : (m_hot ? ShadeCheck(accent, 12) : accent)) : TCOL(TC_DISABLED);
			border = fill;
		}
		else
		{
			fill = pushed ? ShadeCheck(TCOL(TC_EDIT), 8) : TCOL(TC_EDIT);
			border = !enabled ? TCOL(TC_BORDER) : (m_hot || pushed ? TCOL(TC_LIGHT) : TCOL(TC_SUB));
		}
		Gdiplus::SolidBrush fb(GdipColor(fill));
		Gdiplus::Pen bp(GdipColor(border), 1.0f);
		if (radio)
		{
			g.FillEllipse(&fb, box);
			g.DrawEllipse(&bp, box);
			if (on)
			{
				// 가운데 흰 원 (마우스를 올리면 조금 크게)
				const float d = size * (m_hot && enabled && !pushed ? 0.46f : 0.40f);
				Gdiplus::SolidBrush dot(GdipColor(enabled ? RGB(255, 255, 255) : TCOL(TC_EDIT)));
				g.FillEllipse(&dot, box.X + (box.Width - d) / 2.0f, box.Y + (box.Height - d) / 2.0f, d, d);
			}
		}
		else
		{
			Gdiplus::GraphicsPath path;
			AddRoundRect(path, box, (std::max)(2.0f, size * 0.22f));
			g.FillPath(&fb, &path);
			g.DrawPath(&bp, &path);
			if (on)
			{
				Gdiplus::Pen mark(GdipColor(enabled ? RGB(255, 255, 255) : TCOL(TC_EDIT)), (std::max)(1.5f, size * 0.12f));
				mark.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
				mark.SetLineJoin(Gdiplus::LineJoinRound);
				if (check == BST_INDETERMINATE)
					g.DrawLine(&mark, box.X + size * 0.28f, box.Y + box.Height / 2.0f, box.GetRight() - size * 0.28f, box.Y + box.Height / 2.0f);
				else
				{
					const Gdiplus::PointF pts[3] = {
						Gdiplus::PointF(box.X + size * 0.24f, box.Y + size * 0.52f),
						Gdiplus::PointF(box.X + size * 0.42f, box.Y + size * 0.69f),
						Gdiplus::PointF(box.X + size * 0.76f, box.Y + size * 0.33f) };
					g.DrawLines(&mark, pts, 3);
				}
			}
		}
	}

	// 글자
	CString text;
	GetWindowText(text);
	const int gap = (std::max)(4, size / 2);
	CRect tr = rc;
	if (rightButton)
		tr.right = boxX - gap;
	else
		tr.left = boxX + size + gap;
	dc->SetBkMode(TRANSPARENT);
	dc->SetTextColor(enabled ? TCOL(TC_TEXT) : TCOL(TC_DISABLED));
	const bool hidePrefix = (::SendMessageW(m_hWnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEACCEL) != 0;
	UINT fmt = DT_VCENTER | DT_LEFT | (hidePrefix ? DT_HIDEPREFIX : 0);
	if (GetStyle() & BS_MULTILINE)
	{
		// 여러 줄: 높이를 재서 세로 가운데
		CRect calc = tr;
		dc->DrawText(text, calc, DT_WORDBREAK | DT_CALCRECT | (hidePrefix ? DT_HIDEPREFIX : 0));
		tr.top += (tr.Height() - calc.Height()) / 2;
		tr.bottom = tr.top + calc.Height();
		fmt = DT_WORDBREAK | DT_LEFT | (hidePrefix ? DT_HIDEPREFIX : 0);
	}
	else
		fmt |= DT_SINGLELINE;
	dc->DrawText(text, tr, fmt);
	// 키보드 포커스: 글자 둘레 점선 (마우스로 누를 때는 표시 안 함)
	if (GetFocus() == this && !(::SendMessageW(m_hWnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS) && !text.IsEmpty())
	{
		CRect fr = tr;
		dc->DrawText(text, fr, (fmt & ~DT_VCENTER) | DT_CALCRECT);
		const int h = fr.Height();
		fr.OffsetRect(0, (tr.Height() - h) / 2 - (fr.top - tr.top));
		fr.InflateRect(2, 1);
		dc->SetTextColor(TCOL(TC_TEXT));
		dc->DrawFocusRect(fr);
	}
	if (oldFont)
		dc->SelectObject(oldFont);
}

LRESULT CDarkCheck::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_ERASEBKGND:
		return TRUE;
	case WM_PAINT:
	case WM_PRINTCLIENT:
	{
		PAINTSTRUCT ps = {};
		HDC hdc = (message == WM_PAINT) ? ::BeginPaint(m_hWnd, &ps) : reinterpret_cast<HDC>(wParam);
		CRect rc;
		GetClientRect(&rc);
		// 메모리에 그린 뒤 한 번에 복사 (깜빡임 없게)
		CDC* screen = CDC::FromHandle(hdc);
		CDC mem;
		mem.CreateCompatibleDC(screen);
		CBitmap bmp;
		bmp.CreateCompatibleBitmap(screen, (std::max)(1, rc.Width()), (std::max)(1, rc.Height()));
		CBitmap* old = mem.SelectObject(&bmp);
		PaintTo(&mem);
		screen->BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
		mem.SelectObject(old);
		if (message == WM_PAINT)
			::EndPaint(m_hWnd, &ps);
		return 0;
	}
	case WM_MOUSEMOVE:
		if (!m_tracking)
		{
			TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
			m_tracking = ::TrackMouseEvent(&tme) != FALSE;
		}
		if (!m_hot)
		{
			m_hot = true;
			Invalidate(FALSE);
		}
		break;
	case WM_MOUSELEAVE:
		m_tracking = false;
		m_hot = false;
		Invalidate(FALSE);
		break;
	default:
		break;
	}
	const LRESULT r = CWnd::WindowProc(message, wParam, lParam);
	// 원래 컨트롤이 상태를 바꾸며 직접 그린 뒤 → 우리 모양으로 다시 그림
	switch (message)
	{
	case BM_SETCHECK: case BM_SETSTATE: case BM_CLICK: case BM_SETSTYLE:
	case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
	case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR:
	case WM_ENABLE: case WM_SETTEXT: case WM_SETFOCUS: case WM_KILLFOCUS:
	case WM_CAPTURECHANGED: case WM_UPDATEUISTATE: case WM_CHANGEUISTATE:
		if (::IsWindow(m_hWnd))
			::RedrawWindow(m_hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
		break;
	case WM_MOUSEMOVE:   // 누른 채 안팎으로 움직일 때만 (눌림 표시가 바뀜)
		if (::IsWindow(m_hWnd) && ::GetCapture() == m_hWnd)
			::RedrawWindow(m_hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
		break;
	default:
		break;
	}
	return r;
}


// ---------------------------------------------------------------------------
// DarkMenu: 테마 색 오른쪽 클릭 메뉴 (직접 그리기)

namespace
{
	const DWORD kDarkMenuMagic = 0x554E4D44;   // 'DMNU' - 이 모듈이 만든 항목인지 확인

	struct DarkMenuItem
	{
		DWORD   magic = kDarkMenuMagic;
		CString text;     // \t 앞
		CString accel;    // \t 뒤 (단축키 글자)
		bool    sep = false;
		bool    sub = false;
		bool    def = false;
		int     icon = 0;     // 왼쪽 아이콘 (DarkMenuIconFor)
	};

	// 메뉴 글자 → 아이콘 (위에서부터 먼저 맞는 것)
	enum DarkMenuIcon { DMI_NONE = 0, DMI_UNCHECK, DMI_CHECKALL, DMI_TRASH, DMI_MINUS, DMI_ERASER, DMI_MERGE, DMI_PASTE, DMI_TEXT,
		DMI_PICTURE, DMI_REFRESH, DMI_FOLDER, DMI_PLAY, DMI_COLLECTION, DMI_PLUS, DMI_PENCIL, DMI_GEAR, DMI_MOVIE, DMI_COPY,
		DMI_ACTOR, DMI_STUDIO, DMI_TAG };   // 배우 · 제작사 · 태그 탭 상징 (목록 관리 메뉴)
	int DarkMenuIconFor(const CString& text)
	{
		struct Rule { const wchar_t* word; int icon; };
		static const Rule rules[] = {
			{ L"체크 모두 해제", DMI_UNCHECK }, { L"체크 해제", DMI_UNCHECK }, { L"전체 체크", DMI_CHECKALL },
			{ L"빼기", DMI_MINUS }, { L"삭제", DMI_TRASH }, { L"모두 지우기", DMI_TRASH },
			{ L"비우기", DMI_ERASER }, { L"초기화", DMI_ERASER },
			{ L"합치기", DMI_MERGE }, { L"병합", DMI_MERGE },
			{ L"붙여넣기", DMI_PASTE }, { L"텍스트로", DMI_TEXT }, { L"스크린샷", DMI_PICTURE }, { L"새로고침", DMI_REFRESH },
			{ L"탐색기", DMI_FOLDER }, { L"폴더", DMI_FOLDER }, { L"복사", DMI_COPY },
			{ L"재생", DMI_PLAY }, { L"컬렉션에 추가", DMI_COLLECTION },
			{ L"추가", DMI_PLUS }, { L"새 ", DMI_PLUS },
			{ L"편집", DMI_PENCIL }, { L"변경", DMI_PENCIL },
			{ L"배우 관리", DMI_ACTOR }, { L"제작사 관리", DMI_STUDIO }, { L"태그 관리", DMI_TAG },   // 목록 관리: 각 탭의 상징 (품번 관리는 톱니바퀴 그대로)
			{ L"관리", DMI_GEAR },
			{ L"보기", DMI_MOVIE } };
		for (const Rule& r : rules)
			if (text.Find(r.word) >= 0)
				return r.icon;
		return DMI_NONE;
	}

	void DarkMenuDrawIcon(CDC* dc, int icon, const CRect& area, COLORREF col)
	{
		switch (icon)
		{
		case DMI_UNCHECK:    VectorIcon::CheckOff(dc, area, col); break;
		case DMI_CHECKALL:   VectorIcon::CheckOn(dc, area, col); break;
		case DMI_TRASH:      VectorIcon::Trash(dc, area, col); break;
		case DMI_MINUS:      VectorIcon::MinusCircle(dc, area, col); break;
		case DMI_ERASER:     VectorIcon::Eraser(dc, area, col); break;
		case DMI_MERGE:      VectorIcon::Merge(dc, area, col); break;
		case DMI_PASTE:      VectorIcon::Paste(dc, area, col); break;
		case DMI_TEXT:       VectorIcon::TextDoc(dc, area, col); break;
		case DMI_PICTURE:    VectorIcon::Picture(dc, area, col); break;
		case DMI_REFRESH:    VectorIcon::Refresh(dc, area, col); break;
		case DMI_FOLDER:     VectorIcon::Folder(dc, area, col); break;
		case DMI_PLAY:       VectorIcon::Play(dc, area, col, 0.9); break;
		case DMI_COLLECTION: VectorIcon::Collection(dc, area, col); break;
		case DMI_PLUS:       VectorIcon::PlusCircle(dc, area, col); break;
		case DMI_PENCIL:     VectorIcon::Pencil(dc, area, col); break;
		case DMI_GEAR:       VectorIcon::Gear(dc, area, col); break;
		case DMI_MOVIE:      VectorIcon::Movie(dc, area, col); break;
		case DMI_COPY:       VectorIcon::Copy(dc, area, col); break;
		case DMI_ACTOR:      VectorIcon::User(dc, area, col); break;     // 배우 탭 버튼과 같은 사람
		case DMI_STUDIO:     VectorIcon::Camera(dc, area, col); break;   // 제작사 탭: 비디오 카메라
		case DMI_TAG:        VectorIcon::Tag(dc, area, col); break;      // 태그 탭: 꼬리표
		default: break;
		}
	}

	struct DarkMenuState
	{
		HWND    owner = nullptr;
		WNDPROC oldProc = nullptr;
		HFONT   font = nullptr;
		HFONT   bold = nullptr;
		HBRUSH  back = nullptr;
		int     dpi = 96;
		std::vector<std::unique_ptr<DarkMenuItem>> items;
		std::vector<HWND> bordered;   // 테두리 색을 정한 메뉴 창
	};
	DarkMenuState* g_darkMenu = nullptr;

	int DmScale(int v) { return ::MulDiv(v, g_darkMenu ? g_darkMenu->dpi : 96, 96); }

	void DarkMenuConvert(HMENU hm)
	{
		const int n = ::GetMenuItemCount(hm);
		for (int i = 0; i < n; ++i)
		{
			wchar_t buf[512] = {};
			MENUITEMINFOW mii = { sizeof(mii) };
			mii.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_SUBMENU | MIIM_STRING;
			mii.dwTypeData = buf;
			mii.cch = _countof(buf);
			if (!::GetMenuItemInfoW(hm, i, TRUE, &mii) || (mii.fType & MFT_OWNERDRAW))
				continue;
			auto it = std::make_unique<DarkMenuItem>();
			it->sep = (mii.fType & MFT_SEPARATOR) != 0;
			it->sub = mii.hSubMenu != nullptr;
			it->def = (mii.fState & MFS_DEFAULT) != 0;
			CString t = buf;
			const int tab = t.Find(L'\t');
			if (tab >= 0)
			{
				it->accel = t.Mid(tab + 1);
				t = t.Left(tab);
			}
			it->text = t;
			if (!it->sep)
				it->icon = DarkMenuIconFor(t);
			MENUITEMINFOW set = { sizeof(set) };
			set.fMask = MIIM_FTYPE | MIIM_DATA;
			set.fType = mii.fType | MFT_OWNERDRAW;
			set.dwItemData = reinterpret_cast<ULONG_PTR>(it.get());
			::SetMenuItemInfoW(hm, i, TRUE, &set);
			g_darkMenu->items.push_back(std::move(it));
			if (mii.hSubMenu)
				DarkMenuConvert(mii.hSubMenu);
		}
		// 메뉴 가장자리(위아래 여백)도 같은 배경색
		MENUINFO mi = { sizeof(mi) };
		mi.fMask = MIM_BACKGROUND;
		mi.hbrBack = g_darkMenu->back;
		::SetMenuInfo(hm, &mi);
	}

	void DarkMenuMeasure(MEASUREITEMSTRUCT* mis, const DarkMenuItem* it)
	{
		if (it->sep)
		{
			mis->itemWidth = DmScale(10);
			mis->itemHeight = DmScale(7);
			return;
		}
		HDC dc = ::GetDC(nullptr);
		HGDIOBJ old = ::SelectObject(dc, it->def ? g_darkMenu->bold : g_darkMenu->font);
		RECT tr = {}, ar = {};
		::DrawTextW(dc, it->text, -1, &tr, DT_SINGLELINE | DT_CALCRECT);
		int w = DmScale(30) + (tr.right - tr.left) + DmScale(24);
		if (!it->accel.IsEmpty())
		{
			::DrawTextW(dc, it->accel, -1, &ar, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
			w += DmScale(28) + (ar.right - ar.left);
		}
		TEXTMETRICW tm = {};
		::GetTextMetricsW(dc, &tm);
		::SelectObject(dc, old);
		::ReleaseDC(nullptr, dc);
		mis->itemWidth = static_cast<UINT>(w);
		mis->itemHeight = static_cast<UINT>((std::max)(static_cast<int>(tm.tmHeight) + DmScale(10), DmScale(24)));
	}

	void DarkMenuDraw(const DRAWITEMSTRUCT* d, const DarkMenuItem* it)
	{
		HDC dc = d->hDC;
		const RECT rc = d->rcItem;
		const COLORREF back = TCOL(TC_COMBO);
		HBRUSH bb = ::CreateSolidBrush(back);
		::FillRect(dc, &rc, bb);
		::DeleteObject(bb);
		if (it->sep)
		{
			const int y = (rc.top + rc.bottom) / 2;
			RECT line = { rc.left + DmScale(8), y, rc.right - DmScale(8), y + 1 };
			HBRUSH lb = ::CreateSolidBrush(TCOL(TC_EDGE));
			::FillRect(dc, &line, lb);
			::DeleteObject(lb);
			::ExcludeClipRect(dc, rc.left, rc.top, rc.right, rc.bottom);
			return;
		}
		const bool disabled = (d->itemState & (ODS_GRAYED | ODS_DISABLED)) != 0;
		const bool sel = (d->itemState & ODS_SELECTED) != 0;
		if (sel && !disabled)
		{
			// 마우스 오버 / 키보드 선택: 버튼 파랑 둥근 칸
			HBRUSH sb = ::CreateSolidBrush(TCOL(TC_ACCENT));
			HPEN sp = ::CreatePen(PS_SOLID, 1, TCOL(TC_ACCENT));
			HGDIOBJ ob = ::SelectObject(dc, sb), op = ::SelectObject(dc, sp);
			const int r = DmScale(6);
			::RoundRect(dc, rc.left + DmScale(4), rc.top + 1, rc.right - DmScale(4), rc.bottom - 1, r, r);
			::SelectObject(dc, ob);
			::SelectObject(dc, op);
			::DeleteObject(sb);
			::DeleteObject(sp);
		}
		else if (sel && disabled)
		{
			// 비활성 항목 위: 아주 옅은 칸만
			const COLORREF t = TCOL(TC_EDGE);
			HBRUSH sb = ::CreateSolidBrush(RGB((GetRValue(back) + GetRValue(t)) / 2, (GetGValue(back) + GetGValue(t)) / 2, (GetBValue(back) + GetBValue(t)) / 2));
			RECT sr = { rc.left + DmScale(4), rc.top + 1, rc.right - DmScale(4), rc.bottom - 1 };
			::FillRect(dc, &sr, sb);
			::DeleteObject(sb);
		}
		const COLORREF text = disabled ? TCOL(TC_DISABLED) : (sel ? RGB(255, 255, 255) : TCOL(TC_TEXT));
		const COLORREF sub = disabled ? TCOL(TC_DISABLED) : (sel ? RGB(225, 238, 248) : TCOL(TC_SUB2));
		::SetBkMode(dc, TRANSPARENT);

		// 왼쪽 칸: 체크된 항목은 체크 표시, 아니면 기능 아이콘 (삭제는 빨간 휴지통)
		if (!(d->itemState & ODS_CHECKED) && it->icon != 0)
		{
			const int isz = DmScale(16);
			const int ix = rc.left + DmScale(8), iy = (rc.top + rc.bottom - isz) / 2;
			COLORREF ic = disabled ? TCOL(TC_DISABLED) : (sel ? RGB(255, 255, 255) : TCOL(TC_SUB2));
			if (it->icon == DMI_TRASH && !disabled && !sel)
				ic = TCOL(TC_DANGER);
			CDC* cdc = CDC::FromHandle(dc);
			DarkMenuDrawIcon(cdc, it->icon, CRect(ix, iy, ix + isz, iy + isz), ic);
		}
		if (d->itemState & ODS_CHECKED)
		{
			const int cx = rc.left + DmScale(15), cy = (rc.top + rc.bottom) / 2;
			HPEN cp = ::CreatePen(PS_SOLID, (std::max)(2, DmScale(2)), text);
			HGDIOBJ op = ::SelectObject(dc, cp);
			::MoveToEx(dc, cx - DmScale(5), cy, nullptr);
			::LineTo(dc, cx - DmScale(1), cy + DmScale(4));
			::LineTo(dc, cx + DmScale(6), cy - DmScale(4));
			::SelectObject(dc, op);
			::DeleteObject(cp);
		}

		const UINT prefix = (d->itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0;
		HGDIOBJ oldFont = ::SelectObject(dc, it->def ? g_darkMenu->bold : g_darkMenu->font);
		RECT tr = { rc.left + DmScale(30), rc.top, rc.right - DmScale(20), rc.bottom };
		if (!it->accel.IsEmpty())
		{
			::SelectObject(dc, g_darkMenu->font);
			::SetTextColor(dc, sub);
			RECT ar = tr;
			::DrawTextW(dc, it->accel, -1, &ar, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			::SelectObject(dc, it->def ? g_darkMenu->bold : g_darkMenu->font);
		}
		::SetTextColor(dc, text);
		::DrawTextW(dc, it->text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | prefix);
		::SelectObject(dc, oldFont);

		// 하위 메뉴 화살표 (오른쪽, 직접 그림 - 시스템 화살표는 아래 ExcludeClipRect 로 막음)
		if (it->sub)
		{
			const int ax = rc.right - DmScale(12), ay = (rc.top + rc.bottom) / 2, s = DmScale(4);
			POINT pts[3] = { { ax - s / 2, ay - s }, { ax + s / 2 + 1, ay }, { ax - s / 2, ay + s } };
			HBRUSH ab = ::CreateSolidBrush(text);
			HPEN ap = ::CreatePen(PS_SOLID, 1, text);
			HGDIOBJ ob = ::SelectObject(dc, ab), op = ::SelectObject(dc, ap);
			::Polygon(dc, pts, 3);
			::SelectObject(dc, ob);
			::SelectObject(dc, op);
			::DeleteObject(ab);
			::DeleteObject(ap);
		}
		::ExcludeClipRect(dc, rc.left, rc.top, rc.right, rc.bottom);   // 시스템이 그 위에 검은 화살표를 그리지 못하게
	}

	void DarkMenuBorder(HWND menuWnd)
	{
		// Windows 11: 메뉴 창 테두리 색 (DWMWA_BORDER_COLOR = 34) - Windows 10 에서는 실패하고 그대로
		if (!menuWnd || !g_darkMenu)
			return;
		for (HWND h : g_darkMenu->bordered)
			if (h == menuWnd)
				return;
		g_darkMenu->bordered.push_back(menuWnd);
		typedef HRESULT(WINAPI* SetAttr)(HWND, DWORD, LPCVOID, DWORD);
		static SetAttr fn = []() -> SetAttr
		{
			HMODULE m = ::LoadLibraryW(L"dwmapi.dll");
			return m ? reinterpret_cast<SetAttr>(::GetProcAddress(m, "DwmSetWindowAttribute")) : nullptr;
		}();
		if (fn)
		{
			const COLORREF border = TCOL(TC_EDGE);
			fn(menuWnd, 34, &border, sizeof(border));
		}
	}

	LRESULT CALLBACK DarkMenuOwnerProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
	{
		DarkMenuState* st = g_darkMenu;
		if (!st)
			return ::DefWindowProcW(h, msg, wp, lp);
		if (msg == WM_MEASUREITEM)
		{
			MEASUREITEMSTRUCT* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
			const DarkMenuItem* it = (mis && mis->CtlType == ODT_MENU) ? reinterpret_cast<const DarkMenuItem*>(mis->itemData) : nullptr;
			if (it && it->magic == kDarkMenuMagic)
			{
				DarkMenuMeasure(mis, it);
				return TRUE;
			}
		}
		else if (msg == WM_DRAWITEM)
		{
			const DRAWITEMSTRUCT* d = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
			const DarkMenuItem* it = (d && d->CtlType == ODT_MENU) ? reinterpret_cast<const DarkMenuItem*>(d->itemData) : nullptr;
			if (it && it->magic == kDarkMenuMagic)
			{
				DarkMenuDraw(d, it);
				return TRUE;
			}
		}
		else if (msg == WM_ENTERIDLE && wp == MSGF_MENU)
			DarkMenuBorder(reinterpret_cast<HWND>(lp));   // lParam = 메뉴 창
		return ::CallWindowProcW(st->oldProc, h, msg, wp, lp);
	}
}

namespace
{
	// 메뉴가 마우스 왼쪽 클릭으로 닫힌 자리 · 시각 (DarkMenu::ClosedByClickOn)
	ULONGLONG g_menuClickCloseTick = 0;
	POINT     g_menuClickClosePt = {};

	void NoteMenuClosed()
	{
		// 메뉴 밖을 눌러서 닫으면 TrackPopupMenu 가 돌아올 때 왼쪽 버튼이 아직 눌려 있음
		if (::GetAsyncKeyState(VK_LBUTTON) & 0x8000)
		{
			::GetCursorPos(&g_menuClickClosePt);
			g_menuClickCloseTick = ::GetTickCount64();
		}
		else
			g_menuClickCloseTick = 0;
	}
}

bool DarkMenu::ClosedByClickOn(HWND wnd)
{
	if (g_menuClickCloseTick == 0 || !wnd || !::IsWindow(wnd))
		return false;
	const bool recent = (::GetTickCount64() - g_menuClickCloseTick) < 800;   // 그 클릭의 BN_CLICKED (버튼을 뗄 때) 까지
	RECT rc = {};
	::GetWindowRect(wnd, &rc);
	const bool on = ::PtInRect(&rc, g_menuClickClosePt) != FALSE;
	g_menuClickCloseTick = 0;   // 한 번만
	return recent && on;
}

BOOL DarkMenu::Track(CMenu& menu, UINT flags, int x, int y, CWnd* owner)
{
	if (!owner || !owner->GetSafeHwnd() || !menu.GetSafeHmenu() || g_darkMenu)
	{
		const BOOL r = menu.TrackPopupMenu(flags, x, y, owner);   // 주인 창이 없거나 이미 다른 메뉴가 떠 있으면 기본 메뉴
		NoteMenuClosed();
		return r;
	}

	DarkMenuState st;
	{
		HDC sdc = ::GetDC(nullptr);
		st.dpi = (std::max)(96, ::GetDeviceCaps(sdc, LOGPIXELSY));
		::ReleaseDC(nullptr, sdc);
	}
	NONCLIENTMETRICSW ncm = { sizeof(ncm) };
	::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
	st.font = ::CreateFontIndirectW(&ncm.lfMenuFont);
	LOGFONTW lb = ncm.lfMenuFont;
	lb.lfWeight = FW_BOLD;
	st.bold = ::CreateFontIndirectW(&lb);
	st.back = ::CreateSolidBrush(TCOL(TC_COMBO));
	g_darkMenu = &st;
	DarkMenuConvert(menu.GetSafeHmenu());

	// 메뉴가 떠 있는 동안만 주인 창의 메시지를 가로챔 (TrackPopupMenu 는 메뉴가 닫힐 때까지 돌아오지 않음)
	st.owner = owner->GetSafeHwnd();
	st.oldProc = reinterpret_cast<WNDPROC>(::SetWindowLongPtrW(st.owner, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(DarkMenuOwnerProc)));
	// TPM_NONOTIFY 는 빼고 (그리기 메시지를 받기 위해) - TPM_RETURNCMD 면 WM_COMMAND 는 원래대로 보내지 않음
	const BOOL result = menu.TrackPopupMenu(flags & ~TPM_NONOTIFY, x, y, owner);
	NoteMenuClosed();
	if (st.oldProc)
		::SetWindowLongPtrW(st.owner, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(st.oldProc));
	g_darkMenu = nullptr;
	::DeleteObject(st.font);
	::DeleteObject(st.bold);
	::DeleteObject(st.back);
	return result;
}
