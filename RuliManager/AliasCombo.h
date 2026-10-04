#pragma once

#include <functional>

class CAliasCombo;

// 별칭 콤보의 펼침 목록: 리스트 + 오른쪽 스핀 버튼(▲▼)으로 목록 안에서 바로 순서 변경
class CAliasPopup : public CWnd
{
public:
	CAliasCombo*    m_combo = nullptr;
	CListBox        m_list;
	CSpinButtonCtrl m_spin;

	bool CreatePopup(CWnd* owner);
	void Open(const CRect& comboRect, int sel);
	void Close(bool apply);
	bool IsOpen() const { return GetSafeHwnd() && IsWindowVisible(); }

protected:
	bool m_closing = false;
	void Move(int delta);
	void DeleteSelected();
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);   // 우클릭 → [삭제] 메뉴
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);   // 어두운 목록
	CBrush m_listBrush;
	BOOL PreTranslateMessage(MSG* pMsg) override;
	afx_msg void OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized);
	afx_msg void OnActivateApp(BOOL bActive, DWORD dwThreadID);
	afx_msg void OnLbnDblclk();
	afx_msg void OnDeltaPos(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	DECLARE_MESSAGE_MAP()
};

// ▾ / F4 / Alt+↓ 를 누르면 기본 펼침 목록 대신 CAliasPopup 을 띄우는 콤보박스 (CBS_DROPDOWN)
class CAliasCombo : public CComboBox
{
public:
	std::function<void()> m_onReordered;   // 펼침 목록에서 순서를 바꿈 (콤보 항목 순서도 바뀐 뒤)
	std::function<void()> m_onPicked;      // 펼침 목록에서 항목을 고름 (SetCurSel 뒤)
	std::function<void(const CString&)> m_onDeleted;   // 펼침 목록에서 항목을 삭제함 (콤보에서도 지운 뒤)

	void ShowPopup();
	void MoveItem(int from, int to);
	void DeleteItem(int index);
	void PopupClosed() { m_closedTick = ::GetTickCount64(); }
	BOOL PreTranslateMessage(MSG* pMsg) override;

protected:
	CAliasPopup m_popup;
	ULONGLONG   m_closedTick = 0;
	int  CurrentIndex() const;
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnDestroy();
	DECLARE_MESSAGE_MAP()
};
