#include "pch.h"
#include "RuliManager.h"
#include "NameListDlg.h"
#include "BulkNameDlg.h"
#include "TextInfoDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	std::vector<int> SortedNamedIndices(const std::vector<NamedInfo>& list, const CString& lowerQuery)
	{
		std::vector<int> rows;
		for (size_t i = 0; i < list.size(); ++i)
		{
			if (!lowerQuery.IsEmpty())
			{
				CString hay = list[i].name;
				hay.MakeLower();
				if (hay.Find(lowerQuery) < 0)
					continue;
			}
			rows.push_back(static_cast<int>(i));
		}
		std::sort(rows.begin(), rows.end(), [&](int a, int b)
		{
			return ::StrCmpLogicalW(list[a].name, list[b].name) < 0;
		});
		return rows;
	}
}

// ===========================================================================
// CNameListDlg (제작사 / 태그 관리)

CNameListDlg::CNameListDlg(CVideoLibrary& lib, int kind, const CString& selectName, CWnd* pParent)
	: CDialogEx(IDD_NAMELIST, pParent), m_lib(lib), m_kind(kind), m_selectName(selectName)
{
}

void CNameListDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_NL_SEARCH, m_editSearch);
	DDX_Control(pDX, IDC_NL_LIST, m_list);
	DDX_Control(pDX, IDC_NL_IMAGE, m_image);
	DDX_Control(pDX, IDC_NL_NAME, m_editName);
	DDX_Control(pDX, IDC_NL_COUNT, m_staticCount);
	DDX_Control(pDX, IDC_NL_IMAGE_PATH, m_staticImagePath);
	DDX_Control(pDX, IDC_NL_MEMO, m_editMemo);
	DDX_Control(pDX, IDC_NL_SUB, m_editSub);
	DDX_Control(pDX, IDC_NL_URLS, m_editUrls);
	DDX_Control(pDX, IDC_NL_EN, m_editEn);
	DDX_Control(pDX, IDC_NL_JA, m_editJa);
}

BEGIN_MESSAGE_MAP(CNameListDlg, CDialogEx)
	ON_WM_CTLCOLOR()
	ON_EN_CHANGE(IDC_NL_SEARCH, &CNameListDlg::OnEnChangeSearch)
	ON_EN_CHANGE(IDC_NL_NAME, &CNameListDlg::OnFieldChanged)
	ON_EN_CHANGE(IDC_NL_MEMO, &CNameListDlg::OnFieldChanged)
	ON_EN_CHANGE(IDC_NL_SUB, &CNameListDlg::OnFieldChanged)
	ON_EN_CHANGE(IDC_NL_URLS, &CNameListDlg::OnFieldChanged)
	ON_EN_CHANGE(IDC_NL_EN, &CNameListDlg::OnFieldChanged)
	ON_EN_CHANGE(IDC_NL_JA, &CNameListDlg::OnFieldChanged)
	ON_BN_CLICKED(IDC_NL_KIND_STUDIO, &CNameListDlg::OnKindStudio)
	ON_BN_CLICKED(IDC_NL_KIND_LABEL, &CNameListDlg::OnKindLabel)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_NL_LIST, &CNameListDlg::OnLvnItemChanged)
	ON_BN_CLICKED(IDC_NL_NEW, &CNameListDlg::OnBnClickedNew)
	ON_BN_CLICKED(IDC_NL_DELETE, &CNameListDlg::OnBnClickedDelete)
	ON_BN_CLICKED(IDC_NL_BULK, &CNameListDlg::OnBnClickedBulk)
	ON_BN_CLICKED(IDC_NL_SAVE, &CNameListDlg::OnBnClickedSave)
	ON_BN_CLICKED(IDC_NL_IMG_BROWSE, &CNameListDlg::OnBnClickedImageBrowse)
	ON_BN_CLICKED(IDC_NL_IMG_CLEAR, &CNameListDlg::OnBnClickedImageClear)
END_MESSAGE_MAP()

