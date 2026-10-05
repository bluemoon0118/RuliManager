#include "pch.h"
#include "RuliManager.h"
#include "ActorDlg.h"
#include "ImageSearchDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define WM_APP_MERGE_ACTORS (WM_APP + 41)

// 배우 목록 열
enum
{
	COL_NAME = 0, COL_GENDER, COL_AGE, COL_BIRTH, COL_NATION, COL_HEIGHT, COL_DEBUT, COL_RETIRE, COL_RATING, COL_COUNT
};

namespace
{
	bool ParseYmd(const CString& text, SYSTEMTIME& st)
	{
		int y = 0, m = 0, d = 0;
		if (text.IsEmpty() || swscanf_s(text, L"%d-%d-%d", &y, &m, &d) != 3)
			return false;
		if (y < 1601 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31)
			return false;
		st = {};
		st.wYear = static_cast<WORD>(y);
		st.wMonth = static_cast<WORD>(m);
		st.wDay = static_cast<WORD>(d);
		return true;
	}

	// 검색어가 이름 / 별칭에 포함되는지
	bool ActorMatches(const ActorInfo& a, const CString& lowerQuery)
	{
		if (lowerQuery.IsEmpty())
			return true;
		CString hay = a.name + L"\n" + a.aliases;
		hay.MakeLower();
		return hay.Find(lowerQuery) >= 0;
	}

	std::vector<int> SortedActorIndices(const CVideoLibrary& lib, const CString& lowerQuery)
	{
		std::vector<int> rows;
		for (size_t i = 0; i < lib.actors.size(); ++i)
		{
			if (ActorMatches(lib.actors[i], lowerQuery))
				rows.push_back(static_cast<int>(i));
		}
		std::sort(rows.begin(), rows.end(), [&](int a, int b)
		{
			return ::StrCmpLogicalW(lib.actors[a].name, lib.actors[b].name) < 0;
		});
		return rows;
	}
}

// ===========================================================================
// CActorDlg (배우 관리)

CActorDlg::CActorDlg(CVideoLibrary& lib, const CString& selectName, CWnd* pParent)
	: CDialogEx(IDD_ACTORS, pParent), m_lib(lib), m_selectName(selectName)
{
}

void CActorDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_ACT_SEARCH, m_editSearch);
	DDX_Control(pDX, IDC_ACT_LIST, m_list);
	DDX_Control(pDX, IDC_ACT_PHOTO, m_photo);
	DDX_Control(pDX, IDC_ACT_ALIASES, m_comboAliases);
	DDX_Control(pDX, IDC_ACT_GENDER, m_comboGender);
	DDX_Control(pDX, IDC_ACT_BIRTH, m_dateBirth);
	DDX_Control(pDX, IDC_ACT_AGE, m_staticAge);
	DDX_Control(pDX, IDC_ACT_NATIONALITY, m_comboNationality);
	DDX_Control(pDX, IDC_ACT_HEIGHT, m_editHeight);
	DDX_Control(pDX, IDC_ACT_BUST, m_editBust);
	DDX_Control(pDX, IDC_ACT_WAIST, m_editWaist);
	DDX_Control(pDX, IDC_ACT_HIP, m_editHip);
	DDX_Control(pDX, IDC_ACT_CUP, m_comboCup);
	DDX_Control(pDX, IDC_ACT_DEBUT, m_dateDebut);
	DDX_Control(pDX, IDC_ACT_RETIRE, m_dateRetire);
	DDX_Control(pDX, IDC_ACT_COUNT, m_staticCount);
	DDX_Control(pDX, IDC_ACT_PHOTO_PATH, m_staticPhotoPath);
	DDX_Control(pDX, IDC_ACT_MEMO, m_editMemo);
	DDX_Control(pDX, IDC_ACT_URLS, m_editUrls);
}

BEGIN_MESSAGE_MAP(CActorDlg, CDialogEx)
	ON_WM_CTLCOLOR()
	ON_EN_CHANGE(IDC_ACT_SEARCH, &CActorDlg::OnEnChangeSearch)
	ON_CBN_EDITCHANGE(IDC_ACT_ALIASES, &CActorDlg::OnFieldChanged)
	ON_CBN_SELCHANGE(IDC_ACT_ALIASES, &CActorDlg::OnCbnSelchangeAliases)
	ON_CBN_SELCHANGE(IDC_ACT_GENDER, &CActorDlg::OnFieldChanged)
	ON_EN_CHANGE(IDC_ACT_MEMO, &CActorDlg::OnFieldChanged)
	ON_EN_CHANGE(IDC_ACT_URLS, &CActorDlg::OnFieldChanged)
	ON_NOTIFY(DTN_DATETIMECHANGE, IDC_ACT_BIRTH, &CActorDlg::OnDtnBirthChanged)
	ON_NOTIFY(DTN_DATETIMECHANGE, IDC_ACT_DEBUT, &CActorDlg::OnDtnDebutChanged)
	ON_NOTIFY(DTN_DATETIMECHANGE, IDC_ACT_RETIRE, &CActorDlg::OnDtnDebutChanged)   // 같은 처리 (변경 표시)
	ON_CBN_SELCHANGE(IDC_ACT_NATIONALITY, &CActorDlg::OnFieldChanged)
	ON_EN_CHANGE(IDC_ACT_HEIGHT, &CActorDlg::OnFieldChanged)
	ON_EN_CHANGE(IDC_ACT_BUST, &CActorDlg::OnFieldChanged)
	ON_EN_CHANGE(IDC_ACT_WAIST, &CActorDlg::OnFieldChanged)
	ON_EN_CHANGE(IDC_ACT_HIP, &CActorDlg::OnFieldChanged)
	ON_CBN_SELCHANGE(IDC_ACT_CUP, &CActorDlg::OnFieldChanged)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_ACT_LIST, &CActorDlg::OnLvnItemChanged)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_ACT_LIST, &CActorDlg::OnNmCustomDrawList)
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_MESSAGE(WM_APP_MERGE_ACTORS, &CActorDlg::OnMergeActors)
	ON_BN_CLICKED(IDC_ACT_NEW, &CActorDlg::OnBnClickedNew)
	ON_BN_CLICKED(IDC_ACT_DELETE, &CActorDlg::OnBnClickedDelete)
	ON_BN_CLICKED(IDC_ACT_SAVE, &CActorDlg::OnBnClickedSave)
	ON_BN_CLICKED(IDC_ACT_PHOTO_BROWSE, &CActorDlg::OnBnClickedPhotoBrowse)
	ON_BN_CLICKED(IDC_ACT_PHOTO_CLEAR, &CActorDlg::OnBnClickedPhotoClear)
	ON_BN_CLICKED(IDC_ACT_PHOTO_SEARCH, &CActorDlg::OnBnClickedPhotoSearch)
