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
	ON_BN_CLICKED(IDC_TI_PASTE_COMPACT, &CTextInfoDlg::OnPasteCompact)
END_MESSAGE_MAP()

BOOL CTextInfoDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	m_theme.Apply(this);   // 메인 창과 같은 색상 ("적용" 버튼은 초록색)

	SetWindowText(m_title);
	SetDlgItemText(IDC_TI_GUIDE, m_guide);
	if (!m_pasteButton)
		GetDlgItem(IDC_TI_PASTE_COMPACT)->ShowWindow(SW_HIDE);   // [공백라인 제거] (아래 왼쪽)는 웹페이지 붙여넣기 창에서만

	m_editText.SetLimitText(0);   // 길이 제한 없음
	CString t = m_text;
	t.Replace(L"\r\n", L"\n");
	t.Replace(L"\n", L"\r\n");    // 여러 줄 에디트는 CRLF
	m_editText.SetWindowText(t);
	m_editText.SetFocus();
	m_editText.SetSel(t.GetLength(), t.GetLength());   // 커서는 끝에
	return FALSE;
}

void CTextInfoDlg::OnPasteCompact()
{
	// [공백라인 제거]: 지금 입력 칸의 글자에서 빈 줄 · 공백만 있는 줄을 지움 (각 줄 끝 공백도 정리)
	CString text;
	m_editText.GetWindowText(text);
	text.Replace(L"\r\n", L"\n");
	text.Replace(L'\r', L'\n');
	CString out;
	int pos = 0;
	while (pos <= text.GetLength())
	{
		int nl = text.Find(L'\n', pos);
		if (nl < 0) nl = text.GetLength();
		CString line = text.Mid(pos, nl - pos);
		pos = nl + 1;
		CString test = line;
		test.Replace(L'\x00A0', L' ');   // 웹페이지의 줄바꿈 없는 공백
		test.Replace(L'\x3000', L' ');   // 전각 공백
		test.Trim();
		if (test.IsEmpty())
			continue;   // 빈 줄은 뺌
		line.TrimRight();
		if (!out.IsEmpty())
			out += L"\r\n";
		out += line;
	}
	m_editText.SetWindowText(out);
	m_editText.SetFocus();
	m_editText.SetSel(out.GetLength(), out.GetLength());   // 커서는 끝에
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
