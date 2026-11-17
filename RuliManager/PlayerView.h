#pragma once

#include <functional>
#include "MpvPlayer.h"

// ---------------------------------------------------------------------------
// 내장 플레이어 영상 칸 (mpv 가 이 창 안에 자식 창을 만들어 그림 - 그 창은 꺼서 마우스는 이 창이 받음)
class CPlayerVideoWnd : public CWnd
{
public:
	std::function<void()> onClick;        // 한 번 클릭 (더블클릭 시간이 지난 뒤)
	std::function<void()> onDblClick;
	std::function<void()> onMouseMove;
	std::function<bool()> hideCursor;     // true 면 커서 숨김 (전체 화면에서 컨트롤이 숨었을 때)
	std::function<void(CPoint screen)> onContextMenu;

protected:
	bool m_skipUp = false;   // 더블클릭의 두 번째 버튼 떼기는 무시
	bool m_down = false;     // 이 창에서 버튼을 눌렀음 (목록 카드 더블클릭의 마지막 버튼 떼기가 이 창으로 와도 클릭으로 치지 않게)
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnTimer(UINT_PTR id);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg BOOL OnEraseBkgnd(CDC* dc);
	DECLARE_MESSAGE_MAP()
};

// 전체 화면 창 (플레이어 View 를 잠깐 이 창으로 옮김) - 닫기(Alt+F4)는 전체 화면 끝내기
class CPlayerFsWnd : public CWnd
{
public:
	std::function<void()> onClose;
protected:
	afx_msg void OnClose() { if (onClose) onClose(); }
	afx_msg BOOL OnEraseBkgnd(CDC* dc);
	DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------------
// 내장 플레이어 View (메인 창 격자 자리) - 위: [◀ 목록] + 제목 / 가운데: 영상 / 아래: 탐색 바 + 컨트롤 줄
//  - 컨트롤은 직접 그림 (재생 · -10초 · +10초 · 시간 · 속도 · 음소거 · 볼륨 · 전체 화면)
//  - 전체 화면: 이 View 를 전체 화면 창으로 옮김, 마우스가 2.5초 멈추면 컨트롤 · 커버 숨김
class CPlayerView : public CWnd
{
public:
	bool Create(CWnd* parent, UINT id);

	bool Open(const CString& path, const CString& title, double startSec);
	void Close();                      // 재생을 멈추고 mpv 를 정리 (볼륨은 저장)
	bool IsOpen() const { return m_open; }
	double Position() const { return m_mpv.pos; }
	double Duration() const { return m_mpv.dur; }
	bool HandleKey(UINT vk);           // 단축키 (처리했으면 true)
	bool IsFullscreen() const { return m_full; }
	void SetFullscreen(bool on);

	std::function<void()> onBack;      // [◀ 목록] · Esc
	std::function<void()> onDocked;    // 전체 화면에서 돌아옴 (메인 창이 다시 배치)
	std::function<void()> onPlayCounted;   // 이번 재생이 1회로 셀 만큼 재생됨 (누적 1분 또는 길이 10%, 한 번 열 때 한 번)

protected:
	enum Part { P_NONE, P_BACK, P_PLAY, P_REW, P_FWD, P_SPEED, P_MUTE, P_VOL, P_FULL, P_SEEK };
	static const UINT WM_MPV_WAKE = WM_APP + 0x3A1;
	static const UINT WM_PLAYER_ENDED = WM_APP + 0x3A2;   // 끝까지 재생 + [목록으로 돌아가기] (이벤트 처리 밖에서 닫도록 미룸)
	enum { T_HIDE = 1, T_SEEKFIX = 2 };

	CMpvPlayer      m_mpv;
	CPlayerVideoWnd m_video;
	CPlayerFsWnd    m_fsWnd;
	CWnd*  m_dockParent = nullptr;
	bool   m_open = false;
	bool   m_full = false;
	bool   m_barsShown = true;         // 전체 화면에서 컨트롤 보임
	ULONGLONG m_lastActivity = 0;
	CPoint m_lastMouse = CPoint(-1, -1);
	CString m_title;
	// 재생 횟수: 실제로 재생된 시간만 누적 (일시정지 · 탐색으로 건너뛴 구간은 빼고)
	double m_playedSec = 0.0, m_lastPos = -1.0;
	bool   m_counted = false;
	void   TrackPlayed();
	// [플레이어] 설정 (Open 때 읽음)
	double m_seekStep = 5.0, m_seekBig = 30.0;   // ← → / Shift + ← →
	ULONGLONG m_hideMs = 3000;                   // 전체 화면 컨트롤 숨김
	int    m_endAction = 0;                      // 0 = 멈춤, 1 = 목록으로, 2 = 반복
	CFont  m_font, m_boldFont;

	// 배치 (Layout)
	CRect m_rcTop, m_rcVideo, m_rcSeek, m_rcRow;
	CRect m_rcBack, m_rcTitle, m_rcPlay, m_rcRew, m_rcFwd, m_rcTime, m_rcSpeed, m_rcMute, m_rcVol, m_rcFull;
	Part  m_hot = P_NONE, m_press = P_NONE;
	bool  m_dragSeek = false, m_dragVol = false;
	double m_dragSec = -1.0;           // 끌고 있는 탐색 위치
	double m_hoverSec = -1.0;          // 탐색 바 마우스 오버 위치
	// 연속 탐색 (← → · 다이얼 · ±10 버튼을 빠르게 반복): 목표 위치를 직접 쌓고, 반복 중엔 키프레임 탐색 → 멈추면 정확한 위치로
	double m_seekTarget = -1.0;
	ULONGLONG m_lastSeekTick = 0;
	bool m_seekPaused = false;         // 연속 탐색 동안 잠깐 일시정지함 (멈추면 다시 재생)
	void TogglePauseUser();            // 사용자가 재생 / 일시정지 전환 (연속 탐색의 임시 정지는 취소)
	void SeekStep(double delta);
	bool  m_trackLeave = false;

	int  S(int px) const;              // DPI 배율
	void Layout();
	Part HitTest(CPoint pt) const;
	double SeekSecAt(int x) const;
	double VolAt(int x) const;
	void Activity();                   // 마우스 움직임 → 전체 화면 컨트롤 보이기
	void ShowBars(bool show);
	void DisableMpvChildren();
	void CycleSpeed(int dir);
	void Action(Part p);
	static CString FormatTime(double sec);
	void DrawIcon(CDC* dc, Part p, const CRect& rc, COLORREF col);

	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnTimer(UINT_PTR id);
	afx_msg void OnDestroy();
	afx_msg UINT OnGetDlgCode() { return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTCHARS; }
	afx_msg LRESULT OnMpvWake(WPARAM, LPARAM);
	afx_msg LRESULT OnPlayerEnded(WPARAM, LPARAM);
	DECLARE_MESSAGE_MAP()
};
