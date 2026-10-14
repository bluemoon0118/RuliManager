#pragma once

#include <atomic>
#include <functional>

// 영상 스크린샷 추출 (영상 카드 오른쪽 클릭 [스크린샷 추출...])
//  - 재생 시간을 (장수 + 2) 칸으로 나눠 맨 앞 · 맨 뒤 칸은 버리고, 가운데 칸마다 그 칸의 시작 시간에서 한 장
//    예) 3장, 100분 → 20분 간격 → 20 · 40 · 60분 (0~20분, 80~100분 칸은 버림)
//  - 사람이 없으면(Windows 얼굴 인식으로 얼굴을 못 찾으면) 10초씩 뒤로 다시 찍어 봄 (그 칸이 끝나기 전까지),
//    끝까지 못 찾으면 칸 시작 시간의 장면을 그대로 저장
//  - 저장: <영상 폴더>\Screenshot\<영상 이름>_01.jpg ... (같은 이름 규칙의 예전 파일은 먼저 지움)
//  - 영상 읽기 = Media Foundation, 얼굴 인식 = Windows.Media.FaceAnalysis (C++/WinRT, 이 파일만 C++20 · PCH 없이 빌드)
//  - 백그라운드 스레드에서 호출 (오래 걸림)
struct ShotResult
{
	int     saved = 0;      // 저장한 장수
	int     noFace = 0;     // 얼굴을 못 찾아 칸 시작 장면을 저장한 장수
	int     failed = 0;     // 장면을 읽지 못한 장수
	bool    faceDetect = false;   // 얼굴 인식을 썼는지 (이 PC 에서 지원하지 않으면 false)
	bool    canceled = false;
	CString error;          // 영상을 열지 못한 경우 등
	CString folder;         // 저장한 폴더
	CString video;          // 영상 파일 경로
};

ShotResult ExtractScreenshots(const CString& videoPath, int count, const std::atomic<bool>& cancel,
	const std::function<void(int done, int total)>& progress);
