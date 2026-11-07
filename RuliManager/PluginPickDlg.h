#pragma once

#include "DarkTheme.h"
#include "Resource.h"
#include "ImagePreview.h"
#include "ImageViewerWnd.h"
#include <vector>
#include <map>

// ---------------------------------------------------------------------------
// 플러그인 결과 → 채울 항목 고르기 (붙여넣기 흐름의 마지막 확인 창 대신)
//  - 줄마다: [체크 + 항목 이름] [가져온 값 (고칠 수 있음)] [→ (체크했을 때)] [지금 값 (읽기 전용)]
//  - 가져온 값이 현재 값과 같으면 "(같음)" 표시 - 그래도 체크 · 고칠 수 있음 (여러 개를 받을 때 다음 영상에도 그 항목을 적용하도록)
//  - 기본 체크: 플러그인별로 기억한 선택이 있으면 그대로, 없으면 현재 값이 비어 있는 항목만
//  - 맨 아래 이미지 줄 (커버 / 사진): 썸네일 [가져온 이미지] → [지금 이미지], 썸네일 더블클릭 = 원본 보기 창
//  - 여러 항목을 받는 중이면 [나머지 n개에도 같은 항목 적용] (다음부터 창 없이 같은 선택으로)
struct PluginPickRow
{
	CString key;      // 기억용 키 (title, actors ...)
	CString label;    // 화면 이름 (제목, 배우 ...)
	CString cur;      // 지금 값
	CString value;    // 가져온 값 (창에서 고친 값으로 돌아옴)
	bool    check = false;
	bool    same = false;   // 가져온 값 = 지금 값
	bool    image = false;  // 이미지 줄: cur / value = 이미지 파일 경로 (고칠 수 없음, 맨 아래 썸네일)
};

// 한 번의 플러그인 실행 동안 유지되는 선택 상태 (RunPlugin → PasteVideoInfoText / PasteActorInfoText)
struct PluginPickState
{
	CString pluginId;
	CString pluginName;
	int     remaining = 0;          // 이 항목 다음에 남은 결과 수
	bool    applyAll = false;       // [나머지에도 같은 항목 적용] → 다음부터 창 없이
	std::map<CString, bool> states; // 키 → 체크 (applyAll 일 때 쓰는 선택 / 기억한 선택)
	bool    applied = false;        // 이번 항목을 적용했는지 (항목마다 RunPlugin 이 지움)
	bool    stop = false;           // [취소] = 남은 결과 모두 그만
	CString imagePath;              // 이번 항목의 가져온 이미지 (로컬 임시 파일, 없으면 빈 문자열 - RunPlugin 이 받아 둠)

	void LoadRemembered();          // 레지스트리 Settings\PluginPick_<id> = "title=1|actors=0|..."
	void SaveRemembered() const;
	// 기본 체크 정하기: 기억 → 그대로, 없으면 지금 값이 빈 것만 (같음 줄도 같은 규칙)
	void ApplyDefaults(std::vector<PluginPickRow>& rows) const;
};

class CPluginPickDlg : public CDialogEx
{
public:
	CPluginPickDlg(const CString& caption, const CString& guide, const std::vector<PluginPickRow>& rows, int remaining, CWnd* pParent = nullptr);

	std::vector<PluginPickRow> m_rows;   // IDOK 일 때 체크 · 고친 값
	bool m_applyAll = false;
	int  m_result = IDCANCEL;            // IDOK = 적용, IDC_PICK_SKIP = 이 항목만 건너뜀, IDCANCEL = 남은 것 모두 그만

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_PLUGIN_PICK };
#endif

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();

	CString m_caption, m_guide;
	int     m_remaining = 0;
	CDarkDialogTheme m_theme;
	CImagePreview    m_imgNew, m_imgCur;   // 이미지 줄 썸네일 (가져온 이미지 / 지금 이미지)
	CImageViewerWnd  m_viewer;             // 썸네일 더블클릭 → 원본 이미지 보기

	static const UINT kRowIdBase = 3000; // 줄 i: 체크 = base + i*4, 지금 값 = +1, 가져온 값 = +2, 화살표 = +3
	static const UINT kRowIdStep = 4;
	static UINT RowId(size_t i) { return kRowIdBase + static_cast<UINT>(i) * kRowIdStep; }
	void Collect();
	void UpdateArrow(size_t i);           // 체크하면 가져온 값 → 지금 값 사이에 화살표
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);

	afx_msg void OnBnClickedSkip();
	afx_msg void OnBnClickedCheckAll();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()
};
