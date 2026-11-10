#include "pch.h"
#include "FrameReader.h"
#include <mferror.h>
#include <propvarutil.h>
#include <wincodec.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "windowscodecs.lib")

bool CFrameReader::Open(const CString& path)
{
	Close();
	CComPtr<IMFAttributes> attr;
	if (FAILED(::MFCreateAttributes(&attr, 2)))
		return false;
	attr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);   // 디코더 출력 → RGB32 변환
	if (FAILED(::MFCreateSourceReaderFromURL(path, attr, &m_reader)))
	{
		m_reader.Release();
		return false;
	}
	m_reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
	m_reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
	CComPtr<IMFMediaType> type;
	if (FAILED(::MFCreateMediaType(&type)))
	{
		Close();
		return false;
	}
	type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	if (FAILED(m_reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type)) || !UpdateFormat())
	{
		Close();
		return false;
	}
	PROPVARIANT var;
	::PropVariantInit(&var);
	if (SUCCEEDED(m_reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &var)) && var.vt == VT_UI8)
		m_duration = static_cast<LONGLONG>(var.uhVal.QuadPart);
	::PropVariantClear(&var);
	if (m_duration <= 0)
	{
		Close();
		return false;
	}
	m_pos = 0;   // 처음
	return true;
}

void CFrameReader::Close()
{
	m_reader.Release();
	m_duration = 0;
	m_pos = -1;
	m_eos = false;
}

bool CFrameReader::Seek(LONGLONG t)
{
	if (!m_reader)
		return false;
	PROPVARIANT pos;
	if (FAILED(::InitPropVariantFromInt64((std::max)(0LL, t), &pos)))
		return false;
	const HRESULT hr = m_reader->SetCurrentPosition(GUID_NULL, pos);
	::PropVariantClear(&pos);
	m_pos = -1;
	m_eos = false;
	return SUCCEEDED(hr);
}

bool CFrameReader::ReadUntil(LONGLONG t, VideoFrame& out)
{
	// 마지막으로 읽은 샘플만 들고 있다가, 목표에 닿거나(0.1초 오차 허용) 끝에 닿으면 그 한 장만 복사
	CComPtr<IMFSample> last;
	LONGLONG lastTs = 0;
	for (int i = 0; i < 900; ++i)   // 키프레임 간격이 길어도 최대 900장까지만 앞으로
	{
		DWORD flags = 0;
		LONGLONG ts = 0;
		CComPtr<IMFSample> sample;
		if (FAILED(m_reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags, &ts, &sample)))
			break;
		if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
		{
			UpdateFormat();
			last.Release();   // 형식이 바뀌기 전 샘플은 버림
		}
		if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
		{
			m_eos = true;
			break;
		}
		if (!sample)
			continue;
		last = sample;
		lastTs = ts;
		m_pos = ts;
		if (ts >= t - kSecond / 10)   // 목표 시간에 닿음
			break;
	}
	return last && CopySample(last, lastTs, out);
}

bool CFrameReader::Grab(LONGLONG t, VideoFrame& out)
{
	if (!Seek(t))
		return false;
	return ReadUntil(t, out);
}

bool CFrameReader::GrabNext(LONGLONG t, VideoFrame& out)
{
	// 지금 자리 바로 뒤(3초 이내)면 이동하지 않고 이어서 디코딩 - 아니면 Grab
	//  (멀면 이동하는 쪽이 빠름: 이동 후 디코딩 = 앞쪽 키프레임부터 t 까지, 이어서 = 지금 자리부터 t 까지)
	if (m_reader && !m_eos && m_pos >= 0 && t > m_pos && t - m_pos <= 3 * kSecond)
		return ReadUntil(t, out);
	return Grab(t, out);
}

bool CFrameReader::GrabKeyframe(LONGLONG t, VideoFrame& out)
{
	if (!Seek(t))
		return false;
	return ReadUntil(-(1LL << 62), out);   // 첫 장면에서 멈춤 (목표가 아주 앞이라 바로 닿음)
}

bool CFrameReader::Next(VideoFrame& out)
{
	if (!m_reader || m_eos)
		return false;
	return ReadUntil(-(1LL << 62), out);
}

bool CFrameReader::UpdateFormat()
{
	CComPtr<IMFMediaType> cur;
	if (FAILED(m_reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &cur)))
		return false;
	if (FAILED(::MFGetAttributeSize(cur, MF_MT_FRAME_SIZE, &m_w, &m_h)) || m_w == 0 || m_h == 0)
		return false;
	UINT32 stride = 0;
	m_stride = SUCCEEDED(cur->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) ? static_cast<LONG>(stride) : static_cast<LONG>(m_w * 4);
	m_crop = CRect(0, 0, static_cast<int>(m_w), static_cast<int>(m_h));
	MFVideoArea area = {};
	if (SUCCEEDED(cur->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof(area), nullptr)) &&
		area.Area.cx > 0 && area.Area.cy > 0)
	{
		const CRect r(CPoint(area.OffsetX.value, area.OffsetY.value), CSize(area.Area.cx, area.Area.cy));
		CRect clip;
		if (clip.IntersectRect(r, m_crop))
			m_crop = clip;
	}
	return true;
}

