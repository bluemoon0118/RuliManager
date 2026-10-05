#include "pch.h"
#include "RuliManager.h"
#include "RuliManagerDlg.h"
#include "ActorDlg.h"
#include "SettingsDlg.h"
#include "ImageSearchDlg.h"
#include "NameListDlg.h"
#include "DarkTheme.h"
#include "CountryCombo.h"
#include "VectorIcons.h"
#include "TextDraw.h"

#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdiplus.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	const UINT WM_APP_REBUILD_CATS = WM_APP + 1;

	enum Column { COL_NAME = 0, COL_TITLE, COL_SIZE, COL_DATE, COL_RATING, COL_RELEASE, COL_ACTORS, COL_STUDIO, COL_TAGS, COL_PATH, COL_COUNT };

	CString FormatSize(ULONGLONG b)
	{
		CString s;
		if (b < 1024ULL)                       s.Format(L"%llu B", b);
		else if (b < 1024ULL * 1024)           s.Format(L"%.1f KB", b / 1024.0);
		else if (b < 1024ULL * 1024 * 1024)    s.Format(L"%.1f MB", b / (1024.0 * 1024));
		else                                   s.Format(L"%.2f GB", b / (1024.0 * 1024 * 1024));
		return s;
	}

	CString FormatTime(ULONGLONG t)
	{
		if (t == 0) return CString();
		FILETIME ft = { static_cast<DWORD>(t & 0xFFFFFFFF), static_cast<DWORD>(t >> 32) };
		FILETIME local = {};
		SYSTEMTIME st = {};
		if (!::FileTimeToLocalFileTime(&ft, &local) || !::FileTimeToSystemTime(&local, &st))
			return CString();
		CString s;
		s.Format(L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
		return s;
	}

	CString RatingText(int r)
	{
		if (r <= 0) return CString();
		CString s;
		for (int i = 0; i < 5; ++i)
			s += (i < r) ? L'★' : L'☆';
		return s;
	}

	// "YYYY-MM-DD" → SYSTEMTIME
	bool ParseDate(const CString& text, SYSTEMTIME& st)
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

	CString NormalizeTags(const CString& in)
	{
		CString out;
		int pos = 0;
		CString work = in;
		work.Replace(L';', L',');
		for (;;)
		{
			const int p = CVideoLibrary::FindListComma(work, pos);
			CString t = (p < 0) ? work.Mid(pos) : work.Mid(pos, p - pos);
			t.Trim();
			if (!t.IsEmpty())
			{
				if (!out.IsEmpty()) out += L", ";
				out += t;
			}
			if (p < 0) break;
			pos = p + 1;
		}
		return out;
	}

	CString LastErrorText(DWORD err)
	{
		LPWSTR buf = nullptr;
		::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr, err, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
		CString s = buf ? buf : L"";
		if (buf) ::LocalFree(buf);
		s.Trim();
		CString r;
		r.Format(L"%s (코드 %lu)", static_cast<LPCWSTR>(s), err);
		return r;
	}

	bool MoveFileWithRetry(const CString& from, const CString& to)
	{
		// 플레이어가 파일 핸들을 늦게 놓는 경우를 대비해 몇 번 재시도
		for (int i = 0; i < 6; ++i)
		{
			if (::MoveFileExW(from, to, 0))
				return true;
			const DWORD e = ::GetLastError();
			if (e != ERROR_SHARING_VIOLATION && e != ERROR_ACCESS_DENIED)
				return false;
			::Sleep(150);
		}
		return false;
	}
}

// ---------------------------------------------------------------------------
// 간단한 입력 창 (격자 보기에서 이름 변경)

class CInputDlg : public CDialogEx
{
public:
	CInputDlg(const CString& value, CWnd* parent) : CDialogEx(IDD_INPUT, parent), m_value(value) {}
	CString m_value;

protected:
	CDarkDialogTheme m_theme;   // 메인 창과 같은 어두운 색상
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
	{
		if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
			return br;
		return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
	}
	DECLARE_MESSAGE_MAP()

	virtual BOOL OnInitDialog()
	{
		CDialogEx::OnInitDialog();
		m_theme.Apply(this);
		CEdit* edit = static_cast<CEdit*>(GetDlgItem(IDC_INPUT_EDIT));
		edit->SetWindowText(m_value);
		// 확장자 앞까지만 선택
		const int ext = static_cast<int>(::PathFindExtensionW(m_value) - static_cast<LPCWSTR>(m_value));
		edit->SetFocus();
		edit->SetSel(0, ext);
		return FALSE;
	}
	virtual void OnOK()
	{
		GetDlgItemText(IDC_INPUT_EDIT, m_value);
		CDialogEx::OnOK();
	}
};

BEGIN_MESSAGE_MAP(CInputDlg, CDialogEx)
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

// ---------------------------------------------------------------------------

CRuliManagerDlg::CRuliManagerDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_RULIMANAGER_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CRuliManagerDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST_VIDEOS, m_list);
	DDX_Control(pDX, IDC_EDIT_SEARCH, m_editSearch);
	DDX_Control(pDX, IDC_COMBO_FILTER, m_comboFilter);
	DDX_Control(pDX, IDC_COMBO_SORT, m_comboSort);
	DDX_Control(pDX, IDC_EDIT_ACTORS, m_editActors);
	DDX_Control(pDX, IDC_EDIT_VALIASES, m_editVAliases);
	DDX_Control(pDX, IDC_EDIT_STUDIO, m_editStudio);
	DDX_Control(pDX, IDC_DATE_RELEASE, m_dateRelease);
	DDX_Control(pDX, IDC_LIST_CATEGORY, m_listCat);
	DDX_Control(pDX, IDC_EDIT_TAGS, m_editTags);
	DDX_Control(pDX, IDC_EDIT_MEMO, m_editMemo);
	DDX_Control(pDX, IDC_EDIT_CODE, m_editCode);
	DDX_Control(pDX, IDC_EDIT_TITLE, m_editTitle);
	DDX_Control(pDX, IDC_STATIC_NAME, m_staticName);
	DDX_Control(pDX, IDC_STATIC_STATUS, m_staticStatus);
	DDX_Control(pDX, IDC_STATIC_IMAGE, m_staticImage);
	DDX_Control(pDX, IDC_PREVIEW, m_preview);
}

BEGIN_MESSAGE_MAP(CRuliManagerDlg, CDialogEx)
	ON_WM_SIZE()
	ON_WM_DROPFILES()
	ON_WM_MOVE()
	ON_WM_GETMINMAXINFO()
	ON_WM_CLOSE()
	ON_WM_CTLCOLOR()
	ON_WM_CONTEXTMENU()

	ON_BN_CLICKED(IDC_BTN_ADDFOLDER, &CRuliManagerDlg::OnBnClickedAddFolder)
	ON_BN_CLICKED(IDC_BTN_REMOVEFOLDER, &CRuliManagerDlg::OnBnClickedRemoveFolder)
	ON_BN_CLICKED(IDC_BTN_REFRESH, &CRuliManagerDlg::OnBnClickedRefresh)
	ON_BN_CLICKED(IDC_BTN_RESCAN, &CRuliManagerDlg::OnBnClickedRescan)
	ON_BN_CLICKED(IDC_BTN_EXPORTTXT, &CRuliManagerDlg::OnBnClickedExportTxt)
	ON_BN_CLICKED(IDC_BTN_ACTORS, &CRuliManagerDlg::OnBnClickedActors)
	ON_BN_CLICKED(IDC_BTN_SETTINGS, &CRuliManagerDlg::OnBnClickedSettings)
	ON_BN_CLICKED(IDC_BTN_CHANGEIMAGE, &CRuliManagerDlg::OnBnClickedChangeImage)
	ON_BN_CLICKED(IDC_BTN_SEARCHIMAGE, &CRuliManagerDlg::OnBnClickedSearchImage)
	ON_BN_CLICKED(IDC_BTN_PICKACTORS, &CRuliManagerDlg::OnBnClickedPickActors)
	ON_STN_CLICKED(IDC_STATIC_RATING_LBL, &CRuliManagerDlg::OnStnClickedRatingLabel)
	ON_BN_CLICKED(IDC_BTN_PICKSTUDIO, &CRuliManagerDlg::OnBnClickedPickStudio)
	ON_BN_CLICKED(IDC_BTN_PICKTAGS, &CRuliManagerDlg::OnBnClickedPickTags)
	ON_BN_CLICKED(IDC_BTN_PICKVALIASES, &CRuliManagerDlg::OnBnClickedPickVAliases)
	ON_COMMAND(ID_MANAGE_ACTORS, &CRuliManagerDlg::OnManageActors)
	ON_COMMAND(ID_MANAGE_STUDIOS, &CRuliManagerDlg::OnManageStudios)
	ON_COMMAND(ID_MANAGE_TAGS, &CRuliManagerDlg::OnManageTags)
	ON_BN_CLICKED(IDC_BTN_ACTOR_BACK, &CRuliManagerDlg::OnBnClickedActorBack)
	ON_COMMAND(ID_ACTOR_SHOWVIDEOS, &CRuliManagerDlg::OnActorShowVideos)
	ON_COMMAND(ID_ACTOR_EDIT, &CRuliManagerDlg::OnActorEdit)
	ON_COMMAND(ID_CAT_DELETE, &CRuliManagerDlg::OnCatDelete)
	ON_COMMAND(ID_ACTOR_DELETE, &CRuliManagerDlg::OnActorDelete)
	ON_BN_CLICKED(IDC_BTN_SAVE, &CRuliManagerDlg::OnBnClickedSave)
	ON_BN_CLICKED(IDC_BTN_RENAME, &CRuliManagerDlg::OnBnClickedRename)
	ON_BN_CLICKED(IDC_BTN_DELETE, &CRuliManagerDlg::OnBnClickedDelete)
	ON_BN_CLICKED(IDC_BTN_EXPLORER, &CRuliManagerDlg::OnBnClickedExplorer)
	ON_BN_CLICKED(IDC_BTN_OPENDB, &CRuliManagerDlg::OnBnClickedOpenDb)
	ON_BN_CLICKED(IDC_BTN_APPLYDB, &CRuliManagerDlg::OnBnClickedApplyDb)
	ON_BN_CLICKED(IDC_BTN_OPENDEFAULT, &CRuliManagerDlg::OnBnClickedOpenDefault)

	ON_EN_CHANGE(IDC_EDIT_SEARCH, &CRuliManagerDlg::OnEnChangeSearch)
	ON_CBN_SELCHANGE(IDC_COMBO_FILTER, &CRuliManagerDlg::OnCbnSelchangeFilter)
	ON_CBN_SELCHANGE(IDC_COMBO_SORT, &CRuliManagerDlg::OnCbnSelchangeSort)
	ON_BN_CLICKED(IDC_BTN_SORTDIR, &CRuliManagerDlg::OnBnClickedSortDir)
	ON_EN_CHANGE(IDC_EDIT_ACTORS, &CRuliManagerDlg::OnEnChangeActors)
	ON_EN_CHANGE(IDC_EDIT_VALIASES, &CRuliManagerDlg::OnEnChangeVAliases)
	ON_EN_CHANGE(IDC_EDIT_STUDIO, &CRuliManagerDlg::OnEnChangeStudio)
	ON_EN_CHANGE(IDC_EDIT_TAGS, &CRuliManagerDlg::OnEnChangeTags)
	ON_EN_CHANGE(IDC_EDIT_MEMO, &CRuliManagerDlg::OnDetailsChanged)
	ON_EN_CHANGE(IDC_EDIT_CODE, &CRuliManagerDlg::OnDetailsChanged)
	ON_EN_CHANGE(IDC_EDIT_TITLE, &CRuliManagerDlg::OnDetailsChanged)
	ON_NOTIFY(DTN_DATETIMECHANGE, IDC_DATE_RELEASE, &CRuliManagerDlg::OnDtnReleaseChanged)

	ON_NOTIFY(LVN_GETDISPINFO, IDC_LIST_VIDEOS, &CRuliManagerDlg::OnLvnGetDispInfo)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST_VIDEOS, &CRuliManagerDlg::OnLvnItemChanged)
	ON_NOTIFY(LVN_COLUMNCLICK, IDC_LIST_VIDEOS, &CRuliManagerDlg::OnLvnColumnClick)
	ON_NOTIFY(LVN_KEYDOWN, IDC_LIST_VIDEOS, &CRuliManagerDlg::OnLvnKeyDown)
	ON_NOTIFY(LVN_BEGINLABELEDIT, IDC_LIST_VIDEOS, &CRuliManagerDlg::OnLvnBeginLabelEdit)
	ON_NOTIFY(LVN_ENDLABELEDIT, IDC_LIST_VIDEOS, &CRuliManagerDlg::OnLvnEndLabelEdit)
	ON_NOTIFY(NM_DBLCLK, IDC_LIST_VIDEOS, &CRuliManagerDlg::OnNmDblclkList)
	ON_NOTIFY(NM_DBLCLK, IDC_LIST_CATEGORY, &CRuliManagerDlg::OnNmDblclkCategory)
	ON_NOTIFY(LVN_KEYDOWN, IDC_LIST_CATEGORY, &CRuliManagerDlg::OnLvnCatKeyDown)

	ON_CONTROL_RANGE(BN_CLICKED, IDC_RADIO_VIDEO, IDC_RADIO_TAG, &CRuliManagerDlg::OnModeChanged)
	ON_CONTROL_RANGE(BN_CLICKED, IDC_RADIO_LISTVIEW, IDC_RADIO_GRIDVIEW, &CRuliManagerDlg::OnViewStyleChanged)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST_CATEGORY, &CRuliManagerDlg::OnLvnCatItemChanged)
	ON_NOTIFY(LVN_COLUMNCLICK, IDC_LIST_CATEGORY, &CRuliManagerDlg::OnLvnCatColumnClick)
	ON_MESSAGE(WM_APP_REBUILD_CATS, &CRuliManagerDlg::OnRebuildCategories)
END_MESSAGE_MAP()

// ---------------------------------------------------------------------------
// 초기화

BOOL CRuliManagerDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	SetIcon(m_hIcon, TRUE);
	SetIcon(m_hIcon, FALSE);

	// 목록 컨트롤 (가상 목록: LVS_OWNERDATA)
	m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES |
		LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_LABELTIP);
	m_list.InsertColumn(COL_NAME,   L"이름",   LVCFMT_LEFT,  DX(110));
	m_list.InsertColumn(COL_TITLE,  L"제목",   LVCFMT_LEFT,  DX(110));
	m_list.InsertColumn(COL_SIZE,   L"크기",   LVCFMT_RIGHT, DX(45));
	m_list.InsertColumn(COL_DATE,   L"수정일", LVCFMT_LEFT,  DX(65));
	m_list.InsertColumn(COL_RATING, L"별점",   LVCFMT_LEFT,  DX(40));
	m_list.InsertColumn(COL_RELEASE, L"발매일", LVCFMT_LEFT,  DX(48));
	m_list.InsertColumn(COL_ACTORS, L"배우",   LVCFMT_LEFT,  DX(70));
	m_list.InsertColumn(COL_STUDIO, L"스튜디오", LVCFMT_LEFT, DX(60));
	m_list.InsertColumn(COL_TAGS,   L"태그",   LVCFMT_LEFT,  DX(70));
	m_list.InsertColumn(COL_PATH,   L"경로",   LVCFMT_LEFT,  DX(200));

	// 별점 필터
	m_comboFilter.AddString(L"전체");
	for (int i = 1; i <= 5; ++i)
	{
		CString s;
		s.Format(L"%s 이상", static_cast<LPCWSTR>(RatingText(i)));
		m_comboFilter.AddString(s);
	}
	m_comboFilter.SetCurSel(0);

	// 별점 선택: 마우스를 올리면 별 미리 보기, 클릭하면 확정 + 바로 저장
	m_starRating.Create(this, IDC_COMBO_RATING, 5);
	m_starRating.SetWindowPos(GetDlgItem(IDC_STATIC_RATING_LBL), 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);   // 탭 순서: 별점 라벨 다음
	m_starRating.SetColors(kBackColor, RGB(0xFF, 0xC8, 0x1E), RGB(0xE6, 0xEA, 0xEE), kTextColor);
	GetDlgItem(IDC_STATIC_RATING_LBL)->ModifyStyle(0, SS_NOTIFY);   // [별점] 라벨 클릭 → 0점으로 리셋

	// 배우 탭 오른쪽 아래 상세: 별을 누르면 배우 별점 저장 (카드 리본 갱신), 하트는 즐겨찾기 전환
	m_actorPanel.Create(this, IDC_ACTOR_PANEL);
	m_actorPanel.SetColors(kBackColor, kTextColor, RGB(0x8A, 0x9B, 0xA8));
	m_actorPanel.m_onRating = [this](int r)
	{
		if (m_actorInfoIdx < 0 || m_actorInfoIdx >= static_cast<int>(m_lib.actors.size()))
			return;
		m_lib.actors[m_actorInfoIdx].rating = r;
		m_lib.Save();
		m_actorGrid.Invalidate(FALSE);   // 카드의 별점 리본
	};
	m_actorPanel.m_onFavorite = [this]() { ToggleActorFavorite(m_actorInfoIdx); };
	// 영상 상세 정보: 태그 아래 출연 배우 카드
	// 영상 상세 정보: [이미지 변경] 버튼 위 fps / 해상도
	m_mediaInfo.Create(this, IDC_MEDIA_INFO);
	m_mediaInfo.SetColors(kBackColor, RGB(0xA7, 0xB6, 0xC2), RGB(0xF5, 0xF8, 0xFA));
	m_actorStrip.Create(this, IDC_ACTOR_STRIP);
	m_actorStrip.SetColors(kBackColor, RGB(0x8A, 0x9B, 0xA8), RGB(0x2A, 0x35, 0x3D), kScrollColor);
	m_actorStrip.SetEmptyText(L"출연 배우 없음");
	m_actorStrip.m_onDraw = [this](CDC* dc, int i, const CRect& rc, bool hot) { DrawStripCard(dc, i, rc, hot); };
	m_actorStrip.m_onActivate = [this](int i) { OpenActorFromStrip(i); };
	// 카드 오른쪽 위 하트: 마우스 오버 = 반투명 회색 하트, 클릭 = 즐겨찾기 지정/해제
	m_actorStrip.m_onHitPart = [this](int, const CRect& card, CPoint pt) -> int
	{
		CRect heart = StripHeartRect(card);
		heart.InflateRect(2, 2);
		return heart.PtInRect(pt) ? 1 : 0;
	};
	m_actorStrip.m_onPartClick = [this](int i, int part)
	{
		if (part != 1 || i < 0 || i >= static_cast<int>(m_stripActors.size()))
			return;
		const int idx = m_stripActors[i];
		if (idx < 0 || idx >= static_cast<int>(m_lib.actors.size()))
			return;
		m_lib.actors[idx].favorite = !m_lib.actors[idx].favorite;
		m_lib.Save();
		RebuildActorGrid();          // 배우 탭 정렬(즐겨찾기 먼저) 반영 + 카드 띠 다시 그림 (영상 상세 정보는 그대로)
		m_actorStrip.Invalidate(FALSE);
	};
	m_actorStrip.m_onTip = [this](int i) -> CString
	{
		if (i < 0 || i >= static_cast<int>(m_stripActors.size()))
			return CString();
		return m_lib.actors[m_stripActors[i]].name + L"\n(더블클릭: 배우 탭에서 보기)";
	};
	// 별점 오른쪽 물방울 카운트: 클릭 +1 / 오른쪽 클릭 → 초기화 (별점처럼 바로 저장)
	m_dropCounter.Create(this, IDC_DROP_COUNTER);
	m_dropCounter.SetWindowPos(&m_starRating, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);   // 탭 순서: 별점 다음
	m_dropCounter.SetColors(kBackColor, kTextColor, RGB(0x48, 0xAF, 0xF0));
	m_dropCounter.m_onChanged = [this](int)
	{
		if (m_curItem < 0)
			return;
		OnDetailsChanged();
		CommitDetails();
		m_grid.Invalidate(FALSE);   // 카드 아래 물방울 카운트
	};
	m_starRating.m_onChanged = [this](int)
	{
		if (m_curItem < 0)
			return;
		OnDetailsChanged();
		CommitDetails();
		m_grid.Invalidate(FALSE);   // 카드의 별점 리본
	};
	// 제목 칸: 여러 줄 에디트(자동 줄바꿈, 4줄 높이) - 여러 줄 에디트는 안내 문구(Cue Banner)를 지원하지 않음
	// 영상 메모 기능 삭제: 메모 칸 / 라벨은 항상 숨김
	m_editMemo.ShowWindow(SW_HIDE);
	GetDlgItem(IDC_STATIC_MEMO_LBL)->ShowWindow(SW_HIDE);
	m_editCode.SetCueBanner(L"예: ABC-123");

	// 영상 정렬 기준 (Column 순서와 같음)
	{
		const wchar_t* const sortNames[] = { L"이름", L"제목", L"크기", L"수정일", L"별점",
		                                     L"발매일", L"배우", L"스튜디오", L"태그", L"경로" };
		for (const wchar_t* n : sortNames)
			m_comboSort.AddString(n);
		m_sortColumn = AfxGetApp()->GetProfileInt(L"Settings", L"SortColumn", COL_NAME);
		if (m_sortColumn < 0 || m_sortColumn >= static_cast<int>(_countof(sortNames)))
			m_sortColumn = COL_NAME;
		m_sortAsc = AfxGetApp()->GetProfileInt(L"Settings", L"SortAsc", 1) != 0;
	}

	m_editSearch.SetCueBanner(L"이름 · 제목 · 배우(별칭) · 스튜디오 · 태그 · 메모 검색");
	m_editActors.SetCueBanner(L"입력하면 배우 검색 (↓ 목록) · [선택...]");
	m_editVAliases.SetCueBanner(L"이 작품에서 쓴 배우 별칭 (입력 / ↓ 목록)");
	m_dateRelease.SetFormat(L"yyyy-MM-dd");
	m_editStudio.SetCueBanner(L"입력하면 스튜디오 검색 (↓ 목록)");
	m_editTags.SetCueBanner(L"입력하면 태그 검색 (↓ 목록) · 여러 개는 쉼표");
	SetupSuggestions();

	// 미리보기 이미지 영역
	m_preview.SetPlaceholder(L"동영상을 선택하세요.");
	// 탐색기에서 이미지 파일을 끌어다 놓기 (영상 상세의 이미지 영역) - 관리자 권한으로 실행해도 받도록 메시지 허용
	DragAcceptFiles(TRUE);
	::ChangeWindowMessageFilterEx(m_hWnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
	::ChangeWindowMessageFilterEx(m_hWnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
	::ChangeWindowMessageFilterEx(m_hWnd, 0x0049 /* WM_COPYGLOBALDATA */, MSGFLT_ALLOW, nullptr);

	ApplyColors();

	// 분류 목록 (배우 / 스튜디오 / 태그)
	m_listCat.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
	m_listCat.InsertColumn(0, L"배우", LVCFMT_LEFT, DX(60));
	m_listCat.InsertColumn(1, L"영상 수", LVCFMT_RIGHT, DX(30));
	m_listCat.InsertColumn(2, L"메모", LVCFMT_LEFT, DX(100));

	// 보기 모드 복원
	m_mode = AfxGetApp()->GetProfileInt(L"Settings", L"ViewMode", MODE_VIDEO);
	if (m_mode < MODE_VIDEO || m_mode > MODE_TAG)
		m_mode = MODE_VIDEO;
	UpdateModeButtons();
	UpdateCategoryHeader();

	// 탭별 확대 단계 복원
	for (int i = 0; i < 4; ++i)
	{
		CString keyName;
		keyName.Format(L"Zoom%d", i);
		m_zoom[i] = (std::max)(0, (std::min)(3, static_cast<int>(AfxGetApp()->GetProfileInt(L"Settings", keyName, 0))));
	}

	// 격자 보기용 썸네일 이미지 목록 (DPI에 맞춰 크기 조정)
	{
		UINT dpi = ::GetDpiForWindow(m_hWnd);
		if (dpi == 0) dpi = 96;
		m_thumbW = MulDiv(160, dpi, 96);
		m_thumbH = MulDiv(120, dpi, 96);
		m_thumbs.Create(m_thumbW, m_thumbH, ILC_COLOR24, 64, 64);
		AddThumbnail(nullptr, L"이미지 없음");
		m_grid.Create(this, IDC_GRID, this, &m_thumbs, m_thumbW, m_thumbH);
		m_actorOwner.dlg = this;
		m_actorGrid.Create(this, IDC_ACTOR_GRID, &m_actorOwner, &m_thumbs, m_thumbW, m_thumbH);
		SetupActorCards();   // 배우 탭: 세로 사진 카드
		m_catOwner.dlg = this;
		m_catGrid.Create(this, IDC_CAT_GRID, &m_catOwner, &m_thumbs, m_thumbW, m_thumbH);

		// 확대 슬라이더 (4단계)
		m_zoomSlider.Create(this, IDC_ZOOM_SLIDER, 4);
		m_zoomSlider.SetColors(kBackColor, kButtonColor, RGB(0x5C, 0x70, 0x80), RGB(0x8A, 0x9B, 0xA8));
		m_zoomSlider.SetPos(m_zoom[m_mode]);
		m_zoomSlider.m_onChanged = [this](int pos) { OnZoomChanged(pos); };

		// 태그 칩 입력 (예전 태그 입력 칸 / [선택...] 버튼 자리)
		m_tagChips.Create(this, IDC_TAG_CHIPS);
		m_tagChips.SetColors(kEditColor, RGB(0xCE, 0xD9, 0xE0), RGB(0x18, 0x20, 0x26), RGB(0xA7, 0xB6, 0xC2), kTextColor);
		m_tagChips.m_edit.SetColors(kEditColor, kTextColor, kButtonColor, RGB(140, 155, 168));
		m_tagChips.m_edit.Setup([this](std::vector<SuggestItem>& out)
		{
			for (const NamedInfo& n : m_lib.tagInfos)
			{
				bool used = false;   // 이미 붙은 태그는 후보에서 뺌
				for (const CString& tg : m_tagChips.Tags())
					if (tg.CompareNoCase(n.name) == 0) { used = true; break; }
				if (!used)
					out.push_back({ n.name, n.name });
			}
		}, false);
		m_tagChips.m_onChanged = [this]()
		{
			// 칩 → 숨긴 태그 칸 (EN_CHANGE 로 변경 표시)
			m_syncingTags = true;
			m_editTags.SetWindowText(CVideoLibrary::JoinList(m_tagChips.Tags()));
			m_syncingTags = false;
		};
		m_tagChips.m_onDropDown = [this]() { OnBnClickedPickTags(); };
		m_tagChips.m_onHeightChanged = [this]()
		{
			CRect client;
			GetClientRect(&client);
			LayoutControls(client.Width(), client.Height());
		};
		GetDlgItem(IDC_EDIT_TAGS)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_BTN_PICKTAGS)->ShowWindow(SW_HIDE);

		// 배우 칩 입력 (태그와 같은 모양, 이름 뒤에 별칭을 회색으로)
		m_actorChips.Create(this, IDC_ACTOR_CHIPS);
		m_actorChips.SetColors(kEditColor, RGB(0xCE, 0xD9, 0xE0), RGB(0x18, 0x20, 0x26), RGB(0xA7, 0xB6, 0xC2), kTextColor);
		m_actorChips.m_edit.SetColors(kEditColor, kTextColor, kButtonColor, RGB(140, 155, 168));
		m_actorChips.m_edit.SetCueBanner(L"배우 입력 (↓ 목록)");
		// 배우 + 별칭 병합: 칩 이름은 이 작품에서 고른 별칭(없으면 배우 이름), 회색은 대표 이름
		m_actorChips.m_displayText = [this](const CString& tag) { return ActorChipDisplay(tag); };
		m_actorChips.m_onChipClick = [this](int index, CPoint pt) { OnActorChipMenu(index, pt); };
		m_actorChips.m_edit.Setup([this](std::vector<SuggestItem>& out)
		{
			// 이미 붙은 배우(이름 또는 별칭으로)는 후보에서 뺌
			std::set<int> used;
			for (const CString& n : m_actorChips.Tags())
			{
				const int idx = m_lib.FindActorByAnyName(n);
				if (idx >= 0) used.insert(idx);
			}
			for (size_t i = 0; i < m_lib.actors.size(); ++i)
			{
				if (used.count(static_cast<int>(i)))
					continue;
				const ActorInfo& a = m_lib.actors[i];
				out.push_back({ a.name, a.name });
				for (const CString& al : CVideoLibrary::SplitList(a.aliases))
					out.push_back({ al + L"\t" + a.name + L"의 별칭", al });
			}
		}, false);
		m_actorChips.m_onChanged = [this]()
		{
			// 칩 → 숨긴 배우 칸 (EN_CHANGE 로 변경 표시)
			CString oldActors;
			m_editActors.GetWindowText(oldActors);
			const CString newActors = CVideoLibrary::JoinList(m_actorChips.Tags());
			m_syncingActors = true;
			m_editActors.SetWindowText(newActors);
			m_syncingActors = false;
			ApplyDefaultAliases(oldActors, newActors);
		};
		m_actorChips.m_onDropDown = [this]() { OnBnClickedPickActors(); };
		m_actorChips.m_onHeightChanged = m_tagChips.m_onHeightChanged;
		GetDlgItem(IDC_EDIT_ACTORS)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_BTN_PICKACTORS)->ShowWindow(SW_HIDE);

		// 스튜디오 칩 입력 (배우 선택과 같은 모양, 스튜디오는 하나만 - 새로 고르면 바뀜)
		m_studioChips.Create(this, IDC_STUDIO_CHIPS);
		m_studioChips.SetColors(kEditColor, RGB(0xCE, 0xD9, 0xE0), RGB(0x18, 0x20, 0x26), RGB(0xA7, 0xB6, 0xC2), kTextColor);
		m_studioChips.m_edit.SetColors(kEditColor, kTextColor, kButtonColor, RGB(140, 155, 168));
		m_studioChips.m_edit.SetCueBanner(L"스튜디오 입력 (↓ 목록)");
		m_studioChips.m_edit.Setup([this](std::vector<SuggestItem>& out)
		{
			const CString cur = m_studioChips.Tags().empty() ? CString() : m_studioChips.Tags()[0];
			for (const NamedInfo& n : m_lib.studios)
				if (n.name.CompareNoCase(cur) != 0)   // 지금 스튜디오는 후보에서 뺌
					out.push_back({ n.name, n.name });
		}, false);
		m_studioChips.m_onChanged = [this]()
		{
			// 하나만: 새로 추가하면 마지막 것만 남김 → 숨긴 스튜디오 칸 (EN_CHANGE 로 변경 표시)
			std::vector<CString> tags = m_studioChips.Tags();
			if (tags.size() > 1)
			{
				tags.erase(tags.begin(), tags.end() - 1);
				m_studioChips.SetTags(tags);
			}
			m_syncingStudio = true;
			m_editStudio.SetWindowText(tags.empty() ? CString() : tags[0]);
			m_syncingStudio = false;
		};
		m_studioChips.m_onDropDown = [this]() { OnBnClickedPickStudio(); };
		m_studioChips.m_onHeightChanged = m_tagChips.m_onHeightChanged;
		GetDlgItem(IDC_EDIT_STUDIO)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_BTN_PICKSTUDIO)->ShowWindow(SW_HIDE);
		// [별칭] 줄은 배우 칩에 합침 (값은 숨긴 별칭 칸에 보관)
		GetDlgItem(IDC_STATIC_VALIASES_LBL)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_EDIT_VALIASES)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_BTN_PICKVALIASES)->ShowWindow(SW_HIDE);
	}

	// 시작할 때마다 기존 DB 백업 (DB 폴더\backup, 최근 10개, 바뀐 것이 없으면 건너뜀)
	CVideoLibrary::BackupDbFiles(10);

	// 이전 실행이 비정상 종료되어 반영하지 못한 작업 DB 가 남아 있으면 물어봄
	if (CVideoLibrary::HasLeftoverWorkDb())
	{
		const int r = AfxMessageBox(L"이전 실행에서 DB 에 반영하지 못한 변경 내용(library.work.vmdb)이 남아 있습니다.\n\n"
			L"[예] 남은 변경 내용을 DB 에 반영하고 시작\n[아니요] 남은 변경 내용을 버리고 기존 DB 로 시작",
			MB_YESNO | MB_ICONQUESTION);
		if (r == IDYES)
		{
			if (!CVideoLibrary::ApplyLeftoverWorkDb())
				AfxMessageBox(L"남은 변경 내용을 반영하지 못했습니다. 기존 DB 로 시작합니다.", MB_ICONWARNING);
		}
		else
			CVideoLibrary::DiscardWorkDb();
	}
	m_lib.m_onSaved = [this]() { UpdateApplyDbButton(); };

	// 라이브러리 불러오기
	const bool haveLibrary = m_lib.Load();
	{
		// 배우 칸의 별칭 표기 → 배우 이름 + 참여 별칭
		const bool va = m_lib.NormalizeAllVideoActors();
		// 기존 데이터의 배우/스튜디오/태그 이름을 각 목록으로 옮김
		const bool a = m_lib.SyncActorsFromVideos() || va;
		const bool n = m_lib.SyncNamedFromVideos();
		// 품번이 빈 영상은 파일 / 영상 폴더 이름에서 찾아 채움
		const bool codes = m_lib.FillMissingCodes() > 0;
		// 분할 파일(ABC-123_1 / _2 …)은 같은 정보를 사용: 묶음마다 빈 정보를 서로 채움
		const bool parts = m_lib.UnifyPartGroups() > 0;
		// 예전에 원본 경로로 등록한 이미지를 DB 폴더(images)의 복사본으로 교체
		bool img = m_lib.MigrateImagesToStore();
		// Image 폴더의 평문 이미지 → 암호화(.vmimg)로 바꿈 (DB 를 정상적으로 읽었을 때만, 평문은 저장 후 삭제)
		std::vector<CString> plainImages;
		if (haveLibrary && !m_lib.DbBroken())
			img = m_lib.EncryptImageStore(plainImages) || img;
		// 경로가 바뀐(옮기거나 이름을 바꾼) 영상 파일을 기존 정보에 다시 연결
		{
			CWaitCursor wait;
			m_startupRelinked = m_lib.RelinkOnStartup();
		}
		if (a || n || img || codes || parts || m_startupRelinked > 0 || m_lib.LoadedPlainText())   // 예전 평문 DB 는 바로 암호화해서 다시 저장
		{
			if (m_lib.Save())
			{
				for (const CString& f : plainImages)
					::DeleteFileW(f);   // 암호화 사본으로 바뀐 평문 이미지 삭제 (휴지통 거치지 않음)
			}
		}
		if (m_lib.DbBroken())
			AfxMessageBox(L"DB 파일을 복호화하지 못했습니다 (손상되었거나 다른 프로그램 버전의 키).\n"
				L"기존 DB 를 보호하기 위해 이번 실행에서는 DB 를 저장하지 않습니다.", MB_ICONERROR);
		// 교체/삭제되어 더 이상 쓰이지 않는 복사본 정리 (DB를 정상적으로 읽은 경우에만)
		if (haveLibrary)
			m_lib.CleanupImageStore();
	}
	UpdateApplyDbButton();   // 시작 때 정리하며 저장했으면 활성
	RebuildCategories();

	// 목록 / 격자 표시 복원
	const bool grid = AfxGetApp()->GetProfileInt(L"Settings", L"GridView", 0) != 0;
	CheckRadioButton(IDC_RADIO_LISTVIEW, IDC_RADIO_GRIDVIEW, grid ? IDC_RADIO_GRIDVIEW : IDC_RADIO_LISTVIEW);
	SetGridView(grid);

	m_layoutReady = true;
	RestoreWindowPlacement();   // 지난번 창 크기/위치/최대화 상태
	CRect client;
	GetClientRect(&client);
	LayoutControls(client.Width(), client.Height());

	ShowDetails(-1);
	ApplyFilter();
	UpdateSortArrows();
	UpdateSortUI();
	if (IsActorGridMode())
		UpdateStatus();
	else if (IsCategoryListMode())
		ShowNamedInfo(-1);

	CreateScrollBars();

	if (m_startupRelinked > 0)
	{
		CString note;
		note.Format(L"경로가 바뀐 영상 파일 %d개를 기존 정보에 다시 연결했습니다.", m_startupRelinked);
		m_staticStatus.SetWindowText(note);
	}
	return TRUE;
}

