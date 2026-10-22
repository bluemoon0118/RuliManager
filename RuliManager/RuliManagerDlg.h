#pragma once

#include <memory>
#include <set>
#include "VideoLibrary.h"
#include "ImagePreview.h"
#include "ImageViewerWnd.h"
#include "VideoGrid.h"
#include "DarkControls.h"
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
	bool GenreBarActive() const { const int s = GenreSlot(); return s >= 0 && m_genreShow[s] && !YearViewActive(); }   // 연도별 보기에서는 쓰지 않음
	void SyncGenreKey();                  // 대상이 바뀌었으면 그 화면의 고른 태그를 풂
	void SetGenreShow(bool show);
	void UpdateGenreBar(const std::vector<int>& before);
	void RelayoutToolbarHeader();   // 장르 거르기 전 영상(m_view) 기준 태그별 건수 → 칩 줄
	afx_msg void OnBnClickedGenreHide();
	// 영상 상세: 품번 칸 오른쪽 [복사] (품번을 클립보드로)
	CDarkButton m_btnCopyCode;
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
	int  m_studioCardW = 0, m_studioCardH = 0;
	void DrawVideoStudioCard(CDC* dc, const CRect& rc, bool hot);
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
	int  EffPageSize() const { return StudioListView() ? 0 : m_pageSize; }   // 제작사 탭 [리스트]: 페이지 나누지 않음 (전체)
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
	CRect ActorCheckRect(const CRect& card) const;   // 배우 카드 왼쪽 위 체크 칸 자리
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
		bool GridClick(int row, const CRect& card, CPoint pt) override;   // 태그 카드 하트 → 즐겨찾기 전환
		int  GridHitPart(int row, const CRect& card, CPoint pt) override; // 1 = 태그 카드 하트 위
		CString GridTipText(int row, const CRect& card, CPoint pt) override;   // 컬렉션 카드: 제목 + 메모
	};
	CatGridOwner m_catOwner;

	// 스튜디오 / 태그 카드 (로고 · 카메라 아이콘 / 꼬리표 아이콘, 이름, ▶ 영상 수 · 배우 수(스튜디오만))
	int   m_scardW = 255, m_scardH = 230;
	std::map<CString, int> m_studioActorCounts;   // 스튜디오(소문자) → 출연 배우 수
	std::map<CString, int> m_labelActorCounts;    // 레이블(소문자) → 출연 배우 수
	void DrawStudioCard(CDC* dc, int row, const CRect& card, bool selected, bool focused);
	std::vector<int> m_actorRows;          // 배우 격자 행 → m_lib.actors 인덱스
	std::map<CString, int> m_actorCounts;  // 배우 이름(소문자) → 출연작 수
	int     m_actorSel = -1;
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
	struct CatRow { int kind; CString value; };
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
	// 스튜디오 로고 (영상 카드 오른쪽 위에 반투명으로), 이미지 경로(소문자) → GDI+ 이미지
	std::map<CString, std::unique_ptr<Gdiplus::Bitmap>> m_studioLogos;
	std::map<CString, std::unique_ptr<Gdiplus::Bitmap>> m_logoMarks;   // 영상 카드 오른쪽 위 로고: 카드 크기로 줄이고 75% 불투명을 미리 적용한 사본 (경로|폭x높이)
	std::map<CString, std::vector<CString>> m_titleLines;              // 영상 카드 제목 3줄 줄바꿈 결과 (제목|폭|줄높이) - 스크롤 때마다 다시 재지 않게
	Gdiplus::Bitmap* GetStudioLogo(const CString& path);
	void DrawStudioMark(CDC* dc, const CRect& img, const CString& studio, const CString& label = CString());   // 레이블이 있으면 레이블 이미지
	void DrawVideoCard(CDC* dc, int row, const CRect& card, bool selected, bool focused);

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
	afx_msg void OnManageSeries();   // [목록 관리 ▾] → 품번 관리 (제작사 / 레이블의 시리즈 표)
	afx_msg void OnBnClickedPickStudio();
	afx_msg void OnBnClickedPickTags();
	afx_msg void OnBnClickedPickVAliases();

	afx_msg void OnEnChangeSearch();
	afx_msg void OnCbnSelchangeFilter();
	afx_msg void OnCbnSelchangeSort();
	afx_msg void OnBnClickedSortDir();
	void UpdateSortUI();
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
	struct CoverJob { CString key; CString videoPath; int w = 0; int h = 0; UINT gen = 0; int item = -1; CString cacheFile; };   // cacheFile: 이동식 디스크 영상의 커버 캐시
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
	afx_msg void OnVideoRefreshItem();   // 영상 카드 오른쪽 클릭 → 이 영상만 새로고침 (이미지 · 해상도/재생 시간 · 파일 크기 · 스크린샷 다시 읽기)
	afx_msg LRESULT OnShotProgress(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnShotDone(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()
};
