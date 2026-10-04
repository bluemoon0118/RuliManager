#include "pch.h"
#include "ActorDetailPanel.h"
#include "CountryCombo.h"
#include "TextDraw.h"
#include <cmath>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CActorDetailPanel, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_MESSAGE(WM_GETFONT, &CActorDetailPanel::OnGetFont)
END_MESSAGE_MAP()

LRESULT CActorDetailPanel::OnGetFont(WPARAM, LPARAM)
{
	CFont* f = BaseFont();
	return reinterpret_cast<LRESULT>(f ? f->GetSafeHandle() : nullptr);
}

namespace
{
	const UINT kStarId  = 1;
	const UINT kHeartId = 2;
}

bool CActorDetailPanel::Create(CWnd* parent, UINT id)
{
	const CString cls = AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_ARROW), nullptr);
	if (!CWnd::Create(cls, L"", WS_CHILD | WS_CLIPCHILDREN, CRect(0, 0, 100, 100), parent, id))
		return false;
	m_star.Create(this, kStarId, 5);
	m_star.ShowWindow(SW_HIDE);
	m_star.m_onChanged = [this](int r) { if (m_onRating) m_onRating(r); };
	m_heart.Create(this, kHeartId);
	m_heart.m_onClick = [this]() { if (m_onFavorite) m_onFavorite(); };
	SetColors(m_back, m_text, m_label);
	return true;
}

void CActorDetailPanel::SetColors(COLORREF back, COLORREF text, COLORREF label)
{
	m_back = back; m_text = text; m_label = label;
	if (m_star.GetSafeHwnd())
		m_star.SetColors(back, RGB(0xFF, 0xC8, 0x1E), RGB(0xE6, 0xEA, 0xEE), text);
	if (m_heart.GetSafeHwnd())
		m_heart.SetColors(back, RGB(0xF2, 0x5C, 0x54), RGB(0x95, 0x98, 0x9D), RGB(0xC8, 0xCC, 0xD0));   // 카드 하트와 같은 색
	if (GetSafeHwnd()) Invalidate(FALSE);
}

CFont* CActorDetailPanel::BaseFont() const
{
	return GetParent() ? GetParent()->GetFont() : nullptr;
}

void CActorDetailPanel::EnsureFonts()
{
	if (m_nameFont.GetSafeHandle())
		return;
	LOGFONT lf = {};
	if (CFont* f = BaseFont())
		f->GetLogFont(&lf);
	lf.lfHeight = lf.lfHeight * 19 / 10;   // 큰 이름
	lf.lfWeight = FW_BOLD;
	lf.lfQuality = CLEARTYPE_QUALITY;
	m_nameFont.CreateFontIndirect(&lf);
	LOGFONT hl = {};
	if (CFont* f = BaseFont())
		f->GetLogFont(&hl);
	hl.lfHeight = hl.lfHeight * 12 / 10;   // 이력: 조금 크게, 굵게
	hl.lfWeight = FW_BOLD;
	hl.lfQuality = CLEARTYPE_QUALITY;
	m_histFont.CreateFontIndirect(&hl);
}

namespace
{
	const wchar_t* kArrow = L"  \x2192  ";   // →
	const COLORREF kHistColor  = RGB(0xFF, 0x6F, 0x61);   // 이력 이름 (산호색)
	const COLORREF kArrowColor = RGB(0xC0, 0x7A, 0x72);   // 화살표

	// "나기 히카루(Hikaru Nagi, 凪ひかる)" → "나기 히카루"
	CString StripParen(const CString& name)
	{
		int p = name.Find(L'(');
		const int pw = name.Find(L'\xFF08');
		if (p < 0 || (pw >= 0 && pw < p)) p = pw;
		if (p <= 0)
			return name;
		CString left = name.Left(p);
		left.Trim();
		return left.IsEmpty() ? name : left;
	}
}

