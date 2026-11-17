#include "pch.h"
#include "MpvPlayer.h"
#include <atomic>
#include <cstdint>
#include <cstdio>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// ---------------------------------------------------------------------------
// libmpv client API (client.h 의 필요한 부분만 - DLL 에서 이름으로 찾음)
namespace
{
	enum MpvFormat { FMT_NONE = 0, FMT_STRING = 1, FMT_FLAG = 3, FMT_INT64 = 4, FMT_DOUBLE = 5 };
	enum MpvEventId
	{
		EV_NONE = 0, EV_SHUTDOWN = 1, EV_START_FILE = 6, EV_END_FILE = 7, EV_FILE_LOADED = 8,
		EV_VIDEO_RECONFIG = 17, EV_PLAYBACK_RESTART = 21, EV_PROPERTY_CHANGE = 22,
	};
	struct MpvEvent { int event_id; int error; uint64_t reply_userdata; void* data; };
	struct MpvEventProperty { const char* name; int format; void* data; };
	struct MpvEventEndFile { int reason; int error; };

	typedef void* (__cdecl* PFN_create)();
	typedef int   (__cdecl* PFN_initialize)(void*);
	typedef void  (__cdecl* PFN_terminate_destroy)(void*);
	typedef int   (__cdecl* PFN_set_option_string)(void*, const char*, const char*);
	typedef int   (__cdecl* PFN_set_option)(void*, const char*, int, void*);
	typedef int   (__cdecl* PFN_command)(void*, const char**);
	typedef int   (__cdecl* PFN_set_property)(void*, const char*, int, void*);
	typedef int   (__cdecl* PFN_set_property_string)(void*, const char*, const char*);
	typedef int   (__cdecl* PFN_observe_property)(void*, uint64_t, const char*, int);
	typedef MpvEvent* (__cdecl* PFN_wait_event)(void*, double);
	typedef void  (__cdecl* PFN_set_wakeup_callback)(void*, void (__cdecl*)(void*), void*);

	struct MpvApi
	{
		HMODULE dll = nullptr;
		bool tried = false;
		CString path;
		PFN_create create = nullptr;
		PFN_initialize initialize = nullptr;
		PFN_terminate_destroy terminate_destroy = nullptr;
		PFN_set_option_string set_option_string = nullptr;
		PFN_set_option set_option = nullptr;
		PFN_command command = nullptr;
		PFN_set_property set_property = nullptr;
		PFN_set_property_string set_property_string = nullptr;
		PFN_observe_property observe_property = nullptr;
		PFN_wait_event wait_event = nullptr;
		PFN_set_wakeup_callback set_wakeup_callback = nullptr;
		bool ok() const { return dll && create && initialize && terminate_destroy && set_option_string && set_option
			&& command && set_property && set_property_string && observe_property && wait_event && set_wakeup_callback; }
	};
	MpvApi g_mpv;

