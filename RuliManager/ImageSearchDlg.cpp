#include "pch.h"
#include "RuliManager.h"
#include "ImageSearchDlg.h"
#include "ImagePreview.h"
#include "WebHelper.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	const int kCols = 5, kRows = 2;
	const COLORREF kBack   = RGB(0x20, 0x2B, 0x33);
	const COLORREF kCell   = RGB(0x30, 0x40, 0x4D);
	const COLORREF kSel    = RGB(0x13, 0x7C, 0xBD);
	const COLORREF kHover  = RGB(0x5C, 0x70, 0x80);
	const COLORREF kText   = RGB(0xA7, 0xB6, 0xC2);
}

// ===========================================================================
// CThumbPickGrid

BEGIN_MESSAGE_MAP(CThumbPickGrid, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_MOUSEMOVE()
	ON_MESSAGE(WM_MOUSELEAVE, &CThumbPickGrid::OnMouseLeave)
END_MESSAGE_MAP()

bool CThumbPickGrid::Create(CWnd* parent, const CRect& rc, UINT id)
{
	const CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_HAND), nullptr);
	return CWnd::Create(cls, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, rc, parent, id) != FALSE;
}

void CThumbPickGrid::Reset(const CString& placeholder)
{
	m_items.clear();
	m_sel = -1;
	m_hover = -1;
	m_placeholder = placeholder;
	if (GetSafeHwnd()) Invalidate(FALSE);
}

CRect CThumbPickGrid::CellRect(int i) const
{
	CRect rc;
	GetClientRect(&rc);
	const int gap = 6;
	const int w = (rc.Width() - gap * (kCols + 1)) / kCols;
	const int h = (rc.Height() - gap * (kRows + 1)) / kRows;
	const int c = i % kCols, r = i / kCols;
	const int x = gap + c * (w + gap), y = gap + r * (h + gap);
	return CRect(x, y, x + w, y + h);
}

int CThumbPickGrid::InfoHeight(CDC& dc)
{
	TEXTMETRIC tm = {};
	dc.GetTextMetrics(&tm);
	return tm.tmHeight + 2;
}

int CThumbPickGrid::HitTest(CPoint pt) const
{
	for (int i = 0; i < static_cast<int>(m_items.size()) && i < kCols * kRows; ++i)
		if (CellRect(i).PtInRect(pt))
			return i;
	return -1;
}

