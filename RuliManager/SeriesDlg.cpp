#include "pch.h"
#include "RuliManager.h"
#include "SeriesDlg.h"
#include "TextInfoDlg.h"

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
		// 한 칸뿐: 맨 앞 낱말이 품번이면 나머지를 라벨로
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
	DDX_Control(pDX, IDC_SR_LIST, m_list);
	DDX_Control(pDX, IDC_SR_TITLE, m_staticTitle);
	DDX_Control(pDX, IDC_SR_GRID, m_grid);
}

BEGIN_MESSAGE_MAP(CSeriesDlg, CDialogEx)
	ON_WM_CTLCOLOR()
	ON_EN_CHANGE(IDC_SR_SEARCH, &CSeriesDlg::OnEnChangeSearch)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_SR_LIST, &CSeriesDlg::OnLvnItemChanged)
	ON_NOTIFY(NM_DBLCLK, IDC_SR_GRID, &CSeriesDlg::OnGridDblClk)
	ON_NOTIFY(LVN_KEYDOWN, IDC_SR_GRID, &CSeriesDlg::OnGridKeyDown)
	ON_NOTIFY(NM_RCLICK, IDC_SR_GRID, &CSeriesDlg::OnGridRClick)
	ON_EN_KILLFOCUS(IDC_SR_CELL_EDIT, &CSeriesDlg::OnCellEditKillFocus)
	ON_BN_CLICKED(IDC_SR_PASTE, &CSeriesDlg::OnBnClickedPaste)
END_MESSAGE_MAP()

BOOL CSeriesDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	m_theme.Apply(this);   // 메인 창과 같은 색상

	for (const NamedInfo& n : m_lib.studios)
		m_backupStudios.push_back(n.series);
	for (const NamedInfo& n : m_lib.labelInfos)
		m_backupLabels.push_back(n.series);

	CRect r(0, 0, 100, 0);
	MapDialogRect(&r);
	const int dlu = r.right;
	m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
	m_list.InsertColumn(0, L"제작사 / 레이블", LVCFMT_LEFT, dlu * 110 / 100);
	m_list.InsertColumn(1, L"품번 수", LVCFMT_RIGHT, dlu * 40 / 100);
	m_editSearch.SetCueBanner(L"이름 · 품번 검색");

	CRect gr;
	m_grid.GetClientRect(&gr);
	const int w = gr.Width() - ::GetSystemMetrics(SM_CXVSCROLL) - 4;
	m_grid.ModifyStyle(0, WS_CLIPSIBLINGS);   // 셀 편집 칸이 표 위에 보이게
	m_grid.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
	m_grid.InsertColumn(0, L"품번", LVCFMT_LEFT, w * 24 / 100);
	m_grid.InsertColumn(1, L"라벨", LVCFMT_LEFT, w * 32 / 100);
	m_grid.InsertColumn(2, L"설명", LVCFMT_LEFT, w - w * 56 / 100);
	m_cellEdit.Create(WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, CRect(0, 0, 10, 10), this, IDC_SR_CELL_EDIT);
	m_cellEdit.SetFont(GetFont());

	FillList(CodeOf(m_selectName));
	if (m_cur < 0)
		ShowItem(-1);
	return TRUE;
}

NamedInfo* CSeriesDlg::ItemOf(int code)
{
	if (code >= kLabelBase)
		return (code - kLabelBase < static_cast<int>(m_lib.labelInfos.size())) ? &m_lib.labelInfos[code - kLabelBase] : nullptr;
	return (code >= 0 && code < static_cast<int>(m_lib.studios.size())) ? &m_lib.studios[code] : nullptr;
}

CString CSeriesDlg::RowText(int code) const
{
	if (code >= kLabelBase)
	{
		const NamedInfo& lb = m_lib.labelInfos[code - kLabelBase];
		const bool hasParent = m_lib.FindNamed(LIST_STUDIO, lb.parent) >= 0;
		return (hasParent ? L"   └ " : L"└ ") + lb.name;
	}
	return m_lib.studios[code].name;
}

int CSeriesDlg::CodeOf(const CString& name) const
{
	if (name.IsEmpty())
		return -1;
	const int si = m_lib.FindNamed(LIST_STUDIO, name);
	if (si >= 0)
		return si;
	const int li = m_lib.FindLabel(name);
	return (li >= 0) ? kLabelBase + li : -1;
}

