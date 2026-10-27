#include "pch.h"
#include "DarkControls.h"   // DarkMenu: 테마 색 오른쪽 클릭 메뉴
#include "RuliManager.h"
#include "SeriesDlg.h"
#include "TextInfoDlg.h"
#include <algorithm>
#include <cstdlib>
#include <windowsx.h>   // GET_X_LPARAM

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	// 글자 조각에서 품번 접두어 꺼내기: "SONE-479" / "SONE479" / "sone" → "SONE", "300MIUM-001" → "300MIUM" (아니면 빈 문자열)
	CString SeriesPrefixOf(CString t)
	{
		t.Trim();
		t.Trim(L"[]()【】「」<>\"'");
		t.MakeUpper();
		if (t.IsEmpty() || t.GetLength() > 20)
			return CString();
		CString head = t;
		const int dash = t.FindOneOf(L"-_");
		if (dash > 0)
			head = t.Left(dash);
		// 영문 · 숫자만, 영문이 2자 이상
		int letters = 0;
		for (int i = 0; i < head.GetLength(); ++i)
		{
			const wchar_t c = head[i];
			if (c >= L'A' && c <= L'Z') ++letters;
			else if (!(c >= L'0' && c <= L'9')) return CString();
		}
		if (letters < 2 || head.GetLength() > 10)
			return CString();
		if (dash < 0)
		{
			// "-" 없이 붙은 번호는 뗌 (SONE479 → SONE), 앞쪽 숫자(300MIUM)는 유지
			int end = head.GetLength();
			while (end > 0 && head[end - 1] >= L'0' && head[end - 1] <= L'9') --end;
			head = head.Left(end);
			if (head.IsEmpty())
				return CString();
		}
		return head;
	}

	// 한 줄을 칸으로 나눔: 탭(표 복사) → 없으면 두 칸 이상 공백 · " | " · " / " · " - "
	std::vector<CString> SplitCells(const CString& line)
	{
		std::vector<CString> cells;
		auto push = [&cells](CString c) { c.Trim(); if (!c.IsEmpty()) cells.push_back(c); };
		if (line.Find(L'\t') >= 0)
		{
			int pos = 0;
			for (;;)
			{
				const int p = line.Find(L'\t', pos);
				push(p < 0 ? line.Mid(pos) : line.Mid(pos, p - pos));
				if (p < 0) break;
				pos = p + 1;
			}
			return cells;
		}
		CString t = line;
		for (LPCWSTR sep : { L" | ", L"｜", L" / ", L" - ", L" – ", L"：", L": " })
			t.Replace(sep, L"\t");
		while (t.Find(L"   ") >= 0) t.Replace(L"   ", L"  ");
		t.Replace(L"  ", L"\t");
		if (t.Find(L'\t') >= 0)
			return SplitCells(t);
		// 한 칸뿐: 맨 앞 낱말이 품번이면 나머지를 레이블로
		const int sp = t.Find(L' ');
		if (sp > 0 && !SeriesPrefixOf(t.Left(sp)).IsEmpty())
		{
			push(t.Left(sp));
			push(t.Mid(sp + 1));
		}
		else
			push(t);
		return cells;
	}
}

// ===========================================================================
// CSeriesDlg (품번 관리)

CSeriesDlg::CSeriesDlg(CVideoLibrary& lib, const CString& selectName, CWnd* pParent)
	: CDialogEx(IDD_SERIES, pParent), m_lib(lib), m_selectName(selectName)
{
}

void CSeriesDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SR_SEARCH, m_editSearch);
	DDX_Control(pDX, IDC_SR_GRID, m_grid);
	DDX_Control(pDX, IDC_SR_STUDIO, m_comboStudio);
	DDX_Control(pDX, IDC_SR_CODE, m_editCode);
	DDX_Control(pDX, IDC_SR_LABEL, m_editLabel);
	DDX_Control(pDX, IDC_SR_ALT, m_editAlt);
	DDX_Control(pDX, IDC_SR_DESC, m_editDesc);
	DDX_Control(pDX, IDC_SR_IMAGE, m_image);
}

BEGIN_MESSAGE_MAP(CSeriesDlg, CDialogEx)
	ON_WM_CTLCOLOR()
	ON_EN_CHANGE(IDC_SR_SEARCH, &CSeriesDlg::OnEnChangeSearch)
	ON_EN_CHANGE(IDC_SR_CODE, &CSeriesDlg::OnFieldChanged)
	ON_EN_CHANGE(IDC_SR_LABEL, &CSeriesDlg::OnFieldChanged)
	ON_EN_CHANGE(IDC_SR_DESC, &CSeriesDlg::OnFieldChanged)
	ON_EN_CHANGE(IDC_SR_ALT, &CSeriesDlg::OnFieldChanged)
	ON_CBN_SELCHANGE(IDC_SR_STUDIO, &CSeriesDlg::OnFieldChanged)
	ON_LBN_SELCHANGE(IDC_SR_GRID, &CSeriesDlg::OnGridSelChange)
	ON_LBN_DBLCLK(IDC_SR_GRID, &CSeriesDlg::OnGridDblClk)
	ON_WM_MEASUREITEM()
	ON_WM_DRAWITEM()
	ON_WM_CONTEXTMENU()
	ON_NOTIFY(HDN_ITEMCHANGED, IDC_SR_HEADER, &CSeriesDlg::OnHeaderChanged)
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONUP()
	ON_WM_CAPTURECHANGED()
	ON_BN_CLICKED(IDC_SR_NEW, &CSeriesDlg::OnBnClickedNew)
	ON_BN_CLICKED(IDC_SR_DELETE, &CSeriesDlg::OnBnClickedDelete)
	ON_BN_CLICKED(IDC_SR_SAVE, &CSeriesDlg::OnBnClickedSave)
	ON_BN_CLICKED(IDC_SR_IMG_BROWSE, &CSeriesDlg::OnBnClickedImgBrowse)
	ON_BN_CLICKED(IDC_SR_IMG_CLEAR, &CSeriesDlg::OnBnClickedImgClear)
	ON_BN_CLICKED(IDC_SR_PASTE, &CSeriesDlg::OnBnClickedPaste)
END_MESSAGE_MAP()

