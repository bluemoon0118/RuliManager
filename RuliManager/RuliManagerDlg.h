#pragma once

#include <memory>
#include <set>
#include "VideoLibrary.h"
#include "ImagePreview.h"
#include "ImageViewerWnd.h"
#include "VideoGrid.h"
#include "DarkControls.h"
#include "DarkTheme.h"   // CDarkHeader (영상 [리스트] 보기 열 머리글)
#include "SuggestEdit.h"
#include "TagChipCtrl.h"
#include "ZoomSlider.h"
#include "StarRatingCtrl.h"
#include "ActorDetailPanel.h"
#include "ActorStrip.h"
#include "MediaInfoLabel.h"
#include <mutex>
#include <condition_variable>
#include <deque>
#include <thread>
#include <atomic>
#include "DropCounter.h"
#include "ScreenshotExtractor.h"
#include "YearView.h"
#include "PreviewVideo.h"
#include "TreemapView.h"

// 컬렉션 · 제작사 영상 화면 위쪽 정보 (배우 출연작 화면의 배우 정보 자리): 제목(크게) · 정보 줄 · 메모 + (제작사) 하위 레이블 이미지 칸
//  - 더블클릭 = 편집 (m_onDblClick), 레이블 칸 더블클릭 = 그 레이블 영상 (m_onSubDblClick)
class CCollectionInfoPanel : public CWnd
{
public:
	struct SubItem { CString name; Gdiplus::Bitmap* img = nullptr; };
	BOOL Create(CWnd* parent, UINT id);
	void SetInfo(const CString& title, const CString& subLine, const CString& memo, bool favorite);
	void SetSubItems(const CString& caption, const std::vector<SubItem>& items, COLORREF tileBack);   // 비우면 표시 안 함
	std::function<void(int)> m_onSubDblClick;
	void SetColors(COLORREF back, COLORREF text, COLORREF sub) { m_back = back; m_text = text; m_sub = sub; if (GetSafeHwnd()) Invalidate(FALSE); }
	int  CalcHeight(int width);   // 지금 내용을 그리는 데 필요한 높이
	std::function<void()> m_onDblClick;

protected:
	CString  m_title, m_memo, m_subLine;
	bool     m_fav = false;
	CString  m_subCaption;
	std::vector<SubItem> m_subs;
	std::vector<CRect>   m_subRects;   // 마지막 배치의 레이블 칸 (클라이언트 좌표)
	COLORREF m_tileBack = RGB(0x3E, 0x37, 0x56);
	COLORREF m_back = RGB(0x20, 0x2B, 0x33), m_text = RGB(255, 255, 255), m_sub = RGB(160, 170, 180);
	CFont    m_bigFont;
	void     EnsureFont();
	int      Layout(CDC* dc, int width, CRect* titleRc, CRect* countRc, CRect* memoRc, CRect* capRc = nullptr);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	DECLARE_MESSAGE_MAP()
};