bool CFrameReader::CopySample(IMFSample* sample, LONGLONG ts, VideoFrame& out)
{
	CComPtr<IMFMediaBuffer> buf;
	if (FAILED(sample->ConvertToContiguousBuffer(&buf)))
		return false;
	BYTE* base = nullptr;
	LONG pitch = 0;
	CComPtr<IMF2DBuffer> buf2d;
	bool locked2d = false;
	if (SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&buf2d))) && SUCCEEDED(buf2d->Lock2D(&base, &pitch)))
		locked2d = true;
	else
	{
		DWORD maxLen = 0, len = 0;
		if (FAILED(buf->Lock(&base, &maxLen, &len)))
			return false;
		pitch = m_stride;
		if (pitch < 0)   // 아래에서 위로 저장된 영상: 첫 줄은 버퍼 끝쪽
			base += static_cast<LONG_PTR>(-pitch) * (m_h - 1);
	}
	out.w = m_crop.Width();
	out.h = m_crop.Height();
	out.ts = ts;
	out.bgra.resize(static_cast<size_t>(out.w) * out.h * 4);
	for (int y = 0; y < out.h; ++y)
	{
		const BYTE* src = base + static_cast<LONG_PTR>(pitch) * (m_crop.top + y) + m_crop.left * 4;
		BYTE* dst = out.bgra.data() + static_cast<size_t>(y) * out.w * 4;
		memcpy(dst, src, static_cast<size_t>(out.w) * 4);
		for (int x = 0; x < out.w; ++x)
			dst[x * 4 + 3] = 255;   // 알파 (RGB32 는 의미 없는 값)
	}
	if (locked2d)
		buf2d->Unlock2D();
	else
		buf->Unlock();
	return out.w > 0 && out.h > 0;
}

bool SaveFrameJpeg(const VideoFrame& f, const CString& path, int quality)
{
	if (f.w <= 0 || f.h <= 0 || f.bgra.size() < static_cast<size_t>(f.w) * f.h * 4)
		return false;
	CComPtr<IWICImagingFactory> wic;
	if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
		return false;
	CComPtr<IWICStream> stream;
	CComPtr<IWICBitmapEncoder> enc;
	CComPtr<IWICBitmapFrameEncode> frame;
	CComPtr<IPropertyBag2> props;
	if (FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path, GENERIC_WRITE)))
		return false;
	bool ok = SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &enc))
		&& SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache))
		&& SUCCEEDED(enc->CreateNewFrame(&frame, &props));
	if (ok && props)
	{
		PROPBAG2 opt = {};
		opt.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
		VARIANT v;
		::VariantInit(&v);
		v.vt = VT_R4;
		v.fltVal = static_cast<float>((std::max)(1, (std::min)(100, quality))) / 100.0f;
		props->Write(1, &opt, &v);
	}
	if (ok)
		ok = SUCCEEDED(frame->Initialize(props)) && SUCCEEDED(frame->SetSize(static_cast<UINT>(f.w), static_cast<UINT>(f.h)));
	if (ok)
	{
		// JPEG 은 24비트 BGR
		WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
		ok = SUCCEEDED(frame->SetPixelFormat(&fmt)) && IsEqualGUID(fmt, GUID_WICPixelFormat24bppBGR);
		if (ok)
		{
			const UINT stride = static_cast<UINT>((f.w * 3 + 3) & ~3);
			std::vector<BYTE> bgr(static_cast<size_t>(stride) * f.h);
			for (int y = 0; y < f.h; ++y)
			{
				const BYTE* s = f.bgra.data() + static_cast<size_t>(y) * f.w * 4;
				BYTE* d = bgr.data() + static_cast<size_t>(y) * stride;
				for (int x = 0; x < f.w; ++x)
				{
					d[x * 3 + 0] = s[x * 4 + 0];
					d[x * 3 + 1] = s[x * 4 + 1];
					d[x * 3 + 2] = s[x * 4 + 2];
				}
			}
			ok = SUCCEEDED(frame->WritePixels(static_cast<UINT>(f.h), stride, static_cast<UINT>(bgr.size()), bgr.data()))
				&& SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
		}
	}
	frame.Release();
	enc.Release();
	stream.Release();
	if (!ok)
		::DeleteFileW(path);
	return ok;
}