BOOL CNameListDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	// 종류 [제작사] [레이블]: 토글 버튼 (선택 #137CBD, 나머지 #394B59) - 테마 적용 전에 연결
	CDarkButton* kindBtns[] = { &m_radioStudio, &m_radioLabel };
	const UINT kindIds[] = { IDC_NL_KIND_STUDIO, IDC_NL_KIND_LABEL };
	for (int i = 0; i < 2; ++i)
	{
		if (kindBtns[i]->Attach(kindIds[i], this))
		{
			kindBtns[i]->SetColors(DarkColors::Button, RGB(255, 255, 255), DarkColors::Back);
			kindBtns[i]->SetToggle(DarkColors::Combo, RGB(190, 200, 208));
		}
	}
	m_theme.Apply(this, { IDC_NL_DELETE });   // 메인 창과 같은 색상 (삭제 버튼은 빨간색)
	m_image.SetBackColor(DarkColors::Preview);

	// 레이블 / 시리즈 칩 입력 (같은 자리, 항목 종류에 따라 하나만 보임)
	{
		CRect area;
		GetDlgItem(IDC_NL_CHIPS_AREA)->GetWindowRect(&area);
		ScreenToClient(&area);
		GetDlgItem(IDC_NL_CHIPS_AREA)->ShowWindow(SW_HIDE);
		m_chipsRect1 = area;
		CTagChipCtrl* chips[] = { &m_labelChips };
		const UINT ids[] = { IDC_NL_LABEL_CHIPS };
		for (int i = 0; i < 1; ++i)
		{
			chips[i]->Create(this, ids[i]);
			chips[i]->SetColors(DarkColors::Edit, RGB(0xCE, 0xD9, 0xE0), RGB(0x18, 0x20, 0x26), RGB(0xA7, 0xB6, 0xC2), DarkColors::Text);
			chips[i]->m_edit.SetColors(DarkColors::Edit, DarkColors::Text, DarkColors::Button, RGB(140, 155, 168));
			chips[i]->MoveWindow(&area);
			chips[i]->SetWindowPos(GetDlgItem(IDC_NL_CHIPS_AREA), 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);   // 탭 순서: 상위 칸 다음
			chips[i]->m_onChanged = [this]() { OnFieldChanged(); };
			CTagChipCtrl* ctrl = chips[i];
			chips[i]->m_onDropDown = [ctrl]() { ctrl->m_edit.ShowAll(); };
		}
		m_labelChips.m_edit.SetCueBanner(L"레이블 입력 (↓ 목록)");
		SetupChipIcons(m_labelChips, true);   // 레이블 칩: 레이블 이미지

		// 상위 제작사: 칩 하나 (자리 표시 칸 위치에), 등록된 제작사만 · 비우면 상위 없음
		CRect prc;
		GetDlgItem(IDC_NL_PARENT)->GetWindowRect(&prc);
		ScreenToClient(&prc);
		m_parentRect = prc;
		GetDlgItem(IDC_NL_PARENT_LBL)->GetWindowRect(&m_parentLblRect);
		ScreenToClient(&m_parentLblRect);
		GetDlgItem(IDC_NL_CHIPS_LBL)->GetWindowRect(&m_chipsLblRect);
		ScreenToClient(&m_chipsLblRect);
		GetDlgItem(IDC_NL_PARENT)->ShowWindow(SW_HIDE);
		m_parentChips.Create(this, IDC_NL_PARENT_CHIPS);
		m_parentChips.SetColors(DarkColors::Edit, RGB(0xCE, 0xD9, 0xE0), RGB(0x18, 0x20, 0x26), RGB(0xA7, 0xB6, 0xC2), DarkColors::Text);
		m_parentChips.m_edit.SetColors(DarkColors::Edit, DarkColors::Text, DarkColors::Button, RGB(140, 155, 168));
		m_parentChips.m_edit.SetCueBanner(L"상위 제작사 (비우면 없음, ↓ 목록)");
		m_parentChips.MoveWindow(&prc);
		m_parentChips.SetWindowPos(GetDlgItem(IDC_NL_PARENT), 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		m_parentChips.m_onDropDown = [this]() { m_parentChips.m_edit.ShowAll(); };
		SetupChipIcons(m_parentChips, false);   // 상위 칩: 제작사 이미지
		m_parentChips.m_edit.Setup([this](std::vector<SuggestItem>& out)
		{
			const CString cur = m_parentChips.Tags().empty() ? CString() : m_parentChips.Tags()[0];
			for (int i : SortedNamedIndices(m_lib.studios, CString()))
			{
				const NamedInfo& st = m_lib.studios[i];
				if (st.name.CompareNoCase(cur) == 0 || (!m_parentExclude.IsEmpty() && st.name.CompareNoCase(m_parentExclude) == 0))
					continue;
				const std::vector<CString> subs = CVideoLibrary::SplitLines(st.subName);
				CString subShown;
				for (const CString& s : subs) { if (!subShown.IsEmpty()) subShown += L" / "; subShown += s; }
				out.push_back({ subShown.IsEmpty() ? st.name : (st.name + L"\t" + subShown), st.name });
				for (const CString& s : subs)
					out.push_back({ s + L"\t" + st.name + L"의 서브이름", st.name });
			}
		}, false);
		m_parentChips.m_onChanged = [this]()
		{
			// 하나만 · 등록된 제작사만 (서브이름으로 입력하면 제작사 이름으로), 자기 자신은 안 됨
			std::vector<CString> tags = m_parentChips.Tags();
			if (tags.size() > 1)
				tags.erase(tags.begin(), tags.end() - 1);
			if (!tags.empty())
			{
				const int sn = m_lib.FindStudioLoose(tags[0]);
				if (sn < 0 || (!m_parentExclude.IsEmpty() && m_lib.studios[sn].name.CompareNoCase(m_parentExclude) == 0))
				{
					::MessageBeep(MB_ICONWARNING);
					tags.clear();   // 없는 제작사는 상위로 지정할 수 없음 (먼저 제작사로 등록)
				}
				else
					tags[0] = m_lib.studios[sn].name;
			}
			m_parentChips.SetTags(tags);
			OnFieldChanged();
		};
		// 레이블 후보: 다른 레이블 전부 (오른쪽에 지금 상위) - 고르면 저장할 때 이 제작사 아래로 옮김, 새 이름은 새 레이블
		m_labelChips.m_edit.Setup([this](std::vector<SuggestItem>& out)
		{
			std::set<CString> used;
			for (const CString& t : m_labelChips.Tags()) { CString k = t; k.MakeLower(); used.insert(k); }
			for (const NamedInfo& lb : m_lib.labelInfos)
			{
				CString k = lb.name;
				k.MakeLower();
				if (used.count(k))
					continue;
				const bool hasParent = m_lib.FindNamed(LIST_STUDIO, lb.parent) >= 0;
				out.push_back({ lb.name + L"\t" + (hasParent ? L"상위: " + lb.parent : CString(L"상위 없음")), lb.name });
			}
		}, false);
	}

	SetWindowText(m_kind == LIST_STUDIO ? CString(L"제작사 / 레이블 관리") : KindName() + L" 관리");
	SetDlgItemText(IDC_NL_NEW, L"새 " + KindName());

	CRect r(0, 0, 100, 0);
	MapDialogRect(&r);
	const int dlu = r.right;

	m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
	m_list.InsertColumn(0, L"이름", LVCFMT_LEFT, dlu * 115 / 100);
	m_list.InsertColumn(1, L"영상 수", LVCFMT_RIGHT, dlu * 40 / 100);

	m_editSearch.SetCueBanner(L"이름 검색");
	m_image.SetPlaceholder(L"이미지 없음");

	if (m_kind == LIST_TAG)
	{
		// 태그는 이미지·메모 없음: 이미지 영역을 숨기고 이름/영상 수를 위로 올림
		const UINT imageIds[] = { IDC_NL_IMAGE, IDC_NL_IMG_BROWSE, IDC_NL_IMG_CLEAR, IDC_NL_IMAGE_PATH };
		for (UINT id : imageIds)
			GetDlgItem(id)->ShowWindow(SW_HIDE);

		CRect imageRc, nameLblRc;
		GetDlgItem(IDC_NL_IMAGE)->GetWindowRect(&imageRc);
		GetDlgItem(IDC_NL_NAME_LBL)->GetWindowRect(&nameLblRc);
		const int dy = nameLblRc.top - imageRc.top - 2;   // 올릴 거리 (픽셀)

		// 태그는 종류(제작사/레이블) · 상위 · 레이블 줄이 없음: 그 줄들을 숨기고 영상 수를 종류 줄 자리로
		//  (서브이름 칸은 태그의 "다른 이름" 으로 사용 - 텍스트 파일 · 웹페이지 붙여넣기 · 태그 입력에서 그 태그로 인식)
		SetDlgItemText(IDC_NL_SUB_LBL, L"다른 이름\n(줄마다)");
		{
			CRect subLbl, cntLbl, cnt;
			GetDlgItem(IDC_NL_CHIPS_LBL)->GetWindowRect(&subLbl);  ScreenToClient(&subLbl);   // 영어 · 일본어 줄(종류 · 상위 자리) 아래
			GetDlgItem(IDC_NL_COUNT_LBL)->GetWindowRect(&cntLbl); ScreenToClient(&cntLbl);
			GetDlgItem(IDC_NL_COUNT)->GetWindowRect(&cnt);       ScreenToClient(&cnt);
			const int up = cntLbl.top - subLbl.top;
			cntLbl.OffsetRect(0, -up);
			cnt.OffsetRect(0, -up);
			GetDlgItem(IDC_NL_COUNT_LBL)->MoveWindow(&cntLbl);
			GetDlgItem(IDC_NL_COUNT)->MoveWindow(&cnt);
			const UINT hideIds[] = { IDC_NL_KIND_LBL, IDC_NL_KIND_STUDIO, IDC_NL_KIND_LABEL, IDC_NL_PARENT_LBL, IDC_NL_PARENT, IDC_NL_PARENT_CHIPS,
				IDC_NL_CHIPS_LBL, IDC_NL_LABEL_CHIPS };
			for (UINT id : hideIds)
				GetDlgItem(id)->ShowWindow(SW_HIDE);
		}

		const UINT moveIds[] = { IDC_NL_NAME_LBL, IDC_NL_NAME, IDC_NL_SUB_LBL, IDC_NL_SUB, IDC_NL_EN_LBL, IDC_NL_EN, IDC_NL_JA_LBL, IDC_NL_JA, IDC_NL_COUNT_LBL, IDC_NL_COUNT };
		for (UINT id : moveIds)
		{
			CWnd* w = GetDlgItem(id);
			CRect rc;
			w->GetWindowRect(&rc);
			ScreenToClient(&rc);
			rc.OffsetRect(0, -dy);
			w->MoveWindow(&rc);
		}
		// 태그는 메모 · 링크도 없음
		GetDlgItem(IDC_NL_MEMO_LBL)->ShowWindow(SW_HIDE);
		m_editMemo.ShowWindow(SW_HIDE);
		GetDlgItem(IDC_NL_URLS_LBL)->ShowWindow(SW_HIDE);
		m_editUrls.ShowWindow(SW_HIDE);
	}

	if (m_kind != LIST_TAG)
	{
		// 영어 · 일본어 이름은 태그만 (제작사 관리에서는 종류 · 상위 줄 자리라 숨김)
		const UINT ids[] = { IDC_NL_EN_LBL, IDC_NL_EN, IDC_NL_JA_LBL, IDC_NL_JA };
		for (UINT id : ids)
			GetDlgItem(id)->ShowWindow(SW_HIDE);
	}
	else
	{
		m_editEn.SetCueBanner(L"English name");
		m_editJa.SetCueBanner(L"日本語名");
	}

	m_counts = m_lib.CountNamed(m_kind);
	FillList(m_selectName);
	if (m_cur < 0)
		ShowItem(-1);
	return TRUE;
}