// ---------------------------------------------------------------------------
// 레이아웃

int CRuliManagerDlg::DX(int dlu)
{
	CRect r(0, 0, dlu, 0);
	MapDialogRect(&r);
	return r.right;
}

int CRuliManagerDlg::DY(int dlu)
{
	CRect r(0, 0, 0, dlu);
	MapDialogRect(&r);
	return r.bottom;
}

void CRuliManagerDlg::MoveCtrl(UINT id, int x, int y, int w, int h)
{
	if (CWnd* p = GetDlgItem(id))
		p->MoveWindow(x, y, (std::max)(0, w), (std::max)(0, h), FALSE);
}

int CRuliManagerDlg::LeftPaneWidth(int cx)
{
	// 모든 탭: 오른쪽 상세 정보 폭은 고정, 창 크기 변화는 왼쪽 격자가 받음
	const int m = DX(6), gap = DX(4);
	if (m_mode == MODE_TAG)
		return (std::max)(DX(150), cx - 2 * m);   // 태그 탭: 상세 페이지 없이 격자가 전체 폭
	const int avail = cx - 2 * m - gap;
	const int detailW = DX(280);
	return (std::max)(DX(150), avail - detailW);
}

void CRuliManagerDlg::LayoutControls(int cx, int cy)
{
	if (!m_layoutReady || cx <= 0 || cy <= 0)
		return;

	const int m = DX(6), gap = DX(4);
	const int btnH = DY(14), rowH = DY(13);

	// 상단 도구줄
	int x = m, y = m;
	MoveCtrl(IDC_BTN_ADDFOLDER,    x, y, DX(55), btnH);  x += DX(55) + gap;
	MoveCtrl(IDC_BTN_REMOVEFOLDER, x, y, DX(55), btnH);  x += DX(55) + gap;
	MoveCtrl(IDC_BTN_REFRESH,      x, y, DX(50), btnH);  x += DX(50) + gap;
	MoveCtrl(IDC_BTN_RESCAN,       x, y, DX(40), btnH);  x += DX(40) + gap;
	MoveCtrl(IDC_BTN_ACTORS,       x, y, DX(50), btnH);  x += DX(50) + gap * 3;
	MoveCtrl(IDC_STATIC_SEARCH,    x, y + DY(3), DX(20), DY(9));  x += DX(20);
	MoveCtrl(IDC_EDIT_SEARCH,      x, y, DX(140), rowH);  x += DX(140) + gap * 3;
	MoveCtrl(IDC_STATIC_FILTER,    x, y + DY(3), DX(20), DY(9));  x += DX(20);
	MoveCtrl(IDC_COMBO_FILTER,     x, y, DX(70), DY(120));
	// 설정 (톱니바퀴): 오른쪽 위 고정, 정사각형
	{
		const int side = DY(14);
		MoveCtrl(IDC_BTN_SETTINGS, cx - m - side, y, side, side);
	}

	// 하단 버튼줄
	const int by = cy - m - btnH;
	x = m;
	MoveCtrl(IDC_BTN_OPENDEFAULT, x, by, DX(76), btnH);  x += DX(76) + gap;
	const UINT bottomIds[] = { IDC_BTN_DELETE, IDC_BTN_EXPLORER, IDC_BTN_OPENDB, IDC_BTN_APPLYDB, IDC_BTN_EXPORTTXT };
	const int bw = DX(62);
	for (UINT id : bottomIds)
	{
		MoveCtrl(id, x, by, bw, btnH);
		x += bw + gap;
	}
	MoveCtrl(IDC_STATIC_STATUS, x + gap, by + DY(3), cx - m - x - gap, DY(9));

	// 보기 전환 라디오 버튼줄
	const int ry = y + btnH + DY(4);
	const int radioH = DY(14);
	x = m;
	// 영상 / 배우 / 스튜디오 / 태그 토글 버튼 (붙여서 배치)
	MoveCtrl(IDC_RADIO_VIDEO,  x, ry, DX(52), radioH);  x += DX(52) + 2;   // 아이콘 자리만큼 넓게
	MoveCtrl(IDC_RADIO_ACTOR,  x, ry, DX(52), radioH);  x += DX(52) + 2;
	MoveCtrl(IDC_RADIO_STUDIO, x, ry, DX(60), radioH);  x += DX(60) + 2;
	MoveCtrl(IDC_RADIO_TAG,    x, ry, DX(52), radioH);  x += DX(52) + gap * 2;
	// 확대 슬라이더: [태그] 버튼 바로 오른쪽 고정 위치 (모든 탭 공통)
	MoveCtrl(IDC_ZOOM_SLIDER,  x, ry, DX(56), radioH);  x += DX(56) + gap * 4;
	{
		// 스튜디오/태그 목록: [표시: 목록 | 격자], 영상: [정렬 ▾] [▲▼] (같은 자리, 하나만 보임)
		int vx = x;
		MoveCtrl(IDC_STATIC_VIEW,  vx, ry + DY(2), DX(20), DY(9));  vx += DX(22);
		MoveCtrl(IDC_RADIO_LISTVIEW, vx, ry, DX(32), radioH);  vx += DX(34);
		MoveCtrl(IDC_RADIO_GRIDVIEW, vx, ry, DX(32), radioH);

		int sx = x;
		MoveCtrl(IDC_STATIC_SORT, sx, ry + DY(2), DX(20), DY(9));  sx += DX(22);
		MoveCtrl(IDC_COMBO_SORT,  sx, ry - DY(1), DX(60), DY(120));  sx += DX(60) + gap;
		MoveCtrl(IDC_BTN_SORTDIR, sx, ry - DY(1), DX(50), radioH + DY(2));
	}

	// 가운데 영역: 왼쪽 (분류 목록 +) 동영상 목록 / 오른쪽 이미지 + 상세
	const int top = ry + radioH + gap;
	const int bottom = by - gap;
	const int leftW = LeftPaneWidth(cx);
	if (m_mode != MODE_VIDEO)
	{
		// 배우 격자 / 스튜디오·태그 목록 (전체 영역)
		MoveCtrl(IDC_ACTOR_GRID, m, top, leftW, bottom - top);
		MoveCtrl(IDC_LIST_CATEGORY, m, top, leftW, bottom - top);
		MoveCtrl(IDC_CAT_GRID, m, top, leftW, bottom - top);
		const int countW = DX(40);
		const int nameW = (std::max)(DX(60), (leftW - countW) * 40 / 100);
		m_listCat.SetColumnWidth(0, nameW);
		m_listCat.SetColumnWidth(1, countW);
		m_listCat.SetColumnWidth(2, (std::max)(DX(40), leftW - nameW - countW - ::GetSystemMetrics(SM_CXVSCROLL) - 6));

		// 영상 보기 (위에 [◀ 목록] 줄)
		MoveCtrl(IDC_BTN_ACTOR_BACK, m, top, DX(70), btnH);
		MoveCtrl(IDC_STATIC_ACTOR_TITLE, m + DX(74), top + DY(3), leftW - DX(74), DY(9));
		const int t2 = top + btnH + gap;
		MoveCtrl(IDC_LIST_VIDEOS, m, t2, leftW, bottom - t2);
		MoveCtrl(IDC_GRID, m, t2, leftW, bottom - t2);
	}
	else
	{
		MoveCtrl(IDC_LIST_VIDEOS, m, top, leftW, bottom - top);
		MoveCtrl(IDC_GRID, m, top, leftW, bottom - top);
	}

	if (m_mode == MODE_TAG)
	{
		// 태그 탭: 오른쪽 상세 페이지(이미지 + 정보) 없음
		RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
		return;
	}

	const int rx = m + leftW + gap;
	const int rw = cx - m - rx;
	// 상세 정보(편집 칸)는 영상 탭에서만 표시, 다른 탭은 이미지 + 이름/정보 두 줄만
	const bool showDetail = ShowVideoDetail();   // 영상 탭 + 배우 출연작 화면
	// 태그 칩이 여러 줄이면 그만큼 상세 영역을 늘림 (이미지가 줄어듦)
	const int lblW0 = DX(34);   // 라벨 폭 ("스튜디오" 가 잘리지 않게)
	const int chipsH = (showDetail && m_tagChips.GetSafeHwnd())
		? (std::max)(rowH, m_tagChips.CalcHeight(rw - lblW0)) : rowH;
	const int actorChipsH = (showDetail && m_actorChips.GetSafeHwnd())
		? (std::max)(rowH, m_actorChips.CalcHeight(rw - lblW0)) : rowH;
	const int studioChipsH = (showDetail && m_studioChips.GetSafeHwnd())
		? (std::max)(rowH, m_studioChips.CalcHeight(rw - lblW0)) : rowH;
	const int stripH = (m_mode == MODE_VIDEO && m_actorStrip.GetSafeHwnd()) ? m_actorStrip.CalcHeight() : 0;   // 태그 아래 배우 카드 (영상 탭에서만)
	// 제목 칸 높이: 글꼴 4줄 + 테두리 / 여백
	int titleH = rowH;
	if (showDetail && m_editTitle.GetSafeHwnd())
	{
		CClientDC tdc(&m_editTitle);
		CFont* old = tdc.SelectObject(m_editTitle.GetFont());
		TEXTMETRIC tm = {};
		tdc.GetTextMetrics(&tm);
		tdc.SelectObject(old);
		titleH = (std::max)(rowH, static_cast<int>(tm.tmHeight) * 4 + 8);
	}
	const int detailH = showDetail ? DY(116) + (titleH - rowH) + (studioChipsH - rowH) + chipsH + actorChipsH + (stripH > 0 ? stripH + gap : 0)   // 파일 / 이미지 / 품번 / 제목 / 별점 / 발매일 / 배우 / 스튜디오 / 태그 (메모 칸 삭제 → 이미지가 커짐)
		: ((IsActorGridMode() && m_actorPanel.GetSafeHwnd()) ? m_actorPanel.CalcHeight(rw) : DY(26));   // 배우 격자: [별점] 줄 추가   // [별칭] 줄은 배우 칩에 합침
	const int previewBottom = (std::max)(top + DY(60), bottom - detailH - gap);
	MoveCtrl(IDC_PREVIEW, rx, top, rw, previewBottom - top);

	const int lblW = DX(34);   // 라벨 폭 ("스튜디오" 가 잘리지 않게, lblW0 과 같게)
	const int infoX = showDetail ? lblW : 0;   // 다른 탭은 [파일] 글자 없이 왼쪽부터
	int dy = previewBottom + gap;
	MoveCtrl(IDC_STATIC_NAME_LBL, rx, dy + DY(2), lblW, DY(9));
	if (showDetail)
	{
		// 영상: 파일 이름 줄 오른쪽(= [이미지 검색][이미지 변경] 버튼 위)에 fps / 해상도
		const int cbw = DX(54);
		// fps / 해상도 칸은 글자 폭만큼만 차지 (파일 이름이 최대한 보이게)
		const int needW = m_mediaInfo.GetSafeHwnd() ? m_mediaInfo.NeededWidth() : 0;
		const int infoW = (std::min)(needW, cbw * 2 + DX(3));
		const int nameGap = (infoW > 0) ? DX(6) : 0;
		MoveCtrl(IDC_STATIC_NAME, rx + infoX, dy + DY(2), rw - infoX - infoW - nameGap, DY(9));
		MoveCtrl(IDC_MEDIA_INFO, rx + rw - infoW, dy, infoW, DY(11));
	}
	else
		MoveCtrl(IDC_STATIC_NAME, rx + infoX, dy + DY(2), rw - infoX, DY(9));
	dy += DY(11);
	if (showDetail)
	{
		// 영상: 이미지 줄 오른쪽에 [이미지 변경...]
		const int cbw = DX(54);
		MoveCtrl(IDC_STATIC_IMAGE, rx + infoX, dy + DY(2), rw - infoX - cbw * 2 - DX(7), DY(9));
		MoveCtrl(IDC_BTN_SEARCHIMAGE, rx + rw - cbw * 2 - DX(3), dy, cbw, DY(13));
		MoveCtrl(IDC_BTN_CHANGEIMAGE, rx + rw - cbw, dy, cbw, DY(13));
		dy += DY(16);
	}
	else
	{
		MoveCtrl(IDC_STATIC_IMAGE, rx + infoX, dy + DY(2), rw - infoX, DY(9));
		dy += DY(14);
	}
	if (IsActorGridMode() && m_actorPanel.GetSafeHwnd())
	{
		// 배우 상세: 이름 줄 / 정보 줄 대신 패널 (미리보기 바로 아래부터)
		const int py = previewBottom + gap;
		MoveCtrl(IDC_ACTOR_PANEL, rx, py, rw, (std::max)(0, bottom - py));
	}
	if (!showDetail)
	{
		RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
		return;
	}

	MoveCtrl(IDC_STATIC_CODE_LBL, rx, dy + DY(3), lblW, DY(9));     // 품번 (제목 위)
	MoveCtrl(IDC_EDIT_CODE, rx + lblW, dy, rw - lblW, rowH);
	dy += DY(17);

	MoveCtrl(IDC_STATIC_TITLE_LBL, rx, dy + DY(3), lblW, DY(9));
	MoveCtrl(IDC_EDIT_TITLE, rx + lblW, dy, rw - lblW, titleH);   // 여러 줄 (4줄)
	dy += titleH + DY(4);

	MoveCtrl(IDC_STATIC_RATING_LBL, rx, dy + DY(3), lblW, DY(9));
	MoveCtrl(IDC_COMBO_RATING, rx + lblW, dy, DX(80), rowH);
	MoveCtrl(IDC_DROP_COUNTER, rx + lblW + DX(86), dy, DX(40), rowH);   // 별점 오른쪽 물방울 카운트
	MoveCtrl(IDC_BTN_SAVE, rx + rw - DX(50), dy, DX(50), btnH);
	dy += DY(17);

	MoveCtrl(IDC_STATIC_RELEASE_LBL, rx, dy + DY(3), lblW, DY(9));
	MoveCtrl(IDC_DATE_RELEASE, rx + lblW, dy, DX(80), rowH);
	dy += DY(17);

	MoveCtrl(IDC_STATIC_ACTORS_LBL, rx, dy + DY(3), lblW, DY(9));
	MoveCtrl(IDC_EDIT_ACTORS, rx + lblW, dy, rw - lblW - DX(40), rowH);   // 숨김 (값 보관용)
	MoveCtrl(IDC_ACTOR_CHIPS, rx + lblW, dy, rw - lblW, actorChipsH);
	dy += actorChipsH + DY(4);


	MoveCtrl(IDC_STATIC_STUDIO_LBL, rx, dy + DY(3), lblW, DY(9));
	MoveCtrl(IDC_EDIT_STUDIO, rx + lblW, dy, rw - lblW - DX(40), rowH);   // 숨김 (값 보관용)
	MoveCtrl(IDC_STUDIO_CHIPS, rx + lblW, dy, rw - lblW, studioChipsH);   // 배우 선택과 같은 칩 입력
	dy += studioChipsH + DY(4);

	MoveCtrl(IDC_STATIC_TAGS_LBL, rx, dy + DY(3), lblW, DY(9));
	MoveCtrl(IDC_EDIT_TAGS, rx + lblW, dy, rw - lblW - DX(40), rowH);   // 숨김 (값 보관용)
	MoveCtrl(IDC_TAG_CHIPS, rx + lblW, dy, rw - lblW, chipsH);
	dy += chipsH + DY(4);

	if (stripH > 0)
		MoveCtrl(IDC_ACTOR_STRIP, rx, (std::max)(dy, bottom - stripH), rw, stripH);   // 태그 아래 출연 배우 카드 띠 (전체 폭)

	RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void CRuliManagerDlg::SetupSuggestions()
{
	// 입력 칸을 직접 입력할 수 있게 (예전에는 [선택...] 전용 읽기 전용)
	CSuggestEdit* edits[] = { &m_editActors, &m_editVAliases, &m_editStudio, &m_editTags };
	for (CSuggestEdit* e : edits)
	{
		e->SetReadOnly(FALSE);
		e->SetColors(kEditColor, kTextColor, kButtonColor, RGB(140, 155, 168));
	}

	// 배우: 이름 + 별칭 (별칭을 고르면 저장할 때 배우 이름 + 참여 별칭으로 정리됨)
	m_editActors.Setup([this](std::vector<SuggestItem>& out)
	{
		for (const ActorInfo& a : m_lib.actors)
		{
			out.push_back({ a.name, a.name });
			for (const CString& al : CVideoLibrary::SplitList(a.aliases))
				out.push_back({ al + L"\t" + a.name + L"의 별칭", al });
		}
	}, true);

	// 별칭: 이 영상 배우의 별칭을 먼저, 그다음 다른 배우의 별칭
	m_editVAliases.Setup([this](std::vector<SuggestItem>& out)
	{
		CString actorsText;
		m_editActors.GetWindowText(actorsText);
		std::set<int> inVideo;
		for (const CString& n : CVideoLibrary::SplitList(actorsText))
		{
			const int idx = m_lib.FindActorByAnyName(n);
			if (idx >= 0)
				inVideo.insert(idx);
		}
		for (int pass = 0; pass < 2; ++pass)
		{
			for (size_t i = 0; i < m_lib.actors.size(); ++i)
			{
				if ((inVideo.count(static_cast<int>(i)) != 0) != (pass == 0))
					continue;
				const ActorInfo& a = m_lib.actors[i];
				for (const CString& al : CVideoLibrary::SplitList(a.aliases))
					out.push_back({ al + L"\t" + a.name, al });
			}
		}
	}, true);
	// 다른 배우의 별칭을 고르면 그 배우를 배우 칸에도 추가 (저장 시 별칭이 정리되지 않도록)
	m_editVAliases.m_onAccept = [this](const SuggestItem& item)
	{
		const int idx = m_lib.FindActorByAnyName(item.value);
		if (idx < 0)
			return;
		CString actorsText;
		m_editActors.GetWindowText(actorsText);
		std::vector<CString> list = CVideoLibrary::SplitList(actorsText);
		for (const CString& n : list)
		{
			if (m_lib.FindActorByAnyName(n) == idx)
				return;   // 이미 있음
		}
		list.push_back(m_lib.actors[idx].name);
		m_editActors.SetWindowText(CVideoLibrary::JoinList(list));
	};

	// 스튜디오 (하나)
	m_editStudio.Setup([this](std::vector<SuggestItem>& out)
	{
		for (const NamedInfo& n : m_lib.studios)
			out.push_back({ n.name, n.name });
	}, false);

	// 태그 (여러 개)
	m_editTags.Setup([this](std::vector<SuggestItem>& out)
	{
		for (const NamedInfo& n : m_lib.tagInfos)
			out.push_back({ n.name, n.name });
	}, true);
}

void CRuliManagerDlg::HideSuggestions()
{
	m_editActors.HidePopup();
	m_editVAliases.HidePopup();
	m_editStudio.HidePopup();
	m_editTags.HidePopup();
	m_tagChips.m_edit.HidePopup();
	m_actorChips.m_edit.HidePopup();
	if (m_studioChips.GetSafeHwnd())
		m_studioChips.m_edit.HidePopup();
}

void CRuliManagerDlg::OnMove(int x, int y)
{
	CDialogEx::OnMove(x, y);
	if (m_layoutReady)
		HideSuggestions();   // 창을 옮기면 후보 목록이 따로 떠 있지 않도록 닫음
}

void CRuliManagerDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialogEx::OnSize(nType, cx, cy);
	if (m_layoutReady)
		HideSuggestions();
	if (nType != SIZE_MINIMIZED)
		LayoutControls(cx, cy);
}

void CRuliManagerDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	UINT dpi = 96;
	if (GetSafeHwnd())
	{
		const UINT d = ::GetDpiForWindow(m_hWnd);
		if (d) dpi = d;
	}
	lpMMI->ptMinTrackSize.x = MulDiv(900, dpi, 96);
	lpMMI->ptMinTrackSize.y = MulDiv(720, dpi, 96);
	CDialogEx::OnGetMinMaxInfo(lpMMI);
}

// ---------------------------------------------------------------------------
// 목록 / 필터 / 정렬

int CRuliManagerDlg::GetSelectedRow() const
{
	return m_list.GetNextItem(-1, LVNI_SELECTED);
}

int CRuliManagerDlg::GetSelectedItem() const
{
	const int row = GetSelectedRow();
	if (row < 0 || row >= static_cast<int>(m_view.size()))
		return -1;
	return m_view[row];
}

void CRuliManagerDlg::BeginLibraryChange()
{
	// 항목 인덱스가 바뀌기 전에 목록과 상세 표시를 비웁니다.
	CommitDetails();
	m_list.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	m_view.clear();
	m_list.SetItemCountEx(0);
	m_grid.Refresh();
	ShowDetails(-1);
	MarkCategoriesDirty();
}

void CRuliManagerDlg::ApplyFilter()
{
	CString keepPath;
	if (m_curItem >= 0 && m_curItem < static_cast<int>(m_lib.items.size()))
		keepPath = m_lib.items[m_curItem].path;

	CString query;
	m_editSearch.GetWindowText(query);
	query.Trim();
	query.MakeLower();

	// 공백으로 구분된 검색어는 모두 포함해야 일치 (AND)
	std::vector<CString> terms;
	int pos = 0;
	CString token = query.Tokenize(L" ", pos);
	while (!token.IsEmpty())
	{
		terms.push_back(token);
		token = query.Tokenize(L" ", pos);
	}

	const int minRating = (std::max)(0, m_comboFilter.GetCurSel());

	// 배우 이름/별칭 어느 것으로도 검색되도록: 영상의 배우 표기(이름 또는 별칭, 소문자) → 이름, 별칭
	std::map<CString, CString> aliasMap;
	if (!terms.empty())
	{
		for (const ActorInfo& a : m_lib.actors)
		{
			if (a.aliases.IsEmpty()) continue;
			const CString all = a.name + L"," + a.aliases;
			CString key = a.name;
			key.MakeLower();
			aliasMap[key] = all;
			for (const CString& s : CVideoLibrary::SplitList(a.aliases))
			{
				CString sk = s;
				sk.MakeLower();
				aliasMap.emplace(sk, all);
			}
		}
	}
	// 배우의 출연작 보기: 영상의 표기가 그 배우의 이름이거나 별칭이면 포함
	const std::map<CString, int> actorIndex = (m_mode == MODE_ACTOR) ? m_lib.ActorNameIndex() : std::map<CString, int>();
	CString actorTarget = m_catValue;
	actorTarget.MakeLower();

	m_list.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	// 카드의 순번 (1/3): 같은 폴더 · 같은 묶음 이름(마지막 '_' 왼쪽)의 파일 수
	m_partTotal.clear();
	for (const VideoItem& pv : m_lib.items)
	{
		CString key;
		int num = 0;
		SplitPartName(pv.path, key, num);
		++m_partTotal[key];
	}
	m_view.clear();
	for (size_t i = 0; i < m_lib.items.size(); ++i)
	{
		const VideoItem& v = m_lib.items[i];
		if (v.rating < minRating)
			continue;
		if (m_mode != MODE_VIDEO && m_catKind != CAT_ALL)
		{
			const std::vector<CString> vals = GetCategoryValues(v, m_mode);
			if (m_catKind == CAT_NONE)
			{
				if (!vals.empty()) continue;
			}
			else
			{
				bool hit = false;
				for (const CString& n : vals)
				{
					if (n.CompareNoCase(m_catValue) == 0 ||
						(m_mode == MODE_ACTOR && m_lib.ActorKeyOf(actorIndex, n) == actorTarget)) { hit = true; break; }
				}
				if (!hit) continue;
			}
		}
		if (!terms.empty())
		{
			CString hay = v.FileName() + L"\n" + v.code + L"\n" + v.title + L"\n" + v.release + L"\n" + v.actors + L"\n" + v.actorAliases + L"\n" + v.studio + L"\n" + v.tags;   // 영상 메모 기능 삭제
			if (!aliasMap.empty() && !v.actors.IsEmpty())
			{
				for (const CString& n : CVideoLibrary::SplitList(v.actors))
				{
					CString key = n;
					key.MakeLower();
					auto it = aliasMap.find(key);
					if (it != aliasMap.end())
						hay += L"\n" + it->second;
				}
			}
			hay.MakeLower();
			bool ok = true;
			for (const CString& t : terms)
			{
				if (hay.Find(t) < 0) { ok = false; break; }
			}
			if (!ok) continue;
		}
		m_view.push_back(static_cast<int>(i));
	}

	SortView();
	m_list.SetItemCountEx(static_cast<int>(m_view.size()), LVSICF_NOSCROLL);
	m_list.Invalidate(FALSE);
	m_grid.Refresh();

	if (!keepPath.IsEmpty())
		SelectPath(keepPath);

	// 선택하던 동영상이 목록에서 빠졌으면 상세 표시도 비움
	if (GetSelectedItem() < 0 && m_curItem >= 0)
	{
		CommitDetails();
		ShowDetails(-1);
	}

	UpdateStatus();
}

void CRuliManagerDlg::SortView()
{
	const auto& items = m_lib.items;
	const int col = m_sortColumn;
	const bool asc = m_sortAsc;

	std::stable_sort(m_view.begin(), m_view.end(), [&](int a, int b)
	{
		const VideoItem& x = items[a];
		const VideoItem& y = items[b];
		// 저장된 항목을 먼저, 임시 항목(정보 저장 전)은 뒤에 (정렬 방향과 상관없이)
		if (x.pending != y.pending)
			return !x.pending;
		int c = 0;
		switch (col)
		{
		case COL_SIZE:   c = (x.size < y.size) ? -1 : (x.size > y.size ? 1 : 0); break;
		case COL_DATE:   c = (x.modified < y.modified) ? -1 : (x.modified > y.modified ? 1 : 0); break;
		case COL_RATING: c = x.rating - y.rating; break;
		case COL_TITLE:  c = ::StrCmpLogicalW(x.title, y.title); break;
		case COL_RELEASE: c = x.release.Compare(y.release); break;   // YYYY-MM-DD 이므로 문자열 비교 = 날짜 비교
		case COL_ACTORS: c = ::StrCmpLogicalW(x.actors, y.actors); break;
		case COL_STUDIO: c = ::StrCmpLogicalW(x.studio, y.studio); break;
		case COL_TAGS:   c = ::StrCmpLogicalW(x.tags, y.tags); break;
		case COL_PATH:   c = ::StrCmpLogicalW(x.path, y.path); break;
		default: break;
		}
		if (c == 0)
			c = ::StrCmpLogicalW(::PathFindFileNameW(x.path), ::PathFindFileNameW(y.path));
		return asc ? (c < 0) : (c > 0);
	});
}

void CRuliManagerDlg::UpdateSortArrows()
{
	CHeaderCtrl* header = m_list.GetHeaderCtrl();
	if (!header) return;
	const int n = header->GetItemCount();
	for (int i = 0; i < n; ++i)
	{
		HDITEM hd = {};
		hd.mask = HDI_FORMAT;
		header->GetItem(i, &hd);
		hd.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
		if (i == m_sortColumn)
			hd.fmt |= m_sortAsc ? HDF_SORTUP : HDF_SORTDOWN;
		header->SetItem(i, &hd);
	}
}