class CRuliManagerDlg : public CDialogEx, public IVideoGridOwner
{
public:
	CRuliManagerDlg(CWnd* pParent = nullptr);

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_RULIMANAGER_DIALOG };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();

	// 컨트롤
	HICON      m_hIcon;
	CBrush     m_bgBrush;                       // 배경색 브러시
	CBrush     m_editBrush;                     // 에디트 배경색 브러시
	static inline const COLORREF& kBackColor = TCOL(TC_BACK);   // #202B33
	static inline const COLORREF& kTextColor = TCOL(TC_TEXT);
	static inline const COLORREF& kEditColor = TCOL(TC_EDIT);   // #1B252C (에디트 배경)
	static inline const COLORREF& kDisabledText = TCOL(TC_DISABLED);
	static inline const COLORREF& kComboColor = TCOL(TC_COMBO);  // #394B59 (콤보박스 배경)
	static inline const COLORREF& kDateColor  = TCOL(TC_EDIT);  // #1B252C (날짜 컨트롤 배경)
	static inline const COLORREF& kButtonColor = TCOL(TC_ACCENT); // #137CBD (버튼 배경)
	static inline const COLORREF& kSaveColor = TCOL(TC_SAVE);   // #0F9960 (저장 버튼 배경)
	static inline const COLORREF& kDangerColor = TCOL(TC_DANGER); // #DB3737 (삭제/제거 버튼 배경)
	CDarkButton m_darkButtons[20];
	CDarkButton m_btnSettings;      // 오른쪽 위 톱니바퀴 (설정 창)
	CDarkButton m_modeButtons[5];   // 영상 / 배우 / 스튜디오 / 태그 / 컬렉션 토글 버튼
	CDarkButton m_pageButtons[9];   // 페이지: « < 1 2 3 4 5 > »
	void UpdateModeButtons();
	static inline const COLORREF& kScrollColor = TCOL(TC_SCROLL); // #6C7478 (스크롤바 배경)
	static inline const COLORREF& kThumbBackColor = TCOL(TC_THUMB); // #2A353D (격자 이미지 배경)
	CDarkScrollBar m_scrollBars[11];
	void CreateScrollBars();
	CListCtrl  m_list;
	CEdit      m_editSearch;
	CDarkCombo m_comboFilter;
	CStarRatingCtrl m_starRating;   // 별점 (마우스 오버 미리 보기, 클릭 확정)
	CActorDetailPanel m_actorPanel;  // 배우 탭 오른쪽 아래: 큰 이름 + ♥ / 별점 / 성별·나이·국적·키 …
	// 배우 출연작 화면 위쪽 (고정): 왼쪽 배우 사진 + 오른쪽 배우 정보 (배우 카드 더블클릭)
	CImagePreview     m_drillPhoto;
	CActorDetailPanel m_drillPanel;
	int  m_drillActorIdx = -1;
	// 배우 출연작 화면 보기: [격자] (기존) / [연도별] (연도 열 × 1~12월 줄) - 레지스트리 Settings\ActorDrillView
	CYearView   m_yearView;
	CDarkButton m_drillViewBtns[2];
	int  m_actorDrillView = 0;          // 0 = 격자, 1 = 연도별
	int  m_yearViewActor = -1;          // 연도별 보기를 마지막으로 만든 배우 (바뀌면 스크롤을 처음으로)
	bool m_cardImageOnly = false;       // DrawVideoCard: 이미지만 (연도별 보기 카드 - 덧그림 · 글자 없음)
	COLORREF m_cardFill = CLR_INVALID;  // DrawVideoCard: 커버 빈 영역(비율 여백 · 기본 이미지 배경) 색 (연도별 보기 - 배우 이름별 고유색, CLR_INVALID = 기본)
	std::map<int, COLORREF> m_yearCardColors;   // 연도별 보기 카드 항목 → 배우 이름별 고유색
	int  YearCardW() { return DX(73); }              // 연도별 보기 카드: 확대 1단계 영상 카드 이미지의 50% (확대 슬라이더 영향 없음)
	int  YearCardH() { return DX(73) * 9 / 16; }
	bool YearViewActive() const { return DrillHeaderShown() && m_actorDrillView == 1; }
	void RebuildYearView();
	void SetActorDrillView(int mode);
	void ShowVideoContextMenu(int row, CPoint point);   // 영상 오른쪽 클릭 메뉴 (격자 · 목록 · 연도별 공통)
	afx_msg void OnBnClickedDrillViewGrid();
	afx_msg void OnBnClickedDrillViewYear();
	// 제작사 탭 보기: [격자] (바둑판) / [리스트] (줄마다 제작사 + 그 레이블) - 레지스트리 Settings\StudioView
	CDarkButton m_studioViewBtns[2];
	int  m_studioView = 0;              // 0 = 격자, 1 = 리스트
	bool StudioListView() const { return m_mode == MODE_STUDIO && m_drill.IsEmpty() && m_studioView == 1; }
	void SetStudioView(int mode);
	afx_msg void OnBnClickedStudioViewGrid();
	afx_msg void OnBnClickedStudioViewList();
	// 영상 보기: [가로] 격자 (기존 영상 카드) / [세로] 격자 (배우 카드처럼 세로 3:4, 이미지만) - 영상 탭 · 배우 출연작 · 제작사 영상 · 태그 영상 화면별로 저장
	//  세로: 가로가 더 긴 이미지 = 오른쪽 절반, 세로가 더 긴 이미지 = 폭에 맞춰 비율 유지 (넘치면 위아래를 잘라 가운데)
	CDarkButton m_videoViewBtns[2];
	bool m_videoPortrait[4] = {};      // 화면별: [1] 배우 출연작, [2] 제작사 · 레이블 영상, [3] 태그 영상 (레지스트리 …Actor / …Studio / …Tag) - [0] 영상 탭은 [패키지] 보기로 옮김
	bool m_coverPortrait = false;      // GetVideoCover: 세로 카드용으로 자른 커버 (그리는 동안만 켬)
	int  m_pcardW = 160, m_pcardH = 213;   // 세로 카드 크기
	int  VideoViewSlot() const           // [격자|리스트|패키지] 버튼이 있는 영상 화면 (-1 = 없음) - 화면별로 따로 저장
	{
		if (m_mode == MODE_VIDEO) return 0;                              // 영상 탭
		if (m_drill.IsEmpty()) return -1;
		if (m_mode == MODE_ACTOR) return YearViewActive() ? -1 : 1;     // 배우 출연작 ([연도별] 보기는 제외)
		if (m_mode == MODE_STUDIO) return 2;                             // 제작사 · 레이블 영상
		if (m_mode == MODE_TAG) return 3;                                // 태그 영상
		if (m_mode == MODE_COLLECTION) return 4;                         // 컬렉션 영상
		return -1;
	}
	bool VideoPortraitActive() const   // 세로 카드 (이미지만) = [패키지] - [레이블] 보기도 커버 마우스 오버 팝업 · 미리보기는 [패키지] 와 같이 동작
	{
		const int s = VideoViewSlot();
		return s >= 0 && (m_videoView[s] == 2 || m_videoView[s] == 4);
	}
	bool VideoLabelActive() const       // [레이블]: 제작사 · 레이블 영상 화면에서 레이블 + 품번 묶음마다 머리줄, 줄마다 체크 · 품번 · 세로 커버 · 제목 · 출연 배우 · 발매일
	{
		const int s = VideoViewSlot();
		return s == 2 && m_videoView[s] == 4;
	}
	bool VideoWallActive() const        // [월 모드]: 16:9 이미지만 빈틈없이 붙임 + 미리보기 자동 재생
	{
		const int s = VideoViewSlot();
		return s >= 0 && m_videoView[s] == 3;
	}
	bool VideoImageCardActive() const { return VideoPortraitActive() || VideoWallActive(); }   // 이미지만 그리는 카드 ([패키지] · [월 모드])
	// [월 모드] 미리보기 자동 재생: 화면에 보이는 칸마다 미리보기가 있으면 격자의 자식 창으로 재생 (스크롤하면 같이 움직임)
	std::map<int, std::unique_ptr<CPreviewPlayer>> m_wallPlayers;   // 영상 항목 → 재생 창
	int  m_wallSyncTick = 0;
	int  m_wallSelItem = -1;           // 고른 칸 (재생 창을 안쪽으로 줄여 선택 테두리가 보이게)
	void SyncWallPlayers();
	void StopWallPlayers();
	CWallOverlay m_wallOverlay;        // [월 모드] 칸 위쪽 글자 (제목 · 출연 배우 · 발매일, 흰 글자 + 그림자, 재생 창 위에도 보이게 따로 겹친 투명 창)
	void UpdateWallOverlay();
	// [월 모드] CPU 줄이기: 프로그램이 뒤에 있거나(최소화 포함) 스크롤하는 동안 모든 칸 일시정지
	bool      m_wallPlaying = true;
	ULONGLONG m_wallScrollTick = 0;     // 마지막 스크롤 시각 (멈춘 뒤 0.3초 지나면 재생)
	size_t    m_wallOverlaySig = 0;     // 글자 창 내용 (보이는 칸 · 자리 · 글자) - 바뀔 때만 다시 그림
	size_t    m_wallHotSig = 0;         // 마우스가 올라간 칸 · 파트 · 고른 파트 (분할 영상 파트 칸 표시) - 바뀌면 다시 그림
	bool WallShouldPlay();
	void ApplyWallPlayState(bool play);
	void DrawWallOverlay(CDC* dc, const CRect& rc);
	void DrawWallPartZones(CDC* dc, int g, const CRect& card, int hotPart = -2);   // hotPart: -2 = 격자의 마우스 상태 (올라가 있을 때만), 그 밖 = 그 파트를 올라간 칸으로 (-1 = 없음) 항상 그림   // 분할 영상 파트 칸 (구분선 + 번호, 올라간 칸 흰 테두리, 고른 파트 파란 줄) - 색 키 글자 창용
	// [격자] · [패키지] 분할 영상 미리보기 재생 중: 재생 창 위에 파트 칸 ([월 모드] 와 같은 그림, 겹친 투명 글자 창)
	CWallOverlay m_gridPartOverlay;
	int    m_gridPartG = -1;          // 파트 칸을 그릴 카드
	CRect  m_gridPartCard;            // 그 카드 이미지 영역 (글자 창 좌표)
	size_t m_gridPartSig = 0;         // 내용 (카드 · 자리 · 올라간 파트 · 고른 파트) - 바뀌면 다시 그림
	void UpdateGridPartOverlay();
	CPoint    m_gridPartMouse;            // 마지막 마우스 위치 (화면 좌표) - 멈춰 있으면 파트 칸을 숨김
	ULONGLONG m_gridPartMoveTick = 0;     // 마우스가 마지막으로 움직인 시각
	static const ULONGLONG kGridPartIdleMs = 1500;   // 이만큼 멈춰 있으면 파트 칸 숨김 (다시 움직이면 보임)
	afx_msg void OnBnClickedVideoModeWall();
	void SetVideoPortrait(bool portrait);
	afx_msg void OnBnClickedVideoViewH();
	afx_msg void OnBnClickedVideoViewV();
	// 영상 탭 보기 방식: [격자] (카드 - [가로|세로]) / [리스트] (스프레드시트 - 숨겨 두던 목록 컨트롤을 보임) - 레지스트리 Settings\VideoList
	CDarkButton m_videoModeBtns[5];   // [격자] [리스트] [패키지] [월 모드] [레이블] (레이블은 제작사 · 레이블 영상 화면만)
	// [레이블] 보기 (VideoLabelActive)
	struct LabelGroup { CString label; CString code; CString seriesLabel; CString desc; CString image; int count = 0; };   // image: 레이블 대표 이미지   // 묶음 머리줄: 레이블 이름 · 품번 접두어 · 품번(시리즈)의 라벨 · 설명 · 영상(카드) 수
	std::map<int, LabelGroup> m_labelGroups;   // 묶음 첫 격자 칸 → 묶음
	int  m_lrowCoverW = 54, m_lrowCoverH = 72;   // 줄의 세로 커버 크기
	int  m_lrowH = 84;                           // 줄 높이 (카드)
	int  m_lgroupH = 40;                         // 묶음 머리줄 높이 (이미지 없을 때)
	int  m_limgH = 48;                           // 머리줄 레이블 이미지 높이 (이미지가 있는 묶음은 머리줄이 이만큼 + 여백 더 높음)
	int  m_lcodeW = 120;                         // 품번 칸 폭 (품번 글꼴로 10글자)
	struct LabelCols { CRect check, code, cover, title, actors, release; };
	LabelCols LabelColumns(const CRect& card);            // 줄 안 칸 자리 (머리줄 열 이름도 같은 x)
	void LabelGroupKey(int itemIdx, CString& label, CString& code) const;   // 영상의 묶음 (레이블 이름 · 품번 접두어)
	void GroupViewByLabel();                                     // m_view 를 레이블 → 품번 묶음 순으로 (묶음 안은 지금 정렬 그대로)
	void UpdateLabelLines();                                     // 격자 줄 배치 + 묶음 머리줄 (레이블 보기가 아니면 지움)
	void DrawLabelRow(CDC* dc, int g, const CRect& card, bool selected, bool focused);
	void GridDrawGroupHeader(CDC* dc, int firstCard, const CRect& rc) override;
	bool PkgAnchorRect(int g, CRect& rc);                  // [패키지] 팝업 기준 자리 (패키지 = 카드, 레이블 = 커버 칸, 격자 좌표)
	afx_msg void OnBnClickedVideoModeLabel();
	CDarkHeader m_listHeader;          // [리스트] 열 머리글 (어두운 색 + 정렬 삼각형)
	// 영상 화면별 보기: 0 = [격자] (가로 카드), 1 = [리스트] (스프레드시트), 2 = [패키지] (세로 카드, 이미지만), 3 = [월 모드] (16:9 이미지만 빈틈없이 + 미리보기 자동 재생)
	//  [0] 영상 탭, [1] 배우 출연작, [2] 제작사 · 레이블 영상, [3] 태그 영상, [4] 컬렉션 영상 - 레지스트리 Settings\VideoViewMode / …Actor / …Studio / …Tag / …Collection
	int  m_videoView[5] = {};
	bool VideoListActive() const { const int s = VideoViewSlot(); return s >= 0 && m_videoView[s] == 1; }
	void SetVideoMode(int mode);
	afx_msg void OnBnClickedVideoModeGrid();
	afx_msg void OnBnClickedVideoModeList();
	afx_msg void OnBnClickedVideoModePackage();
	afx_msg void OnNmClickList(NMHDR* pNMHDR, LRESULT* pResult);   // [리스트] 체크 칸 클릭
	afx_msg void OnNmCustomDrawList(NMHDR* pNMHDR, LRESULT* pResult);   // [리스트] 첫 열(체크 칸 오른쪽)에 커버 이미지
	CFont      m_listFont;             // [리스트] 글꼴 (기본 글꼴 + 1pt, 머리글 · 별점 숫자 · 태그 칩도)
	CFont      m_listCodeFont;         // [리스트] 품번 글꼴 (목록 글꼴 + 1pt, 굵게)
	CString    ListPartLabel(int itemIdx) const;   // 분할 영상 순번 "(2/3)" (아니면 빈 문자열)
	CImageList m_listRowImages;        // [리스트] 줄 높이용 빈 이미지 목록 (작은 아이콘 높이 = 커버 높이)
	int  m_listCoverW = 96, m_listCoverH = 64;   // [리스트] 커버 칸 크기
	bool ListCoverRect(int row, CRect& rc);      // 그 줄의 커버 그릴 자리 (목록 클라이언트 좌표)
	// [리스트] 별점 열: 영상 상세 별점 컨트롤과 같은 별 (마우스 오버 = 미리 보기, 클릭 = 그 별점으로 저장, 같은 별 다시 = 없음)
	int  m_listStarBandH = 20;                   // 별 줄 높이 (상세 별점 컨트롤 높이와 같게)
	int  m_listStarHoverRow = -1, m_listStarHoverVal = -1;
	bool m_listTrackLeave = false;
	bool ListStarBand(int row, CRect& band);     // 그 줄의 별 줄 자리 (목록 클라이언트 좌표)
	void DrawListTagChips(CDC* dc, const CRect& cell, const CString& tags);
	void AutoSizeListColumns();                  // [리스트] 열 폭: 목록 폭에 꽉 차게 (열마다 최소 폭, 남는 폭은 비율로 나눔 - 커버 · 별점은 고정)   // [리스트] 태그 열: 칩 (보기 전용, 칸 폭에서 줄바꿈, 넘치면 +N)
	void SetListStarHover(int row, int val);
	BOOL PreTranslateMessage(MSG* pMsg) override;
	// 배우 탭(배우 격자) 성별 거르기: [선택없음|여성만|남성만] (목록 도구줄) - 레지스트리 Settings\ActorGender
	CDarkButton m_genderBtns[3];
	int  m_actorGender = 0;            // 0 = 선택없음(전체), 1 = 여성만, 2 = 남성만 (배우 정보의 성별 값이 "여성" / "남성" 인 배우)
	void SetActorGender(int g);
	afx_msg void OnGenderButton(UINT nID);
	// 배우 탭 보기 방식: [격자] (배우 카드) / [트리맵] (차트 - 기준 콤보: 용량 / 영상수, 칸 안에 배우 이름 + 퍼센트)
	CDarkButton  m_actorViewBtns[2];
	int          m_actorView = 0;      // 0 = 격자, 1 = 트리맵 (레지스트리 ActorViewMode)
	int          m_treemapBasis = 0;   // 0 = 용량 (출연작 파일 크기 합), 1 = 영상수 (레지스트리 TreemapBasis)
	CTreemapView m_treemap;            // 배우 격자의 자식 (도구줄 아래 전체)
	CDarkCombo   m_comboTmBasis;       // 트리맵 기준 콤보 (트리맵 보기에서만)
	std::map<CString, ULONGLONG> m_actorBytes;   // 배우 이름(소문자) → 출연작 파일 크기 합
	bool ActorTreemapActive() const { return IsActorGridMode() && m_actorView == 1; }
	void SetActorView(int v);
	void RebuildTreemap();             // m_actorRows (트리맵 보기에서는 페이지 나누기 없이 전체) 로 차트 다시 채움
	void LayoutTreemap();              // 배우 격자 머리 영역(도구줄) 아래 전체로
	afx_msg void OnActorViewButton(UINT nID);
	afx_msg void OnCbnSelchangeTreemapBasis();
	// 장르(태그) 칩: [장르숨김][전체] (목록 도구줄) - [전체] 면 도구줄 아래에 태그 칩 줄, 칩 하나를 골라 그 태그 영상만
	//  화면별(영상 탭 / 배우 출연작 / 제작사 · 레이블 영상)로 따로: 레지스트리 Settings\GenreBar · GenreBarActor · GenreBarStudio
	CDarkButton   m_genreBtns[2];
	CGenreChipBar m_genreBar;
	bool    m_genreShow[3] = {};          // 화면별 [전체] 선택
	CString m_genreSel[3];                // 화면별 고른 태그 (비면 전체)
	CString m_genreKey[3];                // 화면별 고른 태그가 속한 대상 (배우 · 제작사가 바뀌면 고른 태그를 풂)
	int     m_genreBarH = -1;             // 마지막 배치 때 칩 줄 높이
	int  GenreSlot() const                // 0 = 영상 탭, 1 = 배우 출연작, 2 = 제작사 · 레이블 영상, -1 = 장르 칩 없는 화면
	{
		if (m_mode == MODE_VIDEO) return 0;
		if (m_mode == MODE_ACTOR && !m_drill.IsEmpty()) return 1;
		if (m_mode == MODE_STUDIO && !m_drill.IsEmpty()) return 2;
		return -1;
	}
	bool GenreBarActive() const { const int s = GenreSlot(); return s >= 0 && m_genreShow[s] && !YearViewActive() && !VideoLabelActive(); }   // 연도별 · [레이블] 보기에서는 쓰지 않음
	void SyncGenreKey();                  // 대상이 바뀌었으면 그 화면의 고른 태그를 풂
	void SetGenreShow(bool show);
	void UpdateGenreBar(const std::vector<int>& before);
	void RelayoutToolbarHeader();   // 장르 거르기 전 영상(m_view) 기준 태그별 건수 → 칩 줄
	afx_msg void OnBnClickedGenreHide();
	// 영상 상세: 품번 칸 오른쪽 [복사] (품번을 클립보드로)
	CDarkButton m_btnCopyCode;
	// 상세 화면 접기 바: 격자와 상세 화면 사이의 얇은 세로 바 - 누르면 상세 화면을 숨김 / 펼침 (레지스트리 Settings\DetailCollapsed)
	CDarkButton m_detailBar;
	bool        m_detailCollapsed = false;
	int  DetailBarW() { return DX(9); }
	void UpdateDetailBar();             // 화살표 · 툴팁 (상세 화면이 있는 쪽 / 접힘에 맞춰)
	afx_msg void OnBnClickedDetailBar();
	afx_msg void OnBnClickedCopyCode();
	afx_msg void OnBnClickedGenreAll();
	bool DrillHeaderShown() const { return m_mode == MODE_ACTOR && !m_drill.IsEmpty() && m_drillActorIdx >= 0; }
	void UpdateDrillHeader();        // 출연작 화면 위 배우 사진 · 정보 갱신
	// 컬렉션 · 제작사(레이블) 영상 화면 위쪽 (배우 출연작 화면처럼): 왼쪽 커버 · 로고(16:9) + 오른쪽 제목 · 정보 · 메모 (+ 제작사는 하위 레이블 이미지)
	CImagePreview        m_colCover;
	CCollectionInfoPanel m_colInfo;
	CString              m_drillCount;      // 들어간 카드의 영상 수 (글자)
	std::vector<CString> m_headerLabels;    // 위쪽 정보의 칸 순서 (더블클릭 = 그 레이블 / 제작사 영상)
	bool                 m_headerSubIsStudio = false;   // 칸이 상위 제작사 (레이블 영상 화면) - 아니면 하위 레이블
	bool CollectionHeaderShown() const { return m_mode == MODE_COLLECTION && !m_drill.IsEmpty(); }
	bool NamedHeaderShown() const { return (m_mode == MODE_COLLECTION || m_mode == MODE_STUDIO) && !m_drill.IsEmpty(); }
	void UpdateCollectionHeader();   // 컬렉션 · 제작사 · 레이블 위쪽 정보 갱신
	void OnNamedHeaderEdit();        // 위쪽 커버 · 정보 더블클릭: 컬렉션 편집 / 제작사 · 레이블 관리 창
	void ToggleActorFavorite(int actorIdx);
	afx_msg void OnCatDelete();
	afx_msg void OnCatMerge();
	static void SortTagsKo(std::vector<CString>& tags);   // 영상 상세 태그 칩: 가나다 순   // 태그 탭: 선택한 태그를 다른 태그에 병합
	afx_msg void OnActorDelete();
	afx_msg void OnVideoTextInfo();
	afx_msg void OnVideoPasteInfo();
	// 격자 빈곳 오른쪽 클릭 → 새 항목
	afx_msg void OnNewActor();
	afx_msg void OnNewStudio();
	afx_msg void OnNewLabel();
	afx_msg void OnNewTag();
	void ShowNewItemMenu(CPoint screenPt);   // 지금 탭에 맞는 [새 …] 메뉴
	bool AskNewName(const CString& caption, const CString& prompt, CString& name);   // 영상 카드 오른쪽 클릭 → 웹페이지 내용 붙여넣기 (txt 와 연동 없음)
	void ApplyTextToVideo(int idx, const CString& text);   // "항목: 값" 글자로 영상 정보 교체 (텍스트로 정보 입력 / 사이트에서 가져오기)   // 영상 카드 오른쪽 클릭 → 텍스트로 정보 입력 (정보 txt 와 같은 규칙)
	afx_msg void OnActorMerge();      // 배우 카드 오른쪽 클릭 → 다른 배우와 합치기
	void MergeActorInto(int src, int dst, const CString& past);   // src 배우를 dst 배우로 합침 (src 항목 삭제, 영상 참여 별칭 = past, 비우면 그 영상에서 쓰던 이름)
	afx_msg void OnActorPasteInfo();  // 배우 카드 오른쪽 클릭 → 웹페이지 내용 붙여넣기 (프로필 화면 복사 글)
	afx_msg void OnActorTextInfo();   // 배우 카드 오른쪽 클릭 → 텍스트로 정보 입력   // 배우 탭 우클릭 → 배우 삭제 (Del)   // 태그 탭 우클릭 → 삭제
	// 컬렉션 탭
	std::map<CString, int> m_collectionCover;   // 컬렉션(소문자) → 대표 영상(카드 이미지, 처음 들어 있는 영상)
	int  m_ctxVideoItem = -1;                    // 영상 오른쪽 클릭 메뉴의 대상 영상 (컬렉션에 추가 / 빼기)
	std::vector<CString> m_ctxCollections;      // 그 메뉴의 [컬렉션에 추가 ▸] 항목 순서
	void ToggleCollectionFavorite(const CString& name);
	bool IsCollectionFavorite(const CString& name) const;
	void RenameSelectedCollection();
	void DrawCollectionCover(CDC* dc, const CString& name, const CRect& area, COLORREF back);
	afx_msg void OnNewCollection();
	afx_msg void OnCollectionAddNew();
	afx_msg void OnCollectionRemove();
	afx_msg void OnCollectionAdd(UINT nID);
	void ToggleTagFavorite(const CString& tag);   // 태그 즐겨찾기 (태그 목록에 없으면 추가)
	bool IsTagFavorite(const CString& tag) const;
	CDropCounter m_dropCounter;      // 영상 상세 정보: 별점 오른쪽 물방울 카운트
	CMediaInfoLabel m_mediaInfo;     // 영상 상세 정보: [이미지 변경] 위 "fps: 29.97 | 1080p"
	CActorStrip m_actorStrip;        // 영상 상세 정보 메모 아래: 출연 배우 카드 (가로 한 줄)
	// 영상 상세 태그 아래 영역: 0 = 출연 배우 카드 (기본), 1 = 영상 스크린샷 (영상 폴더의 Screenshot 폴더 이미지 격자) - 설정 창, 레지스트리 Settings\DetailStrip
	int         m_detailStripMode = 0;
	CActorStrip m_shotStrip;         // 영상 스크린샷 격자 (여러 줄, 넘치면 세로 스크롤)
	std::vector<CString> m_shotFiles;   // 스크린샷 파일 (이름 순)
	CString     m_shotVideo;         // 스크린샷 격자에 표시 중인 영상 경로 (바뀌면 스크롤을 맨 위로)
	std::map<CString, std::unique_ptr<CBitmap>> m_shotThumbs;   // "경로|폭|높이" → 축소 이미지 (nullptr = 못 읽음)
	int         m_shotCardW = 0, m_shotCardH = 0;
	int         m_shotCols = 3;      // 스크린샷 가로 배치 수 (설정 창, 레지스트리 Settings\ShotCols, 기본 3)
	bool ShotStripShown() const { return (VideoStripShown() || DrillHeaderShown()) && m_detailStripMode == 1; }   // 영상 탭 · 제작사 영상 화면 · 배우 출연작 화면 (배우 출연작 화면은 배우 카드 대신 없음 → 스크린샷만)
	void RefreshShotStrip();         // 지금 영상의 스크린샷 목록 다시 읽기
	void DrawShotCard(CDC* dc, int i, const CRect& rc, bool hot);
	CLinkBar    m_namedLinks;        // 제작사 탭 상세: 제작사 / 레이블 링크 (사이트 아이콘, 정보 줄 아래 - 링크가 있을 때만)
	CActorStrip m_labelStrip;        // 제작사 탭 상세: 선택한 제작사의 하위 레이블 카드 (여러 줄)
	std::vector<int> m_stripLabels;  // 카드: 0 이상 = labelInfos 인덱스 (하위 레이블), 음수 = -(studios 인덱스 + 1) (레이블의 상위 제작사)
	CString m_stripOwner;            // 제작사 탭: 카드 줄을 띄운 제작사 (그 제작사의 하위 제작사 카드인지 가르기용)
	bool m_stripForActor = false;    // true = 배우 상세: 그 배우 출연작의 제작사 / 레이블 카드 (m_stripCounts = 출연 편수)
	std::vector<int> m_stripCounts;
	void UpdateActorStudioStrip(int actorIdx);   // 배우 상세 하단: 출연작의 제작사 / 레이블 카드 (하위 레이블이 있는 제작사는 제외)
	void OpenNamedTarget(const CString& name, bool label);   // 제작사 탭에서 그 제작사 / 레이블의 영상 목록
	CStatic m_staticCodeSeries;      // 영상 상세: 품번 오른쪽 - 품번 접두어가 제작사 · 레이블의 시리즈와 같으면 그 라벨명
	CString m_codeSeriesLabel, m_codeSeriesDesc;   // 품번 오른쪽에 그릴 라벨 (보통) / 설명 (회색)
	void UpdateCodeSeriesText();
	// 영상 상세: 레이블이 있으면 [레이블] 줄만, 없으면 [제작사] 줄만 표시
	bool LabelRowShown() const { return m_labelChips.GetSafeHwnd() && !m_labelChips.Tags().empty(); }
	bool m_labelRowShown = false;
	void RelayoutIfLabelRowChanged(bool focusRow = false);
	// 영상 상세: 제작사 / 레이블 카드 (제작사 탭 상세의 카드 모양, [변경...] 버튼 · × 로 비우기 · 더블클릭 = 그 영상 보기)
	CActorStrip m_studioCard;
	CActorStrip m_labelCard;      // 영상 상세: 레이블 카드 (제작사 카드 아래 따로 한 줄)
	int  m_studioCardW = 0, m_studioCardH = 0;
	void DrawVideoStudioCard(CDC* dc, const CRect& rc, bool hot, bool labelCard = false);   // labelCard: 레이블 줄 카드
	void ClearStudioCard(bool labelCard);   // × : 제작사 카드 = 제작사(와 그 제작사의 레이블) 비우기, 레이블 카드 = 레이블만
	CRect StudioCardXRect(const CRect& card);   // DX() 가 const 가 아니므로 const 멤버로 두지 않음
	void OpenStudioCardTarget(int which = 0);   // which: 0 = 레이블이 있으면 레이블, 1 = 제작사, 2 = 레이블   // focusRow: 바뀐 줄 입력 칸으로 포커스
	CEdit   m_editNamedMemo;         // 제작사 탭 상세: 선택한 제작사 / 레이블 메모 (바로 편집, 포커스를 잃거나 다른 항목을 고르면 저장)
	int     m_memoKind = 0;          // 메모 대상: 0 = 없음, 1 = 제작사, 2 = 레이블
	CString m_memoName;
	bool    m_memoDirty = false;
	bool    m_memoLoading = false;
	void SetNamedMemoTarget(int kind, const CString& name);   // 메모 칸 대상 바꾸기 (이전 메모는 저장)
	void CommitNamedMemo();
	afx_msg void OnNamedMemoChanged();
	afx_msg void OnNamedMemoKillFocus();
	void UpdateLabelStrip(const CString& studioName, const CString& labelName = CString());   // 제작사 상세: 하위 레이블 카드 / 레이블 상세: 상위 제작사 카드 (둘 다 비면 숨김)
	void DrawLabelStripCard(CDC* dc, int i, const CRect& rc, bool hot);  // 레이블 카드 하나
	std::vector<int> m_stripActors;  // 배우 카드 띠의 배우 인덱스
	int  m_stripCardW = 0, m_stripCardH = 0;
	int  m_stripCardCurH = 0;        // 배우 카드 띠의 지금 카드 높이 (제작사 탭 = 제작 당시 나이 줄 없음)
	int  StripAgeH() const { return 1 + m_cardPad / 2 + m_cardLine + m_cardPad / 2; }   // 구분선 + 제작 당시 나이 줄 높이
	bool StripShowsAge() const { return m_mode != MODE_STUDIO || !m_drill.IsEmpty(); }   // 제작사 탭 상세(제작사 · 레이블의 출연 배우)는 영상이 없으므로 나이 줄 없음 (제작사 영상 화면은 있음)
	bool VideoStripShown() const { return m_mode == MODE_VIDEO || ((m_mode == MODE_STUDIO || m_mode == MODE_COLLECTION || m_mode == MODE_TAG) && !m_drill.IsEmpty()); }   // 영상 상세 태그 아래 출연 배우 카드: 영상 탭 + 제작사(레이블) 영상 화면
	bool m_namedActors = false;      // 제작사 탭 상세: 배우 카드 띠를 (하위 레이블 없는) 제작사 / 레이블의 출연 배우로 사용 중
	void UpdateNamedActorStrip(int kind, const CString& name);   // 제작사 탭 상세 하단 배우 카드 (kind: 0 = 숨김, 1 = 제작사, 2 = 레이블)
	void RefreshActorStrip();                                        // 숨긴 배우 칸 값으로 카드 띠 갱신
	void DrawStripCard(CDC* dc, int i, const CRect& rc, bool hot);   // 카드 띠의 배우 카드 하나
	void DrawStripCardFor(CDC* dc, int actorIdx, const CRect& rc, bool hot, bool stripHeartHot = true);   // 배우 번호로 (출연 배우 카드 모양)
	// 영상 카드 아래 배우 수에 마우스 → 카드 아래에 출연 배우 카드(작게, 좌우로) 팝업
	CCardPopup       m_castPopup;
	// [패키지] 카드 마우스 오버: [격자] 영상 카드 팝업 (패키지 카드 가운데에 겹쳐서)
	CCardPopup m_pkgPopup;
	int        m_pkgHoverG = -1;          // 마우스가 올라간 패키지 카드 (격자 칸)
	CRect      m_pkgHoverCard;            // 그 카드 (화면 좌표 - 움직이면 닫고 다시)
	ULONGLONG  m_pkgHoverTick = 0;        // 올라간 시각 (0.25초 뒤 팝업)
	ULONGLONG  m_pkgRepaintTick = 0;      // 팝업 다시 그린 시각 (커버를 읽는 중일 수 있어 0.25초마다)
	bool       m_popupCardDraw = false;   // 팝업에 카드를 그리는 중 (마우스 오버 표시 · 페이드 없음)
	void CheckPackageHover();
	// [패키지] 팝업 카드: 커버에 마우스 → 미리보기 (재생 창은 팝업의 자식, m_previewPlayers 를 같이 씀), 분할 영상은 파트 칸 (팝업에서 고름)
	bool         m_pkgPreviewOn = false;   // 팝업 카드에서 미리보기 재생 중 (재생 창의 부모 = 팝업)
	int          m_pkgPartHot = -1;        // 팝업 커버 위 마우스가 올라간 파트 (-1: 파트 칸 숨김)
	CWallOverlay m_pkgPartOverlay;         // 팝업 커버 위 파트 칸 (미리보기 위에도 보이게 겹친 투명 창 - 팝업에 딸림)
	size_t       m_pkgPartSig = 0;
	CPoint       m_pkgMouse;               // 마지막 마우스 위치 - kGridPartIdleMs 동안 멈추면 파트 칸 숨김
	ULONGLONG    m_pkgMoveTick = 0;
	void CheckPkgPreview();
	void OnPkgPopupMouse(UINT msg, CPoint pt);   // 팝업 카드 마우스 (클라이언트 좌표)
	int  PkgPartAt(CPoint pt) const;             // 팝업 카드 커버 위 파트 번호 (분할 영상이 아니거나 커버 밖이면 -1)
	std::vector<int> m_castActors;      // 팝업에 그릴 배우 번호 (DB 에 있는 배우만)
	int              m_castMore = 0;    // 화면 폭을 넘어 못 그린 배우 수 ("+N")
	int              m_castItem = -1;   // 마우스가 올라간 배우 수의 영상 항목
	ULONGLONG        m_castTick = 0;
	bool VideoCardCountZone(int g, const CRect& card, int which, CRect& zone, bool popupCard = false);   // popupCard: [패키지] 팝업의 [격자] 카드 (보기 방식과 관계없이 계산)   // 영상 카드(가로) 아래 개수 줄의 자리 (which 0 = 태그 수, 1 = 배우 수 - 격자 좌표, 0 개면 false)
	int              m_castKind = 0;    // 팝업 종류: 1 = 출연 배우 카드, 2 = 태그 칩
	bool             m_castInPkg = false;   // 개수 자리가 [패키지] 팝업 카드의 것 (배경 다시 그림은 팝업)
	CRect            m_castCardRect;    // 개수 자리 배경을 칠한 카드 (격자 좌표 - 바뀌면 다시 그림)
	CString          m_castTags;        // 태그 팝업에 그릴 태그 (쉼표 목록)
	void CheckCastHover();
	// 모든 카드 아래 집계(아이콘 + 숫자) 마우스 오버 배경
	HWND   m_countHotGrid = nullptr;   // 아래 줄을 강조 중인 격자
	CRect  m_countHotBand;             // 그 카드의 아래 줄 (격자 좌표)
	CPoint m_countHotPt;
	bool CountHoverAllowed() const;    // 프로그램이 앞에 있고 메뉴가 없을 때
	void HotCountBack(CDC* dc, CVideoGrid& grid, int left, int width, int y, int lineH = -1, int pad = -1);   // lineH · pad: 줄 높이 · 여백 (-1 = 기본 카드 값)   // 집계 한 묶음 자리에 마우스가 있으면 배경
	void CheckCountHover();
	int  CastCardH();                                           // 출연 배우 팝업 카드 높이 (사진 + 이름 한 줄)
	void DrawCastCard(CDC* dc, int actorIdx, const CRect& rc);   // 출연 배우 팝업 카드 (사진 + 이름만)
	void OpenActorFromStrip(int i);                                  // 더블클릭: 배우 탭에서 그 배우 선택
	void GoToActor(int actorIdx);                                    // 배우 탭(배우 목록)으로 가서 그 배우 선택
	CRect StripHeartRect(const CRect& card);                          // 카드 띠 카드의 즐겨찾기 하트 자리
	afx_msg void OnBnClickedApplyDb();
	afx_msg void OnBnClickedRescan();
	afx_msg void OnBnClickedExportTxt(); // [정보 txt 생성]: 영상 폴더 / 배우 폴더에 정보 txt    // [재 스캔]: 새로고침 + 저장된 영상에도 폴더 구조(배우·스튜디오·발매일) 다시 적용   // [DB 반영]: 작업 DB → library.vmdb
	void UpdateApplyDbButton();          // 반영 안 한 변경이 있을 때만 [DB 반영] 활성
	void UpdateActorPanel(const ActorInfo* a, int count);   // 배우 상세 패널 내용 (높이가 바뀌면 다시 배치)
	int             m_actorInfoIdx = -1;   // 오른쪽 패널에 표시 중인 배우
	CDarkCombo m_comboSort;        // 영상 정렬 기준 (격자에는 열 머리글이 없음)
	CDarkCombo m_comboPageSize;    // 페이지당 개수 (전체 / 50 / 100 / 250 / 500 / 1000)
	// 페이지 나누기: 목록(영상 · 배우 · 제작사/태그)마다 지금 페이지, 전체 개수
	int  m_pageSize = 250;         // 지금 탭의 페이지당 개수 (0 = 전체)
	int  m_pageSizes[5] = { 250, 250, 250, 250, 250 };   // 탭(영상 / 배우 / 제작사 / 태그 / 컬렉션)마다 따로 (레지스트리 Settings\PageSize0 ~ 4)
	void ApplyTabPageSize();       // 지금 탭의 페이지당 개수를 m_pageSize 와 콤보에 반영
	// 배우 출연작 화면(배우 카드 더블클릭)의 필터 값은 따로 저장: 정렬 · 오름/내림 · 별점 · 페이지당 개수
	//  (레지스트리 Settings\ActorDrillSortColumn / ActorDrillSortAsc / ActorDrillRating / ActorDrillPageSize)
	//  제작사 탭 영상 화면(제작사 · 레이블 카드 더블클릭)도 같은 방식으로 따로 (Settings\StudioDrill...)
	bool m_drillFilters = false;   // 지금 출연작 / 제작사 영상 화면 값을 쓰는 중
	int  m_drillCtx = 1;           // 1 = 배우 출연작 화면, 2 = 제작사 영상 화면
	int  m_drillPageSizes[3] = { 250, 250, 250 };
	CString DrillKey(LPCWSTR suffix) const { return CString(m_drillCtx == 2 ? L"StudioDrill" : L"ActorDrill") + suffix; }
	int  m_mainSortCol = 0, m_mainRating = 0;
	bool m_mainSortAsc = true;
	void EnterActorDrillFilters(int ctx = 1); // 출연작(1) / 제작사 영상(2) 화면에 들어갈 때: 영상 탭 값 보관 → 그 화면 값 적용
	void LeaveActorDrillFilters(); // 나올 때: 출연작 값 저장 → 영상 탭 값 되돌림
	int  m_pageVideoMain = 0;      // 영상 탭
	int  m_pageDrill = 0;          // 배우 출연작 · 제작사/태그 영상 화면
	int  m_pageActor = 0;          // 배우 격자
	int  m_pageCat = 0;            // 제작사 · 태그 목록
	int  m_totalVideo = 0, m_totalActor = 0, m_totalCat = 0;
	ULONGLONG m_totalVideoBytes = 0;   // 필터된 영상 전체 크기
	int& CurPageRef();             // 지금 보이는 목록의 페이지 번호
	int  CurTotal() const;         // 지금 보이는 목록의 전체 개수
	int  EffPageSize() const { return (StudioListView() || VideoLabelActive()) ? 0 : m_pageSize; }   // 제작사 탭 [리스트] · 영상 [레이블] 보기: 페이지 나누지 않음 (전체)
	int  PageCount(int total, int size = -1) const { if (size < 0) size = EffPageSize(); return (size <= 0 || total <= 0) ? 1 : (total + size - 1) / size; }
	void SlicePage(int& page, int total, int& first, int& last, int size = -1) const;   // 페이지 범위 [first, last) (size < 0: 지금 탭 값)
	void ResetCurPage() { CurPageRef() = 0; }
	void UpdatePager();            // 페이지 버튼 · 개수 요약 갱신
	int  LayoutListToolbar(int x, int y, int w);   // 목록(격자) 머리 영역의 검색 · 정렬 · 페이지 줄 배치 (격자 클라이언트 좌표, 높이 반환)
	// 목록 도구줄은 지금 보이는 격자의 자식으로 옮겨 카드와 함께 스크롤 (알림은 격자가 이 창으로 넘김)
	CVideoGrid* m_toolbarHost = nullptr;
	void SetToolbarHost(CVideoGrid* host);
	static const UINT* ToolbarIds(int& count);