void CSeriesDlg::FillList(int selectCode)
{
	CString query;
	m_editSearch.GetWindowText(query);
	query.Trim();
	query.MakeLower();
	auto matches = [&query](const NamedInfo& n)
	{
		if (query.IsEmpty())
			return true;
		CString hay = n.name + L"\n" + n.subName;
		for (const SeriesInfo& s : CVideoLibrary::ParseSeries(n.series))
			hay += L"\n" + s.name + L"\n" + s.label;
		hay.MakeLower();
		return hay.Find(query) >= 0;
	};

	// 제작사 (이름 순) 바로 아래에 그 레이블 (└), 상위 없는 레이블은 맨 아래 - 제작사 / 레이블 관리 창과 같은 순서
	m_rows.clear();
	std::vector<int> studios;
	for (size_t i = 0; i < m_lib.studios.size(); ++i)
		studios.push_back(static_cast<int>(i));
	std::sort(studios.begin(), studios.end(), [this](int a, int b)
	{
		return ::StrCmpLogicalW(m_lib.studios[a].name, m_lib.studios[b].name) < 0;
	});
	for (int si : studios)
	{
		const NamedInfo& st = m_lib.studios[si];
		const bool stHit = matches(st);
		std::vector<int> lbs;
		for (int li : m_lib.LabelsOf(st.name))
			if (stHit || matches(m_lib.labelInfos[li]))
				lbs.push_back(li);
		if (!stHit && lbs.empty())
			continue;
		m_rows.push_back(si);
		for (int li : lbs)
			m_rows.push_back(kLabelBase + li);
	}
	std::vector<int> orphans;
	for (size_t li = 0; li < m_lib.labelInfos.size(); ++li)
	{
		const NamedInfo& lb = m_lib.labelInfos[li];
		if (m_lib.FindNamed(LIST_STUDIO, lb.parent) < 0 && matches(lb))
			orphans.push_back(static_cast<int>(li));
	}
	std::sort(orphans.begin(), orphans.end(), [this](int a, int b)
	{
		return ::StrCmpLogicalW(m_lib.labelInfos[a].name, m_lib.labelInfos[b].name) < 0;
	});
	for (int li : orphans)
		m_rows.push_back(kLabelBase + li);

	m_loading = true;
	m_list.SetRedraw(FALSE);
	m_list.DeleteAllItems();
	int selRow = -1;
	for (size_t row = 0; row < m_rows.size(); ++row)
	{
		const int code = m_rows[row];
		const int i = m_list.InsertItem(static_cast<int>(row), RowText(code));
		const NamedInfo* n = ItemOf(code);
		const size_t cnt = n ? CVideoLibrary::ParseSeries(n->series).size() : 0;
		CString c;
		if (cnt > 0)
			c.Format(L"%d", static_cast<int>(cnt));
		m_list.SetItemText(i, 1, c);
		if (selectCode >= 0 && code == selectCode)
			selRow = i;
	}
	m_list.SetRedraw(TRUE);
	m_list.Invalidate();
	m_loading = false;

	if (selRow >= 0)
	{
		m_list.SetItemState(selRow, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_list.EnsureVisible(selRow, FALSE);
		ShowItem(m_rows[selRow]);
	}
}

void CSeriesDlg::ShowItem(int code)
{
	EndCellEdit(true);
	CommitCurrent();
	m_cur = ItemOf(code) ? code : -1;
	if (const NamedInfo* n = ItemOf(m_cur))
	{
		m_seriesRows = CVideoLibrary::ParseSeries(n->series);
		CString t;
		t.Format(L"%s: %s", (m_cur >= kLabelBase) ? L"레이블" : L"제작사", static_cast<LPCWSTR>(n->name));
		m_staticTitle.SetWindowText(t);
	}
	else
	{
		m_seriesRows.clear();
		m_staticTitle.SetWindowText(L"(왼쪽에서 제작사 / 레이블을 고르세요)");
	}
	m_dirty = false;
	m_grid.EnableWindow(m_cur >= 0);
	GetDlgItem(IDC_SR_PASTE)->EnableWindow(m_cur >= 0);
	FillGrid();
}

void CSeriesDlg::CommitCurrent()
{
	if (!m_dirty)
		return;
	m_dirty = false;
	NamedInfo* np = ItemOf(m_cur);
	if (!np)
		return;
	NamedInfo& n = *np;
	// 빈 품번 줄은 뺌
	std::vector<SeriesInfo> rows;
	for (const SeriesInfo& s : m_seriesRows)
		if (!s.name.IsEmpty())
			rows.push_back(s);
	n.series = CVideoLibrary::JoinSeries(rows);
	// 같은 시리즈가 다른 제작사 · 레이블에 있으면 그쪽에서 뺌 (시리즈는 하나에만 속함)
	const std::vector<CString> mine = CVideoLibrary::SeriesNames(n.series);
	std::vector<NamedInfo*> others;
	for (NamedInfo& o : m_lib.labelInfos) others.push_back(&o);
	for (NamedInfo& o : m_lib.studios) others.push_back(&o);
	for (NamedInfo* op : others)
	{
		NamedInfo& other = *op;
		if (&other == &n || other.series.IsEmpty())
			continue;
		std::vector<SeriesInfo> keep;
		for (const SeriesInfo& s : CVideoLibrary::ParseSeries(other.series))
		{
			bool taken = false;
			for (const CString& m : mine)
				if (m.CompareNoCase(s.name) == 0) { taken = true; break; }
			if (!taken) keep.push_back(s);
		}
		other.series = CVideoLibrary::JoinSeries(keep);
	}
	m_anyChange = true;
	// 목록의 품번 수 다시
	for (size_t row = 0; row < m_rows.size(); ++row)
	{
		const NamedInfo* it = ItemOf(m_rows[row]);
		const size_t cnt = it ? CVideoLibrary::ParseSeries(it->series).size() : 0;
		CString c;
		if (cnt > 0)
			c.Format(L"%d", static_cast<int>(cnt));
		m_list.SetItemText(static_cast<int>(row), 1, c);
	}
}

void CSeriesDlg::Changed()
{
	m_dirty = true;
}

void CSeriesDlg::FillGrid()
{
	m_grid.SetRedraw(FALSE);
	m_grid.DeleteAllItems();
	for (size_t i = 0; i < m_seriesRows.size(); ++i)
	{
		const SeriesInfo& s = m_seriesRows[i];
		const int r = m_grid.InsertItem(static_cast<int>(i), s.name);
		m_grid.SetItemText(r, 1, s.label);
		m_grid.SetItemText(r, 2, s.desc);
	}
	if (m_cur >= 0)
		m_grid.InsertItem(static_cast<int>(m_seriesRows.size()), L"+ 새 품번 (더블클릭)");   // 마지막 줄: 추가
	m_grid.SetRedraw(TRUE);
	m_grid.Invalidate();
}

void CSeriesDlg::BeginCellEdit(int row, int col)
{
	EndCellEdit(true);
	if (row < 0 || row >= static_cast<int>(m_seriesRows.size()) || col < 0 || col > 2)
		return;
	CRect rc;
	if (col == 0)
	{
		m_grid.GetSubItemRect(row, 1, LVIR_BOUNDS, rc);   // 첫 열은 다음 열 왼쪽까지
		CRect b;
		m_grid.GetItemRect(row, &b, LVIR_BOUNDS);
		rc.right = rc.left;
		rc.left = b.left;
	}
	else
		m_grid.GetSubItemRect(row, col, LVIR_BOUNDS, rc);
	m_grid.EnsureVisible(row, FALSE);
	m_grid.ClientToScreen(&rc);
	ScreenToClient(&rc);
	rc.InflateRect(0, 2);
	const SeriesInfo& s = m_seriesRows[row];
	const CString text = (col == 0) ? s.name : (col == 1) ? s.label : s.desc;
	m_editRow = row;
	m_editCol = col;
	m_cellEdit.SetWindowText(text);
	m_cellEdit.SetWindowPos(&CWnd::wndTop, rc.left, rc.top, rc.Width(), rc.Height(), SWP_SHOWWINDOW);
	m_cellEdit.SetFocus();
	m_cellEdit.SetSel(0, -1);
}

void CSeriesDlg::EndCellEdit(bool save)
{
	if (m_editRow < 0 || !m_cellEdit.GetSafeHwnd())
		return;
	const int row = m_editRow, col = m_editCol;
	m_editRow = m_editCol = -1;   // 먼저 지움 (숨길 때 KILLFOCUS 로 다시 들어오지 않게)
	CString text;
	m_cellEdit.GetWindowText(text);
	m_cellEdit.ShowWindow(SW_HIDE);
	if (!save || row >= static_cast<int>(m_seriesRows.size()))
		return;
	text.Trim();
	SeriesInfo& s = m_seriesRows[row];
	if (col == 0)
		text.MakeUpper();   // 품번 접두어는 대문자로
	CString& field = (col == 0) ? s.name : (col == 1) ? s.label : s.desc;
	if (field == text)
	{
		if (col == 0 && text.IsEmpty() && s.label.IsEmpty() && s.desc.IsEmpty())
		{
			m_seriesRows.erase(m_seriesRows.begin() + row);   // 새로 만들었다 비운 줄
			FillGrid();
		}
		return;
	}
	if (col == 0)
	{
		for (size_t i = 0; i < m_seriesRows.size(); ++i)
			if (static_cast<int>(i) != row && !text.IsEmpty() && m_seriesRows[i].name.CompareNoCase(text) == 0)
			{
				AfxMessageBox(L"같은 품번이 이미 있습니다.", MB_ICONWARNING);
				return;
			}
		// 다른 제작사 · 레이블에 있는 품번이면 옮길지 확인
		if (!text.IsEmpty())
		{
			std::vector<const NamedInfo*> all;
			for (const NamedInfo& o : m_lib.studios) all.push_back(&o);
			for (const NamedInfo& o : m_lib.labelInfos) all.push_back(&o);
			const NamedInfo* me = ItemOf(m_cur);
			for (const NamedInfo* o : all)
			{
				if (o == me)
					continue;
				for (const CString& nm : CVideoLibrary::SeriesNames(o->series))
					if (nm.CompareNoCase(text) == 0)
					{
						CString q;
						q.Format(L"품번 %s 은(는) '%s' 에 등록되어 있습니다.\n이 항목으로 옮길까요?", static_cast<LPCWSTR>(text), static_cast<LPCWSTR>(o->name));
						if (AfxMessageBox(q, MB_YESNO | MB_ICONQUESTION) != IDYES)
							return;
						break;
					}
			}
		}
	}
	field = text;
	if (col == 0 && text.IsEmpty() && s.label.IsEmpty() && s.desc.IsEmpty())
		m_seriesRows.erase(m_seriesRows.begin() + row);   // 빈 줄은 없앰
	FillGrid();
	Changed();
}

void CSeriesDlg::DeleteRow(int row)
{
	EndCellEdit(true);
	if (row < 0 || row >= static_cast<int>(m_seriesRows.size()))
		return;
	m_seriesRows.erase(m_seriesRows.begin() + row);
	FillGrid();
	Changed();
}

void CSeriesDlg::OnGridDblClk(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	if (m_cur < 0)
		return;
	const NMITEMACTIVATE* p = reinterpret_cast<NMITEMACTIVATE*>(pNMHDR);
	LVHITTESTINFO hit = {};
	hit.pt = p->ptAction;
	m_grid.SubItemHitTest(&hit);
	int row = hit.iItem, col = (hit.iSubItem >= 0) ? hit.iSubItem : 0;
	if (row < 0 || row >= static_cast<int>(m_seriesRows.size()))
	{
		// "+ 새 품번" 줄 (또는 빈 곳): 새 줄을 만들고 품번 입력
		m_seriesRows.push_back(SeriesInfo());
		FillGrid();
		row = static_cast<int>(m_seriesRows.size()) - 1;
		col = 0;
	}
	BeginCellEdit(row, col);
}

void CSeriesDlg::OnGridKeyDown(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	const NMLVKEYDOWN* k = reinterpret_cast<NMLVKEYDOWN*>(pNMHDR);
	const int row = m_grid.GetNextItem(-1, LVNI_SELECTED);
	if (k->wVKey == VK_DELETE)
		DeleteRow(row);
	else if (k->wVKey == VK_F2 && row >= 0 && row < static_cast<int>(m_seriesRows.size()))
		BeginCellEdit(row, 0);
}

void CSeriesDlg::OnGridRClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	if (m_cur < 0)
		return;
	const NMITEMACTIVATE* p = reinterpret_cast<NMITEMACTIVATE*>(pNMHDR);
	const int row = (p->iItem >= 0 && p->iItem < static_cast<int>(m_seriesRows.size())) ? p->iItem : -1;
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, 1, L"품번 추가");
	menu.AppendMenu(MF_STRING, 4, L"웹페이지 내용 붙여넣기 (일괄 추가)...");
	if (row >= 0)
	{
		menu.AppendMenu(MF_STRING, 2, L"셀 편집\tF2");
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, 3, L"품번 삭제\tDel");
	}
	CPoint pt;
	::GetCursorPos(&pt);
	const UINT cmd = menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, this);
	if (cmd == 1)
	{
		m_seriesRows.push_back(SeriesInfo());
		FillGrid();
		BeginCellEdit(static_cast<int>(m_seriesRows.size()) - 1, 0);
	}
	else if (cmd == 2)
	{
		LVHITTESTINFO hit = {};
		hit.pt = p->ptAction;
		m_grid.SubItemHitTest(&hit);
		BeginCellEdit(row, hit.iSubItem >= 0 ? hit.iSubItem : 0);
	}
	else if (cmd == 3)
		DeleteRow(row);
	else if (cmd == 4)
		PasteFromWeb();
}

