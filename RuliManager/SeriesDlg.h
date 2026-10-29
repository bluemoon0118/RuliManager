#pragma once

#include "VideoLibrary.h"
#include "DarkTheme.h"
#include "ImagePreview.h"
#include <vector>

// ---------------------------------------------------------------------------
// 줄 높이가 다른 목록 (품번 관리 표) - 목록 상자(LBS_OWNERDRAWVARIABLE)처럼 쓰지만 픽셀 단위로 부드럽게 스크롤
//  (목록 상자는 줄 단위로만 스크롤해서, 줄 높이가 다르면 휠을 돌릴 때 뚝뚝 · 불규칙하게 움직였음)
//  - 부모에게 목록 상자와 같은 알림: WM_MEASUREITEM (AddString 때), WM_DRAWITEM (그릴 때), WM_COMMAND LBN_SELCHANGE / LBN_DBLCLK
//  - 휠 · 스크롤바 · ↑↓ PgUp PgDn Home End, 휠은 짧게 이어서 움직임 (애니메이션)
class CVarList : public CWnd
{
public:
	BOOL CreateOver(CWnd* placeholder);          // 자리(대화상자의 목록 상자)를 지우고 같은 ID · 자리 · 탭 순서로 만듦
	void ResetContent();
	int  AddString(LPCTSTR text);                // 줄 추가 (높이는 부모의 WM_MEASUREITEM)
	int  GetCount() const { return static_cast<int>(m_tops.size()) - 1; }
	int  GetCurSel() const { return m_sel; }     // 없으면 LB_ERR (-1)
	int  SetCurSel(int index);                   // 알림 없음, 보이게 스크롤
	int  GetItemRect(int index, LPRECT rc) const;   // 클라이언트 좌표, 없으면 LB_ERR
	UINT ItemFromPoint(CPoint pt, BOOL& outside) const;
	int  GetTopIndex() const;
	int  SetTopIndex(int index);

protected:
	std::vector<int> m_tops{ 0 };   // 줄 i 위쪽 (내용 좌표) - 크기 = 줄 수 + 1 (마지막 = 전체 높이)
	int  m_sel = -1;
	int  m_scroll = 0;              // 지금 스크롤 (픽셀)
	int  m_target = 0;              // 휠 애니메이션 목표
	bool m_animating = false;
	static const UINT_PTR kAnimTimer = 0x5643;
	int  MaxScroll() const;
	void SetScroll(int pos);        // 바로
	void ScrollTo(int pos);         // 애니메이션
	void UpdateScrollBar();
	void EnsureVisible(int index);
	void Notify(UINT code);
	void SelectByUser(int index);

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg UINT OnGetDlgCode() { return DLGC_WANTARROWS; }
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------------
// 품번 관리 창 (품번 DB) - 배우 관리 창처럼 왼쪽 표 + 오른쪽 입력
//  - 왼쪽 표: 제작사 | 품번 (여러 개면 줄마다) | 레이블 | 설명 - 모든 제작사의 품번(시리즈) 항목, 제작사 → 품번 순 (제작사 없는 항목은 맨 뒤), 위 검색 칸으로 거르기
//  - 오른쪽: 제작사 (콤보) · 품번 (여러 줄 / 쉼표) · 레이블명 · 설명 · 레이블 대표 이미지 → [저장] (다른 줄을 고르거나 [확인] 때도 저장)
//    · 제작사를 바꾸면 그 제작사의 품번 목록으로 옮김, 같은 품번이 다른 항목에 있으면 확인 후 그쪽에서 뺌 (품번은 한 항목에만)
//  - [새 품번] · [삭제] (Del), [웹페이지 붙여넣기...] = 오른쪽에 고른 제작사로 여러 줄 한꺼번에 추가
//  - 순서: 제작사마다 0, 1, 2 … 저장된 순서 (열로는 보이지 않음) - 오른쪽 클릭 메뉴 · Ctrl+↑↓ · 끌어서 놓기로 바꿈
//  - [확인] = 저장하고 닫기, [취소] = 이 창에서 바꾼 것 모두 되돌림
class CSeriesDlg : public CDialogEx
{
public:
	CSeriesDlg(CVideoLibrary& lib, const CString& selectName, CWnd* pParent = nullptr);

