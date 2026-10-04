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
}