void CSeriesDlg::OnBnClickedPaste()
{
	PasteFromWeb();
}

void CSeriesDlg::PasteFromWeb()
{
	if (m_cur < 0)
		return;
	EndCellEdit(true);
	CTextInfoDlg dlg(L"품번 일괄 추가 - 웹페이지 내용 붙여넣기",
		L"사이트의 시리즈 / 품번 목록(표)을 드래그해 복사(Ctrl+C)한 뒤 붙여넣으세요(Ctrl+V). 한 줄에 하나씩 읽습니다.\r\n"
		L"각 줄에서 품번(예: SONE-479, SSIS)을 찾아 접두어를 품번 칸에, 그다음 칸을 라벨, 나머지를 설명으로 넣습니다. 이미 있는 품번은 빈 칸만 채웁니다.",
		CString(), this);
	dlg.m_pasteButton = true;   // [공백라인 제거]
	if (dlg.DoModal() != IDOK)
		return;

	CString text = dlg.m_text;
	text.Replace(L"\r\n", L"\n");
	text.Replace(L'\r', L'\n');
	text.Replace(L'\x00A0', L' ');
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
		for (SeriesInfo& s : m_seriesRows)
			if (s.name.CompareNoCase(prefix) == 0) { found = &s; break; }
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
			m_seriesRows.push_back(s);
			++added;
		}
	}
	if (added + updated > 0)
	{
		FillGrid();
		Changed();
	}
	CString msg;
	msg.Format(L"시리즈 %d개를 추가하고 %d개를 보완했습니다.%s\n\n[확인]을 누르면 저장됩니다.", added, updated,
		skipped > 0 ? static_cast<LPCWSTR>(CString(L"\n(품번을 찾지 못한 줄은 건너뜀)")) : L"");
	AfxMessageBox(msg, MB_ICONINFORMATION);
}