int CNameListDlg::CountOf(const CString& name) const
{
	CString key = name;
	key.MakeLower();
	auto it = m_counts.find(key);
	return it != m_counts.end() ? it->second : 0;
}

bool CNameListDlg::ValidCode(int code) const
{
	if (code < 0)
		return false;
	if (IsLabelCode(code))
		return m_kind == LIST_STUDIO && code - kLabelBase < static_cast<int>(m_lib.labelInfos.size());
	return code < static_cast<int>(m_lib.NamedList(m_kind).size());
}

NamedInfo* CNameListDlg::ItemOf(int code)
{
	if (!ValidCode(code))
		return nullptr;
	return IsLabelCode(code) ? &m_lib.labelInfos[code - kLabelBase] : &Items()[code];
}

int CNameListDlg::CodeOf(const CString& name, bool label) const
{
	if (name.IsEmpty())
		return -1;
	if (label)
	{
		for (size_t i = 0; i < m_lib.labelInfos.size(); ++i)
			if (m_lib.labelInfos[i].name.CompareNoCase(name) == 0)
				return kLabelBase + static_cast<int>(i);
		return -1;
	}
	const std::vector<NamedInfo>& list = m_lib.NamedList(m_kind);
	for (size_t i = 0; i < list.size(); ++i)
		if (list[i].name.CompareNoCase(name) == 0)
			return static_cast<int>(i);
	return -1;
}

CString CNameListDlg::RowText(int code) const
{
	if (code < 0)
		return CString();
	if (IsLabelCode(code))
	{
		const NamedInfo& lb = m_lib.labelInfos[code - kLabelBase];
		if (m_lib.FindNamed(LIST_STUDIO, lb.parent) < 0)
			return lb.name + L"  (레이블 · 상위 없음)";
		return L"      └ " + lb.name;   // 상위 제작사 아래 들여쓰기
	}
	return m_lib.NamedList(m_kind)[code].name;
}

int CNameListDlg::CountOfCode(int code) const
{
	if (code < 0)
		return 0;
	if (IsLabelCode(code))
		return m_lib.CountLabelVideos(m_lib.labelInfos[code - kLabelBase].name);
	return CountOf(m_lib.NamedList(m_kind)[code].name);
}

void CNameListDlg::FillList(const CString& selectName)
{
	int code = CodeOf(selectName, false);
	if (code < 0 && m_kind == LIST_STUDIO)
		code = CodeOf(selectName, true);   // 제작사에 없으면 레이블 이름
	FillListCode(code);
}