	bool m_changed = false;   // 확인으로 닫았고 바뀐 것이 있음

	// 처음 고를 품번 항목 (레이블 보기 머리줄 오른쪽 클릭 → [품번 관리에서 보기]) - m_selectOwner: -2 = 지정 안 함, -1 = 제작사 없는 항목, 0~ = 제작사
	int     m_selectOwner = -2;
	CString m_selectCode;     // 그 항목의 품번 (SeriesInfo::name)
	// 처음부터 [새 품번] 입력 (레이블 보기에서 품번 DB 에 없는 묶음 → [품번 관리에 등록]) - 품번 · 레이블 · 제작사 미리 채움
	bool    m_newPrefill = false;
	CString m_newCode, m_newLabel, m_newStudio;

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();
	virtual BOOL PreTranslateMessage(MSG* pMsg);

	CVideoLibrary& m_lib;
	CString        m_selectName;   // 처음 고를 제작사 (그 제작사의 첫 품번)
	CEdit          m_editSearch;
	CVarList       m_grid;         // 표: 줄 높이가 다른 목록 (품번 수만큼 줄 높이, 칸은 직접 그림 - 픽셀 단위 부드러운 스크롤)
	CDarkHeader    m_header;       // 표 열 머리글 (목록 위)
	int            m_lineH = 16;   // 글자 한 줄 높이
	CComboBox      m_comboStudio;
	CEdit          m_editCode, m_editLabel, m_editAlt, m_editDesc;   // m_editAlt: 레이블 다른 이름 (줄마다 하나)
	// 레이블 구분: 싱글 (레이블명 하나) / 복합 (같은 품번 접두어 안에서 뒷번호로 레이블을 나눔)
	//  복합: 레이블명 = 입력 가능한 콤보 (항목 순서 = [레이블] 보기 출력 순서, ▲▼ 로 조절), 뒷번호 = 여러 줄 칸 (고른 레이블의 번호, 줄마다 하나)
	CComboBox      m_comboType;
	CComboBox      m_subCombo;
	CEdit          m_editSubNums;
	std::vector<SubLabel> m_subs;  // 편집 중인 복합 레이블
	int            m_subCur = -1;  // 콤보에서 고른 레이블
	bool IsCompositeSel() const { return m_comboType.GetSafeHwnd() && m_comboType.GetCurSel() == 1; }
	void UpdateTypeUI();                    // 구분에 따라 레이블명 칸 / 복합 레이블 칸 켜고 끔
	void FillSubList(const std::vector<SubLabel>& subs);
	void RefreshSubCombo(int sel);          // 콤보 항목 다시 + sel 고르고 뒷번호 칸 표시
	void StoreSubNums();                    // 뒷번호 칸 → 고른 레이블
	void SyncSubComboItem(int idx);         // 입력한 이름을 콤보 목록 글자에도
	void MoveSub(int delta);
	std::vector<SubLabel> ReadSubList();
	CImagePreview  m_image;
	CString        m_curImage;     // 지금 항목의 레이블 대표 이미지 (보관소 복사본 전체 경로)

