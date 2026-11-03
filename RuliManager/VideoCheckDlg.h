#pragma once

#include <atomic>
#include <thread>
#include <vector>
#include "DarkTheme.h"

// ---------------------------------------------------------------------------
// 영상 검사 창 (모달리스) - [목록 관리 ▾] → [영상 검사...], 영상 카드 오른쪽 클릭 [영상 검사] 로 목록에 추가
//  - ffmpeg 로 영상을 실제로 풀어 보며 손상 여부를 판정 (별도 스레드에서 한 편씩, [중단] 으로 지금 검사 취소)
//  - 빠른 검사: 머리 정보 + 영상 곳곳 몇 군데를 1초씩 풀어 봄 (1편에 몇 초)
//  - 정밀 검사: 처음부터 끝까지 전부 풀어 봄 (영상 · 소리, 오래 걸림)
//  - 창을 닫아도 숨기기만 함 (검사는 계속) - 메인 창이 닫힐 때 같이 정리
class CVideoCheckDlg : public CDialogEx
{
public:
	explicit CVideoCheckDlg(CWnd* pParent = nullptr);
	virtual ~CVideoCheckDlg();

	bool CreateModeless(CWnd* parent);
	void AddVideos(const std::vector<CString>& paths, bool start = true);   // 목록에 추가 (이미 대기 · 검사 중이면 건너뜀, 끝난 것은 다시 대기로)
	void ShowAndActivate();

	enum State { ST_WAIT = 0, ST_RUN, ST_OK, ST_SUSPECT, ST_BAD, ST_CANCEL, ST_ERROR };

	struct Job
	{
		int     id = 0;
		CString path;
		State   state = ST_WAIT;
		int     percent = 0;
		int     mode = 0;          // 0 = 빠른 검사, 1 = 정밀 검사 (검사할 때 정함)
		CString summary;           // 한 줄 결과
		CString detail;            // 오류 줄 등 자세한 내용
		bool    removeWhenDone = false;
	};

	struct Result
	{
		int     id = 0;
		State   state = ST_ERROR;
		CString summary;
		CString detail;
	};

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnOK() {}          // Enter 로 닫히지 않게
	virtual void OnCancel();        // 닫기 = 숨기기 (검사는 계속)
	virtual void PostNcDestroy();

	CListCtrl m_list;
	CComboBox m_comboMode;
	CEdit     m_editDetail;
	CDarkDialogTheme m_theme;

	std::vector<Job> m_jobs;
	int  m_nextId = 1;
	bool m_running = false;        // 검사 스레드가 도는 중
	bool m_paused = false;         // [중단] 뒤 [시작] 을 누를 때까지 다음 검사를 하지 않음
	int  m_runId = 0;              // 지금 검사 중인 항목
	std::atomic<bool> m_cancel{ false };
	std::thread m_thread;
	CString m_ffmpeg;
	HICON   m_iconSmall = nullptr;   // 창 아이콘 (돋보기 + 재생, 직접 그림)
	HICON   m_iconBig = nullptr;

	CSize m_minSize;               // 창 최소 크기 (처음 크기)
	CRect m_initClient;

	int  FindJob(int id) const;
	void RefreshRow(int index);
	void RebuildList();
	void StartNext();
	void UpdateButtons();
	void UpdateStatusLine();
	void ShowDetailFor(int index);
	void LayoutControls(int cx, int cy);

	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* mmi);
	afx_msg void OnDestroy();
	afx_msg void OnStart();
	afx_msg void OnStop();
	afx_msg void OnRemove();
	afx_msg void OnClearDone();
	afx_msg void OnListItemChanged(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnListCustomDraw(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnListDblClk(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg LRESULT OnCheckProgress(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnCheckDone(WPARAM wParam, LPARAM lParam);
	DECLARE_MESSAGE_MAP()
};
