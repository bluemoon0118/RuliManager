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
	void ForgetFailed();                 // 읽지 못한 경로의 기억을 지움 (다음 SetFile 때 다시 읽음)
	void Forget(const CString& path);    // 그 경로의 기억을 지움 (영상 새로고침 - 다음 SetFile 때 다시 읽음)
	int  NeededWidth();                  // 지금 내용을 그리는 데 필요한 폭 (픽셀, 내용 없으면 0)

	static Info    ReadInfo(const CString& path);               // 속성에서 읽기 (캐시 없음)
	// 디스크 캐시(mediainfo.vmdb)를 거쳐 읽기: 파일 크기 · 수정 시각이 같으면 저장해 둔 값, 아니면 ReadInfo 후 기억 (스레드 안전)
	static Info    ReadInfoCached(const CString& path);
	// 디스크 캐시: 시작할 때 불러오고 끝날 때 저장 (암호화 - DbCrypt), keep 이 있으면 그 경로(소문자)만 남김
	static void    LoadDiskCache(const CString& file);
	static void    SaveDiskCache(const std::set<CString>* keep = nullptr);
	static void    ForgetDisk(const CString& path);             // 그 경로를 디스크 캐시에서 지움 (영상 새로고침)
	static CString ResolutionText(UINT width, UINT height);     // 1920x1080 → "1080p", 3840x2160 → "2160p"
	// 해상도 마크: 반환 = 등급 "SD" / "HD" / "FHD" / "QHD" / "UHD", resText = 큰 글자 "480p" / "720p" / "1080p" / "1440p" / "4K" / "8K"
	static CString GradeText(UINT width, UINT height, CString* resText = nullptr, COLORREF* color = nullptr);
	static CString FpsText(double fps);                         // 29.97 / 30 / 23.976 → "23.98"
	static CString DurationText(ULONGLONG duration100ns);       // 총 재생 시간 → "123분" (반올림, 1분 미만은 "1분")

protected:
	Info     m_info;
	CString  m_path;
	COLORREF m_back = TCOL(TC_BACK);
	COLORREF m_sub = TCOL(TC_SUB);
	COLORREF m_text = TCOL(TC_BRIGHT);
	CFont    m_bold;
	std::map<CString, Info> m_cache;   // 경로(소문자) → 정보

	CFont* BaseFont();
	void   EnsureBold();
	void   BuildParts(CString& left, CString& sep, CString& res) const;
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	DECLARE_MESSAGE_MAP()
};