BOOL CSeriesDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	m_theme.Apply(this, { IDC_SR_DELETE });   // 메인 창과 같은 색상 (삭제 버튼은 빨간색)
	m_image.SetBackColor(DarkColors::Preview);
	m_image.SetPlaceholder(L"이미지 없음");
	m_image.m_onDblClick = [this]() { OnBnClickedImgBrowse(); };

	for (const NamedInfo& n : m_lib.studios)
		m_backupStudios.push_back(n.series);
	m_backupLoose = m_lib.looseSeries;

	// 제작사 콤보 (이름 순, 항목 값 = studios 인덱스)
	{
		std::vector<int> order;
		for (size_t i = 0; i < m_lib.studios.size(); ++i)
			order.push_back(static_cast<int>(i));
		std::sort(order.begin(), order.end(), [this](int a, int b)
		{
			return ::StrCmpLogicalW(NameOf(a), NameOf(b)) < 0;
		});
		for (int si : order)
		{
			const int ci = m_comboStudio.AddString(NameOf(si));
			m_comboStudio.SetItemData(ci, static_cast<DWORD_PTR>(si));
		}
		const int ci = m_comboStudio.InsertString(0, NameOf(LooseIdx()));   // 맨 위: (제작사 없음)
		m_comboStudio.SetItemData(ci, static_cast<DWORD_PTR>(LooseIdx()));
	}

	{
		// 글자 한 줄 높이
		CClientDC dc(this);
		CFont* of = dc.SelectObject(GetFont());
		TEXTMETRIC tm = {};
		dc.GetTextMetrics(&tm);
		dc.SelectObject(of);
		m_lineH = tm.tmHeight + tm.tmExternalLeading;
	}
	{
		// 표 자리(rc 의 목록) 위쪽에 열 머리글, 그 아래로 목록
		CRect gr;
		m_grid.GetWindowRect(&gr);
		ScreenToClient(&gr);
		const int hh = m_lineH + 8;
		m_header.Create(WS_CHILD | WS_VISIBLE | HDS_HORZ | HDS_FULLDRAG, CRect(gr.left, gr.top, gr.right, gr.top + hh), this, IDC_SR_HEADER);
		m_header.SetFont(GetFont());
		m_grid.SetWindowPos(nullptr, gr.left, gr.top + hh, gr.Width(), gr.Height() - hh, SWP_NOZORDER | SWP_NOACTIVATE);
		CRect cr;
		m_grid.GetClientRect(&cr);
		const int w = cr.Width() - ::GetSystemMetrics(SM_CXVSCROLL);
		struct ColDef { LPCWSTR title; int pct; int fmt; };
		// 순서(제작사마다 0, 1, 2 …)는 열로 보이지 않음 - 표 순서 · 메뉴 · 끌기로만
		const ColDef cols[] = { { L"제작사", 22, HDF_LEFT }, { L"품번", 17, HDF_LEFT }, { L"레이블", 27, HDF_LEFT }, { L"설명", 34, HDF_LEFT } };
		int used = 0;
		for (int i = 0; i < 4; ++i)
		{
			HDITEMW hd = {};
			hd.mask = HDI_TEXT | HDI_WIDTH | HDI_FORMAT;
			hd.pszText = const_cast<LPWSTR>(cols[i].title);
			hd.cxy = (i == 3) ? (std::max)(40, w - used) : w * cols[i].pct / 100;
			hd.fmt = cols[i].fmt | HDF_STRING;
			used += hd.cxy;
			m_header.InsertItem(i, &hd);
		}
	}
	m_editSearch.SetCueBanner(L"제작사 · 품번 · 레이블 · 설명 검색");

	int selStudio = m_selectName.IsEmpty() ? -1 : m_lib.FindNamed(LIST_STUDIO, m_selectName);
	CString selName;
	if (selStudio >= 0)
	{
		const std::vector<SeriesInfo> list = CVideoLibrary::ParseSeries(SeriesOf(selStudio));
		if (!list.empty())
			selName = list.front().name;
	}
	FillGrid(selStudio, selName);
	if (m_curStudio < 0)
	{
		ShowEntry(-1, -1);
		if (selStudio >= 0)
			SelectStudioCombo(selStudio);   // 품번이 없는 제작사: [새 품번] 의 제작사로
	}
	return TRUE;
}

CString& CSeriesDlg::SeriesOf(int studio)
{
	if (studio >= 0 && studio < static_cast<int>(m_lib.studios.size()))
		return m_lib.studios[studio].series;
	return m_lib.looseSeries;   // LooseIdx() = 제작사 없는 품번 항목
}

const CString& CSeriesDlg::SeriesOf(int studio) const
{
	if (studio >= 0 && studio < static_cast<int>(m_lib.studios.size()))
		return m_lib.studios[studio].series;
	return m_lib.looseSeries;
}

CString CSeriesDlg::NameOf(int studio) const
{
	if (studio >= 0 && studio < static_cast<int>(m_lib.studios.size()))
		return m_lib.studios[studio].name;
	return studio == LooseIdx() ? CString(L"(제작사 없음)") : CString();
}

std::vector<CString> CSeriesDlg::SplitCodes(const CString& text)
{
	CString t = text;
	for (LPCWSTR sep : { L"\r\n", L"\n", L"\r", L";", L" ", L"\t", L"、", L"，" })
		t.Replace(sep, L",");
	std::vector<CString> out;
	for (CString c : CVideoLibrary::SplitList(t))
	{
		c.Trim();
		c.MakeUpper();
		c.TrimRight(L'-');
		if (c.IsEmpty())
			continue;
		bool dup = false;
		for (const CString& o : out)
			if (o == c) { dup = true; break; }
		if (!dup)
			out.push_back(c);
	}
	return out;
}

int CSeriesDlg::FindEntry(int studio, const CString& name) const
{
	if (studio < 0 || studio > LooseIdx() || name.IsEmpty())
		return -1;
	const std::vector<SeriesInfo> list = CVideoLibrary::ParseSeries(SeriesOf(studio));
	for (size_t i = 0; i < list.size(); ++i)
		if (list[i].name.CompareNoCase(name) == 0)
			return static_cast<int>(i);
	return -1;
}

