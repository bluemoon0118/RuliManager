#include "pch.h"
#include "WallRenderer.h"
#include <d3d10.h>      // ID3D10Multithread
#include <d2d1_1helper.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

using Microsoft::WRL::ComPtr;

namespace
{
	ULONGLONG NowMs() { return ::GetTickCount64(); }

	void EnsureMfStarted()
	{
		// MFStartup 은 참조 횟수 방식 - 한 번만 부르고 끝날 때까지 유지 (MediaInfoLabel 과 같음)
		static std::once_flag once;
		std::call_once(once, []() { ::MFStartup(MF_VERSION, MFSTARTUP_LITE); });
	}
}

// ---------------------------------------------------------------------------
// 칸 하나: 미리보기 파일 하나를 비동기 소스 리더로 읽어 칸 텍스처에 복사
//  - 리더 호출(열기 · 읽기 요청 · 처음으로 · 비우기 · 해제)은 모두 렌더 스레드에서
//  - OnReadSample(MF 작업 스레드)은 프레임을 칸 텍스처로 복사하고 상태 표시만 바꿈
// ---------------------------------------------------------------------------
class CWallTile : public IMFSourceReaderCallback
{
public:
	CWallTile(const CString& file, ID3D11Device* dev, ID3D11DeviceContext* ctx, ID2D1DeviceContext* dc, ID2D1Multithread* lock)
		: m_file(file), m_dev(dev), m_ctx(ctx), m_dc(dc), m_lock(lock) {}