END_MESSAGE_MAP()

BOOL CActorDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	m_theme.Apply(this, { IDC_ACT_DELETE });   // 메인 창과 같은 색상 (삭제 버튼은 빨간색)
	{
		CDarkDateTime* dates[] = { &m_dateBirth, &m_dateDebut, &m_dateRetire };
		for (CDarkDateTime* d : dates)
			d->SetColors(DarkColors::Edit, DarkColors::Text, DarkColors::Border);
		CClientDC dc(this);
		CFont* oldFont = dc.SelectObject(GetFont());
		TEXTMETRIC tm = {};
		dc.GetTextMetrics(&tm);
		dc.SelectObject(oldFont);
		const int itemH = tm.tmHeight + 6;
		m_comboGender.SetColors(DarkColors::Combo, DarkColors::Text, DarkColors::SelBack);
		m_comboGender.SetHeights(itemH - 2, itemH);
		m_comboCup.SetColors(DarkColors::Combo, DarkColors::Text, DarkColors::SelBack);
		m_comboCup.SetHeights(itemH - 2, itemH);
		m_comboNationality.SetColors(DarkColors::Combo, DarkColors::Text, DarkColors::SelBack);
		m_photo.SetBackColor(DarkColors::Preview);
	}
	// 별점: 리소스의 자리 표시용 칸을 별점 컨트롤로 바꿈
	if (CWnd* holder = GetDlgItem(IDC_ACT_RATING))
	{
		CRect rr;
		holder->GetWindowRect(&rr);
		ScreenToClient(&rr);
		holder->DestroyWindow();
		m_starRating.Create(this, IDC_ACT_RATING, 5);
		m_starRating.SetWindowPos(&m_dateRetire, rr.left, rr.top, rr.Width(), rr.Height(), SWP_NOACTIVATE);   // 탭 순서: 은퇴일 다음
		m_starRating.SetColors(DarkColors::Back, RGB(0xFF, 0xC8, 0x1E), RGB(0xE6, 0xEA, 0xEE), DarkColors::Text);
		m_starRating.m_onChanged = [this](int) { OnFieldChanged(); };
	}

	CRect r(0, 0, 100, 0);
	MapDialogRect(&r);
	const int dlu = r.right;   // 100 DLU 의 픽셀 폭

	m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
	// 배우 정보 전체를 목록에서 볼 수 있도록 열 구성
	m_list.InsertColumn(COL_NAME,    L"이름",     LVCFMT_LEFT,  dlu * 140 / 100);
	m_list.InsertColumn(COL_GENDER,  L"성별",     LVCFMT_CENTER, dlu * 24 / 100);
	m_list.InsertColumn(COL_AGE,     L"나이",     LVCFMT_RIGHT, dlu * 26 / 100);
	m_list.InsertColumn(COL_BIRTH,   L"생년월일", LVCFMT_LEFT,  dlu * 48 / 100);
	m_list.InsertColumn(COL_NATION,  L"국적",     LVCFMT_LEFT,  dlu * 62 / 100);
	m_list.InsertColumn(COL_HEIGHT,  L"키",       LVCFMT_RIGHT, dlu * 42 / 100);
	m_list.InsertColumn(COL_DEBUT,   L"데뷔일",   LVCFMT_LEFT,  dlu * 48 / 100);
	m_list.InsertColumn(COL_RETIRE,  L"은퇴일",   LVCFMT_LEFT,  dlu * 48 / 100);
	m_list.InsertColumn(COL_RATING,  L"별점",     LVCFMT_LEFT,  dlu * 40 / 100);
	m_list.InsertColumn(COL_COUNT,   L"출연",     LVCFMT_RIGHT, dlu * 26 / 100);
	{
		// 국적 칸: 글자 앞에 국기(24px + 여백)가 들어갈 만큼 공백을 넣고, 그 자리에 국기를 그림
		CClientDC dc(&m_list);
		CFont* old = dc.SelectObject(m_list.GetFont());
		const int spaceW = (std::max)(1, static_cast<int>(dc.GetTextExtent(L" ").cx));
		dc.SelectObject(old);
		const int n = (CCountryCombo::kFlagW + 8 + spaceW - 1) / spaceW;
		m_flagPad = CString(L' ', n);
	}

	m_editSearch.SetCueBanner(L"이름 · 별칭 검색");
	for (int i = 0; i < CVideoLibrary::GenderCount(); ++i)   // 0 = 미지정, 여성 / 남성 / 트랜스젠더 여성 / 트랜스젠더 남성 / 인터섹스 / 논바이너리
		m_comboGender.AddString(CVideoLibrary::GenderAt(i));
	m_comboCup.AddString(L"");          // 미지정
	for (wchar_t c = L'A'; c <= L'Q'; ++c)
		m_comboCup.AddString(CString(c));
	m_editBust.SetCueBanner(L"B");
	m_editWaist.SetCueBanner(L"W");
	m_editHip.SetCueBanner(L"H");
	m_dateBirth.SetFormat(L"yyyy-MM-dd");
	m_dateDebut.SetFormat(L"yyyy-MM-dd");
	m_dateRetire.SetFormat(L"yyyy-MM-dd");
	m_comboNationality.FillCountries();

	// 별칭 펼침 목록: 스핀(▲▼)으로 순서를 바꾸면 저장 대상, 고른 별칭은 기억
	// 이름 콤보 (대표 이름 + 별칭): 고르거나 순서를 바꾸거나 지우면 [저장] 대상
	m_comboAliases.m_onReordered = [this]() { OnFieldChanged(); };
	m_comboAliases.m_onPicked = [this]() { OnCbnSelchangeAliases(); };
	m_comboAliases.m_onDeleted = [this](const CString&)
	{
		UpdateAliasCue();
		OnFieldChanged();
	};
	m_photo.SetPlaceholder(L"사진 없음");

	// 창 크기 조절용: 각 컨트롤의 처음 위치와 따라갈 방향 기억
	{
		CRect client, listRc;
		GetClientRect(&client);
		m_initClient = client.Size();
		CRect wr;
		GetWindowRect(&wr);
		m_minTrack = wr.Size();
		m_list.GetWindowRect(&listRc);
		ScreenToClient(&listRc);
		const int splitX = listRc.right;        // 이보다 오른쪽 = 정보 칸
		const int bottomY = listRc.bottom;      // 이보다 아래 = 아래쪽 버튼 줄
		for (CWnd* w = GetWindow(GW_CHILD); w; w = w->GetWindow(GW_HWNDNEXT))
		{
			CRect rc;
			w->GetWindowRect(&rc);
			ScreenToClient(&rc);
			int mode = 0;
			const int id = w->GetDlgCtrlID();
			if (id == IDC_ACT_LIST)                    mode = 1 | 2;          // 가로·세로로 늘어남
			else if (id == IDC_ACT_SEARCH)             mode = 1;              // 가로로 늘어남
			else if (id == IDC_ACT_MEMO)               mode = 4 | 2;          // 오른쪽에 붙고 세로로 늘어남
			else if (rc.left >= splitX && rc.top >= bottomY) mode = 4 | 8;    // 저장/닫기: 오른쪽 아래
			else if (rc.left >= splitX)                mode = 4;              // 정보 칸: 오른쪽에 붙음
			else if (rc.top >= bottomY)                mode = 8;              // 새 배우/삭제: 아래에 붙음
			m_anchors.push_back({ w->GetSafeHwnd(), rc, mode });
		}
	}

	FillList(m_selectName);
	if (m_cur < 0)
		ShowActor(-1);
	return TRUE;
}

