#include "pch.h"
#include "ImagePreview.h"

#include <wincodec.h>
#include <shlwapi.h>
#include <d2d1_3.h>     // SVG (Direct2D - Windows 10 1703 이상)
#include "DbCrypt.h"
#include <atomic>
#include <cmath>
#include <string>
#include <vector>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d2d1.lib")

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

	// 내용이 SVG(XML) 인지: 앞부분(BOM · 공백 · <?xml · 주석 · DOCTYPE 뒤)에 "<svg" 가 있으면
	bool IsSvgData(const std::vector<BYTE>& data)
	{
		if (data.size() < 5)
			return false;
		const size_t n = (std::min)(data.size(), static_cast<size_t>(4096));
		std::string head(reinterpret_cast<const char*>(data.data()), n);
		size_t i = 0;
		if (head.compare(0, 3, "\xEF\xBB\xBF") == 0)
			i = 3;   // UTF-8 BOM
		while (i < head.size() && (head[i] == ' ' || head[i] == '\t' || head[i] == '\r' || head[i] == '\n'))
			++i;
		if (i >= head.size() || head[i] != '<')
			return false;
		for (char& c : head)
			c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
		return head.find("<svg", i) != std::string::npos;
	}

	// SVG → 32비트 BGRA(알파 미리 곱함) 비트맵 (Direct2D 의 SVG 그리기 - 외부 라이브러리 없음)
	//  - 크기: SVG 의 viewBox (없으면 width · height) 비율로 긴 변 kSvgPx 픽셀 (벡터라 카드 · 상세 화면에서 줄여 그려도 선명)
	//  - Windows 10 1703 이전(ID2D1DeviceContext5 없음)이면 false
	//  - <text> · CSS <style> · 필터는 Direct2D 가 그리지 않음 (로고 대부분은 글자도 패스라 문제 없음)
	bool LoadWithSvg(CImage& image, const std::vector<BYTE>& data)
	{
		static const float kSvgPx = 1024.0f;
		if (data.empty() || data.size() > 64u * 1024 * 1024)
			return false;
		CComPtr<IWICImagingFactory> wic;
		if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
			return false;
		CComPtr<ID2D1Factory1> d2d;
		D2D1_FACTORY_OPTIONS opt = {};
		if (FAILED(::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opt, reinterpret_cast<void**>(&d2d))))
			return false;
		auto makeTarget = [&](UINT w, UINT h, CComPtr<IWICBitmap>& bmp, CComPtr<ID2D1DeviceContext5>& dc) -> bool
		{
			bmp.Release();
			dc.Release();
			if (FAILED(wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)))
				return false;
			const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
			CComPtr<ID2D1RenderTarget> rt;
			if (FAILED(d2d->CreateWicBitmapRenderTarget(bmp, props, &rt)))
				return false;
			return SUCCEEDED(rt->QueryInterface(__uuidof(ID2D1DeviceContext5), reinterpret_cast<void**>(&dc)));   // SVG 지원 (Win10 1703+)
		};
		auto makeStream = [&]() -> CComPtr<IStream>
		{
			CComPtr<IStream> s;
			s.Attach(::SHCreateMemStream(data.data(), static_cast<UINT>(data.size())));
			return s;
		};

		// 1) 크기 알아내기 (1×1 에 읽어서 viewBox / width · height)
		CComPtr<IWICBitmap> bmp;
		CComPtr<ID2D1DeviceContext5> dc;
		if (!makeTarget(1, 1, bmp, dc))
			return false;
		D2D1_SVG_VIEWBOX vb = {};
		bool hasVb = false;
		float lw = 0, lh = 0;
		{
			CComPtr<IStream> s = makeStream();
			CComPtr<ID2D1SvgDocument> doc;
			if (!s || FAILED(dc->CreateSvgDocument(s, D2D1::SizeF(1.0f, 1.0f), &doc)) || !doc)
				return false;   // SVG 가 아니거나 읽지 못함
			CComPtr<ID2D1SvgElement> root;
			doc->GetRoot(&root);
			if (root)
			{
				if (root->IsAttributeSpecified(L"viewBox")
					&& SUCCEEDED(root->GetAttributeValue(L"viewBox", D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX, &vb, sizeof(vb)))
					&& vb.width > 0 && vb.height > 0)
					hasVb = true;
				D2D1_SVG_LENGTH len = {};
				if (root->IsAttributeSpecified(L"width") && SUCCEEDED(root->GetAttributeValue(L"width", D2D1_SVG_ATTRIBUTE_POD_TYPE_LENGTH, &len, sizeof(len)))
					&& len.units == D2D1_SVG_LENGTH_UNITS_NUMBER)
					lw = len.value;
				if (root->IsAttributeSpecified(L"height") && SUCCEEDED(root->GetAttributeValue(L"height", D2D1_SVG_ATTRIBUTE_POD_TYPE_LENGTH, &len, sizeof(len)))
					&& len.units == D2D1_SVG_LENGTH_UNITS_NUMBER)
					lh = len.value;
			}
		}
		if (!hasVb && lw > 0 && lh > 0)
		{
			vb = { 0, 0, lw, lh };   // viewBox 가 없으면 width · height 를 viewBox 로 (아래에서 크기에 맞춰 늘림)
			hasVb = true;
		}
		const float aspect = hasVb ? vb.width / vb.height : 1.0f;
		UINT w = 0, h = 0;
		if (aspect >= 1.0f)
		{
			w = static_cast<UINT>(kSvgPx);
			h = static_cast<UINT>((std::max)(1.0f, std::floor(kSvgPx / aspect + 0.5f)));
		}
		else
		{
			h = static_cast<UINT>(kSvgPx);
			w = static_cast<UINT>((std::max)(1.0f, std::floor(kSvgPx * aspect + 0.5f)));
		}

		// 2) 그 크기로 그림 (viewBox → 전체 크기, 남는 곳은 투명)
		if (!makeTarget(w, h, bmp, dc))
			return false;
		CComPtr<IStream> s = makeStream();
		CComPtr<ID2D1SvgDocument> doc;
		if (!s || FAILED(dc->CreateSvgDocument(s, D2D1::SizeF(static_cast<float>(w), static_cast<float>(h)), &doc)) || !doc)
			return false;
		CComPtr<ID2D1SvgElement> root;
		doc->GetRoot(&root);
		if (root)
		{
			if (hasVb)
				root->SetAttributeValue(L"viewBox", D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX, &vb, sizeof(vb));
			const D2D1_SVG_LENGTH full = { 100.0f, D2D1_SVG_LENGTH_UNITS_PERCENTAGE };   // 그리는 크기 전체
			root->SetAttributeValue(L"width", D2D1_SVG_ATTRIBUTE_POD_TYPE_LENGTH, &full, sizeof(full));
			root->SetAttributeValue(L"height", D2D1_SVG_ATTRIBUTE_POD_TYPE_LENGTH, &full, sizeof(full));
		}
		dc->BeginDraw();
		dc->Clear(D2D1::ColorF(0, 0, 0, 0));
		dc->DrawSvgDocument(doc);
		if (FAILED(dc->EndDraw()))
			return false;
		dc.Release();   // 그리기 끝 (비트맵 잠금 해제)

		if (!image.IsNull())
			image.Destroy();
		if (!image.Create(static_cast<int>(w), -static_cast<int>(h), 32, CImage::createAlphaChannel))
			return false;
		const int pitch = image.GetPitch();
		if (FAILED(bmp->CopyPixels(nullptr, static_cast<UINT>(pitch), static_cast<UINT>(pitch) * h, static_cast<BYTE*>(image.GetBits()))))
		{
			image.Destroy();
			return false;
		}
		return true;
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
		if (IsSvgData(plain))
		{
			const bool svgOk = LoadWithSvg(image, plain);   // 암호화된 SVG
			::SecureZeroMemory(plain.data(), plain.size());
			return svgOk;
		}
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

	{
		// SVG: 확장자가 .svg 이거나 내용이 SVG 면 Direct2D 로 그림 (WIC · GDI+ 는 SVG 를 못 읽음)
		const CString ext = ::PathFindExtensionW(path);
		if (ext.CompareNoCase(L".svg") == 0)
		{
			std::vector<BYTE> data;
			return ReadAll(path, data) && LoadWithSvg(image, data);
		}
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
	ON_WM_LBUTTONDBLCLK()
	ON_WM_MOUSEMOVE()
	ON_MESSAGE(WM_MOUSELEAVE, &CImagePreview::OnMouseLeave)
END_MESSAGE_MAP()

namespace
{
	std::atomic<bool> g_sfwMosaic{ false };   // 백그라운드 이미지 읽기 스레드에서도 읽음
}

void SetSfwMosaic(bool on) { g_sfwMosaic = on; }
bool IsSfwMosaic() { return g_sfwMosaic; }

void ApplyMosaic(CImage& image)
{
	if (image.IsNull())
		return;
	const int w = image.GetWidth(), h = image.GetHeight();
	if (w <= 0 || h <= 0)
		return;
	// 32비트가 아니면 32비트로 바꿔서 처리
	if (image.GetBPP() != 32)
	{
		CImage tmp;
		if (!tmp.Create(w, h, 32))
			return;
		HDC dc = tmp.GetDC();
		image.BitBlt(dc, 0, 0, SRCCOPY);
		tmp.ReleaseDC();
		image.Destroy();
		image.Attach(tmp.Detach());
	}
	// 칸 크기: 이미지 긴 변의 약 1/24 (작은 이미지도 최소 6픽셀)
	const int block = (std::max)(6, (std::max)(w, h) / 24);
	BYTE* bits = static_cast<BYTE*>(image.GetBits());
	const int pitch = image.GetPitch();   // 아래에서 위로 저장된 이미지면 음수 (줄 계산은 그대로 됨)
	for (int by = 0; by < h; by += block)
	{
		const int y1 = (std::min)(h, by + block);
		for (int bx = 0; bx < w; bx += block)
		{
			const int x1 = (std::min)(w, bx + block);
			unsigned long long sum[4] = {};
			for (int y = by; y < y1; ++y)
			{
				const BYTE* p = bits + static_cast<ptrdiff_t>(y) * pitch + bx * 4;
				for (int x = bx; x < x1; ++x, p += 4)
				{
					sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2]; sum[3] += p[3];
				}
			}
			const unsigned long long n = static_cast<unsigned long long>(y1 - by) * (x1 - bx);
			const BYTE c[4] = { static_cast<BYTE>(sum[0] / n), static_cast<BYTE>(sum[1] / n), static_cast<BYTE>(sum[2] / n), static_cast<BYTE>(sum[3] / n) };
			for (int y = by; y < y1; ++y)
			{
				BYTE* p = bits + static_cast<ptrdiff_t>(y) * pitch + bx * 4;
				for (int x = bx; x < x1; ++x, p += 4)
				{
					p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; p[3] = c[3];
				}
			}
		}
	}
}

