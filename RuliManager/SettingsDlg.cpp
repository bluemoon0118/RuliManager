#include "pch.h"
#include "RuliManager.h"
#include "SettingsDlg.h"

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
	CDialogEx::OnOK();
}

HBRUSH CSettingsDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
		return br;
	return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}
