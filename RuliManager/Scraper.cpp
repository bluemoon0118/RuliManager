#include "pch.h"
#include "Scraper.h"
#include "WebHelper.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	const wchar_t kAvdbsHost[] = L"https://www.avdbs.com";

	// 태그 제거 + HTML 엔티티 풀기 + 공백 정리
	CString StripTags(const CString& html)
	{
		CString out;
		bool inTag = false;
		for (int i = 0; i < html.GetLength(); ++i)
		{
			const wchar_t c = html[i];
			if (c == L'<') { inTag = true; out += L' '; continue; }
			if (c == L'>') { inTag = false; continue; }
			if (!inTag) out += c;
		}
		out = Web::HtmlDecode(out);
		out.Replace(L"&nbsp;", L" ");
		out.Replace(L'\x00A0', L' ');
		out.Replace(L'\t', L' ');
		out.Replace(L'\r', L' ');
		out.Replace(L'\n', L' ');
		while (out.Replace(L"  ", L" ") > 0) {}
		out.Trim();
		return out;
	}

	// from 위치부터 begin ~ end 사이 글자 (없으면 빈 문자열), next 에 end 다음 위치
	CString Between(const CString& s, LPCWSTR begin, LPCWSTR end, int from = 0, int* next = nullptr)
	{
		const int a = s.Find(begin, from);
		if (a < 0) return CString();
		const int b = a + static_cast<int>(wcslen(begin));
		const int e = s.Find(end, b);
		if (e < 0) return CString();
		if (next) *next = e + static_cast<int>(wcslen(end));
		return s.Mid(b, e - b);
	}

	// <span>라벨:</span> 값</p> 의 값 부분 (HTML 그대로)
	CString DetailHtml(const CString& html, LPCWSTR label)
	{
		CString key;
		key.Format(L"<span>%s:</span>", label);
		return Between(html, key, L"</p>");
	}

	// HTML 조각 안의 <a ...>글자</a> 들
	std::vector<CString> AnchorTexts(const CString& frag, LPCWSTR mustContain = nullptr)
	{
		std::vector<CString> out;
		int pos = 0;
		for (;;)
		{
			const int a = frag.Find(L"<a ", pos);
			if (a < 0) break;
			const int gt = frag.Find(L'>', a);
			const int e = (gt >= 0) ? frag.Find(L"</a>", gt) : -1;
			if (gt < 0 || e < 0) break;
			pos = e + 4;
			if (mustContain && frag.Mid(a, gt - a).Find(mustContain) < 0)
				continue;
			CString t = StripTags(frag.Mid(gt + 1, e - gt - 1));
			t.TrimLeft(L'#');
			t.Trim();
			if (!t.IsEmpty())
				out.push_back(t);
		}
		return out;
	}

	CString Join(const std::vector<CString>& list, LPCWSTR sep)
	{
		CString r;
		for (const CString& s : list) { if (!r.IsEmpty()) r += sep; r += s; }
		return r;
	}

	// 품번 비교용: 영숫자만 대문자 (SONE-479 = sone479)
	CString CodeKey(const CString& s)
	{
		CString k;
		for (int i = 0; i < s.GetLength(); ++i)
		{
			const wchar_t c = s[i];
			if ((c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z')) k += c;
			else if (c >= L'a' && c <= L'z') k += static_cast<wchar_t>(c - 32);
		}
		return k;
	}

	bool GetPage(const CString& url, CString& html, CString& err)
	{
		std::vector<BYTE> data;
		if (!Web::HttpGet(url, data, nullptr, &err, 8 * 1024 * 1024))
			return false;
		html = Web::Utf8ToString(data);
		return !html.IsEmpty();
	}

	// 검색 결과 HTML 에서 품번과 맞는 작품의 dvd_idx 찾기 (링크 주변 글자에 품번이 있는 것)
	CString FindDvdIdx(const CString& html, const CString& code)
	{
		const CString want = CodeKey(code);
		CString first;
		int pos = 0;
		for (;;)
		{
			const int a = html.Find(L"dvd_idx=", pos);
			if (a < 0) break;
			int b = a + 8;
			CString idx;
			while (b < html.GetLength() && html[b] >= L'0' && html[b] <= L'9')
				idx += html[b++];
			pos = b;
			if (idx.IsEmpty())
				continue;
			// 링크 앞뒤 800자 안에 품번이 있으면 그 작품
			const int s0 = (std::max)(0, a - 300);
			const CString around = StripTags(html.Mid(s0, 1100));
			if (!want.IsEmpty() && CodeKey(around).Find(want) >= 0)
			{
				// 다른 품번(SONE-4790 등)과 헷갈리지 않게: 품번 바로 뒤가 숫자가 아니어야 함
				const CString ak = CodeKey(around);
				const int f = ak.Find(want);
				if (f >= 0 && (f + want.GetLength() >= ak.GetLength() || !(ak[f + want.GetLength()] >= L'0' && ak[f + want.GetLength()] <= L'9')))
					return idx;
			}
			if (first.IsEmpty())
				first = idx;
		}
		return CString();
	}
}

namespace Scraper
{
	bool ParseAvdbsDvdPage(const CString& html, CString& info)
	{
		info.Empty();
		auto add = [&info](LPCWSTR key, const CString& val)
		{
			CString v = val;
			v.Trim();
			if (v.IsEmpty()) return;
			info += key;
			info += L": ";
			info += v;
			info += L"\r\n";
		};

		// 품번: <span class="inner_name_kr">품번: SONE-479</span>
		CString code = StripTags(Between(html, L"품번:", L"</span>"));
		if (code.IsEmpty())
			code = StripTags(Between(html, L"<span class=\"tomato\">", L"</span>"));
		// 제목: 한국어(title_kr) 우선, 없으면 원문(title_jp)
		CString title;
		{
			const int k = html.Find(L"id=\"title_kr\"");
			if (k >= 0) title = StripTags(Between(html, L">", L"</span>", k));
			if (title.IsEmpty())
			{
				const int j = html.Find(L"id=\"title_jp\"");
				if (j >= 0) title = StripTags(Between(html, L">", L"</span>", j));
			}
		}
		const CString release = StripTags(DetailHtml(html, L"출시"));
		std::vector<CString> cast = AnchorTexts(DetailHtml(html, L"출연"), L"actor_idx=");
		std::vector<CString> makers = AnchorTexts(DetailHtml(html, L"제작사"));
		CString maker = makers.empty() ? StripTags(DetailHtml(html, L"제작사")) : makers[0];
		maker.TrimLeft(L'#');
		const CString label = StripTags(DetailHtml(html, L"레이블"));

		// 출연자가 한 명이면 제목 줄(h1)의 "한글 / 일어 / 영어" 이름을 모아 "한글(English, 日本語)" 로 (배우 언어 단위 연결용)
		if (cast.size() == 1)
		{
			const CString h1 = Between(html, L"<h1 class=\"title\">", L"</h1>");
			std::vector<CString> names;
			int pos = h1.Find(L"color:#0d9fa9");
			while (pos >= 0)
			{
				int next = -1;
				const CString n = StripTags(Between(h1, L"<span>", L"</span>", pos, &next));
				if (next < 0) break;
				if (!n.IsEmpty()) names.push_back(n);
				pos = next;
			}
			// names: [한글, 일어, 영어] 순서
			if (names.size() >= 2 && names[0].CompareNoCase(cast[0]) == 0)
			{
				CString inner;
				if (names.size() >= 3) inner = names[2];                 // 영어
				if (!names[1].IsEmpty()) inner += (inner.IsEmpty() ? L"" : L", ") + names[1];   // 일어
				if (!inner.IsEmpty())
					cast[0] = names[0] + L"(" + inner + L")";
			}
		}

		// 장르: 작품 정보의 "장르 상세" (<div class="genre"> 안의 gen_text)
		std::vector<CString> genres;
		{
			const int g = html.Find(L"<div class=\"genre\">");
			if (g >= 0)
			{
				const int ge = html.Find(L"</ul>", g);
				const CString frag = html.Mid(g, (ge > g ? ge : g + 4000) - g);
				genres = AnchorTexts(frag, L"gen_text");
			}
		}

		add(L"품번", code);
		add(L"제목", title);
		add(L"발매일", release);
		add(L"배우", Join(cast, L", "));
		// 스튜디오: 레이블 / 제작사 (등록된 스튜디오 · 서브이름과 맞는 쪽이 쓰이고, 둘 다 없으면 레이블)
		if (!label.IsEmpty() && !maker.IsEmpty() && label.CompareNoCase(maker) != 0)
			add(L"스튜디오", label + L" / " + maker);
		else
			add(L"스튜디오", label.IsEmpty() ? maker : label);
		add(L"태그", Join(genres, L", "));
		return !code.IsEmpty() || !title.IsEmpty();
	}

	bool FetchAvdbs(const CString& query, CString& info, CString& pageUrl, CString& err)
	{
		CString q = query;
		q.Trim();
		if (q.IsEmpty()) { err = L"주소나 품번을 입력하세요."; return false; }

		// 1) 주소 / dvd_idx 숫자 → 작품 페이지
		if (q.Find(L"avdbs.com") >= 0 && q.Find(L"dvd_idx=") >= 0)
			pageUrl = q.Find(L"://") >= 0 ? q : (L"https://" + q);
		else if (q.SpanIncluding(L"0123456789") == q)
			pageUrl = CString(kAvdbsHost) + L"/menu/dvd.php?dvd_idx=" + q;
		else
		{
			// 2) 품번 → 사이트 검색 결과에서 그 품번의 작품 찾기
			CString html;
			CString idx;
			const CString enc = Web::UrlEncode(q);
			if (GetPage(CString(kAvdbsHost) + L"/menu/search.php?kwd=" + enc, html, err))
				idx = FindDvdIdx(html, q);
			if (idx.IsEmpty())
			{
				// 검색어 기록 API 가 seq 를 주는 경우 그 주소로 다시
				CString json;
				if (GetPage(CString(kAvdbsHost) + L"/w2017/api/iux_kwd_srch_log2.php?op=srch&kwd=" + enc, json, err))
				{
					std::vector<int> seq = Web::JsonNumbers(json, L"seq");
					if (!seq.empty())
					{
						CString url;
						url.Format(L"%s/menu/search.php?kwd=%s&seq=%d", kAvdbsHost, static_cast<LPCWSTR>(enc), seq[0]);
						if (GetPage(url, html, err))
							idx = FindDvdIdx(html, q);
					}
				}
			}
			if (idx.IsEmpty())
			{
				err = L"AVDBS 검색에서 '" + q + L"' 작품을 찾지 못했습니다.\n작품 페이지 주소를 직접 넣어 주세요.";
				return false;
			}
			pageUrl = CString(kAvdbsHost) + L"/menu/dvd.php?dvd_idx=" + idx;
		}

		CString html;
		if (!GetPage(pageUrl, html, err))
		{
			if (err.IsEmpty()) err = L"페이지를 받지 못했습니다.";
			return false;
		}
		if (!ParseAvdbsDvdPage(html, info))
		{
			err = L"작품 정보를 찾지 못했습니다. (페이지 구조가 바뀌었거나 접근이 막혔을 수 있음)";
			return false;
		}
		return true;
	}
}
