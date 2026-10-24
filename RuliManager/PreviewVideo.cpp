#include "pch.h"
#include "PreviewVideo.h"
#include "VideoLibrary.h"

#include <mfplay.h>
#include <evr.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <cmath>

#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "shlwapi.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	CString ExeDir()
	{
		wchar_t buf[MAX_PATH] = {};
		::GetModuleFileNameW(nullptr, buf, MAX_PATH);
		::PathRemoveFileSpecW(buf);
		return buf;
	}

	// 프로그램 실행 (창 없음) - 표준 출력 · 오류를 모아 둠 (뒤쪽 16KB 만), cancel 이 켜지면 끝냄
	bool RunCapture(const CString& cmdLine, const std::atomic<bool>& cancel, std::string& output, DWORD& exitCode, bool& canceled)
	{
		output.clear();
		exitCode = static_cast<DWORD>(-1);
		canceled = false;
		SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
		HANDLE readPipe = nullptr, writePipe = nullptr;
		if (!::CreatePipe(&readPipe, &writePipe, &sa, 0))
			return false;
		::SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);   // 읽는 쪽은 넘기지 않음

		STARTUPINFOW si = { sizeof(si) };
		si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		si.hStdInput = nullptr;
		si.hStdOutput = writePipe;
		si.hStdError = writePipe;
		PROCESS_INFORMATION pi = {};
		CString cmd = cmdLine;   // CreateProcessW 는 명령줄을 고칠 수 있음
		const BOOL created = ::CreateProcessW(nullptr, cmd.GetBuffer(), nullptr, nullptr, TRUE,
			CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi);
		cmd.ReleaseBuffer();
		::CloseHandle(writePipe);   // 자식이 끝나면 읽기가 끝나도록
		if (!created)
		{
			::CloseHandle(readPipe);
			return false;
		}

		auto drain = [&]()
		{
			DWORD avail = 0;
			while (::PeekNamedPipe(readPipe, nullptr, 0, nullptr, &avail, nullptr) && avail > 0)
			{
				char buf[4096];
				DWORD got = 0;
				if (!::ReadFile(readPipe, buf, (std::min)(avail, static_cast<DWORD>(sizeof(buf))), &got, nullptr) || got == 0)
					break;
				output.append(buf, got);
				if (output.size() > 16384)
					output.erase(0, output.size() - 16384);
			}
		};
		for (;;)
		{
			drain();
			if (::WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0)
				break;
			if (cancel)
			{
				::TerminateProcess(pi.hProcess, 1);
				::WaitForSingleObject(pi.hProcess, 5000);
				canceled = true;
				break;
			}
		}
		drain();
		::GetExitCodeProcess(pi.hProcess, &exitCode);
		::CloseHandle(pi.hThread);
		::CloseHandle(pi.hProcess);
		::CloseHandle(readPipe);
		return true;
	}

	CString Utf8ToCString(const std::string& s)
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

	// ffmpeg 출력의 마지막 몇 줄 (실패 이유로 보여 줌)
	CString LastLines(const std::string& out, int lines = 4)
	{
		CString text = Utf8ToCString(out);
		text.Replace(L"\r", L"");
		text.Trim();
		int pos = text.GetLength();
		for (int i = 0; i < lines && pos > 0; ++i)
		{
			const int nl = text.Left(pos).ReverseFind(L'\n');
			pos = (nl < 0) ? 0 : nl;
		}
		CString tail = text.Mid(pos);
		tail.Trim();
		return tail;
	}

	// "Duration: 01:23:45.67" → 초
	double ParseDuration(const std::string& out)
	{
		const size_t p = out.find("Duration: ");
		if (p == std::string::npos)
			return 0;
		int h = 0, m = 0;
		double s = 0;
		if (sscanf_s(out.c_str() + p + 10, "%d:%d:%lf", &h, &m, &s) != 3)
			return 0;
		return h * 3600.0 + m * 60.0 + s;
	}

	CString Num(double v)
	{
		CString s;
		s.Format(L"%.3f", v);
		s.Replace(L',', L'.');   // 소수점이 쉼표인 로캘 대비
		return s;
	}
}