	// IUnknown
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
	{
		if (!ppv)
			return E_POINTER;
		if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFSourceReaderCallback))
		{
			*ppv = static_cast<IMFSourceReaderCallback*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(::InterlockedIncrement(&m_ref)); }
	STDMETHODIMP_(ULONG) Release() override
	{
		const LONG r = ::InterlockedDecrement(&m_ref);
		if (r == 0)
			delete this;
		return static_cast<ULONG>(r);
	}

	// IMFSourceReaderCallback (MF 작업 스레드)
	STDMETHODIMP OnReadSample(HRESULT hr, DWORD /*stream*/, DWORD flags, LONGLONG ts, IMFSample* sample) override
	{
		if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR))
			m_error = true;
		if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
			m_eos = true;
		if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
			m_typeChanged = true;
		if (sample && !m_closing && !m_error)
			CopyFrame(sample, ts);
		m_pending = false;   // 마지막에 (렌더 스레드가 이것을 보고 다음 요청 · 해제)
		return S_OK;
	}
	STDMETHODIMP OnFlush(DWORD) override { m_pending = false; return S_OK; }   // 비우기 끝: 나가 있던 요청은 취소됨
	STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*) override { return S_OK; }

	// ---- 렌더 스레드 ----
	bool Open(IMFDXGIDeviceManager* mgr, bool hw)
	{
		ComPtr<IMFAttributes> attrs;
		if (FAILED(::MFCreateAttributes(&attrs, 4)))
			return false;
		attrs->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, static_cast<IMFSourceReaderCallback*>(this));
		if (hw && mgr)
		{
			attrs->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, mgr);
			attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
			attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);   // 디코더 출력 → RGB32 (GPU)
		}
		else
			attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);            // CPU 변환
		ComPtr<IMFSourceReader> reader;
		if (FAILED(::MFCreateSourceReaderFromURL(m_file, attrs.Get(), &reader)))
			return false;
		reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
		reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
		ComPtr<IMFMediaType> type;
		if (FAILED(::MFCreateMediaType(&type)))
			return false;
		type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
		if (FAILED(reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type.Get())))
			return false;
		m_reader = reader;
		m_hw = hw && mgr;
		return ReadType();
	}

	bool ReadType()
	{
		// 지금 출력 형식의 크기 · 줄 간격 (처음 + 형식이 바뀌었을 때)
		ComPtr<IMFMediaType> cur;
		if (!m_reader || FAILED(m_reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &cur)))
			return false;
		UINT32 w = 0, h = 0;
		if (FAILED(::MFGetAttributeSize(cur.Get(), MF_MT_FRAME_SIZE, &w, &h)) || w == 0 || h == 0)
			return false;
		UINT32 stride = 0;
		LONG s = static_cast<LONG>(w * 4);
		if (SUCCEEDED(cur->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)))
			s = static_cast<LONG>(static_cast<INT32>(stride));
		m_lock->Enter();
		m_fw = w;
		m_fh = h;
		m_stride = s;
		m_lock->Leave();
		m_wantFormat = DXGI_FORMAT_B8G8R8X8_UNORM;
		m_needTexture = true;
		return true;
	}

	// 칸 텍스처 · D2D 비트맵 만들기 (렌더 스레드, 첫 프레임의 형식에 맞춤)
	void EnsureTexture()
	{
		if (!m_needTexture)
			return;
		m_needTexture = false;
		const DXGI_FORMAT want = m_wantFormat.load();
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = m_fw;
		td.Height = m_fh;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = want;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		ComPtr<ID3D11Texture2D> tex;
		ComPtr<ID2D1Bitmap1> bmp;
		if (SUCCEEDED(m_dev->CreateTexture2D(&td, nullptr, &tex)))
		{
			ComPtr<IDXGISurface> surf;
			if (SUCCEEDED(tex.As(&surf)))
			{
				const D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
					D2D1::PixelFormat(want, D2D1_ALPHA_MODE_IGNORE));   // 영상은 불투명
				if (FAILED(m_dc->CreateBitmapFromDxgiSurface(surf.Get(), &bp, &bmp)))
					bmp.Reset();
			}
		}
		m_lock->Enter();
		m_tex = bmp ? tex : nullptr;
		m_bmp = bmp;
		m_texFormat = bmp ? want : DXGI_FORMAT_UNKNOWN;
		m_hasFrame = false;
		m_lock->Leave();
		if (!bmp)
			m_error = true;
	}

	// 렌더 스레드: 재생 시계를 나아가게 하고, 때가 되면 다음 프레임 요청
	void Tick(double elapsedMs, bool playing)
	{
		if (!m_reader || m_closing || m_error || m_pending)
			return;
		if (m_typeChanged)
		{
			m_typeChanged = false;
			ReadType();
		}
		EnsureTexture();
		if (m_error)
			return;
		if (m_eos)
		{
			// 끝: 처음으로 (반복 재생)
			m_eos = false;
			PROPVARIANT pv;
			::PropVariantInit(&pv);
			pv.vt = VT_I8;
			pv.hVal.QuadPart = 0;
			m_reader->SetCurrentPosition(GUID_NULL, pv);
			::PropVariantClear(&pv);
			m_clockMs = 0;
			m_frameTs = -1;
		}
		if (!playing)
			return;
		if (m_frameTs >= 0)
			m_clockMs += elapsedMs;
		if (m_frameTs < 0 || static_cast<double>(m_frameTs) / 10000.0 <= m_clockMs)
		{
			m_pending = true;
			if (FAILED(m_reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, nullptr, nullptr, nullptr)))
			{
				m_pending = false;
				m_error = true;
			}
		}
	}

	void BeginClose()
	{
		if (m_closing)
			return;
		m_closing = true;
		m_closeTick = NowMs();
		if (m_reader && m_pending)
			m_reader->Flush(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS));   // 나가 있는 요청을 빨리 돌려받음
	}
	bool CanFree() const { return !m_pending || NowMs() - m_closeTick > 3000; }
	void FreeReader() { m_reader.Reset(); }   // 리더가 들고 있는 콜백 참조도 함께 풀림

	bool TakeNewFrame() { return m_newFrame.exchange(false); }
	bool HasFrame() const { return m_hasFrame; }
	bool Failed() const { return m_error; }
	const CString& File() const { return m_file; }
	ID2D1Bitmap1* Bitmap() const { return m_bmp.Get(); }
	UINT FrameW() const { return m_fw; }
	UINT FrameH() const { return m_fh; }

