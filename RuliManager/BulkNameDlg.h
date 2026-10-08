#pragma once

#include "VideoLibrary.h"
#include "DarkTheme.h"

// ---------------------------------------------------------------------------
// 태그 / 스튜디오 일괄 등록 창
//  - 한 줄에 하나씩 또는 쉼표로 구분해 여러 이름을 입력 (괄호 안 쉼표는 이름의 일부)
//  - 오른쪽 미리보기에 신규 / 이미 있음 / 입력 중복 상태를 표시
//  - 등록을 누르면 신규 이름만 목록에 추가
class CBulkNameDlg : public CDialogEx
{
public:
	CBulkNameDlg(CVideoLibrary& lib, int kind, CWnd* pParent = nullptr);

	std::vector<CString> m_added;   // 등록된 이름 (입력 순서)

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();

	enum EntryState { ST_NEW = 0, ST_EXISTS = 1, ST_DUP = 2 };
	struct Entry
	{
		CString name;
		int     state = ST_NEW;
	};

	CVideoLibrary& m_lib;
	int            m_kind;
	CEdit          m_editText;
	CListCtrl      m_list;
	CStatic        m_staticSummary;
	std::vector<Entry> m_entries;

	CString KindName() const { return m_kind == LIST_STUDIO ? L"제작사" : L"태그"; }
	void Parse();
	void UpdatePreview();

	afx_msg void OnEnChangeText();
	afx_msg void OnNmCustomDrawList(NMHDR* pNMHDR, LRESULT* pResult);
	CDarkDialogTheme m_theme;          // 메인 창과 같은 어두운 색상
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()
};