void CRuliManagerDlg::SelectPath(const CString& path)
{
	if (path.IsEmpty()) return;
	for (size_t row = 0; row < m_view.size(); ++row)
	{
		if (m_lib.items[m_view[row]].path.CompareNoCase(path) == 0)
		{
			const int r = static_cast<int>(row);
			m_list.SetItemState(r, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			m_list.EnsureVisible(r, FALSE);
			return;
		}
	}
}

void CRuliManagerDlg::UpdateStatus()
{
	if (IsActorGridMode())
	{
		CString a;
		a.Format(L"배우 %d명 (표시 %d명) · 더블클릭하면 출연작 보기",
			static_cast<int>(m_lib.actors.size()), static_cast<int>(m_actorRows.size()));
		m_staticStatus.SetWindowText(a);
		return;
	}
	if (IsCategoryListMode())
	{
		CString c;
		c.Format(L"%s %d개 · 더블클릭하면 영상 보기",
			m_mode == MODE_STUDIO ? L"스튜디오" : L"태그",
			static_cast<int>(m_lib.NamedList(NamedKind()).size()));
		m_staticStatus.SetWindowText(c);
		return;
	}
	CString s;
	const int pendingCount = m_lib.PendingCount();
	s.Format(L"DB %d개 · 임시 %d개 · 표시 %d개 · 등록 폴더 %d개",
		static_cast<int>(m_lib.items.size()) - pendingCount, pendingCount,
		static_cast<int>(m_view.size()),
		static_cast<int>(m_lib.folders.size()));
	m_staticStatus.SetWindowText(s);
}

void CRuliManagerDlg::OnEnChangeSearch()
{
	ApplyFilter();
	if (IsActorGridMode())
		RebuildActorGrid();   // 배우 격자에서는 이름/별칭 검색
	else if (IsCategoryListMode())
		RebuildCategories();  // 스튜디오/태그 목록에서는 이름 검색
}

void CRuliManagerDlg::OnCbnSelchangeFilter()
{
	ApplyFilter();
}

void CRuliManagerDlg::UpdateSortUI()
{
	m_comboSort.SetCurSel(m_sortColumn);
	SetDlgItemText(IDC_BTN_SORTDIR, m_sortAsc ? L"▲ 오름차순" : L"▼ 내림차순");
	AfxGetApp()->WriteProfileInt(L"Settings", L"SortColumn", m_sortColumn);
	AfxGetApp()->WriteProfileInt(L"Settings", L"SortAsc", m_sortAsc ? 1 : 0);
}

void CRuliManagerDlg::OnCbnSelchangeSort()
{
	const int sel = m_comboSort.GetCurSel();
	if (sel < 0 || sel == m_sortColumn)
		return;
	m_sortColumn = sel;
	// 날짜/크기/별점은 큰 값(최신)부터, 나머지는 가나다순부터
	m_sortAsc = (m_sortColumn != COL_DATE && m_sortColumn != COL_RATING &&
	             m_sortColumn != COL_SIZE && m_sortColumn != COL_RELEASE);
	UpdateSortArrows();
	UpdateSortUI();
	ApplyFilter();
}

void CRuliManagerDlg::OnBnClickedSortDir()
{
	m_sortAsc = !m_sortAsc;
	UpdateSortArrows();
	UpdateSortUI();
	ApplyFilter();
}

// ---------------------------------------------------------------------------
// 보기 전환 (영상 / 배우 / 스튜디오 / 태그)

std::vector<CString> CRuliManagerDlg::GetCategoryValues(const VideoItem& v, int mode)
{
	std::vector<CString> out;
	const CString* src = nullptr;
	switch (mode)
	{
	case MODE_ACTOR:  src = &v.actors; break;
	case MODE_STUDIO: src = &v.studio; break;
	case MODE_TAG:    src = &v.tags;   break;
	default:          return out;
	}

	if (mode == MODE_STUDIO)
	{
		CString t = *src;
		t.Trim();
		if (!t.IsEmpty())
			out.push_back(t);
		return out;
	}

	// 배우 / 태그: 쉼표로 구분
	int pos = 0;
	for (;;)
	{
		const int p = CVideoLibrary::FindListComma(*src, pos);
		CString t = (p < 0) ? src->Mid(pos) : src->Mid(pos, p - pos);
		t.Trim();
		if (!t.IsEmpty())
			out.push_back(t);
		if (p < 0) break;
		pos = p + 1;
	}
	return out;
}

void CRuliManagerDlg::UpdateCategoryHeader()
{
	static const wchar_t* const kTitles[] = { L"", L"배우", L"스튜디오", L"태그" };
	LVCOLUMN col = {};
	col.mask = LVCF_TEXT;
	col.pszText = const_cast<LPWSTR>(kTitles[m_mode]);
	m_listCat.SetColumn(0, &col);
}

void CRuliManagerDlg::MarkCategoriesDirty()
{
	// 목록 알림 처리 중에 분류 목록을 다시 만들지 않도록 메시지로 미룸
	if (m_mode == MODE_VIDEO || m_catsDirty)
		return;
	m_catsDirty = true;
	PostMessage(WM_APP_REBUILD_CATS);
}

LRESULT CRuliManagerDlg::OnRebuildCategories(WPARAM, LPARAM)
{
	if (!m_catsDirty)
		return 0;

	RebuildCategories();
	return 0;
}

void CRuliManagerDlg::RebuildCategories()
{
	m_catsDirty = false;
	if (m_mode == MODE_VIDEO)
		return;
	if (m_mode == MODE_ACTOR)
	{
		RebuildActorGrid();   // 배우 보기는 분류 목록 대신 배우 격자
		return;
	}

	CString query;
	m_editSearch.GetWindowText(query);
	query.Trim();
	query.MakeLower();
	if (!IsCategoryListMode())
		query.Empty();   // 영상 보기 중에는 검색어가 영상용

	struct Entry { CString name; CString memo; int count = 0; bool fav = false; };
	std::map<CString, Entry> groups;   // 소문자 키 → 표시 이름/개수
	int none = 0;

	// 스튜디오 / 태그 목록에 있으면 영상이 없어도 표시
	if (m_mode == MODE_STUDIO || m_mode == MODE_TAG)
	{
		for (const NamedInfo& n : m_lib.NamedList(m_mode == MODE_STUDIO ? LIST_STUDIO : LIST_TAG))
		{
			CString key = n.name;
			key.MakeLower();
			groups[key].name = n.name;
			if (m_mode == MODE_STUDIO)
				groups[key].memo = n.memo;   // 태그는 메모 없음
			groups[key].fav = (m_mode == MODE_TAG) && n.favorite;   // 태그 즐겨찾기
		}
	}

	// 스튜디오 카드용: 스튜디오별 출연 배우 수
	m_studioActorCounts.clear();
	if (m_mode == MODE_STUDIO)
	{
		const std::map<CString, int> nameIndex = m_lib.ActorNameIndex();
		std::map<CString, std::set<CString>> actorsOf;
		for (const VideoItem& v : m_lib.items)
		{
			CString s = v.studio;
			s.Trim();
			if (s.IsEmpty())
				continue;
			s.MakeLower();
			for (const CString& n : CVideoLibrary::SplitList(v.actors))
				actorsOf[s].insert(m_lib.ActorKeyOf(nameIndex, n));
		}
		for (const auto& kv : actorsOf)
			m_studioActorCounts[kv.first] = static_cast<int>(kv.second.size());
	}

	for (const VideoItem& v : m_lib.items)
	{
		const std::vector<CString> vals = GetCategoryValues(v, m_mode);
		if (vals.empty())
		{
			++none;
			continue;
		}
		std::set<CString> seen;
		for (const CString& n : vals)
		{
			CString key = n;
			key.MakeLower();
			if (!seen.insert(key).second)
				continue;
			Entry& e = groups[key];
			if (e.name.IsEmpty())
				e.name = n;
			++e.count;
		}
	}

	std::vector<Entry> sorted;
	sorted.reserve(groups.size());
	for (const auto& kv : groups)
	{
		if (!query.IsEmpty() && kv.first.Find(query) < 0)
			continue;
		sorted.push_back(kv.second);
	}

	const bool byCount = m_catSortByCount;
	std::sort(sorted.begin(), sorted.end(), [byCount](const Entry& a, const Entry& b)
	{
		if (a.fav != b.fav)
			return a.fav;   // 즐겨찾기한 태그를 먼저
		if (byCount && a.count != b.count)
			return a.count > b.count;
		return ::StrCmpLogicalW(a.name, b.name) < 0;
	});

	m_loadingCats = true;
	m_listCat.SetRedraw(FALSE);
	m_listCat.DeleteAllItems();
	m_catRows.clear();

	auto addRow = [this](const CString& label, int count, int kind, const CString& value, const CString& memo)
	{
		const int row = m_listCat.InsertItem(m_listCat.GetItemCount(), label);
		CString c;
		c.Format(L"%d", count);
		m_listCat.SetItemText(row, 1, c);
		CString oneLine = memo;
		oneLine.Replace(L"\r\n", L" ");
		oneLine.Replace(L'\n', L' ');
		m_listCat.SetItemText(row, 2, oneLine);
		m_catRows.push_back({ kind, value });
	};

	// (전체)/(미지정) 항목은 표시하지 않음 — 실제 스튜디오/태그만
	(void)none;
	for (const Entry& e : sorted)
		addRow(e.name, e.count, CAT_VALUE, e.name, e.memo);

	// 이전 선택 복원 (선택은 오른쪽 정보 표시용이며 영상 필터와는 별개)
	int sel = -1;
	for (size_t i = 0; i < m_catRows.size(); ++i)
	{
		const CatRow& r = m_catRows[i];
		if (r.kind == m_catSelKind &&
			(m_catSelKind != CAT_VALUE || r.value.CompareNoCase(m_catSelValue) == 0))
		{
			sel = static_cast<int>(i);
			break;
		}
	}
	if (sel >= 0)
	{
		m_listCat.SetItemState(sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_listCat.EnsureVisible(sel, FALSE);
	}

	m_listCat.SetRedraw(TRUE);
	m_listCat.Invalidate();
	m_loadingCats = false;

	m_catGrid.Refresh();
	if (sel >= 0)
		m_catGrid.OnSelectionChanged();

	if (IsCategoryListMode())
		UpdateStatus();
}

void CRuliManagerDlg::OnModeChanged(UINT nID)
{
	const int mode = static_cast<int>(nID) - IDC_RADIO_VIDEO;
	if (mode == m_mode || mode < MODE_VIDEO || mode > MODE_TAG)
		return;

	CommitDetails();
	m_mode = mode;
	m_catKind = CAT_ALL;
	m_catValue.Empty();
	m_drill.Empty();
	m_catSelKind = -1;
	m_catSelValue.Empty();
	AfxGetApp()->WriteProfileInt(L"Settings", L"ViewMode", m_mode);
	UpdateModeButtons();
	m_zoomSlider.SetPos(m_zoom[m_mode]);
	SetupActorCards();   // 탭마다 확대 단계가 다름

	UpdateCategoryHeader();
	UpdateLeftPane();
	RebuildCategories();
	if (IsActorGridMode())
	{
		ShowDetails(-1);
		ShowActorInfo(SelectedActorIndex());
	}
	else if (IsCategoryListMode())
	{
		ShowDetails(-1);
		ShowNamedInfo(-1);
	}

	CRect client;
	GetClientRect(&client);
	LayoutControls(client.Width(), client.Height());
	ApplyFilter();
}

void CRuliManagerDlg::OnLvnCatItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	*pResult = 0;
	if (m_loadingCats)
		return;

	if ((p->uChanged & LVIF_STATE) &&
		(p->uNewState & LVIS_SELECTED) && !(p->uOldState & LVIS_SELECTED) &&
		p->iItem >= 0 && p->iItem < static_cast<int>(m_catRows.size()))
	{
		const CatRow& r = m_catRows[p->iItem];
		m_catSelKind = r.kind;
		m_catSelValue = r.value;
		ShowNamedInfo(p->iItem);   // 오른쪽에 이미지/정보 (영상은 더블클릭하면 표시)
	}
	if (p->uChanged & LVIF_STATE)
		m_catGrid.Invalidate(FALSE);
}

void CRuliManagerDlg::OnLvnCatKeyDown(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLVKEYDOWN* k = reinterpret_cast<NMLVKEYDOWN*>(pNMHDR);
	HandleCatKey(k->wVKey);
	*pResult = 0;
}

void CRuliManagerDlg::HandleCatKey(UINT vk)
{
	switch (vk)
	{
	case VK_SPACE: OnActorShowVideos(); break;
	case VK_F2:    OnActorEdit(); break;
	case VK_F5:    OnBnClickedRefresh(); break;
	case 'F':
		if (::GetKeyState(VK_CONTROL) & 0x8000)
		{
			m_editSearch.SetFocus();
			m_editSearch.SetSel(0, -1);
		}
		break;
	}
}

int CRuliManagerDlg::GetNamedThumbIndex(int kind, const CString& name)
{
	const int idx = m_lib.FindNamed(kind, name);
	if (idx < 0)
		return 0;
	const NamedInfo& n = m_lib.NamedList(kind)[idx];
	if (n.image.IsEmpty())
		return 0;

	CString key;
	key.Format(L"named%d:%s|%s", kind, static_cast<LPCWSTR>(n.name), static_cast<LPCWSTR>(n.image));
	key.MakeLower();
	auto it = m_thumbIndex.find(key);
	if (it != m_thumbIndex.end())
		return it->second;

	int image = 0;
	CImage src;
	if (::PathFileExistsW(n.image) && LoadImageFile(src, n.image))
		image = AddThumbnail(&src, nullptr);
	m_thumbIndex[key] = image;
	return image;
}

// --- 스튜디오/태그 격자 데이터 제공 (행 = m_catRows) ---

int CRuliManagerDlg::CatGridOwner::GridGetCount()
{
	return static_cast<int>(dlg->m_catRows.size());
}

int CRuliManagerDlg::CatGridOwner::GridGetSel()
{
	return dlg->m_listCat.GetNextItem(-1, LVNI_SELECTED);
}

void CRuliManagerDlg::CatGridOwner::GridSetSel(int row)
{
	if (row < 0 || row >= static_cast<int>(dlg->m_catRows.size()))
		return;
	// 선택은 (숨겨진) 분류 목록이 관리 → 기존 선택 처리(오른쪽 정보 표시) 그대로 사용
	dlg->m_listCat.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	dlg->m_listCat.EnsureVisible(row, FALSE);
	dlg->m_catGrid.OnSelectionChanged();
}

void CRuliManagerDlg::CatGridOwner::GridGetItem(int row, int& image, CString& name, CString& line2, CString& line3)
{
	image = 0;
	if (row < 0 || row >= static_cast<int>(dlg->m_catRows.size()))
		return;
	const CatRow& r = dlg->m_catRows[row];
	name = dlg->m_listCat.GetItemText(row, 0);
	line2 = L"영상 " + dlg->m_listCat.GetItemText(row, 1) + L"편";
	line3 = dlg->m_listCat.GetItemText(row, 2);   // 메모
	if (r.kind == CAT_VALUE && dlg->m_mode == MODE_STUDIO)
		image = dlg->GetNamedThumbIndex(LIST_STUDIO, r.value);   // 태그는 이미지 없음
}

void CRuliManagerDlg::CatGridOwner::GridActivate(int row)
{
	dlg->DrillIntoCategory(row);
}

void CRuliManagerDlg::CatGridOwner::GridKey(UINT vk)
{
	dlg->HandleCatKey(vk);
}

void CRuliManagerDlg::OnLvnCatColumnClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	m_catSortByCount = (p->iSubItem == 1);   // 영상 수 열: 많은 순, 그 외: 가나다순
	RebuildCategories();
	*pResult = 0;
}

// ---------------------------------------------------------------------------
// 목록 / 격자(썸네일) 표시

void CRuliManagerDlg::OnViewStyleChanged(UINT nID)
{
	const bool grid = (nID == IDC_RADIO_GRIDVIEW);
	if (grid == m_gridView)
		return;
	SetGridView(grid);
	AfxGetApp()->WriteProfileInt(L"Settings", L"GridView", grid ? 1 : 0);
}

void CRuliManagerDlg::SetGridView(bool grid)
{
	m_gridView = grid;
	const bool listHadFocus = (GetFocus() == &m_list) || (GetFocus() == &m_grid);
	const bool catHadFocus = (GetFocus() == &m_listCat) || (GetFocus() == &m_catGrid);

	UpdateLeftPane();
	if (IsActorGridMode())
		return;
	if (IsCategoryListMode())
	{
		const int sel = m_listCat.GetNextItem(-1, LVNI_SELECTED);
		if (CatGridActive())
		{
			m_catGrid.Refresh();
			m_catGrid.OnSelectionChanged();
			if (catHadFocus) m_catGrid.SetFocus();
		}
		else
		{
			if (sel >= 0) m_listCat.EnsureVisible(sel, FALSE);
			if (catHadFocus) m_listCat.SetFocus();
		}
		return;
	}

	// 영상은 항상 격자 표시
	m_grid.Refresh();
	m_grid.OnSelectionChanged();
	if (listHadFocus) m_grid.SetFocus();
}

// ---------------------------------------------------------------------------
// 격자 보기 데이터 제공 (IVideoGridOwner)

int CRuliManagerDlg::GridGetCount()
{
	return static_cast<int>(m_view.size());
}

int CRuliManagerDlg::GridGetSel()
{
	return GetSelectedRow();
}

void CRuliManagerDlg::GridSetSel(int row)
{
	if (row < 0 || row >= static_cast<int>(m_view.size()))
		return;
	// 선택 상태는 (숨겨진) 목록 컨트롤이 관리 → 기존 선택 처리 그대로 사용
	m_list.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	m_list.EnsureVisible(row, FALSE);
	m_grid.OnSelectionChanged();
}

void CRuliManagerDlg::GridGetItem(int row, int& image, CString& name, CString& release, CString& memo)
{
	if (row < 0 || row >= static_cast<int>(m_view.size()))
	{
		image = 0;
		return;
	}
	const int idx = m_view[row];
	const VideoItem& v = m_lib.items[idx];
	image = GetThumbIndex(idx);
	name = v.title.IsEmpty() ? v.FileName() : v.title;   // 제목이 있으면 제목
	release = v.pending ? CString(L"● 임시 (정보 저장 전)") : v.release;   // 발매일 (날짜만, 없으면 빈 줄)
	memo.Empty();   // 영상 메모 기능 삭제
}

void CRuliManagerDlg::GridActivate(int /*row*/)
{
	OnBnClickedOpenDefault();
}

void CRuliManagerDlg::GridKey(UINT vk)
{
	HandleListKey(vk);
}

int CRuliManagerDlg::AddThumbnail(CImage* src, LPCWSTR text)
{
	CClientDC screen(this);
	CDC mem;
	mem.CreateCompatibleDC(&screen);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&screen, m_thumbW, m_thumbH);
	CBitmap* oldBmp = mem.SelectObject(&bmp);

	mem.FillSolidRect(0, 0, m_thumbW, m_thumbH, kThumbBackColor);   // 이미지 여백 / "이미지 없음" 배경

	if (src && !src->IsNull() && src->GetWidth() > 0 && src->GetHeight() > 0)
	{
		// 비율 유지하여 칸에 맞춤
		const double scale = (std::min)(static_cast<double>(m_thumbW) / src->GetWidth(),
		                                static_cast<double>(m_thumbH) / src->GetHeight());
		const int w = (std::max)(1, static_cast<int>(src->GetWidth() * scale + 0.5));
		const int h = (std::max)(1, static_cast<int>(src->GetHeight() * scale + 0.5));
		const int x = (m_thumbW - w) / 2;
		const int y = (m_thumbH - h) / 2;
		mem.SetStretchBltMode(HALFTONE);
		::SetBrushOrgEx(mem.GetSafeHdc(), 0, 0, nullptr);
		src->Draw(mem.GetSafeHdc(), CRect(x, y, x + w, y + h));
	}
	else if (text)
	{
		CFont* oldFont = mem.SelectObject(GetFont());
		mem.SetBkMode(TRANSPARENT);
		mem.SetTextColor(RGB(150, 150, 150));
		CRect rc(0, 0, m_thumbW, m_thumbH);
		mem.DrawText(text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		mem.SelectObject(oldFont);
	}

	mem.SelectObject(oldBmp);
	return m_thumbs.Add(&bmp, static_cast<CBitmap*>(nullptr));
}

int CRuliManagerDlg::GetThumbIndex(int idx)
{
	if (idx < 0 || idx >= static_cast<int>(m_lib.items.size()))
		return 0;

	const CString key = CVideoLibrary::MakeKey(m_lib.items[idx].path);
	auto it = m_thumbIndex.find(key);
	if (it != m_thumbIndex.end())
		return it->second;

	// 메모리 사용을 제한하기 위해 캐시가 너무 커지면 비움
	if (m_thumbIndex.size() > 1500)
	{
		m_thumbs.Remove(-1);
		m_thumbIndex.clear();
		AddThumbnail(nullptr, L"이미지 없음");
		m_list.Invalidate(FALSE);   // 이미 받아간 인덱스가 무효가 되므로 다시 그림
		m_grid.Invalidate(FALSE);
		m_actorGrid.Invalidate(FALSE);
		m_catGrid.Invalidate(FALSE);
	}

	int image = 0;
	const CString file = FindImageFor(m_lib.items[idx].path);
	if (!file.IsEmpty())
	{
		CImage src;
		if (LoadImageFile(src, file))
			image = AddThumbnail(&src, nullptr);
	}
	m_thumbIndex[key] = image;
	return image;
}

void CRuliManagerDlg::ResetThumbnails()
{
	m_thumbs.Remove(-1);
	m_thumbIndex.clear();
	m_actorPortraits.clear();   // 배우 사진이 바뀌었을 수 있음
	m_videoCovers.clear();      // 영상 이미지가 추가/변경됐을 수 있음
	m_studioLogos.clear();      // 스튜디오 이미지가 바뀌었을 수 있음
	AddThumbnail(nullptr, L"이미지 없음");
	m_grid.Invalidate(FALSE);
	m_actorGrid.Invalidate(FALSE);
	m_catGrid.Invalidate(FALSE);
}

void CRuliManagerDlg::OnLvnGetDispInfo(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLVDISPINFO* p = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);
	LVITEM& it = p->item;
	*pResult = 0;

	const bool valid = (it.iItem >= 0 && it.iItem < static_cast<int>(m_view.size()));

	// 격자 보기: 썸네일 (보이는 항목만 요청되므로 필요할 때 불러옴)
	if (it.mask & LVIF_IMAGE)
		it.iImage = valid ? GetThumbIndex(m_view[it.iItem]) : 0;

	if (!(it.mask & LVIF_TEXT) || it.cchTextMax <= 0)
		return;
	if (!valid)
	{
		it.pszText[0] = 0;
		return;
	}

	const VideoItem& v = m_lib.items[m_view[it.iItem]];
	CString s;
	switch (it.iSubItem)
	{
	case COL_NAME:   s = v.FileName(); break;
	case COL_TITLE:  s = v.title; break;
	case COL_SIZE:   s = FormatSize(v.size); break;
	case COL_DATE:   s = FormatTime(v.modified); break;
	case COL_RATING: s = RatingText(v.rating); break;
	case COL_RELEASE: s = v.release; break;
	case COL_ACTORS: s = v.actors; break;
	case COL_STUDIO: s = v.studio; break;
	case COL_TAGS:   s = v.tags; break;
	case COL_PATH:   s = v.path; break;
	}
	_tcsncpy_s(it.pszText, it.cchTextMax, s, _TRUNCATE);
}

void CRuliManagerDlg::OnLvnItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	*pResult = 0;

	if ((p->uChanged & LVIF_STATE) &&
		(p->uNewState & LVIS_SELECTED) && !(p->uOldState & LVIS_SELECTED) &&
		p->iItem >= 0 && p->iItem < static_cast<int>(m_view.size()))
	{
		const int idx = m_view[p->iItem];
		if (idx != m_curItem)
		{
			CommitDetails();
			ShowDetails(idx);
		}
	}

	if (p->uChanged & LVIF_STATE)
	{
		if ((p->uNewState & LVIS_SELECTED) && !(p->uOldState & LVIS_SELECTED))
			m_grid.OnSelectionChanged();   // 선택 항목이 보이도록 스크롤
		else
			m_grid.Invalidate(FALSE);
	}
}

void CRuliManagerDlg::OnLvnColumnClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	if (p->iSubItem == m_sortColumn)
		m_sortAsc = !m_sortAsc;
	else
	{
		m_sortColumn = p->iSubItem;
		m_sortAsc = (m_sortColumn != COL_DATE && m_sortColumn != COL_RATING && m_sortColumn != COL_SIZE && m_sortColumn != COL_RELEASE);
	}
	UpdateSortArrows();
	ApplyFilter();
	*pResult = 0;
}

void CRuliManagerDlg::OnLvnKeyDown(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLVKEYDOWN* k = reinterpret_cast<NMLVKEYDOWN*>(pNMHDR);
	HandleListKey(k->wVKey);
	*pResult = 0;
}

void CRuliManagerDlg::HandleListKey(UINT vk)
{
	const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
	switch (vk)
	{
	case VK_DELETE: OnBnClickedDelete(); break;
	case VK_F2:     OnBnClickedRename(); break;
	case VK_F5:     OnBnClickedRefresh(); break;
	case VK_SPACE:  OnBnClickedOpenDefault(); break;
	case 'F':       if (ctrl) { m_editSearch.SetFocus(); m_editSearch.SetSel(0, -1); } break;
	case VK_BACK:   if (!m_drill.IsEmpty()) BackToList(); break;
	}
}

void CRuliManagerDlg::OnNmDblclkList(NMHDR* pNMHDR, LRESULT* pResult)
{
	// 더블클릭: 기본 플레이어(파일 연결 프로그램)로 열기
	NMITEMACTIVATE* p = reinterpret_cast<NMITEMACTIVATE*>(pNMHDR);
	if (p->iItem >= 0 && p->iItem < static_cast<int>(m_view.size()))
		OnBnClickedOpenDefault();
	*pResult = 0;
}

// ---------------------------------------------------------------------------
// 상세 정보 (별점 / 태그 / 메모)

void CRuliManagerDlg::ShowDetails(int idx)
{
	m_pendingImage.Empty();   // 다른 영상으로 바뀌면 저장 안 한 새 이미지는 버림 (CommitDetails 가 먼저 저장함)
	m_loadingDetails = true;
	m_curItem = (idx >= 0 && idx < static_cast<int>(m_lib.items.size())) ? idx : -1;

	const BOOL enable = (m_curItem >= 0);
	if (m_curItem >= 0)
	{
		const VideoItem& v = m_lib.items[m_curItem];
		m_staticName.SetWindowText(v.FileName());
		if (m_mediaInfo.GetSafeHwnd())
		{
			const int before = m_mediaInfo.NeededWidth();
			m_mediaInfo.SetFile(v.path);   // fps / 해상도 (경로별로 한 번만 읽음)
			if (m_layoutReady && m_mediaInfo.NeededWidth() != before)
			{
				CRect client;
				GetClientRect(&client);
				LayoutControls(client.Width(), client.Height());   // 칸 폭이 바뀌면 파일 이름 칸도 다시 맞춤
			}
		}
		m_starRating.SetRating(v.rating);
		m_dropCounter.SetCount(v.oCount);
		m_editCode.SetWindowText(v.code);
		m_editTitle.SetWindowText(v.title);
		SYSTEMTIME st = {};
		if (ParseDate(v.release, st))
			m_dateRelease.SetTime(&st);
		else
			m_dateRelease.SetTime(static_cast<LPSYSTEMTIME>(nullptr));   // 없음 (체크 해제)
		m_editActors.SetWindowText(v.actors);
		m_editVAliases.SetWindowText(v.actorAliases);
		m_editStudio.SetWindowText(v.studio);
		m_editTags.SetWindowText(v.tags);
	}
	else
	{
		m_staticName.SetWindowText(L"(선택된 동영상 없음)");
		if (m_mediaInfo.GetSafeHwnd() && m_mediaInfo.NeededWidth() > 0)
		{
			m_mediaInfo.SetFile(CString());
			if (m_layoutReady)
			{
				CRect client;
				GetClientRect(&client);
				LayoutControls(client.Width(), client.Height());
			}
		}
		m_starRating.SetRating(0);
		m_dropCounter.SetCount(0);
		m_editCode.SetWindowText(L"");
		m_editTitle.SetWindowText(L"");
		m_dateRelease.SetTime(static_cast<LPSYSTEMTIME>(nullptr));
		m_editActors.SetWindowText(L"");
		m_editVAliases.SetWindowText(L"");
		m_editStudio.SetWindowText(L"");
		m_editTags.SetWindowText(L"");
	}
	UpdatePreview(m_curItem);

	m_starRating.EnableWindow(enable);
	m_dropCounter.EnableWindow(enable);
	m_editCode.EnableWindow(enable);
	m_editTitle.EnableWindow(enable);
	GetDlgItem(IDC_BTN_CHANGEIMAGE)->EnableWindow(enable);
	GetDlgItem(IDC_BTN_SEARCHIMAGE)->EnableWindow(enable);
	m_dateRelease.EnableWindow(enable);
	m_editActors.EnableWindow(enable);
	GetDlgItem(IDC_BTN_PICKACTORS)->EnableWindow(enable);
	m_editVAliases.EnableWindow(enable);
	GetDlgItem(IDC_BTN_PICKVALIASES)->EnableWindow(enable);
	m_editStudio.EnableWindow(enable);
	GetDlgItem(IDC_BTN_PICKSTUDIO)->EnableWindow(enable);
	m_studioChips.EnableWindow(enable);
	GetDlgItem(IDC_BTN_PICKTAGS)->EnableWindow(enable);
	m_editTags.EnableWindow(enable);
	m_tagChips.EnableWindow(enable);
	m_actorChips.EnableWindow(enable);

	m_detailsDirty = false;
	// 임시 항목은 바꾼 내용이 없어도 [저장]으로 정식 DB에 등록할 수 있음
	const bool pending = (m_curItem >= 0 && m_lib.items[m_curItem].pending);
	GetDlgItem(IDC_BTN_SAVE)->EnableWindow(pending ? TRUE : FALSE);
	if (pending)
		m_staticName.SetWindowText(m_lib.items[m_curItem].FileName() + L"   [임시 - 저장하면 DB 등록]");
	m_loadingDetails = false;
}

void CRuliManagerDlg::OnDetailsChanged()
{
	if (m_loadingDetails || m_curItem < 0)
		return;
	m_detailsDirty = true;
	GetDlgItem(IDC_BTN_SAVE)->EnableWindow(TRUE);
}

void CRuliManagerDlg::OnDtnReleaseChanged(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	if (m_actorStrip.GetSafeHwnd())
		m_actorStrip.Invalidate(FALSE);   // 배우 카드의 제작 당시 나이 다시 계산
	OnDetailsChanged();
	*pResult = 0;
}

void CRuliManagerDlg::CommitDetails()
{
	if (!m_detailsDirty || m_curItem < 0 || m_curItem >= static_cast<int>(m_lib.items.size()))
		return;

	VideoItem& v = m_lib.items[m_curItem];
	v.rating = m_starRating.GetRating();
	v.oCount = m_dropCounter.GetCount();

	m_editCode.GetWindowText(v.code);
	v.code.Trim();
	m_editTitle.GetWindowText(v.title);
	v.title.Replace(L"\r\n", L" ");   // 붙여 넣은 줄바꿈은 공백으로 (제목은 한 줄 값, 칸에서만 줄바꿈 표시)
	v.title.Replace(L'\r', L' ');
	v.title.Replace(L'\n', L' ');
	v.title.Trim();

	SYSTEMTIME st = {};
	if (m_dateRelease.GetTime(&st) == GDT_VALID)
		v.release.Format(L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
	else
		v.release.Empty();

	CString actors;
	m_editActors.GetWindowText(actors);
	v.actors = NormalizeTags(actors);
	CString valiases;
	m_editVAliases.GetWindowText(valiases);
	v.actorAliases = NormalizeTags(valiases);
	m_lib.NormalizeVideoActors(v);   // 별칭으로 고른 배우 → 배우 이름 + 참여 별칭, 빠진 배우의 별칭 정리

	m_editStudio.GetWindowText(v.studio);
	v.studio.Trim();

	CString tags;
	m_editTags.GetWindowText(tags);
	v.tags = NormalizeTags(tags);


	// 새로 고른 이미지: 영상 파일 옆에 "영상이름.확장자" 로 저장
	if (!m_pendingImage.IsEmpty())
	{
		const CString src = m_pendingImage;
		m_pendingImage.Empty();
		// 분할 파일은 순번을 뗀 이름(ABC-123.jpg)으로 저장 → '_' 왼쪽 일치로 묶음 전체가 같은 이미지 사용
		bool ok = ApplyVideoImage(CVideoLibrary::PartGroupPath(v.path), src);
		if (ok)
		{
			const std::vector<size_t> sibs = m_lib.PartSiblings(static_cast<size_t>(m_curItem));
			if (!sibs.empty() || CVideoLibrary::PartGroupPath(v.path) != v.path)
			{
				// 파일별 이미지(ABC-123_2.jpg 등)가 남아 있으면 그쪽이 먼저 보이므로 휴지통으로
				std::vector<CString> paths;
				paths.push_back(v.path);
				for (size_t si : sibs)
					paths.push_back(m_lib.items[si].path);
				RecyclePartImages(paths, CVideoLibrary::PartGroupPath(v.path));
			}
		}
		if (ok)
		{
			ResetThumbnails();      // 카드 / 목록 이미지 다시 읽기
			UpdatePreview(m_curItem);
		}
		else
			AfxMessageBox(L"이미지를 영상 폴더에 저장하지 못했습니다.", MB_ICONWARNING);
	}

	// 영상 정보를 저장하면 임시 목록에서 정식 DB로 옮김
	const bool registered = v.pending;
	v.pending = false;
	// 분할 파일: 같은 묶음의 다른 파일도 같은 정보로 (임시 파일도 함께 등록)
	if (m_lib.SyncPartGroup(static_cast<size_t>(m_curItem)) > 0)
		UpdateStatus();

	m_detailsDirty = false;
	GetDlgItem(IDC_BTN_SAVE)->EnableWindow(FALSE);
	if (registered)
	{
		m_staticName.SetWindowText(v.FileName());
		UpdateStatus();
	}

	m_loadingDetails = true;
	m_editActors.SetWindowText(v.actors);
	m_editVAliases.SetWindowText(v.actorAliases);
	m_editStudio.SetWindowText(v.studio);
	m_editTags.SetWindowText(v.tags);
	m_loadingDetails = false;

	// 직접 입력한 새 배우/스튜디오/태그는 각 목록에도 추가
	m_lib.SyncActorsFromVideos();
	m_lib.SyncNamedFromVideos();

	if (!m_lib.Save())
		AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);
	m_list.Invalidate(FALSE);
	m_grid.Invalidate(FALSE);
	MarkCategoriesDirty();   // 배우/스튜디오/태그 개수 갱신
}

