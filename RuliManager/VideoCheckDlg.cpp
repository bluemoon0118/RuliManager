#include "pch.h"
#include "RuliManager.h"
#include "VideoCheckDlg.h"
#include "PreviewVideo.h"
#include "VectorIcons.h"

#include <functional>
#include <string>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	const UINT WM_VC_PROGRESS = WM_APP + 0x51;   // wParam = 항목 id, lParam = 0 ~ 100
	const UINT WM_VC_DONE     = WM_APP + 0x52;   // lParam = CVideoCheckDlg::Result* (받는 쪽이 delete)

	CString Utf8(const std::string& s)
	{
		if (s.empty())
			return CString();
		const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
		CString w;
		if (n > 0)
		{
			::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.GetBuffer(n), n);
			w.ReleaseBuffer(n);
		}
		return w;
	}

	// 창 아이콘: 프로그램 강조색 바탕 위에 흰 돋보기 + 재생 (VectorIcon::Inspect) - size × size 32비트 아이콘
	HICON MakeInspectIcon(int size)
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
			VectorIcon::Inspect(&mem, rc, RGB(255, 255, 255), 0.86);
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

	CString Num(double v)
	{
		CString s;
		s.Format(L"%.3f", v);
		s.Replace(L',', L'.');
		return s;
	}

	CString TimeText(double sec)
	{
		if (sec < 0)
			sec = 0;
		const int t = static_cast<int>(sec + 0.5);
		CString s;
		if (t >= 3600)
			s.Format(L"%d:%02d:%02d", t / 3600, (t / 60) % 60, t % 60);
		else
			s.Format(L"%d:%02d", t / 60, t % 60);
		return s;
	}

	// 프로그램 실행 (창 없음) - 표준 출력 · 오류를 줄 단위로 넘김 (\r · \n 모두 줄 끝), cancel 이 켜지거나 시간이 지나면 끝냄
	bool RunLines(const CString& cmdLine, const std::atomic<bool>& cancel, DWORD timeoutMs,
		const std::function<void(const std::string&)>& onLine, DWORD& exitCode, bool& canceled, bool& timedOut)
	{
		exitCode = static_cast<DWORD>(-1);
		canceled = timedOut = false;
		SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
		HANDLE readPipe = nullptr, writePipe = nullptr;
		if (!::CreatePipe(&readPipe, &writePipe, &sa, 0))
			return false;
		::SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
		STARTUPINFOW si = { sizeof(si) };
		si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		si.hStdOutput = writePipe;
		si.hStdError = writePipe;
		PROCESS_INFORMATION pi = {};
		CString cmd = cmdLine;
		const BOOL created = ::CreateProcessW(nullptr, cmd.GetBuffer(), nullptr, nullptr, TRUE,
			CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi);
		cmd.ReleaseBuffer();
		::CloseHandle(writePipe);
		if (!created)
		{
			::CloseHandle(readPipe);
			return false;
		}
		std::string partial;
		auto feed = [&](const char* p, DWORD n)
		{
			for (DWORD i = 0; i < n; ++i)
			{
				const char ch = p[i];
				if (ch == '\r' || ch == '\n')
				{
					if (!partial.empty())
						onLine(partial);
					partial.clear();
				}
				else if (partial.size() < 4096)
					partial += ch;
			}
		};
		auto drain = [&]()
		{
			DWORD avail = 0;
			while (::PeekNamedPipe(readPipe, nullptr, 0, nullptr, &avail, nullptr) && avail > 0)
			{
				char buf[4096];
				DWORD got = 0;
				if (!::ReadFile(readPipe, buf, (std::min)(avail, static_cast<DWORD>(sizeof(buf))), &got, nullptr) || got == 0)
					break;
				feed(buf, got);
			}
		};
		const ULONGLONG startTick = ::GetTickCount64();
		for (;;)
		{
			drain();
			if (::WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0)
				break;
			const bool late = (timeoutMs > 0 && ::GetTickCount64() - startTick > timeoutMs);
			if (cancel || late)
			{
				::TerminateProcess(pi.hProcess, 1);
				::WaitForSingleObject(pi.hProcess, 5000);
				(cancel ? canceled : timedOut) = true;
				break;
			}
		}
		drain();
		if (!partial.empty())
			onLine(partial);
		::GetExitCodeProcess(pi.hProcess, &exitCode);
		::CloseHandle(pi.hThread);
		::CloseHandle(pi.hProcess);
		::CloseHandle(readPipe);
		return true;
	}

	// -progress pipe:1 의 "key=value" 줄 (오류 줄과 구분)
	bool IsProgressLine(const std::string& l)
	{
		const size_t eq = l.find('=');
		if (eq == std::string::npos || eq == 0)
			return false;
		for (size_t i = 0; i < eq; ++i)
		{
			const char c = l[i];
			if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
				return false;
		}
		return true;
	}

	long long ProgressValue(const std::string& l, const char* key)
	{
		const size_t kl = strlen(key);
		if (l.compare(0, kl, key) != 0 || l.size() <= kl || l[kl] != '=')
			return -1;
		const char* v = l.c_str() + kl + 1;
		if (*v < '0' || *v > '9')
			return -1;   // N/A
		return _atoi64(v);
	}

	struct Probe
	{
		bool   ran = false;
		bool   canceled = false;
		double duration = 0;      // 초 (모르면 0)
		int    streams = 0;       // Stream # 줄 수
		int    videos = 0;        // 영상 스트림 (첨부 그림 제외)
		std::vector<std::string> lines;
	};

	Probe RunProbe(const CString& ffmpeg, const CString& path, const std::atomic<bool>& cancel)
	{
		Probe p;
		DWORD code = 0;
		bool timedOut = false;
		p.ran = RunLines(L"\"" + ffmpeg + L"\" -hide_banner -nostdin -i \"" + path + L"\"", cancel, 60000,
			[&p](const std::string& l)
			{
				if (p.lines.size() < 400)
					p.lines.push_back(l);
				const size_t d = l.find("Duration: ");
				if (d != std::string::npos && p.duration <= 0)
				{
					int h = 0, m = 0;
					double s = 0;
					if (sscanf_s(l.c_str() + d + 10, "%d:%d:%lf", &h, &m, &s) == 3)
						p.duration = h * 3600.0 + m * 60.0 + s;
				}
				if (l.find("Stream #") != std::string::npos)
				{
					++p.streams;
					if (l.find("Video:") != std::string::npos && l.find("attached pic") == std::string::npos)
						++p.videos;
				}
			}, code, p.canceled, timedOut);
		return p;
	}

	CString LastProbeLines(const Probe& p, int n = 6)
	{
		CString out;
		const int start = (std::max)(0, static_cast<int>(p.lines.size()) - n);
		for (int i = start; i < static_cast<int>(p.lines.size()); ++i)
			out += Utf8(p.lines[i]) + L"\n";
		return out;
	}

	using Progress = std::function<void(int)>;

	CVideoCheckDlg::Result CheckVideo(const CString& ffmpeg, const CString& path, int mode, const std::atomic<bool>& cancel, const Progress& progress)
	{
		CVideoCheckDlg::Result r;
		if (!::PathFileExistsW(path))
		{
			r.state = CVideoCheckDlg::ST_ERROR;
			r.summary = L"파일이 없음";
			return r;
		}
		// 1) 머리 정보
		const Probe p = RunProbe(ffmpeg, path, cancel);
		if (p.canceled) { r.state = CVideoCheckDlg::ST_CANCEL; r.summary = L"취소함"; return r; }
		if (!p.ran) { r.state = CVideoCheckDlg::ST_ERROR; r.summary = L"ffmpeg.exe 를 실행하지 못함"; return r; }
		progress(5);
		CString head;
		head.Format(L"길이: %s · 스트림 %d개 (영상 %d개)\n", p.duration > 0 ? static_cast<LPCWSTR>(TimeText(p.duration)) : L"알 수 없음", p.streams, p.videos);
		if (p.streams == 0)
		{
			r.state = CVideoCheckDlg::ST_BAD;
			r.summary = L"열 수 없음 (머리 정보 손상)";
			r.detail = head + L"\nffmpeg 출력:\n" + LastProbeLines(p);
			return r;
		}
		if (p.videos == 0)
		{
			r.state = CVideoCheckDlg::ST_BAD;
			r.summary = L"영상 스트림 없음";
			r.detail = head + L"\nffmpeg 출력:\n" + LastProbeLines(p, 12);
			return r;
		}

		int errors = 0;
		std::vector<CString> errLines;
		auto addError = [&](const std::string& l, const CString& where)
		{
			++errors;
			if (errLines.size() < 40)
				errLines.push_back(where + Utf8(l));
		};

		if (mode == 0)
		{
			// 2) 빠른 검사: 곳곳 몇 군데를 1초씩 풀어 봄 (끝 부분 포함)
			std::vector<double> pos;
			if (p.duration > 0)
			{
				const int n = 8;
				for (int k = 1; k <= n; ++k)
					pos.push_back(p.duration * k / (n + 1));
				pos.push_back((std::max)(0.0, p.duration - 4));
			}
			else
				pos.push_back(0);
			int failed = 0;
			CString failedAt;
			for (size_t i = 0; i < pos.size(); ++i)
			{
				long long frames = 0;
				DWORD code = 0;
				bool canceled = false, timedOut = false;
				const CString where = L"[" + TimeText(pos[i]) + L"] ";
				const bool ran = RunLines(L"\"" + ffmpeg + L"\" -hide_banner -nostdin -v error -nostats -progress pipe:1 -ss " + Num(pos[i]) +
					L" -i \"" + path + L"\" -t 1 -map 0:V:0 -an -sn -f null -", cancel, 60000,
					[&](const std::string& l)
					{
						if (IsProgressLine(l))
						{
							const long long f = ProgressValue(l, "frame");
							if (f >= 0) frames = f;
						}
						else
							addError(l, where);
					}, code, canceled, timedOut);
				if (canceled) { r.state = CVideoCheckDlg::ST_CANCEL; r.summary = L"취소함"; return r; }
				if (!ran || timedOut || frames <= 0)
				{
					++failed;
					failedAt += (failedAt.IsEmpty() ? CString() : CString(L", ")) + TimeText(pos[i]) + (timedOut ? L"(시간 초과)" : L"");
				}
				progress(5 + static_cast<int>(95 * (i + 1) / pos.size()));
			}
			const int total = static_cast<int>(pos.size());
			CString line;
			line.Format(L"구간 검사: %d곳 중 %d곳 실패", total, failed);
			if (failed)
				line += L" (" + failedAt + L")";
			r.detail = head + line + L"\n";
			if (failed * 2 >= total)
			{
				r.state = CVideoCheckDlg::ST_BAD;
				r.summary.Format(L"손상 - %d곳 중 %d곳을 풀 수 없음", total, failed);
			}
			else if (failed > 0 || errors > 0 || p.duration <= 0)
			{
				r.state = CVideoCheckDlg::ST_SUSPECT;
				r.summary.Format(L"의심 - 실패 %d곳 · 오류 %d줄%s", failed, errors, p.duration <= 0 ? L" · 길이 정보 없음" : L"");
			}
			else
			{
				r.state = CVideoCheckDlg::ST_OK;
				r.summary.Format(L"정상 (빠른 검사 %d곳)", total);
			}
		}
		else
		{
			// 2) 정밀 검사: 처음부터 끝까지 영상 · 소리를 모두 풀어 봄
			double lastTime = 0;
			int lastPct = -1;
			DWORD code = 0;
			bool canceled = false, timedOut = false;
			const bool ran = RunLines(L"\"" + ffmpeg + L"\" -hide_banner -nostdin -v error -nostats -progress pipe:1 -i \"" + path +
				L"\" -map 0:V:0 -map 0:a? -sn -f null -", cancel, 0,
				[&](const std::string& l)
				{
					if (IsProgressLine(l))
					{
						const long long us = ProgressValue(l, "out_time_us");
						if (us >= 0)
						{
							lastTime = us / 1e6;
							if (p.duration > 0)
							{
								const int pct = 5 + static_cast<int>(95.0 * (std::min)(1.0, lastTime / p.duration));
								if (pct != lastPct) { lastPct = pct; progress(pct); }
							}
						}
					}
					else
						addError(l, L"[" + TimeText(lastTime) + L"] ");
				}, code, canceled, timedOut);
			if (canceled) { r.state = CVideoCheckDlg::ST_CANCEL; r.summary = L"취소함"; return r; }
			if (!ran) { r.state = CVideoCheckDlg::ST_ERROR; r.summary = L"ffmpeg.exe 를 실행하지 못함"; return r; }
			const bool reachedEnd = (p.duration <= 0) || lastTime >= p.duration * 0.97 - 1;
			CString line;
			line.Format(L"끝까지 풀기: %s / %s%s\n", static_cast<LPCWSTR>(TimeText(lastTime)),
				p.duration > 0 ? static_cast<LPCWSTR>(TimeText(p.duration)) : L"?", reachedEnd ? L"" : L" (끝까지 풀리지 않음)");
			r.detail = head + line;
			if ((p.duration > 0 && lastTime < p.duration * 0.5) || errors > 50 || (code != 0 && lastTime <= 0))
			{
				r.state = CVideoCheckDlg::ST_BAD;
				r.summary.Format(L"손상 - 오류 %d줄%s", errors, reachedEnd ? L"" : static_cast<LPCWSTR>(L" · " + TimeText(lastTime) + L" 에서 멈춤"));
			}
			else if (errors > 0 || !reachedEnd)
			{
				r.state = CVideoCheckDlg::ST_SUSPECT;
				r.summary.Format(L"의심 - 오류 %d줄%s", errors, reachedEnd ? L"" : L" · 끝까지 풀리지 않음");
			}
			else
			{
				r.state = CVideoCheckDlg::ST_OK;
				r.summary = L"정상 (정밀 검사)";
			}
		}
		if (errors > 0)
		{
			CString e;
			e.Format(L"\n오류 %d줄%s:\n", errors, errors > static_cast<int>(errLines.size()) ? L" (앞부분만)" : L"");
			r.detail += e;
			for (const CString& l : errLines)
				r.detail += l + L"\n";
		}
		return r;
	}

	LPCWSTR StateText(CVideoCheckDlg::State s)
	{
		switch (s)
		{
		case CVideoCheckDlg::ST_WAIT:    return L"대기";
		case CVideoCheckDlg::ST_RUN:     return L"검사 중";
		case CVideoCheckDlg::ST_OK:      return L"정상";
		case CVideoCheckDlg::ST_SUSPECT: return L"의심";
		case CVideoCheckDlg::ST_BAD:     return L"손상";
		case CVideoCheckDlg::ST_CANCEL:  return L"취소";
		default:                         return L"오류";
		}
	}
}

