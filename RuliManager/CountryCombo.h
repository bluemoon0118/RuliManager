#pragma once

#include "DarkControls.h"

// 국적 콤보박스: [국기 이미지] 나라 이름 (직접 그림, 국기는 res\flags.bmp 가로 띠 24x16)
struct CountryInfo
{
	LPCWSTR code;   // ISO 3166-1 alpha-2
	LPCWSTR name;   // 표시/저장 이름
};

class CCountryCombo : public CDarkCombo   // 색상은 CDarkCombo::SetColors
{
public:
	static const int kFlagW = 24;
	static const int kFlagH = 16;
	static const CountryInfo* Countries(int& count);
	static int FindCountry(const CString& name);   // 없으면 -1

	// 국기 그리기 (높이가 16보다 작으면 비율 유지해서 줄여 그림), idx = Countries() 인덱스
	static void DrawFlag(CDC* pDC, int idx, int x, int y, int height = kFlagH);

	void FillCountries();                    // 첫 줄은 빈 칸(미지정)
	void SetCountry(const CString& name);    // 목록에 없는 값은 국기 없이 추가해서 선택
	CString GetCountry() const;

protected:
	void DrawItem(LPDRAWITEMSTRUCT lpDIS) override;
	void MeasureItem(LPMEASUREITEMSTRUCT lpMIS) override;
	int  CompareItem(LPCOMPAREITEMSTRUCT) override { return 0; }
	static CImageList& Flags();
};
