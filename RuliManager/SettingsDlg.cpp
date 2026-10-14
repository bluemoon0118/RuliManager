#include "pch.h"
#include "RuliManager.h"
#include "SettingsDlg.h"
#include <uxtheme.h>
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
	ON_BN_CLICKED(IDC_SET_DEL_VIDEO, &CSettingsDlg::OnCheckChanged)
	ON_BN_CLICKED(IDC_SET_DEL_ACTOR, &CSettingsDlg::OnCheckChanged)
	ON_BN_CLICKED(IDC_SET_DEL_STUDIO, &CSettingsDlg::OnCheckChanged)
	ON_BN_CLICKED(IDC_SET_DEL_TAG, &CSettingsDlg::OnCheckChanged)
END_MESSAGE_MAP()

BOOL CSettingsDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	m_theme.Apply(this, { IDC_SET_DEL_BTN });   // 삭제 버튼은 빨간색
	UpdateCounts();
	CheckDlgButton(IDC_SET_CARD_MEDIA, m_cardMedia ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_SET_SFW, m_sfw ? BST_CHECKED : BST_UNCHECKED);
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

	return TRUE;
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
	m_detailStrip = (IsDlgButtonChecked(IDC_SET_STRIP_SHOTS) == BST_CHECKED) ? 1 : 0;
	{
		BOOL ok = FALSE;
		const int cols = static_cast<int>(GetDlgItemInt(IDC_SET_SHOT_COLS, &ok, FALSE));
		if (!ok || cols < 1 || cols > 10)
		{
			AfxMessageBox(L"스크린샷 가로 배치 수는 1 ~ 10 사이로 입력하세요.", MB_ICONWARNING);
			GetDlgItem(IDC_SET_SHOT_COLS)->SetFocus();
			return;
		}
		m_shotCols = cols;
	}
	if (CComboBox* combo = static_cast<CComboBox*>(GetDlgItem(IDC_SET_THEME)))
		if (combo->GetCurSel() >= 0)
			m_themeIndex = combo->GetCurSel();
	CDialogEx::OnOK();
}

HBRUSH CSettingsDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
		return br;
	return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}
