#include "pch.h"
#include "TextDraw.h"

#include <usp10.h>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
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

	// 한자 크기 보정: 기준 한자(中)의 잉크 높이 · 세로 가운데를 글꼴마다 잼 (GetGlyphOutline 메트릭)
	//  맑은 고딕과 일본어 글꼴(Yu Gothic · Meiryo 등)은 같은 pt 라도 한자를 그리는 크기 · 높이가 달라서,
	//  대체 글꼴로 그린 신자체 한자(覚 · 盗 · 会 등)만 작거나 크게 보임 → 대체 글꼴 크기를 기준 한자 높이가 같아지게 바꾸고 세로 위치도 맞춤
	bool IdeoMetrics(HDC hdc, HFONT font, int& inkH, int& centerY)
	{
		HGDIOBJ old = ::SelectObject(hdc, font);
		const MAT2 m = { {0, 1}, {0, 0}, {0, 0}, {0, 1} };
		GLYPHMETRICS gm = {};
		WORD gi = 0xFFFF;
		const wchar_t ref = L'\x4E2D';   // 中
		bool ok = ::GetGlyphIndicesW(hdc, &ref, 1, &gi, GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR && gi != 0xFFFF &&
			::GetGlyphOutlineW(hdc, ref, GGO_METRICS, &gm, 0, nullptr, &m) != GDI_ERROR && gm.gmBlackBoxY > 0;
		::SelectObject(hdc, old);
		if (!ok)
			return false;
		inkH = static_cast<int>(gm.gmBlackBoxY);
		centerY = gm.gmptGlyphOrigin.y - inkH / 2;   // 기준선에서 잉크 세로 가운데까지 (위쪽 +)
		return true;
	}

	struct FbFont { HFONT font = nullptr; int dy = 0; };   // dy: 기준선 기준 세로 보정 (아래 +, 픽셀)

	// 기본 글꼴과 같은 굵기의 대체 글꼴 - 크기는 한자 높이가 같아지게 (만든 글꼴은 프로그램 끝까지 보관)
	const FbFont& FallbackFont(HDC hdc, const LOGFONTW& base, int faceIndex)
	{
		static std::map<std::wstring, FbFont> cache;
		wchar_t key[200];
		swprintf_s(key, L"%d|%ld|%ld|%d|%d|%d|%s", faceIndex, base.lfHeight, base.lfWeight,
			base.lfItalic, base.lfUnderline, base.lfQuality, base.lfFaceName);
		auto it = cache.find(key);
		if (it != cache.end())
			return it->second;
		FbFont& out = cache[key];
		LOGFONTW lf = base;
		lf.lfWidth = 0;
		lf.lfCharSet = DEFAULT_CHARSET;
		wcscpy_s(lf.lfFaceName, kFallbackFaces[faceIndex]);
		HFONT f = ::CreateFontIndirectW(&lf);
		if (!f)
			return out;
		HFONT baseFont = ::CreateFontIndirectW(&base);
		int bh = 0, bc = 0, fh = 0, fc = 0;
		if (baseFont && IdeoMetrics(hdc, baseFont, bh, bc) && IdeoMetrics(hdc, f, fh, fc) && fh > 0 && base.lfHeight != 0)
		{
			double ratio = static_cast<double>(bh) / fh;
			ratio = (std::max)(0.8, (std::min)(1.25, ratio));
			if (ratio < 0.98 || ratio > 1.02)
			{
				LOGFONTW sl = lf;
				sl.lfHeight = static_cast<LONG>(base.lfHeight * ratio + (base.lfHeight < 0 ? -0.5 : 0.5));
				if (HFONT scaled = ::CreateFontIndirectW(&sl))
				{
					::DeleteObject(f);
					f = scaled;
					IdeoMetrics(hdc, f, fh, fc);
				}
			}
			out.dy = fc - bc;   // 대체 글꼴 한자 가운데가 더 높으면 아래로 (+)
		}
		if (baseFont)
			::DeleteObject(baseFont);
		out.font = f;
		return out;
	}

	bool HasGlyph(HDC hdc, wchar_t ch)
	{
		WORD idx = 0xFFFF;
		if (::GetGlyphIndicesW(hdc, &ch, 1, &idx, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR)
			return true;   // 확인 불가 → 그대로
		return idx != 0xFFFF;
	}

	struct Run { CString text; HFONT font; int dy; };   // font = nullptr → 현재(기본) 글꼴, dy: 세로 보정

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
			runs.push_back({ text, nullptr, 0 });   // 빠른 길: 모든 글자가 현재 글꼴에 있음
			return runs;
		}

		HFONT baseFont = static_cast<HFONT>(::GetCurrentObject(hdc, OBJ_FONT));
		LOGFONTW base = {};
		::GetObjectW(baseFont, sizeof(base), &base);

		for (int i = 0; i < len; ++i)
		{
			const wchar_t ch = text[i];
			HFONT use = nullptr;
			int useDy = 0;
			const bool surrogate = (ch >= 0xD800 && ch <= 0xDFFF);
			if (idx[i] == 0xFFFF && !surrogate && ch >= 0x20)
			{
				for (int f = 0; f < _countof(kFallbackFaces); ++f)
				{
					const FbFont& fb = FallbackFont(hdc, base, f);
					if (!fb.font)
						continue;
					HGDIOBJ old = ::SelectObject(hdc, fb.font);
					const bool ok = HasGlyph(hdc, ch);
					::SelectObject(hdc, old);
					if (ok) { use = fb.font; useDy = fb.dy; break; }
				}
			}
			if (!runs.empty() && runs.back().font == use)
				runs.back().text += ch;
			else
				runs.push_back({ CString(ch), use, useDy });
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

	bool HasMissing(HDC hdc, const CString& text)
	{
		const int len = text.GetLength();
		if (len == 0)
			return false;
		std::vector<WORD> idx(len, 0);
		if (::GetGlyphIndicesW(hdc, text, len, idx.data(), GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR)
			return false;
		for (int i = 0; i < len; ++i)
		{
			const wchar_t ch = text[i];
			if (idx[i] == 0xFFFF && ch >= 0x20 && !(ch >= 0xD800 && ch <= 0xDFFF))
				return true;
		}
		return false;
	}

	// 한 줄 그리기: y = 줄 위쪽 (vcenter 면 rc 세로 가운데)
	static void DrawLine(HDC hdc, const CString& text, const CRect& rc, UINT align, bool ellipsis, bool vcenter, int topY);

	void Draw(HDC hdc, const CString& text, const CRect& rc, UINT align, bool ellipsis)
	{
		DrawLine(hdc, text, rc, align, ellipsis, true, rc.top);
	}

	static void DrawLine(HDC hdc, const CString& text, const CRect& rc, UINT align, bool ellipsis, bool vcenter, int topY)
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
		const int y = vcenter ? rc.top + (rc.Height() - tm.tmHeight) / 2 : topY;
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
			RunOut(hdc, r.text, x, baseline - ftm.tmAscent + r.dy, rc);   // 한자 세로 가운데를 기본 글꼴과 맞춤
			x += RunWidth(hdc, r.text);
			::SelectObject(hdc, old);
		}
	}

	int DrawFmt(HDC hdc, const CString& text, LPRECT prc, UINT fmt)
	{
		// 기본 글꼴에 다 있는 글자 · 접두어(&) 처리 · 경로 줄임은 Windows DrawText 그대로 (예전과 같음)
		if (!prc || text.IsEmpty() || !(fmt & DT_NOPREFIX) || (fmt & (DT_PATH_ELLIPSIS | DT_EXPANDTABS | DT_TABSTOP)) || !HasMissing(hdc, text))
			return ::DrawTextW(hdc, text, text.GetLength(), prc, fmt);

		CRect rc(*prc);
		TEXTMETRICW tm = {};
		::GetTextMetricsW(hdc, &tm);
		const int lineH = tm.tmHeight;
		const UINT align = (fmt & DT_CENTER) ? DT_CENTER : ((fmt & DT_RIGHT) ? DT_RIGHT : DT_LEFT);
		const bool ellipsis = (fmt & (DT_END_ELLIPSIS | DT_WORD_ELLIPSIS)) != 0;

		if (fmt & DT_SINGLELINE)
		{
			if (fmt & DT_CALCRECT)
			{
				prc->right = prc->left + Width(hdc, text);
				prc->bottom = prc->top + lineH;
				return lineH;
			}
			int top = rc.top;
			if (fmt & DT_VCENTER)      top = rc.top + (rc.Height() - lineH) / 2;
			else if (fmt & DT_BOTTOM)  top = rc.bottom - lineH;
			DrawLine(hdc, text, rc, align, ellipsis, false, top);
			return lineH;
		}

		// 여러 줄: 줄바꿈 문자 + (DT_WORDBREAK 면) 폭에 맞춰 자름 - 공백에서 우선, 안 되면 글자 단위
		std::vector<CString> lines;
		const int maxW = (std::max)(1, static_cast<int>(rc.Width()));
		CString t = text;
		t.Replace(L"\r\n", L"\n");
		t.Replace(L'\r', L'\n');
		int pos = 0;
		while (pos <= t.GetLength())
		{
			int nl = t.Find(L'\n', pos);
			if (nl < 0) nl = t.GetLength();
			CString para = t.Mid(pos, nl - pos);
			pos = nl + 1;
			if (!(fmt & DT_WORDBREAK) || Width(hdc, para) <= maxW)
			{
				lines.push_back(para);
				continue;
			}
			while (!para.IsEmpty())
			{
				int lo = 1, hi = para.GetLength();   // 들어가는 최대 글자 수 (이분 탐색)
				while (lo < hi)
				{
					const int mid = (lo + hi + 1) / 2;
					if (Width(hdc, para.Left(mid)) <= maxW) lo = mid; else hi = mid - 1;
				}
				int cut = lo;
				if (cut < para.GetLength())
				{
					const int sp = para.Left(cut + 1).ReverseFind(L' ');
					if (sp > 0) cut = sp + 1;   // 공백 뒤에서 자름
				}
				CString line = para.Left(cut);
				line.TrimRight();
				lines.push_back(line);
				para = para.Mid(cut);
				para.TrimLeft(L' ');
			}
		}
		if (lines.empty())
			lines.push_back(CString());
		const int totalH = lineH * static_cast<int>(lines.size());
		if (fmt & DT_CALCRECT)
		{
			int w = 0;
			for (const CString& l : lines) w = (std::max)(w, Width(hdc, l));
			if (!(fmt & DT_WORDBREAK)) prc->right = prc->left + w;
			prc->bottom = prc->top + totalH;
			return totalH;
		}
		const int visible = (std::max)(1, static_cast<int>(rc.Height()) / (std::max)(1, lineH));
		int top = rc.top;
		if (fmt & DT_VCENTER)      top = rc.top + (rc.Height() - (std::min)(totalH, visible * lineH)) / 2;
		else if (fmt & DT_BOTTOM)  top = rc.bottom - (std::min)(totalH, visible * lineH);
		const int n = (std::min)(visible, static_cast<int>(lines.size()));
		for (int i = 0; i < n; ++i)
		{
			CString l = lines[i];
			const bool last = (i == n - 1) && n < static_cast<int>(lines.size());
			if (last && ellipsis)
				l += L"\x2026";   // 남은 줄이 있으면 마지막 보이는 줄 끝에 "…"
			DrawLine(hdc, l, CRect(rc.left, top, rc.right, top + lineH), align, ellipsis, false, top);
			top += lineH;
		}
		return totalH;
	}
}
