#pragma once

#include "VideoLibrary.h"
#include "DarkTheme.h"
#include "ImagePreview.h"
#include "TagChipCtrl.h"

// ---------------------------------------------------------------------------
// 제작사 / 태그 관리 창: 추가 / 수정 / 삭제 (이름, 이미지, 메모)
//  - 제작사 관리: 목록에 제작사와 그 아래 레이블(└)을 함께 표시, [종류: 제작사 / 레이블] 라디오로 항목 종류 지정
//    (레이블은 상위 제작사 선택, 레이블도 이름 · 서브이름 · 이미지 · 메모)
class CNameListDlg : public CDialogEx
{
public:
	CNameListDlg(CVideoLibrary& lib, int kind, const CString& selectName, CWnd* pParent = nullptr);

	bool m_changed = false;
	// 이미지 경로 → 로고 비트맵 (메인 창의 캐시 사용, 칩 이름 왼쪽 이미지용) - 비어 있으면 이미지 없이
	std::function<Gdiplus::Bitmap*(const CString&)> m_getLogo;

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();

	CVideoLibrary& m_lib;
	int            m_kind;            // LIST_STUDIO / LIST_TAG
	CString        m_selectName;

	CEdit          m_editSearch;
	CListCtrl      m_list;
	CImagePreview  m_image;
	CEdit          m_editName;
	CStatic        m_staticCount;
	CStatic        m_staticImagePath;
	CEdit          m_editMemo;
	CEdit          m_editSub;          // 서브이름 (제작사 · 레이블)
	CDarkButton    m_radioStudio;      // 종류: [제작사] 토글 버튼 (선택 파란색, 나머지 회색 - 메인 창 탭 버튼과 같은 모양)
	CDarkButton    m_radioLabel;       // 종류: [레이블] 토글 버튼
	CTagChipCtrl   m_parentChips;      // 레이블의 상위 제작사 (칩 하나, 비우면 상위 없음 - 등록된 제작사만)
	CString        m_parentExclude;    // 상위로 고를 수 없는 이름 (지금 항목이 제작사일 때 자기 자신)
	CTagChipCtrl   m_labelChips;       // 제작사: 하위 레이블 (여러 개, 태그처럼 칩) - 빼면 그 레이블은 상위 없음으로
	// 제작사 · 레이블: 시리즈 표 (시리즈 | 품번 | 라벨 | 설명) - 더블클릭으로 셀 편집, 마지막 줄 "+ 새 시리즈", Delete / 오른쪽 클릭으로 삭제
	CListCtrl      m_seriesGrid;
	CEdit          m_cellEdit;         // 셀 편집 칸 (표 위에 띄움)
	int            m_editRow = -1, m_editCol = -1;
	std::vector<SeriesInfo> m_seriesRows;   // 표의 시리즈 (저장 전 작업 사본)
	CRect          m_chipsLblRect, m_parentRect, m_parentLblRect;   // 제작사일 때 [레이블] 줄을 [상위] 줄 높이로 올림
	CRect          m_chipsRect1, m_gridRect1, m_gridRect2;   // 제작사 = [레이블] 칩 1 / 시리즈 표 2, 레이블 = 시리즈 표 1
	void FillSeriesGrid();
	void BeginCellEdit(int row, int col);
	void EndCellEdit(bool save);
	void DeleteSeriesRow(int row);
	void PasteSeriesFromWeb();   // 웹페이지에서 복사한 표/목록 붙여넣기 → 시리즈 여러 줄 한꺼번에 추가
	afx_msg void OnSeriesDblClk(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnSeriesKeyDown(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnSeriesRClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnCellEditKillFocus();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	void ApplyLabelChips(const CString& studioName);
	void SetupChipIcons(CTagChipCtrl& chips, bool label);   // 칩 이름 왼쪽에 제작사(label=false) / 레이블 이미지   // 제작사의 레이블 칩 → 레이블 상위 지정 / 새 레이블 / 뺀 레이블은 상위 없음

	// 항목 코드: 0 이상 = 이름 목록(제작사 / 태그) 인덱스, kLabelBase 이상 = 레이블(labelInfos) 인덱스 + kLabelBase
	static const int kLabelBase = 1000000;
	static bool IsLabelCode(int code) { return code >= kLabelBase; }
	bool       ValidCode(int code) const;
	NamedInfo* ItemOf(int code);
	int        CodeOf(const CString& name, bool label) const;
	CString    RowText(int code) const;
	int        CountOfCode(int code) const;

	std::vector<int>       m_rows;    // 목록 행 → 항목 코드
	std::map<CString, int> m_counts;  // 이름(소문자) → 영상 수
	int     m_cur = -1;               // 지금 항목 코드 (-1 = 없음)
	CString m_imagePath;
	bool    m_dirty = false;
	bool    m_loading = false;
	bool    m_structChanged = false;  // Commit 에서 종류 변경 등으로 목록 구조가 바뀜 (항목 코드 다시 계산)

	std::vector<NamedInfo>& Items() { return m_lib.NamedList(m_kind); }
	CString KindName() const { return m_kind == LIST_STUDIO ? L"제작사" : L"태그"; }
	int     CountOf(const CString& name) const;

	void FillList(const CString& selectName);
	void FillListCode(int selectCode);
	void ShowItem(int code);
	bool Commit();
	void SetImage(const CString& path);
	void FillParentCombo(const CString& exclude, const CString& select);
	void UpdateKindControls();

	afx_msg void OnEnChangeSearch();
	afx_msg void OnFieldChanged();
	afx_msg void OnKindChanged();
	afx_msg void OnKindStudio();
	afx_msg void OnKindLabel();
	afx_msg void OnLvnItemChanged(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnBnClickedNew();
	afx_msg void OnBnClickedDelete();
	afx_msg void OnBnClickedBulk();
	afx_msg void OnBnClickedSave();
	afx_msg void OnBnClickedImageBrowse();
	afx_msg void OnBnClickedImageClear();
	CDarkDialogTheme m_theme;          // 메인 창과 같은 어두운 색상
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------------
// 스튜디오 / 태그 선택 창 (스튜디오: 하나만, 태그: 여러 개)
class CNamePickDlg : public CDialogEx
{
public:
	CNamePickDlg(CVideoLibrary& lib, int kind, const CString& current, CWnd* pParent = nullptr);

	CString m_result;          // 확인 시: 선택한 이름 (태그는 쉼표 구분)
	bool    m_added = false;   // 새 항목을 추가했는지

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();

	CVideoLibrary& m_lib;
	int       m_kind;
	bool      m_multi;
	CEdit     m_editSearch;
	CListCtrl m_list;
	CEdit     m_editNew;
	CStatic   m_staticSelected;

	std::vector<CString> m_order;
	bool m_filling = false;

	bool IsChecked(const CString& name) const;
	void SetChecked(const CString& name, bool checked);
	void FillList();
	void UpdateSelectedText();

	afx_msg void OnEnChangeSearch();
	afx_msg void OnLvnItemChanged(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnNmDblclk(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnBnClickedAdd();
	CDarkDialogTheme m_theme;          // 메인 창과 같은 어두운 색상
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()
};
