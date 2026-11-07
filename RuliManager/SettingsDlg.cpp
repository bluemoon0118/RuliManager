#include "pch.h"
#include "RuliManager.h"
#include "SettingsDlg.h"
#include <uxtheme.h>
#include <set>
#pragma comment(lib, "uxtheme.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CSettingsDlg::CSettingsDlg(const CVideoLibrary& lib, CWnd* pParent)
	: CDialogEx(IDD_SETTINGS, pParent), m_lib(lib)
{
}

BEGIN_MESSAGE_MAP(CSettingsDlg, CDialogEx)
	ON_WM_CTLCOLOR()
	ON_BN_CLICKED(IDC_SET_DEL_BTN, &CSettingsDlg::OnBnClickedDeleteDb)
	ON_BN_CLICKED(IDC_SET_EXPORT, &CSettingsDlg::OnBnClickedExportList)
	ON_BN_CLICKED(IDC_SET_IMPORT, &CSettingsDlg::OnBnClickedImportList)
	ON_BN_CLICKED(IDC_SET_PREVIEW_BTN, &CSettingsDlg::OnBnClickedMakePreviews)
	ON_BN_CLICKED(IDC_SET_PREVIEW_DEL, &CSettingsDlg::OnBnClickedDeletePreviews)
	ON_BN_CLICKED(IDC_SET_PREVIEW_REGEN, &CSettingsDlg::OnBnClickedRegenPreviews)
	ON_BN_CLICKED(IDC_SET_PREVIEW_STOP, &CSettingsDlg::OnBnClickedStopPreviews)
	ON_WM_TIMER()
	ON_BN_CLICKED(IDC_SET_DEL_VIDEO, &CSettingsDlg::OnCheckChanged)
	ON_BN_CLICKED(IDC_SET_DEL_ACTOR, &CSettingsDlg::OnCheckChanged)
	ON_BN_CLICKED(IDC_SET_DEL_STUDIO, &CSettingsDlg::OnCheckChanged)
	ON_BN_CLICKED(IDC_SET_DEL_TAG, &CSettingsDlg::OnCheckChanged)
	ON_BN_CLICKED(IDC_SET_TAB1, &CSettingsDlg::OnTab1)
	ON_BN_CLICKED(IDC_SET_TAB2, &CSettingsDlg::OnTab2)
	ON_BN_CLICKED(IDC_SET_TAB3, &CSettingsDlg::OnTab3)
	ON_BN_CLICKED(IDC_SET_TAB4, &CSettingsDlg::OnTab4)
	ON_BN_CLICKED(IDC_SET_TAB5, &CSettingsDlg::OnTab5)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_SET_PLUGIN_LIST, &CSettingsDlg::OnPluginItemChanged)
	ON_BN_CLICKED(IDC_SET_PLUGIN_FOLDER, &CSettingsDlg::OnBnClickedPluginFolder)
	ON_BN_CLICKED(IDC_SET_PLUGIN_RELOAD, &CSettingsDlg::OnBnClickedPluginReload)
	ON_BN_CLICKED(IDC_SET_PLUGIN_HELP, &CSettingsDlg::OnBnClickedPluginHelp)
	ON_CBN_SELCHANGE(IDC_SET_THEME, &CSettingsDlg::OnThemeChanged)
END_MESSAGE_MAP()

// ---------------------------------------------------------------------------
// 테마 미리보기

BEGIN_MESSAGE_MAP(CThemePreview, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

bool CThemePreview::Create(CWnd* parent, const CRect& rc, UINT id)
{
	const CString cls = AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_ARROW), nullptr);
	return CWnd::Create(cls, L"", WS_CHILD | WS_VISIBLE, rc, parent, id) != FALSE;
}

