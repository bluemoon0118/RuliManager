#pragma once

#include <functional>
#include <vector>
#include <map>

// 연도별 보기 (배우 출연작 화면 [연도별])
//            1월           2월     ...   12월   (?)
//   2025년   [카드]        [카드]         ...
//            [카드]
//   2026년   [카드]                       ...
//  - 가로 = 달 (1 ~ 12월, 발매월을 모르는 영상이 있으면 맨 오른쪽 "?" 칸), 세로 = 연도 (영상이 있는 연도만, 오래된 연도부터),
//    발매일이 없는 영상은 맨 아래 "날짜 없음" 줄
//  - 한 달에 영상이 여러 개면 그 칸에 세로로 쌓음 (달 칸 폭 = 카드 하나, 연도 줄 높이 = 그 연도에서 영상이 가장 많은 달 기준)
//  - 연도 줄은 한 줄 걸러 조금 밝은 배경 띠 + 연도 줄 사이 진한 2px 구분선 (연도 이름 칸부터 오른쪽 끝까지)
//  - 카드 뒤 배경색: 주인이 정한 항목별 색 (SetCardColors — 배우 이름(별칭 변경 이력)마다 고유색), 맨 위 줄에 범례 (SetLegend, 있을 때만)
//  - 카드는 주인이 그림 (m_onDrawCard), 위 달 머리글은 세로 스크롤해도, 왼쪽 연도 이름은 가로 스크롤해도 고정
//  - 가로 · 세로 스크롤바 (다른 목록과 같은 직접 그린 스크롤바를 주인이 겹쳐 놓음, 휠 = 세로, Shift + 휠 = 가로), 클릭 = 선택, 더블클릭 = 열기, 오른쪽 클릭 = 메뉴
//  - 나눠진 영상(파트): 첫 번째 파트 카드 하나만, 카드를 파트 수만큼 좌우로 나눠 마우스를 올리면 반투명 1 · 2 … 표시,
//    누른 칸의 파트를 선택 (더블클릭 = 그 파트 열기, 오른쪽 클릭 = 그 파트 메뉴)
class CYearView : public CWnd
{
public:
	struct Column
	{
		CString title;                 // "2025년" / "날짜 없음" (한 줄 = 한 연도)
		std::vector<int> months[13];   // 0 ~ 11 = 1 ~ 12월, 12 = 발매월 모름 (항목 번호 = 주인의 목록 행)
	};

	std::function<void(CDC*, int item, const CRect&, bool selected)> m_onDrawCard;
	std::function<void(int item)> m_onSelect;                 // 클릭
	std::function<void(int item)> m_onActivate;               // 더블클릭
	std::function<bool(int item, const CRect& card, CPoint pt)> m_onDblClickAt;   // 더블클릭 먼저 (카드 · 점 = 클라이언트 좌표, true = 처리함 → 열기 안 함)
	std::function<void(int item, CPoint screenPt)> m_onContext;   // 오른쪽 클릭 (item = -1 이면 빈곳)

	bool Create(CWnd* parent, UINT id);
	void SetColors(COLORREF back, COLORREF title, COLORREF label, COLORREF line);
	void SetCardSize(int w, int h);
	void SetData(std::vector<Column>&& columns, std::map<int, std::vector<int>>&& parts, bool resetScroll);   // parts: 카드 항목 → 파트 항목들 (1번부터, 2개 이상일 때만)
	void SetCardColors(std::map<int, COLORREF>&& colors);   // 카드 항목 → 카드 뒤 배경색 (없는 항목은 칠하지 않음), SetData 전에
	void SetLegend(std::vector<std::pair<CString, COLORREF>>&& legend);   // 맨 위 범례 (이름, 색) — 비면 범례 줄 없음, SetData 전에
	void SetSelected(int item);
	void InvalidateItem(int item);   // 그 카드만 다시 그림 (이미지가 늦게 도착했을 때)
	void ScrollToStart();

protected:
	std::vector<Column> m_cols;
	int  m_cardW = 120, m_cardH = 180;
	int  m_sel = -1;
	std::map<int, std::vector<int>> m_parts;   // 카드 항목 → 파트 항목들
	std::map<int, int> m_partOwner;            // 파트 항목 → 카드 항목
	std::map<int, COLORREF> m_cardColors;      // 카드 항목 → 카드 뒤 배경색
	std::vector<std::pair<CString, COLORREF>> m_legend;   // 범례 (이름, 색)
	int  m_legendH = 0;                        // 범례 줄 높이 (달 머리글 위, 범례가 없으면 0)
	int  m_hotItem = -1, m_hotPart = -1;       // 마우스가 올라간 카드 · 파트 칸
	bool m_tracking = false;
	int  OwnerOf(int item) const;              // 파트면 그 카드 항목, 아니면 그대로
	int  ItemAt(CPoint client, int* part = nullptr) const;   // 누른 곳의 항목 (파트 칸이면 그 파트 항목)
	void SetHot(int item, int part);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
	afx_msg LRESULT OnDarkScrollSetPos(WPARAM wParam, LPARAM lParam);
	int  m_scrollX = 0, m_scrollY = 0;
	COLORREF m_back = TCOL(TC_BACK);
	COLORREF m_title = TCOL(TC_TEXT);
	COLORREF m_label = TCOL(TC_SUB2);
	COLORREF m_line = TCOL(TC_COMBO);
	CFont m_titleFont;

	// 배치 (내용 좌표)
	std::vector<int> m_colX, m_colW;   // 달 칸 (가로) 위치 · 폭
	std::vector<int> m_rowY, m_rowHs;  // 연도 줄 (세로) 위치 · 높이
	std::vector<std::pair<CRect, int>> m_cards;   // 카드 영역 → 항목
	int  m_contentW = 0, m_contentH = 0;
	int  m_headH = 0, m_rowH = 0, m_labelW = 0, m_gap = 6, m_rows = 12;   // 달 머리글 높이 · 카드 한 단 높이 (스크롤 단위) · 연도 이름 폭 · 간격 · 달 칸 수

	void Relayout();
	void UpdateScrollBars();
	void ScrollTo(int x, int y);
	int  HitTest(CPoint client) const;
	CFont* BaseFont();

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnMouseHWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	DECLARE_MESSAGE_MAP()
};