int CActorDetailPanel::LayoutHistory(CDC& dc, int width, std::vector<CPoint>* pos)
{
	if (m_history.size() < 2)
		return 0;
	EnsureFonts();
	CFont* old = dc.SelectObject(&m_histFont);
	const int arrowW = dc.GetTextExtent(kArrow).cx;
	int x = 0, line = 0;
	for (size_t i = 0; i < m_history.size(); ++i)
	{
		const int unitW = (i > 0 ? arrowW : 0) + TextFB::Width(&dc, m_history[i]);
		if (i > 0 && x > 0 && x + unitW > width)
		{
			++line;   // 줄바꿈: 다음 줄은 "→ 이름" 으로 시작
			x = 0;
		}
		if (pos)
			pos->push_back(CPoint(x, line));
		x += unitW;
	}
	dc.SelectObject(old);
	return line + 1;
}

int CActorDetailPanel::NameHeight()
{
	EnsureFonts();
	CClientDC dc(this);
	CFont* old = dc.SelectObject(&m_nameFont);
	TEXTMETRIC tm = {};
	dc.GetTextMetrics(&tm);
	dc.SelectObject(old);
	return tm.tmHeight + 2;
}

int CActorDetailPanel::RowHeight()
{
	CClientDC dc(this);
	CFont* old = dc.SelectObject(BaseFont());
	TEXTMETRIC tm = {};
	dc.GetTextMetrics(&tm);
	dc.SelectObject(old);
	return tm.tmHeight * 17 / 10;   // 줄 간격 넉넉하게
}

int CActorDetailPanel::LabelWidth()
{
	CClientDC dc(this);
	CFont* old = dc.SelectObject(BaseFont());
	const int w = dc.GetTextExtent(L"생년월일:").cx;
	dc.SelectObject(old);
	return w + 16;
}

void CActorDetailPanel::SetActor(const ActorInfo* a, int videoCount)
{
	m_has = (a != nullptr);
	m_rows.clear();
	m_history.clear();
	m_name.Empty();
	m_memo.Empty();
	if (a)
	{
		m_name = a->name;
		if (!a->gender.IsEmpty())
			m_rows.push_back({ L"성별:", a->gender });
		{
			const int age = CVideoLibrary::CalcAge(a->birth);
			CString v;
			if (age >= 0)
				v.Format(L"%d (%s)", age, static_cast<LPCWSTR>(a->birth));
			else
				v = a->birth;
			if (!v.IsEmpty())
				m_rows.push_back({ L"나이:", v });
		}
		if (!a->nationality.IsEmpty())
			m_rows.push_back({ L"국적:", a->nationality, CCountryCombo::FindCountry(a->nationality) });
		if (!a->height.IsEmpty())
			m_rows.push_back({ L"키:", a->height + L"cm" });
		if (!a->bust.IsEmpty() || !a->waist.IsEmpty() || !a->hip.IsEmpty())
		{
			auto part = [](LPCWSTR tag, const CString& v) { return CString(tag) + (v.IsEmpty() ? CString(L"-") : v); };
			m_rows.push_back({ L"치수:", part(L"B", a->bust) + L" " + part(L"W", a->waist) + L" " + part(L"H", a->hip) });
		}
		if (!a->cup.IsEmpty())
			m_rows.push_back({ L"컵:", a->cup + L"컵" });
		if (!a->debut.IsEmpty())
			m_rows.push_back({ L"데뷔:", a->debut });
		if (!a->retire.IsEmpty())
			m_rows.push_back({ L"은퇴:", a->retire });
		CString cnt;
		cnt.Format(L"%d편", videoCount);
		m_rows.push_back({ L"출연:", cnt });

		// 별칭 변경 이력: 배우 관리 창 이름 콤보 순서(대표 이름, 별칭1, 별칭2 …)의 역순
		//   → … 별칭2 → 별칭1 → 대표 이름
		{
			const std::vector<CString> als = CVideoLibrary::SplitList(a->aliases);
			for (auto it = als.rbegin(); it != als.rend(); ++it)
				m_history.push_back(StripParen(*it));
			if (!m_history.empty())
				m_history.push_back(StripParen(a->name));
			// 괄호를 뺀 이름이 같으면 하나로 (대표 이름 쪽을 남김)
			std::vector<CString> uniq;
			for (auto it = m_history.rbegin(); it != m_history.rend(); ++it)
			{
				bool dup = false;
				for (const CString& u : uniq)
					if (u.CompareNoCase(*it) == 0) { dup = true; break; }
				if (!dup)
					uniq.insert(uniq.begin(), *it);
			}
			m_history = uniq;
			if (m_history.size() < 2)   // 서로 다른 이름이 2개 이상일 때만 이력 표시
				m_history.clear();
		}

		m_memo = a->memo;
		m_memo.Trim();
		m_memo.Replace(L"\r\n", L"\n");
		m_memo.Replace(L"\n", L"\r\n");

		m_star.SetRating(a->rating);
		m_heart.SetOn(a->favorite);
	}
	m_star.ShowWindow(m_has ? SW_SHOW : SW_HIDE);
	m_heart.ShowWindow(m_has ? SW_SHOW : SW_HIDE);
	LayoutChildren();
	Invalidate(FALSE);
}

