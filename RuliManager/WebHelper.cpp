#include "pch.h"
#include "WebHelper.h"
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace Web
{
	bool HttpGet(const CString& url, std::vector<BYTE>& out, CString* contentType, CString* err, size_t maxBytes, LPCWSTR headers, CString* setCookies)
	{
		out.clear();
		auto fail = [&](LPCWSTR why) { if (err) *err = why; return false; };

		URL_COMPONENTS uc = {};
		uc.dwStructSize = sizeof(uc);
		wchar_t host[256] = {};
		std::vector<wchar_t> path(4096, 0);
		std::vector<wchar_t> extra(4096, 0);
		uc.lpszHostName = host;      uc.dwHostNameLength = _countof(host);
		uc.lpszUrlPath = path.data(); uc.dwUrlPathLength = static_cast<DWORD>(path.size());
		uc.lpszExtraInfo = extra.data(); uc.dwExtraInfoLength = static_cast<DWORD>(extra.size());
		if (!::WinHttpCrackUrl(url, 0, 0, &uc))
			return fail(L"잘못된 주소");
		const bool secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
		CString object = CString(path.data()) + CString(extra.data());
		if (object.IsEmpty())
			object = L"/";

		HINTERNET session = ::WinHttpOpen(
			L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36",
			WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!session)
			return fail(L"인터넷 연결을 열 수 없음");
		::WinHttpSetTimeouts(session, 5000, 8000, 8000, 15000);
		bool ok = false;
		HINTERNET connect = ::WinHttpConnect(session, host, uc.nPort, 0);
		HINTERNET request = connect ? ::WinHttpOpenRequest(connect, L"GET", object, nullptr,
			WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0) : nullptr;
		if (request &&
			::WinHttpSendRequest(request, headers ? headers : WINHTTP_NO_ADDITIONAL_HEADERS, headers ? static_cast<DWORD>(-1L) : 0,
				WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
			::WinHttpReceiveResponse(request, nullptr))
		{
			DWORD status = 0, size = sizeof(status);
			::WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
			if (setCookies)
			{
				// Set-Cookie 여러 개 → "이름=값; 이름=값"
				for (DWORD index = 0;;)
				{
					wchar_t ck[2048] = {};
					DWORD ckSize = sizeof(ck);
					const DWORD before = index;
					if (!::WinHttpQueryHeaders(request, WINHTTP_QUERY_SET_COOKIE, WINHTTP_HEADER_NAME_BY_INDEX,
						ck, &ckSize, &index) || index == before)
						break;
					CString c = ck;
					const int semi = c.Find(L';');
					if (semi >= 0) c = c.Left(semi);
					c.Trim();
					if (!c.IsEmpty())
					{
						if (!setCookies->IsEmpty()) *setCookies += L"; ";
						*setCookies += c;
					}
				}
			}
			if (contentType)
			{
				wchar_t ct[256] = {};
				DWORD ctSize = sizeof(ct);
				if (::WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX,
					ct, &ctSize, WINHTTP_NO_HEADER_INDEX))
					*contentType = ct;
			}
			for (;;)
			{
				DWORD avail = 0;
				if (!::WinHttpQueryDataAvailable(request, &avail) || avail == 0)
					break;
				const size_t old = out.size();
				if (old + avail > maxBytes)
				{
					out.clear();
					if (err) *err = L"파일이 너무 큼";
					break;
				}
				out.resize(old + avail);
				DWORD read = 0;
				if (!::WinHttpReadData(request, out.data() + old, avail, &read))
				{
					out.clear();
					break;
				}
				out.resize(old + read);
			}
			ok = !out.empty() && status == 200;
			if (!ok && err && err->IsEmpty())
				err->Format(L"응답 오류 (HTTP %u)", status);
			if (status != 200 && status != 0 && !out.empty())
				ok = false;   // 오류 응답 본문은 out 에 남겨 둠 (API 오류 메시지 확인용)
		}
		else if (err)
			*err = L"서버에 연결하지 못함";
		if (request) ::WinHttpCloseHandle(request);
		if (connect) ::WinHttpCloseHandle(connect);
		::WinHttpCloseHandle(session);
		return ok;
	}

	CString UrlEncode(const CString& text)
	{
		const CStringA utf8(CW2A(text, CP_UTF8));
		CString out;
		for (int i = 0; i < utf8.GetLength(); ++i)
		{
			const unsigned char c = static_cast<unsigned char>(utf8[i]);
			if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
				c == '-' || c == '_' || c == '.' || c == '~')
				out += static_cast<wchar_t>(c);
			else
				out.AppendFormat(L"%%%02X", c);
		}
		return out;
	}

	CString HtmlDecode(const CString& text)
	{
		CString s = text;
		s.Replace(L"&quot;", L"\"");
		s.Replace(L"&#39;", L"'");
		s.Replace(L"&#34;", L"\"");
		s.Replace(L"&lt;", L"<");
		s.Replace(L"&gt;", L">");
		s.Replace(L"&amp;", L"&");   // 마지막에 (&amp;quot; 같은 이중 인코딩 보호)
		return s;
	}

	CString Utf8ToString(const std::vector<BYTE>& data)
	{
		if (data.empty())
			return CString();
		const CStringA a(reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()));
		return CString(CA2W(a, CP_UTF8));
	}

	std::vector<CString> JsonStrings(const CString& json, LPCWSTR key)
	{
		std::vector<CString> out;
		const CString pat = CString(L"\"") + key + L"\"";
		int pos = 0;
		for (;;)
		{
			int p = json.Find(pat, pos);
			if (p < 0)
				break;
			p += pat.GetLength();
			while (p < json.GetLength() && (json[p] == L' ' || json[p] == L'\t' || json[p] == L'\r' || json[p] == L'\n'))
				++p;
			if (p >= json.GetLength() || json[p] != L':')
			{
				pos = p;
				continue;
			}
			++p;
			while (p < json.GetLength() && (json[p] == L' ' || json[p] == L'\t' || json[p] == L'\r' || json[p] == L'\n'))
				++p;
			if (p >= json.GetLength() || json[p] != L'"')
			{
				pos = p;
				continue;
			}
			++p;
			CString val;
			while (p < json.GetLength() && json[p] != L'"')
			{
				wchar_t c = json[p];
				if (c == L'\\' && p + 1 < json.GetLength())
				{
					const wchar_t e = json[++p];
					switch (e)
					{
					case L'n': c = L'\n'; break;
					case L't': c = L'\t'; break;
					case L'r': c = L'\r'; break;
					case L'b': c = L'\b'; break;
					case L'f': c = L'\f'; break;
					case L'u':
						if (p + 4 < json.GetLength())
						{
							c = static_cast<wchar_t>(wcstoul(json.Mid(p + 1, 4), nullptr, 16));
							p += 4;
						}
						break;
					default: c = e; break;   // \" \\ \/
					}
				}
				val += c;
				++p;
			}
			out.push_back(val);
			pos = p + 1;
		}
		return out;
	}

	std::vector<int> JsonNumbers(const CString& json, LPCWSTR key)
	{
		std::vector<int> out;
		const CString pat = CString(L"\"") + key + L"\"";
		int pos = 0;
		for (;;)
		{
			int p = json.Find(pat, pos);
			if (p < 0)
				break;
			p += pat.GetLength();
			while (p < json.GetLength() && (json[p] == L' ' || json[p] == L'\t'))
				++p;
			if (p < json.GetLength() && json[p] == L':')
			{
				++p;
				while (p < json.GetLength() && (json[p] == L' ' || json[p] == L'\t'))
					++p;
				if (p < json.GetLength() && iswdigit(json[p]))
					out.push_back(_wtoi(json.Mid(p, 12)));
			}
			pos = p;
		}
		return out;
	}
}
