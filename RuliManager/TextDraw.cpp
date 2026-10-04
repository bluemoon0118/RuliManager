#include "pch.h"
#include "TextDraw.h"

#include <usp10.h>
#pragma comment(lib, "usp10.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	const DWORD kFlags = SSA_GLYPHS | SSA_FALLBACK | SSA_LINK;   // 폰트 링크 + 폴백

	SCRIPT_STRING_ANALYSIS Analyse(HDC hdc, const CString& s)
	{
		SCRIPT_STRING_ANALYSIS ssa = nullptr;
		const int len = s.GetLength();
		if (FAILED(::ScriptStringAnalyse(hdc, static_cast<LPCWSTR>(s), len, len * 3 / 2 + 16, -1, kFlags,
			0, nullptr, nullptr, nullptr, nullptr, nullptr, &ssa)))
			return nullptr;
		return ssa;
	}
}

namespace TextFB
{
	int Width(HDC hdc, const CString& text)
	{
		if (text.IsEmpty())
			return 0;
		SCRIPT_STRING_ANALYSIS ssa = Analyse(hdc, text);
		if (!ssa)
		{
			SIZE sz = {};
			::GetTextExtentPoint32W(hdc, text, text.GetLength(), &sz);
			return sz.cx;
		}
		const SIZE* sz = ::ScriptString_pSize(ssa);
		const int w = sz ? sz->cx : 0;
		::ScriptStringFree(&ssa);
		return w;
	}

	void Draw(HDC hdc, const CString& text, const CRect& rc, UINT align, bool ellipsis)
	{
		if (text.IsEmpty() || rc.Width() <= 0)
			return;
		CString s = text;
		int w = Width(hdc, s);
		if (ellipsis && w > rc.Width())
		{
			// 들어가는 최대 길이 + "…" (이분 탐색)
			int lo = 0, hi = s.GetLength();
			while (lo < hi)
			{
				const int mid = (lo + hi + 1) / 2;
				if (Width(hdc, text.Left(mid) + L"\x2026") <= rc.Width())
					lo = mid;
				else
					hi = mid - 1;
			}
			s = text.Left(lo) + L"\x2026";
			w = Width(hdc, s);
		}

		TEXTMETRICW tm = {};
		::GetTextMetricsW(hdc, &tm);
		int x = rc.left;
		if (align & DT_CENTER)     x = rc.left + (rc.Width() - w) / 2;
		else if (align & DT_RIGHT) x = rc.right - w;
		const int y = rc.top + (rc.Height() - tm.tmHeight) / 2;

		SCRIPT_STRING_ANALYSIS ssa = Analyse(hdc, s);
		if (ssa)
		{
			::ScriptStringOut(ssa, x, y, ETO_CLIPPED, &rc, 0, 0, FALSE);
			::ScriptStringFree(&ssa);
		}
		else
		{
			::ExtTextOutW(hdc, x, y, ETO_CLIPPED, &rc, s, s.GetLength(), nullptr);
		}
	}
}