void CRuliManagerDlg::OnBnClickedSave()
{
	// 임시 항목은 바꾼 내용이 없어도 저장 → 정식 DB 등록
	if (m_curItem >= 0 && m_curItem < static_cast<int>(m_lib.items.size()) && m_lib.items[m_curItem].pending)
		m_detailsDirty = true;
	CommitDetails();
}

// ---------------------------------------------------------------------------
// 배우 관리 / 선택

void CRuliManagerDlg::OpenActorManager(const CString& selectName)
{
	CommitDetails();

	const CString prevSel = m_actorSelName;   // 배우 탭에서 선택하던 배우 (이름이 바뀌어도 다시 찾기 위해)
	CActorDlg dlg(m_lib, selectName, this);
	dlg.DoModal();
	if (!dlg.m_changed)
		return;

	// 대표 이름이 바뀌었으면(이름 콤보에서 다른 별칭을 골라 저장) 예전 이름은 별칭이 되므로 별칭으로 다시 찾아 선택 유지
	//  - 그대로 두면 선택이 풀려 배우 탭 오른쪽 사진/정보가 비어 보였음
	if (!prevSel.IsEmpty() && m_lib.FindActor(prevSel) < 0)
	{
		const int idx = m_lib.FindActorByAnyName(prevSel);
		if (idx >= 0)
			m_actorSelName = m_lib.actors[idx].name;
	}

	// 배우 이름 변경/삭제가 동영상에도 반영되었을 수 있으므로 화면 갱신
	m_lib.NormalizeAllVideoActors();   // 새로 등록한 별칭으로 적힌 배우 → 배우 이름 + 참여 별칭
	m_lib.Save();
	const int cur = m_curItem;
	ResetThumbnails();   // 배우 사진이 바뀌었을 수 있음
	if (m_mode == MODE_ACTOR && !m_drill.IsEmpty() && m_lib.FindActor(m_drill) < 0)
	{
		BackToList();   // 보고 있던 배우가 삭제/이름 변경됨
		return;
	}
	RebuildCategories();
	if (IsActorGridMode())
		ShowActorInfo(SelectedActorIndex());
	ApplyFilter();
	if (cur >= 0 && cur == m_curItem)
		ShowDetails(cur);
	m_list.Invalidate(FALSE);
	m_grid.Invalidate(FALSE);
}

void CRuliManagerDlg::OnBnClickedSettings()
{
	CommitDetails();
	CSettingsDlg dlg(m_lib, this);
	dlg.m_onDeleteDb = [this](int mask) { DeleteDb(mask); };
	dlg.DoModal();
}

void CRuliManagerDlg::DeleteDb(int mask)
{
	// 설정 창 [선택한 DB 삭제]: 영상 / 배우 / 스튜디오 / 태그 (파일은 지우지 않음)
	BeginLibraryChange();   // 항목 인덱스가 바뀌기 전에 목록/상세 비움
	if (mask & CSettingsDlg::DB_VIDEO)
		m_lib.items.clear();   // 저장한 정보 + 임시 항목 (다시 스캔하면 임시 항목으로)
	if (mask & CSettingsDlg::DB_ACTOR)
	{
		m_lib.actors.clear();
		for (VideoItem& v : m_lib.items)   // 영상의 배우 지정도 지워야 다시 생기지 않음
		{
			v.actors.Empty();
			v.actorAliases.Empty();
		}
	}
	if (mask & CSettingsDlg::DB_STUDIO)
	{
		m_lib.studios.clear();
		for (VideoItem& v : m_lib.items)
			v.studio.Empty();
	}
	if (mask & CSettingsDlg::DB_TAG)
	{
		m_lib.tagInfos.clear();
		for (VideoItem& v : m_lib.items)
			v.tags.Empty();
	}
	if (!m_lib.Save())
		AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);

	// 화면 갱신
	ResetThumbnails();
	m_actorSel = -1;
	m_actorSelName.Empty();
	m_catSelKind = -1;
	m_catSelValue.Empty();
	if (m_mode != MODE_VIDEO && !m_drill.IsEmpty())
	{
		BackToList();
		return;
	}
	RebuildCategories();
	ApplyFilter();
	if (IsActorGridMode())
		ShowActorInfo(-1);
	UpdateStatus();
}

void CRuliManagerDlg::OnBnClickedActors()
{
	// [목록 관리 ▾] → 배우 / 스튜디오 / 태그
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_MANAGE_ACTORS,  L"배우 관리...");
	menu.AppendMenu(MF_STRING, ID_MANAGE_STUDIOS, L"스튜디오 관리...");
	menu.AppendMenu(MF_STRING, ID_MANAGE_TAGS,    L"태그 관리...");
	CRect rc;
	GetDlgItem(IDC_BTN_ACTORS)->GetWindowRect(&rc);
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON, rc.left, rc.bottom, this);
}

void CRuliManagerDlg::OnManageActors()
{
	CString select;
	if (!m_drill.IsEmpty())
		select = m_drill;
	else if (IsActorGridMode() && SelectedActorIndex() >= 0)
		select = m_lib.actors[SelectedActorIndex()].name;
	OpenActorManager(select);
}

void CRuliManagerDlg::OnNmDblclkCategory(NMHDR* pNMHDR, LRESULT* pResult)
{
	// 스튜디오/태그 목록에서 항목을 더블클릭하면 해당 영상 목록으로
	NMITEMACTIVATE* p = reinterpret_cast<NMITEMACTIVATE*>(pNMHDR);
	*pResult = 0;
	if (IsCategoryListMode() && p->iItem >= 0 && p->iItem < static_cast<int>(m_catRows.size()))
		DrillIntoCategory(p->iItem);
}

void CRuliManagerDlg::OnManageStudios()
{
	CString select;
	if (m_mode == MODE_STUDIO && m_catKind == CAT_VALUE && !m_drill.IsEmpty())
		select = m_catValue;
	else if (m_mode == MODE_STUDIO && m_catSelKind == CAT_VALUE)
		select = m_catSelValue;
	else if (m_curItem >= 0)
		select = m_lib.items[m_curItem].studio;
	OpenListManager(LIST_STUDIO, select);
}

void CRuliManagerDlg::OnManageTags()
{
	CString select;
	if (m_mode == MODE_TAG && m_catKind == CAT_VALUE && !m_drill.IsEmpty())
		select = m_catValue;
	else if (m_mode == MODE_TAG && m_catSelKind == CAT_VALUE)
		select = m_catSelValue;
	OpenListManager(LIST_TAG, select);
}

void CRuliManagerDlg::OpenListManager(int kind, const CString& selectName)
{
	CommitDetails();

	CNameListDlg dlg(m_lib, kind, selectName, this);
	dlg.DoModal();
	if (!dlg.m_changed)
		return;

	m_lib.Save();
	const int cur = m_curItem;
	const int mode = (kind == LIST_STUDIO) ? MODE_STUDIO : MODE_TAG;
	if (m_mode == mode && !m_drill.IsEmpty() && m_catKind == CAT_VALUE && m_lib.FindNamed(kind, m_catValue) < 0)
	{
		BackToList();   // 보고 있던 항목이 삭제/이름 변경됨
		return;
	}
	RebuildCategories();
	if (IsCategoryListMode())
		ShowNamedInfo(m_listCat.GetNextItem(-1, LVNI_SELECTED));
	ApplyFilter();
	if (cur >= 0 && cur == m_curItem)
		ShowDetails(cur);
	m_list.Invalidate(FALSE);
	m_grid.Invalidate(FALSE);
}

void CRuliManagerDlg::PickNamed(int kind)
{
	if (m_curItem < 0)
		return;

	CEdit& edit = (kind == LIST_STUDIO) ? m_editStudio : m_editTags;
	CString current;
	edit.GetWindowText(current);

	CNamePickDlg dlg(m_lib, kind, current, this);
	const bool ok = (dlg.DoModal() == IDOK);
	if (ok && dlg.m_result != current)
	{
		edit.SetWindowText(dlg.m_result);
		OnDetailsChanged();
		CommitDetails();   // 저장 + 분류 갱신
	}
	else if (dlg.m_added)   // 취소했어도 새로 만든 항목은 유지
	{
		m_lib.Save();
		MarkCategoriesDirty();
	}
}

void CRuliManagerDlg::OnStnClickedRatingLabel()
{
	// [별점] 라벨을 클릭하면 별점 0점(없음)으로 리셋 + 바로 저장
	if (m_curItem < 0 || !m_starRating.IsWindowEnabled() || m_starRating.GetRating() == 0)
		return;
	m_starRating.SetRating(0);
	OnDetailsChanged();
	CommitDetails();
	m_grid.Invalidate(FALSE);   // 카드의 별점 리본
}

void CRuliManagerDlg::OnBnClickedPickStudio()
{
	PickNamed(LIST_STUDIO);
}

void CRuliManagerDlg::ApplyDefaultAliases(const CString& oldActors, const CString& newActors)
{
	// 새로 추가된 배우(칩 입력, 배우 선택 창)는 그 배우의 마지막 고른 별칭을 이 작품의 별칭으로 넣음
	std::vector<int> before;
	for (const CString& n : CVideoLibrary::SplitList(oldActors))
		before.push_back(m_lib.FindActorByAnyName(n));
	CString credited;
	m_editVAliases.GetWindowText(credited);
	std::vector<CString> list = CVideoLibrary::SplitList(credited);
	bool changed = false;
	for (const CString& n : CVideoLibrary::SplitList(newActors))
	{
		const int idx = m_lib.FindActorByAnyName(n);
		if (idx < 0 || std::find(before.begin(), before.end(), idx) != before.end())
			continue;
		const ActorInfo& a = m_lib.actors[idx];
		if (n.CompareNoCase(a.name) != 0)
			continue;   // 별칭으로 직접 입력했으면 그 별칭 그대로
		if (a.lastAlias.IsEmpty() || a.lastAlias.CompareNoCase(a.name) == 0)
			continue;
		bool valid = false;
		for (const CString& al : CVideoLibrary::SplitList(a.aliases))
			if (al.CompareNoCase(a.lastAlias) == 0) { valid = true; break; }
		if (!valid)
			continue;
		bool has = false;
		for (const CString& al : list)
			if (m_lib.FindActorByAnyName(al) == idx) { has = true; break; }
		if (has)
			continue;
		list.push_back(a.lastAlias);
		changed = true;
	}
	if (changed)
		m_editVAliases.SetWindowText(CVideoLibrary::JoinList(list));   // EN_CHANGE → 칩 갱신
}

CString CRuliManagerDlg::ActorChipDisplay(const CString& tag)
{
	const int idx = m_lib.FindActorByAnyName(tag);
	if (idx < 0)
		return tag;
	// 1) 이 작품에서 고른 별칭 (숨긴 별칭 칸 중 이 배우의 것, 마지막 것)
	CString credited;
	m_editVAliases.GetWindowText(credited);
	CString pick;
	for (const CString& al : CVideoLibrary::SplitList(credited))
		if (m_lib.FindActorByAnyName(al) == idx)
			pick = al;
	if (!pick.IsEmpty())
		return pick;
	// 2) 칩이 별칭으로 적혀 있으면(저장 전) 그 별칭, 3) 배우 대표 이름
	return tag;
}

void CRuliManagerDlg::OnActorChipMenu(int index, CPoint screenPt)
{
	if (m_curItem < 0 || index < 0 || index >= static_cast<int>(m_actorChips.Tags().size()))
		return;
	const CString tag = m_actorChips.Tags()[index];
	const int idx = m_lib.FindActorByAnyName(tag);
	if (idx < 0)
		return;
	const ActorInfo& a = m_lib.actors[idx];
	// 메뉴에는 별칭만 (대표 이름은 고르지 않음, 체크된 별칭을 다시 고르면 해제 → 대표 이름 표시)
	std::vector<CString> names;
	for (const CString& al : CVideoLibrary::SplitList(a.aliases))
		if (al.CompareNoCase(a.name) != 0)
			names.push_back(al);
	const CString current = ActorChipDisplay(tag);

	CMenu menu;
	menu.CreatePopupMenu();
	if (names.empty())
		menu.AppendMenu(MF_STRING | MF_GRAYED, 0, L"(등록된 별칭 없음)");
	for (size_t i = 0; i < names.size(); ++i)
		menu.AppendMenu(MF_STRING | (names[i].CompareNoCase(current) == 0 ? MF_CHECKED : 0), static_cast<UINT_PTR>(i + 1), names[i]);
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, 1000, L"배우 관리에서 편집...");
	const UINT cmd = menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY, screenPt.x, screenPt.y, this);
	if (cmd == 0)
		return;
	if (cmd == 1000)
	{
		OpenActorManager(a.name);
		return;
	}
	if (cmd > names.size())
		return;
	CString chosen = names[cmd - 1];
	if (chosen.CompareNoCase(current) == 0)
		chosen = a.name;   // 체크된 별칭을 다시 고르면 해제 (대표 이름으로 표시)

	// 이 작품의 별칭: 이 배우 것은 고른 별칭으로 교체 (해제하면 별칭 없음)
	CString credited;
	m_editVAliases.GetWindowText(credited);
	std::vector<CString> list;
	for (const CString& al : CVideoLibrary::SplitList(credited))
		if (m_lib.FindActorByAnyName(al) != idx)
			list.push_back(al);
	if (chosen.CompareNoCase(a.name) != 0)
		list.push_back(chosen);
	// 다음에 이 배우를 넣을 때 초기값으로 쓸 별칭 (해제하면 대표 이름)
	m_lib.actors[idx].lastAlias = (chosen.CompareNoCase(a.name) != 0) ? chosen : CString();

	// 칩이 별칭으로 적혀 있으면 배우 대표 이름으로 정리
	CString actorsText;
	m_editActors.GetWindowText(actorsText);
	std::vector<CString> actors = CVideoLibrary::SplitList(actorsText);
	for (CString& n : actors)
		if (m_lib.FindActorByAnyName(n) == idx)
			n = a.name;

	m_editVAliases.SetWindowText(CVideoLibrary::JoinList(list));     // EN_CHANGE → 칩 갱신
	m_editActors.SetWindowText(CVideoLibrary::JoinList(CVideoLibrary::SplitList(CVideoLibrary::JoinList(actors))));
	OnDetailsChanged();
	CommitDetails();   // 바로 저장
}

void CRuliManagerDlg::OnEnChangeVAliases()
{
	// 참여 별칭이 바뀌면 배우 칩 표시(별칭)도 갱신
	if (m_actorChips.GetSafeHwnd())
		m_actorChips.SetTags(m_actorChips.Tags());
	OnDetailsChanged();
}

void CRuliManagerDlg::OnEnChangeStudio()
{
	// 숨긴 스튜디오 칸이 바뀌면 (영상 선택, [선택...] 결과, 저장 후 정리) 칩도 갱신
	if (!m_syncingStudio && m_studioChips.GetSafeHwnd())
	{
		CString text;
		m_editStudio.GetWindowText(text);
		text.Trim();
		std::vector<CString> tags;
		if (!text.IsEmpty())
			tags.push_back(text);
		m_studioChips.SetTags(tags);
	}
	OnDetailsChanged();
}

void CRuliManagerDlg::OnEnChangeActors()
{
	// 숨긴 배우 칸이 바뀌면 (영상 선택, [선택...] 결과, 별칭 칸에서 추가, 저장 후 정리) 칩도 갱신
	if (!m_syncingActors)
	{
		CString text;
		m_editActors.GetWindowText(text);
		m_actorChips.SetTags(CVideoLibrary::SplitList(text));
	}
	RefreshActorStrip();   // 태그 아래 배우 카드도 갱신
	OnDetailsChanged();
}

void CRuliManagerDlg::OnEnChangeTags()
{
	// 숨긴 태그 칸이 바뀌면 (영상 선택, [선택...] 결과, 저장 후 정리) 칩도 갱신
	if (!m_syncingTags)
	{
		CString text;
		m_editTags.GetWindowText(text);
		m_tagChips.SetTags(CVideoLibrary::SplitList(text));
	}
	OnDetailsChanged();
}

void CRuliManagerDlg::OnBnClickedPickVAliases()
{
	if (m_curItem < 0)
		return;

	CString actorsText, current;
	m_editActors.GetWindowText(actorsText);
	m_editVAliases.GetWindowText(current);
	std::vector<CString> chosen = CVideoLibrary::SplitList(current);

	// 이 영상 배우들의 별칭 목록 (메뉴: "별칭   (배우)")
	struct Entry { CString alias; CString actor; };
	std::vector<Entry> entries;
	for (const CString& n : CVideoLibrary::SplitList(actorsText))
	{
		const int idx = m_lib.FindActorByAnyName(n);
		if (idx < 0)
			continue;
		for (const CString& al : CVideoLibrary::SplitList(m_lib.actors[idx].aliases))
			entries.push_back({ al, m_lib.actors[idx].name });
	}

	auto isChosen = [&](const CString& s)
	{
		for (const CString& c : chosen)
			if (c.CompareNoCase(s) == 0) return true;
		return false;
	};

	CMenu menu;
	menu.CreatePopupMenu();
	if (entries.empty())
	{
		menu.AppendMenu(MF_STRING | MF_GRAYED, 0,
			actorsText.IsEmpty() ? L"(먼저 배우를 지정하세요)" : L"(지정된 배우에 등록된 별칭이 없습니다 - 배우 관리에서 추가)");
	}
	else
	{
		for (size_t i = 0; i < entries.size(); ++i)
		{
			CString label = entries[i].alias + L"\t" + entries[i].actor;
			menu.AppendMenu(MF_STRING | (isChosen(entries[i].alias) ? MF_CHECKED : 0),
				static_cast<UINT_PTR>(i + 1), label);
		}
	}
	if (!chosen.empty())
	{
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, 9999, L"모두 지우기");
	}

	CRect rc;
	GetDlgItem(IDC_BTN_PICKVALIASES)->GetWindowRect(&rc);
	const UINT cmd = menu.TrackPopupMenu(TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
		rc.right, rc.bottom, this);
	if (cmd == 0)
		return;

	if (cmd == 9999)
	{
		chosen.clear();
	}
	else if (cmd <= entries.size())
	{
		const CString& al = entries[cmd - 1].alias;
		if (isChosen(al))
			chosen.erase(std::remove_if(chosen.begin(), chosen.end(),
				[&](const CString& c) { return c.CompareNoCase(al) == 0; }), chosen.end());
		else
			chosen.push_back(al);
	}

	const CString result = CVideoLibrary::JoinList(chosen);
	if (result != current)
	{
		m_editVAliases.SetWindowText(result);
		OnDetailsChanged();
		CommitDetails();   // 저장
	}
}

void CRuliManagerDlg::OnBnClickedPickTags()
{
	PickNamed(LIST_TAG);
}

void CRuliManagerDlg::OnBnClickedPickActors()
{
	if (m_curItem < 0)
		return;

	CString current;
	m_editActors.GetWindowText(current);

	CActorPickDlg dlg(m_lib, current, this);
	if (dlg.DoModal() != IDOK)
	{
		if (dlg.m_added)   // 취소했어도 새로 만든 배우는 유지
		{
			m_lib.Save();
			MarkCategoriesDirty();
		}
		return;
	}

	if (dlg.m_result != current)
	{
		m_editActors.SetWindowText(dlg.m_result);
		ApplyDefaultAliases(current, dlg.m_result);
		OnDetailsChanged();
		CommitDetails();   // 저장 + 분류 갱신
	}
	else if (dlg.m_added)
	{
		m_lib.Save();
		MarkCategoriesDirty();
	}
}

// ---------------------------------------------------------------------------
// 배우 보기: 배우 격자 → (더블클릭) 출연작 → [◀ 배우 목록]

void CRuliManagerDlg::UpdateLeftPane()
{
	const bool actorGrid = IsActorGridMode();
	const bool catList = IsCategoryListMode();
	const bool drill = (m_mode != MODE_VIDEO && !m_drill.IsEmpty());
	const bool showVideos = !actorGrid && !catList;

	m_listCat.ShowWindow(catList && !CatGridActive() ? SW_SHOW : SW_HIDE);
	m_catGrid.SetShowImage(m_mode != MODE_TAG);   // 태그 카드는 이미지 없이 글자만
	if (m_mode == MODE_STUDIO || m_mode == MODE_TAG)
		m_catGrid.SetFixedTile(m_scardW, m_scardH);   // 스튜디오: 로고 카드, 태그: 꼬리표 카드
	else
		m_catGrid.SetFixedTile(0, 0);
	m_catGrid.ShowWindow(catList && CatGridActive() ? SW_SHOW : SW_HIDE);
	m_actorGrid.ShowWindow(actorGrid ? SW_SHOW : SW_HIDE);
	GetDlgItem(IDC_BTN_ACTOR_BACK)->ShowWindow(drill ? SW_SHOW : SW_HIDE);
	GetDlgItem(IDC_STATIC_ACTOR_TITLE)->ShowWindow(drill ? SW_SHOW : SW_HIDE);
	// 영상은 격자만 표시 (목록 컨트롤은 숨겨 둔 채 선택/데이터 관리용으로만 사용)
	m_list.ShowWindow(SW_HIDE);
	m_grid.ShowWindow(showVideos ? SW_SHOW : SW_HIDE);

	// [표시: 목록 | 격자]는 스튜디오/태그 목록에서만, [정렬]은 영상이 보일 때만
	const UINT viewIds[] = { IDC_STATIC_VIEW, IDC_RADIO_LISTVIEW, IDC_RADIO_GRIDVIEW };
	for (UINT id : viewIds)
		GetDlgItem(id)->ShowWindow(SW_HIDE);   // 모든 화면이 격자 표시라 [표시: 목록 | 격자]는 사용하지 않음
	const UINT sortIds[] = { IDC_STATIC_SORT, IDC_COMBO_SORT, IDC_BTN_SORTDIR };
	for (UINT id : sortIds)
		GetDlgItem(id)->ShowWindow(showVideos ? SW_SHOW : SW_HIDE);

	// 영상 상세 정보(편집 칸)는 영상 탭 + 배우 출연작 화면에서 표시 (태그 아래 배우 카드는 영상 탭에서만)
	const bool showDetail = ShowVideoDetail();
	if (!showDetail)
		HideSuggestions();
	const UINT detailIds[] = {
		IDC_STATIC_NAME_LBL,
		IDC_STATIC_CODE_LBL, IDC_EDIT_CODE, IDC_STATIC_TITLE_LBL, IDC_EDIT_TITLE, IDC_BTN_CHANGEIMAGE, IDC_BTN_SEARCHIMAGE, IDC_MEDIA_INFO,
		IDC_STATIC_RATING_LBL, IDC_COMBO_RATING, IDC_DROP_COUNTER, IDC_BTN_SAVE,
		IDC_STATIC_RELEASE_LBL, IDC_DATE_RELEASE,
		IDC_STATIC_ACTORS_LBL, IDC_ACTOR_CHIPS,
		IDC_STATIC_STUDIO_LBL, IDC_STUDIO_CHIPS,
		IDC_STATIC_TAGS_LBL, IDC_TAG_CHIPS
	};
	for (UINT id : detailIds)
	{
		if (CWnd* w = GetDlgItem(id))
			w->ShowWindow(showDetail ? SW_SHOW : SW_HIDE);
	}
	if (m_actorStrip.GetSafeHwnd())
		m_actorStrip.ShowWindow(m_mode == MODE_VIDEO ? SW_SHOW : SW_HIDE);
	// 배우 별점은 배우 격자 화면에서만
	// 배우 격자 화면: 이름/정보 두 줄 대신 배우 상세 패널
	// 태그 탭: 오른쪽 상세 페이지(이미지 + 이름/정보) 자체를 숨김
	const bool noDetailPane = (m_mode == MODE_TAG);
	if (m_actorPanel.GetSafeHwnd())
		m_actorPanel.ShowWindow(actorGrid ? SW_SHOW : SW_HIDE);
	m_staticName.ShowWindow(actorGrid || noDetailPane ? SW_HIDE : SW_SHOW);
	m_staticImage.ShowWindow(actorGrid || noDetailPane ? SW_HIDE : SW_SHOW);
	m_preview.ShowWindow(noDetailPane ? SW_HIDE : SW_SHOW);

	if (m_layoutReady)
	{
		CRect client;
		GetClientRect(&client);
		LayoutControls(client.Width(), client.Height());
	}
}

int CRuliManagerDlg::SelectedActorIndex() const
{
	if (m_actorSel < 0 || m_actorSel >= static_cast<int>(m_actorRows.size()))
		return -1;
	return m_actorRows[m_actorSel];
}

void CRuliManagerDlg::RebuildActorGrid()
{
	CString query;
	m_editSearch.GetWindowText(query);
	query.Trim();
	query.MakeLower();

	// 출연작 수
	m_actorCounts.clear();
	const std::map<CString, int> nameIndex = m_lib.ActorNameIndex();
	for (const VideoItem& v : m_lib.items)
	{
		std::set<CString> seen;   // 이름과 별칭이 함께 적혀 있어도 한 편으로
		for (const CString& n : CVideoLibrary::SplitList(v.actors))
		{
			const CString key = m_lib.ActorKeyOf(nameIndex, n);   // 별칭 → 배우 이름
			if (seen.insert(key).second)
				++m_actorCounts[key];
		}
	}


	m_actorRows.clear();
	for (size_t i = 0; i < m_lib.actors.size(); ++i)
	{
		const ActorInfo& a = m_lib.actors[i];
		if (!query.IsEmpty())
		{
			CString hay = a.name + L"\n" + a.aliases;
			hay.MakeLower();
			if (hay.Find(query) < 0)
				continue;
		}
		m_actorRows.push_back(static_cast<int>(i));
	}
	std::sort(m_actorRows.begin(), m_actorRows.end(), [this](int a, int b)
	{
		// 즐겨찾기한 배우를 먼저, 그 안에서는 이름 순
		const ActorInfo& x = m_lib.actors[a];
		const ActorInfo& y = m_lib.actors[b];
		if (x.favorite != y.favorite)
			return x.favorite;
		return ::StrCmpLogicalW(x.name, y.name) < 0;
	});

	// 이전 선택 복원
	m_actorSel = -1;
	for (size_t row = 0; row < m_actorRows.size(); ++row)
	{
		if (m_lib.actors[m_actorRows[row]].name.CompareNoCase(m_actorSelName) == 0)
		{
			m_actorSel = static_cast<int>(row);
			break;
		}
	}

	m_actorGrid.Refresh();
	if (m_actorSel >= 0)
		m_actorGrid.OnSelectionChanged();
	if (IsActorGridMode() || IsCategoryListMode())
		UpdateStatus();
	RefreshActorStrip();   // 배우 정보(사진·별점·즐겨찾기)가 바뀌었을 수 있음
}

int CRuliManagerDlg::GetActorThumbIndex(int actorIdx)
{
	if (actorIdx < 0 || actorIdx >= static_cast<int>(m_lib.actors.size()))
		return 0;
	const ActorInfo& a = m_lib.actors[actorIdx];
	if (a.photo.IsEmpty())
		return 0;

	CString key = L"actor:" + a.name + L"|" + a.photo;
	key.MakeLower();
	auto it = m_thumbIndex.find(key);
	if (it != m_thumbIndex.end())
		return it->second;

	int image = 0;
	CImage src;
	if (::PathFileExistsW(a.photo) && LoadImageFile(src, a.photo))
		image = AddThumbnail(&src, nullptr);
	m_thumbIndex[key] = image;
	return image;
}

void CRuliManagerDlg::SelectActorRow(int row)
{
	if (row < 0 || row >= static_cast<int>(m_actorRows.size()))
		return;
	m_actorSel = row;
	m_actorSelName = m_lib.actors[m_actorRows[row]].name;
	m_actorGrid.OnSelectionChanged();
	ShowActorInfo(m_actorRows[row]);
}

void CRuliManagerDlg::ShowActorInfo(int actorIdx)
{
	// 오른쪽 패널: 배우 사진과 정보 (동영상 편집 칸은 비활성)
	CommitDetails();
	ShowDetails(-1);
	const bool validActor = (actorIdx >= 0 && actorIdx < static_cast<int>(m_lib.actors.size()));
	m_actorInfoIdx = validActor ? actorIdx : -1;
	if (!validActor)
	{
		m_staticName.SetWindowText(L"(선택된 배우 없음)");
		UpdateActorPanel(nullptr, 0);
		m_preview.SetPlaceholder(L"배우를 선택하세요.");
		return;
	}

	const ActorInfo& a = m_lib.actors[actorIdx];
	CString key = a.name;
	key.MakeLower();
	auto it = m_actorCounts.find(key);
	const int count = (it != m_actorCounts.end()) ? it->second : 0;

	const int age = CVideoLibrary::CalcAge(a.birth);
	CString title = L"배우: " + a.name;
	if (age >= 0)
	{
		CString t;
		t.Format(L"  (만 %d세)", age);
		title += t;
	}
	m_staticName.SetWindowText(title);
	UpdateActorPanel(&a, count);   // 오른쪽 아래 배우 상세

	// 생년월일 · 국적 · 키 · 데뷔일 · 출연 수 · 별칭
	std::vector<CString> parts;
	if (!a.birth.IsEmpty())       parts.push_back(L"생년월일 " + a.birth);
	if (!a.gender.IsEmpty())      parts.push_back(a.gender);
	if (!a.nationality.IsEmpty()) parts.push_back(a.nationality);
	if (!a.height.IsEmpty())      parts.push_back(a.height + L"cm");
	if (!a.debut.IsEmpty())       parts.push_back(L"데뷔 " + a.debut);
	if (!a.retire.IsEmpty())      parts.push_back(L"은퇴 " + a.retire);
	CString cnt;
	cnt.Format(L"출연 %d편", count);
	parts.push_back(cnt);
	if (!a.aliases.IsEmpty())     parts.push_back(L"별칭: " + a.aliases);
	CString info;
	for (const CString& p : parts)
	{
		if (!info.IsEmpty()) info += L" · ";
		info += p;
	}
	m_staticImage.SetWindowText(info);

	m_preview.Clear();
	if (!a.photo.IsEmpty() && ::PathFileExistsW(a.photo))
		m_preview.SetImageFile(a.photo);
	else
		m_preview.SetPlaceholder(L"사진 없음\n\n[배우 관리]에서 사진을 지정할 수 있습니다.");
}

void CRuliManagerDlg::DrillIntoActor(int row)
{
	if (row < 0 || row >= static_cast<int>(m_actorRows.size()))
		return;
	const ActorInfo& a = m_lib.actors[m_actorRows[row]];

	CommitDetails();
	m_actorSel = row;
	m_actorSelName = a.name;
	m_drill = a.name;
	m_catKind = CAT_VALUE;
	m_catValue = a.name;

	// 배우 검색어는 보관하고, 출연작은 검색어 없이 전부 표시
	m_editSearch.GetWindowText(m_savedSearch);
	m_editSearch.SetWindowText(L"");   // EN_CHANGE → ApplyFilter

	CString key = a.name;
	key.MakeLower();
	auto it = m_actorCounts.find(key);
	CString title;
	title.Format(L"배우: %s  (출연 %d편)", static_cast<LPCWSTR>(a.name),
		it != m_actorCounts.end() ? it->second : 0);
	SetDlgItemText(IDC_STATIC_ACTOR_TITLE, title);
	SetDlgItemText(IDC_BTN_ACTOR_BACK, L"◀ 배우 목록");

	ShowDetails(-1);
	UpdateLeftPane();
	ApplyFilter();
	m_grid.SetFocus();
}

void CRuliManagerDlg::DrillIntoCategory(int row)
{
	if (row < 0 || row >= static_cast<int>(m_catRows.size()))
		return;
	const CatRow r = m_catRows[row];   // 복사 (목록이 다시 만들어질 수 있음)
	const CString label = m_listCat.GetItemText(row, 0);
	const CString count = m_listCat.GetItemText(row, 1);
	const CString kindName = (m_mode == MODE_STUDIO) ? L"스튜디오" : L"태그";

	CommitDetails();
	m_catSelKind = r.kind;
	m_catSelValue = r.value;
	m_drill = label;
	m_catKind = r.kind;
	m_catValue = r.value;

	// 목록 검색어는 보관하고, 영상은 검색어 없이 전부 표시
	m_editSearch.GetWindowText(m_savedSearch);
	m_editSearch.SetWindowText(L"");   // EN_CHANGE → ApplyFilter

	SetDlgItemText(IDC_STATIC_ACTOR_TITLE, kindName + L": " + label + L"  (영상 " + count + L"편)");
	SetDlgItemText(IDC_BTN_ACTOR_BACK, L"◀ " + kindName + L" 목록");

	ShowDetails(-1);
	UpdateLeftPane();
	ApplyFilter();
	m_grid.SetFocus();
}

