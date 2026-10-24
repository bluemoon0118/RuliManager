#pragma once

#include <atomic>

// 미리보기 영상 (영상 카드 오른쪽 클릭 [미리보기 만들기], 영상 상세 커버 이미지에 마우스를 올리면 재생)
//  - 만들기: ffmpeg.exe 로 영상의 여러 구간(기본 10곳 × 2초)을 잘라 이어 붙인 소리 없는 짧은 mp4 (폭 480px)
//    · ffmpeg.exe 찾는 순서: <실행 폴더>\ffmpeg.exe → <실행 폴더>\ffmpeg\bin\ffmpeg.exe → <DB 폴더>\ffmpeg.exe → PATH
//    · 저장: <DB 폴더>\previews\<키>.mp4 (키는 커버 캐시와 같은 규칙 - 이동식 디스크는 드라이브 문자가 바뀌어도 같음)
//    · 백그라운드 스레드에서 호출 (오래 걸림, cancel 이 켜지면 ffmpeg 를 끝내고 돌아옴)
//  - 재생: CPreviewPlayer (Media Foundation MFPlay, 소리 끔 · 반복)
namespace PreviewVideo
{
	CString FindFfmpeg();                       // 없으면 빈 문자열
	CString GetPreviewDir();                    // <DB 폴더>\previews (없으면 만듦)
	// 영상 → 미리보기 파일 경로 (coverCacheFile: CVideoLibrary::CoverCacheFile 결과 - 이동식이면 그 이름을 씀)
	CString PreviewFileFor(const CString& videoPath, const CString& coverCacheFile);

	struct Result
	{
		bool    ok = false;
		bool    canceled = false;
		CString video;      // 영상 경로
		CString file;       // 만든 미리보기 파일
		CString error;      // 실패 이유
	};
	// durationSec: 영상 길이 (0 이하면 ffmpeg 로 알아냄)
	Result Make(const CString& ffmpeg, const CString& videoPath, const CString& outFile, double durationSec, const std::atomic<bool>& cancel);
}

// ---------------------------------------------------------------------------
// 미리보기 재생 창: 소리 없이 반복 재생
//  - Start: 부모(예: 이미지 칸) 위에 같은 크기의 자식 창으로
//  - StartPopup: 카드 위에 겹친 팝업 창 (width > 0: 폭 고정 · 높이는 영상 비율, width <= 0: anchor 와 같은 크기 · 영상은 비율 유지 - 영상을 연 뒤 보임, 활성화하지 않음)
//  - 마우스는 그대로 아래 창이 받음 (WM_NCHITTEST = HTTRANSPARENT) → 카드 클릭 · 더블클릭 · 마우스 움직임 그대로
struct IMFPMediaPlayer;
class CPreviewPlayerCallback;

class CPreviewPlayer : public CWnd
{
public:
	CPreviewPlayer() = default;
	~CPreviewPlayer() override;

	bool Start(CWnd* parent, const CString& file, COLORREF border);   // 이미 재생 중이면 먼저 멈춤
	bool StartPopup(CWnd* owner, const CRect& anchorScreen, int width, const CString& file, COLORREF border);   // anchor 가운데에 겹쳐서 (width <= 0 = anchor 크기 그대로)
	void Stop();
	bool IsPlaying() const { return GetSafeHwnd() != nullptr; }
	const CString& File() const { return m_file; }
	void FitToParent();                          // 부모 크기가 바뀌었으면 맞춤
	void MoveToAnchor(const CRect& anchorScreen);   // 팝업: 겹칠 곳이 움직임 (스크롤) → 따라감

	void OnPlayerEvent(int type);                // 콜백에서 (같은 스레드)

protected:
	IMFPMediaPlayer*        m_player = nullptr;
	CPreviewPlayerCallback* m_cb = nullptr;
	CString                 m_file;
	COLORREF                m_border = RGB(0, 0, 0);
	bool                    m_popup = false;
	CRect                   m_anchor;     // 팝업: 겹칠 카드 (화면 좌표)
	int                     m_popupW = 480;
	void PlacePopup();                    // 팝업: 영상 비율로 크기 · 위치를 정하고 보임

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg LRESULT OnNcHitTest(CPoint point);
	DECLARE_MESSAGE_MAP()
};
