#pragma once

#include <functional>
#include <vector>
#include "VideoLibrary.h"
#include "StarRatingCtrl.h"
#include "HeartToggle.h"

// 배우 탭 오른쪽 아래 상세 정보
//   [큰 굵은 이름]  ♥
//   ☆☆☆☆☆
//   성별:   여성
//   나이:   28 (1998-02-17)
//   국적:   일본 [국기]
//   키:     162cm
//   치수:   B95 W58 H88 / 컵: I컵
//   (아래) 별칭 변경 이력: 아스카 아카 → 시오세 → 나기 히카루
//   (그 아래) 메모
//   데뷔 / 은퇴 (있을 때) / 출연
class CActorDetailPanel : public CWnd
{
public:
	std::function<void(int)> m_onRating;   // 별 클릭 (새 별점)
	std::function<void()>    m_onFavorite; // 하트 클릭

	bool Create(CWnd* parent, UINT id);
	void SetColors(COLORREF back, COLORREF text, COLORREF label);
	void SetActor(const ActorInfo* a, int videoCount);   // nullptr = 선택 없음
	int  CalcHeight(int width);                          // 이 폭에서 필요한 높이

protected:
	struct Row { CString label; CString value; int flag = -1; };
	bool     m_has = false;
	CString  m_name;
	CString  m_memo;                  // 메모 (이력 아래, 여러 줄)
	int    MemoHeight(CDC& dc, int width);   // 메모가 차지할 높이 (최대 줄 수 제한)
	std::vector<Row> m_rows;
	std::vector<CString> m_history;   // 별칭 변경 이력 (별칭 순서 → 마지막이 대표 이름), 괄호 부분은 뺌
	CFont    m_histFont;
	// 이력 줄 배치: 항목마다 시작 위치(x, 줄 번호), 반환값 = 줄 수
	int    LayoutHistory(CDC& dc, int width, std::vector<CPoint>* pos);
	CStarRatingCtrl m_star;
	CHeartToggle    m_heart;
	CFont    m_nameFont;
	COLORREF m_back = RGB(0x20, 0x2B, 0x33);
	COLORREF m_text = RGB(232, 238, 242);
	COLORREF m_label = RGB(0x8A, 0x9B, 0xA8);

	CFont* BaseFont() const;
	void   EnsureFonts();
	int    NameHeight();
	int    RowHeight();
	int    LabelWidth();
	int    HistLineHeight();
	void   LayoutChildren();

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg LRESULT OnGetFont(WPARAM, LPARAM);   // 자식(별점 숫자)이 부모 글꼴을 쓰도록
	DECLARE_MESSAGE_MAP()
};
