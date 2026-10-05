#include "pch.h"
#include "ImagePreview.h"

#include <wincodec.h>
#include <shlwapi.h>
#include "DbCrypt.h"
#pragma comment(lib, "windowscodecs.lib")

namespace
{
	// 파일 전체 읽기
	bool ReadAll(const CString& path, std::vector<BYTE>& out)
	{
		out.clear();
		HANDLE h = ::CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		if (h == INVALID_HANDLE_VALUE)
			return false;
		LARGE_INTEGER size = {};
		bool ok = ::GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < 512LL * 1024 * 1024;
		if (ok)
		{
			out.resize(static_cast<size_t>(size.QuadPart));
			DWORD read = 0;
			ok = ::ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) && read == out.size();
		}
		::CloseHandle(h);
		return ok;
	}

	// 암호화된 이미지 파일이면 복호화한 내용을 plain 에 넣고 true
	bool ReadEncryptedImage(const CString& path, std::vector<BYTE>& plain)
	{
		std::vector<BYTE> raw;
		if (!ReadAll(path, raw) || !DbCrypt::IsEncrypted(raw.data(), raw.size()))
			return false;
		return DbCrypt::Decrypt(raw.data(), raw.size(), plain) && !plain.empty();
	}

	bool LoadWithWic(CImage& image, const CString& path, IStream* stream)
	{
		CComPtr<IWICImagingFactory> factory;
		if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
			return false;

		CComPtr<IWICBitmapDecoder> decoder;
		const HRESULT hr = stream
			? factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)
			: factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
		if (FAILED(hr))
			return false;   // 코덱 없음 (예: WebP 확장 미설치) 또는 손상된 파일

		CComPtr<IWICBitmapFrameDecode> frame;
		if (FAILED(decoder->GetFrame(0, &frame)))
			return false;

		// 32비트 BGRA(알파 미리 곱함)로 변환 → CImage::Draw 의 AlphaBlend 와 맞음
		CComPtr<IWICFormatConverter> converter;
		if (FAILED(factory->CreateFormatConverter(&converter)) ||
			FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
				WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
			return false;

		UINT w = 0, h = 0;
		if (FAILED(converter->GetSize(&w, &h)) || w == 0 || h == 0 || w > 30000 || h > 30000)
			return false;

		if (!image.IsNull())
			image.Destroy();
		// 높이를 음수로 → 위에서 아래로 저장되는 DIB (WIC 출력 순서와 같음)
		if (!image.Create(static_cast<int>(w), -static_cast<int>(h), 32, CImage::createAlphaChannel))
			return false;

		const int pitch = image.GetPitch();
		if (FAILED(converter->CopyPixels(nullptr, static_cast<UINT>(pitch),
			static_cast<UINT>(pitch) * h, static_cast<BYTE*>(image.GetBits()))))
		{
			image.Destroy();
			return false;
		}
		return true;
	}
}

bool LoadImageFile(CImage& image, const CString& path)
{
	if (path.IsEmpty())
		return false;

	// 암호화된 이미지(Image 폴더의 .vmimg): 메모리에서 복호화해서 읽음 (평문 파일을 만들지 않음)
	std::vector<BYTE> plain;
	if (ReadEncryptedImage(path, plain))
	{
		bool ok = false;
		if (IStream* s = ::SHCreateMemStream(plain.data(), static_cast<UINT>(plain.size())))
		{
			ok = LoadWithWic(image, path, s);
			if (!ok)
			{
				LARGE_INTEGER zero = {};
				s->Seek(zero, STREAM_SEEK_SET, nullptr);
				if (!image.IsNull())
					image.Destroy();
				ok = SUCCEEDED(image.Load(s));   // GDI+
			}
			s->Release();
		}
		::SecureZeroMemory(plain.data(), plain.size());
		return ok;
	}

	if (LoadWithWic(image, path, nullptr))
		return true;

	if (!image.IsNull())
		image.Destroy();
	return SUCCEEDED(image.Load(path));   // GDI+ (JPG/PNG/BMP/GIF/TIFF)
}