void CNameListDlg::FillListCode(int selectCode)
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
		hay.MakeLower();
		return hay.Find(query) >= 0;
	};

	m_rows.clear();
	const std::vector<int> sorted = SortedNamedIndices(Items(), CString());
	if (m_kind == LIST_STUDIO)
	{
		// 제작사 (이름 순) 바로 아래에 그 레이블 (└, 이름 순). 검색: 제작사가 맞으면 레이블 전부, 레이블만 맞으면 제작사 + 맞는 레이블
		for (int si : sorted)
		{
			const NamedInfo& st = Items()[si];
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
		// 상위 제작사가 없는 레이블은 맨 아래 (이름 순)
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
	}
	else
	{
		for (int i : sorted)
			if (matches(Items()[i]))
				m_rows.push_back(i);
	}

	m_loading = true;
	m_list.SetRedraw(FALSE);
	m_list.DeleteAllItems();
	int selRow = -1;
	for (size_t row = 0; row < m_rows.size(); ++row)
	{
		const int code = m_rows[row];
		const int i = m_list.InsertItem(static_cast<int>(row), RowText(code));
		CString c;
		c.Format(L"%d", CountOfCode(code));
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

void CNameListDlg::FillParentCombo(const CString& exclude, const CString& select)
{
	// 상위 제작사 칩: 등록된 제작사면 칩 하나, 아니면 비움 (= 상위 없음)
	m_parentExclude = exclude;
	std::vector<CString> tags;
	const int sn = select.IsEmpty() ? -1 : m_lib.FindNamed(LIST_STUDIO, select);
	if (sn >= 0)
		tags.push_back(m_lib.studios[sn].name);
	m_parentChips.SetTags(tags);
}

void CNameListDlg::UpdateKindControls()
{
	if (m_kind != LIST_STUDIO)
		return;
	const BOOL enable = (m_cur >= 0);
	m_radioStudio.EnableWindow(enable);
	m_radioLabel.EnableWindow(enable);
	const BOOL isLabel = enable && m_radioLabel.IsChecked();
	m_parentChips.EnableWindow(isLabel);
	// 제작사(또는 선택 없음)일 때는 [상위] 줄 자체를 숨김
	m_parentChips.ShowWindow(isLabel ? SW_SHOW : SW_HIDE);
	GetDlgItem(IDC_NL_PARENT_LBL)->ShowWindow(isLabel ? SW_SHOW : SW_HIDE);
	if (!isLabel)
		m_parentChips.m_edit.HidePopup();
	GetDlgItem(IDC_NL_PARENT_LBL)->EnableWindow(isLabel);
	// 제작사: [레이블] 칩 + 그 아래 [시리즈] 칩, 레이블: [시리즈] 칩 (첫 줄 자리)
	// [레이블] 글자는 제작사일 때만 (시리즈 표는 글자 없이 전체 폭)
	GetDlgItem(IDC_NL_CHIPS_LBL)->ShowWindow(isLabel ? SW_HIDE : SW_SHOW);
	if (!isLabel)
	{
		// 제작사: [상위] 줄이 숨겨지므로 [레이블] 줄을 그 높이로 올림
		CRect lbl = m_chipsLblRect, chips = m_chipsRect1;
		lbl.OffsetRect(0, m_parentLblRect.top - lbl.top);
		chips.OffsetRect(0, m_parentRect.top - chips.top);
		GetDlgItem(IDC_NL_CHIPS_LBL)->MoveWindow(&lbl);
		m_labelChips.MoveWindow(&chips);
	}
	m_labelChips.ShowWindow(isLabel ? SW_HIDE : SW_SHOW);
	m_labelChips.EnableWindow(enable);
	m_labelChips.m_edit.HidePopup();
}

void CNameListDlg::ShowItem(int code)
{
	m_loading = true;
	m_cur = ValidCode(code) ? code : -1;
	const BOOL enable = (m_cur >= 0);

	if (const NamedInfo* np = ItemOf(m_cur))
	{
		const NamedInfo& n = *np;
		m_editName.SetWindowText(n.name);
		{
			CString subs = n.subName;
			subs.Replace(L"\r\n", L"\n");
			subs.Replace(L"\n", L"\r\n");   // 여러 줄 에디트: 한 줄에 하나
			m_editSub.SetWindowText(subs);
		}
		CString memo = n.memo;
		memo.Replace(L"\r\n", L"\n");
		memo.Replace(L"\n", L"\r\n");
		m_editMemo.SetWindowText(memo);
		m_editEn.SetWindowText(n.nameEn);
		m_editJa.SetWindowText(n.nameJa);
		{
			CString urls = CVideoLibrary::JoinUrls(CVideoLibrary::SplitUrls(n.urls));
			urls.Replace(L"\n", L"\r\n");   // 한 줄에 하나
			m_editUrls.SetWindowText(urls);
		}
		CString c;
		c.Format(L"%d편", CountOfCode(m_cur));
		m_staticCount.SetWindowText(c);
		SetImage(n.image);
		if (m_kind == LIST_STUDIO)
		{
			const bool label = IsLabelCode(m_cur);
			m_radioStudio.SetChecked(!label);
			m_radioLabel.SetChecked(label);
			FillParentCombo(label ? CString() : n.name, label ? n.parent : CString());
			m_labelChips.SetTags(label ? std::vector<CString>() : m_lib.LabelNamesOf(n.name));
		}
	}
	else
	{
		m_editName.SetWindowText(L"");
		m_editSub.SetWindowText(L"");
		m_editMemo.SetWindowText(L"");
		m_editUrls.SetWindowText(L"");
		m_editEn.SetWindowText(L"");
		m_editJa.SetWindowText(L"");
		m_staticCount.SetWindowText(L"");
		SetImage(CString());
		if (m_kind == LIST_STUDIO)
		{
			m_radioStudio.SetChecked(false);
			m_radioLabel.SetChecked(false);
			m_parentExclude.Empty();
			m_parentChips.SetTags({});
			m_labelChips.SetTags({});
		}
	}

	m_editName.EnableWindow(enable);
	m_editSub.EnableWindow(enable);
	m_editMemo.EnableWindow(enable);
	m_editUrls.EnableWindow(enable);
	m_editEn.EnableWindow(enable);
	m_editJa.EnableWindow(enable);
	GetDlgItem(IDC_NL_DELETE)->EnableWindow(enable);
	GetDlgItem(IDC_NL_IMG_BROWSE)->EnableWindow(enable);
	GetDlgItem(IDC_NL_IMG_CLEAR)->EnableWindow(enable);
	UpdateKindControls();

	m_dirty = false;
	GetDlgItem(IDC_NL_SAVE)->EnableWindow(FALSE);
	m_loading = false;
}

void CNameListDlg::SetImage(const CString& path)
{
	m_imagePath = path;
	if (m_kind == LIST_TAG)
		return;   // 태그는 이미지를 표시하지 않음
	m_image.Clear();
	if (path.IsEmpty())
	{
		m_image.SetPlaceholder(L"이미지 없음");
		m_staticImagePath.SetWindowText(L"");
	}
	else
	{
		if (!::PathFileExistsW(path))
			m_image.SetPlaceholder(L"이미지 파일을 찾을 수 없습니다.");
		else
			m_image.SetImageFile(path);
		m_staticImagePath.SetWindowText(path);
	}
}

void CNameListDlg::OnFieldChanged()
{
	if (m_loading || m_cur < 0)
		return;
	m_dirty = true;
	GetDlgItem(IDC_NL_SAVE)->EnableWindow(TRUE);
}

void CNameListDlg::OnKindChanged()
{
	if (m_loading || m_cur < 0)
		return;
	// 레이블로 바꾸면 상위 제작사 칩 사용 (비어 있으면 상위 없음)
	UpdateKindControls();
	OnFieldChanged();
}

void CNameListDlg::SetupChipIcons(CTagChipCtrl& chips, bool label)
{
	// 메인 창 제작사 칩과 같은 방식: 칩 높이에 맞춰 비율 유지, 넓은 로고는 높이의 3배 폭까지
	auto logoOf = [this, label](const CString& tag) -> Gdiplus::Bitmap*
	{
		if (!m_getLogo)
			return nullptr;
		if (label)
		{
			const int li = m_lib.FindLabel(tag);
			return (li >= 0 && !m_lib.labelInfos[li].image.IsEmpty()) ? m_getLogo(m_lib.labelInfos[li].image) : nullptr;
		}
		const int sn = m_lib.FindNamed(LIST_STUDIO, tag);
		return (sn >= 0 && !m_lib.studios[sn].image.IsEmpty()) ? m_getLogo(m_lib.studios[sn].image) : nullptr;
	};
	chips.m_iconWidth = [logoOf](const CString& tag, int h) -> int
	{
		Gdiplus::Bitmap* logo = logoOf(tag);
		if (!logo || logo->GetWidth() == 0 || logo->GetHeight() == 0 || h <= 0)
			return 0;
		const double w = h * static_cast<double>(logo->GetWidth()) / logo->GetHeight();
		return (std::max)(h / 2, (std::min)(h * 3, static_cast<int>(w + 0.5)));
	};
	chips.m_drawIcon = [logoOf](CDC* dc, const CString& tag, const CRect& rc)
	{
		Gdiplus::Bitmap* logo = logoOf(tag);
		if (!logo || logo->GetWidth() == 0 || logo->GetHeight() == 0 || rc.Width() <= 0 || rc.Height() <= 0)
			return;
		const double lw = logo->GetWidth(), lh = logo->GetHeight();
		const double scale = (std::min)(rc.Width() / lw, rc.Height() / lh);
		const int w = (std::max)(1, static_cast<int>(lw * scale));
		const int h = (std::max)(1, static_cast<int>(lh * scale));
		const int x = rc.left + (rc.Width() - w) / 2;
		const int y = rc.top + (rc.Height() - h) / 2;
		Gdiplus::Graphics g(dc->GetSafeHdc());
		g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
		g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
		g.DrawImage(logo, Gdiplus::Rect(x, y, w, h), 0, 0, static_cast<INT>(lw), static_cast<INT>(lh), Gdiplus::UnitPixel);
	};
}

void CNameListDlg::ApplyLabelChips(const CString& studioName)
{
	const std::vector<CString> want = m_labelChips.Tags();
	auto wanted = [&want](const CString& n)
	{
		for (const CString& w : want)
			if (w.CompareNoCase(n) == 0) return true;
		return false;
	};
	// 칩에서 뺀 레이블 → 상위 없음 (레이블 자체와 영상의 레이블은 그대로)
	for (int li : m_lib.LabelsOf(studioName))
		if (!wanted(m_lib.labelInfos[li].name))
			m_lib.SetLabelParent(li, CString());
	CString skipped;
	for (const CString& w : want)
	{
		const int code = CodeOf(w, true);
		if (code >= 0)
		{
			const int li = code - kLabelBase;
			if (m_lib.labelInfos[li].parent.CompareNoCase(studioName) != 0)
				m_lib.SetLabelParent(li, studioName);   // 다른 제작사 / 상위 없음 레이블 → 이 제작사 아래로
		}
		else if (m_lib.FindNamed(LIST_STUDIO, w) >= 0 || w.CompareNoCase(studioName) == 0)
		{
			if (!skipped.IsEmpty()) skipped += L", ";
			skipped += w;   // 제작사 이름과 같은 레이블은 만들 수 없음
		}
		else
		{
			NamedInfo info;
			info.name = w;
			info.parent = studioName;
			m_lib.labelInfos.push_back(info);   // 새 레이블
		}
	}
	if (!skipped.IsEmpty())
		AfxMessageBox(L"제작사 이름과 같아서 레이블로 추가하지 않았습니다: " + skipped, MB_ICONINFORMATION);
}

void CNameListDlg::OnKindStudio()
{
	if (m_loading || m_cur < 0 || m_radioStudio.IsChecked())
		return;
	m_radioStudio.SetChecked(true);
	m_radioLabel.SetChecked(false);
	OnKindChanged();
}

void CNameListDlg::OnKindLabel()
{
	if (m_loading || m_cur < 0 || m_radioLabel.IsChecked())
		return;
	m_radioStudio.SetChecked(false);
	m_radioLabel.SetChecked(true);
	OnKindChanged();
}

bool CNameListDlg::Commit()
{
	m_structChanged = false;
	if (!m_dirty || !ValidCode(m_cur))
		return true;

	const bool wasLabel = IsLabelCode(m_cur);
	const bool wantLabel = (m_kind == LIST_STUDIO && m_radioLabel.IsChecked());
	NamedInfo& n = *ItemOf(m_cur);

	CString name;
	m_editName.GetWindowText(name);
	name.Trim();
	CVideoLibrary::RemoveListCommas(name);   // 구분 쉼표는 사용 불가 (괄호 안 쉼표는 허용)
	if (name.IsEmpty())
	{
		AfxMessageBox(L"이름을 입력하세요.", MB_ICONWARNING);
		m_editName.SetFocus();
		return false;
	}
	// 같은 이름: 제작사끼리 · 레이블끼리 · 제작사와 레이블 사이 모두 불가
	bool dup = false;
	if (m_kind == LIST_STUDIO)
	{
		for (size_t i = 0; i < m_lib.studios.size() && !dup; ++i)
			if (!(!wasLabel && static_cast<int>(i) == m_cur) && m_lib.studios[i].name.CompareNoCase(name) == 0)
				dup = true;
		for (size_t i = 0; i < m_lib.labelInfos.size() && !dup; ++i)
			if (!(wasLabel && kLabelBase + static_cast<int>(i) == m_cur) && m_lib.labelInfos[i].name.CompareNoCase(name) == 0)
				dup = true;
	}
	else
	{
		const int other = m_lib.FindNamed(m_kind, name);
		dup = (other >= 0 && other != m_cur);
	}
	if (dup)
	{
		AfxMessageBox(L"같은 이름이 이미 있습니다. (제작사와 레이블도 서로 다른 이름이어야 합니다)", MB_ICONWARNING);
		m_editName.SetFocus();
		m_editName.SetSel(0, -1);
		return false;
	}

	CString parent;
	if (wantLabel)
	{
		if (!m_parentChips.Tags().empty())   // 비어 있으면 상위 제작사 없는 레이블
			parent = m_parentChips.Tags()[0];
		if (!wasLabel && !m_lib.LabelsOf(n.name).empty())
		{
			AfxMessageBox(L"하위 레이블이 있는 제작사는 레이블로 바꿀 수 없습니다.\n먼저 그 레이블들을 다른 제작사로 옮기거나 삭제하세요.", MB_ICONWARNING);
			return false;
		}
	}

	// 이름 변경 → 영상에도 반영
	if (name != n.name)
	{
		if (wasLabel)
			m_lib.RenameLabelInVideos(n.name, name);
		else
		{
			m_lib.RenameNamedInVideos(m_kind, n.name, name);   // 제작사: 레이블의 상위 이름도
			CString oldKey = n.name, newKey = name;
			oldKey.MakeLower();
			newKey.MakeLower();
			if (oldKey != newKey)
			{
				m_counts[newKey] = CountOf(n.name);
				m_counts.erase(oldKey);
			}
		}
	}
	n.name = name;
	{
		// 서브이름 (제작사 · 레이블) / 다른 이름 (태그): 한 줄에 하나 - 이름과 같은 것 · 빈 줄 · 중복은 뺌
		CString subs;
		m_editSub.GetWindowText(subs);
		std::vector<CString> list;
		for (const CString& sub : CVideoLibrary::SplitLines(subs))
			if (sub.CompareNoCase(name) != 0)
				list.push_back(sub);
		n.subName = CVideoLibrary::JoinLines(list);
	}
	if (m_kind == LIST_STUDIO)
	{
		m_editMemo.GetWindowText(n.memo);   // 태그는 메모 없음
		CString urls;
		m_editUrls.GetWindowText(urls);
		n.urls = CVideoLibrary::JoinUrls(CVideoLibrary::SplitUrls(urls));   // 공백 · 빈 줄 · 중복 정리
	}
	else
	{
		n.memo.Empty();
		n.urls.Empty();
	}
	if (m_kind == LIST_TAG)
	{
		// 태그: 영어 · 일본어 이름 (카드에 "한글 (영어, 일본어)", 텍스트 · 붙여넣기에서 이 이름도 그 태그로)
		m_editEn.GetWindowText(n.nameEn);
		n.nameEn.Trim();
		m_editJa.GetWindowText(n.nameJa);
		n.nameJa.Trim();
	}
	n.image = m_imagePath;
	// 종류 / 상위 제작사 변경 (영상의 제작사 · 레이블도 맞춤)
	int newCode = m_cur;
	if (m_kind == LIST_STUDIO)
	{
		if (!wasLabel && wantLabel)
		{
			newCode = kLabelBase + m_lib.StudioToLabel(m_cur, parent);
			m_structChanged = true;
		}
		else if (wasLabel && !wantLabel)
		{
			newCode = m_lib.LabelToStudio(m_cur - kLabelBase);
			m_structChanged = true;
		}
		else if (wasLabel && n.parent != parent)
		{
			m_lib.SetLabelParent(m_cur - kLabelBase, parent);
			m_structChanged = true;   // 목록 위치(상위 제작사 아래)가 바뀜
		}
		// 제작사: 레이블 칩 → 하위 레이블 지정 (목록 구조가 바뀜)
		if (!wantLabel)
		{
			const NamedInfo* st = ItemOf(newCode);
			if (st)
			{
				const std::vector<CString> before = m_lib.LabelNamesOf(st->name);
				ApplyLabelChips(st->name);
				if (before != m_lib.LabelNamesOf(st->name))
					m_structChanged = true;
				newCode = CodeOf(st->name, false);
			}
		}
		if (m_structChanged)
			m_counts = m_lib.CountNamed(m_kind);
	}

	m_dirty = false;
	m_changed = true;
	GetDlgItem(IDC_NL_SAVE)->EnableWindow(FALSE);

	if (m_structChanged)
	{
		m_cur = newCode;
		FillListCode(newCode);   // 목록 다시 (선택 항목 정보도 다시 표시)
		return true;
	}

	m_loading = true;
	m_editName.SetWindowText(n.name);
	m_loading = false;

	for (size_t row = 0; row < m_rows.size(); ++row)
	{
		if (m_rows[row] == m_cur)
		{
			m_list.SetItemText(static_cast<int>(row), 0, RowText(m_cur));
			break;
		}
	}
	return true;
}

void CNameListDlg::OnLvnItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	*pResult = 0;
	if (m_loading)
		return;

	if ((p->uChanged & LVIF_STATE) &&
		(p->uNewState & LVIS_SELECTED) && !(p->uOldState & LVIS_SELECTED) &&
		p->iItem >= 0 && p->iItem < static_cast<int>(m_rows.size()))
	{
		const int code = m_rows[p->iItem];
		if (code == m_cur)
			return;
		// 저장으로 목록 구조가 바뀔 수 있으므로 고른 항목을 이름 + 종류로 기억
		const NamedInfo* target = ItemOf(code);
		const CString targetName = target ? target->name : CString();
		const bool targetLabel = IsLabelCode(code);
		if (!Commit())
		{
			// 이름 오류: 원래 항목 선택으로 되돌림
			m_loading = true;
			for (size_t row = 0; row < m_rows.size(); ++row)
			{
				if (m_rows[row] == m_cur)
				{
					m_list.SetItemState(static_cast<int>(row), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
					break;
				}
			}
			m_loading = false;
			return;
		}
		if (m_structChanged)
		{
			FillListCode(CodeOf(targetName, targetLabel));
			return;
		}
		ShowItem(code);
	}
}

void CNameListDlg::OnEnChangeSearch()
{
	if (!Commit())
		return;
	FillListCode(m_cur);
}

void CNameListDlg::OnBnClickedNew()
{
	if (!Commit())
		return;

	const CString base = L"새 " + KindName();
	CString name = base;
	for (int n = 2; m_lib.FindNamed(m_kind, name) >= 0 || (m_kind == LIST_STUDIO && m_lib.FindLabel(name) >= 0); ++n)
		name.Format(L"%s %d", static_cast<LPCWSTR>(base), n);

	NamedInfo info;
	info.name = name;
	Items().push_back(info);
	m_changed = true;

	m_editSearch.SetWindowText(L"");
	FillList(name);
	m_editName.SetFocus();
	m_editName.SetSel(0, -1);
}

void CNameListDlg::OnBnClickedDelete()
{
	if (!ValidCode(m_cur))
		return;

	const bool label = IsLabelCode(m_cur);
	const CString name = ItemOf(m_cur)->name;
	const int count = CountOfCode(m_cur);
	const CString kindName = label ? CString(L"레이블") : KindName();
	CString msg;
	if (count > 0)
		msg.Format(L"%s '%s'을(를) 삭제할까요?\n\n연결된 동영상 %d개에서도 빠집니다.",
			static_cast<LPCWSTR>(kindName), static_cast<LPCWSTR>(name), count);
	else
		msg.Format(L"%s '%s'을(를) 삭제할까요?", static_cast<LPCWSTR>(kindName), static_cast<LPCWSTR>(name));
	if (!label && m_kind == LIST_STUDIO)
	{
		const size_t lbs = m_lib.LabelsOf(name).size();
		if (lbs > 0)
		{
			CString more;
			more.Format(L"\n\n이 제작사의 레이블 %d개도 함께 삭제됩니다.", static_cast<int>(lbs));
			msg += more;
		}
	}
	if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;

	if (label)
		m_lib.RemoveLabel(m_cur - kLabelBase);
	else
	{
		m_lib.RemoveNamedFromVideos(m_kind, name);   // 제작사: 그 레이블도 지움
		Items().erase(Items().begin() + m_cur);
		CString key = name;
		key.MakeLower();
		m_counts.erase(key);
	}
	m_changed = true;
	m_dirty = false;
	m_cur = -1;

	FillList(CString());
	ShowItem(-1);
}

void CNameListDlg::OnBnClickedBulk()
{
	if (!Commit())
		return;

	CBulkNameDlg dlg(m_lib, m_kind, this);
	if (dlg.DoModal() != IDOK || dlg.m_added.empty())
		return;

	m_changed = true;
	m_counts = m_lib.CountNamed(m_kind);   // 영상에 이미 쓰이던 이름이면 영상 수도 표시
	m_editSearch.SetWindowText(L"");       // 검색 해제 → 목록 갱신
	FillList(dlg.m_added.front());

	CString msg;
	msg.Format(L"%s %d개를 등록했습니다.", static_cast<LPCWSTR>(KindName()), static_cast<int>(dlg.m_added.size()));
	AfxMessageBox(msg, MB_ICONINFORMATION);
}

void CNameListDlg::OnBnClickedSave()
{
	Commit();
}

void CNameListDlg::OnBnClickedImageBrowse()
{
	if (m_cur < 0)
		return;

	CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
		L"이미지 파일 (*.jpg;*.jpeg;*.png;*.webp;*.bmp;*.gif;*.tif;*.tiff)|*.jpg;*.jpeg;*.png;*.webp;*.bmp;*.gif;*.tif;*.tiff|모든 파일 (*.*)|*.*||",
		this);
	if (dlg.DoModal() != IDOK)
		return;

	// 원본 대신 실행 폴더의 Image\studios 에 복사본을 만들어 그것을 등록 (레이블도 같은 폴더, 태그는 이미지 없음)
	if (m_kind != LIST_STUDIO)
		return;
	const CString copy = CVideoLibrary::StoreImageCopy(dlg.GetPathName(), L"studios");
	if (copy.IsEmpty())
	{
		AfxMessageBox(L"이미지를 DB 폴더로 복사하지 못했습니다.", MB_ICONWARNING);
		return;
	}
	SetImage(copy);
	OnFieldChanged();
}

void CNameListDlg::OnBnClickedImageClear()
{
	if (m_cur < 0)
		return;
	SetImage(CString());
	OnFieldChanged();
}

void CNameListDlg::OnOK()
{
	Commit();   // Enter: 저장만 (메모 칸에서는 줄바꿈)
}

void CNameListDlg::OnCancel()
{
	if (!Commit())
		return;
	CDialogEx::OnCancel();
}

// ===========================================================================
// CNamePickDlg (스튜디오 / 태그 선택)

CNamePickDlg::CNamePickDlg(CVideoLibrary& lib, int kind, const CString& current, CWnd* pParent)
	: CDialogEx(IDD_NAME_PICK, pParent), m_lib(lib), m_kind(kind), m_multi(kind == LIST_TAG)
{
	m_order = CVideoLibrary::SplitList(current);
	if (!m_multi && m_order.size() > 1)
		m_order.resize(1);
}

void CNamePickDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_PICK_SEARCH, m_editSearch);
	DDX_Control(pDX, IDC_PICK_LIST, m_list);
	DDX_Control(pDX, IDC_PICK_NEWNAME, m_editNew);
	DDX_Control(pDX, IDC_PICK_SELECTED, m_staticSelected);
}