private:
	~CWallTile() = default;

	void CopyFrame(IMFSample* sample, LONGLONG ts)
	{
		ComPtr<IMFMediaBuffer> buf;
		if (FAILED(sample->GetBufferByIndex(0, &buf)))
			return;
		bool copied = false;
		ComPtr<IMFDXGIBuffer> dx;
		if (SUCCEEDED(buf.As(&dx)))
		{
			// GPU 프레임: 디코더(비디오 처리기) 텍스처 → 칸 텍스처, 바로 복사하고 샘플을 놓음
			ComPtr<ID3D11Texture2D> src;
			UINT sub = 0;
			if (SUCCEEDED(dx->GetResource(IID_PPV_ARGS(&src))) && SUCCEEDED(dx->GetSubresourceIndex(&sub)))
			{
				D3D11_TEXTURE2D_DESC sd = {};
				src->GetDesc(&sd);
				m_lock->Enter();
				if (m_tex && sd.Format == m_texFormat)
				{
					D3D11_BOX box = { 0, 0, 0, (std::min)(sd.Width, m_fw), (std::min)(sd.Height, m_fh), 1 };
					m_ctx->CopySubresourceRegion(m_tex.Get(), 0, 0, 0, 0, src.Get(), sub, &box);
					copied = true;
				}
				else if (sd.Format != m_texFormat && sd.Format != DXGI_FORMAT_UNKNOWN)
				{
					m_wantFormat = sd.Format;   // 출력 형식이 다름 (예: B8G8R8A8) → 렌더 스레드가 그 형식으로 다시 만듦
					m_needTexture = true;
				}
				m_lock->Leave();
			}
		}
		else
		{
			// CPU 프레임 (하드웨어 변환을 못 씀): RGB32 줄을 위에서부터 칸 텍스처로 올림
			ComPtr<IMF2DBuffer> b2;
			BYTE* p = nullptr;
			LONG pitch = 0;
			bool locked2d = false;
			DWORD len = 0;
			if (SUCCEEDED(buf.As(&b2)) && SUCCEEDED(b2->Lock2D(&p, &pitch)))
				locked2d = true;
			else if (SUCCEEDED(buf->Lock(&p, nullptr, &len)))
			{
				pitch = m_stride;
				if (pitch < 0)
					p += static_cast<size_t>(-pitch) * (m_fh - 1);   // 아래에서 위로 저장된 형식: 첫 줄 = 맨 아래 블록의 시작
			}
			else
				p = nullptr;
			if (p)
			{
				const UINT rowBytes = m_fw * 4;
				m_cpu.resize(static_cast<size_t>(rowBytes) * m_fh);
				for (UINT y = 0; y < m_fh; ++y)
					memcpy(&m_cpu[static_cast<size_t>(y) * rowBytes], p + static_cast<ptrdiff_t>(y) * pitch, rowBytes);
				if (locked2d)
					b2->Unlock2D();
				else
					buf->Unlock();
				m_lock->Enter();
				if (m_tex && m_texFormat == DXGI_FORMAT_B8G8R8X8_UNORM)
				{
					m_ctx->UpdateSubresource(m_tex.Get(), 0, nullptr, m_cpu.data(), rowBytes, 0);
					copied = true;
				}
				m_lock->Leave();
			}
		}
		if (copied)
		{
			m_frameTs = ts;
			m_hasFrame = true;
			m_newFrame = true;
		}
	}

	LONG m_ref = 1;
	CString m_file;
	ComPtr<ID3D11Device>        m_dev;
	ComPtr<ID3D11DeviceContext> m_ctx;
	ComPtr<ID2D1DeviceContext>  m_dc;
	ComPtr<ID2D1Multithread>    m_lock;
	ComPtr<IMFSourceReader>     m_reader;
	bool m_hw = false;

	// 텍스처 (m_lock 으로 보호: 렌더 스레드가 만들고, 콜백이 복사)
	ComPtr<ID3D11Texture2D> m_tex;
	ComPtr<ID2D1Bitmap1>    m_bmp;
	DXGI_FORMAT m_texFormat = DXGI_FORMAT_UNKNOWN;
	std::atomic<DXGI_FORMAT> m_wantFormat{ DXGI_FORMAT_B8G8R8X8_UNORM };
	std::atomic<bool> m_needTexture{ false };
	UINT m_fw = 0, m_fh = 0;
	LONG m_stride = 0;
	std::vector<BYTE> m_cpu;

	// 상태
	std::atomic<bool> m_pending{ false };
	std::atomic<bool> m_eos{ false };
	std::atomic<bool> m_error{ false };
	std::atomic<bool> m_typeChanged{ false };
	std::atomic<bool> m_closing{ false };
	std::atomic<bool> m_hasFrame{ false };
	std::atomic<bool> m_newFrame{ false };
	std::atomic<LONGLONG> m_frameTs{ -1 };   // 마지막으로 받은 프레임 시각 (100ns), -1 = 아직 없음
	double    m_clockMs = 0;                 // 이 칸의 재생 시계 (렌더 스레드)
	ULONGLONG m_closeTick = 0;
};