void CActorDlg::FillList(const CString& selectName)
{
	CString query;
	m_editSearch.GetWindowText(query);
	query.Trim();
	query.MakeLower();

	m_rows = SortedActorIndices(m_lib, query);

	// 배우별 출연작 수 (동영상을 한 번만 훑음, 별칭 표기도 해당 배우로 셈)
	const std::map<CString, int> nameIndex = m_lib.ActorNameIndex();
	std::map<CString, int> counts;
	for (const VideoItem& v : m_lib.items)
	{
		std::set<CString> seen;
		for (const CString& n : CVideoLibrary::SplitList(v.actors))
		{
			const CString key = m_lib.ActorKeyOf(nameIndex, n);
			if (seen.insert(key).second)
				++counts[key];
		}
	}

	m_loading = true;
	m_list.SetRedraw(FALSE);
	m_list.DeleteAllItems();
	int selRow = -1;
	for (size_t row = 0; row < m_rows.size(); ++row)
	{
		const ActorInfo& a = m_lib.actors[m_rows[row]];
		const int i = m_list.InsertItem(static_cast<int>(row), a.name);
		SetRowText(i, a);
		CString c;
		CString key = a.name;
		key.MakeLower();
		auto it = counts.find(key);
		c.Format(L"%d", it != counts.end() ? it->second : 0);
		m_list.SetItemText(i, COL_COUNT, c);
		if (!selectName.IsEmpty() && a.name.CompareNoCase(selectName) == 0)
			selRow = i;
	}
	m_list.SetRedraw(TRUE);
	m_list.Invalidate();
	m_loading = false;

	if (selRow >= 0)
	{
		m_list.SetItemState(selRow, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_list.EnsureVisible(selRow, FALSE);
		ShowActor(m_rows[selRow]);
	}
}

void CActorDlg::ShowActor(int idx)
{
	m_loading = true;
	m_cur = (idx >= 0 && idx < static_cast<int>(m_lib.actors.size())) ? idx : -1;
	const BOOL enable = (m_cur >= 0);

	if (m_cur >= 0)
	{
		const ActorInfo& a = m_lib.actors[m_cur];
		FillAliases(a.name + L"," + a.aliases, a.name);   // 이름 콤보: 대표 이름(= 마지막으로 고른 이름) + 별칭
		SYSTEMTIME st = {};
		if (ParseYmd(a.birth, st))
			m_dateBirth.SetTime(&st);
		else
			m_dateBirth.SetTime(static_cast<LPSYSTEMTIME>(nullptr));
		m_comboNationality.SetCountry(a.nationality);
		{
			int gsel = 0;
			for (int i = 1; i < CVideoLibrary::GenderCount(); ++i)
				if (a.gender == CVideoLibrary::GenderAt(i)) { gsel = i; break; }
			m_comboGender.SetCurSel(gsel);
		}
		m_editHeight.SetWindowText(a.height);
		m_editBust.SetWindowText(a.bust);
		m_editWaist.SetWindowText(a.waist);
		m_editHip.SetWindowText(a.hip);
		{
			const int cupSel = a.cup.IsEmpty() ? 0 : m_comboCup.FindStringExact(0, a.cup);
			m_comboCup.SetCurSel(cupSel < 0 ? 0 : cupSel);
		}
		if (ParseYmd(a.debut, st))
			m_dateDebut.SetTime(&st);
		else
			m_dateDebut.SetTime(static_cast<LPSYSTEMTIME>(nullptr));
		if (ParseYmd(a.retire, st))
			m_dateRetire.SetTime(&st);
		else
			m_dateRetire.SetTime(static_cast<LPSYSTEMTIME>(nullptr));
		m_starRating.SetRating(a.rating);
		CString memo = a.memo;
		memo.Replace(L"\r\n", L"\n");
		memo.Replace(L"\n", L"\r\n");
		m_editMemo.SetWindowText(memo);
		{
			CString urls = CVideoLibrary::JoinUrls(CVideoLibrary::SplitUrls(a.urls));
			urls.Replace(L"\n", L"\r\n");   // 한 줄에 하나
			m_editUrls.SetWindowText(urls);
		}
		CString c;
		c.Format(L"%d편", m_lib.CountVideosWithActor(a.name));
		m_staticCount.SetWindowText(c);
		SetPhoto(a.photo);
	}
	else
	{
		FillAliases(CString(), CString());
		m_dateBirth.SetTime(static_cast<LPSYSTEMTIME>(nullptr));
		m_comboNationality.SetCurSel(0);
		m_comboGender.SetCurSel(0);
		m_editHeight.SetWindowText(L"");
		m_editBust.SetWindowText(L"");
		m_editWaist.SetWindowText(L"");
		m_editHip.SetWindowText(L"");
		m_comboCup.SetCurSel(0);
		m_dateDebut.SetTime(static_cast<LPSYSTEMTIME>(nullptr));
		m_dateRetire.SetTime(static_cast<LPSYSTEMTIME>(nullptr));
		m_starRating.SetRating(0);
		m_editMemo.SetWindowText(L"");
		m_editUrls.SetWindowText(L"");
		m_staticCount.SetWindowText(L"");
		SetPhoto(CString());
	}

	m_comboAliases.EnableWindow(enable);
	m_dateBirth.EnableWindow(enable);
	m_comboNationality.EnableWindow(enable);
	m_comboGender.EnableWindow(enable);
	m_editHeight.EnableWindow(enable);
	m_editBust.EnableWindow(enable);
	m_editWaist.EnableWindow(enable);
	m_editHip.EnableWindow(enable);
	m_comboCup.EnableWindow(enable);
	m_dateDebut.EnableWindow(enable);
	m_dateRetire.EnableWindow(enable);
	m_starRating.EnableWindow(enable);
	m_editMemo.EnableWindow(enable);
	m_editUrls.EnableWindow(enable);
	UpdateAgeText();
	GetDlgItem(IDC_ACT_DELETE)->EnableWindow(enable);
	GetDlgItem(IDC_ACT_PHOTO_BROWSE)->EnableWindow(enable);
	GetDlgItem(IDC_ACT_PHOTO_CLEAR)->EnableWindow(enable);
	GetDlgItem(IDC_ACT_PHOTO_SEARCH)->EnableWindow(enable);

	m_dirty = false;
	GetDlgItem(IDC_ACT_SAVE)->EnableWindow(FALSE);
	m_loading = false;
}

void CActorDlg::SetPhoto(const CString& path)
{
	m_photoPath = path;
	m_photo.Clear();
	if (path.IsEmpty())
	{
		m_photo.SetPlaceholder(L"사진 없음");
		m_staticPhotoPath.SetWindowText(L"");
	}
	else
	{
		if (!::PathFileExistsW(path))
			m_photo.SetPlaceholder(L"사진 파일을 찾을 수 없습니다.");
		else
			m_photo.SetImageFile(path);
		m_staticPhotoPath.SetWindowText(path);
	}
}

void CActorDlg::OnFieldChanged()
{
	if (m_loading || m_cur < 0)
		return;
	m_dirty = true;
	GetDlgItem(IDC_ACT_SAVE)->EnableWindow(TRUE);
}

void CActorDlg::UpdateAgeText()
{
	SYSTEMTIME st = {};
	CString text;
	if (m_cur >= 0 && m_dateBirth.GetTime(&st) == GDT_VALID)
	{
		CString birth;
		birth.Format(L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
		const int age = CVideoLibrary::CalcAge(birth);
		if (age >= 0)
			text.Format(L"만 %d세", age);
	}
	m_staticAge.SetWindowText(text);
}

void CActorDlg::OnDtnDebutChanged(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	OnFieldChanged();
	*pResult = 0;
}

void CActorDlg::OnDtnBirthChanged(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	UpdateAgeText();
	OnFieldChanged();
	*pResult = 0;
}

bool CActorDlg::Commit()
{
	if (!m_dirty || m_cur < 0 || m_cur >= static_cast<int>(m_lib.actors.size()))
		return true;

	ActorInfo& a = m_lib.actors[m_cur];

	// 이름과 별칭은 하나의 목록: 콤보에 보이는(마지막으로 고른) 이름이 대표 이름, 나머지는 별칭
	const std::vector<CString> names = CVideoLibrary::SplitList(GetAliases());
	CString name;
	m_comboAliases.GetWindowText(name);
	name.Trim();
	CVideoLibrary::RemoveListCommas(name);   // 구분 쉼표는 사용 불가 (괄호 안 쉼표는 이름의 일부로 허용)
	if (name.IsEmpty() && !names.empty())
		name = names[0];
	if (name.IsEmpty())
	{
		AfxMessageBox(L"배우 이름을 입력하세요.", MB_ICONWARNING);
		m_comboAliases.SetFocus();
		return false;
	}
	for (const CString& s : names)
		if (s.CompareNoCase(name) == 0) { name = s; break; }   // 목록의 표기 사용
	const int other = m_lib.FindActor(name);
	if (other >= 0 && other != m_cur)
	{
		AfxMessageBox(L"같은 이름의 배우가 이미 있습니다.", MB_ICONWARNING);
		m_comboAliases.SetFocus();
		m_comboAliases.SetEditSel(0, -1);
		return false;
	}

	if (name != a.name)
		m_lib.RenameActorInVideos(a.name, name);   // 동영상의 배우 이름도 대표 이름으로 변경
	a.name = name;
	{
		// 별칭: 대표 이름(또는 앞의 별칭)과 언어 단위로 같은 이름은 넣지 않음
		//  예: 대표 이름 "나기 히카루(Hikaru Nagi, 凪ひかる)" → "나기 히카루(凪ひかる)" 는 같은 이름이므로 무시
		std::vector<CString> others;
		for (const CString& s : names)
		{
			if (s.CompareNoCase(name) == 0 || CVideoLibrary::SameNameByLang(s, name))
				continue;
			bool dup = false;
			for (const CString& o : others)
				if (CVideoLibrary::SameNameByLang(s, o)) { dup = true; break; }
			if (!dup)
				others.push_back(s);
		}
		a.aliases = CVideoLibrary::JoinList(others);
	}
	// 영상에서 마지막으로 고른 별칭: 별칭 목록에 없으면 해제
	{
		bool keep = false;
		for (const CString& s : CVideoLibrary::SplitList(a.aliases))
			if (s.CompareNoCase(a.lastAlias) == 0) { keep = true; break; }
		if (!keep)
			a.lastAlias.Empty();
	}
	// 별칭과 같은 이름으로 따로 등록된 배우(폴더 스캔 등으로 생긴 것)는 합칠지 나중에 확인
	for (const CString& s : CVideoLibrary::SplitList(a.aliases))
	{
		const int o = m_lib.FindActor(s);
		if (o >= 0 && o != m_cur)
			m_mergeNames.push_back(m_lib.actors[o].name);
	}
	if (!m_mergeNames.empty())
		PostMessage(WM_APP_MERGE_ACTORS);   // 목록 알림 처리 중에 목록을 바꾸지 않도록 미룸

	SYSTEMTIME st = {};
	if (m_dateBirth.GetTime(&st) == GDT_VALID)
		a.birth.Format(L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
	else
		a.birth.Empty();

	a.nationality = m_comboNationality.GetCountry();

	const int g = m_comboGender.GetCurSel();
	a.gender = CVideoLibrary::GenderAt(g);   // 0(미지정) / 범위 밖 → ""

	m_editHeight.GetWindowText(a.height);
	a.height.Trim();
	m_editBust.GetWindowText(a.bust);
	a.bust.Trim();
	m_editWaist.GetWindowText(a.waist);
	a.waist.Trim();
	m_editHip.GetWindowText(a.hip);
	a.hip.Trim();
	{
		const int cupSel = m_comboCup.GetCurSel();
		a.cup.Empty();
		if (cupSel > 0)
			m_comboCup.GetLBText(cupSel, a.cup);
	}

	if (m_dateDebut.GetTime(&st) == GDT_VALID)
		a.debut.Format(L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
	else
		a.debut.Empty();

	if (m_dateRetire.GetTime(&st) == GDT_VALID)
		a.retire.Format(L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
	else
		a.retire.Empty();

	a.rating = m_starRating.GetRating();

	m_editMemo.GetWindowText(a.memo);
	{
		CString urls;
		m_editUrls.GetWindowText(urls);
		a.urls = CVideoLibrary::JoinUrls(CVideoLibrary::SplitUrls(urls));   // 공백 · 빈 줄 · 중복 정리
	}
	a.photo = m_photoPath;

	m_dirty = false;
	m_changed = true;
	GetDlgItem(IDC_ACT_SAVE)->EnableWindow(FALSE);

	m_loading = true;
	FillAliases(a.name + L"," + a.aliases, a.name);
	m_loading = false;

	UpdateRow(m_cur);
	return true;
}

void CActorDlg::UpdateRow(int idx)
{
	for (size_t row = 0; row < m_rows.size(); ++row)
	{
		if (m_rows[row] == idx)
		{
			const ActorInfo& a = m_lib.actors[idx];
			const int r = static_cast<int>(row);
			SetRowText(r, a);
			return;
		}
	}
}

void CActorDlg::OnLvnItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	*pResult = 0;
	if (m_loading)
		return;

	if ((p->uChanged & LVIF_STATE) &&
		(p->uNewState & LVIS_SELECTED) && !(p->uOldState & LVIS_SELECTED) &&
		p->iItem >= 0 && p->iItem < static_cast<int>(m_rows.size()))
	{
		const int idx = m_rows[p->iItem];
		if (idx == m_cur)
			return;
		if (!Commit())
		{
			// 이름 오류: 원래 배우 선택으로 되돌림
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
		ShowActor(idx);
	}
}

void CActorDlg::SetRowText(int row, const ActorInfo& a)
{
	m_list.SetItemText(row, COL_NAME, a.name);
	m_list.SetItemText(row, COL_GENDER, a.gender);
	const int age = CVideoLibrary::CalcAge(a.birth);
	CString ageText;
	if (age >= 0) ageText.Format(L"%d세", age);
	m_list.SetItemText(row, COL_AGE, ageText);
	m_list.SetItemText(row, COL_BIRTH, a.birth);
	m_list.SetItemText(row, COL_NATION, NationalityCell(a.nationality));
	m_list.SetItemText(row, COL_HEIGHT, a.height.IsEmpty() ? CString() : a.height + L"cm");
	m_list.SetItemText(row, COL_DEBUT, a.debut);
	m_list.SetItemText(row, COL_RETIRE, a.retire);
	{
		CString stars;
		for (int i = 0; i < 5 && a.rating > 0; ++i)
			stars += (i < a.rating) ? L'★' : L'☆';
		m_list.SetItemText(row, COL_RATING, stars);
	}
}

void CActorDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialogEx::OnSize(nType, cx, cy);
	if (m_anchors.empty() || nType == SIZE_MINIMIZED)
		return;
	const int dx = (std::max)(0, cx - static_cast<int>(m_initClient.cx));   // CSize 는 LONG
	const int dy = (std::max)(0, cy - static_cast<int>(m_initClient.cy));
	HDWP hdwp = ::BeginDeferWindowPos(static_cast<int>(m_anchors.size()));
	for (const Anchor& an : m_anchors)
	{
		if (an.mode == 0 || !::IsWindow(an.hwnd))
			continue;
		CRect rc = an.rc;
		if (an.mode & 4) rc.OffsetRect(dx, 0);
		if (an.mode & 8) rc.OffsetRect(0, dy);
		if (an.mode & 1) rc.right += dx;
		if (an.mode & 2) rc.bottom += dy;
		if (hdwp)
		{
			// 옮기기만 하는 컨트롤은 크기를 건드리지 않음 (콤보박스의 펼침 높이 유지)
			const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | ((an.mode & 3) ? 0 : SWP_NOSIZE);
			hdwp = ::DeferWindowPos(hdwp, an.hwnd, nullptr, rc.left, rc.top, rc.Width(), rc.Height(), flags);
		}
	}
	if (hdwp)
		::EndDeferWindowPos(hdwp);
	Invalidate();
}

void CActorDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	CDialogEx::OnGetMinMaxInfo(lpMMI);
	if (m_minTrack.cx > 0)
	{
		lpMMI->ptMinTrackSize.x = m_minTrack.cx;   // 처음 크기보다 작게는 줄이지 않음
		lpMMI->ptMinTrackSize.y = m_minTrack.cy;
	}
}

CString CActorDlg::NationalityCell(const CString& nationality) const
{
	if (CCountryCombo::FindCountry(nationality) >= 0)
		return m_flagPad + nationality;
	return nationality;   // 국기가 없는 값은 그대로
}

void CActorDlg::OnNmCustomDrawList(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLVCUSTOMDRAW* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(pNMHDR);
	*pResult = CDRF_DODEFAULT;
	switch (cd->nmcd.dwDrawStage)
	{
	case CDDS_PREPAINT:
		*pResult = CDRF_NOTIFYITEMDRAW;
		break;
	case CDDS_ITEMPREPAINT:
		*pResult = CDRF_NOTIFYSUBITEMDRAW;
		break;
	case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
		if (cd->iSubItem == COL_NATION)
			*pResult = CDRF_NOTIFYPOSTPAINT;   // 글자(앞 공백 포함)는 기본으로 그리고, 끝난 뒤 국기를 덧그림
		break;
	case CDDS_ITEMPOSTPAINT | CDDS_SUBITEM:
		if (cd->iSubItem == COL_NATION)
		{
			const int row = static_cast<int>(cd->nmcd.dwItemSpec);
			CString text = m_list.GetItemText(row, COL_NATION);
			text.Trim();
			const int flag = CCountryCombo::FindCountry(text);
			if (flag >= 0)
			{
				CRect rc;
				m_list.GetSubItemRect(row, COL_NATION, LVIR_LABEL, rc);
				const int h = (std::min)(static_cast<int>(CCountryCombo::kFlagH), rc.Height() - 2);
				CDC* dc = CDC::FromHandle(cd->nmcd.hdc);
				CCountryCombo::DrawFlag(dc, flag, rc.left + 6, rc.top + (rc.Height() - h) / 2, h);
			}
		}
		break;
	}
}

void CActorDlg::OnEnChangeSearch()
{
	if (!Commit())
		return;
	const CString keep = (m_cur >= 0) ? m_lib.actors[m_cur].name : CString();
	FillList(keep);
}

void CActorDlg::OnBnClickedNew()
{
	if (!Commit())
		return;

	CString name = L"새 배우";
	for (int n = 2; m_lib.FindActor(name) >= 0; ++n)
		name.Format(L"새 배우 %d", n);

	ActorInfo a;
	a.name = name;
	m_lib.actors.push_back(a);
	m_changed = true;

	m_editSearch.SetWindowText(L"");   // 검색 해제 (EN_CHANGE 로 목록 갱신)
	FillList(name);
	m_comboAliases.SetFocus();
	m_comboAliases.SetEditSel(0, -1);
}

void CActorDlg::OnBnClickedDelete()
{
	if (m_cur < 0)
		return;

	const CString name = m_lib.actors[m_cur].name;
	const int count = m_lib.CountVideosWithActor(name);
	CString msg;
	if (count > 0)
		msg.Format(L"배우 '%s'을(를) 삭제할까요?\n\n이 배우가 연결된 동영상 %d개에서도 빠집니다.",
			static_cast<LPCWSTR>(name), count);
	else
		msg.Format(L"배우 '%s'을(를) 삭제할까요?", static_cast<LPCWSTR>(name));
	if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;

	m_lib.RemoveActorFromVideos(name);
	m_lib.actors.erase(m_lib.actors.begin() + m_cur);
	m_changed = true;
	m_dirty = false;
	m_cur = -1;

	FillList(CString());
	ShowActor(-1);
}

void CActorDlg::OnBnClickedSave()
{
	Commit();
}

LRESULT CActorDlg::OnMergeActors(WPARAM, LPARAM)
{
	std::vector<CString> names;
	names.swap(m_mergeNames);
	if (names.empty() || !Commit())
		return 0;

	const CString curName = (m_cur >= 0) ? m_lib.actors[m_cur].name : CString();
	bool merged = false;
	for (const CString& name : names)
	{
		const int o = m_lib.FindActor(name);
		if (o < 0)
			continue;
		// 이 이름을 별칭으로 가진 배우
		int owner = -1;
		for (size_t i = 0; i < m_lib.actors.size() && owner < 0; ++i)
		{
			if (static_cast<int>(i) == o) continue;
			for (const CString& s : CVideoLibrary::SplitList(m_lib.actors[i].aliases))
				if (s.CompareNoCase(name) == 0) { owner = static_cast<int>(i); break; }
		}
		if (owner < 0)
			continue;

		const CString ownerName = m_lib.actors[owner].name;
		CString msg;
		msg.Format(L"'%s' 배우가 따로 등록되어 있습니다.\n\n"
			L"'%s'의 별칭으로 합칠까요?\n(따로 등록된 '%s' 배우 정보는 삭제되고, '%s'(으)로 표기된 영상 %d개는 '%s'에 연결됩니다)",
			static_cast<LPCWSTR>(name), static_cast<LPCWSTR>(ownerName), static_cast<LPCWSTR>(name),
			static_cast<LPCWSTR>(name), m_lib.CountVideosWithActor(name), static_cast<LPCWSTR>(ownerName));
		if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION) != IDYES)
			continue;

		// 영상의 표기는 그대로 두고(별칭으로 연결됨) 별도 배우 항목만 삭제
		ActorInfo& dst = m_lib.actors[owner];
		const ActorInfo& src = m_lib.actors[o];
		if (dst.photo.IsEmpty() && !src.photo.IsEmpty())
			dst.photo = src.photo;   // 사진이 없으면 가져옴
		m_lib.actors.erase(m_lib.actors.begin() + o);
		merged = true;
	}
	if (!merged)
		return 0;

	m_changed = true;
	m_dirty = false;
	m_cur = curName.IsEmpty() ? -1 : m_lib.FindActor(curName);
	FillList(curName);
	if (m_cur < 0)
		ShowActor(-1);
	return 0;
}

void CActorDlg::OnBnClickedPhotoBrowse()
{
	if (m_cur < 0)
		return;

	CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
		L"이미지 파일 (*.jpg;*.jpeg;*.png;*.webp;*.bmp;*.gif;*.tif;*.tiff)|*.jpg;*.jpeg;*.png;*.webp;*.bmp;*.gif;*.tif;*.tiff|모든 파일 (*.*)|*.*||",
		this);
	if (dlg.DoModal() != IDOK)
		return;

	// 원본 대신 DB 폴더(images\actors)에 복사본을 만들어 그것을 등록
	const CString copy = CVideoLibrary::StoreImageCopy(dlg.GetPathName(), L"actors");
	if (copy.IsEmpty())
	{
		AfxMessageBox(L"사진을 DB 폴더로 복사하지 못했습니다.", MB_ICONWARNING);
		return;
	}
	SetPhoto(copy);
	OnFieldChanged();
}