void CThumbPickGrid::OnPaint()
{
	CPaintDC paint(this);
	CRect rc;
	GetClientRect(&rc);
	CDC mem;
	mem.CreateCompatibleDC(&paint);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&paint, (std::max)(1, rc.Width()), (std::max)(1, rc.Height()));
	CBitmap* old = mem.SelectObject(&bmp);
	mem.FillSolidRect(rc, kBack);
	mem.SetBkMode(TRANSPARENT);
	CFont* oldFont = mem.SelectObject(GetParent()->GetFont());

	if (m_items.empty())
	{
		mem.SetTextColor(kText);
		CRect tr = rc;
		mem.DrawText(m_placeholder, tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}
	for (int i = 0; i < static_cast<int>(m_items.size()) && i < kCols * kRows; ++i)
	{
		const CRect cell = CellRect(i);
		mem.FillSolidRect(cell, kCell);
		if (m_items[i].thumb && !m_items[i].thumb->IsNull())
		{
			const CImage& img = *m_items[i].thumb;
			// 비율 유지해서 칸 안에 맞춤
			CRect in = cell;
			in.DeflateRect(4, 4);
			in.bottom -= InfoHeight(mem);   // 아래 정보 줄 자리
			const double sx = static_cast<double>(in.Width()) / img.GetWidth();
			const double sy = static_cast<double>(in.Height()) / img.GetHeight();
			const double s = (std::min)(sx, sy);
			const int w = (std::max)(1, static_cast<int>(img.GetWidth() * s));
			const int h = (std::max)(1, static_cast<int>(img.GetHeight() * s));
			const int x = in.left + (in.Width() - w) / 2, y = in.top + (in.Height() - h) / 2;
			mem.SetStretchBltMode(HALFTONE);
			::SetBrushOrgEx(mem.GetSafeHdc(), 0, 0, nullptr);
			img.Draw(mem.GetSafeHdc(), x, y, w, h);
		}
		else
		{
			mem.SetTextColor(kText);
			CRect tr = cell;
			tr.bottom -= InfoHeight(mem);
			mem.DrawText(L"(불러오지 못함)", tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		}
		// 이미지 아래: 원본 크기 · 확장자
		if (!m_items[i].info.IsEmpty())
		{
			const int ih = InfoHeight(mem);
			CRect ir(cell.left + 2, cell.bottom - ih - 2, cell.right - 2, cell.bottom - 2);
			mem.SetTextColor(RGB(0xCE, 0xD9, 0xE0));
			mem.DrawText(m_items[i].info, ir, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
		}
		if (i == m_sel || i == m_hover)
		{
			CPen pen(PS_SOLID, i == m_sel ? 3 : 1, i == m_sel ? kSel : kHover);
			CPen* op = mem.SelectObject(&pen);
			CBrush* ob = static_cast<CBrush*>(mem.SelectStockObject(NULL_BRUSH));
			CRect fr = cell;
			fr.DeflateRect(1, 1);
			mem.Rectangle(fr);
			mem.SelectObject(op);
			mem.SelectObject(ob);
		}
	}

	mem.SelectObject(oldFont);
	paint.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(old);
}

void CThumbPickGrid::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetFocus();
	const int hit = HitTest(point);
	if (hit >= 0 && hit != m_sel)
	{
		m_sel = hit;
		Invalidate(FALSE);
	}
	CWnd::OnLButtonDown(nFlags, point);
}

void CThumbPickGrid::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	const int hit = HitTest(point);
	if (hit >= 0)
	{
		m_sel = hit;
		Invalidate(FALSE);
		GetParent()->PostMessage(WM_COMMAND, IDOK);   // 더블클릭 = 선택 확인
	}
	CWnd::OnLButtonDblClk(nFlags, point);
}

void CThumbPickGrid::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_tracking)
	{
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, GetSafeHwnd(), 0 };
		m_tracking = ::TrackMouseEvent(&tme) != FALSE;
	}
	const int hit = HitTest(point);
	if (hit != m_hover)
	{
		m_hover = hit;
		Invalidate(FALSE);
	}
	CWnd::OnMouseMove(nFlags, point);
}

LRESULT CThumbPickGrid::OnMouseLeave(WPARAM, LPARAM)
{
	m_tracking = false;
	if (m_hover != -1)
	{
		m_hover = -1;
		Invalidate(FALSE);
	}
	return 0;
}

// ===========================================================================
// CImageSearchDlg

namespace
{
	const int kPageSize = 10;

	// text 에서 startKey 뒤부터 endKey 앞까지를 모두 꺼냄
	std::vector<CString> Between(const CString& text, const CString& startKey, const CString& endKey)
	{
		std::vector<CString> out;
		int pos = 0;
		for (;;)
		{
			const int a = text.Find(startKey, pos);
			if (a < 0) break;
			const int s = a + startKey.GetLength();
			const int e = text.Find(endKey, s);
			if (e < 0) break;
			out.push_back(text.Mid(s, e - s));
			pos = e + endKey.GetLength();
		}
		return out;
	}

	CString FixUrl(CString u)
	{
		u.Replace(L"\\/", L"/");
		if (u.Left(2) == L"//")
			u = L"https:" + u;
		return u;
	}
}

CImageSearchDlg::CImageSearchDlg(const CString& query, CWnd* pParent)
	: CDialogEx(IDD_IMAGE_SEARCH, pParent), m_query(query)
{
}

BEGIN_MESSAGE_MAP(CImageSearchDlg, CDialogEx)
	ON_WM_CTLCOLOR()
	ON_BN_CLICKED(IDC_IMGS_SEARCH, &CImageSearchDlg::OnBnClickedSearch)
	ON_BN_CLICKED(IDC_IMGS_PREV, &CImageSearchDlg::OnBnClickedPrev)
	ON_BN_CLICKED(IDC_IMGS_NEXT, &CImageSearchDlg::OnBnClickedNext)
