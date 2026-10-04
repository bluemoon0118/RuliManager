#include "pch.h"
#include "resource.h"
#include "CountryCombo.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	// 순서 = res\flags.bmp 의 국기 순서 (자주 쓰는 나라 먼저, 나머지는 가나다순)
	const CountryInfo kCountries[] =
	{
	{ L"KR", L"대한민국" },
	{ L"JP", L"일본" },
	{ L"CN", L"중국" },
	{ L"TW", L"대만" },
	{ L"HK", L"홍콩" },
	{ L"US", L"미국" },
	{ L"CA", L"캐나다" },
	{ L"GB", L"영국" },
	{ L"FR", L"프랑스" },
	{ L"DE", L"독일" },
	{ L"RU", L"러시아" },
	{ L"TH", L"태국" },
	{ L"VN", L"베트남" },
	{ L"PH", L"필리핀" },
	{ L"ID", L"인도네시아" },
	{ L"MY", L"말레이시아" },
	{ L"SG", L"싱가포르" },
	{ L"MN", L"몽골" },
	{ L"IN", L"인도" },
	{ L"AU", L"호주" },
	{ L"BR", L"브라질" },
	{ L"GR", L"그리스" },
	{ L"NG", L"나이지리아" },
	{ L"ZA", L"남아프리카 공화국" },
	{ L"NL", L"네덜란드" },
	{ L"NP", L"네팔" },
	{ L"NO", L"노르웨이" },
	{ L"NZ", L"뉴질랜드" },
	{ L"DK", L"덴마크" },
	{ L"LA", L"라오스" },
	{ L"RO", L"루마니아" },
	{ L"MX", L"멕시코" },
	{ L"MM", L"미얀마" },
	{ L"VE", L"베네수엘라" },
	{ L"BE", L"벨기에" },
	{ L"SA", L"사우디아라비아" },
	{ L"SE", L"스웨덴" },
	{ L"CH", L"스위스" },
	{ L"ES", L"스페인" },
	{ L"AE", L"아랍에미리트" },
	{ L"AR", L"아르헨티나" },
	{ L"IS", L"아이슬란드" },
	{ L"IE", L"아일랜드" },
	{ L"AT", L"오스트리아" },
	{ L"UZ", L"우즈베키스탄" },
	{ L"UA", L"우크라이나" },
	{ L"IR", L"이란" },
	{ L"IL", L"이스라엘" },
	{ L"EG", L"이집트" },
	{ L"IT", L"이탈리아" },
	{ L"CZ", L"체코" },
	{ L"CL", L"칠레" },
	{ L"KZ", L"카자흐스탄" },
	{ L"KH", L"캄보디아" },
	{ L"CO", L"콜롬비아" },
	{ L"CU", L"쿠바" },
	{ L"TR", L"튀르키예" },
	{ L"PK", L"파키스탄" },
	{ L"PE", L"페루" },
	{ L"PT", L"포르투갈" },
	{ L"PL", L"폴란드" },
	{ L"FI", L"핀란드" },
	{ L"HU", L"헝가리" },
	};
}

const CountryInfo* CCountryCombo::Countries(int& count)
{
	count = static_cast<int>(_countof(kCountries));
	return kCountries;
}

int CCountryCombo::FindCountry(const CString& name)
{
	for (int i = 0; i < static_cast<int>(_countof(kCountries)); ++i)
	{
		if (name.CompareNoCase(kCountries[i].name) == 0 || name.CompareNoCase(kCountries[i].code) == 0)
			return i;
	}
	return -1;
}

CImageList& CCountryCombo::Flags()
{
	static CImageList list;
	if (!list.GetSafeHandle())
	{
		list.Create(kFlagW, kFlagH, ILC_COLOR24, 0, 8);
		CBitmap bmp;
		if (bmp.LoadBitmap(IDB_FLAGS))
			list.Add(&bmp, static_cast<CBitmap*>(nullptr));
	}
	return list;
}

