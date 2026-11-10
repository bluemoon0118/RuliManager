#pragma once

// ---------------------------------------------------------------------------
// 영상 장면 읽기 (Media Foundation 소스 리더, 동기) - 스크린샷 추출 · 커버 만들기 공용
//  - 한 개체는 한 스레드에서만 사용 (소스 리더 규칙)
//  - Grab(t): 앞쪽 키프레임으로 이동해 t 까지 디코딩, 마지막 장면 하나만 복사 (예전에는 디코딩한 장면마다 원본 크기로 복사)
//  - GrabNext(t): 지금 읽은 자리보다 조금 뒤(3초 이내)면 이동 없이 이어서 디코딩 (커버 만들기 창의 ← → · 놓을 때)
//  - GrabKeyframe(t): 이동한 뒤 첫 장면(키프레임) 한 장만 - 슬라이더 드래그용, 실제 시각은 out.ts
//  - Next(): 바로 다음 장면 한 장
// ---------------------------------------------------------------------------

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <vector>

// 한 장면 (BGRA, 위에서 아래로, 한 줄 = w * 4 바이트, 알파 255)
struct VideoFrame
{
	int      w = 0, h = 0;
	LONGLONG ts = 0;            // 장면 시각 (100ns)
	std::vector<BYTE> bgra;
};

class CFrameReader
{
public:
	static const LONGLONG kSecond = 10000000LL;   // 100ns 단위 1초

	bool Open(const CString& path);               // 호출 전에 이 스레드에서 MFStartup (COM 초기화 포함)
	void Close();
	bool IsOpen() const { return m_reader != nullptr; }
	LONGLONG Duration() const { return m_duration; }

	bool Grab(LONGLONG t, VideoFrame& out);
	bool GrabNext(LONGLONG t, VideoFrame& out);
	bool GrabKeyframe(LONGLONG t, VideoFrame& out);
	bool Next(VideoFrame& out);

private:
	bool Seek(LONGLONG t);
	bool ReadUntil(LONGLONG t, VideoFrame& out);  // 지금 자리부터 t 에 닿을 때까지 읽고 마지막 장면만 복사
	bool UpdateFormat();
	bool CopySample(IMFSample* sample, LONGLONG ts, VideoFrame& out);

	CComPtr<IMFSourceReader> m_reader;
	LONGLONG m_duration = 0;
	UINT32 m_w = 0, m_h = 0;      // 디코더 출력 크기 (가장자리 여백 포함일 수 있음)
	CRect  m_crop;                // 실제로 보이는 영역
	LONG   m_stride = 0;
	LONGLONG m_pos = -1;          // 마지막으로 읽은 장면 시각 (-1 = 이동 직후 · 모름)
	bool   m_eos = false;
};

// 장면 → JPEG (WIC, quality 0~100) - COM 이 초기화된 스레드에서
bool SaveFrameJpeg(const VideoFrame& f, const CString& path, int quality = 92);
