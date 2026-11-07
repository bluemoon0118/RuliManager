#include "pch.h"
#include "RuliManager.h"
#include "PluginRunDlg.h"
#include "VectorIcons.h"
#include <uxtheme.h>
#include <algorithm>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	const UINT WM_PLUGIN_EVENT = WM_APP + 0x61;   // lParam = CPluginRun::Event* (받는 쪽이 delete)
	const UINT WM_PLUGIN_FRONT = WM_APP + 0x62;   // wParam = 맨 앞으로 올릴 창 (결과 처리 중 뜬 대화 상자)

	// 결과 처리 중(DrainQueue) 이 스레드에서 활성화되는 대화 상자(배우 · 제작사 · 태그 연결 창, 채울 항목 고르기, 메시지 상자)를 알림
	HHOOK g_frontHook = nullptr;
	HWND  g_frontNotify = nullptr;
	LRESULT CALLBACK FrontCbtProc(int code, WPARAM wParam, LPARAM lParam)
	{
		if (code == HCBT_ACTIVATE && g_frontNotify)
		{
			const HWND h = reinterpret_cast<HWND>(wParam);
			wchar_t cls[16] = {};
			if (h != g_frontNotify && ::GetClassNameW(h, cls, _countof(cls)) && wcscmp(cls, L"#32770") == 0)
				::PostMessageW(g_frontNotify, WM_PLUGIN_FRONT, reinterpret_cast<WPARAM>(h), 0);
		}
		return ::CallNextHookEx(g_frontHook, code, wParam, lParam);
	}

	// 다른 프로그램(셀레늄 크롬 등)이 앞에 있어도 창을 맨 앞 + 활성으로 (입력 큐를 잠시 붙여서 포그라운드 제한을 넘김)
	void ForceFront(HWND h)
	{
		if (!::IsWindow(h) || !::IsWindowVisible(h))
			return;
		const HWND fg = ::GetForegroundWindow();
		const DWORD fgThread = fg ? ::GetWindowThreadProcessId(fg, nullptr) : 0;
		const DWORD myThread = ::GetCurrentThreadId();
		const bool attach = fgThread && fgThread != myThread && ::AttachThreadInput(myThread, fgThread, TRUE);
		::SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
		::BringWindowToTop(h);
		::SetForegroundWindow(h);
		if (attach)
			::AttachThreadInput(myThread, fgThread, FALSE);
	}

	// 창 아이콘: 강조색 바탕 위에 흰 퍼즐 조각 (VectorIcon::Plugin)
	HICON MakePluginIcon(int size)
	{
		if (size <= 0)
			return nullptr;
		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
		bi.bmiHeader.biWidth = size;
		bi.bmiHeader.biHeight = -size;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		void* bits = nullptr;
		HBITMAP color = ::CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (!color)
			return nullptr;
		{
			CDC mem;
			mem.CreateCompatibleDC(nullptr);
			HGDIOBJ old = ::SelectObject(mem.GetSafeHdc(), color);
			const CRect rc(0, 0, size, size);
			mem.FillSolidRect(rc, TCOL(TC_ACCENT));
			VectorIcon::Plugin(&mem, rc, RGB(255, 255, 255), 0.86);
			::GdiFlush();
			::SelectObject(mem.GetSafeHdc(), old);
		}
		DWORD* px = static_cast<DWORD*>(bits);
		for (int i = 0; i < size * size; ++i)
			px[i] |= 0xFF000000;   // 불투명
		HBITMAP mask = ::CreateBitmap(size, size, 1, 1, nullptr);
		ICONINFO ii = {};
		ii.fIcon = TRUE;
		ii.hbmColor = color;
		ii.hbmMask = mask;
		HICON icon = ::CreateIconIndirect(&ii);
		::DeleteObject(color);
		::DeleteObject(mask);
		return icon;
	}
}

CPluginRunDlg::CPluginRunDlg(const PluginInfo& plugin, const CString& requestJson, CWnd* pParent)
	: CDialogEx(IDD_PLUGIN_RUN, pParent), m_plugin(plugin), m_request(requestJson)
{
}

CPluginRunDlg::~CPluginRunDlg()
{
	if (m_iconSmall) ::DestroyIcon(m_iconSmall);
	if (m_iconBig) ::DestroyIcon(m_iconBig);
}

BEGIN_MESSAGE_MAP(CPluginRunDlg, CDialogEx)
	ON_MESSAGE(WM_PLUGIN_EVENT, &CPluginRunDlg::OnRunEvent)
	ON_MESSAGE(WM_PLUGIN_FRONT, &CPluginRunDlg::OnBringFront)
	ON_BN_CLICKED(IDC_PLG_STOP, &CPluginRunDlg::OnBnClickedStop)
	ON_WM_TIMER()
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

