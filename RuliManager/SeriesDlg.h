#pragma once

#include "VideoLibrary.h"
#include "DarkTheme.h"

// ---------------------------------------------------------------------------
// 품번 관리 창: 왼쪽 = 제작사 / 레이블 목록, 오른쪽 = 고른 항목의 시리즈(품번 접두어) 표
//  - 표: 품번 | 라벨 | 설명 - 더블클릭으로 셀 편집, 마지막 줄 "+ 새 시리즈", Delete / 오른쪽 클릭으로 삭제
//  - 오른쪽 클릭 [웹페이지 내용 붙여넣기 (일괄 추가)...]
//  - 다른 항목을 고르면 지금 표를 반영, [확인] = 저장하고 닫기, [취소] = 이 창에서 바꾼 것 모두 되돌림
//  - 시리즈 하나는 제작사나 레이블 하나에만 (같은 품번이 다른 항목에 있으면 그쪽에서 뺌)
class CSeriesDlg : public CDialogEx
{
public:
	CSeriesDlg(CVideoLibrary& lib, const CString& selectName, CWnd* pParent = nullptr);

	bool m_changed = false;   // 확인으로 닫았고 바뀐 것이 있음

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();
	virtual BOOL PreTranslateMessage(MSG* pMsg);

	CVideoLibrary& m_lib;
	CString        m_selectName;
	CEdit          m_editSearch;
	CListCtrl      m_list;
	CStatic        m_staticTitle;
	CListCtrl      m_grid;
	CEdit          m_cellEdit;         // 셀 편집 칸 (표 위에 띄움)
	int            m_editRow = -1, m_editCol = -1;

	// 항목 코드: 0 이상 = studios 인덱스, kLabelBase 이상 = labelInfos 인덱스 + kLabelBase
	static const int kLabelBase = 1000000;
	std::vector<int>        m_rows;        // 목록 행 → 항목 코드
	int                     m_cur = -1;    // 지금 항목 코드
	std::vector<SeriesInfo> m_seriesRows;  // 지금 항목의 시리즈 (작업 사본)
	bool                    m_dirty = false;
	bool                    m_loading = false;
	std::vector<CString>    m_backupStudios, m_backupLabels;   // 취소용: 처음 시리즈
	bool                    m_anyChange = false;

	NamedInfo* ItemOf(int code);
	CString    RowText(int code) const;
	int        CodeOf(const CString& name) const;
	void FillList(int selectCode);
	void ShowItem(int code);
	void CommitCurrent();          // 지금 표 → 라이브러리 (다른 항목의 같은 품번은 뺌)
	void FillGrid();
	void BeginCellEdit(int row, int col);
	void EndCellEdit(bool save);
	void DeleteRow(int row);
	void PasteFromWeb();           // 웹페이지에서 복사한 표/목록 붙여넣기 → 시리즈 여러 줄 한꺼번에 추가
	void Changed();

	afx_msg void OnEnChangeSearch();
	afx_msg void OnLvnItemChanged(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnGridDblClk(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnGridKeyDown(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnGridRClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnCellEditKillFocus();
	afx_msg void OnBnClickedPaste();
	CDarkDialogTheme m_theme;
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()
};