void CActorDlg::OnBnClickedPhotoSearch()
{
	if (m_cur < 0)
		return;
	// 검색어: 이름 콤보의 대표 이름 그대로 (풀 네임, 괄호 안 영문/일본어 이름 포함)
	CString name;
	m_comboAliases.GetWindowText(name);
	name.Trim();
	if (name.IsEmpty())
		name = m_lib.actors[m_cur].name;

	CImageSearchDlg dlg(name, this);
	if (dlg.DoModal() != IDOK || dlg.m_resultPath.IsEmpty())
		return;
	// 받은 사진을 DB 폴더(images\actors)에 복사해서 등록
	const CString copy = CVideoLibrary::StoreImageCopy(dlg.m_resultPath, L"actors");
	::DeleteFileW(dlg.m_resultPath);   // 임시 파일 정리
	if (copy.IsEmpty())
	{
		AfxMessageBox(L"사진을 DB 폴더로 복사하지 못했습니다.", MB_ICONWARNING);
		return;
	}
	SetPhoto(copy);
	OnFieldChanged();
}

void CActorDlg::OnBnClickedPhotoClear()
{
	if (m_cur < 0)
		return;
	SetPhoto(CString());
	OnFieldChanged();
}

void CActorDlg::OnOK()
{
	// 별칭 입력 칸에서 Enter → 별칭 목록에 추가
	CWnd* focus = GetFocus();
	if (focus && (focus == &m_comboAliases || focus->GetParent() == &m_comboAliases))
	{
		AddAliasFromEdit();
		return;
	}
	// Enter: 저장만 하고 창은 유지 (메모 칸에서는 줄바꿈)
	Commit();
}