void CThemePreview::OnPaint()
{
	CPaintDC paint(this);
	CRect client;
	GetClientRect(&client);
	CDC dc;
	dc.CreateCompatibleDC(&paint);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&paint, client.Width(), client.Height());
	CBitmap* oldBmp = dc.SelectObject(&bmp);
	auto col = [this](int role) { return Theme::Color(m_index, role); };
	CFont* font = GetParent() ? GetParent()->GetFont() : nullptr;
	CFont* oldFont = font ? dc.SelectObject(font) : nullptr;
	dc.SetBkMode(TRANSPARENT);
	TEXTMETRIC tm = {};
	dc.GetTextMetrics(&tm);
	const int lineH = static_cast<int>(tm.tmHeight);

	auto box = [&](const CRect& r, COLORREF fill, COLORREF edge, int radius)
	{
		CBrush br(fill);
		CPen pen(PS_SOLID, 1, edge);
		CBrush* ob = dc.SelectObject(&br);
		CPen* op = dc.SelectObject(&pen);
		if (radius > 0)
			dc.RoundRect(r, CPoint(radius, radius));
		else
			dc.Rectangle(r);
		dc.SelectObject(ob);
		dc.SelectObject(op);
	};
	auto text = [&](const CString& s, CRect r, COLORREF c, UINT fmt)
	{
		dc.SetTextColor(c);
		dc.DrawText(s, r, fmt | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
	};

	// 창 배경
	box(client, col(TC_BACK), col(TC_BORDER), 0);
	const int pad = (std::max)(6, lineH / 2);
	const CRect inner(client.left + pad, client.top + pad, client.right - pad, client.bottom - pad);
	const int h = inner.Height();

	// 1) 배우 카드: 사진 칸 + 이름 + 회색 글자
	const int cardW = (std::min)(h * 3 / 4, inner.Width() / 4);
	const CRect card(inner.left, inner.top, inner.left + cardW, inner.bottom);
	box(card, col(TC_CARD), col(TC_CARD), 6);
	const CRect photo(card.left + 1, card.top + 1, card.right - 1, card.top + (std::max)(lineH, card.Height() - lineH * 2 - 4));
	dc.FillSolidRect(photo, col(TC_THUMB));
	text(L"사진", photo, col(TC_SUB2), DT_CENTER);
	text(L"배우 이름", CRect(card.left + 4, photo.bottom + 1, card.right - 2, photo.bottom + 1 + lineH), col(TC_TEXT), DT_LEFT);
	text(L"29세", CRect(card.left + 4, photo.bottom + 1 + lineH, card.right - 2, photo.bottom + 1 + lineH * 2), col(TC_SUB), DT_LEFT);

	// 2) 목록: 보통 / 선택 / 마우스 오버 / 회색 글자 줄
	const int gap = pad;
	const int listW = (inner.Width() - cardW - gap * 2) * 45 / 100;
	const CRect list(card.right + gap, inner.top, card.right + gap + listW, inner.bottom);
	box(list, col(TC_EDIT), col(TC_EDGE), 0);
	{
		const int rowH = (list.Height() - 2) / 4;
		struct Row { LPCWSTR s; int back; int fore; };
		const Row rows[4] = { { L"목록 항목", -1, TC_TEXT }, { L"선택한 항목", TC_SEL, TC_TEXT },
		                      { L"마우스 오버", TC_HILITE, TC_TEXT }, { L"회색 글자", -1, TC_GRIDSUB } };
		for (int i = 0; i < 4; ++i)
		{
			CRect r(list.left + 1, list.top + 1 + rowH * i, list.right - 1, list.top + 1 + rowH * (i + 1));
			if (rows[i].back >= 0)
				dc.FillSolidRect(r, col(rows[i].back));
			r.left += 6;
			text(rows[i].s, r, col(rows[i].fore), DT_LEFT);
		}
	}

	// 3) 오른쪽: 입력 칸 · 칩 · 강조 글자 · 버튼 · 레이블 카드
	const CRect right(list.right + gap, inner.top, inner.right, inner.bottom);
	const int rowH = h / 3;
	{
		CRect edit(right.left, right.top, right.right, right.top + rowH - 3);
		box(edit, col(TC_EDIT), col(TC_EDGE), 0);
		edit.left += 6;
		text(L"검색 (안내 글자)", edit, col(TC_HINT), DT_LEFT);
	}
	{
		const int y = right.top + rowH;
		const int chipW = dc.GetTextExtent(L"태그 칩").cx + 16;
		CRect chip(right.left, y, right.left + chipW, y + rowH - 3);
		box(chip, col(TC_LIGHT), col(TC_LIGHT), chip.Height());
		text(L"태그 칩", chip, col(TC_DARK), DT_CENTER);
		const int labW = dc.GetTextExtent(L"레이블").cx + 16;
		CRect lab(chip.right + 6, y, chip.right + 6 + labW, y + rowH - 3);
		box(lab, col(TC_LABELCARD), col(TC_LABELHOT), 6);
		text(L"레이블", lab, col(TC_TEXT), DT_CENTER);
		CRect link(lab.right + 8, y, right.right, y + rowH - 3);
		text(L"강조 글자", link, col(TC_ACCENTTEXT), DT_LEFT);
	}
	{
		const int y = right.top + rowH * 2;
		const LPCWSTR names[3] = { L"확인", L"저장", L"삭제" };
		const int roles[3] = { TC_ACCENT, TC_SAVE, TC_DANGER };
		const int bw = (right.Width() - 12) / 3;
		for (int i = 0; i < 3; ++i)
		{
			CRect b(right.left + (bw + 6) * i, y, right.left + (bw + 6) * i + bw, inner.bottom);
			box(b, col(roles[i]), col(roles[i]), 6);
			text(names[i], b, RGB(255, 255, 255), DT_CENTER);
		}
	}

	if (oldFont)
		dc.SelectObject(oldFont);
	paint.BitBlt(0, 0, client.Width(), client.Height(), &dc, 0, 0, SRCCOPY);
	dc.SelectObject(oldBmp);
}