bool LoadImageFileSfw(CImage& image, const CString& path)
{
	if (!LoadImageFile(image, path))
		return false;
	if (g_sfwMosaic)
		ApplyMosaic(image);   // 메모리의 이미지만 (원본 파일은 그대로)
	return true;
}

bool CImagePreview::SetImageFile(const CString& path, bool sfw)
{
	if (!m_image.IsNull())
		m_image.Destroy();
	m_file.Empty();
	m_alpha = 255;   // 반투명은 다시 SetDimmed 로 지정할 때만
	m_drawDefault = nullptr;

	bool ok = false;
	if (!path.IsEmpty())
	{
		// 이미지를 메모리로 복사한 뒤 파일을 닫으므로 파일이 잠기지 않습니다.
		if (sfw ? LoadImageFileSfw(m_image, path) : LoadImageFile(m_image, path))
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
	// 안내 문구 = 이미지 없음 → 이전 이미지가 남지 않게 지움
	if (!m_image.IsNull())
		m_image.Destroy();
	m_file.Empty();
	m_alpha = 255;   // 반투명은 다시 SetDimmed 로 지정할 때만
	m_drawDefault = nullptr;
	m_text = text;
	Invalidate(FALSE);
}

void CImagePreview::SetDefaultImage(std::function<void(CDC*, const CRect&)> draw)
{
	if (!m_image.IsNull())
		m_image.Destroy();
	m_file.Empty();
	m_alpha = 255;   // 반투명은 다시 SetDimmed 로 지정할 때만
	m_text.Empty();
	m_drawDefault = std::move(draw);
	Invalidate(FALSE);
}

void CImagePreview::Clear()
{
	if (!m_image.IsNull())
		m_image.Destroy();
	m_file.Empty();
	m_alpha = 255;   // 반투명은 다시 SetDimmed 로 지정할 때만
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

void CImagePreview::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	if (m_onDblClick)
		m_onDblClick();
	else
		CStatic::OnLButtonDblClk(nFlags, point);
}

void CImagePreview::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_hoverTracking && m_onHover)
	{
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
		m_hoverTracking = ::TrackMouseEvent(&tme) != FALSE;
		m_onHover(true);
	}
	CStatic::OnMouseMove(nFlags, point);
}