void CActorDlg::FillAliases(const CString& aliases, const CString& select)
{
	const bool loading = m_loading;
	m_loading = true;
	m_comboAliases.ResetContent();
	for (const CString& al : CVideoLibrary::SplitList(aliases))
		m_comboAliases.AddString(al);
	if (m_comboAliases.GetCount() > 0)
	{
		// 마지막으로 고른 별칭 (없으면 첫 번째) 을 보여 줌 (▾ 로 전체 목록)
		int sel = select.IsEmpty() ? CB_ERR : m_comboAliases.FindStringExact(-1, select);
		m_comboAliases.SetCurSel(sel == CB_ERR ? 0 : sel);
	}
	else
		m_comboAliases.SetWindowText(L"");
	m_loading = loading;
	UpdateAliasCue();
}

CString CActorDlg::GetAliases() const
{
	std::vector<CString> list;
	for (int i = 0; i < m_comboAliases.GetCount(); ++i)
	{
		CString s;
		m_comboAliases.GetLBText(i, s);
		list.push_back(s);
	}
	// 입력 칸에 새로 적고 [+]를 누르지 않은 값도 포함 (목록에서 고른 값이면 중복 제거됨)
	CString typed;
	m_comboAliases.GetWindowText(typed);
	for (const CString& s : CVideoLibrary::SplitList(typed))
		list.push_back(s);
	return CVideoLibrary::JoinList(CVideoLibrary::SplitList(CVideoLibrary::JoinList(list)));
}

