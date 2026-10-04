#include "pch.h"
#include "ImagePreview.h"

#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")

namespace
{
	bool LoadWithWic(CImage& image, const CString& path)
	{
		CComPtr<IWICImagingFactory> factory;
		if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
			return false;

		CComPtr<IWICBitmapDecoder> decoder;
		if (FAILED(factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
			WICDecodeMetadataCacheOnDemand, &decoder)))
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
	if (LoadWithWic(image, path))
		return true;

	if (!image.IsNull())
		image.Destroy();
	return SUCCEEDED(image.Load(path));   // GDI+ (JPG/PNG/BMP/GIF/TIFF)
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