namespace PreviewVideo
{
	CString FindFfmpeg()
	{
		const CString exe = ExeDir();
		const CString candidates[] = { exe + L"\\ffmpeg.exe", exe + L"\\ffmpeg\\bin\\ffmpeg.exe", CVideoLibrary::GetDataDir() + L"\\ffmpeg.exe" };
		for (const CString& c : candidates)
			if (::PathFileExistsW(c))
				return c;
		wchar_t buf[MAX_PATH] = {};
		if (::SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, buf, nullptr) > 0)
			return buf;
		return CString();
	}

	CString GetPreviewDir()
	{
		const CString dir = CVideoLibrary::GetDataDir() + L"\\previews";
		::CreateDirectoryW(dir, nullptr);
		return dir;
	}

	CString PreviewFileFor(const CString& videoPath, const CString& coverCacheFile)
	{
		CString name;
		if (!coverCacheFile.IsEmpty())
		{
			// 이동식 디스크: 커버 캐시와 같은 키 (드라이브 문자가 바뀌어도 같음)
			name = ::PathFindFileNameW(coverCacheFile);
			::PathRemoveExtensionW(name.GetBuffer());
			name.ReleaseBuffer();
		}
		else
		{
			CString key = videoPath;
			key.MakeLower();
			unsigned long long h = 1469598103934665603ULL;   // FNV-1a 64비트
			for (int i = 0; i < key.GetLength(); ++i)
				h = (h ^ static_cast<unsigned long long>(key[i])) * 1099511628211ULL;
			name.Format(L"%016llx", h);
		}
		return GetPreviewDir() + L"\\" + name + L".mp4";
	}

	Result Make(const CString& ffmpeg, const CString& videoPath, const CString& outFile, double durationSec, const std::atomic<bool>& cancel)
	{
		Result r;
		r.video = videoPath;
		r.file = outFile;
		if (ffmpeg.IsEmpty() || !::PathFileExistsW(ffmpeg))
		{
			r.error = L"ffmpeg.exe 를 찾을 수 없습니다.";
			return r;
		}
		if (!::PathFileExistsW(videoPath))
		{
			r.error = L"영상 파일이 없습니다.";
			return r;
		}

		std::string out;
		DWORD code = 0;
		bool canceled = false;
		// 길이를 모르면 ffmpeg 로 알아냄 (입력 정보만 출력하고 끝남)
		if (durationSec <= 0)
		{
			if (!RunCapture(L"\"" + ffmpeg + L"\" -hide_banner -nostdin -i \"" + videoPath + L"\"", cancel, out, code, canceled))
			{
				r.error = L"ffmpeg.exe 를 실행하지 못했습니다.";
				return r;
			}
			if (canceled) { r.canceled = true; return r; }
			durationSec = ParseDuration(out);
			if (durationSec <= 0)
			{
				r.error = L"영상 길이를 알 수 없습니다.\n" + LastLines(out);
				return r;
			}
		}

		// 구간: 길이를 (n + 1) 칸으로 나눈 경계마다 segLen 초 (맨 앞 · 맨 뒤는 피함)
		double segLen = 2.0;
		int n = 10;
		if (durationSec < n * segLen * 3)
			n = (std::max)(1, static_cast<int>(durationSec / (segLen * 3)));
		if (n == 1)
			segLen = (std::min)(6.0, (std::max)(0.5, durationSec * 0.5));

		CString inputs, filter, concatIn;
		for (int i = 0; i < n; ++i)
		{
			double start = durationSec * (i + 1) / (n + 1) - segLen / 2;
			start = (std::max)(0.0, (std::min)(start, durationSec - segLen));
			inputs += L" -ss " + Num(start) + L" -t " + Num(segLen) + L" -i \"" + videoPath + L"\"";
			CString part;
			part.Format(L"[%d:v:0]scale=480:-2,setsar=1,fps=24,format=yuv420p[v%d];", i, i);
			filter += part;
			CString lab;
			lab.Format(L"[v%d]", i);
			concatIn += lab;
		}
		CString tail;
		tail.Format(L"concat=n=%d:v=1:a=0[out]", n);
		filter += concatIn + tail;

		const CString tmp = outFile + L".part";
		::DeleteFileW(tmp);
		const CString base = L"\"" + ffmpeg + L"\" -hide_banner -nostdin -loglevel error -y" + inputs +
			L" -filter_complex \"" + filter + L"\" -map \"[out]\" -an ";
		// H.264 (libx264) 우선, 없는 ffmpeg 이면 MPEG-4 Part 2 로 다시
		const CString encoders[] = {
			L"-c:v libx264 -preset veryfast -crf 28 -movflags +faststart -f mp4 \"",
			L"-c:v mpeg4 -q:v 6 -movflags +faststart -f mp4 \"" };
		bool made = false;
		for (const CString& enc : encoders)
		{
			if (!RunCapture(base + enc + tmp + L"\"", cancel, out, code, canceled))
			{
				r.error = L"ffmpeg.exe 를 실행하지 못했습니다.";
				return r;
			}
			if (canceled)
			{
				::DeleteFileW(tmp);
				r.canceled = true;
				return r;
			}
			if (code == 0 && ::PathFileExistsW(tmp))
			{
				made = true;
				break;
			}
		}
		if (!made)
		{
			::DeleteFileW(tmp);
			r.error = L"미리보기를 만들지 못했습니다.\n" + LastLines(out);
			return r;
		}
		// 다 만들면 바꿔치기 (재생 중이라 잠겨 있으면 잠깐 기다렸다 다시)
		for (int tries = 0; tries < 20; ++tries)
		{
			if (::MoveFileExW(tmp, outFile, MOVEFILE_REPLACE_EXISTING))
			{
				r.ok = true;
				return r;
			}
			::Sleep(100);
		}
		::DeleteFileW(tmp);
		r.error = L"미리보기 파일을 저장하지 못했습니다. (파일이 사용 중일 수 있습니다)";
		return r;
	}
}