bool CActorDlg::AddAliasFromEdit()
{
	if (m_cur < 0)
		return false;
	CString typed;
	m_comboAliases.GetWindowText(typed);
	bool added = false;
	int last = CB_ERR;
	for (const CString& s : CVideoLibrary::SplitList(typed))   // 쉼표로 여러 개를 한 번에 넣어도 됨
	{
		int at = m_comboAliases.FindStringExact(-1, s);
		if (at == CB_ERR)
		{
			at = m_comboAliases.AddString(s);
			added = true;
		}
		last = at;
	}
	if (last != CB_ERR)
		m_comboAliases.SetCurSel(last);   // 새로 넣은 이름을 고른 상태로 (저장하면 대표 이름)
	else
		m_comboAliases.SetWindowText(L"");
	if (last != CB_ERR && !added)
		OnFieldChanged();
	UpdateAliasCue();
	if (added)
		OnFieldChanged();
	m_comboAliases.SetFocus();
	return added;
}

void CActorDlg::UpdateAliasCue()
{
	CString cue;
	const int n = m_comboAliases.GetCount();
	if (n > 0)
		cue.Format(L"이름 %d개 ▾ · 새 이름 입력 후 Enter", n);
	else
		cue = L"이름 입력 후 Enter";
	m_comboAliases.SetCueBanner(cue);
}

