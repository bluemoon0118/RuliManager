#pragma once

#include <functional>

// 동영상 한 개의 정보
struct VideoItem
{
	CString   path;          // 전체 경로
	CString   code;          // 품번 (예: SONE-479, 비어 있으면 파일 이름에서 자동으로 찾음)
	CString   title;         // 제목 (파일 이름과 별개, 비어 있으면 파일 이름 사용)
	ULONGLONG size = 0;      // 바이트
	ULONGLONG modified = 0;  // FILETIME (UTC) 64비트 값
	int       rating = 0;    // 0~5
	int       oCount = 0;    // 물방울 카운트 (상세 정보 별점 오른쪽, 클릭마다 +1)
	CString   actors;        // 쉼표로 구분된 배우
	CString   actorAliases;  // 이 작품에서 배우가 쓴 별칭 (쉼표 구분)
	CString   studio;        // 스튜디오 (1개)
	CString   label;         // 레이블 (스튜디오 하위, 1개 - 스튜디오의 레이블 목록에 자동 등록)
	CString   series;        // 시리즈 (레이블 하위, 1개 - 레이블의 시리즈 목록에 자동 등록)
	CString   release;       // 발매일 "YYYY-MM-DD" (없으면 빈 문자열)
	CString   tags;          // 쉼표로 구분된 태그
	CString   memo;          // 메모 (여러 줄)
	bool      pending = false; // 임시 항목: 스캔으로 찾았지만 아직 정보를 저장하지 않음 (pending.tsv 에 기록)

	CString FileName() const { return CString(::PathFindFileNameW(path)); }
};

// 배우 한 명의 정보 (동영상의 actors 에는 배우 이름이 쉼표로 저장됨)
struct ActorInfo
{
	CString name;      // 이름 (중복 불가)
	CString lastAlias; // 영상 배우 칩에서 마지막으로 고른 별칭 (새로 배우를 넣을 때 초기값)
	CString aliases;   // 별칭 (여러 개, 쉼표 구분) - 동영상의 배우 칸에 별칭으로 적혀 있어도 이 배우로 연결
	CString gender;    // 성별 "여성" / "남성" / "트랜스젠더 남성" / "트랜스젠더 여성" / "인터섹스" / "논바이너리" (비어 있으면 미지정)
	CString birth;     // 생년월일 "YYYY-MM-DD"
	CString nationality; // 국적
	CString height;    // 키 (cm, 숫자)
	CString debut;     // 데뷔일 "YYYY-MM-DD"
	CString retire;    // 은퇴일 "YYYY-MM-DD" (비어 있으면 활동 중/모름)
	int     rating = 0; // 별점 0~5 (0 = 없음)
	bool    favorite = false; // 즐겨찾기 (배우 카드 오른쪽 위 하트)
	CString bust;      // 치수: 가슴 (cm, 숫자)
	CString waist;     // 치수: 허리
	CString hip;       // 치수: 엉덩이
	CString cup;       // 컵 ("A" ~ "Q", 비어 있으면 미지정)
	CString photo;     // 사진 파일 경로
	CString memo;      // 메모
	CString urls;      // 링크 URL (여러 개, 줄바꿈 \n 으로 구분)
};

// 스튜디오 / 태그 한 개의 정보 (동영상에는 이름으로 저장됨)
struct NamedInfo
{
	CString name;    // 이름 (중복 불가)
	CString memo;    // 메모
	CString image;   // 이미지(로고 등) 파일 경로
	CString subName; // 서브이름 (스튜디오만, 여러 개 줄바꿈 \n 구분 - 쉼표는 이름의 일부, 보조 표기 예: S1 NO.1 STYLE → 에스원 / S1) - 이 이름으로 적혀 있어도 같은 스튜디오
	CString parent;  // 레이블만: 상위 제작사 이름 (labelInfos 항목)
	CString series;  // 제작사 · 레이블: 시리즈 (여러 개, 한 줄에 하나 "이름␟품번␟라벨␟설명" - ParseSeries / JoinSeries, 시리즈 하나는 제작사나 레이블 하나에만)
	bool    favorite = false;   // 즐겨찾기 (태그 카드 오른쪽 위 하트)
};

enum NamedListKind { LIST_STUDIO = 0, LIST_TAG = 1 };