void CRuliManagerDlg::ShowNamedInfo(int row)
{
	// 오른쪽 패널: 스튜디오/태그 이미지와 정보 (동영상 편집 칸은 비활성)
	CommitDetails();
	ShowDetails(-1);
	const CString kindName = (m_mode == MODE_STUDIO) ? L"스튜디오" : L"태그";
	if (row < 0 || row >= static_cast<int>(m_catRows.size()))
	{
		m_staticName.SetWindowText(L"(선택된 " + kindName + L" 없음)");
		m_preview.SetPlaceholder(kindName + L"을(를) 선택하세요.\n더블클릭하면 해당 영상 목록이 표시됩니다.");
		return;
	}

	const CatRow& r = m_catRows[row];
	const CString count = m_listCat.GetItemText(row, 1);
	if (r.kind != CAT_VALUE)
	{
		m_staticName.SetWindowText(m_listCat.GetItemText(row, 0));
		m_staticImage.SetWindowText(L"영상 " + count + L"편");
		m_preview.SetPlaceholder(L"더블클릭하면 해당 영상 목록이 표시됩니다.");
		return;
	}

	m_staticName.SetWindowText(kindName + L": " + r.value);
	CString info = L"영상 " + count + L"편";
	const int idx = m_lib.FindNamed(NamedKind(), r.value);
	CString image;
	if (idx >= 0)
	{
		const NamedInfo& n = m_lib.NamedList(NamedKind())[idx];
		image = n.image;
		if (NamedKind() == LIST_STUDIO && !n.memo.IsEmpty())   // 태그는 메모 없음
		{
			CString memo = n.memo;
			memo.Replace(L"\r\n", L" ");
			memo.Replace(L'\n', L' ');
			info += L" · " + memo;
		}
	}
	m_staticImage.SetWindowText(info);

	m_preview.Clear();
	if (m_mode == MODE_TAG)
		m_preview.SetPlaceholder(L"# " + r.value + L"\n\n영상 " + count + L"편\n더블클릭하면 해당 영상 목록이 표시됩니다.");
	else if (!image.IsEmpty() && ::PathFileExistsW(image))
		m_preview.SetImageFile(image);
	else
		m_preview.SetPlaceholder(L"이미지 없음\n\n[목록 관리]에서 이미지를 지정할 수 있습니다.");
}

void CRuliManagerDlg::BackToList()
{
	CommitDetails();
	m_drill.Empty();
	m_catKind = CAT_ALL;
	m_catValue.Empty();

	ShowDetails(-1);
	UpdateLeftPane();
	m_editSearch.SetWindowText(m_savedSearch);   // EN_CHANGE → 목록 갱신
	RebuildCategories();                          // 배우: 배우 격자 / 스튜디오·태그: 목록
	ApplyFilter();
	if (m_mode == MODE_ACTOR)
	{
		m_actorGrid.SetFocus();
		ShowActorInfo(SelectedActorIndex());
	}
	else
	{
		if (CatGridActive()) m_catGrid.SetFocus(); else m_listCat.SetFocus();
		ShowNamedInfo(m_listCat.GetNextItem(-1, LVNI_SELECTED));
	}
}

void CRuliManagerDlg::OnBnClickedActorBack()
{
	BackToList();
}

void CRuliManagerDlg::OnActorShowVideos()
{
	if (IsActorGridMode())
	{
		if (m_actorSel >= 0)
			DrillIntoActor(m_actorSel);
	}
	else if (IsCategoryListMode())
	{
		DrillIntoCategory(m_listCat.GetNextItem(-1, LVNI_SELECTED));
	}
}

void CRuliManagerDlg::OnActorEdit()
{
	if (m_mode == MODE_ACTOR)
	{
		const int idx = SelectedActorIndex();
		OpenActorManager(idx >= 0 ? m_lib.actors[idx].name : CString());
	}
	else if (m_mode == MODE_STUDIO)
	{
		OnManageStudios();
	}
	else if (m_mode == MODE_TAG)
	{
		OnManageTags();
	}
}

// --- 배우 격자 데이터 제공 ---

bool CRuliManagerDlg::ActorGridOwner::GridDrawCard(CDC* dc, int row, const CRect& card, bool selected, bool focused)
{
	dlg->DrawActorCard(dc, row, card, selected, focused);
	return true;
}

bool CRuliManagerDlg::ActorGridOwner::GridClick(int row, const CRect& card, CPoint pt)
{
	if (row < 0 || row >= static_cast<int>(dlg->m_actorRows.size()))
		return false;
	if (GridHitPart(row, card, pt) != 1)
		return false;
	dlg->ToggleActorFavorite(dlg->m_actorRows[row]);
	return true;
}

void CRuliManagerDlg::ToggleActorFavorite(int actorIdx)
{
	if (actorIdx < 0 || actorIdx >= static_cast<int>(m_lib.actors.size()))
		return;
	ActorInfo& a = m_lib.actors[actorIdx];
	a.favorite = !a.favorite;
	m_lib.Save();
	m_actorSelName = a.name;
	RebuildActorGrid();          // 즐겨찾기가 앞으로 오도록 다시 정렬 (선택은 이름으로 유지)
	ShowActorInfo(actorIdx);     // 오른쪽 패널 하트
}

void CRuliManagerDlg::UpdateActorPanel(const ActorInfo* a, int count)
{
	if (!m_actorPanel.GetSafeHwnd())
		return;
	const int before = m_actorPanel.CalcHeight(0);   // 0 = 패널의 지금 폭 기준
	m_actorPanel.SetActor(a, count);
	// 줄 수가 바뀌어 필요한 높이가 달라지면 미리보기/패널 배치 다시
	if (m_layoutReady && m_actorPanel.CalcHeight(0) != before)
	{
		CRect client;
		GetClientRect(&client);
		LayoutControls(client.Width(), client.Height());
		RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
	}
}

int CRuliManagerDlg::ActorGridOwner::GridHitPart(int /*row*/, const CRect& card, CPoint pt)
{
	CRect heart = dlg->ActorHeartRect(card);
	heart.InflateRect(3, 3);   // 조금 넉넉하게
	return heart.PtInRect(pt) ? 1 : 0;
}

CRect CRuliManagerDlg::ActorHeartRect(const CRect& card) const
{
	const int size = (std::max)(16, card.Width() * 15 / 100);
	const int pad = (std::max)(4, m_cardPad * 2 / 3);
	return CRect(card.right - pad - size, card.top + pad, card.right - pad, card.top + pad + size);
}

// ---------------------------------------------------------------------------
// 배우 카드

namespace
{
	const COLORREF kCardBack   = RGB(0x30, 0x40, 0x4D);   // 카드 배경
	const COLORREF kCardImage  = RGB(0x2A, 0x35, 0x3D);   // 사진 없을 때
	const COLORREF kCardLine   = RGB(0x39, 0x4B, 0x59);   // 구분선
	const COLORREF kCardSub    = RGB(0xA7, 0xB6, 0xC2);   // 나이 / 별칭
	const COLORREF kCardIcon   = RGB(0xCE, 0xD9, 0xE0);   // ▶ / 필름 아이콘
	const COLORREF kFemale     = RGB(0xF5, 0x49, 0x8B);   // ♀
	const COLORREF kMale       = RGB(0x48, 0xAF, 0xF0);   // ♂
	const COLORREF kTransF     = RGB(0xF5, 0xA9, 0xB8);   // ⚧ 트랜스젠더 여성 (트랜스 깃발 분홍)
	const COLORREF kTransM     = RGB(0x5B, 0xCE, 0xFA);   // ⚧ 트랜스젠더 남성 (트랜스 깃발 하늘색)
	const COLORREF kIntersex   = RGB(0xFF, 0xD8, 0x00);   // ⚥ 인터섹스 (인터섹스 깃발 노랑)
	const COLORREF kNonBinary  = RGB(0xB5, 0x7E, 0xDC);   // ⚲ 논바이너리 (논바이너리 깃발 보라)

	// 성별 기호 + 색 (미지정이면 false). 기호는 TextFB 로 그려서 글꼴에 없으면 Segoe UI Symbol 등으로 대체
	bool GenderSymbol(const CString& g, CString& sym, COLORREF& col)
	{
		if (g == L"여성")                { sym = L"\x2640"; col = kFemale;    return true; }   // ♀
		if (g == L"남성")                { sym = L"\x2642"; col = kMale;      return true; }   // ♂
		if (g == L"트랜스젠더 여성")     { sym = L"\x26A7"; col = kTransF;    return true; }   // ⚧
		if (g == L"트랜스젠더 남성")     { sym = L"\x26A7"; col = kTransM;    return true; }   // ⚧
		if (g == L"인터섹스")            { sym = L"\x26A5"; col = kIntersex;  return true; }   // ⚥
		if (g == L"논바이너리")          { sym = L"\x26B2"; col = kNonBinary; return true; }   // ⚲
		return false;
	}

	// 폭 안에 들어가는 만큼 자르기 (공백이 있으면 공백에서 줄바꿈)
	int FitChars(CDC* dc, const CString& s, int start, int width, bool preferSpace)
	{
		int end = start;
		int lastSpace = -1;
		while (end < s.GetLength())
		{
			if (TextFB::Width(dc, s.Mid(start, end - start + 1)) > width)
				break;
			if (s[end] == L' ')
				lastSpace = end;
			++end;
		}
		if (end < s.GetLength() && preferSpace && lastSpace > start)
			end = lastSpace + 1;
		return (std::max)(end, start + 1);
	}

	void DrawPlayIcon(CDC* dc, int cx, int cy, int r, COLORREF col, COLORREF back)
	{
		(void)back;   // 벡터 아이콘: 재생 삼각형은 뚫려 있음
		VectorIcon::PlayCircle(dc, CRect(cx - r, cy - r, cx + r + 1, cy + r + 1), col);
	}

	// 사진 왼쪽 위 모서리에 대각선 리본 "별점: N" (빨간 띠, 흰 굵은 글씨)
	//  card: 카드 전체 (둥근 모서리 잘라내기용), img: 사진 영역
	void DrawRatingRibbon(CDC* dc, const CRect& card, const CRect& img, int radius, int rating, CFont* font)
	{
		if (rating <= 0 || !font)
			return;
		CString text;
		text.Format(L"별점: %d", rating);

		LOGFONTW lf = {};
		font->GetLogFont(&lf);
		lf.lfWeight = FW_BOLD;
		lf.lfQuality = CLEARTYPE_QUALITY;

		CRgn clip;
		clip.CreateRoundRectRgn(card.left, card.top, card.right + 1, card.bottom + 1, radius, radius);
		dc->SelectClipRgn(&clip);
		dc->IntersectClipRect(img);
		{
			Gdiplus::Graphics g(dc->GetSafeHdc());
			g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
			g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
			Gdiplus::Font gf(dc->GetSafeHdc(), &lf);
			Gdiplus::StringFormat fmt;
			fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
			fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
			fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
			Gdiplus::RectF bound;
			g.MeasureString(text, -1, &gf, Gdiplus::PointF(0, 0), &fmt, &bound);
			const float bandH = bound.Height + 2.0f;          // 띠 두께
			// 글자가 모서리 삼각형 안에 들어가도록 띠 중심을 모서리에서 떨어뜨림
			const float c = (std::max)(bound.Width / 2.6f + bandH / 2.0f, bandH * 1.2f);
			g.TranslateTransform(img.left + c, img.top + c);
			g.RotateTransform(-45.0f);
			const float len = c * 4.0f + bound.Width;        // 넉넉히 (사진 밖은 잘림)
			// 별점별 띠 색: 1 #394B59 / 2 #63411E / 3 #A66321 / 4 #A82A2A / 5 #FF2F39
			static const COLORREF kRibbon[5] = {
				RGB(0x39, 0x4B, 0x59), RGB(0x63, 0x41, 0x1E), RGB(0xA6, 0x63, 0x21), RGB(0xA8, 0x2A, 0x2A), RGB(0xFF, 0x2F, 0x39)
			};
			const COLORREF rc = kRibbon[(std::min)(rating, 5) - 1];
			Gdiplus::SolidBrush band(Gdiplus::Color(235, GetRValue(rc), GetGValue(rc), GetBValue(rc)));
			g.FillRectangle(&band, -len / 2, -bandH / 2, len, bandH);
			Gdiplus::SolidBrush white(Gdiplus::Color(255, 255, 255, 255));
			g.DrawString(text, -1, &gf, Gdiplus::RectF(-len / 2, -bandH / 2, len, bandH), &fmt, &white);
		}
		dc->SelectClipRgn(nullptr);
	}

	// 하트 (24x24 설계 좌표를 area 에 맞춤), alpha 로 반투명
	void DrawHeart(CDC* dc, const CRect& area, COLORREF col, BYTE alpha, bool shadow)
	{
		Gdiplus::Graphics g(dc->GetSafeHdc());
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		const float scale = (std::min)(area.Width(), area.Height()) / 24.0f;
		auto build = [](Gdiplus::GraphicsPath& p)
		{
			p.StartFigure();
			p.AddLine(12.0f, 21.35f, 10.55f, 20.03f);
			p.AddBezier(10.55f, 20.03f, 5.4f, 15.36f, 2.0f, 12.28f, 2.0f, 8.5f);
			p.AddBezier(2.0f, 8.5f, 2.0f, 5.42f, 4.42f, 3.0f, 7.5f, 3.0f);
			p.AddBezier(7.5f, 3.0f, 9.24f, 3.0f, 10.91f, 3.81f, 12.0f, 5.09f);
			p.AddBezier(12.0f, 5.09f, 13.09f, 3.81f, 14.76f, 3.0f, 16.5f, 3.0f);
			p.AddBezier(16.5f, 3.0f, 19.58f, 3.0f, 22.0f, 5.42f, 22.0f, 8.5f);
			p.AddBezier(22.0f, 8.5f, 22.0f, 12.28f, 18.6f, 15.36f, 13.45f, 20.04f);
			p.CloseFigure();
		};
		if (shadow)
		{
			// 밝은 사진 위에서도 보이도록 살짝 그림자
			Gdiplus::GraphicsPath sp;
			build(sp);
			Gdiplus::Matrix m;
			m.Translate(area.left + 1.0f, area.top + 1.5f);
			m.Scale(scale, scale);
			sp.Transform(&m);
			Gdiplus::SolidBrush sb(Gdiplus::Color(70, 0, 0, 0));
			g.FillPath(&sb, &sp);
		}
		Gdiplus::GraphicsPath path;
		build(path);
		Gdiplus::Matrix m;
		m.Translate(static_cast<Gdiplus::REAL>(area.left), static_cast<Gdiplus::REAL>(area.top));
		m.Scale(scale, scale);
		path.Transform(&m);
		Gdiplus::SolidBrush br(Gdiplus::Color(alpha, GetRValue(col), GetGValue(col), GetBValue(col)));
		g.FillPath(&br, &path);
	}

}

namespace
{
	// 꼬리표 아이콘 (왼쪽이 뾰족, 구멍 하나)
	void DrawTagIcon(CDC* dc, int cx, int cy, int r, COLORREF col, COLORREF back)
	{
		(void)back;
		VectorIcon::Tag(dc, CRect(cx - r - 1, cy - r - 1, cx + r + 2, cy + r + 2), col);
	}

	// 사람 아이콘 (머리 + 어깨)
	void DrawPersonIcon(CDC* dc, int cx, int cy, int r, COLORREF col)
	{
		VectorIcon::Person(dc, CRect(cx - r - 1, cy - r - 1, cx + r + 2, cy + r + 2), col);
	}

	// 사진이 없는 배우 카드의 기본 이미지
	//  - 성별 미지정: 흰 사람 (Font Awesome user) — 원본 SVG 처럼 사진 높이의 약 47%, 가운데
	//  - 여성 / 남성: 흰 실루엣 중 하나를 배우 이름으로 골라 표시 (배우마다 다르지만 늘 같은 이미지)
	void DrawDefaultActorImage(CDC* dc, const CRect& img, const ActorInfo& a)
	{
		const CString& gender = a.gender;
		const bool female = CVideoLibrary::IsFemaleLike(gender), male = CVideoLibrary::IsMaleLike(gender);   // 트랜스젠더 여성 / 남성도 같은 실루엣
		const int count = female ? VectorIcon::FemaleCount() : (male ? VectorIcon::MaleCount() : 0);
		if (count > 0)
		{
			// 이름 해시(FNV-1a)로 고름 → 무작위처럼 섞이지만 다시 그려도 바뀌지 않음
			UINT h = 2166136261u;
			for (int i = 0; i < a.name.GetLength(); ++i)
				h = (h ^ static_cast<UINT>(a.name[i])) * 16777619u;
			const int index = static_cast<int>(h % static_cast<UINT>(count));
			if (female)
				VectorIcon::Female(dc, img, RGB(255, 255, 255), index, 0.94);
			else
				VectorIcon::Male(dc, img, RGB(255, 255, 255), index, 0.94);
			return;
		}
		if (!female && !male)   // 미지정 · 인터섹스 · 논바이너리: 흰 사람
		{
			const int side = (std::max)(1, (std::min)(img.Width(), img.Height()));
			const double fill = (std::min)(1.0, img.Height() * (512.0 / 1080.0) / (side * 22.0 / 24.0));
			VectorIcon::User(dc, img, RGB(255, 255, 255), fill);
			return;
		}
		CRect pr = img;
		pr.top += img.Height() / 6;   // 사람 모양이 아래쪽까지 차도록
		VectorIcon::Person(dc, pr, RGB(0x5C, 0x70, 0x80), 0.8);
	}
}

void CRuliManagerDlg::SetupActorCards()
{
	// 글꼴: 이름은 조금 크게 굵게, 성별 기호는 기호 글꼴
	LOGFONT lf = {};
	if (CFont* f = GetFont())
		f->GetLogFont(&lf);
	LOGFONT nameLf = lf;
	nameLf.lfHeight = lf.lfHeight * 115 / 100;
	nameLf.lfWeight = FW_BOLD;
	m_cardNameFont.DeleteObject();
	m_cardNameFont.CreateFontIndirect(&nameLf);
	LOGFONT symLf = lf;
	symLf.lfHeight = lf.lfHeight * 150 / 100;
	symLf.lfWeight = FW_BOLD;
	symLf.lfCharSet = DEFAULT_CHARSET;
	wcscpy_s(symLf.lfFaceName, L"Segoe UI Symbol");
	m_cardSymbolFont.DeleteObject();
	m_cardSymbolFont.CreateFontIndirect(&symLf);
	LOGFONT actorSymLf = symLf;   // 배우 탭 카드 성별 기호: 2pt 크게
	actorSymLf.lfHeight = symLf.lfHeight + lf.lfHeight * 2 / 9;
	if (actorSymLf.lfHeight == symLf.lfHeight)
		actorSymLf.lfHeight += (symLf.lfHeight < 0) ? -2 : 2;
	m_actorSymbolFont.DeleteObject();
	m_actorSymbolFont.CreateFontIndirect(&actorSymLf);
	LOGFONT videoLf = nameLf;   // 영상 카드 제목: 카드 이름 글꼴보다 2pt 크게
	videoLf.lfHeight = nameLf.lfHeight + lf.lfHeight * 2 / 9;
	if (videoLf.lfHeight == nameLf.lfHeight)
		videoLf.lfHeight += (nameLf.lfHeight < 0) ? -2 : 2;
	m_videoTitleFont.DeleteObject();
	m_videoTitleFont.CreateFontIndirect(&videoLf);
	LOGFONT actorLf = nameLf;   // 배우 카드 이름(첫 줄): 카드 제목 글꼴보다 3pt 크게 (1pt + 2pt)
	actorLf.lfHeight = nameLf.lfHeight + lf.lfHeight * 3 / 9;
	if (actorLf.lfHeight == nameLf.lfHeight)
		actorLf.lfHeight += (nameLf.lfHeight < 0) ? -3 : 3;
	m_actorNameFont.DeleteObject();
	m_actorNameFont.CreateFontIndirect(&actorLf);
	LOGFONT subLf = lf;   // 기본(9pt)보다 1pt 크게 (영상 카드 발매일 · 메모 기준)
	subLf.lfHeight = lf.lfHeight * 10 / 9;
	if (subLf.lfHeight == lf.lfHeight)
		subLf.lfHeight += (lf.lfHeight < 0) ? -1 : 1;
	LOGFONT cardSubLf = lf;   // 배우 카드 괄호 안 이름(둘째·셋째 줄): 기본(9pt)보다 2pt 크게
	cardSubLf.lfHeight = lf.lfHeight * 11 / 9;
	if (cardSubLf.lfHeight == subLf.lfHeight)
		cardSubLf.lfHeight += (lf.lfHeight < 0) ? -1 : 1;
	m_cardSubFont.DeleteObject();
	m_cardSubFont.CreateFontIndirect(&cardSubLf);
	LOGFONT vsubLf = subLf;   // 영상 카드 발매일 · 메모: 1pt 큰 크기 + 굵게
	vsubLf.lfWeight = FW_BOLD;
	m_vcardSubFont.DeleteObject();
	m_vcardSubFont.CreateFontIndirect(&vsubLf);

	CClientDC dc(this);
	TEXTMETRIC tm = {};
	CFont* old = dc.SelectObject(&m_cardNameFont);
	dc.GetTextMetrics(&tm);
	m_cardLineB = tm.tmHeight + 1;
	dc.SelectObject(GetFont());
	dc.GetTextMetrics(&tm);
	m_cardLine = tm.tmHeight + 1;
	dc.SelectObject(&m_cardSubFont);
	dc.GetTextMetrics(&tm);
	m_cardSubLine = tm.tmHeight + 1;
	dc.SelectObject(&m_vcardSubFont);
	dc.GetTextMetrics(&tm);
	m_vcardSubLine = tm.tmHeight + 1;
	dc.SelectObject(&m_actorNameFont);
	dc.GetTextMetrics(&tm);
	m_actorLineB = tm.tmHeight + 1;
	dc.SelectObject(&m_videoTitleFont);
	dc.GetTextMetrics(&tm);
	m_videoLineB = tm.tmHeight + 1;
	m_vcardMemoGap = ::MulDiv(2, dc.GetDeviceCaps(LOGPIXELSY), 72);   // 2pt → 픽셀
	dc.SelectObject(old);

	m_cardPad = DX(5);
	const int oldCardW = m_cardW, oldVcardW = m_vcardW;   // 크기가 바뀔 때만 이미지 캐시를 비움
	const double zoom = ZoomFactor();   // 탭별 확대 단계
	m_cardW = static_cast<int>(DX(108) * zoom);
	m_cardImgH = m_cardW * 3 / 2;   // 세로 사진 2:3
	m_actorNameGap = (std::max)(0, m_cardPad - m_vcardMemoGap);   // 사진 ~ 이름 간격: 기본 여백보다 2pt 좁게
	m_cardH = m_cardImgH
		+ m_actorNameGap + (std::max)(m_actorLineB * 2, m_actorLineB + m_cardSubLine * 2)   // 성별 + 이름 (굵은 1줄 + 괄호 안 이름 2줄 자리)
		+ m_cardLine + m_cardPad / 2           // 나이
		+ 1                                    // 구분선
		+ m_cardPad + m_cardLine + m_cardPad;  // ▶ 출연 수 · 스튜디오 수
	m_actorGrid.SetFixedTile(m_cardW, m_cardH);

	// 영상 상세 정보의 출연 배우 카드 (확대 단계와 상관없이 고정 크기, 사진 3:4)
	m_stripCardW = DX(78);   // 1.5배 (예전 DX(52))
	m_stripCardH = m_stripCardW * 4 / 3 + m_cardPad / 2 + m_cardLineB + m_cardLine * 2 + m_cardPad / 2   // 글자 3줄 (굵은 이름 + 2줄)
		+ 1 + m_cardPad / 2 + m_cardLine + m_cardPad / 2;                          // 구분선 + 제작 당시 나이
	if (m_actorStrip.GetSafeHwnd())
		m_actorStrip.SetCardSize(m_stripCardW, m_stripCardH, DX(4));
	if (m_cardW != oldCardW)
		m_actorPortraits.clear();

	// 영상 카드: 가로 이미지(16:9) + 제목 + 발매일 + 메모 3줄 + 구분선 + 태그/배우 수
	m_vcardW = static_cast<int>(DX(146) * zoom);
	m_vcardImgH = m_vcardW * 9 / 16;
	m_vcardH = m_vcardImgH
		+ m_cardPad + m_videoLineB + DX(2)     // 제목 (2pt 큰 글꼴)
		+ m_vcardSubLine + m_vcardMemoGap       // 발매일 / ● 임시 (1pt 큰 굵은 글꼴) + 메모와의 간격 2pt
		+ m_vcardSubLine * 3 + m_cardPad        // 메모 3줄 (1pt 큰 굵은 글꼴)
		+ 1                                     // 구분선
		+ m_cardPad + m_cardLine + m_cardPad;   // 태그 수 · 배우 수
	m_grid.SetFixedTile(m_vcardW, m_vcardH);
	if (m_vcardW != oldVcardW)
		m_videoCovers.clear();

	// 스튜디오 카드: 로고 영역(16:9, 안쪽 여백) + 이름 + 구분선 + 영상/배우 수
	m_scardW = (m_mode == MODE_TAG) ? m_vcardW * 3 / 4 : m_vcardW;   // 태그 카드는 25% 작게
	m_scardH = m_cardPad * 2 + (m_scardW - m_cardPad * 2) * 9 / 16   // 로고 영역
		+ m_cardPad + m_cardLineB + m_cardPad / 2                      // 이름
		+ 1                                                             // 구분선
		+ m_cardPad + m_cardLine + m_cardPad;                           // ▶ 영상 수 · 배우 수
	if (m_mode == MODE_STUDIO || m_mode == MODE_TAG)
		m_catGrid.SetFixedTile(m_scardW, m_scardH);
}

bool CRuliManagerDlg::CatGridOwner::GridClick(int row, const CRect& card, CPoint pt)
{
	if (GridHitPart(row, card, pt) != 1)
		return false;
	dlg->ToggleTagFavorite(dlg->m_catRows[row].value);
	return true;
}

int CRuliManagerDlg::CatGridOwner::GridHitPart(int row, const CRect& card, CPoint pt)
{
	if (dlg->m_mode != MODE_TAG || row < 0 || row >= static_cast<int>(dlg->m_catRows.size()))
		return 0;
	CRect heart = dlg->ActorHeartRect(card);   // 배우 카드와 같은 자리/크기
	heart.InflateRect(3, 3);
	return heart.PtInRect(pt) ? 1 : 0;
}

bool CRuliManagerDlg::IsTagFavorite(const CString& tag) const
{
	const int n = m_lib.FindNamed(LIST_TAG, tag);
	return n >= 0 && m_lib.tagInfos[n].favorite;
}

void CRuliManagerDlg::OnActorDelete()
{
	// 배우 탭(배우 격자): 선택한 배우를 배우 목록과 영상에서 삭제 (영상 파일·이미지 원본은 그대로)
	if (!IsActorGridMode())
		return;
	const int idx = SelectedActorIndex();
	if (idx < 0 || idx >= static_cast<int>(m_lib.actors.size()))
		return;
	const CString name = m_lib.actors[idx].name;
	const int count = m_lib.CountVideosWithActor(name);
	CString msg;
	if (count > 0)
		msg.Format(L"배우 '%s'을(를) 삭제할까요?\n\n이 배우가 연결된 동영상 %d개에서도 빠집니다.", static_cast<LPCWSTR>(name), count);
	else
		msg.Format(L"배우 '%s'을(를) 삭제할까요?", static_cast<LPCWSTR>(name));
	if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;

	// 삭제 후에는 옆 배우를 선택 (목록 순서 기준)
	const int row = m_actorSel;
	m_lib.RemoveActorFromVideos(name);
	m_lib.actors.erase(m_lib.actors.begin() + idx);
	m_lib.Save();

	m_actorSelName.Empty();
	RebuildActorGrid();
	if (!m_actorRows.empty())
	{
		const int next = (std::min)((std::max)(0, row), static_cast<int>(m_actorRows.size()) - 1);
		SelectActorRow(next);
	}
	else
		ShowActorInfo(-1);
	m_catsDirty = true;   // 다른 탭의 개수도 다시 계산
	UpdateStatus();
}

void CRuliManagerDlg::OnCatDelete()
{
	// 태그 탭: 선택한 태그를 목록과 영상에서 삭제 (영상 파일은 그대로)
	if (m_mode != MODE_TAG || m_catSelKind != CAT_VALUE || m_catSelValue.IsEmpty())
		return;
	const CString tag = m_catSelValue;
	const std::map<CString, int> counts = m_lib.CountNamed(LIST_TAG);
	CString key = tag;
	key.MakeLower();
	auto it = counts.find(key);
	const int count = (it != counts.end()) ? it->second : 0;

	CString msg;
	if (count > 0)
		msg.Format(L"태그 '%s'을(를) 삭제할까요?\n\n연결된 동영상 %d개에서도 빠집니다.", static_cast<LPCWSTR>(tag), count);
	else
		msg.Format(L"태그 '%s'을(를) 삭제할까요?", static_cast<LPCWSTR>(tag));
	if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;

	m_lib.RemoveNamedFromVideos(LIST_TAG, tag);
	const int n = m_lib.FindNamed(LIST_TAG, tag);
	if (n >= 0)
		m_lib.tagInfos.erase(m_lib.tagInfos.begin() + n);
	m_lib.Save();

	m_catSelKind = -1;
	m_catSelValue.Empty();
	RebuildCategories();
	UpdateStatus();
}

void CRuliManagerDlg::ToggleTagFavorite(const CString& tag)
{
	if (tag.IsEmpty())
		return;
	int n = m_lib.FindNamed(LIST_TAG, tag);
	if (n < 0)
	{
		// 영상에만 쓰이고 태그 목록에 없던 태그 → 목록에 추가해서 즐겨찾기 저장
		NamedInfo info;
		info.name = tag;
		m_lib.tagInfos.push_back(info);
		n = static_cast<int>(m_lib.tagInfos.size()) - 1;
	}
	m_lib.tagInfos[n].favorite = !m_lib.tagInfos[n].favorite;
	m_lib.Save();
	m_catSelKind = CAT_VALUE;
	m_catSelValue = tag;
	RebuildCategories();   // 즐겨찾기가 앞으로 오도록 다시 정렬 (선택은 이름으로 유지)
}

bool CRuliManagerDlg::CatGridOwner::GridDrawCard(CDC* dc, int row, const CRect& card, bool selected, bool focused)
{
	if (dlg->m_mode != MODE_STUDIO && dlg->m_mode != MODE_TAG)
		return false;
	dlg->DrawStudioCard(dc, row, card, selected, focused);   // 스튜디오 / 태그 카드
	return true;
}

namespace
{
	// 큰 꼬리표 아이콘 (왼쪽이 뾰족, 둥근 모서리, 구멍 하나) - 태그 카드
	void DrawBigTagIcon(CDC* dc, const CRect& area, COLORREF col, COLORREF hole)
	{
		(void)hole;
		VectorIcon::Tag(dc, area, col, 0.85);   // 태그 카드 기본 이미지
	}

	// 비디오 카메라 아이콘 (둥근 몸체 + 오른쪽 렌즈)
	void DrawCameraIcon(CDC* dc, const CRect& area, COLORREF col)
	{
		VectorIcon::Camera(dc, area, col, 0.87);   // 스튜디오 카드 기본 이미지 (원본 SVG 처럼 폭 = 높이의 약 80%)
	}
}