namespace
{
	int g_lastSettingsPage = 0;   // 마지막으로 본 탭 (프로그램 실행 중에만 기억)
}

BOOL CSettingsDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	m_pluginList.SubclassDlgItem(IDC_SET_PLUGIN_LIST, this);   // 테마 적용 전에 (목록 머리글도 어두운 색으로)
	// 탭 버튼: 토글 (선택 = 강조색, 나머지 = 콤보 색) - 테마 적용 전에 연결
	const UINT tabIds[kPages] = { IDC_SET_TAB1, IDC_SET_TAB2, IDC_SET_TAB3, IDC_SET_TAB4, IDC_SET_TAB5 };
	for (int i = 0; i < kPages; ++i)
	{
		if (m_tabs[i].Attach(tabIds[i], this))
		{
			m_tabs[i].SetColors(DarkColors::Button, RGB(255, 255, 255), DarkColors::Back);
			m_tabs[i].SetToggle(DarkColors::Combo, TCOL(TC_TOGGLETEXT));
		}
	}
	m_theme.Apply(this, { IDC_SET_DEL_BTN, IDC_SET_PREVIEW_DEL });   // 삭제 버튼은 빨간색
	// 페이지 나누기: 리소스의 x 위치(400 DLU 단위)로 페이지를 정하고 첫 페이지 자리로 옮김
	{
		CRect unit(0, 0, 400, 0);
		MapDialogRect(&unit);
		const int step = (std::max)(1, static_cast<int>(unit.right));
		for (CWnd* w = GetWindow(GW_CHILD); w; w = w->GetWindow(GW_HWNDNEXT))
		{
			const UINT id = static_cast<UINT>(w->GetDlgCtrlID());
			if (id == IDOK || id == IDCANCEL || (id >= IDC_SET_TAB1 && id <= IDC_SET_TAB4) || id == IDC_SET_TAB5)
				continue;
			CRect r;
			w->GetWindowRect(&r);
			ScreenToClient(&r);
			const int page = (std::max)(0, (std::min)(kPages - 1, static_cast<int>(r.left) / step));
			if (page > 0)
				w->SetWindowPos(nullptr, r.left - page * step, r.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
			m_pageCtrls.push_back({ w->GetSafeHwnd(), page });
		}
		// 스핀 버튼은 옮긴 입력 칸에 다시 붙임
		if (CWnd* spin = GetDlgItem(IDC_SET_SHOT_COLS_SPIN))
			if (CWnd* buddy = GetDlgItem(IDC_SET_SHOT_COLS))
				spin->SendMessage(UDM_SETBUDDY, reinterpret_cast<WPARAM>(buddy->GetSafeHwnd()), 0);
	}
	UpdateCounts();
	CheckDlgButton(IDC_SET_CARD_MEDIA, m_cardMedia ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_SET_SFW, m_sfw ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_SET_MERGE_PARTS, m_mergeParts ? BST_CHECKED : BST_UNCHECKED);
	CheckRadioButton(IDC_SET_RATING_ONE, IDC_SET_RATING_TEN, m_ratingMode == 2 ? IDC_SET_RATING_TEN : (m_ratingMode == 1 ? IDC_SET_RATING_HALF : IDC_SET_RATING_ONE));
	if (CComboBox* combo = static_cast<CComboBox*>(GetDlgItem(IDC_SET_IO_KIND)))
	{
		// 텍스트 내보내기 / 가져오기 대상 (탭 · DB 삭제와 같은 순서: 영상 / 배우 / 제작사 / 태그)
		combo->AddString(L"영상");
		combo->AddString(L"배우");
		combo->AddString(L"제작사 · 레이블");
		combo->AddString(L"태그");
		combo->SetCurSel(0);
	}
	if (CComboBox* combo = static_cast<CComboBox*>(GetDlgItem(IDC_SET_CENSOR_LANG)))
	{
		combo->AddString(L"한글");
		combo->AddString(L"English");
		combo->AddString(L"日本語");
		combo->SetCurSel((m_censorLang >= 0 && m_censorLang <= 2) ? m_censorLang : 0);
	}
	CheckRadioButton(IDC_SET_DETAIL_RIGHT, IDC_SET_DETAIL_LEFT, m_detailLeft ? IDC_SET_DETAIL_LEFT : IDC_SET_DETAIL_RIGHT);
	CheckRadioButton(IDC_SET_STRIP_ACTORS, IDC_SET_STRIP_SHOTS, m_detailStrip == 1 ? IDC_SET_STRIP_SHOTS : IDC_SET_STRIP_ACTORS);
	SetDlgItemInt(IDC_SET_SHOT_COLS, static_cast<UINT>(m_shotCols), FALSE);
	if (CEdit* e = static_cast<CEdit*>(GetDlgItem(IDC_SET_SHOT_COLS)))
		e->SetLimitText(2);
	if (CWnd* spin = GetDlgItem(IDC_SET_SHOT_COLS_SPIN))
	{
		// 가로 배치 수 스핀 버튼 (▲▼ · 위아래 화살표 키로 1 ~ 10)
		spin->SendMessage(UDM_SETRANGE32, 1, 10);
		spin->SendMessage(UDM_SETPOS32, 0, m_shotCols);
		::SetWindowTheme(spin->GetSafeHwnd(), L"DarkMode_Explorer", nullptr);   // 어두운 테마 (지원하는 Windows 만)
	}
	if (CComboBox* combo = static_cast<CComboBox*>(GetDlgItem(IDC_SET_THEME)))
	{
		for (int i = 0; i < Theme::Count(); ++i)
			combo->AddString(Theme::Name(i));
		combo->SetCurSel((m_themeIndex >= 0 && m_themeIndex < Theme::Count()) ? m_themeIndex : 0);
	}
	// 테마 미리보기: [테마] 페이지(첫 페이지 자리 기준 15, 70 DLU, 290 x 48) - "미리보기" 글자 아래
	{
		CRect r(15, 70, 15 + 290, 70 + 48);
		MapDialogRect(&r);
		if (m_themePreview.Create(this, r, IDC_SET_THEME_PREVIEW))
		{
			m_themePreview.SetTheme((m_themeIndex >= 0 && m_themeIndex < Theme::Count()) ? m_themeIndex : 0);
			m_pageCtrls.push_back({ m_themePreview.GetSafeHwnd(), 2 });   // [테마] 탭에서만 보임
		}
	}

	{
		// [플러그인] 목록: 이름 · 대상 · 버전 · 상태, 체크 = 사용
		m_pluginList.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP | LVS_EX_CHECKBOXES);
		CRect lr;
		m_pluginList.GetClientRect(&lr);
		const int w = (std::max)(200, static_cast<int>(lr.Width()) - ::GetSystemMetrics(SM_CXVSCROLL));
		m_pluginList.InsertColumn(0, L"이름", LVCFMT_LEFT, w * 46 / 100);
		m_pluginList.InsertColumn(1, L"대상", LVCFMT_LEFT, w * 18 / 100);
		m_pluginList.InsertColumn(2, L"버전", LVCFMT_LEFT, w * 14 / 100);
		m_pluginList.InsertColumn(3, L"상태", LVCFMT_LEFT, w * 22 / 100);
		m_plugins = PluginHost::LoadAll();
		FillPlugins();
	}

	ShowPage(m_startPage >= 0 ? m_startPage : g_lastSettingsPage);
	UpdatePreviewButtons();
	SetTimer(1, 300, nullptr);   // [만들기 중단] 활성 상태 (미리보기를 만드는 중일 때만)
	return TRUE;
}