// 시리즈 한 개 (제작사 · 레이블의 series 에 한 줄씩: 이름 ␟ 품번 접두어 ␟ 라벨 ␟ 설명)
struct SeriesInfo
{
	CString name;    // 시리즈 = 품번 접두어 (예: SONE, 쉼표로 여러 개도 가능) - 영상 품번이 이 접두어면 이 시리즈로 자동 지정
	CString code;    // (예전 형식의 품번 칸 - 읽을 때 name 으로 옮김, 쓰지 않음)
	CString label;   // 라벨 (별도 표기)
	CString desc;    // 설명
};

// 동영상 라이브러리 (목록 + 등록 폴더 + 배우 + 스튜디오 + 태그 + 저장/불러오기)
// 데이터 파일: %APPDATA%\VideoManager\library.tsv (UTF-8, 탭 구분)
class CVideoLibrary
{
public:
	std::vector<VideoItem> items;
	std::vector<CString>   folders;
	std::vector<ActorInfo> actors;
	std::vector<NamedInfo> studios;
	std::vector<NamedInfo> tagInfos;
	std::vector<NamedInfo> labelInfos;   // 레이블 (제작사 하위, parent = 상위 제작사 이름) - 이름 · 서브이름 · 이미지 · 메모

	bool Load();
	bool Save() const;               // DB 파일은 AES 로 암호화해서 저장 (DbCrypt.h)
	bool LoadedPlainText() const { return m_loadedPlainText; }   // 예전 평문 DB 를 읽었음 → 바로 저장하면 암호화됨
	bool DbBroken() const { return m_dbBroken; }
	// 작업 DB: 실행 중에는 library.work.vmdb 에 저장하고 [DB 반영] / 종료 때 library.vmdb 로 반영
	bool HasUnappliedChanges() const { return m_workDirty; }   // 작업 DB 에 반영 안 한 변경이 있음
	bool ApplyWorkDb();                                         // 작업 DB → library.vmdb (원자적 교체)
	std::function<void()> m_onSaved;                            // 작업 DB 에 저장할 때마다 (버튼 상태 갱신용)                 // 암호화된 DB 를 풀지 못함 (저장 안 함)

	// 폴더를 등록하고 하위 폴더까지 스캔합니다. 새로 추가된 개수를 반환.
	int  AddFolder(const CString& folder);
	// 등록 폴더를 제거하고, 다른 등록 폴더에 속하지 않는 항목을 목록에서 제거합니다.
	void RemoveFolder(size_t index);
	// 모든 등록 폴더를 다시 스캔합니다.
	void Refresh(int& added, int& removed, int* relinked = nullptr);
	// 경로가 바뀐(옮기거나 이름을 바꾼) 파일 다시 연결: 없어진 DB 항목을 스캔에서 새로 찾은 파일과 맞춰 경로만 고침
	//   같은 파일로 보는 기준: 크기가 같고 (파일 이름이 같거나 수정 시각이 같음)
	int  RelinkMoved(const std::vector<VideoItem>& scanned);
	int  RelinkOnStartup();   // 프로그램 시작 시: 없어진 항목이 있을 때만 폴더를 스캔해서 다시 연결 (추가/삭제는 안 함)

	int  FindByPath(const CString& path) const;