	bool LoadApi()
	{
		if (g_mpv.tried)
			return g_mpv.ok();
		g_mpv.tried = true;
		const CString folder = CMpvPlayer::DllFolder();
		const wchar_t* names[] = { L"libmpv-2.dll", L"mpv-2.dll" };   // 배포판마다 이름이 다름
		for (const wchar_t* n : names)
		{
			CString p = folder + n;
			if (!::PathFileExistsW(p))
				continue;
			// DLL 이 쓰는 다른 DLL 도 같은 폴더에서 찾게
			g_mpv.dll = ::LoadLibraryExW(p, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
			if (g_mpv.dll)
			{
				g_mpv.path = p;
				break;
			}
		}
		if (!g_mpv.dll)
		{
			g_mpv.path = folder + L"libmpv-2.dll";
			return false;
		}
		auto get = [](const char* name) { return ::GetProcAddress(g_mpv.dll, name); };
		g_mpv.create = reinterpret_cast<PFN_create>(get("mpv_create"));
		g_mpv.initialize = reinterpret_cast<PFN_initialize>(get("mpv_initialize"));
		g_mpv.terminate_destroy = reinterpret_cast<PFN_terminate_destroy>(get("mpv_terminate_destroy"));
		g_mpv.set_option_string = reinterpret_cast<PFN_set_option_string>(get("mpv_set_option_string"));
		g_mpv.set_option = reinterpret_cast<PFN_set_option>(get("mpv_set_option"));
		g_mpv.command = reinterpret_cast<PFN_command>(get("mpv_command"));
		g_mpv.set_property = reinterpret_cast<PFN_set_property>(get("mpv_set_property"));
		g_mpv.set_property_string = reinterpret_cast<PFN_set_property_string>(get("mpv_set_property_string"));
		g_mpv.observe_property = reinterpret_cast<PFN_observe_property>(get("mpv_observe_property"));
		g_mpv.wait_event = reinterpret_cast<PFN_wait_event>(get("mpv_wait_event"));
		g_mpv.set_wakeup_callback = reinterpret_cast<PFN_set_wakeup_callback>(get("mpv_set_wakeup_callback"));
		return g_mpv.ok();
	}

	// mpv 스레드 → UI 창 알림 (보내 놓은 알림이 아직 처리 안 됐으면 또 보내지 않음)
	struct WakeTarget
	{
		HWND hwnd = nullptr;
		UINT msg = 0;
		std::atomic<bool> pending{ false };
	};
	void __cdecl OnWakeup(void* d)
	{
		WakeTarget* t = static_cast<WakeTarget*>(d);
		if (t && t->hwnd && !t->pending.exchange(true))
			::PostMessageW(t->hwnd, t->msg, 0, 0);
	}
	std::map<void*, WakeTarget*> g_wake;   // mpv 핸들 → 알림 대상 (Destroy 에서 지움)

	enum ObserveId { OB_TIME = 1, OB_DUR, OB_PAUSE, OB_VOL, OB_MUTE, OB_SPEED, OB_EOF, OB_DW, OB_DH };

	CStringA Utf8(const CString& s)
	{
		return CStringA(CW2A(s, CP_UTF8));
	}
}

CString CMpvPlayer::DllFolder()
{
	wchar_t exe[MAX_PATH] = {};
	::GetModuleFileNameW(nullptr, exe, MAX_PATH);
	CString folder = exe;
	const int slash = folder.ReverseFind(L'\\');
	if (slash >= 0)
		folder = folder.Left(slash + 1);
	return folder;
}

bool CMpvPlayer::DllExists(CString* dllPath)
{
	const CString folder = DllFolder();
	const wchar_t* names[] = { L"libmpv-2.dll", L"mpv-2.dll" };
	for (const wchar_t* n : names)
		if (::PathFileExistsW(folder + n))
		{
			if (dllPath) *dllPath = folder + n;
			return true;
		}
	if (dllPath) *dllPath = folder + L"libmpv-2.dll";
	return false;
}

bool CMpvPlayer::Available(CString* dllPath)
{
	const bool ok = LoadApi();
	if (dllPath)
		*dllPath = g_mpv.path;
	return ok;
}

bool CMpvPlayer::Create(HWND hostWnd, HWND notifyWnd, UINT msg, bool hwdec, bool subAuto)
{
	if (m_h)
		return true;
	if (!LoadApi())
		return false;
	void* h = g_mpv.create();
	if (!h)
		return false;
	int64_t wid = static_cast<int64_t>(reinterpret_cast<intptr_t>(hostWnd));
	g_mpv.set_option(h, "wid", FMT_INT64, &wid);
	// 설정 파일 · 스크립트 · 기본 단축키 · 화면 위 컨트롤(OSC)은 쓰지 않음 - 컨트롤 · 단축키는 프로그램이 처리
	const char* opts[][2] = {
		{ "config", "no" }, { "load-scripts", "no" }, { "osc", "no" }, { "ytdl", "no" },
		{ "input-default-bindings", "no" }, { "input-vo-keyboard", "no" }, { "input-cursor", "no" },
		{ "cursor-autohide", "no" }, { "osd-level", "0" }, { "osd-bar", "no" },
		{ "keep-open", "yes" }, { "idle", "yes" }, { "hwdec", hwdec ? "auto-safe" : "no" },
		{ "sub-auto", subAuto ? "exact" : "no" },
		{ "terminal", "no" }, { "audio-display", "no" }, { "save-position-on-quit", "no" },
	};
	for (auto& o : opts)
		g_mpv.set_option_string(h, o[0], o[1]);
	if (g_mpv.initialize(h) < 0)
	{
		g_mpv.terminate_destroy(h);
		return false;
	}
	m_h = h;
	g_mpv.observe_property(h, OB_TIME, "time-pos", FMT_DOUBLE);
	g_mpv.observe_property(h, OB_DUR, "duration", FMT_DOUBLE);
	g_mpv.observe_property(h, OB_PAUSE, "pause", FMT_FLAG);
	g_mpv.observe_property(h, OB_VOL, "volume", FMT_DOUBLE);
	g_mpv.observe_property(h, OB_MUTE, "mute", FMT_FLAG);
	g_mpv.observe_property(h, OB_SPEED, "speed", FMT_DOUBLE);
	g_mpv.observe_property(h, OB_EOF, "eof-reached", FMT_FLAG);
	g_mpv.observe_property(h, OB_DW, "dwidth", FMT_INT64);
	g_mpv.observe_property(h, OB_DH, "dheight", FMT_INT64);
	WakeTarget* t = new WakeTarget;
	t->hwnd = notifyWnd;
	t->msg = msg;
	g_wake[h] = t;
	g_mpv.set_wakeup_callback(h, &OnWakeup, t);
	return true;
}

void CMpvPlayer::Destroy()
{
	if (!m_h)
		return;
	void* h = m_h;
	m_h = nullptr;
	g_mpv.set_wakeup_callback(h, nullptr, nullptr);
	g_mpv.terminate_destroy(h);   // 재생 스레드를 끝낼 때까지 잠깐 기다림
	auto it = g_wake.find(h);
	if (it != g_wake.end())
	{
		delete it->second;
		g_wake.erase(it);
	}
	loaded = false;
	pos = dur = 0.0;
}

bool CMpvPlayer::Command(std::initializer_list<const char*> args)
{
	if (!m_h)
		return false;
	std::vector<const char*> v(args);
	v.push_back(nullptr);
	return g_mpv.command(m_h, v.data()) >= 0;
}

void CMpvPlayer::SetPropString(const char* name, const char* value)
{
	if (m_h)
		g_mpv.set_property_string(m_h, name, value);
}

void CMpvPlayer::SetPropDouble(const char* name, double v)
{
	if (m_h)
		g_mpv.set_property(m_h, name, FMT_DOUBLE, &v);
}

void CMpvPlayer::SetPropFlag(const char* name, bool v)
{
	int f = v ? 1 : 0;
	if (m_h)
		g_mpv.set_property(m_h, name, FMT_FLAG, &f);
}

bool CMpvPlayer::Load(const CString& path, double startSec, bool loop, bool autoPlay)
{
	if (!m_h)
		return false;
	loaded = false;
	eof = false;
	pos = dur = 0.0;
	char start[32] = "none";
	if (startSec > 0.5)
		sprintf_s(start, "%.3f", startSec);
	SetPropString("start", start);   // 파일을 열 때 적용 (열고 나면 none 으로 되돌림 - FILE_LOADED)
	SetPropFlag("pause", !autoPlay);   // 멈춘 채로 열어도 첫 장면(또는 이어 볼 위치)은 보임
	SetPropString("loop-file", loop ? "inf" : "no");
	const CStringA p = Utf8(path);
	return Command({ "loadfile", p.GetString(), "replace" });
}

void CMpvPlayer::Stop()
{
	Command({ "stop" });
	loaded = false;
}

void CMpvPlayer::TogglePause()
{
	if (eof && paused)
	{
		SeekAbs(0.0, true);   // 끝까지 본 뒤 재생: 처음부터
		SetPause(false);
		return;
	}
	SetPause(!paused);
}

void CMpvPlayer::SetPause(bool pause)
{
	SetPropFlag("pause", pause);
}

void CMpvPlayer::SeekAbs(double sec, bool exact)
{
	if (!loaded)
		return;
	char t[32];
	sprintf_s(t, "%.3f", (std::max)(0.0, sec));
	Command({ "seek", t, exact ? "absolute+exact" : "absolute+keyframes" });
}

void CMpvPlayer::SeekRel(double sec)
{
	if (!loaded)
		return;
	char t[32];
	sprintf_s(t, "%.3f", sec);
	Command({ "seek", t, "relative+exact" });
}

void CMpvPlayer::FrameStep(bool back)
{
	if (!loaded)
		return;
	Command({ back ? "frame-back-step" : "frame-step" });
}

void CMpvPlayer::SetVolume(double vol)
{
	SetPropDouble("volume", (std::max)(0.0, (std::min)(130.0, vol)));
}

void CMpvPlayer::SetMute(bool m)
{
	SetPropFlag("mute", m);
}

void CMpvPlayer::SetSpeed(double s)
{
	SetPropDouble("speed", (std::max)(0.1, (std::min)(4.0, s)));
}

void CMpvPlayer::ProcessEvents()
{
	if (!m_h)
		return;
	auto it = g_wake.find(m_h);
	if (it != g_wake.end())
		it->second->pending = false;   // 이제부터 오는 알림은 다시 보냄
	bool changed = false;
	for (int guard = 0; guard < 1000 && m_h; ++guard)
	{
		MpvEvent* e = g_mpv.wait_event(m_h, 0.0);
		if (!e || e->event_id == EV_NONE)
			break;
		switch (e->event_id)
		{
		case EV_PROPERTY_CHANGE:
		{
			const MpvEventProperty* p = static_cast<const MpvEventProperty*>(e->data);
			if (!p)
				break;
			const bool has = (p->data != nullptr && p->format != FMT_NONE);
			auto dbl = [&]() { return (has && p->format == FMT_DOUBLE) ? *static_cast<double*>(p->data) : 0.0; };
			auto flg = [&]() { return (has && p->format == FMT_FLAG) ? (*static_cast<int*>(p->data) != 0) : false; };
			auto i64 = [&]() { return (has && p->format == FMT_INT64) ? static_cast<int>(*static_cast<int64_t*>(p->data)) : 0; };
			switch (e->reply_userdata)
			{
			case OB_TIME:  pos = dbl(); break;
			case OB_DUR:   dur = dbl(); break;
			case OB_PAUSE: paused = flg(); break;
			case OB_VOL:   if (has) volume = dbl(); break;
			case OB_MUTE:  mute = flg(); break;
			case OB_SPEED: if (has) speed = dbl(); break;
			case OB_EOF:
			{
				const bool was = eof;
				eof = flg();
				if (eof && !was && loaded && onEof)
					onEof();
				break;
			}
			case OB_DW:    videoW = i64(); break;
			case OB_DH:    videoH = i64(); break;
			}
			changed = true;
			break;
		}
		case EV_FILE_LOADED:
			loaded = true;
			eof = false;
			SetPropString("start", "none");
			if (onFileLoaded) onFileLoaded();
			changed = true;
			break;
		case EV_VIDEO_RECONFIG:
			if (onVideoReady) onVideoReady();
			break;
		case EV_END_FILE:
		{
			const MpvEventEndFile* ef = static_cast<const MpvEventEndFile*>(e->data);
			const int reason = ef ? ef->reason : 0;
			if (reason != 0)
				loaded = false;   // 끝까지 재생(keep-open)은 마지막 장면에 머무름
			if (onEndFile) onEndFile(reason);
			changed = true;
			break;
		}
		case EV_SHUTDOWN:
			loaded = false;
			changed = true;
			break;
		default:
			break;
		}
	}
	if (changed && onChanged)
		onChanged();
}