END_MESSAGE_MAP()

BOOL CImageSearchDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	m_theme.Apply(this);

	// 자리 표시 칸을 썸네일 격자로 교체
	CRect rc(7, 26, 513, 266);
	if (CWnd* holder = GetDlgItem(IDC_IMGS_GRID))
	{
		holder->GetWindowRect(&rc);
		ScreenToClient(&rc);
		holder->DestroyWindow();
	}
	m_grid.Create(this, rc, IDC_IMGS_GRID);
	m_grid.Reset(L"");

	// 검색 엔진 / 세이프서치 (지난번 선택 기억)
	CComboBox* engine = static_cast<CComboBox*>(GetDlgItem(IDC_IMGS_ENGINE));
	engine->AddString(L"Bing");
	engine->AddString(L"Yandex");
	const int e = AfxGetApp()->GetProfileInt(L"Settings", L"ImgEngine2", ENGINE_BING);
	engine->SetCurSel((e >= 0 && e <= ENGINE_YANDEX) ? e : ENGINE_BING);
	CheckDlgButton(IDC_IMGS_SAFEOFF, AfxGetApp()->GetProfileInt(L"Settings", L"ImgSafeOff", 0) ? BST_CHECKED : BST_UNCHECKED);
	UpdateNavButtons();

	SetDlgItemText(IDC_IMGS_QUERY, m_query);
	if (!m_query.IsEmpty())
		PostMessage(WM_COMMAND, MAKEWPARAM(IDC_IMGS_SEARCH, BN_CLICKED), 0);   // 열리자마자 검색
	return TRUE;
}

void CImageSearchDlg::SetStatus(const CString& s)
{
	SetDlgItemText(IDC_IMGS_STATUS, s);
	if (CWnd* w = GetDlgItem(IDC_IMGS_STATUS))
		w->UpdateWindow();
}

void CImageSearchDlg::UpdateNavButtons()
{
	GetDlgItem(IDC_IMGS_PREV)->EnableWindow(m_offset > 0);
	const bool more = (m_offset + kPageSize < static_cast<int>(m_results.size())) || (!m_exhausted && !m_results.empty());
	GetDlgItem(IDC_IMGS_NEXT)->EnableWindow(more);
}

CString CImageSearchDlg::TempDir() const
{
	wchar_t tmp[MAX_PATH] = {};
	::GetTempPathW(MAX_PATH, tmp);
	CString dir = CString(tmp) + L"VideoManager_ImageSearch";
	::CreateDirectoryW(dir, nullptr);
	return dir;
}

void CImageSearchDlg::OnBnClickedSearch()
{
	DoSearch();
}

CString CImageSearchDlg::InfoText(const Result& r)
{
	CString ext = r.ext;
	if (ext.IsEmpty())
	{
		// 주소의 확장자 (쿼리 문자열 앞까지)
		CString path = r.link;
		const int q = path.FindOneOf(L"?#");
		if (q >= 0) path = path.Left(q);
		ext = ::PathFindExtensionW(path);
		ext.TrimLeft(L'.');
		if (ext.GetLength() > 5) ext.Empty();
	}
	ext.MakeUpper();
	if (ext == L"JPEG") ext = L"JPG";
	CString info;
	if (r.w > 0 && r.h > 0)
		info.Format(L"%d\x00D7%d", r.w, r.h);   // ×
	if (!ext.IsEmpty())
		info += (info.IsEmpty() ? L"" : L" \x00B7 ") + ext;   // ·
	return info.IsEmpty() ? CString(L"크기 모름") : info;
}

void CImageSearchDlg::AddResult(const CString& link, const CString& thumb, int w, int h, const CString& ext)
{
	const CString l = FixUrl(Web::HtmlDecode(link));
	if (l.Left(4).CompareNoCase(L"http") != 0)
		return;
	for (const Result& r : m_results)
		if (r.link == l)
			return;   // 중복
	Result r;
	r.link = l;
	const CString t = FixUrl(Web::HtmlDecode(thumb));
	r.thumbLink = (t.Left(4).CompareNoCase(L"http") == 0) ? t : l;
	r.w = w;
	r.h = h;
	r.ext = ext;
	m_results.push_back(r);
}