int CActorDetailPanel::CalcHeight(int width)
{
	if (!GetSafeHwnd())
		return 0;
	const int nameH = NameHeight();
	const int rowH = RowHeight();
	const int rows = m_has ? static_cast<int>(m_rows.size()) : 0;
	int h = nameH + 4 + rowH + 6 + rows * rowH + 4;   // 이름 + 별 + 줄들
	if (m_has && m_history.size() >= 2)
	{
		if (width <= 0)
		{
			CRect rc;
			GetClientRect(&rc);
			width = rc.Width();
		}
		CClientDC dc(this);
		const int lines = LayoutHistory(dc, (std::max)(50, width), nullptr);
		h += 8 + lines * HistLineHeight();   // 별칭 변경 이력
	}
	if (m_has && !m_memo.IsEmpty())
	{
		if (width <= 0)
		{
			CRect rc;
			GetClientRect(&rc);
			width = rc.Width();
		}
		CClientDC dc(this);
		h += 8 + MemoHeight(dc, (std::max)(50, width));   // 메모
	}
	return h;
}

int CActorDetailPanel::MemoHeight(CDC& dc, int width)
{
	CFont* old = dc.SelectObject(BaseFont());
	TEXTMETRIC tm = {};
	dc.GetTextMetrics(&tm);
	CRect r(0, 0, width, 0);
	dc.DrawText(m_memo, r, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX | DT_CALCRECT);
	dc.SelectObject(old);
	const int maxH = tm.tmHeight * 8;   // 너무 길면 8줄까지만 (나머지는 … 로)
	return (std::min)(static_cast<int>(r.Height()), maxH);
}

int CActorDetailPanel::HistLineHeight()
{
	EnsureFonts();
	CClientDC dc(this);
	CFont* old = dc.SelectObject(&m_histFont);
	TEXTMETRIC tm = {};
	dc.GetTextMetrics(&tm);
	dc.SelectObject(old);
	return tm.tmHeight + 4;
}

void CActorDetailPanel::LayoutChildren()
{
	if (!GetSafeHwnd() || !m_star.GetSafeHwnd())
		return;
	CRect rc;
	GetClientRect(&rc);
	const int nameH = NameHeight();
	const int rowH = RowHeight();

	// 하트: 이름 글자 바로 오른쪽 (이름이 길면 오른쪽 끝)
	const int heartSize = (std::max)(14, nameH * 6 / 10);
	int nameW = 0;
	{
		CClientDC dc(this);
		CFont* old = dc.SelectObject(&m_nameFont);
		nameW = TextFB::Width(&dc, m_name);
		dc.SelectObject(old);
	}
	const int maxNameW = (std::max)(10, static_cast<int>(rc.Width()) - heartSize - 12);
	nameW = (std::min)(nameW, maxNameW);
	m_heart.MoveWindow(nameW + 10, (nameH - heartSize) / 2 + 1, heartSize, heartSize);

	// 별: 이름 아래
	const int starH = (std::max)(14, rowH - 4);
	m_star.MoveWindow(0, nameH + 4, starH * 7, starH);
}