BOOL CPluginRunDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	m_theme.Apply(this, { IDC_PLG_STOP });   // 중단 = 빨간 버튼
	m_iconSmall = MakePluginIcon(::GetSystemMetrics(SM_CXSMICON));
	m_iconBig = MakePluginIcon(::GetSystemMetrics(SM_CXICON));
	if (m_iconSmall) SetIcon(m_iconSmall, FALSE);
	if (m_iconBig) SetIcon(m_iconBig, TRUE);
	SetWindowText(L"플러그인 실행 - " + m_plugin.name);

	if (CWnd* bar = GetDlgItem(IDC_PLG_PROGRESS))
	{
		::SetWindowTheme(bar->GetSafeHwnd(), L"", L"");   // 테마를 끄면 막대 색을 정할 수 있음
		bar->SendMessage(PBM_SETBARCOLOR, 0, static_cast<LPARAM>(TCOL(TC_ACCENT)));
		bar->SendMessage(PBM_SETBKCOLOR, 0, static_cast<LPARAM>(TCOL(TC_EDIT)));
		bar->SendMessage(PBM_SETRANGE32, 0, 100);
	}
	if (CEdit* log = static_cast<CEdit*>(GetDlgItem(IDC_PLG_LOG)))
		log->SetLimitText(0);
	SetMarquee(true);
	SetDlgItemText(IDC_PLG_MSG, L"실행 중...");

	CString err;
	if (!m_run.Start(m_plugin, m_request, GetSafeHwnd(), WM_PLUGIN_EVENT, err))
	{
		Finish(err);
		return TRUE;
	}
	m_startTick = ::GetTickCount64();
	SetTimer(1, 500, nullptr);
	return TRUE;
}

void CPluginRunDlg::SetMarquee(bool on)
{
	CWnd* bar = GetDlgItem(IDC_PLG_PROGRESS);
	if (!bar || m_marquee == on)
		return;
	m_marquee = on;
	if (on)
	{
		bar->ModifyStyle(0, PBS_MARQUEE);
		bar->SendMessage(PBM_SETMARQUEE, TRUE, 30);
	}
	else
	{
		bar->SendMessage(PBM_SETMARQUEE, FALSE, 0);
		bar->ModifyStyle(PBS_MARQUEE, 0);
	}
}

void CPluginRunDlg::AppendLog(const CString& line)
{
	CEdit* log = static_cast<CEdit*>(GetDlgItem(IDC_PLG_LOG));
	if (!log)
		return;
	const int len = log->GetWindowTextLength();
	log->SetSel(len, len);
	log->ReplaceSel((len > 0 ? CString(L"\r\n") : CString()) + line);
}

void CPluginRunDlg::Finish(const CString& message)
{
	m_done = true;
	KillTimer(1);
	KillTimer(2);
	if (m_hiddenWaiting || !IsWindowVisible())
	{
		m_hiddenWaiting = false;
		ShowWindow(SW_SHOW);   // 숨긴 채로 실패하면 이유를 보여 줌
	}
	SetMarquee(false);
	if (CWnd* bar = GetDlgItem(IDC_PLG_PROGRESS))
		bar->SendMessage(PBM_SETPOS, 0, 0);
	SetDlgItemText(IDC_PLG_MSG, message);
	if (!message.IsEmpty())
		AppendLog(L"[RuliManager] " + message);
	SetDlgItemText(IDC_PLG_STOP, L"닫기");
}

LRESULT CPluginRunDlg::OnRunEvent(WPARAM, LPARAM lParam)
{
	std::unique_ptr<CPluginRun::Event> e(reinterpret_cast<CPluginRun::Event*>(lParam));
	if (!e)
		return 0;
	switch (e->kind)
	{
	case CPluginRun::Event::Progress:
		if (e->percent >= 0)
		{
			SetMarquee(false);
			if (CWnd* bar = GetDlgItem(IDC_PLG_PROGRESS))
				bar->SendMessage(PBM_SETPOS, (std::min)(100, e->percent), 0);
		}
		if (!e->text.IsEmpty())
		{
			m_lastMessage = e->text;
			UpdateMessage();
		}
		break;
	case CPluginRun::Event::Log:
		AppendLog(e->text);
		break;
	case CPluginRun::Event::Item:
		if (m_stoppedByUser)
			break;   // 그만한 뒤 늦게 온 결과
		++m_received;
		m_queue.push_back(e->text);
		UpdateMessage();
		DrainQueue();
		break;
	case CPluginRun::Event::Done:
		if (m_busy)
		{
			m_pendingDone = std::move(e);   // 고르기 창 등이 떠 있음 → 처리가 끝나면
			return 0;
		}
		HandleDone(*e);
		break;
	}
	return 0;
}

