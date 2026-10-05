#pragma once

#include "VideoLibrary.h"
#include "DarkTheme.h"
#include "ImagePreview.h"
#include "CountryCombo.h"
#include "AliasCombo.h"
#include "StarRatingCtrl.h"

// ---------------------------------------------------------------------------
// 배우 관리 창: 배우 추가 / 수정 / 삭제 (사진, 생년월일, 별칭, 메모)
class CActorDlg : public CDialogEx
{
public:
	CActorDlg(CVideoLibrary& lib, const CString& selectName, CWnd* pParent = nullptr);

	bool m_changed = false;   // 라이브러리가 바뀌었는지 (호출한 쪽에서 저장/갱신)

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();

	CVideoLibrary& m_lib;
	CString        m_selectName;

	CEdit          m_editSearch;
	CListCtrl      m_list;
	CImagePreview  m_photo;
	CAliasCombo    m_comboAliases;     // 이름: 대표 이름 + 별칭 모두 (보이는 이름 = 마지막으로 고른 이름 = 대표 이름), Enter 추가 / 펼침 목록 우클릭 삭제 · 스핀으로 순서
	void FillAliases(const CString& aliases, const CString& select);
	int  SelectedAliasIndex() const;
	afx_msg void OnCbnSelchangeAliases();   // 고른 별칭 기억
	CString GetAliases() const;         // 목록 + 입력 칸에 적어 둔 값
	bool AddAliasFromEdit();
	void UpdateAliasCue();
	CDarkCombo     m_comboGender;      // 성별: (미지정) / 여성 / 남성
	CDarkDateTime  m_dateBirth;
	CStatic        m_staticAge;
	CCountryCombo  m_comboNationality;  // 국적: [국기] 나라 이름 콤보박스
	CEdit          m_editHeight;
	CEdit          m_editBust;         // 치수 B
	CEdit          m_editWaist;        // 치수 W
	CEdit          m_editHip;          // 치수 H
	CDarkCombo     m_comboCup;         // 컵: (미지정) / A ~ Q
	CDarkDateTime  m_dateDebut;
	CDarkDateTime  m_dateRetire;       // 은퇴일 (체크 해제 = 없음)
	CStarRatingCtrl m_starRating;      // 별점 (마우스 오버 미리 보기, 클릭 확정 → [저장] 대상)
	CStatic        m_staticCount;
	CStatic        m_staticPhotoPath;
	CEdit          m_editMemo;
	CEdit          m_editUrls;         // 링크 URL (한 줄에 하나)

	std::vector<int> m_rows;           // 목록 행 → m_lib.actors 인덱스
	int     m_cur = -1;                // 편집 중인 배우
	CString m_photoPath;               // 편집 중인 사진 경로
	bool    m_dirty = false;
	bool    m_loading = false;

	void FillList(const CString& selectName);
	void ShowActor(int idx);
	bool Commit();                     // 편집 내용 반영 (이름 오류 시 false)
	void UpdateRow(int idx);
	void SetPhoto(const CString& path);

	afx_msg void OnEnChangeSearch();
	afx_msg void OnFieldChanged();
	afx_msg void OnDtnBirthChanged(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDtnDebutChanged(NMHDR* pNMHDR, LRESULT* pResult);
	void UpdateAgeText();
	afx_msg void OnLvnItemChanged(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnNmCustomDrawList(NMHDR* pNMHDR, LRESULT* pResult);   // 국적 칸에 국기 그리기
	void SetRowText(int row, const ActorInfo& a);                        // 목록 한 줄 (출연 수 제외)

	// 창 크기 조절: 처음 위치 기억 → 목록은 넓어지고 오른쪽 정보 칸은 오른쪽에 붙음
	struct Anchor { HWND hwnd; CRect rc; int mode; };
	std::vector<Anchor> m_anchors;
	CSize m_initClient;
	CSize m_minTrack;
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	CString NationalityCell(const CString& nationality) const;          // 국기 자리만큼 앞에 공백
	CString m_flagPad;
	afx_msg void OnBnClickedNew();
	afx_msg void OnBnClickedDelete();
	afx_msg void OnBnClickedSave();
	afx_msg void OnBnClickedPhotoBrowse();
	afx_msg void OnBnClickedPhotoClear();
	afx_msg void OnBnClickedPhotoSearch();   // 구글 이미지 검색 (상위 10개 중 선택)
	afx_msg LRESULT OnMergeActors(WPARAM wParam, LPARAM lParam);
	std::vector<CString> m_mergeNames;   // 별칭과 이름이 같은 별도 배우 (합치기 확인 대기)
	CDarkDialogTheme m_theme;          // 메인 창과 같은 어두운 색상
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------------
// 배우 선택 창: 동영상에 출연한 배우를 체크해서 고름 (새 배우 바로 추가 가능)
class CActorPickDlg : public CDialogEx
{
public:
	CActorPickDlg(CVideoLibrary& lib, const CString& current, CWnd* pParent = nullptr);

	CString m_result;         // 확인 시: 선택한 배우 이름 (쉼표 구분)
	bool    m_added = false;  // 새 배우를 추가했는지

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();

	CVideoLibrary& m_lib;
	CEdit     m_editSearch;
	CListCtrl m_list;
	CEdit     m_editNew;
	CStatic   m_staticSelected;

	std::vector<CString> m_order;   // 선택 순서 유지
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