void CSettingsDlg::UpdatePreviewButtons()
{
	const bool busy = m_isPreviewBusy && m_isPreviewBusy();
	if (CWnd* w = GetDlgItem(IDC_SET_PREVIEW_STOP))
		if ((w->IsWindowEnabled() != FALSE) != busy)
			w->EnableWindow(busy);
}

void CSettingsDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 1)
	{
		UpdatePreviewButtons();
		return;
	}
	CDialogEx::OnTimer(nIDEvent);
}

void CSettingsDlg::ShowPage(int page)
{
	m_page = (std::max)(0, (std::min)(kPages - 1, page));
	g_lastSettingsPage = m_page;
	for (int i = 0; i < kPages; ++i)
		if (m_tabs[i].GetSafeHwnd())
			m_tabs[i].SetChecked(i == m_page);
	for (const auto& pc : m_pageCtrls)
		::ShowWindow(pc.first, pc.second == m_page ? SW_SHOW : SW_HIDE);
	Invalidate();   // 그룹 상자 자리의 배경도 다시
}

void CSettingsDlg::PageOfCtrl(UINT id)
{
	const HWND h = GetDlgItem(id) ? GetDlgItem(id)->GetSafeHwnd() : nullptr;
	for (const auto& pc : m_pageCtrls)
		if (pc.first == h)
		{
			ShowPage(pc.second);
			return;
		}
}