void CSeriesDlg::FillGrid(int selStudio, const CString& selName)
{
	CString query;
	m_editSearch.GetWindowText(query);
	query.Trim();
	query.MakeLower();

	// 모든 제작사의 품번 항목: 제작사 이름 → 품번 순
	m_rows.clear();
	std::vector<int> order;
	for (size_t i = 0; i < m_lib.studios.size(); ++i)
		order.push_back(static_cast<int>(i));
	std::sort(order.begin(), order.end(), [this](int a, int b)
	{
		return ::StrCmpLogicalW(NameOf(a), NameOf(b)) < 0;
	});
	order.push_back(LooseIdx());   // 제작사 없는 항목은 맨 뒤
	std::vector<std::vector<SeriesInfo>> lists(m_lib.studios.size() + 1);
	for (int si : order)
	{
		lists[si] = CVideoLibrary::ParseSeries(SeriesOf(si));
		std::vector<int> idx;
		for (size_t k = 0; k < lists[si].size(); ++k)
		{
			const SeriesInfo& s = lists[si][k];
			if (!query.IsEmpty())
			{
				CString hay = NameOf(si) + L"\n" + s.name + L"\n" + s.label + L"\n" + s.altNames + L"\n" + s.desc;   // 다른 이름도 검색
				hay.MakeLower();
				if (hay.Find(query) < 0)
					continue;
			}
			idx.push_back(static_cast<int>(k));
		}
		for (int k : idx)   // 저장된 순서 그대로 (제작사마다 0, 1, 2 …)
		{
			Row r;
			r.studio = si;
			r.idx = k;
			r.s = lists[si][k];
			m_rows.push_back(r);
		}
	}

	// 제작사가 바뀔 때마다 배경색 2가지를 번갈아 (묶음 번호)
	{
		int band = 0;
		for (size_t k = 0; k < m_rows.size(); ++k)
		{
			if (k > 0 && m_rows[k].studio != m_rows[k - 1].studio)
				band ^= 1;
			m_rows[k].band = band;
		}
	}
	// 목록 다시 (줄 높이는 줄마다 품번 수만큼 - WM_MEASUREITEM)
	m_loading = true;
	m_grid.SetRedraw(FALSE);
	m_grid.ResetContent();
	int selRow = -1;
	for (size_t row = 0; row < m_rows.size(); ++row)
	{
		const Row& r = m_rows[row];
		m_grid.AddString(L"");
		if (r.studio == selStudio && !selName.IsEmpty() && r.s.name.CompareNoCase(selName) == 0)
			selRow = static_cast<int>(row);
	}
	m_grid.SetRedraw(TRUE);
	m_grid.Invalidate();
	SelectRow(selRow);
	m_loading = false;
	if (selRow >= 0)
		ShowEntry(m_rows[selRow].studio, m_rows[selRow].idx);
}

int CSeriesDlg::RowHeight(const Row& r) const
{
	const int lines = (std::max)(1, static_cast<int>(CVideoLibrary::SplitList(r.s.name).size()));
	return (std::min)(255, m_lineH * lines + 8);   // 목록 줄 높이는 255 픽셀까지
}

void CSeriesDlg::SelectRow(int row)
{
	m_grid.SetCurSel(row >= 0 && row < static_cast<int>(m_rows.size()) ? row : -1);   // SetCurSel 은 알림(LBN_SELCHANGE)이 없음, 보이게 스크롤
}

void CSeriesDlg::OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpmis)
{
	if (nIDCtl == IDC_SR_GRID)
	{
		const int row = static_cast<int>(lpmis->itemID);
		lpmis->itemHeight = (row >= 0 && row < static_cast<int>(m_rows.size())) ? RowHeight(m_rows[row]) : m_lineH + 8;
		return;
	}
	CDialogEx::OnMeasureItem(nIDCtl, lpmis);
}

void CSeriesDlg::OnHeaderChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	(void)pNMHDR;
	m_grid.Invalidate(FALSE);   // 열 폭이 바뀜
}

void CSeriesDlg::SelectStudioCombo(int studio)
{
	m_comboStudio.SetCurSel(-1);
	for (int i = 0; i < m_comboStudio.GetCount(); ++i)
		if (static_cast<int>(m_comboStudio.GetItemData(i)) == studio)
		{
			m_comboStudio.SetCurSel(i);
			break;
		}
}

int CSeriesDlg::ComboStudio() const
{
	const int sel = m_comboStudio.GetCurSel();
	return (sel >= 0) ? static_cast<int>(m_comboStudio.GetItemData(sel)) : -1;
}

