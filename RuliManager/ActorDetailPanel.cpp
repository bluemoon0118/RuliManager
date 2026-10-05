#include "pch.h"
#include "ActorDetailPanel.h"
#include "CountryCombo.h"
#include "TextDraw.h"
#include "VectorIcons.h"
#include "WebHelper.h"
#include <memory>
#include <thread>

namespace
{
	const UINT WM_APP_FAVICON = WM_APP + 0x351;   // 백그라운드에서 사이트 아이콘을 받음 → 다시 그리기

	void EnsureGdiPlusPanel()
	{
		static ULONG_PTR token = 0;
		if (!token)
		{
			Gdiplus::GdiplusStartupInput input;
			Gdiplus::GdiplusStartup(&token, &input, nullptr);
		}
	}

	// URL → 도메인 (소문자, www. · 포트 · 경로 제거)  예: https://www.instagram.com/abc → instagram.com
	CString DomainOf(const CString& url)
	{
		CString d = url;
		const int sch = d.Find(L"://");
		if (sch >= 0)
			d = d.Mid(sch + 3);
		const int slash = d.FindOneOf(L"/?#");
		if (slash >= 0)
			d = d.Left(slash);
		const int at = d.ReverseFind(L'@');
		if (at >= 0)
			d = d.Mid(at + 1);
		const int colon = d.Find(L':');
		if (colon >= 0)
			d = d.Left(colon);
		d.MakeLower();
		d.Trim();
		if (d.Left(4) == L"www.")
			d = d.Mid(4);
		return d;
	}

	CString FaviconFile(const CString& domain)
	{
		return CVideoLibrary::GetImageRoot() + L"\\favicons\\" + domain + L".png";
	}
}
#include <cmath>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CActorDetailPanel, CWnd)
	ON_WM_PAINT()
	ON_WM_LBUTTONDBLCLK()
	ON_MESSAGE(WM_APP_FAVICON, &CActorDetailPanel::OnFaviconReady)
	ON_WM_SETCURSOR()
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
	const CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), nullptr);   // 데뷔작 상자 더블클릭
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

CFont* CActorDetailPanel::InfoFont()
{
	EnsureFonts();
	return m_infoFont.GetSafeHandle() ? &m_infoFont : BaseFont();
}