// ---------------------------------------------------------------------------

CVideoCheckDlg::CVideoCheckDlg(CWnd* pParent)
	: CDialogEx(IDD_VIDEO_CHECK, pParent)
{
}

CVideoCheckDlg::~CVideoCheckDlg()
{
	m_cancel = true;
	if (m_thread.joinable())
		m_thread.join();
	if (m_iconSmall) ::DestroyIcon(m_iconSmall);
	if (m_iconBig) ::DestroyIcon(m_iconBig);
}

void CVideoCheckDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_VC_LIST, m_list);
	DDX_Control(pDX, IDC_VC_MODE, m_comboMode);
	DDX_Control(pDX, IDC_VC_DETAIL, m_editDetail);
}

BEGIN_MESSAGE_MAP(CVideoCheckDlg, CDialogEx)
	ON_WM_CTLCOLOR()
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDC_VC_START, &CVideoCheckDlg::OnStart)
	ON_BN_CLICKED(IDC_VC_STOP, &CVideoCheckDlg::OnStop)
	ON_BN_CLICKED(IDC_VC_REMOVE, &CVideoCheckDlg::OnRemove)
	ON_BN_CLICKED(IDC_VC_CLEARDONE, &CVideoCheckDlg::OnClearDone)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_VC_LIST, &CVideoCheckDlg::OnListItemChanged)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_VC_LIST, &CVideoCheckDlg::OnListCustomDraw)
	ON_NOTIFY(NM_DBLCLK, IDC_VC_LIST, &CVideoCheckDlg::OnListDblClk)
	ON_MESSAGE(WM_VC_PROGRESS, &CVideoCheckDlg::OnCheckProgress)
	ON_MESSAGE(WM_VC_DONE, &CVideoCheckDlg::OnCheckDone)