// ===========================================================================
// MFPlay 콜백 (재생 창이 먼저 사라지면 m_owner 를 비움)

class CPreviewPlayerCallback : public IMFPMediaPlayerCallback
{
public:
	explicit CPreviewPlayerCallback(CPreviewPlayer* owner) : m_owner(owner) {}
	void Detach() { m_owner = nullptr; }

	STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
	{
		if (!ppv)
			return E_POINTER;
		if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFPMediaPlayerCallback))
		{
			*ppv = static_cast<IMFPMediaPlayerCallback*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef() override { return ::InterlockedIncrement(&m_ref); }
	STDMETHODIMP_(ULONG) Release() override
	{
		const ULONG n = ::InterlockedDecrement(&m_ref);
		if (n == 0)
			delete this;
		return n;
	}
	void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* e) override
	{
		if (e && m_owner && SUCCEEDED(e->hrEvent))
			m_owner->OnPlayerEvent(static_cast<int>(e->eEventType));
	}

private:
	virtual ~CPreviewPlayerCallback() = default;
	LONG m_ref = 1;
	CPreviewPlayer* m_owner = nullptr;
};

BEGIN_MESSAGE_MAP(CPreviewPlayer, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_NCHITTEST()
END_MESSAGE_MAP()

CPreviewPlayer::~CPreviewPlayer()
{
	Stop();
}

bool CPreviewPlayer::Start(CWnd* parent, const CString& file, COLORREF border)
{
	Stop();
	if (!parent || !parent->GetSafeHwnd() || file.IsEmpty() || !::PathFileExistsW(file))
		return false;
	m_border = border;
	CRect rc;
	parent->GetClientRect(&rc);
	const CString cls = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW, ::LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
	if (!CreateEx(0, cls, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, rc, parent, 0))
		return false;
	m_file = file;
	m_cb = new CPreviewPlayerCallback(this);
	const HRESULT hr = ::MFPCreateMediaPlayer(file, FALSE, MFP_OPTION_NONE, m_cb, GetSafeHwnd(), &m_player);
	if (FAILED(hr) || !m_player)
	{
		Stop();
		return false;
	}
	return true;
}

bool CPreviewPlayer::StartPopup(CWnd* owner, const CRect& anchorScreen, int width, const CString& file, COLORREF border)
{
	Stop();
	if (!owner || !owner->GetSafeHwnd() || file.IsEmpty() || !::PathFileExistsW(file))
		return false;
	m_border = border;
	m_popup = true;
	m_anchor = anchorScreen;
	m_popupW = width;   // 0 이하 = anchor 와 같은 크기 (영상은 비율 유지, 남는 곳은 테두리 색)
	// 숨긴 채 만들고, 영상을 열면(MEDIAITEM_SET) 크기 · 위치를 정해 보임
	CRect rc = anchorScreen;
	if (m_popupW > 0)
	{
		const int h = m_popupW * 9 / 16;
		rc = CRect(anchorScreen.CenterPoint().x - m_popupW / 2, anchorScreen.CenterPoint().y - h / 2,
			anchorScreen.CenterPoint().x - m_popupW / 2 + m_popupW, anchorScreen.CenterPoint().y - h / 2 + h);
	}
	const CString cls = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW, ::LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
	if (!CreateEx(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, L"", WS_POPUP | (m_popupW > 0 ? WS_BORDER : 0), rc, owner, 0))
	{
		m_popup = false;
		return false;
	}
	m_file = file;
	m_cb = new CPreviewPlayerCallback(this);
	const HRESULT hr = ::MFPCreateMediaPlayer(file, FALSE, MFP_OPTION_NONE, m_cb, GetSafeHwnd(), &m_player);
	if (FAILED(hr) || !m_player)
	{
		Stop();
		return false;
	}
	return true;
}

void CPreviewPlayer::PlacePopup()
{
	if (!m_popup || !GetSafeHwnd())
		return;
	// 영상 비율 (화면 비율 값이 있으면 그것, 없으면 픽셀 크기)
	int vw = 16, vh = 9;
	if (m_player)
	{
		SIZE video = {}, ar = {};
		if (SUCCEEDED(m_player->GetNativeVideoSize(&video, &ar)))
		{
			if (ar.cx > 0 && ar.cy > 0) { vw = ar.cx; vh = ar.cy; }
			else if (video.cx > 0 && video.cy > 0) { vw = video.cx; vh = video.cy; }
		}
	}
	int w = m_popupW;
	int h = (w > 0) ? ::MulDiv(w, vh, vw) : 0;
	if (w <= 0)
	{
		// anchor(카드 이미지 영역)와 같은 크기 - 영상은 그 안에 비율 유지
		w = m_anchor.Width();
		h = m_anchor.Height();
		vw = (std::max)(1, w);
		vh = (std::max)(1, h);
	}
	// 화면(작업 영역) 안으로
	MONITORINFO mi = { sizeof(mi) };
	CRect work(0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN));
	if (::GetMonitorInfoW(::MonitorFromRect(&m_anchor, MONITOR_DEFAULTTONEAREST), &mi))
		work = mi.rcWork;
	if (h > work.Height())
	{
		h = work.Height();
		w = ::MulDiv(h, vw, vh);
	}
	const CPoint c = m_anchor.CenterPoint();
	int x = c.x - w / 2, y = c.y - h / 2;
	x = (std::max)(static_cast<int>(work.left), (std::min)(x, static_cast<int>(work.right) - w));
	y = (std::max)(static_cast<int>(work.top), (std::min)(y, static_cast<int>(work.bottom) - h));
	SetWindowPos(&CWnd::wndTop, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void CPreviewPlayer::Stop()
{
	if (m_cb)
		m_cb->Detach();
	if (m_player)
	{
		m_player->Shutdown();
		m_player->Release();
		m_player = nullptr;
	}
	if (m_cb)
	{
		m_cb->Release();
		m_cb = nullptr;
	}
	m_file.Empty();
	if (GetSafeHwnd())
		DestroyWindow();
	m_popup = false;
}

void CPreviewPlayer::MoveToAnchor(const CRect& anchorScreen)
{
	if (!m_popup || !GetSafeHwnd() || anchorScreen == m_anchor)
		return;
	m_anchor = anchorScreen;
	if (IsWindowVisible())
		PlacePopup();   // 아직 안 보이면(영상 여는 중) 보일 때 새 자리로
}

void CPreviewPlayer::FitToParent()
{
	if (!GetSafeHwnd() || m_popup)
		return;
	CWnd* parent = GetParent();
	if (!parent)
		return;
	CRect want, cur;
	parent->GetClientRect(&want);
	GetWindowRect(&cur);
	parent->ScreenToClient(&cur);
	if (cur != want)
		MoveWindow(want, TRUE);
}

void CPreviewPlayer::OnPlayerEvent(int type)
{
	if (!m_player)
		return;
	if (type == MFP_EVENT_TYPE_MEDIAITEM_SET)
	{
		// 소리 끄고, 비율 유지 (남는 곳은 테두리 색), 바로 재생
		m_player->SetMute(TRUE);
		m_player->SetAspectRatioMode(MFVideoARMode_PreservePicture);
		m_player->SetBorderColor(m_border);
		PlacePopup();   // 팝업: 영상 비율로 크기를 정하고 보임
		m_player->Play();
	}
	else if (type == MFP_EVENT_TYPE_PLAYBACK_ENDED)
	{
		// 끝나면 처음부터 다시 (반복)
		PROPVARIANT pos;
		::PropVariantInit(&pos);
		pos.vt = VT_I8;
		pos.hVal.QuadPart = 0;
		m_player->SetPosition(MFP_POSITIONTYPE_100NS, &pos);
		m_player->Play();
	}
}

void CPreviewPlayer::OnPaint()
{
	CPaintDC dc(this);
	if (m_player)
	{
		m_player->UpdateVideo();
		return;
	}
	CRect rc;
	GetClientRect(&rc);
	dc.FillSolidRect(rc, m_border);
}

BOOL CPreviewPlayer::OnEraseBkgnd(CDC* pDC)
{
	if (!m_player)
	{
		CRect rc;
		GetClientRect(&rc);
		pDC->FillSolidRect(rc, m_border);
	}
	return TRUE;
}

void CPreviewPlayer::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	if (m_player)
		m_player->UpdateVideo();
}

LRESULT CPreviewPlayer::OnNcHitTest(CPoint /*point*/)
{
	return HTTRANSPARENT;   // 마우스는 아래(부모 - 커버 이미지 칸)로
}
