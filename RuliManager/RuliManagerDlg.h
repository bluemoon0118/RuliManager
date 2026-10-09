#pragma once

#include <memory>
#include "VideoLibrary.h"
#include "ImagePreview.h"
#include "VideoGrid.h"
#include "DarkControls.h"
#include "SuggestEdit.h"
#include "TagChipCtrl.h"
#include "ZoomSlider.h"
#include "StarRatingCtrl.h"
#include "ActorDetailPanel.h"
#include "ActorStrip.h"
#include "MediaInfoLabel.h"
#include "DropCounter.h"

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
	static const COLORREF kBackColor = RGB(0x20, 0x2B, 0x33);   // #202B33
	static const COLORREF kTextColor = RGB(232, 238, 242);
	static const COLORREF kEditColor = RGB(0x1B, 0x25, 0x2C);   // #1B252C (에디트 배경)
	static const COLORREF kDisabledText = RGB(120, 135, 148);
	static const COLORREF kComboColor = RGB(0x39, 0x4B, 0x59);  // #394B59 (콤보박스 배경)
	static const COLORREF kDateColor  = RGB(0x1B, 0x25, 0x2C);  // #1B252C (날짜 컨트롤 배경)
	static const COLORREF kButtonColor = RGB(0x13, 0x7C, 0xBD); // #137CBD (버튼 배경)
	static const COLORREF kSaveColor = RGB(0x0F, 0x99, 0x60);   // #0F9960 (저장 버튼 배경)
	static const COLORREF kDangerColor = RGB(0xDB, 0x37, 0x37); // #DB3737 (삭제/제거 버튼 배경)
	CDarkButton m_darkButtons[20];
	CDarkButton m_btnSettings;      // 오른쪽 위 톱니바퀴 (설정 창)
	CDarkButton m_modeButtons[4];   // 영상 / 배우 / 스튜디오 / 태그 토글 버튼
	void UpdateModeButtons();
	static const COLORREF kScrollColor = RGB(0x6C, 0x74, 0x78); // #6C7478 (스크롤바 배경)
	static const COLORREF kThumbBackColor = RGB(0x2A, 0x35, 0x3D); // #2A353D (격자 이미지 배경)
	CDarkScrollBar m_scrollBars[8];
	void CreateScrollBars();
	CListCtrl  m_list;
	CEdit      m_editSearch;
	CDarkCombo m_comboFilter;
	CStarRatingCtrl m_starRating;   // 별점 (마우스 오버 미리 보기, 클릭 확정)
	CActorDetailPanel m_actorPanel;  // 배우 탭 오른쪽 아래: 큰 이름 + ♥ / 별점 / 성별·나이·국적·키 …
	void ToggleActorFavorite(int actorIdx);
	afx_msg void OnCatDelete();
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
	afx_msg void OnActorTextInfo();   // 배우 카드 오른쪽 클릭 → 텍스트로 정보 입력   // 배우 탭 우클릭 → 배우 삭제 (Del)   // 태그 탭 우클릭 → 삭제
	void ToggleTagFavorite(const CString& tag);   // 태그 즐겨찾기 (태그 목록에 없으면 추가)
	bool IsTagFavorite(const CString& tag) const;
	CDropCounter m_dropCounter;      // 영상 상세 정보: 별점 오른쪽 물방울 카운트
	CMediaInfoLabel m_mediaInfo;     // 영상 상세 정보: [이미지 변경] 위 "fps: 29.97 | 1080p"
	CActorStrip m_actorStrip;        // 영상 상세 정보 메모 아래: 출연 배우 카드 (가로 한 줄)
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
	void OpenStudioCardTarget();   // focusRow: 바뀐 줄 입력 칸으로 포커스
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
	bool m_namedActors = false;      // 제작사 탭 상세: 배우 카드 띠를 (하위 레이블 없는) 제작사 / 레이블의 출연 배우로 사용 중
	void UpdateNamedActorStrip(int kind, const CString& name);   // 제작사 탭 상세 하단 배우 카드 (kind: 0 = 숨김, 1 = 제작사, 2 = 레이블)
	void RefreshActorStrip();                                        // 숨긴 배우 칸 값으로 카드 띠 갱신
	void DrawStripCard(CDC* dc, int i, const CRect& rc, bool hot);   // 카드 띠의 배우 카드 하나
	void OpenActorFromStrip(int i);                                  // 더블클릭: 배우 탭에서 그 배우 선택
	CRect StripHeartRect(const CRect& card);                          // 카드 띠 카드의 즐겨찾기 하트 자리
	afx_msg void OnBnClickedApplyDb();
	afx_msg void OnBnClickedRescan();
	afx_msg void OnBnClickedExportTxt(); // [정보 txt 생성]: 영상 폴더 / 배우 폴더에 정보 txt    // [재 스캔]: 새로고침 + 저장된 영상에도 폴더 구조(배우·스튜디오·발매일) 다시 적용   // [DB 반영]: 작업 DB → library.vmdb
	void UpdateApplyDbButton();          // 반영 안 한 변경이 있을 때만 [DB 반영] 활성
	void UpdateActorPanel(const ActorInfo* a, int count);   // 배우 상세 패널 내용 (높이가 바뀌면 다시 배치)
	int             m_actorInfoIdx = -1;   // 오른쪽 패널에 표시 중인 배우
	CDarkCombo m_comboSort;        // 영상 정렬 기준 (격자에는 열 머리글이 없음)
	CEdit      m_editCode;    // 품번 (제목 위)
	CEdit      m_editTitle;
	// 배우 / 별칭 / 스튜디오 / 태그: 입력하면 DB에서 실시간 검색해 목록으로 선택 (자동 완성)
	CSuggestEdit m_editActors;
	CSuggestEdit m_editVAliases;   // 이 작품에서 쓴 배우 별칭 (배우 칸 아래)
	CSuggestEdit m_editStudio;
	CDarkDateTime m_dateRelease;  // 발매일 (체크 해제 = 없음)
	CListCtrl  m_listCat;         // 배우/스튜디오/태그 목록
	CSuggestEdit m_editTags;        // 태그 값 보관용 (숨김) - 화면에는 m_tagChips 로 표시
	CTagChipCtrl m_tagChips;        // 태그 칩 입력 ([태그 ×] ... × ⌄)
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
		bool GridClick(int row, const CRect& card, CPoint pt) override;   // 하트 → 즐겨찾기 전환
		int  GridHitPart(int row, const CRect& card, CPoint pt) override; // 1 = 하트 위
	};
	ActorGridOwner m_actorOwner;
	CRect ActorHeartRect(const CRect& card) const;   // 배우 카드 오른쪽 위 즐겨찾기 하트 자리

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
	int  m_zoom[4] = { 0, 0, 0, 0 };   // 영상 / 배우 / 스튜디오 / 태그
	double ZoomFactor() const;
	void OnZoomChanged(int pos);
	void DrawActorCard(CDC* dc, int row, const CRect& card, bool selected, bool focused);
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
	std::map<CString, int> m_partTotal;   // 나눠진 영상 묶음(폴더 + 파일 이름의 마지막 '_' 왼쪽) → 파일 수 (카드의 (1/3) 표시)
	static bool SplitPartName(const CString& path, CString& key, int& num);
	void RecyclePartImages(const std::vector<CString>& videoPaths, const CString& keepBase);   // 분할 파일별 이미지 → 휴지통   // "ABC-123_2.mp4" → 묶음 키 + 순번 2 (순번 없으면 false)
	int   m_sortColumn = 0;
	bool  m_sortAsc = true;
	int   m_curItem = -1;             // 상세 정보에 표시 중인 항목
	bool  m_detailsDirty = false;
	bool  m_loadingDetails = false;
	bool  m_layoutReady = false;
	bool  m_allowLabelEdit = false;

	// 보기 모드 (상단 라디오 버튼)
	enum ViewMode { MODE_VIDEO = 0, MODE_ACTOR, MODE_STUDIO, MODE_TAG };
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
	void MoveCtrl(UINT id, int x, int y, int w, int h);
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

	void ShowDetails(int idx);
	void CommitDetails();

	void UpdatePreview(int idx);

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
	bool ShowVideoDetail() const { return m_mode == MODE_VIDEO || (m_mode == MODE_ACTOR && !m_drill.IsEmpty()); }
	bool IsCategoryListMode() const { return (m_mode == MODE_STUDIO || m_mode == MODE_TAG) && m_drill.IsEmpty(); }
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
	static CString FindImageFor(const CString& videoPath);

	// 영상 카드 (가로 이미지 / 제목 / 발매일(또는 ● 임시) / 메모 3줄 / 태그 수 · 배우 수)
	int   m_vcardW = 255, m_vcardImgH = 143, m_vcardH = 330;
	std::map<CString, std::unique_ptr<CBitmap>> m_videoCovers;   // 카드용 이미지 (크기에 맞게 잘라 둔 것)
	CBitmap* GetVideoCover(int itemIdx, int w, int h);
	// 스튜디오 로고 (영상 카드 오른쪽 위에 반투명으로), 이미지 경로(소문자) → GDI+ 이미지
	std::map<CString, std::unique_ptr<Gdiplus::Bitmap>> m_studioLogos;
	Gdiplus::Bitmap* GetStudioLogo(const CString& path);
	void DrawStudioMark(CDC* dc, const CRect& img, const CString& studio, const CString& label = CString());   // 레이블이 있으면 레이블 이미지
	void DrawVideoCard(CDC* dc, int row, const CRect& card, bool selected, bool focused);

	// 메시지 처리기
	afx_msg void OnSize(UINT nType, int cx, int cy);
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

	DECLARE_MESSAGE_MAP()
};