void CSettingsDlg::UpdateCounts()
{
	int saved = 0, pending = 0;
	for (const VideoItem& v : m_lib.items)
		(v.pending ? pending : saved)++;
	CString t;
	t.Format(L"영상 (%d", saved);
	if (pending > 0)
	{
		CString p;
		p.Format(L" + 임시 %d", pending);
		t += p;
	}
	t += L")";
	SetDlgItemText(IDC_SET_DEL_VIDEO, t);
	t.Format(L"배우 (%d)", static_cast<int>(m_lib.actors.size()));
	SetDlgItemText(IDC_SET_DEL_ACTOR, t);
	t.Format(L"제작사 (%d)", static_cast<int>(m_lib.studios.size()));
	SetDlgItemText(IDC_SET_DEL_STUDIO, t);
	t.Format(L"태그 (%d)", static_cast<int>(m_lib.tagInfos.size()));
	SetDlgItemText(IDC_SET_DEL_TAG, t);
	OnCheckChanged();
}

void CSettingsDlg::OnCheckChanged()
{
	const bool any = IsDlgButtonChecked(IDC_SET_DEL_VIDEO) || IsDlgButtonChecked(IDC_SET_DEL_ACTOR) ||
		IsDlgButtonChecked(IDC_SET_DEL_STUDIO) || IsDlgButtonChecked(IDC_SET_DEL_TAG);
	GetDlgItem(IDC_SET_DEL_BTN)->EnableWindow(any);
}

int CSettingsDlg::SelectedIoKind()
{
	CComboBox* combo = static_cast<CComboBox*>(GetDlgItem(IDC_SET_IO_KIND));
	const int sel = combo ? combo->GetCurSel() : 0;
	// 콤보 순서 → CVideoLibrary::EXPORT_* 번호
	static const int kinds[4] = { CVideoLibrary::EXPORT_VIDEOS, CVideoLibrary::EXPORT_ACTORS, CVideoLibrary::EXPORT_STUDIOS, CVideoLibrary::EXPORT_TAGS };
	return (sel >= 0 && sel <= 3) ? kinds[sel] : CVideoLibrary::EXPORT_VIDEOS;
}

void CSettingsDlg::OnBnClickedExportList()
{
	if (m_onExportList)
		m_onExportList(SelectedIoKind());
}

void CSettingsDlg::OnBnClickedImportList()
{
	if (m_onImportList)
		m_onImportList(SelectedIoKind());
	UpdateCounts();   // DB 삭제 칸의 개수도 다시
}

