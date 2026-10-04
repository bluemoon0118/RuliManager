#include "pch.h"
#include "RuliManager.h"
#include "BulkNameDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CBulkNameDlg::CBulkNameDlg(CVideoLibrary& lib, int kind, CWnd* pParent)
	: CDialogEx(IDD_BULK_NAMES, pParent), m_lib(lib), m_kind(kind)
{
}

void CBulkNameDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_BN_TEXT, m_editText);
	DDX_Control(pDX, IDC_BN_LIST, m_list);
	DDX_Control(pDX, IDC_BN_SUMMARY, m_staticSummary);
}

BEGIN_MESSAGE_MAP(CBulkNameDlg, CDialogEx)
	ON_WM_CTLCOLOR()
	ON_EN_CHANGE(IDC_BN_TEXT, &CBulkNameDlg::OnEnChangeText)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_BN_LIST, &CBulkNameDlg::OnNmCustomDrawList)
END_MESSAGE_MAP()

BOOL CBulkNameDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	m_theme.Apply(this);   // 메인 창과 같은 색상 ("등록" 버튼은 초록색)

	SetWindowText(KindName() + L" 일괄 등록");
	SetDlgItemText(IDC_BN_GUIDE, L"한 줄에 하나씩, 또는 쉼표(,)로 구분해 " + KindName() + L" 이름을 입력하세요. 괄호 안 쉼표는 이름의 일부로 봅니다.");

	CRect rc;
	m_list.GetClientRect(&rc);
	const int statusW = rc.Width() * 30 / 100;
	m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
	m_list.InsertColumn(0, L"이름", LVCFMT_LEFT, rc.Width() - statusW - ::GetSystemMetrics(SM_CXVSCROLL));
	m_list.InsertColumn(1, L"상태", LVCFMT_LEFT, statusW);

	m_editText.SetLimitText(0);   // 길이 제한 없음
	UpdatePreview();
	m_editText.SetFocus();
	return FALSE;
}

void CBulkNameDlg::Parse()
{
	m_entries.clear();

	CString text;
	m_editText.GetWindowText(text);
	text.Replace(L"\r\n", L"\n");
	text.Replace(L'\r', L'\n');
	text.Replace(L'\t', L',');   // 엑셀에서 붙여넣은 탭도 구분자로

	std::set<CString> seen;       // 소문자 이름 (입력 안 중복 검사)
	int pos = 0;
	while (pos <= text.GetLength())
	{
		int nl = text.Find(L'\n', pos);
		if (nl < 0)
			nl = text.GetLength();
		const CString line = text.Mid(pos, nl - pos);
		pos = nl + 1;

		for (CString name : CVideoLibrary::SplitList(line))
		{
			name.Trim();
			if (name.IsEmpty())
				continue;
			Entry e;
			e.name = name;
			CString key = name;
			key.MakeLower();
			if (!seen.insert(key).second)
				e.state = ST_DUP;
			else if (m_lib.FindNamed(m_kind, name) >= 0)
				e.state = ST_EXISTS;
			m_entries.push_back(e);
		}
	}
}

void CBulkNameDlg::UpdatePreview()
{
	Parse();

	static const wchar_t* stateText[] = { L"신규", L"이미 있음", L"입력 중복" };
	int counts[3] = {};

	m_list.SetRedraw(FALSE);
	m_list.DeleteAllItems();
	for (size_t i = 0; i < m_entries.size(); ++i)
	{
		const Entry& e = m_entries[i];
		const int row = m_list.InsertItem(static_cast<int>(i), e.name);
		m_list.SetItemText(row, 1, stateText[e.state]);
		m_list.SetItemData(row, static_cast<DWORD_PTR>(e.state));
		++counts[e.state];
	}
	m_list.SetRedraw(TRUE);
	m_list.Invalidate();

	CString s;
	if (m_entries.empty())
		s = L"입력된 이름이 없습니다.";
	else
		s.Format(L"신규 %d개  ·  이미 있음 %d개  ·  입력 중복 %d개", counts[ST_NEW], counts[ST_EXISTS], counts[ST_DUP]);
	m_staticSummary.SetWindowText(s);

	CString okText;
	if (counts[ST_NEW] > 0)
		okText.Format(L"등록 (%d)", counts[ST_NEW]);
	else
		okText = L"등록";
	GetDlgItem(IDOK)->SetWindowText(okText);
	GetDlgItem(IDOK)->EnableWindow(counts[ST_NEW] > 0);
}

void CBulkNameDlg::OnEnChangeText()
{
	UpdatePreview();
}

void CBulkNameDlg::OnNmCustomDrawList(NMHDR* pNMHDR, LRESULT* pResult)
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
	{
		const int state = static_cast<int>(cd->nmcd.lItemlParam);
		if (cd->iSubItem == 1)
		{
			// 상태 글자색: 신규 = 초록, 이미 있음 = 회색, 입력 중복 = 주황
			cd->clrText = (state == ST_NEW) ? RGB(0x3D, 0xCC, 0x91)
				: (state == ST_EXISTS) ? RGB(0x8A, 0x9B, 0xA8) : RGB(0xFF, 0xB3, 0x66);
		}
		else
		{
			cd->clrText = (state == ST_NEW) ? RGB(0xF5, 0xF8, 0xFA) : RGB(0x8A, 0x9B, 0xA8);
		}
		*pResult = CDRF_DODEFAULT;
		break;
	}
	}
}

void CBulkNameDlg::OnOK()
{
	UpdatePreview();
	const bool favorite = (IsDlgButtonChecked(IDC_BN_FAV) == BST_CHECKED);

	m_added.clear();
	for (const Entry& e : m_entries)
	{
		if (e.state != ST_NEW || m_lib.FindNamed(m_kind, e.name) >= 0)
			continue;
		NamedInfo info;
		info.name = e.name;
		info.favorite = favorite;
		m_lib.NamedList(m_kind).push_back(info);
		m_added.push_back(e.name);
	}
	if (m_added.empty())
	{
		AfxMessageBox(L"새로 등록할 이름이 없습니다.", MB_ICONINFORMATION);
		return;
	}
	CDialogEx::OnOK();
}

HBRUSH CBulkNameDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
		return br;
	return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}
