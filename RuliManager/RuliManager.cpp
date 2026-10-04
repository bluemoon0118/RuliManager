#include "pch.h"
#include "RuliManager.h"
#include "RuliManagerDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CRuliManagerApp, CWinApp)
END_MESSAGE_MAP()

CRuliManagerApp::CRuliManagerApp()
{
}

CRuliManagerApp theApp;

BOOL CRuliManagerApp::InitInstance()
{
	INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES };
	InitCommonControlsEx(&icc);

	CWinApp::InitInstance();

	// 셸 기능(탐색기에서 보기 등)을 위한 OLE/COM 초기화
	if (!AfxOleInit())
	{
		AfxMessageBox(_T("OLE 초기화에 실패했습니다."));
		return FALSE;
	}

	SetRegistryKey(_T("VideoManager"));   // 예전 이름 그대로 (기존 설정 유지)

	CRuliManagerDlg dlg;
	m_pMainWnd = &dlg;
	dlg.DoModal();

	// 대화 상자가 닫혔으므로 애플리케이션을 종료합니다.
	return FALSE;
}
