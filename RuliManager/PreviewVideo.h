#pragma once

#include <atomic>
#include <functional>

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
	bool StartAt(CWnd* parent, const CRect& rcInParent, const CString& file, COLORREF border);   // 부모 안 rc 자리에 자식 창으로 (영상 [월 모드] 칸)
	void MoveTo(const CRect& rcInParent);           // 자식 창 자리 옮기기
	void Pause();                                    // 일시정지 (월 모드: 프로그램이 뒤에 있거나 스크롤 중)
	void Resume();                                   // 이어서 재생
	bool IsPaused() const { return m_paused; }
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
	bool                    m_paused = false;
	bool                    m_showOnResume = false;   // 멈춘 동안 영상이 준비됨 → 다시 재생할 때 보임
	CRect                   m_anchor;     // 팝업: 겹칠 카드 (화면 좌표)
	int                     m_popupW = 480;
	void PlacePopup();                    // 팝업: 영상 비율로 크기 · 위치를 정하고 보임

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg LRESULT OnNcHitTest(CPoint point);
	DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------------
// 영상 [월 모드] 글자 덧그림 창: 격자 위에 겹친 투명 팝업 (색 키 투명 - 글자만 보임, 마우스는 통과)
//  - 재생 중인 미리보기(자식 창) 위에도 글자가 보이도록 별도 최상위 창 (주인 창에 딸림, 활성화하지 않음)
//  - 내용은 주인이 그림 (m_onDraw - 배경은 키 색으로 채워 둠, 키 색 픽셀은 투명)
class CWallOverlay : public CWnd
{
public:
	static constexpr COLORREF kKey = RGB(1, 0, 1);   // 투명으로 뺄 색 (글자 · 그림자에는 쓰지 않음)
	std::function<void(CDC*, const CRect&)> m_onDraw;
	void ShowOver(CWnd* owner, const CRect& screenRc);   // 없으면 만들고, 자리 · 크기를 맞춰 보임 (자리 · 크기가 바뀌었거나 새로 보일 때만 다시 그림)
	void HideOverlay();

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg LRESULT OnNcHitTest(CPoint) { return HTTRANSPARENT; }
	afx_msg int OnMouseActivate(CWnd*, UINT, UINT) { return MA_NOACTIVATE; }
	DECLARE_MESSAGE_MAP()
};