void CRuliManagerDlg::DrawStudioCard(CDC* dc, int row, const CRect& rc, bool selected, bool focused)
{
	if (row < 0 || row >= static_cast<int>(m_catRows.size()))
		return;
	const CString name = m_catRows[row].value;
	const int radius = DX(4);

	// 카드 배경
	{
		CBrush br(kCardBack);
		CPen pen(PS_SOLID, 1, kCardBack);
		CBrush* ob = dc->SelectObject(&br);
		CPen* op = dc->SelectObject(&pen);
		dc->RoundRect(rc, CPoint(radius, radius));
		dc->SelectObject(ob);
		dc->SelectObject(op);
	}

	// 로고 영역: 이미지가 있으면 비율 유지해서 가운데, 없으면 카메라 아이콘
	const CRect area(rc.left + m_cardPad * 2, rc.top + m_cardPad * 2, rc.right - m_cardPad * 2,
		rc.top + m_cardPad * 2 + (m_scardW - m_cardPad * 2) * 9 / 16 - m_cardPad * 2);
	const bool isTag = (m_mode == MODE_TAG);
	const int n = isTag ? -1 : m_lib.FindNamed(LIST_STUDIO, name);
	Gdiplus::Bitmap* logo = (n >= 0) ? GetStudioLogo(m_lib.studios[n].image) : nullptr;
	if (isTag)
	{
		DrawBigTagIcon(dc, area, RGB(255, 255, 255), kCardBack);   // 태그: 흰 꼬리표
		// 즐겨찾기: 오른쪽 위 하트 (지정 = 빨간 하트, 하트 자리에 마우스 오버 = 반투명 회색 하트, 누르면 전환)
		const CRect heart = ActorHeartRect(rc);
		if (IsTagFavorite(name))
			DrawHeart(dc, heart, RGB(0xF2, 0x5C, 0x54), 255, true);
		else if (m_catGrid.HotRow() == row && m_catGrid.HotPart() == 1)
			DrawHeart(dc, heart, RGB(0xC8, 0xCC, 0xD0), 170, true);
	}
	else if (logo && logo->GetWidth() > 0 && logo->GetHeight() > 0)
	{
		const double lw = logo->GetWidth(), lh = logo->GetHeight();
		const double scale = (std::min)(area.Width() / lw, area.Height() / lh);
		const int w = (std::max)(1, static_cast<int>(lw * scale));
		const int h = (std::max)(1, static_cast<int>(lh * scale));
		Gdiplus::Graphics g(dc->GetSafeHdc());
		g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
		g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
		g.DrawImage(logo, area.left + (area.Width() - w) / 2, area.top + (area.Height() - h) / 2, w, h);
	}
	else
	{
		DrawCameraIcon(dc, area, RGB(255, 255, 255));
	}

	CFont* normal = GetFont();
	CFont* oldFont = dc->SelectObject(&m_cardNameFont);
	dc->SetBkMode(TRANSPARENT);
	const int x = rc.left + m_cardPad * 2;
	const int right = rc.right - m_cardPad * 2;
	int y = rc.top + m_cardPad * 2 + (m_scardW - m_cardPad * 2) * 9 / 16 - m_cardPad + m_cardPad;

	// 이름 (굵게, 한 줄)
	dc->SetTextColor(kTextColor);
	CRect nr(x, y, right, y + m_cardLineB);
	TextFB::Draw(dc, name, nr, DT_LEFT, true);
	y += m_cardLineB + m_cardPad / 2;

	// 구분선
	dc->FillSolidRect(rc.left + m_cardPad, y, rc.Width() - m_cardPad * 2, 1, kCardLine);
	y += 1 + m_cardPad;

	// ▶ 영상 수 · 배우 수 (가운데)
	dc->SelectObject(normal);
	const int videos = _wtoi(m_listCat.GetItemText(row, 1));
	CString key = name;
	key.MakeLower();
	auto ita = m_studioActorCounts.find(key);
	const int actors = (ita != m_studioActorCounts.end()) ? ita->second : 0;
	CString vText, aText;
	vText.Format(L"%d", videos);
	aText.Format(L"%d", actors);
	const int iconR = (std::max)(4, m_cardLine * 2 / 5);
	const int gapIcon = DX(3), gapGroup = DX(10);
	int total = iconR * 2 + gapIcon + dc->GetTextExtent(vText).cx;
	if (!isTag)   // 태그 카드는 영상 수만
		total += gapGroup + iconR * 2 + gapIcon + dc->GetTextExtent(aText).cx;
	int sx = rc.left + (rc.Width() - total) / 2;
	const int cy = y + m_cardLine / 2;
	dc->SetTextColor(kTextColor);
	DrawPlayIcon(dc, sx + iconR, cy, iconR, kCardIcon, kCardBack);
	sx += iconR * 2 + gapIcon;
	dc->TextOut(sx, y, vText);
	if (!isTag)
	{
		sx += dc->GetTextExtent(vText).cx + gapGroup;
		DrawPersonIcon(dc, sx + iconR, cy, iconR, kCardIcon);
		sx += iconR * 2 + gapIcon;
		dc->TextOut(sx, y, aText);
	}

	// 선택 테두리
	if (selected)
	{
		CPen pen(PS_SOLID, 2, focused ? kButtonColor : RGB(0x5C, 0x70, 0x80));
		CPen* op = dc->SelectObject(&pen);
		CBrush* ob = static_cast<CBrush*>(dc->SelectStockObject(NULL_BRUSH));
		CRect sr = rc;
		sr.DeflateRect(1, 1);
		dc->RoundRect(sr, CPoint(radius, radius));
		dc->SelectObject(op);
		dc->SelectObject(ob);
	}
	dc->SelectObject(oldFont);
}

double CRuliManagerDlg::ZoomFactor() const
{
	static const double kZoom[4] = { 1.0, 1.3, 1.65, 2.1 };
	const int mode = (m_mode >= MODE_VIDEO && m_mode <= MODE_TAG) ? m_mode : MODE_VIDEO;
	return kZoom[(std::max)(0, (std::min)(3, m_zoom[mode]))];
}

void CRuliManagerDlg::OnZoomChanged(int pos)
{
	if (m_mode < MODE_VIDEO || m_mode > MODE_TAG)
		return;
	m_zoom[m_mode] = pos;
	CString keyName;
	keyName.Format(L"Zoom%d", m_mode);
	AfxGetApp()->WriteProfileInt(L"Settings", keyName, pos);

	CWaitCursor wait;
	SetupActorCards();   // 현재 탭 확대 단계로 카드 크기 다시 계산 (이미지도 새 크기로 다시 읽음)
	m_grid.OnSelectionChanged();
	m_actorGrid.OnSelectionChanged();
	m_catGrid.OnSelectionChanged();
}

bool CRuliManagerDlg::GridDrawCard(CDC* dc, int row, const CRect& card, bool selected, bool focused)
{
	DrawVideoCard(dc, row, card, selected, focused);
	return true;
}

CBitmap* CRuliManagerDlg::GetVideoCover(int itemIdx, int w, int h)
{
	if (itemIdx < 0 || itemIdx >= static_cast<int>(m_lib.items.size()) || w <= 0 || h <= 0)
		return nullptr;
	CString key;
	key.Format(L"%s|%dx%d", static_cast<LPCWSTR>(m_lib.items[itemIdx].path), w, h);
	key.MakeLower();
	auto it = m_videoCovers.find(key);
	if (it != m_videoCovers.end())
		return it->second.get();   // 이미지가 없으면 nullptr 이 저장되어 있음

	if (m_videoCovers.size() > 600)
		m_videoCovers.clear();     // 메모리 제한 (다시 그릴 때 필요한 것만 다시 읽음)

	std::unique_ptr<CBitmap> bmp;
	const CString file = FindImageFor(m_lib.items[itemIdx].path);
	CImage img;
	if (!file.IsEmpty() && LoadImageFile(img, file) && !img.IsNull())
	{
		CClientDC screen(this);
		bmp = std::make_unique<CBitmap>();
		bmp->CreateCompatibleBitmap(&screen, w, h);
		CDC mem;
		mem.CreateCompatibleDC(&screen);
		CBitmap* old = mem.SelectObject(bmp.get());
		mem.FillSolidRect(0, 0, w, h, kThumbBackColor);
		// 비율을 유지한 채 이미지 전체가 카드 이미지 칸에 들어가도록 맞춤 (자르지 않음, 남는 곳은 배경색 여백 - 가운데 정렬)
		const double iw = img.GetWidth(), ih = img.GetHeight();
		const double scale = (std::min)(w / iw, h / ih);
		const int dw = (std::max)(1, (std::min)(w, static_cast<int>(iw * scale + 0.5)));
		const int dh = (std::max)(1, (std::min)(h, static_cast<int>(ih * scale + 0.5)));
		const int dx = (w - dw) / 2;
		const int dy = (h - dh) / 2;
		mem.SetStretchBltMode(HALFTONE);
		::SetBrushOrgEx(mem.GetSafeHdc(), 0, 0, nullptr);
		img.StretchBlt(mem.GetSafeHdc(), dx, dy, dw, dh, 0, 0, static_cast<int>(iw), static_cast<int>(ih), SRCCOPY);
		mem.SelectObject(old);
	}
	CBitmap* result = bmp.get();
	m_videoCovers[key] = std::move(bmp);
	return result;
}

namespace
{
	// GDI+ 는 처음 쓸 때 한 번 시작 (프로그램이 끝날 때까지 유지)
	void EnsureGdiPlus()
	{
		static ULONG_PTR token = 0;
		if (!token)
		{
			Gdiplus::GdiplusStartupInput input;
			Gdiplus::GdiplusStartup(&token, &input, nullptr);
		}
	}
}

Gdiplus::Bitmap* CRuliManagerDlg::GetStudioLogo(const CString& path)
{
	if (path.IsEmpty())
		return nullptr;
	CString key = path;
	key.MakeLower();
	auto it = m_studioLogos.find(key);
	if (it != m_studioLogos.end())
		return it->second.get();

	EnsureGdiPlus();
	std::unique_ptr<Gdiplus::Bitmap> bmp;
	if (::PathFileExistsW(path))
	{
		// PNG 등은 투명도까지 그대로 읽음 (암호화된 이미지는 메모리에서 복호화한 스트림으로)
		if (IStream* s = OpenImageFileStream(path))
		{
			std::unique_ptr<Gdiplus::Bitmap> tmp(Gdiplus::Bitmap::FromStream(s));
			if (tmp && tmp->GetLastStatus() == Gdiplus::Ok && tmp->GetWidth() > 0 && tmp->GetHeight() > 0)
			{
				// 스트림과 분리된 복사본 (스트림을 바로 놓아도 되게)
				bmp = std::make_unique<Gdiplus::Bitmap>(static_cast<INT>(tmp->GetWidth()), static_cast<INT>(tmp->GetHeight()), PixelFormat32bppPARGB);
				Gdiplus::Graphics g(bmp.get());
				g.DrawImage(tmp.get(), 0, 0, static_cast<INT>(tmp->GetWidth()), static_cast<INT>(tmp->GetHeight()));
			}
			tmp.reset();
			s->Release();
		}
		if (bmp && bmp->GetLastStatus() != Gdiplus::Ok)
			bmp.reset();
		if (!bmp)
		{
			// GDI+ 로 못 읽는 형식(WebP 등)은 WIC 로 읽어서 변환 (투명도 없음)
			CImage img;
			if (LoadImageFile(img, path) && !img.IsNull())
			{
				bmp.reset(Gdiplus::Bitmap::FromHBITMAP(static_cast<HBITMAP>(img), nullptr));
				if (bmp && bmp->GetLastStatus() != Gdiplus::Ok)
					bmp.reset();
			}
		}
	}
	Gdiplus::Bitmap* result = bmp.get();
	m_studioLogos[key] = std::move(bmp);
	return result;
}

void CRuliManagerDlg::DrawStudioMark(CDC* dc, const CRect& img, const CString& studio)
{
	CString name = studio;
	name.Trim();
	if (name.IsEmpty())
		return;
	const int margin = DX(4);
	const int n = m_lib.FindNamed(LIST_STUDIO, name);
	Gdiplus::Bitmap* logo = (n >= 0) ? GetStudioLogo(m_lib.studios[n].image) : nullptr;
	if (logo && logo->GetWidth() > 0 && logo->GetHeight() > 0)
	{
		// 이미지 오른쪽 위: 폭 38% · 높이 24% 안에 비율 유지, 75% 불투명
		const double maxW = img.Width() * 0.38, maxH = img.Height() * 0.24;
		const double lw = logo->GetWidth(), lh = logo->GetHeight();
		const double scale = (std::min)(maxW / lw, maxH / lh);
		const int w = (std::max)(1, static_cast<int>(lw * scale));
		const int h = (std::max)(1, static_cast<int>(lh * scale));
		const int x = img.right - margin - w;
		const int y = img.top + margin;

		Gdiplus::Graphics g(dc->GetSafeHdc());
		g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
		g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
		Gdiplus::ColorMatrix cm = {
			1, 0, 0, 0, 0,
			0, 1, 0, 0, 0,
			0, 0, 1, 0, 0,
			0, 0, 0, 0.75f, 0,
			0, 0, 0, 0, 1 };
		Gdiplus::ImageAttributes attr;
		attr.SetColorMatrix(&cm);
		g.DrawImage(logo, Gdiplus::Rect(x, y, w, h), 0, 0, static_cast<INT>(lw), static_cast<INT>(lh), Gdiplus::UnitPixel, &attr);
		return;
	}

	// 스튜디오 이미지가 없으면 이름을 반투명 글자로
	EnsureGdiPlus();
	LOGFONT lf = {};
	m_cardNameFont.GetLogFont(&lf);
	Gdiplus::Graphics g(dc->GetSafeHdc());
	g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
	Gdiplus::Font font(dc->GetSafeHdc(), &lf);
	Gdiplus::RectF box(static_cast<Gdiplus::REAL>(img.left + margin), static_cast<Gdiplus::REAL>(img.top + margin),
		static_cast<Gdiplus::REAL>(img.Width() - margin * 2), static_cast<Gdiplus::REAL>(img.Height() / 3));
	Gdiplus::StringFormat fmt;
	fmt.SetAlignment(Gdiplus::StringAlignmentFar);           // 오른쪽 정렬
	fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
	fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
	Gdiplus::SolidBrush shadow(Gdiplus::Color(140, 0, 0, 0));
	Gdiplus::RectF sbox = box;
	sbox.Offset(1.0f, 1.0f);
	g.DrawString(name, -1, &font, sbox, &fmt, &shadow);
	Gdiplus::SolidBrush brush(Gdiplus::Color(190, 255, 255, 255));
	g.DrawString(name, -1, &font, box, &fmt, &brush);
}

bool CRuliManagerDlg::SplitPartName(const CString& path, CString& key, int& num)
{
	return CVideoLibrary::PartGroupKey(path, key, num);   // "ABC-123_2.mp4" → 묶음 키 + 순번 2
}

void CRuliManagerDlg::DrawVideoCard(CDC* dc, int row, const CRect& rc, bool selected, bool focused)
{
	if (row < 0 || row >= static_cast<int>(m_view.size()))
		return;
	const int idx = m_view[row];
	const VideoItem& v = m_lib.items[idx];
	const int radius = DX(4);

	// 카드 배경 (둥근 모서리)
	{
		CBrush br(kCardBack);
		CPen pen(PS_SOLID, 1, kCardBack);
		CBrush* ob = dc->SelectObject(&br);
		CPen* op = dc->SelectObject(&pen);
		dc->RoundRect(rc, CPoint(radius, radius));
		dc->SelectObject(ob);
		dc->SelectObject(op);
	}

	// 이미지 (위쪽 모서리 둥글게)
	const CRect img(rc.left, rc.top, rc.right, rc.top + m_vcardImgH);
	{
		CRgn clip;
		clip.CreateRoundRectRgn(rc.left, rc.top, rc.right + 1, rc.bottom + 1, radius, radius);
		dc->SelectClipRgn(&clip);
		dc->IntersectClipRect(img);
		if (CBitmap* bmp = GetVideoCover(idx, img.Width(), img.Height()))
		{
			CDC mem;
			mem.CreateCompatibleDC(dc);
			CBitmap* old = mem.SelectObject(bmp);
			dc->BitBlt(img.left, img.top, img.Width(), img.Height(), &mem, 0, 0, SRCCOPY);
			mem.SelectObject(old);
		}
		else
		{
			dc->FillSolidRect(img, kThumbBackColor);
			VectorIcon::Movie(dc, img, RGB(255, 255, 255), 0.78);   // 영상 기본 이미지: 흰 원 + 재생 (원본 SVG 처럼 이미지 높이의 약 71%)
		}
		dc->SelectClipRgn(nullptr);
	}

	// 스튜디오가 지정되어 있으면 이미지 오른쪽 위에 로고(없으면 이름)를 반투명으로
	DrawStudioMark(dc, img, v.studio);

	// 별점: 이미지 왼쪽 위 대각선 리본 (배우 카드와 같은 모양/색)
	DrawRatingRibbon(dc, rc, img, radius, v.rating, GetFont());

	CFont* normal = GetFont();
	CFont* oldFont = dc->SelectObject(&m_videoTitleFont);   // 제목: 2pt 큰 글꼴
	dc->SetBkMode(TRANSPARENT);
	const int x = rc.left + m_cardPad;
	const int right = rc.right - m_cardPad;
	int y = img.bottom + m_cardPad;

	// 제목 (없으면 파일 이름) - 굵게, 한 줄
	// 카드 첫 줄: 제목 대신 품번 (품번이 없으면 제목, 제목도 없으면 파일 이름)
	const CString title = !v.code.IsEmpty() ? v.code : (v.title.IsEmpty() ? v.FileName() : v.title);
	// 품번 오른쪽에 파일 순번 "(2/3)" - 파일 이름의 마지막 '_' 오른쪽 숫자 기준, 같은 묶음 파일이 2개 이상일 때만
	CString partLabel;
	{
		CString key;
		int num = 0;
		if (SplitPartName(v.path, key, num))
		{
			const auto it = m_partTotal.find(key);
			const int total = (it != m_partTotal.end()) ? it->second : 1;
			if (total > 1)
				partLabel.Format(L"(%d/%d)", num, total);
		}
	}
	dc->SetTextColor(kTextColor);
	CRect tr(x, y, right, y + m_videoLineB);
	if (partLabel.IsEmpty())
		TextFB::Draw(dc, title, tr, DT_LEFT, true);   // 글꼴 대체 (일본어 한자 등)
	else
	{
		const int labelW = TextFB::Width(dc, partLabel);
		const int gapW = DX(3);
		CRect cr = tr;
		cr.right = (std::max)(cr.left, tr.right - labelW - gapW);   // 순번이 잘리지 않게 품번 쪽을 줄임
		const int codeW = (std::min)(TextFB::Width(dc, title), cr.Width());
		TextFB::Draw(dc, title, cr, DT_LEFT, true);
		CRect lr(tr.left + codeW + gapW, tr.top, tr.right, tr.bottom);
		dc->SetTextColor(kCardSub);
		TextFB::Draw(dc, partLabel, lr, DT_LEFT, false);
	}
	y += m_videoLineB + DX(2);

	// 발매일, 임시 항목은 예전처럼 "● 임시 (정보 저장 전)" (강조색)
	dc->SelectObject(&m_vcardSubFont);   // 발매일 · 제목: 기본보다 1pt 큰 굵은 글꼴
	CRect dr(x, y, right, y + m_vcardSubLine);
	if (v.pending)
	{
		dc->SetTextColor(RGB(110, 180, 255));
		dc->DrawText(L"● 임시 (정보 저장 전)", dr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	}
	else
	{
		dc->SetTextColor(kCardSub);
		dc->DrawText(v.release, dr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	}
	y += m_vcardSubLine + m_vcardMemoGap;   // 발매일과 제목 사이 2pt 더

	// 제목 3줄 (메모 대신, 제목이 없으면 빈칸) - 일본어 한자 등은 글꼴 대체로 그리고, 줄바꿈은 직접 계산
	{
		CString rest = v.title;
		rest.Replace(L"\r\n", L" ");
		rest.Replace(L'\n', L' ');
		rest.Trim();
		dc->SetTextColor(RGB(0xCE, 0xD9, 0xE0));
		const int lineW = right - x;
		for (int line = 0; line < 3 && !rest.IsEmpty() && lineW > 0; ++line)
		{
			CRect lr(x, y + m_vcardSubLine * line, right, y + m_vcardSubLine * (line + 1));
			if (line == 2 || TextFB::Width(dc, rest) <= lineW)
			{
				TextFB::Draw(dc, rest, lr, DT_LEFT, true);   // 마지막 줄: 넘치면 "…"
				break;
			}
			// 이 줄에 들어가는 글자 수 (최소 1자), 가능하면 공백에서 끊음
			int fit = 1;
			while (fit < rest.GetLength() && TextFB::Width(dc, rest.Left(fit + 1)) <= lineW)
				++fit;
			const int sp = rest.Left(fit + 1).ReverseFind(L' ');
			if (sp > 0 && sp <= fit)
				fit = sp;
			TextFB::Draw(dc, rest.Left(fit), lr, DT_LEFT, false);
			rest = rest.Mid(fit);
			rest.TrimLeft();
		}
	}
	y += m_vcardSubLine * 3 + m_cardPad;
	dc->SelectObject(normal);   // 아래 개수 줄은 기본 글꼴

	// 구분선
	dc->FillSolidRect(rc.left, y, rc.Width(), 1, kCardLine);
	y += 1 + m_cardPad;

	// 태그 수 · 배우 수 · 물방울 카운트 (가운데, 0 이면 생략)
	const int tags = static_cast<int>(CVideoLibrary::SplitList(v.tags).size());
	const int actors = static_cast<int>(CVideoLibrary::SplitList(v.actors).size());
	const int drops = v.oCount;
	CString tText, aText, oText;
	tText.Format(L"%d", tags);
	aText.Format(L"%d", actors);
	oText.Format(L"%d", drops);
	const int iconR = (std::max)(4, m_cardLine * 2 / 5);
	const int gapIcon = DX(3), gapGroup = DX(10);
	int total = 0;
	if (tags > 0)
		total += iconR * 2 + gapIcon + dc->GetTextExtent(tText).cx;
	if (actors > 0)
		total += (total > 0 ? gapGroup : 0) + iconR * 2 + gapIcon + dc->GetTextExtent(aText).cx;
	if (drops > 0)
		total += (total > 0 ? gapGroup : 0) + iconR * 2 + gapIcon + dc->GetTextExtent(oText).cx;
	int sx = rc.left + (rc.Width() - total) / 2;
	const int cy = y + m_cardLine / 2;
	dc->SetTextColor(kTextColor);
	if (tags > 0)
	{
		DrawTagIcon(dc, sx + iconR, cy, iconR, kCardIcon, kCardBack);
		sx += iconR * 2 + gapIcon;
		dc->TextOut(sx, y, tText);
		sx += dc->GetTextExtent(tText).cx + gapGroup;
	}
	if (actors > 0)
	{
		DrawPersonIcon(dc, sx + iconR, cy, iconR, kCardIcon);
		sx += iconR * 2 + gapIcon;
		dc->TextOut(sx, y, aText);
		sx += dc->GetTextExtent(aText).cx + gapGroup;
	}
	if (drops > 0)
	{
		VectorIcon::Drops(dc, CRect(sx - 1, cy - iconR - 1, sx + iconR * 2 + 2, cy + iconR + 2), kCardIcon);   // 물방울 카운트
		sx += iconR * 2 + gapIcon;
		dc->TextOut(sx, y, oText);
	}

	// 선택 테두리
	if (selected)
	{
		CPen pen(PS_SOLID, 2, focused ? kButtonColor : RGB(0x5C, 0x70, 0x80));
		CPen* op = dc->SelectObject(&pen);
		CBrush* ob = static_cast<CBrush*>(dc->SelectStockObject(NULL_BRUSH));
		CRect sr = rc;
		sr.DeflateRect(1, 1);
		dc->RoundRect(sr, CPoint(radius, radius));
		dc->SelectObject(op);
		dc->SelectObject(ob);
	}
	dc->SelectObject(oldFont);
}

CBitmap* CRuliManagerDlg::GetActorPortrait(int actorIdx, int w, int h)
{
	if (actorIdx < 0 || actorIdx >= static_cast<int>(m_lib.actors.size()) || w <= 0 || h <= 0)
		return nullptr;
	const ActorInfo& a = m_lib.actors[actorIdx];
	if (a.photo.IsEmpty())
		return nullptr;
	CString key;
	key.Format(L"%s|%dx%d", static_cast<LPCWSTR>(a.photo), w, h);
	key.MakeLower();
	auto it = m_actorPortraits.find(key);
	if (it != m_actorPortraits.end())
		return it->second.get();   // 없으면 nullptr 이 저장되어 있음 (다시 읽지 않음)

	std::unique_ptr<CBitmap> bmp;
	CImage img;
	if (::PathFileExistsW(a.photo) && LoadImageFile(img, a.photo) && !img.IsNull())
	{
		CClientDC screen(this);
		bmp = std::make_unique<CBitmap>();
		bmp->CreateCompatibleBitmap(&screen, w, h);
		CDC mem;
		mem.CreateCompatibleDC(&screen);
		CBitmap* old = mem.SelectObject(bmp.get());
		mem.FillSolidRect(0, 0, w, h, kCardImage);
		// 카드를 꽉 채우도록 확대 (가로는 가운데, 세로는 얼굴이 보이게 위쪽 기준으로 자름)
		const double iw = img.GetWidth(), ih = img.GetHeight();
		const double scale = (std::max)(w / iw, h / ih);
		const int sw = (std::max)(1, static_cast<int>(w / scale));
		const int sh = (std::max)(1, static_cast<int>(h / scale));
		const int sx = (std::max)(0, static_cast<int>((iw - sw) / 2));
		const int sy = (std::max)(0, static_cast<int>((ih - sh) * 0.15));
		mem.SetStretchBltMode(HALFTONE);
		::SetBrushOrgEx(mem.GetSafeHdc(), 0, 0, nullptr);
		img.StretchBlt(mem.GetSafeHdc(), 0, 0, w, h, sx, sy, sw, sh, SRCCOPY);
		mem.SelectObject(old);
	}
	CBitmap* result = bmp.get();
	m_actorPortraits[key] = std::move(bmp);
	return result;
}

void CRuliManagerDlg::DrawActorCard(CDC* dc, int row, const CRect& rc, bool selected, bool focused)
{
	if (row < 0 || row >= static_cast<int>(m_actorRows.size()))
		return;
	const int idx = m_actorRows[row];
	const ActorInfo& a = m_lib.actors[idx];
	const int radius = DX(4);

	// 카드 배경 (둥근 모서리), 선택되면 파란 테두리
	{
		CBrush br(kCardBack);
		CPen pen(PS_SOLID, 1, kCardBack);
		CBrush* ob = dc->SelectObject(&br);
		CPen* op = dc->SelectObject(&pen);
		dc->RoundRect(rc, CPoint(radius, radius));
		dc->SelectObject(ob);
		dc->SelectObject(op);
	}

	// 사진 (위쪽 모서리만 둥글게 보이도록 카드 모양으로 잘라 그림)
	const CRect img(rc.left, rc.top, rc.right, rc.top + m_cardImgH);
	{
		CRgn clip;
		clip.CreateRoundRectRgn(rc.left, rc.top, rc.right + 1, rc.bottom + 1, radius, radius);
		dc->SelectClipRgn(&clip);
		dc->IntersectClipRect(img);
		if (CBitmap* bmp = GetActorPortrait(idx, img.Width(), img.Height()))
		{
			CDC mem;
			mem.CreateCompatibleDC(dc);
			CBitmap* old = mem.SelectObject(bmp);
			dc->BitBlt(img.left, img.top, img.Width(), img.Height(), &mem, 0, 0, SRCCOPY);
			mem.SelectObject(old);
		}
		else
		{
			dc->FillSolidRect(img, kCardImage);
			DrawDefaultActorImage(dc, img, a);   // 배우 기본 이미지 (벡터)
		}
		dc->SelectClipRgn(nullptr);
	}

	// 별점: 사진 왼쪽 위 대각선 리본
	DrawRatingRibbon(dc, rc, img, radius, a.rating, GetFont());

	// 즐겨찾기: 오른쪽 위 하트 (지정 = 빨간 하트, 하트 자리에 마우스 오버 = 반투명 회색 하트, 누르면 전환)
	{
		const bool hot = (m_actorGrid.HotRow() == row && m_actorGrid.HotPart() == 1);   // 하트 자리에 마우스가 있을 때만
		const CRect heart = ActorHeartRect(rc);
		if (a.favorite)
			DrawHeart(dc, heart, RGB(0xF2, 0x5C, 0x54), 255, true);
		else if (hot)
			DrawHeart(dc, heart, RGB(0xC8, 0xCC, 0xD0), 170, true);
	}

	// 국기: 사진 오른쪽 아래
	const int flag = CCountryCombo::FindCountry(a.nationality);
	if (flag >= 0)
		CCountryCombo::DrawFlag(dc, flag, img.right - m_cardPad - CCountryCombo::kFlagW, img.bottom - m_cardPad - CCountryCombo::kFlagH);

	CFont* normal = GetFont();
	CFont* oldFont = dc->SelectObject(normal);
	dc->SetBkMode(TRANSPARENT);

	// 성별 기호 + 이름(굵게, 흰색), 최대 2줄 (별칭은 표시 안 함)
	int x = rc.left + m_cardPad;
	int y = img.bottom + m_actorNameGap;   // 사진 바로 아래 (기본 여백보다 2pt 좁게)
	const int right = rc.right - m_cardPad;
	int textX = x;
	CString gsym;
	COLORREF gcol = kTextColor;
	if (GenderSymbol(a.gender, gsym, gcol))
	{
		dc->SelectObject(&m_actorSymbolFont);   // 배우 탭 카드: 2pt 큰 성별 기호
		dc->SetTextColor(gcol);
		const int symW = TextFB::Width(dc, gsym);   // ♀ ♂ ⚧ ⚥ ⚲ (글꼴에 없으면 대체 글꼴)
		CRect sr(x, y, x + symW, y + m_actorLineB * 2);   // 이름 첫 두 줄 높이에 맞춰 가운데
		TextFB::Draw(dc, gsym, sr, DT_LEFT, false);
		textX = x + symW + DX(3) + m_vcardMemoGap;   // 성별 기호 오른쪽 여백 (+2pt)
	}

	// 이름에 "(...)" 가 있으면: 첫 줄 = 괄호 앞 이름(굵게), 둘째 줄 = 괄호 안 이름(회색)
	CString mainName = a.name, subName;
	{
		int po = a.name.Find(L'(');
		if (po < 0) po = a.name.Find(L'\xFF08');   // 전각 （
		if (po > 0)
		{
			int pc = a.name.ReverseFind(L')');
			const int pcW = a.name.ReverseFind(L'\xFF09');   // 전각 ）
			if (pcW > pc) pc = pcW;
			if (pc < po) pc = a.name.GetLength();     // 닫는 괄호가 없으면 끝까지
			CString inner = a.name.Mid(po + 1, pc - po - 1);
			inner.Trim();
			CString left = a.name.Left(po);
			left.Trim();
			if (!left.IsEmpty() && !inner.IsEmpty())
			{
				mainName = left;
				subName = inner;
			}
		}
	}
	if (!subName.IsEmpty())
	{
		dc->SelectObject(&m_actorNameFont);
		dc->SetTextColor(kTextColor);
		TextFB::Draw(dc, mainName, CRect(textX, y, right, y + m_actorLineB), DT_LEFT, true);
		// 괄호 안에 쉼표가 있으면 나눠서 줄마다 (최대 2줄, 남는 것은 둘째 줄에 이어서)
		std::vector<CString> parts;
		{
			CString t = subName;
			t.Replace(L'\xFF0C', L',');   // 전각 ，
			t.Replace(L'\x3001', L',');   // 、
			int start = 0;
			for (;;)
			{
				const int comma = t.Find(L',', start);
				CString part = (comma < 0) ? t.Mid(start) : t.Mid(start, comma - start);
				part.Trim();
				if (!part.IsEmpty())
					parts.push_back(part);
				if (comma < 0)
					break;
				start = comma + 1;
			}
		}
		if (parts.size() > 2)
		{
			for (size_t k = 2; k < parts.size(); ++k)
				parts[1] += L", " + parts[k];
			parts.resize(2);
		}
		dc->SelectObject(&m_cardSubFont);   // 기본보다 한 단계 큰 글꼴
		dc->SetTextColor(kCardSub);
		int sy = y + m_actorLineB;
		for (const CString& part : parts)
		{
			TextFB::Draw(dc, part, CRect(textX, sy, right, sy + m_cardSubLine), DT_LEFT, true);
			sy += m_cardSubLine;
		}
	}
	else
	{
		dc->SelectObject(&m_actorNameFont);
		const int width = (std::max)(10, right - textX);
		const CString& name = a.name;
		int lineY = y;
		int pos = 0;
		int lines = 0;
		while (pos < name.GetLength() && lines < 2)
		{
			int end = FitChars(dc, name, pos, width, true);
			CString part = name.Mid(pos, end - pos);
			if (lines == 1 && end < name.GetLength())
			{
				// 둘째 줄에서 넘치면 … 으로 줄임
				CRect tr(textX, lineY, right, lineY + m_actorLineB);
				CString rest = name.Mid(pos);
				dc->SetTextColor(kTextColor);
				TextFB::Draw(dc, rest, tr, DT_LEFT, true);
				pos = name.GetLength();
				++lines;
				break;
			}
			part.TrimRight();
			dc->SetTextColor(kTextColor);
			TextFB::Draw(dc, part, CRect(textX, lineY, right, lineY + m_actorLineB), DT_LEFT, false);   // 글꼴 대체
			pos = end;
			while (pos < name.GetLength() && name[pos] == L' ')
				++pos;
			++lines;
			if (pos < name.GetLength())
				lineY += m_actorLineB;
		}

	}
	y += (std::max)(m_actorLineB * 2, m_actorLineB + m_cardSubLine * 2);   // 이름 영역 (m_cardH 계산과 같게)

	// 나이
	dc->SelectObject(normal);
	const int age = CVideoLibrary::CalcAge(a.birth);
	if (age >= 0)
	{
		CString ageText;
		ageText.Format(L"%d 살", age);
		dc->SetTextColor(kCardSub);
		dc->TextOut(x, y, ageText);
	}
	y += m_cardLine + m_cardPad / 2;

	// 구분선
	dc->FillSolidRect(rc.left, y, rc.Width(), 1, kCardLine);
	y += 1 + m_cardPad;

	// ▶ 출연 수 (가운데)
	CString key = a.name;
	key.MakeLower();
	auto itc = m_actorCounts.find(key);
	const int videos = (itc != m_actorCounts.end()) ? itc->second : 0;

	CString vText;
	vText.Format(L"%d", videos);
	const int iconR = (std::max)(4, m_cardLine * 2 / 5);
	const int gapIcon = DX(3);
	const int total = iconR * 2 + gapIcon + dc->GetTextExtent(vText).cx;
	int sx = rc.left + (rc.Width() - total) / 2;
	const int cy = y + m_cardLine / 2;
	DrawPlayIcon(dc, sx + iconR, cy, iconR, kCardIcon, kCardBack);
	sx += iconR * 2 + gapIcon;
	dc->SetTextColor(kTextColor);
	dc->TextOut(sx, y, vText);

	// 선택 테두리
	if (selected)
	{
		CPen pen(PS_SOLID, 2, focused ? kButtonColor : RGB(0x5C, 0x70, 0x80));
		CPen* op = dc->SelectObject(&pen);
		CBrush* ob = static_cast<CBrush*>(dc->SelectStockObject(NULL_BRUSH));
		CRect sr = rc;
		sr.DeflateRect(1, 1);
		dc->RoundRect(sr, CPoint(radius, radius));
		dc->SelectObject(op);
		dc->SelectObject(ob);
	}
	dc->SelectObject(oldFont);
}

int CRuliManagerDlg::ActorGridOwner::GridGetCount()
{
	return static_cast<int>(dlg->m_actorRows.size());
}

int CRuliManagerDlg::ActorGridOwner::GridGetSel()
{
	return dlg->m_actorSel;
}

void CRuliManagerDlg::ActorGridOwner::GridSetSel(int row)
{
	dlg->SelectActorRow(row);
}

void CRuliManagerDlg::ActorGridOwner::GridGetItem(int row, int& image, CString& name, CString& line2, CString& line3)
{
	image = 0;
	if (row < 0 || row >= static_cast<int>(dlg->m_actorRows.size()))
		return;
	const int idx = dlg->m_actorRows[row];
	const ActorInfo& a = dlg->m_lib.actors[idx];
	image = dlg->GetActorThumbIndex(idx);
	name = a.name;

	CString key = a.name;
	key.MakeLower();
	auto it = dlg->m_actorCounts.find(key);
	// 둘째 줄: 나이 · 출연 수
	line2.Format(L"출연 %d편", it != dlg->m_actorCounts.end() ? it->second : 0);
	const int age = CVideoLibrary::CalcAge(a.birth);
	if (age >= 0)
	{
		CString t;
		t.Format(L"%d세 · ", age);
		line2 = t + line2;
	}

	// 셋째 줄(2줄): 국적 · 키 · 데뷔 → 별칭 → 메모
	std::vector<CString> parts;
	if (!a.gender.IsEmpty())      parts.push_back(a.gender);
	if (!a.nationality.IsEmpty()) parts.push_back(a.nationality);
	if (!a.height.IsEmpty())      parts.push_back(a.height + L"cm");
	if (!a.debut.IsEmpty())       parts.push_back(L"데뷔 " + a.debut.Left(4));
	if (!a.retire.IsEmpty())      parts.push_back(L"은퇴 " + a.retire.Left(4));
	if (!a.aliases.IsEmpty())     parts.push_back(L"별칭: " + a.aliases);
	if (!a.memo.IsEmpty())        parts.push_back(a.memo);
	line3.Empty();
	for (const CString& p : parts)
	{
		if (!line3.IsEmpty()) line3 += L" · ";
		line3 += p;
	}
}

void CRuliManagerDlg::ActorGridOwner::GridActivate(int row)
{
	dlg->DrillIntoActor(row);
}

void CRuliManagerDlg::ActorGridOwner::GridKey(UINT vk)
{
	switch (vk)
	{
	case VK_F2:    dlg->OnActorEdit(); break;
	case VK_DELETE: dlg->OnActorDelete(); break;
	case VK_SPACE: dlg->OnActorShowVideos(); break;
	case VK_F5:    dlg->OnBnClickedRefresh(); break;
	case 'F':
		if (::GetKeyState(VK_CONTROL) & 0x8000)
		{
			dlg->m_editSearch.SetFocus();
			dlg->m_editSearch.SetSel(0, -1);
		}
		break;
	}
}

// ---------------------------------------------------------------------------
// 미리보기 이미지

void CRuliManagerDlg::OnBnClickedChangeImage()
{
	if (m_curItem < 0 || m_curItem >= static_cast<int>(m_lib.items.size()))
		return;
	CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
		L"이미지 파일 (*.jpg;*.jpeg;*.png;*.webp;*.bmp;*.gif;*.tif;*.tiff)|*.jpg;*.jpeg;*.png;*.webp;*.bmp;*.gif;*.tif;*.tiff|모든 파일 (*.*)|*.*||",
		this);
	if (dlg.DoModal() != IDOK)
		return;
	const CString path = dlg.GetPathName();
	if (!SetPendingVideoImage(path, L"새 이미지: " + CString(::PathFindFileNameW(path)) + L"  (저장하면 적용)"))
		AfxMessageBox(L"이미지 파일을 열 수 없습니다.", MB_ICONWARNING);
}

bool CRuliManagerDlg::SetPendingVideoImage(const CString& path, const CString& label)
{
	if (m_curItem < 0 || m_curItem >= static_cast<int>(m_lib.items.size()))
		return false;
	CImage test;
	if (!LoadImageFile(test, path))
		return false;
	// 저장 전까지는 미리보기만 바꿈 → [저장] 하면 영상 파일 옆에 복사
	m_pendingImage = path;
	m_preview.Clear();
	m_preview.SetImageFile(path);
	m_staticImage.SetWindowText(label);
	OnDetailsChanged();
	return true;
}

void CRuliManagerDlg::OnDropFiles(HDROP hDropInfo)
{
	// 영상 상세(영상 탭 / 배우 출연작)의 이미지 영역에 놓은 첫 번째 파일 → 새 이미지 (저장하면 적용)
	CPoint pt;
	::DragQueryPoint(hDropInfo, &pt);   // 대화상자 클라이언트 좌표
	CString file;
	const UINT count = ::DragQueryFileW(hDropInfo, 0xFFFFFFFF, nullptr, 0);
	if (count > 0)
	{
		const UINT len = ::DragQueryFileW(hDropInfo, 0, nullptr, 0);
		::DragQueryFileW(hDropInfo, 0, file.GetBuffer(len + 1), len + 1);
		file.ReleaseBuffer();
	}
	::DragFinish(hDropInfo);

	if (file.IsEmpty() || !ShowVideoDetail() || !m_preview.GetSafeHwnd() || !m_preview.IsWindowVisible())
		return;
	CRect pr;
	m_preview.GetWindowRect(&pr);
	ScreenToClient(&pr);
	if (!pr.PtInRect(pt))
		return;   // 이미지 영역 밖에 놓음
	if (m_curItem < 0 || m_curItem >= static_cast<int>(m_lib.items.size()))
	{
		AfxMessageBox(L"이미지를 지정할 영상을 먼저 선택하세요.", MB_ICONINFORMATION);
		return;
	}
	if (::GetFileAttributesW(file) & FILE_ATTRIBUTE_DIRECTORY)
		return;
	SetForegroundWindow();
	if (!SetPendingVideoImage(file, L"끌어다 놓은 새 이미지: " + CString(::PathFindFileNameW(file)) + L"  (저장하면 적용)"))
		AfxMessageBox(L"이미지 파일을 열 수 없습니다.", MB_ICONWARNING);
}

void CRuliManagerDlg::OnBnClickedSearchImage()
{
	if (m_curItem < 0 || m_curItem >= static_cast<int>(m_lib.items.size()))
		return;
	// 검색어: 제목 칸 (비었으면 파일 이름), [2024.01.01] 같은 대괄호 부분은 뺌
	// 검색어: 품번이 있으면 품번, 없으면 제목 칸 (비었으면 파일 이름)
	CString query;
	m_editCode.GetWindowText(query);
	query.Trim();
	if (query.IsEmpty())
	{
		m_editTitle.GetWindowText(query);
		query.Trim();
	}
	if (query.IsEmpty())
	{
		query = m_lib.items[m_curItem].FileName();
		::PathRemoveExtensionW(query.GetBuffer());
		query.ReleaseBuffer();
	}
	for (;;)
	{
		const int a = query.Find(L'[');
		const int z = (a >= 0) ? query.Find(L']', a) : -1;
		if (a < 0 || z < 0)
			break;
		query.Delete(a, z - a + 1);
	}
	query.Replace(L'_', L' ');
	query.Trim();

	CImageSearchDlg dlg(query, this);
	if (dlg.DoModal() != IDOK || dlg.m_resultPath.IsEmpty())
		return;
	// 저장 전까지는 미리보기만 바꿈 → [저장] 하면 영상 파일 옆에 "영상이름.확장자" 로 복사
	m_pendingImage = dlg.m_resultPath;
	m_preview.Clear();
	m_preview.SetImageFile(m_pendingImage);
	m_staticImage.SetWindowText(L"검색한 새 이미지  (저장하면 적용)");
	OnDetailsChanged();
}

void CRuliManagerDlg::RecyclePartImages(const std::vector<CString>& videoPaths, const CString& keepBase)
{
	// 분할 파일마다 같은 이름의 이미지(ABC-123_2.jpg / ABC-123_2.mp4.jpg)를 휴지통으로 (대표 이름 이미지는 유지)
	static const wchar_t* const kImageExts[] = { L".jpg", L".jpeg", L".png", L".webp", L".bmp", L".gif", L".tif", L".tiff" };
	CString keepStem = keepBase;
	const int kext = static_cast<int>(::PathFindExtensionW(keepBase) - static_cast<LPCWSTR>(keepBase));
	if (kext > 0)
		keepStem = keepBase.Left(kext);
	std::vector<wchar_t> from;
	for (const CString& vp : videoPaths)
	{
		CString stem = vp;
		const int extPos = static_cast<int>(::PathFindExtensionW(vp) - static_cast<LPCWSTR>(vp));
		if (extPos > 0)
			stem = vp.Left(extPos);
		if (stem.CompareNoCase(keepStem) == 0)
			continue;   // 대표 이름 그 자체 (ABC-123.mp4) → 방금 저장한 이미지
		for (const wchar_t* e : kImageExts)
		{
			const CString c1 = stem + e, c2 = vp + e;
			for (const CString& c : { c1, c2 })
			{
				if (::PathFileExistsW(c))
				{
					from.insert(from.end(), static_cast<LPCWSTR>(c), static_cast<LPCWSTR>(c) + c.GetLength());
					from.push_back(L'\0');
				}
			}
		}
	}
	if (from.empty())
		return;
	from.push_back(L'\0');   // 이중 NULL 종료
	SHFILEOPSTRUCTW op = {};
	op.hwnd = m_hWnd;
	op.wFunc = FO_DELETE;
	op.pFrom = from.data();
	op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
	::SHFileOperationW(&op);
}

bool CRuliManagerDlg::ApplyVideoImage(const CString& videoPath, const CString& src)
{
	if (videoPath.IsEmpty() || !::PathFileExistsW(src))
		return false;

	// 대상: 영상 파일과 같은 이름 + 원본 이미지 확장자   예: D:\영상\movie.mp4 + a.png → D:\영상\movie.png
	CString stem = videoPath;
	const int extPos = static_cast<int>(::PathFindExtensionW(videoPath) - static_cast<LPCWSTR>(videoPath));
	if (extPos > 0 && extPos < videoPath.GetLength())
		stem = videoPath.Left(extPos);
	CString ext = ::PathFindExtensionW(src);
	ext.MakeLower();
	if (ext.IsEmpty() || ext.GetLength() > 5)
		ext = L".jpg";
	const CString dst = stem + ext;
	if (dst.CompareNoCase(src) == 0)
		return true;   // 이미 그 파일

	// 원본 이미지를 먼저 임시로 복사 (원본이 지울 대상 안에 있어도 안전하게)
	const CString tmp = stem + L".vmtmp" + ext;
	if (!::CopyFileW(src, tmp, FALSE))
		return false;

	// 기존 이미지(다른 확장자 포함, movie.jpg / movie.mp4.jpg 등)는 휴지통으로 → 새 이미지가 표시되도록
	static const wchar_t* const kImageExts[] = { L".jpg", L".jpeg", L".png", L".webp", L".bmp", L".gif", L".tif", L".tiff" };
	std::vector<wchar_t> from;
	for (const wchar_t* e : kImageExts)
	{
		const CString c1 = stem + e, c2 = videoPath + e;
		for (const CString& c : { c1, c2 })
		{
			if (::PathFileExistsW(c))
			{
				from.insert(from.end(), static_cast<LPCWSTR>(c), static_cast<LPCWSTR>(c) + c.GetLength());
				from.push_back(L'\0');
			}
		}
	}
	if (!from.empty())
	{
		from.push_back(L'\0');   // 이중 NULL 종료
		SHFILEOPSTRUCTW op = {};
		op.hwnd = m_hWnd;
		op.wFunc = FO_DELETE;
		op.pFrom = from.data();
		op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
		::SHFileOperationW(&op);
	}

	m_preview.Clear();   // 미리보기가 파일을 쥐고 있지 않도록 (메모리로 읽으므로 보통은 필요 없음)
	const bool ok = ::MoveFileExW(tmp, dst, MOVEFILE_REPLACE_EXISTING) != FALSE;
	if (!ok)
		::DeleteFileW(tmp);
	else
		::SetFileAttributesW(dst, FILE_ATTRIBUTE_NORMAL);
	return ok;
}

CString CRuliManagerDlg::FindImageFor(const CString& videoPath)
{
	static const wchar_t* const kImageExts[] = {
		L".jpg", L".jpeg", L".png", L".webp", L".bmp", L".gif", L".tif", L".tiff"
	};

	// 1) 확장자를 뺀 같은 이름   예: movie.mp4 → movie.jpg
	CString stem = videoPath;
	const int ext = static_cast<int>(::PathFindExtensionW(videoPath) - static_cast<LPCWSTR>(videoPath));
	if (ext > 0 && ext < videoPath.GetLength())
		stem = videoPath.Left(ext);
	for (const wchar_t* e : kImageExts)
	{
		const CString candidate = stem + e;
		if (::PathFileExistsW(candidate))
			return candidate;
	}

	// 2) 확장자까지 포함한 이름 예: movie.mp4 → movie.mp4.jpg
	for (const wchar_t* e : kImageExts)
	{
		const CString candidate = videoPath + e;
		if (::PathFileExistsW(candidate))
			return candidate;
	}

	// 3) '_' 왼쪽만 같은 이름   예: ABC-123.mp4 ↔ ABC-123_cover.jpg, ABC-123_1080p.mp4 ↔ ABC-123.jpg
	//    (같은 폴더, 대소문자 무시, 여러 개면 이름 순으로 첫 번째)
	auto keyOf = [](const CString& stemName) -> CString
	{
		const int us = stemName.Find(L'_');
		CString k = (us >= 0) ? stemName.Left(us) : stemName;
		k.Trim();
		return k;
	};
	const CString dir = videoPath.Left(static_cast<int>(::PathFindFileNameW(videoPath) - static_cast<LPCWSTR>(videoPath)));
	const CString key = keyOf(stem.Mid(dir.GetLength()));
	if (key.IsEmpty())
		return CString();

	CString best;
	WIN32_FIND_DATAW fd = {};
	HANDLE h = ::FindFirstFileW(dir + key + L"*", &fd);   // 이름이 key 로 시작하는 파일만
	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;
			const CString name = fd.cFileName;
			const LPCWSTR fext = ::PathFindExtensionW(name);
			bool isImage = false;
			for (const wchar_t* e : kImageExts)
			{
				if (_wcsicmp(fext, e) == 0) { isImage = true; break; }
			}
			if (!isImage)
				continue;
			const CString imgStem = name.Left(static_cast<int>(fext - static_cast<LPCWSTR>(name)));
			if (keyOf(imgStem).CompareNoCase(key) != 0)
				continue;   // ABC-1234.jpg 처럼 앞부분만 같은 다른 작품은 제외
			if (best.IsEmpty() || ::StrCmpLogicalW(name, best) < 0)
				best = name;
		} while (::FindNextFileW(h, &fd));
		::FindClose(h);
	}
	return best.IsEmpty() ? CString() : dir + best;
}

