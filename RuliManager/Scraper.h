#pragma once

// ---------------------------------------------------------------------------
// 사이트에서 영상 정보 가져오기 (현재: AVDBS 작품 페이지)
//  - 입력: 작품 페이지 주소(https://www.avdbs.com/menu/dvd.php?dvd_idx=…), dvd_idx 숫자, 또는 품번(ABC-123 → 사이트 검색으로 페이지 찾기)
//  - 결과: 정보 txt 와 같은 "항목: 값" 글자 (품번 · 제목 · 발매일 · 배우 · 스튜디오 · 태그) → 텍스트로 정보 입력 창에서 확인 후 적용
namespace Scraper
{
	bool FetchAvdbs(const CString& query, CString& infoText, CString& pageUrl, CString& err);
	bool ParseAvdbsDvdPage(const CString& html, CString& infoText);   // 받은 HTML → "항목: 값"
}