	// 배우
	int  FindActor(const CString& name) const;                 // 이름(대소문자 무시)으로 찾기
	// 동영상의 배우 표기(이름 또는 별칭)로 배우 찾기: 이름이 우선, 없으면 별칭
	int  FindActorByAnyName(const CString& name) const;
	int  FindActorByNamePart(const CString& name, int exclude = -1) const;
	int  MergeEmptyDuplicateActors();
	int  FindLabelLoose(const CString& label) const;       // 레이블 이름 · 서브이름으로 찾기 (공백 · 대소문자 · 기호 무시, labelInfos 인덱스)
	int  FindStudioByLabel(const CString& label, CString* labelName = nullptr) const;   // 레이블 이름으로 그 레이블을 가진 스튜디오 찾기 (공백 · 대소문자 · 기호 무시, 없으면 -1) / labelName: 등록된 표기
	void ResolveVideoLabel(VideoItem& v) const;            // 스튜디오 칸에 레이블 이름이 적혔으면 → 상위 스튜디오 + 레이블, 레이블만 있으면 상위 스튜디오 채움
	int  FindStudioLoose(const CString& name) const;   // 스튜디오 이름 · 서브이름으로 찾기 (그대로 → 공백 · 대소문자 · 기호 무시 순, 없으면 -1)
	static bool SameNameByLang(const CString& a, const CString& b);
	// 성별 목록 (저장 값, 순서 = 배우 관리 창 콤보 순서, 0번 = 미지정 "")
	static int     GenderCount();
	static CString GenderAt(int i);
	static CString NormalizeGender(const CString& text);   // "Female" / "Transgender Female" / "MTF" / "女性" … → 저장 값 (모르면 빈 문자열)
	static bool    IsFemaleLike(const CString& g);         // 여성 · 트랜스젠더 여성 (여성 실루엣 기본 이미지)
	static bool    IsMaleLike(const CString& g);           // 남성 · 트랜스젠더 남성 (남성 실루엣 기본 이미지)   // 두 이름의 한글 / 영어 / 일어 조각 중 같은 언어끼리 하나라도 같으면 true
	bool CleanupActorAliases();          // 배우 이름(또는 앞의 별칭)과 언어 단위로 같은 별칭은 뺌 (영상의 참여 별칭도 정리, 변경 시 true)   // 정보가 하나도 없는 배우가 다른 배우와 언어 단위 이름이 같으면 그 배우로 합침 (반환: 합친 수)   // 이름의 한글 / 영어 / 일어 조각(괄호 앞 · 괄호 안) 단위로 같은 배우 (없으면 -1)
	// 이름/별칭(소문자) → 배우 인덱스 (반복 조회용, 이름이 별칭보다 우선)
	std::map<CString, int> ActorNameIndex() const;
	// 동영상의 배우 표기 → 연결된 배우 이름(소문자). 목록에 없으면 표기 그대로(소문자)
	CString ActorKeyOf(const std::map<CString, int>& index, const CString& videoName) const;
	// 영상의 배우 칸에 별칭으로 적힌 배우를 본래 이름으로 바꾸고 그 별칭은 "참여 별칭"으로 옮김,
	// 배우 칸에 없는 배우의 참여 별칭은 정리 (변경되면 true)
	bool NormalizeVideoActors(VideoItem& v) const;
	bool NormalizeAllVideoActors();
	bool FillMissingActorPhotos();                               // 사진 없는 배우를 배우 폴더의 이미지로 채움 (변경 시 true)
	bool SyncActorsFromVideos();                                 // 동영상에만 있는 배우 이름을 배우 목록에 추가
	void RenameActorInVideos(const CString& oldName, const CString& newName);
	int  RemoveActorFromVideos(const CString& name);            // 반환: 영향 받은 동영상 수
	int  CountVideosWithActor(const CString& name) const;
	CString FindCodeOnDate(const CString& actorName, const CString& date) const;
	int     FindVideoOnDate(const CString& actorName, const CString& date) const;   // 위 영상의 인덱스 (없으면 -1)   // 그 배우가 나온 영상 중 발매일이 date 인 영상의 품번 (데뷔작 표시, 없으면 빈 문자열)

	// 스튜디오 / 태그 (kind = LIST_STUDIO / LIST_TAG)
	std::vector<NamedInfo>&       NamedList(int kind)       { return kind == LIST_STUDIO ? studios : tagInfos; }
	const std::vector<NamedInfo>& NamedList(int kind) const { return kind == LIST_STUDIO ? studios : tagInfos; }
	int  FindNamed(int kind, const CString& name) const;
	// 레이블 (제작사 하위)
	int  FindLabel(const CString& name) const;                   // 레이블 이름 · 서브이름으로 찾기 (대소문자 무시, 없으면 -1)
	std::vector<int> LabelsOf(const CString& studioName) const;   // 이 제작사의 레이블 (labelInfos 인덱스, 이름 순)
	std::vector<CString> LabelNamesOf(const CString& studioName) const;
	int  CountLabelVideos(const CString& labelName) const;       // 이 레이블이 지정된 영상 수
	// 시리즈 (레이블 하위, 레이블의 series 목록)
	int  FindLabelOfSeries(const CString& series, CString* seriesName = nullptr) const;   // 이 시리즈를 가진 레이블 (labelInfos 인덱스, 없으면 -1) / seriesName: 등록 표기
	static std::vector<SeriesInfo> ParseSeries(const CString& text);   // series 칸 → 시리즈 목록 (이름 없는 줄 · 같은 이름 중복은 뺌)
	static CString JoinSeries(const std::vector<SeriesInfo>& list);
	static std::vector<CString> SeriesNames(const CString& text);     // series 칸의 시리즈 이름만
	CString SeriesForCode(const CString& code) const;                  // 영상 품번의 접두어가 등록된 시리즈 (없으면 빈 문자열)
	bool    SeriesInfoForCode(const CString& code, SeriesInfo& out) const;   // 위와 같은 규칙으로 찾은 시리즈 정보 (라벨 · 설명 포함)
	int  FindStudioOfSeries(const CString& series, CString* seriesName = nullptr) const;  // 이 시리즈를 가진 제작사 (studios 인덱스, 없으면 -1)
	std::vector<CString> AllSeries() const;                      // 모든 레이블의 시리즈 + 영상에만 있는 시리즈 (이름 순, 중복 제거)
	void RenameLabelInVideos(const CString& oldName, const CString& newName);
	int  RemoveLabel(int labelIdx);                              // 레이블 삭제 (영상의 레이블도 비움) → 영향받은 영상 수
	void SetLabelParent(int labelIdx, const CString& parent);    // 상위 제작사 변경 (그 레이블 영상의 제작사도 바뀜)
	int  StudioToLabel(int studioIdx, const CString& parent);    // 제작사 → 레이블 (영상: 제작사 = 상위, 레이블 = 이 이름) → 새 레이블 인덱스
	int  LabelToStudio(int labelIdx);                            // 레이블 → 제작사 (영상: 제작사 = 이 이름, 레이블 비움) → 새 제작사 인덱스
	bool SyncNamedFromVideos();                                  // 동영상에만 있는 이름을 목록에 추가
	void RenameNamedInVideos(int kind, const CString& oldName, const CString& newName);
	int  RemoveNamedFromVideos(int kind, const CString& name);
	std::map<CString, int> CountNamed(int kind) const;          // 이름(소문자) → 영상 수
	static std::vector<CString> VideoNamedValues(const VideoItem& v, int kind);