void CRuliManagerDlg::UpdatePreview(int idx)
{
	if (idx < 0 || idx >= static_cast<int>(m_lib.items.size()))
	{
		m_preview.Clear();
		m_preview.SetPlaceholder(L"동영상을 선택하세요.");
		m_staticImage.SetWindowText(L"");
		return;
	}

	const VideoItem& v = m_lib.items[idx];
	const CString image = FindImageFor(v.path);
	if (image.IsEmpty())
	{
		m_preview.Clear();
		CString stem = v.FileName();
		::PathRemoveExtensionW(stem.GetBuffer());
		stem.ReleaseBuffer();
		m_preview.SetPlaceholder(L"이미지 없음\n\n같은 폴더에 '" + stem +
			L".jpg' (또는 .png / .webp / .bmp / .gif) 파일을 두면 여기에 표시됩니다.");
		m_staticImage.SetWindowText(L"이미지: 없음");
		return;
	}

	m_preview.SetImageFile(image);
	m_staticImage.SetWindowText(L"이미지: " + CString(::PathFindFileNameW(image)));
}

// ---------------------------------------------------------------------------
// 폴더

void CRuliManagerDlg::OnBnClickedAddFolder()
{
	CFolderPickerDialog dlg(nullptr, 0, this);
	dlg.m_ofn.lpstrTitle = L"동영상 폴더 선택";
	if (dlg.DoModal() != IDOK)
		return;

	const CString folder = dlg.GetFolderPath();
	if (folder.IsEmpty())
		return;

	CommitDetails();
	const CString keep = (m_curItem >= 0) ? m_lib.items[m_curItem].path : CString();

	int added = 0;
	{
		CWaitCursor wait;
		BeginLibraryChange();
		added = m_lib.AddFolder(folder);
		m_lib.SyncActorsFromVideos();   // 폴더 구조로 찾은 배우/스튜디오를 목록에 추가
		m_lib.SyncNamedFromVideos();
		m_lib.Save();
		ApplyFilter();
		SelectPath(keep);
	}

	CString msg;
	msg.Format(L"'%s'\n\n새 동영상 %d개를 임시 목록에 추가했습니다.\n영상 정보를 저장하면 DB에 등록됩니다.", static_cast<LPCWSTR>(folder), added);
	AfxMessageBox(msg, MB_ICONINFORMATION);
}

void CRuliManagerDlg::OnBnClickedRemoveFolder()
{
	if (m_lib.folders.empty())
	{
		AfxMessageBox(L"등록된 폴더가 없습니다.", MB_ICONINFORMATION);
		return;
	}

	CMenu menu;
	menu.CreatePopupMenu();
	for (size_t i = 0; i < m_lib.folders.size(); ++i)
		menu.AppendMenu(MF_STRING, static_cast<UINT_PTR>(i + 1), m_lib.folders[i]);

	CRect rc;
	GetDlgItem(IDC_BTN_REMOVEFOLDER)->GetWindowRect(&rc);
	const UINT cmd = menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
		rc.left, rc.bottom, this);
	if (cmd == 0)
		return;

	const size_t index = cmd - 1;
	CString msg;
	msg.Format(L"'%s' 폴더를 목록에서 제거할까요?\n\n"
		L"※ 실제 파일은 삭제되지 않지만, 해당 동영상의 별점·태그·메모 정보는 사라집니다.",
		static_cast<LPCWSTR>(m_lib.folders[index]));
	if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;

	CWaitCursor wait;
	BeginLibraryChange();
	m_lib.RemoveFolder(index);
	m_lib.Save();
	ApplyFilter();
}

void CRuliManagerDlg::OnBnClickedRefresh()
{
	CommitDetails();
	const CString keep = (m_curItem >= 0) ? m_lib.items[m_curItem].path : CString();

	int added = 0, removed = 0, relinked = 0;
	{
		CWaitCursor wait;
		BeginLibraryChange();
		m_lib.Refresh(added, removed, &relinked);   // 경로가 바뀐 파일은 기존 정보에 다시 연결
		m_lib.SyncActorsFromVideos();   // 폴더 구조로 찾은 배우/스튜디오를 목록에 추가
		m_lib.SyncNamedFromVideos();
		ResetThumbnails();   // 이미지가 추가/변경됐을 수 있음
		m_lib.Save();
		ApplyFilter();
		SelectPath(keep);
	}

	CString s;
	s.Format(L"새로고침 완료: 경로가 바뀐 파일 %d개 다시 연결, 새 파일 %d개 임시 목록에 추가, 없어진 파일 %d개 제거 (임시 %d개는 정보를 저장하면 DB 등록)",
		relinked, added, removed, m_lib.PendingCount());
	m_staticStatus.SetWindowText(s);
}

// ---------------------------------------------------------------------------
// 파일 작업

void CRuliManagerDlg::OnBnClickedRename()
{
	const int row = GetSelectedRow();
	if (row < 0)
	{
		AfxMessageBox(L"이름을 바꿀 동영상을 선택하세요.", MB_ICONINFORMATION);
		return;
	}
	{
		// 격자 보기: 입력 창으로 이름 변경
		CInputDlg dlg(m_lib.items[m_view[row]].FileName(), this);
		if (dlg.DoModal() == IDOK)
			RenameRow(row, dlg.m_value);
		m_grid.SetFocus();
	}
}

void CRuliManagerDlg::OnLvnBeginLabelEdit(NMHDR* pNMHDR, LRESULT* pResult)
{
	UNREFERENCED_PARAMETER(pNMHDR);
	// 클릭만으로 편집 모드에 들어가지 않도록 [이름 변경]/F2 에서만 허용
	*pResult = m_allowLabelEdit ? FALSE : TRUE;
	m_allowLabelEdit = false;
}

void CRuliManagerDlg::OnLvnEndLabelEdit(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLVDISPINFO* p = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);
	*pResult = FALSE;   // 가상 목록이므로 텍스트는 데이터에서 다시 읽음

	if (!p->item.pszText)   // 취소(ESC)
		return;
	RenameRow(p->item.iItem, p->item.pszText);
}

void CRuliManagerDlg::RenameRow(int row, CString newName)
{
	if (row < 0 || row >= static_cast<int>(m_view.size()))
		return;

	const int idx = m_view[row];
	VideoItem& v = m_lib.items[idx];

	newName.Trim();
	if (newName.IsEmpty())
		return;
	if (newName.FindOneOf(L"\\/:*?\"<>|") >= 0)
	{
		AfxMessageBox(L"파일 이름에 다음 문자는 사용할 수 없습니다.\n\\ / : * ? \" < > |", MB_ICONWARNING);
		return;
	}

	const CString oldName = v.FileName();
	if (newName == oldName)
		return;

	// 확장자를 지운 경우 원래 확장자를 유지
	if (*::PathFindExtensionW(newName) == 0)
		newName += ::PathFindExtensionW(v.path);

	const CString dir = v.path.Left(v.path.GetLength() - oldName.GetLength());
	const CString newPath = dir + newName;

	if (newPath.CompareNoCase(v.path) != 0 && ::PathFileExistsW(newPath))
	{
		AfxMessageBox(L"같은 이름의 파일이 이미 있습니다.", MB_ICONWARNING);
		return;
	}

	const CString oldPath = v.path;
	const CString oldImage = FindImageFor(oldPath);
	if (idx == m_curItem)
		m_preview.Clear();

	if (!MoveFileWithRetry(oldPath, newPath))
	{
		AfxMessageBox(L"이름을 바꾸지 못했습니다.\n" + LastErrorText(::GetLastError()), MB_ICONERROR);
		if (idx == m_curItem) UpdatePreview(idx);
		return;
	}
	m_thumbIndex.erase(CVideoLibrary::MakeKey(oldPath));
	v.path = newPath;

	// 같은 이름의 이미지 파일도 함께 이름 변경
	if (!oldImage.IsEmpty())
	{
		CString newImage;
		if (oldImage.GetLength() > oldPath.GetLength() &&
			_wcsnicmp(oldImage, oldPath, oldPath.GetLength()) == 0)
		{
			// movie.mp4.jpg 형식
			newImage = newPath + oldImage.Mid(oldPath.GetLength());
		}
		else
		{
			// movie.jpg 형식
			CString newStem = newPath;
			::PathRemoveExtensionW(newStem.GetBuffer());
			newStem.ReleaseBuffer();
			newImage = newStem + ::PathFindExtensionW(oldImage);
		}

		if (newImage.CompareNoCase(oldImage) != 0)
		{
			if (::PathFileExistsW(newImage) && newImage.CompareNoCase(oldImage) != 0)
				AfxMessageBox(L"동영상 이름은 바꿨지만, 같은 이름의 이미지 파일이 이미 있어 이미지 이름은 바꾸지 않았습니다.", MB_ICONWARNING);
			else if (!MoveFileWithRetry(oldImage, newImage))
				AfxMessageBox(L"동영상 이름은 바꿨지만, 이미지 이름은 바꾸지 못했습니다.\n" + LastErrorText(::GetLastError()), MB_ICONWARNING);
		}
	}

	m_lib.Save();
	if (idx == m_curItem)
	{
		m_staticName.SetWindowText(v.FileName());
		UpdatePreview(idx);
	}
	m_list.RedrawItems(row, row);
	m_grid.Invalidate(FALSE);
}

void CRuliManagerDlg::OnBnClickedDelete()
{
	// 선택한 영상의 DB 정보(제목/별점/배우/스튜디오/발매일/태그/메모)만 삭제 - 파일은 그대로 둠
	const int row = GetSelectedRow();
	const int idx = GetSelectedItem();
	if (idx < 0)
	{
		AfxMessageBox(L"DB에서 삭제할 동영상을 선택하세요.", MB_ICONINFORMATION);
		return;
	}

	const VideoItem old = m_lib.items[idx];
	const bool exists = ::PathFileExistsW(old.path) != FALSE;
	CString msg;
	if (old.pending)
		msg.Format(L"임시 항목을 목록에서 뺄까요?\n(파일은 삭제되지 않으며, 다음 폴더 스캔 때 다시 임시 항목으로 나타납니다)\n\n%s",
			static_cast<LPCWSTR>(old.path));
	else if (exists)
		msg.Format(L"선택한 영상의 DB 정보를 삭제할까요?\n(파일은 삭제되지 않고 임시 항목으로 돌아갑니다)\n\n%s",
			static_cast<LPCWSTR>(old.path));
	else
		msg.Format(L"선택한 영상의 DB 정보를 삭제할까요?\n(파일이 없어서 목록에서도 빠집니다)\n\n%s",
			static_cast<LPCWSTR>(old.path));
	if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;

	BeginLibraryChange();
	if (!old.pending && exists)
	{
		// 저장된 정보는 모두 지우고 스캔으로 처음 찾은 상태(임시 항목)로 되돌림
		VideoItem fresh;
		fresh.path = old.path;
		fresh.size = old.size;
		fresh.modified = old.modified;
		fresh.pending = true;
		m_lib.ApplyFolderStructure(fresh);   // 폴더 구조/이름의 배우·스튜디오·발매일은 다시 자동 지정
		m_lib.items[idx] = fresh;
	}
	else
	{
		m_lib.items.erase(m_lib.items.begin() + idx);
	}
	m_lib.Save();
	ApplyFilter();

	// 같은 자리(없으면 마지막) 항목 선택
	if (!m_view.empty())
	{
		const int next = (std::min)((std::max)(row, 0), static_cast<int>(m_view.size()) - 1);
		m_list.SetItemState(next, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_list.EnsureVisible(next, FALSE);
	}
}

void CRuliManagerDlg::OnBnClickedOpenDb()
{
	// 라이브러리(DB) 파일이 있는 폴더를 탐색기로 열고 파일을 선택
	CommitDetails();
	m_lib.Save();   // 최신 내용이 파일에 있도록

	const CString path = CVideoLibrary::GetDataFilePath();   // <실행 파일 폴더>\library.vmdb (정보 + 이미지 하나로)
	if (::PathFileExistsW(path))
	{
		PIDLIST_ABSOLUTE pidl = ::ILCreateFromPathW(path);
		if (pidl)
		{
			::SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
			::ILFree(pidl);
			return;
		}
	}

	// 파일이 없으면 폴더만 열기
	CString folder = path;
	::PathRemoveFileSpecW(folder.GetBuffer());
	folder.ReleaseBuffer();
	::ShellExecuteW(m_hWnd, L"open", folder, nullptr, nullptr, SW_SHOWNORMAL);
}

void CRuliManagerDlg::OnBnClickedExplorer()
{
	const int idx = GetSelectedItem();
	if (idx < 0)
	{
		AfxMessageBox(L"동영상을 선택하세요.", MB_ICONINFORMATION);
		return;
	}

	const CString path = m_lib.items[idx].path;
	PIDLIST_ABSOLUTE pidl = ::ILCreateFromPathW(path);
	if (pidl)
	{
		::SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
		::ILFree(pidl);
	}
	else
	{
		CString args = L"/select,\"" + path + L"\"";
		::ShellExecuteW(m_hWnd, L"open", L"explorer.exe", args, nullptr, SW_SHOWNORMAL);
	}
}

void CRuliManagerDlg::OnBnClickedOpenDefault()
{
	const int idx = GetSelectedItem();
	if (idx < 0)
	{
		AfxMessageBox(L"동영상을 선택하세요.", MB_ICONINFORMATION);
		return;
	}

	const CString path = m_lib.items[idx].path;
	if (!::PathFileExistsW(path))
	{
		AfxMessageBox(L"파일이 존재하지 않습니다.\n[새로고침]으로 목록을 갱신하세요.", MB_ICONWARNING);
		return;
	}
	const HINSTANCE h = ::ShellExecuteW(m_hWnd, L"open", path, nullptr, nullptr, SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(h) <= 32)
		AfxMessageBox(L"기본 프로그램으로 열지 못했습니다.", MB_ICONWARNING);
}

// ---------------------------------------------------------------------------
// 오른쪽 클릭 메뉴

void CRuliManagerDlg::OnContextMenu(CWnd* pWnd, CPoint point)
{
	if (pWnd->GetSafeHwnd() == m_actorGrid.GetSafeHwnd())
	{
		int row = m_actorSel;
		if (point.x == -1 && point.y == -1)
		{
			CRect r;
			if (row < 0 || !m_actorGrid.GetTileRect(row, r)) return;
			point = r.CenterPoint();
			m_actorGrid.ClientToScreen(&point);
		}
		else
		{
			CPoint pt = point;
			m_actorGrid.ScreenToClient(&pt);
			row = m_actorGrid.HitTest(pt);
			if (row < 0) return;
			SelectActorRow(row);
		}
		CMenu menu;
		menu.CreatePopupMenu();
		menu.AppendMenu(MF_STRING, ID_ACTOR_SHOWVIDEOS, L"출연작 보기\tEnter");
		menu.AppendMenu(MF_STRING, ID_ACTOR_EDIT,       L"배우 정보 편집...\tF2");
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, ID_ACTOR_DELETE,     L"배우 삭제\tDel");
		menu.SetDefaultItem(ID_ACTOR_SHOWVIDEOS);
		menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
		return;
	}

	if (pWnd->GetSafeHwnd() == m_catGrid.GetSafeHwnd())
	{
		int row = m_listCat.GetNextItem(-1, LVNI_SELECTED);
		if (point.x == -1 && point.y == -1)
		{
			CRect r;
			if (row < 0 || !m_catGrid.GetTileRect(row, r)) return;
			point = r.CenterPoint();
			m_catGrid.ClientToScreen(&point);
		}
		else
		{
			CPoint pt = point;
			m_catGrid.ScreenToClient(&pt);
			row = m_catGrid.HitTest(pt);
			if (row < 0) return;
			m_catOwner.GridSetSel(row);
		}
		CMenu menu;
		menu.CreatePopupMenu();
		menu.AppendMenu(MF_STRING, ID_ACTOR_SHOWVIDEOS, L"영상 보기\tEnter");
		menu.AppendMenu(MF_STRING, ID_ACTOR_EDIT,       L"편집...\tF2");
		if (m_mode == MODE_TAG)
		{
			menu.AppendMenu(MF_SEPARATOR);
			menu.AppendMenu(MF_STRING, ID_CAT_DELETE, L"삭제");
		}
		menu.SetDefaultItem(ID_ACTOR_SHOWVIDEOS);
		menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
		return;
	}

	if (pWnd->GetSafeHwnd() == m_listCat.GetSafeHwnd())
	{
		int row = m_listCat.GetNextItem(-1, LVNI_SELECTED);
		if (point.x == -1 && point.y == -1)
		{
			if (row < 0) return;
			CRect r;
			m_listCat.GetItemRect(row, &r, LVIR_LABEL);
			point = CPoint(r.left + 10, r.bottom);
			m_listCat.ClientToScreen(&point);
		}
		else
		{
			CPoint pt = point;
			m_listCat.ScreenToClient(&pt);
			row = m_listCat.HitTest(pt);
			if (row < 0) return;
			m_listCat.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		}
		CMenu menu;
		menu.CreatePopupMenu();
		menu.AppendMenu(MF_STRING, ID_ACTOR_SHOWVIDEOS, L"영상 보기\tEnter");
		menu.AppendMenu(MF_STRING, ID_ACTOR_EDIT,       L"편집...\tF2");
		if (m_mode == MODE_TAG)
		{
			menu.AppendMenu(MF_SEPARATOR);
			menu.AppendMenu(MF_STRING, ID_CAT_DELETE, L"삭제");
		}
		menu.SetDefaultItem(ID_ACTOR_SHOWVIDEOS);
		menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
		return;
	}

	const bool fromGrid = (pWnd->GetSafeHwnd() == m_grid.GetSafeHwnd());
	if (pWnd->GetSafeHwnd() != m_list.GetSafeHwnd() && !fromGrid)
	{
		CDialogEx::OnContextMenu(pWnd, point);
		return;
	}

	int row = -1;
	if (point.x == -1 && point.y == -1)   // 키보드(Shift+F10, 메뉴 키)
	{
		row = GetSelectedRow();
		if (row < 0) return;
		CRect r;
		if (fromGrid)
		{
			m_grid.GetTileRect(row, r);
			point = r.CenterPoint();
			m_grid.ClientToScreen(&point);
		}
		else
		{
			m_list.GetItemRect(row, &r, LVIR_LABEL);
			point = CPoint(r.left + 10, r.bottom);
			m_list.ClientToScreen(&point);
		}
	}
	else if (fromGrid)
	{
		CPoint pt = point;
		m_grid.ScreenToClient(&pt);
		row = m_grid.HitTest(pt);
		if (row < 0) return;
		GridSetSel(row);
	}
	else
	{
		CPoint pt = point;
		m_list.ScreenToClient(&pt);
		row = m_list.HitTest(pt);
		if (row < 0) return;
		m_list.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	}

	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, IDC_BTN_OPENDEFAULT, L"재생 (기본 플레이어)\t더블클릭");
	menu.AppendMenu(MF_STRING, IDC_BTN_EXPLORER,    L"탐색기에서 보기");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, IDC_BTN_DELETE,      L"DB에서 삭제 (파일 유지)\tDel");
	menu.SetDefaultItem(IDC_BTN_OPENDEFAULT);
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
}

