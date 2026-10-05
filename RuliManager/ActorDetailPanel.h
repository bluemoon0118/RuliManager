#pragma once

#include <functional>
#include <vector>
#include <memory>
#include "VideoLibrary.h"
#include "StarRatingCtrl.h"
#include "HeartToggle.h"

// 배우 탭 오른쪽 아래 상세 정보
//   [큰 굵은 이름]  ♥
//   ☆☆☆☆☆
//   성별:   여성
//   출생:   1998-02-17 (28세)
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
	std::function<void()>    m_onDebutDblClick;   // 데뷔작 품번 상자 더블클릭 (그 영상으로 이동)

	bool Create(CWnd* parent, UINT id);
	void SetColors(COLORREF back, COLORREF text, COLORREF label);
	void SetActor(const ActorInfo* a, int videoCount, const CString& debutCode = CString());   // nullptr = 선택 없음, debutCode = 데뷔일과 같은 날 발매된 출연작 품번
	int  CalcHeight(int width);                          // 이 폭에서 필요한 높이

protected:
	struct Row { CString label; CString value; int flag = -1; CString symbol; COLORREF symColor = 0; CString badge; std::vector<CString> links; };   // links: 링크 줄 (사이트 아이콘으로, 더블클릭하면 브라우저로 열기)   // badge: 값 오른쪽 테두리 상자 글자 (데뷔작 품번)   // symbol: 값 왼쪽 기호 (성별 ♀ ♂ …)
	bool     m_has = false;
	CString  m_name;
	CString  m_memo;                  // 메모 (이력 아래, 여러 줄)
	int    MemoHeight(CDC& dc, int width);   // 메모가 차지할 높이 (최대 줄 수 제한)
	std::vector<Row> m_rows;
	std::vector<CString> m_history;   // 별칭 변경 이력 (별칭 순서 → 마지막이 대표 이름), 괄호 부분은 뺌
	CFont    m_histFont;
	CFont    m_symFont;    // 성별 기호 (크게, 굵게)
	// 이력 줄 배치: 항목마다 시작 위치(x, 줄 번호), 반환값 = 줄 수
	int    LayoutHistory(CDC& dc, int width, std::vector<CPoint>* pos);
	CStarRatingCtrl m_star;
	CHeartToggle    m_heart;
	CFont    m_nameFont;
	COLORREF m_back = RGB(0x20, 0x2B, 0x33);
	COLORREF m_text = RGB(232, 238, 242);
	COLORREF m_label = RGB(0x8A, 0x9B, 0xA8);

	CFont* BaseFont() const;
	CFont* InfoFont();      // 별점 아래 글자(정보 줄 · 이력 · 메모): 기본 글꼴 + 2pt
	CFont  m_infoFont;
	void   EnsureFonts();
	int    NameHeight();
	int    RowHeight();        // 정보 줄 높이 (여백 4pt 줄임)
	int    StarRowHeight();    // 별점 줄 높이 (예전 줄 높이 그대로)
	int    LabelWidth();
	int    HistLineHeight();
	void   LayoutChildren();

	afx_msg void OnPaint();
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	CRect m_badgeRect;
	std::vector<std::pair<CRect, CString>> m_linkRects;   // 마지막으로 그린 링크 아이콘 영역 → URL
	// 사이트 아이콘(파비콘): Image\favicons\<도메인>.png 에 받아 두고 씀 (없으면 백그라운드로 받음)
	std::map<CString, std::unique_ptr<Gdiplus::Bitmap>> m_favicons;   // 도메인 → 아이콘 (nullptr = 아직 없음)
	std::set<CString> m_faviconRequested;                               // 이번 실행에서 받기를 시도한 도메인
	Gdiplus::Bitmap* Favicon(const CString& domain);
	void DrawLinkIcon(CDC& dc, const CString& url, const CRect& rc);
	afx_msg LRESULT OnFaviconReady(WPARAM, LPARAM);
	CToolTipCtrl m_tip;   // 링크 아이콘 위에서 주소 표시
	std::vector<std::pair<CRect, CString>> m_tipRects;   // 풍선 도움말을 만든 기준 (바뀌면 다시 만듦)
	BOOL PreTranslateMessage(MSG* pMsg) override;
	void UpdateLinkTips();   // 마지막으로 그린 데뷔작 품번 상자 (없으면 빈 사각형)
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg LRESULT OnGetFont(WPARAM, LPARAM);   // 자식(별점 숫자)이 부모 글꼴을 쓰도록
	DECLARE_MESSAGE_MAP()
};