	static std::vector<CString> SplitList(const CString& text); // 쉼표 구분 → 목록 (공백 제거, 괄호 안 쉼표는 이름의 일부)
	// 목록 구분 쉼표 찾기: 괄호 ( ) / （ ） 안의 쉼표는 구분자가 아님 → "나기 히카루(Hikaru Nagi, 凪ひかる)" 는 한 이름
	static int  FindListComma(const CString& text, int start);          // start 이후 첫 구분 쉼표 (없으면 -1)
	static int  ReverseFindListComma(const CString& text, int before);  // before 앞의 마지막 구분 쉼표 (없으면 -1)
	static void RemoveListCommas(CString& text);                       // 이름에서 구분 쉼표만 지움 (괄호 안 쉼표는 유지)
	static CString JoinList(const std::vector<CString>& list);
	// 생년월일("YYYY-MM-DD")로 오늘 기준 만 나이 계산 (알 수 없으면 -1)
	static int     CalcAge(const CString& birth);
	// 배우 요약 문구: "1990-05-01 (36세)" / "36세" 등
	static CString AgeText(const CString& birth);
	// 텍스트에서 [YYYY.MM.DD] / [YYYYMMDD] 를 찾아 "YYYY-MM-DD" 로 반환 (없으면 빈 문자열)
	static CString FindBracketDate(const CString& text);

	static CString GetDataDir();           // DB 위치 = 실행 파일 폴더 (쓰기 권한이 없으면 %APPDATA%\VideoManager)
	static CString GetAppDataDir();        // %APPDATA%\VideoManager (예전 DB 위치, 없으면 만듦)
	// DB 는 두 파일: library.vmdb = 정보(library.tsv) + 임시 목록(pending.tsv), images.vmdb = 이미지 복사본 전부 (각각 AES 암호화)
	//  - 실행 중 이미지는 캐시 폴더(%LOCALAPPDATA%\VideoManager\cache\images)에 풀어서 사용, 종료 시 지움
	static CString GetWorkDbPath();        // <실행 파일 폴더>\library.work.vmdb (실행 중 작업 DB)
	static bool    HasLeftoverWorkDb();    // 이전 실행에서 반영하지 않고 남은 작업 DB 가 있음 (원본과 내용이 다를 때만)
	static void    DiscardWorkDb();        // 남은 작업 DB 버리기
	static bool    ApplyLeftoverWorkDb();  // 남은 작업 DB 를 원본에 반영
	static CString GetDataFilePath();      // <실행 파일 폴더>\library.vmdb (정보 + 임시 목록)
	static CString GetImagesDbPath();      // <실행 파일 폴더>\images.vmdb (예전 이미지 DB, 처음 실행 때 Image 폴더로 풀고 .bak)
	static CString GetImageRoot();         // <실행 파일 폴더>\Image (배우 사진 · 스튜디오 이미지 보관, actors / studios — 태그는 이미지 없음)
	static CString GetBackupDir();         // <DB 폴더>\backup (없으면 만듦)
	// 프로그램 시작 시 DB 백업: library.vmdb 를 backup 폴더에 날짜·시각 이름으로 복사
	//  - 가장 최근 백업과 내용이 같으면 그 파일은 건너뜀, 파일마다 최근 keep 개만 남김
	//  - 반환: 새로 만든 백업 파일 수
	static int     BackupDbFiles(int keep = 10);
	static CString GetLegacyLibraryPath(); // 예전 정보 파일 library.tsv (처음 한 번 옮겨 오고 .bak 으로 바꿈)
	static CString GetPendingFilePath();   // 예전 임시 목록 pending.tsv
	static CString GetCacheDir();          // 실행 중 이미지를 풀어 두는 폴더
	static void    ClearImageCache();      // 캐시 이미지 지우기 (종료 시)
	int  PendingCount() const;

