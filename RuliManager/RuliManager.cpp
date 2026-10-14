#include "pch.h"
#include "RuliManager.h"
#include "RuliManagerDlg.h"
#include "Version.h"

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

	// 프로그램은 하나만 실행: 이미 실행 중이면 그 창을 앞으로 가져오고 끝냄
	//  (같은 DB · 이미지 캐시를 두 프로그램이 동시에 쓰면 저장 내용이 꼬일 수 있음)
	HANDLE singleMutex = ::CreateMutexW(nullptr, TRUE, L"Local\\RuliManager.SingleInstance.{7C3E9A51-2B64-4F0D-9E21-5A8C7B13D0F4}");
	if (singleMutex && ::GetLastError() == ERROR_ALREADY_EXISTS)
	{
		struct Finder { HWND found = nullptr; } finder;
		::EnumWindows([](HWND h, LPARAM lp) -> BOOL
		{
			wchar_t title[128] = {};
			::GetWindowTextW(h, title, 127);
			const CString prefix = CString(_T(APP_NAME_STR)) + L" v";   // 창 제목 "RuliManager v0.2"
			if (_wcsnicmp(title, prefix, prefix.GetLength()) == 0 && ::IsWindowVisible(h))
			{
				reinterpret_cast<Finder*>(lp)->found = h;
				return FALSE;
			}
			return TRUE;
		}, reinterpret_cast<LPARAM>(&finder));
		if (finder.found)
		{
			if (::IsIconic(finder.found))
				::ShowWindow(finder.found, SW_RESTORE);
			::SetForegroundWindow(finder.found);
		}
		else
		{
			AfxMessageBox(_T("RuliManager 가 이미 실행 중입니다."), MB_ICONINFORMATION);
		}
		::CloseHandle(singleMutex);
		return FALSE;
	}

	// 셸 기능(탐색기에서 보기 등)을 위한 OLE/COM 초기화
	if (!AfxOleInit())
	{
		AfxMessageBox(_T("OLE 초기화에 실패했습니다."));
		return FALSE;
	}

	SetRegistryKey(_T("VideoManager"));   // 예전 이름 그대로 (기존 설정 유지)
	Theme::LoadFromSettings();   // 테마 색상 (창을 만들기 전에 한 번 정함, 기본 "Stash")

	CRuliManagerDlg dlg;
	m_pMainWnd = &dlg;
	dlg.DoModal();
	if (singleMutex)
		::CloseHandle(singleMutex);   // 다음 실행 허용

	if (m_restart)
	{
		// 설정 창에서 테마를 바꾸고 [다시 시작]: 같은 실행 파일을 새로 띄움 (하나만 실행 잠금을 푼 뒤)
		wchar_t exe[MAX_PATH] = {};
		if (::GetModuleFileNameW(nullptr, exe, MAX_PATH))
			::ShellExecuteW(nullptr, L"open", exe, nullptr, nullptr, SW_SHOWNORMAL);
	}

	// 대화 상자가 닫혔으므로 애플리케이션을 종료합니다.
	return FALSE;
}