// ---------------------------------------------------------------------------

CWallRenderer::~CWallRenderer()
{
	Destroy();
}

bool CWallRenderer::Create(HWND grid, CString* error)
{
	Destroy();
	auto fail = [&](LPCWSTR what, HRESULT hr) -> bool
	{
		CString s;
		s.Format(L"%s 실패 (0x%08X)", what, static_cast<unsigned>(hr));
		if (error)
			*error = s;
		m_failReason = s;
		// 만든 것 정리
		m_dc.Reset(); m_d2dDev.Reset(); m_d2dLock.Reset(); m_d2dFactory.Reset();
		m_visual.Reset(); m_dtarget.Reset(); m_dcomp.Reset(); m_swap.Reset();
		m_mgr.Reset(); m_ctx.Reset(); m_dev.Reset();
		return false;
	};
	if (!::IsWindow(grid))
		return fail(L"격자 창 없음", E_HANDLE);
	m_failed = false;
	m_quit = false;

	// 1) D3D11 장치 (BGRA = D2D 용, VIDEO_SUPPORT = 하드웨어 디코딩 공유)
	static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
	HRESULT hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, _countof(levels), D3D11_SDK_VERSION, &m_dev, nullptr, &m_ctx);
	if (hr == E_INVALIDARG)   // 11_1 을 모르는 런타임
		hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels + 1, _countof(levels) - 1, D3D11_SDK_VERSION, &m_dev, nullptr, &m_ctx);
	m_hwDecode = SUCCEEDED(hr);
	if (FAILED(hr))
	{
		flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;   // 비디오 지원 없이 (CPU 디코딩)
		hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels + 1, _countof(levels) - 1, D3D11_SDK_VERSION, &m_dev, nullptr, &m_ctx);
	}
	if (FAILED(hr))
		return fail(L"D3D11 장치 만들기", hr);
	{
		ComPtr<ID3D10Multithread> mt;
		if (SUCCEEDED(m_dev.As(&mt)))
			mt->SetMultithreadProtected(TRUE);   // 디코더(MF 작업 스레드)와 렌더 스레드가 같은 장치를 씀
	}
	ComPtr<IDXGIDevice> dxgiDev;
	if (FAILED(hr = m_dev.As(&dxgiDev)))
		return fail(L"DXGI 장치", hr);

	// 2) Media Foundation 장치 관리자 (디코더가 같은 장치를 씀)
	EnsureMfStarted();
	if (m_hwDecode)
	{
		if (FAILED(hr = ::MFCreateDXGIDeviceManager(&m_mgrToken, &m_mgr)) || FAILED(hr = m_mgr->ResetDevice(m_dev.Get(), m_mgrToken)))
		{
			m_mgr.Reset();
			m_hwDecode = false;   // CPU 디코딩으로
		}
	}

	// 3) D2D (여러 스레드용 팩터리 - 디코드 콜백의 텍스처 복사와 같은 잠금을 씀)
	D2D1_FACTORY_OPTIONS fo = {};
	if (FAILED(hr = ::D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), &fo, reinterpret_cast<void**>(m_d2dFactory.GetAddressOf()))))
		return fail(L"D2D 팩터리", hr);
	if (FAILED(hr = m_d2dFactory.As(&m_d2dLock)))
		return fail(L"D2D 잠금", hr);
	if (FAILED(hr = m_d2dFactory->CreateDevice(dxgiDev.Get(), &m_d2dDev)))
		return fail(L"D2D 장치", hr);
	if (FAILED(hr = m_d2dDev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_dc)))
		return fail(L"D2D 컨텍스트", hr);
	m_dc->SetDpi(96.0f, 96.0f);   // 좌표 = 픽셀
	m_dc->SetUnitMode(D2D1_UNIT_MODE_PIXELS);

	// 4) 스왑체인 (합성용, 미리 곱한 알파 = 투명 레이어) + 격자 창 위 DirectComposition 레이어
	CRect rc;
	::GetClientRect(grid, &rc);
	m_w = static_cast<UINT>((std::max)(8, rc.Width()));
	m_h = static_cast<UINT>((std::max)(8, rc.Height()));
	ComPtr<IDXGIAdapter> adapter;
	ComPtr<IDXGIFactory2> factory;
	if (FAILED(hr = dxgiDev->GetAdapter(&adapter)) || FAILED(hr = adapter->GetParent(IID_PPV_ARGS(&factory))))
		return fail(L"DXGI 팩터리", hr);
	DXGI_SWAP_CHAIN_DESC1 sd = {};
	sd.Width = m_w;
	sd.Height = m_h;
	sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	sd.SampleDesc.Count = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = 2;
	sd.Scaling = DXGI_SCALING_STRETCH;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
	if (FAILED(hr = factory->CreateSwapChainForComposition(m_dev.Get(), &sd, nullptr, &m_swap)))
		return fail(L"스왑체인", hr);
	if (FAILED(hr = ::DCompositionCreateDevice(dxgiDev.Get(), IID_PPV_ARGS(&m_dcomp))))
		return fail(L"DirectComposition 장치", hr);
	if (FAILED(hr = m_dcomp->CreateTargetForHwnd(grid, TRUE, &m_dtarget)))   // TRUE = 격자의 GDI 그림 위
		return fail(L"DirectComposition 대상", hr);
	if (FAILED(hr = m_dcomp->CreateVisual(&m_visual)))
		return fail(L"DirectComposition 비주얼", hr);
	m_visual->SetContent(m_swap.Get());
	m_dtarget->SetRoot(m_visual.Get());
	if (!ResizeTarget(m_w, m_h))
		return fail(L"그리기 대상", E_FAIL);
	// 처음은 투명하게
	m_dc->BeginDraw();
	m_dc->Clear(D2D1::ColorF(0, 0, 0, 0));
	m_dc->EndDraw();
	m_swap->Present(0, 0);
	if (FAILED(hr = m_dcomp->Commit()))
		return fail(L"DirectComposition 적용", hr);
	m_layerEmpty = true;

	m_thread = std::thread([this]() { RenderLoop(); });
	return true;
}