void CSeriesDlg::SetImage(const CString& path)
{
	m_curImage = path;
	m_image.Clear();
	if (path.IsEmpty())
		m_image.SetPlaceholder(L"이미지 없음 (더블클릭: 선택)");
	else if (::GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
		m_image.SetPlaceholder(L"이미지 파일을 찾을 수 없습니다.");
	else
		m_image.SetImageFile(path);
	SetDlgItemText(IDC_SR_IMAGE_PATH, path.IsEmpty() ? CString() : CString(::PathFindFileNameW(path)));
}

void CSeriesDlg::ShowFields(int studio, const SeriesInfo* s)
{
	m_loading = true;
	SelectStudioCombo(studio);
	CString codes;
	if (s)
		for (const CString& c : CVideoLibrary::SplitList(s->name))
		{
			if (!codes.IsEmpty()) codes += L"\r\n";
			codes += c;
		}
	m_editCode.SetWindowText(codes);
	m_editLabel.SetWindowText(s ? s->label : CString());
	{
		CString alt = s ? s->altNames : CString();
		alt.Replace(L"\r\n", L"\n");
		alt.Replace(L"\n", L"\r\n");   // 에디트 줄바꿈
		m_editAlt.SetWindowText(alt);
	}
	CString desc = s ? s->desc : CString();
	m_editDesc.SetWindowText(desc);
	SetImage(s ? s->image : CString());
	m_loading = false;
}

void CSeriesDlg::ShowEntry(int studio, int idx)
{
	std::vector<SeriesInfo> list;
	if (studio >= 0 && studio <= LooseIdx())
		list = CVideoLibrary::ParseSeries(SeriesOf(studio));
	if (idx < 0 || idx >= static_cast<int>(list.size()))
	{
		m_curStudio = -1;
		m_curName.Empty();
		ShowFields(-1, nullptr);
	}
	else
	{
		m_curStudio = studio;
		m_curName = list[idx].name;
		ShowFields(studio, &list[idx]);
	}
	m_new = false;
	m_dirty = false;
	UpdateButtons();
}

void CSeriesDlg::UpdateButtons()
{
	const BOOL edit = (m_curStudio >= 0 || m_new);
	for (UINT id : { IDC_SR_STUDIO, IDC_SR_CODE, IDC_SR_LABEL, IDC_SR_ALT, IDC_SR_DESC, IDC_SR_IMG_BROWSE, IDC_SR_IMG_CLEAR })
		GetDlgItem(id)->EnableWindow(edit);
	GetDlgItem(IDC_SR_DELETE)->EnableWindow(m_curStudio >= 0 || m_new);
	GetDlgItem(IDC_SR_SAVE)->EnableWindow(m_dirty);
}

void CSeriesDlg::OnFieldChanged()
{
	if (m_loading || (m_curStudio < 0 && !m_new))
		return;
	m_dirty = true;
	GetDlgItem(IDC_SR_SAVE)->EnableWindow(TRUE);
}

bool CSeriesDlg::Commit()
{
	if (!m_dirty)
		return true;
	const int studio = ComboStudio();
	CString codeText, label, desc, alt;
	m_editCode.GetWindowText(codeText);
	m_editLabel.GetWindowText(label);
	m_editAlt.GetWindowText(alt);
	alt.Replace(L"\r\n", L"\n");
	alt.Trim();
	m_editDesc.GetWindowText(desc);
	label.Trim();
	desc.Replace(L"\r\n", L" ");
	desc.Replace(L'\n', L' ');
	desc.Trim();
	const std::vector<CString> codes = SplitCodes(codeText);
	if (codes.empty())
	{
		if (m_new && label.IsEmpty() && desc.IsEmpty() && alt.IsEmpty() && m_curImage.IsEmpty())
		{
			// 아무것도 넣지 않은 새 품번: 그냥 버림
			m_dirty = false;
			m_new = false;
			return true;
		}
		AfxMessageBox(L"품번을 입력하세요. (품번 앞자리, 예: SONE)", MB_ICONWARNING);
		m_editCode.SetFocus();
		return false;
	}
	if (studio < 0)
	{
		AfxMessageBox(L"제작사를 고르세요.", MB_ICONWARNING);
		m_comboStudio.SetFocus();
		return false;
	}

	// 같은 품번이 다른 항목에 있으면 확인 후 그쪽에서 뺌 (품번은 한 항목에만)
	struct Hit { int studio; CString entry; CString code; };
	std::vector<Hit> hits;
	for (int si = 0; si <= LooseIdx(); ++si)
		for (const SeriesInfo& s : CVideoLibrary::ParseSeries(SeriesOf(si)))
		{
			if (!m_new && si == m_curStudio && s.name.CompareNoCase(m_curName) == 0)
				continue;   // 지금 항목
			for (CString p : CVideoLibrary::SplitList(s.name))
			{
				p.Trim();
				p.MakeUpper();
				p.TrimRight(L'-');
				for (const CString& c : codes)
					if (c == p)
						hits.push_back({ si, s.name, c });
			}
		}
	if (!hits.empty())
	{
		CString msg = L"다른 항목에 이미 있는 품번입니다:\n";
		for (const Hit& h : hits)
			msg += L"  " + h.code + L"  (" + NameOf(h.studio) + L" · " + h.entry + L")\n";
		msg += L"\n그 항목에서 빼고 여기에 둘까요?";
		if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION) != IDYES)
			return false;
		for (const Hit& h : hits)
		{
			std::vector<SeriesInfo> list = CVideoLibrary::ParseSeries(SeriesOf(h.studio));
			for (size_t k = 0; k < list.size(); ++k)
			{
				if (list[k].name.CompareNoCase(h.entry) != 0)
					continue;
				std::vector<CString> keep;
				for (CString p : CVideoLibrary::SplitList(list[k].name))
				{
					CString q = p;
					q.Trim();
					q.MakeUpper();
					q.TrimRight(L'-');
					if (q != h.code)
						keep.push_back(p.Trim());
				}
				if (keep.empty())
					list.erase(list.begin() + k);
				else
					list[k].name = CVideoLibrary::JoinList(keep);
				break;
			}
			SeriesOf(h.studio) = CVideoLibrary::JoinSeries(list);
		}
	}

	SeriesInfo s;
	s.name = CVideoLibrary::JoinList(codes);
	s.label = label;
	s.desc = desc;
	s.altNames = alt;
	s.image = m_curImage;
	if (!m_new && m_curStudio >= 0)
	{
		// 예전 자리에서 뺌 (같은 제작사면 그 자리에 그대로 넣음)
		std::vector<SeriesInfo> list = CVideoLibrary::ParseSeries(SeriesOf(m_curStudio));
		int at = -1;
		for (size_t k = 0; k < list.size(); ++k)
			if (list[k].name.CompareNoCase(m_curName) == 0) { at = static_cast<int>(k); break; }
		if (m_curStudio == studio && at >= 0)
			list[at] = s;
		else
		{
			if (at >= 0)
				list.erase(list.begin() + at);
			SeriesOf(m_curStudio) = CVideoLibrary::JoinSeries(list);
			list = CVideoLibrary::ParseSeries(SeriesOf(studio));
			list.push_back(s);
		}
		SeriesOf(studio) = CVideoLibrary::JoinSeries(list);
	}
	else
	{
		std::vector<SeriesInfo> list = CVideoLibrary::ParseSeries(SeriesOf(studio));
		list.push_back(s);
		SeriesOf(studio) = CVideoLibrary::JoinSeries(list);
	}
	m_curStudio = studio;
	m_curName = s.name;
	m_new = false;
	m_dirty = false;
	m_anyChange = true;
	return true;
}

void CSeriesDlg::OnBnClickedSave()
{
	if (!Commit())
		return;
	const int studio = m_curStudio;
	const CString name = m_curName;
	FillGrid(studio, name);
	if (FindEntry(studio, name) >= 0 && SelRow() < 0)
		ShowEntry(studio, FindEntry(studio, name));   // 검색에 걸리지 않아 표에 없어도 입력 칸은 그대로
	UpdateButtons();
}

void CSeriesDlg::OnBnClickedNew()
{
	if (!Commit())
		return;
	const int studio = (m_curStudio >= 0) ? m_curStudio : ComboStudio();
	if (m_anyChange)
		FillGrid(-1, CString());
	SelectRow(-1);
	m_curStudio = -1;
	m_curName.Empty();
	ShowFields(studio, nullptr);   // 제작사는 지금 것 그대로
	m_new = true;
	m_dirty = false;
	UpdateButtons();
	m_editCode.SetFocus();
}