// --- Bing: 결과 페이지의 m="{&quot;murl&quot;:&quot;원본&quot; ... &quot;turl&quot;:&quot;썸네일&quot;}"
bool CImageSearchDlg::FetchBing(CString& err)
{
	CString url;
	url.Format(L"https://www.bing.com/images/search?q=%s&form=HDRSC2&first=%d&count=35%s",
		static_cast<LPCWSTR>(Web::UrlEncode(m_curQuery)), static_cast<int>(m_results.size()) + 1,
		m_safeOff ? L"&adlt=off" : L"");
	CString headers = L"Accept: text/html,application/xhtml+xml\r\nAccept-Language: ko-KR,ko;q=0.9,ja;q=0.8,en;q=0.7\r\n";
	if (m_safeOff)
		headers += L"Cookie: SRCHHPGUSR=ADLT=OFF\r\n";   // 세이프서치 끄기
	std::vector<BYTE> body;
	if (!Web::HttpGet(url, body, nullptr, &err, 8 * 1024 * 1024, headers))
		return false;
	const CString html = Web::Utf8ToString(body);

	const size_t before = m_results.size();
	const CString murlKey = L"&quot;murl&quot;:&quot;";
	const CString turlKey = L"&quot;turl&quot;:&quot;";
	const CString endQ = L"&quot;";
	int pos = 0;
	for (;;)
	{
		const int m = html.Find(murlKey, pos);
		if (m < 0) break;
		const int ms = m + murlKey.GetLength();
		const int me = html.Find(endQ, ms);
		if (me < 0) break;
		CString turl;
		const int nextM = html.Find(murlKey, me);
		const int t = html.Find(turlKey, me);
		if (t >= 0 && (nextM < 0 || t < nextM))
		{
			const int ts = t + turlKey.GetLength();
			const int te = html.Find(endQ, ts);
			if (te > ts) turl = html.Mid(ts, te - ts);
		}
		// 같은 결과 칸 안의 "1280 x 720 · jpeg" (다음 결과 앞까지)
		int w = 0, h = 0;
		CString ext;
		{
			const int segEnd = (nextM < 0) ? html.GetLength() : nextM;
			const CString infoKey = L"class=\"nowrap\">";
			int sp = html.Find(infoKey, me);
			if (sp > segEnd)
				sp = -1;   // 다른 결과의 정보는 쓰지 않음
			if (sp >= 0)
			{
				sp += infoKey.GetLength();
				const int ep = html.Find(L'<', sp);
				if (ep > sp)
				{
					CString info = Web::HtmlDecode(html.Mid(sp, ep - sp));   // "1280 x 720 · jpeg"
					if (swscanf_s(info, L"%d x %d", &w, &h) != 2) { w = h = 0; }
					const int dot = info.ReverseFind(L'\x00B7');
					if (dot >= 0) { ext = info.Mid(dot + 1); ext.Trim(); }
				}
			}
		}
		AddResult(html.Mid(ms, me - ms), turl, w, h, ext);
		pos = me + endQ.GetLength();
	}
	if (m_results.size() == before)
		err = L"결과 없음";
	return m_results.size() > before;
}

