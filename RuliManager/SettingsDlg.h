#pragma once

#include <functional>
#include "DarkTheme.h"
#include "VideoLibrary.h"

// ---------------------------------------------------------------------------
// 테마 미리보기: 고른 테마의 색으로 화면 조각(카드 · 목록 · 입력 칸 · 칩 · 버튼)을 작게 그림 (적용은 다시 시작할 때)
class CThemePreview : public CWnd
{
public:
	bool Create(CWnd* parent, const CRect& rc, UINT id);
	void SetTheme(int index) { m_index = index; if (GetSafeHwnd()) Invalidate(FALSE); }

protected:
	int m_index = 0;
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------------
// 설정 창 (메인 창 오른쪽 위 톱니바퀴 버튼)
//  - DB 삭제: 영상 / 배우 / 스튜디오 / 태그 중 고른 것을 지움 (파일은 지우지 않음)
class CSettingsDlg : public CDialogEx
{
public:
	enum DbMask { DB_VIDEO = 1, DB_ACTOR = 2, DB_STUDIO = 4, DB_TAG = 8 };

	CSettingsDlg(const CVideoLibrary& lib, CWnd* pParent = nullptr);

	// [선택한 DB 삭제] 확인 후 호출 (메인 창이 실제로 지우고 화면 갱신)
	std::function<void(int mask)> m_onDeleteDb;
	// [텍스트 내보내기 / 가져오기] (what: CVideoLibrary::EXPORT_ACTORS / STUDIOS / TAGS) - 메인 창이 파일 고르기 · 처리 · 화면 갱신
	std::function<void(int what)> m_onExportList;
	std::function<void(int what)> m_onImportList;

	// [표시] 영상 카드 해상도 · 재생 시간 (DoModal 전에 지금 값, IDOK 후 고른 값)
	bool m_cardMedia = true;
	bool m_sfw = false;   // SFW: 배우 · 영상 이미지 모자이크
	bool m_mergeParts = false;   // 분할 영상 합쳐서 보기 (격자에 첫 파트 카드 하나만)
	bool m_ratingHalf = false;   // 별점 단위: false = 1점, true = 0.5점
	int  m_censorLang = 0;       // 검열 정보 표시 언어: 0 한글, 1 영어, 2 일본어
	bool m_detailLeft = false;   // 상세 화면 위치: false = 오른쪽, true = 왼쪽
	int  m_detailStrip = 0;  // [영상 상세 태그 아래] 0 = 출연 배우 카드, 1 = 영상 스크린샷
	int  m_shotCols = 3;     // 영상 스크린샷 가로 배치 수 (1 ~ 10)
	int  m_themeIndex = 0;   // [테마] 고른 테마 번호 (Theme::Name, 다시 시작하면 적용)

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_SETTINGS };
#endif

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();

	const CVideoLibrary& m_lib;
	CDarkDialogTheme m_theme;   // 메인 창과 같은 어두운 색상

	void UpdateCounts();        // 체크박스 글자에 개수 표시
	// 탭: [표시] [상세 화면] [테마] [DB 관리] - 리소스에서 페이지마다 x 400 DLU 씩 떨어뜨려 둔 컨트롤을 첫 페이지 자리로 옮기고 탭마다 보이기 / 숨기기
	CDarkButton m_tabs[4];
	std::vector<std::pair<HWND, int>> m_pageCtrls;   // (컨트롤, 페이지 번호)
	int  m_page = 0;
	void ShowPage(int page);
	void PageOfCtrl(UINT id);   // 그 컨트롤이 있는 페이지로 (확인 때 잘못된 값으로 포커스 옮길 때)
	afx_msg void OnTab1() { ShowPage(0); }
	afx_msg void OnTab2() { ShowPage(1); }
	afx_msg void OnTab3() { ShowPage(2); }
	afx_msg void OnTab4() { ShowPage(3); }
	CThemePreview m_themePreview;   // [테마] 고른 색 미리보기
	afx_msg void OnThemeChanged();  // 테마 콤보 → 미리보기 갱신
	afx_msg void OnBnClickedDeleteDb();
	afx_msg void OnBnClickedExportList();
	afx_msg void OnBnClickedImportList();
	int  SelectedIoKind();
	afx_msg void OnCheckChanged();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()
};
