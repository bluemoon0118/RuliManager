#pragma once

// 이미지 파일을 CImage 로 불러옵니다.
// WIC(Windows Imaging Component)를 먼저 사용하므로 WebP 등도 지원하고, 실패하면 GDI+로 다시 시도합니다.
// Image 폴더의 암호화된 이미지(.vmimg, DbCrypt 형식)도 메모리에서 복호화해서 읽음
bool LoadImageFile(CImage& image, const CString& path);
// 이미지 파일 내용을 메모리 스트림으로 (암호화된 파일은 복호화한 내용), 실패하면 nullptr. 호출한 쪽에서 Release
IStream* OpenImageFileStream(const CString& path);

// 이미지를 비율 유지하여 중앙에 표시하는 정적 컨트롤
class CImagePreview : public CStatic
{
public:
	// 이미지 파일을 불러와 표시합니다. 빈 문자열이면 이미지를 지웁니다.
	bool SetImageFile(const CString& path);
	// 이미지가 없을 때 표시할 안내 문구
	void SetPlaceholder(const CString& text);
	void Clear();
	void SetBackColor(COLORREF bg) { m_bk = bg; if (GetSafeHwnd()) Invalidate(FALSE); }

	const CString& GetImageFile() const { return m_file; }

protected:
	CImage  m_image;
	CString m_file;
	CString m_text;
	COLORREF m_bk = RGB(30, 30, 30);

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	DECLARE_MESSAGE_MAP()
};
