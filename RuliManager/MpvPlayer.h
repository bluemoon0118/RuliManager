#pragma once

#include <functional>

// ---------------------------------------------------------------------------
// libmpv 래퍼 (내장 플레이어)
//  - libmpv-2.dll 은 실행 파일 폴더에 둠 (없으면 내장 플레이어를 쓰지 않고 기본 플레이어로 엶)
//  - DLL 은 처음 쓸 때 LoadLibrary 로 읽음 (프로그램은 DLL 없이도 실행됨 - 헤더 · lib 없이 필요한 함수만 GetProcAddress)
//  - 재생 창: Create 에 넘긴 창(hostWnd) 안에 mpv 가 자식 창을 만들어 그림
//  - mpv 이벤트: mpv 스레드가 notifyWnd 로 msg 를 보냄 → 그 창이 ProcessEvents 호출 (UI 스레드에서 상태 갱신)
class CMpvPlayer
{
public:
	CMpvPlayer() = default;
	~CMpvPlayer() { Destroy(); }
	CMpvPlayer(const CMpvPlayer&) = delete;
	CMpvPlayer& operator=(const CMpvPlayer&) = delete;

	// DLL 을 읽을 수 있는지 (한 번 읽으면 계속 씀). dllPath: 찾은 DLL 경로 / 못 찾으면 찾아본 경로
	static bool Available(CString* dllPath = nullptr);
	static CString DllFolder();   // DLL 을 두는 폴더 (실행 파일 폴더)
	static bool DllExists(CString* dllPath = nullptr);   // 파일만 있는지 (읽지는 않음)

	bool Create(HWND hostWnd, HWND notifyWnd, UINT msg, bool hwdec = true, bool subAuto = true);   // hwdec: 하드웨어 디코딩, subAuto: 같은 이름 자막 자동
	void Destroy();
	bool IsCreated() const { return m_h != nullptr; }

	bool Load(const CString& path, double startSec, bool loop = false, bool autoPlay = true);   // startSec > 0 이면 그 위치부터, loop = 끝나면 처음부터 반복, autoPlay = 열자마자 재생 (false = 멈춘 채로)
	void Stop();
	void TogglePause();
	void SetPause(bool pause);
	void SeekAbs(double sec, bool exact);
	void SeekRel(double sec);
	void FrameStep(bool back);
	void SetVolume(double vol);   // 0 ~ 130
	void SetMute(bool mute);
	void SetSpeed(double speed);

	void ProcessEvents();   // notify 메시지를 받으면 호출

	// 상태 (ProcessEvents 가 갱신)
	double pos = 0.0, dur = 0.0, volume = 100.0, speed = 1.0;
	bool   paused = false, mute = false, loaded = false, eof = false;
	int    videoW = 0, videoH = 0;

	std::function<void()> onChanged;      // 위치 · 길이 · 일시정지 등이 바뀜 (다시 그리기)
	std::function<void()> onFileLoaded;   // 파일을 열었음
	std::function<void()> onVideoReady;   // 영상 창이 만들어짐 (마우스를 받지 않게 끔 - 호스트 창이 받음)
	std::function<void()> onEof;          // 끝까지 재생함 (keep-open: 마지막 장면에서 멈춤)
	std::function<void(int reason)> onEndFile;   // 0 = 끝까지 재생, 2 = 멈춤, 4 = 오류

private:
	void* m_h = nullptr;   // mpv_handle*
	bool Command(std::initializer_list<const char*> args);
	void SetPropString(const char* name, const char* value);
	void SetPropDouble(const char* name, double v);
	void SetPropFlag(const char* name, bool v);
};
