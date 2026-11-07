#pragma once

#include "DarkTheme.h"
#include "PluginHost.h"
#include "Resource.h"
#include <deque>
#include <functional>

// ---------------------------------------------------------------------------
// 플러그인 실행 창 (모달): 진행 막대 · 메시지 · 플러그인 출력 기록, [중단]
//  - 성공하면 바로 닫힘 (IDOK, m_resultJson = 결과 줄), 실패 · 중단이면 열린 채로 이유와 기록을 보여 줌
//  - 결과 한 편({"item":{...}})이 올 때마다 m_onItem 으로 바로 넘김 (채울 항목 고르기 창 등 - 처리하는 동안 플러그인은 일시 정지,
//    처리가 끝나면 다시 실행). m_onItem 이 false 를 돌려주면 남은 결과를 버리고 플러그인 중단
class CPluginRunDlg : public CDialogEx
{
public:
	CPluginRunDlg(const PluginInfo& plugin, const CString& requestJson, CWnd* pParent = nullptr);
	~CPluginRunDlg();

	CString m_resultJson;   // IDOK 일 때 플러그인 결과 ({"ok":true,...})
	std::function<bool(const CString& line)> m_onItem;   // 결과 한 편 ({"item":{...}} 줄) → 적용, false = 그만
	int  m_total = 0;       // 요청한 항목 수 (진행 표시 "결과 n / 전체")
	int  m_received = 0;    // 받은 결과 수
	bool m_stoppedByUser = false;   // m_onItem 이 그만을 알림 (남은 결과 버림)

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_PLUGIN_RUN };
#endif

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK() {}   // Enter 로 닫히지 않게
	virtual void OnCancel();

	PluginInfo m_plugin;
	CString    m_request;
	CPluginRun m_run;
	CDarkDialogTheme m_theme;
	HICON      m_iconSmall = nullptr;
	HICON      m_iconBig = nullptr;
	ULONGLONG  m_startTick = 0;
	bool       m_done = false;
	bool       m_closeWhenDone = false;   // 실행 중에 창을 닫으면: 중단하고 끝나면 닫음
	bool       m_marquee = false;
	CString    m_lastMessage;
	std::deque<CString> m_queue;   // 처리 기다리는 결과 (고르기 창이 떠 있는 동안 온 것)
	bool       m_busy = false;     // m_onItem 처리 중 (안에서 다른 창이 뜸 - 다시 들어오지 않게)
	bool       m_hiddenWaiting = false;   // 결과를 모두 처리해서 창을 숨기고 플러그인 종료를 기다리는 중 (5초 넘으면 다시 보임)
	ULONGLONG  m_pauseTick = 0;    // 플러그인을 멈춘 때 (멈춘 시간은 경과 시간 · 시간 제한에서 뺌)
	std::unique_ptr<CPluginRun::Event> m_pendingDone;   // 처리 중에 끝난 경우 - 처리가 끝나면 마저
	void DrainQueue();
	void HandleDone(const CPluginRun::Event& e);
	void UpdateMessage();
	// 플러그인이 띄운 창(셀레늄 크롬 등)을 처음 보일 때 한 번 RuliManager 뒤로 보내고 포커스를 되돌림 (최대 크기 등은 그대로)
	std::vector<HWND> m_behindDone;   // 이미 뒤로 보낸 창 (사용자가 앞으로 꺼내면 그대로 둠)
	void KeepPluginWindowsBehind();

	void SetMarquee(bool on);
	void AppendLog(const CString& line);
	void Finish(const CString& message);   // 실패 · 중단: 이유 표시 + [닫기]

	afx_msg LRESULT OnRunEvent(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnBringFront(WPARAM wParam, LPARAM lParam);   // 결과 처리 중 뜬 대화 상자를 맨 앞으로
	afx_msg void OnBnClickedStop();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()
};
