#pragma once

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif

#include "targetver.h"

#define _AFX_ALL_WARNINGS

#include <afxwin.h>         // MFC 핵심 및 표준 구성 요소
#include <afxext.h>         // MFC 확장
#include <afxdisp.h>        // MFC 자동화 / ActiveX 컨테이너
#include <afxcmn.h>         // 공용 컨트롤
#include <afxdtctl.h>       // 날짜/시간 선택 컨트롤 (CDateTimeCtrl)
#include <afxdialogex.h>

#include <atlbase.h>        // CComPtr, CComBSTR
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <atlimage.h>       // CImage (JPG/PNG/BMP/GIF 불러오기)

#include <vector>
#include <map>
#include <set>
#include <algorithm>

#pragma comment(lib, "shlwapi.lib")

#ifdef _UNICODE
#if defined _M_IX86
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_ARM64
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='arm64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif
#endif