void CSeriesDlg::DeleteCurrent()
{
	if (m_new)
	{
		ShowEntry(-1, -1);   // 입력 중인 새 품번 버림
		return;
	}
	if (m_curStudio < 0)
		return;
	CString q;
	q.Format(L"품번 '%s' (%s) 을(를) 삭제할까요?", static_cast<LPCWSTR>(m_curName), static_cast<LPCWSTR>(NameOf(m_curStudio)));
	if (AfxMessageBox(q, MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;
	std::vector<SeriesInfo> list = CVideoLibrary::ParseSeries(SeriesOf(m_curStudio));
	for (size_t k = 0; k < list.size(); ++k)
		if (list[k].name.CompareNoCase(m_curName) == 0) { list.erase(list.begin() + k); break; }
	SeriesOf(m_curStudio) = CVideoLibrary::JoinSeries(list);
	m_anyChange = true;
	const int row = SelRow();
	m_dirty = false;
	FillGrid(-1, CString());
	ShowEntry(-1, -1);
	// 지운 줄 자리(또는 마지막 줄)를 고름
	const int n = static_cast<int>(m_rows.size());
	if (n > 0 && row >= 0)
	{
		const int sel = (std::min)(row, n - 1);
		SelectRow(sel);
		ShowEntry(m_rows[sel].studio, m_rows[sel].idx);
	}
}

void CSeriesDlg::OnBnClickedDelete()
{
	DeleteCurrent();
}

void CSeriesDlg::OnBnClickedImgBrowse()
{
	if (m_curStudio < 0 && !m_new)
		return;
	CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
		L"이미지 파일 (*.jpg;*.jpeg;*.png;*.webp;*.bmp;*.gif;*.tif;*.tiff;*.svg)|*.jpg;*.jpeg;*.png;*.webp;*.bmp;*.gif;*.tif;*.tiff;*.svg|모든 파일 (*.*)|*.*||",
		this);
	if (dlg.DoModal() != IDOK)
		return;
	// 원본 대신 이미지 보관소(studios 폴더)에 복사본을 만들어 그것을 등록 (제작사 로고와 같은 곳)
	const CString copy = CVideoLibrary::StoreImageCopy(dlg.GetPathName(), L"studios");
	if (copy.IsEmpty())
	{
		AfxMessageBox(L"이미지를 DB 폴더로 복사하지 못했습니다.", MB_ICONWARNING);
		return;
	}
	SetImage(copy);
	OnFieldChanged();
}

void CSeriesDlg::OnBnClickedImgClear()
{
	if (m_curStudio < 0 && !m_new)
		return;
	SetImage(CString());
	OnFieldChanged();
}

void CSeriesDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpdis)
{
	if (nIDCtl != IDC_SR_GRID)
	{
		CDialogEx::OnDrawItem(nIDCtl, lpdis);
		return;
	}
	// 줄 전체를 직접 그림: 품번은 줄마다 하나씩 (줄 높이 = 품번 수), 레이블 · 설명은 줄 높이 안에서 줄바꿈
	const int row = static_cast<int>(lpdis->itemID);
	if (row < 0 || row >= static_cast<int>(m_rows.size()) || lpdis->itemAction == ODA_FOCUS)
		return;
	const Row& r = m_rows[row];
	CDC* dc = CDC::FromHandle(lpdis->hDC);
	const CRect rc = lpdis->rcItem;
	const bool sel = (lpdis->itemState & ODS_SELECTED) != 0;
	const bool drop = (m_dragging && row == m_dropRow);   // 끌어서 놓을 자리
	// 제작사 묶음마다 배경 2가지 번갈아 (기본 칸 색 / 조금 밝게)
	const COLORREF base = DarkColors::Edit;
	const int lum = (GetRValue(base) * 299 + GetGValue(base) * 587 + GetBValue(base) * 114) / 1000;
	const int d = (lum < 128) ? 14 : -14;   // 어두운 테마는 밝게, 밝은 테마는 어둡게
	auto shift = [d](int v) { return (std::max)(0, (std::min)(255, v + d)); };
	const COLORREF bandBack = (r.band & 1) ? RGB(shift(GetRValue(base)), shift(GetGValue(base)), shift(GetBValue(base))) : base;
	const COLORREF back = drop ? DarkColors::Button : (sel ? DarkColors::SelBack : bandBack);
	dc->FillSolidRect(rc, back);
	// 제작사 칸 머지: 같은 제작사 줄이 이어지면 제작사 칸은 한 칸처럼 (이름은 묶음 전체 높이의 세로 가운데, 칸 사이 가로줄 없음, 선택 색 없음)
	const bool sameAsNext = (row + 1 < static_cast<int>(m_rows.size()) && m_rows[row + 1].studio == r.studio);
	CRect studioCell(0, 0, 0, 0);   // 제작사 칸 = 첫 열
	{
		CRect h0;
		if (m_header.GetItemRect(0, &h0))
			studioCell = CRect(rc.left, rc.top, rc.left + h0.Width(), rc.bottom);
	}
	if (!studioCell.IsRectEmpty())
		dc->FillSolidRect(studioCell, bandBack);
	{
		CPen pen(PS_SOLID, 1, DarkColors::Border);
		CPen* op = dc->SelectObject(&pen);
		if (sameAsNext && !studioCell.IsRectEmpty())
		{
			// 가로줄: 제작사 칸만 빼고
			dc->MoveTo(rc.left, rc.bottom - 1);
			dc->LineTo(studioCell.left, rc.bottom - 1);
			dc->MoveTo(studioCell.right, rc.bottom - 1);
			dc->LineTo(rc.right, rc.bottom - 1);
		}
		else
		{
			dc->MoveTo(rc.left, rc.bottom - 1);
			dc->LineTo(rc.right, rc.bottom - 1);
		}
		if (!studioCell.IsRectEmpty())
		{
			// 제작사 칸 양옆 세로줄 (머지한 칸 경계)
			dc->MoveTo(studioCell.left, rc.top);
			dc->LineTo(studioCell.left, rc.bottom);
			dc->MoveTo(studioCell.right - 1, rc.top);
			dc->LineTo(studioCell.right - 1, rc.bottom);
		}
		dc->SelectObject(op);
	}
	const int saved = dc->SaveDC();
	dc->SelectObject(GetFont());
	dc->SetBkMode(TRANSPARENT);
	const int pad = 6;
	int x = rc.left;
	for (int col = 0; col < 4; ++col)
	{
		CRect hr;
		const int cw = m_header.GetItemRect(col, &hr) ? hr.Width() : 0;
		CRect cell(x + pad, rc.top + 4, x + cw - pad, rc.bottom - 4);
		x += cw;
		if (cell.right <= cell.left)
			continue;
		CString text;
		UINT fmt = DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS;
		if (col == 1)
		{
			// 품번: 줄마다 하나 (위에서부터)
			for (const CString& c : CVideoLibrary::SplitList(r.s.name))
			{
				if (!text.IsEmpty()) text += L"\n";
				text += c;
			}
			fmt |= DT_TOP;
			dc->SetTextColor(drop ? RGB(255, 255, 255) : DarkColors::Text);
		}
		else if (col == 0)
		{
			// 머지한 제작사 칸: 같은 제작사 줄 전체 높이의 세로 가운데에 이름 - 줄마다 자기 자리에 걸친 부분만 그림
			int first = row, last = row;
			while (first > 0 && m_rows[first - 1].studio == r.studio) --first;
			while (last + 1 < static_cast<int>(m_rows.size()) && m_rows[last + 1].studio == r.studio) ++last;
			int above = 0, total = 0;
			for (int k = first; k <= last; ++k)
			{
				const int h = RowHeight(m_rows[k]);
				if (k < row) above += h;
				total += h;
			}
			text = NameOf(r.studio);
			fmt |= DT_WORDBREAK | DT_EDITCONTROL;
			dc->SetTextColor(DarkColors::Text);
			const CRect group(cell.left, rc.top - above + 4, cell.right, rc.top - above + total - 4);
			CRect calc(group.left, 0, group.right, 0);
			dc->DrawText(text, calc, fmt | DT_CALCRECT);
			const int h = (std::min)(static_cast<int>(calc.Height()), static_cast<int>(group.Height()));
			CRect tr(group.left, group.top + (group.Height() - h) / 2, group.right, group.top + (group.Height() - h) / 2 + h);
			const int clip = dc->SaveDC();
			dc->IntersectClipRect(CRect(studioCell.left, rc.top, studioCell.right - 1, rc.bottom - (sameAsNext ? 0 : 1)));   // 이 줄 자리만
			dc->DrawText(text, tr, fmt);
			dc->RestoreDC(clip);
			continue;
		}
		else
		{
			text = (col == 2) ? r.s.label : r.s.desc;
			fmt |= DT_WORDBREAK | DT_EDITCONTROL;
			dc->SetTextColor(drop ? RGB(255, 255, 255) : (col == 3 ? TCOL(TC_SUB) : DarkColors::Text));
			// 세로 가운데 (줄 수만큼)
			CRect calc(cell.left, 0, cell.right, 0);
			dc->DrawText(text, calc, fmt | DT_CALCRECT);
			const int h = (std::min)(static_cast<int>(calc.Height()), static_cast<int>(cell.Height()));
			cell.top += (cell.Height() - h) / 2;
			cell.bottom = cell.top + h;
		}
		dc->DrawText(text, cell, fmt);
	}
	dc->RestoreDC(saved);
}

