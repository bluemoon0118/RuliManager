#pragma once

#include <vector>

// 간단한 HTTP(S) GET (WinHTTP) + URL 인코딩 + JSON 문자열 꺼내기 + HTML 엔티티 풀기
namespace Web
{
	// url 내용을 out 에 받음 (최대 maxBytes). 성공(200)이면 true, 실패 시 err 에 이유
	bool HttpGet(const CString& url, std::vector<BYTE>& out, CString* contentType = nullptr,
		CString* err = nullptr, size_t maxBytes = 20 * 1024 * 1024, LPCWSTR headers = nullptr,
		CString* setCookies = nullptr);   // setCookies: 응답의 Set-Cookie 들을 "a=1; b=2" 로 (다음 요청의 Cookie 헤더용)
	CString HtmlDecode(const CString& text);         // &quot; &amp; &lt; &gt; &#39; 등 풀기
	CString UrlEncode(const CString& text);          // UTF-8 퍼센트 인코딩
	CString Utf8ToString(const std::vector<BYTE>& data);

	// JSON 텍스트에서 "key": "값" 을 순서대로 모두 꺼냄 (중첩 무시, 이스케이프 풀기)
	std::vector<CString> JsonStrings(const CString& json, LPCWSTR key);
	// JSON 텍스트에서 "key": 숫자 를 순서대로 모두 꺼냄
	std::vector<int> JsonNumbers(const CString& json, LPCWSTR key);
}
