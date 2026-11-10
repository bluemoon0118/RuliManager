#pragma once

// ---------------------------------------------------------------------------
// [월 모드] 통합 렌더러 (D3D11 + DirectComposition + Media Foundation 소스 리더)
//  - 칸마다 재생 창(CPreviewPlayer · MFPlay · EVR)을 띄우던 방식 대신
//    D3D11 장치 1개 + 격자 창 위의 투명 레이어(DirectComposition, 스왑체인 1개)에 모든 칸의 영상을 한 번에 그림
//  - 칸마다 비동기 IMFSourceReader (하드웨어 디코딩, 같은 장치 공유) → 칸 텍스처 1장 → D2D 비트맵
//  - 레이어는 투명이라 프레임이 없는 칸(아직 못 읽음 · 동시 디코딩 상한 밖)은 격자가 GDI 로 그린 커버가 그대로 보임
//    (선택 테두리 · 마우스 · 스크롤은 격자가 지금처럼 처리, 렌더러는 영상 프레임만)
//  - 스레드: UI = 칸 목록만 넘김(SetTiles) / 렌더 스레드 = 칸 열기 · 닫기 · 읽기 요청 · 그리기 (30Hz, 바뀐 게 있을 때만 Present)
//            / Media Foundation 작업 스레드 = 디코딩 결과를 칸 텍스처로 복사
//  - 장치 분실 등으로 실패하면 Failed() = true → 호출 쪽이 예전 방식(CPreviewPlayer)으로 돌아감
// ---------------------------------------------------------------------------

#include <d3d11.h>
#include <d2d1_1.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

struct WallTileSpec
{
	int     item = -1;   // 영상 항목 번호 (칸 구분용)
	CRect   rc;          // 격자 클라이언트 좌표의 그릴 자리
	CString file;        // 미리보기 파일
};

class CWallTile;

class CWallRenderer
{
public:
	CWallRenderer() = default;
	~CWallRenderer();
	CWallRenderer(const CWallRenderer&) = delete;
	CWallRenderer& operator=(const CWallRenderer&) = delete;

	bool Create(HWND grid, CString* error = nullptr);   // UI 스레드. 실패하면 false (예전 방식 사용)
	void Destroy();                                      // UI 스레드. 렌더 스레드를 끝내고 모두 해제
	bool Ready() const { return m_thread.joinable() && !m_failed; }
	bool Failed() const { return m_failed; }
	CString FailReason() const;

	void SetTiles(const std::vector<WallTileSpec>& tiles, const CSize& client);   // UI 스레드: 보이는 칸 (위 · 왼쪽 칸부터)
	void SetPlaying(bool on);                                                      // 재생 / 일시정지 (멈추면 읽기 요청을 안 함, 마지막 프레임은 그대로)
	void Clear();                                                                  // 칸 모두 닫고 레이어 비움
	void SetMaxDecode(int n) { m_maxDecode = (n < 1) ? 1 : n; }                    // 동시에 여는 칸 수 상한 (밖의 칸은 커버)

private:
	void RenderLoop();
	bool ResizeTarget(UINT w, UINT h);
	void Fail(const CString& why);
	void CloseAllTiles(DWORD waitMs);

	// 장치 (Create 에서 만들고 Destroy 에서 해제)
	Microsoft::WRL::ComPtr<ID3D11Device>          m_dev;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext>   m_ctx;
	Microsoft::WRL::ComPtr<IMFDXGIDeviceManager>  m_mgr;
	UINT                                          m_mgrToken = 0;
	bool                                          m_hwDecode = false;   // 장치를 디코딩에 쓸 수 있음 (VIDEO_SUPPORT)
	Microsoft::WRL::ComPtr<IDXGISwapChain1>       m_swap;
	Microsoft::WRL::ComPtr<IDCompositionDevice>   m_dcomp;
	Microsoft::WRL::ComPtr<IDCompositionTarget>   m_dtarget;
	Microsoft::WRL::ComPtr<IDCompositionVisual>   m_visual;
	Microsoft::WRL::ComPtr<ID2D1Factory1>         m_d2dFactory;
	Microsoft::WRL::ComPtr<ID2D1Multithread>      m_d2dLock;   // D2D · D3D 즉시 컨텍스트 공용 잠금 (디코드 콜백의 복사도 이것으로)
	Microsoft::WRL::ComPtr<ID2D1Device>           m_d2dDev;
	Microsoft::WRL::ComPtr<ID2D1DeviceContext>    m_dc;
	Microsoft::WRL::ComPtr<ID2D1Bitmap1>          m_targetBmp;
	UINT m_w = 0, m_h = 0;

	// 렌더 스레드만 만지는 것
	std::map<int, CWallTile*> m_tiles;      // 항목 → 칸 (참조 1개 보유)
	std::vector<CWallTile*>   m_closing;    // 닫는 중 (읽기 요청이 돌아오면 해제)
	bool m_layerEmpty = true;               // 마지막으로 투명하게 비웠음

	// UI ↔ 렌더 스레드 (m_lock)
	mutable std::mutex        m_lock;
	std::condition_variable   m_wake;
	std::vector<WallTileSpec> m_specs;
	CSize                     m_client;
	unsigned                  m_specVer = 0;
	CString                   m_failReason;

	std::thread       m_thread;
	std::atomic<bool> m_quit{ false };
	std::atomic<bool> m_playing{ true };
	std::atomic<bool> m_failed{ false };
	std::atomic<int>  m_maxDecode{ 24 };
};