// --- Yandex: 결과 페이지의 "img_href" / "origUrl" (원본), "thumb" 의 url (썸네일)
bool CImageSearchDlg::FetchYandex(CString& err)
{
	CString url;
	url.Format(L"https://yandex.com/images/search?text=%s&p=%d%s",
		static_cast<LPCWSTR>(Web::UrlEncode(m_curQuery)), m_page, m_safeOff ? L"&family=no" : L"&family=moderate");
	std::vector<BYTE> body;
	if (!Web::HttpGet(url, body, nullptr, &err, 8 * 1024 * 1024,
		L"Accept: text/html,application/xhtml+xml\r\nAccept-Language: ko-KR,ko;q=0.9,ja;q=0.8,en;q=0.7\r\n"))
		return false;
	const CString html = Web::Utf8ToString(body);
	if (html.Find(L"captcha") >= 0 && html.Find(L"img_href") < 0 && html.Find(L"origUrl") < 0)
	{
		err = L"Yandex 가 로봇 확인(캡차)을 요구함 - 잠시 후 다시 하거나 다른 엔진을 쓰세요";
		return false;
	}
	// 페이지 안의 JSON 은 &quot; 로 감싸져 있을 수도, 그냥 " 일 수도 있음 → 둘 다 시도
	std::vector<CString> links = Between(html, L"&quot;img_href&quot;:&quot;", L"&quot;");
	if (links.empty()) links = Between(html, L"\"img_href\":\"", L"\"");
	if (links.empty()) links = Between(html, L"&quot;origUrl&quot;:&quot;", L"&quot;");
	if (links.empty()) links = Between(html, L"\"origUrl\":\"", L"\"");
	std::vector<CString> thumbs = Between(html, L"&quot;thumb&quot;:{&quot;url&quot;:&quot;", L"&quot;");
	if (thumbs.empty()) thumbs = Between(html, L"\"thumb\":{\"url\":\"", L"\"");
	// 원본 크기: "origWidth"/"origHeight" (없으면 모름)
	CString plain = html;
	plain.Replace(L"&quot;", L"\"");
	const std::vector<int> ws = Web::JsonNumbers(plain, L"origWidth");
	const std::vector<int> hs = Web::JsonNumbers(plain, L"origHeight");
	const bool sizeOk = (ws.size() == links.size() && hs.size() == links.size());
	const size_t before = m_results.size();
	for (size_t i = 0; i < links.size(); ++i)
		AddResult(links[i], i < thumbs.size() ? thumbs[i] : CString(),
			sizeOk ? ws[i] : 0, sizeOk ? hs[i] : 0);
	if (m_results.size() == before)
		err = L"결과 없음 (Yandex 페이지 구조가 바뀌었을 수 있음)";
	return m_results.size() > before;
}

bool CImageSearchDlg::FetchMore(CString& err)
{
	if (m_exhausted)
		return false;
	bool ok = false;
	switch (m_curEngine)
	{
	case ENGINE_YANDEX: ok = FetchYandex(err); break;
	default:            ok = FetchBing(err); break;
	}
	++m_page;
	if (!ok)
		m_exhausted = true;
	return ok;
}

void CImageSearchDlg::DoSearch()
{
	CString query;
	GetDlgItemText(IDC_IMGS_QUERY, query);
	query.Trim();
	if (query.IsEmpty())
		return;

	m_curQuery = query;
	m_curEngine = static_cast<CComboBox*>(GetDlgItem(IDC_IMGS_ENGINE))->GetCurSel();
	if (m_curEngine < 0) m_curEngine = ENGINE_BING;
	m_safeOff = IsDlgButtonChecked(IDC_IMGS_SAFEOFF) == BST_CHECKED;
	AfxGetApp()->WriteProfileInt(L"Settings", L"ImgEngine2", m_curEngine);
	AfxGetApp()->WriteProfileInt(L"Settings", L"ImgSafeOff", m_safeOff ? 1 : 0);
	m_results.clear();
	m_offset = 0;
	m_page = 0;
	m_exhausted = false;

	CWaitCursor wait;
	m_grid.Reset(L"검색 중...");
	m_grid.UpdateWindow();
	SetStatus(L"검색 중: " + query);

	CString err;
	if (!FetchMore(err))
	{
		m_grid.Reset(L"검색 결과가 없습니다.");
		SetStatus(L"검색 실패: " + err);
		UpdateNavButtons();
		return;
	}
	ShowPage();
}

