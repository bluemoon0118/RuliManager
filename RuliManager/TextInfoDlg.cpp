#include "pch.h"
#include "RuliManager.h"
#include "TextInfoDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CTextInfoDlg::CTextInfoDlg(const CString& title, const CString& guide, const CString& text, CWnd* pParent)
	: CDialogEx(IDD_TEXT_INFO, pParent), m_text(text), m_title(title), m_guide(guide)
{
}

void CTextInfoDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_TI_TEXT, m_editText);
}

BEGIN_MESSAGE_MAP(CTextInfoDlg, CDialogEx)
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

BOOL CTextInfoDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	m_theme.Apply(this);   // 메인 창과 같은 색상 ("적용" 버튼은 초록색)

	SetWindowText(m_title);
	SetDlgItemText(IDC_TI_GUIDE, m_guide);

	m_editText.SetLimitText(0);   // 길이 제한 없음
	CString t = m_text;
	t.Replace(L"\r\n", L"\n");
	t.Replace(L"\n", L"\r\n");    // 여러 줄 에디트는 CRLF
	m_editText.SetWindowText(t);
	m_editText.SetFocus();
	m_editText.SetSel(t.GetLength(), t.GetLength());   // 커서는 끝에
	return FALSE;
}

void CTextInfoDlg::OnOK()
{
	m_editText.GetWindowText(m_text);
	CDialogEx::OnOK();
}

HBRUSH CTextInfoDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
		return br;
	return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}