public:
	// 도구줄 컨트롤은 격자의 자식이라 CWnd 기본 GetDlgItem 으로는 못 찾음 → 격자에서도 찾음
	CWnd* GetDlgItem(int nID) const
	{
		CWnd* w = CDialogEx::GetDlgItem(nID);
		if (!w && m_toolbarHost && m_toolbarHost->GetSafeHwnd())
			w = m_toolbarHost->GetDlgItem(nID);
		return w;
	}
	void SetDlgItemText(int nID, LPCTSTR text) { if (CWnd* w = GetDlgItem(nID)) w->SetWindowText(text); }
	int  GetDlgItemText(int nID, CString& text) const { text.Empty(); if (CWnd* w = GetDlgItem(nID)) w->GetWindowText(text); return text.GetLength(); }
protected:
	afx_msg void OnPageButton(UINT nID);
	afx_msg void OnCbnSelchangePageSize();
	CEdit      m_editCode;    // 품번 (제목 위)
	CEdit      m_editSeries;  // 시리즈 이름 (제작사 / 레이블 카드 아래, 품번 시리즈와 별개의 고유 글자 - VideoItem::seriesTitle)
	CEdit      m_editTitle;
	// 배우 / 별칭 / 스튜디오 / 태그: 입력하면 DB에서 실시간 검색해 목록으로 선택 (자동 완성)
	CSuggestEdit m_editActors;
	CSuggestEdit m_editVAliases;   // 이 작품에서 쓴 배우 별칭 (배우 칸 아래)
	CSuggestEdit m_editStudio;
	CDarkDateTime m_dateRelease;  // 발매일 (체크 해제 = 없음)
	void ExportListText(int what, CWnd* owner);   // 설정 창: 배우 / 제작사 / 태그 DB → 암호화 안 한 텍스트 파일
	void ImportListText(int what, CWnd* owner);   // 설정 창: 그 텍스트 파일 → 일괄 등록 (있는 항목은 값이 있는 칸만 덮어씀)
	CDarkCombo    m_comboCensor;  // 검열 정보 (발매일 오른쪽): 해당없음 / 검열됨 / 모자이크파괴 / 무수정 / 무수정-유출
	void FillCensorCombo();       // 설정 언어로 항목 다시 채움 (고른 값 유지)
	afx_msg void OnCbnSelchangeCensor();
	CListCtrl  m_listCat;         // 배우/스튜디오/태그 목록
	CSuggestEdit m_editTags;        // 태그 값 보관용 (숨김) - 화면에는 m_tagChips 로 표시
	CTagChipCtrl m_tagChips;        // 태그 칩 입력 ([태그 ×] ... × ⌄)
	CCardPopup   m_tagPopup;        // 태그 칩에 마우스를 올리면 그 태그 카드 (태그 탭 카드 모양)
	CString      m_tagPopupName;
	CCardPopup   m_actorPopup;      // 영상 상세 배우 칩에 마우스를 올리면 그 배우 카드 (배우 탭 카드 모양)
	int          m_actorPopupIdx = -1;
	void DrawTagPopupCard(CDC* dc, const CRect& rc, const CString& tag);
	// [폴더 관리] 버튼에 마우스를 올리면 등록 폴더 목록 팝업 (아이콘 · 경로 · 상태)
	struct FolderRow { CString path; CString tag; bool removable = false; bool offline = false; };
	std::vector<FolderRow> BuildFolderRows();
	CCardPopup             m_folderPopup;
	std::vector<FolderRow> m_folderPopupRows;
	CSize FolderPopupLayout(CDC* dc);                    // 팝업 크기 계산 (dc 에 글꼴을 골라 둔 상태에서)
	void  DrawFolderPopup(CDC* dc, const CRect& rc);
	void  ShowFolderPopup();
	CFont        m_detailFont;      // 영상 상세 글자 컨트롤 글꼴 (기본 + 1pt)
	int          m_detailTmH = 0;   // m_detailFont 글자 높이 (픽셀)
	void         ApplyDetailFonts();
	CTagChipCtrl m_studioChips;     // 스튜디오 칩 입력 (배우와 같은 모양, 하나만) - 값은 숨긴 m_editStudio 에 보관
	bool         m_syncingStudio = false;
	CTagChipCtrl m_labelChips;      // 레이블 칩 입력 (스튜디오 하위, 하나만) - 후보는 지금 스튜디오의 레이블 먼저
	CTagChipCtrl m_seriesChips;     // 시리즈 칩 입력 (레이블 하위, 하나만) - 후보는 지금 레이블의 시리즈 먼저
	afx_msg void OnEnChangeStudio();
	CTagChipCtrl m_actorChips;      // 배우 칩 입력 ([배우 (이 작품의 별칭) ×] ... × ⌄) - 값은 숨긴 m_editActors 에 보관
	bool         m_syncingActors = false;
	afx_msg void OnEnChangeActors();
	afx_msg void OnEnChangeVAliases();
	void ApplyDefaultAliases(const CString& oldActors, const CString& newActors);   // 새로 넣은 배우는 마지막 고른 별칭으로
	CString ActorChipDisplay(const CString& tag);       // 배우 칩에 보일 이름 (이 작품의 별칭 우선)
	bool    ActorChipGender(const CString& tag, CString& sym, COLORREF& col);   // 배우 칩 왼쪽 성별 기호 (♀ ♂ …, 성별 미지정 · 등록 안 된 배우는 false)
	void    OnActorChipMenu(int index, CPoint screenPt);  // 배우 칩 클릭: 이름/별칭 중 고르기
	bool         m_syncingTags = false;
	afx_msg void OnEnChangeTags();
	void SetupSuggestions();
	void HideSuggestions();
	afx_msg void OnMove(int x, int y);
	CEdit      m_editMemo;
	CStatic    m_staticName;
	CStatic    m_staticStatus;
	CStatic    m_staticImage;
	CImagePreview m_preview;     // 동영상과 같은 이름의 이미지 표시
	CImageViewerWnd m_imageViewer;   // 원본 이미지 보기 창 (영상 · 배우 상세 이미지 더블클릭)
	CVideoGrid m_grid;           // 격자 보기 (이미지 + 파일명 + 발매일 + 메모)
	CVideoGrid m_actorGrid;      // 배우 보기 (사진 + 이름 + 생년월일/출연 수 + 별칭/메모)

	// 배우 격자에 데이터를 제공하는 연결 객체
	struct ActorGridOwner : public IVideoGridOwner
	{
		CRuliManagerDlg* dlg = nullptr;
		int  GridGetCount() override;
		int  GridGetSel() override;
		void GridSetSel(int row) override;
		void GridGetItem(int row, int& image, CString& name, CString& line2, CString& line3) override;
		void GridActivate(int row) override;
		void GridKey(UINT vk) override;
		bool GridDrawCard(CDC* dc, int row, const CRect& card, bool selected, bool focused) override;
		bool GridClick(int row, const CRect& card, CPoint pt) override;   // 하트 → 즐겨찾기 전환, 체크 칸 → 체크 전환
		int  GridHitPart(int row, const CRect& card, CPoint pt) override; // 1 = 하트 위, 2 = 왼쪽 위 체크 칸 위
		bool GridDblClick(int row, const CRect& card, CPoint pt) override; // 체크 칸 빠른 두 번 클릭 = 두 번 전환 (출연작으로 안 들어감)
	};
	ActorGridOwner m_actorOwner;
	CRect ActorHeartRect(const CRect& card) const;   // 배우 카드 오른쪽 위 즐겨찾기 하트 자리
	CRect ActorCheckRect(const CRect& card) const;   // 카드 왼쪽 위 체크 칸 자리 (배우 · 영상 · 제작사 · 태그 · 컬렉션 공용, 크기 고정)
	int   m_checkSize = 18;                          // 체크 칸 한 변 (글꼴 기준, 카드 크기 계산 때 정함)
	std::set<CString> m_actorChecked;                // 체크한 배우 (소문자 이름, 목록을 다시 만들어도 유지)
	bool IsActorChecked(int actorIdx) const;
	void ToggleActorCheck(int actorIdx);
	std::vector<int> CheckedActorIndices() const;    // 체크한 배우 번호 (지금 있는 배우만)
	afx_msg void OnActorDeleteChecked();             // 체크한 배우 모두 삭제
	afx_msg void OnActorUncheckAll();                // 체크 모두 해제
	afx_msg void OnActorMergeChecked();              // 체크한 배우들을 오른쪽 클릭한(선택한) 배우로 병합

	// 배우 카드 (세로 사진 + 국기 / 성별 기호 + 이름(별칭) / 나이 / ▶ 출연 수)
	CFont m_cardNameFont;
	CFont m_cardSymbolFont;
	CFont m_actorSymbolFont;   // 배우 탭 카드 성별 기호 (카드 기호 글꼴보다 2pt 크게)
	CFont m_videoTitleFont;   // 영상 카드 제목 (카드 이름 글꼴보다 2pt 크게)
	int   m_videoLineB = 22;  // 그 한 줄 높이
	int   m_actorNameGap = 5;  // 배우 카드 사진과 이름 사이 간격 (기본 여백 - 2pt)
	int   m_vcardMemoGap = 3;  // 영상 카드 발매일과 메모 사이 추가 간격 (2pt)
	CFont m_vcardSubFont;     // 영상 카드 발매일 · 메모 (기본보다 1pt 크게, 굵게)
	int   m_vcardSubLine = 18;
	CFont m_actorNameFont;    // 배우 카드 이름 (영상 카드 제목보다 한 단계 크게)
	int   m_actorLineB = 20;  // 그 한 줄 높이
	CFont m_cardSubFont;      // 배우 카드 괄호 안 이름 (기본 글꼴보다 한 단계 크게)
	int   m_cardSubLine = 18;   // 그 한 줄 높이
	int   m_cardW = 190, m_cardImgH = 285, m_cardH = 380;
	int   m_cardPad = 8, m_cardLineB = 18, m_cardLine = 16;
	std::map<CString, std::unique_ptr<CBitmap>> m_actorPortraits;   // 카드용 세로 사진 (크기에 맞게 잘라 둔 것)
	void SetupActorCards();          // 카드 크기 계산 (현재 탭의 확대 단계 반영)

	// 격자 확대 (탭마다 4단계, 슬라이더)
	CZoomSlider m_zoomSlider;
	int  m_zoom[7] = { 0, 0, 0, 0, 0, 0, 0 };   // 영상 / 배우 / 스튜디오 / 태그 / 배우 출연작 화면 / 제작사 영상 화면 / 컬렉션 (레지스트리 Settings\Zoom0 ~ 6)
	int  ZoomSlot() const   // 배우 탭: 배우 격자 / 출연작 화면, 제작사 탭: 제작사 목록 / 영상 화면 따로
	{
		if (m_mode == MODE_ACTOR && !m_drill.IsEmpty()) return 4;
		if (m_mode == MODE_STUDIO && !m_drill.IsEmpty()) return 5;
		if (m_mode == MODE_COLLECTION) return 6;   // 컬렉션 탭 (목록 · 영상 화면 같이)
		return (m_mode >= MODE_VIDEO && m_mode <= MODE_TAG) ? m_mode : MODE_VIDEO;
	}
	double ZoomFactor() const;
	// 영상 화면 카드 크기: 해상도별 · 확대 단계별 "가로 몇 개" 표 (상세 화면을 접었을 때의 격자 폭 기준)
	int  VideoColumns(int step, bool wall, int monitorW) const;   // 가로 칸 수
	int  VideoRefGridWidth(int* monitorW = nullptr);              // 기준 격자 폭 (창이 놓인 모니터에서 최대화 + 상세 화면 접힘일 때의 격자 안쪽 폭)
	int  m_lastRefGridW = 0;                                      // 기준 격자 폭 (처음 한 번 계산 - 모니터를 옮겨도 그대로)
	int  m_refMonitorW = 0;                                       // 그때의 모니터 가로 픽셀 (해상도 표 고르기)
	void OnZoomChanged(int pos);
	void DrawActorCard(CDC* dc, int row, const CRect& card, bool selected, bool focused);
	void DrawActorCardFor(CDC* dc, int idx, const CRect& card, bool selected, bool focused, bool heartHot, bool showCounts = true);   // 배우 번호로 (배우 탭 카드 모양, showCounts = 아래 구분선 + ▶ 출연 수)
	int  ActorCardCountsH() const { return 1 + m_cardPad + m_cardLine + m_cardPad; }   // 배우 카드 아래 출연 수 영역 높이
	CBitmap* GetActorPortrait(int actorIdx, int w, int h);

	// 스튜디오/태그 격자 (m_listCat 와 같은 행을 카드로 표시, 선택은 m_listCat 이 관리)
	CVideoGrid m_catGrid;
	struct CatGridOwner : public IVideoGridOwner
	{
		CRuliManagerDlg* dlg = nullptr;
		int  GridGetCount() override;
		int  GridGetSel() override;
		void GridSetSel(int row) override;
		void GridGetItem(int row, int& image, CString& name, CString& line2, CString& line3) override;
		void GridActivate(int row) override;
		void GridKey(UINT vk) override;
		bool GridDrawCard(CDC* dc, int row, const CRect& card, bool selected, bool focused) override;   // 스튜디오 카드
		bool GridClick(int row, const CRect& card, CPoint pt) override;   // 태그 카드 하트 → 즐겨찾기 전환, 체크 칸 → 체크 전환
		int  GridHitPart(int row, const CRect& card, CPoint pt) override; // 1 = 태그 · 컬렉션 카드 하트 위, 2 = 왼쪽 위 체크 칸 위 (제작사 · 태그 · 컬렉션 카드)
		bool GridDblClick(int row, const CRect& card, CPoint pt) override; // 체크 칸 빠른 두 번 클릭 = 두 번 전환 (영상 화면으로 안 들어감)
		CString GridTipText(int row, const CRect& card, CPoint pt) override;   // 컬렉션 카드: 제목 + 메모
	};
	CatGridOwner m_catOwner;
	struct CatRow { int kind; CString value; };   // 분류 목록 한 행 (m_catRows)
	// 제작사 · 태그 · 컬렉션 카드 체크 (왼쪽 위 체크 칸) → 체크한 항목 모두 삭제 (탭별로 따로, 목록을 다시 만들어도 유지)
	std::set<CString> m_catChecked[5];               // [탭] → "종류|소문자 이름" (제작사 탭은 제작사 / 레이블 구분)
	bool CatChecksEnabled() const { return IsCategoryListMode() && !StudioListView(); }   // 카드 목록에서만 (제작사 [리스트] 보기 제외)
	static CString CatCheckKey(int kind, const CString& value);
	bool IsCatChecked(int kind, const CString& value) const;
	void ToggleCatCheck(int row);
	std::vector<CatRow> CheckedCatRows() const;      // 체크한 항목 (지금 목록에 있는 것만, 목록 순)
	afx_msg void OnCatDeleteChecked();               // 체크한 항목 모두 삭제
	afx_msg void OnCatUncheckAll();                  // 체크 모두 해제
	// 영상 카드 체크 (왼쪽 위 체크 칸) → 체크한 영상 DB 정보 모두 삭제 (파일은 그대로)
	static const int kVideoCheckPart = 1000;         // GridHitPart 값 (분할 파트 칸 1 · 2 … 와 겹치지 않게)
	std::set<CString> m_videoChecked;                // 체크한 영상 (소문자 경로, 목록을 다시 만들어도 유지)
	bool IsVideoCardChecked(int g) const;            // 격자 칸 (합쳐 보기 카드면 첫 파트 기준)
	bool IsItemChecked(int idx) const;
	int  CheckedItemCount() const;                   // 체크한 영상 수 (지금 라이브러리에 있는 것만)
	void ToggleVideoCheck(int g);                    // 합쳐 보기 카드는 파트 모두
	afx_msg void OnVideoDeleteChecked();
	afx_msg void OnVideoUncheckAll();
	afx_msg void OnCheckAll();                       // 빈 곳 오른쪽 클릭 [전체 체크]: 지금 보이는 카드 모두 체크 (배우 / 제작사 · 태그 · 컬렉션 / 영상)
	int  CheckAllCount() const;                      // 그때 체크할 수 있는 카드 수 (0 = 메뉴 없음)

	// 스튜디오 / 태그 카드 (로고 · 카메라 아이콘 / 꼬리표 아이콘, 이름, ▶ 영상 수 · 배우 수(스튜디오만))
	int   m_scardW = 255, m_scardH = 230;
	int   m_scardLogoH = 143;
	// 제작사 탭 카드: 확대 단계마다 글자 · 여백도 카드 폭에 비례 (로고 16:9 · 카드 비율 · 글자 비율이 단계와 상관없이 같게)
	CFont m_scardNameFont, m_scardTextFont;
	int   m_scardPad = 4, m_scardLineB = 16, m_scardLine = 14;   // 카드 위쪽 로고 칸 높이 (제작사 카드는 카드 비율을 맞추느라 16:9 가 아닐 수 있음)
	std::map<CString, int> m_studioActorCounts;   // 스튜디오(소문자) → 출연 배우 수
	std::map<CString, int> m_studioLevel;         // 제작사(소문자) → 레벨 (가장 아래 제작사 = 0, 상위는 1 + 하위 중 가장 큰 레벨) - 카드 배경색
	void ComputeStudioLevels();
	COLORREF StudioLevelBack(const CString& studioName) const;   // 레벨별 카드 배경 (0 = 기본 카드색)
	std::map<CString, int> m_labelActorCounts;    // 레이블(소문자) → 출연 배우 수
	void DrawStudioCard(CDC* dc, int row, const CRect& card, bool selected, bool focused);
	std::vector<int> m_actorRows;          // 배우 격자 행 → m_lib.actors 인덱스
	std::map<CString, int> m_actorCounts;  // 배우 이름(소문자) → 출연작 수
	int     m_actorSel = -1;
	CString m_gotoActorName;   // 탭 전환 때 배우 탭에서 고를 배우 (GoToActor 만, 보통 탭 전환은 선택 해제)
	CString m_actorSelName;
	CString m_drill;                  // 영상을 보고 있는 배우/스튜디오/태그 (비어 있으면 배우 격자 / 분류 목록)
	int     m_catSelKind = -1;        // 분류 목록에서 선택한 행 (영상 필터와는 별개)
	CString m_catSelValue;
	CString m_savedSearch;

	// 데이터
	CVideoLibrary    m_lib;
	std::vector<int> m_view;          // 목록 행 → m_lib.items 인덱스
	// 분할 영상 합쳐서 보기 (설정 창, 레지스트리 Settings\MergeParts): 격자에 첫 번째 파트 카드 하나만, 이미지 위를 파트 수만큼 좌우로 나눠 고름
	bool m_mergeParts = false;
	bool m_detailLeft = false;                 // 상세 화면 위치: false = 오른쪽 (기본), true = 왼쪽 (설정 창, 레지스트리 Settings\DetailLeft)
	std::vector<int> m_gridRows;               // 격자 칸 → m_view 행 (카드에 그리는 첫 파트, 비어 있으면 칸 = 행)
	std::vector<int> m_rowGrid;                // m_view 행 → 격자 칸 (파트 행은 그 카드 칸)
	std::map<int, std::vector<int>> m_gridParts;   // 격자 칸 → 파트 행들 (번호 순, 2개 이상일 때만)
	int  GridRowOf(int g) const;               // 격자 칸 → 행 (합쳐 보기가 아니면 그대로)
	int  GridIdxOf(int row) const;             // 행 → 격자 칸
	void SelectViewRow(int row);               // m_view 행 선택 (격자 칸이 아니라 행 번호)
	void BuildMergedView(bool apply);          // m_view 를 분할 묶음끼리 붙여 정렬하고 격자 칸 표를 만듦 (apply = false: 표만 비움)
	int  PartZoneAt(int g, const CRect& card, CPoint pt) const;   // 합친 카드 이미지 위 파트 칸 (0 부터, 아니면 -1)
	std::map<CString, int> m_partTotal;   // 나눠진 영상 묶음(폴더 + 파일 이름의 마지막 '_' 왼쪽) → 파일 수 (카드의 (1/3) 표시)
	static bool SplitPartName(const CString& path, CString& key, int& num);
	void RecyclePartImages(const std::vector<CString>& videoPaths, const CString& keepBase);   // 분할 파일별 이미지 → 휴지통   // "ABC-123_2.mp4" → 묶음 키 + 순번 2 (순번 없으면 false)
	int   m_sortColumn = 0;
	bool  m_sortAsc = true;
	int   m_actorSortCol = 0;        // 배우 격자 정렬: 0 = 이름, 1 = 작품 수 (즐겨찾기는 항상 먼저)
	bool  m_actorSortAsc = true;
	int   m_tagSortCol = 0;          // 태그 카드 정렬: 0 = 이름, 1 = 영상 수 (즐겨찾기는 항상 먼저)
	bool  m_tagSortAsc = true;
	int   m_colSortCol = 0;          // 컬렉션 카드 정렬: 0 = 이름, 1 = 영상 수 (즐겨찾기는 항상 먼저)
	bool  m_colSortAsc = true;
	int   m_studioSortCol = 0;       // 제작사 카드 정렬: 0 = 이름, 1 = 영상 수 (레이블은 상위 제작사 바로 뒤, 같은 기준)
	bool  m_studioSortAsc = true;
	int   m_sortComboKind = -1;      // 정렬 콤보 항목: 0 = 영상, 1 = 배우, 2 = 태그 (탭이 바뀌면 다시 채움)
	int   m_curItem = -1;             // 상세 정보에 표시 중인 항목
	bool  m_detailsDirty = false;
	bool  m_loadingDetails = false;
	bool  m_layoutReady = false;
	bool  m_allowLabelEdit = false;

	// 보기 모드 (상단 라디오 버튼)
	enum ViewMode { MODE_VIDEO = 0, MODE_ACTOR, MODE_STUDIO, MODE_TAG, MODE_COLLECTION, MODE_COUNT };
	enum CatKind  { CAT_ALL = 0, CAT_NONE, CAT_VALUE, CAT_LABEL };   // CAT_LABEL: 제작사 탭의 레이블 항목 (value = 레이블 이름)
	// (CatRow 는 위쪽 체크 항목 선언에서 쓰므로 앞으로 옮김)
	int     m_mode = MODE_VIDEO;
	int     m_catKind = CAT_ALL;      // 선택된 분류
	CString m_catValue;
	std::vector<CatRow> m_catRows;   // m_listCat 행 → 분류
	bool    m_catSortByCount = false;
	bool    m_catsDirty = false;
	bool    m_loadingCats = false;

	// 격자(썸네일) 보기
	bool    m_gridView = false;
	int     m_thumbW = 160;
	int     m_thumbH = 120;
	CImageList m_thumbs;                 // 0번 = "이미지 없음"
	std::map<CString, int> m_thumbIndex; // 동영상 경로(소문자) → 이미지 목록 인덱스

	// 내부 기능
	int  DX(int dlu);
	int  DY(int dlu);
	void MoveCtrl(UINT id, int x, int y, int w, int h);   // 위치 · 크기가 바뀐 컨트롤만 옮기고 다시 그릴 영역에 모음
	std::map<UINT, CRect> m_ctrlRects;     // MoveCtrl 로 마지막에 정한 위치 (같으면 건너뜀)
	std::vector<CRect> m_layoutDirty;      // 옮긴 컨트롤의 예전 · 새 영역 (부모 클라이언트 좌표)
	void FlushLayoutRedraw();              // 모은 영역만 다시 그림 (전체를 지우고 다시 그리면 버튼이 깜빡임)
	void LayoutControls(int cx, int cy);
	int  LeftPaneWidth(int cx);      // 왼쪽 격자 영역 폭 (오른쪽 상세 폭은 고정)

	void ApplyFilter();
	void SortView();
	void UpdateSortArrows();
	void SelectPath(const CString& path);
	void BeginLibraryChange();
	void UpdateStatus();

	int  GetSelectedRow() const;
	int  GetSelectedItem() const;
	bool IsItemOffline(int idx) const;   // 연결 안 된 이동식 디스크의 영상 (수정 불가)

	void ShowDetails(int idx);
	void CommitDetails();

	void UpdatePreview(int idx);
	void OpenImageViewer();       // 상세 이미지(영상 / 배우) → 원본 이미지 보기 창
	void OpenImageViewerFile(const CString& path, const CString& caption);

	static std::vector<CString> GetCategoryValues(const VideoItem& v, int mode);
	void RebuildCategories();
	void MarkCategoriesDirty();
	void UpdateCategoryHeader();

	void SetGridView(bool grid);
	int  GetThumbIndex(int idx);
	int  AddThumbnail(CImage* src, LPCWSTR text);
	void ResetThumbnails();
	void HandleListKey(UINT vk);

	// 배우 보기
	bool IsActorGridMode() const { return m_mode == MODE_ACTOR && m_drill.IsEmpty(); }
	// 영상 상세 정보(편집 칸) 표시: 영상 탭 + 배우 탭에서 배우를 더블클릭해 들어간 출연작 화면
	bool ShowVideoDetail() const { return m_mode == MODE_VIDEO || ((m_mode == MODE_ACTOR || m_mode == MODE_STUDIO || m_mode == MODE_COLLECTION || m_mode == MODE_TAG) && !m_drill.IsEmpty()); }   // + 컬렉션 · 태그 영상 화면   // 영상 탭 + 배우 출연작 · 제작사(레이블) 영상 화면
	bool IsCategoryListMode() const { return (m_mode == MODE_STUDIO || m_mode == MODE_TAG || m_mode == MODE_COLLECTION) && m_drill.IsEmpty(); }
	bool TagLikeMode() const { return (m_mode == MODE_TAG || m_mode == MODE_COLLECTION) && m_drill.IsEmpty(); }   // 태그 · 컬렉션 목록만 (영상 화면은 영상 상세 표시)   // 오른쪽 상세 페이지 없이 격자 전체 폭 (태그 · 컬렉션 목록 - 컬렉션 영상 화면은 영상 상세 표시)
	bool IsCatCardMode() const { return IsCategoryListMode(); }   // 제작사 · 태그 카드 목록 (정렬 콤보 사용)
	int&  CatSortCol() { return (m_mode == MODE_STUDIO) ? m_studioSortCol : (m_mode == MODE_COLLECTION ? m_colSortCol : m_tagSortCol); }
	bool& CatSortAsc() { return (m_mode == MODE_STUDIO) ? m_studioSortAsc : (m_mode == MODE_COLLECTION ? m_colSortAsc : m_tagSortAsc); }
	LPCWSTR CatSortKey() const { return (m_mode == MODE_STUDIO) ? L"StudioSort" : (m_mode == MODE_COLLECTION ? L"CollectionSort" : L"TagSort"); }   // 레지스트리 이름 앞부분
	int  NamedKind() const { return m_mode == MODE_STUDIO ? LIST_STUDIO : LIST_TAG; }
	// 스튜디오/태그는 항상 격자 표시
	bool CatGridActive() const { return true; }
	void DrillIntoCategory(int row);
	void ShowNamedInfo(int row);
	void HandleCatKey(UINT vk);
	int  GetNamedThumbIndex(int kind, const CString& name);
	afx_msg void OnLvnCatKeyDown(NMHDR* pNMHDR, LRESULT* pResult);
	void UpdateLeftPane();
	void RebuildActorGrid();
	void SelectActorRow(int row);
	void ShowActorInfo(int actorIdx);
	void DrillIntoActor(int row);
	void BackToList();
	int  GetActorThumbIndex(int actorIdx);
	int  SelectedActorIndex() const;
	afx_msg void OnBnClickedActorBack();
	afx_msg void OnActorShowVideos();
	afx_msg void OnActorEdit();
	void RenameRow(int row, CString newName);

	// IVideoGridOwner
	int  GridGetCount() override;
	int  GridGetSel() override;
	void GridSetSel(int row) override;
	void GridGetItem(int row, int& image, CString& name, CString& release, CString& memo) override;
	void GridActivate(int row) override;
	void GridKey(UINT vk) override;
	bool GridDrawCard(CDC* dc, int row, const CRect& card, bool selected, bool focused) override;   // 영상 카드
	int  GridHoverZone(int row, const CRect& card, CPoint pt) override;   // 1 = 영상 카드 이미지 위 (이미지만 표시)
	bool GridDblClick(int row, const CRect& card, CPoint pt) override;    // 스크린샷 추출 진행 표시 더블클릭 → 멈춤
	bool GridClick(int row, const CRect& card, CPoint pt) override;     // 합친 카드: 이미지 위 파트 칸 → 그 파트 선택
	int  GridHitPart(int row, const CRect& card, CPoint pt) override;   // 합친 카드: 이미지 위 파트 칸 번호 (1 부터)
	void DrawShotBadge(CDC* dc, int row, const CRect& card);   // 스크린샷 추출 "(3/15)" / "(대기 2)" (카드 왼쪽 아래, 격자 · 연도별)
	void InvalidateShotCards();                                // 진행 표시가 바뀜: 격자 + 연도별 보기 다시 그림
	bool ShotBadgeRect(int row, const CRect& card, CRect& out, CString* text = nullptr, int* queuePos = nullptr);   // 영상 카드 왼쪽 아래 "(3/15)" / "(대기 2)" 영역 (추출 중 · 대기 중인 영상만, queuePos 0 = 추출 중)
	CString GridTipText(int row, const CRect& card, CPoint pt) override;   // 영상 카드 제목 3줄 칸: 잘렸으면 제목 전체
	// 영상 카드 덧그림(로고 · 별점 리본 · 해상도 / 재생 시간) 페이드: 행 → 지금 불투명도 (0 ~ 255, 바뀌는 중인 카드만)
	std::map<int, int> m_overlayFade;
	UINT_PTR m_fadeTimer = 0;
	static const UINT_PTR kListMediaTimer = 0x4C4D;   // [리스트] 재생 시간 · 해상도를 받으면 모아서 한 번만 다시 그림
	bool m_listMediaTimer = false;
	static const UINT_PTR kOverlayFadeTimer = 0x4F46;
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	static CString FindImageFor(const CString& videoPath);

	// 영상 카드 (가로 이미지 / 제목 / 발매일(또는 ● 임시) / 메모 3줄 / 태그 수 · 배우 수)
	int   m_vcardW = 255, m_vcardImgH = 143, m_vcardH = 330;
	std::map<CString, std::unique_ptr<CBitmap>> m_videoCovers;   // 카드용 이미지 (크기에 맞게 잘라 둔 것)
	std::map<CString, UINT64> m_coverUse;                        // 마지막으로 쓴 순번 (많아지면 오래 안 쓴 것부터 지움)
	UINT64 m_coverTick = 0;
	CBitmap* GetVideoCover(int itemIdx, int w, int h, bool* loading = nullptr, bool prefetch = false);   // 없으면 백그라운드 읽기 요청 (*loading = 읽는 중, prefetch = 미리 읽기 - 나중 순서)
	void GridPrefetch(int first, int last) override;
	void GridScrolled() override;   // 스크롤: 미리보기 팝업이 카드를 따라감
	// 스튜디오 로고 (영상 카드 오른쪽 위에 반투명으로), 이미지 경로(소문자) → GDI+ 이미지
	std::map<CString, std::unique_ptr<Gdiplus::Bitmap>> m_studioLogos;
	std::map<CString, std::unique_ptr<Gdiplus::Bitmap>> m_logoMarks;   // 영상 카드 오른쪽 위 로고: 카드 크기로 줄이고 75% 불투명을 미리 적용한 사본 (경로|폭x높이)
	std::map<CString, std::vector<CString>> m_titleLines;              // 영상 카드 제목 3줄 줄바꿈 결과 (제목|폭|줄높이) - 스크롤 때마다 다시 재지 않게
	Gdiplus::Bitmap* GetStudioLogo(const CString& path);
	void DrawStudioMark(CDC* dc, const CRect& img, const CString& studio, const CString& label = CString());   // 레이블이 있으면 레이블 이미지
	void DrawVideoCard(CDC* dc, int row, const CRect& card, bool selected, bool focused);
	void DrawPartZones(CDC* dc, int row, const CRect& rc, const CRect& img, int radius, bool show);   // 분할 영상 합쳐 보기 카드: img 위에 파트 칸 · 번호 (show = 마우스가 이미지 위)

	// 메시지 처리기
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg BOOL OnDeviceChange(UINT nEventType, DWORD_PTR dwData);   // 이동식 디스크 연결 / 분리 → 연결 상태 · 드라이브 문자 다시 확인
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg void OnClose();
	void SaveWindowPlacement();      // 창 위치/크기/최대화 상태 저장 (다음 실행 때 복원)
	void RestoreWindowPlacement();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDis);
	// 스튜디오 상세 이름 줄: "스튜디오: " + 이름(굵게) + "  (서브이름)"(회색) - 이름 칸을 그동안만 직접 그림
	bool    m_nameRich = false;
	CString m_nameRichPrefix, m_nameRichName, m_nameRichSub;
	CFont   m_nameBoldFont;
	void    SetNameRich(bool on);
	void ApplyColors();
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);

	afx_msg void OnBnClickedAddFolder();
	afx_msg void OnBnClickedRemoveFolder();
	bool AddFolderInteractive();     // 폴더 선택 → (이동식 디스크?) → 등록 · 스캔 (추가했으면 true)
	bool RemoveFolderAt(int idx);    // 확인 후 등록 폴더 제거 (제거했으면 true)
	afx_msg void OnBnClickedRefresh();
	afx_msg void OnBnClickedSave();
	afx_msg void OnBnClickedRename();
	afx_msg void OnBnClickedDelete();
	afx_msg void OnBnClickedExplorer();
	afx_msg void OnBnClickedOpenDb();
	afx_msg void OnBnClickedOpenDefault();
	afx_msg void OnBnClickedActors();
	afx_msg void OnBnClickedSettings();
	void DeleteDb(int mask);
	CString m_pendingImage;   // 영상 상세에서 고른 새 이미지 (저장하면 영상 파일 옆에 같은 이름으로 복사)
	afx_msg void OnBnClickedChangeImage();
	afx_msg void OnDropFiles(HDROP hDropInfo);   // 영상 상세의 이미지 영역에 이미지 파일을 끌어다 놓으면 새 이미지로 지정
	bool SetPendingVideoImage(const CString& path, const CString& label);   // 새 이미지 미리보기 (저장하면 적용)
	afx_msg void OnBnClickedSearchImage();   // 영상 이미지 인터넷 검색 (배우 사진 검색과 같은 창)
	bool ApplyVideoImage(const CString& videoPath, const CString& src);   // 이미지를 "영상이름.확장자" 로 복사
	int  m_startupRelinked = 0;   // 시작할 때 다시 연결한 영상 수 (상태 줄 표시)   // 설정 창의 DB 삭제 (CSettingsDlg::DbMask)
	afx_msg void OnBnClickedPickActors();
	afx_msg void OnNmDblclkCategory(NMHDR* pNMHDR, LRESULT* pResult);
	void OpenActorManager(const CString& selectName);
	void OpenListManager(int kind, const CString& selectName);   // 스튜디오/태그
	void PickNamed(int kind);
	afx_msg void OnManageActors();
	afx_msg void OnManageStudios();
	afx_msg void OnManageTags();
	afx_msg void OnManageFiles();    // [목록 관리 ▾] → 파일 관리 (등록 폴더 트리 · 배우\[발매일]품번\품번.mp4 규칙 검사 · 복구)
	afx_msg void OnManageSeries();   // [목록 관리 ▾] → 품번 관리 (제작사 / 레이블의 시리즈 표)
	afx_msg void OnBnClickedPickStudio();
	afx_msg void OnBnClickedPickTags();
	afx_msg void OnBnClickedPickVAliases();

	afx_msg void OnEnChangeSearch();
	afx_msg void OnCbnSelchangeFilter();
	afx_msg void OnCbnSelchangeSort();
	afx_msg void OnBnClickedSortDir();
	void UpdateSortUI();
	void SetSortDirButton(bool asc);   // 정렬 방향 버튼 아이콘 + 툴팁
	afx_msg void OnDetailsChanged();
	afx_msg void OnStnClickedRatingLabel();   // [별점] 라벨 클릭 → 0점
	afx_msg void OnDtnReleaseChanged(NMHDR* pNMHDR, LRESULT* pResult);

	afx_msg void OnLvnGetDispInfo(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnLvnItemChanged(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnLvnColumnClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnLvnKeyDown(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnLvnBeginLabelEdit(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnLvnEndLabelEdit(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnNmDblclkList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnModeChanged(UINT nID);
	afx_msg void OnViewStyleChanged(UINT nID);
	afx_msg void OnLvnCatItemChanged(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnLvnCatColumnClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg LRESULT OnRebuildCategories(WPARAM wParam, LPARAM lParam);
	// 영상 카드 이미지 오른쪽 아래 해상도 · 재생 시간: Windows 속성을 백그라운드 스레드에서 읽어 경로별로 보관
	struct MediaQueue
	{
		std::mutex mx;
		std::condition_variable cv;
		std::deque<CString> q;
		HWND hwnd = nullptr;
		bool stop = false;   // 창을 닫을 때 스레드 끝내기
	};
	std::shared_ptr<MediaQueue> m_mediaQ;                        // 읽을 경로 대기열 (스레드와 공유)
	std::thread m_mediaThread;                                   // 읽기 스레드 (창을 닫을 때 끝날 때까지 기다림 - 메모리 누수 방지)
	void StopCardMediaThread();
	void ForgetFailedMediaInfo();   // 읽지 못한 해상도 · 재생 시간 기억을 지움 (이동식 디스크를 꽂으면 다시 읽음)
	afx_msg void OnDestroy();
	std::map<CString, CMediaInfoLabel::Info> m_cardMedia;        // 경로(소문자) → 해상도 · 재생 시간
	bool m_showCardMedia = true;                                 // 설정: 영상 카드에 해상도 · 재생 시간 표시 (레지스트리 Settings\CardMediaInfo)
	std::set<CString> m_cardMediaRequested;                      // 읽기를 요청한 경로 (다시 요청하지 않음)
	const CMediaInfoLabel::Info* CardMediaInfo(const CString& path);   // 있으면 정보, 없으면 백그라운드 읽기 요청 후 nullptr
	afx_msg LRESULT OnCardMediaReady(WPARAM wParam, LPARAM lParam);
	// 영상 카드 이미지: 백그라운드 스레드에서 이미지 파일을 찾아 읽고 카드 크기로 줄여 둠 (읽는 동안 카드에 "불러오는 중" 표시)
	struct CoverJob { CString key; CString videoPath; int w = 0; int h = 0; UINT gen = 0; int item = -1; CString cacheFile; bool portrait = false; };   // portrait: 세로 카드용 자르기   // cacheFile: 이동식 디스크 영상의 커버 캐시
	struct CoverQueue
	{
		std::mutex mx;
		std::condition_variable cv;
		std::deque<CoverJob> q;   // 뒤에서 꺼냄 (최근에 그린 = 지금 보이는 카드 먼저)
		HWND hwnd = nullptr;
		bool stop = false;
	};
	std::shared_ptr<CoverQueue> m_coverQ;
	std::vector<std::thread> m_coverThreads;
	std::set<CString> m_coverPending;                            // 읽는 중인 카드 이미지 키
	UINT m_coverGen = 1;                                         // 캐시를 비울 때마다 증가 (그 전 요청의 결과는 버림)
	void ClearVideoCovers();                                     // 카드 이미지 캐시 · 대기열 비우기
	void StopCoverThreads();
	afx_msg LRESULT OnCoverReady(WPARAM wParam, LPARAM lParam);
	// 영상 스크린샷 추출 (오른쪽 클릭 [스크린샷 추출...]) - 백그라운드 스레드 하나, 진행은 창 제목에
	std::thread       m_shotThread;
	std::atomic<bool> m_shotCancel{ false };
	bool              m_shotBusy = false;
	CString           m_shotTitleBase;   // 추출 전 창 제목 (끝나면 되돌림)
	CString           m_shotVideoPath;   // 추출 중인 영상 (그 영상 카드에 진행 표시)
	int               m_shotDone = 0, m_shotTotal = 0;
	// 대기열: 추출 중에 더 넣은 영상 (넣은 순서대로 하나씩, 프로그램을 닫으면 버림)
	struct ShotJob { CString path; int count = 0; };
	std::deque<ShotJob> m_shotQueue;
	void StartNextShot();          // 대기열 맨 앞 작업 시작 (비었으면 끝 - 창 제목 되돌림)
	void UpdateShotTitle();        // 창 제목: "스크린샷 추출 중 (3 / 15) · 대기 2"
	int  ShotQueuePos(const CString& path) const;   // 대기열 순번 (1부터, 없으면 0)
	afx_msg void OnVideoScreenshot();
	// 미리보기 영상 (오른쪽 클릭 [미리보기 만들기] - ffmpeg, 백그라운드 스레드 하나 · 대기열, 진행은 아래 상태 줄)
	struct PreviewJob { CString path; CString out; };
	std::deque<PreviewJob> m_prevQueue;
	std::thread       m_prevThread;
	std::atomic<bool> m_prevCancel{ false };
	bool              m_prevBusy = false;
	CString           m_prevBusyPath;      // 만드는 중인 영상 (그 영상은 재생하지 않음)
	CString           m_ffmpegPath;
	int               m_prevDone = 0, m_prevTotal = 0, m_prevOk = 0;
	CString           m_prevFirstError;    // 실패한 첫 영상과 이유 (모두 끝나면 한 번 알림)
	CString PreviewFileOf(int idx) const;   // 영상 항목 → 미리보기 파일 경로 (없어도 경로는 돌려줌)
	void QueuePreviews(const std::vector<int>& items);
	void MakeAllPreviews(CWnd* parent, bool regen = false);   // 설정 [미리보기 일괄 만들기]: 미리보기가 없는 영상만 (확인 후), regen = [미리보기 재생성]: 이미 있는 것도 다시
	void DeleteAllPreviews(CWnd* parent); // 설정 [미리보기 모두 삭제]: previews 폴더의 미리보기 파일 모두 지움 (확인 후, 만드는 중이면 멈춤)   // 설정 [미리보기 일괄 만들기]: 미리보기가 없는 영상 모두 대기열에 (확인 후)
	void StartNextPreview();
	void UpdatePreviewStatus();
	afx_msg void OnVideoMakePreview();
	afx_msg void OnVideoMakePreviewChecked();
	afx_msg LRESULT OnPreviewDone(WPARAM wParam, LPARAM lParam);
	// 미리보기 재생: 영상 격자 카드 이미지에 마우스를 올리면 바로 카드 이미지 자리에 소리 없이 반복 재생 (격자의 자식 창 - [월 모드] 처럼 화면에 직접, 별도 팝업 창 없음)
	//  분할 영상 파트를 바꿀 때는 다음 파트를 뒤에서 열어 두고(m_pNextPlayer, 숨김) 준비되면 바꿔 끼움 → 꺼졌다 켜지지 않음
	CPreviewPlayer  m_previewPlayers[2];
	CPreviewPlayer* m_pPrevPlayer = &m_previewPlayers[0];   // 지금 보이는 재생 창
	CPreviewPlayer* m_pNextPlayer = &m_previewPlayers[1];   // 바꿀 파트 (여는 중)
	int  m_nextHoverItem = -1;                              // m_pNextPlayer 로 여는 파트 항목 (-1: 없음)
	void StartNextPart(int item);                           // 다음 파트를 지금 재생 창 뒤에서 열기 (미리보기가 없으면 false 처럼 지금 창을 닫음)
	void SwapNextPart();                                    // 다음 파트가 준비됐으면 바꿔 끼움
	static const UINT_PTR kPreviewHoverTimer = 0x5056;   // 마우스 위치 살피기 (0.06초마다)
	int       m_hoverItem = -1;          // 마우스가 올라가 있는 영상 카드의 항목 (-1: 없음)
	ULONGLONG m_hoverTick = 0;           // 그 카드에 올라간 시각
	CRect     m_hoverAnchor;             // 팝업을 띄운 카드 이미지 영역 (화면 좌표 - 스크롤 등으로 카드가 움직이면 닫음)
	void StopPreviewPlayer();
	void CheckPreviewHover();
	bool CardAnchorOfItem(int item, CRect& anchorScreen);   // 지금 페이지 격자에서 그 영상 카드의 이미지 영역 (화면 좌표, 없으면 false)
	void FollowPreviewPopup();                              // 미리보기 팝업을 카드 자리로 (카드가 사라졌거나 격자 밖이면 닫음)
	// 미리보기를 만드는 동안 설정 버튼 톱니바퀴가 돌아감 (모두 끝나면 멈춤)
	static const UINT_PTR kGearSpinTimer = 0x4753;
	double m_gearAngle = 0.0;
	bool   m_gearSpinning = false;
	void SetGearSpin(bool on);
	bool   m_prevStopped = false;   // [만들기 중단]: 대기열을 비우고 지금 것을 멈춤 (끝 알림이 오면 정리)
	void StopPreviews();
	int  PreviewItemAt(CPoint screen, CRect& anchorScreen) const;   // 마우스 아래 영상 카드의 항목 (분할 영상은 그 파트 칸의 파트, -1: 없음)
	afx_msg void OnVideoRefreshItem();   // 영상 카드 오른쪽 클릭 → 이 영상만 새로고침 (이미지 · 해상도/재생 시간 · 파일 크기 · 스크린샷 다시 읽기)
	afx_msg LRESULT OnShotProgress(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnShotDone(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()
};