BEGIN_MESSAGE_MAP(CNamePickDlg, CDialogEx)
	ON_WM_CTLCOLOR()
	ON_EN_CHANGE(IDC_PICK_SEARCH, &CNamePickDlg::OnEnChangeSearch)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_PICK_LIST, &CNamePickDlg::OnLvnItemChanged)
	ON_NOTIFY(NM_DBLCLK, IDC_PICK_LIST, &CNamePickDlg::OnNmDblclk)
	ON_BN_CLICKED(IDC_PICK_ADD, &CNamePickDlg::OnBnClickedAdd)
END_MESSAGE_MAP()

BOOL CNamePickDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	m_theme.Apply(this);   // 메인 창과 같은 색상
	if (m_single)
	{
		m_multi = false;
		if (m_order.size() > 1)
			m_order.resize(1);
	}

	const CString kindName = (m_kind == LIST_STUDIO) ? L"제작사" : L"태그";
	SetWindowText(!m_caption.IsEmpty() ? m_caption :
		(m_kind == LIST_STUDIO ? CString(L"제작사 / 레이블") : kindName) + (m_multi ? L" 선택 (여러 개 가능)" : L" 선택 (하나만)"));
	SetDlgItemText(IDC_PICK_NEWLABEL, L"새 " + kindName);

	// 현재 값이 목록에 없으면 추가 (보통은 이미 동기화되어 있음)
	for (const CString& n : m_order)
	{
		if (m_lib.FindNamed(m_kind, n) < 0 && !(m_kind == LIST_STUDIO && m_lib.FindLabel(n) >= 0))   // 레이블 이름이면 그대로
		{
			NamedInfo info;
			info.name = n;
			m_lib.NamedList(m_kind).push_back(info);
			m_added = true;
		}
	}

	CRect rc;
	m_list.GetClientRect(&rc);
	m_list.SetExtendedStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	m_list.InsertColumn(0, L"이름", LVCFMT_LEFT, rc.Width() - ::GetSystemMetrics(SM_CXVSCROLL));

	m_editSearch.SetCueBanner(L"이름 검색");
	m_editNew.SetCueBanner(L"목록에 없는 이름");

	FillList();
	UpdateSelectedText();
	m_editSearch.SetFocus();
	return FALSE;
}