	// 등록 이미지 보관소: <실행 폴더>\Image\<sub> (배우 사진 · 스튜디오 이미지의 암호화 사본 .vmimg)
	static CString GetImageStoreDir(LPCWSTR sub = nullptr);
	static bool    IsInImageStore(const CString& path);
	// 이미지를 보관소로 복사하고 복사본 경로를 반환 (이미 보관소 안이면 그대로, 실패하면 빈 문자열)
	// 영상 경로의 상위 폴더 중 배우 이름과 같은 폴더에서 이미지 파일 찾기 (없으면 빈 문자열)
	static CString FindActorFolderImage(const CString& videoPath, const CString& actorName);
	static CString FindActorFolder(const CString& videoPath, const CString& actorName);   // 배우 이름과 같은 상위 폴더 (없으면 빈 문자열)
	static CString FindActorTextFile(const CString& dir, const CString& actorName);       // 배우 폴더의 텍스트 파일 (*.txt)
	static bool    ApplyActorTextInfo(ActorInfo& a, const CString& file);                // 텍스트(항목: 값)에서 비어 있는 배우 정보 채우기
	static bool    ApplyActorText(ActorInfo& a, CString text);                      // 위와 같은 규칙, 파일 대신 글자 (직접 입력 창)
	static CString ActorInfoText(const ActorInfo& a);                                    // 배우 정보 → "항목: 값" 글자 ([정보 txt 생성]과 같은 형식)
	static void    ClearActorTextFields(ActorInfo& a);
	static std::vector<CString> SplitUrls(const CString& text);   // URL 목록 (줄바꿈 · 공백 · | 구분, 중복 제거)
	static CString JoinUrls(const std::vector<CString>& urls);
	static std::vector<CString> SplitLines(const CString& text);   // 줄마다 하나 (앞뒤 공백 · 빈 줄 · 중복 제거, 쉼표는 그대로) - 스튜디오 서브이름
	static CString JoinLines(const std::vector<CString>& lines);   // 줄바꿈(\n)으로 이어 붙임   // 줄바꿈(\n)으로 이어 붙임                                   // 텍스트로 읽는 항목만 비움 (직접 입력 창 = 입력한 내용으로 교체)
	static bool    HasNoActorInfo(const ActorInfo& a);
	// 정보 txt 내보내기 (같은 폴더, 덮어쓰기 · 내용이 같으면 건너뜀). 반환: 새로 쓴 파일 수
	static CString ExtractCode(const CString& name);
	// 분할 파일 (ABC-123_1.mp4, ABC-123_2.mp4 …): 같은 폴더 + 마지막 '_' 왼쪽 이름이 같으면 한 묶음, 정보(품번 · 제목 · 별점 · 물방울 · 발매일 · 배우 · 스튜디오 · 태그)를 같이 사용
	static bool PartGroupKey(const CString& path, CString& key, int& num);   // 묶음 키(소문자) + 순번 ("_" 오른쪽 숫자, 없으면 0 → false)
	static CString PartGroupPath(const CString& path);                      // 순번을 뗀 대표 경로 (D:\a\ABC-123_2.mp4 → D:\a\ABC-123.mp4)
	std::vector<size_t> PartSiblings(size_t idx) const;                      // 같은 묶음의 다른 영상 (자기 자신 제외)
	int  SyncPartGroup(size_t idx);   // idx 의 정보를 같은 묶음 영상에 복사 (임시 항목도 등록됨). 반환: 바꾼 수
	int  UnifyPartGroups();           // 묶음마다 비어 있는 정보를 다른 파일의 정보로 채움 (등록된 파일이 있으면 임시 파일도 등록). 반환: 바꾼 수
	int  FillMissingCodes();   // 품번이 빈 영상은 파일/폴더 이름에서 찾아 채움 (반환: 채운 수)                          // 이름에서 품번 찾기 (ABC-123 / ABC123 → ABC-123, 없으면 빈 문자열)
	static CString FindVideoTextFile(const CString& videoPath);                 // 영상 폴더의 정보 txt (같은 이름 → '_' 왼쪽 같은 이름 → 영상·txt 가 하나씩이면 그것)
	bool ApplyVideoTextInfo(VideoItem& v, const CString& file) const;           // 텍스트(항목: 값)에서 비어 있는 영상 정보 채우기
	bool ApplyVideoText(VideoItem& v, CString text) const;                 // 위와 같은 규칙, 파일 대신 글자 (직접 입력 창)
	static CString VideoInfoText(const VideoItem& v);                           // 영상 정보 → "항목: 값" 글자 ([정보 txt 생성]과 같은 형식)
	static void    ClearVideoTextFields(VideoItem& v);                          // 텍스트로 읽는 항목만 비움
	// 웹페이지에서 복사한 글(AVDBS · FANZA 등 "출시: … / 출연: #이름 / 제작사: … / 레이블: … / 장르 상세 …") → "항목: 값" 글자
	static CString ParsePastedVideoText(const CString& raw);
	int ExportVideoInfoTxt(int& unchanged, int& failed) const;                  // 영상 폴더\영상이름.txt (저장된 영상)
	int ExportActorInfoTxt(int& unchanged, int& noFolder, int& failed) const;   // 배우 폴더\배우폴더이름.txt                                    // 생년월일·키·국적·치수 등이 모두 비어 있음
	static CString StoreImageCopy(const CString& src, LPCWSTR sub, bool force = false);   // force: 보관소 안의 파일도 새 암호화 사본으로
	bool MigrateImagesToStore();      // 보관소 밖 이미지(기존 데이터)를 복사본으로 교체 (변경 시 true)
	// Image 폴더의 평문 이미지를 암호화 사본(.vmimg)으로 바꾸고 연결도 바꿈 (평문 경로는 plainFiles 에, 저장 후 지울 것)
	bool EncryptImageStore(std::vector<CString>& plainFiles);
	static bool IsEncryptedFile(const CString& path);   // DbCrypt 형식으로 암호화된 파일인지
	int  CleanupImageStore() const;   // 어디에도 연결되지 않은 복사본을 휴지통으로 (반환: 정리한 수)
	static bool    IsVideoFile(LPCWSTR path);
	static CString MakeKey(const CString& path);
	static bool    IsUnder(const CString& path, const CString& folder);

private:
	bool m_loadedPlainText = false;
	bool m_dbBroken = false;
	mutable bool m_workDirty = false;
	// 이미지가 바뀌지 않았으면 저장할 때 images.vmdb 를 다시 쓰지 않음
	mutable CString           m_imgSig;
	mutable bool              m_legacyToMove = false;   // 예전 파일을 옮겨 왔음 → 첫 저장 후 .bak 으로
	bool ReadPackedDb(CString& text, CString& pending);
	bool WritePackedDb(const CString& text, const CString& pending) const;
	static CString ImageSignature(std::vector<CString>* files);   // 캐시 이미지 목록/크기/시각
	static void ScanFolder(const CString& folder, std::vector<VideoItem>& out, int depth);
	int  MergeScanned(const std::vector<VideoItem>& scanned);
public:   // DB 삭제 시 임시 항목으로 되돌릴 때도 사용
	// 등록 폴더 기준 폴더 구조로 배우/스튜디오 채우기 (새로 찾은 임시 항목에만 적용)
	//   (영상 파일은 각자 영상 폴더 안에 있음 - 영상 폴더 이름은 배우/스튜디오로 쓰지 않음)
	//   폴더\영상\영상.mp4               → 영상만
	//   폴더\배우\영상\영상.mp4          → 배우
	//   폴더\스튜디오\배우\영상\영상.mp4 → 스튜디오, 배우 (더 깊으면 위 두 단계 사용)
	//   영상 파일 이름 / 영상 폴더 이름에 [YYYY.MM.DD] 또는 [YYYYMMDD] 가 있으면 발매일로 지정
	void ApplyFolderStructure(VideoItem& v) const;
};
