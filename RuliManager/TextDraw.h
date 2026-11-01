#pragma once

// 글꼴 대체(폰트 링크/폴백)를 하는 한 줄 글자 그리기 (Uniscribe ScriptString)
// - 한국어 글꼴에 없는 일본어 한자(凪 등)·기호도 다른 글꼴로 대신 그려서 빈칸으로 보이지 않음
namespace TextFB
{
	int  Width(HDC hdc, const CString& text);                                     // 글자 폭 (픽셀)
	// rc 안에 세로 가운데로 한 줄 그리기. align = DT_LEFT / DT_CENTER / DT_RIGHT, ellipsis = 넘치면 "…"
	void Draw(HDC hdc, const CString& text, const CRect& rc, UINT align = DT_LEFT, bool ellipsis = true);
	inline int  Width(CDC* dc, const CString& text) { return Width(dc->GetSafeHdc(), text); }
	inline void Draw(CDC* dc, const CString& text, const CRect& rc, UINT align = DT_LEFT, bool ellipsis = true) { Draw(dc->GetSafeHdc(), text, rc, align, ellipsis); }

	// DrawText 대신 쓰는 함수 (같은 인자 · 같은 결과): 기본 글꼴에 없는 글자(일본 신자체 한자 등)가 있을 때만
	//  글자 단위 대체 + 한자 크기 · 세로 위치 보정으로 그림 (한 줄 · 여러 줄 · DT_CALCRECT · 정렬 · "…"), 없으면 Windows DrawText 그대로
	int DrawFmt(HDC hdc, const CString& text, LPRECT rc, UINT fmt);
	inline int DrawFmt(CDC* dc, const CString& text, LPRECT rc, UINT fmt) { return DrawFmt(dc->GetSafeHdc(), text, rc, fmt); }
	inline int DrawFmt(CDC* dc, LPCWSTR text, int len, LPRECT rc, UINT fmt) { return DrawFmt(dc->GetSafeHdc(), len < 0 ? CString(text) : CString(text, len), rc, fmt); }
}
