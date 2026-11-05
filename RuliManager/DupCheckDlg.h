#pragma once

#include <functional>
#include <map>
#include <set>
#include <vector>
#include "DarkTheme.h"

class CVideoLibrary;

// ---------------------------------------------------------------------------
// 중복 파일 검사 창 ([관리 도구 ▾] → [중복 파일 검사...])
//  - 같은 크기(바이트) · 같은 품번(분할 영상은 같은 파트 번호끼리)의 영상을 묶음으로 찾음
//  - 묶음마다 머리줄 + 영상 카드 격자 (커버 · 파일 이름 · 크기 · 재생 시간 · 해상도 · 수정일 · 위치)
//  - 카드 더블클릭 = 재생, 체크한 파일은 [선택 삭제] = 휴지통으로 보내고 DB 에서도 삭제 (주인 창이 처리)

// 묶음 카드 격자 (직접 그림, 세로 스크롤)
class CDupGrid : public CWnd
{
public:
	struct Card
	{
		int       item = -1;      // 라이브러리 항목 번호
		CString   path;
		CString   name;           // 파일 이름
		CString   folder;
		CString   code;
		ULONGLONG size = 0;
		CString   sizeText, timeText, resText, dateText;
		UINT      height = 0;     // 해상도 세로 (자동 선택용)
		bool      offline = false;
		CString   image;          // 커버 이미지 경로
	};
	struct Group
	{
		CString title;            // 머리줄
		std::vector<Card> cards;
	};

	std::function<void(const CString& path)> m_onPlay;
	std::function<void(const CString& path)> m_onExplore;
	std::function<void()> m_onCheckChanged;

	bool Create(CWnd* parent, const CRect& rc, UINT id);
	void SetGroups(const std::vector<Group>& groups);
	const std::vector<Group>& Groups() const { return m_groups; }
	std::set<CString>& Checked() { return m_checked; }   // 소문자 경로
	bool IsChecked(const CString& path) const;
	void SetChecked(const CString& path, bool on);
	void ClearImages();

protected:
	std::vector<Group> m_groups;
	std::set<CString>  m_checked;
	std::map<CString, HBITMAP> m_thumbs;   // 소문자 경로 → 카드 크기로 줄인 커버 (없으면 nullptr)
	struct Slot { int g = -1, c = -1; CRect rc; };   // 카드 자리 (내용 좌표)
	std::vector<Slot> m_slots;
	std::vector<std::pair<int, CRect>> m_heads;     // 머리줄 (묶음 번호, 내용 좌표)
	int  m_contentH = 0;
	int  m_scroll = 0;
	int  m_hot = -1, m_hotCheck = -1;               // m_slots 번호
	int  m_focus = -1;
	bool m_tracking = false;
	int  m_cardW = 220, m_imgH = 124, m_lineH = 18, m_pad = 8, m_gap = 10, m_headH = 28;
	CFont m_font, m_bold, m_headFont;

	void Relayout();
	void UpdateScroll();
	void ScrollTo(int y);
	int  SlotAt(CPoint client) const;
	CRect CheckRect(const CRect& card) const;
	HBITMAP Thumb(const Card& c, int w, int h);
	void DrawCard(CDC* dc, const Card& c, const CRect& rc, bool hot, bool hotCheck, bool focus);
	void ToggleSlot(int s);

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnDestroy();
	DECLARE_MESSAGE_MAP()
};

class CDupCheckDlg : public CDialogEx
{
public:
	CDupCheckDlg(CVideoLibrary& lib, CWnd* pParent = nullptr);

	// 주인 창이 채움
	std::function<bool(int item)> m_isOffline;                       // 연결 안 된 이동식 디스크 영상 (삭제 불가)
	std::function<CString(const CString& videoPath)> m_findImage;     // 커버 이미지 경로
	std::function<std::vector<CString>(const std::vector<CString>& paths)> m_remove;   // 휴지통 + DB 삭제, 지우지 못한 파일 목록을 돌려줌

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnOK() {}

	CVideoLibrary& m_lib;
	CDupGrid m_grid;
	CDarkDialogTheme m_theme;
	CSize m_minSize;

	void Scan();
	void UpdateStatus();
	void LayoutControls(int cx, int cy);

	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* mmi);
	afx_msg void OnScan();
	afx_msg void OnAuto();
	afx_msg void OnClearChecks();
	afx_msg void OnRemove();
	DECLARE_MESSAGE_MAP()
};