LRESULT CImagePreview::OnMouseLeave(WPARAM, LPARAM)
{
	if (m_hoverTracking)
	{
		m_hoverTracking = false;
		if (m_onHover)
			m_onHover(false);
	}
	return 0;
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

			if (m_alpha < 255)
			{
				// 반투명 (연결 안 된 이동식 디스크 영상): 축소한 이미지를 임시 DC 에 그린 뒤 배경 위에 섞음
				CDC tmp;
				tmp.CreateCompatibleDC(&dc);
				CBitmap tbmp;
				tbmp.CreateCompatibleBitmap(&dc, w, h);
				CBitmap* ob = tmp.SelectObject(&tbmp);
				tmp.FillSolidRect(0, 0, w, h, m_bk);
				tmp.SetStretchBltMode(HALFTONE);
				::SetBrushOrgEx(tmp.GetSafeHdc(), 0, 0, nullptr);
				m_image.Draw(tmp.GetSafeHdc(), CRect(0, 0, w, h));
				BLENDFUNCTION bf = { AC_SRC_OVER, 0, m_alpha, 0 };
				::AlphaBlend(mem.GetSafeHdc(), x, y, w, h, tmp.GetSafeHdc(), 0, 0, w, h, bf);
				tmp.SelectObject(ob);
			}
			else
			{
				mem.SetStretchBltMode(HALFTONE);
				::SetBrushOrgEx(mem.GetSafeHdc(), 0, 0, nullptr);
				m_image.Draw(mem.GetSafeHdc(), CRect(x, y, x + w, y + h));
			}
		}
	}
	else if (m_drawDefault)
	{
		m_drawDefault(&mem, rc);   // 기본 이미지 (사진 없는 배우의 실루엣 등)
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
