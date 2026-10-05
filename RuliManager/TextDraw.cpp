#include "pch.h"
#include "TextDraw.h"

#include <usp10.h>
#include <map>
#include <string>
#include <vector>
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

	// 한 글꼴로 한 조각 폭 / 그리기 (Uniscribe)
	int RunWidth(HDC hdc, const CString& s)
	{
		if (s.IsEmpty())
			return 0;
		SCRIPT_STRING_ANALYSIS ssa = Analyse(hdc, s);
		if (!ssa)
		{
			SIZE sz = {};
			::GetTextExtentPoint32W(hdc, s, s.GetLength(), &sz);
			return sz.cx;
		}
		const SIZE* sz = ::ScriptString_pSize(ssa);
		const int w = sz ? sz->cx : 0;
		::ScriptStringFree(&ssa);
		return w;
	}

	void RunOut(HDC hdc, const CString& s, int x, int y, const CRect& clip)
	{
		SCRIPT_STRING_ANALYSIS ssa = Analyse(hdc, s);
		if (ssa)
		{
			::ScriptStringOut(ssa, x, y, ETO_CLIPPED, &clip, 0, 0, FALSE);
			::ScriptStringFree(&ssa);
		}
		else
			::ExtTextOutW(hdc, x, y, ETO_CLIPPED, &clip, s, s.GetLength(), nullptr);
	}

	// ── 글자 단위 글꼴 대체 ──
	// Uniscribe 폴백은 "글꼴이 그 문자 체계(한자)를 지원하는지"로만 판단해서,
	// 맑은 고딕처럼 한자는 있지만 일본 국자(凪 등)가 없는 글꼴은 대체가 안 되고 엉뚱한 글자(□/|)로 나옴.
	// → 현재 글꼴에 없는 글자를 직접 찾아(GetGlyphIndices) 그 글자가 있는 글꼴로 따로 그림.
	const wchar_t* const kFallbackFaces[] = {
		L"Yu Gothic UI", L"Meiryo UI", L"Meiryo", L"MS UI Gothic", L"MS Gothic",
		L"Microsoft YaHei UI", L"Microsoft JhengHei UI", L"SimSun", L"MingLiU",
		L"Segoe UI Symbol", L"Segoe UI Emoji", L"Arial Unicode MS"
	};

	// 기본 글꼴과 같은 크기 / 굵기의 대체 글꼴 (만든 글꼴은 프로그램 끝까지 보관)
	HFONT FallbackFont(const LOGFONTW& base, int faceIndex)
	{
		static std::map<std::wstring, HFONT> cache;
		wchar_t key[160];
		swprintf_s(key, L"%d|%ld|%ld|%d|%d|%d", faceIndex, base.lfHeight, base.lfWeight,
			base.lfItalic, base.lfUnderline, base.lfQuality);
		auto it = cache.find(key);
		if (it != cache.end())
			return it->second;
		LOGFONTW lf = base;
		lf.lfWidth = 0;
		lf.lfCharSet = DEFAULT_CHARSET;
		wcscpy_s(lf.lfFaceName, kFallbackFaces[faceIndex]);
		HFONT f = ::CreateFontIndirectW(&lf);
		cache[key] = f;
		return f;
	}

	bool HasGlyph(HDC hdc, wchar_t ch)
	{
		WORD idx = 0xFFFF;
		if (::GetGlyphIndicesW(hdc, &ch, 1, &idx, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR)
			return true;   // 확인 불가 → 그대로
		return idx != 0xFFFF;
	}

	struct Run { CString text; HFONT font; };   // font = nullptr → 현재(기본) 글꼴

	// 글자마다 그릴 글꼴을 정해서 같은 글꼴끼리 묶음
	std::vector<Run> SplitRuns(HDC hdc, const CString& text)
	{
		std::vector<Run> runs;
		const int len = text.GetLength();
		std::vector<WORD> idx(len > 0 ? len : 1, 0);
		bool missing = false;
		if (len > 0 && ::GetGlyphIndicesW(hdc, text, len, idx.data(), GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR)
		{
			for (int i = 0; i < len; ++i)
				if (idx[i] == 0xFFFF) { missing = true; break; }
		}
		if (!missing)
		{
			runs.push_back({ text, nullptr });   // 빠른 길: 모든 글자가 현재 글꼴에 있음
			return runs;
		}

		HFONT baseFont = static_cast<HFONT>(::GetCurrentObject(hdc, OBJ_FONT));
		LOGFONTW base = {};
		::GetObjectW(baseFont, sizeof(base), &base);

		for (int i = 0; i < len; ++i)
		{
			const wchar_t ch = text[i];
			HFONT use = nullptr;
			const bool surrogate = (ch >= 0xD800 && ch <= 0xDFFF);
			if (idx[i] == 0xFFFF && !surrogate && ch >= 0x20)
			{
				for (int f = 0; f < _countof(kFallbackFaces); ++f)
				{
					HFONT cand = FallbackFont(base, f);
					if (!cand)
						continue;
					HGDIOBJ old = ::SelectObject(hdc, cand);
					const bool ok = HasGlyph(hdc, ch);
					::SelectObject(hdc, old);
					if (ok) { use = cand; break; }
				}
			}
			if (!runs.empty() && runs.back().font == use)
				runs.back().text += ch;
			else
				runs.push_back({ CString(ch), use });
		}
		return runs;
	}
}

namespace TextFB
{
	int Width(HDC hdc, const CString& text)
	{
		if (text.IsEmpty())
			return 0;
		int w = 0;
		for (const Run& r : SplitRuns(hdc, text))
		{
			if (!r.font)
			{
				w += RunWidth(hdc, r.text);
				continue;
			}
			HGDIOBJ old = ::SelectObject(hdc, r.font);
			w += RunWidth(hdc, r.text);
			::SelectObject(hdc, old);
		}
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
		const int baseline = y + tm.tmAscent;

		for (const Run& r : SplitRuns(hdc, s))
		{
			if (!r.font)
			{
				RunOut(hdc, r.text, x, y, rc);
				x += RunWidth(hdc, r.text);
				continue;
			}
			// 대체 글꼴: 기본 글꼴과 기준선(baseline)을 맞춰서 그림
			HGDIOBJ old = ::SelectObject(hdc, r.font);
			TEXTMETRICW ftm = {};
			::GetTextMetricsW(hdc, &ftm);
			RunOut(hdc, r.text, x, baseline - ftm.tmAscent, rc);
			x += RunWidth(hdc, r.text);
			::SelectObject(hdc, old);
		}
	}
}