int CActorDlg::SelectedAliasIndex() const
{
	int sel = m_comboAliases.GetCurSel();
	if (sel == CB_ERR)
	{
		// 입력 칸의 글자가 목록의 별칭과 같으면 그 별칭
		CString typed;
		m_comboAliases.GetWindowText(typed);
		typed.Trim();
		if (!typed.IsEmpty())
			sel = m_comboAliases.FindStringExact(-1, typed);
	}
	return sel;
}

void CActorDlg::OnCbnSelchangeAliases()
{
	if (m_loading || m_cur < 0)
		return;
	const int sel = m_comboAliases.GetCurSel();
	if (sel == CB_ERR)
		return;
	// 고른 이름이 대표 이름이 됨 ([저장] 또는 다른 배우를 고를 때 반영)
	CString pick;
	m_comboAliases.GetLBText(sel, pick);
	if (pick.CompareNoCase(m_lib.actors[m_cur].name) != 0)
		OnFieldChanged();
}

void CActorDlg::OnCancel()
{
	// 닫기 / ESC: 편집 내용을 반영하고 닫음
	if (!Commit())
		return;
	CDialogEx::OnCancel();
}

// ===========================================================================
// CActorPickDlg (배우 선택)

CActorPickDlg::CActorPickDlg(CVideoLibrary& lib, const CString& current, CWnd* pParent)
	: CDialogEx(IDD_ACTOR_PICK, pParent), m_lib(lib)
{
	m_order = CVideoLibrary::SplitList(current);
}

void CActorPickDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_PICK_SEARCH, m_editSearch);
	DDX_Control(pDX, IDC_PICK_LIST, m_list);
	DDX_Control(pDX, IDC_PICK_NEWNAME, m_editNew);
	DDX_Control(pDX, IDC_PICK_SELECTED, m_staticSelected);
}

