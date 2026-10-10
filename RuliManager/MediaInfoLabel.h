#pragma once

// 영상 상세 정보: [이미지 변경] 버튼 위 오른쪽에 "123분 | fps: 29.97 | 1080p" 표시
//  - fps / 구분선은 회색, 해상도는 흰색 굵게, 오른쪽 정렬
//  - 값은 Windows 속성(파일 탐색기의 "길이 / 프레임 속도 / 프레임 너비·높이")에서 읽고, 없는 값은 Media Foundation 으로 파일을 직접 열어 읽음 - 경로별로 기억
class CMediaInfoLabel : public CWnd
{
public:
	struct Info
	{
		bool   ok = false;
		double fps = 0;     // 초당 프레임 (0 = 모름)
		UINT   width = 0;
		UINT   height = 0;
		ULONGLONG duration100ns = 0;   // 총 재생 시간 (100ns 단위, 0 = 모름)
	};

	bool Create(CWnd* parent, UINT id);
	void SetColors(COLORREF back, COLORREF sub, COLORREF text);
	void SetFile(const CString& path);   // 빈 문자열 = 표시 없음
	int  NeededWidth();                  // 지금 내용을 그리는 데 필요한 폭 (픽셀, 내용 없으면 0)

	static Info    ReadInfo(const CString& path);               // 속성에서 읽기 (캐시 없음)
	static CString ResolutionText(UINT width, UINT height);     // 1920x1080 → "1080p", 3840x2160 → "2160p"
	static CString FpsText(double fps);                         // 29.97 / 30 / 23.976 → "23.98"
	static CString DurationText(ULONGLONG duration100ns);       // 총 재생 시간 → "123분" (반올림, 1분 미만은 "1분")

protected:
	Info     m_info;
	CString  m_path;
	COLORREF m_back = RGB(0x20, 0x2B, 0x33);
	COLORREF m_sub = RGB(0xA7, 0xB6, 0xC2);
	COLORREF m_text = RGB(0xF5, 0xF8, 0xFA);
	CFont    m_bold;
	std::map<CString, Info> m_cache;   // 경로(소문자) → 정보

	CFont* BaseFont();
	void   EnsureBold();
	void   BuildParts(CString& left, CString& sep, CString& res) const;
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	DECLARE_MESSAGE_MAP()
};