void CSettingsDlg::OnBnClickedDeleteDb()
{
	int mask = 0;
	CString names;
	auto add = [&](UINT id, int bit, LPCWSTR name)
	{
		if (IsDlgButtonChecked(id))
		{
			mask |= bit;
			if (!names.IsEmpty()) names += L", ";
			names += name;
		}
	};
	add(IDC_SET_DEL_VIDEO, DB_VIDEO, L"영상");
	add(IDC_SET_DEL_ACTOR, DB_ACTOR, L"배우");
	add(IDC_SET_DEL_STUDIO, DB_STUDIO, L"제작사");
	add(IDC_SET_DEL_TAG, DB_TAG, L"태그");
	if (mask == 0)
		return;

	CString msg;
	msg.Format(L"다음 DB를 모두 삭제할까요?\n\n    %s\n\n"
		L"- 영상: 저장한 정보와 임시 항목을 모두 지움 (다시 스캔하면 임시 항목으로 다시 나타남)\n"
		L"- 배우 / 제작사 / 태그: 목록과 영상에 지정된 값을 함께 지움\n"
		L"- 영상 파일과 이미지 원본은 지우지 않습니다.\n\n되돌릴 수 없습니다.",
		static_cast<LPCWSTR>(names));
	if (AfxMessageBox(msg, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
		return;

	if (m_onDeleteDb)
		m_onDeleteDb(mask);

	CheckDlgButton(IDC_SET_DEL_VIDEO, BST_UNCHECKED);
	CheckDlgButton(IDC_SET_DEL_ACTOR, BST_UNCHECKED);
	CheckDlgButton(IDC_SET_DEL_STUDIO, BST_UNCHECKED);
	CheckDlgButton(IDC_SET_DEL_TAG, BST_UNCHECKED);
	UpdateCounts();
	AfxMessageBox(L"삭제했습니다.", MB_ICONINFORMATION);
}

void CSettingsDlg::OnOK()
{
	m_cardMedia = IsDlgButtonChecked(IDC_SET_CARD_MEDIA) == BST_CHECKED;
	m_sfw = IsDlgButtonChecked(IDC_SET_SFW) == BST_CHECKED;
	m_mergeParts = IsDlgButtonChecked(IDC_SET_MERGE_PARTS) == BST_CHECKED;
	m_ratingMode = IsDlgButtonChecked(IDC_SET_RATING_TEN) == BST_CHECKED ? 2 : (IsDlgButtonChecked(IDC_SET_RATING_HALF) == BST_CHECKED ? 1 : 0);
	if (CComboBox* combo = static_cast<CComboBox*>(GetDlgItem(IDC_SET_CENSOR_LANG)))
		if (combo->GetCurSel() >= 0)
			m_censorLang = combo->GetCurSel();
	m_detailLeft = IsDlgButtonChecked(IDC_SET_DETAIL_LEFT) == BST_CHECKED;
	m_detailStrip = (IsDlgButtonChecked(IDC_SET_STRIP_SHOTS) == BST_CHECKED) ? 1 : 0;
	{
		BOOL ok = FALSE;
		const int cols = static_cast<int>(GetDlgItemInt(IDC_SET_SHOT_COLS, &ok, FALSE));
		if (!ok || cols < 1 || cols > 10)
		{
			AfxMessageBox(L"스크린샷 가로 배치 수는 1 ~ 10 사이로 입력하세요.", MB_ICONWARNING);
			PageOfCtrl(IDC_SET_SHOT_COLS);   // 그 칸이 있는 탭으로
			GetDlgItem(IDC_SET_SHOT_COLS)->SetFocus();
			return;
		}
		m_shotCols = cols;
	}
	if (CComboBox* combo = static_cast<CComboBox*>(GetDlgItem(IDC_SET_THEME)))
		if (combo->GetCurSel() >= 0)
			m_themeIndex = combo->GetCurSel();
	{
		// [플러그인] 체크 해제한 것 → 레지스트리 (지금 목록에 없는 폴더의 예전 기록은 그대로 둠)
		std::set<CString> present;
		for (const PluginInfo& p : m_plugins)
		{
			CString k = p.id;
			k.MakeLower();
			present.insert(k);
		}
		std::vector<CString> disabled;
		for (const CString& id : PluginHost::LoadDisabled())
		{
			CString k = id;
			k.MakeLower();
			if (present.count(k) == 0)
				disabled.push_back(id);
		}
		for (int i = 0; i < m_pluginList.GetItemCount() && i < static_cast<int>(m_plugins.size()); ++i)
			if (!m_pluginList.GetCheck(i))
				disabled.push_back(m_plugins[i].id);
		PluginHost::SaveDisabled(disabled);
	}
	CDialogEx::OnOK();
}

void CSettingsDlg::FillPlugins()
{
	m_pluginFilling = true;
	m_pluginList.SetRedraw(FALSE);
	m_pluginList.DeleteAllItems();
	for (size_t i = 0; i < m_plugins.size(); ++i)
	{
		const PluginInfo& p = m_plugins[i];
		const int row = m_pluginList.InsertItem(static_cast<int>(i), p.name);
		m_pluginList.SetItemText(row, 1, p.TargetName());
		m_pluginList.SetItemText(row, 2, p.version);
		m_pluginList.SetItemText(row, 3, !p.error.IsEmpty() ? CString(L"오류") : (p.enabled ? CString(L"사용") : CString(L"꺼짐")));
		m_pluginList.SetCheck(row, p.enabled ? TRUE : FALSE);
	}
	if (!m_plugins.empty())
		m_pluginList.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	m_pluginList.SetRedraw(TRUE);
	m_pluginList.Invalidate();
	m_pluginFilling = false;
	UpdatePluginDesc();
}

void CSettingsDlg::UpdatePluginDesc()
{
	CString t;
	const int row = m_pluginList.GetNextItem(-1, LVNI_SELECTED);
	if (m_plugins.empty())
		t = L"플러그인이 없습니다. [폴더 열기] 로 plugins 폴더를 열어 플러그인 폴더를 넣고 [새로 고침] 하세요.";
	else if (row >= 0 && row < static_cast<int>(m_plugins.size()))
	{
		const PluginInfo& p = m_plugins[row];
		if (!p.error.IsEmpty())
			t = L"오류: " + p.error + L"  (" + p.id + L"\\plugin.json)";
		else
		{
			t = p.description.IsEmpty() ? CString(L"(설명 없음)") : p.description;
			if (!p.author.IsEmpty())
				t += L"  - " + p.author;
			t += L"  [메뉴: " + p.menu + L"]";
		}
	}
	SetDlgItemText(IDC_SET_PLUGIN_DESC, t);
}

void CSettingsDlg::OnPluginItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	if (m_pluginFilling)
		return;
	const NMLISTVIEW* nm = reinterpret_cast<const NMLISTVIEW*>(pNMHDR);
	if (nm->uChanged & LVIF_STATE)
	{
		const int row = nm->iItem;
		if ((nm->uNewState ^ nm->uOldState) & LVIS_STATEIMAGEMASK)
		{
			// 체크 바꿈: 상태 칸만 바로 갱신 (저장은 확인을 누를 때)
			if (row >= 0 && row < static_cast<int>(m_plugins.size()) && m_plugins[row].error.IsEmpty())
				m_pluginList.SetItemText(row, 3, m_pluginList.GetCheck(row) ? L"사용" : L"꺼짐");
		}
		if ((nm->uNewState ^ nm->uOldState) & LVIS_SELECTED)
			UpdatePluginDesc();
	}
}

void CSettingsDlg::OnBnClickedPluginFolder()
{
	const CString dir = PluginHost::PluginsDir(true);
	::ShellExecuteW(GetSafeHwnd(), L"open", dir, nullptr, nullptr, SW_SHOWNORMAL);
}

void CSettingsDlg::OnBnClickedPluginReload()
{
	// 지금 체크 상태는 유지 (같은 폴더 이름), 새로 생긴 플러그인은 기록대로
	std::map<CString, bool> checks;
	for (int i = 0; i < m_pluginList.GetItemCount() && i < static_cast<int>(m_plugins.size()); ++i)
	{
		CString k = m_plugins[i].id;
		k.MakeLower();
		checks[k] = m_pluginList.GetCheck(i) != FALSE;
	}
	m_plugins = PluginHost::LoadAll();
	for (PluginInfo& p : m_plugins)
	{
		CString k = p.id;
		k.MakeLower();
		const auto it = checks.find(k);
		if (it != checks.end())
			p.enabled = it->second;
	}
	FillPlugins();
}

void CSettingsDlg::OnBnClickedPluginHelp()
{
	// plugins\README.md (플러그인 만드는 법 - 요청 · 결과 JSON 형식, 예제)
	const CString file = PluginHost::PluginsDir() + L"\\README.md";
	if (::PathFileExistsW(file))
		::ShellExecuteW(GetSafeHwnd(), L"open", file, nullptr, nullptr, SW_SHOWNORMAL);
	else
		AfxMessageBox(L"plugins 폴더에 README.md 가 없습니다.\n\n" + file, MB_ICONINFORMATION);
}

void CSettingsDlg::OnThemeChanged()
{
	if (CComboBox* combo = static_cast<CComboBox*>(GetDlgItem(IDC_SET_THEME)))
		if (combo->GetCurSel() >= 0)
			m_themePreview.SetTheme(combo->GetCurSel());
}

HBRUSH CSettingsDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
		return br;
	return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}