bool CNamePickDlg::IsChecked(const CString& name) const
{
	for (const CString& n : m_order)
	{
		if (n.CompareNoCase(name) == 0)
			return true;
	}
	return false;
}

void CNamePickDlg::SetChecked(const CString& name, bool checked)
{
	if (checked)
	{
		if (!m_multi)
			m_order.clear();   // 스튜디오는 하나만
		if (!IsChecked(name))
			m_order.push_back(name);
	}
	else
	{
		m_order.erase(std::remove_if(m_order.begin(), m_order.end(),
			[&](const CString& n) { return n.CompareNoCase(name) == 0; }), m_order.end());
	}
}

void CNamePickDlg::FillList()
{
	CString query;
	m_editSearch.GetWindowText(query);
	query.Trim();
	query.MakeLower();

	const std::vector<NamedInfo>& items = m_lib.NamedList(m_kind);

	// 행: (표시 글자, 데이터) - 제작사는 그 아래에 레이블(└)도
	std::vector<std::pair<CString, int>> rowsOut;
	if (m_kind == LIST_STUDIO)
	{
		auto hit = [&query](const CString& name)
		{
			if (query.IsEmpty()) return true;
			CString h = name;
			h.MakeLower();
			return h.Find(query) >= 0;
		};
		for (int si : SortedNamedIndices(items, CString()))
		{
			const bool stHit = hit(items[si].name);
			std::vector<int> lbs;
			for (int li : m_lib.LabelsOf(items[si].name))
				if (stHit || hit(m_lib.labelInfos[li].name))
					lbs.push_back(li);
			if (!stHit && lbs.empty())
				continue;
			rowsOut.push_back({ items[si].name, si });
			for (int li : lbs)
				rowsOut.push_back({ L"      └ " + m_lib.labelInfos[li].name, kPickLabelBase + li });
		}
		// 상위 제작사가 없는 레이블은 맨 아래
		for (size_t li = 0; li < m_lib.labelInfos.size(); ++li)
		{
			const NamedInfo& lb = m_lib.labelInfos[li];
			if (m_lib.FindNamed(LIST_STUDIO, lb.parent) < 0 && hit(lb.name))
				rowsOut.push_back({ lb.name + L"  (레이블)", kPickLabelBase + static_cast<int>(li) });
		}
	}
	else
	{
		for (int r : SortedNamedIndices(items, query))
			if (m_exclude.IsEmpty() || items[r].name.CompareNoCase(m_exclude) != 0)
				rowsOut.push_back({ items[r].name, r });
	}

	m_filling = true;
	m_list.SetRedraw(FALSE);
	m_list.DeleteAllItems();
	for (size_t row = 0; row < rowsOut.size(); ++row)
	{
		const int i = m_list.InsertItem(static_cast<int>(row), rowsOut[row].first);
		m_list.SetItemData(i, static_cast<DWORD_PTR>(rowsOut[row].second));
		m_list.SetCheck(i, IsChecked(NameOfData(rowsOut[row].second)) ? TRUE : FALSE);
	}
	m_list.SetRedraw(TRUE);
	m_list.Invalidate();
	m_filling = false;
}