void CCountryCombo::DrawFlag(CDC* pDC, int idx, int x, int y, int height)
{
	CImageList& flags = Flags();
	if (idx < 0 || idx >= flags.GetImageCount())
		return;
	const int h = (std::max)(4, (std::min)(height, static_cast<int>(kFlagH)));
	const int w = h * kFlagW / kFlagH;
	if (h == kFlagH)
	{
		flags.Draw(pDC, idx, CPoint(x, y), ILD_NORMAL);
	}
	else
	{
		HICON icon = flags.ExtractIcon(idx);
		if (icon)
		{
			::DrawIconEx(pDC->GetSafeHdc(), x, y, icon, w, h, 0, nullptr, DI_NORMAL);
			::DestroyIcon(icon);
		}
	}
	CBrush border(RGB(160, 160, 160));
	pDC->FrameRect(CRect(x - 1, y - 1, x + w + 1, y + h + 1), &border);   // 흰 국기도 구분되게 테두리
}

void CCountryCombo::FillCountries()
{
	// 대화상자 생성 시점의 WM_MEASUREITEM 은 이 클래스로 오지 않으므로 높이를 직접 지정
	MEASUREITEMSTRUCT mis = {};
	MeasureItem(&mis);
	SetItemHeight(-1, mis.itemHeight);   // 선택된 값 표시 부분
	SetItemHeight(0, mis.itemHeight);    // 목록 항목

	ResetContent();
	const int blank = AddString(L"");
	SetItemData(blank, static_cast<DWORD_PTR>(-1));
	for (int i = 0; i < static_cast<int>(_countof(kCountries)); ++i)
	{
		const int row = AddString(kCountries[i].name);
		SetItemData(row, static_cast<DWORD_PTR>(i));
	}
	SetCurSel(0);
}

void CCountryCombo::SetCountry(const CString& name)
{
	CString n = name;
	n.Trim();
	if (n.IsEmpty())
	{
		SetCurSel(0);
		return;
	}
	const int idx = FindCountry(n);
	for (int row = 0; row < GetCount(); ++row)
	{
		const int data = static_cast<int>(GetItemData(row));
		CString text;
		GetLBText(row, text);
		if ((idx >= 0 && data == idx) || (idx < 0 && data < 0 && text.CompareNoCase(n) == 0))
		{
			SetCurSel(row);
			return;
		}
	}
	// 목록에 없는 국적(예전에 직접 입력한 값)은 국기 없이 맨 끝에 추가
	const int row = AddString(n);
	SetItemData(row, static_cast<DWORD_PTR>(-1));
	SetCurSel(row);
}

CString CCountryCombo::GetCountry() const
{
	CString text;
	const int row = GetCurSel();
	if (row >= 0)
		GetLBText(row, text);
	return text;
}

void CCountryCombo::MeasureItem(LPMEASUREITEMSTRUCT lpMIS)
{
	int h = kFlagH + 4;
	if (GetSafeHwnd())
	{
		CClientDC dc(this);
		CFont* old = dc.SelectObject(GetFont());
		TEXTMETRIC tm = {};
		dc.GetTextMetrics(&tm);
		dc.SelectObject(old);
		h = (std::max)(h, static_cast<int>(tm.tmHeight) + 4);
	}
	lpMIS->itemHeight = h;
}

void CCountryCombo::DrawItem(LPDRAWITEMSTRUCT lpDIS)
{
	CDC dc;
	dc.Attach(lpDIS->hDC);
	CRect rc(lpDIS->rcItem);
	const bool selected = (lpDIS->itemState & ODS_SELECTED) != 0;
	const bool disabled = (lpDIS->itemState & ODS_DISABLED) != 0;
	const bool isField = (lpDIS->itemState & ODS_COMBOBOXEDIT) != 0;
	dc.FillSolidRect(rc, (selected && !isField) ? m_selBg : m_bg);   // 메인 창 콤보와 같은 색

	if (lpDIS->itemID != static_cast<UINT>(-1))
	{
		const int flag = static_cast<int>(lpDIS->itemData);
		CString text;
		GetLBText(static_cast<int>(lpDIS->itemID), text);

		const int fx = rc.left + 3;
		const int fy = rc.top + (rc.Height() - kFlagH) / 2;
		DrawFlag(&dc, flag, fx, fy);
		CRect tr = rc;
		tr.left = fx + kFlagW + 6;
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(disabled ? RGB(120, 135, 148) : m_text);
		dc.DrawText(text.IsEmpty() ? CString(L"(미지정)") : text, tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
	}
	if ((lpDIS->itemState & ODS_FOCUS) && !(lpDIS->itemState & ODS_NOFOCUSRECT))
		dc.DrawFocusRect(rc);
	dc.Detach();
}
