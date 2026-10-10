#pragma once

#include "DarkTheme.h"

// ---------------------------------------------------------------------------
// 텍스트로 정보 입력 창 (영상 / 배우 카드 오른쪽 클릭)
//  - 스캔 때 읽는 정보 txt 와 같은 "항목: 값" 형식을 직접 입력 · 붙여넣기
//  - 처음에는 지금 정보가 같은 형식으로 채워져 있음 → 고치고 [적용]
class CTextInfoDlg : public CDialogEx
{
public:
	CTextInfoDlg(const CString& title, const CString& guide, const CString& text, CWnd* pParent = nullptr);

	CString m_text;   // [적용] 했을 때 입력 내용
	bool    m_pasteButton = false;   // 위쪽 [공백라인 제거] 버튼 표시 (웹페이지 붙여넣기 창)

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();

	CString m_title, m_guide;
	CEdit   m_editText;
	CDarkDialogTheme m_theme;          // 메인 창과 같은 어두운 색상

	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnPasteCompact();   // 입력 칸 글자에서 빈 줄(공백만 있는 줄 포함)을 지움
	DECLARE_MESSAGE_MAP()
};
