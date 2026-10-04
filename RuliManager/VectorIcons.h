#pragma once

// 벡터 아이콘 (GDI+ 경로로 그려서 크기와 상관없이 매끄러움, 안티앨리어싱)
// 24x24 단위로 설계한 모양을 area 안에 비율 유지해서 가운데 배치 (fill = area 의 짧은 변 대비 크기)
// 구멍(재생 삼각형, 꼬리표 구멍 등)은 실제로 뚫려 있어 배경이 비침
namespace VectorIcon
{
	void Camera(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);      // 비디오 카메라 (스튜디오 기본 이미지, Font Awesome video)
	void Tag(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);         // 꼬리표 (태그)
	void Person(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);      // 사람 (배우)
	void User(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);        // 사람 (성별 미지정 배우 기본 이미지, Font Awesome user)
	void Movie(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);       // 원 + 재생 (영상 기본 이미지, Font Awesome circle-play)
	void PlayCircle(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);  // 원 + 재생 (영상 수)
	void Gear(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);        // 톱니바퀴 (설정)
	void Heart(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);       // 하트 (즐겨찾기)
	void Drops(CDC* dc, const CRect& area, COLORREF col, double fill = 1.0);       // 물방울 (영상 카운트)

	// 실루엣: SVG 경로 데이터([점 수, x, y ...] 반복, -1 = 다음 <path>, 0 = 끝)를 viewBox 비율 그대로 area 안에 맞춤 (evenodd)
	void Silhouette(CDC* dc, const CRect& area, COLORREF col, const float* data, float vbW, float vbH, double fill = 1.0);
	int  FemaleCount();                                                             // 여성 배우 기본 이미지 수
	void Female(CDC* dc, const CRect& area, COLORREF col, int index, double fill = 1.0);   // 여성 배우 기본 이미지 (index 번째)
	int  MaleCount();                                                               // 남성 배우 기본 이미지 수
	void Male(CDC* dc, const CRect& area, COLORREF col, int index, double fill = 1.0);     // 남성 배우 기본 이미지 (index 번째)
}