// ---------------------------------------------------------------------------
// 종료 / 키 처리

void CRuliManagerDlg::OnOK()
{
	// Enter 키: 목록에서는 기본 플레이어로 재생, 그 외에는 무시 (대화상자가 닫히지 않도록)
	CWnd* focus = GetFocus();
	if (focus && focus->GetSafeHwnd() == m_actorGrid.GetSafeHwnd())
	{
		if (m_actorSel >= 0)
			DrillIntoActor(m_actorSel);
		return;
	}
	if (focus && (focus->GetSafeHwnd() == m_listCat.GetSafeHwnd() || focus->GetSafeHwnd() == m_catGrid.GetSafeHwnd()))
	{
		DrillIntoCategory(m_listCat.GetNextItem(-1, LVNI_SELECTED));
		return;
	}
	if (focus && (focus->GetSafeHwnd() == m_list.GetSafeHwnd() || focus->GetSafeHwnd() == m_grid.GetSafeHwnd()))
		OnBnClickedOpenDefault();
	else if (focus && (focus->GetSafeHwnd() == m_editTags.GetSafeHwnd() ||
	                   focus->GetSafeHwnd() == m_tagChips.m_edit.GetSafeHwnd() ||
	                   focus->GetSafeHwnd() == m_actorChips.m_edit.GetSafeHwnd() ||
	                   focus->GetSafeHwnd() == m_studioChips.m_edit.GetSafeHwnd() ||
	                   focus->GetSafeHwnd() == m_editTitle.GetSafeHwnd() ||
	                   focus->GetSafeHwnd() == m_editCode.GetSafeHwnd() ||
	                   focus->GetSafeHwnd() == m_editActors.GetSafeHwnd() ||
	                   focus->GetSafeHwnd() == m_editVAliases.GetSafeHwnd() ||
	                   focus->GetSafeHwnd() == m_editStudio.GetSafeHwnd()))
		CommitDetails();
}

void CRuliManagerDlg::OnCancel()
{
	// ESC 키로 닫히지 않도록 무시. 종료는 OnClose 에서 처리.
}

// ---------------------------------------------------------------------------
// 배경색 (#202B33)

void CRuliManagerDlg::ApplyColors()
{
	if (!m_bgBrush.GetSafeHandle())
		m_bgBrush.CreateSolidBrush(kBackColor);
	if (!m_editBrush.GetSafeHandle())
		m_editBrush.CreateSolidBrush(kEditColor);

	// 목록 컨트롤
	CListCtrl* lists[] = { &m_list, &m_listCat };
	for (CListCtrl* l : lists)
	{
		l->SetBkColor(kBackColor);
		l->SetTextBkColor(kBackColor);
		l->SetTextColor(kTextColor);
	}

	// 콤보박스 (#394B59) / 날짜 컨트롤 (#1B252C)
	{
		CClientDC dc(this);
		CFont* old = dc.SelectObject(GetFont());
		TEXTMETRIC tm = {};
		dc.GetTextMetrics(&tm);
		dc.SelectObject(old);
		const int itemH = tm.tmHeight + 6;
		const int fieldH = (std::max)(itemH - 2, DY(13) - 6);   // 에디트와 비슷한 높이
		CDarkCombo* combos[] = { &m_comboFilter, &m_comboSort };
		for (CDarkCombo* c : combos)
		{
			c->SetColors(kComboColor, kTextColor, RGB(38, 79, 120));
			c->SetHeights(fieldH, itemH);
		}
	}
	m_dateRelease.SetColors(kDateColor, kTextColor, RGB(70, 85, 98));

	// 버튼 (#137CBD)
	const UINT buttons[] = {
		IDC_BTN_ADDFOLDER, IDC_BTN_REMOVEFOLDER, IDC_BTN_REFRESH, IDC_BTN_RESCAN, IDC_BTN_ACTORS,
		IDC_BTN_ACTOR_BACK, IDC_BTN_SAVE, IDC_BTN_PICKACTORS, IDC_BTN_PICKVALIASES, IDC_BTN_PICKSTUDIO, IDC_BTN_PICKTAGS,
		IDC_BTN_OPENDEFAULT, IDC_BTN_DELETE, IDC_BTN_EXPLORER, IDC_BTN_SORTDIR, IDC_BTN_OPENDB, IDC_BTN_CHANGEIMAGE, IDC_BTN_SEARCHIMAGE,
		IDC_BTN_APPLYDB, IDC_BTN_EXPORTTXT
	};
	for (size_t i = 0; i < _countof(buttons) && i < _countof(m_darkButtons); ++i)
	{
		if (m_darkButtons[i].GetSafeHwnd() || m_darkButtons[i].Attach(buttons[i], this))
		{
			// 삭제/제거 버튼은 빨간색
			const bool danger = (buttons[i] == IDC_BTN_DELETE || buttons[i] == IDC_BTN_REMOVEFOLDER);
			const bool isSave = (buttons[i] == IDC_BTN_SAVE || buttons[i] == IDC_BTN_APPLYDB);   // 저장 · DB 반영 버튼은 초록색
			m_darkButtons[i].SetColors(danger ? kDangerColor : (isSave ? kSaveColor : kButtonColor), RGB(255, 255, 255), kBackColor);
		}
	}

	// 설정 버튼 (회색 바탕 + 흰 톱니바퀴)
	if (m_btnSettings.GetSafeHwnd() || m_btnSettings.Attach(IDC_BTN_SETTINGS, this))
	{
		m_btnSettings.SetColors(RGB(0x39, 0x4B, 0x59), RGB(255, 255, 255), kBackColor);
		m_btnSettings.SetIcon(CDarkButton::ICON_GEAR);
	}

	// 격자 / 미리보기
	m_grid.SetBackColor(kBackColor);
	m_actorGrid.SetBackColor(kBackColor);
	m_catGrid.SetBackColor(kBackColor);
	m_preview.SetBackColor(RGB(0x18, 0x21, 0x28));

	// 영상 / 배우 / 스튜디오 / 태그: 토글 버튼 (선택 #137CBD, 나머지 #394B59)
	for (int i = 0; i < 4; ++i)
	{
		if (m_modeButtons[i].GetSafeHwnd() || m_modeButtons[i].Attach(IDC_RADIO_VIDEO + i, this))
		{
			m_modeButtons[i].SetColors(kButtonColor, RGB(255, 255, 255), kBackColor);
			m_modeButtons[i].SetToggle(kComboColor, RGB(190, 200, 208));
			if (i == MODE_VIDEO)
				m_modeButtons[i].SetIcon(CDarkButton::ICON_PLAY);     // [● ▶ 영상]
			else if (i == MODE_ACTOR)
				m_modeButtons[i].SetIcon(CDarkButton::ICON_PERSON);   // [사람 배우]
			else if (i == MODE_STUDIO)
				m_modeButtons[i].SetIcon(CDarkButton::ICON_CAMERA);   // [카메라 스튜디오]
			else if (i == MODE_TAG)
				m_modeButtons[i].SetIcon(CDarkButton::ICON_TAG);      // [꼬리표 태그]
		}
	}
	UpdateModeButtons();

	// 라디오 버튼은 테마가 켜져 있으면 글자색을 바꿀 수 없으므로 테마를 끔
	const UINT radios[] = { IDC_RADIO_LISTVIEW, IDC_RADIO_GRIDVIEW };
	for (UINT id : radios)
	{
		if (CWnd* w = GetDlgItem(id))
			::SetWindowTheme(w->GetSafeHwnd(), L"", L"");
	}
	Invalidate();
}

void CRuliManagerDlg::CreateScrollBars()
{
	// 목록 / 분류 목록 / 메모 / 격자의 스크롤바를 #6C7478 로 직접 그림
	struct Target { CWnd* wnd; bool vertical; CDarkScrollBar::TargetType type; };
	const Target targets[] = {
		{ &m_list,      true,  CDarkScrollBar::TARGET_LISTVIEW },
		{ &m_list,      false, CDarkScrollBar::TARGET_LISTVIEW },
		{ &m_listCat,   true,  CDarkScrollBar::TARGET_LISTVIEW },
		{ &m_listCat,   false, CDarkScrollBar::TARGET_LISTVIEW },
		{ &m_editMemo,  true,  CDarkScrollBar::TARGET_EDIT },
		{ &m_grid,      true,  CDarkScrollBar::TARGET_GRID },
		{ &m_actorGrid, true,  CDarkScrollBar::TARGET_GRID },
		{ &m_catGrid,   true,  CDarkScrollBar::TARGET_GRID },
	};
	for (size_t i = 0; i < _countof(targets) && i < _countof(m_scrollBars); ++i)
	{
		if (m_scrollBars[i].GetSafeHwnd() || !targets[i].wnd->GetSafeHwnd())
			continue;
		m_scrollBars[i].SetColors(kScrollColor, RGB(0xB5, 0xBC, 0xC0), RGB(0xDD, 0xE2, 0xE5));
		m_scrollBars[i].Create(this, targets[i].wnd, targets[i].vertical, targets[i].type);
	}
}

void CRuliManagerDlg::UpdateModeButtons()
{
	for (int i = 0; i < 4; ++i)
	{
		if (m_modeButtons[i].GetSafeHwnd())
			m_modeButtons[i].SetChecked(i == m_mode);
	}
}

HBRUSH CRuliManagerDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (!m_bgBrush.GetSafeHandle())
		return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);

	switch (nCtlColor)
	{
	case CTLCOLOR_DLG:
		return m_bgBrush;

	case CTLCOLOR_EDIT:
		// 에디트 (검색, 제목, 메모 등)
		pDC->SetTextColor(kTextColor);
		pDC->SetBkColor(kEditColor);
		return m_editBrush;

	case CTLCOLOR_STATIC:
	{
		// 읽기 전용/비활성 에디트도 CTLCOLOR_STATIC 을 보냄 → 에디트 배경색 사용
		TCHAR cls[16] = {};
		::GetClassName(pWnd->GetSafeHwnd(), cls, 16);
		if (_tcsicmp(cls, _T("Edit")) == 0)
		{
			pDC->SetTextColor(pWnd->IsWindowEnabled() ? kTextColor : kDisabledText);
			pDC->SetBkColor(kEditColor);
			return m_editBrush;
		}
		pDC->SetTextColor(kTextColor);
		pDC->SetBkColor(kBackColor);
		pDC->SetBkMode(TRANSPARENT);
		return m_bgBrush;
	}

	case CTLCOLOR_BTN:
		pDC->SetBkColor(kBackColor);
		return m_bgBrush;   // 버튼 모서리가 흰색으로 보이지 않도록
	}
	return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CRuliManagerDlg::OnClose()
{
	CommitDetails();
	m_lib.Save();
	// 종료할 때 작업 DB 를 원본(library.vmdb)에 반영하고 작업 DB 삭제
	if (!m_lib.DbBroken())
	{
		if (m_lib.ApplyWorkDb())
			CVideoLibrary::DiscardWorkDb();
		else if (AfxMessageBox(L"DB 에 반영하지 못했습니다 (파일이 사용 중이거나 쓰기 권한 없음).\n"
			L"그래도 종료할까요? (변경 내용은 library.work.vmdb 에 남아 있어 다음 실행 때 반영할 수 있습니다)",
			MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
			return;
	}
	SaveWindowPlacement();
	m_preview.Clear();
	CVideoLibrary::ClearImageCache();   // 예전 버전이 %LOCALAPPDATA% 캐시에 풀어 둔 이미지 정리 (이미지는 이제 실행 폴더의 Image 폴더)
	EndDialog(IDCANCEL);
}

void CRuliManagerDlg::SaveWindowPlacement()
{
	WINDOWPLACEMENT wp = { sizeof(wp) };
	if (GetWindowPlacement(&wp))
		AfxGetApp()->WriteProfileBinary(L"Settings", L"WindowPlacement", reinterpret_cast<LPBYTE>(&wp), sizeof(wp));
}

void CRuliManagerDlg::RestoreWindowPlacement()
{
	LPBYTE data = nullptr;
	UINT size = 0;
	if (!AfxGetApp()->GetProfileBinary(L"Settings", L"WindowPlacement", &data, &size) || !data)
		return;
	WINDOWPLACEMENT wp = {};
	const bool ok = (size == sizeof(wp));
	if (ok)
		memcpy(&wp, data, sizeof(wp));
	delete[] data;
	if (!ok || wp.length != sizeof(wp))
		return;

	// 저장된 위치가 지금 연결된 모니터 밖이면(모니터 구성 변경 등) 복원하지 않음
	CRect rc(wp.rcNormalPosition);
	if (rc.Width() < 200 || rc.Height() < 150 || !::MonitorFromRect(&rc, MONITOR_DEFAULTTONULL))
		return;
	if (wp.showCmd == SW_SHOWMINIMIZED || wp.showCmd == SW_MINIMIZE)
		wp.showCmd = SW_SHOWNORMAL;   // 최소화 상태로는 시작하지 않음
	wp.flags = 0;
	SetWindowPlacement(&wp);
}

// ---------------------------------------------------------------------------
// 영상 상세 정보: 태그 아래 출연 배우 카드

void CRuliManagerDlg::RefreshActorStrip()
{
	if (!m_actorStrip.GetSafeHwnd())
		return;
	CString text;
	m_editActors.GetWindowText(text);
	std::vector<int> list;
	for (const CString& n : CVideoLibrary::SplitList(text))
	{
		const int idx = m_lib.FindActorByAnyName(n);   // 별칭으로 적혀 있어도 그 배우
		if (idx >= 0 && std::find(list.begin(), list.end(), idx) == list.end())
			list.push_back(idx);
	}
	m_stripActors.swap(list);
	m_actorStrip.SetCount(static_cast<int>(m_stripActors.size()));
	m_actorStrip.Invalidate(FALSE);
}

void CRuliManagerDlg::DrawStripCard(CDC* dc, int i, const CRect& rc, bool hot)
{
	if (i < 0 || i >= static_cast<int>(m_stripActors.size()))
		return;
	const int idx = m_stripActors[i];
	if (idx < 0 || idx >= static_cast<int>(m_lib.actors.size()))
		return;
	const ActorInfo& a = m_lib.actors[idx];
	const int radius = DX(4);

	// 카드 배경 (둥근 모서리)
	{
		CBrush br(kCardBack);
		CPen pen(PS_SOLID, 1, kCardBack);
		CBrush* ob = dc->SelectObject(&br);
		CPen* op = dc->SelectObject(&pen);
		dc->RoundRect(rc, CPoint(radius, radius));
		dc->SelectObject(ob);
		dc->SelectObject(op);
	}

	// 사진 (3:4, 위쪽 모서리만 둥글게)
	const CRect img(rc.left, rc.top, rc.right, rc.top + rc.Width() * 4 / 3);
	{
		CRgn clip;
		clip.CreateRoundRectRgn(rc.left, rc.top, rc.right + 1, rc.bottom + 1, radius, radius);
		dc->SelectClipRgn(&clip);
		dc->IntersectClipRect(img);
		if (CBitmap* bmp = GetActorPortrait(idx, img.Width(), img.Height()))
		{
			CDC mem;
			mem.CreateCompatibleDC(dc);
			CBitmap* old = mem.SelectObject(bmp);
			dc->BitBlt(img.left, img.top, img.Width(), img.Height(), &mem, 0, 0, SRCCOPY);
			mem.SelectObject(old);
		}
		else
		{
			dc->FillSolidRect(img, kCardImage);
			DrawDefaultActorImage(dc, img, a);
		}
		dc->SelectClipRgn(nullptr);
	}

	// 별점 리본 (왼쪽 위) / 즐겨찾기 하트 (오른쪽 위, 지정된 경우만)
	DrawRatingRibbon(dc, rc, img, radius, a.rating, GetFont());
	// 국기: 사진 오른쪽 아래 (배우 탭 카드와 같게)
	{
		const int flag = CCountryCombo::FindCountry(a.nationality);
		if (flag >= 0)
		{
			const int fp = (std::max)(3, m_cardPad / 2);
			CCountryCombo::DrawFlag(dc, flag, img.right - fp - CCountryCombo::kFlagW, img.bottom - fp - CCountryCombo::kFlagH);
		}
	}
	{
		// 즐겨찾기 하트: 지정 = 빨간 하트, 하트 자리에 마우스 오버 = 반투명 회색 하트 (누르면 전환)
		const CRect heart = StripHeartRect(rc);
		if (a.favorite)
			DrawHeart(dc, heart, RGB(0xF2, 0x5C, 0x54), 255, true);
		else if (hot && m_actorStrip.HotPart() == 1)
			DrawHeart(dc, heart, RGB(0xC8, 0xCC, 0xD0), 170, true);
	}

	// 이름 3줄: 첫 줄 = 괄호 앞 이름(굵게), 둘째·셋째 줄 = 괄호 안 이름 (쉼표마다 줄을 나눔, 없으면 나이)
	CString mainName = a.name, subName;
	{
		int po = a.name.Find(L'(');
		if (po < 0) po = a.name.Find(L'\xFF08');
		if (po > 0)
		{
			int pc = a.name.ReverseFind(L')');
			const int pcW = a.name.ReverseFind(L'\xFF09');
			if (pcW > pc) pc = pcW;
			if (pc < po) pc = a.name.GetLength();
			CString inner = a.name.Mid(po + 1, pc - po - 1);
			inner.Trim();
			CString left = a.name.Left(po);
			left.Trim();
			if (!left.IsEmpty() && !inner.IsEmpty())
			{
				mainName = left;
				subName = inner;
			}
		}
	}
	if (subName.IsEmpty())
	{
		const int age = CVideoLibrary::CalcAge(a.birth);
		if (age >= 0)
			subName.Format(L"%d 살", age);
	}

	CFont* oldFont = dc->SelectObject(&m_cardNameFont);
	dc->SetBkMode(TRANSPARENT);
	const int x = rc.left + DX(3), right = rc.right - DX(3);
	int y = img.bottom + m_cardPad / 2;
	{
		// 성별 기호(♀ 분홍 / ♂ 파랑) + 이름을 한 덩어리로 가운데 정렬
		CString sym;
		COLORREF symColor = kTextColor;
		GenderSymbol(a.gender, sym, symColor);   // ♀ ♂ ⚧ ⚥ ⚲
		int symW = 0;
		if (!sym.IsEmpty())
		{
			dc->SelectObject(&m_cardSymbolFont);
			symW = TextFB::Width(dc, sym) + DX(2);
			dc->SelectObject(&m_cardNameFont);
		}
		const int avail = right - x;
		const int nameW = (std::min)(TextFB::Width(dc, mainName), (std::max)(10, avail - symW));
		const int startX = x + (std::max)(0, (avail - symW - nameW) / 2);
		if (!sym.IsEmpty())
		{
			dc->SelectObject(&m_cardSymbolFont);
			dc->SetTextColor(symColor);
			CRect sr(startX, y, startX + symW, y + m_cardLineB);
			TextFB::Draw(dc, sym, sr, DT_LEFT, false);
			dc->SelectObject(&m_cardNameFont);
		}
		dc->SetTextColor(kTextColor);
		TextFB::Draw(dc, mainName, CRect(startX + symW, y, (std::min)(right, startX + symW + nameW + 1), y + m_cardLineB), DT_LEFT, true);
	}
	y += m_cardLineB;
	if (!subName.IsEmpty())
	{
		// 괄호 안에 쉼표가 있으면 나눠서 줄마다 (최대 2줄, 남는 것은 셋째 줄에 이어서)
		std::vector<CString> parts;
		{
			CString t = subName;
			t.Replace(L'\xFF0C', L',');   // 전각 ，
			t.Replace(L'\x3001', L',');   // 、
			int start = 0;
			for (;;)
			{
				const int comma = t.Find(L',', start);
				CString part = (comma < 0) ? t.Mid(start) : t.Mid(start, comma - start);
				part.Trim();
				if (!part.IsEmpty())
					parts.push_back(part);
				if (comma < 0)
					break;
				start = comma + 1;
			}
		}
		if (parts.size() > 2)
		{
			for (size_t k = 2; k < parts.size(); ++k)
				parts[1] += L", " + parts[k];
			parts.resize(2);
		}
		dc->SelectObject(GetFont());
		dc->SetTextColor(kCardSub);
		for (const CString& part : parts)
		{
			TextFB::Draw(dc, part, CRect(x, y, right, y + m_cardLine), DT_CENTER, true);
			y += m_cardLine;
		}
	}

	// 맨 아래: 구분선 + 제작 당시 나이 (발매일 - 생년월일)
	{
		const int lineY = rc.bottom - m_cardPad / 2 - m_cardLine - m_cardPad / 2 - 1;
		dc->FillSolidRect(rc.left, lineY, rc.Width(), 1, kCardLine);
		int age = -1;
		SYSTEMTIME rel = {};
		int by = 0, bm = 0, bd = 0;
		const bool hasRelease = (m_curItem >= 0 && m_dateRelease.GetSafeHwnd() && m_dateRelease.GetTime(&rel) == GDT_VALID);
		if (hasRelease && !a.birth.IsEmpty() && swscanf_s(a.birth, L"%d-%d-%d", &by, &bm, &bd) == 3 && by > 0)
		{
			age = rel.wYear - by;
			if (rel.wMonth < bm || (rel.wMonth == bm && rel.wDay < bd))
				--age;   // 발매일에 아직 생일 전
			if (age < 0 || age >= 150)
				age = -1;
		}
		CString text;
		if (age >= 0)
			text.Format(L"제작 당시 나이 %d살", age);
		else
			text = L"제작 당시 나이 -";
		dc->SelectObject(GetFont());
		dc->SetTextColor(age >= 0 ? kCardIcon : kCardSub);
		const int ty = lineY + 1 + m_cardPad / 2;
		TextFB::Draw(dc, text, CRect(rc.left + DX(3), ty, rc.right - DX(3), ty + m_cardLine), DT_CENTER, true);
	}
	dc->SelectObject(oldFont);

	// 마우스 오버: 파란 테두리
	if (hot)
	{
		CPen pen(PS_SOLID, 2, kButtonColor);
		CPen* op = dc->SelectObject(&pen);
		CBrush* ob = static_cast<CBrush*>(dc->SelectStockObject(NULL_BRUSH));
		CRect sr = rc;
		sr.DeflateRect(1, 1);
		dc->RoundRect(sr, CPoint(radius, radius));
		dc->SelectObject(op);
		dc->SelectObject(ob);
	}
}

CRect CRuliManagerDlg::StripHeartRect(const CRect& card)
{
	const int size = (std::max)(12, card.Width() * 20 / 100);
	const int pad = (std::max)(3, DX(3));
	return CRect(card.right - pad - size, card.top + pad, card.right - pad, card.top + pad + size);
}

void CRuliManagerDlg::OpenActorFromStrip(int i)
{
	if (i < 0 || i >= static_cast<int>(m_stripActors.size()))
		return;
	const int idx = m_stripActors[i];
	if (idx < 0 || idx >= static_cast<int>(m_lib.actors.size()))
		return;

	CommitDetails();
	m_actorSelName = m_lib.actors[idx].name;   // 배우 탭으로 바뀌면 이 배우를 선택
	if (m_mode != MODE_ACTOR)
		OnModeChanged(IDC_RADIO_ACTOR);
	if (SelectedActorIndex() != idx)
	{
		// 검색어 때문에 목록에 없으면 검색을 지우고 다시
		m_editSearch.SetWindowText(L"");
		RebuildActorGrid();
	}
	for (size_t row = 0; row < m_actorRows.size(); ++row)
	{
		if (m_actorRows[row] == idx)
		{
			SelectActorRow(static_cast<int>(row));
			m_actorGrid.EnsureVisible(static_cast<int>(row));
			break;
		}
	}
	m_actorGrid.SetFocus();
}

// ---------------------------------------------------------------------------
// 작업 DB 반영

void CRuliManagerDlg::UpdateApplyDbButton()
{
	if (CWnd* w = GetDlgItem(IDC_BTN_APPLYDB))
		w->EnableWindow(m_lib.HasUnappliedChanges() && !m_lib.DbBroken());
}

void CRuliManagerDlg::OnBnClickedApplyDb()
{
	CommitDetails();   // 편집 중인 상세 정보도 먼저 저장
	if (!m_lib.HasUnappliedChanges())
	{
		UpdateApplyDbButton();
		return;
	}
	CWaitCursor wait;
	if (m_lib.ApplyWorkDb())
		SetDlgItemText(IDC_STATIC_STATUS, L"DB 에 반영했습니다.");
	else
		AfxMessageBox(L"DB 에 반영하지 못했습니다 (파일이 사용 중이거나 쓰기 권한 없음).", MB_ICONWARNING);
	UpdateApplyDbButton();
}

// ---------------------------------------------------------------------------
// 재 스캔: 새로고침 + 이미 저장된 영상에도 폴더 구조 다시 적용

void CRuliManagerDlg::OnBnClickedRescan()
{
	if (AfxMessageBox(L"재 스캔할까요?\n\n"
		L"등록 폴더를 다시 읽고, 이미 저장된 영상에도 영상 폴더의 정보 txt 와 폴더 구조를 다시 적용해\n"
		L"비어 있는 칸(제목 · 발매일 · 배우 · 스튜디오 · 태그 · 메모 등)을 채웁니다. (값이 있는 칸은 바꾸지 않음)\n\n"
		L"영상이 많으면 시간이 걸릴 수 있습니다.",
		MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;
	CommitDetails();
	const CString keep = (m_curItem >= 0) ? m_lib.items[m_curItem].path : CString();

	int added = 0, removed = 0, relinked = 0, filled = 0;
	{
		CWaitCursor wait;
		BeginLibraryChange();
		m_lib.Refresh(added, removed, &relinked);   // 새 파일 · 없어진 파일 · 경로가 바뀐 파일
		// 저장된 영상도 폴더/파일 이름에서 비어 있는 배우 · 스튜디오 · 발매일을 다시 채움 (값이 있는 칸은 그대로)
		for (VideoItem& v : m_lib.items)
		{
			const VideoItem before = v;
			m_lib.ApplyFolderStructure(v);   // 영상 폴더 txt → 폴더 구조 순서로 빈칸 채움
			if (v.code != before.code || v.actors != before.actors || v.studio != before.studio || v.release != before.release ||
				v.title != before.title || v.tags != before.tags || v.memo != before.memo ||
				v.rating != before.rating || v.oCount != before.oCount)
				++filled;
		}
		m_lib.SyncActorsFromVideos();   // 새 배우 추가 (배우 폴더의 사진 · 텍스트 정보 포함)
		m_lib.SyncNamedFromVideos();
		ResetThumbnails();
		m_lib.Save();
		RebuildActorGrid();
		m_catsDirty = true;
		ApplyFilter();
		SelectPath(keep);
		if (m_curItem >= 0)
			ShowDetails(m_curItem);   // 채워진 배우 · 스튜디오 · 발매일을 상세 정보에도
	}

	CString s;
	s.Format(L"재 스캔 완료: txt · 폴더 구조로 정보를 채운 영상 %d개, 경로가 바뀐 파일 %d개 다시 연결, 새 파일 %d개 임시 목록에 추가, 없어진 파일 %d개 제거",
		filled, relinked, added, removed);
	m_staticStatus.SetWindowText(s);
}

// ---------------------------------------------------------------------------
// 정보 txt 생성: 영상 정보 → 영상 폴더\영상이름.txt, 배우 정보 → 배우 폴더\배우폴더이름.txt

void CRuliManagerDlg::OnBnClickedExportTxt()
{
	if (AfxMessageBox(L"배우 · 영상 정보를 txt 파일로 만들까요?\n\n"
		L"· 영상: 저장된 영상마다 같은 폴더에 '영상 이름.txt' (예: ABC-123.mp4 → ABC-123.txt)\n"
		L"· 배우: 배우 폴더가 있는 배우마다 배우 폴더에 '배우 폴더 이름.txt'\n\n"
		L"같은 이름의 txt 가 있으면 덮어씁니다. (내용이 같으면 그대로 둠)",
		MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;
	CommitDetails();   // 편집 중인 내용도 포함

	int vSame = 0, vFail = 0, aSame = 0, aNoFolder = 0, aFail = 0;
	int vWritten = 0, aWritten = 0;
	{
		CWaitCursor wait;
		vWritten = m_lib.ExportVideoInfoTxt(vSame, vFail);
		aWritten = m_lib.ExportActorInfoTxt(aSame, aNoFolder, aFail);
	}
	CString vFailText, aFailText;
	if (vFail) vFailText.Format(L", 실패 %d개", vFail);
	if (aFail) aFailText.Format(L", 실패 %d개", aFail);
	CString msg;
	msg.Format(L"정보 txt 생성 완료\n\n영상: %d개 생성/갱신, %d개 변경 없음%s\n배우: %d개 생성/갱신, %d개 변경 없음, 배우 폴더 없음 %d명%s",
		vWritten, vSame, static_cast<LPCWSTR>(vFailText), aWritten, aSame, aNoFolder, static_cast<LPCWSTR>(aFailText));
	AfxMessageBox(msg, (vFail || aFail) ? MB_ICONWARNING : MB_ICONINFORMATION);
}
