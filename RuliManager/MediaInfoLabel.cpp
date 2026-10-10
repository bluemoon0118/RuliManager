#include "pch.h"
#include "MediaInfoLabel.h"
#include "TextDraw.h"
#include <shobjidl.h>
#include <propidl.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	// FMTID_VideoSummaryInformation {64440491-4C8B-11D1-8B70-080036B11A03}
	const GUID kVideoFmtId = { 0x64440491, 0x4C8B, 0x11D1, { 0x8B, 0x70, 0x08, 0x00, 0x36, 0xB1, 0x1A, 0x03 } };
	const PROPERTYKEY kFrameWidth  = { kVideoFmtId, 3 };   // PKEY_Video_FrameWidth
	const PROPERTYKEY kFrameHeight = { kVideoFmtId, 4 };   // PKEY_Video_FrameHeight
	const PROPERTYKEY kFrameRate   = { kVideoFmtId, 6 };   // PKEY_Video_FrameRate (1000초당 프레임 수)
	// FMTID_AudioSummaryInformation {64440490-4C8B-11D1-8B70-080036B11A03}
	const GUID kMediaFmtId = { 0x64440490, 0x4C8B, 0x11D1, { 0x8B, 0x70, 0x08, 0x00, 0x36, 0xB1, 0x1A, 0x03 } };
	const PROPERTYKEY kDuration    = { kMediaFmtId, 3 };   // PKEY_Media_Duration (100ns 단위)

	ULONGLONG ReadUInt64(IPropertyStore* store, const PROPERTYKEY& key)
	{
		PROPVARIANT pv;
		::PropVariantInit(&pv);
		ULONGLONG value = 0;
		if (SUCCEEDED(store->GetValue(key, &pv)))
		{
			switch (pv.vt)
			{
			case VT_UI8: value = pv.uhVal.QuadPart; break;
			case VT_I8:  value = pv.hVal.QuadPart > 0 ? static_cast<ULONGLONG>(pv.hVal.QuadPart) : 0; break;
			case VT_UI4: value = pv.ulVal; break;
			default: break;
			}
		}
		::PropVariantClear(&pv);
		return value;
	}

	UINT ReadUInt(IPropertyStore* store, const PROPERTYKEY& key)
	{
		PROPVARIANT pv;
		::PropVariantInit(&pv);
		UINT value = 0;
		if (SUCCEEDED(store->GetValue(key, &pv)))
		{
			switch (pv.vt)
			{
			case VT_UI4: value = pv.ulVal; break;
			case VT_I4:  value = pv.lVal > 0 ? static_cast<UINT>(pv.lVal) : 0; break;
			case VT_UI8: value = static_cast<UINT>(pv.uhVal.QuadPart); break;
			case VT_UI2: value = pv.uiVal; break;
			default: break;
			}
		}
		::PropVariantClear(&pv);
		return value;
	}
}

BEGIN_MESSAGE_MAP(CMediaInfoLabel, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

bool CMediaInfoLabel::Create(CWnd* parent, UINT id)
{
	const CString cls = AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_ARROW), nullptr);
	return CWnd::Create(cls, L"", WS_CHILD, CRect(0, 0, 100, 20), parent, id) != FALSE;
}