void CSeriesDlg::OnGridSelChange()
{
	if (m_loading)
		return;
	OnRowSelected(SelRow());
}

void CSeriesDlg::OnRowSelected(int row)
{
	if (row < 0 || row >= static_cast<int>(m_rows.size()))
		return;
	const Row target = m_rows[row];
	const CString targetName = target.s.name;
	if (!m_new && target.studio == m_curStudio && targetName.CompareNoCase(m_curName) == 0)
		return;
	if (m_dirty)
	{
		if (!Commit())
		{
			// 잘못된 값: 지금 항목 줄로 되돌림
			int back = -1;
			for (size_t i = 0; i < m_rows.size(); ++i)
				if (m_rows[i].studio == m_curStudio && m_rows[i].s.name.CompareNoCase(m_curName) == 0) { back = static_cast<int>(i); break; }
			SelectRow(back);
			return;
		}
		FillGrid(target.studio, targetName);   // 표 다시 (고른 줄 다시 찾음)
		if (SelRow() < 0)
			ShowEntry(-1, -1);
		return;
	}
	ShowEntry(target.studio, FindEntry(target.studio, targetName));
}

void CSeriesDlg::OnGridDblClk()
{
	if (m_curStudio >= 0)
	{
		m_editCode.SetFocus();   // 오른쪽 입력으로
		m_editCode.SetSel(0, -1);
	}
}

void CSeriesDlg::OnContextMenu(CWnd* pWnd, CPoint point)
{
	if (pWnd->GetSafeHwnd() != m_grid.GetSafeHwnd())
	{
		CDialogEx::OnContextMenu(pWnd, point);
		return;
	}
	if (point.x == -1 && point.y == -1)
	{
		// 키보드 (Shift+F10 · 메뉴 키): 고른 줄 자리
		CRect ir;
		const int s = SelRow();
		if (s >= 0 && m_grid.GetItemRect(s, &ir) != LB_ERR)
			point = ir.CenterPoint();
		m_grid.ClientToScreen(&point);
	}
	else
	{
		// 오른쪽 클릭한 줄을 먼저 고름
		CPoint pt = point;
		m_grid.ScreenToClient(&pt);
		BOOL outside = TRUE;
		const int row = static_cast<int>(m_grid.ItemFromPoint(pt, outside));
		if (!outside && row >= 0 && row < static_cast<int>(m_rows.size()) && row != SelRow())
		{
			SelectRow(row);
			OnRowSelected(row);
		}
	}
	ShowGridMenu(point);
}

void CSeriesDlg::ShowGridMenu(CPoint screen)
{
	const bool onRow = (SelRow() >= 0);
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, 1, L"새 품번");
	menu.AppendMenu(MF_STRING, 4, L"웹페이지 내용 붙여넣기 (일괄 추가)...");
	if (onRow && m_curStudio >= 0)
	{
		// 순서 바꾸기 (같은 제작사 안)
		const int idx = FindEntry(m_curStudio, m_curName);
		const int cnt = static_cast<int>(CVideoLibrary::ParseSeries(SeriesOf(m_curStudio)).size());
		const UINT up = (idx > 0) ? 0 : MF_GRAYED;
		const UINT down = (idx >= 0 && idx < cnt - 1) ? 0 : MF_GRAYED;
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING | up, 5, L"위로 이동\tCtrl+↑");
		menu.AppendMenu(MF_STRING | down, 6, L"아래로 이동\tCtrl+↓");
		menu.AppendMenu(MF_STRING | up, 7, L"맨 위로 이동");
		menu.AppendMenu(MF_STRING | down, 8, L"맨 아래로 이동");
	}
	if (onRow)
	{
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, 3, L"품번 삭제\tDel");
	}
	const UINT cmd = DarkMenu::Track(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, screen.x, screen.y, this);
	if (cmd == 1)
		OnBnClickedNew();
	else if (cmd == 3)
		DeleteCurrent();
	else if (cmd == 4)
		PasteFromWeb();
	else if (cmd == 5)
		MoveCurrent(-1);
	else if (cmd == 6)
		MoveCurrent(+1);
	else if (cmd == 7)
		MoveCurrent(INT_MIN);
	else if (cmd == 8)
		MoveCurrent(INT_MAX);
}