END_MESSAGE_MAP()

bool CVideoCheckDlg::CreateModeless(CWnd* parent)
{
	return Create(IDD_VIDEO_CHECK, parent) != FALSE;
}

BOOL CVideoCheckDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	m_theme.Apply(this, { IDC_VC_REMOVE });
	// 제목 표시줄 · Alt+Tab 아이콘 (돋보기 + 재생)
	m_iconSmall = MakeInspectIcon(::GetSystemMetrics(SM_CXSMICON));
	m_iconBig = MakeInspectIcon(::GetSystemMetrics(SM_CXICON));
	if (m_iconSmall) SetIcon(m_iconSmall, FALSE);
	if (m_iconBig) SetIcon(m_iconBig, TRUE);

	m_comboMode.AddString(L"빠른 검사 (곳곳 몇 군데 · 1편에 몇 초)");
	m_comboMode.AddString(L"정밀 검사 (처음부터 끝까지 · 오래 걸림)");
	const int mode = AfxGetApp()->GetProfileInt(L"Settings", L"VideoCheckMode", 0);
	m_comboMode.SetCurSel((mode == 1) ? 1 : 0);

	m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
	CRect rc;
	m_list.GetClientRect(&rc);
	const int w = (std::max)(300, rc.Width());
	m_list.InsertColumn(0, L"파일", LVCFMT_LEFT, w * 30 / 100);
	m_list.InsertColumn(1, L"상태", LVCFMT_LEFT, w * 11 / 100);
	m_list.InsertColumn(2, L"결과", LVCFMT_LEFT, w * 34 / 100);
	m_list.InsertColumn(3, L"폴더", LVCFMT_LEFT, w * 25 / 100);

	CRect wr;
	GetWindowRect(&wr);
	m_minSize = wr.Size();
	GetClientRect(&m_initClient);
	m_editDetail.SetLimitText(0);
	m_editDetail.SetWindowText(L"목록에서 영상을 고르면 자세한 결과가 여기에 나옵니다.\r\n"
		L"영상 카드 오른쪽 클릭 [영상 검사] 로 목록에 추가하면 차례로 검사합니다. 창을 닫아도 검사는 계속됩니다.");
	UpdateButtons();
	UpdateStatusLine();
	return TRUE;
}