void CActorDetailPanel::EnsureFonts()
{
	if (m_nameFont.GetSafeHandle())
		return;
	{
		// 정보 줄 · 이력 · 메모 글꼴: 기본 글꼴보다 2pt 크게
		LOGFONT il = {};
		if (CFont* f = BaseFont())
			f->GetLogFont(&il);
		CClientDC sdc(nullptr);
		const int dpi = (std::max)(1, sdc.GetDeviceCaps(LOGPIXELSY));
		const double pt = std::abs(il.lfHeight) * 72.0 / dpi;
		il.lfHeight = -static_cast<LONG>(std::lround((pt + 2.0) * dpi / 72.0));
		il.lfQuality = CLEARTYPE_QUALITY;
		m_infoFont.CreateFontIndirect(&il);
	}
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
	if (m_infoFont.GetSafeHandle())
		m_infoFont.GetLogFont(&hl);          // 이력도 정보 줄 글꼴(+2pt) 기준
	hl.lfHeight = hl.lfHeight * 12 / 10;   // 이력: 조금 크게, 굵게
	hl.lfWeight = FW_BOLD;
	hl.lfQuality = CLEARTYPE_QUALITY;
	m_histFont.CreateFontIndirect(&hl);
	LOGFONT sl = {};
	if (CFont* f = BaseFont())
		f->GetLogFont(&sl);
	sl.lfHeight = sl.lfHeight * 16 / 10;   // 성별 기호: 1.6배, 굵게 (잘 보이게)
	sl.lfWeight = FW_BOLD;
	sl.lfQuality = CLEARTYPE_QUALITY;
	m_symFont.CreateFontIndirect(&sl);
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

int CActorDetailPanel::StarRowHeight()
{
	CClientDC dc(this);
	CFont* old = dc.SelectObject(InfoFont());
	TEXTMETRIC tm = {};
	dc.GetTextMetrics(&tm);
	dc.SelectObject(old);
	return tm.tmHeight * 17 / 10;
}

int CActorDetailPanel::RowHeight()
{
	CClientDC dc(this);
	CFont* old = dc.SelectObject(InfoFont());
	TEXTMETRIC tm = {};
	dc.GetTextMetrics(&tm);
	const int dpi = (std::max)(1, dc.GetDeviceCaps(LOGPIXELSY));
	dc.SelectObject(old);
	const int less = static_cast<int>(std::lround(4.0 * dpi / 72.0));   // 줄 위아래 여백 4pt 줄임 (2pt + 2pt)
	return (std::max)(static_cast<int>(tm.tmHeight) + 1, static_cast<int>(tm.tmHeight * 17 / 10) - less);
}

int CActorDetailPanel::LabelWidth()
{
	CClientDC dc(this);
	CFont* old = dc.SelectObject(InfoFont());
	const int w = dc.GetTextExtent(L"생년월일:").cx;
	dc.SelectObject(old);
	return w + 16;
}

void CActorDetailPanel::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	// 링크 아이콘 더블클릭 → 기본 브라우저로 열기 (주소에 :// 가 없으면 https:// 를 붙임)
	if (m_has)
	{
		for (const auto& lr : m_linkRects)
		{
			if (lr.first.PtInRect(point))
			{
				CString url = lr.second;
				if (url.Find(L"://") < 0)
					url = L"https://" + url;
				::ShellExecuteW(GetSafeHwnd(), L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
				return;
			}
		}
	}
	// 데뷔작 품번 상자를 더블클릭하면 그 영상으로 이동
	if (m_has && !m_badgeRect.IsRectEmpty() && m_badgeRect.PtInRect(point) && m_onDebutDblClick)
	{
		m_onDebutDblClick();
		return;
	}
	CWnd::OnLButtonDblClk(nFlags, point);
}

Gdiplus::Bitmap* CActorDetailPanel::Favicon(const CString& domain)
{
	if (domain.IsEmpty())
		return nullptr;
	auto it = m_favicons.find(domain);
	if (it != m_favicons.end() && it->second)
		return it->second.get();
	// 받아 둔 파일이 있으면 읽음
	const CString file = FaviconFile(domain);
	if (::PathFileExistsW(file))
	{
		EnsureGdiPlusPanel();
		std::unique_ptr<Gdiplus::Bitmap> tmp(Gdiplus::Bitmap::FromFile(file));
		if (tmp && tmp->GetLastStatus() == Gdiplus::Ok && tmp->GetWidth() > 0)
		{
			// 파일을 잠그지 않도록 메모리 사본으로 (Clone - DEBUG_NEW 와 GDI+ operator new 충돌을 피함)
			std::unique_ptr<Gdiplus::Bitmap> copy(tmp->Clone(0, 0, static_cast<INT>(tmp->GetWidth()), static_cast<INT>(tmp->GetHeight()), PixelFormat32bppARGB));
			if (!copy || copy->GetLastStatus() != Gdiplus::Ok)
				return nullptr;
			Gdiplus::Bitmap* p = copy.get();
			m_favicons[domain] = std::move(copy);
			return p;
		}
	}
	// 없으면 백그라운드로 받기 (Google 파비콘 서비스, 64px PNG) → 받으면 다시 그림
	if (m_faviconRequested.insert(domain).second)
	{
		const HWND hwnd = GetSafeHwnd();
		const CString url = L"https://www.google.com/s2/favicons?sz=64&domain=" + Web::UrlEncode(domain);
		std::thread([hwnd, url, file]()
		{
			std::vector<BYTE> data;
			if (!Web::HttpGet(url, data, nullptr, nullptr, 512 * 1024) || data.size() < 16)
				return;
			const CString dir = file.Left(file.ReverseFind(L'\\'));
			::SHCreateDirectoryExW(nullptr, dir, nullptr);
			HANDLE h = ::CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (h == INVALID_HANDLE_VALUE)
				return;
			DWORD written = 0;
			const BOOL ok = ::WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
			::CloseHandle(h);
			if (ok && ::IsWindow(hwnd))
				::PostMessageW(hwnd, WM_APP_FAVICON, 0, 0);
		}).detach();
	}
	return nullptr;
}

void CActorDetailPanel::DrawLinkIcon(CDC& dc, const CString& url, const CRect& rc)
{
	const CString domain = DomainOf(url);
	if (Gdiplus::Bitmap* icon = Favicon(domain))
	{
		Gdiplus::Graphics g(dc.GetSafeHdc());
		g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
		g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
		g.DrawImage(icon, Gdiplus::Rect(rc.left, rc.top, rc.Width(), rc.Height()), 0, 0,
			static_cast<INT>(icon->GetWidth()), static_cast<INT>(icon->GetHeight()), Gdiplus::UnitPixel);
		return;
	}
	// 아이콘이 아직 없으면: 둥근 사각형 + 도메인 첫 글자
	CBrush br(RGB(0x39, 0x4B, 0x59));
	CPen pen(PS_SOLID, 1, RGB(0x5C, 0x70, 0x80));
	CBrush* ob = dc.SelectObject(&br);
	CPen* op = dc.SelectObject(&pen);
	dc.RoundRect(rc, CPoint(6, 6));
	dc.SelectObject(ob);
	dc.SelectObject(op);
	CString letter = domain.IsEmpty() ? CString(L"?") : domain.Left(1);
	letter.MakeUpper();
	dc.SetTextColor(RGB(0xE6, 0xEA, 0xEE));
	TextFB::Draw(&dc, letter, rc, DT_CENTER, false);
}

LRESULT CActorDetailPanel::OnFaviconReady(WPARAM, LPARAM)
{
	Invalidate(FALSE);   // 받은 아이콘으로 다시 그림
	return 0;
}

BOOL CActorDetailPanel::PreTranslateMessage(MSG* pMsg)
{
	if (m_tip.GetSafeHwnd())
		m_tip.RelayEvent(pMsg);
	return CWnd::PreTranslateMessage(pMsg);
}

void CActorDetailPanel::UpdateLinkTips()
{
	// 링크 아이콘마다 주소 풍선 도움말 (아이콘 위치가 바뀌었을 때만 다시 만듦)
	if (m_tipRects == m_linkRects && m_tip.GetSafeHwnd())
		return;
	m_tipRects = m_linkRects;
	if (m_tip.GetSafeHwnd())
		m_tip.DestroyWindow();
	if (m_linkRects.empty() || !m_tip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX))
		return;
	m_tip.SetMaxTipWidth(600);
	for (size_t i = 0; i < m_linkRects.size(); ++i)
		m_tip.AddTool(this, m_linkRects[i].second + L"  (더블클릭하면 열기)", m_linkRects[i].first, static_cast<UINT_PTR>(i + 1));
	m_tip.Activate(TRUE);
}

