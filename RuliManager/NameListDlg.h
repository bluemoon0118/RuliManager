#pragma once

#include "VideoLibrary.h"
#include "DarkTheme.h"
#include "ImagePreview.h"

// ---------------------------------------------------------------------------
// 스튜디오 / 태그 관리 창: 추가 / 수정 / 삭제 (이름, 이미지, 메모)
class CNameListDlg : public CDialogEx
{
public:
	CNameListDlg(CVideoLibrary& lib, int kind, const CString& selectName, CWnd* pParent = nullptr);

	bool m_changed = false;

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

	std::vector<int>       m_rows;    // 목록 행 → 이름 목록 인덱스
	std::map<CString, int> m_counts;  // 이름(소문자) → 영상 수
	int     m_cur = -1;
	CString m_imagePath;
	bool    m_dirty = false;
	bool    m_loading = false;

	std::vector<NamedInfo>& Items() { return m_lib.NamedList(m_kind); }
	CString KindName() const { return m_kind == LIST_STUDIO ? L"스튜디오" : L"태그"; }
	int     CountOf(const CString& name) const;

	void FillList(const CString& selectName);
	void ShowItem(int idx);
	bool Commit();
	void SetImage(const CString& path);

	afx_msg void OnEnChangeSearch();
	afx_msg void OnFieldChanged();
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