CString CNamePickDlg::NameOfData(int data) const
{
	if (data >= kPickLabelBase)
	{
		const int li = data - kPickLabelBase;
		return (li < static_cast<int>(m_lib.labelInfos.size())) ? m_lib.labelInfos[li].name : CString();
	}
	const std::vector<NamedInfo>& items = m_lib.NamedList(m_kind);
	return (data >= 0 && data < static_cast<int>(items.size())) ? items[data].name : CString();
}

void CNamePickDlg::UpdateSelectedText()
{
	CString s = L"선택: ";
	s += m_order.empty() ? CString(L"(없음)") : CVideoLibrary::JoinList(m_order);
	m_staticSelected.SetWindowText(s);
}

void CNamePickDlg::OnEnChangeSearch()
{
	FillList();
}

void CNamePickDlg::OnLvnItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	*pResult = 0;
	if (m_filling || p->iItem < 0 || !(p->uChanged & LVIF_STATE))
		return;

	const UINT oldImg = p->uOldState & LVIS_STATEIMAGEMASK;
	const UINT newImg = p->uNewState & LVIS_STATEIMAGEMASK;
	if (oldImg == newImg || oldImg == 0 || newImg == 0)
		return;

	const CString itemName = NameOfData(static_cast<int>(m_list.GetItemData(p->iItem)));
	if (itemName.IsEmpty())
		return;

	const bool checked = (newImg == INDEXTOSTATEIMAGEMASK(2));
	SetChecked(itemName, checked);

	// 하나만 선택: 다른 체크 해제
	if (checked && !m_multi)
	{
		m_filling = true;
		for (int i = 0; i < m_list.GetItemCount(); ++i)
		{
			if (i != p->iItem && m_list.GetCheck(i))
				m_list.SetCheck(i, FALSE);
		}
		m_filling = false;
	}
	UpdateSelectedText();
}