bool CWallRenderer::ResizeTarget(UINT w, UINT h)
{
	// 그리기 대상(백버퍼)을 새 크기로 - 렌더 스레드 또는 만들 때
	m_d2dLock->Enter();
	m_dc->SetTarget(nullptr);
	m_targetBmp.Reset();
	HRESULT hr = S_OK;
	if (w != m_w || h != m_h)
		hr = m_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
	if (SUCCEEDED(hr))
	{
		m_w = w;
		m_h = h;
		ComPtr<IDXGISurface> surf;
		hr = m_swap->GetBuffer(0, IID_PPV_ARGS(&surf));
		if (SUCCEEDED(hr))
		{
			const D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
			hr = m_dc->CreateBitmapFromDxgiSurface(surf.Get(), &bp, &m_targetBmp);
			if (SUCCEEDED(hr))
				m_dc->SetTarget(m_targetBmp.Get());
		}
	}
	m_d2dLock->Leave();
	return SUCCEEDED(hr);
}

void CWallRenderer::Destroy()
{
	if (m_thread.joinable())
	{
		m_quit = true;
		m_wake.notify_all();
		m_thread.join();   // 스레드가 끝나면서 칸을 모두 닫음
	}
	if (m_dtarget)
	{
		m_dtarget->SetRoot(nullptr);
		if (m_dcomp)
			m_dcomp->Commit();
	}
	m_targetBmp.Reset();
	if (m_dc)
		m_dc->SetTarget(nullptr);
	m_dc.Reset(); m_d2dDev.Reset(); m_d2dLock.Reset(); m_d2dFactory.Reset();
	m_visual.Reset(); m_dtarget.Reset(); m_dcomp.Reset(); m_swap.Reset();
	m_mgr.Reset(); m_ctx.Reset(); m_dev.Reset();
	{
		std::lock_guard<std::mutex> g(m_lock);
		m_specs.clear();
		++m_specVer;
	}
	m_layerEmpty = true;
}