BOOL CActorDetailPanel::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	// 데뷔작 상자 위에서는 손 모양 커서 (더블클릭으로 이동할 수 있음을 표시)
	CPoint pt;
	::GetCursorPos(&pt);
	ScreenToClient(&pt);
	bool overLink = false;
	for (const auto& lr : m_linkRects)
		if (lr.first.PtInRect(pt)) { overLink = true; break; }
	if (nHitTest == HTCLIENT && m_has && (overLink || (!m_badgeRect.IsRectEmpty() && m_badgeRect.PtInRect(pt))))
	{
		::SetCursor(::LoadCursor(nullptr, IDC_HAND));
		return TRUE;
	}
	return CWnd::OnSetCursor(pWnd, nHitTest, message);
}

void CActorDetailPanel::SetActor(const ActorInfo* a, int videoCount, const CString& debutCode)
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
				v.Format(L"%s (%d세)", static_cast<LPCWSTR>(a->birth), age);   // 출생: 1995-03-12 (31세)
			else
				v = a->birth;
			if (!v.IsEmpty())
				m_rows.push_back({ L"출생:", v });
		}
		if (!a->nationality.IsEmpty())
			m_rows.push_back({ L"국적:", a->nationality, CCountryCombo::FindCountry(a->nationality) });
		// 신체: "162cm B88 W58 H86 (E컵)" (키 + 치수 + 컵을 한 줄로, 키 · 컵 줄은 따로 없음)
		//   치수가 없으면 컵은 괄호 없이: "162cm E컵" / "E컵"
		if (!a->height.IsEmpty() || !a->bust.IsEmpty() || !a->waist.IsEmpty() || !a->hip.IsEmpty() || !a->cup.IsEmpty())
		{
			auto part = [](LPCWSTR tag, const CString& v) { return CString(tag) + (v.IsEmpty() ? CString(L"-") : v); };
			auto join = [](CString& acc, const CString& piece) { if (!acc.IsEmpty()) acc += L" "; acc += piece; };
			CString body;
			if (!a->height.IsEmpty())
				join(body, a->height + L"cm");
			const bool sizes = !a->bust.IsEmpty() || !a->waist.IsEmpty() || !a->hip.IsEmpty();
			if (sizes)
				join(body, part(L"B", a->bust) + L" " + part(L"W", a->waist) + L" " + part(L"H", a->hip));
			if (!a->cup.IsEmpty())
				join(body, sizes ? (L"(" + a->cup + L"컵)") : (a->cup + L"컵"));
			m_rows.push_back({ L"신체:", body });
		}
		if (!a->debut.IsEmpty())
		{
			Row dr{ L"데뷔:", a->debut };
			dr.badge = debutCode;   // 데뷔일과 같은 날 발매된 출연작 품번 → 날짜 오른쪽에 상자로
			m_rows.push_back(dr);
		}
		if (!a->retire.IsEmpty())
			m_rows.push_back({ L"은퇴:", a->retire });
		{
			// 활동기간: 년도만 "2015년 ~ 현재" (은퇴일이 없으면 현재, 데뷔일이 없으면 표시 안 함)
			auto yearOf = [](const CString& d) -> CString
			{
				CString y = d.Left(4);
				return (y.GetLength() == 4 && y.SpanIncluding(L"0123456789") == y) ? y : CString();
			};
			const CString from = yearOf(a->debut);
			if (!from.IsEmpty())
			{
				const CString to = yearOf(a->retire);
				CString period = from + L"년 ~ " + (to.IsEmpty() ? CString(L"현재") : to + L"년");   // 2015년 ~ 현재 / 2015년 ~ 2023년
				// 데뷔일(년-월-일)로부터 지난 날 수 · 주년: 활동 중이면 오늘까지, 은퇴했으면 은퇴일까지
				//   예: "2015년 ~ 현재 (+3,912일, 10주년)"
				auto parseYmd = [](const CString& d, SYSTEMTIME& st) -> bool
				{
					int y = 0, m = 0, dd = 0;
					if (swscanf_s(d, L"%d-%d-%d", &y, &m, &dd) != 3 || y < 1900 || m < 1 || m > 12 || dd < 1 || dd > 31)
						return false;
					st = {};
					st.wYear = static_cast<WORD>(y); st.wMonth = static_cast<WORD>(m); st.wDay = static_cast<WORD>(dd);
					return true;
				};
				SYSTEMTIME s0 = {}, s1 = {};
				bool haveEnd = !a->retire.IsEmpty() && parseYmd(a->retire, s1);
				if (!haveEnd && a->retire.IsEmpty())
				{
					::GetLocalTime(&s1);
					s1.wHour = s1.wMinute = s1.wSecond = s1.wMilliseconds = 0;
					s1.wDayOfWeek = 0;
					haveEnd = true;
				}
				FILETIME f0 = {}, f1 = {};
				if (parseYmd(a->debut, s0) && haveEnd && ::SystemTimeToFileTime(&s0, &f0) && ::SystemTimeToFileTime(&s1, &f1))
				{
					const ULONGLONG t0 = (static_cast<ULONGLONG>(f0.dwHighDateTime) << 32) | f0.dwLowDateTime;
					const ULONGLONG t1 = (static_cast<ULONGLONG>(f1.dwHighDateTime) << 32) | f1.dwLowDateTime;
					if (t1 >= t0)
					{
						const long long days = static_cast<long long>((t1 - t0) / (10000000ULL * 60 * 60 * 24));
						int years = s1.wYear - s0.wYear;   // 만 주년 (기념일이 지나야 +1)
						if (s1.wMonth < s0.wMonth || (s1.wMonth == s0.wMonth && s1.wDay < s0.wDay))
							--years;
						CString num;
						num.Format(L"%lld", days);
						for (int pos = num.GetLength() - 3; pos > 0; pos -= 3)
							num.Insert(pos, L',');   // 천 단위 쉼표
						CString extra;
						if (years >= 1)
							extra.Format(L" (+%s일, %d주년)", static_cast<LPCWSTR>(num), years);
						else
							extra.Format(L" (+%s일)", static_cast<LPCWSTR>(num));
						period += extra;
					}
				}
				m_rows.push_back({ L"활동기간:", period });
			}
		}
		CString cnt;
		cnt.Format(L"%d편", videoCount);
		m_rows.push_back({ L"출연:", cnt });
		{
			// 링크: 한 줄에 사이트 아이콘(파비콘)들을 나란히, 더블클릭하면 그 주소로
			const std::vector<CString> urls = CVideoLibrary::SplitUrls(a->urls);
			if (!urls.empty())
			{
				Row lr{ L"링크:", CString() };
				lr.links = urls;
				m_rows.push_back(lr);
			}
		}

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
	int h = nameH + 4 + StarRowHeight() + 6 + rows * rowH + 4;   // 이름 + 별 + 줄들
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
	CFont* old = dc.SelectObject(InfoFont());
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
	const int starH = (std::max)(14, StarRowHeight() - 4);   // 별 크기는 그대로
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
	CFont* oldFont = mem.SelectObject(InfoFont());

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
		m_badgeRect.SetRectEmpty();
		m_linkRects.clear();
		mem.SelectObject(InfoFont());
		const int labelW = LabelWidth();
		int y = nameH + 4 + StarRowHeight() + 6;
		for (const Row& r : m_rows)
		{
			mem.SetTextColor(m_label);
			mem.DrawText(r.label, CRect(0, y, labelW, y + rowH), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			mem.SetTextColor(m_text);
			CRect vr(labelW, y, rc.right, y + rowH);
			if (!r.symbol.IsEmpty())
			{
				// 값 왼쪽 기호 (성별): 기호 색으로, 글꼴에 없으면 대체 글꼴
				mem.SetTextColor(r.symColor);
				CFont* prev = mem.SelectObject(&m_symFont);   // 큰 기호 (줄 높이보다 조금 커도 위아래로 넘쳐 그림)
				const int sw = TextFB::Width(&mem, r.symbol);
				TextFB::Draw(&mem, r.symbol, CRect(vr.left, vr.top - rowH / 3, vr.left + sw, vr.bottom + rowH / 3), DT_LEFT, false);
				mem.SelectObject(prev);
				vr.left += sw + 5;
				mem.SetTextColor(m_text);
			}
			if (r.flag >= 0)
			{
				// 국기를 나라 이름 왼쪽에: [국기] 일본
				const int fh = (std::min)(CCountryCombo::kFlagH, rowH);
				const int fw = CCountryCombo::kFlagW * fh / (std::max)(1, CCountryCombo::kFlagH);   // 높이에 맞춘 국기 폭
				CCountryCombo::DrawFlag(&mem, r.flag, vr.left, y + (rowH - fh) / 2, fh);
				CRect tr = vr;
				tr.left += fw + 6;
				mem.DrawText(r.value, tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			}
			else if (!r.links.empty())
			{
				// 링크: 사이트 아이콘을 나란히, 사이에 가는 세로 구분선  예) [X] | [Instagram] | [YouTube]
				//   아이콘 = 각 사이트의 파비콘 (줄 높이의 약 70% 정사각형), 넘치면 생략
				const int sz = (std::max)(12, static_cast<int>(rowH) * 7 / 10);
				const int sepGap = 7;                      // 아이콘과 구분선 사이
				const int sepH = (std::max)(8, sz * 8 / 10);
				int ix = vr.left;
				bool firstIcon = true;
				for (const CString& u : r.links)
				{
					const int need = (firstIcon ? 0 : sepGap * 2 + 1) + sz;
					if (ix + need > rc.right)
						break;
					if (!firstIcon)
					{
						ix += sepGap;
						mem.FillSolidRect(ix, y + (rowH - sepH) / 2, 1, sepH, RGB(0x5C, 0x70, 0x80));   // 구분선
						ix += 1 + sepGap;
					}
					const CRect ir(ix, y + (rowH - sz) / 2, ix + sz, y + (rowH - sz) / 2 + sz);
					DrawLinkIcon(mem, u, ir);
					m_linkRects.push_back({ ir, u });
					ix += sz;
					firstIcon = false;
				}
			}
			else if (!r.badge.IsEmpty())
			{
				// 값 + 오른쪽에 테두리 상자 (예: "2018-05-24 [STAR-888]") - 상자 글자는 기본(작은) 글꼴
				const int tw = TextFB::Width(&mem, r.value);
				TextFB::Draw(&mem, r.value, vr, DT_LEFT, true);
				CFont* prev = mem.SelectObject(BaseFont());
				const int bw = TextFB::Width(&mem, r.badge) + 10;
				TEXTMETRIC btm = {};
				mem.GetTextMetrics(&btm);
				const int bh = (std::min)(static_cast<int>(rowH) - 2, static_cast<int>(btm.tmHeight) + 4);
				const int bx = vr.left + tw + 8;
				const int by = y + (rowH - bh) / 2;
				if (bx + bw <= rc.right)
				{
					m_badgeRect = CRect(bx, by, bx + bw, by + bh);   // 더블클릭 판정용
					CPen pen(PS_SOLID, 1, RGB(0x8A, 0x9B, 0xA8));
					CPen* op = mem.SelectObject(&pen);
					CBrush* ob = static_cast<CBrush*>(mem.SelectStockObject(NULL_BRUSH));
					mem.RoundRect(CRect(bx, by, bx + bw, by + bh), CPoint(5, 5));
					mem.SelectObject(ob);
					mem.SelectObject(op);
					mem.SetTextColor(RGB(0xCE, 0xD9, 0xE0));
					TextFB::Draw(&mem, r.badge, CRect(bx + 5, by, bx + bw - 5, by + bh), DT_LEFT, false);
					mem.SetTextColor(m_text);
				}
				mem.SelectObject(prev);
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
			TEXTMETRIC htm = {};
			mem.GetTextMetrics(&htm);
			for (size_t i = 0; i < m_history.size() && i < pos.size(); ++i)
			{
				int x = pos[i].x;
				const int ly = top + pos[i].y * histH;
				if (i > 0)
				{
					// 화살표는 글자(→)대신 직접 그려서 한글 글자의 세로 가운데에 맞춤
					//  (글자 위치 = TextFB 와 같이 줄 안 세로 가운데, 한글 모양은 내부 여백 아래 ~ 기준선 사이)
					const int textTop = ly + (histH - htm.tmHeight) / 2;
					const double cy = textTop + (htm.tmInternalLeading + htm.tmAscent) / 2.0;
					const double thick = (std::max)(1.2, htm.tmHeight / 11.0 * 0.8);   // 크기 80% (길이 · 굵기 · 화살촉)
					VectorIcon::ArrowRight(&mem, x + arrowW * 0.324, x + arrowW * 0.676, cy, thick, kArrowColor);
					x += arrowW;
				}
				mem.SetTextColor(kHistColor);
				TextFB::Draw(&mem, m_history[i], CRect(x, ly, rc.right, ly + histH), DT_LEFT, true);
			}
			mem.SelectObject(InfoFont());
			y = top + lines * histH;
		}

		// 메모 (여러 줄, 넘치면 마지막 줄 …)
		if (!m_memo.IsEmpty())
		{
			const int mh = MemoHeight(mem, (std::max)(50, static_cast<int>(rc.Width())));
			mem.SelectObject(InfoFont());
			mem.SetTextColor(RGB(0xCE, 0xD9, 0xE0));
			CRect mr(0, y + 8, rc.right, y + 8 + mh);
			mem.DrawText(m_memo, mr, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS | DT_NOPREFIX);
		}
	}

	mem.SelectObject(oldFont);
	paint.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	UpdateLinkTips();   // 링크 아이콘 풍선 도움말
	mem.SelectObject(old);
}