IStream* OpenImageFileStream(const CString& path)
{
	if (path.IsEmpty())
		return nullptr;
	std::vector<BYTE> data;
	if (!ReadEncryptedImage(path, data) && !ReadAll(path, data))
		return nullptr;
	IStream* s = ::SHCreateMemStream(data.data(), static_cast<UINT>(data.size()));
	::SecureZeroMemory(data.data(), data.size());
	return s;
}

BEGIN_MESSAGE_MAP(CImagePreview, CStatic)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
END_MESSAGE_MAP()

bool CImagePreview::SetImageFile(const CString& path)
{
	if (!m_image.IsNull())
		m_image.Destroy();
	m_file.Empty();

	bool ok = false;
	if (!path.IsEmpty())
	{
		// 이미지를 메모리로 복사한 뒤 파일을 닫으므로 파일이 잠기지 않습니다.
		if (LoadImageFile(m_image, path))
		{
			m_file = path;
			ok = true;
		}
		else
		{
			m_text = L"이미지를 불러올 수 없습니다.\n" + CString(::PathFindFileNameW(path));
		}
	}

	Invalidate(FALSE);
	return ok;
}

void CImagePreview::SetPlaceholder(const CString& text)
{
	m_text = text;
	if (m_image.IsNull())
		Invalidate(FALSE);
}

void CImagePreview::Clear()
{
	if (!m_image.IsNull())
		m_image.Destroy();
	m_file.Empty();
	Invalidate(FALSE);
}

BOOL CImagePreview::OnEraseBkgnd(CDC* /*pDC*/)
{
	return TRUE;   // OnPaint 에서 전부 그림 (깜빡임 방지)
}

void CImagePreview::OnSize(UINT nType, int cx, int cy)
{
	CStatic::OnSize(nType, cx, cy);
	Invalidate(FALSE);
}

void CImagePreview::OnPaint()
{
	CPaintDC dc(this);

	CRect rc;
	GetClientRect(&rc);
	if (rc.IsRectEmpty())
		return;

	// 더블 버퍼링
	CDC mem;
	mem.CreateCompatibleDC(&dc);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
	CBitmap* oldBmp = mem.SelectObject(&bmp);

	mem.FillSolidRect(&rc, m_bk);

	if (!m_image.IsNull())
	{
		const int iw = m_image.GetWidth();
		const int ih = m_image.GetHeight();
		if (iw > 0 && ih > 0)
		{
			// 비율 유지 맞춤 (작은 이미지는 확대하지 않음)
			double scale = (std::min)(static_cast<double>(rc.Width()) / iw,
			                          static_cast<double>(rc.Height()) / ih);
			scale = (std::min)(scale, 1.0);
			const int w = (std::max)(1, static_cast<int>(iw * scale + 0.5));
			const int h = (std::max)(1, static_cast<int>(ih * scale + 0.5));
			const int x = rc.left + (rc.Width() - w) / 2;
			const int y = rc.top + (rc.Height() - h) / 2;

			mem.SetStretchBltMode(HALFTONE);
			::SetBrushOrgEx(mem.GetSafeHdc(), 0, 0, nullptr);
			m_image.Draw(mem.GetSafeHdc(), CRect(x, y, x + w, y + h));
		}
	}
	else
	{
		CFont* oldFont = nullptr;
		if (CWnd* parent = GetParent())
		{
			if (CFont* f = parent->GetFont())
				oldFont = mem.SelectObject(f);
		}
		mem.SetBkMode(TRANSPARENT);
		mem.SetTextColor(RGB(170, 170, 170));

		CRect textRc = rc;
		textRc.DeflateRect(10, 10);
		CRect calc = textRc;
		mem.DrawText(m_text, &calc, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
		const int offset = (std::max)(0, (textRc.Height() - calc.Height()) / 2);
		textRc.top += offset;
		mem.DrawText(m_text, &textRc, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);

		if (oldFont)
			mem.SelectObject(oldFont);
	}

	dc.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(oldBmp);
}