void CNamePickDlg::OnNmDblclk(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMITEMACTIVATE* p = reinterpret_cast<NMITEMACTIVATE*>(pNMHDR);
	if (p->iItem >= 0)
		m_list.SetCheck(p->iItem, !m_list.GetCheck(p->iItem));
	*pResult = 0;
}

void CNamePickDlg::OnBnClickedAdd()
{
	CString name;
	m_editNew.GetWindowText(name);
	name.Trim();
	CVideoLibrary::RemoveListCommas(name);   // 괄호 안 쉼표는 허용
	if (name.IsEmpty())
	{
		m_editNew.SetFocus();
		return;
	}

	std::vector<NamedInfo>& items = m_lib.NamedList(m_kind);
	const int existing = m_lib.FindNamed(m_kind, name);
	if (existing >= 0)
	{
		name = items[existing].name;
	}
	else
	{
		NamedInfo info;
		info.name = name;
		items.push_back(info);
		m_added = true;
	}

	SetChecked(name, true);
	m_editNew.SetWindowText(L"");
	m_editSearch.SetWindowText(L"");
	FillList();
	UpdateSelectedText();

	for (int i = 0; i < m_list.GetItemCount(); ++i)
	{
		if (NameOfData(static_cast<int>(m_list.GetItemData(i))).CompareNoCase(name) == 0)
		{
			m_list.EnsureVisible(i, FALSE);
			m_list.SetItemState(i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			break;
		}
	}
}

void CNamePickDlg::OnOK()
{
	CWnd* focus = GetFocus();
	if (focus && focus->GetSafeHwnd() == m_editNew.GetSafeHwnd())
	{
		OnBnClickedAdd();
		return;
	}
	if (focus && focus->GetSafeHwnd() == m_editSearch.GetSafeHwnd())
		return;

	m_result = CVideoLibrary::JoinList(m_order);
	CDialogEx::OnOK();
}

HBRUSH CNameListDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
		return br;
	return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}

HBRUSH CNamePickDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
		return br;
	return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}