void CSeriesDlg::MoveEntry(int studio, int from, int to)
{
	std::vector<SeriesInfo> list = CVideoLibrary::ParseSeries(SeriesOf(studio));
	const int n = static_cast<int>(list.size());
	if (from < 0 || from >= n)
		return;
	to = (std::max)(0, (std::min)(n - 1, to));
	if (to == from)
		return;
	const SeriesInfo item = list[from];
	list.erase(list.begin() + from);
	list.insert(list.begin() + to, item);
	SeriesOf(studio) = CVideoLibrary::JoinSeries(list);
	m_anyChange = true;
	FillGrid(studio, item.name);   // 옮긴 항목을 다시 고름
}

void CSeriesDlg::MoveCurrent(int delta)
{
	if (m_curStudio < 0 || m_new)
		return;
	if (!Commit())
		return;
	const int idx = FindEntry(m_curStudio, m_curName);
	if (idx < 0)
		return;
	const int to = (delta == INT_MIN) ? 0 : (delta == INT_MAX) ? INT_MAX / 2 : idx + delta;
	MoveEntry(m_curStudio, idx, to);
}

int CSeriesDlg::DropRowAt(CPoint screen) const
{
	CPoint pt = screen;
	m_grid.ScreenToClient(&pt);
	CRect rc;
	m_grid.GetClientRect(&rc);
	if (!rc.PtInRect(pt))
		return -1;
	BOOL outside = TRUE;
	const int row = static_cast<int>(m_grid.ItemFromPoint(pt, outside));
	if (outside || row < 0 || row >= static_cast<int>(m_rows.size()) || m_rows[row].studio != m_dragStudio)
		return -1;   // 다른 제작사 줄에는 놓지 않음 (제작사는 오른쪽 [제작사] 로 바꿈)
	if (m_rows[row].s.name.CompareNoCase(m_dragName) == 0)
		return -1;   // 자기 자신
	return row;
}

void CSeriesDlg::SetDropRow(int row)
{
	if (row == m_dropRow)
		return;
	const int old = m_dropRow;
	m_dropRow = row;
	CRect ir;
	if (old >= 0 && m_grid.GetItemRect(old, &ir) != LB_ERR)
		m_grid.InvalidateRect(&ir, FALSE);
	if (row >= 0 && m_grid.GetItemRect(row, &ir) != LB_ERR)
		m_grid.InvalidateRect(&ir, FALSE);
	m_grid.UpdateWindow();
}

void CSeriesDlg::BeginDrag(int row)
{
	if (row < 0 || row >= static_cast<int>(m_rows.size()))
		return;
	if (m_dirty && !Commit())
		return;
	if (row >= static_cast<int>(m_rows.size()))
		return;
	m_dragStudio = m_rows[row].studio;
	m_dragName = m_rows[row].s.name;
	m_dragging = true;
	m_dropRow = -1;
	SetCapture();   // 목록의 마우스 잡기를 대화상자가 가져옴
	::SetCursor(::LoadCursor(nullptr, IDC_NO));
}

void CSeriesDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_dragging)
	{
		CDialogEx::OnMouseMove(nFlags, point);
		return;
	}
	CPoint scr = point;
	ClientToScreen(&scr);
	// 표 위 · 아래 끝 근처면 스크롤
	CPoint pt = scr;
	m_grid.ScreenToClient(&pt);
	CRect rc;
	m_grid.GetClientRect(&rc);
	const int edge = ::GetSystemMetrics(SM_CYMENU);
	if (pt.x >= rc.left && pt.x < rc.right)
	{
		const int top = m_grid.GetTopIndex();
		if (pt.y < rc.top + edge && pt.y >= rc.top - edge * 2 && top > 0)
			m_grid.SetTopIndex(top - 1);
		else if (pt.y > rc.bottom - edge && pt.y <= rc.bottom + edge * 2 && top + 1 < m_grid.GetCount())
			m_grid.SetTopIndex(top + 1);
	}
	const int row = DropRowAt(scr);
	SetDropRow(row);
	::SetCursor(::LoadCursor(nullptr, row >= 0 ? IDC_ARROW : IDC_NO));
}

void CSeriesDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_dragging)
	{
		CPoint scr = point;
		ClientToScreen(&scr);
		m_dropRow = DropRowAt(scr);
		EndDrag(true);
		return;
	}
	CDialogEx::OnLButtonUp(nFlags, point);
}

void CSeriesDlg::OnCaptureChanged(CWnd* pWnd)
{
	if (m_dragging && pWnd != this)
		EndDrag(false);   // 다른 창이 마우스를 가져감 → 취소
	CDialogEx::OnCaptureChanged(pWnd);
}

void CSeriesDlg::EndDrag(bool drop)
{
	if (!m_dragging)
		return;
	const int row = m_dropRow;
	const int studio = m_dragStudio;
	const CString name = m_dragName;
	m_dragging = false;
	m_dropRow = -1;
	if (GetCapture() == this)
		ReleaseCapture();
	m_grid.Invalidate(FALSE);
	if (!drop || row < 0 || row >= static_cast<int>(m_rows.size()) || m_rows[row].studio != studio)
		return;
	const int from = FindEntry(studio, name);
	MoveEntry(studio, from, m_rows[row].idx);   // 놓은 줄의 자리로 (위에서 내리면 그 줄 아래, 아래에서 올리면 그 줄 위)
}

void CSeriesDlg::OnBnClickedPaste()
{
	PasteFromWeb();
}