CString CWallRenderer::FailReason() const
{
	std::lock_guard<std::mutex> g(m_lock);
	return m_failReason;
}

void CWallRenderer::Fail(const CString& why)
{
	{
		std::lock_guard<std::mutex> g(m_lock);
		m_failReason = why;
	}
	m_failed = true;
}

void CWallRenderer::SetTiles(const std::vector<WallTileSpec>& tiles, const CSize& client)
{
	if (!m_thread.joinable())
		return;
	{
		std::lock_guard<std::mutex> g(m_lock);
		// 같으면 렌더 스레드를 깨우지 않음 (0.5초마다 같은 목록이 옴)
		bool same = (client == m_client && tiles.size() == m_specs.size());
		for (size_t i = 0; same && i < tiles.size(); ++i)
			same = tiles[i].item == m_specs[i].item && tiles[i].rc == m_specs[i].rc && tiles[i].file == m_specs[i].file;
		if (same)
			return;
		m_specs = tiles;
		m_client = client;
		++m_specVer;
	}
	m_wake.notify_all();
}

void CWallRenderer::SetPlaying(bool on)
{
	if (m_playing.exchange(on) != on)
		m_wake.notify_all();
}

void CWallRenderer::Clear()
{
	SetTiles({}, m_client);
}

void CWallRenderer::CloseAllTiles(DWORD waitMs)
{
	for (auto& kv : m_tiles)
	{
		kv.second->BeginClose();
		m_closing.push_back(kv.second);
	}
	m_tiles.clear();
	const ULONGLONG until = NowMs() + waitMs;
	while (!m_closing.empty())
	{
		for (auto it = m_closing.begin(); it != m_closing.end(); )
		{
			if ((*it)->CanFree() || NowMs() > until)
			{
				(*it)->FreeReader();
				(*it)->Release();
				it = m_closing.erase(it);
			}
			else
				++it;
		}
		if (!m_closing.empty())
			::Sleep(5);
	}
}