void CImageSearchDlg::ShowPage()
{
	CWaitCursor wait;
	const CString dir = TempDir();
	m_grid.m_items.clear();
	m_grid.m_sel = -1;
	const int end = (std::min)(m_offset + kPageSize, static_cast<int>(m_results.size()));
	for (int i = m_offset; i < end; ++i)
	{
		Result& r = m_results[i];
		if (!r.loaded)
		{
			CString s;
			s.Format(L"썸네일 받는 중... %d / %d", i - m_offset + 1, end - m_offset);
			SetStatus(s);
			r.loaded = true;
			std::vector<BYTE> data;
			if (Web::HttpGet(r.thumbLink, data, nullptr, nullptr, 3 * 1024 * 1024))
			{
				CString file;
				file.Format(L"%s\\thumb%d.img", static_cast<LPCWSTR>(dir), i);
				CFile f;
				if (f.Open(file, CFile::modeCreate | CFile::modeWrite))
				{
					f.Write(data.data(), static_cast<UINT>(data.size()));
					f.Close();
					auto img = std::make_shared<CImage>();
					if (LoadImageFile(*img, file))
						r.thumb = img;
					::DeleteFileW(file);
				}
			}
		}
		CThumbPickGrid::Item it;
		it.thumb = r.thumb;
		it.link = r.link;
		it.thumbLink = r.thumbLink;
		it.info = InfoText(r);
		m_grid.m_items.push_back(std::move(it));
		m_grid.Invalidate(FALSE);
		m_grid.UpdateWindow();
	}
	CString s;
	s.Format(L"%d ~ %d번째 결과 · 사진을 클릭해서 고르고 [선택] (더블클릭도 됨)", m_offset + 1, end);
	SetStatus(s);
	UpdateNavButtons();
}

void CImageSearchDlg::OnBnClickedPrev()
{
	if (m_offset <= 0)
		return;
	m_offset = (std::max)(0, m_offset - kPageSize);
	ShowPage();
}

void CImageSearchDlg::OnBnClickedNext()
{
	const int next = m_offset + kPageSize;
	if (next >= static_cast<int>(m_results.size()) && !m_exhausted)
	{
		CWaitCursor wait;
		SetStatus(L"다음 결과 받는 중...");
		CString err;
		FetchMore(err);
	}
	if (next >= static_cast<int>(m_results.size()))
	{
		SetStatus(L"더 이상 결과가 없습니다.");
		UpdateNavButtons();
		return;
	}
	m_offset = next;
	ShowPage();
}

void CImageSearchDlg::OnOK()
{
	const int sel = m_grid.m_sel;
	if (sel < 0 || sel >= static_cast<int>(m_grid.m_items.size()))
	{
		SetStatus(L"사진을 먼저 고르세요.");
		return;
	}
	CWaitCursor wait;
	SetStatus(L"원본 사진 받는 중...");
	const CThumbPickGrid::Item& it = m_grid.m_items[sel];

	// 원본을 받고, 실패하면(차단 등) 썸네일이라도 사용
	const CString dir = TempDir();
	auto download = [&](const CString& url, const CString& name) -> CString
	{
		std::vector<BYTE> data;
		CString ct;
		if (!Web::HttpGet(url, data, &ct))
			return CString();
		CString ext = L".jpg";
		ct.MakeLower();
		if (ct.Find(L"png") >= 0) ext = L".png";
		else if (ct.Find(L"webp") >= 0) ext = L".webp";
		else if (ct.Find(L"gif") >= 0) ext = L".gif";
		else if (ct.Find(L"bmp") >= 0) ext = L".bmp";
		const CString file = dir + L"\\" + name + ext;
		CFile f;
		if (!f.Open(file, CFile::modeCreate | CFile::modeWrite))
			return CString();
		f.Write(data.data(), static_cast<UINT>(data.size()));
		f.Close();
		CImage test;
		if (!LoadImageFile(test, file))   // 이미지가 아니면(HTML 오류 페이지 등) 버림
		{
			::DeleteFileW(file);
			return CString();
		}
		return file;
	};
	CString path = download(it.link, L"photo");
	if (path.IsEmpty())
		path = download(it.thumbLink, L"photo_thumb");
	if (path.IsEmpty())
	{
		SetStatus(L"사진을 받지 못했습니다. 다른 사진을 골라 보세요.");
		return;
	}
	m_resultPath = path;
	CDialogEx::OnOK();
}

HBRUSH CImageSearchDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
		return br;
	return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}