void CActorDetailPanel::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	LayoutChildren();
	Invalidate(FALSE);
}

void CActorDetailPanel::OnPaint()
{
	CPaintDC paint(this);
	CRect rc;
	GetClientRect(&rc);
	CDC mem;
	mem.CreateCompatibleDC(&paint);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&paint, (std::max)(1, rc.Width()), (std::max)(1, rc.Height()));
	CBitmap* old = mem.SelectObject(&bmp);
	mem.FillSolidRect(rc, m_back);
	mem.SetBkMode(TRANSPARENT);
	EnsureFonts();

	const int nameH = NameHeight();
	const int rowH = RowHeight();
	CFont* oldFont = mem.SelectObject(BaseFont());

	if (!m_has)
	{
		mem.SetTextColor(m_label);
		CRect tr(0, 0, rc.right, rowH);
		mem.DrawText(L"(선택된 배우 없음)", tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}
	else
	{
		// 이름 (크게, 굵게, 글꼴 대체)
		const int heartSize = (std::max)(14, nameH * 6 / 10);
		mem.SelectObject(&m_nameFont);
		mem.SetTextColor(RGB(255, 255, 255));
		TextFB::Draw(&mem, m_name, CRect(0, 0, (std::max)(10, static_cast<int>(rc.right) - heartSize - 12), nameH), DT_LEFT, true);

		// 정보 줄: 라벨(회색) + 값(흰색)
		mem.SelectObject(BaseFont());
		const int labelW = LabelWidth();
		int y = nameH + 4 + rowH + 6;
		for (const Row& r : m_rows)
		{
			mem.SetTextColor(m_label);
			mem.DrawText(r.label, CRect(0, y, labelW, y + rowH), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			mem.SetTextColor(m_text);
			CRect vr(labelW, y, rc.right, y + rowH);
			if (r.flag >= 0)
			{
				const int tw = mem.GetTextExtent(r.value).cx;
				mem.DrawText(r.value, vr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
				const int fh = (std::min)(CCountryCombo::kFlagH, rowH - 6);
				CCountryCombo::DrawFlag(&mem, r.flag, labelW + tw + 6, y + (rowH - fh) / 2, fh);
			}
			else
			{
				TextFB::Draw(&mem, r.value, vr, DT_LEFT, true);
			}
			y += rowH;
		}

		// 별칭 변경 이력: 아스카 아카 → 시오세 → 나기 히카루
		std::vector<CPoint> pos;
		const int lines = LayoutHistory(mem, (std::max)(50, static_cast<int>(rc.Width())), &pos);
		if (lines > 0)
		{
			const int histH = HistLineHeight();
			const int top = y + 8;
			mem.SelectObject(&m_histFont);
			const int arrowW = mem.GetTextExtent(kArrow).cx;
			for (size_t i = 0; i < m_history.size() && i < pos.size(); ++i)
			{
				int x = pos[i].x;
				const int ly = top + pos[i].y * histH;
				if (i > 0)
				{
					mem.SetTextColor(kArrowColor);
					mem.DrawText(kArrow, -1, CRect(x, ly, x + arrowW, ly + histH), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
					x += arrowW;
				}
				mem.SetTextColor(kHistColor);
				TextFB::Draw(&mem, m_history[i], CRect(x, ly, rc.right, ly + histH), DT_LEFT, true);
			}
			mem.SelectObject(BaseFont());
			y = top + lines * histH;
		}

		// 메모 (여러 줄, 넘치면 마지막 줄 …)
		if (!m_memo.IsEmpty())
		{
			const int mh = MemoHeight(mem, (std::max)(50, static_cast<int>(rc.Width())));
			mem.SelectObject(BaseFont());
			mem.SetTextColor(RGB(0xCE, 0xD9, 0xE0));
			CRect mr(0, y + 8, rc.right, y + 8 + mh);
			mem.DrawText(m_memo, mr, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS | DT_NOPREFIX);
		}
	}

	mem.SelectObject(oldFont);
	paint.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(old);
}
