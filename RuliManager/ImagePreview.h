#pragma once

#include <functional>

// 이미지 파일을 CImage 로 불러옵니다.
// WIC(Windows Imaging Component)를 먼저 사용하므로 WebP 등도 지원하고, 실패하면 GDI+로 다시 시도합니다.
// Image 폴더의 암호화된 이미지(.vmimg, DbCrypt 형식)도 메모리에서 복호화해서 읽음
bool LoadImageFile(CImage& image, const CString& path);
// 이미지 파일 내용을 메모리 스트림으로 (암호화된 파일은 복호화한 내용), 실패하면 nullptr. 호출한 쪽에서 Release
IStream* OpenImageFileStream(const CString& path);

// SFW(모자이크) 옵션: 켜면 배우 · 영상 이미지를 읽을 때 메모리에서만 모자이크 처리 (원본 파일은 바꾸지 않음)
void SetSfwMosaic(bool on);
bool IsSfwMosaic();
void ApplyMosaic(CImage& image);                                   // 이미지를 칸 단위 평균색으로 (메모리의 이미지만)
bool LoadImageFileSfw(CImage& image, const CString& path);         // LoadImageFile + (SFW 켜져 있으면) 모자이크

// 이미지를 비율 유지하여 중앙에 표시하는 정적 컨트롤
class CImagePreview : public CStatic
{
public:
	// 이미지 파일을 불러와 표시합니다. 빈 문자열이면 이미지를 지웁니다.
	bool SetImageFile(const CString& path, bool sfw = false);   // sfw: SFW 옵션이 켜져 있으면 모자이크로 (배우 · 영상 이미지)
	// 이미지가 없을 때 표시할 안내 문구 (이전 이미지는 지움)
	void SetPlaceholder(const CString& text);
	// 이미지가 없을 때 직접 그리는 기본 이미지 (예: 사진 없는 배우의 실루엣) - 이전 이미지는 지움
	void SetDefaultImage(std::function<void(CDC*, const CRect&)> draw);
	void Clear();
	void SetBackColor(COLORREF bg) { m_bk = bg; if (GetSafeHwnd()) Invalidate(FALSE); }
	COLORREF GetBackColor() const { return m_bk; }
	// 이미지를 반투명으로 (연결 안 된 이동식 디스크 영상) - 이미지를 바꾸면(SetImageFile 등) 다시 불투명
	void SetDimmed(bool dim) { const BYTE a = dim ? 100 : 255; if (a != m_alpha) { m_alpha = a; if (GetSafeHwnd()) Invalidate(FALSE); } }

	const CString& GetImageFile() const { return m_file; }

protected:
	CImage  m_image;
	CString m_file;
	CString m_text;
	COLORREF m_bk = RGB(30, 30, 30);
	BYTE    m_alpha = 255;   // 이미지 불투명도 (100 = 반투명)
	std::function<void(CDC*, const CRect&)> m_drawDefault;   // 기본 이미지 (있으면 안내 문구 대신)
public:
	std::function<void()> m_onDblClick;   // 더블클릭 (SS_NOTIFY 스타일이 있어야 마우스를 받음)
	std::function<void(bool)> m_onHover;   // 마우스가 들어옴(true) / 나감(false) (SS_NOTIFY 필요) - 영상 상세: 미리보기 영상 재생
protected:
	bool m_hoverTracking = false;

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
	DECLARE_MESSAGE_MAP()
};