void CVideoCheckDlg::ShowAndActivate()
{
	if (!GetSafeHwnd())
		return;
	if (IsIconic())
		ShowWindow(SW_RESTORE);
	ShowWindow(SW_SHOW);
	SetForegroundWindow();
}

void CVideoCheckDlg::OnCancel()
{
	ShowWindow(SW_HIDE);   // 닫기 = 숨기기 (검사는 계속)
}

void CVideoCheckDlg::PostNcDestroy()
{
	CDialogEx::PostNcDestroy();   // 메인 창이 delete
}

void CVideoCheckDlg::OnDestroy()
{
	m_cancel = true;
	if (m_thread.joinable())
		m_thread.join();
	MSG msg;
	while (::PeekMessageW(&msg, GetSafeHwnd(), WM_VC_DONE, WM_VC_DONE, PM_REMOVE))
		delete reinterpret_cast<Result*>(msg.lParam);
	m_running = false;
	if (m_comboMode.GetSafeHwnd())
		AfxGetApp()->WriteProfileInt(L"Settings", L"VideoCheckMode", m_comboMode.GetCurSel() == 1 ? 1 : 0);
	CDialogEx::OnDestroy();
}

HBRUSH CVideoCheckDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
		return br;
	return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CVideoCheckDlg::OnGetMinMaxInfo(MINMAXINFO* mmi)
{
	if (m_minSize.cx > 0)
	{
		mmi->ptMinTrackSize.x = m_minSize.cx;
		mmi->ptMinTrackSize.y = m_minSize.cy * 3 / 4;
	}
	CDialogEx::OnGetMinMaxInfo(mmi);
}

void CVideoCheckDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialogEx::OnSize(nType, cx, cy);
	if (m_list.GetSafeHwnd() && nType != SIZE_MINIMIZED)
		LayoutControls(cx, cy);
}

void CVideoCheckDlg::LayoutControls(int cx, int cy)
{
	// 위 줄(방식 · 단추)은 그대로, [닫기] 는 오른쪽 끝, 목록 · 자세히 · 상태 줄은 창 크기를 따라
	CRect m(7, 7, 7, 7);
	MapDialogRect(&m);
	const int mx = m.left, my = m.top;
	auto rectOf = [this](UINT id) { CRect r; GetDlgItem(id)->GetWindowRect(&r); ScreenToClient(&r); return r; };
	if (CWnd* close = GetDlgItem(IDCANCEL))
	{
		CRect r = rectOf(IDCANCEL);
		close->SetWindowPos(nullptr, cx - mx - r.Width(), r.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
	const CRect listRc = rectOf(IDC_VC_LIST);
	CRect st = rectOf(IDC_VC_STATUS);
	const int stH = st.Height();
	const int stY = cy - my - stH;
	GetDlgItem(IDC_VC_STATUS)->SetWindowPos(nullptr, mx, stY, cx - mx * 2, stH, SWP_NOZORDER | SWP_NOACTIVATE);
	const int avail = stY - my / 2 - listRc.top;
	const int detailH = (std::max)(60, avail * 32 / 100);
	const int listH = (std::max)(60, avail - detailH - my / 2);
	m_list.SetWindowPos(nullptr, mx, listRc.top, cx - mx * 2, listH, SWP_NOZORDER | SWP_NOACTIVATE);
	m_editDetail.SetWindowPos(nullptr, mx, listRc.top + listH + my / 2, cx - mx * 2, detailH, SWP_NOZORDER | SWP_NOACTIVATE);
	Invalidate();
}

int CVideoCheckDlg::FindJob(int id) const
{
	for (size_t i = 0; i < m_jobs.size(); ++i)
		if (m_jobs[i].id == id)
			return static_cast<int>(i);
	return -1;
}

void CVideoCheckDlg::RefreshRow(int index)
{
	if (index < 0 || index >= static_cast<int>(m_jobs.size()) || index >= m_list.GetItemCount())
		return;
	const Job& j = m_jobs[index];
	m_list.SetItemText(index, 0, ::PathFindFileNameW(j.path));
	CString st = StateText(j.state);
	if (j.state == ST_RUN)
		st.Format(L"검사 중 %d%%", j.percent);
	m_list.SetItemText(index, 1, st);
	CString res = j.summary;
	if (j.state == ST_RUN && res.IsEmpty())
		res = (j.mode == 1) ? L"정밀 검사 중..." : L"빠른 검사 중...";
	m_list.SetItemText(index, 2, res);
	CString dir = j.path.Left(static_cast<int>(::PathFindFileNameW(j.path) - static_cast<LPCWSTR>(j.path)));
	dir.TrimRight(L'\\');
	m_list.SetItemText(index, 3, dir);
}

void CVideoCheckDlg::RebuildList()
{
	const int top = m_list.GetTopIndex();
	m_list.SetRedraw(FALSE);
	m_list.DeleteAllItems();
	for (size_t i = 0; i < m_jobs.size(); ++i)
	{
		m_list.InsertItem(static_cast<int>(i), L"");
		m_list.SetItemData(static_cast<int>(i), static_cast<DWORD_PTR>(m_jobs[i].id));
		RefreshRow(static_cast<int>(i));
	}
	if (top > 0 && top < m_list.GetItemCount())
		m_list.EnsureVisible(top, FALSE);
	m_list.SetRedraw(TRUE);
	m_list.Invalidate();
}

void CVideoCheckDlg::AddVideos(const std::vector<CString>& paths, bool start)
{
	int added = 0, firstNew = -1;
	for (const CString& p : paths)
	{
		if (p.IsEmpty())
			continue;
		bool skip = false;
		for (Job& j : m_jobs)
		{
			if (j.path.CompareNoCase(p) != 0)
				continue;
			if (j.state != ST_WAIT && j.state != ST_RUN)
			{
				// 끝난 항목: 다시 대기로
				j.state = ST_WAIT;
				j.percent = 0;
				j.summary.Empty();
				j.detail.Empty();
				++added;
			}
			skip = true;
			break;
		}
		if (skip)
			continue;
		Job j;
		j.id = m_nextId++;
		j.path = p;
		if (firstNew < 0)
			firstNew = static_cast<int>(m_jobs.size());
		m_jobs.push_back(j);
		++added;
	}
	if (GetSafeHwnd())
	{
		RebuildList();
		if (firstNew >= 0)
			m_list.EnsureVisible(static_cast<int>(m_jobs.size()) - 1, FALSE);
	}
	if (start && added > 0)
		m_paused = false;
	StartNext();
	UpdateButtons();
	UpdateStatusLine();
}

void CVideoCheckDlg::StartNext()
{
	if (m_running || m_paused || !GetSafeHwnd())
		return;
	int idx = -1;
	for (size_t i = 0; i < m_jobs.size(); ++i)
		if (m_jobs[i].state == ST_WAIT) { idx = static_cast<int>(i); break; }
	if (idx < 0)
	{
		UpdateButtons();
		UpdateStatusLine();
		return;
	}
	if (m_ffmpeg.IsEmpty() || !::PathFileExistsW(m_ffmpeg))
		m_ffmpeg = PreviewVideo::FindFfmpeg();
	if (m_ffmpeg.IsEmpty())
	{
		m_paused = true;
		UpdateButtons();
		UpdateStatusLine();
		MessageBox(L"ffmpeg.exe 를 찾을 수 없습니다.\n\nffmpeg.exe 를 프로그램(RuliManager.exe) 폴더에 넣은 뒤 [시작] 을 누르세요.\n"
			L"(프로그램 폴더\\ffmpeg\\bin\\ffmpeg.exe 또는 PATH 에 있어도 됩니다)", L"영상 검사", MB_ICONINFORMATION);
		return;
	}
	Job& j = m_jobs[idx];
	j.state = ST_RUN;
	j.percent = 0;
	j.mode = (m_comboMode.GetCurSel() == 1) ? 1 : 0;
	j.summary.Empty();
	j.detail.Empty();
	RefreshRow(idx);
	m_running = true;
	m_runId = j.id;
	m_cancel = false;
	if (m_thread.joinable())
		m_thread.join();
	const HWND hwnd = GetSafeHwnd();
	const CString ffmpeg = m_ffmpeg, path = j.path;
	const int mode = j.mode, id = j.id;
	std::atomic<bool>* cancel = &m_cancel;   // 창이 없어질 때 OnDestroy 가 스레드를 기다리므로 유효
	m_thread = std::thread([hwnd, ffmpeg, path, mode, id, cancel]()
	{
		Result* r = new Result(CheckVideo(ffmpeg, path, mode, *cancel, [hwnd, id](int pct)
		{
			::PostMessageW(hwnd, WM_VC_PROGRESS, static_cast<WPARAM>(id), static_cast<LPARAM>(pct));
		}));
		r->id = id;
		if (!::PostMessageW(hwnd, WM_VC_DONE, 0, reinterpret_cast<LPARAM>(r)))
			delete r;
	});
	UpdateButtons();
	UpdateStatusLine();
}

LRESULT CVideoCheckDlg::OnCheckProgress(WPARAM wParam, LPARAM lParam)
{
	const int idx = FindJob(static_cast<int>(wParam));
	if (idx >= 0 && m_jobs[idx].state == ST_RUN)
	{
		const int pct = (std::max)(0, (std::min)(100, static_cast<int>(lParam)));
		if (pct != m_jobs[idx].percent)
		{
			m_jobs[idx].percent = pct;
			RefreshRow(idx);
			UpdateStatusLine();
		}
	}
	return 0;
}

LRESULT CVideoCheckDlg::OnCheckDone(WPARAM, LPARAM lParam)
{
	Result* r = reinterpret_cast<Result*>(lParam);
	if (m_thread.joinable())
		m_thread.join();
	m_running = false;
	m_runId = 0;
	if (r)
	{
		const int idx = FindJob(r->id);
		if (idx >= 0)
		{
			if (m_jobs[idx].removeWhenDone)
			{
				m_jobs.erase(m_jobs.begin() + idx);
				RebuildList();
			}
			else
			{
				Job& j = m_jobs[idx];
				j.state = r->state;
				j.percent = 100;
				j.summary = r->summary;
				j.detail = r->detail;
				RefreshRow(idx);
				if (m_list.GetItemState(idx, LVIS_SELECTED) & LVIS_SELECTED)
					ShowDetailFor(idx);
			}
		}
		delete r;
	}
	StartNext();
	UpdateButtons();
	UpdateStatusLine();
	return 0;
}

void CVideoCheckDlg::OnStart()
{
	// 취소 · 오류였던 항목도 다시 대기로
	bool any = false;
	for (size_t i = 0; i < m_jobs.size(); ++i)
		if (m_jobs[i].state == ST_CANCEL || m_jobs[i].state == ST_ERROR)
		{
			m_jobs[i].state = ST_WAIT;
			m_jobs[i].summary.Empty();
			m_jobs[i].detail.Empty();
			RefreshRow(static_cast<int>(i));
			any = true;
		}
	(void)any;
	m_paused = false;
	StartNext();
	UpdateButtons();
	UpdateStatusLine();
}

void CVideoCheckDlg::OnStop()
{
	// 지금 검사는 취소하고, 남은 대기 항목은 [시작] 을 누를 때까지 멈춤
	m_paused = true;
	if (m_running)
		m_cancel = true;
	UpdateButtons();
	UpdateStatusLine();
}

void CVideoCheckDlg::OnRemove()
{
	std::vector<int> sel;
	POSITION pos = m_list.GetFirstSelectedItemPosition();
	while (pos)
		sel.push_back(m_list.GetNextSelectedItem(pos));
	if (sel.empty())
		return;
	for (auto it = sel.rbegin(); it != sel.rend(); ++it)
	{
		const int i = *it;
		if (i < 0 || i >= static_cast<int>(m_jobs.size()))
			continue;
		if (m_running && m_jobs[i].id == m_runId)
		{
			m_jobs[i].removeWhenDone = true;   // 검사 중: 취소하고 끝나면 뺌
			m_cancel = true;
			continue;
		}
		m_jobs.erase(m_jobs.begin() + i);
	}
	RebuildList();
	m_editDetail.SetWindowText(L"");
	UpdateButtons();
	UpdateStatusLine();
}

void CVideoCheckDlg::OnClearDone()
{
	for (size_t i = m_jobs.size(); i-- > 0;)
	{
		const State s = m_jobs[i].state;
		if (s != ST_WAIT && s != ST_RUN)
			m_jobs.erase(m_jobs.begin() + i);
	}
	RebuildList();
	m_editDetail.SetWindowText(L"");
	UpdateButtons();
	UpdateStatusLine();
}

void CVideoCheckDlg::UpdateButtons()
{
	if (!GetSafeHwnd())
		return;
	int waits = 0, retry = 0, done = 0;
	for (const Job& j : m_jobs)
	{
		if (j.state == ST_WAIT) ++waits;
		else if (j.state == ST_CANCEL || j.state == ST_ERROR) ++retry;
		if (j.state != ST_WAIT && j.state != ST_RUN) ++done;
	}
	GetDlgItem(IDC_VC_START)->EnableWindow(!m_running && (waits + retry) > 0);
	GetDlgItem(IDC_VC_STOP)->EnableWindow(m_running || (!m_paused && waits > 0));
	GetDlgItem(IDC_VC_REMOVE)->EnableWindow(m_list.GetSelectedCount() > 0);
	GetDlgItem(IDC_VC_CLEARDONE)->EnableWindow(done > 0);
}

void CVideoCheckDlg::UpdateStatusLine()
{
	if (!GetSafeHwnd())
		return;
	int cnt[7] = {};
	CString runName;
	int runPct = 0;
	for (const Job& j : m_jobs)
	{
		++cnt[j.state];
		if (j.state == ST_RUN) { runName = ::PathFindFileNameW(j.path); runPct = j.percent; }
	}
	CString s;
	s.Format(L"대기 %d · 정상 %d · 의심 %d · 손상 %d", cnt[ST_WAIT], cnt[ST_OK], cnt[ST_SUSPECT], cnt[ST_BAD]);
	if (cnt[ST_CANCEL] || cnt[ST_ERROR])
	{
		CString t;
		t.Format(L" · 취소 %d · 오류 %d", cnt[ST_CANCEL], cnt[ST_ERROR]);
		s += t;
	}
	if (m_running)
	{
		CString t;
		t.Format(L"   ▶ 검사 중: %s (%d%%)%s", static_cast<LPCWSTR>(runName), runPct, m_cancel ? L" - 취소하는 중..." : L"");
		s += t;
	}
	else if (m_paused && cnt[ST_WAIT] > 0)
		s += L"   ■ 멈춤 - [시작] 을 누르면 이어서 검사";
	SetDlgItemText(IDC_VC_STATUS, s);
}

void CVideoCheckDlg::ShowDetailFor(int index)
{
	if (index < 0 || index >= static_cast<int>(m_jobs.size()))
	{
		m_editDetail.SetWindowText(L"");
		return;
	}
	const Job& j = m_jobs[index];
	CString t = j.path + L"\n";
	if (j.state == ST_WAIT)
		t += L"대기 중";
	else if (j.state == ST_RUN)
		t += (j.mode == 1) ? L"정밀 검사 중..." : L"빠른 검사 중...";
	else
		t += CString(j.mode == 1 ? L"정밀 검사" : L"빠른 검사") + L" · " + StateText(j.state) + L"\n" + j.summary + L"\n\n" + j.detail;
	t.Replace(L"\r\n", L"\n");
	t.Replace(L"\n", L"\r\n");
	m_editDetail.SetWindowText(t);
}

void CVideoCheckDlg::OnListItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	if ((p->uChanged & LVIF_STATE) && (p->uNewState & LVIS_SELECTED) && !(p->uOldState & LVIS_SELECTED))
		ShowDetailFor(p->iItem);
	UpdateButtons();
	*pResult = 0;
}

void CVideoCheckDlg::OnListDblClk(NMHDR* pNMHDR, LRESULT* pResult)
{
	// 더블클릭: 탐색기에서 그 파일을 골라 보여 줌
	NMITEMACTIVATE* p = reinterpret_cast<NMITEMACTIVATE*>(pNMHDR);
	if (p->iItem >= 0 && p->iItem < static_cast<int>(m_jobs.size()))
	{
		const CString args = L"/select,\"" + m_jobs[p->iItem].path + L"\"";
		::ShellExecuteW(GetSafeHwnd(), L"open", L"explorer.exe", args, nullptr, SW_SHOWNORMAL);
	}
	*pResult = 0;
}

void CVideoCheckDlg::OnListCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
	// [상태] · [결과] 칸 글자색: 정상 초록 · 의심 주황 · 손상 빨강
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
		const int i = static_cast<int>(cd->nmcd.dwItemSpec);
		COLORREF c = DarkColors::Text;
		if ((cd->iSubItem == 1 || cd->iSubItem == 2) && i >= 0 && i < static_cast<int>(m_jobs.size()))
		{
			switch (m_jobs[i].state)
			{
			case ST_OK:      c = RGB(61, 204, 145); break;
			case ST_SUSPECT: c = RGB(242, 157, 73); break;
			case ST_BAD:     c = RGB(255, 115, 115); break;
			case ST_RUN:     c = RGB(72, 175, 240); break;
			case ST_CANCEL:
			case ST_ERROR:   c = TCOL(TC_SUB); break;
			default: break;
			}
		}
		cd->clrText = c;
		*pResult = CDRF_DODEFAULT;
		break;
	}
	default:
		break;
	}
}
