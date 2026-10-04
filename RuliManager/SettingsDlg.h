#pragma once

#include <functional>
#include "DarkTheme.h"
#include "VideoLibrary.h"

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

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_SETTINGS };
#endif

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();

	const CVideoLibrary& m_lib;
	CDarkDialogTheme m_theme;   // 메인 창과 같은 어두운 색상

	void UpdateCounts();        // 체크박스 글자에 개수 표시
	afx_msg void OnBnClickedDeleteDb();
	afx_msg void OnCheckChanged();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()
};
