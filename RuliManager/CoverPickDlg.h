#pragma once

#include "DarkTheme.h"
#include "FrameReader.h"
#include "Resource.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

// ---------------------------------------------------------------------------
// 커버 만들기 창 (영상 카드 오른쪽 클릭 [커버 만들기...])
//  - 재생은 하지 않음: 슬라이더로 시점을 고르면 그 장면 한 장을 보여 주고, [커버로 저장] · [스크린샷으로 저장]
//  - 슬라이더 드래그 중 = 키프레임 한 장 (빠름), 놓으면 = 그 시각의 정확한 장면
//  - ← → 1초, Shift + ← → 10초, . = 다음 장면, Enter = 커버로 저장
//  - 장면 읽기는 작업 스레드 하나 (CFrameReader), 요청은 최신 것 하나만 (드래그 중 쌓인 요청은 버림)
//  - 저장은 원본 해상도 JPEG (품질 92), 커버 적용은 호출 쪽 (m_onSaveCover - ApplyVideoImage 등)
// ---------------------------------------------------------------------------
class CCoverPickDlg : public CDialogEx
{
public:
	CCoverPickDlg(const CString& videoPath, CWnd* pParent = nullptr);
	enum { IDD = IDD_COVER_PICK };

	CString m_shotDir;                                           // [스크린샷으로 저장] 폴더 (끝에 \)
	std::function<bool(const CString& jpgFile)> m_onSaveCover;   // 임시 JPEG → 커버로 (확인 · 적용 · 화면 갱신은 호출 쪽, 성공하면 true)
	std::function<void()> m_onShotSaved;                         // 스크린샷을 저장함 (스크린샷 격자 갱신)

protected:
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void OnOK();       // Enter = 커버로 저장 (창은 닫지 않음)
	virtual void OnCancel();

	afx_msg void OnPaint();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnDestroy();
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg LRESULT OnFrameReady(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnOpened(WPARAM wParam, LPARAM lParam);
	afx_msg void OnBnClickedSaveCover();
	afx_msg void OnBnClickedSaveShot();
	DECLARE_MESSAGE_MAP()

private:
	enum ReqKind { rqNone = 0, rqKeyframe, rqExact, rqNext };
	void Request(ReqKind kind, LONGLONG t);
	void Worker();
	void StopWorker();
	void UpdateTimeText();
	LONGLONG SliderTime() const;
	void SetSliderTime(LONGLONG t);

	CString m_video;
	CDarkDialogTheme m_theme;
	CSliderCtrl m_slider;
	CRect m_frameRc;                         // 장면을 그리는 자리 (클라이언트 좌표)
	std::unique_ptr<VideoFrame> m_frame;     // 보이는 장면 (원본 크기)
	bool m_frameExact = false;               // 보이는 장면이 정확한 시각의 장면 (false = 드래그 중 키프레임)
	CString m_error;

	// 작업 스레드
	std::thread m_thread;
	std::mutex m_mx;
	std::condition_variable m_cv;
	ReqKind  m_reqKind = rqNone;
	LONGLONG m_reqT = 0;
	bool     m_quit = false;
	std::atomic<LONGLONG> m_duration{ 0 };
	HWND     m_self = nullptr;
};