BEGIN_MESSAGE_MAP(CActorPickDlg, CDialogEx)
	ON_WM_CTLCOLOR()
	ON_EN_CHANGE(IDC_PICK_SEARCH, &CActorPickDlg::OnEnChangeSearch)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_PICK_LIST, &CActorPickDlg::OnLvnItemChanged)
	ON_NOTIFY(NM_DBLCLK, IDC_PICK_LIST, &CActorPickDlg::OnNmDblclk)
	ON_BN_CLICKED(IDC_PICK_ADD, &CActorPickDlg::OnBnClickedAdd)
END_MESSAGE_MAP()

BOOL CActorPickDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	m_theme.Apply(this);   // 메인 창과 같은 색상

	// 선택된 배우가 목록에 없으면 추가 (보통은 이미 동기화되어 있음)
	for (const CString& n : m_order)
	{
		if (m_lib.FindActorByAnyName(n) < 0)   // 별칭으로 연결된 표기는 새 배우를 만들지 않음
		{
			ActorInfo a;
			a.name = n;
			m_lib.actors.push_back(a);
			m_added = true;
		}
	}

	CRect rc;
	m_list.GetClientRect(&rc);
	m_list.SetExtendedStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	m_list.InsertColumn(0, L"이름", LVCFMT_LEFT, rc.Width() - ::GetSystemMetrics(SM_CXVSCROLL));

	m_editSearch.SetCueBanner(L"이름 · 별칭 검색");
	m_editNew.SetCueBanner(L"목록에 없는 배우 이름");

	FillList();
	UpdateSelectedText();
	m_editSearch.SetFocus();
	return FALSE;   // 포커스를 직접 지정
}

bool CActorPickDlg::IsChecked(const CString& name) const
{
	// 선택 목록의 표기가 이 배우의 이름이거나 별칭이면 선택된 것으로 봄
	const std::map<CString, int> index = m_lib.ActorNameIndex();
	const CString target = m_lib.ActorKeyOf(index, name);
	for (const CString& n : m_order)
	{
		if (n.CompareNoCase(name) == 0 || m_lib.ActorKeyOf(index, n) == target)
			return true;
	}
	return false;
}

void CActorPickDlg::SetChecked(const CString& name, bool checked)
{
	if (checked)
	{
		if (!IsChecked(name))
			m_order.push_back(name);
	}
	else
	{
		const std::map<CString, int> index = m_lib.ActorNameIndex();
		const CString target = m_lib.ActorKeyOf(index, name);
		m_order.erase(std::remove_if(m_order.begin(), m_order.end(),
			[&](const CString& n) { return n.CompareNoCase(name) == 0 || m_lib.ActorKeyOf(index, n) == target; }), m_order.end());
	}
}

void CActorPickDlg::FillList()
{
	CString query;
	m_editSearch.GetWindowText(query);
	query.Trim();
	query.MakeLower();

	const std::vector<int> rows = SortedActorIndices(m_lib, query);

	m_filling = true;
	m_list.SetRedraw(FALSE);
	m_list.DeleteAllItems();
	for (size_t row = 0; row < rows.size(); ++row)
	{
		const ActorInfo& a = m_lib.actors[rows[row]];
		// 대표 이름(배우 관리에서 마지막으로 고른 이름)만 표시 - 별칭은 검색에만 사용
		const int i = m_list.InsertItem(static_cast<int>(row), a.name);
		m_list.SetItemData(i, static_cast<DWORD_PTR>(rows[row]));
		m_list.SetCheck(i, IsChecked(a.name) ? TRUE : FALSE);
	}
	m_list.SetRedraw(TRUE);
	m_list.Invalidate();
	m_filling = false;
}

void CActorPickDlg::UpdateSelectedText()
{
	CString s = L"선택: ";
	s += m_order.empty() ? CString(L"(없음)") : CVideoLibrary::JoinList(m_order);
	m_staticSelected.SetWindowText(s);
}

void CActorPickDlg::OnEnChangeSearch()
{
	FillList();
}

void CActorPickDlg::OnLvnItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	*pResult = 0;
	if (m_filling || p->iItem < 0 || !(p->uChanged & LVIF_STATE))
		return;

	const UINT oldImg = p->uOldState & LVIS_STATEIMAGEMASK;
	const UINT newImg = p->uNewState & LVIS_STATEIMAGEMASK;
	if (oldImg == newImg || oldImg == 0 || newImg == 0)
		return;   // 체크 상태 변화가 아님

	const int idx = static_cast<int>(m_list.GetItemData(p->iItem));
	if (idx < 0 || idx >= static_cast<int>(m_lib.actors.size()))
		return;

	SetChecked(m_lib.actors[idx].name, newImg == INDEXTOSTATEIMAGEMASK(2));
	UpdateSelectedText();
}

void CActorPickDlg::OnNmDblclk(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMITEMACTIVATE* p = reinterpret_cast<NMITEMACTIVATE*>(pNMHDR);
	if (p->iItem >= 0)
		m_list.SetCheck(p->iItem, !m_list.GetCheck(p->iItem));
	*pResult = 0;
}

void CActorPickDlg::OnBnClickedAdd()
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

	const int existing = m_lib.FindActorByAnyName(name);
	if (existing >= 0)
	{
		// 이름이면 기존 표기, 별칭이면 입력한 별칭 표기 그대로 (해당 배우에 연결됨)
		if (m_lib.FindActor(name) >= 0)
			name = m_lib.actors[existing].name;
	}
	else
	{
		ActorInfo a;
		a.name = name;
		m_lib.actors.push_back(a);
		m_added = true;
	}

	SetChecked(name, true);
	m_editNew.SetWindowText(L"");
	m_editSearch.SetWindowText(L"");   // 전체 목록으로 (EN_CHANGE 로 갱신)
	FillList();
	UpdateSelectedText();

	// 추가한 배우가 보이도록
	const int shown = m_lib.FindActorByAnyName(name);
	for (int i = 0; i < m_list.GetItemCount(); ++i)
	{
		const int idx = static_cast<int>(m_list.GetItemData(i));
		if (idx == shown)
		{
			m_list.EnsureVisible(i, FALSE);
			m_list.SetItemState(i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			break;
		}
	}
}

void CActorPickDlg::OnOK()
{
	// 새 배우 입력 칸에서 Enter → 추가
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

HBRUSH CActorDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
		return br;
	return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}

HBRUSH CActorPickDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
		return br;
	return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}