void CSeriesDlg::PasteFromWeb()
{
	if (!Commit())
		return;
	const int studio = ComboStudio();
	if (studio < 0)
	{
		AfxMessageBox(L"붙여 넣을 제작사를 오른쪽 [제작사] 에서 먼저 고르세요.\n(품번을 하나 고르거나 [새 품번] 을 누른 뒤 제작사 선택)", MB_ICONINFORMATION);
		return;
	}
	CTextInfoDlg dlg(L"품번 일괄 추가 - 웹페이지 내용 붙여넣기 (" + NameOf(studio) + L")",
		L"사이트의 시리즈 / 품번 목록(표)을 드래그해 복사(Ctrl+C)한 뒤 붙여넣으세요(Ctrl+V). 한 줄에 하나씩 읽습니다.\r\n"
		L"각 줄에서 품번(예: SONE-479, SSIS)을 찾아 접두어를 품번 칸에, 그다음 칸을 레이블, 나머지를 설명으로 넣습니다. 이미 있는 품번은 빈 칸만 채웁니다.",
		CString(), this);
	dlg.m_pasteButton = true;   // [공백라인 제거]
	if (dlg.DoModal() != IDOK)
		return;

	CString text = dlg.m_text;
	text.Replace(L"\r\n", L"\n");
	text.Replace(L'\r', L'\n');
	text.Replace(L'\x00A0', L' ');
	std::vector<SeriesInfo> rows = CVideoLibrary::ParseSeries(SeriesOf(studio));
	int added = 0, updated = 0, skipped = 0;
	int pos = 0;
	while (pos <= text.GetLength())
	{
		int nl = text.Find(L'\n', pos);
		if (nl < 0) nl = text.GetLength();
		CString line = text.Mid(pos, nl - pos);
		pos = nl + 1;
		line.Trim();
		if (line.IsEmpty())
			continue;
		const std::vector<CString> cells = SplitCells(line);
		// 품번 칸: 앞쪽 칸 중 처음으로 품번 모양인 것
		int ci = -1;
		CString prefix;
		for (size_t i = 0; i < cells.size() && i < 3; ++i)
		{
			prefix = SeriesPrefixOf(cells[i]);
			if (!prefix.IsEmpty()) { ci = static_cast<int>(i); break; }
		}
		if (ci < 0)
		{
			++skipped;   // 머리글 · 설명 줄 등
			continue;
		}
		CString label, desc;
		std::vector<CString> rest;
		for (size_t i = 0; i < cells.size(); ++i)
			if (static_cast<int>(i) != ci) rest.push_back(cells[i]);
		if (!rest.empty())
			label = rest[0];
		for (size_t i = 1; i < rest.size(); ++i)
		{
			if (!desc.IsEmpty()) desc += L" ";
			desc += rest[i];
		}
		SeriesInfo* found = nullptr;
		for (SeriesInfo& s : rows)
			for (const CString& c : SplitCodes(s.name))
				if (c == prefix) { found = &s; break; }
		if (found)
		{
			bool ch = false;
			if (found->label.IsEmpty() && !label.IsEmpty()) { found->label = label; ch = true; }
			if (found->desc.IsEmpty() && !desc.IsEmpty()) { found->desc = desc; ch = true; }
			if (ch) ++updated;
		}
		else
		{
			SeriesInfo s;
			s.name = prefix;
			s.label = label;
			s.desc = desc;
			rows.push_back(s);
			++added;
		}
	}
	if (added + updated > 0)
	{
		SeriesOf(studio) = CVideoLibrary::JoinSeries(rows);
		m_anyChange = true;
		const CString keep = m_curName;
		const int keepStudio = m_curStudio;
		FillGrid(keepStudio, keep);
		if (SelRow() < 0)
			ShowEntry(-1, -1);
	}
	CString msg;
	msg.Format(L"품번 %d개를 추가하고 %d개를 보완했습니다.%s\n\n[확인]을 누르면 저장됩니다.", added, updated,
		skipped > 0 ? static_cast<LPCWSTR>(CString(L"\n(품번을 찾지 못한 줄은 건너뜀)")) : L"");
	AfxMessageBox(msg, MB_ICONINFORMATION);
}

BOOL CSeriesDlg::PreTranslateMessage(MSG* pMsg)
{
	// 표: Del = 삭제, 끌기 시작 (왼쪽 버튼을 누른 채 조금 움직이면)
	if (pMsg->hwnd == m_grid.GetSafeHwnd())
	{
		if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_DELETE)
		{
			DeleteCurrent();
			return TRUE;
		}
		if (pMsg->message == WM_LBUTTONDOWN)
		{
			m_pressPt = CPoint(GET_X_LPARAM(pMsg->lParam), GET_Y_LPARAM(pMsg->lParam));
			BOOL outside = TRUE;
			const int row = static_cast<int>(m_grid.ItemFromPoint(m_pressPt, outside));
			m_pressRow = (!outside && row < static_cast<int>(m_rows.size())) ? row : -1;
			m_pressing = (m_pressRow >= 0);
		}
		else if (pMsg->message == WM_LBUTTONUP)
			m_pressing = false;
		else if (pMsg->message == WM_MOUSEMOVE && m_pressing && !m_dragging)
		{
			if (!(pMsg->wParam & MK_LBUTTON))
				m_pressing = false;
			else
			{
				const CPoint pt(GET_X_LPARAM(pMsg->lParam), GET_Y_LPARAM(pMsg->lParam));
				if (std::abs(pt.x - m_pressPt.x) > ::GetSystemMetrics(SM_CXDRAG) || std::abs(pt.y - m_pressPt.y) > ::GetSystemMetrics(SM_CYDRAG))
				{
					m_pressing = false;
					BeginDrag(m_pressRow);
					return TRUE;
				}
			}
		}
	}
	// 표에서 Ctrl+↑ / Ctrl+↓ = 순서 바꾸기
	if (pMsg->message == WM_KEYDOWN && pMsg->hwnd == m_grid.GetSafeHwnd() && (pMsg->wParam == VK_UP || pMsg->wParam == VK_DOWN)
		&& (::GetKeyState(VK_CONTROL) & 0x8000))
	{
		MoveCurrent(pMsg->wParam == VK_UP ? -1 : +1);
		return TRUE;
	}
	// 한 줄 칸(레이블명)에서 Enter = [저장] (여러 줄 칸은 줄바꿈, 대화상자 [확인] 으로 닫히지 않게)
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN && pMsg->hwnd == m_editLabel.GetSafeHwnd())
	{
		OnBnClickedSave();
		return TRUE;
	}
	return CDialogEx::PreTranslateMessage(pMsg);
}

void CSeriesDlg::OnEnChangeSearch()
{
	if (!Commit())
		return;
	FillGrid(m_curStudio, m_curName);
	if (SelRow() < 0 && !m_new)
		ShowEntry(-1, -1);
}

void CSeriesDlg::OnOK()
{
	if (!Commit())
		return;
	m_changed = m_anyChange;
	CDialogEx::OnOK();
}

void CSeriesDlg::OnCancel()
{
	if (m_anyChange || m_dirty)
	{
		if (AfxMessageBox(L"바꾼 품번을 저장하지 않고 닫을까요?", MB_YESNO | MB_ICONQUESTION) != IDYES)
			return;
		// 처음 상태로 되돌림 (이 창에서는 제작사 추가 · 삭제가 없으므로 인덱스 그대로)
		for (size_t i = 0; i < m_lib.studios.size() && i < m_backupStudios.size(); ++i)
			m_lib.studios[i].series = m_backupStudios[i];
		m_lib.looseSeries = m_backupLoose;
	}
	m_changed = false;
	CDialogEx::OnCancel();
}

HBRUSH CSeriesDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
		return br;
	return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}