	struct Row { int studio = -1; int idx = -1; SeriesInfo s; int band = 0; };   // band: 제작사 묶음 번호 (0 / 1 번갈아 배경색)   // 표 한 줄 = studios[studio] 의 시리즈 idx 번째 (s = 그리기용 사본)
	int  RowHeight(const Row& r) const;   // 품번 수만큼 (최대 255 픽셀)
	int  SelRow() const { const int s = m_grid.GetSafeHwnd() ? m_grid.GetCurSel() : LB_ERR; return s == LB_ERR ? -1 : s; }
	void SelectRow(int row);              // -1 = 선택 없음 (알림 없이)
	void OnRowSelected(int row);          // 사용자가 고른 줄
	void ShowGridMenu(CPoint screen);
	bool    m_pressing = false;           // 표에서 왼쪽 버튼 누름 (끌기 시작 판단)
	CPoint  m_pressPt;
	int     m_pressRow = -1;
	std::vector<Row> m_rows;
	int     m_curStudio = -1;      // 지금 편집 중인 항목 (제작사 인덱스, -1 = 없음)
	CString m_curName;             // 그 항목의 품번 (저장된 값 - 인덱스 대신 이 이름으로 찾음)
	bool    m_new = false;         // [새 품번] 입력 중
	bool    m_dirty = false;
	bool    m_loading = false;
	bool    m_anyChange = false;
	std::vector<CString> m_backupStudios;   // 취소용: 처음 시리즈
	CString m_backupLoose;
	// 제작사 인덱스 0 ~ studios.size()-1 = 제작사, LooseIdx() (= studios.size()) = 제작사 없는 품번 항목 (m_lib.looseSeries)
	int LooseIdx() const { return static_cast<int>(m_lib.studios.size()); }
	CString&       SeriesOf(int studio);
	const CString& SeriesOf(int studio) const;
	CString        NameOf(int studio) const;

	static std::vector<CString> SplitCodes(const CString& text);   // 품번 칸 → 접두어 목록 (줄 · 쉼표 · 공백, 대문자, 중복 제거)
	int  FindEntry(int studio, const CString& name) const;         // studios[studio] 시리즈 중 품번이 name 인 것 (없으면 -1)
	void FillGrid(int selStudio, const CString& selName);
	void ShowEntry(int studio, int idx);   // -1 = 비움
	void ShowFields(int studio, const SeriesInfo* s);
	bool Commit();                         // 오른쪽 입력 → 품번 DB (잘못된 값이면 false)
	void SetImage(const CString& path);
	void UpdateButtons();
	void SelectStudioCombo(int studio);
	int  ComboStudio() const;
	void DeleteCurrent();
	void PasteFromWeb();                   // 웹페이지에서 복사한 표/목록 붙여넣기 → 품번 여러 줄 한꺼번에 추가

	afx_msg void OnEnChangeSearch();
	afx_msg void OnFieldChanged();
	afx_msg void OnGridSelChange();
	afx_msg void OnGridDblClk();
	afx_msg void OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpmis);
	afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpdis);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnHeaderChanged(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnBnClickedNew();
	afx_msg void OnBnClickedDelete();
	afx_msg void OnBnClickedSave();
	afx_msg void OnBnClickedImgBrowse();
	afx_msg void OnBnClickedImgClear();
	afx_msg void OnBnClickedPaste();
	afx_msg void OnTypeChanged();
	afx_msg void OnBnClickedSubAdd();
	afx_msg void OnBnClickedSubDel();
	afx_msg void OnBnClickedSubUp();
	afx_msg void OnBnClickedSubDown();
	afx_msg void OnSubComboSelChange();
	afx_msg void OnSubComboEditChange();
	afx_msg void OnSubComboKillFocus();
	afx_msg void OnSubNumsChange();
	// 순서: 제작사마다 0, 1, 2 … (품번 DB 에 저장된 순서 = 표 출력 순서) - 오른쪽 클릭 [위로 · 아래로 · 맨 위로 · 맨 아래로], Ctrl+↑↓, 끌어서 놓기 (같은 제작사 안)
	void MoveCurrent(int delta);                         // delta: -1 / +1, INT_MIN = 맨 위, INT_MAX = 맨 아래
	void MoveEntry(int studio, int from, int to);
	bool    m_dragging = false;
	int     m_dragStudio = -1;
	CString m_dragName;
	int     m_dropRow = -1;                              // 강조한 표 줄 (-1 = 없음)
	int  DropRowAt(CPoint screen) const;                 // 놓을 수 있는 표 줄 (같은 제작사의 다른 항목, 아니면 -1)
	void SetDropRow(int row);
	void BeginDrag(int row);
	void EndDrag(bool drop);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnCaptureChanged(CWnd* pWnd);
	CDarkDialogTheme m_theme;
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()
};