void CMediaInfoLabel::SetColors(COLORREF back, COLORREF sub, COLORREF text)
{
	m_back = back;
	m_sub = sub;
	m_text = text;
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

CMediaInfoLabel::Info CMediaInfoLabel::ReadInfo(const CString& path)
{
	Info info;
	if (path.IsEmpty() || !::PathFileExistsW(path))
		return info;
	IPropertyStore* store = nullptr;
	if (SUCCEEDED(::SHGetPropertyStoreFromParsingName(path, nullptr, GPS_DEFAULT, IID_PPV_ARGS(&store))) && store)
	{
		info.width = ReadUInt(store, kFrameWidth);
		info.height = ReadUInt(store, kFrameHeight);
		const UINT rate = ReadUInt(store, kFrameRate);
		info.fps = rate / 1000.0;
		info.duration100ns = ReadUInt64(store, kDuration);
		store->Release();
	}
	info.ok = (info.width > 0 && info.height > 0) || info.fps > 0 || info.duration100ns > 0;
	return info;
}

CString CMediaInfoLabel::ResolutionText(UINT width, UINT height)
{
	if (width == 0 || height == 0)
		return CString();
	// 가로가 넓은 영상(1920x800 등)도 1080p 로 보이도록 16:9 기준 높이와 실제 높이 중 큰 값
	const UINT eff = (std::max)(height, width * 9 / 16);
	static const UINT kStd[] = { 4320, 2160, 1440, 1080, 720, 576, 480, 360, 240 };
	for (UINT s : kStd)
	{
		if (eff >= s * 92 / 100 && eff <= s * 108 / 100)
		{
			CString t;
			t.Format(L"%up", s);
			return t;
		}
	}
	CString t;
	t.Format(L"%up", height);
	return t;
}

CString CMediaInfoLabel::FpsText(double fps)
{
	if (fps <= 0)
		return CString();
	CString t;
	t.Format(L"%.2f", fps);
	// 끝의 0 과 소수점 정리: 30.00 → 30, 25.50 → 25.5
	while (t.Find(L'.') >= 0 && (t.Right(1) == L"0" || t.Right(1) == L"."))
	{
		const bool dot = (t.Right(1) == L".");
		t.Truncate(t.GetLength() - 1);
		if (dot)
			break;
	}
	return t;
}

CString CMediaInfoLabel::DurationText(ULONGLONG duration100ns)
{
	if (duration100ns == 0)
		return CString();
	const ULONGLONG sec = duration100ns / 10000000ULL;
	ULONGLONG minutes = (sec + 30) / 60;   // 반올림
	if (minutes == 0)
		minutes = 1;
	CString t;
	t.Format(L"%llu분", minutes);
	return t;
}

void CMediaInfoLabel::SetFile(const CString& path)
{
	if (path.CompareNoCase(m_path) == 0 && !path.IsEmpty())
		return;
	m_path = path;
	m_info = Info();
	if (!path.IsEmpty())
	{
		CString key = path;
		key.MakeLower();
		auto it = m_cache.find(key);
		if (it != m_cache.end())
			m_info = it->second;
		else
		{
			m_info = ReadInfo(path);
			m_cache[key] = m_info;
		}
	}
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

CFont* CMediaInfoLabel::BaseFont()
{
	CWnd* parent = GetParent();
	return parent ? parent->GetFont() : nullptr;
}

void CMediaInfoLabel::EnsureBold()
{
	CFont* base = BaseFont();
	if (base && !m_bold.GetSafeHandle())
	{
		LOGFONT lf = {};
		base->GetLogFont(&lf);
		lf.lfWeight = FW_BOLD;
		m_bold.CreateFontIndirect(&lf);
	}
}

void CMediaInfoLabel::BuildParts(CString& left, CString& sep, CString& res) const
{
	left.Empty(); sep.Empty(); res.Empty();
	if (!m_info.ok)
		return;
	const CString fps = FpsText(m_info.fps);
	res = ResolutionText(m_info.width, m_info.height);
	// 왼쪽(회색): 총 재생 시간 + fps  예) "123분 | fps: 29.97"
	left = DurationText(m_info.duration100ns);
	if (!fps.IsEmpty())
		left += (left.IsEmpty() ? L"" : L" | ") + CString(L"fps: ") + fps;
	if (!left.IsEmpty() && !res.IsEmpty())
		sep = L" | ";
}

int CMediaInfoLabel::NeededWidth()
{
	CString left, sep, res;
	BuildParts(left, sep, res);
	CFont* base = BaseFont();
	if (!base || (left.IsEmpty() && res.IsEmpty()) || !GetSafeHwnd())
		return 0;
	EnsureBold();
	CClientDC dc(this);
	CFont* old = dc.SelectObject(base);
	int w = (left.IsEmpty() ? 0 : dc.GetTextExtent(left).cx) + (sep.IsEmpty() ? 0 : dc.GetTextExtent(sep).cx);
	dc.SelectObject(&m_bold);
	w += res.IsEmpty() ? 0 : dc.GetTextExtent(res).cx;
	dc.SelectObject(old);
	return w + 2;
}

void CMediaInfoLabel::OnPaint()
{
	CPaintDC paint(this);
	CRect rc;
	GetClientRect(&rc);
	CDC mem;
	mem.CreateCompatibleDC(&paint);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&paint, (std::max)(1, rc.Width()), (std::max)(1, rc.Height()));
	CBitmap* oldBmp = mem.SelectObject(&bmp);
	mem.FillSolidRect(rc, m_back);

	CFont* base = BaseFont();
	EnsureBold();

	if (m_info.ok && base)
	{
		CString left, sep, res;
		BuildParts(left, sep, res);

		CFont* old = mem.SelectObject(base);
		const int leftW = left.IsEmpty() ? 0 : mem.GetTextExtent(left).cx;
		const int sepW = sep.IsEmpty() ? 0 : mem.GetTextExtent(sep).cx;
		mem.SelectObject(&m_bold);
		const int resW = res.IsEmpty() ? 0 : mem.GetTextExtent(res).cx;

		mem.SetBkMode(TRANSPARENT);
		int x = rc.right - (leftW + sepW + resW);   // 오른쪽 정렬
		mem.SelectObject(base);
		mem.SetTextColor(m_sub);
		if (leftW)
		{
			mem.DrawText(left, CRect(x, rc.top, x + leftW, rc.bottom), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			x += leftW;
		}
		if (sepW)
		{
			mem.DrawText(sep, CRect(x, rc.top, x + sepW, rc.bottom), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			x += sepW;
		}
		if (resW)
		{
			mem.SelectObject(&m_bold);
			mem.SetTextColor(m_text);
			mem.DrawText(res, CRect(x, rc.top, x + resW, rc.bottom), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		}
		mem.SelectObject(old);
	}

	paint.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(oldBmp);
}
