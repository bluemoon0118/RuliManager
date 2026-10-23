#pragma once

// 벡터 아이콘 (GDI+ 경로로 그려서 크기와 상관없이 매끄러움, 안티앨리어싱)
// 24x24 단위로 설계한 모양을 area 안에 비율 유지해서 가운데 배치 (fill = area 의 짧은 변 대비 크기)
// 구멍(재생 삼각형, 꼬리표 구멍 등)은 실제로 뚫려 있어 배경이 비침
namespace VectorIcon
{
	void Camera(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);      // 비디오 카메라 (스튜디오 기본 이미지, Font Awesome video)
	void Tag(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);         // 꼬리표 (태그)
	void Collection(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);  // 필름 (컬렉션 기본 이미지 · 탭 아이콘)
	void FolderPlay(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);  // 폴더 + 재생 구멍 (등록 폴더 - 폴더 관리 창)
	void FolderWarn(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);  // 폴더 + 느낌표 구멍 (찾을 수 없는 폴더)
	void UsbDrive(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);    // USB 메모리 (이동식 디스크 폴더)
	void Person(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);      // 사람 (배우)
	void User(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);        // 사람 (성별 미지정 배우 기본 이미지, Font Awesome user)
	void Movie(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);       // 원 + 재생 (영상 기본 이미지, Font Awesome circle-play)
	void PlayCircle(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);  // 원 + 재생 (영상 수)
	void Gear(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);
	void Copy(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);        // 겹친 두 장 (복사 - 품번 복사 버튼)
	// 메뉴 아이콘 (오른쪽 클릭 메뉴 왼쪽 - DarkMenu)
	void Trash(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);       // 휴지통 (삭제)
	void MinusCircle(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0); // 원 + 빼기 (컬렉션에서 빼기)
	void PlusCircle(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);  // 원 + 더하기 (추가)
	void Pencil(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);      // 연필 (편집 · 변경)
	void Refresh(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);     // 둥근 화살표 (새로고침)
	void Folder(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);      // 폴더 (탐색기 · 폴더 관리)
	void Play(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);        // 재생 삼각형 (재생)
	void Paste(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);       // 클립보드 (붙여넣기)
	void TextDoc(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);     // 글 줄이 있는 문서 (텍스트로 정보 입력)
	void Picture(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);     // 사진 (스크린샷 추출)
	void Merge(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);       // 겹친 두 원 (합치기 · 병합)
	void Eraser(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);      // 지우개 (비우기 · 초기화)
	void CheckOn(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);     // 체크된 칸 (전체 체크)
	void CheckOff(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);    // 빈 칸 (체크 모두 해제)
	void Heart(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);       // 하트 (즐겨찾기)
	void Drops(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);       // 물방울 (영상 카운트)
	void ArrowRight(CDC* dc, double x0, double x1, double cy, double thick, COLORREF col);   // 오른쪽 화살표 → (x0~x1, 세로 중심 cy, 선 굵기)

	// 실루엣: SVG 경로 데이터([점 수, x, y ...] 반복, -1 = 다음 <path>, 0 = 끝)를 viewBox 비율 그대로 area 안에 맞춤 (evenodd)
	void Silhouette(CDC* dc, const CRect& area, COLORREF col, const float* data, float vbW, float vbH, double fill = 1.0);
	int  FemaleCount();                                                             // 여성 배우 기본 이미지 수
	void Female(CDC* dc, const CRect& area, COLORREF col, int index, double fill = 1.0);   // 여성 배우 기본 이미지 (index 번째)
	int  MaleCount();                                                               // 남성 배우 기본 이미지 수
	void Male(CDC* dc, const CRect& area, COLORREF col, int index, double fill = 1.0);     // 남성 배우 기본 이미지 (index 번째)
}