void CPluginRunDlg::DrainQueue()
{
	if (m_busy)
		return;   // 이미 처리 중 (안쪽 창의 메시지 루프에서 다시 불림) - 쌓아 둔 것은 바깥 루프가 이어서
	m_busy = true;
	EnableWindow(FALSE);   // 처리 중에는 실행 창을 누르지 못하게 (안쪽 창이 닫히면 다시)
	// 처리 중 뜨는 대화 상자(연결 창 · 고르기 창 등)는 맨 앞으로 (플러그인 크롬 창 등에 가려지지 않게)
	g_frontNotify = GetSafeHwnd();
	if (!g_frontHook)
		g_frontHook = ::SetWindowsHookExW(WH_CBT, FrontCbtProc, nullptr, ::GetCurrentThreadId());
	// 처리하는 동안(채울 항목 고르기 · 연결 창) 플러그인은 일시 정지 - 고른 뒤에 다음 작업을 이어서
	if (m_run.Running())
	{
		m_run.Pause(true);
		m_pauseTick = ::GetTickCount64();
		m_lastMessage = L"일시 정지 - 결과 처리 중 (항목 고르기 등)";
		UpdateMessage();
	}
	while (!m_queue.empty() && !m_stoppedByUser)
	{
		const CString line = m_queue.front();
		m_queue.pop_front();
		if (m_onItem && !m_onItem(line))
		{
			// [취소]: 남은 결과는 버리고 플러그인도 멈춤
			m_stoppedByUser = true;
			m_queue.clear();
			if (m_run.Running())
			{
				m_closeWhenDone = true;
				m_run.Cancel();
			}
		}
		UpdateMessage();
	}
	if (m_run.Paused())
	{
		m_run.Pause(false);   // 다시 실행
		if (m_pauseTick)
			m_startTick += ::GetTickCount64() - m_pauseTick;   // 멈춘 시간은 빼고
		m_pauseTick = 0;
		m_lastMessage = L"다시 실행 중...";
		UpdateMessage();
	}
	if (g_frontHook)
	{
		::UnhookWindowsHookEx(g_frontHook);
		g_frontHook = nullptr;
	}
	g_frontNotify = nullptr;
	EnableWindow(TRUE);
	// 요청한 결과를 모두 받아 처리했으면 실행 창은 숨기고 플러그인이 끝나기만 기다림 (끝 줄 · 종료까지의 잠깐 동안 창이 다시 보이지 않게)
	// 5초 안에 끝나지 않으면 다시 보여 줌 (멈춘 플러그인을 [중단] 할 수 있게)
	const bool allDone = m_total > 0 && m_received >= m_total && m_queue.empty() && !m_stoppedByUser && !m_pendingDone && m_run.Running();
	if (allDone && IsWindowVisible())
	{
		ShowWindow(SW_HIDE);
		m_hiddenWaiting = true;
		SetTimer(2, 5000, nullptr);
	}
	else if (!m_pendingDone && !m_stoppedByUser)
		SetActiveWindow();   // 안쪽 창이 닫히면서 활성이 메인 창으로 간 것을 되돌림
	m_busy = false;
	if (m_pendingDone)
	{
		std::unique_ptr<CPluginRun::Event> d = std::move(m_pendingDone);
		HandleDone(*d);
	}
}

void CPluginRunDlg::HandleDone(const CPluginRun::Event& e)
{
	if (e.ok)
	{
		m_resultJson = e.resultJson;
		m_done = true;
		KillTimer(1);
		EndDialog(IDOK);
		return;
	}
	if (m_closeWhenDone || m_stoppedByUser)
	{
		m_done = true;
		EndDialog(IDCANCEL);
		return;
	}
	CString msg = e.text;
	if (e.timedOut && m_plugin.timeout > 0)
		msg.Format(L"시간 제한(%d초)을 넘겨 중단했습니다. plugin.json 의 \"timeout\" 으로 늘릴 수 있습니다.", m_plugin.timeout);
	Finish(msg);
}

void CPluginRunDlg::UpdateMessage()
{
	if (m_done)
		return;
	const ULONGLONG now = m_pauseTick ? m_pauseTick : ::GetTickCount64();   // 멈춘 동안은 시간이 가지 않게
	const ULONGLONG sec = m_startTick ? (now - m_startTick) / 1000 : 0;
	CString t;
	t.Format(L"%s (%llu초)", static_cast<LPCWSTR>(m_lastMessage.IsEmpty() ? CString(L"실행 중...") : m_lastMessage), sec);
	if (m_received > 0)
	{
		CString n;
		if (m_total > 0)
			n.Format(L"  · 결과 %d / %d", m_received, m_total);
		else
			n.Format(L"  · 결과 %d", m_received);
		t += n;
	}
	SetDlgItemText(IDC_PLG_MSG, t);
}

void CPluginRunDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 2)
	{
		KillTimer(2);
		if (!m_done && m_hiddenWaiting)
		{
			m_hiddenWaiting = false;
			m_lastMessage = L"결과를 모두 받았습니다 - 플러그인이 끝나기를 기다리는 중";
			UpdateMessage();
			ShowWindow(SW_SHOW);
		}
		return;
	}
	if (nIDEvent == 1 && !m_done)
	{
		const ULONGLONG sec = (::GetTickCount64() - m_startTick) / 1000;
		if (m_plugin.timeout > 0 && sec >= static_cast<ULONGLONG>(m_plugin.timeout) && m_run.Running() && !m_run.Paused())
		{
			m_run.Cancel(true);
			return;
		}
		UpdateMessage();
		if (m_plugin.keepBehind && m_run.Running() && !m_run.Paused())
			KeepPluginWindowsBehind();
		return;
	}
	CDialogEx::OnTimer(nIDEvent);
}

namespace
{
	struct BehindCtx
	{
		const std::vector<DWORD>* pids;
		std::vector<HWND> found;
	};

	BOOL CALLBACK FindPluginWindows(HWND h, LPARAM lp)
	{
		BehindCtx* ctx = reinterpret_cast<BehindCtx*>(lp);
		if (!::IsWindowVisible(h) || ::IsIconic(h))
			return TRUE;
		DWORD pid = 0;
		::GetWindowThreadProcessId(h, &pid);
		if (std::find(ctx->pids->begin(), ctx->pids->end(), pid) != ctx->pids->end())
			ctx->found.push_back(h);
		return TRUE;
	}
}

LRESULT CPluginRunDlg::OnBringFront(WPARAM wParam, LPARAM)
{
	ForceFront(reinterpret_cast<HWND>(wParam));
	return 0;
}

void CPluginRunDlg::KeepPluginWindowsBehind()
{
	const std::vector<DWORD> pids = m_run.ProcessIds();
	if (pids.empty())
		return;
	BehindCtx ctx{ &pids, {} };
	::EnumWindows(FindPluginWindows, reinterpret_cast<LPARAM>(&ctx));
	CWnd* main = AfxGetMainWnd();
	const HWND mainWnd = main ? main->GetSafeHwnd() : nullptr;
	bool moved = false;
	for (HWND h : ctx.found)
	{
		if (std::find(m_behindDone.begin(), m_behindDone.end(), h) != m_behindDone.end())
			continue;   // 이미 한 번 보냄 (사용자가 꺼내 보는 중일 수 있음)
		m_behindDone.push_back(h);
		// 메인 창 바로 아래로 (크기 · 최대화 그대로, 활성화하지 않음)
		::SetWindowPos(h, mainWnd ? mainWnd : HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
		moved = true;
	}
	if (!moved)
		return;
	// 플러그인 창이 포커스를 가져갔으면 실행 창으로 되돌림 (다른 프로세스가 앞에 있을 때는 입력 큐를 잠시 붙여서)
	const HWND fg = ::GetForegroundWindow();
	if (fg && std::find(ctx.found.begin(), ctx.found.end(), fg) != ctx.found.end())
	{
		const DWORD fgThread = ::GetWindowThreadProcessId(fg, nullptr);
		const DWORD myThread = ::GetCurrentThreadId();
		const bool attach = fgThread != myThread && ::AttachThreadInput(myThread, fgThread, TRUE);
		if (mainWnd)
			::SetWindowPos(mainWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		::SetForegroundWindow(GetSafeHwnd());
		::SetActiveWindow(GetSafeHwnd());
		if (attach)
			::AttachThreadInput(myThread, fgThread, FALSE);
	}
}

void CPluginRunDlg::OnBnClickedStop()
{
	if (m_done || !m_run.Running())
	{
		EndDialog(IDCANCEL);
		return;
	}
	SetDlgItemText(IDC_PLG_MSG, L"중단하는 중...");
	m_run.Cancel();
}

void CPluginRunDlg::OnCancel()
{
	// Esc · 닫기 단추: 실행 중이면 중단하고 끝나면 닫음
	if (!m_done && m_run.Running())
	{
		m_closeWhenDone = true;
		m_run.Cancel();
		return;
	}
	CDialogEx::OnCancel();
}

HBRUSH CPluginRunDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
		return br;
	return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}