void CSeriesDlg::OnCellEditKillFocus()
{
	EndCellEdit(true);
}

BOOL CSeriesDlg::PreTranslateMessage(MSG* pMsg)
{
	// 셀 편집 중: Enter = 저장, Esc = 취소, Tab = 다음 셀
	if (pMsg->message == WM_KEYDOWN && m_editRow >= 0 && pMsg->hwnd == m_cellEdit.GetSafeHwnd())
	{
		if (pMsg->wParam == VK_RETURN) { EndCellEdit(true); m_grid.SetFocus(); return TRUE; }
		if (pMsg->wParam == VK_ESCAPE) { EndCellEdit(false); m_grid.SetFocus(); return TRUE; }
		if (pMsg->wParam == VK_TAB)
		{
			const int row = m_editRow;
			const int col = m_editCol + ((::GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
			EndCellEdit(true);
			if (row < static_cast<int>(m_seriesRows.size()) && col >= 0 && col <= 2)
				BeginCellEdit(row, col);
			return TRUE;
		}
	}
	return CDialogEx::PreTranslateMessage(pMsg);
}

void CSeriesDlg::OnEnChangeSearch()
{
	EndCellEdit(true);
	CommitCurrent();
	FillList(m_cur);
	if (m_list.GetNextItem(-1, LVNI_SELECTED) < 0)
		ShowItem(-1);
}

void CSeriesDlg::OnLvnItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	const NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	if (m_loading || !(p->uChanged & LVIF_STATE) || !(p->uNewState & LVIS_SELECTED) || (p->uOldState & LVIS_SELECTED))
		return;
	if (p->iItem >= 0 && p->iItem < static_cast<int>(m_rows.size()) && m_rows[p->iItem] != m_cur)
		ShowItem(m_rows[p->iItem]);
}

void CSeriesDlg::OnOK()
{
	EndCellEdit(true);
	CommitCurrent();
	m_changed = m_anyChange;
	CDialogEx::OnOK();
}

void CSeriesDlg::OnCancel()
{
	EndCellEdit(false);
	CommitCurrent();
	if (m_anyChange)
	{
		if (AfxMessageBox(L"바꾼 품번을 저장하지 않고 닫을까요?", MB_YESNO | MB_ICONQUESTION) != IDYES)
			return;
		// 처음 상태로 되돌림 (이 창에서는 항목 추가 · 삭제가 없으므로 인덱스 그대로)
		for (size_t i = 0; i < m_lib.studios.size() && i < m_backupStudios.size(); ++i)
			m_lib.studios[i].series = m_backupStudios[i];
		for (size_t i = 0; i < m_lib.labelInfos.size() && i < m_backupLabels.size(); ++i)
			m_lib.labelInfos[i].series = m_backupLabels[i];
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