void CWallRenderer::RenderLoop()
{
	::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	unsigned seenVer = static_cast<unsigned>(-1);
	std::vector<WallTileSpec> specs;
	CSize client;
	ULONGLONG last = NowMs();
	unsigned drawnVer = static_cast<unsigned>(-1);
	std::map<int, std::pair<CString, ULONGLONG>> bad;   // 열지 못했거나 오류 난 칸 (항목 → 파일, 다시 시도할 시각)
	std::map<int, ULONGLONG> lastSeen;                   // 열린 칸이 마지막으로 보인 시각
	while (!m_quit)
	{
		{
			std::unique_lock<std::mutex> g(m_lock);
			m_wake.wait_for(g, std::chrono::milliseconds(33));
			if (m_quit)
				break;
			if (m_specVer != seenVer)
			{
				seenVer = m_specVer;
				specs = m_specs;
				client = m_client;
			}
		}
		if (m_failed)
			continue;
		const ULONGLONG now = NowMs();
		const ULONGLONG gap = now - last;
		const double elapsed = static_cast<double>(gap > 200 ? 200 : gap);   // 멈춰 있다 깨어나도 한꺼번에 건너뛰지 않음
		last = now;
		const bool playing = m_playing;
		bool dirty = false;

		// 크기
		if (client.cx > 0 && client.cy > 0 && (static_cast<UINT>(client.cx) != m_w || static_cast<UINT>(client.cy) != m_h))
		{
			if (!ResizeTarget(static_cast<UINT>(client.cx), static_cast<UINT>(client.cy)))
			{
				Fail(L"그리기 대상 크기 바꾸기 실패");
				continue;
			}
			dirty = true;
		}

		// 칸 맞추기
		//  - 화면 밖으로 나간 칸은 2초 동안 열어 둠 (휠을 빠르게 올렸다 내려도 다시 열지 않고 바로 보이게), 파일이 바뀌면 바로 닫음
		//  - 읽다가 오류가 난 칸 · 열지 못한 칸은 1.5초 뒤 다시 시도 (빠른 스크롤로 디코더를 열고 닫는 중이면 잠깐 실패할 수 있음)
		//  - 새 칸은 보이는 칸 수가 상한 안일 때 한 번에 4개까지 엶 (멈춘 동안은 열지 않음)
		std::map<int, const WallTileSpec*> want;
		for (const WallTileSpec& s : specs)
			want[s.item] = &s;
		int visibleOpen = 0;
		for (auto it = m_tiles.begin(); it != m_tiles.end(); )
		{
			auto w = want.find(it->first);
			bool close = false;
			if (it->second->Failed())
			{
				bad[it->first] = std::make_pair(it->second->File(), now + 1500);
				close = true;
			}
			else if (w != want.end())
			{
				if (w->second->file.CompareNoCase(it->second->File()) != 0)
					close = true;   // 미리보기를 다시 만듦 · 다른 파트
				else
				{
					lastSeen[it->first] = now;
					++visibleOpen;
				}
			}
			else
			{
				auto ls = lastSeen.find(it->first);
				if (ls == lastSeen.end() || now - ls->second > 2000)
					close = true;   // 2초 넘게 안 보임
			}
			if (close)
			{
				if (it->second->HasFrame() && w != want.end())
					dirty = true;
				it->second->BeginClose();
				m_closing.push_back(it->second);
				lastSeen.erase(it->first);
				it = m_tiles.erase(it);
			}
			else
				++it;
		}
		const int maxOpen = m_maxDecode;
		// 열려 있는 칸이 너무 많으면 (안 보이는 칸이 쌓임) 가장 오래 안 보인 칸부터 닫음
		while (static_cast<int>(m_tiles.size()) > maxOpen + 8)
		{
			auto victim = m_tiles.end();
			ULONGLONG oldest = ~0ULL;
			for (auto it = m_tiles.begin(); it != m_tiles.end(); ++it)
			{
				if (want.count(it->first))
					continue;
				auto ls = lastSeen.find(it->first);
				const ULONGLONG seen = (ls == lastSeen.end()) ? 0 : ls->second;
				if (seen < oldest)
				{
					oldest = seen;
					victim = it;
				}
			}
			if (victim == m_tiles.end())
				break;
			victim->second->BeginClose();
			m_closing.push_back(victim->second);
			lastSeen.erase(victim->first);
			m_tiles.erase(victim);
		}
		if (playing)
		{
			int opened = 0;
			for (const WallTileSpec& s : specs)
			{
				if (opened >= 4 || visibleOpen >= maxOpen)
					break;
				if (m_tiles.count(s.item))
					continue;
				auto b = bad.find(s.item);
				if (b != bad.end() && b->second.first.CompareNoCase(s.file) == 0 && now < b->second.second)
					continue;   // 조금 뒤 다시 시도
				CWallTile* t = new CWallTile(s.file, m_dev.Get(), m_ctx.Get(), m_dc.Get(), m_d2dLock.Get());
				bool ok = t->Open(m_mgr.Get(), m_hwDecode);
				if (!ok && m_hwDecode)
				{
					// 하드웨어 변환이 안 되는 파일 · 장치: CPU 디코딩으로 다시
					t->FreeReader();
					t->Release();
					t = new CWallTile(s.file, m_dev.Get(), m_ctx.Get(), m_dc.Get(), m_d2dLock.Get());
					ok = t->Open(nullptr, false);
				}
				if (!ok)
				{
					t->FreeReader();
					t->Release();
					t = nullptr;
				}
				if (t)
				{
					m_tiles[s.item] = t;
					lastSeen[s.item] = now;
					bad.erase(s.item);
					++visibleOpen;
				}
				else
					bad[s.item] = std::make_pair(s.file, now + 1500);   // 그동안 커버만 보임
				++opened;
			}
		}
		for (auto it = bad.begin(); it != bad.end(); )
		{
			if (want.count(it->first))
				++it;
			else
				it = bad.erase(it);   // 화면 밖으로 나감 → 다시 보이면 바로 다시 시도
		}

		// 읽기 요청 · 새 프레임
		for (auto& kv : m_tiles)
		{
			const bool visible = want.count(kv.first) > 0;
			kv.second->Tick(elapsed, playing && visible);   // 안 보이는 칸은 멈춰 둠 (마지막 프레임 유지)
			if (kv.second->TakeNewFrame() && visible)
				dirty = true;
		}

		// 닫는 중인 칸 해제
		for (auto it = m_closing.begin(); it != m_closing.end(); )
		{
			if ((*it)->CanFree())
			{
				(*it)->FreeReader();
				(*it)->Release();
				it = m_closing.erase(it);
			}
			else
				++it;
		}

		// 그릴 것: 새 프레임 · 칸 자리 바뀜 · 칸이 닫힘 (그 밖에는 Present 하지 않음)
		if (!dirty && drawnVer == seenVer)
			continue;
		drawnVer = seenVer;
		if (m_layerEmpty && !dirty)
		{
			bool anyFrame = false;
			for (const auto& kv : m_tiles)
				anyFrame = anyFrame || kv.second->HasFrame();
			if (!anyFrame)
				continue;   // 이미 비어 있고 그릴 프레임도 없음
		}

		// 그리기: 투명하게 비우고, 프레임이 있는 칸만 칸 비율에 맞춰 잘라서 (가운데 기준) 그림
		m_dc->BeginDraw();
		m_dc->Clear(D2D1::ColorF(0, 0, 0, 0));
		bool any = false;
		for (const WallTileSpec& s : specs)
		{
			auto it = m_tiles.find(s.item);
			if (it == m_tiles.end() || !it->second->HasFrame() || !it->second->Bitmap())
				continue;
			const CWallTile* t = it->second;
			const float fw = static_cast<float>(t->FrameW()), fh = static_cast<float>(t->FrameH());
			const float dw = static_cast<float>(s.rc.Width()), dh = static_cast<float>(s.rc.Height());
			if (fw <= 0 || fh <= 0 || dw <= 0 || dh <= 0)
				continue;
			const float scale = (std::max)(dw / fw, dh / fh);
			const float cw = dw / scale, ch = dh / scale;
			const D2D1_RECT_F src = D2D1::RectF((fw - cw) / 2, (fh - ch) / 2, (fw + cw) / 2, (fh + ch) / 2);
			const D2D1_RECT_F dst = D2D1::RectF(static_cast<float>(s.rc.left), static_cast<float>(s.rc.top), static_cast<float>(s.rc.right), static_cast<float>(s.rc.bottom));
			m_dc->DrawBitmap(t->Bitmap(), &dst, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR, &src, nullptr);
			any = true;
		}
		HRESULT hr = m_dc->EndDraw();
		if (SUCCEEDED(hr))
			hr = m_swap->Present(1, 0);
		if (FAILED(hr))
		{
			CString why;
			why.Format(L"그리기 실패 (0x%08X) - 장치 분실 등", static_cast<unsigned>(hr));
			Fail(why);
			continue;
		}
		m_layerEmpty = !any;
	}
	CloseAllTiles(1500);
	::CoUninitialize();
}
