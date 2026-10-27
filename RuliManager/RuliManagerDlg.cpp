#include "pch.h"
#include "RuliManager.h"
#include "RuliManagerDlg.h"
#include "ActorDlg.h"
#include "TextInfoDlg.h"
#include "TagLinkDlg.h"
#include <cmath>
#include <thread>
#include "SettingsDlg.h"
#include "Version.h"
#include <dbt.h>
#include "ImageSearchDlg.h"
#include "NameListDlg.h"
#include "SeriesDlg.h"
#include "FileManageDlg.h"
#include "DarkTheme.h"
#include "CountryCombo.h"
#include "VectorIcons.h"
#include "TextDraw.h"

#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")   // AlphaBlend (영상 카드 덧그림 페이드)

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	void EnsureGdiPlus();   // GDI+ 시작 (아래 정의 - 앞쪽 코드에서도 쓰기 위해 미리 선언)
}

namespace
{
	const UINT WM_APP_REBUILD_CATS = WM_APP + 1;
	const UINT WM_APP_CARD_MEDIA = WM_APP + 2;   // 백그라운드에서 영상 해상도 · 재생 시간을 읽음 (lParam = CardMediaResult*)
	struct CardMediaResult
	{
		CString key;
		CMediaInfoLabel::Info info;
	};
	void DrawDefaultActorImage(CDC* dc, const CRect& img, const ActorInfo& a);   // 아래에서 정의 (사진 없는 배우 카드의 기본 이미지)
	const UINT WM_APP_SHOT_PROGRESS = WM_APP + 4;   // 스크린샷 추출 진행 (wParam = 끝난 장수, lParam = 전체)
	const UINT WM_APP_SHOT_DONE = WM_APP + 5;       // 스크린샷 추출 끝 (lParam = ShotResult*)
	const UINT WM_APP_PREVIEW_DONE = WM_APP + 6;    // 미리보기 영상 만들기 끝 (lParam = PreviewVideo::Result*)
	const UINT WM_APP_COVER_READY = WM_APP + 3;  // 백그라운드에서 영상 카드 이미지를 읽음 (lParam = CoverResult*)
	struct CoverResult
	{
		CString key;
		HBITMAP bmp = nullptr;   // 이미지가 없거나 못 읽으면 nullptr
		UINT gen = 0;
		int item = -1;           // 영상 항목 (그 카드만 다시 그리기)
	};

	enum Column { COL_NAME = 0, COL_TITLE, COL_SIZE, COL_DATE, COL_RATING, COL_RELEASE, COL_ACTORS, COL_STUDIO, COL_TAGS, COL_PATH, COL_COUNT };
	const int SORT_ACTORCOUNT = COL_COUNT;   // 정렬 콤보 전용 (목록 열 없음): 참여 배우 수
	// 정렬 콤보 · [리스트] 열 정렬 전용 (예전 목록 열에 없던 기준)
	const int SORT_CODE = COL_COUNT + 1;       // 품번
	const int SORT_LABEL = COL_COUNT + 2;      // 레이블
	const int SORT_DURATION = COL_COUNT + 3;   // 재생 시간 (읽어 둔 것만, 모르면 0)
	const int SORT_RES = COL_COUNT + 4;        // 해상도 (가로 × 세로)
	const int SORT_LAST = SORT_RES;
	// 영상 탭 [리스트] 보기 열 (스프레드시트) → 정렬 기준
	//  체크 칸 + 커버 · 품번 · 날짜(발매일) · 별점 · 길이(재생 시간) · 제작사 / 레이블 · 배우 · 태그
	enum ListCol { LC_COVER = 0, LC_CODE, LC_RELEASE, LC_RATING, LC_DURATION, LC_STUDIO, LC_ACTORS, LC_TAGS, LC_COUNT };
	const int kListSort[LC_COUNT] = { -1, SORT_CODE, COL_RELEASE, COL_RATING, SORT_DURATION, COL_STUDIO, COL_ACTORS, COL_TAGS };   // -1 = 커버 (정렬 없음), 제작사 / 레이블 = 제작사 기준
	bool SortDescFirst(int col)   // 처음 고르면 큰 값부터 (날짜 · 크기 · 별점 · 배우수 · 재생 시간 · 해상도)
	{
		return col == COL_DATE || col == COL_RATING || col == COL_SIZE || col == COL_RELEASE || col == SORT_ACTORCOUNT || col == SORT_DURATION || col == SORT_RES;
	}

	CString FormatSize(ULONGLONG b)
	{
		CString s;
		if (b < 1024ULL)                       s.Format(L"%llu B", b);
		else if (b < 1024ULL * 1024)           s.Format(L"%.1f KB", b / 1024.0);
		else if (b < 1024ULL * 1024 * 1024)    s.Format(L"%.1f MB", b / (1024.0 * 1024));
		else if (b < 1024ULL * 1024 * 1024 * 1024) s.Format(L"%.2f GB", b / (1024.0 * 1024 * 1024));
		else                                   s.Format(L"%.2f TB", b / (1024.0 * 1024 * 1024 * 1024));   // 테라 단위 (필터 용량 합계 등)
		return s;
	}

	CString FormatTime(ULONGLONG t)
	{
		if (t == 0) return CString();
		FILETIME ft = { static_cast<DWORD>(t & 0xFFFFFFFF), static_cast<DWORD>(t >> 32) };
		FILETIME local = {};
		SYSTEMTIME st = {};
		if (!::FileTimeToLocalFileTime(&ft, &local) || !::FileTimeToSystemTime(&local, &st))
			return CString();
		CString s;
		s.Format(L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
		return s;
	}

	// 검열 정보 색 (영상 카드 사각형 · 영상 상세 콤보 글자): 검열됨 회색 / 모자이크파괴 주황 / 무수정 초록 / 유출 빨강, 해당없음 = CLR_DEFAULT
	COLORREF CensorColor(int censor)
	{
		static const COLORREF k[CVideoLibrary::kCensorCount] = {
			CLR_DEFAULT, RGB(0xA7, 0xB6, 0xC2), RGB(0xFF, 0xB0, 0x4C), RGB(0x5C, 0xD6, 0x8A), RGB(0xFF, 0x6B, 0x6B)
		};
		return (censor >= 0 && censor < CVideoLibrary::kCensorCount) ? k[censor] : CLR_DEFAULT;
	}

	// 별점 글자 (반 별 단위): ★★★½☆
	CString RatingText(int r)
	{
		if (r <= 0) return CString();
		CString s;
		for (int i = 0; i < 5; ++i)
			s += (r >= i * 2 + 2) ? L'★' : (r == i * 2 + 1 ? L'½' : L'☆');
		return s;
	}

	// "YYYY-MM-DD" → SYSTEMTIME
	bool ParseDate(const CString& text, SYSTEMTIME& st)
	{
		int y = 0, m = 0, d = 0;
		if (text.IsEmpty() || swscanf_s(text, L"%d-%d-%d", &y, &m, &d) != 3)
			return false;
		if (y < 1601 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31)
			return false;
		st = {};
		st.wYear = static_cast<WORD>(y);
		st.wMonth = static_cast<WORD>(m);
		st.wDay = static_cast<WORD>(d);
		return true;
	}

	CString NormalizeTags(const CString& in)
	{
		CString out;
		int pos = 0;
		CString work = in;
		work.Replace(L';', L',');
		for (;;)
		{
			const int p = CVideoLibrary::FindListComma(work, pos);
			CString t = (p < 0) ? work.Mid(pos) : work.Mid(pos, p - pos);
			t.Trim();
			if (!t.IsEmpty())
			{
				if (!out.IsEmpty()) out += L", ";
				out += t;
			}
			if (p < 0) break;
			pos = p + 1;
		}
		return out;
	}

	CString LastErrorText(DWORD err)
	{
		LPWSTR buf = nullptr;
		::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr, err, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
		CString s = buf ? buf : L"";
		if (buf) ::LocalFree(buf);
		s.Trim();
		CString r;
		r.Format(L"%s (코드 %lu)", static_cast<LPCWSTR>(s), err);
		return r;
	}

	bool MoveFileWithRetry(const CString& from, const CString& to)
	{
		// 플레이어가 파일 핸들을 늦게 놓는 경우를 대비해 몇 번 재시도
		for (int i = 0; i < 6; ++i)
		{
			if (::MoveFileExW(from, to, 0))
				return true;
			const DWORD e = ::GetLastError();
			if (e != ERROR_SHARING_VIOLATION && e != ERROR_ACCESS_DENIED)
				return false;
			::Sleep(150);
		}
		return false;
	}
}

// ---------------------------------------------------------------------------
// 간단한 입력 창 (격자 보기에서 이름 변경)

class CInputDlg : public CDialogEx
{
public:
	CInputDlg(const CString& value, CWnd* parent, const CString& caption = CString(), const CString& prompt = CString())
		: CDialogEx(IDD_INPUT, parent), m_value(value), m_caption(caption), m_prompt(prompt) {}
	CString m_value;
	CString m_caption, m_prompt;   // 비어 있으면 rc 의 "이름 변경" / "새 파일 이름"

protected:
	CDarkDialogTheme m_theme;   // 메인 창과 같은 어두운 색상
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
	{
		if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
			return br;
		return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
	}
	DECLARE_MESSAGE_MAP()

	virtual BOOL OnInitDialog()
	{
		CDialogEx::OnInitDialog();
		m_theme.Apply(this);
		CEdit* edit = static_cast<CEdit*>(GetDlgItem(IDC_INPUT_EDIT));
		edit->SetWindowText(m_value);
		if (!m_caption.IsEmpty()) SetWindowText(m_caption);
		if (!m_prompt.IsEmpty()) SetDlgItemText(IDC_INPUT_PROMPT, m_prompt);
		edit->SetFocus();
		if (!m_prompt.IsEmpty())
		{
			edit->SetSel(0, -1);   // 일반 입력: 전체 선택
			return FALSE;
		}
		// 확장자 앞까지만 선택
		const int ext = static_cast<int>(::PathFindExtensionW(m_value) - static_cast<LPCWSTR>(m_value));
		edit->SetSel(0, ext);
		return FALSE;
	}
	virtual void OnOK()
	{
		GetDlgItemText(IDC_INPUT_EDIT, m_value);
		CDialogEx::OnOK();
	}
};

BEGIN_MESSAGE_MAP(CInputDlg, CDialogEx)
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

// ---------------------------------------------------------------------------
// 컬렉션 편집 창: 제목 · 커버 이미지 · 메모 (새 컬렉션도 이 창)

class CCollectionEditDlg : public CDialogEx
{
public:
	CCollectionEditDlg(CWnd* parent, const CString& caption) : CDialogEx(IDD_COLLECTION_EDIT, parent), m_caption(caption) {}
	CString m_title, m_memo, m_image;      // 열 때 값 → 확인 시 결과 (m_image: 고른 이미지 파일, 비우면 대표 영상 커버)
	std::function<bool(const CString&)> m_titleTaken;   // 같은 제목의 다른 컬렉션이 있으면 true

protected:
	CString m_caption;
	CImagePreview m_cover;
	CDarkDialogTheme m_theme;
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
	{
		if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
			return br;
		return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
	}
	void ShowCover()
	{
		if (!m_image.IsEmpty() && ::PathFileExistsW(m_image))
			m_cover.SetImageFile(m_image);
		else
			m_cover.SetPlaceholder(L"커버 없음\n\n(대표 영상 커버를 사용)");
	}
	afx_msg void OnPick()
	{
		CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
			L"이미지 파일 (*.jpg;*.jpeg;*.png;*.webp;*.bmp;*.gif;*.tif;*.tiff;*.svg)|*.jpg;*.jpeg;*.png;*.webp;*.bmp;*.gif;*.tif;*.tiff;*.svg|모든 파일 (*.*)|*.*||", this);
		if (dlg.DoModal() != IDOK)
			return;
		m_image = dlg.GetPathName();
		ShowCover();
	}
	afx_msg void OnClear()
	{
		m_image.Empty();
		ShowCover();
	}
	DECLARE_MESSAGE_MAP()

	virtual BOOL OnInitDialog()
	{
		CDialogEx::OnInitDialog();
		m_theme.Apply(this);
		m_cover.SubclassDlgItem(IDC_COL_COVER, this);
		m_cover.SetBackColor(TCOL(TC_PREVIEW));
		m_cover.m_onDblClick = [this]() { OnPick(); };   // 커버 더블클릭 = 이미지 선택
		SetWindowText(m_caption);
		SetDlgItemText(IDC_COL_TITLE, m_title);
		CString memo = m_memo;
		memo.Replace(L"\r\n", L"\n");
		memo.Replace(L"\n", L"\r\n");
		SetDlgItemText(IDC_COL_MEMO, memo);
		ShowCover();
		CEdit* title = static_cast<CEdit*>(GetDlgItem(IDC_COL_TITLE));
		title->SetFocus();
		title->SetSel(0, -1);
		return FALSE;
	}
	virtual void OnOK()
	{
		CString title;
		GetDlgItemText(IDC_COL_TITLE, title);
		title.Trim();
		CVideoLibrary::RemoveListCommas(title);   // 구분 쉼표는 제목에 쓸 수 없음
		title.Trim();
		if (title.IsEmpty())
		{
			AfxMessageBox(L"제목을 입력하세요.", MB_ICONINFORMATION);
			GetDlgItem(IDC_COL_TITLE)->SetFocus();
			return;
		}
		if (m_titleTaken && m_titleTaken(title))
		{
			AfxMessageBox(L"같은 제목의 컬렉션이 이미 있습니다.", MB_ICONINFORMATION);
			GetDlgItem(IDC_COL_TITLE)->SetFocus();
			return;
		}
		m_title = title;
		GetDlgItemText(IDC_COL_MEMO, m_memo);
		m_memo.Replace(L"\r\n", L"\n");
		m_memo.TrimRight();
		CDialogEx::OnOK();
	}
};

BEGIN_MESSAGE_MAP(CCollectionEditDlg, CDialogEx)
	ON_WM_CTLCOLOR()
	ON_BN_CLICKED(IDC_COL_COVER_PICK, &CCollectionEditDlg::OnPick)
	ON_BN_CLICKED(IDC_COL_COVER_CLEAR, &CCollectionEditDlg::OnClear)
END_MESSAGE_MAP()

// ---------------------------------------------------------------------------
// 폴더 관리 창: 등록 폴더를 칩 목록으로 (칩마다 폴더 아이콘 · 경로 · [이동식 디스크] 표시 · ×)

class CFolderChipList : public CWnd
{
public:
	struct Item { CString path; CString tag; bool removable = false; bool offline = false; };
	std::function<void(int)> m_onRemove;   // × 클릭
	std::function<void(int)> m_onOpen;     // 칩 더블클릭 (탐색기로 열기)

	BOOL Create(CWnd* parent, const CRect& rc, UINT id)
	{
		const CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
		return CWnd::CreateEx(0, cls, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, rc, parent, id);
	}
	void SetItems(const std::vector<Item>& items) { m_items = items; m_scroll = 0; m_hotX = -1; if (GetSafeHwnd()) Invalidate(FALSE); }

protected:
	std::vector<Item>  m_items;
	std::vector<CRect> m_chips, m_xs;   // 칩 · × 영역 (내용 좌표, 스크롤 전)
	int  m_scroll = 0, m_contentH = 0;
	int  m_hotX = -1, m_hotChip = -1;
	bool m_tracking = false;

	void DoLayout(CDC* dc, int width)
	{
		m_chips.clear();
		m_xs.clear();
		const int lineH = dc->GetTextExtent(L"Ag가").cy;
		// 한 줄에 폴더 하나: 폭 전체를 채운 사각형 칸
		const int chipH = lineH + 14, pad = 8, gap = 4, xW = lineH;
		int y = pad;
		for (size_t i = 0; i < m_items.size(); ++i)
		{
			const CRect chip(pad, y, width - pad, y + chipH);
			m_chips.push_back(chip);
			m_xs.push_back(CRect(chip.right - 10 - xW, chip.top + (chipH - xW) / 2, chip.right - 10, chip.top + (chipH + xW) / 2));
			y += chipH + gap;
		}
		m_contentH = y - gap + pad;
	}
	int HitChip(CPoint pt, bool* onX) const
	{
		pt.y += m_scroll;
		for (size_t i = 0; i < m_chips.size(); ++i)
			if (m_chips[i].PtInRect(pt))
			{
				if (onX) *onX = m_xs[i].PtInRect(pt) != FALSE;
				return static_cast<int>(i);
			}
		return -1;
	}
	afx_msg void OnPaint()
	{
		CPaintDC pdc(this);
		CRect client;
		GetClientRect(&client);
		CDC mem;
		mem.CreateCompatibleDC(&pdc);
		CBitmap bmp;
		bmp.CreateCompatibleBitmap(&pdc, (std::max)(1, client.Width()), (std::max)(1, client.Height()));
		CBitmap* oldBmp = mem.SelectObject(&bmp);
		mem.FillSolidRect(&client, TCOL(TC_EDIT));
		{
			CBrush border(TCOL(TC_BORDER));
			mem.FrameRect(&client, &border);
		}
		CFont* oldFont = mem.SelectObject(GetParent()->GetFont());
		mem.SetBkMode(TRANSPARENT);
		DoLayout(&mem, client.Width());
		m_scroll = (std::max)(0, (std::min)(m_scroll, m_contentH - client.Height()));
		if (m_items.empty())
		{
			mem.SetTextColor(TCOL(TC_SUB2));
			mem.DrawText(L"등록된 폴더가 없습니다.\n[폴더 추가...] 로 동영상 폴더를 등록하세요.", &client, DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
		}
		const int lineH = mem.GetTextExtent(L"Ag가").cy;
		for (size_t i = 0; i < m_items.size() && i < m_chips.size(); ++i)
		{
			CRect chip = m_chips[i];
			chip.OffsetRect(0, -m_scroll);
			if (chip.bottom < 0 || chip.top > client.bottom)
				continue;
			const Item& it = m_items[i];
			const COLORREF back = it.removable ? TCOL(TC_LABELCARD) : TCOL(TC_COMBO);   // 이동식 디스크 = 보라빛
			mem.FillSolidRect(&chip, back);   // 꽉 찬 사각형 칸
			if (static_cast<int>(i) == m_hotChip)
			{
				CBrush hot(TCOL(TC_CARDHOT));
				mem.FrameRect(&chip, &hot);   // 마우스 오버 테두리
			}
			const COLORREF text = it.offline ? TCOL(TC_SUB2) : TCOL(TC_TEXT);
			// 종류별 아이콘: 이동식 디스크 = USB, 찾을 수 없음 = 폴더 + !, 일반 = 폴더 + 재생
			{
				const int isz = lineH + 4;
				const int iy = chip.top + (chip.Height() - isz) / 2;
				CRect icon(chip.left + 10, iy, chip.left + 10 + isz, iy + isz);
				if (it.removable)    VectorIcon::UsbDrive(&mem, icon, text, 1.0);
				else if (it.offline) VectorIcon::FolderWarn(&mem, icon, text, 1.0);
				else                 VectorIcon::FolderPlay(&mem, icon, text, 1.0);
			}
			int x = chip.left + 10 + lineH + 4 + 10;
			CRect xr = m_xs[i];
			xr.OffsetRect(0, -m_scroll);
			const int tagW = it.tag.IsEmpty() ? 0 : TextFB::Width(&mem, it.tag) + 8;
			mem.SetTextColor(text);
			TextFB::Draw(&mem, it.path, CRect(x, chip.top, xr.left - 8 - tagW, chip.bottom), DT_LEFT, true);
			if (!it.tag.IsEmpty())
			{
				mem.SetTextColor(it.offline ? RGB(0xF2, 0x9B, 0x54) : TCOL(TC_SUB2));
				TextFB::Draw(&mem, it.tag, CRect(xr.left - 8 - tagW + 8, chip.top, xr.left - 8, chip.bottom), DT_LEFT, true);
			}
			// × (마우스를 올리면 빨강)
			const COLORREF xc = (static_cast<int>(i) == m_hotX) ? RGB(0xDB, 0x37, 0x37) : TCOL(TC_SUB2);
			CPen xp(PS_SOLID, 2, xc);
			CPen* op = mem.SelectObject(&xp);
			const int m = xr.Width() / 4;
			mem.MoveTo(xr.left + m, xr.top + m);
			mem.LineTo(xr.right - m, xr.bottom - m);
			mem.MoveTo(xr.right - m, xr.top + m);
			mem.LineTo(xr.left + m, xr.bottom - m);
			mem.SelectObject(op);
		}
		mem.SelectObject(oldFont);
		pdc.BitBlt(0, 0, client.Width(), client.Height(), &mem, 0, 0, SRCCOPY);
		mem.SelectObject(oldBmp);
	}
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnLButtonDown(UINT, CPoint pt)
	{
		SetFocus();
		bool onX = false;
		const int i = HitChip(pt, &onX);
		if (i >= 0 && onX && m_onRemove)
			m_onRemove(i);
	}
	afx_msg void OnLButtonDblClk(UINT, CPoint pt)
	{
		bool onX = false;
		const int i = HitChip(pt, &onX);
		if (i >= 0 && !onX && m_onOpen)
			m_onOpen(i);
	}
	afx_msg void OnMouseMove(UINT, CPoint pt)
	{
		if (!m_tracking)
		{
			TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
			m_tracking = ::TrackMouseEvent(&tme) != FALSE;
		}
		bool onX = false;
		const int i = HitChip(pt, &onX);
		const int hx = onX ? i : -1;
		if (hx != m_hotX || i != m_hotChip)
		{
			m_hotX = hx;
			m_hotChip = i;
			Invalidate(FALSE);
		}
	}
	afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM)
	{
		m_tracking = false;
		m_hotX = m_hotChip = -1;
		Invalidate(FALSE);
		return 0;
	}
	afx_msg BOOL OnMouseWheel(UINT, short zDelta, CPoint)
	{
		m_scroll -= zDelta / 3;
		Invalidate(FALSE);
		return TRUE;
	}
	DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CFolderChipList, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_MOUSEMOVE()
	ON_MESSAGE(WM_MOUSELEAVE, &CFolderChipList::OnMouseLeave)
	ON_WM_MOUSEWHEEL()
END_MESSAGE_MAP()

class CFolderManageDlg : public CDialogEx
{
public:
	explicit CFolderManageDlg(CWnd* parent) : CDialogEx(IDD_FOLDER_MANAGE, parent) {}
	std::function<std::vector<CFolderChipList::Item>()> m_getItems;   // 지금 등록 폴더 목록
	std::function<bool()> m_onAdd;                                     // [폴더 추가...] (추가했으면 true)
	std::function<bool(int)> m_onRemove;                               // 칩 × (제거했으면 true)
	std::function<void(int)> m_onOpen;                                 // 칩 더블클릭
	std::function<void()> m_onRefreshState;                            // 연결 상태 다시 확인 (드라이브 문자 변경 · 연결 여부)

protected:
	CFolderChipList  m_chips;
	CDarkDialogTheme m_theme;
	CString m_lastSig;   // 마지막으로 표시한 목록 (경로 · 상태) - 바뀌었을 때만 다시 그림
	void Reload(bool force = true)
	{
		const std::vector<CFolderChipList::Item> items = m_getItems ? m_getItems() : std::vector<CFolderChipList::Item>();
		CString sig;
		for (const CFolderChipList::Item& it : items)
			sig += it.path + L"|" + it.tag + L"\n";
		if (!force && sig == m_lastSig)
			return;   // 상태 그대로 (스크롤 · 마우스 오버 유지)
		m_lastSig = sig;
		m_chips.SetItems(items);
		CString info;
		info.Format(L"등록 폴더 %d개 · 칩의 × = 목록에서 제거 (파일은 그대로), 칩 더블클릭 = 탐색기로 열기", static_cast<int>(items.size()));
		SetDlgItemText(IDC_FM_INFO, info);
	}
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
	{
		if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
			return br;
		return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
	}
	afx_msg void OnAdd()
	{
		if (m_onAdd && m_onAdd())
			Reload();
	}
	// 이동식 디스크 연결 상태 실시간 반영: 장치 연결 / 분리 알림 + 2초마다 확인 (알림이 안 오는 장치 대비)
	afx_msg BOOL OnDeviceChange(UINT nEventType, DWORD_PTR dwData)
	{
		if (m_onRefreshState)
			m_onRefreshState();
		Reload(false);
		return CDialogEx::OnDeviceChange(nEventType, dwData);
	}
	afx_msg void OnTimer(UINT_PTR id)
	{
		if (id == 1)
		{
			if (m_onRefreshState)
				m_onRefreshState();
			Reload(false);
			return;
		}
		CDialogEx::OnTimer(id);
	}
	afx_msg void OnDestroy()
	{
		KillTimer(1);
		CDialogEx::OnDestroy();
	}
	DECLARE_MESSAGE_MAP()

	virtual BOOL OnInitDialog()
	{
		CDialogEx::OnInitDialog();
		m_theme.Apply(this);
		CRect rc;
		CWnd* place = GetDlgItem(IDC_FM_LIST);
		place->GetWindowRect(&rc);
		ScreenToClient(&rc);
		place->ShowWindow(SW_HIDE);
		m_chips.Create(this, rc, IDC_FM_LIST + 1000);
		m_chips.m_onRemove = [this](int i)
		{
			if (m_onRemove && m_onRemove(i))
				Reload();
		};
		m_chips.m_onOpen = [this](int i) { if (m_onOpen) m_onOpen(i); };
		Reload();
		SetTimer(1, 2000, nullptr);   // 이동식 디스크 연결 상태 확인
		return TRUE;
	}
};

BEGIN_MESSAGE_MAP(CFolderManageDlg, CDialogEx)
	ON_WM_CTLCOLOR()
	ON_WM_DEVICECHANGE()
	ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDC_FM_ADD, &CFolderManageDlg::OnAdd)
END_MESSAGE_MAP()

// ---------------------------------------------------------------------------

CRuliManagerDlg::CRuliManagerDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_RULIMANAGER_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CRuliManagerDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST_VIDEOS, m_list);
	DDX_Control(pDX, IDC_EDIT_SEARCH, m_editSearch);
	DDX_Control(pDX, IDC_COMBO_FILTER, m_comboFilter);
	DDX_Control(pDX, IDC_COMBO_SORT, m_comboSort);
	DDX_Control(pDX, IDC_COMBO_PAGESIZE, m_comboPageSize);
	DDX_Control(pDX, IDC_COMBO_TREEMAP_BASIS, m_comboTmBasis);
	DDX_Control(pDX, IDC_EDIT_ACTORS, m_editActors);
	DDX_Control(pDX, IDC_EDIT_VALIASES, m_editVAliases);
	DDX_Control(pDX, IDC_EDIT_STUDIO, m_editStudio);
	DDX_Control(pDX, IDC_DATE_RELEASE, m_dateRelease);
	DDX_Control(pDX, IDC_COMBO_CENSOR, m_comboCensor);
	DDX_Control(pDX, IDC_LIST_CATEGORY, m_listCat);
	DDX_Control(pDX, IDC_EDIT_TAGS, m_editTags);
	DDX_Control(pDX, IDC_EDIT_MEMO, m_editMemo);
	DDX_Control(pDX, IDC_EDIT_CODE, m_editCode);
	DDX_Control(pDX, IDC_EDIT_SERIES, m_editSeries);
	DDX_Control(pDX, IDC_EDIT_TITLE, m_editTitle);
	DDX_Control(pDX, IDC_STATIC_NAME, m_staticName);
	DDX_Control(pDX, IDC_STATIC_STATUS, m_staticStatus);
	DDX_Control(pDX, IDC_STATIC_IMAGE, m_staticImage);
	DDX_Control(pDX, IDC_PREVIEW, m_preview);
}

BEGIN_MESSAGE_MAP(CRuliManagerDlg, CDialogEx)
	ON_WM_SIZE()
	ON_WM_DEVICECHANGE()
	ON_WM_DROPFILES()
	ON_WM_MOVE()
	ON_WM_GETMINMAXINFO()
	ON_WM_CLOSE()
	ON_WM_CTLCOLOR()
	ON_WM_DRAWITEM()
	ON_WM_CONTEXTMENU()
	ON_WM_TIMER()
	ON_WM_DESTROY()

	ON_BN_CLICKED(IDC_BTN_ADDFOLDER, &CRuliManagerDlg::OnBnClickedAddFolder)
	ON_BN_CLICKED(IDC_BTN_REMOVEFOLDER, &CRuliManagerDlg::OnBnClickedRemoveFolder)
	ON_BN_CLICKED(IDC_BTN_REFRESH, &CRuliManagerDlg::OnBnClickedRefresh)
	ON_BN_CLICKED(IDC_BTN_RESCAN, &CRuliManagerDlg::OnBnClickedRescan)
	ON_BN_CLICKED(IDC_BTN_EXPORTTXT, &CRuliManagerDlg::OnBnClickedExportTxt)
	ON_BN_CLICKED(IDC_BTN_ACTORS, &CRuliManagerDlg::OnBnClickedActors)
	ON_BN_CLICKED(IDC_BTN_SETTINGS, &CRuliManagerDlg::OnBnClickedSettings)
	ON_BN_CLICKED(IDC_BTN_CHANGEIMAGE, &CRuliManagerDlg::OnBnClickedChangeImage)
	ON_BN_CLICKED(IDC_BTN_SEARCHIMAGE, &CRuliManagerDlg::OnBnClickedSearchImage)
	ON_BN_CLICKED(IDC_BTN_PICKACTORS, &CRuliManagerDlg::OnBnClickedPickActors)
	ON_STN_CLICKED(IDC_STATIC_RATING_LBL, &CRuliManagerDlg::OnStnClickedRatingLabel)
	ON_BN_CLICKED(IDC_BTN_PICKSTUDIO, &CRuliManagerDlg::OnBnClickedPickStudio)
	ON_BN_CLICKED(IDC_BTN_PICKTAGS, &CRuliManagerDlg::OnBnClickedPickTags)
	ON_BN_CLICKED(IDC_BTN_PICKVALIASES, &CRuliManagerDlg::OnBnClickedPickVAliases)
	ON_COMMAND(ID_MANAGE_ACTORS, &CRuliManagerDlg::OnManageActors)
	ON_COMMAND(ID_MANAGE_STUDIOS, &CRuliManagerDlg::OnManageStudios)
	ON_COMMAND(ID_MANAGE_TAGS, &CRuliManagerDlg::OnManageTags)
	ON_COMMAND(ID_MANAGE_SERIES, &CRuliManagerDlg::OnManageSeries)
	ON_COMMAND(ID_MANAGE_FILES, &CRuliManagerDlg::OnManageFiles)
	ON_BN_CLICKED(IDC_BTN_ACTOR_BACK, &CRuliManagerDlg::OnBnClickedActorBack)
	ON_BN_CLICKED(IDC_DRILL_VIEW_GRID, &CRuliManagerDlg::OnBnClickedDrillViewGrid)
	ON_BN_CLICKED(IDC_DRILL_VIEW_YEAR, &CRuliManagerDlg::OnBnClickedDrillViewYear)
	ON_BN_CLICKED(IDC_STUDIO_VIEW_GRID, &CRuliManagerDlg::OnBnClickedStudioViewGrid)
	ON_BN_CLICKED(IDC_STUDIO_VIEW_LIST, &CRuliManagerDlg::OnBnClickedStudioViewList)
	ON_BN_CLICKED(IDC_VIDEO_VIEW_H, &CRuliManagerDlg::OnBnClickedVideoViewH)
	ON_BN_CLICKED(IDC_VIDEO_VIEW_V, &CRuliManagerDlg::OnBnClickedVideoViewV)
	ON_BN_CLICKED(IDC_VIDEO_MODE_GRID, &CRuliManagerDlg::OnBnClickedVideoModeGrid)
	ON_BN_CLICKED(IDC_VIDEO_MODE_LIST, &CRuliManagerDlg::OnBnClickedVideoModeList)
	ON_BN_CLICKED(IDC_VIDEO_MODE_PACKAGE, &CRuliManagerDlg::OnBnClickedVideoModePackage)
	ON_BN_CLICKED(IDC_VIDEO_MODE_WALL, &CRuliManagerDlg::OnBnClickedVideoModeWall)
	ON_BN_CLICKED(IDC_VIDEO_MODE_LABEL, &CRuliManagerDlg::OnBnClickedVideoModeLabel)
	ON_NOTIFY(NM_CLICK, IDC_LIST_VIDEOS, &CRuliManagerDlg::OnNmClickList)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_LIST_VIDEOS, &CRuliManagerDlg::OnNmCustomDrawList)
	ON_COMMAND_RANGE(IDC_ACTOR_GENDER_ALL, IDC_ACTOR_GENDER_M, &CRuliManagerDlg::OnGenderButton)
	ON_COMMAND_RANGE(IDC_ACTOR_VIEW_GRID, IDC_ACTOR_VIEW_TREEMAP, &CRuliManagerDlg::OnActorViewButton)
	ON_CBN_SELCHANGE(IDC_COMBO_TREEMAP_BASIS, &CRuliManagerDlg::OnCbnSelchangeTreemapBasis)
	ON_BN_CLICKED(IDC_GENRE_HIDE, &CRuliManagerDlg::OnBnClickedGenreHide)
	ON_BN_CLICKED(IDC_BTN_COPYCODE, &CRuliManagerDlg::OnBnClickedCopyCode)
	ON_BN_CLICKED(IDC_DETAIL_BAR, &CRuliManagerDlg::OnBnClickedDetailBar)
	ON_BN_CLICKED(IDC_GENRE_ALL, &CRuliManagerDlg::OnBnClickedGenreAll)
	ON_COMMAND(ID_ACTOR_SHOWVIDEOS, &CRuliManagerDlg::OnActorShowVideos)
	ON_COMMAND(ID_ACTOR_EDIT, &CRuliManagerDlg::OnActorEdit)
	ON_COMMAND(ID_CAT_DELETE, &CRuliManagerDlg::OnCatDelete)
	ON_COMMAND(ID_CAT_MERGE, &CRuliManagerDlg::OnCatMerge)
	ON_COMMAND(ID_ACTOR_DELETE, &CRuliManagerDlg::OnActorDelete)
	ON_COMMAND(ID_ACTOR_MERGE, &CRuliManagerDlg::OnActorMerge)
	ON_COMMAND(ID_ACTOR_DELETECHECKED, &CRuliManagerDlg::OnActorDeleteChecked)
	ON_COMMAND(ID_ACTOR_UNCHECKALL, &CRuliManagerDlg::OnActorUncheckAll)
	ON_COMMAND(ID_CAT_DELETECHECKED, &CRuliManagerDlg::OnCatDeleteChecked)
	ON_COMMAND(ID_CAT_UNCHECKALL, &CRuliManagerDlg::OnCatUncheckAll)
	ON_COMMAND(ID_VIDEO_DELETECHECKED, &CRuliManagerDlg::OnVideoDeleteChecked)
	ON_COMMAND(ID_VIDEO_UNCHECKALL, &CRuliManagerDlg::OnVideoUncheckAll)
	ON_COMMAND(ID_CHECK_ALL, &CRuliManagerDlg::OnCheckAll)
	ON_COMMAND(ID_ACTOR_MERGECHECKED, &CRuliManagerDlg::OnActorMergeChecked)
	ON_COMMAND(ID_VIDEO_TEXTINFO, &CRuliManagerDlg::OnVideoTextInfo)
	ON_COMMAND(ID_VIDEO_PASTEINFO, &CRuliManagerDlg::OnVideoPasteInfo)
	ON_COMMAND(ID_VIDEO_SCREENSHOT, &CRuliManagerDlg::OnVideoScreenshot)
	ON_COMMAND(ID_VIDEO_REFRESHITEM, &CRuliManagerDlg::OnVideoRefreshItem)
	ON_MESSAGE(WM_APP_SHOT_PROGRESS, &CRuliManagerDlg::OnShotProgress)
	ON_MESSAGE(WM_APP_SHOT_DONE, &CRuliManagerDlg::OnShotDone)
	ON_COMMAND(ID_VIDEO_MAKEPREVIEW, &CRuliManagerDlg::OnVideoMakePreview)
	ON_COMMAND(ID_VIDEO_MAKEPREVIEWCHECKED, &CRuliManagerDlg::OnVideoMakePreviewChecked)
	ON_MESSAGE(WM_APP_PREVIEW_DONE, &CRuliManagerDlg::OnPreviewDone)
	ON_COMMAND(ID_NEW_ACTOR, &CRuliManagerDlg::OnNewActor)
	ON_COMMAND(ID_NEW_STUDIO, &CRuliManagerDlg::OnNewStudio)
	ON_COMMAND(ID_NEW_LABEL, &CRuliManagerDlg::OnNewLabel)
	ON_COMMAND(ID_NEW_TAG, &CRuliManagerDlg::OnNewTag)
	ON_COMMAND(ID_ACTOR_TEXTINFO, &CRuliManagerDlg::OnActorTextInfo)
	ON_COMMAND(ID_ACTOR_PASTEINFO, &CRuliManagerDlg::OnActorPasteInfo)
	ON_BN_CLICKED(IDC_BTN_SAVE, &CRuliManagerDlg::OnBnClickedSave)
	ON_BN_CLICKED(IDC_BTN_RENAME, &CRuliManagerDlg::OnBnClickedRename)
	ON_BN_CLICKED(IDC_BTN_DELETE, &CRuliManagerDlg::OnBnClickedDelete)
	ON_BN_CLICKED(IDC_BTN_EXPLORER, &CRuliManagerDlg::OnBnClickedExplorer)
	ON_BN_CLICKED(IDC_BTN_OPENDB, &CRuliManagerDlg::OnBnClickedOpenDb)
	ON_BN_CLICKED(IDC_BTN_APPLYDB, &CRuliManagerDlg::OnBnClickedApplyDb)
	ON_BN_CLICKED(IDC_BTN_OPENDEFAULT, &CRuliManagerDlg::OnBnClickedOpenDefault)

	ON_EN_CHANGE(IDC_EDIT_SEARCH, &CRuliManagerDlg::OnEnChangeSearch)
	ON_CBN_SELCHANGE(IDC_COMBO_FILTER, &CRuliManagerDlg::OnCbnSelchangeFilter)
	ON_CBN_SELCHANGE(IDC_COMBO_SORT, &CRuliManagerDlg::OnCbnSelchangeSort)
	ON_BN_CLICKED(IDC_BTN_SORTDIR, &CRuliManagerDlg::OnBnClickedSortDir)
	ON_EN_CHANGE(IDC_EDIT_ACTORS, &CRuliManagerDlg::OnEnChangeActors)
	ON_EN_CHANGE(IDC_EDIT_VALIASES, &CRuliManagerDlg::OnEnChangeVAliases)
	ON_EN_CHANGE(IDC_EDIT_STUDIO, &CRuliManagerDlg::OnEnChangeStudio)
	ON_EN_CHANGE(IDC_EDIT_TAGS, &CRuliManagerDlg::OnEnChangeTags)
	ON_EN_CHANGE(IDC_EDIT_MEMO, &CRuliManagerDlg::OnDetailsChanged)
	ON_EN_CHANGE(IDC_NAMED_MEMO, &CRuliManagerDlg::OnNamedMemoChanged)
	ON_EN_KILLFOCUS(IDC_NAMED_MEMO, &CRuliManagerDlg::OnNamedMemoKillFocus)
	ON_EN_CHANGE(IDC_EDIT_CODE, &CRuliManagerDlg::OnDetailsChanged)
	ON_EN_CHANGE(IDC_EDIT_SERIES, &CRuliManagerDlg::OnDetailsChanged)
	ON_EN_CHANGE(IDC_EDIT_TITLE, &CRuliManagerDlg::OnDetailsChanged)
	ON_NOTIFY(DTN_DATETIMECHANGE, IDC_DATE_RELEASE, &CRuliManagerDlg::OnDtnReleaseChanged)
	ON_CBN_SELCHANGE(IDC_COMBO_CENSOR, &CRuliManagerDlg::OnCbnSelchangeCensor)

	ON_NOTIFY(LVN_GETDISPINFO, IDC_LIST_VIDEOS, &CRuliManagerDlg::OnLvnGetDispInfo)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST_VIDEOS, &CRuliManagerDlg::OnLvnItemChanged)
	ON_NOTIFY(LVN_COLUMNCLICK, IDC_LIST_VIDEOS, &CRuliManagerDlg::OnLvnColumnClick)
	ON_NOTIFY(LVN_KEYDOWN, IDC_LIST_VIDEOS, &CRuliManagerDlg::OnLvnKeyDown)
	ON_NOTIFY(LVN_BEGINLABELEDIT, IDC_LIST_VIDEOS, &CRuliManagerDlg::OnLvnBeginLabelEdit)
	ON_NOTIFY(LVN_ENDLABELEDIT, IDC_LIST_VIDEOS, &CRuliManagerDlg::OnLvnEndLabelEdit)
	ON_NOTIFY(NM_DBLCLK, IDC_LIST_VIDEOS, &CRuliManagerDlg::OnNmDblclkList)
	ON_NOTIFY(NM_DBLCLK, IDC_LIST_CATEGORY, &CRuliManagerDlg::OnNmDblclkCategory)
	ON_NOTIFY(LVN_KEYDOWN, IDC_LIST_CATEGORY, &CRuliManagerDlg::OnLvnCatKeyDown)

	ON_CONTROL_RANGE(BN_CLICKED, IDC_RADIO_VIDEO, IDC_RADIO_COLLECTION, &CRuliManagerDlg::OnModeChanged)
	ON_COMMAND(ID_COL_NEW, &CRuliManagerDlg::OnNewCollection)
	ON_COMMAND(ID_COL_ADD_NEW, &CRuliManagerDlg::OnCollectionAddNew)
	ON_COMMAND(ID_COL_REMOVE, &CRuliManagerDlg::OnCollectionRemove)
	ON_COMMAND_RANGE(ID_COL_ADD_FIRST, ID_COL_ADD_LAST, &CRuliManagerDlg::OnCollectionAdd)
	ON_CONTROL_RANGE(BN_CLICKED, IDC_PAGE_FIRST, IDC_PAGE_LAST, &CRuliManagerDlg::OnPageButton)
	ON_CBN_SELCHANGE(IDC_COMBO_PAGESIZE, &CRuliManagerDlg::OnCbnSelchangePageSize)
	ON_CONTROL_RANGE(BN_CLICKED, IDC_RADIO_LISTVIEW, IDC_RADIO_GRIDVIEW, &CRuliManagerDlg::OnViewStyleChanged)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST_CATEGORY, &CRuliManagerDlg::OnLvnCatItemChanged)
	ON_NOTIFY(LVN_COLUMNCLICK, IDC_LIST_CATEGORY, &CRuliManagerDlg::OnLvnCatColumnClick)
	ON_MESSAGE(WM_APP_REBUILD_CATS, &CRuliManagerDlg::OnRebuildCategories)
	ON_MESSAGE(WM_APP_CARD_MEDIA, &CRuliManagerDlg::OnCardMediaReady)
	ON_MESSAGE(WM_APP_COVER_READY, &CRuliManagerDlg::OnCoverReady)
END_MESSAGE_MAP()

// ---------------------------------------------------------------------------
// 초기화

BOOL CRuliManagerDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// 창 제목: 프로그램 이름 + 버전 (Version.h 에서 관리, 리소스 CAPTION 에는 버전을 적지 않음)
	SetWindowText(_T(APP_NAME_STR) _T(" v") _T(APP_VERSION_STR));
	// SFW: 배우 · 영상 이미지 모자이크 (설정 창) - 이미지를 읽기 전에
	SetSfwMosaic(AfxGetApp()->GetProfileInt(L"Settings", L"Sfw", 0) != 0);

	SetIcon(m_hIcon, TRUE);
	SetIcon(m_hIcon, FALSE);

	// 목록 컨트롤 (가상 목록: LVS_OWNERDATA)
	// 목록 컨트롤 (가상 목록) = 선택 관리 + 영상 탭 [리스트] 보기 (스프레드시트, 첫 열 체크 칸)
	m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_CHECKBOXES |
		LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_LABELTIP);
	m_list.SetCallbackMask(LVIS_STATEIMAGEMASK);   // 체크 칸 상태는 LVN_GETDISPINFO 로 (m_videoChecked)
	{
		// [리스트] 글꼴: 기본 글꼴보다 1pt 크게 (목록에 주면 열 머리글도 같은 글꼴)
		LOGFONT lf = {};
		GetFont()->GetLogFont(&lf);
		CClientDC sdc(this);
		const int dpi = (std::max)(1, sdc.GetDeviceCaps(LOGPIXELSY));
		const double pt = std::abs(lf.lfHeight) * 72.0 / dpi;
		lf.lfHeight = -static_cast<LONG>(std::lround((pt + 1.0) * dpi / 72.0));
		if (m_listFont.CreateFontIndirect(&lf))
			m_list.SetFont(&m_listFont);
		// 품번: 목록 글꼴보다 1pt 더 크게 + 굵게
		lf.lfHeight = -static_cast<LONG>(std::lround((pt + 2.0) * dpi / 72.0));
		lf.lfWeight = FW_BOLD;
		m_listCodeFont.CreateFontIndirect(&lf);
	}
	{
		// 첫 열: 체크 칸 + 커버 이미지 (줄 높이는 빈 작은 아이콘 목록의 높이로 - 커버 3:2)
		m_listCoverH = (std::max)(64, DY(60));   // 예전 DY(30) 의 2배
		m_listCoverW = m_listCoverH * 3 / 2;
		m_listRowImages.Create(1, m_listCoverH, ILC_COLOR32, 1, 1);
		m_list.SetImageList(&m_listRowImages, LVSIL_SMALL);
		const int stateW = ::GetSystemMetrics(SM_CXSMICON) + 6;   // 체크 칸 자리
		m_list.InsertColumn(LC_COVER, L"", LVCFMT_LEFT, stateW + m_listCoverW + 8);
	}
	m_list.InsertColumn(LC_CODE,     L"품번",          LVCFMT_LEFT,  DX(62));
	m_list.InsertColumn(LC_RELEASE,  L"날짜",          LVCFMT_LEFT,  DX(48));
	m_listStarBandH = DY(13);   // 상세 별점 컨트롤 높이 (rowH)
	m_list.InsertColumn(LC_RATING,   L"별점",          LVCFMT_LEFT,  CStarRatingCtrl::StarsWidth(m_listStarBandH, 5) + DX(18) + 8);   // 별 5개 + 점수 글자
	m_list.InsertColumn(LC_DURATION, L"길이",          LVCFMT_RIGHT, DX(36));
	m_list.InsertColumn(LC_STUDIO,   L"제작사 / 레이블", LVCFMT_LEFT, DX(100));
	m_list.InsertColumn(LC_ACTORS,   L"배우",          LVCFMT_LEFT,  DX(110));
	m_list.InsertColumn(LC_TAGS,     L"태그",          LVCFMT_LEFT,  DX(140));
	if (CHeaderCtrl* hdr = m_list.GetHeaderCtrl())
	{
		m_listHeader.SubclassWindow(hdr->GetSafeHwnd());   // 어두운 열 머리글 (정렬 삼각형 포함)
		m_listHeader.SetBold(true);                        // 첫 줄(열 제목)은 굵게
	}
	CDarkDialogTheme::ThemeChild(m_list.GetSafeHwnd());     // 어두운 선택 표시 · 체크 칸

	// 별점 필터
	m_comboFilter.AddString(L"전체");
	for (int i = 1; i <= 5; ++i)
	{
		CString s;
		s.Format(L"%s 이상", static_cast<LPCWSTR>(RatingText(i * 2)));
		m_comboFilter.AddString(s);
	}
	m_comboFilter.SetCurSel(0);

	// 페이지당 개수 (각 탭 목록 위 도구줄) - 탭마다 따로 저장
	{
		const int sizes[] = { 0, 50, 100, 250, 500, 1000 };
		for (int i = 0; i < _countof(sizes); ++i)
		{
			CString s;
			if (sizes[i] == 0) s = L"전체"; else s.Format(L"%d", sizes[i]);
			m_comboPageSize.AddString(s);
			m_comboPageSize.SetItemData(i, static_cast<DWORD_PTR>(sizes[i]));
		}
		const int common = AfxGetApp()->GetProfileInt(L"Settings", L"PageSize", 250);   // 예전 (탭 공통) 값
		for (int t = 0; t < MODE_COUNT; ++t)
		{
			CString key;
			key.Format(L"PageSize%d", t);
			const int v = AfxGetApp()->GetProfileInt(L"Settings", key, common);
			bool known = false;
			for (int sz : sizes) if (sz == v) known = true;
			m_pageSizes[t] = known ? v : 250;
		}
		{
			// 배우 출연작 화면 / 제작사 영상 화면 (따로 저장)
			const struct { int ctx; LPCWSTR key; int def; } drills[] = {
				{ 1, L"ActorDrillPageSize", m_pageSizes[MODE_ACTOR] }, { 2, L"StudioDrillPageSize", m_pageSizes[MODE_STUDIO] } };
			for (const auto& d : drills)
			{
				const int v = AfxGetApp()->GetProfileInt(L"Settings", d.key, d.def);
				bool known = false;
				for (int sz : sizes) if (sz == v) known = true;
				m_drillPageSizes[d.ctx] = known ? v : 250;
			}
		}
	}
	m_editSearch.SetCueBanner(L"검색...");   // [검색] 글자 대신 안내 문구

	// 별점 선택: 마우스를 올리면 별 미리 보기, 클릭하면 확정 + 바로 저장
	m_starRating.Create(this, IDC_COMBO_RATING, 5);
	m_starRating.SetWindowPos(GetDlgItem(IDC_STATIC_RATING_LBL), 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);   // 탭 순서: 별점 라벨 다음
	m_starRating.SetColors(kBackColor, RGB(0xFF, 0xC8, 0x1E), RGB(0xE6, 0xEA, 0xEE), kTextColor);
	GetDlgItem(IDC_STATIC_RATING_LBL)->ModifyStyle(0, SS_NOTIFY);   // [별점] 라벨 클릭 → 0점으로 리셋

	// 배우 출연작 화면 위쪽 (고정): 배우 사진 + 정보 (별 · 하트도 동작)
	m_drillPhoto.Create(L"", WS_CHILD | SS_NOTIFY, CRect(0, 0, 10, 10), this, IDC_DRILL_PHOTO);
	m_drillPhoto.SetBackColor(kBackColor);
	m_drillPhoto.m_onDblClick = [this]()   // 배우 출연작 화면 위 배우 사진 더블클릭: 원본 이미지 보기 창
	{
		if (m_drillActorIdx >= 0 && m_drillActorIdx < static_cast<int>(m_lib.actors.size()))
			OpenImageViewerFile(m_drillPhoto.GetImageFile(), m_lib.actors[m_drillActorIdx].name);
	};
	// 컬렉션 영상 화면 위쪽 (고정): 커버 + 제목 · 영상 수 · 메모 (더블클릭 = 컬렉션 편집)
	m_colCover.Create(L"", WS_CHILD | SS_NOTIFY, CRect(0, 0, 10, 10), this, IDC_COL_HDR_COVER);
	m_colCover.SetBackColor(kBackColor);
	m_colCover.m_onDblClick = [this]() { OnNamedHeaderEdit(); };
	m_colInfo.Create(this, IDC_COL_HDR_INFO);
	m_colInfo.SetColors(kBackColor, kTextColor, TCOL(TC_SUB2));
	m_colInfo.m_onDblClick = [this]() { OnNamedHeaderEdit(); };
	m_colInfo.m_onSubDblClick = [this](int i)   // 제작사 영상 화면 위 하위 레이블 칸 더블클릭 → 그 레이블의 영상
	{
		if (i >= 0 && i < static_cast<int>(m_headerLabels.size()))
		{
			const CString name = m_headerLabels[i];   // 복사 (화면이 바뀌면 목록도 바뀜)
			OpenNamedTarget(name, !m_headerSubIsStudio);   // 하위 레이블 → 레이블 영상, 상위 제작사 → 제작사 영상
		}
	};
	m_drillPanel.Create(this, IDC_DRILL_PANEL);
	m_drillPanel.SetColors(kBackColor, kTextColor, TCOL(TC_SUB2));
	m_drillPanel.SetShowHistory(false);   // 출연작 화면 위 정보에는 내역(별칭 변경 이력) 없음
	m_drillPanel.SetShowMemo(false);      // 메모도 표시하지 않음
	m_drillPanel.SetShowCount(false);     // 출연 수도 표시하지 않음
	m_drillPanel.m_onRating = [this](int r)
	{
		if (m_drillActorIdx < 0 || m_drillActorIdx >= static_cast<int>(m_lib.actors.size()))
			return;
		m_lib.actors[m_drillActorIdx].rating = r;
		m_lib.Save();
		m_actorGrid.Invalidate(FALSE);
		UpdateDrillHeader();
	};
	m_drillPanel.m_onFavorite = [this]() { ToggleActorFavorite(m_drillActorIdx); UpdateDrillHeader(); };

	// 배우 탭 오른쪽 아래 상세: 별을 누르면 배우 별점 저장 (카드 리본 갱신), 하트는 즐겨찾기 전환
	m_actorPanel.Create(this, IDC_ACTOR_PANEL);
	m_actorPanel.SetColors(kBackColor, kTextColor, TCOL(TC_SUB2));
	m_actorPanel.m_onRating = [this](int r)
	{
		if (m_actorInfoIdx < 0 || m_actorInfoIdx >= static_cast<int>(m_lib.actors.size()))
			return;
		m_lib.actors[m_actorInfoIdx].rating = r;
		m_lib.Save();
		m_actorGrid.Invalidate(FALSE);   // 카드의 별점 리본
	};
	m_actorPanel.m_onFavorite = [this]() { ToggleActorFavorite(m_actorInfoIdx); };
	m_actorPanel.m_onDebutDblClick = [this]()
	{
		// 데뷔작 품번 상자 더블클릭 → 그 배우의 출연작 화면으로 들어가서 데뷔작 선택
		if (!IsActorGridMode() || m_actorInfoIdx < 0 || m_actorInfoIdx >= static_cast<int>(m_lib.actors.size()))
			return;
		const ActorInfo a = m_lib.actors[m_actorInfoIdx];
		const int item = m_lib.FindVideoOnDate(a.name, a.debut);
		if (item < 0 || m_actorSel < 0)
			return;
		DrillIntoActor(m_actorSel);
		for (size_t row = 0; row < m_view.size(); ++row)
		{
			if (m_view[row] == item)
			{
				SelectViewRow(static_cast<int>(row));
				break;
			}
		}
	};
	// 영상 상세 정보: 태그 아래 출연 배우 카드
	// 영상 상세 정보: [이미지 변경] 버튼 위 fps / 해상도
	m_mediaInfo.Create(this, IDC_MEDIA_INFO);
	m_mediaInfo.SetColors(kBackColor, TCOL(TC_SUB), TCOL(TC_BRIGHT));
	m_actorStrip.Create(this, IDC_ACTOR_STRIP);
	// 영상 스크린샷 격자 (설정에서 고르면 출연 배우 카드 자리에)
	m_detailStripMode = (AfxGetApp()->GetProfileInt(L"Settings", L"DetailStrip", 0) == 1) ? 1 : 0;
	m_mergeParts = AfxGetApp()->GetProfileInt(L"Settings", L"MergeParts", 0) != 0;
	{
		// 별점 체계 (설정 창): 0 = 1점 · 5점 만점, 1 = 0.5점 · 5점 만점, 2 = 1점 · 10점 만점 (처음에는 예전 RatingHalf 값)
		const int oldHalf = AfxGetApp()->GetProfileInt(L"Settings", L"RatingHalf", 0) != 0 ? 1 : 0;
		CStarRatingCtrl::SetRatingMode(AfxGetApp()->GetProfileInt(L"Settings", L"RatingMode", oldHalf));
	}
	CVideoLibrary::SetCensorLang(AfxGetApp()->GetProfileInt(L"Settings", L"CensorLang", 0));          // 검열 정보 표시 언어 (설정 창)
	FillCensorCombo();
	m_detailLeft = AfxGetApp()->GetProfileInt(L"Settings", L"DetailLeft", 0) != 0;
	m_detailCollapsed = AfxGetApp()->GetProfileInt(L"Settings", L"DetailCollapsed", 0) != 0;   // 상세 화면 접힘 (접기 바)
	m_shotCols = (std::max)(1, (std::min)(10, static_cast<int>(AfxGetApp()->GetProfileInt(L"Settings", L"ShotCols", 3))));
	m_shotStrip.Create(this, IDC_SHOT_STRIP);
	m_shotStrip.SetColors(kBackColor, TCOL(TC_SUB2), TCOL(TC_THUMB), kScrollColor);
	m_shotStrip.SetEmptyText(L"스크린샷 없음 (영상 폴더 안 Screenshot 폴더의 이미지)");
	m_shotStrip.SetWrap(true);
	m_shotStrip.ShowWindow(SW_HIDE);
	m_shotStrip.m_onDraw = [this](CDC* dc, int i, const CRect& rc, bool hot) { DrawShotCard(dc, i, rc, hot); };
	m_shotStrip.m_onActivate = [this](int i)   // 더블클릭: 원본 이미지 보기 창
	{
		// 여러 장 모드: 좌우 끝 < / > 로 같은 영상의 다른 스크린샷
		if (i >= 0 && i < static_cast<int>(m_shotFiles.size()) && !m_imageViewer.OpenList(this, m_shotFiles, i, true))
			AfxMessageBox(L"이미지 파일을 열 수 없습니다.", MB_ICONWARNING);
	};
	m_shotStrip.m_onTip = [this](int i) -> CString
	{
		return (i >= 0 && i < static_cast<int>(m_shotFiles.size())) ? CString(::PathFindFileNameW(m_shotFiles[i])) : CString();
	};
	m_actorStrip.SetColors(kBackColor, TCOL(TC_SUB2), TCOL(TC_THUMB), kScrollColor);
	m_actorStrip.SetEmptyText(L"출연 배우 없음");
	m_actorStrip.m_onDraw = [this](CDC* dc, int i, const CRect& rc, bool hot) { DrawStripCard(dc, i, rc, hot); };
	m_actorStrip.m_onActivate = [this](int i) { OpenActorFromStrip(i); };
	// 카드 오른쪽 위 하트: 마우스 오버 = 반투명 회색 하트, 클릭 = 즐겨찾기 지정/해제
	m_actorStrip.m_onHitPart = [this](int, const CRect& card, CPoint pt) -> int
	{
		CRect heart = StripHeartRect(card);
		heart.InflateRect(2, 2);
		return heart.PtInRect(pt) ? 1 : 0;
	};
	m_actorStrip.m_onPartClick = [this](int i, int part)
	{
		if (part != 1 || i < 0 || i >= static_cast<int>(m_stripActors.size()))
			return;
		const int idx = m_stripActors[i];
		if (idx < 0 || idx >= static_cast<int>(m_lib.actors.size()))
			return;
		m_lib.actors[idx].favorite = !m_lib.actors[idx].favorite;
		m_lib.Save();
		RebuildActorGrid();          // 배우 탭 정렬(즐겨찾기 먼저) 반영 + 카드 띠 다시 그림 (영상 상세 정보는 그대로)
		m_actorStrip.Invalidate(FALSE);
	};
	// 영상 상세: 품번 칸 오른쪽에 시리즈 라벨명
	m_staticCodeSeries.Create(L"", WS_CHILD | SS_OWNERDRAW, CRect(0, 0, 10, 10), this, IDC_STATIC_CODE_SERIES);   // 라벨(보통) + 설명(회색) 직접 그림
	m_staticCodeSeries.SetFont(GetFont());
	// 제작사 탭 상세: 메모 (여러 줄, 바로 편집)
	m_editNamedMemo.Create(WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
		CRect(0, 0, 100, 40), this, IDC_NAMED_MEMO);
	m_editNamedMemo.SetFont(GetFont());
	m_editNamedMemo.ModifyStyleEx(0, WS_EX_CLIENTEDGE, SWP_FRAMECHANGED);
	CDarkDialogTheme::ThemeChild(m_editNamedMemo.GetSafeHwnd());   // 어두운 스크롤바
	// 영상 상세: 제작사 / 레이블 카드 (한 장)
	m_studioCardW = DX(160);   // 폭은 LayoutControls 에서 상세 칸 폭으로 맞춤
	m_studioCardH = DY(32) * 4 / 5;   // 한 줄 카드: [이미지] 이름 (DY(40) → DY(32) → 다시 20% 줄임) - 제작사 · 레이블 카드 공용
	m_studioCard.Create(this, IDC_STUDIO_CARD);
	m_studioCard.SetColors(kBackColor, TCOL(TC_SUB2), TCOL(TC_THUMB), kScrollColor);
	m_studioCard.SetCardSize(m_studioCardW, m_studioCardH, DX(4));
	m_studioCard.SetCount(1);
	m_studioCard.m_onDraw = [this](CDC* dc, int, const CRect& rc, bool hot) { DrawVideoStudioCard(dc, rc, hot); };
	m_studioCard.m_onActivate = [this](int) { if (m_curItem >= 0 && !IsItemOffline(m_curItem)) OnBnClickedPickStudio(); };   // 더블클릭: 제작사 / 레이블 변경 (선택 창)
	m_studioCard.m_onHitPart = [this](int, const CRect& card, CPoint pt) -> int
	{
		CString studio;
		m_editStudio.GetWindowText(studio);
		studio.Trim();
		if (m_curItem < 0 || IsItemOffline(m_curItem) || studio.IsEmpty())
			return 0;
		CRect x = StudioCardXRect(card);
		x.InflateRect(2, 2);
		return x.PtInRect(pt) ? 1 : 0;   // × : 제작사 비우기
	};
	m_studioCard.m_onPartClick = [this](int, int part)
	{
		if (part == 1)
			ClearStudioCard(false);
	};
	m_studioCard.m_onTip = [this](int) -> CString
	{
		if (m_curItem < 0)
			return CString();
		CString studio;
		m_editStudio.GetWindowText(studio);
		studio.Trim();
		if (studio.IsEmpty())
			return L"더블클릭: 제작사 / 레이블 선택";
		CString t = L"제작사: " + studio;
		const std::vector<CString> chain = m_lib.StudioParentChain(studio);
		if (!chain.empty())
		{
			CString s;
			for (size_t k = chain.size(); k-- > 0;)
				s += chain[k] + (k > 0 ? L" › " : L"");
			t += L"\n상위 제작사: " + s;
		}
		return t + L"\n더블클릭: 변경 · 오른쪽 클릭: 영상 보기";
	};
	// 영상 상세: 레이블 카드 (제작사 카드 아래 따로 한 줄 - 제작사와 레이블을 나눠 표시)
	m_labelCard.Create(this, IDC_LABEL_CARD);
	m_labelCard.SetColors(kBackColor, TCOL(TC_SUB2), TCOL(TC_THUMB), kScrollColor);
	m_labelCard.SetCardSize(m_studioCardW, m_studioCardH, DX(4));
	m_labelCard.SetCount(1);
	m_labelCard.m_onDraw = [this](CDC* dc, int, const CRect& rc, bool hot) { DrawVideoStudioCard(dc, rc, hot, true); };
	m_labelCard.m_onActivate = [](int) {};   // 레이블은 수정 불가 (품번 DB 값 - 품번 관리에서 변경, 오른쪽 클릭 메뉴)
	m_labelCard.m_onHitPart = [this](int, const CRect& card, CPoint pt) -> int
	{
		(void)card; (void)pt;
		return 0;   // 레이블은 품번 DB 값 (× 로 비우기 없음)
	};
	m_labelCard.m_onPartClick = [this](int, int part)
	{
		if (part == 1)
			ClearStudioCard(true);
	};
	m_labelCard.m_onTip = [this](int) -> CString
	{
		if (m_curItem < 0)
			return CString();
		if (!LabelRowShown())
			return L"레이블 없음 (품번 DB 에 이 품번의 레이블이 없음)\n오른쪽 클릭: 품번 관리";
		return L"레이블: " + m_labelChips.Tags()[0] + L"\n(품번 DB 의 레이블 - 여기서는 수정 불가)\n오른쪽 클릭: 영상 보기 · 품번 관리";
	};
	// 제작사 탭 상세: 하위 레이블 카드 (여러 줄, 더블클릭 = 그 레이블 영상 보기)
	m_namedLinks.Create(this, IDC_NAMED_LINKS);
	m_namedLinks.SetColors(kBackColor, TCOL(TC_SUB2));
	m_labelStrip.Create(this, IDC_LABEL_STRIP);
	m_labelStrip.SetColors(kBackColor, TCOL(TC_SUB2), TCOL(TC_THUMB), kScrollColor);
	m_labelStrip.SetWrap(true);
	m_labelStrip.SetCardSize(DX(64), DX(64) * 9 / 16 + DY(13), DX(4));
	m_labelStrip.m_onDraw = [this](CDC* dc, int i, const CRect& rc, bool hot) { DrawLabelStripCard(dc, i, rc, hot); };
	m_labelStrip.m_onActivate = [this](int i)
	{
		if (i < 0 || i >= static_cast<int>(m_stripLabels.size()))
			return;
		const int code = m_stripLabels[i];
		const bool studio = (code < 0);
		const CString name = studio ? m_lib.studios[-code - 1].name : m_lib.labelInfos[code].name;
		OpenNamedTarget(name, !studio);   // 배우 상세에서도 제작사 탭으로 이동
	};
	m_labelStrip.m_onTip = [this](int i) -> CString
	{
		if (i < 0 || i >= static_cast<int>(m_stripLabels.size()))
			return CString();
		const int code = m_stripLabels[i];
		CString t;
		if (m_stripForActor)
		{
			// 배우 상세: 그 배우의 출연 편수
			const CString name = (code < 0) ? m_lib.studios[-code - 1].name : m_lib.labelInfos[code].name;
			const int cnt = (i < static_cast<int>(m_stripCounts.size())) ? m_stripCounts[i] : 0;
			t.Format(L"%s: %s\n출연 %d편 (더블클릭: %s 영상 보기)", (code < 0) ? L"제작사" : L"레이블",
				static_cast<LPCWSTR>(name), cnt, (code < 0) ? L"제작사" : L"레이블");
		}
		else if (code < 0)
		{
			const NamedInfo& st = m_lib.studios[-code - 1];
			const CString name = st.name;
			int cnt = 0;
			for (const VideoItem& v : m_lib.items)
				if (v.studio.CompareNoCase(name) == 0) ++cnt;
			const bool child = !m_stripOwner.IsEmpty() && st.parent.CompareNoCase(m_stripOwner) == 0;   // 고른 제작사의 하위 제작사
			t.Format(L"%s: %s\n영상 %d편 (더블클릭: 제작사 영상 보기)", child ? L"하위 제작사" : L"상위 제작사", static_cast<LPCWSTR>(name), cnt);
		}
		else
		{
			const CString name = m_lib.labelInfos[code].name;
			t.Format(L"%s\n영상 %d편 (더블클릭: 레이블 영상 보기)", static_cast<LPCWSTR>(name), m_lib.CountLabelVideos(name));
		}
		return t;
	};
	m_actorStrip.m_onTip = [this](int i) -> CString
	{
		if (i < 0 || i >= static_cast<int>(m_stripActors.size()))
			return CString();
		return m_lib.actors[m_stripActors[i]].name + L"\n(더블클릭: 배우 탭에서 보기)";
	};
	// 별점 오른쪽 물방울 카운트: 클릭 +1 / 오른쪽 클릭 → 초기화 (별점처럼 바로 저장)
	m_dropCounter.Create(this, IDC_DROP_COUNTER);
	m_dropCounter.SetWindowPos(&m_starRating, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);   // 탭 순서: 별점 다음
	m_dropCounter.SetColors(kBackColor, kTextColor, RGB(0x48, 0xAF, 0xF0));
	m_dropCounter.m_onChanged = [this](int)
	{
		if (m_curItem < 0)
			return;
		OnDetailsChanged();
		CommitDetails();
		m_grid.Invalidate(FALSE);   // 카드 아래 물방울 카운트
	};
	m_starRating.m_onChanged = [this](int)
	{
		if (m_curItem < 0)
			return;
		OnDetailsChanged();
		CommitDetails();
		m_grid.Invalidate(FALSE);   // 카드의 별점 리본
	};
	// 제목 칸: 여러 줄 에디트(자동 줄바꿈, 4줄 높이) - 여러 줄 에디트는 안내 문구(Cue Banner)를 지원하지 않음
	// 영상 메모 기능 삭제: 메모 칸 / 라벨은 항상 숨김
	m_editMemo.ShowWindow(SW_HIDE);
	GetDlgItem(IDC_STATIC_MEMO_LBL)->ShowWindow(SW_HIDE);
	m_editCode.SetCueBanner(L"예: ABC-123");

	// 영상 정렬 기준 (Column 순서와 같음)
	{
		const wchar_t* const sortNames[] = { L"이름", L"제목", L"크기", L"수정일", L"별점",
		                                     L"발매일", L"배우", L"제작사", L"태그", L"경로", L"배우수", L"품번", L"레이블", L"재생 시간", L"해상도" };   // 배우수 = 참여 배우 수 (SORT_ACTORCOUNT)
		for (const wchar_t* n : sortNames)
			m_comboSort.AddString(n);
		m_sortColumn = AfxGetApp()->GetProfileInt(L"Settings", L"SortColumn", COL_NAME);
		if (m_sortColumn < 0 || m_sortColumn >= static_cast<int>(_countof(sortNames)))
			m_sortColumn = COL_NAME;
		m_sortAsc = AfxGetApp()->GetProfileInt(L"Settings", L"SortAsc", 1) != 0;
		m_sortComboKind = 0;
		m_actorSortCol = AfxGetApp()->GetProfileInt(L"Settings", L"ActorSortColumn", 0);
		if (m_actorSortCol < 0 || m_actorSortCol > 1)
			m_actorSortCol = 0;
		m_actorSortAsc = AfxGetApp()->GetProfileInt(L"Settings", L"ActorSortAsc", 1) != 0;
		m_colSortCol = AfxGetApp()->GetProfileInt(L"Settings", L"CollectionSortColumn", 0);
		if (m_colSortCol < 0 || m_colSortCol > 1)
			m_colSortCol = 0;
		m_colSortAsc = AfxGetApp()->GetProfileInt(L"Settings", L"CollectionSortAsc", 1) != 0;
		m_tagSortCol = AfxGetApp()->GetProfileInt(L"Settings", L"TagSortColumn", 0);
		if (m_tagSortCol < 0 || m_tagSortCol > 1)
			m_tagSortCol = 0;
		m_tagSortAsc = AfxGetApp()->GetProfileInt(L"Settings", L"TagSortAsc", 1) != 0;
		m_studioSortCol = AfxGetApp()->GetProfileInt(L"Settings", L"StudioSortColumn", 0);
		if (m_studioSortCol < 0 || m_studioSortCol > 1)
			m_studioSortCol = 0;
		m_studioSortAsc = AfxGetApp()->GetProfileInt(L"Settings", L"StudioSortAsc", 1) != 0;
	}

	m_editSearch.SetCueBanner(L"이름 · 제목 · 배우(별칭) · 제작사 · 태그 · 메모 검색");
	m_editActors.SetCueBanner(L"입력하면 배우 검색 (↓ 목록) · [선택...]");
	m_editVAliases.SetCueBanner(L"이 작품에서 쓴 배우 별칭 (입력 / ↓ 목록)");
	m_dateRelease.SetFormat(L"yyyy-MM-dd");
	m_editStudio.SetCueBanner(L"입력하면 제작사 검색 (↓ 목록)");
	m_editTags.SetCueBanner(L"입력하면 태그 검색 (↓ 목록) · 여러 개는 쉼표");
	SetupSuggestions();

	// 미리보기 이미지 영역
	m_preview.SetPlaceholder(L"동영상을 선택하세요.");
	m_preview.ModifyStyle(0, SS_NOTIFY);   // 마우스(더블클릭)를 받도록
	m_preview.m_onDblClick = [this]() { OpenImageViewer(); };   // 영상 상세: 원본 이미지 보기 창
	SetTimer(kPreviewHoverTimer, 60, nullptr);   // 영상 카드 마우스 오버 → 미리보기 팝업 (CheckPreviewHover)
	// 탐색기에서 이미지 파일을 끌어다 놓기 (영상 상세의 이미지 영역) - 관리자 권한으로 실행해도 받도록 메시지 허용
	DragAcceptFiles(TRUE);
	::ChangeWindowMessageFilterEx(m_hWnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
	::ChangeWindowMessageFilterEx(m_hWnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
	::ChangeWindowMessageFilterEx(m_hWnd, 0x0049 /* WM_COPYGLOBALDATA */, MSGFLT_ALLOW, nullptr);

	ApplyColors();

	// 분류 목록 (배우 / 스튜디오 / 태그)
	m_listCat.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
	m_listCat.InsertColumn(0, L"배우", LVCFMT_LEFT, DX(60));
	m_listCat.InsertColumn(1, L"영상 수", LVCFMT_RIGHT, DX(30));
	m_listCat.InsertColumn(2, L"메모", LVCFMT_LEFT, DX(100));

	// 보기 모드 복원
	m_mode = AfxGetApp()->GetProfileInt(L"Settings", L"ViewMode", MODE_VIDEO);
	if (m_mode < MODE_VIDEO || m_mode >= MODE_COUNT)
		m_mode = MODE_VIDEO;
	ApplyTabPageSize();
	UpdateModeButtons();
	UpdateCategoryHeader();

	// 탭별 확대 단계 복원
	for (int i = 0; i < 7; ++i)
	{
		CString keyName;
		keyName.Format(L"Zoom%d", i);
		const int def = (i == 4) ? m_zoom[MODE_ACTOR] : (i == 5 ? m_zoom[MODE_STUDIO] : 0);   // 출연작 / 제작사 영상 화면: 처음에는 목록 값
		m_zoom[i] = (std::max)(0, (std::min)(3, static_cast<int>(AfxGetApp()->GetProfileInt(L"Settings", keyName, def))));
	}

	// 격자 보기용 썸네일 이미지 목록 (DPI에 맞춰 크기 조정)
	{
		UINT dpi = ::GetDpiForWindow(m_hWnd);
		if (dpi == 0) dpi = 96;
		m_thumbW = MulDiv(160, dpi, 96);
		m_thumbH = MulDiv(120, dpi, 96);
		m_thumbs.Create(m_thumbW, m_thumbH, ILC_COLOR24, 64, 64);
		AddThumbnail(nullptr, L"이미지 없음");
		m_grid.Create(this, IDC_GRID, this, &m_thumbs, m_thumbW, m_thumbH);
		// 배우 출연작 화면 [연도별] 보기 + [격자] [연도별] 전환 버튼 ([◀ 배우 목록] 줄 오른쪽)
		m_actorDrillView = (AfxGetApp()->GetProfileInt(L"Settings", L"ActorDrillView", 0) == 1) ? 1 : 0;
		m_yearView.Create(this, IDC_YEAR_VIEW);
		m_yearView.SetColors(kBackColor, kTextColor, TCOL(TC_SUB2), TCOL(TC_COMBO));
		m_yearView.ShowWindow(SW_HIDE);
		m_yearView.m_onDrawCard = [this](CDC* dc, int item, const CRect& rc, bool sel)
		{
			m_cardImageOnly = true;   // 이미지만 (고정 크기 - 확대 슬라이더와 관계없음)
			const auto cit = m_yearCardColors.find(item);
			m_cardFill = (cit != m_yearCardColors.end()) ? cit->second : CLR_INVALID;   // 커버 빈 영역 = 배우 이름별 고유색
			DrawVideoCard(dc, item, rc, sel, GetFocus() == &m_yearView);
			m_cardImageOnly = false;
			m_cardFill = CLR_INVALID;
		};
		m_yearView.m_onSelect = [this](int item) { SelectViewRow(item); };
		m_yearView.m_onActivate = [this](int item) { GridActivate(item); };
		m_yearView.m_onDblClickAt = [this](int item, const CRect& card, CPoint pt)   // 스크린샷 추출 표시 더블클릭 = 멈춤 / 대기열에서 뺌
		{
			m_cardImageOnly = true;   // ShotBadgeRect: 연도별 카드 배치
			const bool handled = GridDblClick(item, card, pt);
			m_cardImageOnly = false;
			return handled;
		};
		m_yearView.m_onContext = [this](int item, CPoint pt) { if (item >= 0) ShowVideoContextMenu(item, pt); };
		m_yearView.m_onKey = [this](UINT vk) { HandleListKey(vk); };   // F5 새로고침 · Del · F2 · Space · Backspace · Ctrl+F (격자와 같게)
		{
			const wchar_t* labels[2] = { L"격자", L"연도별" };
			for (int i = 0; i < 2; ++i)
			{
				const UINT id = IDC_DRILL_VIEW_GRID + i;
				HWND h = ::CreateWindowExW(0, L"BUTTON", labels[i], WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10,
					m_hWnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), AfxGetInstanceHandle(), nullptr);
				if (!h)
					continue;
				::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(GetFont()->GetSafeHandle()), TRUE);
				if (m_drillViewBtns[i].Attach(id, this))
				{
					m_drillViewBtns[i].SetColors(kButtonColor, RGB(255, 255, 255), kBackColor);
					m_drillViewBtns[i].SetToggle(kComboColor, TCOL(TC_TOGGLETEXT));
					m_drillViewBtns[i].SetChecked(i == m_actorDrillView);
					m_drillViewBtns[i].SetSegment(i == 0 ? CDarkButton::SEG_LEFT : CDarkButton::SEG_RIGHT);   // [격자|연도별] 한 묶음
					// 글자 대신 아이콘 (가로 칸 2×2 / 달력), 마우스를 올리면 이름
					m_drillViewBtns[i].SetIcon(i == 0 ? CDarkButton::ICON_VIEW_GRID : CDarkButton::ICON_VIEW_YEAR);
					m_drillViewBtns[i].SetTooltip(labels[i]);
				}
			}
		}
		{
			// 제작사 탭: [격자] [리스트] (목록 도구줄 - 정렬 오른쪽)
			m_studioView = (AfxGetApp()->GetProfileInt(L"Settings", L"StudioView", 0) == 1) ? 1 : 0;
			const wchar_t* labels[2] = { L"격자", L"리스트" };
			for (int i = 0; i < 2; ++i)
			{
				const UINT id = IDC_STUDIO_VIEW_GRID + i;
				HWND h = ::CreateWindowExW(0, L"BUTTON", labels[i], WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10,
					m_hWnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), AfxGetInstanceHandle(), nullptr);
				if (!h)
					continue;
				::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(GetFont()->GetSafeHandle()), TRUE);
				if (m_studioViewBtns[i].Attach(id, this))
				{
					m_studioViewBtns[i].SetColors(kButtonColor, RGB(255, 255, 255), kBackColor);
					m_studioViewBtns[i].SetToggle(kComboColor, TCOL(TC_TOGGLETEXT));
					m_studioViewBtns[i].SetChecked(i == m_studioView);
					m_studioViewBtns[i].SetSegment(i == 0 ? CDarkButton::SEG_LEFT : CDarkButton::SEG_RIGHT);   // [격자|리스트] 한 묶음
					// 글자 대신 아이콘 (영상 탭 [격자|리스트] 와 같은 그림), 마우스를 올리면 이름
					m_studioViewBtns[i].SetIcon(i == 0 ? CDarkButton::ICON_VIEW_GRID : CDarkButton::ICON_VIEW_LIST);
					m_studioViewBtns[i].SetTooltip(labels[i]);
				}
			}
		}
		{
			// 배우 탭: [선택없음] [여성만] [남성만] 성별 거르기 (목록 도구줄 - 정렬 오른쪽)
			m_actorGender = (std::max)(0, (std::min)(2, static_cast<int>(AfxGetApp()->GetProfileInt(L"Settings", L"ActorGender", 0))));
			const wchar_t* labels[3] = { L"선택없음", L"여성만", L"남성만" };
			for (int i = 0; i < 3; ++i)
			{
				const UINT id = IDC_ACTOR_GENDER_ALL + i;
				HWND h = ::CreateWindowExW(0, L"BUTTON", labels[i], WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10,
					m_hWnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), AfxGetInstanceHandle(), nullptr);
				if (!h)
					continue;
				::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(GetFont()->GetSafeHandle()), TRUE);
				if (m_genderBtns[i].Attach(id, this))
				{
					m_genderBtns[i].SetColors(kButtonColor, RGB(255, 255, 255), kBackColor);
					m_genderBtns[i].SetToggle(kComboColor, TCOL(TC_TOGGLETEXT));
					m_genderBtns[i].SetChecked(i == m_actorGender);
					m_genderBtns[i].SetSegment(i == 0 ? CDarkButton::SEG_LEFT : (i == 2 ? CDarkButton::SEG_RIGHT : CDarkButton::SEG_MIDDLE));   // 붙은 한 묶음
					// 글자 대신 성별 기호 (♀♂ 나란히 / ♀ / ♂), 마우스를 올리면 이름
					m_genderBtns[i].SetIcon(i == 0 ? CDarkButton::ICON_SYMBOL_ALL : (i == 1 ? CDarkButton::ICON_SYMBOL_F : CDarkButton::ICON_SYMBOL_M));
					m_genderBtns[i].SetTooltip(labels[i]);
				}
			}
		}
		{
			// 배우 탭: [격자] [트리맵] 보기 방식 + 트리맵 기준 콤보 [용량 | 영상수] (목록 도구줄 - 성별 오른쪽)
			CWinApp* app = AfxGetApp();
			m_actorView = (app->GetProfileInt(L"Settings", L"ActorViewMode", 0) == 1) ? 1 : 0;
			m_treemapBasis = (app->GetProfileInt(L"Settings", L"TreemapBasis", 0) == 1) ? 1 : 0;
			const wchar_t* labels[2] = { L"격자", L"트리맵 차트" };
			for (int i = 0; i < 2; ++i)
			{
				const UINT id = IDC_ACTOR_VIEW_GRID + i;
				HWND h = ::CreateWindowExW(0, L"BUTTON", labels[i], WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10,
					m_hWnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), AfxGetInstanceHandle(), nullptr);
				if (!h)
					continue;
				::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(GetFont()->GetSafeHandle()), TRUE);
				if (m_actorViewBtns[i].Attach(id, this))
				{
					m_actorViewBtns[i].SetColors(kButtonColor, RGB(255, 255, 255), kBackColor);
					m_actorViewBtns[i].SetToggle(kComboColor, TCOL(TC_TOGGLETEXT));
					m_actorViewBtns[i].SetChecked(i == m_actorView);
					m_actorViewBtns[i].SetSegment(i == 0 ? CDarkButton::SEG_LEFT : CDarkButton::SEG_RIGHT);   // [격자|트리맵] 한 묶음
					m_actorViewBtns[i].SetIcon(i == 0 ? CDarkButton::ICON_VIEW_GRID : CDarkButton::ICON_VIEW_TREEMAP);
					m_actorViewBtns[i].SetTooltip(labels[i]);
				}
			}
			if (m_comboTmBasis.GetSafeHwnd())
			{
				m_comboTmBasis.ResetContent();
				m_comboTmBasis.AddString(L"용량");
				m_comboTmBasis.AddString(L"영상수");
				m_comboTmBasis.SetCurSel(m_treemapBasis);
			}
		}
		{
			// 영상 탭: [격자] [리스트] 보기 방식 (목록 도구줄 - 정렬 오른쪽, [가로|세로] 앞)
			// 영상 탭: [격자] [리스트] [패키지] (예전 값: VideoList = 리스트, VideoPortrait = 격자 세로 → 패키지)
			// 화면별 (배우 출연작 · 제작사 · 태그 영상의 예전 [가로|세로] 값: 세로 → 패키지)
			CWinApp* app = AfxGetApp();
			static const wchar_t* vkeys[5] = { L"VideoViewMode", L"VideoViewModeActor", L"VideoViewModeStudio", L"VideoViewModeTag", L"VideoViewModeCollection" };
			static const wchar_t* oldPortrait[5] = { L"VideoPortrait", L"VideoPortraitActor", L"VideoPortraitStudio", L"VideoPortraitTag", nullptr };
			for (int s = 0; s < 5; ++s)
			{
				int vm = static_cast<int>(app->GetProfileInt(L"Settings", vkeys[s], -1));
				if (vm == 4 && s != 2)
					vm = 0;   // [레이블] 은 제작사 · 레이블 영상 화면만
				if (vm < 0 || vm > 4)
				{
					vm = (oldPortrait[s] && app->GetProfileInt(L"Settings", oldPortrait[s], 0) == 1) ? 2 : 0;
					if (s == 0 && app->GetProfileInt(L"Settings", L"VideoList", 0) == 1)
						vm = 1;
				}
				m_videoView[s] = vm;
			}
			const wchar_t* labels[5] = { L"격자", L"리스트", L"패키지", L"월 모드", L"레이블" };
			static const UINT modeIds[5] = { IDC_VIDEO_MODE_GRID, IDC_VIDEO_MODE_LIST, IDC_VIDEO_MODE_PACKAGE, IDC_VIDEO_MODE_WALL, IDC_VIDEO_MODE_LABEL };
			static const int modeIcons[5] = { CDarkButton::ICON_VIEW_GRID, CDarkButton::ICON_VIEW_LIST, CDarkButton::ICON_VIEW_PACKAGE, CDarkButton::ICON_VIEW_WALL, CDarkButton::ICON_VIEW_LABEL };
			for (int i = 0; i < 5; ++i)
			{
				const UINT id = modeIds[i];
				HWND h = ::CreateWindowExW(0, L"BUTTON", labels[i], WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10,
					m_hWnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), AfxGetInstanceHandle(), nullptr);
				if (!h)
					continue;
				::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(GetFont()->GetSafeHandle()), TRUE);
				if (m_videoModeBtns[i].Attach(id, this))
				{
					m_videoModeBtns[i].SetColors(kButtonColor, RGB(255, 255, 255), kBackColor);
					m_videoModeBtns[i].SetToggle(kComboColor, TCOL(TC_TOGGLETEXT));
					m_videoModeBtns[i].SetChecked(i == m_videoView[0]);   // 화면이 바뀌면 UpdateLeftPane 이 그 화면 값으로
					m_videoModeBtns[i].SetSegment(i == 0 ? CDarkButton::SEG_LEFT : (i >= 3 ? CDarkButton::SEG_RIGHT : CDarkButton::SEG_MIDDLE));   // [격자|리스트|패키지|월 모드(|레이블)] 한 묶음 - 월 모드는 레이블이 보이면 가운데 (UpdateLeftPane)
					// 글자 대신 아이콘 (가로 칸 2×2 / 썸네일 + 줄 / 세로 케이스 / 3×3 + 재생), 마우스를 올리면 이름
					m_videoModeBtns[i].SetIcon(modeIcons[i]);
					m_videoModeBtns[i].SetTooltip(labels[i]);
				}
			}
		}
		{
			// 영상 탭: [가로] [세로] 격자 (목록 도구줄 - 정렬 오른쪽)
			static const wchar_t* pkeys[4] = { L"VideoPortrait", L"VideoPortraitActor", L"VideoPortraitStudio", L"VideoPortraitTag" };
			for (int s = 0; s < 4; ++s)
				m_videoPortrait[s] = AfxGetApp()->GetProfileInt(L"Settings", pkeys[s], 0) == 1;
			const wchar_t* labels[2] = { L"가로", L"세로" };
			for (int i = 0; i < 2; ++i)
			{
				const UINT id = IDC_VIDEO_VIEW_H + i;
				HWND h = ::CreateWindowExW(0, L"BUTTON", labels[i], WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10,
					m_hWnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), AfxGetInstanceHandle(), nullptr);
				if (!h)
					continue;
				::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(GetFont()->GetSafeHandle()), TRUE);
				if (m_videoViewBtns[i].Attach(id, this))
				{
					m_videoViewBtns[i].SetColors(kButtonColor, RGB(255, 255, 255), kBackColor);
					m_videoViewBtns[i].SetToggle(kComboColor, TCOL(TC_TOGGLETEXT));
					m_videoViewBtns[i].SetChecked((i == 1) == m_videoPortrait[0]);   // 화면이 바뀌면 UpdateLeftPane 이 그 화면 값으로
					m_videoViewBtns[i].SetSegment(i == 0 ? CDarkButton::SEG_LEFT : CDarkButton::SEG_RIGHT);   // [가로|세로] 한 묶음
				}
			}
		}
		{
			// 영상 탭: [장르숨김] [전체] (목록 도구줄) + 태그 칩 줄 (도구줄 아래)
			m_genreShow[0] = AfxGetApp()->GetProfileInt(L"Settings", L"GenreBar", 0) == 1;
			m_genreShow[1] = AfxGetApp()->GetProfileInt(L"Settings", L"GenreBarActor", 0) == 1;
			m_genreShow[2] = AfxGetApp()->GetProfileInt(L"Settings", L"GenreBarStudio", 0) == 1;
			const wchar_t* labels[2] = { L"장르숨김", L"전체" };
			for (int i = 0; i < 2; ++i)
			{
				const UINT id = IDC_GENRE_HIDE + i;
				HWND h = ::CreateWindowExW(0, L"BUTTON", labels[i], WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10,
					m_hWnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), AfxGetInstanceHandle(), nullptr);
				if (!h)
					continue;
				::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(GetFont()->GetSafeHandle()), TRUE);
				if (m_genreBtns[i].Attach(id, this))
				{
					m_genreBtns[i].SetColors(kButtonColor, RGB(255, 255, 255), kBackColor);
					m_genreBtns[i].SetToggle(kComboColor, TCOL(TC_TOGGLETEXT));
					m_genreBtns[i].SetChecked((i == 1) == m_genreShow[0]);   // 화면이 바뀌면 UpdateLeftPane 이 그 화면 값으로
					m_genreBtns[i].SetSegment(i == 0 ? CDarkButton::SEG_LEFT : CDarkButton::SEG_RIGHT);   // [장르숨김|전체] 한 묶음
					// 글자 대신 아이콘 (빗금 꼬리표 / 꼬리표), 마우스를 올리면 이름
					m_genreBtns[i].SetIcon(i == 0 ? CDarkButton::ICON_GENRE_OFF : CDarkButton::ICON_GENRE_ON);
					m_genreBtns[i].SetTooltip(labels[i]);
				}
			}
			// 영상 상세: 품번 칸 오른쪽 [복사] 버튼 (아이콘만)
			if (HWND hc = ::CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10,
				m_hWnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_BTN_COPYCODE)), AfxGetInstanceHandle(), nullptr))
			{
				::SendMessageW(hc, WM_SETFONT, reinterpret_cast<WPARAM>(GetFont()->GetSafeHandle()), TRUE);
				if (m_btnCopyCode.Attach(IDC_BTN_COPYCODE, this))
				{
					m_btnCopyCode.SetColors(TCOL(TC_COMBO), RGB(255, 255, 255), kBackColor);
					m_btnCopyCode.SetIcon(CDarkButton::ICON_COPY);
				}
			}
			// 상세 화면 접기 바 (격자와 상세 화면 사이, 세로로 길게)
			if (HWND hb = ::CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 10, 10,
				m_hWnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_DETAIL_BAR)), AfxGetInstanceHandle(), nullptr))
			{
				::SendMessageW(hb, WM_SETFONT, reinterpret_cast<WPARAM>(GetFont()->GetSafeHandle()), TRUE);
				if (m_detailBar.Attach(IDC_DETAIL_BAR, this))
				{
					m_detailBar.SetColors(TCOL(TC_COMBO), TCOL(TC_SUB2), kBackColor);
					UpdateDetailBar();
				}
			}
			m_genreBar.Create(this, IDC_GENRE_CHIPS);
			m_genreBar.SetFontPtr(GetFont());
			m_genreBar.m_onSelect = [this](const CString& tag)
			{
				const int s = GenreSlot();
				if (s < 0)
					return;
				m_genreSel[s] = tag;
				CurPageRef() = 0;   // 첫 페이지부터
				ApplyFilter();
				m_grid.ScrollToTop();
			};
		}
		m_actorOwner.dlg = this;
		m_actorGrid.Create(this, IDC_ACTOR_GRID, &m_actorOwner, &m_thumbs, m_thumbW, m_thumbH);
		if (m_treemap.Create(&m_actorGrid, IDC_TREEMAP))   // 배우 [트리맵] 차트: 배우 격자의 자식 (도구줄 아래 전체)
		{
			m_treemap.SetColors(kBackColor, RGB(255, 255, 255));
			m_treemap.SetEmptyText(L"표시할 배우가 없습니다.");
			m_treemap.m_onClick = [this](int row) { if (row >= 0) SelectActorRow(row); };
			m_treemap.m_onActivate = [this](int row) { if (row >= 0) DrillIntoActor(row); };
			m_treemap.m_formatValue = [this](double v) -> CString
			{
				if (m_treemapBasis == 0)
					return FormatSize(static_cast<ULONGLONG>(v));
				CString s;
				s.Format(L"%.0f편", v);
				return s;
			};
		}
		SetupActorCards();   // 배우 탭: 세로 사진 카드
		m_catOwner.dlg = this;
		m_catGrid.Create(this, IDC_CAT_GRID, &m_catOwner, &m_thumbs, m_thumbW, m_thumbH);

		// 확대 슬라이더 (4단계)
		m_zoomSlider.Create(this, IDC_ZOOM_SLIDER, 4);
		m_zoomSlider.SetColors(kBackColor, kButtonColor, TCOL(TC_BORDER), TCOL(TC_SUB2));
		m_zoomSlider.SetPos(m_zoom[ZoomSlot()]);
		m_zoomSlider.m_onChanged = [this](int pos) { OnZoomChanged(pos); };

		// 태그 칩 입력 (예전 태그 입력 칸 / [선택...] 버튼 자리)
		m_tagChips.Create(this, IDC_TAG_CHIPS);
		m_tagChips.SetColors(kEditColor, TCOL(TC_LIGHT), TCOL(TC_DARK), TCOL(TC_SUB), kTextColor);
		m_tagChips.m_edit.SetColors(kEditColor, kTextColor, kButtonColor, TCOL(TC_HINT));
		m_tagChips.m_edit.Setup([this](std::vector<SuggestItem>& out)
		{
			for (const NamedInfo& n : m_lib.tagInfos)
			{
				bool used = false;   // 이미 붙은 태그는 후보에서 뺌
				for (const CString& tg : m_tagChips.Tags())
					if (tg.CompareNoCase(n.name) == 0) { used = true; break; }
				if (!used)
				{
					out.push_back({ n.name, n.name });
					for (const CString& alt : CVideoLibrary::SplitLines(n.subName))   // 다른 이름으로 입력해도 그 태그
						out.push_back({ alt + L"\t" + n.name + L"의 다른 이름", n.name });
					if (!n.nameEn.IsEmpty())
						out.push_back({ n.nameEn + L"\t" + n.name, n.name });   // 영어 · 일본어 이름으로 입력해도 그 태그
					if (!n.nameJa.IsEmpty())
						out.push_back({ n.nameJa + L"\t" + n.name, n.name });
				}
			}
		}, false);
		m_tagChips.m_onChanged = [this]()
		{
			// 칩 → 숨긴 태그 칸 (EN_CHANGE 로 변경 표시) - 새로 넣은 태그도 가나다 순 자리로, 다른 이름은 태그 이름으로
			std::vector<CString> sorted;
			for (const CString& t : m_tagChips.Tags())
			{
				const int n = m_lib.FindNamed(LIST_TAG, t);
				const CString name = (n >= 0) ? m_lib.tagInfos[n].name : t;
				if (std::find_if(sorted.begin(), sorted.end(), [&](const CString& o) { return o.CompareNoCase(name) == 0; }) == sorted.end())
					sorted.push_back(name);
			}
			SortTagsKo(sorted);
			if (sorted != m_tagChips.Tags())
				m_tagChips.SetTags(sorted);
			m_syncingTags = true;
			m_editTags.SetWindowText(CVideoLibrary::JoinList(sorted));
			m_syncingTags = false;
		};
		m_tagChips.m_onDropDown = [this]() { OnBnClickedPickTags(); };
		// [폴더 관리] 버튼 위에 마우스 → 등록 폴더 목록 팝업, 벗어나면 닫음
		m_folderPopup.CreatePopup(this);
		m_folderPopup.m_back = TCOL(TC_COMBO);
		m_folderPopup.m_onDraw = [this](CDC* dc, const CRect& rc) { DrawFolderPopup(dc, rc); };
		m_darkButtons[0].m_onHover = [this](bool in) { if (in) ShowFolderPopup(); else m_folderPopup.Hide(); };   // [0] = IDC_BTN_ADDFOLDER
		// 태그 칩 위에 마우스 → 그 태그 카드 팝업 (태그 탭 카드와 같은 모양), 벗어나면 닫음
		m_tagPopup.CreatePopup(this);
		m_tagPopup.m_back = kBackColor;
		m_tagPopup.m_onDraw = [this](CDC* dc, const CRect& rc) { DrawTagPopupCard(dc, rc, m_tagPopupName); };
		m_tagChips.m_onChipHover = [this](int index, const CRect& chipScreen)
		{
			if (index < 0 || index >= static_cast<int>(m_tagChips.Tags().size()))
			{
				m_tagPopup.Hide();
				return;
			}
			m_tagPopupName = m_tagChips.Tags()[index];
			// 태그 탭 카드 크기 (영상 카드 폭의 75%)
			const int w = m_vcardW * 3 / 4;
			const int h = m_cardPad * 2 + (w - m_cardPad * 2) * 9 / 16 + m_cardPad + m_cardLineB + m_cardPad / 2 + 1 + m_cardPad + m_cardLine + m_cardPad;
			m_tagPopup.ShowNear(chipScreen, CSize(w + 2, h + 2));
		};
		m_tagChips.m_onHeightChanged = [this]()
		{
			CRect client;
			GetClientRect(&client);
			LayoutControls(client.Width(), client.Height());
		};
		GetDlgItem(IDC_EDIT_TAGS)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_BTN_PICKTAGS)->ShowWindow(SW_HIDE);

		// 배우 칩 입력 (태그와 같은 모양, 이름 뒤에 별칭을 회색으로)
		m_actorChips.Create(this, IDC_ACTOR_CHIPS);
		m_actorChips.SetColors(kEditColor, TCOL(TC_LIGHT), TCOL(TC_DARK), TCOL(TC_SUB), kTextColor);
		m_actorChips.m_edit.SetColors(kEditColor, kTextColor, kButtonColor, TCOL(TC_HINT));
		m_actorChips.m_edit.SetCueBanner(L"배우 입력 (↓ 목록)");
		// 배우 + 별칭 병합: 칩 이름은 이 작품에서 고른 별칭(없으면 배우 이름), 회색은 대표 이름
		m_actorChips.m_displayText = [this](const CString& tag) { return ActorChipDisplay(tag); };
		// 배우 칩 왼쪽: 성별 기호 (♀ 분홍 / ♂ 파랑 / ⚧ ⚥ ⚲ - 배우 카드와 같은 색, 성별 미지정이면 없음)
		m_actorChips.m_iconWidth = [this](const CString& tag, int h) -> int
		{
			CString sym;
			COLORREF col = 0;
			return ActorChipGender(tag, sym, col) ? (std::max)(10, h) : 0;   // 칩 높이만큼 정사각형
		};
		m_actorChips.m_drawIcon = [this](CDC* dc, const CString& tag, const CRect& rc)
		{
			CString sym;
			COLORREF col = 0;
			if (!ActorChipGender(tag, sym, col))
				return;
			// 칸을 꽉 채워 굵게: ♀ / ♂ 는 선으로 직접 그림 (원 + 십자 / 원 + 화살표), 나머지는 칸 높이 크기의 굵은 글자
			EnsureGdiPlus();
			Gdiplus::Graphics g(dc->GetSafeHdc());
			g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
			g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
			const Gdiplus::Color gc(255, GetRValue(col), GetGValue(col), GetBValue(col));
			const float s = static_cast<float>((std::min)(rc.Width(), rc.Height())) * 0.9f;   // 칸의 90% (가운데)
			const float x0 = rc.left + (rc.Width() - s) / 2.0f, y0 = rc.top + (rc.Height() - s) / 2.0f;
			const float w = (std::max)(1.6f, s * 0.14f);   // 선 굵기
			Gdiplus::Pen pen(gc, w);
			pen.SetStartCap(Gdiplus::LineCapRound);
			pen.SetEndCap(Gdiplus::LineCapRound);
			if (sym == L"\x2640")   // ♀: 위 원 + 아래 십자
			{
				const float d = s * 0.56f;
				const float cx = x0 + s / 2.0f;
				g.DrawEllipse(&pen, cx - d / 2.0f, y0 + w / 2.0f, d, d);
				const float top = y0 + w / 2.0f + d, bottom = y0 + s - w / 2.0f;
				g.DrawLine(&pen, cx, top, cx, bottom);
				const float cy = top + (bottom - top) * 0.45f, half = s * 0.2f;
				g.DrawLine(&pen, cx - half, cy, cx + half, cy);
			}
			else if (sym == L"\x2642")   // ♂: 왼쪽 아래 원 + 오른쪽 위 화살표
			{
				const float d = s * 0.6f;
				const float ex = x0 + w / 2.0f, ey = y0 + s - w / 2.0f - d;
				g.DrawEllipse(&pen, ex, ey, d, d);
				const float r = d / 2.0f, ccx = ex + r, ccy = ey + r;
				const float k = 0.7071f;   // 45도
				const float sx = ccx + r * k, sy = ccy - r * k;               // 원 위 오른쪽
				const float tx = x0 + s - w / 2.0f, ty = y0 + w / 2.0f;     // 화살 끝 (오른쪽 위 모서리)
				g.DrawLine(&pen, sx, sy, tx, ty);
				const float a = s * 0.3f;   // 화살촉 길이
				g.DrawLine(&pen, tx - a, ty, tx, ty);
				g.DrawLine(&pen, tx, ty, tx, ty + a);
			}
			else
			{
				Gdiplus::FontFamily family(L"Segoe UI Symbol");
				Gdiplus::Font font(&family, s, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
				Gdiplus::StringFormat fmt;
				fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
				fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
				Gdiplus::SolidBrush br(gc);
				g.DrawString(sym, -1, &font, Gdiplus::RectF(x0 - s * 0.25f, y0 - s * 0.25f, s * 1.5f, s * 1.5f), &fmt, &br);
			}
		};
		// 칩 메뉴(이름/별칭 고르기)는 오른쪽 클릭 (OnContextMenu), 왼쪽 클릭은 메뉴 없음
		m_actorChips.m_onChipDblClick = [this](int index)   // 더블클릭: 배우 탭에서 그 배우로 이동
		{
			m_actorPopup.Hide();
			if (index < 0 || index >= static_cast<int>(m_actorChips.Tags().size()))
				return;
			const int ai = m_lib.FindActorByAnyName(m_actorChips.Tags()[index]);
			if (ai >= 0)
				GoToActor(ai);
		};
		// 배우 칩 위에 마우스 → 그 배우 카드 팝업 (배우 탭 카드와 같은 모양), 벗어나면 닫음 (DB 에 없는 배우는 표시 안 함)
		m_actorPopup.CreatePopup(this);
		m_actorPopup.m_back = kBackColor;
		m_actorPopup.m_onDraw = [this](CDC* dc, const CRect& rc)
		{
			CRect card = rc;
			card.DeflateRect(1, 1);
			DrawActorCardFor(dc, m_actorPopupIdx, card, false, false, false, false);   // 아래 출연 수 영역 없이
		};
		m_wallOverlay.m_onDraw = [this](CDC* dc, const CRect& rc) { DrawWallOverlay(dc, rc); };   // [월 모드] 칸 위 글자
		m_gridPartOverlay.m_onDraw = [this](CDC* dc, const CRect&)   // [격자] 분할 영상 미리보기 위 파트 칸
		{
			if (m_gridPartG >= 0)
				DrawWallPartZones(dc, m_gridPartG, m_gridPartCard);
		};
		// 영상 카드 아래 배우 수 → 출연 배우 카드 팝업 (출연 배우 카드 모양, 좌우로)
		m_castPopup.CreatePopup(this);
		// [패키지] 카드 마우스 오버: [격자] 영상 카드 팝업 (패키지 카드 가운데에 겹쳐서)
		m_pkgPopup.CreatePopup(this);
		m_pkgPopup.m_back = kBackColor;
		m_pkgPopup.m_fadeMs = 160;   // 부드럽게 나타남
		m_pkgPopup.m_interactive = true;   // 마우스를 받음: 클릭 = 고르기(분할 영상은 파트), 더블클릭 = 재생, 오른쪽 클릭 = 메뉴, 휠 = 격자 스크롤
		m_pkgPopup.m_onMouse = [this](UINT msg, CPoint pt) { OnPkgPopupMouse(msg, pt); };
		m_pkgPopup.m_onWheel = [this](WPARAM wp, LPARAM lp) { if (m_grid.GetSafeHwnd()) m_grid.SendMessage(WM_MOUSEWHEEL, wp, lp); };
		m_pkgPartOverlay.m_onDraw = [this](CDC* dc, const CRect& rc)   // 팝업 카드 커버 위 파트 칸 (미리보기 위에도)
		{
			if (m_pkgHoverG >= 0 && m_pkgPartHot >= 0)
				DrawWallPartZones(dc, m_pkgHoverG, rc, m_pkgPartHot);
		};
		m_pkgPopup.m_onDraw = [this](CDC* dc, const CRect& rc)
		{
			const int row = (m_pkgHoverG >= 0) ? GridRowOf(m_pkgHoverG) : -1;
			if (row < 0 || row >= static_cast<int>(m_view.size()))
				return;
			const bool oldImageOnly = m_cardImageOnly, oldPortrait = m_coverPortrait;
			m_cardImageOnly = false;   // [격자] 카드 모양 (이미지 + 제목 · 발매일 · 메모 · 집계)
			m_coverPortrait = false;
			m_popupCardDraw = true;
			DrawVideoCard(dc, row, rc, GridGetSel() == m_pkgHoverG, true);   // 고른 카드면 선택 테두리
			m_popupCardDraw = false;
			m_cardImageOnly = oldImageOnly;
			m_coverPortrait = oldPortrait;
		};
		m_castPopup.m_back = kBackColor;
		m_castPopup.m_fadeMs = 120;   // 부드럽게 나타남 (출연 배우 카드 · 태그 칩 팝업)
		m_castPopup.m_onDraw = [this](CDC* dc, const CRect& rc)
		{
			if (m_castKind == 2)
			{
				DrawListTagChips(dc, rc, m_castTags);   // 태그 칩 (모두 보이는 크기로 띄움)
				return;
			}
			// 카드들 (테두리 없이, 사진 + 이름만), 못 그린 배우가 있으면 맨 끝에 "+N"
			const int pad = DX(5), gap = DX(4);
			int x = rc.left + pad;
			const int y = rc.top + pad;
			for (int ai : m_castActors)
			{
				DrawCastCard(dc, ai, CRect(x, y, x + m_stripCardW, y + CastCardH()));
				x += m_stripCardW + gap;
			}
			if (m_castMore > 0)
			{
				CString more;
				more.Format(L"+%d", m_castMore);
				CRect mr(x, y, x + DX(28), y + CastCardH());
				dc->SetBkMode(TRANSPARENT);
				dc->SetTextColor(TCOL(TC_SUB2));
				CFont* of = dc->SelectObject(GetFont());
				dc->DrawText(more, mr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
				dc->SelectObject(of);
			}
		};
		m_actorChips.m_onChipHover = [this](int index, const CRect& chipScreen)
		{
			const int idx = (index >= 0 && index < static_cast<int>(m_actorChips.Tags().size()))
				? m_lib.FindActorByAnyName(m_actorChips.Tags()[index]) : -1;
			if (idx < 0)
			{
				m_actorPopup.Hide();
				return;
			}
			if (idx != m_actorPopupIdx)
			{
				m_actorPopupIdx = idx;
				m_actorPopup.Invalidate(FALSE);
			}
			m_actorPopup.ShowNear(chipScreen, CSize(m_cardW + 2, m_cardH - ActorCardCountsH() + 2));   // 배우 탭 카드 크기 (지금 확대 단계 기준, 출연 수 영역 뺌)
		};
		m_actorChips.m_edit.Setup([this](std::vector<SuggestItem>& out)
		{
			// 이미 붙은 배우(이름 또는 별칭으로)는 후보에서 뺌
			std::set<int> used;
			for (const CString& n : m_actorChips.Tags())
			{
				const int idx = m_lib.FindActorByAnyName(n);
				if (idx >= 0) used.insert(idx);
			}
			for (size_t i = 0; i < m_lib.actors.size(); ++i)
			{
				if (used.count(static_cast<int>(i)))
					continue;
				const ActorInfo& a = m_lib.actors[i];
				out.push_back({ a.name, a.name });
				for (const CString& al : CVideoLibrary::SplitList(a.aliases))
					out.push_back({ al + L"\t" + a.name + L"의 별칭", al });
			}
		}, false);
		m_actorChips.m_onChanged = [this]()
		{
			// 칩 → 숨긴 배우 칸 (EN_CHANGE 로 변경 표시)
			CString oldActors;
			m_editActors.GetWindowText(oldActors);
			const CString newActors = CVideoLibrary::JoinList(m_actorChips.Tags());
			m_syncingActors = true;
			m_editActors.SetWindowText(newActors);
			m_syncingActors = false;
			ApplyDefaultAliases(oldActors, newActors);
		};
		m_actorChips.m_onDropDown = [this]() { OnBnClickedPickActors(); };
		m_actorChips.m_onHeightChanged = m_tagChips.m_onHeightChanged;
		GetDlgItem(IDC_EDIT_ACTORS)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_BTN_PICKACTORS)->ShowWindow(SW_HIDE);

		// 스튜디오 칩 입력 (배우 선택과 같은 모양, 스튜디오는 하나만 - 새로 고르면 바뀜)
		m_studioChips.Create(this, IDC_STUDIO_CHIPS);
		m_studioChips.SetColors(kEditColor, TCOL(TC_LIGHT), TCOL(TC_DARK), TCOL(TC_SUB), kTextColor);
		m_studioChips.m_edit.SetColors(kEditColor, kTextColor, kButtonColor, TCOL(TC_HINT));
		m_studioChips.m_edit.SetCueBanner(L"제작사 입력 (↓ 목록)");
		m_studioChips.m_edit.Setup([this](std::vector<SuggestItem>& out)
		{
			const CString cur = m_studioChips.Tags().empty() ? CString() : m_studioChips.Tags()[0];
			for (const NamedInfo& n : m_lib.studios)
			{
				if (n.name.CompareNoCase(cur) == 0)   // 지금 스튜디오는 후보에서 뺌
					continue;
				// 서브이름이 있으면 오른쪽에 회색으로, 서브이름으로 입력해도 찾아짐 (고르면 스튜디오 이름)
				const std::vector<CString> subs = CVideoLibrary::SplitLines(n.subName);   // 서브이름 여러 개 (줄바꿈 구분)
				CString subShown;
				for (const CString& sub : subs) { if (!subShown.IsEmpty()) subShown += L" / "; subShown += sub; }
				out.push_back({ subShown.IsEmpty() ? n.name : (n.name + L"\t" + subShown), n.name });
				for (const CString& sub : subs)
					out.push_back({ sub + L"\t" + n.name + L"의 서브이름", n.name });
			}
			// 레이블도 후보로 (고르면 레이블 + 상위 제작사, 화면은 [레이블] 줄로 바뀜)
			for (const NamedInfo& lb : m_lib.labelInfos)
			{
				const bool hasParent = m_lib.FindNamed(LIST_STUDIO, lb.parent) >= 0;
				out.push_back({ lb.name + L"\t레이블" + (hasParent ? L" · " + lb.parent : CString()), lb.name });
			}
		}, false);
		m_studioChips.m_onChanged = [this]()
		{
			// 하나만: 새로 추가하면 마지막 것만 남김 → 숨긴 스튜디오 칸 (EN_CHANGE 로 변경 표시)
			std::vector<CString> tags = m_studioChips.Tags();
			if (tags.size() > 1)
			{
				tags.erase(tags.begin(), tags.end() - 1);
				m_studioChips.SetTags(tags);
			}
			// 제작사 칸에 레이블을 넣으면 → 레이블 칩 + 상위 제작사 (상위가 없으면 제작사는 빈칸)
			if (!tags.empty() && m_lib.FindStudioLoose(tags[0]) < 0)
			{
				const int li = m_lib.FindLabelLoose(tags[0]);
				if (li >= 0 && m_labelChips.GetSafeHwnd())
				{
					m_labelChips.SetTags({ m_lib.labelInfos[li].name });
					const int owner = m_lib.FindNamed(LIST_STUDIO, m_lib.labelInfos[li].parent);
					tags.clear();
					if (owner >= 0)
						tags.push_back(m_lib.studios[owner].name);
					m_studioChips.SetTags(tags);
					m_syncingStudio = true;
					m_editStudio.SetWindowText(tags.empty() ? CString() : tags[0]);
					m_syncingStudio = false;
					OnDetailsChanged();
					RelayoutIfLabelRowChanged(true);
					return;
				}
			}
			m_syncingStudio = true;
			m_editStudio.SetWindowText(tags.empty() ? CString() : tags[0]);
			m_syncingStudio = false;
			// (레이블 개편: 레이블은 품번 DB 값이라 제작사를 바꿔도 비우지 않음)
			// 시리즈가 다른 제작사의 시리즈면 비움 (다른 제작사 레이블의 시리즈도 - 저장할 때 그 레이블이 다시 채워지지 않게)
			if (m_seriesChips.GetSafeHwnd() && !m_seriesChips.Tags().empty())
			{
				const CString sr = m_seriesChips.Tags()[0];
				const int newStudio = tags.empty() ? -1 : m_lib.FindStudioLoose(tags[0]);
				const int ss = m_lib.FindStudioOfSeries(sr);
				bool clear = (ss >= 0 && (tags.empty() || newStudio != ss));
				const int sl = m_lib.FindLabelOfSeries(sr);
				if (!clear && sl >= 0)
				{
					const int owner = m_lib.FindNamed(LIST_STUDIO, m_lib.labelInfos[sl].parent);
					if (owner >= 0 && (tags.empty() || newStudio != owner))
						clear = true;
				}
				if (clear)
					m_seriesChips.SetTags({});
			}
			RelayoutIfLabelRowChanged(true);
		};
		// 스튜디오 이미지가 있으면 칩 이름 왼쪽에 (비율 유지, 칩 높이에 맞춤, 너무 넓은 로고는 높이의 3배까지)
		m_studioChips.m_iconWidth = [this](const CString& tag, int h) -> int
		{
			const int n = m_lib.FindNamed(LIST_STUDIO, tag);
			Gdiplus::Bitmap* logo = (n >= 0) ? GetStudioLogo(m_lib.studios[n].image) : nullptr;
			if (!logo || logo->GetWidth() == 0 || logo->GetHeight() == 0 || h <= 0)
				return 0;
			const double w = h * static_cast<double>(logo->GetWidth()) / logo->GetHeight();
			return (std::max)(h / 2, (std::min)(h * 3, static_cast<int>(w + 0.5)));
		};
		m_studioChips.m_drawIcon = [this](CDC* dc, const CString& tag, const CRect& rc)
		{
			const int n = m_lib.FindNamed(LIST_STUDIO, tag);
			Gdiplus::Bitmap* logo = (n >= 0) ? GetStudioLogo(m_lib.studios[n].image) : nullptr;
			if (!logo || logo->GetWidth() == 0 || logo->GetHeight() == 0 || rc.Width() <= 0 || rc.Height() <= 0)
				return;
			const double lw = logo->GetWidth(), lh = logo->GetHeight();
			const double scale = (std::min)(rc.Width() / lw, rc.Height() / lh);
			const int w = (std::max)(1, static_cast<int>(lw * scale));
			const int h = (std::max)(1, static_cast<int>(lh * scale));
			const int x = rc.left + (rc.Width() - w) / 2;
			const int y = rc.top + (rc.Height() - h) / 2;
			Gdiplus::Graphics g(dc->GetSafeHdc());
			g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
			g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
			g.DrawImage(logo, Gdiplus::Rect(x, y, w, h), 0, 0, static_cast<INT>(lw), static_cast<INT>(lh), Gdiplus::UnitPixel);
		};
		m_studioChips.m_onDropDown = [this]() { OnBnClickedPickStudio(); };
		m_studioChips.m_onHeightChanged = m_tagChips.m_onHeightChanged;
		GetDlgItem(IDC_EDIT_STUDIO)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_BTN_PICKSTUDIO)->ShowWindow(SW_HIDE);

		// 레이블 칩 입력 (스튜디오 하위, 하나만): 후보는 지금 스튜디오의 레이블 먼저, 그다음 다른 스튜디오의 레이블 (오른쪽에 스튜디오 이름)
		m_labelChips.Create(this, IDC_LABEL_CHIPS);
		m_labelChips.SetColors(kEditColor, TCOL(TC_LIGHT), TCOL(TC_DARK), TCOL(TC_SUB), kTextColor);
		m_labelChips.m_edit.SetColors(kEditColor, kTextColor, kButtonColor, TCOL(TC_HINT));
		m_labelChips.m_edit.SetCueBanner(L"레이블 입력 (↓ 목록)");
		m_labelChips.m_edit.Setup([this](std::vector<SuggestItem>& out)
		{
			CString studio;
			m_editStudio.GetWindowText(studio);
			studio.Trim();
			const int sn = studio.IsEmpty() ? -1 : m_lib.FindStudioLoose(studio);
			const CString cur = m_labelChips.Tags().empty() ? CString() : m_labelChips.Tags()[0];
			auto addFrom = [&](int i)
			{
				const NamedInfo& n = m_lib.studios[i];
				for (const CString& l : m_lib.LabelNamesOf(n.name))
					if (l.CompareNoCase(cur) != 0)
						out.push_back({ l + L"\t" + n.name, l });
			};
			if (sn >= 0)
				addFrom(sn);
			for (size_t i = 0; i < m_lib.studios.size(); ++i)
				if (static_cast<int>(i) != sn)
					addFrom(static_cast<int>(i));
			for (const NamedInfo& lb : m_lib.labelInfos)   // 상위 제작사가 없는 레이블
				if (m_lib.FindNamed(LIST_STUDIO, lb.parent) < 0 && lb.name.CompareNoCase(cur) != 0)
					out.push_back({ lb.name + L"\t상위 없음", lb.name });
		}, false);
		m_labelChips.m_onChanged = [this]()
		{
			// 하나만: 새로 추가하면 마지막 것만 남김
			std::vector<CString> tags = m_labelChips.Tags();
			if (tags.size() > 1)
			{
				tags.erase(tags.begin(), tags.end() - 1);
				m_labelChips.SetTags(tags);
			}
			// 스튜디오가 비어 있거나 지금 스튜디오의 레이블이 아니면 → 그 레이블을 가진 스튜디오로
			if (!tags.empty())
			{
				CString studio;
				m_editStudio.GetWindowText(studio);
				studio.Trim();
				const int sn = studio.IsEmpty() ? -1 : m_lib.FindStudioLoose(studio);
				bool has = false;
				if (sn >= 0)
					for (const CString& l : m_lib.LabelNamesOf(m_lib.studios[sn].name))
						if (l.CompareNoCase(tags[0]) == 0) { has = true; break; }
				if (!has)
				{
					const int owner = m_lib.FindStudioByLabel(tags[0]);
					if (owner >= 0 && owner != sn)
						m_editStudio.SetWindowText(m_lib.studios[owner].name);   // EN_CHANGE → 스튜디오 칩도 갱신
				}
			}
			// 시리즈가 다른 레이블의 시리즈면 비움
			if (m_seriesChips.GetSafeHwnd() && !m_seriesChips.Tags().empty())
			{
				const int sl = m_lib.FindLabelOfSeries(m_seriesChips.Tags()[0]);
				if (sl >= 0 && (tags.empty() || m_lib.FindLabel(tags[0]) != sl))
					m_seriesChips.SetTags({});
			}
			OnDetailsChanged();
			RelayoutIfLabelRowChanged(true);   // 레이블을 넣거나 빼면 [제작사] / [레이블] 줄 전환
		};
		m_labelChips.m_onDropDown = [this]() { m_labelChips.m_edit.ShowAll(); };
		// 레이블 이미지가 있으면 칩 이름 왼쪽에 (제작사 칩과 같은 방식)
		m_labelChips.m_iconWidth = [this](const CString& tag, int h) -> int
		{
			const int n = m_lib.FindLabel(tag);
			Gdiplus::Bitmap* logo = (n >= 0) ? GetStudioLogo(m_lib.labelInfos[n].image) : nullptr;
			if (!logo || logo->GetWidth() == 0 || logo->GetHeight() == 0 || h <= 0)
				return 0;
			const double w = h * static_cast<double>(logo->GetWidth()) / logo->GetHeight();
			return (std::max)(h / 2, (std::min)(h * 3, static_cast<int>(w + 0.5)));
		};
		m_labelChips.m_drawIcon = [this](CDC* dc, const CString& tag, const CRect& rc)
		{
			const int n = m_lib.FindLabel(tag);
			Gdiplus::Bitmap* logo = (n >= 0) ? GetStudioLogo(m_lib.labelInfos[n].image) : nullptr;
			if (!logo || logo->GetWidth() == 0 || logo->GetHeight() == 0 || rc.Width() <= 0 || rc.Height() <= 0)
				return;
			const double lw = logo->GetWidth(), lh = logo->GetHeight();
			const double scale = (std::min)(rc.Width() / lw, rc.Height() / lh);
			const int w = (std::max)(1, static_cast<int>(lw * scale));
			const int h = (std::max)(1, static_cast<int>(lh * scale));
			const int x = rc.left + (rc.Width() - w) / 2;
			const int y = rc.top + (rc.Height() - h) / 2;
			Gdiplus::Graphics g(dc->GetSafeHdc());
			g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
			g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
			g.DrawImage(logo, Gdiplus::Rect(x, y, w, h), 0, 0, static_cast<INT>(lw), static_cast<INT>(lh), Gdiplus::UnitPixel);
		};
		m_labelChips.m_onHeightChanged = m_tagChips.m_onHeightChanged;

		// 시리즈 칩 입력 (레이블 하위, 하나만): 후보는 지금 레이블의 시리즈 먼저, 그다음 다른 시리즈 (오른쪽에 레이블 이름)
		m_seriesChips.Create(this, IDC_SERIES_CHIPS);
		m_seriesChips.ShowWindow(SW_HIDE);                    // 영상 상세에는 표시하지 않음 (값 보관용)
		m_editSeries.SetCueBanner(L"시리즈");
		m_seriesChips.SetColors(kEditColor, TCOL(TC_LIGHT), TCOL(TC_DARK), TCOL(TC_SUB), kTextColor);
		m_seriesChips.m_edit.SetColors(kEditColor, kTextColor, kButtonColor, TCOL(TC_HINT));
		m_seriesChips.m_edit.SetCueBanner(L"시리즈 입력 (↓ 목록)");
		m_seriesChips.m_edit.Setup([this](std::vector<SuggestItem>& out)
		{
			const CString lbName = m_labelChips.Tags().empty() ? CString() : m_labelChips.Tags()[0];
			const int cl = lbName.IsEmpty() ? -1 : m_lib.FindLabel(lbName);
			const CString cur = m_seriesChips.Tags().empty() ? CString() : m_seriesChips.Tags()[0];
			std::set<CString> used;
			auto push = [&](const CString& s, const CString& owner)
			{
				CString k = s;
				k.MakeLower();
				if (s.CompareNoCase(cur) == 0 || !used.insert(k).second)
					return;
				out.push_back({ owner.IsEmpty() ? s : (s + L"\t" + owner), s });
			};
			if (cl >= 0)
				for (const CString& s : CVideoLibrary::SeriesNames(m_lib.labelInfos[cl].series))
					push(s, m_lib.labelInfos[cl].name);
			// 그다음 지금 제작사의 시리즈
			CString studio;
			m_editStudio.GetWindowText(studio);
			studio.Trim();
			const int cs = studio.IsEmpty() ? -1 : m_lib.FindStudioLoose(studio);
			if (cs >= 0)
				for (const CString& s : CVideoLibrary::SeriesNames(m_lib.studios[cs].series))
					push(s, m_lib.studios[cs].name);
			for (const CString& s : m_lib.AllSeries())
			{
				const int li = m_lib.FindLabelOfSeries(s);
				const int si = (li < 0) ? m_lib.FindStudioOfSeries(s) : -1;
				push(s, li >= 0 ? m_lib.labelInfos[li].name : (si >= 0 ? m_lib.studios[si].name : CString()));
			}
		}, false);
		m_seriesChips.m_onChanged = [this]()
		{
			// 하나만: 새로 추가하면 마지막 것만 남김
			std::vector<CString> tags = m_seriesChips.Tags();
			if (tags.size() > 1)
			{
				tags.erase(tags.begin(), tags.end() - 1);
				m_seriesChips.SetTags(tags);
			}
			// 레이블이 비어 있거나 다른 레이블이면 → 그 시리즈를 가진 레이블로 (레이블의 상위 제작사도)
			if (!tags.empty())
			{
				const int li = m_lib.FindLabelOfSeries(tags[0]);
				const CString curLabel = m_labelChips.Tags().empty() ? CString() : m_labelChips.Tags()[0];
				if (li >= 0 && (curLabel.IsEmpty() || m_lib.FindLabel(curLabel) != li))
				{
					m_labelChips.SetTags({ m_lib.labelInfos[li].name });
					const int owner = m_lib.FindNamed(LIST_STUDIO, m_lib.labelInfos[li].parent);
					if (owner >= 0)
					{
						CString studio;
						m_editStudio.GetWindowText(studio);
						if (studio.CompareNoCase(m_lib.studios[owner].name) != 0)
							m_editStudio.SetWindowText(m_lib.studios[owner].name);   // EN_CHANGE → 제작사 칩도 갱신
					}
				}
				else if (li < 0)
				{
					// 제작사의 시리즈: 제작사가 다르면 그 제작사로 (지금 레이블이 다른 제작사 소속이면 레이블은 비움)
					const int sn = m_lib.FindStudioOfSeries(tags[0]);
					CString studio;
					m_editStudio.GetWindowText(studio);
					studio.Trim();
					if (sn >= 0 && m_lib.FindStudioLoose(studio) != sn)
					{
						m_editStudio.SetWindowText(m_lib.studios[sn].name);
						if (!curLabel.IsEmpty())
						{
							const int lo = m_lib.FindStudioByLabel(curLabel);
							if (lo >= 0 && lo != sn)
								m_labelChips.SetTags({});
						}
					}
				}
			}
			OnDetailsChanged();
		};
		m_seriesChips.m_onDropDown = [this]() { m_seriesChips.m_edit.ShowAll(); };
		m_seriesChips.m_onHeightChanged = m_tagChips.m_onHeightChanged;

		ApplyDetailFonts();   // 영상 상세 글자 컨트롤: 기본 글꼴 + 1pt
		// [별칭] 줄은 배우 칩에 합침 (값은 숨긴 별칭 칸에 보관)
		GetDlgItem(IDC_STATIC_VALIASES_LBL)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_EDIT_VALIASES)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_BTN_PICKVALIASES)->ShowWindow(SW_HIDE);
	}

	// 시작할 때마다 기존 DB 백업 (DB 폴더\backup, 최근 10개, 바뀐 것이 없으면 건너뜀)
	CVideoLibrary::BackupDbFiles(10);

	// 이전 실행이 비정상 종료되어 반영하지 못한 작업 DB 가 남아 있으면 물어봄
	if (CVideoLibrary::HasLeftoverWorkDb())
	{
		const int r = AfxMessageBox(L"이전 실행에서 DB 에 반영하지 못한 변경 내용(library.work.vmdb)이 남아 있습니다.\n\n"
			L"[예] 남은 변경 내용을 DB 에 반영하고 시작\n[아니요] 남은 변경 내용을 버리고 기존 DB 로 시작",
			MB_YESNO | MB_ICONQUESTION);
		if (r == IDYES)
		{
			if (!CVideoLibrary::ApplyLeftoverWorkDb())
				AfxMessageBox(L"남은 변경 내용을 반영하지 못했습니다. 기존 DB 로 시작합니다.", MB_ICONWARNING);
		}
		else
			CVideoLibrary::DiscardWorkDb();
	}
	m_lib.m_onSaved = [this]() { UpdateApplyDbButton(); };

	// 라이브러리 불러오기
	const bool haveLibrary = m_lib.Load();
	CMediaInfoLabel::LoadDiskCache(CVideoLibrary::GetDataDir() + L"\\mediainfo.vmdb");   // 영상 해상도 · 재생 시간 캐시 (카드마다 파일을 열지 않게)
	// DB 버전 확인: 저장한 프로그램 버전이 지금과 다르면 원본 DB 백업 후 변환 (변환 후 저장하면 지금 버전으로 기록)
	bool dbConverted = false;
	CString dbConvertNote;
	if (haveLibrary && !m_lib.DbBroken())
	{
		const CString dbVer = m_lib.DbVersion();
		const CString appVer = CVideoLibrary::AppVersion();
		if (dbVer != appVer)
		{
			const CString shownVer = dbVer.IsEmpty() ? CString(L"(기록 없음)") : dbVer;
			CString backup;
			bool doConvert = true;
			if (!CVideoLibrary::BackupDataFile(dbVer, backup))
			{
				doConvert = AfxMessageBox(L"DB 버전이 달라 변환하기 전에 백업을 만들지 못했습니다.\n\n" + backup +
					L"\n\n백업 없이 변환할까요? (아니요 = 변환하지 않고 시작)", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
				backup.Empty();
			}
			if (CVideoLibrary::CompareVersions(dbVer, appVer) > 0)
			{
				// 더 새 버전 프로그램이 저장한 DB: 변환하지 않음 (이 버전으로 저장하면 새 버전의 정보가 빠질 수 있음)
				AfxMessageBox(L"이 DB 는 더 새 버전(" + dbVer + L")의 프로그램이 저장했습니다. 지금 버전은 " + appVer + L" 입니다.\n\n" +
					(backup.IsEmpty() ? CString() : L"원본 DB 를 백업했습니다:\n" + backup + L"\n\n") +
					L"이 버전에서 저장하면 새 버전에서 추가된 정보가 빠질 수 있습니다.", MB_ICONWARNING);
				doConvert = false;
			}
			if (doConvert)
				m_lib.ConvertDb(dbVer);
			dbConverted = true;   // 변환 여부와 관계없이 저장해서 지금 버전으로 기록
			dbConvertNote.Format(L"DB 버전 %s → %s %s", static_cast<LPCWSTR>(shownVer), static_cast<LPCWSTR>(appVer),
				doConvert ? L"변환" : L"(변환 안 함)");
			if (!backup.IsEmpty())
				dbConvertNote += L", 백업: " + CString(::PathFindFileNameW(backup));
		}
	}
	{
		// 배우 칸의 별칭 표기 → 배우 이름 + 참여 별칭
		const bool va = m_lib.NormalizeAllVideoActors();
		// 기존 데이터의 배우/스튜디오/태그 이름을 각 목록으로 옮김
		const bool a = m_lib.SyncActorsFromVideos() || va;
		// 레이블 개편: 예전 레이블(제작사 하위) → 품번 DB 의 레이블 (한 번만 - 원본 DB 백업 후)
		bool lbl = false;
		if (haveLibrary && !m_lib.DbBroken())
		{
			bool need = !m_lib.labelInfos.empty();
			for (const VideoItem& v : m_lib.items)
				if (!v.label.IsEmpty()) { need = true; break; }
			if (need)
			{
				// 안내 창 · 백업은 처음 한 번만 (레지스트리 Settings\LabelMigrated) - 그 뒤에 남은 레이블 값이 있으면 조용히 옮김
				const bool first = AfxGetApp()->GetProfileInt(L"Settings", L"LabelMigrated", 0) == 0;
				CString backup;
				if (first)
					CVideoLibrary::BackupDataFile(m_lib.DbVersion() + L"_label", backup);
				CString report;
				lbl = m_lib.MigrateLabelsToSeries(&report);
				if (lbl && first)
				{
					if (!backup.IsEmpty())
						report += L"\n\n원본 DB 백업: " + backup;
					AfxMessageBox(report, MB_ICONINFORMATION);
				}
			}
			AfxGetApp()->WriteProfileInt(L"Settings", L"LabelMigrated", 1);   // 다음부터 안내 창 없음
		}
		const bool n = m_lib.SyncNamedFromVideos() || lbl;
		// 품번이 빈 영상은 파일 / 영상 폴더 이름에서 찾아 채움
		const bool codes = m_lib.FillMissingCodes() > 0;
		// 분할 파일(ABC-123_1 / _2 …)은 같은 정보를 사용: 묶음마다 빈 정보를 서로 채움
		const bool parts = m_lib.UnifyPartGroups() > 0;
		// 예전에 원본 경로로 등록한 이미지를 DB 폴더(images)의 복사본으로 교체
		bool img = m_lib.MigrateImagesToStore();
		// Image 폴더의 예전 암호화 이미지(.vmimg) → 평문 사본으로 풀어 연결 (DB 를 정상적으로 읽었을 때만, 원래 파일은 지우지 않음)
		if (haveLibrary && !m_lib.DbBroken())
			img = m_lib.DecryptImageStore() || img;
		// 경로가 바뀐(옮기거나 이름을 바꾼) 영상 파일을 기존 정보에 다시 연결
		{
			CWaitCursor wait;
			m_startupRelinked = m_lib.RelinkOnStartup();
		}
		if (a || n || img || codes || parts || dbConverted || m_startupRelinked > 0 || m_lib.LoadedPlainText())   // 예전 평문 DB 는 바로 암호화해서 다시 저장
		{
			m_lib.Save();
		}
		if (m_lib.DbBroken())
			AfxMessageBox(L"DB 파일을 복호화하지 못했습니다 (손상되었거나 다른 프로그램 버전의 키).\n"
				L"기존 DB 를 보호하기 위해 이번 실행에서는 DB 를 저장하지 않습니다.", MB_ICONERROR);
		// 교체/삭제되어 더 이상 쓰이지 않는 복사본 정리 (DB를 정상적으로 읽은 경우에만)
		if (haveLibrary && !m_lib.DbBroken())
			m_lib.CleanupImageStore();
	}
	UpdateApplyDbButton();   // 시작 때 정리하며 저장했으면 활성
	RebuildCategories();

	// 목록 / 격자 표시 복원
	m_showCardMedia = AfxGetApp()->GetProfileInt(L"Settings", L"CardMediaInfo", 1) != 0;   // 영상 카드 해상도 · 재생 시간 (설정 창)
	const bool grid = AfxGetApp()->GetProfileInt(L"Settings", L"GridView", 0) != 0;
	CheckRadioButton(IDC_RADIO_LISTVIEW, IDC_RADIO_GRIDVIEW, grid ? IDC_RADIO_GRIDVIEW : IDC_RADIO_LISTVIEW);
	SetGridView(grid);

	m_layoutReady = true;
	RestoreWindowPlacement();   // 지난번 창 크기/위치/최대화 상태
	CRect client;
	GetClientRect(&client);
	LayoutControls(client.Width(), client.Height());

	ShowDetails(-1);
	ApplyFilter();
	UpdateSortArrows();
	UpdateSortUI();
	if (IsActorGridMode())
		UpdateStatus();
	else if (IsCategoryListMode())
		ShowNamedInfo(-1);

	CreateScrollBars();

	if (m_startupRelinked > 0 || !dbConvertNote.IsEmpty())
	{
		CString note;
		if (m_startupRelinked > 0)
			note.Format(L"경로가 바뀐 영상 파일 %d개를 기존 정보에 다시 연결했습니다.", m_startupRelinked);
		if (!dbConvertNote.IsEmpty())
			note += (note.IsEmpty() ? CString() : CString(L"  ")) + dbConvertNote;
		m_staticStatus.SetWindowText(note);
	}
	DarkEdge::AttachAll(GetSafeHwnd());   // 에디트 테두리를 어두운 색으로 (나중에 만든 칸 포함, 도구줄 칸은 격자 안으로 옮겨도 그대로)
	return TRUE;
}

// ---------------------------------------------------------------------------
// 레이아웃

int CRuliManagerDlg::DX(int dlu)
{
	CRect r(0, 0, dlu, 0);
	MapDialogRect(&r);
	return r.right;
}

int CRuliManagerDlg::DY(int dlu)
{
	CRect r(0, 0, 0, dlu);
	MapDialogRect(&r);
	return r.bottom;
}

void CRuliManagerDlg::MoveCtrl(UINT id, int x, int y, int w, int h)
{
	CWnd* p = GetDlgItem(id);
	if (!p)
		return;
	const CRect want(x, y, x + (std::max)(0, w), y + (std::max)(0, h));
	auto it = m_ctrlRects.find(id);
	if (it != m_ctrlRects.end() && it->second == want)
		return;   // 그대로면 옮기지도 다시 그리지도 않음 (카드 클릭 때 버튼 깜빡임 방지)
	CRect before;
	p->GetWindowRect(&before);
	ScreenToClient(&before);
	p->MoveWindow(want.left, want.top, want.Width(), want.Height(), FALSE);
	CRect after;
	p->GetWindowRect(&after);   // 콤보박스는 드롭다운 높이가 아닌 실제 높이
	ScreenToClient(&after);
	m_layoutDirty.push_back(before);
	m_layoutDirty.push_back(after);
	m_ctrlRects[id] = want;
}

void CRuliManagerDlg::FlushLayoutRedraw()
{
	// 직접 그린 스크롤바: 대상(격자 · 목록)이 옮겨지거나 크기가 바뀌면 바로 따라감
	//  (예전에는 50ms 타이머로만 맞춰서 창 크기를 바꾸는 동안 스크롤바 배경과 썸이 따로 보였음)
	for (CDarkScrollBar& sb : m_scrollBars)
	{
		if (sb.GetSafeHwnd())
		{
			sb.Sync();
			if (sb.IsWindowVisible())
				sb.RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
		}
	}
	if (m_layoutDirty.empty())
		return;
	CRgn rgn;
	rgn.CreateRectRgn(0, 0, 0, 0);
	for (const CRect& r : m_layoutDirty)
	{
		if (r.IsRectEmpty())
			continue;
		CRgn one;
		one.CreateRectRgnIndirect(&r);
		rgn.CombineRgn(&rgn, &one, RGN_OR);
	}
	m_layoutDirty.clear();
	RedrawWindow(nullptr, &rgn, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

int CRuliManagerDlg::LeftPaneWidth(int cx)
{
	// 모든 탭: 오른쪽 상세 정보 폭은 고정, 창 크기 변화는 왼쪽 격자가 받음
	const int m = DX(6), gap = DX(4);
	if (TagLikeMode())
		return (std::max)(DX(150), cx - 2 * m);   // 태그 · 컬렉션 탭: 상세 페이지 없이 격자가 전체 폭
	if (m_detailCollapsed)
		return (std::max)(DX(150), cx - 2 * m - DetailBarW());   // 상세 화면 접힘: 접기 바만 남기고 격자가 전체 폭
	const int avail = cx - 2 * m - gap - DetailBarW();   // 격자 | 접기 바 | 간격 | 상세 화면
	const int detailW = DX(280);
	return (std::max)(DX(150), avail - detailW);
}

const UINT* CRuliManagerDlg::ToolbarIds(int& count)
{
	static const UINT ids[] = {
		IDC_EDIT_SEARCH, IDC_COMBO_FILTER, IDC_COMBO_SORT, IDC_BTN_SORTDIR, IDC_STUDIO_VIEW_GRID, IDC_STUDIO_VIEW_LIST, IDC_VIDEO_MODE_GRID, IDC_VIDEO_MODE_LIST, IDC_VIDEO_MODE_PACKAGE, IDC_VIDEO_MODE_WALL, IDC_VIDEO_MODE_LABEL, IDC_VIDEO_VIEW_H, IDC_VIDEO_VIEW_V, IDC_ACTOR_GENDER_ALL, IDC_ACTOR_GENDER_F, IDC_ACTOR_GENDER_M, IDC_ACTOR_VIEW_GRID, IDC_ACTOR_VIEW_TREEMAP, IDC_COMBO_TREEMAP_BASIS, IDC_GENRE_HIDE, IDC_GENRE_ALL, IDC_COMBO_PAGESIZE, IDC_ZOOM_SLIDER,
		IDC_PAGE_FIRST, IDC_PAGE_PREV, IDC_PAGE_N1, IDC_PAGE_N2, IDC_PAGE_N3, IDC_PAGE_N4, IDC_PAGE_N5, IDC_PAGE_NEXT, IDC_PAGE_LAST,
		IDC_STATIC_PAGEINFO, IDC_GENRE_CHIPS   // 장르 칩 줄도 격자의 자식으로 (빠져 있어서 대화상자 자식으로 남아 격자와 겹쳐 그려졌음)
	};
	count = _countof(ids);
	return ids;
}

void CRuliManagerDlg::SetToolbarHost(CVideoGrid* host)
{
	if (host == m_toolbarHost || !host || !host->GetSafeHwnd())
		return;
	CWnd* from = m_toolbarHost ? static_cast<CWnd*>(m_toolbarHost) : static_cast<CWnd*>(this);
	int n = 0;
	const UINT* ids = ToolbarIds(n);
	for (int i = 0; i < n; ++i)
	{
		HWND h = ::GetDlgItem(from->GetSafeHwnd(), ids[i]);
		if (h)
			::SetParent(h, host->GetSafeHwnd());   // 격자의 자식 → 카드와 함께 스크롤, 격자 밖으로 나가면 잘림
	}
	if (m_toolbarHost && m_toolbarHost->GetSafeHwnd())
		m_toolbarHost->SetHeaderHeight(0);   // 이전 격자는 머리 영역 없음
	m_toolbarHost = host;
}

int CRuliManagerDlg::LayoutListToolbar(int x, int y, int w)
{
	// 목록(격자) 머리 영역의 도구줄 (가운데 정렬, 격자 클라이언트 좌표 - 스크롤한 만큼 위로)
	//  1줄: [검색...] [정렬 ▾][▲▼] [페이지당 ▾] [화면별 버튼 묶음] [확대 ━●━]   (보이는 것만)
	//  2줄: [«] [<] [1] [2] [3] [4] [5] [>] [»]
	//  3줄: 1-250 / 1,251개 (6.9 TiB)
	if (!m_toolbarHost || !m_toolbarHost->GetSafeHwnd())
		return 0;
	const int gap = DX(4);
	const int rowH = DY(13), btnH = DY(14);
	const int scroll = m_toolbarHost->ScrollY();
	auto visible = [this](UINT id) { CWnd* c = GetDlgItem(id); return c && (c->GetStyle() & WS_VISIBLE); };
	auto place = [this, scroll](UINT id, int px, int py, int pw, int ph)
	{
		CWnd* c = GetDlgItem(id);
		if (!c)
			return;
		const CRect want(px, py - scroll, px + (std::max)(0, pw), py - scroll + (std::max)(0, ph));
		CRect cur;
		c->GetWindowRect(&cur);
		c->GetParent()->ScreenToClient(&cur);
		// 콤보는 드롭다운 높이를 따로 가지므로 위치 · 폭만 비교
		TCHAR cls[16] = {};
		::GetClassName(c->GetSafeHwnd(), cls, 16);
		const bool combo = (_tcsicmp(cls, _T("ComboBox")) == 0);
		if (cur.left == want.left && cur.top == want.top && cur.Width() == want.Width() && (combo || cur.Height() == want.Height()))
			return;   // 그대로면 옮기지 않음 (깜빡임 방지)
		c->MoveWindow(want, TRUE);
	};
	struct Item { UINT id; int w; int h; int dy; bool joinNext = false; };   // joinNext: 다음 칸과 붙임 (세그먼트 버튼)
	std::vector<Item> row1;
	// 순서 (보이는 것만): [검색][정렬 ▾][▲▼][페이지당 ▾] + 화면별 묶음 + [확대]
	//  영상 화면(영상 탭 · 배우 / 제작사 / 태그 / 컬렉션 영상): [장르숨김|전체][격자|리스트|패키지]
	//  배우 탭: [선택없음|여성만|남성만] · 제작사 탭: [격자|리스트] · 태그 탭: 없음
	row1.push_back({ IDC_EDIT_SEARCH, DX(150), rowH, 0 });
	if (visible(IDC_COMBO_SORT))   row1.push_back({ IDC_COMBO_SORT, DX(60), DY(120), 0, visible(IDC_BTN_SORTDIR) });   // [정렬 ▾][▲▼] 붙은 한 묶음
	if (visible(IDC_BTN_SORTDIR))
	{
		// 아이콘만 (오름차순 / 내림차순은 툴팁) - 콤보 칸과 같은 높이 · 같은 위치로 붙여 한 묶음처럼
		int sh = rowH;
		if (CWnd* sc = GetDlgItem(IDC_COMBO_SORT))
		{
			CRect r;
			sc->GetWindowRect(&r);   // 콤보는 창 높이 = 선택 칸 높이 (펼친 목록 제외)
			if (r.Height() > 0) sh = r.Height();
		}
		row1.push_back({ IDC_BTN_SORTDIR, sh * 12 / 10, sh, 0 });
	}
	if (visible(IDC_COMBO_PAGESIZE)) row1.push_back({ IDC_COMBO_PAGESIZE, DX(42), DY(120), 0 });
	if (visible(IDC_GENRE_HIDE)) row1.push_back({ IDC_GENRE_HIDE, btnH * 13 / 10, btnH, -DY(1) / 2, visible(IDC_GENRE_ALL) });   // 영상 화면 [장르숨김|전체] 붙은 한 묶음 (아이콘 버튼)
	if (visible(IDC_GENRE_ALL))  row1.push_back({ IDC_GENRE_ALL, btnH * 13 / 10, btnH, -DY(1) / 2 });
	if (visible(IDC_ACTOR_GENDER_ALL)) row1.push_back({ IDC_ACTOR_GENDER_ALL, btnH * 13 / 10, btnH, -DY(1) / 2, true });   // 배우 탭 [선택없음|여성만|남성만] 붙은 한 묶음 (아이콘 버튼)
	if (visible(IDC_ACTOR_GENDER_F))   row1.push_back({ IDC_ACTOR_GENDER_F, btnH * 13 / 10, btnH, -DY(1) / 2, true });
	if (visible(IDC_ACTOR_GENDER_M))   row1.push_back({ IDC_ACTOR_GENDER_M, btnH * 13 / 10, btnH, -DY(1) / 2 });
	if (visible(IDC_ACTOR_VIEW_GRID))    row1.push_back({ IDC_ACTOR_VIEW_GRID, btnH * 13 / 10, btnH, -DY(1) / 2, visible(IDC_ACTOR_VIEW_TREEMAP) });   // 배우 탭 [격자|트리맵] 붙은 한 묶음 (아이콘 버튼)
	if (visible(IDC_ACTOR_VIEW_TREEMAP)) row1.push_back({ IDC_ACTOR_VIEW_TREEMAP, btnH * 13 / 10, btnH, -DY(1) / 2 });
	if (visible(IDC_COMBO_TREEMAP_BASIS)) row1.push_back({ IDC_COMBO_TREEMAP_BASIS, DX(48), DY(60), 0 });   // 트리맵 기준 [용량 | 영상수]
	if (visible(IDC_STUDIO_VIEW_GRID)) row1.push_back({ IDC_STUDIO_VIEW_GRID, btnH * 13 / 10, btnH, -DY(1) / 2, visible(IDC_STUDIO_VIEW_LIST) });   // 제작사 탭 [격자|리스트] 붙은 한 묶음 (아이콘 버튼)
	if (visible(IDC_STUDIO_VIEW_LIST)) row1.push_back({ IDC_STUDIO_VIEW_LIST, btnH * 13 / 10, btnH, -DY(1) / 2 });
	{
		// 영상 [격자|리스트|패키지] 붙은 한 묶음 - 아이콘 버튼 (높이보다 조금 넓게)
		const int vw = btnH * 13 / 10;
		if (visible(IDC_VIDEO_MODE_GRID))    row1.push_back({ IDC_VIDEO_MODE_GRID, vw, btnH, -DY(1) / 2, true });
		if (visible(IDC_VIDEO_MODE_LIST))    row1.push_back({ IDC_VIDEO_MODE_LIST, vw, btnH, -DY(1) / 2, true });
		if (visible(IDC_VIDEO_MODE_PACKAGE)) row1.push_back({ IDC_VIDEO_MODE_PACKAGE, vw, btnH, -DY(1) / 2, visible(IDC_VIDEO_MODE_WALL) });
		if (visible(IDC_VIDEO_MODE_WALL))    row1.push_back({ IDC_VIDEO_MODE_WALL, vw, btnH, -DY(1) / 2, visible(IDC_VIDEO_MODE_LABEL) });
		if (visible(IDC_VIDEO_MODE_LABEL))   row1.push_back({ IDC_VIDEO_MODE_LABEL, vw, btnH, -DY(1) / 2 });
	}
	if (visible(IDC_VIDEO_VIEW_H)) row1.push_back({ IDC_VIDEO_VIEW_H, DX(28), btnH, -DY(1) / 2, visible(IDC_VIDEO_VIEW_V) });   // 예전 [가로|세로] (지금은 늘 숨김)
	if (visible(IDC_VIDEO_VIEW_V)) row1.push_back({ IDC_VIDEO_VIEW_V, DX(28), btnH, -DY(1) / 2 });
	if (visible(IDC_ZOOM_SLIDER)) row1.push_back({ IDC_ZOOM_SLIDER, DX(56), btnH, 0 });   // 영상 [리스트] 보기에서는 숨김
	int total = 0;
	for (const Item& it : row1)
		total += it.w + (it.joinNext ? 0 : gap);
	total -= gap;
	// 폭이 모자라면 검색 칸을 줄임
	if (total > w)
	{
		const int cut = (std::min)(row1[0].w - DX(60), total - w);
		row1[0].w -= cut;
		total -= cut;
	}
	int cx = x + (std::max)(0, (w - total) / 2);
	for (const Item& it : row1)
	{
		place(it.id, cx, y + it.dy, it.w, it.h);
		cx += it.w + (it.joinNext ? 0 : gap);
	}
	int yy = y + btnH + gap;

	// 페이지 버튼 + 개수 · 용량 줄 ([레이블] 보기: 둘 다 숨기고 자리도 비우지 않음)
	if (!VideoLabelActive())
	{
		const int pw = DX(18);
		int shown = 0;
		for (int i = 0; i < _countof(m_pageButtons); ++i)
			if (visible(IDC_PAGE_FIRST + i)) ++shown;
		const int pgTotal = shown * pw + (std::max)(0, shown - 1) * 2;
		int px = x + (std::max)(0, (w - pgTotal) / 2);
		for (int i = 0; i < _countof(m_pageButtons); ++i)
		{
			if (!visible(IDC_PAGE_FIRST + i))
				continue;
			place(IDC_PAGE_FIRST + i, px, yy, pw, btnH);
			px += pw + 2;
		}
		yy += btnH + gap / 2;
		place(IDC_STATIC_PAGEINFO, x, yy, w, DY(9));
		yy += DY(9) + gap;
	}
	// 장르(태그) 칩 줄: 페이지 정보 아래 ~ 카드 위 (폭이 모자라면 여러 줄, 줄마다 가운데 정렬)
	if (visible(IDC_GENRE_CHIPS) && m_genreBar.GetSafeHwnd())
	{
		const int side = DX(6);
		const int cw = (std::max)(DX(60), w - side * 2);
		const int ch = m_genreBar.CalcHeight(cw);
		yy += gap / 2;
		place(IDC_GENRE_CHIPS, x + side, yy, cw, ch);
		m_genreBar.Invalidate(FALSE);   // 크기가 같아도 칩 배치(CalcHeight)가 바뀌었을 수 있음
		m_genreBarH = ch;
		yy += ch + gap;
	}
	else
		m_genreBarH = -1;
	return yy - y;
}

void CRuliManagerDlg::LayoutControls(int cx, int cy)
{
	if (!m_layoutReady || cx <= 0 || cy <= 0)
		return;

	const int m = DX(6), gap = DX(4);
	const int btnH = DY(14), rowH = DY(13);

	if (m_mode != MODE_STUDIO)
	{
		// 제작사 탭 상세 전용 컨트롤은 다른 탭에서 숨김 (태그 탭은 상세 영역 배치 전에 끝나므로 여기서)
		if (m_editNamedMemo.GetSafeHwnd()) m_editNamedMemo.ShowWindow(SW_HIDE);
		if (m_labelStrip.GetSafeHwnd() && !IsActorGridMode()) m_labelStrip.ShowWindow(SW_HIDE);   // 배우 상세는 아래에서 정함
		if (m_namedLinks.GetSafeHwnd()) m_namedLinks.ShowWindow(SW_HIDE);
	}

	// 상단 도구줄: [영상][배우][제작사][태그] 토글 버튼 (붙여서) → [폴더 추가] ...
	int x = m, y = m;
	const int radioH = btnH;
	MoveCtrl(IDC_RADIO_VIDEO,  x, y, DX(52), radioH);  x += DX(52) + 2;   // 아이콘 자리만큼 넓게
	MoveCtrl(IDC_RADIO_ACTOR,  x, y, DX(52), radioH);  x += DX(52) + 2;
	MoveCtrl(IDC_RADIO_STUDIO, x, y, DX(60), radioH);  x += DX(60) + 2;
	MoveCtrl(IDC_RADIO_TAG,    x, y, DX(52), radioH);  x += DX(52) + 2;
	MoveCtrl(IDC_RADIO_COLLECTION, x, y, DX(60), radioH);  x += DX(60) + gap * 3;
	MoveCtrl(IDC_BTN_ADDFOLDER,    x, y, DX(55), btnH);  x += DX(55) + gap;   // [폴더 관리] (예전 [폴더 추가] [폴더 제거] 를 합침)
	if (CWnd* rm = GetDlgItem(IDC_BTN_REMOVEFOLDER))
		if (rm->IsWindowVisible()) rm->ShowWindow(SW_HIDE);
	MoveCtrl(IDC_BTN_REFRESH,      x, y, DX(50), btnH);  x += DX(50) + gap;
	MoveCtrl(IDC_BTN_RESCAN,       x, y, DX(40), btnH);  x += DX(40) + gap;
	MoveCtrl(IDC_BTN_ACTORS,       x, y, DX(50), btnH);  x += DX(50) + gap * 3;
	// 검색 / 별점 / 정렬 / 페이지 / 확대는 각 탭 목록 위 도구줄로 옮김 (LayoutListToolbar)
	// 설정 (톱니바퀴): 오른쪽 위 고정, 정사각형
	{
		const int side = DY(14);
		MoveCtrl(IDC_BTN_SETTINGS, cx - m - side, y, side, side);
	}

	// 하단 버튼줄
	const int by = cy - m - btnH;
	x = m;
	MoveCtrl(IDC_BTN_OPENDEFAULT, x, by, DX(76), btnH);  x += DX(76) + gap;
	const UINT bottomIds[] = { IDC_BTN_DELETE, IDC_BTN_EXPLORER, IDC_BTN_OPENDB, IDC_BTN_APPLYDB, IDC_BTN_EXPORTTXT };
	const int bw = DX(62);
	for (UINT id : bottomIds)
	{
		MoveCtrl(id, x, by, bw, btnH);
		x += bw + gap;
	}
	MoveCtrl(IDC_STATIC_STATUS, x + gap, by + DY(3), cx - m - x - gap, DY(9));

	// 가운데 영역: 왼쪽 (분류 목록 +) 동영상 목록 / 오른쪽 이미지 + 상세 (탭 버튼 줄이 맨 위 줄로 올라가 바로 아래부터)
	const int top = y + btnH + DY(4);
	const int bottom = by - gap;
	const int leftW = LeftPaneWidth(cx);
	// 격자(목록) 영역 x: 상세 화면이 오른쪽(기본)이면 왼쪽 끝, 왼쪽이면 오른쪽 끝 (설정 창, m_detailLeft)
	const int lx = (m_detailLeft && !TagLikeMode()) ? (cx - m - leftW) : m;
	CRect gridRc;   // 영상 격자 자리 (연도별 보기에서 나눠 씀)
	if (m_mode != MODE_VIDEO)
	{
		// 배우 격자 / 스튜디오·태그 목록 (검색 · 정렬 · 페이지 도구줄은 격자 머리 영역 - 함께 스크롤)
		const bool drillView = !m_drill.IsEmpty();
		int lt = drillView ? top + btnH + gap : top;   // 영상 화면은 [◀ 목록] 줄 아래
		if (DrillHeaderShown() && m_drillPanel.GetSafeHwnd())
		{
			// 배우 출연작 화면: [◀ 배우 목록] 줄 아래 왼쪽 배우 사진(3:4) + 오른쪽 배우 정보 (스크롤해도 고정)
			const int photoH0 = DY(150);
			const int photoW = photoH0 * 3 / 4;
			const int panelW = (std::max)(DX(80), leftW - photoW - gap * 2);
			const int headerH = (std::max)(photoH0, (std::min)(m_drillPanel.CalcHeight(panelW), DY(190)));
			MoveCtrl(IDC_DRILL_PHOTO, lx, lt, photoW, photoH0);
			MoveCtrl(IDC_DRILL_PANEL, lx + photoW + gap * 2, lt, panelW, headerH);
			lt += headerH + gap;
		}
		else if (NamedHeaderShown() && m_colInfo.GetSafeHwnd())
		{
			// 컬렉션 · 제작사(레이블) 영상 화면: [◀ 목록] 줄 아래 왼쪽 커버 · 로고(16:9) + 오른쪽 제목 · 정보 · 메모 (+ 하위 레이블 이미지) (스크롤해도 고정)
			const int coverH = DY(100);
			const int coverW = coverH * 16 / 9;
			const int panelW = (std::max)(DX(80), leftW - coverW - gap * 2);
			const int headerH = (std::max)(coverH, (std::min)(m_colInfo.CalcHeight(panelW), DY(200)));
			MoveCtrl(IDC_COL_HDR_COVER, lx, lt, coverW, coverH);
			MoveCtrl(IDC_COL_HDR_INFO, lx + coverW + gap * 2, lt, panelW, headerH);
			lt += headerH + gap;
		}
		MoveCtrl(IDC_ACTOR_GRID, lx, lt, leftW, bottom - lt);
		MoveCtrl(IDC_LIST_CATEGORY, lx, lt, leftW, bottom - lt);
		MoveCtrl(IDC_CAT_GRID, lx, lt, leftW, bottom - lt);
		const int countW = DX(40);
		const int nameW = (std::max)(DX(60), (leftW - countW) * 40 / 100);
		m_listCat.SetColumnWidth(0, nameW);
		m_listCat.SetColumnWidth(1, countW);
		m_listCat.SetColumnWidth(2, (std::max)(DX(40), leftW - nameW - countW - ::GetSystemMetrics(SM_CXVSCROLL) - 6));

		// 영상 보기 (위에 [◀ 목록] 줄)
		MoveCtrl(IDC_BTN_ACTOR_BACK, lx, top, DX(70), btnH);
		MoveCtrl(IDC_STATIC_ACTOR_TITLE, lx + DX(74), top + DY(3), leftW - DX(74), DY(9));
		{
			// 배우 출연작 화면: [◀ 배우 목록] 바로 오른쪽에 [격자|연도별] (간격 없이 붙인 아이콘 버튼 한 묶음)
			//  (배우 출연작 화면은 제목 글자를 숨기고 배우 머리 영역을 쓰므로 제목 자리와 겹치지 않음)
			const int vbw = btnH * 13 / 10;
			const int vx = lx + DX(70) + DX(4);
			MoveCtrl(IDC_DRILL_VIEW_GRID, vx, top, vbw, btnH);
			MoveCtrl(IDC_DRILL_VIEW_YEAR, vx + vbw, top, vbw, btnH);
		}
		MoveCtrl(IDC_LIST_VIDEOS, lx, lt, leftW, bottom - lt);
		MoveCtrl(IDC_GRID, lx, lt, leftW, bottom - lt);
		gridRc = CRect(lx, lt, lx + leftW, bottom);
	}
	else
	{
		MoveCtrl(IDC_LIST_VIDEOS, lx, top, leftW, bottom - top);
		MoveCtrl(IDC_GRID, lx, top, leftW, bottom - top);
	}
	// 목록 도구줄: 지금 보이는 격자의 머리 영역으로 (카드와 함께 스크롤)
	{
		CVideoGrid* host = IsActorGridMode() ? &m_actorGrid : (IsCategoryListMode() ? &m_catGrid : &m_grid);
		SetToolbarHost(host);
		if (host->GetSafeHwnd())
		{
			CRect hc;
			host->GetClientRect(&hc);
			const int tbH = LayoutListToolbar(0, gap, hc.Width());
			host->SetHeaderHeight(tbH + gap);
			if (host == &m_actorGrid)
				LayoutTreemap();   // 배우 [트리맵]: 도구줄 아래 전체
			// 영상 탭 [리스트] 보기: 격자는 도구줄 높이만, 그 아래 전체를 목록(스프레드시트)으로
			if (VideoListActive() && host == &m_grid)
			{
				// 영상 탭은 맨 위부터, 배우 · 제작사 · 태그 · 컬렉션 영상 화면은 위쪽 정보(사진 · 커버 등) 아래부터
				const int gTop = gridRc.IsRectEmpty() ? top : static_cast<int>(gridRc.top);
				const int edge = ::GetSystemMetrics(SM_CYEDGE) * 2;   // 격자의 WS_EX_CLIENTEDGE 테두리
				const int headH = (std::min)(bottom - gTop, tbH + gap + edge);
				MoveCtrl(IDC_GRID, lx, gTop, leftW, headH);
				MoveCtrl(IDC_LIST_VIDEOS, lx, gTop + headH, leftW, (std::max)(0, bottom - gTop - headH));
				AutoSizeListColumns();   // 목록 폭에 맞춰 열 폭
			}
		}
	}
	// 배우 출연작 [연도별]: 격자(도구줄 포함)는 숨기고 그 자리 전체를 연도별 보기로
	if (YearViewActive() && m_yearView.GetSafeHwnd() && !gridRc.IsRectEmpty())
	{
		MoveCtrl(IDC_YEAR_VIEW, gridRc.left, gridRc.top, gridRc.Width(), gridRc.Height());
		m_yearView.SetCardSize(YearCardW(), YearCardH());
	}

	// 상세 화면 접기 바: 격자 바로 옆 (상세 화면 쪽), 위아래는 가운데 영역 전체 (태그 · 컬렉션 목록은 상세 화면이 없어 숨김)
	if (m_detailBar.GetSafeHwnd())
	{
		if (TagLikeMode())
		{
			if (m_detailBar.IsWindowVisible())
				m_detailBar.ShowWindow(SW_HIDE);
		}
		else
		{
			const int bx = m_detailLeft ? (lx - DetailBarW()) : (lx + leftW);
			m_detailBar.SetWindowPos(nullptr, bx, top, DetailBarW(), bottom - top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
		}
	}

	if (TagLikeMode())
	{
		// 태그 · 컬렉션 탭: 오른쪽 상세 페이지(이미지 + 정보) 없음
		FlushLayoutRedraw();
		return;
	}

	// 상세 화면: 오른쪽(기본) / 왼쪽 (설정 창), 접기 바 다음 간격 뒤
	int rx = m_detailLeft ? m : (m + leftW + DetailBarW() + gap);
	int rw = cx - 2 * m - gap - DetailBarW() - leftW;
	if (m_detailCollapsed)
	{
		// 접힘: 상세 화면 컨트롤은 크기는 그대로 두고 창 밖으로 (보이기 / 숨기기 처리를 건드리지 않음)
		rw = DX(280);
		rx = m_detailLeft ? (-rw - DX(2000)) : (cx + DX(2000));
	}
	// 상세 정보(편집 칸)는 영상 탭에서만 표시, 다른 탭은 이미지 + 이름/정보 두 줄만
	const bool showDetail = ShowVideoDetail();   // 영상 탭 + 배우 출연작 · 제작사 영상 화면
	// 태그 칩이 여러 줄이면 그만큼 상세 영역을 늘림 (이미지가 줄어듦)
	const int lblW0 = DX(37);   // 라벨 폭 ("스튜디오" 가 잘리지 않게, 상세 글꼴 +1pt)
	// 상세 글꼴(+1pt)에 맞춘 줄 높이 / 라벨 높이
	const int dRowH = (std::max)(rowH, m_detailTmH + 8);          // 품번 · 발매일 등 한 줄 칸
	const int lblH = (std::max)(DY(9), m_detailTmH + 2);          // 라벨 높이
	const int nameStep = (std::max)(DY(11), lblH + 2);            // 파일 이름 줄
	const int imgStep = (std::max)(DY(16), lblH + 6);             // 이미지 줄 ([이미지 검색/변경])
	const int lblOff = (dRowH - lblH) / 2;                        // 한 줄 칸 옆 라벨 세로 위치
	const int chipLblOff = (std::max)(0, (m_detailTmH + 14 - lblH) / 2);   // 칩 첫 줄 옆 라벨 세로 위치
	// 줄 사이 세로 여백: 예전 DY(4) 에서 4pt 줄임 (2pt + 2pt, 최소 1픽셀)
	int vGap = DY(4);
	{
		CClientDC gdc(this);
		const int pt2 = static_cast<int>(std::lround(4.0 * (std::max)(1, gdc.GetDeviceCaps(LOGPIXELSY)) / 72.0));
		vGap = (std::max)(1, DY(4) - pt2);
	}
	const int chipsH = (showDetail && m_tagChips.GetSafeHwnd())
		? (std::max)(rowH, m_tagChips.CalcHeight(rw - lblW0)) : rowH;
	const int actorChipsH = (showDetail && m_actorChips.GetSafeHwnd())
		? (std::max)(rowH, m_actorChips.CalcHeight(rw - lblW0)) : rowH;
	const int studioChipsH = (showDetail && m_studioChips.GetSafeHwnd())
		? (std::max)(rowH, m_studioChips.CalcHeight(rw - lblW0)) : rowH;
	const int labelChipsH = (showDetail && m_labelChips.GetSafeHwnd())
		? (std::max)(rowH, m_labelChips.CalcHeight(rw - lblW0)) : rowH;
	const int seriesChipsH = (showDetail && m_seriesChips.GetSafeHwnd())
		? (std::max)(rowH, m_seriesChips.CalcHeight(rw - lblW0)) : rowH;
	// 배우 카드 높이: 제작사 탭(출연 배우 띠)은 제작 당시 나이 줄을 뺀 높이
	if (m_actorStrip.GetSafeHwnd() && m_stripCardH > 0)
	{
		const int wantH = StripShowsAge() ? m_stripCardH : m_stripCardH - StripAgeH();
		if (wantH != m_stripCardCurH)
		{
			m_stripCardCurH = wantH;
			m_actorStrip.SetCardSize(m_stripCardW, m_stripCardCurH, DX(4));
		}
	}
	const int stripH = ((VideoStripShown() || ShotStripShown()) && m_actorStrip.GetSafeHwnd()) ? m_actorStrip.CalcHeight() : 0;   // 태그 아래 배우 카드 (영상 탭 + 제작사 영상 화면) / 스크린샷 (+ 배우 출연작 화면)
	// 제작사 탭 상세(제작사 선택): 정보 줄 아래 하위 레이블 카드 (여러 줄, 오른쪽 영역 높이의 절반까지)
	int labelStripH = 0;
	if (!showDetail && m_mode == MODE_STUDIO && !IsActorGridMode() && m_labelStrip.GetSafeHwnd() && !m_stripForActor && !m_stripLabels.empty())   // 하위 레이블 · 상위 제작사 카드
		labelStripH = (std::min)(m_labelStrip.CalcWrapHeight(rw), (std::max)(0, (bottom - top) / 2));
	// 배우 상세: 배우 정보 패널 아래 출연작의 제작사 / 레이블 카드 (여러 줄 - 3줄까지 보이고, 4줄 이상이면 세로 스크롤)
	if (IsActorGridMode() && m_labelStrip.GetSafeHwnd() && m_stripForActor && !m_stripLabels.empty())
		labelStripH = (std::min)(m_labelStrip.CalcWrapHeight(rw), m_labelStrip.RowsHeight(3));
	// 제작사 탭 상세(제작사 · 레이블 선택): 정보 줄 아래 링크 줄 (링크가 있을 때만)
	const int namedLinksH = (!showDetail && m_mode == MODE_STUDIO && !IsActorGridMode() && m_namedLinks.GetSafeHwnd() && m_namedLinks.HasUrls())
		? DY(14) * 21 / 20 + 4 : 0;   // 아이콘 = 높이 - 4px = 예전(DY(14) 의 70%)의 1.5배
	// 제작사 탭 상세(제작사 · 레이블 선택): 정보 줄 아래 메모 칸 (약 4줄)
	const int namedMemoH = (!showDetail && m_mode == MODE_STUDIO && !IsActorGridMode() && m_editNamedMemo.GetSafeHwnd() && m_memoKind != 0)
		? DY(40) : 0;
	// 제작사 탭 상세(제작사 · 레이블): 맨 아래 출연 배우 카드 (가로 한 줄, 하위 레이블이 있으면 레이블 카드 아래)
	const int namedActorH = (!showDetail && m_mode == MODE_STUDIO && !IsActorGridMode() && m_actorStrip.GetSafeHwnd() && m_namedActors)
		? m_actorStrip.CalcHeight() : 0;
	// 제목 칸 높이: 글꼴 4줄 + 테두리 / 여백
	int titleH = rowH;
	if (showDetail && m_editTitle.GetSafeHwnd())
	{
		CClientDC tdc(&m_editTitle);
		CFont* old = tdc.SelectObject(m_editTitle.GetFont());
		TEXTMETRIC tm = {};
		tdc.GetTextMetrics(&tm);
		tdc.SelectObject(old);
		titleH = (std::max)(rowH, static_cast<int>(tm.tmHeight) * 4 + 8);
	}
	const int detailH = showDetail ? DY(116) + (nameStep - DY(11)) + (imgStep - DY(16)) + 3 * (dRowH - rowH) - 7 * (DY(4) - vGap) + (titleH - rowH) + ((m_studioCard.GetSafeHwnd() ? m_studioCardH + 2 : studioChipsH) - rowH) + (m_labelCard.GetSafeHwnd() ? m_studioCardH + 2 + vGap : 0) + chipsH + actorChipsH + (dRowH + vGap) + (stripH > 0 ? stripH + gap : 0)   // (dRowH + vGap) = 시리즈 줄   // 파일 / 이미지 / 품번 / 제목 / 별점 / 발매일 / 배우 / 스튜디오 / 태그 (메모 칸 삭제 → 이미지가 커짐)
		: ((IsActorGridMode() && m_actorPanel.GetSafeHwnd()) ? m_actorPanel.CalcHeight(rw) + (labelStripH > 0 ? labelStripH + gap : 0) : DY(26) + (namedLinksH > 0 ? namedLinksH : 0) + (namedMemoH > 0 ? namedMemoH + gap : 0) + (labelStripH > 0 ? labelStripH + gap : 0) + (namedActorH > 0 ? namedActorH + gap : 0));   // 배우 격자: [별점] 줄 추가   // [별칭] 줄은 배우 칩에 합침
	const int previewBottom = (std::max)(top + DY(60), bottom - detailH - gap);
	MoveCtrl(IDC_PREVIEW, rx, top, rw, previewBottom - top);

	const int lblW = lblW0;    // 라벨 폭 ("스튜디오" 가 잘리지 않게, lblW0 과 같게)
	const int infoX = showDetail ? lblW : 0;   // 다른 탭은 [파일] 글자 없이 왼쪽부터
	int dy = previewBottom + gap;
	MoveCtrl(IDC_STATIC_NAME_LBL, rx, dy + DY(2), lblW, showDetail ? lblH : DY(9));
	if (showDetail)
	{
		// 영상: 파일 이름 줄 오른쪽(= [이미지 검색][이미지 변경] 버튼 위)에 fps / 해상도
		const int cbw = DX(54);
		// fps / 해상도 칸은 글자 폭만큼만 차지 (파일 이름이 최대한 보이게)
		const int needW = m_mediaInfo.GetSafeHwnd() ? m_mediaInfo.NeededWidth() : 0;
		const int infoW = (std::min)(needW, cbw * 3 + DX(3));   // 재생 시간 · fps · 해상도 (최대 버튼 3개 폭)
		const int nameGap = (infoW > 0) ? DX(6) : 0;
		MoveCtrl(IDC_STATIC_NAME, rx + infoX, dy + DY(2), rw - infoX - infoW - nameGap, lblH);
		MoveCtrl(IDC_MEDIA_INFO, rx + rw - infoW, dy, infoW, DY(11));
	}
	else
		MoveCtrl(IDC_STATIC_NAME, rx + infoX, dy + DY(2), rw - infoX, DY(9));
	dy += showDetail ? nameStep : DY(11);
	if (showDetail)
	{
		// 영상: 이미지 줄 오른쪽에 [이미지 변경...]
		const int cbw = DX(54);
		MoveCtrl(IDC_STATIC_IMAGE, rx + infoX, dy + DY(2), rw - infoX - cbw * 2 - DX(7), lblH);
		MoveCtrl(IDC_BTN_SEARCHIMAGE, rx + rw - cbw * 2 - DX(3), dy + 4, cbw, DY(13));   // [이미지 검색] [이미지 변경] 4픽셀 아래로
		MoveCtrl(IDC_BTN_CHANGEIMAGE, rx + rw - cbw, dy + 4, cbw, DY(13));
		dy += imgStep;
	}
	else
	{
		MoveCtrl(IDC_STATIC_IMAGE, rx + infoX, dy + DY(2), rw - infoX, DY(9));
		dy += DY(14);
	}
	if (IsActorGridMode() && m_actorPanel.GetSafeHwnd())
	{
		// 배우 상세: 이름 줄 / 정보 줄 대신 패널 (미리보기 바로 아래부터)
		const int py = previewBottom + gap;
		const int stripSpace = (labelStripH > 0) ? labelStripH + gap : 0;
		MoveCtrl(IDC_ACTOR_PANEL, rx, py, rw, (std::max)(0, bottom - py - stripSpace));
		if (m_labelStrip.GetSafeHwnd())
		{
			if (labelStripH > 0)
				MoveCtrl(IDC_LABEL_STRIP, rx, bottom - labelStripH, rw, labelStripH);   // 패널 아래 제작사 / 레이블 카드
			m_labelStrip.ShowWindow(labelStripH > 0 ? SW_SHOW : SW_HIDE);
		}
	}
	// 제작사 탭 상세: 이름 → (정보 줄) → 링크 → 하위 레이블 카드 → 메모 순
	if (m_namedLinks.GetSafeHwnd())
	{
		if (namedLinksH > 0)
		{
			MoveCtrl(IDC_NAMED_LINKS, rx, dy, rw, namedLinksH);   // 정보 줄 아래 링크 아이콘
			m_namedLinks.Invalidate(FALSE);
			dy += namedLinksH;
		}
		m_namedLinks.ShowWindow(namedLinksH > 0 ? SW_SHOW : SW_HIDE);
	}
	if (m_labelStrip.GetSafeHwnd() && !IsActorGridMode())
	{
		if (labelStripH > 0)
		{
			MoveCtrl(IDC_LABEL_STRIP, rx, dy + gap / 2, rw, labelStripH);   // 정보 줄 아래 레이블 카드
			dy += labelStripH + gap;
		}
		m_labelStrip.ShowWindow(labelStripH > 0 ? SW_SHOW : SW_HIDE);
	}
	if (m_editNamedMemo.GetSafeHwnd())
	{
		if (namedMemoH > 0)
		{
			MoveCtrl(IDC_NAMED_MEMO, rx, dy + gap / 2, rw, namedMemoH);   // 카드 아래 메모
			dy += namedMemoH + gap;
		}
		m_editNamedMemo.ShowWindow(namedMemoH > 0 ? SW_SHOW : SW_HIDE);
	}
	if (m_mode == MODE_STUDIO && m_drill.IsEmpty() && m_actorStrip.GetSafeHwnd())   // 제작사 목록 상세 (영상 화면은 아래 태그 밑 배우 카드)
	{
		if (namedActorH > 0)
			MoveCtrl(IDC_ACTOR_STRIP, rx, (std::max)(dy + gap / 2, bottom - namedActorH), rw, namedActorH);   // 맨 아래 출연 배우 카드
		m_actorStrip.ShowWindow(namedActorH > 0 ? SW_SHOW : SW_HIDE);
	}
	if (!showDetail)
	{
		FlushLayoutRedraw();   // 옮긴 컨트롤 영역만
		return;
	}

	MoveCtrl(IDC_STATIC_CODE_LBL, rx, dy + lblOff, lblW, lblH);     // 품번 (제목 위)
	{
		// 품번 칸은 14자 폭 (품번 글꼴 기준 + 테두리 여백), 오른쪽에 시리즈 라벨명
		int codeW = DX(70);
		if (m_editCode.GetSafeHwnd())
		{
			CClientDC cdc(&m_editCode);
			CFont* old = cdc.SelectObject(m_editCode.GetFont());
			codeW = cdc.GetTextExtent(L"ABCDEFG-123456").cx + ::GetSystemMetrics(SM_CXEDGE) * 2 + DX(6);   // 14자
			cdc.SelectObject(old);
		}
		const int copyW = dRowH;   // [복사] 정사각형
		codeW = (std::min)(codeW, (std::max)(DX(30), rw - lblW - copyW - 2));
		MoveCtrl(IDC_EDIT_CODE, rx + lblW, dy, codeW, dRowH);
		MoveCtrl(IDC_BTN_COPYCODE, rx + lblW + codeW + 2, dy, copyW, dRowH);   // 품번 칸 바로 오른쪽
		const int serX = lblW + codeW + 2 + copyW + DX(6);
		MoveCtrl(IDC_STATIC_CODE_SERIES, rx + serX, dy, (std::max)(0, rw - serX), dRowH);
	}
	dy += dRowH + vGap;

	MoveCtrl(IDC_STATIC_TITLE_LBL, rx, dy + lblOff, lblW, lblH);
	MoveCtrl(IDC_EDIT_TITLE, rx + lblW, dy, rw - lblW, titleH);   // 여러 줄 (4줄)
	dy += titleH + vGap;

	MoveCtrl(IDC_STATIC_RATING_LBL, rx, dy + lblOff, lblW, lblH);
	MoveCtrl(IDC_COMBO_RATING, rx + lblW, dy + (dRowH - rowH) / 2, DX(80), rowH);
	MoveCtrl(IDC_DROP_COUNTER, rx + lblW + DX(86), dy + (dRowH - rowH) / 2, DX(40), rowH);   // 별점 오른쪽 물방울 카운트
	MoveCtrl(IDC_BTN_SAVE, rx + rw - DX(50), dy + (dRowH - btnH) / 2, DX(50), btnH);
	dy += dRowH + vGap;

	MoveCtrl(IDC_STATIC_RELEASE_LBL, rx, dy + lblOff, lblW, lblH);
	MoveCtrl(IDC_DATE_RELEASE, rx + lblW, dy, DX(86), dRowH);
	{
		// 발매일 오른쪽: 검열 콤보 (라벨 없이) - 폭 = 가장 긴 항목 글자 + 여백 + ▼ 버튼
		const int cx0 = rx + lblW + DX(92);
		int cw = DX(60);
		if (m_comboCensor.GetSafeHwnd() && m_comboCensor.GetCount() > 0)
		{
			CClientDC cdc(&m_comboCensor);
			CFont* old = cdc.SelectObject(m_comboCensor.GetFont());
			int maxW = 0;
			for (int i = 0; i < m_comboCensor.GetCount(); ++i)
			{
				CString t;
				m_comboCensor.GetLBText(i, t);
				maxW = (std::max)(maxW, static_cast<int>(cdc.GetTextExtent(t).cx));
			}
			cdc.SelectObject(old);
			cw = maxW + 8 + ::GetSystemMetrics(SM_CXVSCROLL) + ::GetSystemMetrics(SM_CXEDGE) * 2 + 4;   // 글자 좌우 여백(4+4) + ▼ + 테두리
		}
		MoveCtrl(IDC_COMBO_CENSOR, cx0, dy, (std::max)(DX(30), (std::min)(cw, rx + rw - cx0)), DY(120));
	}
	dy += dRowH + vGap;

	MoveCtrl(IDC_STATIC_ACTORS_LBL, rx, dy + chipLblOff, lblW, lblH);
	MoveCtrl(IDC_EDIT_ACTORS, rx + lblW, dy, rw - lblW - DX(40), rowH);   // 숨김 (값 보관용)
	MoveCtrl(IDC_ACTOR_CHIPS, rx + lblW, dy, rw - lblW, actorChipsH);
	dy += actorChipsH + vGap;


	// 제작사 / 레이블: 레이블이 있으면 [레이블] 줄만, 없으면 [제작사] 줄만 (같은 자리)
	m_labelRowShown = LabelRowShown();
	MoveCtrl(IDC_EDIT_STUDIO, rx + lblW, dy, rw - lblW - DX(40), rowH);   // 숨김 (값 보관용)
	if (m_studioCard.GetSafeHwnd())
	{
		// 제작사 / 레이블 카드 한 장 (제작사 탭 상세의 카드 모양) + 오른쪽 [변경...]
		//  - 값은 숨긴 제작사 · 레이블 칩이 보관 (레이블이 있으면 레이블 카드, 없으면 제작사 카드)
		SetDlgItemText(IDC_STATIC_STUDIO_LBL, L"제작사");   // 제작사 줄 · 레이블 줄을 따로
		MoveCtrl(IDC_STATIC_STUDIO_LBL, rx, dy + 1 + (m_studioCardH - lblH) / 2, lblW, lblH);   // 카드 세로 가운데
		// 한 줄 카드: 상세 칸 전체 폭 (이미지 + 이름 가로 배치)
		if (m_studioCardW != rw - lblW)
		{
			m_studioCardW = (std::max)(DX(40), rw - lblW);
			m_studioCard.SetCardSize(m_studioCardW, m_studioCardH, 0);
		}
		MoveCtrl(IDC_STUDIO_CARD, rx + lblW, dy + 1, m_studioCardW, m_studioCardH);   // 더블클릭 = 변경 (버튼 없음)
		dy += m_studioCardH + 2 + vGap;
		// 레이블 줄 (제작사 줄 아래, 같은 모양 카드)
		if (m_labelCard.GetSafeHwnd())
		{
			SetDlgItemText(IDC_STATIC_LABEL_LBL, L"레이블");
			MoveCtrl(IDC_STATIC_LABEL_LBL, rx, dy + 1 + (m_studioCardH - lblH) / 2, lblW, lblH);
			m_labelCard.SetCardSize(m_studioCardW, m_studioCardH, 0);
			MoveCtrl(IDC_LABEL_CARD, rx + lblW, dy + 1, m_studioCardW, m_studioCardH);
			dy += m_studioCardH + 2 + vGap;
		}
		const UINT showIds[] = { IDC_STATIC_STUDIO_LBL, IDC_STUDIO_CARD, IDC_STATIC_LABEL_LBL, IDC_LABEL_CARD };
		const UINT hideIds[] = { IDC_STUDIO_CHIPS, IDC_LABEL_CHIPS, IDC_BTN_PICKSTUDIO };
		for (UINT id : showIds) if (CWnd* w = GetDlgItem(id)) w->ShowWindow(SW_SHOW);
		for (UINT id : hideIds) if (CWnd* w = GetDlgItem(id)) w->ShowWindow(SW_HIDE);
		{ m_studioCard.Invalidate(FALSE); if (m_labelCard.GetSafeHwnd()) m_labelCard.Invalidate(FALSE); }
		if (m_labelCard.GetSafeHwnd())
			m_labelCard.Invalidate(FALSE);
	}
	else if (m_labelRowShown)
	{
		MoveCtrl(IDC_STATIC_LABEL_LBL, rx, dy + chipLblOff, lblW, lblH);   // 레이블 (제작사 하위)
		MoveCtrl(IDC_LABEL_CHIPS, rx + lblW, dy, rw - lblW, labelChipsH);
		dy += labelChipsH + vGap;
	}
	else
	{
		MoveCtrl(IDC_STATIC_STUDIO_LBL, rx, dy + chipLblOff, lblW, lblH);
		MoveCtrl(IDC_STUDIO_CHIPS, rx + lblW, dy, rw - lblW, studioChipsH);   // 배우 선택과 같은 칩 입력
		dy += studioChipsH + vGap;
	}

	(void)seriesChipsH;   // 시리즈(품번) 칩은 숨김 (값 보관)
	// 시리즈 이름: 제작사 / 레이블 카드 아래 한 줄 글자 칸 (품번 시리즈와 별개)
	MoveCtrl(IDC_STATIC_SERIES_LBL, rx, dy + lblOff, lblW, lblH);
	MoveCtrl(IDC_EDIT_SERIES, rx + lblW, dy, rw - lblW, dRowH);
	dy += dRowH + vGap;

	MoveCtrl(IDC_STATIC_TAGS_LBL, rx, dy + chipLblOff, lblW, lblH);
	MoveCtrl(IDC_EDIT_TAGS, rx + lblW, dy, rw - lblW - DX(40), rowH);   // 숨김 (값 보관용)
	MoveCtrl(IDC_TAG_CHIPS, rx + lblW, dy, rw - lblW, chipsH);
	dy += chipsH + vGap;

	if (stripH > 0)
	{
		if (ShotStripShown() && m_shotStrip.GetSafeHwnd())
		{
			// 영상 스크린샷: 출연 배우 카드와 같은 높이 안에 가로 m_shotCols 칸 (16:9 칸, 설정 창), 넘치면 세로 스크롤
			//  (세로 스크롤바가 생겨도 그 수만큼 들어가도록 스크롤바 자리 = m_barH(7) + 3 을 빼고 나눔)
			const int gapS = DX(4);
			const int cols = (std::max)(1, m_shotCols);
			const int sw = (std::max)(DX(32), (rw - (m_shotStrip.BarHeight() + 3) - gapS * (cols - 1)) / cols);
			const int sh = sw * 9 / 16;
			if (sh != m_shotCardH || sw != m_shotCardW)
			{
				m_shotCardH = sh;
				m_shotCardW = sw;
				m_shotStrip.SetCardSize(sw, sh, gapS);
				m_shotThumbs.clear();   // 칸 크기가 바뀌면 다시 축소
			}
			MoveCtrl(IDC_SHOT_STRIP, rx, (std::max)(dy, bottom - stripH), rw, stripH);
		}
		else
			MoveCtrl(IDC_ACTOR_STRIP, rx, (std::max)(dy, bottom - stripH), rw, stripH);   // 태그 아래 출연 배우 카드 띠 (전체 폭)
	}

	FlushLayoutRedraw();   // 옮긴 컨트롤 영역만 다시 그림 (그대로인 버튼은 깜빡이지 않음)
}

void CRuliManagerDlg::ApplyDetailFonts()
{
	// 영상 상세보기의 글자 컨트롤(라벨 · 파일 이름 · 이미지 줄 · 품번 · 제목 · 발매일 · 배우 / 스튜디오 / 태그 칩)을 1pt 크게
	if (!m_detailFont.GetSafeHandle())
	{
		LOGFONT lf = {};
		GetFont()->GetLogFont(&lf);
		CClientDC sdc(this);
		const int dpi = (std::max)(1, sdc.GetDeviceCaps(LOGPIXELSY));
		const double pt = std::abs(lf.lfHeight) * 72.0 / dpi;
		lf.lfHeight = -static_cast<LONG>(std::lround((pt + 1.0) * dpi / 72.0));
		m_detailFont.CreateFontIndirect(&lf);
		CFont* old = sdc.SelectObject(&m_detailFont);
		TEXTMETRIC tm = {};
		sdc.GetTextMetrics(&tm);
		sdc.SelectObject(old);
		m_detailTmH = tm.tmHeight;
	}
	const UINT ids[] = {
		IDC_STATIC_NAME_LBL, IDC_STATIC_NAME, IDC_STATIC_IMAGE,
		IDC_STATIC_CODE_LBL, IDC_EDIT_CODE, IDC_STATIC_CODE_SERIES, IDC_STATIC_TITLE_LBL, IDC_EDIT_TITLE,
		IDC_STATIC_RATING_LBL, IDC_STATIC_RELEASE_LBL, IDC_DATE_RELEASE,
		IDC_STATIC_ACTORS_LBL, IDC_STATIC_STUDIO_LBL, IDC_STATIC_LABEL_LBL, IDC_STATIC_SERIES_LBL, IDC_EDIT_SERIES, IDC_STATIC_TAGS_LBL };
	for (UINT id : ids)
		if (CWnd* w = GetDlgItem(id))
			w->SetFont(&m_detailFont, FALSE);
	CTagChipCtrl* chips[] = { &m_actorChips, &m_studioChips, &m_labelChips, &m_seriesChips, &m_tagChips };
	for (CTagChipCtrl* c : chips)
		if (c->GetSafeHwnd())
			c->SetFont(&m_detailFont, FALSE);
}

void CRuliManagerDlg::SetupSuggestions()
{
	// 입력 칸을 직접 입력할 수 있게 (예전에는 [선택...] 전용 읽기 전용)
	CSuggestEdit* edits[] = { &m_editActors, &m_editVAliases, &m_editStudio, &m_editTags };
	for (CSuggestEdit* e : edits)
	{
		e->SetReadOnly(FALSE);
		e->SetColors(kEditColor, kTextColor, kButtonColor, TCOL(TC_HINT));
	}

	// 배우: 이름 + 별칭 (별칭을 고르면 저장할 때 배우 이름 + 참여 별칭으로 정리됨)
	m_editActors.Setup([this](std::vector<SuggestItem>& out)
	{
		for (const ActorInfo& a : m_lib.actors)
		{
			out.push_back({ a.name, a.name });
			for (const CString& al : CVideoLibrary::SplitList(a.aliases))
				out.push_back({ al + L"\t" + a.name + L"의 별칭", al });
		}
	}, true);

	// 별칭: 이 영상 배우의 별칭을 먼저, 그다음 다른 배우의 별칭
	m_editVAliases.Setup([this](std::vector<SuggestItem>& out)
	{
		CString actorsText;
		m_editActors.GetWindowText(actorsText);
		std::set<int> inVideo;
		for (const CString& n : CVideoLibrary::SplitList(actorsText))
		{
			const int idx = m_lib.FindActorByAnyName(n);
			if (idx >= 0)
				inVideo.insert(idx);
		}
		for (int pass = 0; pass < 2; ++pass)
		{
			for (size_t i = 0; i < m_lib.actors.size(); ++i)
			{
				if ((inVideo.count(static_cast<int>(i)) != 0) != (pass == 0))
					continue;
				const ActorInfo& a = m_lib.actors[i];
				for (const CString& al : CVideoLibrary::SplitList(a.aliases))
					out.push_back({ al + L"\t" + a.name, al });
			}
		}
	}, true);
	// 다른 배우의 별칭을 고르면 그 배우를 배우 칸에도 추가 (저장 시 별칭이 정리되지 않도록)
	m_editVAliases.m_onAccept = [this](const SuggestItem& item)
	{
		const int idx = m_lib.FindActorByAnyName(item.value);
		if (idx < 0)
			return;
		CString actorsText;
		m_editActors.GetWindowText(actorsText);
		std::vector<CString> list = CVideoLibrary::SplitList(actorsText);
		for (const CString& n : list)
		{
			if (m_lib.FindActorByAnyName(n) == idx)
				return;   // 이미 있음
		}
		list.push_back(m_lib.actors[idx].name);
		m_editActors.SetWindowText(CVideoLibrary::JoinList(list));
	};

	// 스튜디오 (하나)
	m_editStudio.Setup([this](std::vector<SuggestItem>& out)
	{
		for (const NamedInfo& n : m_lib.studios)
			out.push_back({ n.name, n.name });
	}, false);

	// 태그 (여러 개)
	m_editTags.Setup([this](std::vector<SuggestItem>& out)
	{
		for (const NamedInfo& n : m_lib.tagInfos)
			out.push_back({ n.name, n.name });
	}, true);
}

void CRuliManagerDlg::HideSuggestions()
{
	m_editActors.HidePopup();
	m_editVAliases.HidePopup();
	m_editStudio.HidePopup();
	m_editTags.HidePopup();
	m_tagChips.m_edit.HidePopup();
	m_actorChips.m_edit.HidePopup();
	if (m_studioChips.GetSafeHwnd())
		m_studioChips.m_edit.HidePopup();
	if (m_labelChips.GetSafeHwnd())
		m_labelChips.m_edit.HidePopup();
	if (m_seriesChips.GetSafeHwnd())
		m_seriesChips.m_edit.HidePopup();
}

void CRuliManagerDlg::OnMove(int x, int y)
{
	CDialogEx::OnMove(x, y);
	if (m_layoutReady)
		HideSuggestions();   // 창을 옮기면 후보 목록이 따로 떠 있지 않도록 닫음
}

BOOL CRuliManagerDlg::OnDeviceChange(UINT nEventType, DWORD_PTR dwData)
{
	// USB · 외장 하드를 꽂거나 빼면: 이동식 디스크 폴더의 연결 상태와 드라이브 문자를 다시 확인
	if ((nEventType == DBT_DEVICEARRIVAL || nEventType == DBT_DEVICEREMOVECOMPLETE) && dwData &&
		reinterpret_cast<const DEV_BROADCAST_HDR*>(dwData)->dbch_devicetype == DBT_DEVTYP_VOLUME && !m_lib.removableFolders.empty())
	{
		CommitDetails();
		if (nEventType == DBT_DEVICEARRIVAL)
			ForgetFailedMediaInfo();   // 연결 안 된 상태에서 읽지 못한 해상도 · 재생 시간을 다시 읽게
		const int keepIdx = m_curItem;   // 항목 순서는 그대로, 경로만 바뀜
		if (m_lib.RemapRemovableDrives() > 0)   // 다른 문자로 연결됨 → 경로 바뀜
		{
			m_lib.Save();
			ResetThumbnails();
			ApplyFilter();
			if (keepIdx >= 0 && keepIdx < static_cast<int>(m_lib.items.size()))
				SelectPath(m_lib.items[keepIdx].path);
		}
		if (m_grid.GetSafeHwnd())
			m_grid.Invalidate(FALSE);   // 카드의 "이동식 디스크 (연결 안 됨)" 표시
		if (m_curItem >= 0)
			ShowDetails(m_curItem);   // 연결 / 해제에 따라 상세 수정 가능 여부 다시 적용
	}
	return CDialogEx::OnDeviceChange(nEventType, dwData);
}

void CRuliManagerDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialogEx::OnSize(nType, cx, cy);
	if (m_layoutReady)
		HideSuggestions();
	if (nType != SIZE_MINIMIZED)
		LayoutControls(cx, cy);
}

void CRuliManagerDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	UINT dpi = 96;
	if (GetSafeHwnd())
	{
		const UINT d = ::GetDpiForWindow(m_hWnd);
		if (d) dpi = d;
	}
	lpMMI->ptMinTrackSize.x = MulDiv(900, dpi, 96);
	lpMMI->ptMinTrackSize.y = MulDiv(720, dpi, 96);
	CDialogEx::OnGetMinMaxInfo(lpMMI);
}

// ---------------------------------------------------------------------------
// 목록 / 필터 / 정렬

int CRuliManagerDlg::GetSelectedRow() const
{
	return m_list.GetNextItem(-1, LVNI_SELECTED);
}

int CRuliManagerDlg::GetSelectedItem() const
{
	const int row = GetSelectedRow();
	if (row < 0 || row >= static_cast<int>(m_view.size()))
		return -1;
	return m_view[row];
}

bool CRuliManagerDlg::IsItemOffline(int idx) const
{
	// 연결 해제된 이동식 디스크의 영상: DB 삭제 말고는 수정 불가 (상세 · 오른쪽 클릭 메뉴)
	if (idx < 0 || idx >= static_cast<int>(m_lib.items.size()))
		return false;
	bool online = true;
	return m_lib.IsRemovableItem(m_lib.items[idx].path, &online) && !online;
}

void CRuliManagerDlg::BeginLibraryChange()
{
	// 항목 인덱스가 바뀌기 전에 목록과 상세 표시를 비웁니다.
	CommitDetails();
	m_list.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	m_view.clear();
	BuildMergedView(false);
	m_list.SetItemCountEx(0);
	m_grid.Refresh();
	ShowDetails(-1);
	MarkCategoriesDirty();
}

void CRuliManagerDlg::ApplyFilter()
{
	CString keepPath;
	if (m_curItem >= 0 && m_curItem < static_cast<int>(m_lib.items.size()))
		keepPath = m_lib.items[m_curItem].path;

	CString query;
	m_editSearch.GetWindowText(query);
	query.Trim();
	query.MakeLower();

	// 공백으로 구분된 검색어는 모두 포함해야 일치 (AND)
	std::vector<CString> terms;
	int pos = 0;
	CString token = query.Tokenize(L" ", pos);
	while (!token.IsEmpty())
	{
		terms.push_back(token);
		token = query.Tokenize(L" ", pos);
	}

	const int minRating = 0;   // 별점 거르기 콤보는 없앰 (늘 전체) - 예전: 콤보 선택 × 2 (반 별 단위)

	// 배우 이름/별칭 어느 것으로도 검색되도록: 영상의 배우 표기(이름 또는 별칭, 소문자) → 이름, 별칭
	std::map<CString, CString> aliasMap;
	if (!terms.empty())
	{
		for (const ActorInfo& a : m_lib.actors)
		{
			if (a.aliases.IsEmpty()) continue;
			const CString all = a.name + L"," + a.aliases;
			CString key = a.name;
			key.MakeLower();
			aliasMap[key] = all;
			for (const CString& s : CVideoLibrary::SplitList(a.aliases))
			{
				CString sk = s;
				sk.MakeLower();
				aliasMap.emplace(sk, all);
			}
		}
	}
	// 배우의 출연작 보기: 영상의 표기가 그 배우의 이름이거나 별칭이면 포함
	const std::map<CString, int> actorIndex = (m_mode == MODE_ACTOR) ? m_lib.ActorNameIndex() : std::map<CString, int>();
	CString actorTarget = m_catValue;
	actorTarget.MakeLower();

	m_list.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	// 카드의 순번 (1/3): 같은 폴더 · 같은 묶음 이름(마지막 '_' 왼쪽)의 파일 수
	m_partTotal.clear();
	for (const VideoItem& pv : m_lib.items)
	{
		CString key;
		int num = 0;
		SplitPartName(pv.path, key, num);
		++m_partTotal[key];
	}
	m_view.clear();
	for (size_t i = 0; i < m_lib.items.size(); ++i)
	{
		const VideoItem& v = m_lib.items[i];
		if (v.rating < minRating)
			continue;
		if (m_mode != MODE_VIDEO && m_catKind != CAT_ALL)
		{
			const std::vector<CString> vals = GetCategoryValues(v, m_mode);
			if (m_catKind == CAT_LABEL)
			{
				if (v.label.IsEmpty() || v.label.CompareNoCase(m_catValue) != 0) continue;   // 레이블의 영상
			}
			else if (m_catKind == CAT_NONE)
			{
				if (!vals.empty()) continue;
			}
			else
			{
				bool hit = false;
				CString lb = v.label;
				lb.Trim();
				if (m_mode == MODE_STUDIO && !lb.IsEmpty())
				{
					// 제작사 카드의 영상 목록: 레이블 영상은 그 레이블의 상위 제작사가 이 제작사일 때 (하위 레이블 포함 - 카드 합계와 같게)
					const int li = m_lib.FindLabel(lb);
					CString parent = (li >= 0) ? m_lib.labelInfos[li].parent : CString();
					parent.Trim();
					if (parent.IsEmpty())
						parent = v.studio;   // 등록되지 않았거나 상위가 없는 레이블 → 영상에 지정한 제작사
					parent.Trim();
					hit = parent.CompareNoCase(m_catValue) == 0;
				}
				else
				{
					for (const CString& n : vals)
					{
						if (n.CompareNoCase(m_catValue) == 0 ||
							(m_mode == MODE_ACTOR && m_lib.ActorKeyOf(actorIndex, n) == actorTarget)) { hit = true; break; }
					}
				}
				if (!hit) continue;
			}
		}
		if (!terms.empty())
		{
			CString hay = v.FileName() + L"\n" + v.code + L"\n" + v.title + L"\n" + v.release + L"\n" + v.actors + L"\n" + v.actorAliases + L"\n" + v.studio + L"\n" + v.label + L"\n" + v.series + L"\n" + v.seriesTitle + L"\n" + v.tags;   // 영상 메모 기능 삭제
			if (!v.studio.IsEmpty())
			{
				const int sn = m_lib.FindNamed(LIST_STUDIO, v.studio);   // 스튜디오 서브이름으로도 검색
				if (sn >= 0 && !m_lib.studios[sn].subName.IsEmpty())
					hay += L"\n" + m_lib.studios[sn].subName;
			}
			if (!aliasMap.empty() && !v.actors.IsEmpty())
			{
				for (const CString& n : CVideoLibrary::SplitList(v.actors))
				{
					CString key = n;
					key.MakeLower();
					auto it = aliasMap.find(key);
					if (it != aliasMap.end())
						hay += L"\n" + it->second;
				}
			}
			hay.MakeLower();
			bool ok = true;
			for (const CString& t : terms)
			{
				if (hay.Find(t) < 0) { ok = false; break; }
			}
			if (!ok) continue;
		}
		m_view.push_back(static_cast<int>(i));
	}
	// 영상 탭 장르 칩: 지금까지 걸러진 영상 기준으로 태그별 건수 → 고른 태그가 있으면 그 태그 영상만
	if (GenreBarActive())
	{
		SyncGenreKey();
		const CString& genreSel = m_genreSel[GenreSlot()];
		const std::vector<int> before = m_view;
		UpdateGenreBar(before);
		if (!genreSel.IsEmpty())
		{
			m_view.clear();
			for (int i : before)
			{
				for (const CString& t : CVideoLibrary::SplitList(m_lib.items[i].tags))
				{
					CString tt = t;
					tt.Trim();
					if (tt.CompareNoCase(genreSel) == 0) { m_view.push_back(i); break; }
				}
			}
		}
	}

	SortView();
	if (VideoLabelActive())
		GroupViewByLabel();   // [레이블]: 품번 DB 의 레이블 순번으로 고정 (정렬 콤보 무시)
	// 분할 영상 합쳐서 보기: 같은 묶음 파트를 첫 파트 자리에 번호 순으로 붙임 (페이지 · 격자는 카드 단위)
	const bool merge = m_mergeParts && !YearViewActive();
	BuildMergedView(merge);
	// 페이지 나누기: 정렬된 전체 중 지금 페이지만 표시 (m_view = 그 페이지의 항목)
	m_totalVideoBytes = 0;
	for (int i : m_view)
		m_totalVideoBytes += m_lib.items[i].size;
	m_totalVideo = merge ? static_cast<int>(m_gridRows.size()) : static_cast<int>(m_view.size());   // 합쳐 보기: 카드 수
	{
		int first = 0, last = m_totalVideo;
		// 배우 격자 등에서 불려도 영상 페이지 (영상 탭 목록은 영상 탭의 페이지당 개수, 출연작/영상 화면은 지금 탭 값)
		if (m_drill.IsEmpty())
			SlicePage(m_pageVideoMain, m_totalVideo, first, last, m_pageSizes[MODE_VIDEO]);
		else if (!YearViewActive())   // 연도별 보기는 페이지를 나누지 않고 전부
			SlicePage(m_pageDrill, m_totalVideo, first, last);
		if (first > 0 || last < m_totalVideo)
		{
			if (merge)
			{
				// 카드 범위 → 행 범위 (묶음은 붙어 있으므로 카드 경계에서 자름)
				const int rowFirst = m_gridRows[first];
				const int rowLast = (last < static_cast<int>(m_gridRows.size())) ? m_gridRows[last] : static_cast<int>(m_view.size());
				m_view = std::vector<int>(m_view.begin() + rowFirst, m_view.begin() + rowLast);
				BuildMergedView(true);   // 이 페이지의 칸 표 (순서는 이미 묶음끼리 붙어 있어 그대로)
			}
			else
				m_view = std::vector<int>(m_view.begin() + first, m_view.begin() + last);
		}
	}
	m_list.SetItemCountEx(static_cast<int>(m_view.size()), LVSICF_NOSCROLL);
	m_list.Invalidate(FALSE);
	AutoSizeListColumns();   // 세로 스크롤바가 생기거나 없어지면 폭이 바뀜
	UpdateLabelLines();      // [레이블]: 줄 배치 + 묶음 머리줄 (아니면 지움)
	m_grid.Refresh();

	if (!keepPath.IsEmpty())
		SelectPath(keepPath);

	// 선택하던 동영상이 목록에서 빠졌으면 상세 표시도 비움
	if (GetSelectedItem() < 0 && m_curItem >= 0)
	{
		CommitDetails();
		ShowDetails(-1);
	}

	UpdateStatus();
	UpdatePager();
	if (GenreBarActive())
		RelayoutToolbarHeader();   // 장르 칩 줄 높이 · 페이지 버튼이 정해진 뒤 머리 영역 다시 (겹침 방지)
	if (YearViewActive())
		RebuildYearView();
}

void CRuliManagerDlg::SortView()
{
	const auto& items = m_lib.items;
	const int col = m_sortColumn;
	const bool asc = m_sortAsc;
	// 배우수 정렬: 영상마다 참여 배우 수를 미리 셈 (비교할 때마다 나누지 않게)
	std::vector<int> actorCount;
	if (col == SORT_ACTORCOUNT)
	{
		actorCount.assign(items.size(), 0);
		for (int i : m_view)
			if (i >= 0 && i < static_cast<int>(items.size()))
				actorCount[i] = static_cast<int>(CVideoLibrary::SplitList(items[i].actors).size());
	}
	// 재생 시간 · 해상도 정렬: 읽어 둔 미디어 정보(카드 · 리스트에 표시한 것)만 - 모르면 0
	std::vector<ULONGLONG> mediaKey;
	if (col == SORT_DURATION || col == SORT_RES)
	{
		mediaKey.assign(items.size(), 0);
		for (int i : m_view)
		{
			if (i < 0 || i >= static_cast<int>(items.size()))
				continue;
			CString key = items[i].path;
			key.MakeLower();
			auto mit = m_cardMedia.find(key);
			if (mit != m_cardMedia.end() && mit->second.ok)
				mediaKey[i] = (col == SORT_DURATION) ? mit->second.duration100ns : static_cast<ULONGLONG>(mit->second.width) * mit->second.height;
		}
	}

	std::stable_sort(m_view.begin(), m_view.end(), [&](int a, int b)
	{
		const VideoItem& x = items[a];
		const VideoItem& y = items[b];
		// 저장된 항목을 먼저, 임시 항목(정보 저장 전)은 뒤에 (정렬 방향과 상관없이)
		if (x.pending != y.pending)
			return !x.pending;
		int c = 0;
		switch (col)
		{
		case COL_SIZE:   c = (x.size < y.size) ? -1 : (x.size > y.size ? 1 : 0); break;
		case COL_DATE:   c = (x.modified < y.modified) ? -1 : (x.modified > y.modified ? 1 : 0); break;
		case COL_RATING: c = x.rating - y.rating; break;
		case COL_TITLE:  c = ::StrCmpLogicalW(x.title, y.title); break;
		case COL_RELEASE: c = x.release.Compare(y.release); break;   // YYYY-MM-DD 이므로 문자열 비교 = 날짜 비교
		case COL_ACTORS: c = ::StrCmpLogicalW(x.actors, y.actors); break;
		case COL_STUDIO: c = ::StrCmpLogicalW(x.studio, y.studio); break;
		case COL_TAGS:   c = ::StrCmpLogicalW(x.tags, y.tags); break;
		case COL_PATH:   c = ::StrCmpLogicalW(x.path, y.path); break;
		default:
			if (col == SORT_ACTORCOUNT)
				c = actorCount[a] - actorCount[b];   // 참여 배우 수
			else if (col == SORT_CODE)
				c = ::StrCmpLogicalW(x.code, y.code);
			else if (col == SORT_LABEL)
				c = ::StrCmpLogicalW(x.label, y.label);
			else if (col == SORT_DURATION || col == SORT_RES)
				c = (mediaKey[a] < mediaKey[b]) ? -1 : (mediaKey[a] > mediaKey[b] ? 1 : 0);
			break;
		}
		if (c == 0)
			c = ::StrCmpLogicalW(::PathFindFileNameW(x.path), ::PathFindFileNameW(y.path));
		return asc ? (c < 0) : (c > 0);
	});
}

void CRuliManagerDlg::UpdateSortArrows()
{
	CHeaderCtrl* header = m_list.GetHeaderCtrl();
	if (!header) return;
	const int n = header->GetItemCount();
	for (int i = 0; i < n; ++i)
	{
		HDITEM hd = {};
		hd.mask = HDI_FORMAT;
		header->GetItem(i, &hd);
		hd.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
		if (i < LC_COUNT && kListSort[i] == m_sortColumn)   // [리스트] 열 → 정렬 기준
			hd.fmt |= m_sortAsc ? HDF_SORTUP : HDF_SORTDOWN;
		header->SetItem(i, &hd);
	}
}

void CRuliManagerDlg::SelectPath(const CString& path)
{
	if (path.IsEmpty()) return;
	for (size_t row = 0; row < m_view.size(); ++row)
	{
		if (m_lib.items[m_view[row]].path.CompareNoCase(path) == 0)
		{
			const int r = static_cast<int>(row);
			m_list.SetItemState(r, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			m_list.EnsureVisible(r, FALSE);
			return;
		}
	}
}

void CRuliManagerDlg::UpdateStatus()
{
	if (IsActorGridMode())
	{
		CString a;
		a.Format(L"배우 %d명 (표시 %d명) · 더블클릭하면 출연작 보기",
			static_cast<int>(m_lib.actors.size()), static_cast<int>(m_actorRows.size()));
		m_staticStatus.SetWindowText(a);
		return;
	}
	if (IsCategoryListMode())
	{
		CString c;
		c.Format(L"%s %d개 · 더블클릭하면 영상 보기",
			m_mode == MODE_STUDIO ? L"제작사" : (m_mode == MODE_COLLECTION ? L"컬렉션" : L"태그"),
			static_cast<int>(m_mode == MODE_COLLECTION ? m_lib.collectionInfos.size() : m_lib.NamedList(NamedKind()).size()));
		m_staticStatus.SetWindowText(c);
		return;
	}
	CString s;
	const int pendingCount = m_lib.PendingCount();
	s.Format(L"DB %d개 · 임시 %d개 · 표시 %d개 · 등록 폴더 %d개",
		static_cast<int>(m_lib.items.size()) - pendingCount, pendingCount,
		static_cast<int>(m_view.size()),
		static_cast<int>(m_lib.folders.size()));
	m_staticStatus.SetWindowText(s);
}

// ---------------------------------------------------------------------------
// 페이지 나누기 (각 탭 목록 위 도구줄)

void CRuliManagerDlg::ApplyTabPageSize()
{
	if (m_mode < MODE_VIDEO || m_mode >= MODE_COUNT)
		return;
	m_pageSize = m_drillFilters ? m_drillPageSizes[m_drillCtx] : m_pageSizes[m_mode];   // 출연작 / 제작사 영상 화면은 따로
	for (int i = 0; i < m_comboPageSize.GetCount(); ++i)
	{
		if (static_cast<int>(m_comboPageSize.GetItemData(i)) == m_pageSize)
		{
			m_comboPageSize.SetCurSel(i);
			break;
		}
	}
}

int& CRuliManagerDlg::CurPageRef()
{
	if (IsActorGridMode())
		return m_pageActor;
	if (IsCategoryListMode())
		return m_pageCat;
	return m_drill.IsEmpty() ? m_pageVideoMain : m_pageDrill;
}

int CRuliManagerDlg::CurTotal() const
{
	if (IsActorGridMode())
		return m_totalActor;
	if (IsCategoryListMode())
		return m_totalCat;
	return m_totalVideo;
}

void CRuliManagerDlg::SlicePage(int& page, int total, int& first, int& last, int size) const
{
	if (size < 0)
		size = EffPageSize();
	const int pages = PageCount(total, size);
	page = (std::max)(0, (std::min)(page, pages - 1));   // 개수가 줄었으면 마지막 페이지로
	if (size <= 0)
	{
		first = 0;
		last = total;
		return;
	}
	first = (std::min)(total, page * size);
	last = (std::min)(total, first + size);
}

void CRuliManagerDlg::UpdatePager()
{
	if (!GetDlgItem(IDC_PAGE_FIRST))
		return;
	const int total = CurTotal();
	const int pages = PageCount(total);
	int& pageRef = CurPageRef();
	pageRef = (std::max)(0, (std::min)(pageRef, pages - 1));
	const int page = pageRef;

	// 번호 버튼: 지금 페이지를 가운데로 최대 5개
	const bool treemap = ActorTreemapActive() || VideoLabelActive();   // 배우 [트리맵] · 영상 [레이블]: 페이지 없이 전체 → 페이지 버튼 숨김
	const int nShow = treemap ? 0 : (std::min)(5, pages);
	const int start = (std::max)(0, (std::min)(page - 2, pages - nShow));
	bool relayout = false;
	auto show = [&](UINT id, bool on)
	{
		CWnd* w = GetDlgItem(id);
		const bool was = (w->GetStyle() & WS_VISIBLE) != 0;
		if (was != on)
		{
			w->ShowWindow(on ? SW_SHOW : SW_HIDE);
			relayout = true;
		}
	};
	for (int i = 0; i < 5; ++i)
	{
		const UINT id = IDC_PAGE_N1 + i;
		const bool on = i < nShow;
		show(id, on);
		if (on)
		{
			CString t;
			t.Format(L"%d", start + i + 1);
			CString cur;
			GetDlgItemText(id, cur);
			if (cur != t)
				SetDlgItemText(id, t);
			m_pageButtons[2 + i].SetChecked(start + i == page);   // 지금 페이지 = 파란색
		}
	}
	const bool many = !treemap && pages > 1;
	show(IDC_PAGE_FIRST, many);
	show(IDC_PAGE_PREV, many);
	show(IDC_PAGE_NEXT, many);
	show(IDC_PAGE_LAST, many);
	GetDlgItem(IDC_PAGE_FIRST)->EnableWindow(page > 0);
	GetDlgItem(IDC_PAGE_PREV)->EnableWindow(page > 0);
	GetDlgItem(IDC_PAGE_NEXT)->EnableWindow(page < pages - 1);
	GetDlgItem(IDC_PAGE_LAST)->EnableWindow(page < pages - 1);
	for (int i : { 0, 1, 7, 8 })
		m_pageButtons[i].SetChecked(false);

	// 개수 요약: 1-250 / 1,251개 (영상은 전체 크기)
	auto withComma = [](int n) -> CString
	{
		CString s;
		s.Format(L"%d", n);
		for (int i = s.GetLength() - 3; i > 0; i -= 3)
			s.Insert(i, L',');
		return s;
	};
	int first = 0, last = 0;
	int p = page;
	SlicePage(p, total, first, last);
	CString info;
	if (total <= 0)
		info = L"0개";
	else if (treemap)
		info.Format(L"배우 %s명", static_cast<LPCWSTR>(withComma(total)));
	else
		info.Format(L"%s-%s / %s개", static_cast<LPCWSTR>(withComma(first + 1)), static_cast<LPCWSTR>(withComma(last)), static_cast<LPCWSTR>(withComma(total)));
	if (!IsActorGridMode() && !IsCategoryListMode() && total > 0)
		info += L"  (" + FormatSize(m_totalVideoBytes) + L")";
	CString cur;
	GetDlgItemText(IDC_STATIC_PAGEINFO, cur);
	if (cur != info)
		SetDlgItemText(IDC_STATIC_PAGEINFO, info);
	show(IDC_STATIC_PAGEINFO, !VideoLabelActive());   // [레이블]: 개수 · 용량 줄 숨김

	if (relayout && m_layoutReady)
	{
		CRect client;
		GetClientRect(&client);
		LayoutControls(client.Width(), client.Height());
	}
}

void CRuliManagerDlg::OnPageButton(UINT nID)
{
	const int total = CurTotal();
	const int pages = PageCount(total);
	int& page = CurPageRef();
	int want = page;
	switch (nID)
	{
	case IDC_PAGE_FIRST: want = 0; break;
	case IDC_PAGE_PREV:  want = page - 1; break;
	case IDC_PAGE_NEXT:  want = page + 1; break;
	case IDC_PAGE_LAST:  want = pages - 1; break;
	default:
	{
		CString t;
		GetDlgItemText(nID, t);
		want = _wtoi(t) - 1;
		break;
	}
	}
	want = (std::max)(0, (std::min)(want, pages - 1));
	if (want == page)
		return;
	page = want;
	CommitDetails();
	if (IsActorGridMode())
	{
		RebuildActorGrid();
		if (!m_actorRows.empty()) m_actorGrid.EnsureVisible(0);
	}
	else if (IsCategoryListMode())
	{
		RebuildCategories();
		if (!m_catRows.empty()) { m_catGrid.EnsureVisible(0); m_listCat.EnsureVisible(0, FALSE); }
	}
	else
	{
		ApplyFilter();
		if (!m_view.empty()) { m_grid.EnsureVisible(0); m_list.EnsureVisible(0, FALSE); }
	}
}

void CRuliManagerDlg::OnCbnSelchangePageSize()
{
	const int sel = m_comboPageSize.GetCurSel();
	if (sel < 0)
		return;
	const int size = static_cast<int>(m_comboPageSize.GetItemData(sel));
	if (size == m_pageSize)
		return;
	// 지금 탭의 목록만: 보던 첫 항목이 들어 있는 페이지로
	auto keep = [&](int& page)
	{
		const int firstIdx = (m_pageSize > 0) ? page * m_pageSize : 0;
		page = (size > 0) ? firstIdx / size : 0;
	};
	if (m_drillFilters)
	{
		// 배우 출연작 화면: 그 화면 값만 따로 저장
		keep(m_pageDrill);
		m_pageSize = size;
		m_drillPageSizes[m_drillCtx] = size;
		AfxGetApp()->WriteProfileInt(L"Settings", DrillKey(L"PageSize"), size);
		CommitDetails();
		ApplyFilter();
		return;
	}
	if (m_mode == MODE_VIDEO)
		keep(m_pageVideoMain);
	keep(m_pageDrill);
	if (m_mode == MODE_ACTOR)
		keep(m_pageActor);
	if (m_mode == MODE_STUDIO || m_mode == MODE_TAG || m_mode == MODE_COLLECTION)
		keep(m_pageCat);
	m_pageSize = size;
	m_pageSizes[m_mode] = size;   // 이 탭에만 저장
	CString key;
	key.Format(L"PageSize%d", m_mode);
	AfxGetApp()->WriteProfileInt(L"Settings", key, m_pageSize);
	CommitDetails();
	ApplyFilter();
	if (IsActorGridMode())
		RebuildActorGrid();
	else if (IsCategoryListMode())
		RebuildCategories();
}

void CRuliManagerDlg::OnEnChangeSearch()
{
	ResetCurPage();   // 검색어가 바뀌면 첫 페이지부터
	ApplyFilter();
	if (IsActorGridMode())
		RebuildActorGrid();   // 배우 격자에서는 이름/별칭 검색
	else if (IsCategoryListMode())
		RebuildCategories();  // 스튜디오/태그 목록에서는 이름 검색
}

void CRuliManagerDlg::OnCbnSelchangeFilter()
{
	if (m_drillFilters)
		AfxGetApp()->WriteProfileInt(L"Settings", DrillKey(L"Rating"), (std::max)(0, m_comboFilter.GetCurSel()));   // 출연작 / 제작사 영상 화면 별점 (따로 저장)
	ResetCurPage();
	ApplyFilter();
}

void CRuliManagerDlg::SetSortDirButton(bool asc)
{
	// 정렬 방향 버튼: 글자 대신 아이콘 (위 화살표 + 짧은→긴 막대 / 아래 화살표 + 긴→짧은 막대), 이름은 툴팁
	const wchar_t* name = asc ? L"오름차순" : L"내림차순";
	SetDlgItemText(IDC_BTN_SORTDIR, name);
	for (CDarkButton& b : m_darkButtons)
		if (b.GetSafeHwnd() && b.GetDlgCtrlID() == IDC_BTN_SORTDIR)
		{
			b.SetIcon(asc ? CDarkButton::ICON_SORT_ASC : CDarkButton::ICON_SORT_DESC);
			b.SetTooltip(name);
		}
}

void CRuliManagerDlg::UpdateSortUI()
{
	// 정렬 콤보: 배우 격자는 [이름 | 작품수], 제작사 · 태그 카드는 [이름 | 영상수], 그 외(영상)는 영상 정렬 기준
	const int kind = IsActorGridMode() ? 1 : (IsCatCardMode() ? 2 : 0);
	if (kind != m_sortComboKind)
	{
		m_sortComboKind = kind;
		m_comboSort.ResetContent();
		if (kind == 1)
		{
			m_comboSort.AddString(L"이름");
			m_comboSort.AddString(L"작품수");
		}
		else if (kind == 2)
		{
			m_comboSort.AddString(L"이름");
			m_comboSort.AddString(L"영상수");
		}
		else
		{
			const wchar_t* const sortNames[] = { L"이름", L"제목", L"크기", L"수정일", L"별점",
			                                     L"발매일", L"배우", L"제작사", L"태그", L"경로", L"배우수", L"품번", L"레이블", L"재생 시간", L"해상도" };   // 배우수 = 참여 배우 수 (SORT_ACTORCOUNT)
			for (const wchar_t* n : sortNames)
				m_comboSort.AddString(n);
		}
	}
	if (kind == 1)
	{
		m_comboSort.SetCurSel(m_actorSortCol);
		SetSortDirButton(m_actorSortAsc);
		AfxGetApp()->WriteProfileInt(L"Settings", L"ActorSortColumn", m_actorSortCol);
		AfxGetApp()->WriteProfileInt(L"Settings", L"ActorSortAsc", m_actorSortAsc ? 1 : 0);
		return;
	}
	if (kind == 2)
	{
		// 제작사 / 태그 / 컬렉션 탭은 따로 기억
		m_comboSort.SetCurSel(CatSortCol());
		SetSortDirButton(CatSortAsc());
		AfxGetApp()->WriteProfileInt(L"Settings", CString(CatSortKey()) + L"Column", CatSortCol());
		AfxGetApp()->WriteProfileInt(L"Settings", CString(CatSortKey()) + L"Asc", CatSortAsc() ? 1 : 0);
		return;
	}
	m_comboSort.SetCurSel(m_sortColumn);
	SetSortDirButton(m_sortAsc);
	// 배우 출연작 화면은 따로 저장
	AfxGetApp()->WriteProfileInt(L"Settings", m_drillFilters ? DrillKey(L"SortColumn") : CString(L"SortColumn"), m_sortColumn);
	AfxGetApp()->WriteProfileInt(L"Settings", m_drillFilters ? DrillKey(L"SortAsc") : CString(L"SortAsc"), m_sortAsc ? 1 : 0);
	UpdateSortArrows();   // [리스트] 열 머리글의 정렬 삼각형도
}

void CRuliManagerDlg::OnCbnSelchangeSort()
{
	const int sel = m_comboSort.GetCurSel();
	if (IsActorGridMode())
	{
		// 배우 격자: 이름 = 가나다순부터, 작품수 = 많은 순부터
		if (sel < 0 || sel > 1 || sel == m_actorSortCol)
			return;
		m_actorSortCol = sel;
		m_actorSortAsc = (sel == 0);
		m_pageActor = 0;
		UpdateSortUI();
		RebuildActorGrid();
		return;
	}
	if (IsCatCardMode())
	{
		// 제작사 · 태그 카드: 이름 = 가나다순부터, 영상수 = 많은 순부터
		if (sel < 0 || sel > 1 || sel == CatSortCol())
			return;
		CatSortCol() = sel;
		CatSortAsc() = (sel == 0);
		m_pageCat = 0;
		UpdateSortUI();
		RebuildCategories();
		return;
	}
	if (sel < 0 || sel == m_sortColumn)
		return;
	m_sortColumn = sel;
	// 날짜/크기/별점은 큰 값(최신)부터, 나머지는 가나다순부터
	m_sortAsc = !SortDescFirst(m_sortColumn);   // 배우수 · 재생 시간 · 해상도도 큰 값부터
	ResetCurPage();
	UpdateSortArrows();
	UpdateSortUI();
	ApplyFilter();
}

void CRuliManagerDlg::OnBnClickedSortDir()
{
	if (IsActorGridMode())
	{
		m_actorSortAsc = !m_actorSortAsc;
		m_pageActor = 0;
		UpdateSortUI();
		RebuildActorGrid();
		return;
	}
	if (IsCatCardMode())
	{
		CatSortAsc() = !CatSortAsc();
		m_pageCat = 0;
		UpdateSortUI();
		RebuildCategories();
		return;
	}
	ResetCurPage();
	m_sortAsc = !m_sortAsc;
	UpdateSortArrows();
	UpdateSortUI();
	ApplyFilter();
}

// ---------------------------------------------------------------------------
// 보기 전환 (영상 / 배우 / 스튜디오 / 태그)

std::vector<CString> CRuliManagerDlg::GetCategoryValues(const VideoItem& v, int mode)
{
	std::vector<CString> out;
	const CString* src = nullptr;
	switch (mode)
	{
	case MODE_ACTOR:  src = &v.actors; break;
	case MODE_STUDIO: src = &v.studio; break;
	case MODE_TAG:    src = &v.tags;   break;
	case MODE_COLLECTION: src = &v.collections; break;
	default:          return out;
	}

	if (mode == MODE_STUDIO)
	{
		CString t = *src;
		t.Trim();
		if (!t.IsEmpty())
			out.push_back(t);
		return out;
	}

	// 배우 / 태그: 쉼표로 구분
	int pos = 0;
	for (;;)
	{
		const int p = CVideoLibrary::FindListComma(*src, pos);
		CString t = (p < 0) ? src->Mid(pos) : src->Mid(pos, p - pos);
		t.Trim();
		if (!t.IsEmpty())
			out.push_back(t);
		if (p < 0) break;
		pos = p + 1;
	}
	return out;
}

void CRuliManagerDlg::UpdateCategoryHeader()
{
	static const wchar_t* const kTitles[] = { L"", L"배우", L"제작사", L"태그", L"컬렉션" };
	LVCOLUMN col = {};
	col.mask = LVCF_TEXT;
	col.pszText = const_cast<LPWSTR>(kTitles[m_mode]);
	m_listCat.SetColumn(0, &col);
}

void CRuliManagerDlg::MarkCategoriesDirty()
{
	// 목록 알림 처리 중에 분류 목록을 다시 만들지 않도록 메시지로 미룸
	if (m_mode == MODE_VIDEO || m_catsDirty)
		return;
	m_catsDirty = true;
	PostMessage(WM_APP_REBUILD_CATS);
}

const CMediaInfoLabel::Info* CRuliManagerDlg::CardMediaInfo(const CString& path)
{
	if (path.IsEmpty())
		return nullptr;
	CString key = path;
	key.MakeLower();
	bool online = true;
	if (m_lib.IsRemovableItem(path, &online) && !online)
		return nullptr;   // 연결 안 된 이동식 디스크: 카드에 해상도 · 재생 시간 표시 안 함, 읽지도 않음 (실패를 기억하지 않게 → 연결되면 그때 읽음)
	auto it = m_cardMedia.find(key);
	if (it != m_cardMedia.end())
		return &it->second;
	if (!m_cardMediaRequested.insert(key).second)
		return nullptr;   // 읽는 중
	if (!m_mediaQ)
	{
		// 처음 요청할 때 읽기 스레드 하나 시작 (대기열이 비면 기다림, 프로그램이 끝날 때까지)
		m_mediaQ = std::make_shared<MediaQueue>();
		m_mediaQ->hwnd = GetSafeHwnd();
		std::shared_ptr<MediaQueue> q = m_mediaQ;
		m_mediaThread = std::thread([q]()
		{
			const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			// 낮은 우선순위 (CPU · 디스크 I/O 모두) - 화면 그리기 · 조작을 방해하지 않게
			::SetThreadPriority(::GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
			for (;;)
			{
				CString p;
				{
					std::unique_lock<std::mutex> lock(q->mx);
					q->cv.wait(lock, [&q]() { return q->stop || !q->q.empty(); });
					if (q->stop)
						break;
					p = q->q.front();
					q->q.pop_front();
				}
				if (!::IsWindow(q->hwnd))
					break;
				CardMediaResult* r = new CardMediaResult;
				r->key = p;
				r->key.MakeLower();
				r->info = CMediaInfoLabel::ReadInfoCached(p);   // 디스크 캐시(mediainfo.vmdb) → 없거나 파일이 바뀌었으면 읽음
				if (!::PostMessageW(q->hwnd, WM_APP_CARD_MEDIA, 0, reinterpret_cast<LPARAM>(r)))
				{
					delete r;
					break;
				}
			}
			if (SUCCEEDED(hr))
				::CoUninitialize();
		});
	}
	{
		std::lock_guard<std::mutex> lock(m_mediaQ->mx);
		m_mediaQ->q.push_back(path);
	}
	m_mediaQ->cv.notify_one();
	return nullptr;
}

void CRuliManagerDlg::StopCardMediaThread()
{
	// 읽기 스레드 끝내기: 대기열을 비우고 멈춤 신호 → 지금 읽는 파일이 끝날 때까지 기다림
	if (m_mediaQ)
	{
		{
			std::lock_guard<std::mutex> lock(m_mediaQ->mx);
			m_mediaQ->stop = true;
			m_mediaQ->q.clear();
		}
		m_mediaQ->cv.notify_all();
	}
	if (m_mediaThread.joinable())
		m_mediaThread.join();
	m_mediaQ.reset();
	m_cardMediaRequested.clear();   // 읽지 못한 요청은 다시 요청할 수 있게 (이미 받은 정보는 m_cardMedia 에 남음)
	// 아직 처리하지 못한 결과 메시지의 메모리 정리
	MSG msg;
	while (::PeekMessageW(&msg, GetSafeHwnd(), WM_APP_CARD_MEDIA, WM_APP_CARD_MEDIA, PM_REMOVE))
		delete reinterpret_cast<CardMediaResult*>(msg.lParam);
}

void CRuliManagerDlg::ForgetFailedMediaInfo()
{
	// 읽지 못한(ok = false) 결과만 지움 → 다음에 그릴 때 다시 읽기 요청 (읽은 정보는 그대로)
	for (auto it = m_cardMedia.begin(); it != m_cardMedia.end();)
	{
		if (!it->second.ok)
		{
			m_cardMediaRequested.erase(it->first);
			it = m_cardMedia.erase(it);
		}
		else
			++it;
	}
	if (m_mediaInfo.GetSafeHwnd())
		m_mediaInfo.ForgetFailed();   // 상세의 해상도 · 재생 시간 칸도
}

void CRuliManagerDlg::OnDestroy()
{
	// 스크린샷 추출 중이면 대기열은 버리고 멈춤 신호 → 지금 장면이 끝날 때까지 기다림, 남은 결과 메시지 정리
	m_shotQueue.clear();
	m_shotCancel = true;
	if (m_shotThread.joinable())
		m_shotThread.join();
	// 미리보기 영상: 재생 멈춤, 만드는 중이면 ffmpeg 를 끝내고 기다림, 남은 결과 메시지 정리
	KillTimer(kPreviewHoverTimer);
	KillTimer(kGearSpinTimer);
	StopPreviewPlayer();
	StopWallPlayers();
	if (m_wallOverlay.GetSafeHwnd())
		m_wallOverlay.DestroyWindow();
	if (m_gridPartOverlay.GetSafeHwnd())
		m_gridPartOverlay.DestroyWindow();
	if (m_pkgPartOverlay.GetSafeHwnd())
		m_pkgPartOverlay.DestroyWindow();
	m_prevQueue.clear();
	m_prevCancel = true;
	if (m_prevThread.joinable())
		m_prevThread.join();
	{
		MSG msg;
		while (::PeekMessageW(&msg, GetSafeHwnd(), WM_APP_PREVIEW_DONE, WM_APP_PREVIEW_DONE, PM_REMOVE))
			delete reinterpret_cast<PreviewVideo::Result*>(msg.lParam);
	}
	{
		MSG msg;
		while (::PeekMessageW(&msg, GetSafeHwnd(), WM_APP_SHOT_DONE, WM_APP_SHOT_DONE, PM_REMOVE))
			delete reinterpret_cast<ShotResult*>(msg.lParam);
	}
	StopCardMediaThread();
	{
		// 해상도 · 재생 시간 디스크 캐시 저장 (DB 에 있는 영상만 남김)
		std::set<CString> keep;
		for (const VideoItem& v : m_lib.items)
		{
			CString k = v.path;
			k.MakeLower();
			keep.insert(k);
		}
		CMediaInfoLabel::SaveDiskCache(&keep);
	}
	StopCoverThreads();
	if (m_fadeTimer)
	{
		KillTimer(kOverlayFadeTimer);
		m_fadeTimer = 0;
	}
	CDialogEx::OnDestroy();
}

LRESULT CRuliManagerDlg::OnCardMediaReady(WPARAM, LPARAM lParam)
{
	std::unique_ptr<CardMediaResult> r(reinterpret_cast<CardMediaResult*>(lParam));
	if (!r)
		return 0;
	m_cardMedia[r->key] = r->info;
	if (r->info.ok && m_grid.GetSafeHwnd() && m_grid.IsWindowVisible())
		m_grid.Invalidate(FALSE);   // 받은 해상도 · 재생 시간으로 다시 그림
	if (r->info.ok && VideoListActive() && m_list.GetSafeHwnd() && !m_listMediaTimer)
	{
		// [리스트] 재생 시간 · 해상도 열: 결과가 연달아 오므로 0.15초 모아서 한 번만 다시 그림 (결과마다 표 전체를 다시 그리면 스크롤이 느려짐)
		m_listMediaTimer = SetTimer(kListMediaTimer, 150, nullptr) != 0;
	}
	return 0;
}

LRESULT CRuliManagerDlg::OnRebuildCategories(WPARAM, LPARAM)
{
	if (!m_catsDirty)
		return 0;

	RebuildCategories();
	return 0;
}

void CRuliManagerDlg::RebuildCategories()
{
	m_catsDirty = false;
	if (m_mode == MODE_VIDEO)
		return;
	if (m_mode == MODE_ACTOR)
	{
		RebuildActorGrid();   // 배우 보기는 분류 목록 대신 배우 격자
		return;
	}

	CString query;
	m_editSearch.GetWindowText(query);
	query.Trim();
	query.MakeLower();
	if (!IsCategoryListMode())
		query.Empty();   // 영상 보기 중에는 검색어가 영상용

	struct Entry { CString name; CString memo; int count = 0; bool fav = false; };
	std::map<CString, Entry> groups;   // 소문자 키 → 표시 이름/개수
	int none = 0;

	// 컬렉션: 목록의 컬렉션은 영상이 없어도 표시, 카드 이미지 = 처음 들어 있는 영상
	if (m_mode == MODE_COLLECTION)
	{
		for (const NamedInfo& n : m_lib.collectionInfos)
		{
			CString key = n.name;
			key.MakeLower();
			groups[key].name = n.name;
			groups[key].memo = n.memo;
			groups[key].fav = n.favorite;
		}
		m_collectionCover.clear();
		for (size_t i = 0; i < m_lib.items.size(); ++i)
			for (const CString& c : CVideoLibrary::SplitList(m_lib.items[i].collections))
			{
				CString key = c;
				key.MakeLower();
				if (m_collectionCover.find(key) == m_collectionCover.end())
					m_collectionCover[key] = static_cast<int>(i);
			}
	}
	// 스튜디오 / 태그 목록에 있으면 영상이 없어도 표시
	if (m_mode == MODE_STUDIO || m_mode == MODE_TAG)
	{
		for (const NamedInfo& n : m_lib.NamedList(m_mode == MODE_STUDIO ? LIST_STUDIO : LIST_TAG))
		{
			CString key = n.name;
			key.MakeLower();
			groups[key].name = n.name;
			if (m_mode == MODE_STUDIO)
				groups[key].memo = n.memo;   // 태그는 메모 없음
			groups[key].fav = (m_mode == MODE_TAG) && n.favorite;   // 태그 즐겨찾기
		}
	}

	// 제작사 탭 합계: 레이블 없는 영상은 지정한 제작사로, 레이블 영상은 그 레이블의 상위 제작사로 (하위 레이블 포함)
	//  → 영상 하나를 제작사 하나에만 셈 (등록되지 않았거나 상위가 없는 레이블이면 영상에 지정한 제작사로)
	auto studioKeyOf = [this](const VideoItem& v) -> CString
	{
		CString lb = v.label;
		lb.Trim();
		CString s;
		if (lb.IsEmpty())
			s = v.studio;
		else
		{
			const int li = m_lib.FindLabel(lb);
			if (li >= 0)
				s = m_lib.labelInfos[li].parent;
			s.Trim();
			if (s.IsEmpty())
				s = v.studio;
		}
		s.Trim();
		return s;
	};

	// 스튜디오 카드용: 스튜디오별 출연 배우 수 (하위 레이블 영상 포함 - 카드 합계와 같게)
	m_studioActorCounts.clear();
	if (m_mode == MODE_STUDIO)
		ComputeStudioLevels();   // 상위 · 하위 연결이 바뀌었을 수 있으니 목록을 만들 때마다 다시
	if (m_mode == MODE_STUDIO)
	{
		const std::map<CString, int> nameIndex = m_lib.ActorNameIndex();
		std::map<CString, std::set<CString>> actorsOf;
		for (const VideoItem& v : m_lib.items)
		{
			CString s = studioKeyOf(v);
			if (s.IsEmpty())
				continue;
			s.MakeLower();
			for (const CString& n : CVideoLibrary::SplitList(v.actors))
				actorsOf[s].insert(m_lib.ActorKeyOf(nameIndex, n));
		}
		for (const auto& kv : actorsOf)
			m_studioActorCounts[kv.first] = static_cast<int>(kv.second.size());
	}
	m_labelActorCounts.clear();
	std::map<CString, int> labelVideoCounts;   // 레이블(소문자) → 영상 수
	if (m_mode == MODE_STUDIO)
	{
		const std::map<CString, int> nameIndex = m_lib.ActorNameIndex();
		std::map<CString, std::set<CString>> actorsOf;
		for (const VideoItem& v : m_lib.items)
		{
			CString l = v.label;
			l.Trim();
			if (l.IsEmpty())
				continue;
			l.MakeLower();
			++labelVideoCounts[l];
			for (const CString& n : CVideoLibrary::SplitList(v.actors))
				actorsOf[l].insert(m_lib.ActorKeyOf(nameIndex, n));
		}
		for (const auto& kv : actorsOf)
			m_labelActorCounts[kv.first] = static_cast<int>(kv.second.size());
	}

	for (const VideoItem& v : m_lib.items)
	{
		// 제작사 탭: 하위 레이블 영상도 상위 제작사 합계에 넣음 (studioKeyOf)
		if (m_mode == MODE_STUDIO)
		{
			const CString s = studioKeyOf(v);
			if (s.IsEmpty())
			{
				CString lb = v.label;
				if (lb.Trim().IsEmpty())
					++none;
				continue;
			}
			CString key = s;
			key.MakeLower();
			Entry& e = groups[key];
			if (e.name.IsEmpty())
				e.name = s;
			++e.count;
			continue;
		}
		const std::vector<CString> vals = GetCategoryValues(v, m_mode);
		if (vals.empty())
		{
			++none;
			continue;
		}
		const bool countIt = true;
		std::set<CString> seen;
		for (const CString& n : vals)
		{
			CString key = n;
			key.MakeLower();
			if (!seen.insert(key).second)
				continue;
			Entry& e = groups[key];
			if (e.name.IsEmpty())
				e.name = n;
			if (countIt)
				++e.count;
		}
	}

	std::vector<Entry> sorted;
	sorted.reserve(groups.size());
	for (const auto& kv : groups)
	{
		if (!query.IsEmpty() && kv.first.Find(query) < 0)
			continue;
		sorted.push_back(kv.second);
	}

	// 제작사 · 태그 탭은 정렬 콤보(이름 / 영상수 + 오름·내림, 탭마다 따로)
	const bool byCount = (CatSortCol() == 1);
	const bool asc = CatSortAsc();
	std::sort(sorted.begin(), sorted.end(), [byCount, asc](const Entry& a, const Entry& b)
	{
		if (a.fav != b.fav)
			return a.fav;   // 즐겨찾기한 태그를 먼저
		if (byCount)
		{
			if (a.count != b.count)
				return asc ? (a.count < b.count) : (a.count > b.count);
			return ::StrCmpLogicalW(a.name, b.name) < 0;   // 같은 수면 이름 순
		}
		const int c = ::StrCmpLogicalW(a.name, b.name);
		return asc ? (c < 0) : (c > 0);
	});

	m_loadingCats = true;
	m_listCat.SetRedraw(FALSE);
	m_listCat.DeleteAllItems();
	m_catRows.clear();

	// 행을 모은 뒤 지금 페이지만 넣음 (페이지 나누기)
	struct PendingRow { CString label; int count; int kind; CString value; CString memo; };
	std::vector<PendingRow> allRows;
	auto addRow = [&allRows](const CString& label, int count, int kind, const CString& value, const CString& memo)
	{
		allRows.push_back({ label, count, kind, value, memo });
	};
	auto insertRow = [this](const CString& label, int count, int kind, const CString& value, const CString& memo)
	{
		const int row = m_listCat.InsertItem(m_listCat.GetItemCount(), label);
		CString c;
		c.Format(L"%d", count);
		m_listCat.SetItemText(row, 1, c);
		CString oneLine = memo;
		oneLine.Replace(L"\r\n", L" ");
		oneLine.Replace(L'\n', L' ');
		m_listCat.SetItemText(row, 2, oneLine);
		m_catRows.push_back({ kind, value });
	};

	// (전체)/(미지정) 항목은 표시하지 않음 — 실제 스튜디오/태그만
	(void)none;
	int restStart = INT_MAX;   // 제작사 탭: 상위 없는 레이블이 시작하는 행
	if (m_mode == MODE_STUDIO)
	{
		// 제작사 탭: 격자 = 제작사만 (상위 → 하위 순), 리스트 = 제작사 바로 뒤에 그 레이블, 상위가 없거나 제작사가 검색에서 빠진 레이블은 맨 뒤
		std::map<CString, std::vector<Entry>> labelsOf;   // 상위 제작사(소문자) → 레이블
		std::vector<Entry> restLabels;
		std::set<CString> shownStudios;
		for (const Entry& e : sorted)
		{
			CString k = e.name;
			k.MakeLower();
			shownStudios.insert(k);
		}
		for (const NamedInfo& lb : m_lib.labelInfos)
		{
			CString lk = lb.name, pk = lb.parent;
			lk.MakeLower();
			pk.MakeLower();
			const bool parentExists = m_lib.FindNamed(LIST_STUDIO, lb.parent) >= 0;
			if (!query.IsEmpty() && lk.Find(query) < 0 && !(parentExists && shownStudios.count(pk)))
				continue;
			Entry e;
			e.name = lb.name;
			e.memo = lb.memo;
			auto it = labelVideoCounts.find(lk);
			e.count = (it != labelVideoCounts.end()) ? it->second : 0;
			if (parentExists && shownStudios.count(pk))
				labelsOf[pk].push_back(e);
			else
				restLabels.push_back(e);
		}
		auto cmp = [byCount, asc](const Entry& a, const Entry& b)   // 레이블도 제작사와 같은 기준 · 방향
		{
			if (byCount)
			{
				if (a.count != b.count)
					return asc ? (a.count < b.count) : (a.count > b.count);
				return ::StrCmpLogicalW(a.name, b.name) < 0;
			}
			const int c = ::StrCmpLogicalW(a.name, b.name);
			return asc ? (c < 0) : (c > 0);
		};
		// 제작사 순서: 상위 → 중위 → 하위 (상위 제작사 바로 뒤에 그 하위 제작사들, 여러 단계) - 형제끼리는 정렬 콤보 순서
		//  상위 제작사가 검색 등으로 목록에 없으면 그 제작사가 맨 위 단계
		std::map<CString, size_t> indexOf;   // 제작사(소문자) → sorted 번호
		for (size_t i = 0; i < sorted.size(); ++i)
		{
			CString k = sorted[i].name;
			k.MakeLower();
			indexOf[k] = i;
		}
		std::map<size_t, std::vector<size_t>> kidsOf;   // sorted 번호 → 하위 제작사 sorted 번호 (sorted 순서)
		std::vector<size_t> roots;
		for (size_t i = 0; i < sorted.size(); ++i)
		{
			const int si = m_lib.FindNamed(LIST_STUDIO, sorted[i].name);
			CString pk = si >= 0 ? m_lib.studios[si].parent : CString();
			pk.Trim();
			pk.MakeLower();
			auto pit = pk.IsEmpty() ? indexOf.end() : indexOf.find(pk);
			if (pit != indexOf.end() && pit->second != i)
				kidsOf[pit->second].push_back(i);
			else
				roots.push_back(i);
		}
		std::vector<size_t> order;
		std::vector<bool> visited(sorted.size(), false);
		std::function<void(size_t)> visit = [&](size_t i)
		{
			if (visited[i])
				return;   // 순환 연결 방지
			visited[i] = true;
			order.push_back(i);
			auto kit = kidsOf.find(i);
			if (kit != kidsOf.end())
				for (size_t c : kit->second)
					visit(c);
		};
		for (size_t r : roots)
			visit(r);
		for (size_t i = 0; i < sorted.size(); ++i)   // 순환으로 닿지 않은 제작사
			visit(i);

		// 격자 보기: 제작사만 / 리스트 보기: 줄마다 제작사 + 그 레이블
		const bool withLabels = StudioListView();
		for (size_t oi : order)
		{
			const Entry& e = sorted[oi];
			addRow(e.name, e.count, CAT_VALUE, e.name, e.memo);
			if (!withLabels)
				continue;
			CString k = e.name;
			k.MakeLower();
			auto it = labelsOf.find(k);
			if (it == labelsOf.end())
				continue;
			std::sort(it->second.begin(), it->second.end(), cmp);
			for (const Entry& l : it->second)
				addRow(l.name, l.count, CAT_LABEL, l.name, l.memo);
		}
		restStart = static_cast<int>(allRows.size());   // 리스트 보기: 상위 없는 레이블은 한 묶음으로 이어서
		if (withLabels)
		{
			std::sort(restLabels.begin(), restLabels.end(), cmp);
			for (const Entry& l : restLabels)
				addRow(l.name, l.count, CAT_LABEL, l.name, l.memo);
		}
	}
	else
	{
		for (const Entry& e : sorted)
			addRow(e.name, e.count, CAT_VALUE, e.name, e.memo);
	}
	m_totalCat = static_cast<int>(allRows.size());
	{
		int first = 0, last = 0;
		SlicePage(m_pageCat, m_totalCat, first, last);
		for (int i = first; i < last; ++i)
			insertRow(allRows[i].label, allRows[i].count, allRows[i].kind, allRows[i].value, allRows[i].memo);

		// 제작사 탭 리스트 보기: 줄마다 [제작사] [레이블] [레이블] ... (한 줄로만 - 넘치면 좌우 스크롤)
		std::vector<GridLine> lines;
		if (StudioListView())
		{
			for (int i = first; i < last; ++i)
			{
				const int j = i - first;
				if (allRows[i].kind == CAT_VALUE)
					lines.push_back({ j, 0, 1 });
				else if (i >= restStart)
				{
					if (i == restStart || j == 0)
						lines.push_back({ j, 0, 0 });   // 상위 없는 레이블: 줄 맨 앞부터 이어서
				}
				else if (j == 0)
					lines.push_back({ 0, 1, 1 });       // 앞 페이지 제작사의 레이블이 이어짐 → 제작사 칸 비움
			}
		}
		m_catGrid.SetLines(lines);
	}

	// 이전 선택 복원 (선택은 오른쪽 정보 표시용이며 영상 필터와는 별개)
	int sel = -1;
	for (size_t i = 0; i < m_catRows.size(); ++i)
	{
		const CatRow& r = m_catRows[i];
		if (r.kind == m_catSelKind &&
			((m_catSelKind != CAT_VALUE && m_catSelKind != CAT_LABEL) || r.value.CompareNoCase(m_catSelValue) == 0))
		{
			sel = static_cast<int>(i);
			break;
		}
	}
	if (sel >= 0)
	{
		m_listCat.SetItemState(sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_listCat.EnsureVisible(sel, FALSE);
	}

	m_listCat.SetRedraw(TRUE);
	m_listCat.Invalidate();
	m_loadingCats = false;

	m_catGrid.Refresh();
	if (sel >= 0)
		m_catGrid.OnSelectionChanged();

	if (IsCategoryListMode())
	{
		UpdateStatus();
		UpdatePager();
	}
}

void CRuliManagerDlg::OnModeChanged(UINT nID)
{
	const int mode = static_cast<int>(nID) - IDC_RADIO_VIDEO;
	if (mode == m_mode || mode < MODE_VIDEO || mode >= MODE_COUNT)
		return;

	CommitDetails();
	LeaveActorDrillFilters();   // 배우 출연작 화면에서 탭을 바꾸면 영상 탭 값으로 되돌림
	m_mode = mode;
	m_catKind = CAT_ALL;
	m_catValue.Empty();
	m_drill.Empty();
	m_drillActorIdx = -1;
	m_pageActor = 0;   // 탭을 바꾸면 배우 · 제작사/태그 목록은 첫 페이지부터 (영상 탭은 보던 페이지 유지)
	m_pageCat = 0;
	m_pageDrill = 0;
	// 탭을 바꾸면 모든 화면의 고른 장르 칩을 풂 ([장르숨김|전체] 선택은 그대로)
	if (!m_genreSel[0].IsEmpty())
		m_pageVideoMain = 0;   // 영상 탭은 거르던 목록의 페이지였으므로 첫 페이지부터
	for (CString& s : m_genreSel)
		s.Empty();
	// 탭을 바꾸면 카드 체크도 모두 풂 (배우 · 제작사 · 태그 · 컬렉션 · 영상)
	m_actorChecked.clear();
	for (std::set<CString>& s : m_catChecked)
		s.clear();
	m_videoChecked.clear();
	// 탭을 바꾸면 배우 선택도 풂 (다른 화면에서 배우를 열어 배우 탭으로 올 때만 그 배우를 고름 - GoToActor)
	m_actorSel = -1;
	m_actorSelName = m_gotoActorName;
	m_gotoActorName.Empty();
	ApplyTabPageSize();   // 탭마다 따로 저장한 페이지당 개수
	m_catSelKind = -1;
	m_catSelValue.Empty();
	AfxGetApp()->WriteProfileInt(L"Settings", L"ViewMode", m_mode);

	// 이전 탭의 상세 상태를 모두 비움 (제작사 메모 · 링크 · 레이블/제작사 카드 · 출연 배우 카드 · 이름 줄 · 미리보기)
	SetNamedMemoTarget(0, CString());
	if (m_namedLinks.GetSafeHwnd())
		m_namedLinks.SetUrls({});
	UpdateLabelStrip(CString(), CString());   // m_stripForActor 도 해제
	m_namedActors = false;
	m_stripActors.clear();
	if (m_actorStrip.GetSafeHwnd())
		m_actorStrip.SetCount(0);
	ShowDetails(-1);   // 영상 상세 칸 · 이름 줄(직접 그리기 해제) · 미리보기 · 태그 아래 배우 카드
	UpdateModeButtons();
	m_zoomSlider.SetPos(m_zoom[ZoomSlot()]);
	SetupActorCards();   // 탭마다 확대 단계가 다름

	UpdateCategoryHeader();
	UpdateLeftPane();
	RebuildCategories();
	if (IsActorGridMode())
	{
		ShowDetails(-1);
		ShowActorInfo(SelectedActorIndex());
	}
	else if (IsCategoryListMode())
	{
		ShowDetails(-1);
		ShowNamedInfo(-1);
	}

	CRect client;
	GetClientRect(&client);
	LayoutControls(client.Width(), client.Height());
	ApplyFilter();
	// 탭을 바꾸면 목록 스크롤은 맨 위부터
	m_grid.ScrollToTop();
	m_actorGrid.ScrollToTop();
	m_catGrid.ScrollToTop();
	if (m_listCat.GetSafeHwnd() && m_listCat.GetItemCount() > 0)
		m_listCat.EnsureVisible(0, FALSE);
}

void CRuliManagerDlg::OnLvnCatItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	*pResult = 0;
	if (m_loadingCats)
		return;

	if ((p->uChanged & LVIF_STATE) &&
		(p->uNewState & LVIS_SELECTED) && !(p->uOldState & LVIS_SELECTED) &&
		p->iItem >= 0 && p->iItem < static_cast<int>(m_catRows.size()))
	{
		const CatRow& r = m_catRows[p->iItem];
		m_catSelKind = r.kind;
		m_catSelValue = r.value;
		ShowNamedInfo(p->iItem);   // 오른쪽에 이미지/정보 (영상은 더블클릭하면 표시)
	}
	if (p->uChanged & LVIF_STATE)
		m_catGrid.Invalidate(FALSE);
}

void CRuliManagerDlg::OnLvnCatKeyDown(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLVKEYDOWN* k = reinterpret_cast<NMLVKEYDOWN*>(pNMHDR);
	HandleCatKey(k->wVKey);
	*pResult = 0;
}

void CRuliManagerDlg::HandleCatKey(UINT vk)
{
	switch (vk)
	{
	case VK_SPACE: OnActorShowVideos(); break;
	case VK_F2:    OnActorEdit(); break;
	case VK_F5:    OnBnClickedRefresh(); break;
	case VK_DELETE:
		if (m_mode == MODE_COLLECTION || m_mode == MODE_TAG || m_mode == MODE_STUDIO)
			OnCatDelete();   // 컬렉션 · 태그 삭제 (선택한 카드가 체크되어 있으면 체크한 항목 모두 - 제작사는 체크했을 때만)
		break;
	case 'F':
		if (::GetKeyState(VK_CONTROL) & 0x8000)
		{
			m_editSearch.SetFocus();
			m_editSearch.SetSel(0, -1);
		}
		break;
	}
}

int CRuliManagerDlg::GetNamedThumbIndex(int kind, const CString& name)
{
	const int idx = m_lib.FindNamed(kind, name);
	if (idx < 0)
		return 0;
	const NamedInfo& n = m_lib.NamedList(kind)[idx];
	if (n.image.IsEmpty())
		return 0;

	CString key;
	key.Format(L"named%d:%s|%s", kind, static_cast<LPCWSTR>(n.name), static_cast<LPCWSTR>(n.image));
	key.MakeLower();
	auto it = m_thumbIndex.find(key);
	if (it != m_thumbIndex.end())
		return it->second;

	int image = 0;
	CImage src;
	if (::PathFileExistsW(n.image) && LoadImageFile(src, n.image))
		image = AddThumbnail(&src, nullptr);
	m_thumbIndex[key] = image;
	return image;
}

// --- 스튜디오/태그 격자 데이터 제공 (행 = m_catRows) ---

int CRuliManagerDlg::CatGridOwner::GridGetCount()
{
	return static_cast<int>(dlg->m_catRows.size());
}

int CRuliManagerDlg::CatGridOwner::GridGetSel()
{
	return dlg->m_listCat.GetNextItem(-1, LVNI_SELECTED);
}

void CRuliManagerDlg::CatGridOwner::GridSetSel(int row)
{
	if (row < 0 || row >= static_cast<int>(dlg->m_catRows.size()))
		return;
	// 선택은 (숨겨진) 분류 목록이 관리 → 기존 선택 처리(오른쪽 정보 표시) 그대로 사용
	dlg->m_listCat.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	dlg->m_listCat.EnsureVisible(row, FALSE);
	dlg->m_catGrid.OnSelectionChanged();
}

void CRuliManagerDlg::CatGridOwner::GridGetItem(int row, int& image, CString& name, CString& line2, CString& line3)
{
	image = 0;
	if (row < 0 || row >= static_cast<int>(dlg->m_catRows.size()))
		return;
	const CatRow& r = dlg->m_catRows[row];
	name = dlg->m_listCat.GetItemText(row, 0);
	line2 = L"영상 " + dlg->m_listCat.GetItemText(row, 1) + L"편";
	line3 = dlg->m_listCat.GetItemText(row, 2);   // 메모
	if (r.kind == CAT_VALUE && dlg->m_mode == MODE_STUDIO)
		image = dlg->GetNamedThumbIndex(LIST_STUDIO, r.value);   // 태그는 이미지 없음
}

void CRuliManagerDlg::CatGridOwner::GridActivate(int row)
{
	dlg->DrillIntoCategory(row);
}

void CRuliManagerDlg::CatGridOwner::GridKey(UINT vk)
{
	dlg->HandleCatKey(vk);
}

void CRuliManagerDlg::OnLvnCatColumnClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	m_catSortByCount = (p->iSubItem == 1);   // 영상 수 열: 많은 순, 그 외: 가나다순
	CatSortCol() = m_catSortByCount ? 1 : 0;   // 정렬 콤보와 같은 값으로
	CatSortAsc() = !m_catSortByCount;
	UpdateSortUI();
	RebuildCategories();
	*pResult = 0;
}

// ---------------------------------------------------------------------------
// 목록 / 격자(썸네일) 표시

void CRuliManagerDlg::OnViewStyleChanged(UINT nID)
{
	const bool grid = (nID == IDC_RADIO_GRIDVIEW);
	if (grid == m_gridView)
		return;
	SetGridView(grid);
	AfxGetApp()->WriteProfileInt(L"Settings", L"GridView", grid ? 1 : 0);
}

void CRuliManagerDlg::SetGridView(bool grid)
{
	m_gridView = grid;
	const bool listHadFocus = (GetFocus() == &m_list) || (GetFocus() == &m_grid);
	const bool catHadFocus = (GetFocus() == &m_listCat) || (GetFocus() == &m_catGrid);

	UpdateLeftPane();
	if (IsActorGridMode())
		return;
	if (IsCategoryListMode())
	{
		const int sel = m_listCat.GetNextItem(-1, LVNI_SELECTED);
		if (CatGridActive())
		{
			m_catGrid.Refresh();
			m_catGrid.OnSelectionChanged();
			if (catHadFocus) m_catGrid.SetFocus();
		}
		else
		{
			if (sel >= 0) m_listCat.EnsureVisible(sel, FALSE);
			if (catHadFocus) m_listCat.SetFocus();
		}
		return;
	}

	// 영상은 항상 격자 표시
	m_grid.Refresh();
	m_grid.OnSelectionChanged();
	if (listHadFocus) m_grid.SetFocus();
}

// ---------------------------------------------------------------------------
// 격자 보기 데이터 제공 (IVideoGridOwner)

void CRuliManagerDlg::BuildMergedView(bool apply)
{
	m_gridRows.clear();
	m_rowGrid.clear();
	m_gridParts.clear();
	if (!apply)
		return;
	// 1) 묶음(2개 이상) → 행들, 파트 번호 순
	std::map<CString, std::vector<std::pair<int, int>>> groups;   // 묶음 키 → (번호, 항목)
	std::vector<CString> keyOf(m_view.size());
	for (size_t r = 0; r < m_view.size(); ++r)
	{
		CString key;
		int num = 0;
		if (SplitPartName(m_lib.items[m_view[r]].path, key, num))
		{
			key.MakeLower();
			keyOf[r] = key;
			groups[key].push_back({ num, m_view[r] });
		}
	}
	// 2) 정렬된 순서대로, 묶음은 처음 나온 자리에 전부(번호 순) 붙임
	std::vector<int> order;
	order.reserve(m_view.size());
	std::set<CString> done;
	std::vector<std::pair<size_t, size_t>> cards;   // (시작 행, 행 수)
	for (size_t r = 0; r < m_view.size(); ++r)
	{
		const CString& key = keyOf[r];
		auto git = key.IsEmpty() ? groups.end() : groups.find(key);
		if (git == groups.end() || git->second.size() < 2)
		{
			cards.push_back({ order.size(), 1 });
			order.push_back(m_view[r]);
			continue;
		}
		if (!done.insert(key).second)
			continue;   // 이미 첫 파트 자리에 붙임
		auto list = git->second;
		std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		cards.push_back({ order.size(), list.size() });
		for (const auto& pr : list)
			order.push_back(pr.second);
	}
	m_view.swap(order);
	// 3) 칸 표
	m_rowGrid.assign(m_view.size(), -1);
	for (size_t g = 0; g < cards.size(); ++g)
	{
		const int start = static_cast<int>(cards[g].first), n = static_cast<int>(cards[g].second);
		m_gridRows.push_back(start);
		std::vector<int> parts;
		for (int k = 0; k < n; ++k)
		{
			m_rowGrid[start + k] = static_cast<int>(g);
			parts.push_back(start + k);
		}
		if (n >= 2)
			m_gridParts[static_cast<int>(g)] = std::move(parts);
	}
}

int CRuliManagerDlg::GridRowOf(int g) const
{
	if (m_gridRows.empty())
		return g;
	return (g >= 0 && g < static_cast<int>(m_gridRows.size())) ? m_gridRows[g] : -1;
}

int CRuliManagerDlg::GridIdxOf(int row) const
{
	if (m_gridRows.empty())
		return row;
	return (row >= 0 && row < static_cast<int>(m_rowGrid.size())) ? m_rowGrid[row] : -1;
}

int CRuliManagerDlg::PartZoneAt(int g, const CRect& card, CPoint pt) const
{
	auto it = m_gridParts.find(g);
	if (it == m_gridParts.end() || card.Width() <= 0)
		return -1;
	if (VideoPortraitActive())
		return -1;   // [패키지]: 파트 고르기는 팝업된 [격자] 카드에서 (패키지 카드에는 파트 칸 없음)
	const CRect img(card.left, card.top, card.right, VideoImageCardActive() ? card.bottom : card.top + m_vcardImgH);   // DrawVideoCard 의 이미지 영역 ([세로] 격자 · [월 모드] = 카드 전체)
	if (!img.PtInRect(pt))
		return -1;
	const int n = static_cast<int>(it->second.size());
	return (std::min)(n - 1, (std::max)(0, static_cast<int>((pt.x - img.left) * n / img.Width())));
}

int CRuliManagerDlg::GridHitPart(int g, const CRect& card, CPoint pt)
{
	if (VideoLabelActive())
	{
		CRect check = LabelColumns(card).check;   // [레이블]: 줄 왼쪽 체크 칸
		check.InflateRect(DX(4), DX(4));
		return check.PtInRect(pt) ? kVideoCheckPart : 0;
	}
	// 왼쪽 위 체크 칸이 먼저 (분할 파트 칸 위에 겹쳐도)
	CRect check = ActorCheckRect(card);
	check.InflateRect(3, 3);
	if (check.PtInRect(pt))
		return kVideoCheckPart;
	const int z = PartZoneAt(g, card, pt);
	return z >= 0 ? z + 1 : 0;
}

bool CRuliManagerDlg::GridClick(int g, const CRect& card, CPoint pt)
{
	if (GridHitPart(g, card, pt) == kVideoCheckPart)
	{
		ToggleVideoCheck(g);   // 영상 카드 왼쪽 위 체크 칸
		return true;
	}
	const int z = PartZoneAt(g, card, pt);
	if (z < 0)
		return false;
	SelectViewRow(m_gridParts[g][z]);   // 누른 칸의 파트
	return true;
}

int CRuliManagerDlg::GridGetCount()
{
	if (YearViewActive() || VideoListActive())
		return 0;   // 연도별 보기 · 영상 탭 [리스트] 보기: 격자는 도구줄(머리 영역)만
	return m_gridRows.empty() ? static_cast<int>(m_view.size()) : static_cast<int>(m_gridRows.size());
}

int CRuliManagerDlg::GridGetSel()
{
	return GridIdxOf(GetSelectedRow());   // 합쳐 보기: 파트를 골라도 그 카드
}

void CRuliManagerDlg::GridSetSel(int g)
{
	// 격자 칸 → 행 (합쳐 보기에서 같은 카드의 다른 파트를 이미 골랐으면 그대로)
	if (!m_gridRows.empty() && g >= 0 && GridIdxOf(GetSelectedRow()) == g)
	{
		m_grid.OnSelectionChanged();
		return;
	}
	SelectViewRow(GridRowOf(g));
}

void CRuliManagerDlg::SelectViewRow(int row)
{
	if (row < 0 || row >= static_cast<int>(m_view.size()))
		return;
	// 선택 상태는 (숨겨진) 목록 컨트롤이 관리 → 기존 선택 처리 그대로 사용
	m_list.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	m_list.EnsureVisible(row, FALSE);
	m_grid.OnSelectionChanged();
}

void CRuliManagerDlg::GridGetItem(int g, int& image, CString& name, CString& release, CString& memo)
{
	const int row = GridRowOf(g);
	if (row < 0 || row >= static_cast<int>(m_view.size()))
	{
		image = 0;
		return;
	}
	const int idx = m_view[row];
	const VideoItem& v = m_lib.items[idx];
	image = GetThumbIndex(idx);
	name = v.title.IsEmpty() ? v.FileName() : v.title;   // 제목이 있으면 제목
	release = v.pending ? CString(L"● 임시 (정보 저장 전)") : v.release;   // 발매일 (날짜만, 없으면 빈 줄)
	memo.Empty();   // 영상 메모 기능 삭제
}

void CRuliManagerDlg::GridActivate(int /*row*/)
{
	OnBnClickedOpenDefault();
}

void CRuliManagerDlg::GridKey(UINT vk)
{
	HandleListKey(vk);
}

int CRuliManagerDlg::AddThumbnail(CImage* src, LPCWSTR text)
{
	CClientDC screen(this);
	CDC mem;
	mem.CreateCompatibleDC(&screen);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&screen, m_thumbW, m_thumbH);
	CBitmap* oldBmp = mem.SelectObject(&bmp);

	mem.FillSolidRect(0, 0, m_thumbW, m_thumbH, kThumbBackColor);   // 이미지 여백 / "이미지 없음" 배경

	if (src && !src->IsNull() && src->GetWidth() > 0 && src->GetHeight() > 0)
	{
		// 비율 유지하여 칸에 맞춤
		const double scale = (std::min)(static_cast<double>(m_thumbW) / src->GetWidth(),
		                                static_cast<double>(m_thumbH) / src->GetHeight());
		const int w = (std::max)(1, static_cast<int>(src->GetWidth() * scale + 0.5));
		const int h = (std::max)(1, static_cast<int>(src->GetHeight() * scale + 0.5));
		const int x = (m_thumbW - w) / 2;
		const int y = (m_thumbH - h) / 2;
		mem.SetStretchBltMode(HALFTONE);
		::SetBrushOrgEx(mem.GetSafeHdc(), 0, 0, nullptr);
		src->Draw(mem.GetSafeHdc(), CRect(x, y, x + w, y + h));
	}
	else if (text)
	{
		CFont* oldFont = mem.SelectObject(GetFont());
		mem.SetBkMode(TRANSPARENT);
		mem.SetTextColor(RGB(150, 150, 150));
		CRect rc(0, 0, m_thumbW, m_thumbH);
		mem.DrawText(text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		mem.SelectObject(oldFont);
	}

	mem.SelectObject(oldBmp);
	return m_thumbs.Add(&bmp, static_cast<CBitmap*>(nullptr));
}

int CRuliManagerDlg::GetThumbIndex(int idx)
{
	if (idx < 0 || idx >= static_cast<int>(m_lib.items.size()))
		return 0;

	const CString key = CVideoLibrary::MakeKey(m_lib.items[idx].path);
	auto it = m_thumbIndex.find(key);
	if (it != m_thumbIndex.end())
		return it->second;

	// 메모리 사용을 제한하기 위해 캐시가 너무 커지면 비움
	if (m_thumbIndex.size() > 1500)
	{
		m_thumbs.Remove(-1);
		m_thumbIndex.clear();
		AddThumbnail(nullptr, L"이미지 없음");
		m_list.Invalidate(FALSE);   // 이미 받아간 인덱스가 무효가 되므로 다시 그림
		m_grid.Invalidate(FALSE);
		m_actorGrid.Invalidate(FALSE);
		m_catGrid.Invalidate(FALSE);
	}

	int image = 0;
	const CString file = FindImageFor(m_lib.items[idx].path);
	if (!file.IsEmpty())
	{
		CImage src;
		if (LoadImageFileSfw(src, file))   // SFW: 모자이크
			image = AddThumbnail(&src, nullptr);
	}
	m_thumbIndex[key] = image;
	return image;
}

void CRuliManagerDlg::ResetThumbnails()
{
	m_thumbs.Remove(-1);
	m_thumbIndex.clear();
	m_actorPortraits.clear();   // 배우 사진이 바뀌었을 수 있음
	m_shotThumbs.clear();       // 영상 스크린샷 (SFW 전환 등)
	ClearVideoCovers();         // 영상 이미지가 추가/변경됐을 수 있음
	m_studioLogos.clear();      // 스튜디오 이미지가 바뀌었을 수 있음
	m_logoMarks.clear();
	AddThumbnail(nullptr, L"이미지 없음");
	m_grid.Invalidate(FALSE);
	m_actorGrid.Invalidate(FALSE);
	m_catGrid.Invalidate(FALSE);
}

void CRuliManagerDlg::OnLvnGetDispInfo(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLVDISPINFO* p = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);
	LVITEM& it = p->item;
	*pResult = 0;

	const bool valid = (it.iItem >= 0 && it.iItem < static_cast<int>(m_view.size()));

	// 이미지 없음 ([리스트]는 글자만) - 예전에는 여기서 썸네일 파일을 바로 읽어서(줄마다 디스크 · 이미지 해독) 스크롤이 매우 느렸음
	if (it.mask & LVIF_IMAGE)
		it.iImage = I_IMAGENONE;
	if (it.mask & LVIF_STATE)
	{
		// [리스트] 첫 열 체크 칸 = 영상 카드 체크 (m_videoChecked)
		it.state = INDEXTOSTATEIMAGEMASK((valid && IsItemChecked(m_view[it.iItem])) ? 2 : 1);
		it.stateMask = LVIS_STATEIMAGEMASK;
	}

	if (!(it.mask & LVIF_TEXT) || it.cchTextMax <= 0)
		return;
	if (!valid)
	{
		it.pszText[0] = 0;
		return;
	}

	const VideoItem& v = m_lib.items[m_view[it.iItem]];
	CString s;
	switch (it.iSubItem)
	{
	case LC_COVER:   break;   // 커버는 직접 그림 (OnNmCustomDrawList)
	case LC_CODE:
		// [리스트]에서는 직접 그림 (굵은 큰 글꼴 + 회색 순번 - OnNmCustomDrawList), 숨긴 목록(격자 보기)일 때만 글자
		if (!VideoListActive())
		{
			s = v.code;
			const CString part = ListPartLabel(m_view[it.iItem]);
			if (!part.IsEmpty())
				s = s.IsEmpty() ? part : (s + L" " + part);
		}
		break;
	case LC_RELEASE: s = v.pending ? CString(L"● 임시") : v.release; break;
	case LC_RATING:  break;   // 별은 직접 그림 (OnNmCustomDrawList)
	case LC_DURATION:
		if (VideoListActive())   // 보이는 줄만 요청됨 → 읽어 오면 다시 그림 (OnCardMediaReady)
			if (const CMediaInfoLabel::Info* mi = CardMediaInfo(v.path))
				if (mi->ok && mi->duration100ns)
				{
					// 시:분:초 (예: 2:03:45, 1시간 미만도 0:45:12)
					const ULONGLONG sec = mi->duration100ns / 10000000ULL;
					s.Format(L"%llu:%02llu:%02llu", sec / 3600, (sec / 60) % 60, sec % 60);
				}
		break;
	case LC_STUDIO:
	{
		// 제작사 / 레이블 (있는 것만)
		CString st = v.studio, lb = v.label;
		st.Trim();
		lb.Trim();
		s = (!st.IsEmpty() && !lb.IsEmpty()) ? (st + L" / " + lb) : (st.IsEmpty() ? lb : st);
		break;
	}
	case LC_ACTORS:  s = v.actors; break;
	case LC_TAGS:    break;   // 칩으로 직접 그림 (OnNmCustomDrawList)
	}
	_tcsncpy_s(it.pszText, it.cchTextMax, s, _TRUNCATE);
}

void CRuliManagerDlg::OnLvnItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	*pResult = 0;

	if ((p->uChanged & LVIF_STATE) &&
		(p->uNewState & LVIS_SELECTED) && !(p->uOldState & LVIS_SELECTED) &&
		p->iItem >= 0 && p->iItem < static_cast<int>(m_view.size()))
	{
		const int idx = m_view[p->iItem];
		if (idx != m_curItem)
		{
			CommitDetails();
			ShowDetails(idx);
		}
		if (m_yearView.GetSafeHwnd() && m_yearView.IsWindowVisible())
			m_yearView.SetSelected(p->iItem);   // 연도별 보기의 선택 표시도
	}

	if (p->uChanged & LVIF_STATE)
	{
		if ((p->uNewState & LVIS_SELECTED) && !(p->uOldState & LVIS_SELECTED))
			m_grid.OnSelectionChanged();   // 선택 항목이 보이도록 스크롤
		else
			m_grid.Invalidate(FALSE);
	}
}

void CRuliManagerDlg::OnLvnColumnClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	// [리스트] 열 제목 클릭: 그 열 기준 정렬 (같은 열이면 방향만 바꿈) - 정렬 콤보 · 방향 버튼도 같이
	NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	const int col = (p->iSubItem >= 0 && p->iSubItem < LC_COUNT) ? kListSort[p->iSubItem] : m_sortColumn;
	if (col < 0)
	{
		*pResult = 0;
		return;   // 커버 열: 정렬 없음
	}
	if (col == m_sortColumn)
		m_sortAsc = !m_sortAsc;
	else
	{
		m_sortColumn = col;
		m_sortAsc = !SortDescFirst(col);
	}
	ResetCurPage();
	UpdateSortArrows();
	UpdateSortUI();
	ApplyFilter();
	*pResult = 0;
}

void CRuliManagerDlg::OnLvnKeyDown(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLVKEYDOWN* k = reinterpret_cast<NMLVKEYDOWN*>(pNMHDR);
	HandleListKey(k->wVKey);
	*pResult = 0;
}

void CRuliManagerDlg::HandleListKey(UINT vk)
{
	const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
	switch (vk)
	{
	case VK_DELETE: OnBnClickedDelete(); break;
	case VK_F2:     OnBnClickedRename(); break;
	case VK_F5:     OnBnClickedRefresh(); break;
	case VK_SPACE:  OnBnClickedOpenDefault(); break;
	case 'F':       if (ctrl) { m_editSearch.SetFocus(); m_editSearch.SetSel(0, -1); } break;
	case VK_BACK:   if (!m_drill.IsEmpty()) BackToList(); break;
	}
}

void CRuliManagerDlg::OnNmDblclkList(NMHDR* pNMHDR, LRESULT* pResult)
{
	// 더블클릭: 기본 플레이어(파일 연결 프로그램)로 열기
	NMITEMACTIVATE* p = reinterpret_cast<NMITEMACTIVATE*>(pNMHDR);
	if (p->iItem >= 0 && p->iItem < static_cast<int>(m_view.size()))
		OnBnClickedOpenDefault();
	*pResult = 0;
}

// ---------------------------------------------------------------------------
// 상세 정보 (별점 / 태그 / 메모)

void CRuliManagerDlg::ShowDetails(int idx)
{
	SetNameRich(false);   // 스튜디오 상세의 직접 그리기 해제 (일반 글자로)
	m_pendingImage.Empty();   // 다른 영상으로 바뀌면 저장 안 한 새 이미지는 버림 (CommitDetails 가 먼저 저장함)
	m_loadingDetails = true;
	m_curItem = (idx >= 0 && idx < static_cast<int>(m_lib.items.size())) ? idx : -1;

	const bool offline = IsItemOffline(m_curItem);   // 연결 안 된 이동식 디스크 영상 → 보기만
	const BOOL enable = (m_curItem >= 0 && !offline);
	if (m_curItem >= 0)
	{
		const VideoItem& v = m_lib.items[m_curItem];
		m_staticName.SetWindowText(v.FileName());
		if (m_mediaInfo.GetSafeHwnd())
		{
			const int before = m_mediaInfo.NeededWidth();
			m_mediaInfo.SetFile(offline ? CString() : v.path);   // fps / 해상도 (경로별로 한 번만 읽음, 연결 안 된 이동식 디스크는 읽지 않음)
			if (m_layoutReady && m_mediaInfo.NeededWidth() != before)
			{
				CRect client;
				GetClientRect(&client);
				LayoutControls(client.Width(), client.Height());   // 칸 폭이 바뀌면 파일 이름 칸도 다시 맞춤
			}
		}
		m_starRating.SetRating(v.rating);
		m_comboCensor.SetCurSel(v.censor);
		m_dropCounter.SetCount(v.oCount);
		m_editCode.SetWindowText(v.code);
		m_editTitle.SetWindowText(v.title);
		SYSTEMTIME st = {};
		if (ParseDate(v.release, st))
			m_dateRelease.SetTime(&st);
		else
			m_dateRelease.SetTime(static_cast<LPSYSTEMTIME>(nullptr));   // 없음 (체크 해제)
		m_editActors.SetWindowText(v.actors);
		m_editVAliases.SetWindowText(v.actorAliases);
		m_editStudio.SetWindowText(v.studio);
		if (m_labelChips.GetSafeHwnd())
		{
			std::vector<CString> lt;
			if (!v.label.IsEmpty()) lt.push_back(v.label);
			m_labelChips.SetTags(lt);
		}
		if (m_seriesChips.GetSafeHwnd())
		{
			std::vector<CString> st;
			if (!v.series.IsEmpty()) st.push_back(v.series);
			m_seriesChips.SetTags(st);
		}
		m_editSeries.SetWindowText(v.seriesTitle);
		m_editTags.SetWindowText(v.tags);
	}
	else
	{
		m_staticName.SetWindowText(L"(선택된 동영상 없음)");
		if (m_mediaInfo.GetSafeHwnd() && m_mediaInfo.NeededWidth() > 0)
		{
			m_mediaInfo.SetFile(CString());
			if (m_layoutReady)
			{
				CRect client;
				GetClientRect(&client);
				LayoutControls(client.Width(), client.Height());
			}
		}
		m_starRating.SetRating(0);
		m_comboCensor.SetCurSel(0);
		m_dropCounter.SetCount(0);
		m_editCode.SetWindowText(L"");
		m_editTitle.SetWindowText(L"");
		m_dateRelease.SetTime(static_cast<LPSYSTEMTIME>(nullptr));
		m_editActors.SetWindowText(L"");
		m_editVAliases.SetWindowText(L"");
		m_editStudio.SetWindowText(L"");
		if (m_labelChips.GetSafeHwnd())
			m_labelChips.SetTags({});
		if (m_seriesChips.GetSafeHwnd())
			m_seriesChips.SetTags({});
		m_editSeries.SetWindowText(L"");
		m_editTags.SetWindowText(L"");
	}
	UpdatePreview(m_curItem);
	RefreshShotStrip();   // 설정이 영상 스크린샷이면 태그 아래 격자

	m_starRating.EnableWindow(enable);
	m_dropCounter.EnableWindow(enable);
	m_editCode.EnableWindow(enable);
	m_editTitle.EnableWindow(enable);
	GetDlgItem(IDC_BTN_CHANGEIMAGE)->EnableWindow(enable);
	GetDlgItem(IDC_BTN_SEARCHIMAGE)->EnableWindow(enable);
	m_dateRelease.EnableWindow(enable);
	m_comboCensor.EnableWindow(enable);
	m_editActors.EnableWindow(enable);
	GetDlgItem(IDC_BTN_PICKACTORS)->EnableWindow(enable);
	m_editVAliases.EnableWindow(enable);
	GetDlgItem(IDC_BTN_PICKVALIASES)->EnableWindow(enable);
	m_editStudio.EnableWindow(enable);
	GetDlgItem(IDC_BTN_PICKSTUDIO)->EnableWindow(enable);
	m_studioChips.EnableWindow(enable);
	if (m_labelChips.GetSafeHwnd())
		m_labelChips.EnableWindow(enable);
	if (m_seriesChips.GetSafeHwnd())
		m_seriesChips.EnableWindow(enable);
	m_editSeries.EnableWindow(enable);
	GetDlgItem(IDC_BTN_PICKTAGS)->EnableWindow(enable);
	m_editTags.EnableWindow(enable);
	m_tagChips.EnableWindow(enable);
	m_actorChips.EnableWindow(enable);

	m_detailsDirty = false;
	// 임시 항목은 바꾼 내용이 없어도 [저장]으로 정식 DB에 등록할 수 있음
	const bool pending = (m_curItem >= 0 && m_lib.items[m_curItem].pending && !offline);
	GetDlgItem(IDC_BTN_SAVE)->EnableWindow(pending ? TRUE : FALSE);
	if (pending)
		m_staticName.SetWindowText(m_lib.items[m_curItem].FileName() + L"   [임시 - 저장하면 DB 등록]");
	else if (offline)
		m_staticName.SetWindowText(m_lib.items[m_curItem].FileName() + L"   [이동식 디스크 연결 안 됨 - 수정 불가]");
	m_loadingDetails = false;
	RelayoutIfLabelRowChanged();   // 레이블 유무에 따라 [제작사] / [레이블] 줄
}

void CRuliManagerDlg::RelayoutIfLabelRowChanged(bool focusRow)
{
	if (m_studioCard.GetSafeHwnd())
	{
		// 카드 모양: 줄 전환 없이 카드 · 라벨 글자만 갱신
		{ m_studioCard.Invalidate(FALSE); if (m_labelCard.GetSafeHwnd()) m_labelCard.Invalidate(FALSE); }
		const bool shown = LabelRowShown();
		if (shown != m_labelRowShown)
		{
			m_labelRowShown = shown;
			SetDlgItemText(IDC_STATIC_STUDIO_LBL, shown ? L"레이블" : L"제작사");
		}
		return;
	}
	if (!m_layoutReady || LabelRowShown() == m_labelRowShown)
		return;
	CRect client;
	GetClientRect(&client);
	LayoutControls(client.Width(), client.Height());
	if (!focusRow)
		return;
	if (LabelRowShown())
		m_labelChips.m_edit.SetFocus();      // 바뀐 줄에서 계속 입력
	else if (m_studioChips.GetSafeHwnd())
		m_studioChips.m_edit.SetFocus();
}

void CRuliManagerDlg::UpdateCodeSeriesText()
{
	if (!m_staticCodeSeries.GetSafeHwnd())
		return;
	CString code, label, desc;
	if (m_curItem >= 0)
		m_editCode.GetWindowText(code);
	SeriesInfo s;
	if (!code.IsEmpty() && m_lib.SeriesInfoForCode(code, s))
	{
		label = s.label;   // 시리즈의 라벨명
		desc = s.desc;     // 설명 (회색)
	}
	if (label != m_codeSeriesLabel || desc != m_codeSeriesDesc)
	{
		m_codeSeriesLabel = label;
		m_codeSeriesDesc = desc;
		m_staticCodeSeries.Invalidate(FALSE);
	}
}

void CRuliManagerDlg::OnDetailsChanged()
{
	UpdateCodeSeriesText();   // 품번을 고치면 라벨명도 바로
	if (m_studioCard.GetSafeHwnd())
	{
		{ m_studioCard.Invalidate(FALSE); if (m_labelCard.GetSafeHwnd()) m_labelCard.Invalidate(FALSE); }   // 제작사 / 레이블 카드
		CString st;
		m_editStudio.GetWindowText(st);
		st.Trim();
		CString want = (LabelRowShown() && st.IsEmpty()) ? L"레이블" : L"제작사";   // 제작사 + 레이블이면 "제작사" (카드에 둘 다)
		CString cur;
		GetDlgItemText(IDC_STATIC_STUDIO_LBL, cur);
		if (cur != want)
			SetDlgItemText(IDC_STATIC_STUDIO_LBL, want);
	}
	if (m_loadingDetails || m_curItem < 0 || IsItemOffline(m_curItem))
		return;
	m_detailsDirty = true;
	GetDlgItem(IDC_BTN_SAVE)->EnableWindow(TRUE);
}

void CRuliManagerDlg::OnDtnReleaseChanged(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	if (m_actorStrip.GetSafeHwnd())
		m_actorStrip.Invalidate(FALSE);   // 배우 카드의 제작 당시 나이 다시 계산
	OnDetailsChanged();
	*pResult = 0;
}

void CRuliManagerDlg::ExportListText(int what, CWnd* owner)
{
	CommitDetails();
	static const LPCWSTR defNames[4] = { L"actors.txt", L"studios.txt", L"tags.txt", L"videos.txt" };
	static const LPCWSTR kinds[4] = { L"배우", L"제작사 · 레이블", L"태그", L"영상" };
	what = (std::max)(0, (std::min)(3, what));
	CFileDialog fd(FALSE, L"txt", defNames[what], OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST,
		L"텍스트 파일 (*.txt)|*.txt|모든 파일 (*.*)|*.*||", owner);
	if (fd.DoModal() != IDOK)
		return;
	if (!m_lib.ExportListFile(what, fd.GetPathName()))
	{
		AfxMessageBox(L"파일을 저장하지 못했습니다.\n\n" + fd.GetPathName(), MB_ICONWARNING);
		return;
	}
	CString msg;
	msg.Format(L"%s DB 를 텍스트로 내보냈습니다 (암호화 안 함, UTF-8).\n\n%s\n\n"
		L"한 줄에 한 항목, 칸은 탭으로 구분합니다. 고친 뒤 [불러와 일괄 등록]으로 다시 넣을 수 있습니다.",
		kinds[what], static_cast<LPCWSTR>(fd.GetPathName()));
	AfxMessageBox(msg, MB_ICONINFORMATION);
}

void CRuliManagerDlg::ImportListText(int what, CWnd* owner)
{
	CommitDetails();
	static const LPCWSTR kinds[4] = { L"배우", L"제작사 · 레이블", L"태그", L"영상" };
	what = (std::max)(0, (std::min)(3, what));
	CFileDialog fd(TRUE, L"txt", nullptr, OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST,
		L"텍스트 파일 (*.txt)|*.txt|모든 파일 (*.*)|*.*||", owner);
	if (fd.DoModal() != IDOK)
		return;
	CString ask;
	ask.Format(L"이 파일로 %s 을(를) 일괄 등록할까요?\n\n%s\n\n"
		L"없는 항목은 새로 추가하고, 이미 있는 항목(배우 = 고유 번호 · 이름, 영상 = 파일 경로, 나머지 = 이름)은 파일에 값이 있는 칸만 덮어씁니다 (빈 칸은 그대로).",
		kinds[what], static_cast<LPCWSTR>(fd.GetPathName()));
	if (AfxMessageBox(ask, MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;
	int added = 0, updated = 0;
	if (!m_lib.ImportListFile(what, fd.GetPathName(), added, updated))
	{
		AfxMessageBox(L"파일을 읽지 못했습니다.\n\n" + fd.GetPathName(), MB_ICONWARNING);
		return;
	}
	if (what == CVideoLibrary::EXPORT_VIDEOS)
	{
		m_lib.SyncActorsFromVideos();       // 영상에만 있는 배우 · 제작사 · 태그 이름을 목록에 추가
		m_lib.SyncNamedFromVideos();
		m_lib.NormalizeAllVideoActors();
	}
	if (what == CVideoLibrary::EXPORT_ACTORS)
	{
		m_lib.CleanupActorAliases();        // 이름과 같은 별칭 정리
		m_lib.NormalizeAllVideoActors();    // 새 별칭으로 적힌 영상 배우 → 배우 이름 + 참여 별칭
	}
	if (!m_lib.Save())
		AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);
	// 화면 갱신
	ResetThumbnails();   // 사진 · 이미지가 바뀌었을 수 있음
	MarkCategoriesDirty();
	RebuildCategories();
	ApplyFilter();
	if (IsActorGridMode())
		ShowActorInfo(SelectedActorIndex());
	UpdateStatus();
	CString msg;
	msg.Format(L"%s 일괄 등록을 마쳤습니다.\n\n새로 추가: %d개\n바뀐 항목: %d개", kinds[what], added, updated);
	AfxMessageBox(msg, MB_ICONINFORMATION);
}

void CRuliManagerDlg::FillCensorCombo()
{
	if (!m_comboCensor.GetSafeHwnd())
		return;
	const int sel = m_comboCensor.GetCurSel();
	m_comboCensor.ResetContent();
	std::vector<COLORREF> colors;
	for (int i = 0; i < CVideoLibrary::kCensorCount; ++i)
	{
		m_comboCensor.AddString(CVideoLibrary::CensorName(i, 0));   // 영상 상세 콤보는 항상 한글
		colors.push_back(CensorColor(i));                           // 글자색: 영상 카드의 검열 색과 같음
	}
	m_comboCensor.SetItemColors(colors);
	m_comboCensor.SetCurSel(sel >= 0 ? sel : 0);
}

void CRuliManagerDlg::OnCbnSelchangeCensor()
{
	if (m_curItem < 0)
		return;
	OnDetailsChanged();
	CommitDetails();   // 바로 저장 (별점과 같이)
}

void CRuliManagerDlg::CommitDetails()
{
	CommitNamedMemo();   // 제작사 탭 메모 칸도 함께 저장
	if (!m_detailsDirty || m_curItem < 0 || m_curItem >= static_cast<int>(m_lib.items.size()))
		return;
	if (IsItemOffline(m_curItem))
	{
		m_detailsDirty = false;   // 연결 안 된 이동식 디스크 영상은 저장하지 않음
		m_pendingImage.Empty();
		return;
	}

	VideoItem& v = m_lib.items[m_curItem];
	v.rating = m_starRating.GetRating();
	v.censor = (std::max)(0, m_comboCensor.GetCurSel());
	v.oCount = m_dropCounter.GetCount();

	m_editCode.GetWindowText(v.code);
	v.code.Trim();
	m_editTitle.GetWindowText(v.title);
	v.title.Replace(L"\r\n", L" ");   // 붙여 넣은 줄바꿈은 공백으로 (제목은 한 줄 값, 칸에서만 줄바꿈 표시)
	v.title.Replace(L'\r', L' ');
	v.title.Replace(L'\n', L' ');
	v.title.Trim();

	SYSTEMTIME st = {};
	if (m_dateRelease.GetTime(&st) == GDT_VALID)
		v.release.Format(L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
	else
		v.release.Empty();

	CString actors;
	m_editActors.GetWindowText(actors);
	v.actors = NormalizeTags(actors);
	CString valiases;
	m_editVAliases.GetWindowText(valiases);
	v.actorAliases = NormalizeTags(valiases);
	m_lib.NormalizeVideoActors(v);   // 별칭으로 고른 배우 → 배우 이름 + 참여 별칭, 빠진 배우의 별칭 정리

	m_editStudio.GetWindowText(v.studio);
	v.studio.Trim();
	// 레이블: 영상에 저장하지 않음 (품번 DB 의 레이블 - 품번을 바꾸면 다시 찾음)
	v.label = m_lib.LabelForCode(v.code);
	if (m_seriesChips.GetSafeHwnd())
	{
		v.series = m_seriesChips.Tags().empty() ? CString() : m_seriesChips.Tags()[0];
		v.series.Trim();
	}
	m_editSeries.GetWindowText(v.seriesTitle);   // 시리즈 이름 (자유 글자)
	v.seriesTitle.Trim();

	CString tags;
	m_editTags.GetWindowText(tags);
	v.tags = NormalizeTags(tags);
	{
		// 태그의 다른 이름으로 적은 것은 태그 이름으로 (중복은 하나로)
		std::vector<CString> out;
		for (const CString& t : CVideoLibrary::SplitList(v.tags))
		{
			const int n = m_lib.FindNamed(LIST_TAG, t);
			const CString name = (n >= 0) ? m_lib.tagInfos[n].name : t;
			if (std::find_if(out.begin(), out.end(), [&](const CString& o) { return o.CompareNoCase(name) == 0; }) == out.end())
				out.push_back(name);
		}
		v.tags = CVideoLibrary::JoinList(out);
	}


	// 새로 고른 이미지: 영상 파일 옆에 "영상이름.확장자" 로 저장
	if (!m_pendingImage.IsEmpty())
	{
		const CString src = m_pendingImage;
		m_pendingImage.Empty();
		// 분할 파일은 순번을 뗀 이름(ABC-123.jpg)으로 저장 → '_' 왼쪽 일치로 묶음 전체가 같은 이미지 사용
		bool ok = ApplyVideoImage(CVideoLibrary::PartGroupPath(v.path), src);
		if (ok)
		{
			const std::vector<size_t> sibs = m_lib.PartSiblings(static_cast<size_t>(m_curItem));
			if (!sibs.empty() || CVideoLibrary::PartGroupPath(v.path) != v.path)
			{
				// 파일별 이미지(ABC-123_2.jpg 등)가 남아 있으면 그쪽이 먼저 보이므로 휴지통으로
				std::vector<CString> paths;
				paths.push_back(v.path);
				for (size_t si : sibs)
					paths.push_back(m_lib.items[si].path);
				RecyclePartImages(paths, CVideoLibrary::PartGroupPath(v.path));
			}
		}
		if (ok)
		{
			ResetThumbnails();      // 카드 / 목록 이미지 다시 읽기
			UpdatePreview(m_curItem);
		}
		else
			AfxMessageBox(L"이미지를 영상 폴더에 저장하지 못했습니다.", MB_ICONWARNING);
	}

	// 영상 정보를 저장하면 임시 목록에서 정식 DB로 옮김
	const bool registered = v.pending;
	v.pending = false;
	// 분할 파일: 같은 묶음의 다른 파일도 같은 정보로 (임시 파일도 함께 등록)
	if (m_lib.SyncPartGroup(static_cast<size_t>(m_curItem)) > 0)
		UpdateStatus();

	m_detailsDirty = false;
	GetDlgItem(IDC_BTN_SAVE)->EnableWindow(FALSE);
	if (registered)
	{
		m_staticName.SetWindowText(v.FileName());
		UpdateStatus();
	}

	m_loadingDetails = true;
	m_editActors.SetWindowText(v.actors);
	m_editVAliases.SetWindowText(v.actorAliases);
	m_editStudio.SetWindowText(v.studio);
	m_editTags.SetWindowText(v.tags);
	m_loadingDetails = false;

	// 직접 입력한 새 배우/스튜디오/태그는 각 목록에도 추가 (레이블은 그 스튜디오의 레이블 목록에)
	m_lib.SyncActorsFromVideos();
	m_lib.SyncNamedFromVideos();
	m_loadingDetails = true;
	m_editStudio.SetWindowText(v.studio);   // 레이블 이름으로 적은 스튜디오 → 상위 스튜디오로 바뀌었을 수 있음
	if (m_labelChips.GetSafeHwnd())
	{
		std::vector<CString> lt;
		if (!v.label.IsEmpty()) lt.push_back(v.label);
		m_labelChips.SetTags(lt);
	}
	if (m_seriesChips.GetSafeHwnd())
	{
		std::vector<CString> st;
		if (!v.series.IsEmpty()) st.push_back(v.series);
		m_seriesChips.SetTags(st);
	}
	m_loadingDetails = false;
	RelayoutIfLabelRowChanged();   // 저장 때 레이블이 정리되면 줄 전환

	if (!m_lib.Save())
		AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);
	m_list.Invalidate(FALSE);
	m_grid.Invalidate(FALSE);
	MarkCategoriesDirty();   // 배우/스튜디오/태그 개수 갱신
	// 배우 출연작 [연도별]: 참여 별칭(이름별 배경색) · 발매일(칸 위치)이 바뀌었을 수 있으므로 다시 만듦 (스크롤은 유지)
	if (YearViewActive())
		RebuildYearView();
}

void CRuliManagerDlg::OnBnClickedSave()
{
	// 임시 항목은 바꾼 내용이 없어도 저장 → 정식 DB 등록
	if (m_curItem >= 0 && m_curItem < static_cast<int>(m_lib.items.size()) && m_lib.items[m_curItem].pending)
		m_detailsDirty = true;
	CommitDetails();
}

// ---------------------------------------------------------------------------
// 배우 관리 / 선택

void CRuliManagerDlg::OpenActorManager(const CString& selectName)
{
	CommitDetails();

	const CString prevSel = m_actorSelName;   // 배우 탭에서 선택하던 배우 (이름이 바뀌어도 다시 찾기 위해)
	CActorDlg dlg(m_lib, selectName, this);
	dlg.DoModal();
	if (!dlg.m_changed)
		return;

	// 대표 이름이 바뀌었으면(이름 콤보에서 다른 별칭을 골라 저장) 예전 이름은 별칭이 되므로 별칭으로 다시 찾아 선택 유지
	//  - 그대로 두면 선택이 풀려 배우 탭 오른쪽 사진/정보가 비어 보였음
	if (!prevSel.IsEmpty() && m_lib.FindActor(prevSel) < 0)
	{
		const int idx = m_lib.FindActorByAnyName(prevSel);
		if (idx >= 0)
			m_actorSelName = m_lib.actors[idx].name;
	}

	// 배우 이름 변경/삭제가 동영상에도 반영되었을 수 있으므로 화면 갱신
	m_lib.NormalizeAllVideoActors();   // 새로 등록한 별칭으로 적힌 배우 → 배우 이름 + 참여 별칭
	m_lib.Save();
	const int cur = m_curItem;
	ResetThumbnails();   // 배우 사진이 바뀌었을 수 있음
	if (m_mode == MODE_ACTOR && !m_drill.IsEmpty() && m_lib.FindActor(m_drill) < 0)
	{
		BackToList();   // 보고 있던 배우가 삭제/이름 변경됨
		return;
	}
	RebuildCategories();
	if (IsActorGridMode())
		ShowActorInfo(SelectedActorIndex());
	ApplyFilter();
	if (cur >= 0 && cur == m_curItem)
		ShowDetails(cur);
	m_list.Invalidate(FALSE);
	m_grid.Invalidate(FALSE);
}

void CRuliManagerDlg::OnBnClickedSettings()
{
	CommitDetails();
	CSettingsDlg dlg(m_lib, this);
	dlg.m_onDeleteDb = [this](int mask) { DeleteDb(mask); };
	dlg.m_onMakePreviews = [this, &dlg]() { MakeAllPreviews(&dlg); };
	dlg.m_onDeletePreviews = [this, &dlg]() { DeleteAllPreviews(&dlg); };
	dlg.m_onRegenPreviews = [this, &dlg]() { MakeAllPreviews(&dlg, true); };
	dlg.m_onStopPreviews = [this]() { StopPreviews(); };
	dlg.m_isPreviewBusy = [this]() { return m_prevBusy && !m_prevStopped; };
	dlg.m_onExportList = [this, &dlg](int what) { ExportListText(what, &dlg); };
	dlg.m_onImportList = [this, &dlg](int what) { ImportListText(what, &dlg); };
	dlg.m_cardMedia = m_showCardMedia;
	dlg.m_sfw = IsSfwMosaic();
	dlg.m_mergeParts = m_mergeParts;
	dlg.m_ratingMode = CStarRatingCtrl::RatingMode();
	dlg.m_censorLang = CVideoLibrary::CensorLang();
	dlg.m_detailLeft = m_detailLeft;
	dlg.m_detailStrip = m_detailStripMode;
	dlg.m_shotCols = m_shotCols;
	const int savedTheme = Theme::Find(AfxGetApp()->GetProfileString(L"Settings", L"Theme", L"Stash"));   // 저장된 테마 (다시 시작 전이면 지금 화면과 다를 수 있음)
	dlg.m_themeIndex = (savedTheme >= 0) ? savedTheme : Theme::Current();
	const INT_PTR result = dlg.DoModal();
	if (result == IDOK && dlg.m_sfw != IsSfwMosaic())
	{
		// SFW: 배우 · 영상 이미지 모자이크 켜기/끄기 → 읽어 둔 이미지를 모두 버리고 다시 읽음
		SetSfwMosaic(dlg.m_sfw);
		AfxGetApp()->WriteProfileInt(L"Settings", L"Sfw", dlg.m_sfw ? 1 : 0);
		CWaitCursor wait;
		ResetThumbnails();   // 영상 카드 이미지 · 썸네일 · 배우 사진 캐시
		if (m_actorStrip.GetSafeHwnd()) m_actorStrip.Invalidate(FALSE);
		if (m_labelStrip.GetSafeHwnd()) m_labelStrip.Invalidate(FALSE);
		// 지금 보이는 미리보기 · 출연작 화면 사진도 다시
		if (IsActorGridMode())
			ShowActorInfo(SelectedActorIndex());
		else if (m_curItem >= 0)
			UpdatePreview(m_curItem);
		if (DrillHeaderShown())
			UpdateDrillHeader();
	}
	if (result == IDOK && dlg.m_ratingMode != CStarRatingCtrl::RatingMode())
	{
		// 별점 체계: 입력 단위 · 숫자 표시만 바뀜 (저장된 값은 반 별 단위 그대로 - 7 = 3.5점 = 7/10)
		CStarRatingCtrl::SetRatingMode(dlg.m_ratingMode);
		AfxGetApp()->WriteProfileInt(L"Settings", L"RatingMode", CStarRatingCtrl::RatingMode());
		AfxGetApp()->WriteProfileInt(L"Settings", L"RatingHalf", CStarRatingCtrl::HalfStep() ? 1 : 0);   // 예전 버전 호환
		Invalidate(FALSE);   // 별점 칸 숫자 · 카드 리본
		if (m_grid.GetSafeHwnd()) m_grid.Invalidate(FALSE);
		if (m_actorGrid.GetSafeHwnd()) m_actorGrid.Invalidate(FALSE);
		if (m_starRating.GetSafeHwnd()) m_starRating.Invalidate(FALSE);
	}
	if (result == IDOK && dlg.m_censorLang != CVideoLibrary::CensorLang())
	{
		// 검열 정보 표시 언어 → 영상 카드의 검열 글자 (영상 상세 콤보는 항상 한글)
		CVideoLibrary::SetCensorLang(dlg.m_censorLang);
		AfxGetApp()->WriteProfileInt(L"Settings", L"CensorLang", dlg.m_censorLang);
		if (m_grid.GetSafeHwnd())
			m_grid.Invalidate(FALSE);
	}
	if (result == IDOK && dlg.m_cardMedia != m_showCardMedia)
	{
		m_showCardMedia = dlg.m_cardMedia;
		AfxGetApp()->WriteProfileInt(L"Settings", L"CardMediaInfo", m_showCardMedia ? 1 : 0);
		if (!m_showCardMedia)
			StopCardMediaThread();   // 읽고 있던 대기열 멈춤 (이미 읽은 정보는 남겨 둠 → 다시 켜면 바로 표시)
		if (m_grid.GetSafeHwnd())
			m_grid.Invalidate(FALSE);
	}
	if (result == IDOK && dlg.m_detailLeft != m_detailLeft)
	{
		// 상세 화면 위치 (왼쪽 / 오른쪽) → 다시 배치
		m_detailLeft = dlg.m_detailLeft;
		AfxGetApp()->WriteProfileInt(L"Settings", L"DetailLeft", m_detailLeft ? 1 : 0);
		UpdateDetailBar();   // 접기 바 화살표 방향
		if (m_layoutReady)
		{
			CRect client;
			GetClientRect(&client);
			LayoutControls(client.Width(), client.Height());
			RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);   // 양쪽이 바뀌었으므로 전체
		}
	}
	if (result == IDOK && dlg.m_mergeParts != m_mergeParts)
	{
		// 분할 영상 합쳐서 보기 켜기 / 끄기 → 격자 칸 다시 (선택한 영상은 유지)
		m_mergeParts = dlg.m_mergeParts;
		AfxGetApp()->WriteProfileInt(L"Settings", L"MergeParts", m_mergeParts ? 1 : 0);
		m_overlayFade.clear();   // 격자 칸 번호가 바뀜
		ApplyFilter();
	}
	if (result == IDOK && dlg.m_shotCols != m_shotCols)
	{
		// 스크린샷 가로 배치 수 → 칸 크기 다시 계산
		m_shotCols = dlg.m_shotCols;
		AfxGetApp()->WriteProfileInt(L"Settings", L"ShotCols", m_shotCols);
		m_shotCardW = m_shotCardH = 0;
		if (m_layoutReady)
		{
			CRect client;
			GetClientRect(&client);
			LayoutControls(client.Width(), client.Height());
		}
	}
	if (result == IDOK && dlg.m_detailStrip != m_detailStripMode)
	{
		// 영상 상세 태그 아래: 출연 배우 카드 ↔ 영상 스크린샷
		m_detailStripMode = dlg.m_detailStrip;
		AfxGetApp()->WriteProfileInt(L"Settings", L"DetailStrip", m_detailStripMode);
		UpdateLeftPane();      // 두 띠의 보이기 / 숨기기 + 다시 배치
		RefreshShotStrip();
		RefreshActorStrip();
	}
	if (result == IDOK && dlg.m_themeIndex != ((savedTheme >= 0) ? savedTheme : Theme::Current()))
	{
		// 테마: 저장만 하고 다시 시작하면 적용 (색을 컨트롤마다 만들 때 정하므로)
		Theme::SaveToSettings(dlg.m_themeIndex);
		if (dlg.m_themeIndex != Theme::Current())
		{
			CString msg;
			msg.Format(L"테마를 '%s'(으)로 바꿨습니다.\n프로그램을 다시 시작하면 적용됩니다.\n\n지금 다시 시작할까요?", Theme::Name(dlg.m_themeIndex));
			if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION) == IDYES)
			{
				theApp.m_restart = true;
				SendMessage(WM_CLOSE);   // 평소처럼 저장하고 닫음
				if (ContinueModal())
					theApp.m_restart = false;   // 닫기를 취소함 (저장 실패 확인 창에서 [아니요] 등)
			}
		}
	}
}

void CRuliManagerDlg::DeleteDb(int mask)
{
	// 설정 창 [선택한 DB 삭제]: 영상 / 배우 / 스튜디오 / 태그 (파일은 지우지 않음)
	BeginLibraryChange();   // 항목 인덱스가 바뀌기 전에 목록/상세 비움
	if (mask & CSettingsDlg::DB_VIDEO)
		m_lib.items.clear();   // 저장한 정보 + 임시 항목 (다시 스캔하면 임시 항목으로)
	if (mask & CSettingsDlg::DB_ACTOR)
	{
		m_lib.actors.clear();
		for (VideoItem& v : m_lib.items)   // 영상의 배우 지정도 지워야 다시 생기지 않음
		{
			v.actors.Empty();
			v.actorAliases.Empty();
		}
	}
	if (mask & CSettingsDlg::DB_STUDIO)
	{
		m_lib.studios.clear();
		m_lib.labelInfos.clear();   // 레이블은 제작사 하위
		for (VideoItem& v : m_lib.items)
		{
			v.studio.Empty();
			v.label.Empty();
		}
	}
	if (mask & CSettingsDlg::DB_TAG)
	{
		m_lib.tagInfos.clear();
		for (VideoItem& v : m_lib.items)
			v.tags.Empty();
	}
	if (!m_lib.Save())
		AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);

	// 화면 갱신
	ResetThumbnails();
	m_actorSel = -1;
	m_actorSelName.Empty();
	m_catSelKind = -1;
	m_catSelValue.Empty();
	if (m_mode != MODE_VIDEO && !m_drill.IsEmpty())
	{
		BackToList();
		return;
	}
	RebuildCategories();
	ApplyFilter();
	if (IsActorGridMode())
		ShowActorInfo(-1);
	UpdateStatus();
}

void CRuliManagerDlg::OnBnClickedActors()
{
	// [목록 관리 ▾] → 배우 / 스튜디오 / 태그 (열린 채로 다시 누르면 닫힘)
	if (DarkMenu::ClosedByClickOn(GetDlgItem(IDC_BTN_ACTORS)->GetSafeHwnd()))
		return;   // 이 버튼을 눌러 메뉴를 닫았음 - 그 클릭으로 다시 열지 않음
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_MANAGE_ACTORS,  L"배우 관리...");
	menu.AppendMenu(MF_STRING, ID_MANAGE_STUDIOS, L"제작사 관리...");
	menu.AppendMenu(MF_STRING, ID_MANAGE_TAGS,    L"태그 관리...");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_MANAGE_SERIES,  L"품번 관리...");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_MANAGE_FILES,   L"파일 관리...");
	CRect rc;
	GetDlgItem(IDC_BTN_ACTORS)->GetWindowRect(&rc);
	DarkMenu::Track(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON, rc.left, rc.bottom, this);
}

void CRuliManagerDlg::OnManageActors()
{
	CString select;
	if (!m_drill.IsEmpty())
		select = m_drill;
	else if (IsActorGridMode() && SelectedActorIndex() >= 0)
		select = m_lib.actors[SelectedActorIndex()].name;
	OpenActorManager(select);
}

void CRuliManagerDlg::OnNmDblclkCategory(NMHDR* pNMHDR, LRESULT* pResult)
{
	// 스튜디오/태그 목록에서 항목을 더블클릭하면 해당 영상 목록으로
	NMITEMACTIVATE* p = reinterpret_cast<NMITEMACTIVATE*>(pNMHDR);
	*pResult = 0;
	if (IsCategoryListMode() && p->iItem >= 0 && p->iItem < static_cast<int>(m_catRows.size()))
		DrillIntoCategory(p->iItem);
}

void CRuliManagerDlg::OnManageStudios()
{
	CString select;
	if (m_mode == MODE_STUDIO && (m_catKind == CAT_VALUE || m_catKind == CAT_LABEL) && !m_drill.IsEmpty())
		select = m_catValue;
	else if (m_mode == MODE_STUDIO && (m_catSelKind == CAT_VALUE || m_catSelKind == CAT_LABEL))
		select = m_catSelValue;   // 레이블이면 관리 창에서도 그 레이블 선택
	else if (m_curItem >= 0)
		select = m_lib.items[m_curItem].studio;
	OpenListManager(LIST_STUDIO, select);
}

void CRuliManagerDlg::OnManageTags()
{
	CString select;
	if (m_mode == MODE_TAG && m_catKind == CAT_VALUE && !m_drill.IsEmpty())
		select = m_catValue;
	else if (m_mode == MODE_TAG && m_catSelKind == CAT_VALUE)
		select = m_catSelValue;
	OpenListManager(LIST_TAG, select);
}

void CRuliManagerDlg::OnManageFiles()
{
	// 파일 관리 창: 등록 폴더 트리 + 규칙(등록 폴더\배우\[YYYY.MM.DD]품번\품번.mp4 · 품번.jpg) 검사 · 복구
	CommitDetails();
	StopPreviewPlayer();   // 옮길 영상의 미리보기를 쥐고 있지 않게
	StopWallPlayers();
	if (m_pkgPopup.GetSafeHwnd()) m_pkgPopup.Hide();
	if (m_castPopup.GetSafeHwnd()) m_castPopup.Hide();
	CFileManageDlg dlg(m_lib, this);
	dlg.m_onPathChanged = [this](const CString& oldPath, const CString& newPath)
	{
		// 미리보기 영상 파일도 새 경로 이름으로 (없으면 그대로)
		const CString oldPrev = PreviewVideo::PreviewFileFor(oldPath, m_lib.CoverCacheFile(oldPath));
		const CString newPrev = PreviewVideo::PreviewFileFor(newPath, m_lib.CoverCacheFile(newPath));
		if (!oldPrev.IsEmpty() && !newPrev.IsEmpty() && oldPrev.CompareNoCase(newPrev) != 0 && ::PathFileExistsW(oldPrev))
			::MoveFileExW(oldPrev, newPrev, MOVEFILE_REPLACE_EXISTING);
	};
	dlg.m_isBusy = [this](const CString& path) -> bool
	{
		// 스크린샷 추출 · 미리보기 만들기 중이거나 대기 중인 영상은 옮기지 않음
		if (m_shotBusy && m_shotVideoPath.CompareNoCase(path) == 0)
			return true;
		if (ShotQueuePos(path) > 0)
			return true;
		if (m_prevBusy && m_prevBusyPath.CompareNoCase(path) == 0)
			return true;
		for (const PreviewJob& j : m_prevQueue)
			if (j.path.CompareNoCase(path) == 0)
				return true;
		return false;
	};
	dlg.DoModal();
	if (!dlg.m_changed)
		return;
	m_lib.Save();
	ClearVideoCovers();   // 카드 이미지 다시 (경로가 바뀜)
	const int cur = m_curItem;
	ApplyFilter();
	if (cur >= 0 && cur == m_curItem)
		ShowDetails(cur);
	UpdateStatus();
}

void CRuliManagerDlg::OnManageSeries()
{
	// 품번 관리 창: 지금 보고 있는 제작사 / 레이블 (또는 지금 영상의 레이블 · 제작사) 선택
	CString select;
	if (m_mode == MODE_STUDIO && (m_catKind == CAT_VALUE || m_catKind == CAT_LABEL) && !m_drill.IsEmpty())
		select = m_catValue;
	else if (m_mode == MODE_STUDIO && (m_catSelKind == CAT_VALUE || m_catSelKind == CAT_LABEL))
		select = m_catSelValue;
	else if (m_curItem >= 0)
		select = m_lib.items[m_curItem].studio;   // 레이블은 품번 DB 의 칸 (왼쪽 목록은 제작사만)
	CommitDetails();
	CSeriesDlg dlg(m_lib, select, this);
	dlg.DoModal();
	if (!dlg.m_changed)
		return;
	m_lib.RefreshDerivedLabels();   // 영상 레이블 = 품번 DB 의 레이블 (바뀐 레이블 반영)
	m_lib.Save();
	if (!IsCategoryListMode() && !IsActorGridMode())
		ApplyFilter();              // 카드 · [레이블] 보기 묶음 다시
	const int cur = m_curItem;
	if (cur >= 0)
		ShowDetails(cur);   // 품번 옆 라벨 · 설명 다시
	if (IsCategoryListMode())
		ShowNamedInfo(m_listCat.GetNextItem(-1, LVNI_SELECTED));
}

void CRuliManagerDlg::OpenListManager(int kind, const CString& selectName)
{
	CommitDetails();

	CNameListDlg dlg(m_lib, kind, selectName, this);
	dlg.m_getLogo = [this](const CString& path) { return GetStudioLogo(path); };   // 상위 · 레이블 칩의 이미지
	dlg.DoModal();
	if (!dlg.m_changed)
		return;

	m_lib.Save();
	const int cur = m_curItem;
	const int mode = (kind == LIST_STUDIO) ? MODE_STUDIO : MODE_TAG;
	if (m_mode == mode && !m_drill.IsEmpty() &&
		((m_catKind == CAT_VALUE && m_lib.FindNamed(kind, m_catValue) < 0) ||
		 (m_catKind == CAT_LABEL && m_lib.FindLabel(m_catValue) < 0)))
	{
		BackToList();   // 보고 있던 항목이 삭제/이름 변경됨
		return;
	}
	RebuildCategories();
	if (IsCategoryListMode())
		ShowNamedInfo(m_listCat.GetNextItem(-1, LVNI_SELECTED));
	ApplyFilter();
	if (cur >= 0 && cur == m_curItem)
		ShowDetails(cur);
	m_list.Invalidate(FALSE);
	m_grid.Invalidate(FALSE);
}

void CRuliManagerDlg::PickNamed(int kind)
{
	if (m_curItem < 0 || IsItemOffline(m_curItem))
		return;

	CEdit& edit = (kind == LIST_STUDIO) ? m_editStudio : m_editTags;
	CString current;
	edit.GetWindowText(current);
	current.Trim();
	CString curPub;   // 지금 붙은 공용 레이블 (상위 제작사가 없는 레이블 - 제작사와 함께 선택된 상태로 엶)
	if (kind == LIST_STUDIO && LabelRowShown())
	{
		const CString lb = m_labelChips.Tags()[0];
		const int li = m_lib.FindLabel(lb);
		if (li >= 0 && m_lib.FindNamed(LIST_STUDIO, m_lib.labelInfos[li].parent) < 0)
			curPub = m_lib.labelInfos[li].name;   // 공용 레이블: 제작사는 그대로 선택, 레이블은 따로
		else if (li >= 0)
			current = lb;   // 제작사 소속 레이블이면 레이블이 선택된 상태로 (등록 안 된 레이블은 제작사로 잘못 추가되므로 제작사로)
	}

	CNamePickDlg dlg(m_lib, kind, current, this);
	dlg.m_curLabel = curPub;
	const bool ok = (dlg.DoModal() == IDOK);
	if (ok && kind == LIST_STUDIO && (dlg.m_result != current || dlg.m_resultLabel.CompareNoCase(curPub) != 0) && m_studioChips.GetSafeHwnd())
	{
		// 제작사 칩에 넣은 것과 같게 처리: 레이블이면 레이블 + 상위 제작사, 제작사면 다른 제작사 레이블은 비움
		std::vector<CString> tags;
		if (!dlg.m_result.IsEmpty())
			tags.push_back(dlg.m_result);
		else if (!dlg.m_resultLabel.IsEmpty())
			tags.push_back(dlg.m_resultLabel);   // 공용 레이블만 → 제작사 없이 레이블
		else if (m_labelChips.GetSafeHwnd())
			m_labelChips.SetTags({});   // 선택 해제 = 제작사 · 레이블 모두 비움
		m_studioChips.SetTags(tags);
		if (m_studioChips.m_onChanged)
			m_studioChips.m_onChanged();
		if (m_labelChips.GetSafeHwnd() && !dlg.m_result.IsEmpty())
		{
			if (!dlg.m_resultLabel.IsEmpty() && m_lib.FindNamed(LIST_STUDIO, dlg.m_result) >= 0)
				m_labelChips.SetTags({ dlg.m_resultLabel });   // 제작사 + 공용 레이블
			else if (dlg.m_resultLabel.IsEmpty() && !curPub.IsEmpty() && !m_labelChips.Tags().empty() &&
			         m_labelChips.Tags()[0].CompareNoCase(curPub) == 0)
				m_labelChips.SetTags({});   // 공용 레이블 체크를 풀었음
		}
		OnDetailsChanged();
		CommitDetails();
		RelayoutIfLabelRowChanged();
	}
	else if (ok && dlg.m_result != current)
	{
		edit.SetWindowText(dlg.m_result);
		OnDetailsChanged();
		CommitDetails();   // 저장 + 분류 갱신
	}
	else if (dlg.m_added)   // 취소했어도 새로 만든 항목은 유지
	{
		m_lib.Save();
		MarkCategoriesDirty();
	}
}

void CRuliManagerDlg::OnStnClickedRatingLabel()
{
	// [별점] 라벨을 클릭하면 별점 0점(없음)으로 리셋 + 바로 저장
	if (m_curItem < 0 || !m_starRating.IsWindowEnabled() || m_starRating.GetRating() == 0)
		return;
	m_starRating.SetRating(0);
	OnDetailsChanged();
	CommitDetails();
	m_grid.Invalidate(FALSE);   // 카드의 별점 리본
}

void CRuliManagerDlg::OnBnClickedPickStudio()
{
	PickNamed(LIST_STUDIO);
}

void CRuliManagerDlg::ApplyDefaultAliases(const CString& oldActors, const CString& newActors)
{
	// 새로 추가된 배우(칩 입력, 배우 선택 창)는 그 배우의 마지막 고른 별칭을 이 작품의 별칭으로 넣음
	std::vector<int> before;
	for (const CString& n : CVideoLibrary::SplitList(oldActors))
		before.push_back(m_lib.FindActorByAnyName(n));
	CString credited;
	m_editVAliases.GetWindowText(credited);
	std::vector<CString> list = CVideoLibrary::SplitList(credited);
	bool changed = false;
	for (const CString& n : CVideoLibrary::SplitList(newActors))
	{
		const int idx = m_lib.FindActorByAnyName(n);
		if (idx < 0 || std::find(before.begin(), before.end(), idx) != before.end())
			continue;
		const ActorInfo& a = m_lib.actors[idx];
		if (n.CompareNoCase(a.name) != 0)
			continue;   // 별칭으로 직접 입력했으면 그 별칭 그대로
		if (a.lastAlias.IsEmpty() || a.lastAlias.CompareNoCase(a.name) == 0)
			continue;
		bool valid = false;
		for (const CString& al : CVideoLibrary::SplitList(a.aliases))
			if (al.CompareNoCase(a.lastAlias) == 0) { valid = true; break; }
		if (!valid)
			continue;
		bool has = false;
		for (const CString& al : list)
			if (m_lib.FindActorByAnyName(al) == idx) { has = true; break; }
		if (has)
			continue;
		list.push_back(a.lastAlias);
		changed = true;
	}
	if (changed)
		m_editVAliases.SetWindowText(CVideoLibrary::JoinList(list));   // EN_CHANGE → 칩 갱신
}

CString CRuliManagerDlg::ActorChipDisplay(const CString& tag)
{
	const int idx = m_lib.FindActorByAnyName(tag);
	if (idx < 0)
		return tag;
	// 1) 이 작품에서 고른 별칭 (숨긴 별칭 칸 중 이 배우의 것, 마지막 것)
	CString credited;
	m_editVAliases.GetWindowText(credited);
	CString pick;
	for (const CString& al : CVideoLibrary::SplitList(credited))
		if (m_lib.FindActorByAnyName(al) == idx)
			pick = al;
	if (!pick.IsEmpty())
		return pick;
	// 2) 칩이 별칭으로 적혀 있으면(저장 전) 그 별칭, 3) 배우 대표 이름
	return tag;
}

void CRuliManagerDlg::OnActorChipMenu(int index, CPoint screenPt)
{
	if (m_curItem < 0 || index < 0 || index >= static_cast<int>(m_actorChips.Tags().size()))
		return;
	const CString tag = m_actorChips.Tags()[index];
	const int idx = m_lib.FindActorByAnyName(tag);
	if (idx < 0)
		return;
	const ActorInfo& a = m_lib.actors[idx];
	// 메뉴에는 별칭만 (대표 이름은 고르지 않음, 체크된 별칭을 다시 고르면 해제 → 대표 이름 표시)
	std::vector<CString> names;
	for (const CString& al : CVideoLibrary::SplitList(a.aliases))
		if (al.CompareNoCase(a.name) != 0)
			names.push_back(al);
	const CString current = ActorChipDisplay(tag);

	CMenu menu;
	menu.CreatePopupMenu();
	if (names.empty())
		menu.AppendMenu(MF_STRING | MF_GRAYED, 0, L"(등록된 별칭 없음)");
	for (size_t i = 0; i < names.size(); ++i)
		menu.AppendMenu(MF_STRING | (names[i].CompareNoCase(current) == 0 ? MF_CHECKED : 0), static_cast<UINT_PTR>(i + 1), names[i]);
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, 1000, L"배우 관리에서 편집...");
	const UINT cmd = DarkMenu::Track(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY, screenPt.x, screenPt.y, this);
	if (cmd == 0)
		return;
	if (cmd == 1000)
	{
		OpenActorManager(a.name);
		return;
	}
	if (cmd > names.size())
		return;
	CString chosen = names[cmd - 1];
	if (chosen.CompareNoCase(current) == 0)
		chosen = a.name;   // 체크된 별칭을 다시 고르면 해제 (대표 이름으로 표시)

	// 이 작품의 별칭: 이 배우 것은 고른 별칭으로 교체 (해제하면 별칭 없음)
	CString credited;
	m_editVAliases.GetWindowText(credited);
	std::vector<CString> list;
	for (const CString& al : CVideoLibrary::SplitList(credited))
		if (m_lib.FindActorByAnyName(al) != idx)
			list.push_back(al);
	if (chosen.CompareNoCase(a.name) != 0)
		list.push_back(chosen);
	// 다음에 이 배우를 넣을 때 초기값으로 쓸 별칭 (해제하면 대표 이름)
	m_lib.actors[idx].lastAlias = (chosen.CompareNoCase(a.name) != 0) ? chosen : CString();

	// 칩이 별칭으로 적혀 있으면 배우 대표 이름으로 정리
	CString actorsText;
	m_editActors.GetWindowText(actorsText);
	std::vector<CString> actors = CVideoLibrary::SplitList(actorsText);
	for (CString& n : actors)
		if (m_lib.FindActorByAnyName(n) == idx)
			n = a.name;

	m_editVAliases.SetWindowText(CVideoLibrary::JoinList(list));     // EN_CHANGE → 칩 갱신
	m_editActors.SetWindowText(CVideoLibrary::JoinList(CVideoLibrary::SplitList(CVideoLibrary::JoinList(actors))));
	OnDetailsChanged();
	CommitDetails();   // 바로 저장
}

void CRuliManagerDlg::OnEnChangeVAliases()
{
	// 참여 별칭이 바뀌면 배우 칩 표시(별칭)도 갱신
	if (m_actorChips.GetSafeHwnd())
		m_actorChips.SetTags(m_actorChips.Tags());
	OnDetailsChanged();
}

void CRuliManagerDlg::OnEnChangeStudio()
{
	// 숨긴 스튜디오 칸이 바뀌면 (영상 선택, [선택...] 결과, 저장 후 정리) 칩도 갱신
	if (!m_syncingStudio && m_studioChips.GetSafeHwnd())
	{
		CString text;
		m_editStudio.GetWindowText(text);
		text.Trim();
		std::vector<CString> tags;
		if (!text.IsEmpty())
			tags.push_back(text);
		m_studioChips.SetTags(tags);
	}
	OnDetailsChanged();
}

void CRuliManagerDlg::OnEnChangeActors()
{
	// 숨긴 배우 칸이 바뀌면 (영상 선택, [선택...] 결과, 별칭 칸에서 추가, 저장 후 정리) 칩도 갱신
	if (!m_syncingActors)
	{
		CString text;
		m_editActors.GetWindowText(text);
		m_actorChips.SetTags(CVideoLibrary::SplitList(text));
	}
	RefreshActorStrip();   // 태그 아래 배우 카드도 갱신
	OnDetailsChanged();
}

void CRuliManagerDlg::SortTagsKo(std::vector<CString>& tags)
{
	// 가나다 순 (한글 → 자모 순, 영문 · 숫자는 Windows 정렬 규칙 - 숫자는 크기 순)
	std::stable_sort(tags.begin(), tags.end(), [](const CString& a, const CString& b)
	{
		return ::StrCmpLogicalW(a, b) < 0;
	});
}

void CRuliManagerDlg::OnEnChangeTags()
{
	// 숨긴 태그 칸이 바뀌면 (영상 선택, [선택...] 결과, 저장 후 정리) 칩도 갱신
	if (!m_syncingTags)
	{
		CString text;
		m_editTags.GetWindowText(text);
		std::vector<CString> tags = CVideoLibrary::SplitList(text);
		SortTagsKo(tags);   // 영상 상세 태그는 가나다 순
		m_tagChips.SetTags(tags);
	}
	OnDetailsChanged();
}

void CRuliManagerDlg::OnBnClickedPickVAliases()
{
	if (DarkMenu::ClosedByClickOn(GetDlgItem(IDC_BTN_PICKVALIASES)->GetSafeHwnd()))
		return;   // 열린 메뉴를 이 버튼으로 닫았음 - 다시 열지 않음
	if (m_curItem < 0 || IsItemOffline(m_curItem))
		return;

	CString actorsText, current;
	m_editActors.GetWindowText(actorsText);
	m_editVAliases.GetWindowText(current);
	std::vector<CString> chosen = CVideoLibrary::SplitList(current);

	// 이 영상 배우들의 별칭 목록 (메뉴: "별칭   (배우)")
	struct Entry { CString alias; CString actor; };
	std::vector<Entry> entries;
	for (const CString& n : CVideoLibrary::SplitList(actorsText))
	{
		const int idx = m_lib.FindActorByAnyName(n);
		if (idx < 0)
			continue;
		for (const CString& al : CVideoLibrary::SplitList(m_lib.actors[idx].aliases))
			entries.push_back({ al, m_lib.actors[idx].name });
	}

	auto isChosen = [&](const CString& s)
	{
		for (const CString& c : chosen)
			if (c.CompareNoCase(s) == 0) return true;
		return false;
	};

	CMenu menu;
	menu.CreatePopupMenu();
	if (entries.empty())
	{
		menu.AppendMenu(MF_STRING | MF_GRAYED, 0,
			actorsText.IsEmpty() ? L"(먼저 배우를 지정하세요)" : L"(지정된 배우에 등록된 별칭이 없습니다 - 배우 관리에서 추가)");
	}
	else
	{
		for (size_t i = 0; i < entries.size(); ++i)
		{
			CString label = entries[i].alias + L"\t" + entries[i].actor;
			menu.AppendMenu(MF_STRING | (isChosen(entries[i].alias) ? MF_CHECKED : 0),
				static_cast<UINT_PTR>(i + 1), label);
		}
	}
	if (!chosen.empty())
	{
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, 9999, L"모두 지우기");
	}

	CRect rc;
	GetDlgItem(IDC_BTN_PICKVALIASES)->GetWindowRect(&rc);
	const UINT cmd = DarkMenu::Track(menu, TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
		rc.right, rc.bottom, this);
	if (cmd == 0)
		return;

	if (cmd == 9999)
	{
		chosen.clear();
	}
	else if (cmd <= entries.size())
	{
		const CString& al = entries[cmd - 1].alias;
		if (isChosen(al))
			chosen.erase(std::remove_if(chosen.begin(), chosen.end(),
				[&](const CString& c) { return c.CompareNoCase(al) == 0; }), chosen.end());
		else
			chosen.push_back(al);
	}

	const CString result = CVideoLibrary::JoinList(chosen);
	if (result != current)
	{
		m_editVAliases.SetWindowText(result);
		OnDetailsChanged();
		CommitDetails();   // 저장
	}
}

void CRuliManagerDlg::OnBnClickedPickTags()
{
	PickNamed(LIST_TAG);
}

void CRuliManagerDlg::OnBnClickedPickActors()
{
	if (m_curItem < 0 || IsItemOffline(m_curItem))
		return;

	CString current;
	m_editActors.GetWindowText(current);

	CActorPickDlg dlg(m_lib, current, this);
	if (dlg.DoModal() != IDOK)
	{
		if (dlg.m_added)   // 취소했어도 새로 만든 배우는 유지
		{
			m_lib.Save();
			MarkCategoriesDirty();
		}
		return;
	}

	if (dlg.m_result != current)
	{
		m_editActors.SetWindowText(dlg.m_result);
		ApplyDefaultAliases(current, dlg.m_result);
		OnDetailsChanged();
		CommitDetails();   // 저장 + 분류 갱신
	}
	else if (dlg.m_added)
	{
		m_lib.Save();
		MarkCategoriesDirty();
	}
}

// ---------------------------------------------------------------------------
// 배우 보기: 배우 격자 → (더블클릭) 출연작 → [◀ 배우 목록]

void CRuliManagerDlg::UpdateLeftPane()
{
	const bool actorGrid = IsActorGridMode();
	const bool catList = IsCategoryListMode();
	const bool drill = (m_mode != MODE_VIDEO && !m_drill.IsEmpty());
	const bool showVideos = !actorGrid && !catList;

	m_listCat.ShowWindow(catList && !CatGridActive() ? SW_SHOW : SW_HIDE);
	m_catGrid.SetShowImage(m_mode != MODE_TAG);   // 태그 카드는 이미지 없이 글자만
	if (m_mode == MODE_STUDIO || TagLikeMode())
		m_catGrid.SetFixedTile(m_scardW, m_scardH);   // 스튜디오: 로고 카드, 태그: 꼬리표 카드, 컬렉션: 대표 영상 카드
	else
		m_catGrid.SetFixedTile(0, 0);
	m_catGrid.ShowWindow(catList && CatGridActive() ? SW_SHOW : SW_HIDE);
	m_actorGrid.ShowWindow(actorGrid ? SW_SHOW : SW_HIDE);
	GetDlgItem(IDC_BTN_ACTOR_BACK)->ShowWindow(drill ? SW_SHOW : SW_HIDE);
	// 배우 출연작 화면: [◀ 배우 목록] 줄 아래 배우 사진 + 정보 (고정) - 이름은 정보 쪽에 크게 나오므로 제목 글자는 숨김
	const bool drillHeader = DrillHeaderShown();
	const bool colHeader = NamedHeaderShown();   // 컬렉션 · 제작사 영상 화면: 커버 · 로고 + 제목 · 메모 (제목 글자는 숨김)
	GetDlgItem(IDC_STATIC_ACTOR_TITLE)->ShowWindow(drill && !drillHeader && !colHeader ? SW_SHOW : SW_HIDE);
	if (m_colCover.GetSafeHwnd()) m_colCover.ShowWindow(colHeader ? SW_SHOW : SW_HIDE);
	if (m_colInfo.GetSafeHwnd()) m_colInfo.ShowWindow(colHeader ? SW_SHOW : SW_HIDE);
	if (m_drillPhoto.GetSafeHwnd()) m_drillPhoto.ShowWindow(drillHeader ? SW_SHOW : SW_HIDE);
	if (m_drillPanel.GetSafeHwnd()) m_drillPanel.ShowWindow(drillHeader ? SW_SHOW : SW_HIDE);
	// 영상은 격자만 표시 (목록 컨트롤은 숨겨 둔 채 선택/데이터 관리용으로만 사용)
	// 영상 탭 [리스트] 보기: 목록 컨트롤을 보이고, 격자는 머리 영역(도구줄)만 (스크롤바 없음)
	const bool videoList = VideoListActive();
	m_list.ShowWindow(videoList ? SW_SHOW : SW_HIDE);
	m_grid.SetScrollHidden(videoList);
	if (m_zoomSlider.GetSafeHwnd())
		m_zoomSlider.ShowWindow(videoList || ActorTreemapActive() ? SW_HIDE : SW_SHOW);   // [리스트] 보기 · 배우 [트리맵] 은 카드가 없으므로 확대 슬라이더 숨김
	m_grid.ShowWindow(showVideos && !YearViewActive() ? SW_SHOW : SW_HIDE);   // 연도별 보기에서는 격자(도구줄 포함) 숨김
	if (m_yearView.GetSafeHwnd())
		m_yearView.ShowWindow(showVideos && YearViewActive() ? SW_SHOW : SW_HIDE);   // 배우 출연작 [연도별]
	for (CDarkButton& b : m_drillViewBtns)
		if (b.GetSafeHwnd())
			b.ShowWindow(drillHeader ? SW_SHOW : SW_HIDE);   // [격자] [연도별] 은 배우 출연작 화면에서만
	for (CDarkButton& b : m_studioViewBtns)
		if (b.GetSafeHwnd())
			b.ShowWindow(catList && m_mode == MODE_STUDIO ? SW_SHOW : SW_HIDE);   // [격자] [리스트] 는 제작사 카드 목록에서만
	for (CDarkButton& b : m_genderBtns)
		if (b.GetSafeHwnd())
			b.ShowWindow(actorGrid ? SW_SHOW : SW_HIDE);   // [선택없음] [여성만] [남성만] 은 배우 격자에서만
	for (int i = 0; i < 2; ++i)
		if (m_actorViewBtns[i].GetSafeHwnd())
		{
			m_actorViewBtns[i].SetChecked(i == m_actorView);
			m_actorViewBtns[i].ShowWindow(actorGrid ? SW_SHOW : SW_HIDE);   // [격자] [트리맵] 은 배우 목록에서만
		}
	if (m_comboTmBasis.GetSafeHwnd())
		m_comboTmBasis.ShowWindow(ActorTreemapActive() ? SW_SHOW : SW_HIDE);   // 기준 콤보는 트리맵 보기에서만
	if (m_treemap.GetSafeHwnd())
		m_treemap.ShowWindow(ActorTreemapActive() ? SW_SHOW : SW_HIDE);
	{
		// [격자] [리스트] [패키지]: 영상 탭 · 배우 출연작(연도별 제외) · 제작사(레이블) · 태그 · 컬렉션 영상 화면, 선택은 그 화면 값
		const int vs = VideoViewSlot();
		for (int i = 0; i < 5; ++i)
			if (m_videoModeBtns[i].GetSafeHwnd())
			{
				if (vs >= 0)
					m_videoModeBtns[i].SetChecked(i == m_videoView[vs]);
				const bool show = vs >= 0 && (i < 4 || vs == 2);   // [레이블] 은 제작사 · 레이블 영상 화면만
				m_videoModeBtns[i].ShowWindow(show ? SW_SHOW : SW_HIDE);
			}
		if (m_videoModeBtns[3].GetSafeHwnd())
			m_videoModeBtns[3].SetSegment(vs == 2 ? CDarkButton::SEG_MIDDLE : CDarkButton::SEG_RIGHT);   // [월 모드] 오른쪽에 [레이블] 이 붙으면 가운데
		// 예전 [가로|세로] 는 [패키지] 로 옮겨서 쓰지 않음
		for (CDarkButton& b : m_videoViewBtns)
			if (b.GetSafeHwnd())
				b.ShowWindow(SW_HIDE);
	}
	{
		// [장르숨김] [전체]: 영상 탭 · 배우 출연작 · 제작사(레이블) 영상 화면 (연도별 보기 제외), 선택은 그 화면 값
		const int gs = GenreSlot();
		const bool genreBtns = gs >= 0 && !YearViewActive() && !VideoLabelActive();   // [레이블] 보기: 장르 숨김
		for (int i = 0; i < 2; ++i)
			if (m_genreBtns[i].GetSafeHwnd())
			{
				if (gs >= 0)
					m_genreBtns[i].SetChecked((i == 1) == m_genreShow[gs]);
				m_genreBtns[i].ShowWindow(genreBtns ? SW_SHOW : SW_HIDE);
			}
	}
	if (m_genreBar.GetSafeHwnd())
		m_genreBar.ShowWindow(GenreBarActive() ? SW_SHOW : SW_HIDE);

	// [표시: 목록 | 격자]는 스튜디오/태그 목록에서만, [정렬]은 영상이 보일 때만
	const UINT viewIds[] = { IDC_STATIC_VIEW, IDC_RADIO_LISTVIEW, IDC_RADIO_GRIDVIEW };
	for (UINT id : viewIds)
		GetDlgItem(id)->ShowWindow(SW_HIDE);   // 모든 화면이 격자 표시라 [표시: 목록 | 격자]는 사용하지 않음
	const UINT sortIds[] = { IDC_COMBO_SORT, IDC_BTN_SORTDIR };
	for (UINT id : sortIds)
		GetDlgItem(id)->ShowWindow((showVideos && !VideoLabelActive()) || (actorGrid && !ActorTreemapActive()) || IsCatCardMode() ? SW_SHOW : SW_HIDE);   // [레이블] 보기: 정렬 숨김 (레이블 순번으로 고정)   // 배우 [트리맵]: 정렬 숨김 (값 큰 순서)   // 배우 격자: [이름 | 작품수], 제작사 · 태그 카드: [이름 | 영상수]
	// 목록 위 도구줄: [검색] [별점(영상이 보일 때만)] ... - 예전 "검색" / "별점" / "정렬" 글자는 쓰지 않음
	const UINT oldLabels[] = { IDC_STATIC_SEARCH, IDC_STATIC_FILTER, IDC_STATIC_SORT };
	for (UINT id : oldLabels)
		if (CWnd* w = GetDlgItem(id)) w->ShowWindow(SW_HIDE);
	GetDlgItem(IDC_COMBO_FILTER)->ShowWindow(SW_HIDE);   // 별점 거르기 콤보는 없앰 (어느 화면에서도 숨김, 거르기도 하지 않음)
	GetDlgItem(IDC_COMBO_PAGESIZE)->ShowWindow(StudioListView() || ActorTreemapActive() || VideoLabelActive() ? SW_HIDE : SW_SHOW);   // [레이블]: 페이지 없이 전체   // 배우 [트리맵]: 페이지 없이 전체   // 제작사 [리스트]: 페이지당 개수 숨김 (전체 표시)
	UpdateSortUI();   // 영상 / 배우 정렬 항목으로 콤보 채움
	UpdatePager();

	// 영상 상세 정보(편집 칸)는 영상 탭 + 배우 출연작 화면에서 표시 (태그 아래 배우 카드는 영상 탭에서만)
	const bool showDetail = ShowVideoDetail();
	if (!showDetail)
		HideSuggestions();
	const UINT detailIds[] = {
		IDC_STATIC_NAME_LBL,
		IDC_STATIC_CODE_LBL, IDC_EDIT_CODE, IDC_BTN_COPYCODE, IDC_STATIC_TITLE_LBL, IDC_EDIT_TITLE, IDC_BTN_CHANGEIMAGE, IDC_BTN_SEARCHIMAGE, IDC_MEDIA_INFO,   // 품번 오른쪽 레이블 글자(IDC_STATIC_CODE_SERIES)는 늘 숨김 - 아래 레이블 카드로
		IDC_STATIC_RATING_LBL, IDC_COMBO_RATING, IDC_DROP_COUNTER, IDC_BTN_SAVE,
		IDC_STATIC_RELEASE_LBL, IDC_DATE_RELEASE, IDC_COMBO_CENSOR,
		IDC_STATIC_ACTORS_LBL, IDC_ACTOR_CHIPS,
		IDC_STATIC_STUDIO_LBL, IDC_STUDIO_CHIPS,
		IDC_STATIC_LABEL_LBL, IDC_LABEL_CHIPS, IDC_STUDIO_CARD, IDC_LABEL_CARD,
		IDC_STATIC_TAGS_LBL, IDC_TAG_CHIPS
	};
	for (UINT id : detailIds)
	{
		if (CWnd* w = GetDlgItem(id))
			w->ShowWindow(showDetail ? SW_SHOW : SW_HIDE);
	}
	if (CWnd* w = GetDlgItem(IDC_STATIC_CODE_SERIES))
		w->ShowWindow(SW_HIDE);   // 품번 오른쪽 레이블 · 설명 글자 없앰
	// 시리즈(품번) 칩은 숨김 (값 보관), 시리즈 이름 글자 칸을 표시
	if (CWnd* w = GetDlgItem(IDC_SERIES_CHIPS))
		w->ShowWindow(SW_HIDE);
	for (UINT id : { static_cast<UINT>(IDC_STATIC_SERIES_LBL), static_cast<UINT>(IDC_EDIT_SERIES) })
		if (CWnd* w = GetDlgItem(id))
			w->ShowWindow(showDetail ? SW_SHOW : SW_HIDE);
	if (m_actorStrip.GetSafeHwnd())
		m_actorStrip.ShowWindow(VideoStripShown() && !ShotStripShown() ? SW_SHOW : SW_HIDE);
	if (m_shotStrip.GetSafeHwnd())
		m_shotStrip.ShowWindow(ShotStripShown() ? SW_SHOW : SW_HIDE);   // 설정: 출연 배우 카드 대신 영상 스크린샷
	// 배우 별점은 배우 격자 화면에서만
	// 배우 격자 화면: 이름/정보 두 줄 대신 배우 상세 패널
	// 태그 탭: 오른쪽 상세 페이지(이미지 + 이름/정보) 자체를 숨김
	const bool noDetailPane = TagLikeMode();   // 태그 · 컬렉션
	if (m_actorPanel.GetSafeHwnd())
		m_actorPanel.ShowWindow(actorGrid ? SW_SHOW : SW_HIDE);
	m_staticName.ShowWindow(actorGrid || noDetailPane ? SW_HIDE : SW_SHOW);
	m_staticImage.ShowWindow(actorGrid || noDetailPane ? SW_HIDE : SW_SHOW);
	m_preview.ShowWindow(noDetailPane ? SW_HIDE : SW_SHOW);
	// 제작사 탭 상세 전용 (메모 · 레이블 카드 · 출연 배우 카드): 다른 탭에서는 숨김 (제작사 탭은 LayoutControls 가 다시 정함)
	if (m_mode != MODE_STUDIO)
	{
		if (m_editNamedMemo.GetSafeHwnd()) m_editNamedMemo.ShowWindow(SW_HIDE);
		if (m_labelStrip.GetSafeHwnd() && !actorGrid) m_labelStrip.ShowWindow(SW_HIDE);   // 배우 상세는 LayoutControls 가 정함
		if (m_namedLinks.GetSafeHwnd()) m_namedLinks.ShowWindow(SW_HIDE);
		if (m_mode != MODE_VIDEO && m_actorStrip.GetSafeHwnd()) m_actorStrip.ShowWindow(SW_HIDE);
	}

	if (m_layoutReady)
	{
		CRect client;
		GetClientRect(&client);
		LayoutControls(client.Width(), client.Height());
	}
}

int CRuliManagerDlg::SelectedActorIndex() const
{
	if (m_actorSel < 0 || m_actorSel >= static_cast<int>(m_actorRows.size()))
		return -1;
	return m_actorRows[m_actorSel];
}

void CRuliManagerDlg::RebuildActorGrid()
{
	CString query;
	m_editSearch.GetWindowText(query);
	query.Trim();
	query.MakeLower();

	// 출연작 수
	m_actorCounts.clear();
	m_actorBytes.clear();
	const std::map<CString, int> nameIndex = m_lib.ActorNameIndex();
	for (const VideoItem& v : m_lib.items)
	{
		std::set<CString> seen;   // 이름과 별칭이 함께 적혀 있어도 한 편으로
		for (const CString& n : CVideoLibrary::SplitList(v.actors))
		{
			const CString key = m_lib.ActorKeyOf(nameIndex, n);   // 별칭 → 배우 이름
			if (seen.insert(key).second)
			{
				++m_actorCounts[key];
				m_actorBytes[key] += v.size;   // 트리맵 [용량] 기준
			}
		}
	}


	m_actorRows.clear();
	for (size_t i = 0; i < m_lib.actors.size(); ++i)
	{
		const ActorInfo& a = m_lib.actors[i];
		if (m_actorGender == 1 && a.gender != L"여성")
			continue;   // 성별 거르기: 여성만
		if (m_actorGender == 2 && a.gender != L"남성")
			continue;   // 남성만
		if (!query.IsEmpty())
		{
			CString hay = a.name + L"\n" + a.aliases;
			hay.MakeLower();
			if (hay.Find(query) < 0)
				continue;
		}
		m_actorRows.push_back(static_cast<int>(i));
	}
	auto countOf = [this](const ActorInfo& a) -> int
	{
		CString key = a.name;
		key.MakeLower();
		auto it = m_actorCounts.find(key);
		return (it != m_actorCounts.end()) ? it->second : 0;
	};
	std::sort(m_actorRows.begin(), m_actorRows.end(), [this, &countOf](int a, int b)
	{
		// 즐겨찾기한 배우를 먼저, 그 안에서는 정렬 콤보 기준 (이름 / 작품수, 같으면 이름 순)
		const ActorInfo& x = m_lib.actors[a];
		const ActorInfo& y = m_lib.actors[b];
		if (x.favorite != y.favorite)
			return x.favorite;
		if (m_actorSortCol == 1)
		{
			const int cx = countOf(x), cy = countOf(y);
			if (cx != cy)
				return m_actorSortAsc ? (cx < cy) : (cx > cy);
			return ::StrCmpLogicalW(x.name, y.name) < 0;
		}
		const int c = ::StrCmpLogicalW(x.name, y.name);
		return m_actorSortAsc ? (c < 0) : (c > 0);
	});

	// 페이지 나누기: 지금 페이지의 배우만 (m_actorRows = 그 페이지)
	m_totalActor = static_cast<int>(m_actorRows.size());
	{
		int first = 0, last = 0;
		SlicePage(m_pageActor, m_totalActor, first, last);
		if (m_actorView == 1)
			first = 0, last = m_totalActor;   // [트리맵]: 페이지 나누기 없이 전체 (출연작 화면에 있어도 - 돌아오면 바로 차트)
		if (first > 0 || last < m_totalActor)
			m_actorRows = std::vector<int>(m_actorRows.begin() + first, m_actorRows.begin() + last);
	}

	// 이전 선택 복원
	m_actorSel = -1;
	for (size_t row = 0; row < m_actorRows.size(); ++row)
	{
		if (m_lib.actors[m_actorRows[row]].name.CompareNoCase(m_actorSelName) == 0)
		{
			m_actorSel = static_cast<int>(row);
			break;
		}
	}

	m_actorGrid.Refresh();
	if (m_actorSel >= 0)
		m_actorGrid.OnSelectionChanged();
	RebuildTreemap();
	if (IsActorGridMode() || IsCategoryListMode())
		UpdateStatus();
	if (IsActorGridMode())
		UpdatePager();
	RefreshActorStrip();   // 배우 정보(사진·별점·즐겨찾기)가 바뀌었을 수 있음
}

int CRuliManagerDlg::GetActorThumbIndex(int actorIdx)
{
	if (actorIdx < 0 || actorIdx >= static_cast<int>(m_lib.actors.size()))
		return 0;
	const ActorInfo& a = m_lib.actors[actorIdx];
	if (a.photo.IsEmpty())
		return 0;

	CString key = L"actor:" + a.name + L"|" + a.photo;
	key.MakeLower();
	auto it = m_thumbIndex.find(key);
	if (it != m_thumbIndex.end())
		return it->second;

	int image = 0;
	CImage src;
	if (::PathFileExistsW(a.photo) && LoadImageFileSfw(src, a.photo))   // SFW: 모자이크
		image = AddThumbnail(&src, nullptr);
	m_thumbIndex[key] = image;
	return image;
}

void CRuliManagerDlg::SelectActorRow(int row)
{
	if (row < 0 || row >= static_cast<int>(m_actorRows.size()))
		return;
	m_actorSel = row;
	m_actorSelName = m_lib.actors[m_actorRows[row]].name;
	m_actorGrid.OnSelectionChanged();
	if (m_treemap.GetSafeHwnd())
		m_treemap.SetSelectedTag(row);
	ShowActorInfo(m_actorRows[row]);
}

void CRuliManagerDlg::ShowActorInfo(int actorIdx)
{
	// 오른쪽 패널: 배우 사진과 정보 (동영상 편집 칸은 비활성)
	SetNameRich(false);
	CommitDetails();
	ShowDetails(-1);
	const bool validActor = (actorIdx >= 0 && actorIdx < static_cast<int>(m_lib.actors.size()));
	m_actorInfoIdx = validActor ? actorIdx : -1;
	UpdateActorStudioStrip(m_actorInfoIdx);   // 하단 제작사 / 레이블 카드
	if (!validActor)
	{
		m_staticName.SetWindowText(L"(선택된 배우 없음)");
		UpdateActorPanel(nullptr, 0);
		m_preview.SetPlaceholder(L"배우를 선택하세요.");
		return;
	}

	const ActorInfo& a = m_lib.actors[actorIdx];
	CString key = a.name;
	key.MakeLower();
	auto it = m_actorCounts.find(key);
	const int count = (it != m_actorCounts.end()) ? it->second : 0;

	const int age = CVideoLibrary::CalcAge(a.birth);
	CString title = L"배우: " + a.name;
	if (age >= 0)
	{
		CString t;
		t.Format(L"  (만 %d세)", age);
		title += t;
	}
	m_staticName.SetWindowText(title);
	UpdateActorPanel(&a, count);   // 오른쪽 아래 배우 상세

	// 생년월일 · 국적 · 키 · 데뷔일 · 출연 수 · 별칭
	std::vector<CString> parts;
	if (!a.birth.IsEmpty())       parts.push_back(L"생년월일 " + a.birth);
	if (!a.gender.IsEmpty())      parts.push_back(a.gender);
	if (!a.nationality.IsEmpty()) parts.push_back(a.nationality);
	if (!a.height.IsEmpty())      parts.push_back(a.height + L"cm");
	if (!a.debut.IsEmpty())       parts.push_back(L"데뷔 " + a.debut);
	if (!a.retire.IsEmpty())      parts.push_back(L"은퇴 " + a.retire);
	CString cnt;
	cnt.Format(L"출연 %d편", count);
	parts.push_back(cnt);
	if (!a.aliases.IsEmpty())     parts.push_back(L"별칭: " + a.aliases);
	CString info;
	for (const CString& p : parts)
	{
		if (!info.IsEmpty()) info += L" · ";
		info += p;
	}
	m_staticImage.SetWindowText(info);

	m_preview.Clear();
	if (a.photo.IsEmpty() || !::PathFileExistsW(a.photo) || !m_preview.SetImageFile(a.photo, true))
	{
		// 사진 없음: 배우 카드와 같은 기본 이미지 (성별 실루엣, 가운데 3:4 칸)
		const ActorInfo copy = a;
		m_preview.SetDefaultImage([copy](CDC* dc, const CRect& rc)
		{
			int h = rc.Height(), w = h * 3 / 4;
			if (w > rc.Width()) { w = rc.Width(); h = w * 4 / 3; }
			const CRect box(rc.left + (rc.Width() - w) / 2, rc.top + (rc.Height() - h) / 2,
				rc.left + (rc.Width() - w) / 2 + w, rc.top + (rc.Height() - h) / 2 + h);
			dc->FillSolidRect(box, kThumbBackColor);
			DrawDefaultActorImage(dc, box, copy);
		});
	}
}

void CRuliManagerDlg::DrillIntoActor(int row)
{
	if (row < 0 || row >= static_cast<int>(m_actorRows.size()))
		return;
	const ActorInfo& a = m_lib.actors[m_actorRows[row]];

	CommitDetails();
	m_actorSel = row;
	m_actorSelName = a.name;
	m_drill = a.name;
	m_pageDrill = 0;   // 출연작은 첫 페이지부터
	EnterActorDrillFilters();   // 출연작 화면의 정렬 · 별점 · 페이지당 개수 (따로 저장한 값)
	// 확대 단계도 출연작 화면 값으로 (배우 격자와 따로)
	m_zoomSlider.SetPos(m_zoom[ZoomSlot()]);
	SetupActorCards();
	m_catKind = CAT_VALUE;
	m_catValue = a.name;

	// 배우 검색어는 보관하고, 출연작은 검색어 없이 전부 표시
	m_editSearch.GetWindowText(m_savedSearch);
	m_editSearch.SetWindowText(L"");   // EN_CHANGE → ApplyFilter

	CString key = a.name;
	key.MakeLower();
	auto it = m_actorCounts.find(key);
	CString title;
	title.Format(L"배우: %s  (출연 %d편)", static_cast<LPCWSTR>(a.name),
		it != m_actorCounts.end() ? it->second : 0);
	SetDlgItemText(IDC_STATIC_ACTOR_TITLE, title);
	SetDlgItemText(IDC_BTN_ACTOR_BACK, L"◀ 배우 목록");
	m_drillActorIdx = m_actorRows[row];
	UpdateDrillHeader();   // 위쪽 배우 사진 + 정보

	ShowDetails(-1);
	UpdateLeftPane();
	ApplyFilter();
	m_grid.ScrollToTop();   // 카드로 들어가면 출연작은 맨 위부터
	m_grid.SetFocus();
}

void CRuliManagerDlg::EnterActorDrillFilters(int ctx)
{
	if (m_drillFilters)
		return;
	m_drillCtx = (ctx == 2) ? 2 : 1;
	// 영상 탭 값 보관
	m_mainSortCol = m_sortColumn;
	m_mainSortAsc = m_sortAsc;
	m_mainRating = (std::max)(0, m_comboFilter.GetCurSel());
	// 출연작 화면 값 (처음이면 영상 탭 값으로 시작)
	CWinApp* app = AfxGetApp();
	int col = app->GetProfileInt(L"Settings", DrillKey(L"SortColumn"), m_sortColumn);
	if (col < COL_NAME || col > SORT_LAST)
		col = m_sortColumn;
	m_sortColumn = col;
	m_sortAsc = app->GetProfileInt(L"Settings", DrillKey(L"SortAsc"), m_sortAsc ? 1 : 0) != 0;
	int rating = app->GetProfileInt(L"Settings", DrillKey(L"Rating"), 0);
	if (rating < 0 || rating >= m_comboFilter.GetCount())
		rating = 0;
	m_comboFilter.SetCurSel(rating);
	m_drillFilters = true;
	ApplyTabPageSize();
	UpdateSortArrows();
	UpdateSortUI();
}

void CRuliManagerDlg::LeaveActorDrillFilters()
{
	if (!m_drillFilters)
		return;
	// 출연작 화면 값 저장 (정렬 · 페이지당 개수는 바꿀 때 이미 저장)
	AfxGetApp()->WriteProfileInt(L"Settings", DrillKey(L"Rating"), (std::max)(0, m_comboFilter.GetCurSel()));
	m_drillFilters = false;
	// 영상 탭 값 되돌림
	m_sortColumn = m_mainSortCol;
	m_sortAsc = m_mainSortAsc;
	m_comboFilter.SetCurSel(m_mainRating);
	ApplyTabPageSize();
	UpdateSortArrows();
	UpdateSortUI();
}

void CRuliManagerDlg::UpdateDrillHeader()
{
	if (!m_drillPanel.GetSafeHwnd())
		return;
	const bool valid = (m_drillActorIdx >= 0 && m_drillActorIdx < static_cast<int>(m_lib.actors.size()));
	if (!valid)
	{
		m_drillPanel.SetActor(nullptr, 0);
		m_drillPhoto.Clear();
		return;
	}
	const ActorInfo& a = m_lib.actors[m_drillActorIdx];
	CString key = a.name;
	key.MakeLower();
	auto it = m_actorCounts.find(key);
	const int count = (it != m_actorCounts.end()) ? it->second : 0;
	m_drillPanel.SetActor(&a, count, m_lib.FindCodeOnDate(a.name, a.debut));
	if (a.photo.IsEmpty() || !::PathFileExistsW(a.photo) || !m_drillPhoto.SetImageFile(a.photo, true))
	{
		// 사진 없음: 배우 카드와 같은 기본 이미지 (성별 실루엣)
		const ActorInfo copy = a;
		m_drillPhoto.SetDefaultImage([copy](CDC* dc, const CRect& rc)
		{
			dc->FillSolidRect(rc, kThumbBackColor);
			DrawDefaultActorImage(dc, rc, copy);
		});
	}
}

void CRuliManagerDlg::DrillIntoCategory(int row)
{
	if (row < 0 || row >= static_cast<int>(m_catRows.size()))
		return;
	const CatRow r = m_catRows[row];   // 복사 (목록이 다시 만들어질 수 있음)
	const CString label = m_listCat.GetItemText(row, 0);
	const CString count = m_listCat.GetItemText(row, 1);
	const CString kindName = (m_mode == MODE_STUDIO) ? L"제작사" : (m_mode == MODE_COLLECTION ? L"컬렉션" : L"태그");
	const CString itemKind = (r.kind == CAT_LABEL) ? CString(L"레이블") : kindName;

	CommitDetails();
	m_catSelKind = r.kind;
	m_catSelValue = r.value;
	m_drill = label;
	m_pageDrill = 0;   // 영상 목록은 첫 페이지부터
	if (m_mode == MODE_STUDIO)
	{
		// 제작사 영상 화면: 정렬 · 별점 · 페이지당 개수 · 확대 단계는 따로 저장한 값
		EnterActorDrillFilters(2);
		m_zoomSlider.SetPos(m_zoom[ZoomSlot()]);
		SetupActorCards();
	}
	else
		SetupActorCards();   // 태그 · 컬렉션 영상 화면: 영상 격자 칸 크기 ([세로] 격자면 세로 카드)
	m_catKind = r.kind;
	m_catValue = r.value;
	m_namedActors = false;   // 제작사 영상 화면: 배우 카드 띠는 고른 영상의 출연 배우 (목록으로 돌아가면 ShowNamedInfo 가 다시 정함)

	// 목록 검색어는 보관하고, 영상은 검색어 없이 전부 표시
	m_editSearch.GetWindowText(m_savedSearch);
	m_editSearch.SetWindowText(L"");   // EN_CHANGE → ApplyFilter

	SetDlgItemText(IDC_STATIC_ACTOR_TITLE, itemKind + L": " + label + L"  (영상 " + count + L"편)");
	SetDlgItemText(IDC_BTN_ACTOR_BACK, L"◀ " + kindName + L" 목록");
	m_drillCount = count;
	UpdateCollectionHeader();   // 컬렉션 · 제작사 · 레이블: 위쪽 커버(로고) · 제목 · 메모

	ShowDetails(-1);
	UpdateLeftPane();
	ApplyFilter();
	m_grid.ScrollToTop();   // 카드로 들어가면 영상 목록은 맨 위부터
	m_grid.SetFocus();
}

void CRuliManagerDlg::ShowNamedInfo(int row)
{
	// 오른쪽 패널: 스튜디오/태그 이미지와 정보 (동영상 편집 칸은 비활성)
	CommitDetails();
	ShowDetails(-1);
	const CString kindName = (m_mode == MODE_STUDIO) ? L"제작사" : (m_mode == MODE_COLLECTION ? L"컬렉션" : L"태그");
	// 제작사 / 레이블을 고르면 메모 칸
	{
		int mk = 0;
		CString mn;
		if (m_mode == MODE_STUDIO && row >= 0 && row < static_cast<int>(m_catRows.size()))
		{
			if (m_catRows[row].kind == CAT_VALUE)      { mk = 1; mn = m_catRows[row].value; }
			else if (m_catRows[row].kind == CAT_LABEL) { mk = 2; mn = m_catRows[row].value; }
		}
		SetNamedMemoTarget(mk, mn);
		// 링크 줄 (제작사 / 레이블 링크)
		std::vector<CString> urls;
		if (mk == 1)
		{
			const int si = m_lib.FindNamed(LIST_STUDIO, mn);
			if (si >= 0) urls = CVideoLibrary::SplitUrls(m_lib.studios[si].urls);
		}
		else if (mk == 2)
		{
			const int li = m_lib.FindLabel(mn);
			if (li >= 0) urls = CVideoLibrary::SplitUrls(m_lib.labelInfos[li].urls);
		}
		const bool hadLinks = m_namedLinks.HasUrls();
		m_namedLinks.SetUrls(urls);
		if (hadLinks != m_namedLinks.HasUrls() && m_layoutReady)
		{
			CRect client;
			GetClientRect(&client);
			LayoutControls(client.Width(), client.Height());
		}
	}
	// 제작사를 고르면 하위 레이블 카드, 레이블을 고르면 상위 제작사 카드 (그 외는 숨김)
	{
		const bool valid = (m_mode == MODE_STUDIO && row >= 0 && row < static_cast<int>(m_catRows.size()));
		UpdateLabelStrip((valid && m_catRows[row].kind == CAT_VALUE) ? m_catRows[row].value : CString(),
			(valid && m_catRows[row].kind == CAT_LABEL) ? m_catRows[row].value : CString());
		// 제작사 · 레이블: 맨 아래 출연 배우 카드 (하위 레이블이 있는 제작사도 레이블 카드 아래에 표시)
		int ak = 0;
		if (valid && m_catRows[row].kind == CAT_LABEL)
			ak = 2;
		else if (valid && m_catRows[row].kind == CAT_VALUE)
			ak = 1;
		UpdateNamedActorStrip(ak, ak ? m_catRows[row].value : CString());
	}
	if (row < 0 || row >= static_cast<int>(m_catRows.size()))
	{
		m_staticName.SetWindowText(L"(선택된 " + kindName + L" 없음)");
		m_preview.SetPlaceholder(kindName + L"을(를) 선택하세요.\n더블클릭하면 해당 영상 목록이 표시됩니다.");
		return;
	}

	const CatRow& r = m_catRows[row];
	const CString count = m_listCat.GetItemText(row, 1);
	if (r.kind == CAT_LABEL)
	{
		// 레이블: "레이블: 이름  (서브이름)" + 상위 제작사 · 메모, 레이블 이미지
		const int li = m_lib.FindLabel(r.value);
		CString subName, info = L"영상 " + count + L"편", image;
		if (li >= 0)
		{
			const NamedInfo& lb = m_lib.labelInfos[li];
			image = lb.image;
			for (const CString& sub : CVideoLibrary::SplitLines(lb.subName))
			{
				if (!subName.IsEmpty()) subName += L" / ";
				subName += sub;
			}
			// 상위 제작사 · 메모는 정보 줄에 표시하지 않음 (메모는 아래 메모 칸에)
		}
		m_staticName.SetWindowText(L"레이블: " + r.value + (subName.IsEmpty() ? CString() : L"  (" + subName + L")"));
		m_nameRichPrefix = L"레이블: ";
		m_nameRichName = r.value;
		m_nameRichSub = subName;
		SetNameRich(true);
		m_staticImage.SetWindowText(info);
		m_preview.Clear();
		if (!image.IsEmpty() && ::PathFileExistsW(image))
			m_preview.SetImageFile(image);
		else
			m_preview.SetPlaceholder(L"이미지 없음\n\n[목록 관리]에서 이미지를 지정할 수 있습니다.");
		return;
	}
	if (r.kind != CAT_VALUE)
	{
		m_staticName.SetWindowText(m_listCat.GetItemText(row, 0));
		m_staticImage.SetWindowText(L"영상 " + count + L"편");
		m_preview.SetPlaceholder(L"더블클릭하면 해당 영상 목록이 표시됩니다.");
		return;
	}

	CString info = L"영상 " + count + L"편";
	const int idx = m_lib.FindNamed(NamedKind(), r.value);
	CString image;
	CString title = kindName + L": " + r.value;
	CString subName;
	if (idx >= 0)
	{
		const NamedInfo& n = m_lib.NamedList(NamedKind())[idx];
		image = n.image;
		if (NamedKind() == LIST_STUDIO)
		{
			for (const CString& sub : CVideoLibrary::SplitLines(n.subName))   // 표시: "에스원 / S1"
			{
				if (!subName.IsEmpty()) subName += L" / ";
				subName += sub;
			}
		}
		if (NamedKind() == LIST_STUDIO && !m_lib.LabelsOf(n.name).empty() && m_mode != MODE_STUDIO)   // 제작사 탭은 아래 레이블 카드로 대신
		{
			// 레이블: 이름(영상 수) / ...   예) 레이블: S1(120) / S1 NO.1 STYLE(8)
			CString lbText;
			for (const CString& l : m_lib.LabelNamesOf(n.name))
			{
				int cnt = 0;
				for (const VideoItem& v : m_lib.items)
					if (v.label.CompareNoCase(l) == 0 && v.studio.CompareNoCase(n.name) == 0)
						++cnt;
				CString one;
				one.Format(L"%s(%d)", static_cast<LPCWSTR>(l), cnt);
				if (!lbText.IsEmpty()) lbText += L" / ";
				lbText += one;
			}
			info += L" · 레이블: " + lbText;
		}
		if (NamedKind() == LIST_STUDIO)
		{
			// 상위 제작사 (가장 위부터 "A › B") · 바로 아래 하위 제작사
			const std::vector<CString> chain = m_lib.StudioParentChain(n.name);
			if (!chain.empty())
			{
				CString s;
				for (size_t k = chain.size(); k-- > 0;)
				{
					s += chain[k];
					if (k > 0)
						s += L" › ";
				}
				info += L" · 상위: " + s;
			}
			const std::vector<int> kids = m_lib.ChildStudiosOf(n.name);
			if (!kids.empty())
			{
				CString s;
				for (int k : kids)
				{
					if (!s.IsEmpty())
						s += L", ";
					s += m_lib.studios[k].name;
				}
				info += L" · 하위 제작사: " + s;
			}
		}
		if (NamedKind() == LIST_STUDIO && !n.memo.IsEmpty() && m_mode != MODE_STUDIO)   // 태그는 메모 없음, 제작사 탭은 아래 메모 칸에
		{
			CString memo = n.memo;
			memo.Replace(L"\r\n", L" ");
			memo.Replace(L'\n', L' ');
			info += L" · " + memo;
		}
	}
	m_staticName.SetWindowText(title + (subName.IsEmpty() ? CString() : L"  (" + subName + L")"));
	if (m_mode == MODE_STUDIO)
	{
		// 스튜디오: 이름은 굵게, 서브이름은 회색  예) 스튜디오: S1 NO.1 STYLE  (에스원, S1)
		m_nameRichPrefix = kindName + L": ";
		m_nameRichName = r.value;
		m_nameRichSub = subName;
		SetNameRich(true);
	}
	m_staticImage.SetWindowText(info);

	m_preview.Clear();
	if (m_mode == MODE_TAG)
		m_preview.SetPlaceholder(L"# " + r.value + L"\n\n영상 " + count + L"편\n더블클릭하면 해당 영상 목록이 표시됩니다.");
	else if (!image.IsEmpty() && ::PathFileExistsW(image))
		m_preview.SetImageFile(image);
	else
		m_preview.SetPlaceholder(L"이미지 없음\n\n[목록 관리]에서 이미지를 지정할 수 있습니다.");
}

void CRuliManagerDlg::BackToList()
{
	CommitDetails();
	m_drill.Empty();
	m_drillActorIdx = -1;
	LeaveActorDrillFilters();   // 영상 탭 정렬 · 별점 · 페이지당 개수로 되돌림
	if (m_mode == MODE_ACTOR || m_mode == MODE_STUDIO)
	{
		// 확대 단계: 배우 격자 / 제작사 목록 값으로
		m_zoomSlider.SetPos(m_zoom[ZoomSlot()]);
		SetupActorCards();
	}
	m_catKind = CAT_ALL;
	m_catValue.Empty();

	ShowDetails(-1);
	UpdateLeftPane();
	m_editSearch.SetWindowText(m_savedSearch);   // EN_CHANGE → 목록 갱신
	RebuildCategories();                          // 배우: 배우 격자 / 스튜디오·태그: 목록
	ApplyFilter();
	if (m_mode == MODE_ACTOR)
	{
		m_actorGrid.SetFocus();
		ShowActorInfo(SelectedActorIndex());
	}
	else
	{
		if (CatGridActive()) m_catGrid.SetFocus(); else m_listCat.SetFocus();
		ShowNamedInfo(m_listCat.GetNextItem(-1, LVNI_SELECTED));
	}
}

void CRuliManagerDlg::OnBnClickedActorBack()
{
	BackToList();
}

void CRuliManagerDlg::OnActorShowVideos()
{
	if (IsActorGridMode())
	{
		if (m_actorSel >= 0)
			DrillIntoActor(m_actorSel);
	}
	else if (IsCategoryListMode())
	{
		DrillIntoCategory(m_listCat.GetNextItem(-1, LVNI_SELECTED));
	}
}

void CRuliManagerDlg::OnActorEdit()
{
	if (m_mode == MODE_ACTOR)
	{
		const int idx = SelectedActorIndex();
		OpenActorManager(idx >= 0 ? m_lib.actors[idx].name : CString());
	}
	else if (m_mode == MODE_STUDIO)
	{
		OnManageStudios();
	}
	else if (m_mode == MODE_TAG)
	{
		OnManageTags();
	}
	else if (m_mode == MODE_COLLECTION)
	{
		RenameSelectedCollection();   // 컬렉션: F2 = 편집 (제목 · 커버 · 메모)
	}
}

// --- 배우 격자 데이터 제공 ---

bool CRuliManagerDlg::ActorGridOwner::GridDrawCard(CDC* dc, int row, const CRect& card, bool selected, bool focused)
{
	dlg->DrawActorCard(dc, row, card, selected, focused);
	return true;
}

bool CRuliManagerDlg::ActorGridOwner::GridClick(int row, const CRect& card, CPoint pt)
{
	if (row < 0 || row >= static_cast<int>(dlg->m_actorRows.size()))
		return false;
	const int part = GridHitPart(row, card, pt);
	if (part == 2)
	{
		dlg->ToggleActorCheck(dlg->m_actorRows[row]);
		return true;
	}
	if (part != 1)
		return false;
	dlg->ToggleActorFavorite(dlg->m_actorRows[row]);
	return true;
}

bool CRuliManagerDlg::ActorGridOwner::GridDblClick(int row, const CRect& card, CPoint pt)
{
	// 체크 칸을 빠르게 두 번 누름: 두 번째 클릭도 체크 전환 (출연작 화면으로 들어가지 않음)
	if (row < 0 || row >= static_cast<int>(dlg->m_actorRows.size()) || GridHitPart(row, card, pt) != 2)
		return false;
	dlg->ToggleActorCheck(dlg->m_actorRows[row]);
	return true;
}

bool CRuliManagerDlg::IsActorChecked(int actorIdx) const
{
	if (actorIdx < 0 || actorIdx >= static_cast<int>(m_lib.actors.size()) || m_actorChecked.empty())
		return false;
	CString key = m_lib.actors[actorIdx].name;
	key.MakeLower();
	return m_actorChecked.count(key) > 0;
}

void CRuliManagerDlg::ToggleActorCheck(int actorIdx)
{
	if (actorIdx < 0 || actorIdx >= static_cast<int>(m_lib.actors.size()))
		return;
	CString key = m_lib.actors[actorIdx].name;
	key.MakeLower();
	if (!m_actorChecked.erase(key))
		m_actorChecked.insert(key);
	for (size_t row = 0; row < m_actorRows.size(); ++row)
	{
		CRect r;
		if (m_actorRows[row] == actorIdx && m_actorGrid.GetTileRect(static_cast<int>(row), r))
			m_actorGrid.InvalidateRect(&r, FALSE);
	}
}

std::vector<int> CRuliManagerDlg::CheckedActorIndices() const
{
	std::vector<int> out;
	for (size_t i = 0; i < m_lib.actors.size() && !m_actorChecked.empty(); ++i)
		if (IsActorChecked(static_cast<int>(i)))
			out.push_back(static_cast<int>(i));
	return out;
}

void CRuliManagerDlg::ToggleActorFavorite(int actorIdx)
{
	if (actorIdx < 0 || actorIdx >= static_cast<int>(m_lib.actors.size()))
		return;
	ActorInfo& a = m_lib.actors[actorIdx];
	a.favorite = !a.favorite;
	m_lib.Save();
	m_actorSelName = a.name;
	RebuildActorGrid();          // 즐겨찾기가 앞으로 오도록 다시 정렬 (선택은 이름으로 유지)
	ShowActorInfo(actorIdx);     // 오른쪽 패널 하트
}

void CRuliManagerDlg::UpdateActorPanel(const ActorInfo* a, int count)
{
	if (!m_actorPanel.GetSafeHwnd())
		return;
	const int before = m_actorPanel.CalcHeight(0);   // 0 = 패널의 지금 폭 기준
	// 데뷔일과 같은 날 발매된 출연작이 있으면 그 품번을 데뷔일 오른쪽에
	const CString debutCode = a ? m_lib.FindCodeOnDate(a->name, a->debut) : CString();
	m_actorPanel.SetActor(a, count, debutCode);
	// 줄 수가 바뀌어 필요한 높이가 달라지면 미리보기/패널 배치 다시
	if (m_layoutReady && m_actorPanel.CalcHeight(0) != before)
	{
		CRect client;
		GetClientRect(&client);
		LayoutControls(client.Width(), client.Height());   // 옮긴 영역은 LayoutControls 가 다시 그림
	}
}

int CRuliManagerDlg::ActorGridOwner::GridHitPart(int /*row*/, const CRect& card, CPoint pt)
{
	CRect heart = dlg->ActorHeartRect(card);
	heart.InflateRect(3, 3);   // 조금 넉넉하게
	if (heart.PtInRect(pt))
		return 1;
	CRect check = dlg->ActorCheckRect(card);
	check.InflateRect(3, 3);
	return check.PtInRect(pt) ? 2 : 0;
}

CRect CRuliManagerDlg::ActorCheckRect(const CRect& card) const
{
	const int size = m_checkSize;   // 고정 크기 (예전: 카드 폭의 12% → 탭 · 확대 단계마다 달랐음)
	const int pad = (std::max)(4, m_cardPad * 2 / 3);
	return CRect(card.left + pad, card.top + pad, card.left + pad + size, card.top + pad + size);
}

CRect CRuliManagerDlg::ActorHeartRect(const CRect& card) const
{
	const int size = (std::max)(16, card.Width() * 15 / 100);
	const int pad = (std::max)(4, m_cardPad * 2 / 3);
	return CRect(card.right - pad - size, card.top + pad, card.right - pad, card.top + pad + size);
}

// ---------------------------------------------------------------------------
// 배우 카드

namespace
{
	const COLORREF& kCardBack  = TCOL(TC_CARD);   // 카드 배경
	const COLORREF& kCardImage = TCOL(TC_THUMB);   // 사진 없을 때
	const COLORREF& kCardLine  = TCOL(TC_COMBO);   // 구분선
	const COLORREF& kCardSub   = TCOL(TC_SUB);   // 나이 / 별칭
	const COLORREF& kCardIcon  = TCOL(TC_LIGHT);   // ▶ / 필름 아이콘
	const COLORREF kFemale     = RGB(0xF5, 0x49, 0x8B);   // ♀
	const COLORREF kMale       = RGB(0x48, 0xAF, 0xF0);   // ♂
	const COLORREF kTransF     = RGB(0xF5, 0xA9, 0xB8);   // ⚧ 트랜스젠더 여성 (트랜스 깃발 분홍)
	const COLORREF kTransM     = RGB(0x5B, 0xCE, 0xFA);   // ⚧ 트랜스젠더 남성 (트랜스 깃발 하늘색)
	const COLORREF kIntersex   = RGB(0xFF, 0xD8, 0x00);   // ⚥ 인터섹스 (인터섹스 깃발 노랑)
	const COLORREF kNonBinary  = RGB(0xB5, 0x7E, 0xDC);   // ⚲ 논바이너리 (논바이너리 깃발 보라)

	// 성별 기호 + 색 (미지정이면 false). 기호는 TextFB 로 그려서 글꼴에 없으면 Segoe UI Symbol 등으로 대체
	bool GenderSymbol(const CString& g, CString& sym, COLORREF& col)
	{
		if (g == L"여성")                { sym = L"\x2640"; col = kFemale;    return true; }   // ♀
		if (g == L"남성")                { sym = L"\x2642"; col = kMale;      return true; }   // ♂
		if (g == L"트랜스젠더 여성")     { sym = L"\x26A7"; col = kTransF;    return true; }   // ⚧
		if (g == L"트랜스젠더 남성")     { sym = L"\x26A7"; col = kTransM;    return true; }   // ⚧
		if (g == L"인터섹스")            { sym = L"\x26A5"; col = kIntersex;  return true; }   // ⚥
		if (g == L"논바이너리")          { sym = L"\x26B2"; col = kNonBinary; return true; }   // ⚲
		return false;
	}
}

bool CRuliManagerDlg::ActorChipGender(const CString& tag, CString& sym, COLORREF& col)
{
	const int idx = m_lib.FindActorByAnyName(tag);
	if (idx < 0 || idx >= static_cast<int>(m_lib.actors.size()))
		return false;
	return GenderSymbol(m_lib.actors[idx].gender, sym, col);
}

namespace
{

	// 폭 안에 들어가는 만큼 자르기 (공백이 있으면 공백에서 줄바꿈)
	int FitChars(CDC* dc, const CString& s, int start, int width, bool preferSpace)
	{
		int end = start;
		int lastSpace = -1;
		while (end < s.GetLength())
		{
			if (TextFB::Width(dc, s.Mid(start, end - start + 1)) > width)
				break;
			if (s[end] == L' ')
				lastSpace = end;
			++end;
		}
		if (end < s.GetLength() && preferSpace && lastSpace > start)
			end = lastSpace + 1;
		return (std::max)(end, start + 1);
	}

	void DrawPlayIcon(CDC* dc, int cx, int cy, int r, COLORREF col, COLORREF back)
	{
		(void)back;   // 벡터 아이콘: 재생 삼각형은 뚫려 있음
		VectorIcon::PlayCircle(dc, CRect(cx - r, cy - r, cx + r + 1, cy + r + 1), col);
	}

	// 사진 왼쪽 위 모서리에 대각선 리본 "별점: N" (빨간 띠, 흰 굵은 글씨)
	//  card: 카드 전체 (둥근 모서리 잘라내기용), img: 사진 영역
	void DrawRatingRibbon(CDC* dc, const CRect& card, const CRect& img, int radius, int rating, CFont* font)
	{
		if (rating <= 0 || !font)
			return;
		CString text;
		text = L"별점: " + CStarRatingCtrl::ScoreLabel(rating);   // 반 별 단위 → "3.5" (5점 만점) / "7/10" (10점 만점)

		LOGFONTW lf = {};
		font->GetLogFont(&lf);
		lf.lfWeight = FW_BOLD;
		lf.lfQuality = CLEARTYPE_QUALITY;

		CRgn clip;
		clip.CreateRoundRectRgn(card.left, card.top, card.right + 1, card.bottom + 1, radius, radius);
		dc->SelectClipRgn(&clip);
		dc->IntersectClipRect(img);
		{
			Gdiplus::Graphics g(dc->GetSafeHdc());
			g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
			g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
			Gdiplus::Font gf(dc->GetSafeHdc(), &lf);
			Gdiplus::StringFormat fmt;
			fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
			fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
			fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
			Gdiplus::RectF bound;
			g.MeasureString(text, -1, &gf, Gdiplus::PointF(0, 0), &fmt, &bound);
			const float bandH = bound.Height + 2.0f;          // 띠 두께
			// 글자가 모서리 삼각형 안에 들어가도록 띠 중심을 모서리에서 떨어뜨림
			const float c = (std::max)(bound.Width / 2.6f + bandH / 2.0f, bandH * 1.2f);
			g.TranslateTransform(img.left + c, img.top + c);
			g.RotateTransform(-45.0f);
			const float len = c * 4.0f + bound.Width;        // 넉넉히 (사진 밖은 잘림)
			// 별점별 띠 색: 1 #394B59 / 2 #63411E / 3 #A66321 / 4 #A82A2A / 5 #FF2F39
			static const COLORREF kRibbon[5] = {
				TCOL(TC_COMBO), RGB(0x63, 0x41, 0x1E), RGB(0xA6, 0x63, 0x21), RGB(0xA8, 0x2A, 0x2A), RGB(0xFF, 0x2F, 0x39)
			};
			const COLORREF rc = kRibbon[(std::min)((rating + 1) / 2, 5) - 1];   // 반 별은 올림한 별 수의 색
			Gdiplus::SolidBrush band(Gdiplus::Color(235, GetRValue(rc), GetGValue(rc), GetBValue(rc)));
			g.FillRectangle(&band, -len / 2, -bandH / 2, len, bandH);
			Gdiplus::SolidBrush white(Gdiplus::Color(255, 255, 255, 255));
			g.DrawString(text, -1, &gf, Gdiplus::RectF(-len / 2, -bandH / 2, len, bandH), &fmt, &white);
		}
		dc->SelectClipRgn(nullptr);
	}

	// 카드 체크 칸 (윈도우 11 모양): 체크 = 강조색 바탕 + 어두운 체크 표시, 빈 칸 = 반투명 어두운 바탕 + 흰 테두리
	void DrawCardCheck(CDC* dc, const CRect& area, bool checked)
	{
		Gdiplus::Graphics g(dc->GetSafeHdc());
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		const float x = static_cast<float>(area.left) + 0.5f, y = static_cast<float>(area.top) + 0.5f;
		const float w = static_cast<float>(area.Width()) - 1.0f, h = static_cast<float>(area.Height()) - 1.0f;
		const float r = (std::max)(2.0f, w * 0.22f);
		Gdiplus::GraphicsPath box;
		box.AddArc(x, y, r * 2, r * 2, 180, 90);
		box.AddArc(x + w - r * 2, y, r * 2, r * 2, 270, 90);
		box.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0, 90);
		box.AddArc(x, y + h - r * 2, r * 2, r * 2, 90, 90);
		box.CloseFigure();
		if (checked)
		{
			Gdiplus::SolidBrush fill(Gdiplus::Color(255, 0x4C, 0xC2, 0xFF));
			g.FillPath(&fill, &box);
			Gdiplus::Pen mark(Gdiplus::Color(255, 0x10, 0x10, 0x10), (std::max)(1.6f, w * 0.12f));
			mark.SetStartCap(Gdiplus::LineCapRound);
			mark.SetEndCap(Gdiplus::LineCapRound);
			mark.SetLineJoin(Gdiplus::LineJoinRound);
			const Gdiplus::PointF pts[3] = {
				Gdiplus::PointF(x + w * 0.24f, y + h * 0.52f),
				Gdiplus::PointF(x + w * 0.42f, y + h * 0.70f),
				Gdiplus::PointF(x + w * 0.76f, y + h * 0.32f) };
			g.DrawLines(&mark, pts, 3);
		}
		else
		{
			Gdiplus::SolidBrush fill(Gdiplus::Color(140, 0x20, 0x20, 0x20));
			g.FillPath(&fill, &box);
			Gdiplus::Pen border(Gdiplus::Color(230, 0xFF, 0xFF, 0xFF), (std::max)(1.2f, w * 0.07f));
			g.DrawPath(&border, &box);
		}
	}

	// 하트 (24x24 설계 좌표를 area 에 맞춤), alpha 로 반투명
	void DrawHeart(CDC* dc, const CRect& area, COLORREF col, BYTE alpha, bool shadow)
	{
		Gdiplus::Graphics g(dc->GetSafeHdc());
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		const float scale = (std::min)(area.Width(), area.Height()) / 24.0f;
		auto build = [](Gdiplus::GraphicsPath& p)
		{
			p.StartFigure();
			p.AddLine(12.0f, 21.35f, 10.55f, 20.03f);
			p.AddBezier(10.55f, 20.03f, 5.4f, 15.36f, 2.0f, 12.28f, 2.0f, 8.5f);
			p.AddBezier(2.0f, 8.5f, 2.0f, 5.42f, 4.42f, 3.0f, 7.5f, 3.0f);
			p.AddBezier(7.5f, 3.0f, 9.24f, 3.0f, 10.91f, 3.81f, 12.0f, 5.09f);
			p.AddBezier(12.0f, 5.09f, 13.09f, 3.81f, 14.76f, 3.0f, 16.5f, 3.0f);
			p.AddBezier(16.5f, 3.0f, 19.58f, 3.0f, 22.0f, 5.42f, 22.0f, 8.5f);
			p.AddBezier(22.0f, 8.5f, 22.0f, 12.28f, 18.6f, 15.36f, 13.45f, 20.04f);
			p.CloseFigure();
		};
		if (shadow)
		{
			// 밝은 사진 위에서도 보이도록 살짝 그림자
			Gdiplus::GraphicsPath sp;
			build(sp);
			Gdiplus::Matrix m;
			m.Translate(area.left + 1.0f, area.top + 1.5f);
			m.Scale(scale, scale);
			sp.Transform(&m);
			Gdiplus::SolidBrush sb(Gdiplus::Color(70, 0, 0, 0));
			g.FillPath(&sb, &sp);
		}
		Gdiplus::GraphicsPath path;
		build(path);
		Gdiplus::Matrix m;
		m.Translate(static_cast<Gdiplus::REAL>(area.left), static_cast<Gdiplus::REAL>(area.top));
		m.Scale(scale, scale);
		path.Transform(&m);
		Gdiplus::SolidBrush br(Gdiplus::Color(alpha, GetRValue(col), GetGValue(col), GetBValue(col)));
		g.FillPath(&br, &path);
	}

}

namespace
{
	// 꼬리표 아이콘 (왼쪽이 뾰족, 구멍 하나)
	void DrawTagIcon(CDC* dc, int cx, int cy, int r, COLORREF col, COLORREF back)
	{
		(void)back;
		VectorIcon::Tag(dc, CRect(cx - r - 1, cy - r - 1, cx + r + 2, cy + r + 2), col);
	}

	// 사람 아이콘 (머리 + 어깨)
	void DrawPersonIcon(CDC* dc, int cx, int cy, int r, COLORREF col)
	{
		VectorIcon::Person(dc, CRect(cx - r - 1, cy - r - 1, cx + r + 2, cy + r + 2), col);
	}

	// 사진이 없는 배우 카드의 기본 이미지
	//  - 성별 미지정: 흰 사람 (Font Awesome user) — 원본 SVG 처럼 사진 높이의 약 47%, 가운데
	//  - 여성 / 남성: 흰 실루엣 중 하나를 배우 이름으로 골라 표시 (배우마다 다르지만 늘 같은 이미지)
	void DrawDefaultActorImage(CDC* dc, const CRect& img, const ActorInfo& a)
	{
		const CString& gender = a.gender;
		const bool female = CVideoLibrary::IsFemaleLike(gender), male = CVideoLibrary::IsMaleLike(gender);   // 트랜스젠더 여성 / 남성도 같은 실루엣
		const int count = female ? VectorIcon::FemaleCount() : (male ? VectorIcon::MaleCount() : 0);
		if (count > 0)
		{
			// 이름 해시(FNV-1a)로 고름 → 무작위처럼 섞이지만 다시 그려도 바뀌지 않음
			UINT h = 2166136261u;
			for (int i = 0; i < a.name.GetLength(); ++i)
				h = (h ^ static_cast<UINT>(a.name[i])) * 16777619u;
			const int index = static_cast<int>(h % static_cast<UINT>(count));
			if (female)
				VectorIcon::Female(dc, img, RGB(255, 255, 255), index, 0.94);
			else
				VectorIcon::Male(dc, img, RGB(255, 255, 255), index, 0.94);
			return;
		}
		if (!female && !male)   // 미지정 · 인터섹스 · 논바이너리: 흰 사람
		{
			const int side = (std::max)(1, (std::min)(img.Width(), img.Height()));
			const double fill = (std::min)(1.0, img.Height() * (512.0 / 1080.0) / (side * 22.0 / 24.0));
			VectorIcon::User(dc, img, RGB(255, 255, 255), fill);
			return;
		}
		CRect pr = img;
		pr.top += img.Height() / 6;   // 사람 모양이 아래쪽까지 차도록
		VectorIcon::Person(dc, pr, TCOL(TC_BORDER), 0.8);
	}
}

void CRuliManagerDlg::SetupActorCards()
{
	// 글꼴: 이름은 조금 크게 굵게, 성별 기호는 기호 글꼴
	LOGFONT lf = {};
	if (CFont* f = GetFont())
		f->GetLogFont(&lf);
	LOGFONT nameLf = lf;
	nameLf.lfHeight = lf.lfHeight * 115 / 100;
	nameLf.lfWeight = FW_BOLD;
	m_cardNameFont.DeleteObject();
	m_cardNameFont.CreateFontIndirect(&nameLf);
	LOGFONT symLf = lf;
	symLf.lfHeight = lf.lfHeight * 150 / 100;
	symLf.lfWeight = FW_BOLD;
	symLf.lfCharSet = DEFAULT_CHARSET;
	wcscpy_s(symLf.lfFaceName, L"Segoe UI Symbol");
	m_cardSymbolFont.DeleteObject();
	m_cardSymbolFont.CreateFontIndirect(&symLf);
	LOGFONT actorSymLf = symLf;   // 배우 탭 카드 성별 기호: 2pt 크게
	actorSymLf.lfHeight = symLf.lfHeight + lf.lfHeight * 2 / 9;
	if (actorSymLf.lfHeight == symLf.lfHeight)
		actorSymLf.lfHeight += (symLf.lfHeight < 0) ? -2 : 2;
	m_actorSymbolFont.DeleteObject();
	m_actorSymbolFont.CreateFontIndirect(&actorSymLf);
	LOGFONT videoLf = nameLf;   // 영상 카드 제목: 카드 이름 글꼴보다 2pt 크게
	videoLf.lfHeight = nameLf.lfHeight + lf.lfHeight * 2 / 9;
	if (videoLf.lfHeight == nameLf.lfHeight)
		videoLf.lfHeight += (nameLf.lfHeight < 0) ? -2 : 2;
	m_videoTitleFont.DeleteObject();
	m_videoTitleFont.CreateFontIndirect(&videoLf);
	LOGFONT actorLf = nameLf;   // 배우 카드 이름(첫 줄): 카드 제목 글꼴보다 3pt 크게 (1pt + 2pt)
	actorLf.lfHeight = nameLf.lfHeight + lf.lfHeight * 3 / 9;
	if (actorLf.lfHeight == nameLf.lfHeight)
		actorLf.lfHeight += (nameLf.lfHeight < 0) ? -3 : 3;
	m_actorNameFont.DeleteObject();
	m_actorNameFont.CreateFontIndirect(&actorLf);
	LOGFONT subLf = lf;   // 기본(9pt)보다 1pt 크게 (영상 카드 발매일 · 메모 기준)
	subLf.lfHeight = lf.lfHeight * 10 / 9;
	if (subLf.lfHeight == lf.lfHeight)
		subLf.lfHeight += (lf.lfHeight < 0) ? -1 : 1;
	LOGFONT cardSubLf = lf;   // 배우 카드 괄호 안 이름(둘째·셋째 줄): 기본(9pt)보다 2pt 크게
	cardSubLf.lfHeight = lf.lfHeight * 11 / 9;
	if (cardSubLf.lfHeight == subLf.lfHeight)
		cardSubLf.lfHeight += (lf.lfHeight < 0) ? -1 : 1;
	m_cardSubFont.DeleteObject();
	m_cardSubFont.CreateFontIndirect(&cardSubLf);
	LOGFONT vsubLf = subLf;   // 영상 카드 발매일 · 메모: 1pt 큰 크기 + 굵게
	vsubLf.lfWeight = FW_BOLD;
	m_vcardSubFont.DeleteObject();
	m_vcardSubFont.CreateFontIndirect(&vsubLf);

	CClientDC dc(this);
	TEXTMETRIC tm = {};
	CFont* old = dc.SelectObject(&m_cardNameFont);
	dc.GetTextMetrics(&tm);
	m_cardLineB = tm.tmHeight + 1;
	dc.SelectObject(GetFont());
	dc.GetTextMetrics(&tm);
	m_cardLine = tm.tmHeight + 1;
	dc.SelectObject(&m_cardSubFont);
	dc.GetTextMetrics(&tm);
	m_cardSubLine = tm.tmHeight + 1;
	dc.SelectObject(&m_vcardSubFont);
	dc.GetTextMetrics(&tm);
	m_vcardSubLine = tm.tmHeight + 1;
	dc.SelectObject(&m_actorNameFont);
	dc.GetTextMetrics(&tm);
	m_actorLineB = tm.tmHeight + 1;
	dc.SelectObject(&m_videoTitleFont);
	dc.GetTextMetrics(&tm);
	m_videoLineB = tm.tmHeight + 1;
	m_vcardMemoGap = ::MulDiv(2, dc.GetDeviceCaps(LOGPIXELSY), 72);   // 2pt → 픽셀
	dc.SelectObject(old);

	m_cardPad = DX(5);
	m_checkSize = (std::max)(14, DY(10));   // 카드 체크 칸: 모든 탭 · 확대 단계에서 같은 크기 (기본 글꼴 높이의 약 1.25배, DPI 반영)
	const int oldCardW = m_cardW, oldVcardW = m_vcardW;   // 크기가 바뀔 때만 이미지 캐시를 비움
	const double zoom = ZoomFactor();   // 탭별 확대 단계
	m_cardW = static_cast<int>(DX(108) * zoom);
	m_cardImgH = m_cardW * 3 / 2;   // 세로 사진 2:3
	m_actorNameGap = (std::max)(0, m_cardPad - m_vcardMemoGap);   // 사진 ~ 이름 간격: 기본 여백보다 2pt 좁게
	m_cardH = m_cardImgH
		+ m_actorNameGap + (std::max)(m_actorLineB * 2, m_actorLineB + m_cardSubLine * 2)   // 성별 + 이름 (굵은 1줄 + 괄호 안 이름 2줄 자리)
		+ m_cardLine + m_cardPad / 2           // 나이
		+ 1                                    // 구분선
		+ m_cardPad + m_cardLine + m_cardPad;  // ▶ 출연 수 · 스튜디오 수
	m_actorGrid.SetFixedTile(m_cardW, m_cardH);

	// 영상 상세 정보의 출연 배우 카드 (확대 단계와 상관없이 고정 크기, 사진 3:4)
	m_stripCardW = DX(78);   // 1.5배 (예전 DX(52))
	m_stripCardH = m_stripCardW * 4 / 3 + m_cardPad / 2 + m_cardLineB + m_cardLine * 2 + m_cardPad / 2   // 글자 3줄 (굵은 이름 + 2줄)
		+ 1 + m_cardPad / 2 + m_cardLine + m_cardPad / 2;                          // 구분선 + 제작 당시 나이
	m_stripCardCurH = StripShowsAge() ? m_stripCardH : m_stripCardH - StripAgeH();
	if (m_actorStrip.GetSafeHwnd())
		m_actorStrip.SetCardSize(m_stripCardW, m_stripCardCurH, DX(4));
	if (m_cardW != oldCardW)
		m_actorPortraits.clear();

	// 영상 카드: 가로 이미지(16:9) + 제목 + 발매일 + 메모 3줄 + 구분선 + 태그/배우 수
	//  영상 화면(영상 탭 · 배우 / 제작사 / 태그 / 컬렉션 영상): 해상도별 표의 "가로 몇 개"로 폭을 정함 (상세 화면 접힘 기준 - 펼치면 그만큼 덜 들어감)
	m_vcardW = static_cast<int>(DX(146) * zoom);
	int wallW = 0;   // [월 모드] 칸 폭
	if (VideoViewSlot() >= 0 && m_grid.GetSafeHwnd())
	{
		int monW = 0;
		const int refW = VideoRefGridWidth(&monW);   // 처음 계산한 값 그대로
		const int step = (std::max)(0, (std::min)(3, m_zoom[ZoomSlot()]));
		const int pad = m_grid.BasePad();
		if (refW > DX(200))
		{
			m_vcardW = (std::max)(DX(80), refW / VideoColumns(step, false, monW) - pad * 2);   // [격자]: 칸 = 카드 + 여백 × 2
			wallW = (std::max)(DX(60), refW / VideoColumns(step, true, monW));                // [월 모드]: 여백 없음
		}
	}
	m_vcardImgH = m_vcardW * 9 / 16;
	m_vcardH = m_vcardImgH
		+ m_cardPad + m_videoLineB + DX(2)     // 제목 (2pt 큰 글꼴)
		+ m_vcardSubLine + m_vcardMemoGap       // 발매일 / ● 임시 (1pt 큰 굵은 글꼴) + 메모와의 간격 2pt
		+ m_vcardSubLine * 3 + m_cardPad        // 메모 3줄 (1pt 큰 굵은 글꼴)
		+ 1                                     // 구분선
		+ m_cardPad + m_cardLine + m_cardPad;   // 태그 수 · 배우 수
	// 영상 탭 [세로] 격자: 배우 카드 폭, 세로 3:4, 이미지만
	m_pcardW = m_cardW;
	m_pcardH = m_pcardW * 4 / 3;
	m_grid.SetTilePad(VideoWallActive() ? 0 : (VideoLabelActive() ? DX(2) : -1));   // [월 모드]: 칸끼리 딱 붙임, [레이블]: 줄 사이 좁게
	m_grid.SetFillWidth(VideoLabelActive());
	if (VideoLabelActive())
	{
		// [레이블]: 줄 = 격자 폭 전체, 높이 = 세로 커버(3:4, 확대 단계 반영) + 위아래 여백 / 묶음 머리줄 = 제목 줄 + 열 이름 줄
		m_lrowCoverW = (std::max)(DX(61), static_cast<int>(DX(88) * zoom * 0.9));   // 2배 (예전 DX(44)) 에서 10% 줄임
		{
			// 품번 칸: 품번 글꼴로 10글자 (예: ABCDE-1234) + 조금 여유
			CClientDC cdc(this);
			CFont* of = cdc.SelectObject(m_listCodeFont.GetSafeHandle() ? &m_listCodeFont : GetFont());
			m_lcodeW = cdc.GetTextExtent(L"ABCDE-1234").cx + DX(6);
			cdc.SelectObject(of);
		}
		m_lrowCoverH = m_lrowCoverW * 4 / 3;
		m_lrowH = m_lrowCoverH + DX(5) * 2;
		m_lgroupH = DX(8) + m_videoLineB + DX(2) + m_cardLine + DX(5);
		m_limgH = (std::max)(DX(36), static_cast<int>(DX(44) * zoom));   // 레이블 이미지 높이 (확대 단계 반영)
		m_grid.SetGroupHeaderHeight(m_lgroupH);
		m_grid.SetFixedTile(DX(300), m_lrowH);
	}
	else
		m_grid.SetGroupHeaderHeight(0);
	if (VideoLabelActive())
	{
		// 위에서 정함
	}
	else if (VideoPortraitActive())
		m_grid.SetFixedTile(m_pcardW, m_pcardH);
	else if (VideoWallActive())
	{
		// [월 모드]: 16:9 이미지만 - 해상도별 표의 "가로 몇 개" (표를 못 쓰면 [격자] 이미지 폭)
		const int ww = (wallW > 0) ? wallW : m_vcardW;
		m_grid.SetFixedTile(ww, ww * 9 / 16);
	}
	else
		m_grid.SetFixedTile(m_vcardW, m_vcardH);
	if (m_vcardW != oldVcardW)
		ClearVideoCovers();

	// 스튜디오 카드: 로고 영역(16:9, 안쪽 여백) + 이름 + 구분선 + 영상/배우 수
	// 태그 카드는 25% 작게, 제작사 · 레이블 카드는 30% 작게 (영상 카드 폭 기준 - 확대 단계마다 같은 비율), 컬렉션은 영상 카드 폭 (대표 영상 이미지)
	m_scardW = (m_mode == MODE_TAG) ? m_vcardW * 3 / 4 : (m_mode == MODE_STUDIO ? m_vcardW * 7 / 10 : m_vcardW);
	if (m_mode == MODE_TAG || m_mode == MODE_STUDIO)
	{
		// 제작사 · 태그 카드 폭: 최대 확대 단계 폭 기준으로 단계마다 70 / 79 / 89 / 100%
		//  (글자 줄 높이는 단계와 상관없이 그대로라, 영상 카드처럼 1.0 ~ 2.1배로 줄이면 최소 단계에서 정사각형에 가까워짐 → 작은 단계의 폭을 넓혀 직사각형 유지)
		static const double kScardStep[4] = { 0.70, 0.79, 0.89, 1.0 };
		const int step = (std::max)(0, (std::min)(3, m_zoom[ZoomSlot()]));
		const double frac = (m_mode == MODE_TAG) ? 0.75 : 0.70;
		m_scardW = static_cast<int>(DX(146) * 2.1 * frac * kScardStep[step]);
	}
	const int textH = m_cardPad + m_cardLineB + m_cardPad / 2          // 이름
		+ 1                                                             // 구분선
		+ m_cardPad + m_cardLine + m_cardPad;                           // ▶ 영상 수 · 배우 수
	m_scardLogoH = (m_scardW - m_cardPad * 2) * 9 / 16;                 // 로고 영역 (16:9)
	m_scardH = m_cardPad * 2 + m_scardLogoH + textH;
	// 제작사 · 레이블 카드: 글자 크기 · 여백은 확대 단계와 상관없이 그대로, 로고 칸은 늘 카드 폭의 16:9 (로고가 차지하는 비율이 단계마다 같음)
	//  (카드 세로 비율은 글자 줄 높이만큼 단계마다 조금 달라짐 - 글자 · 로고 · 카드 비율을 모두 고정할 수는 없음)
	if (m_scardNameFont.GetSafeHandle()) m_scardNameFont.DeleteObject();   // 비례 글꼴은 쓰지 않음 (DrawStudioCard 는 기본 글꼴)
	if (m_scardTextFont.GetSafeHandle()) m_scardTextFont.DeleteObject();
	if (m_mode == MODE_STUDIO || TagLikeMode())
		m_catGrid.SetFixedTile(m_scardW, m_scardH);
}

bool CRuliManagerDlg::CatGridOwner::GridClick(int row, const CRect& card, CPoint pt)
{
	const int part = GridHitPart(row, card, pt);
	if (part == 2)
	{
		dlg->ToggleCatCheck(row);   // 왼쪽 위 체크 칸 (제작사 · 태그 · 컬렉션 카드)
		return true;
	}
	if (part != 1)
		return false;
	if (dlg->m_mode == MODE_COLLECTION)
		dlg->ToggleCollectionFavorite(dlg->m_catRows[row].value);
	else
		dlg->ToggleTagFavorite(dlg->m_catRows[row].value);
	return true;
}

int CRuliManagerDlg::CatGridOwner::GridHitPart(int row, const CRect& card, CPoint pt)
{
	if (row < 0 || row >= static_cast<int>(dlg->m_catRows.size()))
		return 0;
	if (dlg->TagLikeMode())
	{
		CRect heart = dlg->ActorHeartRect(card);   // 배우 카드와 같은 자리/크기
		heart.InflateRect(3, 3);
		if (heart.PtInRect(pt))
			return 1;
	}
	if (dlg->CatChecksEnabled() && dlg->m_catRows[row].kind != CAT_ALL && dlg->m_catRows[row].kind != CAT_NONE)
	{
		CRect check = dlg->ActorCheckRect(card);   // 배우 카드와 같은 왼쪽 위 체크 칸
		check.InflateRect(3, 3);
		if (check.PtInRect(pt))
			return 2;
	}
	return 0;
}

bool CRuliManagerDlg::CatGridOwner::GridDblClick(int row, const CRect& card, CPoint pt)
{
	// 체크 칸을 빠르게 두 번 누름: 두 번째 클릭도 체크 전환 (영상 화면으로 들어가지 않음)
	if (GridHitPart(row, card, pt) != 2)
		return false;
	dlg->ToggleCatCheck(row);
	return true;
}

CString CRuliManagerDlg::CatCheckKey(int kind, const CString& value)
{
	CString key = value;
	key.MakeLower();
	CString out;
	out.Format(L"%d|%s", kind, static_cast<LPCWSTR>(key));
	return out;
}

bool CRuliManagerDlg::IsCatChecked(int kind, const CString& value) const
{
	if (m_mode < 0 || m_mode >= MODE_COUNT || m_catChecked[m_mode].empty())
		return false;
	return m_catChecked[m_mode].count(CatCheckKey(kind, value)) > 0;
}

void CRuliManagerDlg::ToggleCatCheck(int row)
{
	if (!CatChecksEnabled() || row < 0 || row >= static_cast<int>(m_catRows.size()))
		return;
	const CatRow& r = m_catRows[row];
	if (r.kind != CAT_VALUE && r.kind != CAT_LABEL)
		return;
	std::set<CString>& set = m_catChecked[m_mode];
	const CString key = CatCheckKey(r.kind, r.value);
	if (!set.erase(key))
		set.insert(key);
	CRect tile;
	if (m_catGrid.GetTileRect(row, tile))
		m_catGrid.InvalidateRect(&tile, FALSE);
}

std::vector<CRuliManagerDlg::CatRow> CRuliManagerDlg::CheckedCatRows() const
{
	std::vector<CatRow> out;
	if (m_mode < 0 || m_mode >= MODE_COUNT || m_catChecked[m_mode].empty())
		return out;
	std::set<CString> seen;
	for (const CatRow& r : m_catRows)
	{
		if ((r.kind != CAT_VALUE && r.kind != CAT_LABEL) || !IsCatChecked(r.kind, r.value))
			continue;
		if (seen.insert(CatCheckKey(r.kind, r.value)).second)
			out.push_back(r);
	}
	return out;
}

void CRuliManagerDlg::OnCatDeleteChecked()
{
	// 제작사 · 태그 · 컬렉션 탭: 체크한 항목을 모두 삭제 (영상 파일은 그대로)
	//  제작사: 영상의 제작사 · 레이블도 비우고 그 제작사의 레이블도 삭제 / 레이블: 영상의 레이블을 비움
	//  태그: 영상에서도 빠짐 / 컬렉션: 들어 있던 영상은 컬렉션에서만 빠짐
	if (!CatChecksEnabled())
		return;
	const std::vector<CatRow> rows = CheckedCatRows();
	if (rows.empty())
		return;
	const wchar_t* kindName = (m_mode == MODE_TAG) ? L"태그" : (m_mode == MODE_COLLECTION ? L"컬렉션" : L"제작사 · 레이블");
	int links = 0, labelsOfStudios = 0;
	if (m_mode == MODE_TAG)
	{
		const std::map<CString, int> counts = m_lib.CountNamed(LIST_TAG);
		for (const CatRow& r : rows)
		{
			CString key = r.value;
			key.MakeLower();
			auto it = counts.find(key);
			if (it != counts.end())
				links += it->second;
		}
	}
	else if (m_mode == MODE_COLLECTION)
	{
		for (const CatRow& r : rows)
			for (const VideoItem& v : m_lib.items)
				if (m_lib.InCollection(v, r.value))
					++links;
	}
	else
	{
		for (const CatRow& r : rows)
		{
			for (const VideoItem& v : m_lib.items)
			{
				CString s = (r.kind == CAT_LABEL) ? v.label : v.studio;
				s.Trim();
				if (!s.IsEmpty() && s.CompareNoCase(r.value) == 0)
					++links;
			}
			if (r.kind == CAT_VALUE)
				labelsOfStudios += static_cast<int>(m_lib.LabelsOf(r.value).size());
		}
	}
	CString msg;
	msg.Format(L"체크한 %s %d개를 삭제할까요?\n\n", kindName, static_cast<int>(rows.size()));
	for (size_t k = 0; k < rows.size() && k < 15; ++k)
		msg += L"  " + rows[k].value + (rows[k].kind == CAT_LABEL && m_mode == MODE_STUDIO ? CString(L"  (레이블)") : CString()) + L"\n";
	if (rows.size() > 15)
	{
		CString more;
		more.Format(L"  … 외 %d개\n", static_cast<int>(rows.size() - 15));
		msg += more;
	}
	if (links > 0)
	{
		CString v;
		if (m_mode == MODE_COLLECTION)
			v.Format(L"\n들어 있는 영상(연결 %d건)은 컬렉션에서만 빠집니다 (영상 파일 · 정보는 그대로).", links);
		else
			v.Format(L"\n연결된 동영상(연결 %d건)에서도 빠집니다.", links);
		msg += v;
	}
	if (labelsOfStudios > 0)
	{
		CString v;
		v.Format(L"\n체크한 제작사의 레이블 %d개도 함께 삭제됩니다.", labelsOfStudios);
		msg += v;
	}
	if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;

	CommitDetails();
	for (const CatRow& r : rows)
	{
		if (m_mode == MODE_TAG)
		{
			m_lib.RemoveNamedFromVideos(LIST_TAG, r.value);
			const int n = m_lib.FindNamed(LIST_TAG, r.value);
			if (n >= 0)
				m_lib.tagInfos.erase(m_lib.tagInfos.begin() + n);
		}
		else if (m_mode == MODE_COLLECTION)
			m_lib.DeleteCollection(r.value);
		else if (r.kind == CAT_LABEL)
		{
			const int li = m_lib.FindLabel(r.value);   // 위에서 상위 제작사를 지우며 이미 지워졌을 수 있음
			if (li >= 0)
				m_lib.RemoveLabel(li);
			else
				for (VideoItem& v : m_lib.items)
					if (!v.label.IsEmpty() && v.label.CompareNoCase(r.value) == 0)
						v.label.Empty();
		}
		else
		{
			m_lib.RemoveNamedFromVideos(LIST_STUDIO, r.value);   // 영상의 제작사 · 레이블 + 그 제작사의 레이블
			const int n = m_lib.FindNamed(LIST_STUDIO, r.value);
			if (n >= 0)
				m_lib.studios.erase(m_lib.studios.begin() + n);
		}
	}
	m_catChecked[m_mode].clear();
	if (!m_lib.Save())
		AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);

	m_catSelKind = -1;
	m_catSelValue.Empty();
	m_catsDirty = true;   // 다른 탭의 개수도 다시 계산
	RebuildCategories();
	ApplyFilter();        // 영상의 제작사 · 태그 · 컬렉션이 바뀜
	UpdateStatus();
}

void CRuliManagerDlg::OnCatUncheckAll()
{
	if (m_mode >= 0 && m_mode < MODE_COUNT)
		m_catChecked[m_mode].clear();
	if (m_catGrid.GetSafeHwnd())
		m_catGrid.Invalidate(FALSE);
}

// --- 영상 카드 체크 ---

bool CRuliManagerDlg::IsItemChecked(int idx) const
{
	if (m_videoChecked.empty() || idx < 0 || idx >= static_cast<int>(m_lib.items.size()))
		return false;
	CString key = m_lib.items[idx].path;
	key.MakeLower();
	return m_videoChecked.count(key) > 0;
}

int CRuliManagerDlg::CheckedItemCount() const
{
	int n = 0;
	for (size_t i = 0; i < m_lib.items.size() && !m_videoChecked.empty(); ++i)
		if (IsItemChecked(static_cast<int>(i)))
			++n;
	return n;
}

bool CRuliManagerDlg::IsVideoCardChecked(int g) const
{
	const int row = GridRowOf(g);
	return row >= 0 && row < static_cast<int>(m_view.size()) && IsItemChecked(m_view[row]);
}

void CRuliManagerDlg::ToggleVideoCheck(int g)
{
	const int row = GridRowOf(g);
	if (row < 0 || row >= static_cast<int>(m_view.size()))
		return;
	const bool on = !IsVideoCardChecked(g);
	// 분할 영상 합쳐 보기 카드: 파트 모두 같이
	std::vector<int> rows;
	auto pit = m_gridParts.find(g);
	if (pit != m_gridParts.end())
		rows = pit->second;
	else
		rows.push_back(row);
	for (int r : rows)
	{
		if (r < 0 || r >= static_cast<int>(m_view.size()))
			continue;
		CString key = m_lib.items[m_view[r]].path;
		key.MakeLower();
		if (on)
			m_videoChecked.insert(key);
		else
			m_videoChecked.erase(key);
	}
	CRect tile;
	if (m_grid.GetTileRect(g, tile))
		m_grid.InvalidateRect(&tile, FALSE);
}

void CRuliManagerDlg::OnVideoDeleteChecked()
{
	// 체크한 영상의 DB 정보를 모두 삭제 (파일은 그대로) - 하나 삭제([DB에서 삭제])와 같은 규칙
	//  저장된 영상(파일 있음) → 임시 항목으로 되돌림, 임시 항목 · 파일 없는 영상 → 목록에서 빠짐
	std::vector<int> list;
	for (size_t i = 0; i < m_lib.items.size() && !m_videoChecked.empty(); ++i)
		if (IsItemChecked(static_cast<int>(i)))
			list.push_back(static_cast<int>(i));
	if (list.empty())
	{
		m_videoChecked.clear();
		return;
	}
	CString msg;
	msg.Format(L"체크한 영상 %d개의 DB 정보를 삭제할까요?\n(파일은 삭제되지 않습니다 - 파일이 있으면 임시 항목으로 돌아가고, 임시 항목 · 파일이 없는 영상은 목록에서 빠집니다)\n\n",
		static_cast<int>(list.size()));
	for (size_t k = 0; k < list.size() && k < 15; ++k)
	{
		const VideoItem& v = m_lib.items[list[k]];
		msg += L"  " + (v.title.IsEmpty() ? v.FileName() : v.title) + L"\n";
	}
	if (list.size() > 15)
	{
		CString more;
		more.Format(L"  … 외 %d개\n", static_cast<int>(list.size() - 15));
		msg += more;
	}
	if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;

	CommitDetails();
	const int row = GetSelectedRow();
	BeginLibraryChange();
	for (auto it = list.rbegin(); it != list.rend(); ++it)   // 뒤에서부터 (지워도 앞 번호 그대로)
	{
		const int idx = *it;
		const VideoItem old = m_lib.items[idx];
		const bool exists = ::PathFileExistsW(old.path) != FALSE;
		if (!old.pending && exists)
		{
			VideoItem fresh;
			fresh.path = old.path;
			fresh.size = old.size;
			fresh.modified = old.modified;
			fresh.pending = true;
			m_lib.ApplyFolderStructure(fresh);   // 폴더 구조/이름의 배우·스튜디오·발매일은 다시 자동 지정
			m_lib.items[idx] = fresh;
		}
		else
			m_lib.items.erase(m_lib.items.begin() + idx);
	}
	m_videoChecked.clear();
	if (!m_lib.Save())
		AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);
	m_catsDirty = true;
	ApplyFilter();
	if (!m_view.empty())
	{
		const int next = (std::min)((std::max)(row, 0), static_cast<int>(m_view.size()) - 1);
		m_list.SetItemState(next, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_list.EnsureVisible(next, FALSE);
	}
	else
	{
		CommitDetails();
		ShowDetails(-1);
	}
}

int CRuliManagerDlg::CheckAllCount() const
{
	if (IsActorGridMode())
		return static_cast<int>(m_actorRows.size());
	if (CatChecksEnabled())
	{
		int n = 0;
		for (const CatRow& r : m_catRows)
			if (r.kind == CAT_VALUE || r.kind == CAT_LABEL)
				++n;
		return n;
	}
	if (IsCategoryListMode() || YearViewActive())
		return 0;   // 제작사 [리스트] 보기 · 연도별 보기: 체크 칸 없음
	return static_cast<int>(m_view.size());   // 영상 격자: 지금 페이지
}

void CRuliManagerDlg::OnCheckAll()
{
	if (IsActorGridMode())
	{
		for (int idx : m_actorRows)
		{
			if (idx < 0 || idx >= static_cast<int>(m_lib.actors.size()))
				continue;
			CString key = m_lib.actors[idx].name;
			key.MakeLower();
			m_actorChecked.insert(key);
		}
		m_actorGrid.Invalidate(FALSE);
		return;
	}
	if (CatChecksEnabled())
	{
		for (const CatRow& r : m_catRows)
			if (r.kind == CAT_VALUE || r.kind == CAT_LABEL)
				m_catChecked[m_mode].insert(CatCheckKey(r.kind, r.value));
		m_catGrid.Invalidate(FALSE);
		return;
	}
	if (IsCategoryListMode() || YearViewActive())
		return;
	for (int idx : m_view)   // 지금 페이지의 영상 (합쳐 보기 카드의 파트도 m_view 에 모두 있음)
	{
		if (idx < 0 || idx >= static_cast<int>(m_lib.items.size()))
			continue;
		CString key = m_lib.items[idx].path;
		key.MakeLower();
		m_videoChecked.insert(key);
	}
	m_grid.Invalidate(FALSE);
	if (m_list.GetSafeHwnd())
		m_list.Invalidate(FALSE);   // [리스트] 체크 칸
}

void CRuliManagerDlg::OnVideoUncheckAll()
{
	m_videoChecked.clear();
	if (m_grid.GetSafeHwnd())
		m_grid.Invalidate(FALSE);
	if (m_list.GetSafeHwnd())
		m_list.Invalidate(FALSE);   // [리스트] 체크 칸
}

CString CRuliManagerDlg::CatGridOwner::GridTipText(int row, const CRect& /*card*/, CPoint /*pt*/)
{
	// 컬렉션 카드에 마우스를 올리면 제목 + 메모
	if (dlg->m_mode != MODE_COLLECTION || row < 0 || row >= static_cast<int>(dlg->m_catRows.size()))
		return CString();
	const int ci = dlg->m_lib.FindCollection(dlg->m_catRows[row].value);
	if (ci < 0 || dlg->m_lib.collectionInfos[ci].memo.IsEmpty())
		return CString();
	CString memo = dlg->m_lib.collectionInfos[ci].memo;
	memo.Replace(L"\n", L"\r\n");
	return dlg->m_lib.collectionInfos[ci].name + L"\r\n\r\n" + memo;
}

bool CRuliManagerDlg::IsTagFavorite(const CString& tag) const
{
	const int n = m_lib.FindNamed(LIST_TAG, tag);
	return n >= 0 && m_lib.tagInfos[n].favorite;
}

// ---------------------------------------------------------------------------
// 텍스트로 정보 입력 (정보 txt 와 같은 "항목: 값" 규칙)

void CRuliManagerDlg::OnVideoTextInfo()
{
	CommitDetails();   // 편집 중인 내용 먼저 저장
	const int idx = m_curItem;
	if (idx < 0 || idx >= static_cast<int>(m_lib.items.size()) || IsItemOffline(idx))
		return;
	const VideoItem& cur = m_lib.items[idx];
	CTextInfoDlg dlg(L"텍스트로 정보 입력 - " + cur.FileName(),
		L"정보 txt 와 같은 \"항목: 값\" 형식입니다 (스캔 때 읽는 규칙과 같음). 입력한 내용으로 바뀌고, 지운 항목은 비워집니다.\r\n"
		L"항목: 품번 · 제목 · 발매일 · 별점 · 배우(출연) · 참여 별칭 · 제작사 · 레이블 · 시리즈 · 태그(장르) · 물방울  (목록은 쉼표 · / · # 로 구분)",
		CVideoLibrary::VideoInfoText(cur), this);
	if (dlg.DoModal() != IDOK)
		return;
	ApplyTextToVideo(idx, dlg.m_text);
}

void CRuliManagerDlg::OnVideoPasteInfo()
{
	CommitDetails();   // 편집 중인 내용 먼저 저장
	const int idx = m_curItem;
	if (idx < 0 || idx >= static_cast<int>(m_lib.items.size()) || IsItemOffline(idx))
		return;
	const CString fileName = m_lib.items[idx].FileName();

	// 1단계: 사이트 화면을 복사한 글자를 그대로 붙여넣기 (정보 txt 와는 연동하지 않음)
	CTextInfoDlg dlg(L"웹페이지 내용 붙여넣기 - " + fileName,
		L"작품 사이트 화면을 드래그해 복사(Ctrl+C)한 뒤 여기에 붙여넣으세요(Ctrl+V). 정보 txt 파일과는 연동되지 않습니다.\r\n"
		L"출시 / 출연 / 제작사 / 레이블 / 장르, 品番 / 発売日 / 出演者 / メーカー / レーベル / ジャンル 같은 항목을 찾아 읽습니다.",
		CString(), this);
	dlg.m_pasteButton = true;   // [공백라인 제거]
	if (dlg.DoModal() != IDOK)
		return;

	const CString info = CVideoLibrary::ParsePastedVideoText(dlg.m_text);
	if (info.IsEmpty())
	{
		AfxMessageBox(L"붙여넣은 내용에서 영상 정보를 찾지 못했습니다.", MB_ICONINFORMATION);
		return;
	}

	// 읽어낸 항목만 기존 정보에 덮어쓰기 (없는 항목은 그대로 유지)
	VideoItem fetched;
	std::vector<CString> webTags;   // 붙여넣은 태그 (목록 이름으로 바꾸기 전)
	m_lib.ApplyVideoText(fetched, info, &webTags);
	VideoItem merged = m_lib.items[idx];
	if (!fetched.code.IsEmpty())    merged.code = fetched.code;
	if (!fetched.title.IsEmpty())   merged.title = fetched.title;
	if (!fetched.release.IsEmpty()) merged.release = fetched.release;
	if (!fetched.actors.IsEmpty())
	{
		// 출연: 붙여넣은 "출연" 의 배우만 (기존 배우는 바꿈), 등록된 배우의 과거 이름(다른 이름)으로 적혀 있으면 참여 별칭 = 그 과거 이름
		//  - 이름 판단: 대표 이름 그대로 → 다른 이름 그대로 → 언어별로 대표 이름과 같음 → 언어별로 다른 이름과 같음 (앞쪽 우선)
		//  - 지금 영상 배우와 연결 안 되는 붙여넣은 배우가 있으면 연결 창 (왼쪽 = 붙여넣은 출연, 오른쪽 = 지금 배우)
		//  - 연결된 배우(같은 배우로 판단된 것 포함)는 기존 이름 + 붙여넣은 이름을 조립 → "한글이름(영어이름, 일본어이름)" (언어마다 기존 표기 우선)
		auto addUnique = [](std::vector<CString>& v, const CString& s)
		{
			for (const CString& e : v)
				if (e.CompareNoCase(s) == 0) return;
			v.push_back(s);
		};
		// 붙여넣은 그대로의 출연 이름 (ApplyVideoText 가 대표 이름으로 바꾸기 전) - 확인 글자의 "배우:" 줄
		std::vector<CString> rawCast;
		{
			int pos = 0;
			while (pos <= info.GetLength())
			{
				int nl = info.Find(L'\n', pos);
				if (nl < 0) nl = info.GetLength();
				CString line = info.Mid(pos, nl - pos);
				pos = nl + 1;
				line.Trim();
				if (line.Left(3) == L"배우:")
				{
					rawCast = CVideoLibrary::SplitList(line.Mid(3));
					break;
				}
			}
			if (rawCast.empty())
				rawCast = CVideoLibrary::SplitList(fetched.actors);
		}
		// 이름 → 등록된 배우 (ai) + 과거 이름으로 적혔으면 그 다른 이름
		auto resolve = [this](const CString& n, int& ai, CString& alias)
		{
			ai = -1;
			alias.Empty();
			for (size_t i = 0; i < m_lib.actors.size() && ai < 0; ++i)
			{
				const ActorInfo& a = m_lib.actors[i];
				if (a.name.CompareNoCase(n) == 0) { ai = static_cast<int>(i); break; }
				for (const CString& al : CVideoLibrary::SplitList(a.aliases))
					if (al.CompareNoCase(n) == 0) { ai = static_cast<int>(i); alias = al; break; }
			}
			for (size_t i = 0; i < m_lib.actors.size() && ai < 0; ++i)
				if (CVideoLibrary::SameNameByLang(m_lib.actors[i].name, n)) ai = static_cast<int>(i);
			for (size_t i = 0; i < m_lib.actors.size() && ai < 0; ++i)
				for (const CString& al : CVideoLibrary::SplitList(m_lib.actors[i].aliases))
					if (CVideoLibrary::SameNameByLang(al, n)) { ai = static_cast<int>(i); alias = al; break; }
		};
		struct CastItem { CString raw; int ai = -1; CString disp; };   // disp = 이 작품에서 쓴 이름 (과거 이름이면 과거 이름)
		std::vector<CastItem> web, cur;
		for (const CString& n : rawCast)
		{
			CastItem w;
			w.raw = n;
			CString alias;
			resolve(n, w.ai, alias);
			w.disp = (w.ai < 0) ? n : (alias.IsEmpty() ? m_lib.actors[w.ai].name : alias);
			bool dup = false;
			for (const CastItem& e : web)
				if (e.disp.CompareNoCase(w.disp) == 0 || e.raw.CompareNoCase(w.raw) == 0) { dup = true; break; }
			if (!dup)
				web.push_back(w);
		}
		{
			const VideoItem& v = m_lib.items[idx];
			const std::vector<CString> credited = CVideoLibrary::SplitList(v.actorAliases);
			for (const CString& n : CVideoLibrary::SplitList(v.actors))
			{
				CastItem c;
				c.raw = n;
				c.ai = m_lib.FindActorByAnyName(n);
				c.disp = n;
				if (c.ai >= 0)
				{
					c.disp = m_lib.actors[c.ai].name;
					for (const CString& cr : credited)
						if (m_lib.FindActorByAnyName(cr) == c.ai && cr.CompareNoCase(m_lib.actors[c.ai].name) != 0) { c.disp = cr; break; }
				}
				bool dup = false;
				for (const CastItem& e : cur)
					if (e.disp.CompareNoCase(c.disp) == 0) { dup = true; break; }
				if (!dup)
					cur.push_back(c);
			}
		}
		auto sameCast = [](const CastItem& w, const CastItem& c)
		{
			if (w.ai >= 0 || c.ai >= 0)
				return w.ai == c.ai;
			return w.raw.CompareNoCase(c.raw) == 0 || CVideoLibrary::SameNameByLang(w.raw, c.raw);
		};
		// 붙여넣은 배우마다 연결된 지금 배우 (cur 번호, -1 = 없음), 추가 여부 / 지금 배우마다 유지 여부
		std::vector<int> linkOf(web.size(), -1);
		std::vector<bool> addWeb(web.size(), true), keepCur(cur.size(), false);
		bool anyUnlinked = false;
		for (size_t i = 0; i < web.size(); ++i)
		{
			for (size_t j = 0; j < cur.size(); ++j)
				if (sameCast(web[i], cur[j])) { linkOf[i] = static_cast<int>(j); break; }
			if (linkOf[i] < 0) anyUnlinked = true;
		}
		if (!cur.empty() && anyUnlinked)
		{
			std::vector<CString> curDisp, webDisp, autoLink;
			std::vector<bool> addDefault;
			for (const CastItem& c : cur) curDisp.push_back(c.disp);
			for (size_t i = 0; i < web.size(); ++i)
			{
				webDisp.push_back(web[i].raw);
				autoLink.push_back(linkOf[i] >= 0 ? cur[linkOf[i]].disp : CString());
				addDefault.push_back(true);
			}
			CTagLinkDlg link(curDisp, webDisp, autoLink, addDefault, this);
			link.m_caption = L"출연 배우 연결";
			link.m_guide = L"붙여넣은 출연 배우(왼쪽)를 지금 영상의 배우(오른쪽)와 연결하세요. 연결하면 같은 배우로 보고 기존 이름과 붙여넣은 이름을 조립해 \"한글이름(영어이름, 일본어이름)\" 으로 저장합니다. "
				L"연결 안 된 왼쪽 칩 = 추가 ↔ 무시, 연결 안 된 오른쪽 칩 = 지움 ↔ 유지 (누르면 전환).";
			link.m_headCur = L"지금 배우";
			link.m_headWeb = L"붙여넣은 출연";
			link.m_keepDefault.assign(cur.size(), false);
			if (link.DoModal() != IDOK)
				return;   // 취소: 붙여넣기 전체 취소
			for (size_t i = 0; i < web.size() && i < link.m_result.size(); ++i)
			{
				const CTagLinkDlg::Result& r = link.m_result[i];
				linkOf[i] = -1;
				for (size_t j = 0; j < cur.size() && !r.linked.IsEmpty(); ++j)
					if (cur[j].disp == r.linked) { linkOf[i] = static_cast<int>(j); break; }
				addWeb[i] = r.addNew;
			}
			for (size_t j = 0; j < cur.size(); ++j)
				for (const CString& k : link.m_keepTags)
					if (k == cur[j].disp) { keepCur[j] = true; break; }
		}
		// 이름 조립: 기존 이름(대표 이름 또는 그 작품의 과거 이름) + 붙여넣은 이름 → 배우 목록 · 영상들의 이름도 바꿈
		bool libChanged = false;
		auto compose = [&](int ai, const CString& target, const CString& raw) -> CString
		{
			const CString full = CVideoLibrary::ComposeFullName(target, raw);
			if (full == target)
				return target;
			if (ai < 0)
				return full;   // 등록 안 된 배우: 조립한 이름 그대로 (저장할 때 배우 목록에 추가)
			const int other = m_lib.FindActorByAnyName(full);
			if (other >= 0 && other != ai)
				return target;   // 다른 배우가 이미 그 이름을 씀
			ActorInfo& a = m_lib.actors[ai];
			if (a.name.CompareNoCase(target) == 0)
			{
				m_lib.RenameActorInVideos(a.name, full);
				a.name = full;
			}
			else
			{
				// 과거 이름(다른 이름)을 조립한 이름으로: 배우의 다른 이름 + 영상들의 참여 별칭
				std::vector<CString> al = CVideoLibrary::SplitList(a.aliases);
				for (CString& e : al)
					if (e.CompareNoCase(target) == 0) e = full;
				a.aliases = CVideoLibrary::JoinList(al);
				for (VideoItem& v : m_lib.items)
				{
					std::vector<CString> cr = CVideoLibrary::SplitList(v.actorAliases);
					bool hit = false;
					for (CString& e : cr)
						if (e.CompareNoCase(target) == 0) { e = full; hit = true; }
					if (hit)
						v.actorAliases = CVideoLibrary::JoinList(cr);
				}
			}
			libChanged = true;
			return full;
		};
		std::vector<CString> names;
		std::vector<bool> curUsed(cur.size(), false);
		for (size_t i = 0; i < web.size(); ++i)
		{
			if (linkOf[i] >= 0)
			{
				CastItem& c = cur[linkOf[i]];
				const CString target = c.disp;
				c.disp = compose(c.ai, target, web[i].raw);
				curUsed[linkOf[i]] = true;
				addUnique(names, c.disp);
			}
			else if (addWeb[i])
				addUnique(names, compose(web[i].ai, web[i].disp, web[i].raw));
		}
		for (size_t j = 0; j < cur.size(); ++j)
			if (!curUsed[j] && keepCur[j])
				addUnique(names, cur[j].disp);
		if (libChanged)
		{
			if (!m_lib.Save())
				AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);
			MarkCategoriesDirty();
		}
		// 확인 창의 "배우:" 에는 이 작품에서 쓴 이름 그대로 (과거 이름이면 과거 이름) → 적용할 때 배우 이름 + 참여 별칭으로 정리됨
		merged.actors = CVideoLibrary::JoinList(names);
		merged.actorAliases.Empty();   // 참여 별칭은 "배우:" 의 과거 이름으로 정해짐 (따로 적지 않음)
	}
	// 제작사 · 레이블: 이미 설정된 값과 다른 값을 붙여넣으면 연결 창 (왼쪽 = 붙여넣은 값, 오른쪽 = 지금 값)
	//  - 연결 = 같은 제작사 · 레이블 → 지금 값 유지 + 붙여넣은 이름을 그 항목의 서브이름으로 (다음부터 자동으로 같은 것)
	//  - 연결 안 된 붙여넣은 값: 처음 "추가" = 그 종류(제작사 / 레이블)의 값을 붙여넣은 값으로 바꿈, "무시" = 안 바꿈
	//  - 연결 안 된 지금 값: 같은 종류의 붙여넣은 값이 있으면 처음 "지움", 없으면 처음 "유지"
	bool studioLinkDone = false;
	{
		CString curStudio = merged.studio, curLabel = merged.label;
		curStudio.Trim();
		curLabel.Trim();
		auto sameStudio = [this](const CString& a, const CString& b)
		{
			if (a.CompareNoCase(b) == 0) return true;
			const int x = m_lib.FindStudioLoose(a), y = m_lib.FindStudioLoose(b);
			return x >= 0 && x == y;
		};
		auto sameLabel = [this](const CString& a, const CString& b)
		{
			if (a.CompareNoCase(b) == 0) return true;
			const int x = m_lib.FindLabelLoose(a), y = m_lib.FindLabelLoose(b);
			return x >= 0 && x == y;
		};
		const bool conflict = (!fetched.studio.IsEmpty() && !curStudio.IsEmpty() && !sameStudio(fetched.studio, curStudio)) ||
		                      (!fetched.label.IsEmpty() && !curLabel.IsEmpty() && !sameLabel(fetched.label, curLabel));
		if (conflict)
		{
			struct Item { int kind; CString name; CString disp; };   // kind 0 = 제작사, 1 = 레이블
			auto disp = [](int kind, const CString& n) { return CString(kind == 0 ? L"제작사: " : L"레이블: ") + n; };
			std::vector<Item> web, cur;
			if (!fetched.studio.IsEmpty()) web.push_back({ 0, fetched.studio, disp(0, fetched.studio) });
			if (!fetched.label.IsEmpty())  web.push_back({ 1, fetched.label, disp(1, fetched.label) });
			if (!curStudio.IsEmpty())      cur.push_back({ 0, curStudio, disp(0, curStudio) });
			if (!curLabel.IsEmpty())       cur.push_back({ 1, curLabel, disp(1, curLabel) });
			auto sameItem = [&](const Item& w, const Item& c)   // 같은 항목인지 (종류가 달라도 레이블 ↔ 레이블 이름 등으로 비교)
			{
				return (c.kind == 0) ? sameStudio(w.name, c.name) : sameLabel(w.name, c.name);
			};
			std::vector<CString> curDisp, webDisp, autoLink;
			std::vector<bool> addDefault, keepDefault;
			for (const Item& c : cur) curDisp.push_back(c.disp);
			for (const Item& w : web)
			{
				webDisp.push_back(w.disp);
				CString link;
				for (const Item& c : cur)
					if (sameItem(w, c)) { link = c.disp; break; }
				autoLink.push_back(link);
				addDefault.push_back(link.IsEmpty());
			}
			for (const Item& c : cur)
			{
				bool linked = false, sameKindWeb = false;
				for (size_t i = 0; i < web.size(); ++i)
				{
					if (autoLink[i] == c.disp) linked = true;
					if (web[i].kind == c.kind && autoLink[i].IsEmpty()) sameKindWeb = true;
				}
				keepDefault.push_back(linked || !sameKindWeb);
			}
			CTagLinkDlg link(curDisp, webDisp, autoLink, addDefault, this);
			link.m_caption = L"제작사 · 레이블 연결";
			link.m_guide = L"붙여넣은 제작사 · 레이블(왼쪽)을 지금 영상에 설정된 값(오른쪽)과 연결하세요. 연결하면 지금 값을 유지하고 붙여넣은 이름을 그 항목의 서브이름으로 저장합니다(다음부터 자동으로 같은 것). "
				L"연결 안 된 왼쪽 칩 = 추가(그 종류의 값을 바꿈) ↔ 무시, 연결 안 된 오른쪽 칩 = 지움 ↔ 유지 (누르면 전환).";
			link.m_headCur = L"지금 값";
			link.m_headWeb = L"붙여넣은 값";
			link.m_keepDefault = keepDefault;
			if (link.DoModal() != IDOK)
				return;   // 취소: 붙여넣기 전체 취소
			auto keptCur = [&](const Item& c)
			{
				for (const CString& k : link.m_keepTags)
					if (k == c.disp) return true;
				return false;
			};
			CString newStudio, newLabel;
			for (const Item& c : cur)
				if (keptCur(c)) (c.kind == 0 ? newStudio : newLabel) = c.name;
			bool aliasChanged = false;
			for (size_t i = 0; i < web.size() && i < link.m_result.size(); ++i)
			{
				const CTagLinkDlg::Result& r = link.m_result[i];
				const Item& w = web[i];
				if (!r.linked.IsEmpty())
				{
					// 연결: 붙여넣은 이름 → 그 항목의 서브이름 (이미 같은 항목으로 찾아지면 그대로)
					for (const Item& c : cur)
					{
						if (c.disp != r.linked || sameItem(w, c))
							continue;
						const int n = (c.kind == 0) ? m_lib.FindStudioLoose(c.name) : m_lib.FindLabelLoose(c.name);
						if (n < 0)
							continue;   // 목록에 없는 이름
						NamedInfo& t = (c.kind == 0) ? m_lib.studios[n] : m_lib.labelInfos[n];
						bool dup = false;
						for (const CString& e : CVideoLibrary::SplitLines(t.subName))
							if (e.CompareNoCase(w.name) == 0) { dup = true; break; }
						if (!dup && w.name.CompareNoCase(t.name) != 0)
						{
							t.subName.Trim();
							t.subName += (t.subName.IsEmpty() ? CString() : CString(L"\n")) + w.name;
							aliasChanged = true;
						}
					}
				}
				else if (r.addNew)
					(w.kind == 0 ? newStudio : newLabel) = w.name;   // 그 종류의 값을 붙여넣은 값으로
			}
			if (!sameStudio(newStudio, curStudio))
				merged.series = fetched.series;   // 제작사가 바뀌면 시리즈도 붙여넣은 값 (없으면 비움)
			else if (!fetched.series.IsEmpty())
				merged.series = fetched.series;
			merged.studio = newStudio;
			merged.label = newLabel;
			if (aliasChanged)
			{
				if (!m_lib.Save())
					AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);
				MarkCategoriesDirty();
			}
			studioLinkDone = true;
		}
	}
	if (!studioLinkDone)
	{
		if (!fetched.studio.IsEmpty())
		{
			if (fetched.studio != merged.studio)
			{
				merged.label = fetched.label;   // 스튜디오가 바뀌면 예전 레이블 · 시리즈는 버림
				merged.series = fetched.series;
			}
			merged.studio = fetched.studio;
		}
		if (!fetched.label.IsEmpty())   merged.label = fetched.label;
		if (!fetched.series.IsEmpty())  merged.series = fetched.series;
	}
	if (!fetched.tags.IsEmpty())    merged.tags = fetched.tags;
	if (!fetched.seriesTitle.IsEmpty()) merged.seriesTitle = fetched.seriesTitle;   // 웹페이지의 "시리즈"(이름) → 상세 아래 [시리즈] 칸

	// 태그 연결: 붙여넣은 태그 중 현재 영상 태그에 없는 것이 있으면 현재 태그와 연결하는 창 (연결한 이름은 그 태그의 다른 이름으로)
	//  - 영상 태그 = 현재 태그 + 추가로 고른 붙여넣은 태그 (병합), 현재 태그가 없으면 예전처럼 붙여넣은 태그 그대로
	const std::vector<CString> curTags = CVideoLibrary::SplitList(m_lib.items[idx].tags);
	if (!webTags.empty() && !curTags.empty())
	{
		auto resolved = [this](const CString& t)   // DB 태그 이름 (없으면 그대로)
		{
			const int n = m_lib.FindNamed(LIST_TAG, t);
			return n >= 0 ? m_lib.tagInfos[n].name : t;
		};
		auto findCur = [&curTags, &resolved](const CString& t) -> CString   // 현재 태그 중 같은 태그
		{
			const CString r = resolved(t);
			for (const CString& c : curTags)
				if (c.CompareNoCase(t) == 0 || c.CompareNoCase(r) == 0 || resolved(c).CompareNoCase(r) == 0)
					return c;
			return CString();
		};
		std::vector<CString> autoLink;
		std::vector<bool> addDefault;
		bool anyUnlinked = false;
		std::vector<bool> curLinked(curTags.size(), false);
		for (const CString& w : webTags)
		{
			const CString cur = findCur(w);   // 같은 태그로 확인된 것만 처음부터 연결
			autoLink.push_back(cur);
			addDefault.push_back(cur.IsEmpty());   // 연결 안 된 붙여넣은 태그는 무조건 추가 상태로 시작
			if (cur.IsEmpty())
				anyUnlinked = true;
			for (size_t c = 0; c < curTags.size(); ++c)
				if (!cur.IsEmpty() && curTags[c].CompareNoCase(cur) == 0)
					curLinked[c] = true;
		}
		for (bool l : curLinked)
			if (!l)
				anyUnlinked = true;   // 연결 안 된 현재 태그 (처음에 지움 상태) 도 창에서 확인
		std::vector<CString> out = curTags;   // 모두 연결됨 (창 없음): 현재 태그 그대로
		if (anyUnlinked)
		{
			CTagLinkDlg link(curTags, webTags, autoLink, addDefault, this);
			if (link.DoModal() != IDOK)
				return;   // 취소: 붙여넣기 전체 취소
			out = link.m_keepTags;   // 현재 태그 중 연결된 것 + 유지로 바꾼 것만 (나머지는 지움)
			// 1) 연결 결과 정리: 다른 이름 추가 / 태그 병합 / 영상에 추가
			std::vector<std::pair<CString, CString>> aliasList;   // (현재 태그, 그 태그의 다른 이름으로 넣을 웹 이름) - 웹 이름이 아직 어느 태그도 아님
			std::vector<std::pair<CString, CString>> mergeList;   // (지울 현재 태그, 남길 태그 = 웹 이름이 이미 가리키는 태그) - 왼쪽 기준 병합
			for (const CTagLinkDlg::Result& r : link.m_result)
			{
				if (!r.linked.IsEmpty())
				{
					CString web = r.web;
					web.Trim();
					if (web.IsEmpty() || web.CompareNoCase(r.linked) == 0)
						continue;   // 같은 이름
					const int n = m_lib.FindNamed(LIST_TAG, r.linked);
					const int wn = m_lib.FindNamed(LIST_TAG, web);
					if (wn >= 0 && wn == n)
						continue;   // 이미 같은 태그 (이름 · 다른 이름 · 영어 · 일본어 이름)
					if (wn >= 0)
					{
						// 웹 이름이 이미 다른 태그 → 왼쪽(웹) 태그 기준으로 병합, 오른쪽 태그 이름은 다른 이름으로
						const CString srcName = (n >= 0) ? m_lib.tagInfos[n].name : r.linked;
						const CString dstName = m_lib.tagInfos[wn].name;
						if (std::find_if(mergeList.begin(), mergeList.end(), [&](const auto& m) { return m.first.CompareNoCase(srcName) == 0; }) == mergeList.end())
							mergeList.push_back({ srcName, dstName });
					}
					else
						aliasList.push_back({ r.linked, web });   // 웹 이름 → 오른쪽 태그의 다른 이름
					continue;
				}
				if (!r.addNew)
					continue;   // 무시
				const CString name = resolved(r.web);
				if (std::find_if(out.begin(), out.end(), [&](const CString& o) { return o.CompareNoCase(name) == 0 || resolved(o).CompareNoCase(name) == 0; }) == out.end())
					out.push_back(name);
			}
			// 2) 병합은 다른 영상의 태그도 바꾸므로 한 번 확인
			if (!mergeList.empty())
			{
				const std::map<CString, int> counts = m_lib.CountNamed(LIST_TAG);
				CString msg = L"연결한 태그 중 이미 있는 태그가 있어 왼쪽(붙여넣은) 태그 기준으로 병합합니다.\n\n";
				for (const auto& m : mergeList)
				{
					CString key = m.first;
					key.MakeLower();
					auto it = counts.find(key);
					CString line;
					line.Format(L"  '%s' → '%s'  (동영상 %d개)\n", static_cast<LPCWSTR>(m.first), static_cast<LPCWSTR>(m.second), it != counts.end() ? it->second : 0);
					msg += line;
				}
				msg += L"\n오른쪽 태그는 모든 동영상에서 왼쪽 태그로 바뀌고, 이름은 왼쪽 태그의 다른 이름으로 들어간 뒤 태그 목록에서 삭제됩니다. 병합할까요?\n(아니요: 병합하지 않고 연결만 유지)";
				if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION) != IDYES)
					mergeList.clear();
			}
			bool libChanged = false;
			auto addSub = [](NamedInfo& t, const CString& name)   // 다른 이름(줄바꿈 구분)에 추가 (이름 · 이미 있는 다른 이름은 건너뜀)
			{
				CString s = name;
				s.Trim();
				if (s.IsEmpty() || s.CompareNoCase(t.name) == 0)
					return;
				for (const CString& e : CVideoLibrary::SplitLines(t.subName))
					if (e.CompareNoCase(s) == 0)
						return;
				t.subName.Trim();
				t.subName += (t.subName.IsEmpty() ? CString() : CString(L"\n")) + s;
			};
			// 3) 병합: 영상 태그 바꿈 → 오른쪽 태그의 이름 · 다른 이름 · 영어 · 일본어 이름을 왼쪽 태그의 다른 이름으로 → 오른쪽 태그 삭제
			for (const auto& m : mergeList)
			{
				const int si = m_lib.FindNamed(LIST_TAG, m.first);
				int di = m_lib.FindNamed(LIST_TAG, m.second);
				if (di < 0 || si == di)
					continue;
				m_lib.RenameNamedInVideos(LIST_TAG, m.first, m.second);   // 모든 영상 (이미 왼쪽 태그가 있으면 하나로)
				if (si >= 0)
				{
					const NamedInfo s = m_lib.tagInfos[si];
					NamedInfo& d = m_lib.tagInfos[di];
					addSub(d, s.name);
					for (const CString& e : CVideoLibrary::SplitLines(s.subName))
						addSub(d, e);
					if (d.nameEn.IsEmpty()) d.nameEn = s.nameEn; else addSub(d, s.nameEn);
					if (d.nameJa.IsEmpty()) d.nameJa = s.nameJa; else addSub(d, s.nameJa);
					if (d.memo.IsEmpty()) d.memo = s.memo;
					if (d.image.IsEmpty()) d.image = s.image;
					if (s.favorite) d.favorite = true;
					m_lib.tagInfos.erase(m_lib.tagInfos.begin() + si);
				}
				else
					addSub(m_lib.tagInfos[di], m.first);   // 목록에 없던 태그: 이름만 다른 이름으로
				for (CString& o : out)   // 이 영상의 남길 태그도 왼쪽 태그로
					if (o.CompareNoCase(m.first) == 0)
						o = m.second;
				libChanged = true;
			}
			// 4) 다른 이름 추가: 웹 이름이 아직 어느 태그도 아니면 오른쪽 태그의 다른 이름으로 (태그 목록에 없는 태그면 등록)
			for (const auto& al : aliasList)
			{
				if (m_lib.FindNamed(LIST_TAG, al.second) >= 0)
					continue;   // 그사이 다른 태그 이름이 됨
				int n = m_lib.FindNamed(LIST_TAG, al.first);
				if (n < 0)
				{
					NamedInfo info;
					info.name = al.first;
					m_lib.tagInfos.push_back(info);
					n = static_cast<int>(m_lib.tagInfos.size()) - 1;
				}
				addSub(m_lib.tagInfos[n], al.second);
				libChanged = true;
			}
			// 남길 태그 중복 정리 (병합으로 같은 태그가 됐을 수 있음)
			{
				std::vector<CString> uniq;
				for (const CString& o : out)
					if (std::find_if(uniq.begin(), uniq.end(), [&](const CString& u) { return u.CompareNoCase(o) == 0; }) == uniq.end())
						uniq.push_back(o);
				out.swap(uniq);
			}
			if (libChanged)
			{
				// 다른 이름 · 병합은 연결 창 확인 때 바로 저장 (다음 확인 창을 취소해도 남음)
				if (!m_lib.Save())
					AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);
				MarkCategoriesDirty();
				if (!mergeList.empty())
				{
					m_grid.Invalidate(FALSE);
					if (m_curItem >= 0)
						ShowDetails(m_curItem);   // 상세 칸의 태그도 병합된 이름으로 (나중에 저장할 때 예전 태그가 되살아나지 않게)
				}
			}
		}
		merged.tags = CVideoLibrary::JoinList(out);
	}

	// 2단계: 읽어낸 결과를 확인 / 수정한 뒤 적용
	CTextInfoDlg dlg2(L"읽어낸 정보 확인 - " + fileName,
		L"붙여넣은 내용에서 읽어낸 결과입니다. 확인하고 필요하면 고친 뒤 확인을 누르면 적용됩니다.",
		CVideoLibrary::VideoInfoText(merged), this);
	if (dlg2.DoModal() != IDOK)
		return;
	ApplyTextToVideo(idx, dlg2.m_text);   // 저장 포함
}

void CRuliManagerDlg::ApplyTextToVideo(int idx, const CString& text)
{
	if (idx < 0 || idx >= static_cast<int>(m_lib.items.size()))
		return;
	VideoItem v = m_lib.items[idx];
	CVideoLibrary::ClearVideoTextFields(v);
	m_lib.ApplyVideoText(v, text);
	VideoItem& dst = m_lib.items[idx];
	dst.code = v.code; dst.title = v.title; dst.release = v.release; dst.rating = v.rating; dst.oCount = v.oCount; dst.censor = v.censor;
	dst.actors = v.actors; dst.actorAliases = v.actorAliases; dst.studio = v.studio; dst.label = v.label; dst.series = v.series; dst.seriesTitle = v.seriesTitle; dst.tags = v.tags;
	dst.pending = false;   // 정보를 입력했으면 정식 DB 로
	m_lib.NormalizeVideoActors(dst);
	m_lib.SyncPartGroup(static_cast<size_t>(idx));   // 분할 파일도 같은 정보
	m_lib.SyncActorsFromVideos();   // 새 배우 / 스튜디오 / 태그는 목록에도 추가
	m_lib.SyncNamedFromVideos();
	if (!m_lib.Save())
		AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);

	MarkCategoriesDirty();
	ApplyFilter();
	if (m_curItem == idx)
		ShowDetails(idx);
	UpdateStatus();
	m_list.Invalidate(FALSE);
	m_grid.Invalidate(FALSE);
}

void CRuliManagerDlg::OnActorTextInfo()
{
	if (!IsActorGridMode())
		return;
	const int idx = SelectedActorIndex();
	if (idx < 0 || idx >= static_cast<int>(m_lib.actors.size()))
		return;
	const ActorInfo& cur = m_lib.actors[idx];
	CTextInfoDlg dlg(L"텍스트로 정보 입력 - " + cur.name,
		L"배우 폴더 txt 와 같은 \"항목: 값\" 형식입니다 (스캔 때 읽는 규칙과 같음). 입력한 내용으로 바뀌고, 지운 항목은 비워집니다.\r\n"
		L"항목: 다른이름(# 구분) · 성별 · 생년월일 · 국적 · 키 · 치수 · 가슴/허리/엉덩이 · 컵 · 데뷔 · 은퇴 · URL(한 줄에 하나, 여러 줄)  (이름 · 별점 · 즐겨찾기 · 메모는 바뀌지 않음)",
		CVideoLibrary::ActorInfoText(cur), this);
	if (dlg.DoModal() != IDOK)
		return;

	ActorInfo a = m_lib.actors[idx];
	CVideoLibrary::ClearActorTextFields(a);
	CVideoLibrary::ApplyActorText(a, dlg.m_text);
	m_lib.actors[idx] = a;
	m_lib.CleanupActorAliases();        // 이름과 같은 별칭 정리
	m_lib.NormalizeAllVideoActors();    // 새 별칭으로 적힌 영상 배우 → 배우 이름 + 참여 별칭
	if (!m_lib.Save())
		AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);

	ResetThumbnails();   // 기본 이미지(성별)가 바뀌었을 수 있음
	RebuildActorGrid();
	ShowActorInfo(SelectedActorIndex());
	MarkCategoriesDirty();
}

void CRuliManagerDlg::MergeActorInto(int j, int cur, const CString& past)
{
	// j 배우를 cur 배우로 합침
	//  - 영상: j 배우(이름 · 별칭 표기) → cur 배우, 참여 별칭 = past (비우면 그 영상에서 j 가 쓰던 이름, 없으면 j 의 이름)
	//  - 배우 정보: cur 에 비어 있는 칸만 j 에서 채움, j 의 이름 · 다른 이름은 cur 의 다른 이름으로, j 항목은 삭제
	if (j < 0 || cur < 0 || j == cur || j >= static_cast<int>(m_lib.actors.size()) || cur >= static_cast<int>(m_lib.actors.size()))
		return;
	const CString dstName = m_lib.actors[cur].name;
	const CString srcName = m_lib.actors[j].name;
	// 영상: 그 배우(이름 · 별칭 표기) → 이 배우, 참여 별칭은 과거 이름으로
	for (VideoItem& v : m_lib.items)
	{
		std::vector<CString> list = CVideoLibrary::SplitList(v.actors);
		bool found = false;
		for (CString& n : list)
			if (m_lib.FindActorByAnyName(n) == j) { n = dstName; found = true; }
		if (!found)
			continue;
		std::vector<CString> credited;
		CString srcCredit;   // 그 영상에서 그 배우가 쓰던 이름 (참여 별칭)
		for (const CString& c : CVideoLibrary::SplitList(v.actorAliases))
		{
			const int who = m_lib.FindActorByAnyName(c);
			if (who == j && srcCredit.IsEmpty())
				srcCredit = c;
			if (who != j && who != cur)
				credited.push_back(c);   // 다른 배우의 참여 별칭은 그대로
		}
		const CString credit = !past.IsEmpty() ? past : (!srcCredit.IsEmpty() ? srcCredit : srcName);
		if (!CVideoLibrary::SameNameByLang(credit, dstName))
			credited.push_back(credit);   // 남는 배우 이름과 같은 이름이면 참여 별칭 없이
		v.actors = CVideoLibrary::JoinList(CVideoLibrary::SplitList(CVideoLibrary::JoinList(list)));
		v.actorAliases = CVideoLibrary::JoinList(CVideoLibrary::SplitList(CVideoLibrary::JoinList(credited)));
	}
	// 배우 정보: 비어 있는 칸만 채움
	ActorInfo& dst = m_lib.actors[cur];
	const ActorInfo src = m_lib.actors[j];
	auto fill = [](CString& d, const CString& s) { if (d.IsEmpty() && !s.IsEmpty()) d = s; };
	fill(dst.photo, src.photo);
	fill(dst.gender, src.gender);
	fill(dst.birth, src.birth);
	fill(dst.nationality, src.nationality);
	fill(dst.height, src.height);
	fill(dst.debut, src.debut);
	fill(dst.retire, src.retire);
	fill(dst.bust, src.bust);
	fill(dst.waist, src.waist);
	fill(dst.hip, src.hip);
	fill(dst.cup, src.cup);
	fill(dst.memo, src.memo);
	if (!src.urls.IsEmpty())
	{
		std::vector<CString> urls = CVideoLibrary::SplitUrls(dst.urls);
		for (const CString& u : CVideoLibrary::SplitUrls(src.urls))
			if (std::find_if(urls.begin(), urls.end(), [&](const CString& e) { return e.CompareNoCase(u) == 0; }) == urls.end())
				urls.push_back(u);
		dst.urls = CVideoLibrary::JoinUrls(urls);
	}
	dst.rating = (std::max)(dst.rating, src.rating);
	dst.favorite = dst.favorite || src.favorite;
	// 다른 이름: 그 배우의 이름 · 다른 이름 중 아직 없는 것만 뒤에 (언어별로 같은 이름은 구성요소가 많은 쪽 유지)
	std::vector<CString> al = CVideoLibrary::SplitList(dst.aliases);
	std::vector<CString> incoming = CVideoLibrary::SplitList(src.aliases);
	incoming.insert(incoming.begin(), src.name);
	for (const CString& n : incoming)
	{
		if (CVideoLibrary::SameNameByLang(n, dst.name))
			continue;
		bool done = false;
		for (CString& e : al)
		{
			if (!CVideoLibrary::SameNameByLang(e, n))
				continue;
			if (CVideoLibrary::NameLangCount(n) > CVideoLibrary::NameLangCount(e))
				e = n;
			done = true;
			break;
		}
		if (!done)
			al.push_back(n);
	}
	dst.aliases = CVideoLibrary::JoinList(al);
	m_lib.actors.erase(m_lib.actors.begin() + j);
}

void CRuliManagerDlg::OnActorMerge()
{
	// 배우 카드 오른쪽 클릭 → 다른 배우와 합치기: 고른 배우와 합친 뒤 어느 이름을 남길지 선택
	CommitDetails();
	const int src = SelectedActorIndex();
	if (src < 0 || src >= static_cast<int>(m_lib.actors.size()))
		return;
	const CString srcName = m_lib.actors[src].name;
	CActorPickDlg dlg(m_lib, CString(), this);
	dlg.m_single = true;
	dlg.m_exclude = srcName;
	dlg.m_caption = L"'" + srcName + L"' 와(과) 합칠 배우 선택";
	if (dlg.DoModal() != IDOK)
		return;
	const int dst = m_lib.FindActorByAnyName(dlg.m_result);
	if (dst < 0 || dst == src)
		return;
	const CString dstName = m_lib.actors[dst].name;

	CString msg;
	msg.Format(L"두 배우를 한 사람으로 합칩니다.\n\n  A: %s  (영상 %d개)\n  B: %s  (영상 %d개)\n\n"
		L"[예] = B 로 합침 (B 이름 유지, A 는 B 의 다른 이름)\n[아니요] = A 로 합침 (A 이름 유지, B 는 A 의 다른 이름)\n\n"
		L"합쳐지는 배우의 영상은 남는 배우로 바뀌고(참여 별칭 = 그때 쓰던 이름), 정보는 비어 있는 칸만 가져온 뒤 그 배우 항목은 삭제됩니다.",
		static_cast<LPCWSTR>(srcName), m_lib.CountVideosWithActor(srcName), static_cast<LPCWSTR>(dstName), m_lib.CountVideosWithActor(dstName));
	const int answer = AfxMessageBox(msg, MB_YESNOCANCEL | MB_ICONQUESTION);
	if (answer == IDCANCEL)
		return;
	const CString keep = (answer == IDYES) ? dstName : srcName;
	const CString gone = (answer == IDYES) ? srcName : dstName;
	MergeActorInto(m_lib.FindActor(gone), m_lib.FindActor(keep), CString());
	m_lib.CleanupActorAliases();        // 이름과 같은 별칭 정리
	m_lib.NormalizeAllVideoActors();    // 영상 배우 → 배우 이름 + 참여 별칭
	if (!m_lib.Save())
		AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);

	ResetThumbnails();
	m_actorSelName = keep;   // 남은 배우 선택
	RebuildActorGrid();
	for (size_t row = 0; row < m_actorRows.size(); ++row)
	{
		if (m_actorRows[row] == m_lib.FindActor(keep))
		{
			SelectActorRow(static_cast<int>(row));
			m_actorGrid.EnsureVisible(static_cast<int>(row));
			break;
		}
	}
	ShowActorInfo(SelectedActorIndex());
	MarkCategoriesDirty();
	ApplyFilter();   // 영상의 배우가 바뀜
}

void CRuliManagerDlg::OnActorPasteInfo()
{
	// 배우 탭: 배우 프로필 사이트 화면을 복사한 글 → 읽어낸 항목만 덮어써서 확인 창 → 적용 (영상의 [웹페이지 내용 붙여넣기] 와 같은 흐름)
	if (!IsActorGridMode())
		return;
	const int idx = SelectedActorIndex();
	if (idx < 0 || idx >= static_cast<int>(m_lib.actors.size()))
		return;
	const CString name = m_lib.actors[idx].name;

	// 1단계: 사이트 화면 글 붙여넣기
	CTextInfoDlg dlg(L"웹페이지 내용 붙여넣기 - " + name,
		L"배우 프로필 사이트 화면을 드래그해 복사(Ctrl+C)한 뒤 여기에 붙여넣으세요(Ctrl+V).\r\n"
		L"생년월일 / 키 / 치수(B W H) / 컵 / 국적 / 데뷔 / 은퇴 / 다른 이름, 生年月日 / 身長 / スリーサイズ / カップ / 出身地 / デビュー / 別名 같은 항목을 찾아 읽습니다.",
		CString(), this);
	dlg.m_pasteButton = true;   // [공백라인 제거]
	if (dlg.DoModal() != IDOK)
		return;
	const CString info = CVideoLibrary::ParsePastedActorText(dlg.m_text);
	// "이름: …" 줄 (붙여넣은 글 맨 앞의 ":" 없는 줄들 → "한글(English, 日本語)") - ApplyActorText 는 이름을 읽지 않으므로 따로
	auto nameLineOf = [](const CString& text) -> CString
	{
		CString t = text;
		t.Replace(L"\r\n", L"\n");
		int pos = 0;
		while (pos <= t.GetLength())
		{
			int nl = t.Find(L'\n', pos);
			if (nl < 0) nl = t.GetLength();
			CString l = t.Mid(pos, nl - pos);
			pos = nl + 1;
			l.Trim();
			const int sep = l.FindOneOf(L":\xFF1A");
			if (sep <= 0)
				continue;
			CString k = l.Left(sep);
			k.Trim();
			if (k == L"이름" || k.CompareNoCase(L"name") == 0)
			{
				CString v = l.Mid(sep + 1);
				v.Trim();
				CVideoLibrary::RemoveListCommas(v);   // 구분 쉼표는 이름에 쓸 수 없음 (괄호 안 쉼표는 허용)
				return v;
			}
		}
		return CString();
	};
	const CString pastedName = nameLineOf(info);
	ActorInfo fetched;
	fetched.name = pastedName.IsEmpty() ? name : pastedName;   // 다른 이름이 배우 이름과 같으면 빼기 위해
	const bool anyField = !info.IsEmpty() && CVideoLibrary::ApplyActorText(fetched, info);
	if (!anyField && pastedName.IsEmpty())
	{
		AfxMessageBox(L"붙여넣은 내용에서 배우 정보를 찾지 못했습니다.", MB_ICONINFORMATION);
		return;
	}

	// 읽어낸 항목만 기존 정보에 덮어쓰기 (없는 항목은 그대로, 다른 이름은 합침)
	ActorInfo merged = m_lib.actors[idx];
	if (!pastedName.IsEmpty())
		merged.name = pastedName;   // 확인 창의 "이름:" 줄 (고치면 그 이름으로)
	auto take = [](CString& dst, const CString& src) { if (!src.IsEmpty()) dst = src; };
	take(merged.gender, fetched.gender);
	take(merged.birth, fetched.birth);
	take(merged.nationality, fetched.nationality);
	take(merged.height, fetched.height);
	take(merged.bust, fetched.bust);
	take(merged.waist, fetched.waist);
	take(merged.hip, fetched.hip);
	take(merged.cup, fetched.cup);
	take(merged.debut, fetched.debut);
	take(merged.retire, fetched.retire);
	if (!fetched.aliases.IsEmpty())
	{
		// 다른 이름 (사이트에는 이름 변경 순서 = 오래된 것부터 → ApplyActorText 가 이미 역순으로 읽음: fetched.aliases = 최근 이름부터)
		//  - 배우 관리 창 이름 콤보에 그 순서대로 (현재 이름 다음에 가장 최근 과거 이름)
		//  - 현재 이름과 같은 이름(언어 단위로 같음)은 버림 (변경 내역의 마지막 = 현재 이름)
		//  - 이미 등록된 다른 이름과 같은 이름이면: 구성요소(언어 수)가 더 많은 쪽으로 대체 ("아오이" → "아오이(Aoi, 葵)")
		//  - 사이트에 없는 기존 다른 이름은 뒤에 그대로 남김
		std::vector<CString> existing = CVideoLibrary::SplitList(merged.aliases);
		std::vector<bool> used(existing.size(), false);
		std::vector<CString> result;
		auto sameName = [](const CString& a, const CString& b)
		{
			return a.CompareNoCase(b) == 0 || CVideoLibrary::SameNameByLang(a, b);
		};
		for (const CString& web : CVideoLibrary::SplitList(fetched.aliases))
		{
			if (sameName(web, merged.name))
				continue;   // 현재 이름
			bool dup = false;
			for (const CString& r : result)
				if (sameName(r, web)) { dup = true; break; }
			if (dup)
				continue;
			CString pick = web;
			for (size_t e = 0; e < existing.size(); ++e)
			{
				if (used[e] || !sameName(existing[e], web))
					continue;
				used[e] = true;
				if (CVideoLibrary::NameLangCount(existing[e]) >= CVideoLibrary::NameLangCount(web))
					pick = existing[e];   // 기존 이름이 같거나 더 완전하면 그대로
			}
			result.push_back(pick);
		}
		for (size_t e = 0; e < existing.size(); ++e)
		{
			if (used[e] || sameName(existing[e], merged.name))
				continue;
			bool dup = false;
			for (const CString& r : result)
				if (sameName(r, existing[e])) { dup = true; break; }
			if (!dup)
				result.push_back(existing[e]);
		}
		merged.aliases = CVideoLibrary::JoinList(result);
	}

	// 2단계: 확인 / 수정 후 적용 (텍스트로 정보 입력과 같은 형식 · 같은 적용)
	CTextInfoDlg dlg2(L"읽어낸 정보 확인 - " + name,
		L"붙여넣은 내용에서 읽어낸 결과입니다. 확인하고 필요하면 고친 뒤 확인을 누르면 적용됩니다. (\"이름:\" 을 바꾸면 배우 이름도 바뀜, 별점 · 즐겨찾기 · 메모는 그대로)",
		CVideoLibrary::ActorInfoText(merged), this);
	if (dlg2.DoModal() != IDOK)
		return;
	ActorInfo a = m_lib.actors[idx];
	CVideoLibrary::ClearActorTextFields(a);
	CVideoLibrary::ApplyActorText(a, dlg2.m_text);
	// 이름이 바뀌었으면 배우 이름 변경 (영상의 배우 이름도, 예전 이름이 언어 단위로도 다르면 다른 이름으로 남김)
	{
		const CString newName = nameLineOf(dlg2.m_text);
		if (!newName.IsEmpty() && newName != a.name)
		{
			const int other = m_lib.FindActor(newName);
			if (other >= 0 && other != idx)
				AfxMessageBox(L"같은 이름의 배우가 이미 있어 이름은 바꾸지 않았습니다.\n\n" + newName, MB_ICONWARNING);
			else
			{
				const CString oldName = a.name;
				m_lib.RenameActorInVideos(oldName, newName);
				a.name = newName;
				if (!CVideoLibrary::SameNameByLang(oldName, newName))
				{
					std::vector<CString> al = CVideoLibrary::SplitList(a.aliases);
					al.push_back(oldName);
					a.aliases = CVideoLibrary::JoinList(al);
				}
			}
		}
	}
	m_lib.actors[idx] = a;

	// 과거 이름(다른 이름)과 언어별 이름이 같은 다른 배우가 따로 등록되어 있으면 → 이 배우로 병합
	//  - 그 배우로 등록된 영상은 이 배우 + 참여 별칭 = 그 과거 이름 (마지막 이름이 아니라 그때 쓰던 이름으로)
	//  - 그 배우의 정보는 이 배우에 비어 있는 칸만 채우고, 다른 이름은 합친 뒤 그 배우 항목은 삭제
	bool mergedAny = false;
	{
		struct MergeItem { CString other; CString past; };
		std::vector<MergeItem> merges;
		for (const CString& past : CVideoLibrary::SplitList(a.aliases))
		{
			for (size_t j = 0; j < m_lib.actors.size(); ++j)
			{
				if (static_cast<int>(j) == idx)
					continue;
				const ActorInfo& o = m_lib.actors[j];
				bool similar = CVideoLibrary::SameNameByLang(o.name, past);
				for (const CString& oa : CVideoLibrary::SplitList(o.aliases))
					if (!similar && CVideoLibrary::SameNameByLang(oa, past)) similar = true;
				if (!similar)
					continue;
				bool dup = false;
				for (const MergeItem& m : merges)
					if (m.other.CompareNoCase(o.name) == 0) { dup = true; break; }
				if (!dup)
					merges.push_back({ o.name, past });
			}
		}
		if (!merges.empty())
		{
			CString msg;
			msg.Format(L"과거 이름과 같은 배우가 따로 등록되어 있습니다. '%s'(으)로 병합할까요?\n\n", static_cast<LPCWSTR>(a.name));
			for (const MergeItem& m : merges)
			{
				CString line;
				line.Format(L"  '%s' → 과거 이름 '%s'  (영상 %d개)\n", static_cast<LPCWSTR>(m.other), static_cast<LPCWSTR>(m.past), m_lib.CountVideosWithActor(m.other));
				msg += line;
			}
			msg += L"\n병합하면 그 배우의 영상은 이 배우(참여 별칭 = 과거 이름)로 바뀌고, 그 배우 정보는 비어 있는 칸만 가져온 뒤 삭제됩니다.";
			if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION) != IDYES)
				merges.clear();
		}
		for (const MergeItem& m : merges)
		{
			const int j = m_lib.FindActor(m.other);
			const int cur = m_lib.FindActor(a.name);
			if (j < 0 || cur < 0 || j == cur)
				continue;
			MergeActorInto(j, cur, m.past);
			mergedAny = true;
		}
	}
	m_lib.CleanupActorAliases();        // 이름과 같은 별칭 정리
	m_lib.NormalizeAllVideoActors();    // 새 별칭으로 적힌 영상 배우 → 배우 이름 + 참여 별칭
	if (!m_lib.Save())
		AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);

	ResetThumbnails();   // 기본 이미지(성별)가 바뀌었을 수 있음
	RebuildActorGrid();
	ShowActorInfo(SelectedActorIndex());
	MarkCategoriesDirty();
	if (mergedAny)
		ApplyFilter();   // 병합으로 영상의 배우가 바뀜
}

void CRuliManagerDlg::OnActorDelete()
{
	// 배우 탭(배우 격자): 선택한 배우를 배우 목록과 영상에서 삭제 (영상 파일·이미지 원본은 그대로)
	if (!IsActorGridMode())
		return;
	const int idx = SelectedActorIndex();
	if (idx < 0 || idx >= static_cast<int>(m_lib.actors.size()))
		return;
	if (IsActorChecked(idx))
	{
		OnActorDeleteChecked();   // 선택한 배우가 체크되어 있으면 체크한 배우 모두 삭제 (메뉴 "배우 삭제(건수)" 와 같음)
		return;
	}
	const CString name = m_lib.actors[idx].name;
	const int count = m_lib.CountVideosWithActor(name);
	CString msg;
	if (count > 0)
		msg.Format(L"배우 '%s'을(를) 삭제할까요?\n\n이 배우가 연결된 동영상 %d개에서도 빠집니다.", static_cast<LPCWSTR>(name), count);
	else
		msg.Format(L"배우 '%s'을(를) 삭제할까요?", static_cast<LPCWSTR>(name));
	if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;

	// 삭제 후에는 옆 배우를 선택 (목록 순서 기준)
	const int row = m_actorSel;
	m_lib.RemoveActorFromVideos(name);
	m_lib.actors.erase(m_lib.actors.begin() + idx);
	m_lib.Save();

	m_actorSelName.Empty();
	RebuildActorGrid();
	if (!m_actorRows.empty())
	{
		const int next = (std::min)((std::max)(0, row), static_cast<int>(m_actorRows.size()) - 1);
		SelectActorRow(next);
	}
	else
		ShowActorInfo(-1);
	m_catsDirty = true;   // 다른 탭의 개수도 다시 계산
	UpdateStatus();
}

void CRuliManagerDlg::OnActorDeleteChecked()
{
	// 체크한 배우를 배우 목록과 영상에서 모두 삭제 (영상 파일 · 이미지 원본은 그대로)
	if (!IsActorGridMode())
		return;
	const std::vector<int> list = CheckedActorIndices();
	if (list.empty())
		return;
	std::vector<CString> names;
	int videos = 0;
	for (int i : list)
	{
		names.push_back(m_lib.actors[i].name);
		videos += m_lib.CountVideosWithActor(m_lib.actors[i].name);
	}
	CString msg;
	msg.Format(L"체크한 배우 %d명을 삭제할까요?\n\n", static_cast<int>(names.size()));
	for (size_t k = 0; k < names.size() && k < 15; ++k)
		msg += L"  " + names[k] + L"\n";
	if (names.size() > 15)
	{
		CString more;
		more.Format(L"  … 외 %d명\n", static_cast<int>(names.size() - 15));
		msg += more;
	}
	if (videos > 0)
	{
		CString v;
		v.Format(L"\n이 배우들이 연결된 동영상(연결 %d건)에서도 빠집니다.", videos);
		msg += v;
	}
	if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;

	CommitDetails();
	const int row = m_actorSel;
	for (const CString& n : names)
	{
		m_lib.RemoveActorFromVideos(n);
		const int i = m_lib.FindActor(n);
		if (i >= 0)
			m_lib.actors.erase(m_lib.actors.begin() + i);
	}
	m_actorChecked.clear();
	if (!m_lib.Save())
		AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);

	m_actorSelName.Empty();
	RebuildActorGrid();
	if (!m_actorRows.empty())
	{
		const int next = (std::min)((std::max)(0, row), static_cast<int>(m_actorRows.size()) - 1);
		SelectActorRow(next);
	}
	else
		ShowActorInfo(-1);
	m_catsDirty = true;   // 다른 탭의 개수도 다시 계산
	ApplyFilter();        // 영상의 배우가 바뀜
	UpdateStatus();
}

void CRuliManagerDlg::OnActorMergeChecked()
{
	// 체크한 배우 병합: 오른쪽 클릭한(선택한) 배우 기준 - 나머지 체크한 배우는 그 배우의 다른 이름으로 (영상 · 정보도 합침, 항목은 삭제)
	if (!IsActorGridMode())
		return;
	const int target = SelectedActorIndex();
	if (target < 0 || target >= static_cast<int>(m_lib.actors.size()) || !IsActorChecked(target))
		return;
	std::vector<CString> others;
	for (int i : CheckedActorIndices())
		if (i != target)
			others.push_back(m_lib.actors[i].name);
	if (others.empty())
		return;
	const CString keep = m_lib.actors[target].name;
	CString msg;
	msg.Format(L"체크한 배우 %d명을 '%s'(으)로 병합할까요?\n\n", static_cast<int>(others.size()), static_cast<LPCWSTR>(keep));
	for (size_t k = 0; k < others.size() && k < 15; ++k)
	{
		CString line;
		line.Format(L"  %s  (영상 %d개)\n", static_cast<LPCWSTR>(others[k]), m_lib.CountVideosWithActor(others[k]));
		msg += line;
	}
	if (others.size() > 15)
	{
		CString more;
		more.Format(L"  … 외 %d명\n", static_cast<int>(others.size() - 15));
		msg += more;
	}
	msg += L"\n병합되는 배우의 이름 · 다른 이름은 '" + keep + L"' 의 다른 이름으로 추가되고, 그 영상은 '" + keep + L"'(참여 별칭 = 그때 쓰던 이름)로 바뀝니다. "
		L"정보는 비어 있는 칸만 가져온 뒤 그 배우 항목은 삭제됩니다.";
	if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;

	CommitDetails();
	for (const CString& n : others)
		MergeActorInto(m_lib.FindActor(n), m_lib.FindActor(keep), CString());
	m_actorChecked.clear();
	m_lib.CleanupActorAliases();        // 이름과 같은 별칭 정리
	m_lib.NormalizeAllVideoActors();    // 영상 배우 → 배우 이름 + 참여 별칭
	if (!m_lib.Save())
		AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);

	ResetThumbnails();
	m_actorSelName = keep;   // 남은 배우 선택
	RebuildActorGrid();
	const int keepIdx = m_lib.FindActor(keep);
	for (size_t row = 0; row < m_actorRows.size(); ++row)
	{
		if (m_actorRows[row] == keepIdx)
		{
			SelectActorRow(static_cast<int>(row));
			m_actorGrid.EnsureVisible(static_cast<int>(row));
			break;
		}
	}
	ShowActorInfo(SelectedActorIndex());
	MarkCategoriesDirty();
	ApplyFilter();   // 영상의 배우가 바뀜
}

void CRuliManagerDlg::OnActorUncheckAll()
{
	m_actorChecked.clear();
	if (m_actorGrid.GetSafeHwnd())
		m_actorGrid.Invalidate(FALSE);
}

void CRuliManagerDlg::OnCatDelete()
{
	// 선택한 카드가 체크되어 있으면 체크한 항목 모두 삭제 (메뉴 "삭제(건수)" 와 같음)
	if (CatChecksEnabled() && (m_catSelKind == CAT_VALUE || m_catSelKind == CAT_LABEL) && !m_catSelValue.IsEmpty()
		&& IsCatChecked(m_catSelKind, m_catSelValue))
	{
		OnCatDeleteChecked();
		return;
	}
	// 컬렉션 탭: 선택한 컬렉션 삭제 (영상은 컬렉션에서만 빠짐, 파일 · 정보는 그대로)
	if (m_mode == MODE_COLLECTION && m_catSelKind == CAT_VALUE && !m_catSelValue.IsEmpty())
	{
		const CString name = m_catSelValue;
		int count = 0;
		for (const VideoItem& v : m_lib.items)
			if (m_lib.InCollection(v, name))
				++count;
		CString msg;
		msg.Format(L"컬렉션 '%s'을(를) 삭제할까요?\n\n들어 있는 영상 %d개는 컬렉션에서만 빠집니다 (영상 파일 · 정보는 그대로).",
			static_cast<LPCWSTR>(name), count);
		if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
			return;
		m_lib.DeleteCollection(name);
		m_lib.Save();
		m_catSelKind = -1;
		m_catSelValue.Empty();
		RebuildCategories();
		UpdateStatus();
		return;
	}
	// 태그 탭: 선택한 태그를 목록과 영상에서 삭제 (영상 파일은 그대로)
	if (m_mode != MODE_TAG || m_catSelKind != CAT_VALUE || m_catSelValue.IsEmpty())
		return;
	const CString tag = m_catSelValue;
	const std::map<CString, int> counts = m_lib.CountNamed(LIST_TAG);
	CString key = tag;
	key.MakeLower();
	auto it = counts.find(key);
	const int count = (it != counts.end()) ? it->second : 0;

	CString msg;
	if (count > 0)
		msg.Format(L"태그 '%s'을(를) 삭제할까요?\n\n연결된 동영상 %d개에서도 빠집니다.", static_cast<LPCWSTR>(tag), count);
	else
		msg.Format(L"태그 '%s'을(를) 삭제할까요?", static_cast<LPCWSTR>(tag));
	if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;

	m_lib.RemoveNamedFromVideos(LIST_TAG, tag);
	const int n = m_lib.FindNamed(LIST_TAG, tag);
	if (n >= 0)
		m_lib.tagInfos.erase(m_lib.tagInfos.begin() + n);
	m_lib.Save();

	m_catSelKind = -1;
	m_catSelValue.Empty();
	RebuildCategories();
	UpdateStatus();
}

void CRuliManagerDlg::OnCatMerge()
{
	// 태그 탭: 선택한 태그(원래)를 고른 태그(대상)에 병합 - 영상의 원래 태그는 대상 태그로 바뀌고, 원래 태그는 목록에서 삭제
	if (m_mode != MODE_TAG || m_catSelKind != CAT_VALUE || m_catSelValue.IsEmpty())
		return;
	const CString src = m_catSelValue;
	CNamePickDlg dlg(m_lib, LIST_TAG, CString(), this);
	dlg.m_single = true;
	dlg.m_exclude = src;
	dlg.m_caption = L"'" + src + L"' 태그를 병합할 태그 선택";
	if (dlg.DoModal() != IDOK)
		return;
	CString dst = dlg.m_result;
	dst.Trim();
	{
		const int d = m_lib.FindNamed(LIST_TAG, dst);   // 다른 이름으로 골라도 태그 이름으로
		if (d >= 0)
			dst = m_lib.tagInfos[d].name;
	}
	if (dst.IsEmpty() || dst.CompareNoCase(src) == 0)
		return;

	const std::map<CString, int> counts = m_lib.CountNamed(LIST_TAG);
	CString key = src;
	key.MakeLower();
	auto it = counts.find(key);
	const int count = (it != counts.end()) ? it->second : 0;
	CString msg;
	msg.Format(L"태그 '%s'을(를) '%s'에 병합할까요?\n\n동영상 %d개의 '%s' 태그가 '%s'(으)로 바뀌고, '%s'은(는) 태그 목록에서 삭제됩니다.",
		static_cast<LPCWSTR>(src), static_cast<LPCWSTR>(dst), count, static_cast<LPCWSTR>(src), static_cast<LPCWSTR>(dst), static_cast<LPCWSTR>(src));
	if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;

	CommitDetails();
	m_lib.RenameNamedInVideos(LIST_TAG, src, dst);   // 영상의 태그 바꿈 (이미 대상 태그가 있으면 하나로)
	const int si = m_lib.FindNamed(LIST_TAG, src);
	const int di = m_lib.FindNamed(LIST_TAG, dst);
	if (si >= 0 && di < 0)
		m_lib.tagInfos[si].name = dst;   // 대상이 목록에 없으면 원래 항목 이름만 바꿈 (즐겨찾기 유지)
	else if (si >= 0)
	{
		if (m_lib.tagInfos[si].favorite)
			m_lib.tagInfos[di].favorite = true;   // 원래 태그가 즐겨찾기면 대상도
		m_lib.tagInfos.erase(m_lib.tagInfos.begin() + si);
	}
	else if (di < 0)
	{
		NamedInfo info;
		info.name = dst;
		m_lib.tagInfos.push_back(info);
	}
	m_lib.Save();

	m_catSelKind = CAT_VALUE;
	m_catSelValue = dst;   // 병합한 태그 선택
	RebuildCategories();
	if (m_curItem >= 0)
		ShowDetails(m_curItem);
	UpdateStatus();
}

void CRuliManagerDlg::ToggleTagFavorite(const CString& tag)
{
	if (tag.IsEmpty())
		return;
	int n = m_lib.FindNamed(LIST_TAG, tag);
	if (n < 0)
	{
		// 영상에만 쓰이고 태그 목록에 없던 태그 → 목록에 추가해서 즐겨찾기 저장
		NamedInfo info;
		info.name = tag;
		m_lib.tagInfos.push_back(info);
		n = static_cast<int>(m_lib.tagInfos.size()) - 1;
	}
	m_lib.tagInfos[n].favorite = !m_lib.tagInfos[n].favorite;
	m_lib.Save();
	m_catSelKind = CAT_VALUE;
	m_catSelValue = tag;
	RebuildCategories();   // 즐겨찾기가 앞으로 오도록 다시 정렬 (선택은 이름으로 유지)
}

bool CRuliManagerDlg::CatGridOwner::GridDrawCard(CDC* dc, int row, const CRect& card, bool selected, bool focused)
{
	if (dlg->m_mode != MODE_STUDIO && !dlg->TagLikeMode())
		return false;
	dlg->DrawStudioCard(dc, row, card, selected, focused);   // 스튜디오 / 태그 / 컬렉션 카드
	// 왼쪽 위 체크 칸 (배우 카드와 같음): 체크했으면 항상, 아니면 그 자리에 마우스가 있을 때만 빈 칸
	if (dlg->CatChecksEnabled() && row >= 0 && row < static_cast<int>(dlg->m_catRows.size()))
	{
		const CatRow& r = dlg->m_catRows[row];
		if (r.kind == CAT_VALUE || r.kind == CAT_LABEL)
		{
			const bool checked = dlg->IsCatChecked(r.kind, r.value);
			if (checked || (dlg->m_catGrid.HotRow() == row && dlg->m_catGrid.HotPart() == 2))
				DrawCardCheck(dc, dlg->ActorCheckRect(card), checked);
		}
	}
	return true;
}

namespace
{
	// 큰 꼬리표 아이콘 (왼쪽이 뾰족, 둥근 모서리, 구멍 하나) - 태그 카드
	void DrawBigTagIcon(CDC* dc, const CRect& area, COLORREF col, COLORREF hole)
	{
		(void)hole;
		VectorIcon::Tag(dc, area, col, 0.85);   // 태그 카드 기본 이미지
	}

	// 비디오 카메라 아이콘 (둥근 몸체 + 오른쪽 렌즈)
	void DrawCameraIcon(CDC* dc, const CRect& area, COLORREF col)
	{
		VectorIcon::Camera(dc, area, col, 0.87);   // 스튜디오 카드 기본 이미지 (원본 SVG 처럼 폭 = 높이의 약 80%)
	}
}

void CRuliManagerDlg::DrawTagPopupCard(CDC* dc, const CRect& outer, const CString& tag)
{
	// 태그 칩 팝업: 태그 탭 카드와 같은 모양 (꼬리표 아이콘 · 이름 (영어, 일본어) · 구분선 · ▶ 영상 수, 즐겨찾기 하트)
	CRect rc = outer;
	rc.DeflateRect(1, 1);
	const int radius = DX(4);
	{
		CBrush br(kCardBack);
		CPen pen(PS_SOLID, 1, TCOL(TC_BORDER));
		CBrush* ob = dc->SelectObject(&br);
		CPen* op = dc->SelectObject(&pen);
		dc->RoundRect(rc, CPoint(radius, radius));
		dc->SelectObject(ob);
		dc->SelectObject(op);
	}
	const int w = rc.Width();
	const CRect area(rc.left + m_cardPad * 2, rc.top + m_cardPad * 2, rc.right - m_cardPad * 2,
		rc.top + m_cardPad * 2 + (w - m_cardPad * 2) * 9 / 16 - m_cardPad * 2);
	DrawBigTagIcon(dc, area, RGB(255, 255, 255), kCardBack);
	if (IsTagFavorite(tag))
		DrawHeart(dc, ActorHeartRect(rc), RGB(0xF2, 0x5C, 0x54), 255, true);

	CFont* normal = GetFont();
	CFont* oldFont = dc->SelectObject(&m_cardNameFont);
	dc->SetBkMode(TRANSPARENT);
	const int x = rc.left + m_cardPad * 2;
	const int right = rc.right - m_cardPad * 2;
	int y = rc.top + m_cardPad * 2 + (w - m_cardPad * 2) * 9 / 16;

	// 이름 (굵게) + 회색 " (영어, 일본어)"
	CString name = tag, extra;
	const int ti = m_lib.FindNamed(LIST_TAG, tag);
	if (ti >= 0)
	{
		const NamedInfo& tg = m_lib.tagInfos[ti];
		name = tg.name;
		CString parts = tg.nameEn;
		if (!tg.nameJa.IsEmpty())
			parts += (parts.IsEmpty() ? L"" : L", ") + tg.nameJa;
		if (!parts.IsEmpty())
			extra = L" (" + parts + L")";
	}
	dc->SetTextColor(kTextColor);
	TextFB::Draw(dc, name, CRect(x, y, right, y + m_cardLineB), DT_LEFT, true);
	if (!extra.IsEmpty())
	{
		const int nw = TextFB::Width(dc, name);
		if (x + nw < right)
		{
			dc->SelectObject(normal);
			dc->SetTextColor(kCardSub);
			TextFB::Draw(dc, extra, CRect(x + nw, y, right, y + m_cardLineB), DT_LEFT, true);
		}
	}
	y += m_cardLineB + m_cardPad / 2;

	// 구분선
	dc->FillSolidRect(rc.left + m_cardPad, y, rc.Width() - m_cardPad * 2, 1, kCardLine);
	y += 1 + m_cardPad;

	// ▶ 영상 수 (가운데)
	dc->SelectObject(normal);
	int videos = 0;
	{
		for (const VideoItem& v : m_lib.items)
			for (const CString& t : CVideoLibrary::SplitList(v.tags))
				if (t.CompareNoCase(name) == 0) { ++videos; break; }
	}
	CString vText;
	vText.Format(L"%d", videos);
	const int iconR = (std::max)(4, m_cardLine * 2 / 5);
	const int gapIcon = DX(3);
	const int total = iconR * 2 + gapIcon + dc->GetTextExtent(vText).cx;
	int sx = rc.left + (rc.Width() - total) / 2;
	const int cy = y + m_cardLine / 2;
	dc->SetTextColor(kTextColor);
	DrawPlayIcon(dc, sx + iconR, cy, iconR, kCardIcon, kCardBack);
	sx += iconR * 2 + gapIcon;
	dc->TextOut(sx, y, vText);
	dc->SelectObject(oldFont);
}

// 제작사 레벨: 하위 제작사가 없는 제작사 = 0, 상위 제작사 = 1 + 하위 제작사 중 가장 큰 레벨
//  → 맨 아래 제작사는 늘 기본색, 하위 아래에 하위가 생기면 위쪽 레벨이 모두 다시 계산됨
void CRuliManagerDlg::ComputeStudioLevels()
{
	m_studioLevel.clear();
	std::map<CString, std::vector<CString>> kids;   // 상위(소문자) → 하위(소문자)
	for (const NamedInfo& s : m_lib.studios)
	{
		CString p = s.parent;
		p.Trim();
		if (p.IsEmpty() || m_lib.FindNamed(LIST_STUDIO, p) < 0)
			continue;
		CString pk = m_lib.studios[m_lib.FindNamed(LIST_STUDIO, p)].name, k = s.name;
		pk.MakeLower();
		k.MakeLower();
		if (pk != k)
			kids[pk].push_back(k);
	}
	std::set<CString> busy;   // 순환 연결 방지
	std::function<int(const CString&)> level = [&](const CString& k) -> int
	{
		auto it = m_studioLevel.find(k);
		if (it != m_studioLevel.end())
			return it->second;
		if (!busy.insert(k).second)
			return 0;
		int lv = 0;
		auto kit = kids.find(k);
		if (kit != kids.end())
			for (const CString& c : kit->second)
				lv = (std::max)(lv, level(c) + 1);
		busy.erase(k);
		m_studioLevel[k] = lv;
		return lv;
	};
	for (const NamedInfo& s : m_lib.studios)
	{
		CString k = s.name;
		k.MakeLower();
		level(k);
	}
}

COLORREF CRuliManagerDlg::StudioLevelBack(const CString& studioName) const
{
	const COLORREF base = TCOL(TC_CARD);
	CString k = studioName;
	k.MakeLower();
	auto it = m_studioLevel.find(k);
	const int lv = (it == m_studioLevel.end()) ? 0 : it->second;
	if (lv <= 0)
		return base;
	// 레벨 1 파랑 · 2 초록 · 3 주황 · 4 분홍 (레이블 보라와 겹치지 않게), 5 부터 다시 - 기본 카드색에 섞어서 테마와 어울리게
	static const COLORREF hues[] = { RGB(0x4A, 0x8F, 0xD9), RGB(0x3F, 0xB5, 0x8A), RGB(0xD9, 0x9A, 0x3A), RGB(0xD9, 0x5A, 0x7A) };
	const COLORREF h = hues[(lv - 1) % 4];
	const int t = 30;   // 섞는 비율 (%)
	return RGB((GetRValue(base) * (100 - t) + GetRValue(h) * t) / 100,
		(GetGValue(base) * (100 - t) + GetGValue(h) * t) / 100,
		(GetBValue(base) * (100 - t) + GetBValue(h) * t) / 100);
}

void CRuliManagerDlg::DrawStudioCard(CDC* dc, int row, const CRect& rc, bool selected, bool focused)
{
	// 제작사 탭: 카드 폭에 비례한 글자 · 여백 (SetupActorCards), 태그 · 컬렉션: 기본 값
	const bool scaledCard = (m_mode == MODE_STUDIO && m_scardNameFont.GetSafeHandle() && m_scardTextFont.GetSafeHandle());
	const int cPad = scaledCard ? m_scardPad : m_cardPad;
	const int cLineB = scaledCard ? m_scardLineB : m_cardLineB;
	const int cLine = scaledCard ? m_scardLine : m_cardLine;
	CFont* nameFont = scaledCard ? &m_scardNameFont : &m_cardNameFont;
	CFont* textFont = scaledCard ? &m_scardTextFont : GetFont();
	if (row < 0 || row >= static_cast<int>(m_catRows.size()))
		return;
	const CString name = m_catRows[row].value;
	const bool isLabelCard = (m_catRows[row].kind == CAT_LABEL);
	const int radius = DX(4);
	// 레이블 카드는 배경색을 달리 (제작사 #30404D, 레이블 보라빛 #3E3756), 제작사는 레벨마다 색 (맨 아래 = 기본색)
	const COLORREF cardBack = isLabelCard ? TCOL(TC_LABELCARD) : (m_mode == MODE_STUDIO ? StudioLevelBack(name) : kCardBack);

	// 카드 배경
	{
		CBrush br(cardBack);
		CPen pen(PS_SOLID, 1, cardBack);
		CBrush* ob = dc->SelectObject(&br);
		CPen* op = dc->SelectObject(&pen);
		dc->RoundRect(rc, CPoint(radius, radius));
		dc->SelectObject(ob);
		dc->SelectObject(op);
	}

	// 로고 영역: 이미지가 있으면 비율 유지해서 가운데, 없으면 카메라 아이콘
	const CRect area(rc.left + cPad * 2, rc.top + cPad * 2, rc.right - cPad * 2,
		rc.top + cPad * 2 + m_scardLogoH - cPad * 2);
	const bool isTag = (m_mode == MODE_TAG);
	const bool isCol = (m_mode == MODE_COLLECTION);
	const int n = (isTag || isCol || isLabelCard) ? -1 : m_lib.FindNamed(LIST_STUDIO, name);
	const int ln = isLabelCard ? m_lib.FindLabel(name) : -1;
	Gdiplus::Bitmap* logo = (n >= 0) ? GetStudioLogo(m_lib.studios[n].image)
		: ((ln >= 0) ? GetStudioLogo(m_lib.labelInfos[ln].image) : nullptr);
	if (isTag)
	{
		DrawBigTagIcon(dc, area, RGB(255, 255, 255), cardBack);   // 태그: 흰 꼬리표
		// 즐겨찾기: 오른쪽 위 하트 (지정 = 빨간 하트, 하트 자리에 마우스 오버 = 반투명 회색 하트, 누르면 전환)
		const CRect heart = ActorHeartRect(rc);
		if (IsTagFavorite(name))
			DrawHeart(dc, heart, RGB(0xF2, 0x5C, 0x54), 255, true);
		else if (m_catGrid.HotRow() == row && m_catGrid.HotPart() == 1)
			DrawHeart(dc, heart, RGB(0xC8, 0xCC, 0xD0), 170, true);
	}
	else if (isCol)
	{
		DrawCollectionCover(dc, name, area, cardBack);   // 컬렉션: 대표 영상 커버 (없으면 컬렉션 아이콘)
		const CRect heart = ActorHeartRect(rc);
		if (IsCollectionFavorite(name))
			DrawHeart(dc, heart, RGB(0xF2, 0x5C, 0x54), 255, true);
		else if (m_catGrid.HotRow() == row && m_catGrid.HotPart() == 1)
			DrawHeart(dc, heart, RGB(0xC8, 0xCC, 0xD0), 170, true);
	}
	else if (logo && logo->GetWidth() > 0 && logo->GetHeight() > 0)
	{
		const double lw = logo->GetWidth(), lh = logo->GetHeight();
		const double scale = (std::min)(area.Width() / lw, area.Height() / lh);
		const int w = (std::max)(1, static_cast<int>(lw * scale));
		const int h = (std::max)(1, static_cast<int>(lh * scale));
		Gdiplus::Graphics g(dc->GetSafeHdc());
		g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
		g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
		g.DrawImage(logo, area.left + (area.Width() - w) / 2, area.top + (area.Height() - h) / 2, w, h);
	}
	else
	{
		DrawCameraIcon(dc, area, RGB(255, 255, 255));
	}
	// 하위 제작사: 로고 영역 왼쪽 위에 상위 제작사 이름 (작은 칩 "▲ 상위")
	if (n >= 0 && !m_lib.studios[n].parent.IsEmpty() && m_lib.FindNamed(LIST_STUDIO, m_lib.studios[n].parent) >= 0)
	{
		CFont* of = dc->SelectObject(textFont);
		const CString pt = L"▲ " + m_lib.studios[n].parent;
		TEXTMETRIC tm = {};
		dc->GetTextMetrics(&tm);
		const int padX = DX(4);
		const int cw = (std::min)(static_cast<int>(rc.Width()) - cPad * 2, TextFB::Width(dc, pt) + padX * 2);
		const CRect chip(rc.left + cPad, rc.top + cPad, rc.left + cPad + cw, rc.top + cPad + tm.tmHeight + DX(2));
		CBrush br(TCOL(TC_COMBO));
		CPen pen(PS_SOLID, 1, TCOL(TC_COMBO));
		CBrush* ob = dc->SelectObject(&br);
		CPen* op = dc->SelectObject(&pen);
		dc->RoundRect(chip, CPoint(DX(4), DX(4)));
		dc->SelectObject(ob);
		dc->SelectObject(op);
		dc->SetBkMode(TRANSPARENT);
		dc->SetTextColor(kCardSub);
		TextFB::Draw(dc, pt, CRect(chip.left + padX, chip.top, chip.right - padX, chip.bottom), DT_LEFT, true);
		dc->SelectObject(of);
	}

	CFont* normal = textFont;
	CFont* oldFont = dc->SelectObject(nameFont);
	dc->SetBkMode(TRANSPARENT);
	const int x = rc.left + cPad * 2;
	const int right = rc.right - cPad * 2;
	int y = rc.top + cPad * 2 + m_scardLogoH;

	// 이름 (굵게, 한 줄) - 서브이름은 카드에 표시하지 않음
	//  태그: "한글 (영어, 일본어)" - 괄호 부분은 회색 보통 글꼴 (있는 것만)
	CString extra;
	if (isTag)
	{
		const int ti = m_lib.FindNamed(LIST_TAG, name);
		if (ti >= 0)
		{
			const NamedInfo& tg = m_lib.tagInfos[ti];
			CString parts = tg.nameEn;
			if (!tg.nameJa.IsEmpty())
				parts += (parts.IsEmpty() ? L"" : L", ") + tg.nameJa;
			if (!parts.IsEmpty())
				extra = L" (" + parts + L")";
		}
	}
	dc->SetTextColor(kTextColor);
	CRect nr(x, y, right, y + cLineB);
	TextFB::Draw(dc, name, nr, DT_LEFT, true);
	if (!extra.IsEmpty())
	{
		const int nw = TextFB::Width(dc, name);
		if (x + nw < right)
		{
			dc->SelectObject(normal);
			dc->SetTextColor(kCardSub);
			TextFB::Draw(dc, extra, CRect(x + nw, y, right, y + cLineB), DT_LEFT, true);
			dc->SelectObject(nameFont);
		}
	}
	y += cLineB + cPad / 2;

	// 구분선
	dc->FillSolidRect(rc.left + cPad, y, rc.Width() - cPad * 2, 1, kCardLine);
	y += 1 + cPad;

	// ▶ 영상 수 · 배우 수 (가운데)
	dc->SelectObject(normal);
	const int videos = _wtoi(m_listCat.GetItemText(row, 1));
	CString key = name;
	key.MakeLower();
	const std::map<CString, int>& actorCounts = isLabelCard ? m_labelActorCounts : m_studioActorCounts;
	auto ita = actorCounts.find(key);
	const int actors = (ita != actorCounts.end()) ? ita->second : 0;
	CString vText, aText;
	vText.Format(L"%d", videos);
	aText.Format(L"%d", actors);
	const int iconR = (std::max)(4, cLine * 2 / 5);
	const int gapIcon = DX(3), gapGroup = DX(10);
	int total = iconR * 2 + gapIcon + dc->GetTextExtent(vText).cx;
	if (!isTag && !isCol)   // 태그 · 컬렉션 카드는 영상 수만
		total += gapGroup + iconR * 2 + gapIcon + dc->GetTextExtent(aText).cx;
	int sx = rc.left + (rc.Width() - total) / 2;
	const int cy = y + cLine / 2;
	dc->SetTextColor(kTextColor);
	HotCountBack(dc, m_catGrid, sx, iconR * 2 + gapIcon + dc->GetTextExtent(vText).cx, y, cLine, cPad);   // 마우스 오버 = 배경
	DrawPlayIcon(dc, sx + iconR, cy, iconR, kCardIcon, cardBack);
	sx += iconR * 2 + gapIcon;
	dc->TextOut(sx, y, vText);
	if (!isTag && !isCol)
	{
		sx += dc->GetTextExtent(vText).cx + gapGroup;
		HotCountBack(dc, m_catGrid, sx, iconR * 2 + gapIcon + dc->GetTextExtent(aText).cx, y, cLine, cPad);
		DrawPersonIcon(dc, sx + iconR, cy, iconR, kCardIcon);
		sx += iconR * 2 + gapIcon;
		dc->TextOut(sx, y, aText);
	}

	// 선택 테두리
	if (selected)
	{
		CPen pen(PS_SOLID, 2, focused ? kButtonColor : TCOL(TC_BORDER));
		CPen* op = dc->SelectObject(&pen);
		CBrush* ob = static_cast<CBrush*>(dc->SelectStockObject(NULL_BRUSH));
		CRect sr = rc;
		sr.DeflateRect(1, 1);
		dc->RoundRect(sr, CPoint(radius, radius));
		dc->SelectObject(op);
		dc->SelectObject(ob);
	}
	dc->SelectObject(oldFont);
}

void CRuliManagerDlg::SetNamedMemoTarget(int kind, const CString& name)
{
	if (!m_editNamedMemo.GetSafeHwnd())
		return;
	CommitNamedMemo();
	const bool relayout = ((m_memoKind != 0) != (kind != 0));
	m_memoKind = kind;
	m_memoName = name;
	CString memo;
	if (kind == 1)
	{
		const int n = m_lib.FindNamed(LIST_STUDIO, name);
		if (n >= 0) memo = m_lib.studios[n].memo;
	}
	else if (kind == 2)
	{
		const int n = m_lib.FindLabel(name);
		if (n >= 0) memo = m_lib.labelInfos[n].memo;
	}
	memo.Replace(L"\r\n", L"\n");
	memo.Replace(L"\n", L"\r\n");
	m_memoLoading = true;
	m_editNamedMemo.SetWindowText(memo);
	m_memoLoading = false;
	m_memoDirty = false;
	if (relayout && m_layoutReady)
	{
		CRect client;
		GetClientRect(&client);
		LayoutControls(client.Width(), client.Height());
	}
}

void CRuliManagerDlg::CommitNamedMemo()
{
	if (!m_memoDirty || m_memoKind == 0 || !m_editNamedMemo.GetSafeHwnd())
		return;
	m_memoDirty = false;
	CString memo;
	m_editNamedMemo.GetWindowText(memo);
	memo.Replace(L"\r\n", L"\n");
	memo.TrimRight();
	NamedInfo* target = nullptr;
	if (m_memoKind == 1)
	{
		const int n = m_lib.FindNamed(LIST_STUDIO, m_memoName);
		if (n >= 0) target = &m_lib.studios[n];
	}
	else
	{
		const int n = m_lib.FindLabel(m_memoName);
		if (n >= 0) target = &m_lib.labelInfos[n];
	}
	if (!target || target->memo == memo)
		return;
	target->memo = memo;
	if (!m_lib.Save())
		AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);
	// 목록의 메모 열(카드 셋째 줄)도 갱신
	CString oneLine = memo;
	oneLine.Replace(L'\n', L' ');
	for (size_t r = 0; r < m_catRows.size(); ++r)
	{
		const bool same = (m_memoKind == 1 ? m_catRows[r].kind == CAT_VALUE : m_catRows[r].kind == CAT_LABEL)
			&& m_catRows[r].value.CompareNoCase(m_memoName) == 0;
		if (same)
			m_listCat.SetItemText(static_cast<int>(r), 2, oneLine);
	}
}

void CRuliManagerDlg::OnNamedMemoChanged()
{
	if (!m_memoLoading)
		m_memoDirty = true;
}

void CRuliManagerDlg::OnNamedMemoKillFocus()
{
	CommitNamedMemo();
}

void CRuliManagerDlg::UpdateLabelStrip(const CString& studioName, const CString& labelName)
{
	if (!m_labelStrip.GetSafeHwnd())
		return;
	std::vector<int> labels;
	m_stripOwner = studioName;
	if (!studioName.IsEmpty())
	{
		// 제작사: [상위 제작사] + [하위 제작사들] + [하위 레이블들] (제작사 카드 = 음수 코드)
		const int self = m_lib.FindNamed(LIST_STUDIO, studioName);
		if (self >= 0 && !m_lib.studios[self].parent.IsEmpty())
		{
			const int pn = m_lib.FindNamed(LIST_STUDIO, m_lib.studios[self].parent);
			if (pn >= 0 && pn != self)
				labels.push_back(-(pn + 1));
		}
		for (int ci : m_lib.ChildStudiosOf(studioName))
			labels.push_back(-(ci + 1));
		for (int li : m_lib.LabelsOf(studioName))
			labels.push_back(li);
	}
	if (!labelName.IsEmpty())
	{
		// 레이블: 상위 제작사 카드 하나 (상위가 없으면 카드 없음)
		const int li = m_lib.FindLabel(labelName);
		const int sn = (li >= 0) ? m_lib.FindNamed(LIST_STUDIO, m_lib.labelInfos[li].parent) : -1;
		if (sn >= 0)
			labels.push_back(-(sn + 1));
	}
	const bool relayout = (labels.size() != m_stripLabels.size()) || m_stripForActor;
	m_stripForActor = false;
	m_stripCounts.clear();
	m_stripLabels = labels;
	m_labelStrip.SetCount(static_cast<int>(m_stripLabels.size()));
	if (relayout && m_layoutReady)
	{
		CRect client;
		GetClientRect(&client);
		LayoutControls(client.Width(), client.Height());   // 카드 줄 수만큼 미리보기 높이 조정
	}
	m_labelStrip.Invalidate(FALSE);
}

void CRuliManagerDlg::UpdateActorStudioStrip(int actorIdx)
{
	if (!m_labelStrip.GetSafeHwnd())
		return;
	// 그 배우 출연작의 제작사만 표시: 레이블 영상은 그 레이블의 상위 제작사로 셈 (상위가 없으면 영상에 지정한 제작사)
	std::map<int, int> count;   // 카드 코드(-(제작사 인덱스 + 1)) → 출연 편수
	if (actorIdx >= 0 && actorIdx < static_cast<int>(m_lib.actors.size()))
	{
		std::map<CString, int> studioCode;   // 제작사 이름(소문자) → 코드 (0 = 목록에 없는 제작사 → 표시 안 함)
		for (const VideoItem& v : m_lib.items)
		{
			bool mine = false;
			for (const CString& n : CVideoLibrary::SplitList(v.actors))
				if (m_lib.FindActorByAnyName(n) == actorIdx) { mine = true; break; }
			if (!mine)
				continue;
			CString studio;
			if (!v.label.IsEmpty())
			{
				const int li = m_lib.FindLabel(v.label);
				if (li >= 0)
					studio = m_lib.labelInfos[li].parent;
				studio.Trim();
			}
			if (studio.IsEmpty())
				studio = v.studio;
			studio.Trim();
			if (studio.IsEmpty())
				continue;
			CString key = studio;
			key.MakeLower();
			auto it = studioCode.find(key);
			if (it == studioCode.end())
			{
				const int si = m_lib.FindNamed(LIST_STUDIO, studio);
				it = studioCode.emplace(key, si >= 0 ? -(si + 1) : 0).first;
			}
			if (it->second != 0)
				++count[it->second];
		}
	}
	std::vector<int> list;
	for (const auto& kv : count)
		list.push_back(kv.first);
	auto nameOf = [this](int code) -> const CString& { return (code < 0) ? m_lib.studios[-code - 1].name : m_lib.labelInfos[code].name; };
	std::sort(list.begin(), list.end(), [&](int a, int b)
	{
		if (count[a] != count[b])
			return count[a] > count[b];   // 출연 편수 많은 순, 같으면 이름 순
		return ::StrCmpLogicalW(nameOf(a), nameOf(b)) < 0;
	});
	const bool relayout = (list.size() != m_stripLabels.size()) || !m_stripForActor;
	m_stripForActor = true;
	m_stripCounts.clear();
	for (int c : list)
		m_stripCounts.push_back(count[c]);
	m_stripLabels.swap(list);
	m_labelStrip.SetCount(static_cast<int>(m_stripLabels.size()));
	if (relayout && m_layoutReady)
	{
		CRect client;
		GetClientRect(&client);
		LayoutControls(client.Width(), client.Height());
	}
	m_labelStrip.Invalidate(FALSE);
}

void CRuliManagerDlg::DrawLabelStripCard(CDC* dc, int i, const CRect& rc, bool hot)
{
	if (i < 0 || i >= static_cast<int>(m_stripLabels.size()))
		return;
	const int code = m_stripLabels[i];
	const bool studio = (code < 0);   // 레이블의 상위 제작사 카드
	const NamedInfo& lb = studio ? m_lib.studios[-code - 1] : m_lib.labelInfos[code];
	const COLORREF back = studio ? kCardBack : TCOL(TC_LABELCARD);   // 제작사 / 레이블 카드 색 (제작사 탭 격자와 같음)
	const int radius = DX(4);
	{
		CBrush br(back);
		CPen pen(PS_SOLID, 1, hot ? (studio ? TCOL(TC_CARDHOT) : TCOL(TC_LABELHOT)) : back);
		CBrush* ob = dc->SelectObject(&br);
		CPen* op = dc->SelectObject(&pen);
		dc->RoundRect(rc, CPoint(radius, radius));
		dc->SelectObject(ob);
		dc->SelectObject(op);
	}
	const int pad = DX(3);
	const int nameH = DY(11);
	const CRect area(rc.left + pad, rc.top + pad, rc.right - pad, rc.bottom - nameH - pad);
	Gdiplus::Bitmap* logo = GetStudioLogo(lb.image);
	if (logo && logo->GetWidth() > 0 && logo->GetHeight() > 0 && area.Width() > 0 && area.Height() > 0)
	{
		const double lw = logo->GetWidth(), lh = logo->GetHeight();
		const double scale = (std::min)(area.Width() / lw, area.Height() / lh);
		const int w = (std::max)(1, static_cast<int>(lw * scale));
		const int h = (std::max)(1, static_cast<int>(lh * scale));
		Gdiplus::Graphics g(dc->GetSafeHdc());
		g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
		g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
		g.DrawImage(logo, area.left + (area.Width() - w) / 2, area.top + (area.Height() - h) / 2, w, h);
	}
	else
	{
		CRect ic = area;
		ic.DeflateRect(area.Width() / 6, area.Height() / 8);
		DrawCameraIcon(dc, ic, RGB(255, 255, 255));
	}
	CFont* old = dc->SelectObject(GetFont());
	dc->SetBkMode(TRANSPARENT);
	dc->SetTextColor(kTextColor);
	CString cardName = lb.name;
	if (!m_stripForActor && studio)
	{
		// 제작사 탭: 하위 제작사 "└ 이름", 상위 제작사 "▲ 이름"
		const bool child = !m_stripOwner.IsEmpty() && lb.parent.CompareNoCase(m_stripOwner) == 0;
		cardName = (child ? L"└ " : L"▲ ") + lb.name;
	}
	if (m_stripForActor && i < static_cast<int>(m_stripCounts.size()))
	{
		// 배우 상세: 이름 오른쪽에 그 배우의 출연 편수  예) "S1 NO.1 STYLE (12)"
		CString c;
		c.Format(L" (%d)", m_stripCounts[i]);
		cardName += c;
	}
	TextFB::Draw(dc, cardName, CRect(rc.left + pad, rc.bottom - nameH - pad / 2, rc.right - pad, rc.bottom - pad / 2), DT_CENTER, true);
	dc->SelectObject(old);
}

CRect CRuliManagerDlg::StudioCardXRect(const CRect& card)
{
	// 한 줄 카드 오른쪽 끝, 세로 가운데
	const int size = (std::max)(10, (std::min)(DY(12), card.Height() * 60 / 100));   // 카드가 커져도 × 크기는 그대로
	const int pad = (std::max)(3, DX(3));
	const int top = card.top + (card.Height() - size) / 2;
	return CRect(card.right - pad - size, top, card.right - pad, top + size);
}

void CRuliManagerDlg::ClearStudioCard(bool labelCard)
{
	if (m_curItem < 0 || IsItemOffline(m_curItem) || !m_studioChips.GetSafeHwnd())
		return;
	if (labelCard)
	{
		// 레이블만 비움 (제작사는 그대로)
		if (m_labelChips.GetSafeHwnd())
		{
			m_labelChips.SetTags({});
			if (m_labelChips.m_onChanged)
				m_labelChips.m_onChanged();
		}
	}
	else
	{
		// 제작사 비움 + 그 제작사의 레이블도 (상위 제작사가 없는 공용 레이블은 그대로)
		CString studio;
		m_editStudio.GetWindowText(studio);
		studio.Trim();
		bool clearLabel = false;
		if (LabelRowShown())
		{
			const int li = m_lib.FindLabel(m_labelChips.Tags()[0]);
			clearLabel = (li >= 0 && !studio.IsEmpty() && m_lib.labelInfos[li].parent.CompareNoCase(studio) == 0);
		}
		if (clearLabel && m_labelChips.GetSafeHwnd())
			m_labelChips.SetTags({});
		m_studioChips.SetTags({});
		if (m_studioChips.m_onChanged)
			m_studioChips.m_onChanged();
	}
	OnDetailsChanged();
	RelayoutIfLabelRowChanged();
	{ m_studioCard.Invalidate(FALSE); if (m_labelCard.GetSafeHwnd()) m_labelCard.Invalidate(FALSE); }
	if (m_labelCard.GetSafeHwnd())
		m_labelCard.Invalidate(FALSE);
}

void CRuliManagerDlg::DrawVideoStudioCard(CDC* dc, const CRect& rc, bool hot, bool labelCard)
{
	// 영상 상세의 제작사 카드 / 레이블 카드 (각각 따로 한 줄)
	CString studio;
	if (m_curItem >= 0)
		m_editStudio.GetWindowText(studio);
	studio.Trim();
	const bool label = labelCard;
	const CString labelName = (m_curItem >= 0 && LabelRowShown()) ? m_labelChips.Tags()[0] : CString();
	const bool both = false;
	const CString name = labelCard ? labelName : studio;
	const int radius = DX(4);
	const int pad = DX(3);
	CFont* old = dc->SelectObject(GetFont());
	dc->SetBkMode(TRANSPARENT);
	if (name.IsEmpty())
	{
		// 비어 있음: 점선 "선택..." (더블클릭)
		CBrush br(RGB(0x26, 0x32, 0x3B));
		CPen pen(PS_DOT, 1, (hot && !labelCard) ? TCOL(TC_CARDHOT) : TCOL(TC_BORDER));   // 레이블 카드: 수정 불가 (마우스 오버 강조 없음)
		CBrush* ob = dc->SelectObject(&br);
		CPen* op = dc->SelectObject(&pen);
		dc->RoundRect(rc, CPoint(radius, radius));
		dc->SelectObject(ob);
		dc->SelectObject(op);
		dc->SetTextColor(TCOL(TC_SUB2));
		CRect tr = rc;
		dc->DrawText(m_curItem >= 0 ? (labelCard ? L"레이블 없음" : L"선택...") : L"", tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		dc->SelectObject(old);
		return;
	}
	// 카드 한 칸: 배경(제작사 남색 / 레이블 보라빛) + [이미지 (높이에 맞춤, 넓은 로고는 높이의 3배 폭까지)] 이름
	auto drawSeg = [&](const CRect& sr, const CString& segName, bool isLabel, int rightReserve)
	{
		const NamedInfo* info = nullptr;
		if (isLabel)
		{
			const int li = m_lib.FindLabel(segName);
			if (li >= 0) info = &m_lib.labelInfos[li];
		}
		else
		{
			const int sn = m_lib.FindNamed(LIST_STUDIO, segName);
			if (sn >= 0) info = &m_lib.studios[sn];
		}
		const COLORREF back = kCardBack;   // 제작사 · 레이블 같은 색 (레이블은 품번 DB 값 - 수정 불가, 마우스 오버 강조 없음)
		{
			CBrush br(back);
			CPen pen(PS_SOLID, 1, (hot && !isLabel) ? TCOL(TC_CARDHOT) : back);
			CBrush* ob = dc->SelectObject(&br);
			CPen* op = dc->SelectObject(&pen);
			dc->RoundRect(sr, CPoint(radius, radius));
			dc->SelectObject(ob);
			dc->SelectObject(op);
		}
		const int ih = (std::max)(4, sr.Height() - pad * 2);
		int x = sr.left + pad * 2;
		Gdiplus::Bitmap* logo = info ? GetStudioLogo(info->image) : (isLabel ? GetStudioLogo(m_lib.LabelImage(segName)) : nullptr);   // 레이블: 품번 DB 의 대표 이미지
		if (logo && logo->GetWidth() > 0 && logo->GetHeight() > 0)
		{
			const double lw = logo->GetWidth(), lh = logo->GetHeight();
			const int maxW = (std::min)(ih * 3, (std::max)(ih, sr.Width() / 2));   // 반쪽 카드에서는 폭의 절반까지
			const double scale = (std::min)(maxW / lw, ih / lh);
			const int w = (std::max)(1, static_cast<int>(lw * scale));
			const int h = (std::max)(1, static_cast<int>(lh * scale));
			Gdiplus::Graphics g(dc->GetSafeHdc());
			g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
			g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
			g.DrawImage(logo, x, sr.top + (sr.Height() - h) / 2, w, h);
			x += w + DX(5);
		}
		else
		{
			const CRect ic(x, sr.top + pad, x + ih * 5 / 4, sr.bottom - pad);
			DrawCameraIcon(dc, ic, RGB(255, 255, 255));
			x = ic.right + DX(5);
		}
		dc->SetTextColor(kTextColor);
		const int right = sr.right - pad - rightReserve;
		if (right > x)
			TextFB::Draw(dc, segName, CRect(x, sr.top, right, sr.bottom), DT_LEFT, true);
	};
	const int xReserve = hot ? StudioCardXRect(rc).Width() + pad : 0;
	if (both)
	{
		// 왼쪽 = 제작사, 오른쪽 = 레이블 (사이 간격)
		const int gapW = DX(3);
		const int half = (rc.Width() - gapW) / 2;
		drawSeg(CRect(rc.left, rc.top, rc.left + half, rc.bottom), studio, false, 0);
		drawSeg(CRect(rc.right - half, rc.top, rc.right, rc.bottom), labelName, true, xReserve);
	}
	else
		drawSeg(rc, name, label, xReserve);
	// 마우스 오버: 오른쪽 끝 × (비우기)
	if (hot)
	{
		const CRect x = StudioCardXRect(rc);
		CBrush br(TCOL(TC_DARK));
		CPen pen(PS_SOLID, 1, TCOL(TC_DARK));
		CBrush* ob = dc->SelectObject(&br);
		CPen* op = dc->SelectObject(&pen);
		dc->Ellipse(x);
		dc->SelectObject(ob);
		dc->SelectObject(op);
		CPen xp(PS_SOLID, (std::max)(1, x.Width() / 8), RGB(0xE0, 0xE6, 0xEA));
		op = dc->SelectObject(&xp);
		const int d = x.Width() * 3 / 10;
		const CPoint c = x.CenterPoint();
		dc->MoveTo(c.x - d, c.y - d); dc->LineTo(c.x + d + 1, c.y + d + 1);
		dc->MoveTo(c.x + d, c.y - d); dc->LineTo(c.x - d - 1, c.y + d + 1);
		dc->SelectObject(op);
	}
	dc->SelectObject(old);
}

void CRuliManagerDlg::OpenStudioCardTarget(int which)
{
	if (m_curItem < 0)
		return;
	CString studio;
	m_editStudio.GetWindowText(studio);
	studio.Trim();
	const bool label = (which == 1) ? false : LabelRowShown();
	const CString name = label ? m_labelChips.Tags()[0] : studio;
	if (name.IsEmpty())
	{
		OnBnClickedPickStudio();   // 비어 있으면 선택 창
		return;
	}
	OpenNamedTarget(name, label);
}

void CRuliManagerDlg::OpenNamedTarget(const CString& name, bool label)
{
	if (name.IsEmpty())
		return;
	CommitDetails();
	if (m_mode != MODE_STUDIO)
		OnModeChanged(IDC_RADIO_STUDIO);
	else if (!m_drill.IsEmpty())
		BackToList();
	for (int pass = 0; pass < 2; ++pass)
	{
		for (size_t r = 0; r < m_catRows.size(); ++r)
		{
			if (m_catRows[r].kind == (label ? CAT_LABEL : CAT_VALUE) && m_catRows[r].value.CompareNoCase(name) == 0)
			{
				DrillIntoCategory(static_cast<int>(r));   // 그 제작사 / 레이블의 영상
				return;
			}
		}
		// 검색어 때문에 목록에 없으면 검색을 지우고 다시
		m_editSearch.SetWindowText(L"");
		RebuildCategories();
	}
}

int CRuliManagerDlg::VideoColumns(int step, bool wall, int monitorW) const
{
	// 영상 화면 확대 단계(1 ~ 4)별 가로 칸 수 - 상세 화면을 접었을 때의 격자 폭(창 최대화) 기준
	//  해상도(모니터 가로 픽셀)별로 따로 정함. 아직 2K(2560) 만 맞춤 - 다른 해상도는 2K 값을 그대로 씀 (나중에 맞출 자리)
	struct Row { int maxW; int grid[4]; int wall[4]; };
	static const Row table[] = {
		{ 1920, { 9, 8, 6, 4 }, { 8, 6, 5, 3 } },   // FHD 이하 (임시: 2K 와 같음)
		{ 2560, { 9, 8, 6, 4 }, { 8, 6, 5, 3 } },   // 2K (2560 x 1440) - 격자 9 / 8 / 6 / 4, 월 모드 8 / 6 / 5 / 3
		{ 99999, { 9, 8, 6, 4 }, { 8, 6, 5, 3 } },  // 4K 등 (임시: 2K 와 같음)
	};
	step = (std::max)(0, (std::min)(3, step));
	for (const Row& r : table)
		if (monitorW <= r.maxW)
			return wall ? r.wall[step] : r.grid[step];
	return wall ? table[1].wall[step] : table[1].grid[step];
}

int CRuliManagerDlg::VideoRefGridWidth(int* monitorW)
{
	// 창이 놓인 모니터의 작업 영역 폭 = 최대화했을 때의 창 안쪽 폭 → 상세 화면 접힘일 때 격자 폭 (여백 · 접기 바 · 격자 테두리 · 스크롤바 뺌)
	//  처음 한 번(프로그램을 켤 때의 모니터)만 계산해 두고 그대로 씀 - 창을 다른 모니터로 옮겨도 다시 계산하지 않음
	if (m_lastRefGridW > 0)
	{
		if (monitorW)
			*monitorW = m_refMonitorW;
		return m_lastRefGridW;
	}
	HMONITOR mon = ::MonitorFromWindow(GetSafeHwnd(), MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi = { sizeof(mi) };
	if (!::GetMonitorInfoW(mon, &mi))
		return 0;
	m_refMonitorW = mi.rcMonitor.right - mi.rcMonitor.left;
	if (monitorW)
		*monitorW = m_refMonitorW;
	const int clientW = mi.rcWork.right - mi.rcWork.left;
	const int m = DX(6);
	int gridOuter = clientW - 2 * m - DetailBarW();
	// 격자 창의 바깥 - 안쪽 차이 (테두리 + 세로 스크롤바)
	int nonClient = ::GetSystemMetrics(SM_CXEDGE) * 2 + ::GetSystemMetrics(SM_CXVSCROLL);
	if (m_grid.GetSafeHwnd())
	{
		CRect wr, cr;
		m_grid.GetWindowRect(&wr);
		m_grid.GetClientRect(&cr);
		if (wr.Width() > 0 && wr.Width() > cr.Width())
			nonClient = (std::max)(nonClient, wr.Width() - cr.Width());
	}
	m_lastRefGridW = gridOuter - nonClient - 2;   // 반올림 오차로 한 칸이 밀리지 않게 조금 여유
	return m_lastRefGridW;
}

double CRuliManagerDlg::ZoomFactor() const
{
	static const double kZoom[4] = { 1.0, 1.3, 1.65, 2.1 };
	return kZoom[(std::max)(0, (std::min)(3, m_zoom[ZoomSlot()]))];   // 배우 출연작 화면은 따로
}

void CRuliManagerDlg::OnZoomChanged(int pos)
{
	if (m_mode < MODE_VIDEO || m_mode >= MODE_COUNT)
		return;
	const int slot = ZoomSlot();   // 배우 탭: 배우 격자(1) / 출연작 화면(4) 따로 저장
	m_zoom[slot] = pos;
	CString keyName;
	keyName.Format(L"Zoom%d", slot);
	AfxGetApp()->WriteProfileInt(L"Settings", keyName, pos);

	CWaitCursor wait;
	SetupActorCards();   // 현재 탭 확대 단계로 카드 크기 다시 계산 (이미지도 새 크기로 다시 읽음)
	// 배율을 바꾸면 스크롤은 맨 처음으로 (고른 카드로 옮기지 않음)
	for (CVideoGrid* g : { &m_grid, &m_actorGrid, &m_catGrid })
		if (g->GetSafeHwnd())
		{
			g->ScrollToTop();
			g->Invalidate(FALSE);
		}
}

bool CRuliManagerDlg::GridDrawCard(CDC* dc, int g, const CRect& card, bool selected, bool focused)
{
	if (VideoLabelActive())
	{
		DrawLabelRow(dc, g, card, selected, focused);   // [레이블]: 체크 · 품번 · 세로 커버 · 제목 · 출연 배우 · 발매일
		return true;
	}
	if (VideoImageCardActive())
	{
		// 영상 탭 [세로] 격자: 이미지만 (세로 카드용으로 자른 커버), [월 모드]: 16:9 이미지만 (모서리 각지게)
		m_cardImageOnly = true;
		m_coverPortrait = VideoPortraitActive();
		DrawVideoCard(dc, GridRowOf(g), card, selected, focused);
		m_cardImageOnly = false;
		m_coverPortrait = false;
	}
	else
		DrawVideoCard(dc, GridRowOf(g), card, selected, focused);   // 합쳐 보기: 첫 파트로 그림 (순번은 고른 파트)
	// 왼쪽 위 체크 칸 (배우 카드와 같음): 체크했으면 항상, 아니면 그 자리에 마우스가 있을 때만 빈 칸 (연도별 보기는 격자를 쓰지 않음)
	const bool checked = IsVideoCardChecked(g);
	if (checked || (m_grid.HotRow() == g && m_grid.HotPart() == kVideoCheckPart))
		DrawCardCheck(dc, ActorCheckRect(card), checked);
	return true;
}

// ---------------------------------------------------------------------------
// [레이블] 보기 (제작사 · 레이블 영상 화면): 레이블 + 품번 접두어 묶음마다 머리줄, 줄마다 체크 · 품번 · 세로 커버 · 제목 · 출연 배우 · 발매일

void CRuliManagerDlg::LabelGroupKey(int itemIdx, CString& label, CString& code) const
{
	label.Empty();
	code.Empty();
	if (itemIdx < 0 || itemIdx >= static_cast<int>(m_lib.items.size()))
		return;
	const VideoItem& v = m_lib.items[itemIdx];
	label = v.label;
	label.Trim();
	if (!label.IsEmpty())
	{
		const int li = m_lib.FindLabel(label);
		if (li >= 0)
			label = m_lib.labelInfos[li].name;   // 등록된 표기
	}
	// 품번 접두어: DB 의 시리즈(품번 접두어) → 없으면 품번의 '-' 앞 (없으면 앞쪽 영문)
	code = v.series;
	code.Trim();
	if (code.IsEmpty())
	{
		CString c = v.code;
		c.Trim();
		const int dash = c.Find(L'-');
		if (dash > 0)
			code = c.Left(dash);
		else
		{
			int n = 0;
			while (n < c.GetLength() && ::IsCharAlphaW(c[n]))
				++n;
			code = c.Left(n);
		}
		code.Trim();
	}
	code.MakeUpper();
}

void CRuliManagerDlg::GroupViewByLabel()
{
	// 정렬 고정 (정렬 콤보는 숨김): 품번 DB 의 레이블 순번 (제작사 → 품번 관리 창의 순서 → 항목 안 접두어 순서)
	//  품번 DB 에 없는 품번은 맨 뒤 (레이블 이름 → 접두어 순), 묶음 안은 품번 순
	struct Key { bool reg = false; int owner = 0, index = 0, part = 0; CString label, prefix, code; };
	std::map<int, Key> keys;
	for (int i : m_view)
	{
		Key k;
		LabelGroupKey(i, k.label, k.prefix);
		k.code = m_lib.items[i].code;
		k.reg = m_lib.SeriesSlotForCode(k.code, k.owner, k.index, k.part);
		keys[i] = k;
	}
	std::stable_sort(m_view.begin(), m_view.end(), [&keys](int a, int b)
	{
		const Key& ka = keys[a];
		const Key& kb = keys[b];
		if (ka.reg != kb.reg)
			return ka.reg;
		if (ka.reg)
		{
			if (ka.owner != kb.owner) return ka.owner < kb.owner;
			if (ka.index != kb.index) return ka.index < kb.index;
			if (ka.part != kb.part)   return ka.part < kb.part;
		}
		else
		{
			if (ka.label.IsEmpty() != kb.label.IsEmpty())
				return !ka.label.IsEmpty();
			const int lc = ::StrCmpLogicalW(ka.label, kb.label);
			if (lc != 0)
				return lc < 0;
			if (ka.prefix.IsEmpty() != kb.prefix.IsEmpty())
				return !ka.prefix.IsEmpty();
			const int pc = ::StrCmpLogicalW(ka.prefix, kb.prefix);
			if (pc != 0)
				return pc < 0;
		}
		return ::StrCmpLogicalW(ka.code, kb.code) < 0;   // 묶음 안: 품번 순
	});
}

void CRuliManagerDlg::UpdateLabelLines()
{
	m_labelGroups.clear();
	if (!VideoLabelActive() || !m_grid.GetSafeHwnd())
	{
		m_grid.SetLines({});
		return;
	}
	// 카드마다 한 줄, 묶음(레이블 · 품번)이 바뀌는 카드 위에 머리줄
	std::vector<GridLine> lines;
	const int count = GridGetCount();
	CString prevL, prevC;
	int groupStart = -1;
	for (int g = 0; g < count; ++g)
	{
		const int row = GridRowOf(g);
		CString l, c;
		if (row >= 0 && row < static_cast<int>(m_view.size()))
			LabelGroupKey(m_view[row], l, c);
		const bool head = (g == 0) || l.CompareNoCase(prevL) != 0 || c.CompareNoCase(prevC) != 0;
		if (head)
		{
			groupStart = g;
			LabelGroup grp;
			grp.label = l;
			grp.code = c;
			SeriesInfo si;   // 품번(시리즈)의 라벨 · 설명
			if (row >= 0 && row < static_cast<int>(m_view.size()) && m_lib.SeriesInfoForCode(m_lib.items[m_view[row]].code, si))
			{
				grp.seriesLabel = si.label;
				grp.seriesLabel.Trim();
				grp.desc = si.desc;
				grp.desc.Replace(L"\r\n", L" ");
				grp.desc.Replace(L'\n', L' ');
				grp.desc.Trim();
				grp.image = si.image;
			}
			if (grp.image.IsEmpty())
				grp.image = m_lib.LabelImage(!grp.seriesLabel.IsEmpty() ? grp.seriesLabel : grp.label);   // 같은 레이블의 다른 품번 이미지
			if (!grp.image.IsEmpty() && ::GetFileAttributesW(grp.image) == INVALID_FILE_ATTRIBUTES)
				grp.image.Empty();
			m_labelGroups[g] = grp;
			prevL = l;
			prevC = c;
		}
		++m_labelGroups[groupStart].count;
		GridLine ln;
		ln.start = g;
		ln.header = head;
		if (head && !m_labelGroups[g].image.IsEmpty())
			ln.headerH = m_lgroupH + m_limgH + DX(6);   // 레이블 이미지 줄만큼 높게
		lines.push_back(ln);
	}
	m_grid.SetLines(lines);
}

CRuliManagerDlg::LabelCols CRuliManagerDlg::LabelColumns(const CRect& card)
{
	// 왼쪽부터 체크 · 품번 · 커버 (고정 폭), 오른쪽 끝 발매일 (고정 폭), 가운데 남는 폭을 제목 7 : 출연 배우 3
	LabelCols c;
	const int pad = DX(8);
	const int cy = (card.top + card.bottom) / 2;
	int x = card.left + pad;
	c.check = CRect(x, cy - m_checkSize / 2, x + m_checkSize, cy - m_checkSize / 2 + m_checkSize);
	x = c.check.right + pad;
	c.code = CRect(x, card.top, x + m_lcodeW, card.bottom);
	x = c.code.right + pad;
	c.cover = CRect(x, cy - m_lrowCoverH / 2, x + m_lrowCoverW, cy - m_lrowCoverH / 2 + m_lrowCoverH);
	x = c.cover.right + pad + DX(4);
	const int right = card.right - pad;
	c.release = CRect(right - DX(70), card.top, right, card.bottom);
	const int avail = (std::max)(0, static_cast<int>(c.release.left) - pad - x);
	const int actorsW = (std::max)((std::min)(DX(100), avail / 2), (std::min)(DX(260), avail * 3 / 10));
	const int titleW = (std::max)(0, avail - actorsW - pad);
	c.title = CRect(x, card.top + DX(4), x + titleW, card.bottom - DX(4));
	c.actors = CRect(c.title.right + pad, card.top + DX(4), c.title.right + pad + actorsW, card.bottom - DX(4));
	return c;
}

bool CRuliManagerDlg::PkgAnchorRect(int g, CRect& rc)
{
	if (!m_grid.GetCardRect(g, rc))
		return false;
	if (VideoLabelActive())
		rc = LabelColumns(rc).cover;   // [레이블]: 커버 칸에 마우스 → 팝업 (커버 가운데)
	return true;
}

void CRuliManagerDlg::GridDrawGroupHeader(CDC* dc, int firstCard, const CRect& rc)
{
	if (!VideoLabelActive())
		return;
	auto it = m_labelGroups.find(firstCard);
	if (it == m_labelGroups.end())
		return;
	const LabelGroup& grp = it->second;
	const int saved = dc->SaveDC();
	dc->SetBkMode(TRANSPARENT);
	// 줄 카드와 같은 x (격자 여백 DX(2) - SetupActorCards 의 SetTilePad)
	const CRect card(rc.left + DX(2), rc.top, rc.right - DX(2), rc.bottom);
	const LabelCols cols = LabelColumns(card);

	// 레이블 대표 이미지: 맨 위 (높이 m_limgH, 비율 유지 · 넓은 로고는 높이의 5배 폭까지)
	int top = rc.top + DX(8);
	if (!grp.image.IsEmpty())
	{
		if (Gdiplus::Bitmap* logo = GetStudioLogo(grp.image))
		{
			if (logo->GetWidth() > 0 && logo->GetHeight() > 0)
			{
				const double lw = logo->GetWidth(), lh = logo->GetHeight();
				const double scale = (std::min)(m_limgH / lh, (m_limgH * 5.0) / lw);
				const int w = (std::max)(1, static_cast<int>(lw * scale));
				const int h = (std::max)(1, static_cast<int>(lh * scale));
				Gdiplus::Graphics g(dc->GetSafeHdc());
				g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
				g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
				g.DrawImage(logo, card.left + DX(8), top + (m_limgH - h) / 2, w, h);
			}
		}
		top += m_limgH + DX(6);
	}
	// 첫 줄: 품번(시리즈)의 레이블 (없으면 영상 레이블 이름, 굵게 크게) + (품번) 강조색 + 품번의 설명 + 오른쪽 끝 영상 수
	CRect line(card.left + DX(8), top, card.right - DX(8), top + m_videoLineB);
	{
		CString cnt;
		cnt.Format(L"%d편", grp.count);
		dc->SelectObject(GetFont());
		dc->SetTextColor(TCOL(TC_SUB));
		CRect cr = line;
		dc->DrawText(cnt, cr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		line.right -= dc->GetTextExtent(cnt).cx + DX(10);   // 아래 글자가 영상 수와 겹치지 않게
	}
	dc->SelectObject(&m_videoTitleFont);
	dc->SetTextColor(TCOL(TC_TEXT));
	const CString name = !grp.seriesLabel.IsEmpty() ? grp.seriesLabel : (grp.label.IsEmpty() ? CString(L"(레이블 없음)") : grp.label);
	CRect nr = line;
	dc->DrawText(name, nr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	int x = line.left + (std::min)(static_cast<int>(line.Width()), static_cast<int>(dc->GetTextExtent(name).cx));
	if (!grp.code.IsEmpty())
	{
		const CString code = L"  (" + grp.code + L")";
		dc->SetTextColor(TCOL(TC_ACCENTTEXT));
		CRect cr(x, line.top, line.right, line.bottom);
		dc->DrawText(code, cr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
		x += dc->GetTextExtent(code).cx;
	}
	if (!grp.desc.IsEmpty() && x < line.right)
	{
		// 품번(시리즈)의 설명: 라벨 오른쪽 (넘치면 …)
		dc->SelectObject(&m_listFont);
		dc->SetTextColor(TCOL(TC_GRIDSUB));
		CRect cr(x + DX(12), line.top, line.right, line.bottom);
		dc->DrawText(grp.desc, cr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	}

	// 둘째 줄: 열 이름 (줄의 칸과 같은 자리) + 아래 구분선
	const int ly = line.bottom + DX(2);
	dc->SelectObject(GetFont());
	dc->SetTextColor(TCOL(TC_SUB));
	auto caption = [&](const CRect& col, const wchar_t* text, UINT align)
	{
		CRect r(col.left, ly, col.right, ly + m_cardLine);
		dc->DrawText(text, -1, r, align | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	};
	caption(CRect(cols.check.left - DX(2), 0, cols.check.right + DX(8), 0), L"체크", DT_LEFT);
	caption(cols.code, L"품번", DT_LEFT);
	caption(cols.cover, L"커버", DT_CENTER);
	caption(cols.title, L"제목", DT_LEFT);
	caption(cols.actors, L"출연 배우", DT_LEFT);
	caption(cols.release, L"발매일", DT_LEFT);
	CPen pen(PS_SOLID, 1, TCOL(TC_EDGE));
	CPen* op = dc->SelectObject(&pen);
	dc->MoveTo(card.left, rc.bottom - DX(2));
	dc->LineTo(card.right, rc.bottom - DX(2));
	dc->SelectObject(op);
	dc->RestoreDC(saved);
}

void CRuliManagerDlg::DrawLabelRow(CDC* dc, int g, const CRect& card, bool selected, bool focused)
{
	const int row = GridRowOf(g);
	if (row < 0 || row >= static_cast<int>(m_view.size()))
		return;
	const int idx = m_view[row];
	const VideoItem& v = m_lib.items[idx];
	const bool hot = (m_grid.HotRow() == g);
	const int radius = DX(4);
	const int saved = dc->SaveDC();
	dc->SetBkMode(TRANSPARENT);

	// 줄 배경: 선택 = 선택색, 마우스 오버 = 조금 밝게, 그 밖 = 카드색
	COLORREF back = kCardBack;
	if (selected)
		back = focused ? TCOL(TC_SEL) : TCOL(TC_SELNOFOCUS);
	else if (hot)
		back = RGB((std::min)(255, GetRValue(kCardBack) + 12), (std::min)(255, GetGValue(kCardBack) + 12), (std::min)(255, GetBValue(kCardBack) + 12));
	{
		CBrush br(back);
		CPen pen(PS_SOLID, 1, selected && focused ? kButtonColor : back);
		CBrush* ob = dc->SelectObject(&br);
		CPen* op = dc->SelectObject(&pen);
		dc->RoundRect(card, CPoint(radius, radius));
		dc->SelectObject(ob);
		dc->SelectObject(op);
	}
	const LabelCols cols = LabelColumns(card);

	// 체크 칸 (늘 보임)
	DrawCardCheck(dc, cols.check, IsVideoCardChecked(g));

	// 텍스트 칸 그리기: 세로 가운데 (여러 줄은 칸 높이까지, 넘치면 …)
	auto drawBlock = [dc](const CString& s, CRect r, UINT fmt)
	{
		if (s.IsEmpty() || r.IsRectEmpty())
			return;
		CRect calc(r.left, 0, r.right, 0);
		dc->DrawText(s, calc, fmt | DT_CALCRECT);
		const int h = (std::min)(static_cast<int>(calc.Height()), static_cast<int>(r.Height()));
		r.top += (r.Height() - h) / 2;
		r.bottom = r.top + h;
		dc->DrawText(s, r, fmt);
	};

	// 품번 (굵게) + 분할 영상 순번 / 파트 수
	{
		CString code = v.code;
		code.Trim();
		if (code.IsEmpty())
			code = L"-";
		CString part;
		auto pit = m_gridParts.find(g);
		if (pit != m_gridParts.end() && pit->second.size() > 1)
			part.Format(L"(%d파트)", static_cast<int>(pit->second.size()));
		else
			part = ListPartLabel(idx);
		dc->SelectObject(&m_listCodeFont);
		dc->SetTextColor(TCOL(TC_TEXT));
		if (part.IsEmpty())
			drawBlock(code, cols.code, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
		else
		{
			const int lh = dc->GetTextExtent(L"A").cy;
			CRect top(cols.code.left, (cols.code.top + cols.code.bottom) / 2 - lh, cols.code.right, (cols.code.top + cols.code.bottom) / 2);
			dc->DrawText(code, top, DT_LEFT | DT_SINGLELINE | DT_BOTTOM | DT_NOPREFIX | DT_END_ELLIPSIS);
			dc->SelectObject(GetFont());
			dc->SetTextColor(TCOL(TC_SUB));
			CRect bottom(cols.code.left, top.bottom + DX(1), cols.code.right, top.bottom + DX(1) + lh);
			dc->DrawText(part, bottom, DT_LEFT | DT_SINGLELINE | DT_TOP | DT_NOPREFIX | DT_END_ELLIPSIS);
		}
	}

	// 세로 커버 ([패키지] 카드와 같은 세로로 자른 커버, 이미지만)
	{
		const bool oldOnly = m_cardImageOnly, oldPortrait = m_coverPortrait;
		m_cardImageOnly = true;
		m_coverPortrait = true;
		DrawVideoCard(dc, row, cols.cover, false, false);
		m_cardImageOnly = oldOnly;
		m_coverPortrait = oldPortrait;
		if (hot && m_grid.HotZone() == 1)
		{
			// 커버에 마우스: 테두리 (잠시 뒤 [격자] 영상 카드 팝업)
			CPen pen(PS_SOLID, 2, TCOL(TC_CARDHOT));
			CPen* op = dc->SelectObject(&pen);
			CBrush* ob = static_cast<CBrush*>(dc->SelectStockObject(NULL_BRUSH));
			CRect sr = cols.cover;
			sr.DeflateRect(1, 1);
			dc->RoundRect(sr, CPoint(radius, radius));
			dc->SelectObject(op);
			dc->SelectObject(ob);
		}
	}

	// 제목 (여러 줄)
	{
		CString title = v.title.IsEmpty() ? v.FileName() : v.title;
		title.Replace(L"\r\n", L" ");
		title.Replace(L'\n', L' ');
		dc->SelectObject(&m_listFont);
		dc->SetTextColor(TCOL(TC_TEXT));
		drawBlock(title, cols.title, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS | DT_NOPREFIX);
	}

	// 출연 배우 (쉼표로, 여러 줄)
	{
		CString actors;
		for (const CString& n : CVideoLibrary::SplitList(v.actors))
		{
			CString t = n;
			t.Trim();
			if (t.IsEmpty())
				continue;
			if (!actors.IsEmpty())
				actors += L", ";
			actors += t;
		}
		dc->SelectObject(&m_listFont);
		dc->SetTextColor(actors.IsEmpty() ? TCOL(TC_SUB) : TCOL(TC_ACCENTTEXT));
		drawBlock(actors.IsEmpty() ? CString(L"-") : actors, cols.actors, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS | DT_NOPREFIX);
	}

	// 발매일
	{
		const CString rel = v.pending ? CString(L"● 임시") : (v.release.IsEmpty() ? CString(L"-") : v.release);
		dc->SelectObject(&m_listFont);
		dc->SetTextColor(v.release.IsEmpty() && !v.pending ? TCOL(TC_SUB) : TCOL(TC_TEXT));
		drawBlock(rel, cols.release, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
	}
	dc->RestoreDC(saved);
}

void CRuliManagerDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kListMediaTimer)
	{
		KillTimer(kListMediaTimer);
		m_listMediaTimer = false;
		if (VideoListActive() && m_list.GetSafeHwnd())
			m_list.Invalidate(FALSE);
		return;
	}
	if (nIDEvent == kGearSpinTimer)
	{
		// 미리보기를 만드는 동안 설정 버튼 톱니바퀴를 돌림
		m_gearAngle += 9.0;
		if (m_gearAngle >= 360.0)
			m_gearAngle -= 360.0;
		if (m_btnSettings.GetSafeHwnd())
			m_btnSettings.SetIconAngle(m_gearAngle);
		return;
	}
	if (nIDEvent == kPreviewHoverTimer)
	{
		// [월 모드] 재생 / 일시정지: 프로그램이 앞에 있고 스크롤이 0.3초 멈췄을 때만 재생
		if (VideoWallActive())
		{
			const bool play = WallShouldPlay();
			if (play != m_wallPlaying)
			{
				ApplyWallPlayState(play);
				if (play)
					SyncWallPlayers();   // 멈춘 동안 새로 보인 칸 재생 시작
			}
		}
		// [월 모드] 재생 창: 0.5초마다 (새로 만든 미리보기 · 목록 바뀜 · 창 크기) + 고른 칸이 바뀌면 바로 다시 맞춤
		const int sel = VideoWallActive() ? GridGetSel() : -1;
		if (++m_wallSyncTick >= 8 || sel != m_wallSelItem || (!VideoWallActive() && !m_wallPlayers.empty()))
		{
			m_wallSyncTick = 0;
			SyncWallPlayers();
		}
		if (VideoWallActive() && m_wallOverlay.GetSafeHwnd())
		{
			// 분할 영상 파트 칸 표시: 마우스가 올라간 칸 · 파트 · 고른 파트가 바뀌면 글자 창 다시 그림
			const size_t hotSig = (static_cast<size_t>(m_grid.HotRow() + 1) * 1000003u) ^ (static_cast<size_t>(m_grid.HotZone()) * 7919u)
				^ (static_cast<size_t>(m_grid.HotPart() + 1) * 104729u) ^ (static_cast<size_t>(GetSelectedRow() + 1) * 31u);
			if (hotSig != m_wallHotSig)
			{
				m_wallHotSig = hotSig;
				m_wallOverlay.Invalidate(FALSE);
			}
		}
		if (m_wallSyncTick != 0 && VideoWallActive() && m_wallOverlay.GetSafeHwnd())
		{
			// 주 창을 옮기거나 크기를 바꾸면 글자 창도 따라감 (자리가 같으면 아무것도 안 함)
			CRect want, cur;
			if (m_grid.GetSafeHwnd() && m_grid.IsWindowVisible() && !IsIconic())
			{
				m_grid.GetClientRect(&want);
				m_grid.ClientToScreen(&want);
				m_wallOverlay.GetWindowRect(&cur);
				if (want != cur || !m_wallOverlay.IsWindowVisible())
					UpdateWallOverlay();
			}
			else
				m_wallOverlay.HideOverlay();
		}
		CheckPackageHover();   // [패키지] 카드 마우스 오버 → [격자] 영상 카드 팝업 (미리보기보다 먼저)
		CheckPkgPreview();     // [패키지] 팝업 카드 커버 → 미리보기 + 분할 영상 파트 칸
		CheckPreviewHover();   // 영상 카드 마우스 오버 → 바로 미리보기 팝업
		UpdateGridPartOverlay();   // 분할 영상 미리보기 위 파트 칸
		CheckCastHover();      // 영상 카드 아래 배우 수 마우스 오버 → 출연 배우 카드 팝업
		CheckCountHover();     // 모든 카드 아래 집계 줄: 마우스 오버 배경
		return;
	}
	if (nIDEvent != kOverlayFadeTimer)
	{
		CDialogEx::OnTimer(nIDEvent);
		return;
	}
	// 영상 카드 덧그림 페이드: 목표(이미지 위 = 0, 아니면 255)까지 한 번에 조금씩 (약 0.12초)
	const int step = 36;
	for (auto it = m_overlayFade.begin(); it != m_overlayFade.end(); )
	{
		const int row = it->first;
		const bool hidden = (m_grid.HotRow() == row && m_grid.HotZone() == 1);
		const int target = hidden ? 0 : 255;
		int& a = it->second;
		a = (a < target) ? (std::min)(target, a + step) : (std::max)(target, a - step);
		CRect r;
		if (m_grid.GetSafeHwnd() && m_grid.GetTileRect(row, r))
			m_grid.InvalidateRect(&r, FALSE);
		if (a == 255 && !hidden)
			it = m_overlayFade.erase(it);   // 다 나타남 → 추적 끝
		else
			++it;
	}
	// 숨긴 채 멈춘 카드만 남으면 타이머는 쉼 (다시 바뀌면 그릴 때 다시 시작)
	bool moving = false;
	for (const auto& kv : m_overlayFade)
	{
		const bool hidden = (m_grid.HotRow() == kv.first && m_grid.HotZone() == 1);
		if (kv.second != (hidden ? 0 : 255)) { moving = true; break; }
	}
	if (!moving && m_fadeTimer)
	{
		KillTimer(kOverlayFadeTimer);
		m_fadeTimer = 0;
	}
}

CString CRuliManagerDlg::GridTipText(int g, const CRect& card, CPoint pt)
{
	const int row = GridRowOf(g);   // 격자 칸 → 행 (합쳐 보기면 카드의 첫 파트)
	{
		CRect badge;
		int pos = 0;
		if (ShotBadgeRect(row, card, badge, nullptr, &pos) && badge.PtInRect(pt))
		{
			if (pos > 0)
				return L"스크린샷 추출 대기 중 - 더블클릭하면 대기열에서 뺌";
			return m_shotCancel ? CString(L"스크린샷 추출을 멈추는 중...") : CString(L"스크린샷 추출 중 - 더블클릭하면 멈춤 (다음 대기 영상으로)");
		}
	}
	// 영상 카드의 제목 3줄 칸 위에서, 제목이 다 보이지 않으면(3줄을 넘어 "…") 제목 전체를 풍선 도움말로
	if (row < 0 || row >= static_cast<int>(m_view.size()))
		return CString();
	if (VideoLabelActive())
	{
		// [레이블]: 제목 · 출연 배우 칸 위 = 그 글자 전체 (잘렸을 수 있으므로)
		const LabelCols cols = LabelColumns(card);
		const VideoItem& lv = m_lib.items[m_view[row]];
		if (cols.title.PtInRect(pt))
			return lv.title.IsEmpty() ? lv.FileName() : lv.title;
		if (cols.actors.PtInRect(pt) && !lv.actors.IsEmpty())
		{
			CString a;
			for (const CString& n : CVideoLibrary::SplitList(lv.actors))
				a += (a.IsEmpty() ? L"" : L", ") + n;
			return a;
		}
		return CString();
	}
	const VideoItem& v = m_lib.items[m_view[row]];
	CString rest = v.title;
	rest.Replace(L"\r\n", L" ");
	rest.Replace(L'\n', L' ');
	rest.Trim();
	if (rest.IsEmpty())
		return CString();
	// DrawVideoCard 와 같은 배치: 이미지 → 품번 줄 → 발매일 줄 → 제목 3줄
	const int x = card.left + m_cardPad;
	const int right = card.right - m_cardPad;
	const int top = card.top + m_vcardImgH + m_cardPad + m_videoLineB + DX(2) + m_vcardSubLine + m_vcardMemoGap;
	const CRect titleRc(x, top, right, top + m_vcardSubLine * 3);
	if (!titleRc.PtInRect(pt))
		return CString();
	const int lineW = right - x;
	CString lkey;
	lkey.Format(L"%d|%d|", lineW, m_vcardSubLine);
	lkey += rest;
	auto it = m_titleLines.find(lkey);
	if (it == m_titleLines.end() || it->second.empty())
		return CString();
	const std::vector<CString>& lines = it->second;
	if (lines.size() < 3)
		return CString();   // 3줄 안에 다 들어감
	CClientDC cdc(this);
	CFont* old = cdc.SelectObject(&m_vcardSubFont);
	const bool cut = TextFB::Width(&cdc, lines.back()) > lineW;   // 마지막 줄이 넘치면 "…" 로 잘림
	cdc.SelectObject(old);
	return cut ? rest : CString();
}

bool CRuliManagerDlg::ShotBadgeRect(int row, const CRect& card, CRect& out, CString* text, int* queuePos)
{
	if (!m_shotBusy || row < 0 || row >= static_cast<int>(m_view.size()))
		return false;
	// 분할 영상 합쳐 보기(격자): 카드의 파트 중 하나라도 추출 중 / 대기 중이면 표시
	std::vector<int> rows{ row };
	if (!m_cardImageOnly || VideoImageCardActive())   // 연도별 보기는 묶음 없음 ([세로] 격자 · [월 모드]는 묶음 카드)
	{
		auto pit = m_gridParts.find(GridIdxOf(row));
		if (pit != m_gridParts.end() && GridRowOf(pit->first) == row)
			rows = pit->second;
	}
	CString t;
	int pos = 0;
	bool found = false;
	for (int pr : rows)
	{
		const VideoItem& v = m_lib.items[m_view[pr]];
		if (!m_shotVideoPath.IsEmpty() && v.path.CompareNoCase(m_shotVideoPath) == 0)
		{
			t.Format(L"(%d/%d)", m_shotDone, m_shotTotal);   // 추출 중
			pos = 0;
			found = true;
			break;
		}
		const int qp = ShotQueuePos(v.path);
		if (qp > 0 && (!found || qp < pos))
		{
			pos = qp;
			t.Format(L"(대기 %d)", pos);                      // 대기열 순번 (여러 파트면 가장 앞)
			found = true;
		}
	}
	if (!found)
		return false;
	if (text) *text = t;
	if (queuePos) *queuePos = pos;
	// 카드 전체의 왼쪽 아래 (연도별 보기 = 이미지만 있는 카드: 커버 이미지 왼쪽 아래, 여백을 좁게)
	CClientDC dc(this);
	CFont* pf = dc.SelectObject(&m_vcardSubFont);
	const int tw = dc.GetTextExtent(t).cx;
	dc.SelectObject(pf);
	const int margin = m_cardImageOnly ? DX(2) : DX(4);
	const int bh = m_vcardSubLine + DX(4);
	const int bottom = card.bottom - margin;
	out = CRect(card.left + margin, bottom - bh, card.left + margin + tw + DX(10), bottom);
	return true;
}

void CRuliManagerDlg::DrawShotBadge(CDC* dc, int row, const CRect& card)
{
	CRect badge;
	CString t;
	int pos = 0;
	if (!ShotBadgeRect(row, card, badge, &t, &pos))
		return;
	const int saved = dc->SaveDC();
	dc->IntersectClipRect(card);   // 연도별 보기의 작은 카드에서도 카드 밖으로 넘치지 않게
	CBrush br(RGB(0x10, 0x14, 0x18));
	CPen pen(PS_SOLID, 1, pos > 0 ? TCOL(TC_BORDER) : kButtonColor);   // 대기 = 회색 테두리, 추출 중 = 파란 테두리
	CBrush* ob = dc->SelectObject(&br);
	CPen* op = dc->SelectObject(&pen);
	dc->RoundRect(badge, CPoint(DX(4), DX(4)));
	dc->SelectObject(ob);
	dc->SelectObject(op);
	CFont* pf = dc->SelectObject(&m_vcardSubFont);   // 굵은 글꼴
	dc->SetBkMode(TRANSPARENT);
	dc->SetTextColor(pos > 0 ? TCOL(TC_SUB) : RGB(255, 255, 255));
	CRect tr = badge;
	dc->DrawText(t, tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	dc->SelectObject(pf);
	dc->RestoreDC(saved);
}

void CRuliManagerDlg::InvalidateShotCards()
{
	// 스크린샷 추출 진행 · 대기 순번 표시가 바뀜: 격자 + 연도별 보기
	if (m_grid.GetSafeHwnd())
		m_grid.Invalidate(FALSE);
	if (m_yearView.GetSafeHwnd() && m_yearView.IsWindowVisible())
		m_yearView.Invalidate(FALSE);
}

bool CRuliManagerDlg::GridDblClick(int g, const CRect& card, CPoint pt)
{
	if (VideoLabelActive())
	{
		if (GridHitPart(g, card, pt) == kVideoCheckPart)
		{
			ToggleVideoCheck(g);   // 체크 칸 빠른 두 번 클릭 = 두 번 전환 (재생하지 않음)
			return true;
		}
		return false;   // 줄 더블클릭 = 재생
	}
	if (VideoImageCardActive() && !m_cardImageOnly)
	{
		// [세로] 격자 · [월 모드]: 이미지만 그린 카드 배치로 (스크린샷 표시 자리 · 파트 칸 없음)
		m_cardImageOnly = true;
		const bool handled = GridDblClick(g, card, pt);
		m_cardImageOnly = false;
		return handled;
	}
	if (GridHitPart(g, card, pt) == kVideoCheckPart)
	{
		ToggleVideoCheck(g);   // 체크 칸 빠른 두 번 클릭 = 두 번 전환 (재생하지 않음)
		return true;
	}
	const int row = GridRowOf(g);   // 격자 칸 → 행 (연도별 보기는 칸 표가 없어 그대로)
	CRect badge;
	int pos = 0;
	if (!ShotBadgeRect(row, card, badge, nullptr, &pos) || !badge.PtInRect(pt))
	{
		// 분할 영상 합쳐 보기: 이미지 위 파트 칸 더블클릭 = 그 파트 재생 ([세로] 격자는 카드 전체가 파트 칸)
		if (!m_cardImageOnly || VideoImageCardActive())
		{
			const int z = PartZoneAt(g, card, pt);
			if (z >= 0)
			{
				SelectViewRow(m_gridParts[g][z]);
				GridActivate(g);
				return true;
			}
		}
		return false;
	}
	if (pos > 0)
	{
		// 대기 중인 영상: 대기열에서 뺌
		m_shotQueue.erase(m_shotQueue.begin() + (pos - 1));
		UpdateShotTitle();
		InvalidateShotCards();
	}
	else
		m_shotCancel = true;   // 추출 중인 영상: 멈춤 (지금 장면이 끝나면 멈추고 저장한 것은 남김) → 다음 대기 영상
	return true;
}

int CRuliManagerDlg::GridHoverZone(int /*row*/, const CRect& card, CPoint pt)
{
	// 영상 카드 이미지 영역 (DrawVideoCard 의 img 와 같음) - [세로] 격자는 카드 전체 (분할 영상 파트 칸 표시)
	if (VideoLabelActive())
		return LabelColumns(card).cover.PtInRect(pt) ? 1 : 0;   // [레이블]: 커버 칸
	const CRect img(card.left, card.top, card.right, VideoImageCardActive() ? card.bottom : card.top + m_vcardImgH);
	return img.PtInRect(pt) ? 1 : 0;
}

namespace
{
	// 이미지 파일 → w×h 32비트 DIB (비율 유지 · 가운데 · 남는 곳은 배경색) - 백그라운드 스레드용 (화면 DC 를 쓰지 않음)
	//  portrait: 세로 카드용 - 가로가 더 긴 이미지는 오른쪽 절반, 세로가 더 긴 이미지는 전체를 폭에 맞춤 (넘치는 위아래는 가운데 기준으로 잘라냄)
	HBITMAP MakeCoverBitmap(const CString& file, int w, int h, COLORREF back, bool portrait = false)
	{
		CImage img;
		if (file.IsEmpty() || !LoadImageFileSfw(img, file) || img.IsNull())   // SFW: 모자이크 (백그라운드 스레드)
			return nullptr;
		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
		bi.bmiHeader.biWidth = w;
		bi.bmiHeader.biHeight = -h;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		void* bits = nullptr;
		HBITMAP bmp = ::CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (!bmp)
			return nullptr;
		HDC mem = ::CreateCompatibleDC(nullptr);
		HGDIOBJ old = ::SelectObject(mem, bmp);
		HBRUSH br = ::CreateSolidBrush(back);
		RECT all = { 0, 0, w, h };
		::FillRect(mem, &all, br);
		::DeleteObject(br);
		if (portrait)
		{
			const int iw0 = img.GetWidth(), ih0 = img.GetHeight();
			int sx = 0, sw = iw0;
			if (iw0 > ih0)
			{
				sx = iw0 / 2;            // 가로가 더 긴 이미지: 오른쪽 절반 (앞표지)
				sw = iw0 - sx;
			}
			int sy = 0, sh = ih0;
			const double scale = static_cast<double>(w) / (std::max)(1, sw);   // 폭에 맞춤
			int dh = static_cast<int>(sh * scale + 0.5);
			int dy = 0;
			if (dh > h)
			{
				sh = (std::max)(1, static_cast<int>(h / scale + 0.5));   // 넘치는 위아래는 잘라냄 (가운데)
				sy = (ih0 - sh) / 2;
				dh = h;
			}
			else
				dy = (h - dh) / 2;       // 모자라면 위아래 여백 (배경색)
			::SetStretchBltMode(mem, HALFTONE);
			::SetBrushOrgEx(mem, 0, 0, nullptr);
			img.StretchBlt(mem, 0, dy, w, (std::max)(1, dh), sx, sy, sw, sh, SRCCOPY);
			::SelectObject(mem, old);
			::DeleteDC(mem);
			return bmp;
		}
		// 비율을 유지한 채 이미지 전체가 카드 이미지 칸에 들어가도록 맞춤 (자르지 않음, 가운데 정렬)
		const double iw = img.GetWidth(), ih = img.GetHeight();
		const double scale = (std::min)(w / iw, h / ih);
		const int dw = (std::max)(1, (std::min)(w, static_cast<int>(iw * scale + 0.5)));
		const int dh = (std::max)(1, (std::min)(h, static_cast<int>(ih * scale + 0.5)));
		::SetStretchBltMode(mem, HALFTONE);
		::SetBrushOrgEx(mem, 0, 0, nullptr);
		img.StretchBlt(mem, (w - dw) / 2, (h - dh) / 2, dw, dh, 0, 0, static_cast<int>(iw), static_cast<int>(ih), SRCCOPY);
		::SelectObject(mem, old);
		::DeleteDC(mem);
		return bmp;
	}

	// MakeCoverBitmap 으로 만든 커버에서 실제 이미지 영역 (가장자리의 배경색(back)만 있는 줄 · 칸을 뺀 나머지, 비트맵 좌표)
	//  - 32비트 DIB 가 아니면 비트맵 전체
	CRect CoverContentRect(HBITMAP bmp, COLORREF back)
	{
		DIBSECTION ds = {};
		if (!bmp || ::GetObject(bmp, sizeof(ds), &ds) != sizeof(ds) || !ds.dsBm.bmBits || ds.dsBm.bmBitsPixel != 32)
		{
			BITMAP bm = {};
			if (bmp)
				::GetObject(bmp, sizeof(bm), &bm);
			return CRect(0, 0, bm.bmWidth, bm.bmHeight);
		}
		const int w = ds.dsBm.bmWidth, h = ds.dsBm.bmHeight;
		const bool topDown = ds.dsBmih.biHeight < 0;
		const BYTE* base = static_cast<const BYTE*>(ds.dsBm.bmBits);
		const DWORD key = (static_cast<DWORD>(GetRValue(back)) << 16) | (static_cast<DWORD>(GetGValue(back)) << 8) | GetBValue(back);
		auto px = [&](int x, int y) -> DWORD
		{
			const int row = topDown ? y : (h - 1 - y);
			return reinterpret_cast<const DWORD*>(base + static_cast<size_t>(row) * ds.dsBm.bmWidthBytes)[x] & 0x00FFFFFF;
		};
		auto rowEmpty = [&](int y) { for (int x = 0; x < w; ++x) if (px(x, y) != key) return false; return true; };
		auto colEmpty = [&](int x, int y0, int y1) { for (int y = y0; y < y1; ++y) if (px(x, y) != key) return false; return true; };
		int top = 0, bottom = h;
		while (top < bottom && rowEmpty(top)) ++top;
		while (bottom > top && rowEmpty(bottom - 1)) --bottom;
		if (top >= bottom)
			return CRect(0, 0, 0, 0);   // 전부 배경색
		int left = 0, right = w;
		while (left < right && colEmpty(left, top, bottom)) ++left;
		while (right > left && colEmpty(right - 1, top, bottom)) --right;
		return CRect(left, top, right, bottom);
	}
}

CBitmap* CRuliManagerDlg::GetVideoCover(int itemIdx, int w, int h, bool* loading, bool prefetch)
{
	if (loading)
		*loading = false;
	if (itemIdx < 0 || itemIdx >= static_cast<int>(m_lib.items.size()) || w <= 0 || h <= 0)
		return nullptr;
	CString key;
	key.Format(L"%s|%dx%d%s", static_cast<LPCWSTR>(m_lib.items[itemIdx].path), w, h, m_coverPortrait ? L"|p" : L"");   // |p = 세로 카드용으로 자름
	key.MakeLower();
	auto it = m_videoCovers.find(key);
	if (it != m_videoCovers.end())
	{
		if (!prefetch)
			m_coverUse[key] = ++m_coverTick;
		return it->second.get();   // 이미지가 없으면 nullptr 이 저장되어 있음
	}

	if (m_videoCovers.size() > 600)
	{
		// 메모리 제한: 오래 안 쓴 것 200개만 지움 (전부 지우면 보이는 카드까지 다시 읽느라 깜빡임)
		std::vector<std::pair<UINT64, CString>> order;
		order.reserve(m_videoCovers.size());
		for (const auto& kv : m_videoCovers)
		{
			auto u = m_coverUse.find(kv.first);
			order.emplace_back(u != m_coverUse.end() ? u->second : 0, kv.first);
		}
		std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		for (size_t i = 0; i < order.size() && i < 200; ++i)
		{
			m_videoCovers.erase(order[i].second);
			m_coverUse.erase(order[i].second);
		}
	}

	if (loading)
		*loading = true;
	if (!m_coverPending.insert(key).second)
	{
		// 이미 대기 중: 지금 보이는 카드가 요청하면 맨 뒤(다음 차례)로 옮김 (미리 읽기로 앞쪽에 넣어 둔 것)
		if (!prefetch && m_coverQ)
		{
			std::lock_guard<std::mutex> lock(m_coverQ->mx);
			auto& q = m_coverQ->q;
			for (auto it = q.begin(); it != q.end(); ++it)
			{
				if (it->key == key)
				{
					if (std::next(it) != q.end())
					{
						CoverJob job = *it;
						q.erase(it);
						q.push_back(job);
					}
					break;
				}
			}
		}
		return nullptr;            // 이미 읽는 중
	}

	if (!m_coverQ)
	{
		// 처음 요청할 때 읽기 스레드 2개 시작 (창을 닫을 때 끝날 때까지 기다림)
		m_coverQ = std::make_shared<CoverQueue>();
		m_coverQ->hwnd = GetSafeHwnd();
		for (int n = 0; n < 2; ++n)
		{
			std::shared_ptr<CoverQueue> q = m_coverQ;
			m_coverThreads.emplace_back([q]()
			{
				const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // WIC
				for (;;)
				{
					CoverJob job;
					{
						std::unique_lock<std::mutex> lock(q->mx);
						q->cv.wait(lock, [&q]() { return q->stop || !q->q.empty(); });
						if (q->stop)
							break;
						job = q->q.back();   // 최근 요청 먼저
						q->q.pop_back();
					}
					CoverResult* r = new CoverResult;
					r->key = job.key;
					r->gen = job.gen;
					r->item = job.item;
					const CString file = FindImageFor(job.videoPath);
					if (!file.IsEmpty())
					{
						r->bmp = MakeCoverBitmap(file, job.w, job.h, kThumbBackColor, job.portrait);
						// 이동식 디스크 영상: 커버를 캐시에 보관 (디스크를 빼도 표시)
						if (r->bmp && !job.cacheFile.IsEmpty() && !::PathFileExistsW(job.cacheFile))
							CVideoLibrary::SaveCoverCache(file, job.cacheFile);
					}
					else if (!job.cacheFile.IsEmpty() && ::PathFileExistsW(job.cacheFile))
						r->bmp = MakeCoverBitmap(job.cacheFile, job.w, job.h, kThumbBackColor, job.portrait);   // 디스크 연결 안 됨 → 캐시의 커버
					if (!::IsWindow(q->hwnd) || !::PostMessageW(q->hwnd, WM_APP_COVER_READY, 0, reinterpret_cast<LPARAM>(r)))
					{
						if (r->bmp) ::DeleteObject(r->bmp);
						delete r;
						break;
					}
				}
				if (SUCCEEDED(hr))
					::CoUninitialize();
			});
		}
	}
	{
		std::lock_guard<std::mutex> lock(m_coverQ->mx);
		CoverJob job;
		job.key = key;
		job.videoPath = m_lib.items[itemIdx].path;
		job.w = w;
		job.h = h;
		job.gen = m_coverGen;
		job.item = itemIdx;
		job.cacheFile = m_lib.CoverCacheFile(job.videoPath);   // 이동식 디스크 영상이면 커버 캐시 파일
		job.portrait = m_coverPortrait;
		if (prefetch)
			m_coverQ->q.push_front(job);   // 미리 읽기: 보이는 카드 다음에
		else
			m_coverQ->q.push_back(job);
		// 빠르게 스크롤하면 지나간 카드 요청이 쌓임 → 오래된 것은 버림 (다시 보이면 다시 요청)
		while (m_coverQ->q.size() > 120)
		{
			m_coverPending.erase(m_coverQ->q.front().key);
			m_coverQ->q.pop_front();
		}
	}
	m_coverQ->cv.notify_one();
	return nullptr;
}

void CRuliManagerDlg::ClearVideoCovers()
{
	m_videoCovers.clear();
	m_coverUse.clear();
	m_coverPending.clear();
	++m_coverGen;   // 읽는 중이던 결과는 버림 (이미지가 바뀌었을 수 있음)
	if (m_coverQ)
	{
		std::lock_guard<std::mutex> lock(m_coverQ->mx);
		m_coverQ->q.clear();
	}
}

void CRuliManagerDlg::StopCoverThreads()
{
	if (m_coverQ)
	{
		{
			std::lock_guard<std::mutex> lock(m_coverQ->mx);
			m_coverQ->stop = true;
			m_coverQ->q.clear();
		}
		m_coverQ->cv.notify_all();
	}
	for (std::thread& th : m_coverThreads)
		if (th.joinable())
			th.join();
	m_coverThreads.clear();
	m_coverQ.reset();
	m_coverPending.clear();
	// 아직 처리하지 못한 결과 정리
	MSG msg;
	while (::PeekMessageW(&msg, GetSafeHwnd(), WM_APP_COVER_READY, WM_APP_COVER_READY, PM_REMOVE))
	{
		CoverResult* r = reinterpret_cast<CoverResult*>(msg.lParam);
		if (r && r->bmp) ::DeleteObject(r->bmp);
		delete r;
	}
}

LRESULT CRuliManagerDlg::OnCoverReady(WPARAM, LPARAM lParam)
{
	std::unique_ptr<CoverResult> r(reinterpret_cast<CoverResult*>(lParam));
	if (!r)
		return 0;
	if (r->gen != m_coverGen)
	{
		if (r->bmp) ::DeleteObject(r->bmp);   // 캐시를 비운 뒤 도착한 예전 결과
		return 0;
	}
	m_coverPending.erase(r->key);
	std::unique_ptr<CBitmap> bmp;
	if (r->bmp)
	{
		bmp = std::make_unique<CBitmap>();
		bmp->Attach(r->bmp);
	}
	m_videoCovers[r->key] = std::move(bmp);
	m_coverUse[r->key] = ++m_coverTick;
	// 그 카드가 화면에 보이면 그 카드만 다시 그림 (전체를 다시 그리면 스크롤 중 깜빡이고 무거움)
	if (m_grid.GetSafeHwnd() && m_grid.IsWindowVisible())
	{
		CRect client;
		m_grid.GetClientRect(&client);
		for (size_t row = 0; row < m_view.size(); ++row)
		{
			if (m_view[row] != r->item)
				continue;
			CRect card;
			if (m_grid.GetCardRect(GridIdxOf(static_cast<int>(row)), card) && CRect().IntersectRect(&card, &client))
				m_grid.InvalidateRect(&card, FALSE);
			break;
		}
	}
	if (VideoListActive() && m_list.GetSafeHwnd() && m_list.IsWindowVisible())
	{
		// [리스트]: 그 줄의 커버 칸만 다시 그림
		for (size_t row = 0; row < m_view.size(); ++row)
			if (m_view[row] == r->item)
			{
				CRect rc;
				if (ListCoverRect(static_cast<int>(row), rc))
					m_list.InvalidateRect(&rc, FALSE);
				break;
			}
	}
	if (m_mode == MODE_COLLECTION && m_catGrid.GetSafeHwnd() && m_catGrid.IsWindowVisible())
		m_catGrid.Invalidate(FALSE);   // 컬렉션 카드의 대표 영상 이미지
	if (m_yearView.GetSafeHwnd() && m_yearView.IsWindowVisible())   // 연도별 보기도 그 카드만
	{
		for (size_t row = 0; row < m_view.size(); ++row)
			if (m_view[row] == r->item)
			{
				m_yearView.InvalidateItem(static_cast<int>(row));
				break;
			}
	}
	return 0;
}

void CRuliManagerDlg::GridPrefetch(int first, int last)
{
	// 화면 바로 위 · 아래 카드의 이미지를 미리 읽어 둠 (스크롤해서 보일 때 바로 그려지게)
	if (!m_grid.GetSafeHwnd() || m_vcardImgH <= 0)
		return;
	for (int g = first; g <= last; ++g)   // 격자 칸 (합쳐 보기면 카드의 첫 파트)
	{
		const int row = GridRowOf(g);
		if (row < 0 || row >= static_cast<int>(m_view.size()))
			continue;
		CRect card;
		if (!m_grid.GetCardRect(g, card) || card.Width() <= 0)
			continue;
		if (VideoLabelActive())
		{
			m_coverPortrait = true;   // [레이블]: 줄의 세로 커버 칸
			GetVideoCover(m_view[row], m_lrowCoverW, m_lrowCoverH, nullptr, true);
			m_coverPortrait = false;
		}
		else if (VideoPortraitActive())
		{
			m_coverPortrait = true;   // [세로] 격자: 카드 전체 = 세로로 자른 커버
			GetVideoCover(m_view[row], card.Width(), card.Height(), nullptr, true);
			m_coverPortrait = false;
		}
		else
			GetVideoCover(m_view[row], card.Width(), VideoWallActive() ? card.Height() : m_vcardImgH, nullptr, true);   // [월 모드]: 칸 전체 = 이미지
	}
}

namespace
{
	// GDI+ 는 처음 쓸 때 한 번 시작 (프로그램이 끝날 때까지 유지)
	void EnsureGdiPlus()
	{
		static ULONG_PTR token = 0;
		if (!token)
		{
			Gdiplus::GdiplusStartupInput input;
			Gdiplus::GdiplusStartup(&token, &input, nullptr);
		}
	}
}

Gdiplus::Bitmap* CRuliManagerDlg::GetStudioLogo(const CString& path)
{
	if (path.IsEmpty())
		return nullptr;
	CString key = path;
	key.MakeLower();
	auto it = m_studioLogos.find(key);
	if (it != m_studioLogos.end())
		return it->second.get();

	EnsureGdiPlus();
	std::unique_ptr<Gdiplus::Bitmap> bmp;
	if (::PathFileExistsW(path))
	{
		// PNG 등은 투명도까지 그대로 읽음 (암호화된 이미지는 메모리에서 복호화한 스트림으로)
		if (IStream* s = OpenImageFileStream(path))
		{
			std::unique_ptr<Gdiplus::Bitmap> tmp(Gdiplus::Bitmap::FromStream(s));
			if (tmp && tmp->GetLastStatus() == Gdiplus::Ok && tmp->GetWidth() > 0 && tmp->GetHeight() > 0)
			{
				// 스트림과 분리된 복사본 (스트림을 바로 놓아도 되게)
				bmp = std::make_unique<Gdiplus::Bitmap>(static_cast<INT>(tmp->GetWidth()), static_cast<INT>(tmp->GetHeight()), PixelFormat32bppPARGB);
				Gdiplus::Graphics g(bmp.get());
				g.DrawImage(tmp.get(), 0, 0, static_cast<INT>(tmp->GetWidth()), static_cast<INT>(tmp->GetHeight()));
			}
			tmp.reset();
			s->Release();
		}
		if (bmp && bmp->GetLastStatus() != Gdiplus::Ok)
			bmp.reset();
		if (!bmp)
		{
			// GDI+ 로 못 읽는 형식(WebP · SVG 등)은 WIC / Direct2D 로 읽어서 변환 - 32비트면 투명도 그대로 (알파 미리 곱한 BGRA)
			CImage img;
			if (LoadImageFile(img, path) && !img.IsNull())
			{
				const int w = img.GetWidth(), h = img.GetHeight();
				if (img.GetBPP() == 32 && img.GetPitch() > 0)
				{
					Gdiplus::Bitmap wrap(w, h, img.GetPitch(), PixelFormat32bppPARGB, static_cast<BYTE*>(img.GetBits()));   // CImage 메모리를 그대로 (위→아래)
					bmp = std::make_unique<Gdiplus::Bitmap>(w, h, PixelFormat32bppPARGB);
					Gdiplus::Graphics g(bmp.get());
					g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
					g.DrawImage(&wrap, 0, 0, w, h);
				}
				else
					bmp.reset(Gdiplus::Bitmap::FromHBITMAP(static_cast<HBITMAP>(img), nullptr));
				if (bmp && bmp->GetLastStatus() != Gdiplus::Ok)
					bmp.reset();
			}
		}
	}
	Gdiplus::Bitmap* result = bmp.get();
	m_studioLogos[key] = std::move(bmp);
	return result;
}

void CRuliManagerDlg::DrawStudioMark(CDC* dc, const CRect& img, const CString& studio, const CString& label)
{
	CString name = studio, lname = label;
	name.Trim();
	lname.Trim();
	if (name.IsEmpty() && lname.IsEmpty())
		return;
	const int margin = DX(4);
	// 레이블이 있으면 레이블 이미지 (없으면 제작사 이미지), 둘 다 없으면 이름 글자 (레이블 이름 우선)
	Gdiplus::Bitmap* logo = nullptr;
	if (!lname.IsEmpty())
	{
		logo = GetStudioLogo(m_lib.LabelImage(lname));   // 레이블 = 품번 DB 의 레이블 대표 이미지
	}
	if (!logo && !name.IsEmpty())
	{
		const int n = m_lib.FindNamed(LIST_STUDIO, name);
		logo = (n >= 0) ? GetStudioLogo(m_lib.studios[n].image) : nullptr;
	}
	if (!lname.IsEmpty())
		name = lname;
	if (logo && logo->GetWidth() > 0 && logo->GetHeight() > 0)
	{
		// 이미지 오른쪽 위: 폭 38% · 높이 24% 안에 비율 유지, 75% 불투명
		const double maxW = img.Width() * 0.38, maxH = img.Height() * 0.24;
		const double lw = logo->GetWidth(), lh = logo->GetHeight();
		const double scale = (std::min)(maxW / lw, maxH / lh);
		const int w = (std::max)(1, static_cast<int>(lw * scale));
		const int h = (std::max)(1, static_cast<int>(lh * scale));
		const int x = img.right - margin - w;
		const int y = img.top + margin;

		// 줄이기(고품질) + 75% 불투명은 처음 한 번만 해서 보관 → 그릴 때는 크기 그대로 복사 (스크롤 중 CPU 사용량 줄임)
		CString key;
		key.Format(L"%p|%dx%d", static_cast<void*>(logo), w, h);
		Gdiplus::Bitmap* mark = nullptr;
		auto it = m_logoMarks.find(key);
		if (it != m_logoMarks.end())
			mark = it->second.get();
		else
		{
			if (m_logoMarks.size() > 400)
				m_logoMarks.clear();
			auto bmp = std::make_unique<Gdiplus::Bitmap>(w, h, PixelFormat32bppPARGB);
			{
				Gdiplus::Graphics bg(bmp.get());
				bg.Clear(Gdiplus::Color(0, 0, 0, 0));
				bg.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
				bg.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
				Gdiplus::ColorMatrix cm = {
					1, 0, 0, 0, 0,
					0, 1, 0, 0, 0,
					0, 0, 1, 0, 0,
					0, 0, 0, 0.75f, 0,
					0, 0, 0, 0, 1 };
				Gdiplus::ImageAttributes attr;
				attr.SetColorMatrix(&cm);
				bg.DrawImage(logo, Gdiplus::Rect(0, 0, w, h), 0, 0, static_cast<INT>(lw), static_cast<INT>(lh), Gdiplus::UnitPixel, &attr);
			}
			if (bmp->GetLastStatus() != Gdiplus::Ok)
				bmp.reset();
			mark = bmp.get();
			m_logoMarks[key] = std::move(bmp);
		}
		if (mark)
		{
			Gdiplus::Graphics g(dc->GetSafeHdc());
			g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
			g.SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);
			g.DrawImage(mark, x, y, w, h);   // 크기 그대로 (다시 줄이지 않음)
		}
		return;
	}

	// 스튜디오 이미지가 없으면 이름을 반투명 글자로
	EnsureGdiPlus();
	LOGFONT lf = {};
	m_cardNameFont.GetLogFont(&lf);
	Gdiplus::Graphics g(dc->GetSafeHdc());
	g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
	Gdiplus::Font font(dc->GetSafeHdc(), &lf);
	Gdiplus::RectF box(static_cast<Gdiplus::REAL>(img.left + margin), static_cast<Gdiplus::REAL>(img.top + margin),
		static_cast<Gdiplus::REAL>(img.Width() - margin * 2), static_cast<Gdiplus::REAL>(img.Height() / 3));
	Gdiplus::StringFormat fmt;
	fmt.SetAlignment(Gdiplus::StringAlignmentFar);           // 오른쪽 정렬
	fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
	fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
	Gdiplus::SolidBrush shadow(Gdiplus::Color(140, 0, 0, 0));
	Gdiplus::RectF sbox = box;
	sbox.Offset(1.0f, 1.0f);
	g.DrawString(name, -1, &font, sbox, &fmt, &shadow);
	Gdiplus::SolidBrush brush(Gdiplus::Color(190, 255, 255, 255));
	g.DrawString(name, -1, &font, box, &fmt, &brush);
}

bool CRuliManagerDlg::SplitPartName(const CString& path, CString& key, int& num)
{
	return CVideoLibrary::PartGroupKey(path, key, num);   // "ABC-123_2.mp4" → 묶음 키 + 순번 2
}

void CRuliManagerDlg::DrawPartZones(CDC* dc, int row, const CRect& rc, const CRect& img, int radius, bool show)
{
	const int gIdx = GridIdxOf(row);
	// 분할 영상 합쳐 보기: 이미지 위에 마우스를 올리면 파트 수만큼 좌우 칸 + 반투명 번호 (올라간 칸은 밝게, 고른 파트 칸은 아래 파란 줄)
	const auto partsIt = m_gridParts.find(gIdx);
	const bool merged = (partsIt != m_gridParts.end() && GridRowOf(gIdx) == row);
	int selPart = -1;   // 이 카드에서 고른 파트 (0 부터)
	if (merged)
	{
		const int selRow = GetSelectedRow();
		for (size_t p = 0; p < partsIt->second.size(); ++p)
			if (partsIt->second[p] == selRow) { selPart = static_cast<int>(p); break; }
	}
	if (merged && show)
	{
		EnsureGdiPlus();
		const int n = static_cast<int>(partsIt->second.size());
		const int hotPart = m_grid.HotPart() - 1;
		CRgn clip;
		clip.CreateRoundRectRgn(rc.left, rc.top, rc.right + 1, rc.bottom + 1, radius, radius);
		dc->SelectClipRgn(&clip);
		dc->IntersectClipRect(img);
		{
			Gdiplus::Graphics g(dc->GetSafeHdc());
			g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
			g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
			Gdiplus::SolidBrush dim(Gdiplus::Color(70, 0, 0, 0));
			Gdiplus::SolidBrush hot(Gdiplus::Color(60, 255, 255, 255));
			Gdiplus::Pen sep(Gdiplus::Color(110, 255, 255, 255), 1.0f);
			Gdiplus::FontFamily family(L"Segoe UI");
			const float fs = (std::max)(9.0f, (std::min)(img.Height() * 0.45f, img.Width() / static_cast<float>(n) * 0.6f));
			Gdiplus::Font font(&family, fs, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
			Gdiplus::StringFormat fmt;
			fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
			fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
			for (int p = 0; p < n; ++p)
			{
				const int x0 = img.left + img.Width() * p / n, x1 = img.left + img.Width() * (p + 1) / n;
				const Gdiplus::RectF cell(static_cast<float>(x0), static_cast<float>(img.top), static_cast<float>(x1 - x0), static_cast<float>(img.Height()));
				g.FillRectangle(p == hotPart ? static_cast<Gdiplus::Brush*>(&hot) : static_cast<Gdiplus::Brush*>(&dim), cell);
				if (p > 0)
					g.DrawLine(&sep, static_cast<float>(x0), static_cast<float>(img.top + 2), static_cast<float>(x0), static_cast<float>(img.bottom - 2));
				Gdiplus::SolidBrush text(Gdiplus::Color(p == hotPart ? 235 : 150, 255, 255, 255));
				wchar_t num[8];
				swprintf_s(num, L"%d", p + 1);
				g.DrawString(num, -1, &font, cell, &fmt, &text);
				if (p == selPart)
				{
					Gdiplus::SolidBrush selBar(Gdiplus::Color(230, GetRValue(kButtonColor), GetGValue(kButtonColor), GetBValue(kButtonColor)));
					const float bh = static_cast<float>((std::max)(2, DX(3)));
					g.FillRectangle(&selBar, Gdiplus::RectF(cell.X + 2, cell.GetBottom() - bh - 1, cell.Width - 4, bh));
				}
			}
		}
		dc->SelectClipRgn(nullptr);
	}
}

void CRuliManagerDlg::DrawVideoCard(CDC* dc, int row, const CRect& rc, bool selected, bool focused)
{
	if (row < 0 || row >= static_cast<int>(m_view.size()))
		return;
	const int idx = m_view[row];
	const VideoItem& v = m_lib.items[idx];
	const int radius = (m_cardImageOnly && VideoWallActive()) ? 0 : DX(4);   // [월 모드]: 빈틈없이 붙이므로 각진 모서리

	// 카드 배경 (둥근 모서리)
	{
		CBrush br(kCardBack);
		CPen pen(PS_SOLID, 1, kCardBack);
		CBrush* ob = dc->SelectObject(&br);
		CPen* op = dc->SelectObject(&pen);
		dc->RoundRect(rc, CPoint(radius, radius));
		dc->SelectObject(ob);
		dc->SelectObject(op);
	}

	// 이미지 (위쪽 모서리 둥글게) - 이미지만 그릴 때(연도별 보기)는 카드 전체가 이미지
	const CRect img(rc.left, rc.top, rc.right, m_cardImageOnly ? rc.bottom : rc.top + m_vcardImgH);
	{
		CRgn clip;
		clip.CreateRoundRectRgn(rc.left, rc.top, rc.right + 1, rc.bottom + 1, radius, radius);
		dc->SelectClipRgn(&clip);
		dc->IntersectClipRect(img);
		bool coverLoading = false;
		if (CBitmap* bmp = GetVideoCover(idx, img.Width(), img.Height(), &coverLoading))
		{
			CDC mem;
			mem.CreateCompatibleDC(dc);
			CBitmap* old = mem.SelectObject(bmp);
			bool online = true;
			if (m_lib.IsRemovableItem(v.path, &online) && !online)
			{
				// 이동식 디스크 연결 안 됨: 캐시의 커버를 반투명으로
				dc->FillSolidRect(img, m_cardFill != CLR_INVALID ? m_cardFill : kThumbBackColor);
				BLENDFUNCTION bf = { AC_SRC_OVER, 0, 100, 0 };
				::AlphaBlend(dc->GetSafeHdc(), img.left, img.top, img.Width(), img.Height(), mem.GetSafeHdc(), 0, 0, img.Width(), img.Height(), bf);
			}
			else
				dc->BitBlt(img.left, img.top, img.Width(), img.Height(), &mem, 0, 0, SRCCOPY);
			mem.SelectObject(old);
			if (m_cardFill != CLR_INVALID)
			{
				// 연도별 보기: 이미지 비율 때문에 남는 빈 영역(위아래 / 좌우)을 배우 이름별 고유색으로
				CRect content = CoverContentRect(static_cast<HBITMAP>(bmp->GetSafeHandle()), kThumbBackColor);
				content.OffsetRect(img.left, img.top);
				content &= img;
				if (content.IsRectEmpty())
					dc->FillSolidRect(img, m_cardFill);
				else
				{
					if (content.top > img.top)       dc->FillSolidRect(img.left, img.top, img.Width(), content.top - img.top, m_cardFill);
					if (content.bottom < img.bottom) dc->FillSolidRect(img.left, content.bottom, img.Width(), img.bottom - content.bottom, m_cardFill);
					if (content.left > img.left)     dc->FillSolidRect(img.left, content.top, content.left - img.left, content.Height(), m_cardFill);
					if (content.right < img.right)   dc->FillSolidRect(content.right, content.top, img.right - content.right, content.Height(), m_cardFill);
				}
			}
		}
		else if (coverLoading)
		{
			// 이미지를 읽는 중: 어두운 재생 아이콘 + "불러오는 중..." (기본 이미지와 구분되게)
			dc->FillSolidRect(img, kThumbBackColor);
			CRect iconRc = img;
			iconRc.bottom -= img.Height() / 6;
			VectorIcon::Movie(dc, iconRc, RGB(0x5C, 0x6A, 0x74), 0.55);
			CRect textRc(img.left, iconRc.bottom - img.Height() / 12, img.right, img.bottom - DY(2));
			CFont* prevFont = dc->SelectObject(GetFont());
			dc->SetBkMode(TRANSPARENT);
			dc->SetTextColor(RGB(0xA8, 0xB4, 0xBC));
			dc->DrawText(L"불러오는 중...", textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			dc->SelectObject(prevFont);
		}
		else
		{
			dc->FillSolidRect(img, m_cardFill != CLR_INVALID ? m_cardFill : kThumbBackColor);   // 연도별 보기: 배우 이름별 고유색
			VectorIcon::Movie(dc, img, RGB(255, 255, 255), 0.78);   // 영상 기본 이미지: 흰 원 + 재생 (원본 SVG 처럼 이미지 높이의 약 71%)
		}
		dc->SelectClipRgn(nullptr);
	}

	if (m_cardImageOnly)
	{
		// [세로] 격자: 분할 영상 합쳐 보기 카드에 마우스를 올리면 카드 전체에 파트 칸 (가로 카드의 이미지 위와 같음)
		if (VideoWallActive())   // [패키지] 는 파트 칸 없음 (팝업 카드에서 고름)
			DrawPartZones(dc, row, rc, img, radius, m_grid.HotRow() == GridIdxOf(row) && m_grid.HotZone() == 1);
		// 연도별 보기: 이미지만 (로고 · 별점 리본 · 재생 시간 · 아래 글자 없음), 선택 테두리만
		if (selected)
		{
			CPen pen(PS_SOLID, 2, focused ? kButtonColor : TCOL(TC_BORDER));
			CPen* op = dc->SelectObject(&pen);
			CBrush* ob = static_cast<CBrush*>(dc->SelectStockObject(NULL_BRUSH));
			CRect sr = rc;
			sr.DeflateRect(1, 1);
			dc->RoundRect(sr, CPoint(radius, radius));
			dc->SelectObject(op);
			dc->SelectObject(ob);
		}
		DrawShotBadge(dc, row, rc);   // 스크린샷 추출 "(3/15)" / "(대기 2)" — 커버 이미지 왼쪽 아래
		return;
	}

	// 이미지 위에 마우스를 올리면 이미지만 (로고 · 별점 리본 · 해상도 / 재생 시간이 부드럽게 사라짐 / 나타남)
	const int gIdx = GridIdxOf(row);   // 격자 칸 (분할 영상 합쳐 보기면 카드 칸, 아니면 행과 같음)
	const bool imageOnly = !m_popupCardDraw && (m_grid.HotRow() == gIdx && m_grid.HotZone() == 1);   // [패키지] 팝업 카드: 마우스 오버 표시 없음
	int overlayAlpha = 255;   // 0 = 숨김, 255 = 다 보임
	if (!m_popupCardDraw)
	{
		auto fit = m_overlayFade.find(gIdx);   // 격자 칸 기준 (OnTimer 의 페이드도 칸)
		if (fit != m_overlayFade.end())
			overlayAlpha = fit->second;
		if ((imageOnly && overlayAlpha != 0) || (!imageOnly && overlayAlpha != 255))
		{
			m_overlayFade[gIdx] = overlayAlpha;   // 목표까지 타이머로 조금씩 (StepOverlayFade)
			if (!m_fadeTimer)
				m_fadeTimer = SetTimer(kOverlayFadeTimer, 15, nullptr);
		}
	}
	auto drawOverlays = [&](CDC* d, const CRect& rcX, const CRect& imgX)
	{
		// 스튜디오가 지정되어 있으면 이미지 오른쪽 위에 로고(없으면 이름)를 반투명으로
		DrawStudioMark(d, imgX, v.studio, v.label);   // 레이블이 있으면 레이블 이미지

		// 별점: 이미지 왼쪽 위 대각선 리본 (배우 카드와 같은 모양/색)
		DrawRatingRibbon(d, rcX, imgX, radius, v.rating, GetFont());

		// 이동식 디스크에서 등록한 영상: 이미지 왼쪽 아래에 "이동식 디스크" (연결 안 됐으면 "(연결 안 됨)")
		{
			bool online = false;
			if (m_lib.IsRemovableItem(v.path, &online))
			{
				const CString text = online ? CString(L"이동식 디스크") : CString(L"이동식 디스크 (연결 안 됨)");
				CFont* prev = d->SelectObject(&m_vcardSubFont);   // 굵은 글꼴
				const int margin = DX(4);
				const int bh = m_vcardSubLine + DX(1);
				const int tw = d->GetTextExtent(text).cx;
				const CRect box(imgX.left + margin, imgX.bottom - margin - bh, (std::min)(static_cast<int>(imgX.right) - margin, static_cast<int>(imgX.left) + margin + tw + 1), imgX.bottom - margin);
				d->SetBkMode(TRANSPARENT);
				const UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
				CRect sr = box;
				sr.OffsetRect(1, 1);
				d->SetTextColor(RGB(0, 0, 0));
				d->DrawText(text, sr, fmt);   // 그림자 (밝은 이미지에서도 보이게)
				CRect tr = box;
				d->SetTextColor(online ? RGB(0xFF, 0xC4, 0x5C) : RGB(0xB8, 0xC0, 0xC6));   // 연결됨: 주황 / 연결 안 됨: 회색
				d->DrawText(text, tr, fmt);
				d->SelectObject(prev);
			}
		}

		// 이미지 오른쪽 아래: 재생 시간(시:분:초)  예) 2:03:15 - 바탕 없이, 글자 뒤에 어두운 그림자 (밝은 이미지에서도 보이게)
		//  (해상도는 품번 줄 오른쪽 끝의 해상도 마크로 옮김 → 이미지 안에서는 뺌)
		//  (설정에서 끄면 읽지도 그리지도 않음 - 파일이 많을 때 처음 로딩 부담)
		if (const CMediaInfoLabel::Info* mi = m_showCardMedia ? CardMediaInfo(v.path) : nullptr)
		{
			const CString res;   // 이미지 안 해상도 글자는 표시 안 함 (해상도 마크)
			CString dur;
			ULONGLONG total100ns = mi->duration100ns;
			{
				// 분할 영상 합쳐 보기(격자의 합친 카드): 파트들 재생 시간의 합 (모든 파트를 읽은 뒤에, 그 전에는 첫 파트만)
				auto pit = m_gridParts.find(gIdx);
				if (pit != m_gridParts.end() && GridRowOf(gIdx) == row && !m_cardImageOnly)
				{
					ULONGLONG sum = 0;
					bool all = true;
					for (int pr : pit->second)
					{
						if (pr < 0 || pr >= static_cast<int>(m_view.size()))
							continue;
						const CMediaInfoLabel::Info* pi = CardMediaInfo(m_lib.items[m_view[pr]].path);   // 없으면 읽기 요청
						if (!pi) { all = false; continue; }
						sum += pi->duration100ns;
					}
					if (all && sum > 0)
						total100ns = sum;
				}
			}
			if (total100ns > 0)
			{
				const ULONGLONG sec = total100ns / 10000000ULL;
				dur.Format(L"%llu:%02llu:%02llu", sec / 3600, (sec / 60) % 60, sec % 60);
			}
			if (!res.IsEmpty() || !dur.IsEmpty())
			{
				CFont* prev = d->SelectObject(&m_vcardSubFont);   // 굵은 글꼴
				const int resW = res.IsEmpty() ? 0 : d->GetTextExtent(res).cx;
				d->SelectObject(GetFont());
				const int gapW = (!res.IsEmpty() && !dur.IsEmpty()) ? DX(3) : 0;
				const int durW = dur.IsEmpty() ? 0 : d->GetTextExtent(dur).cx;
				const int margin = DX(4);
				const int bh = m_vcardSubLine + DX(1);
				const CRect box(imgX.right - margin - (resW + gapW + durW), imgX.bottom - margin - bh, imgX.right - margin, imgX.bottom - margin);
				d->SetBkMode(TRANSPARENT);
				auto shadowText = [d](const CString& s, const CRect& r, COLORREF color)
				{
					const UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
					CRect sr = r;
					sr.OffsetRect(1, 1);
					d->SetTextColor(RGB(0, 0, 0));
					d->DrawText(s, sr, fmt);   // 그림자
					CRect r2 = r;
					d->SetTextColor(color);
					d->DrawText(s, r2, fmt);
				};
				int tx = box.left;
				if (!res.IsEmpty())
				{
					d->SelectObject(&m_vcardSubFont);
					shadowText(res, CRect(tx, box.top, tx + resW + 1, box.bottom), RGB(255, 255, 255));
					tx += resW + gapW;
				}
				if (!dur.IsEmpty())
				{
					d->SelectObject(GetFont());
					shadowText(dur, CRect(tx, box.top, tx + durW + 1, box.bottom), RGB(0xE6, 0xEA, 0xEE));
				}
				d->SelectObject(prev);
			}
		}
	};
	if (overlayAlpha >= 255)
		drawOverlays(dc, rc, img);
	else if (overlayAlpha > 0)
	{
		// 이미지 부분을 복사한 임시 그림에 덧그림을 그리고, 그 결과를 투명도(alpha)로 원래 이미지 위에 섞음
		CDC tmp;
		tmp.CreateCompatibleDC(dc);
		CBitmap tbmp;
		tbmp.CreateCompatibleBitmap(dc, img.Width(), img.Height());
		CBitmap* ob = tmp.SelectObject(&tbmp);
		tmp.BitBlt(0, 0, img.Width(), img.Height(), dc, img.left, img.top, SRCCOPY);
		CRect rcX = rc, imgX = img;
		rcX.OffsetRect(-img.left, -img.top);
		imgX.OffsetRect(-img.left, -img.top);
		drawOverlays(&tmp, rcX, imgX);
		tmp.SelectClipRgn(nullptr);
		BLENDFUNCTION bf = { AC_SRC_OVER, 0, static_cast<BYTE>(overlayAlpha), 0 };
		::AlphaBlend(dc->GetSafeHdc(), img.left, img.top, img.Width(), img.Height(), tmp.GetSafeHdc(), 0, 0, img.Width(), img.Height(), bf);
		tmp.SelectObject(ob);
	}

	DrawPartZones(dc, row, rc, img, radius, imageOnly);   // 분할 영상 합쳐 보기: 파트 칸 (이미지 위에 마우스를 올렸을 때)
	// 아래 순번 · 라벨에서 쓰는 이 카드의 묶음 / 고른 파트
	const auto partsIt = m_gridParts.find(gIdx);
	const bool merged = (partsIt != m_gridParts.end() && GridRowOf(gIdx) == row);
	int selPart = -1;   // 이 카드에서 고른 파트 (0 부터)
	if (merged)
	{
		const int selRow = GetSelectedRow();
		for (size_t p = 0; p < partsIt->second.size(); ++p)
			if (partsIt->second[p] == selRow) { selPart = static_cast<int>(p); break; }
	}

	// 스크린샷 추출 중인 영상: 카드 왼쪽 아래에 "(3/15)", 대기 중인 영상은 "(대기 2)" (더블클릭하면 멈춤 / 대기열에서 뺌)
	DrawShotBadge(dc, row, rc);

	CFont* normal = GetFont();
	CFont* oldFont = dc->SelectObject(&m_videoTitleFont);   // 제목: 2pt 큰 글꼴
	dc->SetBkMode(TRANSPARENT);
	const int x = rc.left + m_cardPad;
	const int right = rc.right - m_cardPad;
	int y = img.bottom + m_cardPad;

	// 제목 (없으면 파일 이름) - 굵게, 한 줄
	// 카드 첫 줄: 제목 대신 품번 (품번이 없으면 제목, 제목도 없으면 파일 이름)
	const CString title = !v.code.IsEmpty() ? v.code : (v.title.IsEmpty() ? v.FileName() : v.title);
	// 품번 오른쪽에 파일 순번 "(2/3)" - 파일 이름의 마지막 '_' 오른쪽 숫자 기준, 같은 묶음 파일이 2개 이상일 때만
	CString partLabel;
	{
		CString key;
		int num = 0;
		// 합쳐 보기: 이 카드에서 고른 파트의 순번 (안 골랐으면 첫 파트)
		const CString& labelPath = (merged && selPart > 0) ? m_lib.items[m_view[partsIt->second[selPart]]].path : v.path;
		if (SplitPartName(labelPath, key, num))
		{
			const auto it = m_partTotal.find(key);
			const int total = (it != m_partTotal.end()) ? it->second : 1;
			if (total > 1)
				partLabel.Format(L"(%d/%d)", num, total);
		}
	}
	dc->SetTextColor(kTextColor);
	CRect tr(x, y, right, y + m_videoLineB);
	// 품번 줄 오른쪽 끝: 해상도 마크 - 품번색(플랫) 테두리 안에 [카드 배경색 칸: 1080p (품번색 글자)] [품번색 칸: FHD (카드 배경색 글자)]
	//  (SD 는 연회색, 설정에서 해상도 표시를 끄거나 연결 안 된 이동식 디스크면 없음)
	if (const CMediaInfoLabel::Info* mi = m_showCardMedia ? CardMediaInfo(v.path) : nullptr)
	{
		CString resText;
		const CString grade = mi->ok ? CMediaInfoLabel::GradeText(mi->width, mi->height, &resText) : CString();
		if (!grade.IsEmpty())
		{
			const bool sd = (grade == L"SD");
			const COLORREF face = sd ? RGB(0xB4, 0xBC, 0xC4) : kTextColor;   // 테두리 · 오른쪽 칸 (플랫, 품번 글자색)
			const COLORREF txt  = face;                                                  // 검은 칸의 글자
			CFont* pf = dc->SelectObject(&m_vcardSubFont);   // 굵은 글꼴
			const CSize rs = dc->GetTextExtent(resText);
			const CSize gs = dc->GetTextExtent(grade);
			const int frame = (std::max)(1, DX(2) - 1);       // 테두리 두께
			const int padX = DX(3);
			const int bh = (std::min)(static_cast<int>(tr.Height()), static_cast<int>((std::max)(rs.cy, gs.cy)) + DY(2));
			const int darkW = rs.cx + padX * 2;
			const int goldW = gs.cx + padX * 2;
			const int bw = frame + darkW + goldW + frame / 2;
			CRect badge(tr.right - bw, tr.top + (tr.Height() - bh) / 2, tr.right, 0);
			badge.bottom = badge.top + bh;
			if (badge.left > tr.left + DX(24))   // 카드가 아주 좁으면 생략
			{
				// 안쪽 칸 (크기 고정)
				const int inset = (std::max)(1, frame - 1);
				CRect dark(badge.left + inset, badge.top + inset, badge.left + frame + darkW + (frame - inset), badge.bottom - inset);
				// 바깥 사각형: 사방으로 1px 줄임 (안쪽 칸보다 작아지지는 않음)
				CRect outer = badge;
				outer.DeflateRect(1, 1);
				outer.left = (std::min)(outer.left, dark.left);
				outer.top = (std::min)(outer.top, dark.top);
				outer.bottom = (std::max)(outer.bottom, dark.bottom);
				outer.right = (std::max)(outer.right, dark.right + 1);
				dc->FillSolidRect(outer, face);      // 품번색 바탕 (테두리 + 오른쪽 칸), 각진 모서리
				dc->FillSolidRect(dark, kCardBack);   // 안쪽 칸: 카드 배경색
				dc->SetBkMode(TRANSPARENT);
				dc->SetTextColor(txt);
				dc->DrawText(resText, dark, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
				CRect gold(dark.right, outer.top, outer.right, outer.bottom);
				dc->SetTextColor(kCardBack);   // 등급 글자: 카드 배경색
				dc->DrawText(grade, gold, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
				tr.right = badge.left - DX(4);   // 품번은 마크 왼쪽까지
			}
			dc->SelectObject(pf);
			dc->SetTextColor(kTextColor);
		}
	}
	if (partLabel.IsEmpty())
		TextFB::Draw(dc, title, tr, DT_LEFT, true);   // 글꼴 대체 (일본어 한자 등)
	else
	{
		const int labelW = TextFB::Width(dc, partLabel);
		const int gapW = DX(3);
		CRect cr = tr;
		cr.right = (std::max)(cr.left, tr.right - labelW - gapW);   // 순번이 잘리지 않게 품번 쪽을 줄임
		const int codeW = (std::min)(TextFB::Width(dc, title), cr.Width());
		TextFB::Draw(dc, title, cr, DT_LEFT, true);
		CRect lr(tr.left + codeW + gapW, tr.top, tr.right, tr.bottom);
		dc->SetTextColor(kCardSub);
		TextFB::Draw(dc, partLabel, lr, DT_LEFT, false);
	}
	y += m_videoLineB + DX(2);

	// 발매일, 임시 항목은 예전처럼 "● 임시 (정보 저장 전)" (강조색)
	dc->SelectObject(&m_vcardSubFont);   // 발매일 · 제목: 기본보다 1pt 큰 굵은 글꼴
	CRect dr(x, y, right, y + m_vcardSubLine);
	if (v.pending)
	{
		dc->SetTextColor(TCOL(TC_ACCENTTEXT));
		dc->DrawText(L"● 임시 (정보 저장 전)", dr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	}
	else
	{
		// 발매일 줄 오른쪽 끝 (품번 줄 해상도 마크 아래): 검열 정보 - 설정 창의 검열 정보 언어로
		if (v.censor > 0)
		{
			const CString cname = CVideoLibrary::CensorName(v.censor);
			const COLORREF ccol = CensorColor(v.censor);
			const int cw = TextFB::Width(dc, cname);
			const int padX = DX(3);
			TEXTMETRIC ctm = {};
			dc->GetTextMetrics(&ctm);
			const int bh = (std::min)(static_cast<int>(dr.Height()), static_cast<int>(ctm.tmHeight) + DY(2));
			// 사각형 (검열 색으로 채움, 글자는 카드 배경색 = 반전) - 해상도 마크와 같은 오른쪽 끝
			CRect box(right - cw - padX * 2, dr.top + (dr.Height() - bh) / 2, right, 0);
			box.bottom = box.top + bh;
			if (box.left > dr.left + DX(30))   // 카드가 아주 좁으면 생략
			{
				dc->FillSolidRect(box, ccol);      // 검열 색으로 전체 칠함
				dc->SetTextColor(kCardBack);       // 글자: 카드 배경색 (반전)
				TextFB::Draw(dc, cname, CRect(box.left + padX, box.top, box.right - padX + 1, box.bottom), DT_LEFT, false);   // 일본어 글꼴 대체
				dr.right = box.left - DX(4);   // 발매일은 그 왼쪽까지
			}
		}
		dc->SetTextColor(kCardSub);
		dc->DrawText(v.release, dr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	}
	y += m_vcardSubLine + m_vcardMemoGap;   // 발매일과 제목 사이 2pt 더

	// 제목 3줄 (메모 대신, 제목이 없으면 빈칸) - 일본어 한자 등은 글꼴 대체로 그리고, 줄바꿈은 직접 계산
	{
		CString rest = v.title;
		rest.Replace(L"\r\n", L" ");
		rest.Replace(L'\n', L' ');
		rest.Trim();
		dc->SetTextColor(TCOL(TC_LIGHT));
		const int lineW = right - x;
		// 줄바꿈 계산(글자 폭을 한 글자씩 잼)은 제목 · 폭 · 글꼴 높이별로 한 번만 하고 보관 (스크롤 중 CPU 사용량 줄임)
		CString lkey;
		lkey.Format(L"%d|%d|", lineW, m_vcardSubLine);
		lkey += rest;
		auto lit = m_titleLines.find(lkey);
		if (lit == m_titleLines.end())
		{
			if (m_titleLines.size() > 3000)
				m_titleLines.clear();
			std::vector<CString> lines;
			CString r = rest;
			for (int line = 0; line < 3 && !r.IsEmpty() && lineW > 0; ++line)
			{
				if (line == 2 || TextFB::Width(dc, r) <= lineW)
				{
					lines.push_back(r);   // 마지막 줄: 넘치면 그릴 때 "…"
					break;
				}
				// 이 줄에 들어가는 글자 수 (최소 1자), 가능하면 공백에서 끊음
				int fit = 1;
				while (fit < r.GetLength() && TextFB::Width(dc, r.Left(fit + 1)) <= lineW)
					++fit;
				const int sp = r.Left(fit + 1).ReverseFind(L' ');
				if (sp > 0 && sp <= fit)
					fit = sp;
				lines.push_back(r.Left(fit));
				r = r.Mid(fit);
				r.TrimLeft();
			}
			lit = m_titleLines.emplace(lkey, std::move(lines)).first;
		}
		const std::vector<CString>& lines = lit->second;
		for (size_t line = 0; line < lines.size(); ++line)
		{
			CRect lr(x, y + m_vcardSubLine * static_cast<int>(line), right, y + m_vcardSubLine * static_cast<int>(line + 1));
			TextFB::Draw(dc, lines[line], lr, DT_LEFT, line + 1 == lines.size());   // 마지막 줄만 "…"
		}
	}
	y += m_vcardSubLine * 3 + m_cardPad;
	dc->SelectObject(normal);   // 아래 개수 줄은 기본 글꼴

	// 구분선
	dc->FillSolidRect(rc.left, y, rc.Width(), 1, kCardLine);
	y += 1 + m_cardPad;

	// 태그 수 · 배우 수 · 물방울 카운트 (가운데, 0 이면 생략)
	const int tags = static_cast<int>(CVideoLibrary::SplitList(v.tags).size());
	const int actors = static_cast<int>(CVideoLibrary::SplitList(v.actors).size());
	const int drops = v.oCount;
	CString tText, aText, oText;
	tText.Format(L"%d", tags);
	aText.Format(L"%d", actors);
	oText.Format(L"%d", drops);
	const int iconR = (std::max)(4, m_cardLine * 2 / 5);
	const int gapIcon = DX(3), gapGroup = DX(10);
	int total = 0;
	if (tags > 0)
		total += iconR * 2 + gapIcon + dc->GetTextExtent(tText).cx;
	if (actors > 0)
		total += (total > 0 ? gapGroup : 0) + iconR * 2 + gapIcon + dc->GetTextExtent(aText).cx;
	if (drops > 0)
		total += (total > 0 ? gapGroup : 0) + iconR * 2 + gapIcon + dc->GetTextExtent(oText).cx;
	int sx = rc.left + (rc.Width() - total) / 2;
	const int cy = y + m_cardLine / 2;
	dc->SetTextColor(kTextColor);
	if (tags > 0)
	{
		HotCountBack(dc, m_grid, sx, iconR * 2 + gapIcon + dc->GetTextExtent(tText).cx, y);   // 마우스 오버 = 배경
		DrawTagIcon(dc, sx + iconR, cy, iconR, kCardIcon, kCardBack);
		sx += iconR * 2 + gapIcon;
		dc->TextOut(sx, y, tText);
		sx += dc->GetTextExtent(tText).cx + gapGroup;
	}
	if (actors > 0)
	{
		HotCountBack(dc, m_grid, sx, iconR * 2 + gapIcon + dc->GetTextExtent(aText).cx, y);
		DrawPersonIcon(dc, sx + iconR, cy, iconR, kCardIcon);
		sx += iconR * 2 + gapIcon;
		dc->TextOut(sx, y, aText);
		sx += dc->GetTextExtent(aText).cx + gapGroup;
	}
	if (drops > 0)
	{
		HotCountBack(dc, m_grid, sx, iconR * 2 + gapIcon + dc->GetTextExtent(oText).cx, y);
		VectorIcon::Drops(dc, CRect(sx - 1, cy - iconR - 1, sx + iconR * 2 + 2, cy + iconR + 2), kCardIcon);   // 물방울 카운트
		sx += iconR * 2 + gapIcon;
		dc->TextOut(sx, y, oText);
	}

	// 선택 테두리
	if (selected)
	{
		CPen pen(PS_SOLID, 2, focused ? kButtonColor : TCOL(TC_BORDER));
		CPen* op = dc->SelectObject(&pen);
		CBrush* ob = static_cast<CBrush*>(dc->SelectStockObject(NULL_BRUSH));
		CRect sr = rc;
		sr.DeflateRect(1, 1);
		dc->RoundRect(sr, CPoint(radius, radius));
		dc->SelectObject(op);
		dc->SelectObject(ob);
	}
	dc->SelectObject(oldFont);
}

CBitmap* CRuliManagerDlg::GetActorPortrait(int actorIdx, int w, int h)
{
	if (actorIdx < 0 || actorIdx >= static_cast<int>(m_lib.actors.size()) || w <= 0 || h <= 0)
		return nullptr;
	const ActorInfo& a = m_lib.actors[actorIdx];
	if (a.photo.IsEmpty())
		return nullptr;
	CString key;
	key.Format(L"%s|%dx%d", static_cast<LPCWSTR>(a.photo), w, h);
	key.MakeLower();
	auto it = m_actorPortraits.find(key);
	if (it != m_actorPortraits.end())
		return it->second.get();   // 없으면 nullptr 이 저장되어 있음 (다시 읽지 않음)

	std::unique_ptr<CBitmap> bmp;
	CImage img;
	if (::PathFileExistsW(a.photo) && LoadImageFileSfw(img, a.photo) && !img.IsNull())   // SFW: 모자이크
	{
		CClientDC screen(this);
		bmp = std::make_unique<CBitmap>();
		bmp->CreateCompatibleBitmap(&screen, w, h);
		CDC mem;
		mem.CreateCompatibleDC(&screen);
		CBitmap* old = mem.SelectObject(bmp.get());
		mem.FillSolidRect(0, 0, w, h, kCardImage);
		// 카드를 꽉 채우도록 확대 (가로는 가운데, 세로는 얼굴이 보이게 위쪽 기준으로 자름)
		const double iw = img.GetWidth(), ih = img.GetHeight();
		const double scale = (std::max)(w / iw, h / ih);
		const int sw = (std::max)(1, static_cast<int>(w / scale));
		const int sh = (std::max)(1, static_cast<int>(h / scale));
		const int sx = (std::max)(0, static_cast<int>((iw - sw) / 2));
		const int sy = (std::max)(0, static_cast<int>((ih - sh) * 0.15));
		mem.SetStretchBltMode(HALFTONE);
		::SetBrushOrgEx(mem.GetSafeHdc(), 0, 0, nullptr);
		img.StretchBlt(mem.GetSafeHdc(), 0, 0, w, h, sx, sy, sw, sh, SRCCOPY);
		mem.SelectObject(old);
	}
	CBitmap* result = bmp.get();
	m_actorPortraits[key] = std::move(bmp);
	return result;
}

void CRuliManagerDlg::DrawActorCard(CDC* dc, int row, const CRect& rc, bool selected, bool focused)
{
	if (row < 0 || row >= static_cast<int>(m_actorRows.size()))
		return;
	const bool hot = (m_actorGrid.HotRow() == row && m_actorGrid.HotPart() == 1);   // 하트 자리에 마우스가 있을 때만
	DrawActorCardFor(dc, m_actorRows[row], rc, selected, focused, hot);
	// 왼쪽 위 체크 칸: 체크했으면 항상, 아니면 그 자리에 마우스가 있을 때만 빈 칸
	const bool checked = IsActorChecked(m_actorRows[row]);
	if (checked || (m_actorGrid.HotRow() == row && m_actorGrid.HotPart() == 2))
		DrawCardCheck(dc, ActorCheckRect(rc), checked);
}

void CRuliManagerDlg::DrawActorCardFor(CDC* dc, int idx, const CRect& rc, bool selected, bool focused, bool heartHot, bool showCounts)
{
	if (idx < 0 || idx >= static_cast<int>(m_lib.actors.size()))
		return;
	const ActorInfo& a = m_lib.actors[idx];
	const int radius = DX(4);

	// 카드 배경 (둥근 모서리), 선택되면 파란 테두리
	{
		CBrush br(kCardBack);
		CPen pen(PS_SOLID, 1, kCardBack);
		CBrush* ob = dc->SelectObject(&br);
		CPen* op = dc->SelectObject(&pen);
		dc->RoundRect(rc, CPoint(radius, radius));
		dc->SelectObject(ob);
		dc->SelectObject(op);
	}

	// 사진 (위쪽 모서리만 둥글게 보이도록 카드 모양으로 잘라 그림)
	const CRect img(rc.left, rc.top, rc.right, rc.top + m_cardImgH);
	{
		CRgn clip;
		clip.CreateRoundRectRgn(rc.left, rc.top, rc.right + 1, rc.bottom + 1, radius, radius);
		dc->SelectClipRgn(&clip);
		dc->IntersectClipRect(img);
		if (CBitmap* bmp = GetActorPortrait(idx, img.Width(), img.Height()))
		{
			CDC mem;
			mem.CreateCompatibleDC(dc);
			CBitmap* old = mem.SelectObject(bmp);
			dc->BitBlt(img.left, img.top, img.Width(), img.Height(), &mem, 0, 0, SRCCOPY);
			mem.SelectObject(old);
		}
		else
		{
			dc->FillSolidRect(img, kCardImage);
			DrawDefaultActorImage(dc, img, a);   // 배우 기본 이미지 (벡터)
		}
		dc->SelectClipRgn(nullptr);
	}

	// 별점: 사진 왼쪽 위 대각선 리본
	DrawRatingRibbon(dc, rc, img, radius, a.rating, GetFont());

	// 즐겨찾기: 오른쪽 위 하트 (지정 = 빨간 하트, 하트 자리에 마우스 오버 = 반투명 회색 하트, 누르면 전환)
	{
		const bool hot = heartHot;
		const CRect heart = ActorHeartRect(rc);
		if (a.favorite)
			DrawHeart(dc, heart, RGB(0xF2, 0x5C, 0x54), 255, true);
		else if (hot)
			DrawHeart(dc, heart, RGB(0xC8, 0xCC, 0xD0), 170, true);
	}

	// 국기: 사진 오른쪽 아래
	const int flag = CCountryCombo::FindCountry(a.nationality);
	if (flag >= 0)
		CCountryCombo::DrawFlag(dc, flag, img.right - m_cardPad - CCountryCombo::kFlagW, img.bottom - m_cardPad - CCountryCombo::kFlagH);

	CFont* normal = GetFont();
	CFont* oldFont = dc->SelectObject(normal);
	dc->SetBkMode(TRANSPARENT);

	// 성별 기호 + 이름(굵게, 흰색), 최대 2줄 (별칭은 표시 안 함)
	int x = rc.left + m_cardPad;
	int y = img.bottom + m_actorNameGap;   // 사진 바로 아래 (기본 여백보다 2pt 좁게)
	const int right = rc.right - m_cardPad;
	int textX = x;
	CString gsym;
	COLORREF gcol = kTextColor;
	if (GenderSymbol(a.gender, gsym, gcol))
	{
		dc->SelectObject(&m_actorSymbolFont);   // 배우 탭 카드: 2pt 큰 성별 기호
		dc->SetTextColor(gcol);
		const int symW = TextFB::Width(dc, gsym);   // ♀ ♂ ⚧ ⚥ ⚲ (글꼴에 없으면 대체 글꼴)
		CRect sr(x, y, x + symW, y + m_actorLineB * 2);   // 이름 첫 두 줄 높이에 맞춰 가운데
		TextFB::Draw(dc, gsym, sr, DT_LEFT, false);
		textX = x + symW + DX(3) + m_vcardMemoGap;   // 성별 기호 오른쪽 여백 (+2pt)
	}

	// 이름에 "(...)" 가 있으면: 첫 줄 = 괄호 앞 이름(굵게), 둘째 줄 = 괄호 안 이름(회색)
	CString mainName = a.name, subName;
	{
		int po = a.name.Find(L'(');
		if (po < 0) po = a.name.Find(L'\xFF08');   // 전각 （
		if (po > 0)
		{
			int pc = a.name.ReverseFind(L')');
			const int pcW = a.name.ReverseFind(L'\xFF09');   // 전각 ）
			if (pcW > pc) pc = pcW;
			if (pc < po) pc = a.name.GetLength();     // 닫는 괄호가 없으면 끝까지
			CString inner = a.name.Mid(po + 1, pc - po - 1);
			inner.Trim();
			CString left = a.name.Left(po);
			left.Trim();
			if (!left.IsEmpty() && !inner.IsEmpty())
			{
				mainName = left;
				subName = inner;
			}
		}
	}
	if (!subName.IsEmpty())
	{
		dc->SelectObject(&m_actorNameFont);
		dc->SetTextColor(kTextColor);
		TextFB::Draw(dc, mainName, CRect(textX, y, right, y + m_actorLineB), DT_LEFT, true);
		// 괄호 안에 쉼표가 있으면 나눠서 줄마다 (최대 2줄, 남는 것은 둘째 줄에 이어서)
		std::vector<CString> parts;
		{
			CString t = subName;
			t.Replace(L'\xFF0C', L',');   // 전각 ，
			t.Replace(L'\x3001', L',');   // 、
			int start = 0;
			for (;;)
			{
				const int comma = t.Find(L',', start);
				CString part = (comma < 0) ? t.Mid(start) : t.Mid(start, comma - start);
				part.Trim();
				if (!part.IsEmpty())
					parts.push_back(part);
				if (comma < 0)
					break;
				start = comma + 1;
			}
		}
		if (parts.size() > 2)
		{
			for (size_t k = 2; k < parts.size(); ++k)
				parts[1] += L", " + parts[k];
			parts.resize(2);
		}
		dc->SelectObject(&m_cardSubFont);   // 기본보다 한 단계 큰 글꼴
		dc->SetTextColor(kCardSub);
		int sy = y + m_actorLineB;
		for (const CString& part : parts)
		{
			TextFB::Draw(dc, part, CRect(textX, sy, right, sy + m_cardSubLine), DT_LEFT, true);
			sy += m_cardSubLine;
		}
	}
	else
	{
		dc->SelectObject(&m_actorNameFont);
		const int width = (std::max)(10, right - textX);
		const CString& name = a.name;
		int lineY = y;
		int pos = 0;
		int lines = 0;
		while (pos < name.GetLength() && lines < 2)
		{
			int end = FitChars(dc, name, pos, width, true);
			CString part = name.Mid(pos, end - pos);
			if (lines == 1 && end < name.GetLength())
			{
				// 둘째 줄에서 넘치면 … 으로 줄임
				CRect tr(textX, lineY, right, lineY + m_actorLineB);
				CString rest = name.Mid(pos);
				dc->SetTextColor(kTextColor);
				TextFB::Draw(dc, rest, tr, DT_LEFT, true);
				pos = name.GetLength();
				++lines;
				break;
			}
			part.TrimRight();
			dc->SetTextColor(kTextColor);
			TextFB::Draw(dc, part, CRect(textX, lineY, right, lineY + m_actorLineB), DT_LEFT, false);   // 글꼴 대체
			pos = end;
			while (pos < name.GetLength() && name[pos] == L' ')
				++pos;
			++lines;
			if (pos < name.GetLength())
				lineY += m_actorLineB;
		}

	}
	y += (std::max)(m_actorLineB * 2, m_actorLineB + m_cardSubLine * 2);   // 이름 영역 (m_cardH 계산과 같게)

	// 나이
	dc->SelectObject(normal);
	const int age = CVideoLibrary::CalcAge(a.birth);
	if (age >= 0)
	{
		CString ageText;
		ageText.Format(L"%d 살", age);
		dc->SetTextColor(kCardSub);
		dc->TextOut(x, y, ageText);
	}
	y += m_cardLine + m_cardPad / 2;

	if (showCounts)   // 영상 상세 배우 칩 팝업에서는 뺌
	{
		// 구분선
		dc->FillSolidRect(rc.left, y, rc.Width(), 1, kCardLine);
		y += 1 + m_cardPad;

		// ▶ 출연 수 (가운데)
		CString key = a.name;
		key.MakeLower();
		auto itc = m_actorCounts.find(key);
		const int videos = (itc != m_actorCounts.end()) ? itc->second : 0;

		CString vText;
		vText.Format(L"%d", videos);
		const int iconR = (std::max)(4, m_cardLine * 2 / 5);
		const int gapIcon = DX(3);
		const int total = iconR * 2 + gapIcon + dc->GetTextExtent(vText).cx;
		int sx = rc.left + (rc.Width() - total) / 2;
		const int cy = y + m_cardLine / 2;
		HotCountBack(dc, m_actorGrid, sx, total, y);   // 마우스 오버 = 배경
		DrawPlayIcon(dc, sx + iconR, cy, iconR, kCardIcon, kCardBack);
		sx += iconR * 2 + gapIcon;
		dc->SetTextColor(kTextColor);
		dc->TextOut(sx, y, vText);
	}

	// 선택 테두리
	if (selected)
	{
		CPen pen(PS_SOLID, 2, focused ? kButtonColor : TCOL(TC_BORDER));
		CPen* op = dc->SelectObject(&pen);
		CBrush* ob = static_cast<CBrush*>(dc->SelectStockObject(NULL_BRUSH));
		CRect sr = rc;
		sr.DeflateRect(1, 1);
		dc->RoundRect(sr, CPoint(radius, radius));
		dc->SelectObject(op);
		dc->SelectObject(ob);
	}
	dc->SelectObject(oldFont);
}

int CRuliManagerDlg::ActorGridOwner::GridGetCount()
{
	if (dlg->ActorTreemapActive())
		return 0;   // [트리맵] 보기: 카드 없이 도구줄 + 차트만
	return static_cast<int>(dlg->m_actorRows.size());
}

int CRuliManagerDlg::ActorGridOwner::GridGetSel()
{
	return dlg->m_actorSel;
}

void CRuliManagerDlg::ActorGridOwner::GridSetSel(int row)
{
	dlg->SelectActorRow(row);
}

void CRuliManagerDlg::ActorGridOwner::GridGetItem(int row, int& image, CString& name, CString& line2, CString& line3)
{
	image = 0;
	if (row < 0 || row >= static_cast<int>(dlg->m_actorRows.size()))
		return;
	const int idx = dlg->m_actorRows[row];
	const ActorInfo& a = dlg->m_lib.actors[idx];
	image = dlg->GetActorThumbIndex(idx);
	name = a.name;

	CString key = a.name;
	key.MakeLower();
	auto it = dlg->m_actorCounts.find(key);
	// 둘째 줄: 나이 · 출연 수
	line2.Format(L"출연 %d편", it != dlg->m_actorCounts.end() ? it->second : 0);
	const int age = CVideoLibrary::CalcAge(a.birth);
	if (age >= 0)
	{
		CString t;
		t.Format(L"%d세 · ", age);
		line2 = t + line2;
	}

	// 셋째 줄(2줄): 국적 · 키 · 데뷔 → 별칭 → 메모
	std::vector<CString> parts;
	if (!a.gender.IsEmpty())      parts.push_back(a.gender);
	if (!a.nationality.IsEmpty()) parts.push_back(a.nationality);
	if (!a.height.IsEmpty())      parts.push_back(a.height + L"cm");
	if (!a.debut.IsEmpty())       parts.push_back(L"데뷔 " + a.debut.Left(4));
	if (!a.retire.IsEmpty())      parts.push_back(L"은퇴 " + a.retire.Left(4));
	if (!a.aliases.IsEmpty())     parts.push_back(L"별칭: " + a.aliases);
	if (!a.memo.IsEmpty())        parts.push_back(a.memo);
	line3.Empty();
	for (const CString& p : parts)
	{
		if (!line3.IsEmpty()) line3 += L" · ";
		line3 += p;
	}
}

void CRuliManagerDlg::ActorGridOwner::GridActivate(int row)
{
	dlg->DrillIntoActor(row);
}

void CRuliManagerDlg::ActorGridOwner::GridKey(UINT vk)
{
	switch (vk)
	{
	case VK_F2:    dlg->OnActorEdit(); break;
	case VK_DELETE: dlg->OnActorDelete(); break;
	case VK_SPACE: dlg->OnActorShowVideos(); break;
	case VK_F5:    dlg->OnBnClickedRefresh(); break;
	case 'F':
		if (::GetKeyState(VK_CONTROL) & 0x8000)
		{
			dlg->m_editSearch.SetFocus();
			dlg->m_editSearch.SetSel(0, -1);
		}
		break;
	}
}

// ---------------------------------------------------------------------------
// 미리보기 이미지

void CRuliManagerDlg::OnBnClickedChangeImage()
{
	if (m_curItem < 0 || m_curItem >= static_cast<int>(m_lib.items.size()) || IsItemOffline(m_curItem))
		return;
	CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
		L"이미지 파일 (*.jpg;*.jpeg;*.png;*.webp;*.bmp;*.gif;*.tif;*.tiff;*.svg)|*.jpg;*.jpeg;*.png;*.webp;*.bmp;*.gif;*.tif;*.tiff;*.svg|모든 파일 (*.*)|*.*||",
		this);
	if (dlg.DoModal() != IDOK)
		return;
	const CString path = dlg.GetPathName();
	if (!SetPendingVideoImage(path, L"새 이미지: " + CString(::PathFindFileNameW(path)) + L"  (저장하면 적용)"))
		AfxMessageBox(L"이미지 파일을 열 수 없습니다.", MB_ICONWARNING);
}

bool CRuliManagerDlg::SetPendingVideoImage(const CString& path, const CString& label)
{
	if (m_curItem < 0 || m_curItem >= static_cast<int>(m_lib.items.size()))
		return false;
	if (IsItemOffline(m_curItem))
		return true;   // 연결 안 된 이동식 디스크 영상: 이미지 변경 안 함 (메시지 없이 무시)
	CImage test;
	if (!LoadImageFile(test, path))
		return false;
	// 저장 전까지는 미리보기만 바꿈 → [저장] 하면 영상 파일 옆에 복사
	m_pendingImage = path;   // (미리보기는 SFW 모자이크)
	m_preview.Clear();
	m_preview.SetImageFile(path, true);
	m_staticImage.SetWindowText(label);
	OnDetailsChanged();
	return true;
}

void CRuliManagerDlg::OnDropFiles(HDROP hDropInfo)
{
	// 영상 상세(영상 탭 / 배우 출연작)의 이미지 영역에 놓은 첫 번째 파일 → 새 이미지 (저장하면 적용)
	CPoint pt;
	::DragQueryPoint(hDropInfo, &pt);   // 대화상자 클라이언트 좌표
	CString file;
	const UINT count = ::DragQueryFileW(hDropInfo, 0xFFFFFFFF, nullptr, 0);
	if (count > 0)
	{
		const UINT len = ::DragQueryFileW(hDropInfo, 0, nullptr, 0);
		::DragQueryFileW(hDropInfo, 0, file.GetBuffer(len + 1), len + 1);
		file.ReleaseBuffer();
	}
	::DragFinish(hDropInfo);

	if (file.IsEmpty() || !ShowVideoDetail() || !m_preview.GetSafeHwnd() || !m_preview.IsWindowVisible())
		return;
	CRect pr;
	m_preview.GetWindowRect(&pr);
	ScreenToClient(&pr);
	if (!pr.PtInRect(pt))
		return;   // 이미지 영역 밖에 놓음
	if (m_curItem < 0 || m_curItem >= static_cast<int>(m_lib.items.size()))
	{
		AfxMessageBox(L"이미지를 지정할 영상을 먼저 선택하세요.", MB_ICONINFORMATION);
		return;
	}
	if (::GetFileAttributesW(file) & FILE_ATTRIBUTE_DIRECTORY)
		return;
	SetForegroundWindow();
	if (!SetPendingVideoImage(file, L"끌어다 놓은 새 이미지: " + CString(::PathFindFileNameW(file)) + L"  (저장하면 적용)"))
		AfxMessageBox(L"이미지 파일을 열 수 없습니다.", MB_ICONWARNING);
}

void CRuliManagerDlg::OnBnClickedSearchImage()
{
	if (m_curItem < 0 || m_curItem >= static_cast<int>(m_lib.items.size()) || IsItemOffline(m_curItem))
		return;
	// 검색어: 제목 칸 (비었으면 파일 이름), [2024.01.01] 같은 대괄호 부분은 뺌
	// 검색어: 품번이 있으면 품번, 없으면 제목 칸 (비었으면 파일 이름)
	CString query;
	m_editCode.GetWindowText(query);
	query.Trim();
	if (query.IsEmpty())
	{
		m_editTitle.GetWindowText(query);
		query.Trim();
	}
	if (query.IsEmpty())
	{
		query = m_lib.items[m_curItem].FileName();
		::PathRemoveExtensionW(query.GetBuffer());
		query.ReleaseBuffer();
	}
	for (;;)
	{
		const int a = query.Find(L'[');
		const int z = (a >= 0) ? query.Find(L']', a) : -1;
		if (a < 0 || z < 0)
			break;
		query.Delete(a, z - a + 1);
	}
	query.Replace(L'_', L' ');
	query.Trim();

	CImageSearchDlg dlg(query, this);
	if (dlg.DoModal() != IDOK || dlg.m_resultPath.IsEmpty())
		return;
	// 저장 전까지는 미리보기만 바꿈 → [저장] 하면 영상 파일 옆에 "영상이름.확장자" 로 복사
	m_pendingImage = dlg.m_resultPath;
	m_preview.Clear();
	m_preview.SetImageFile(m_pendingImage, true);
	m_staticImage.SetWindowText(L"검색한 새 이미지  (저장하면 적용)");
	OnDetailsChanged();
}

void CRuliManagerDlg::RecyclePartImages(const std::vector<CString>& videoPaths, const CString& keepBase)
{
	// 분할 파일마다 같은 이름의 이미지(ABC-123_2.jpg / ABC-123_2.mp4.jpg)를 휴지통으로 (대표 이름 이미지는 유지)
	static const wchar_t* const kImageExts[] = { L".jpg", L".jpeg", L".png", L".webp", L".bmp", L".gif", L".tif", L".tiff", L".svg" };
	CString keepStem = keepBase;
	const int kext = static_cast<int>(::PathFindExtensionW(keepBase) - static_cast<LPCWSTR>(keepBase));
	if (kext > 0)
		keepStem = keepBase.Left(kext);
	std::vector<wchar_t> from;
	for (const CString& vp : videoPaths)
	{
		CString stem = vp;
		const int extPos = static_cast<int>(::PathFindExtensionW(vp) - static_cast<LPCWSTR>(vp));
		if (extPos > 0)
			stem = vp.Left(extPos);
		if (stem.CompareNoCase(keepStem) == 0)
			continue;   // 대표 이름 그 자체 (ABC-123.mp4) → 방금 저장한 이미지
		for (const wchar_t* e : kImageExts)
		{
			const CString c1 = stem + e, c2 = vp + e;
			for (const CString& c : { c1, c2 })
			{
				if (::PathFileExistsW(c))
				{
					from.insert(from.end(), static_cast<LPCWSTR>(c), static_cast<LPCWSTR>(c) + c.GetLength());
					from.push_back(L'\0');
				}
			}
		}
	}
	if (from.empty())
		return;
	from.push_back(L'\0');   // 이중 NULL 종료
	SHFILEOPSTRUCTW op = {};
	op.hwnd = m_hWnd;
	op.wFunc = FO_DELETE;
	op.pFrom = from.data();
	op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
	::SHFileOperationW(&op);
}

bool CRuliManagerDlg::ApplyVideoImage(const CString& videoPath, const CString& src)
{
	if (videoPath.IsEmpty() || !::PathFileExistsW(src))
		return false;

	// 대상: 영상 파일과 같은 이름 + 원본 이미지 확장자   예: D:\영상\movie.mp4 + a.png → D:\영상\movie.png
	CString stem = videoPath;
	const int extPos = static_cast<int>(::PathFindExtensionW(videoPath) - static_cast<LPCWSTR>(videoPath));
	if (extPos > 0 && extPos < videoPath.GetLength())
		stem = videoPath.Left(extPos);
	CString ext = ::PathFindExtensionW(src);
	ext.MakeLower();
	if (ext.IsEmpty() || ext.GetLength() > 5)
		ext = L".jpg";
	const CString dst = stem + ext;
	if (dst.CompareNoCase(src) == 0)
		return true;   // 이미 그 파일

	// 원본 이미지를 먼저 임시로 복사 (원본이 지울 대상 안에 있어도 안전하게)
	const CString tmp = stem + L".vmtmp" + ext;
	if (!::CopyFileW(src, tmp, FALSE))
		return false;

	// 기존 이미지(다른 확장자 포함, movie.jpg / movie.mp4.jpg 등)는 휴지통으로 → 새 이미지가 표시되도록
	static const wchar_t* const kImageExts[] = { L".jpg", L".jpeg", L".png", L".webp", L".bmp", L".gif", L".tif", L".tiff", L".svg" };
	std::vector<wchar_t> from;
	for (const wchar_t* e : kImageExts)
	{
		const CString c1 = stem + e, c2 = videoPath + e;
		for (const CString& c : { c1, c2 })
		{
			if (::PathFileExistsW(c))
			{
				from.insert(from.end(), static_cast<LPCWSTR>(c), static_cast<LPCWSTR>(c) + c.GetLength());
				from.push_back(L'\0');
			}
		}
	}
	if (!from.empty())
	{
		from.push_back(L'\0');   // 이중 NULL 종료
		SHFILEOPSTRUCTW op = {};
		op.hwnd = m_hWnd;
		op.wFunc = FO_DELETE;
		op.pFrom = from.data();
		op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
		::SHFileOperationW(&op);
	}

	m_preview.Clear();   // 미리보기가 파일을 쥐고 있지 않도록 (메모리로 읽으므로 보통은 필요 없음)
	const bool ok = ::MoveFileExW(tmp, dst, MOVEFILE_REPLACE_EXISTING) != FALSE;
	if (!ok)
		::DeleteFileW(tmp);
	else
		::SetFileAttributesW(dst, FILE_ATTRIBUTE_NORMAL);
	return ok;
}

CString CRuliManagerDlg::FindImageFor(const CString& videoPath)
{
	static const wchar_t* const kImageExts[] = {
		L".jpg", L".jpeg", L".png", L".webp", L".bmp", L".gif", L".tif", L".tiff", L".svg"
	};

	// 1) 확장자를 뺀 같은 이름   예: movie.mp4 → movie.jpg
	CString stem = videoPath;
	const int ext = static_cast<int>(::PathFindExtensionW(videoPath) - static_cast<LPCWSTR>(videoPath));
	if (ext > 0 && ext < videoPath.GetLength())
		stem = videoPath.Left(ext);
	for (const wchar_t* e : kImageExts)
	{
		const CString candidate = stem + e;
		if (::PathFileExistsW(candidate))
			return candidate;
	}

	// 2) 확장자까지 포함한 이름 예: movie.mp4 → movie.mp4.jpg
	for (const wchar_t* e : kImageExts)
	{
		const CString candidate = videoPath + e;
		if (::PathFileExistsW(candidate))
			return candidate;
	}

	// 3) '_' 왼쪽만 같은 이름   예: ABC-123.mp4 ↔ ABC-123_cover.jpg, ABC-123_1080p.mp4 ↔ ABC-123.jpg
	//    (같은 폴더, 대소문자 무시, 여러 개면 이름 순으로 첫 번째)
	auto keyOf = [](const CString& stemName) -> CString
	{
		const int us = stemName.Find(L'_');
		CString k = (us >= 0) ? stemName.Left(us) : stemName;
		k.Trim();
		return k;
	};
	const CString dir = videoPath.Left(static_cast<int>(::PathFindFileNameW(videoPath) - static_cast<LPCWSTR>(videoPath)));
	const CString key = keyOf(stem.Mid(dir.GetLength()));
	if (key.IsEmpty())
		return CString();

	CString best;
	WIN32_FIND_DATAW fd = {};
	HANDLE h = ::FindFirstFileW(dir + key + L"*", &fd);   // 이름이 key 로 시작하는 파일만
	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;
			const CString name = fd.cFileName;
			const LPCWSTR fext = ::PathFindExtensionW(name);
			bool isImage = false;
			for (const wchar_t* e : kImageExts)
			{
				if (_wcsicmp(fext, e) == 0) { isImage = true; break; }
			}
			if (!isImage)
				continue;
			const CString imgStem = name.Left(static_cast<int>(fext - static_cast<LPCWSTR>(name)));
			if (keyOf(imgStem).CompareNoCase(key) != 0)
				continue;   // ABC-1234.jpg 처럼 앞부분만 같은 다른 작품은 제외
			if (best.IsEmpty() || ::StrCmpLogicalW(name, best) < 0)
				best = name;
		} while (::FindNextFileW(h, &fd));
		::FindClose(h);
	}
	return best.IsEmpty() ? CString() : dir + best;
}

void CRuliManagerDlg::OpenImageViewer()
{
	// 상세 이미지 영역 더블클릭: 지금 보이는 이미지를 원본 크기로 팝업 창에
	//  - 영상 상세(영상 탭 / 배우 출연작 / 제작사 영상): 영상 이미지 (저장 전 새 이미지 · 커버 캐시 포함)
	//  - 배우 탭 상세: 배우 사진 (사진이 없어 기본 실루엣이면 열지 않음)
	CString caption;
	if (ShowVideoDetail())
	{
		if (m_curItem < 0 || m_curItem >= static_cast<int>(m_lib.items.size()))
			return;
		const VideoItem& v = m_lib.items[m_curItem];
		caption = v.code.IsEmpty() ? v.FileName() : v.code;
	}
	else if (m_mode == MODE_ACTOR)
	{
		if (m_actorSel < 0 || m_actorSel >= static_cast<int>(m_actorRows.size()))
			return;
		caption = m_lib.actors[m_actorRows[m_actorSel]].name;
	}
	else
		return;
	OpenImageViewerFile(m_preview.GetImageFile(), caption);
}

void CRuliManagerDlg::OpenImageViewerFile(const CString& path, const CString& caption)
{
	if (path.IsEmpty())
		return;   // 이미지 없음 (안내 문구 · 기본 이미지)
	if (!m_imageViewer.Open(this, path, true, caption))   // SFW 옵션이 켜져 있으면 모자이크
		AfxMessageBox(L"이미지 파일을 열 수 없습니다.", MB_ICONWARNING);
}

void CRuliManagerDlg::UpdatePreview(int idx)
{
	if (idx < 0 || idx >= static_cast<int>(m_lib.items.size()))
	{
		m_preview.Clear();
		m_preview.SetPlaceholder(L"동영상을 선택하세요.");
		m_staticImage.SetWindowText(L"");
		return;
	}

	const VideoItem& v = m_lib.items[idx];
	const CString image = FindImageFor(v.path);
	if (image.IsEmpty())
	{
		// 이동식 디스크 연결 안 됨: 커버 캐시가 있으면 그것을
		const CString cached = m_lib.CoverCacheFile(v.path);
		if (!cached.IsEmpty() && ::PathFileExistsW(cached))
		{
			m_preview.SetImageFile(cached, true);
			m_preview.SetDimmed(IsItemOffline(idx));   // 연결 안 됨: 카드처럼 반투명
			m_staticImage.SetWindowText(L"이미지: 커버 캐시 (이동식 디스크 연결 안 됨)");
			return;
		}
		m_preview.Clear();
		CString stem = v.FileName();
		::PathRemoveExtensionW(stem.GetBuffer());
		stem.ReleaseBuffer();
		m_preview.SetPlaceholder(L"이미지 없음\n\n같은 폴더에 '" + stem +
			L".jpg' (또는 .png / .webp / .bmp / .gif) 파일을 두면 여기에 표시됩니다.");
		m_staticImage.SetWindowText(L"이미지: 없음");
		return;
	}

	m_preview.SetImageFile(image, true);   // 영상 이미지 (SFW: 모자이크)
	m_preview.SetDimmed(IsItemOffline(idx));   // 연결 안 된 이동식 디스크 영상: 반투명
	m_staticImage.SetWindowText(L"이미지: " + CString(::PathFindFileNameW(image)));
}

// ---------------------------------------------------------------------------
// 폴더

void CRuliManagerDlg::OnBnClickedAddFolder()
{
	// [폴더 관리]: 등록 폴더를 칩 목록으로 보여 주는 창 (추가 · 제거 · 탐색기로 열기)
	m_folderPopup.Hide();
	CFolderManageDlg dlg(this);
	dlg.m_getItems = [this]()
	{
		std::vector<CFolderChipList::Item> items;
		for (const FolderRow& r : BuildFolderRows())
		{
			CFolderChipList::Item it;
			it.path = r.path;
			it.tag = r.tag;
			it.removable = r.removable;
			it.offline = r.offline;
			items.push_back(it);
		}
		return items;
	};
	dlg.m_onAdd = [this]() { return AddFolderInteractive(); };
	dlg.m_onRemove = [this](int i) { return RemoveFolderAt(i); };
	dlg.m_onRefreshState = [this]() { m_lib.UpdateRemovableState(); };   // 드라이브 문자 변경(경로 바꿈)은 메인 창의 장치 알림 처리가 함
	dlg.m_onOpen = [this](int i)
	{
		if (i >= 0 && i < static_cast<int>(m_lib.folders.size()))
			::ShellExecuteW(m_hWnd, L"open", m_lib.folders[i], nullptr, nullptr, SW_SHOWNORMAL);
	};
	dlg.DoModal();
}

// 등록 폴더 + 상태 (폴더 관리 창 목록 · [폴더 관리] 버튼 팝업 공용)
std::vector<CRuliManagerDlg::FolderRow> CRuliManagerDlg::BuildFolderRows()
{
	std::vector<FolderRow> rows;
	for (const CString& f : m_lib.folders)
	{
		FolderRow r;
		r.path = f;
		r.removable = m_lib.IsRemovableFolder(f);
		if (r.removable)
		{
			r.offline = !m_lib.RemovableConnected(f);
			r.tag = r.offline ? L"[이동식 디스크 - 연결 안 됨]" : L"[이동식 디스크]";
		}
		else if (::GetFileAttributesW(f) == INVALID_FILE_ATTRIBUTES)
		{
			r.offline = true;
			r.tag = L"[찾을 수 없음]";
		}
		rows.push_back(r);
	}
	return rows;
}

// 팝업 배치 (가로: 여백 · 아이콘 · 경로 · 상태 · 여백, 세로: 제목 + 폴더 줄)
CSize CRuliManagerDlg::FolderPopupLayout(CDC* dc)
{
	const int lineH = dc->GetTextExtent(L"Ag가").cy;
	const int pad = 10, rowH = lineH + 8, icon = lineH + 2, gap = 8;
	CFont* old = dc->SelectObject(&m_cardNameFont);
	CString title;
	title.Format(L"등록 폴더 %d개", static_cast<int>(m_folderPopupRows.size()));
	int w = dc->GetTextExtent(title).cx;
	const int titleH = dc->GetTextExtent(L"Ag가").cy;
	dc->SelectObject(old);
	for (const FolderRow& r : m_folderPopupRows)
	{
		int rw = icon + gap + dc->GetTextExtent(r.path).cx;
		if (!r.tag.IsEmpty())
			rw += gap + dc->GetTextExtent(r.tag).cx;
		w = (std::max)(w, rw);
	}
	if (m_folderPopupRows.empty())
		w = (std::max)(w, static_cast<int>(dc->GetTextExtent(L"등록된 폴더가 없습니다. 눌러서 폴더를 추가하세요.").cx));
	const int rows = (std::max)(1, static_cast<int>(m_folderPopupRows.size()));
	w = (std::min)(w + pad * 2, (std::max)(300, ::GetSystemMetrics(SM_CXSCREEN) * 2 / 3));   // 너무 긴 경로는 화면 2/3 에서 자름 (끝 … )
	return CSize(w + 2, pad + titleH + 6 + rows * rowH + pad - 4 + 2);
}

void CRuliManagerDlg::DrawFolderPopup(CDC* dc, const CRect& rc)
{
	CFont* oldFont = dc->SelectObject(GetFont());
	const int lineH = dc->GetTextExtent(L"Ag가").cy;
	const int pad = 10, rowH = lineH + 8, iconSz = lineH + 2, gap = 8;
	dc->SetBkMode(TRANSPARENT);
	// 테두리
	{
		CBrush edge(TCOL(TC_EDGE));
		dc->FrameRect(rc, &edge);
	}
	// 제목
	dc->SelectObject(&m_cardNameFont);
	const int titleH = dc->GetTextExtent(L"Ag가").cy;
	CString title;
	title.Format(L"등록 폴더 %d개", static_cast<int>(m_folderPopupRows.size()));
	dc->SetTextColor(TCOL(TC_TEXT));
	CRect tr(rc.left + pad, rc.top + pad, rc.right - pad, rc.top + pad + titleH);
	dc->DrawText(title, tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	dc->SelectObject(GetFont());

	int y = tr.bottom + 6;
	if (m_folderPopupRows.empty())
	{
		dc->SetTextColor(TCOL(TC_SUB2));
		CRect er(rc.left + pad, y, rc.right - pad, y + rowH);
		dc->DrawText(L"등록된 폴더가 없습니다. 눌러서 폴더를 추가하세요.", er, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	}
	for (const FolderRow& r : m_folderPopupRows)
	{
		const COLORREF text = r.offline ? TCOL(TC_SUB2) : TCOL(TC_TEXT);
		CRect icon(rc.left + pad, y + (rowH - iconSz) / 2, rc.left + pad + iconSz, y + (rowH - iconSz) / 2 + iconSz);
		if (r.removable)    VectorIcon::UsbDrive(dc, icon, text, 1.0);
		else if (r.offline) VectorIcon::FolderWarn(dc, icon, text, 1.0);
		else                VectorIcon::FolderPlay(dc, icon, text, 1.0);
		int right = rc.right - pad;
		if (!r.tag.IsEmpty())
		{
			// 상태는 오른쪽 끝에 붙여 항상 보이게 (경로가 길면 경로 쪽을 … 로 줄임)
			const int tw = dc->GetTextExtent(r.tag).cx;
			const int pathW = dc->GetTextExtent(r.path).cx;
			const int tx = (std::min)(right - tw, static_cast<int>(icon.right) + gap + pathW + gap);   // LONG → int (형 맞춤)
			dc->SetTextColor(r.offline ? RGB(0xF2, 0x9B, 0x54) : TCOL(TC_SUB2));
			CRect sr(tx, y, tx + tw, y + rowH);
			dc->DrawText(r.tag, sr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			right = tx - gap;
		}
		dc->SetTextColor(text);
		CRect pr(icon.right + gap, y, right, y + rowH);
		dc->DrawText(r.path, pr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_PATH_ELLIPSIS);
		y += rowH;
	}
	dc->SelectObject(oldFont);
}

void CRuliManagerDlg::ShowFolderPopup()
{
	CWnd* btn = GetDlgItem(IDC_BTN_ADDFOLDER);
	if (!btn || !m_folderPopup.GetSafeHwnd())
		return;
	m_folderPopup.m_back = TCOL(TC_COMBO);   // 테마가 바뀌었을 수 있음
	m_lib.UpdateRemovableState();   // 이동식 디스크 연결 상태를 지금 기준으로
	m_folderPopupRows = BuildFolderRows();
	CSize size;
	{
		CClientDC dc(this);
		CFont* old = dc.SelectObject(GetFont());
		size = FolderPopupLayout(&dc);
		dc.SelectObject(old);
	}
	CRect anchor;
	btn->GetWindowRect(&anchor);
	m_folderPopup.ShowNear(anchor, size);
	m_folderPopup.Invalidate(FALSE);
}

bool CRuliManagerDlg::AddFolderInteractive()
{
	CFolderPickerDialog dlg(nullptr, 0, this);
	dlg.m_ofn.lpstrTitle = L"동영상 폴더 선택";
	if (dlg.DoModal() != IDOK)
		return false;

	const CString folder = dlg.GetFolderPath();
	if (folder.IsEmpty())
		return false;

	// 이동식 디스크 지정: 디스크를 뺀 채로 실행 · 새로고침해도 영상 정보를 지우지 않고, 드라이브 문자가 바뀌어도 다시 연결
	bool removable = false;
	{
		wchar_t root[MAX_PATH] = {};
		wcsncpy_s(root, folder, _TRUNCATE);
		::PathStripToRootW(root);
		const bool looksRemovable = (::GetDriveTypeW(root) == DRIVE_REMOVABLE) || m_lib.IsRemovableFolder(folder);
		const int r = AfxMessageBox(L"이 폴더가 이동식 디스크(USB 메모리 · 외장 하드 등)에 있습니까?\n\n"
			L"[예] 이동식 디스크로 등록\n"
			L"  · 디스크를 뺀 채로 프로그램을 켜거나 새로고침해도 그 영상 정보가 지워지지 않음\n"
			L"  · 드라이브 문자가 바뀌어도(E: → F:) 같은 디스크를 찾아 다시 연결\n"
			L"  · 영상 카드 이미지에 \"이동식 디스크\" 표시\n"
			L"[아니요] 일반 폴더로 등록",
			MB_YESNOCANCEL | MB_ICONQUESTION | (looksRemovable ? MB_DEFBUTTON1 : MB_DEFBUTTON2));
		if (r == IDCANCEL)
			return false;
		removable = (r == IDYES);
	}

	CommitDetails();
	const CString keep = (m_curItem >= 0) ? m_lib.items[m_curItem].path : CString();

	int added = 0;
	{
		CWaitCursor wait;
		BeginLibraryChange();
		added = m_lib.AddFolder(folder, removable);
		m_lib.SyncActorsFromVideos();   // 폴더 구조로 찾은 배우/스튜디오를 목록에 추가
		m_lib.SyncNamedFromVideos();
		m_lib.Save();
		ApplyFilter();
		SelectPath(keep);
	}

	CString msg;
	msg.Format(L"'%s'\n\n새 동영상 %d개를 임시 목록에 추가했습니다.\n영상 정보를 저장하면 DB에 등록됩니다.", static_cast<LPCWSTR>(folder), added);
	AfxMessageBox(msg, MB_ICONINFORMATION);
	return true;
}

void CRuliManagerDlg::OnBnClickedRemoveFolder()
{
	OnBnClickedAddFolder();   // 예전 [폴더 제거] → 폴더 관리 창
}

bool CRuliManagerDlg::RemoveFolderAt(int idx)
{
	if (idx < 0 || idx >= static_cast<int>(m_lib.folders.size()))
		return false;
	const size_t index = static_cast<size_t>(idx);
	CString msg;
	msg.Format(L"'%s' 폴더를 목록에서 제거할까요?\n\n"
		L"※ 실제 파일은 삭제되지 않지만, 해당 동영상의 별점·태그·메모 정보는 사라집니다.",
		static_cast<LPCWSTR>(m_lib.folders[index]));
	if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION) != IDYES)
		return false;

	CWaitCursor wait;
	BeginLibraryChange();
	m_lib.RemoveFolder(index);
	m_lib.Save();
	ApplyFilter();
	return true;
}

void CRuliManagerDlg::OnVideoRefreshItem()
{
	// 선택한 영상만 새로고침: 영상 이미지(커버) · 해상도/재생 시간 · 파일 크기/수정일 · 스크린샷을 다시 읽음 (나눠진 영상은 묶음 전체)
	//  - 전체 [새로고침] 과 달리 폴더를 다시 훑지 않고, DB 의 정보(제목 · 배우 · 태그 …)는 바꾸지 않음
	const int idx = GetSelectedItem();
	if (idx < 0 || idx >= static_cast<int>(m_lib.items.size()) || IsItemOffline(idx))
		return;
	CommitDetails();
	std::vector<size_t> targets{ static_cast<size_t>(idx) };
	for (size_t s : m_lib.PartSiblings(static_cast<size_t>(idx)))
		targets.push_back(s);
	int missing = 0;
	for (size_t t : targets)
	{
		VideoItem& v = m_lib.items[t];
		CString low = v.path;
		low.MakeLower();
		// 커버 이미지 (카드 크기별 캐시: "경로|폭x높이")
		const CString prefix = low + L"|";
		for (auto it = m_videoCovers.begin(); it != m_videoCovers.end(); )
			it = (it->first.Left(prefix.GetLength()) == prefix) ? m_videoCovers.erase(it) : std::next(it);
		for (auto it = m_coverUse.begin(); it != m_coverUse.end(); )
			it = (it->first.Left(prefix.GetLength()) == prefix) ? m_coverUse.erase(it) : std::next(it);
		for (auto it = m_coverPending.begin(); it != m_coverPending.end(); )
			it = (it->Left(prefix.GetLength()) == prefix) ? m_coverPending.erase(it) : std::next(it);
		m_thumbIndex.erase(CVideoLibrary::MakeKey(v.path));
		// 해상도 · 재생 시간 (카드 · 상세)
		m_cardMedia.erase(low);
		m_cardMediaRequested.erase(low);
		m_mediaInfo.Forget(v.path);
		// 파일 크기 · 수정일
		WIN32_FILE_ATTRIBUTE_DATA fa = {};
		if (::GetFileAttributesExW(v.path, GetFileExInfoStandard, &fa))
		{
			v.size = (static_cast<ULONGLONG>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
			v.modified = (static_cast<ULONGLONG>(fa.ftLastWriteTime.dwHighDateTime) << 32) | fa.ftLastWriteTime.dwLowDateTime;
		}
		else
			++missing;
	}
	// 스크린샷 (축소 이미지 · 목록 다시 읽기)
	m_shotThumbs.clear();
	m_shotFiles.clear();
	m_shotVideo.Empty();
	m_lib.Save();

	m_grid.Invalidate(FALSE);
	if (m_yearView.GetSafeHwnd() && m_yearView.IsWindowVisible())
		m_yearView.Invalidate(FALSE);
	if (m_curItem >= 0)
		ShowDetails(m_curItem);   // 상세 이미지 · 해상도 · 스크린샷 다시
	RefreshShotStrip();
	UpdateStatus();
	CString s;
	s.Format(L"새로고침: %s%s", static_cast<LPCWSTR>(m_lib.items[idx].FileName()),
		targets.size() > 1 ? static_cast<LPCWSTR>(CString(L" (나눠진 파일 묶음 전체)")) : L"");
	if (missing > 0)
	{
		CString m;
		m.Format(L" - 파일을 찾지 못한 영상 %d개", missing);
		s += m;
	}
	m_staticStatus.SetWindowText(s);
}

void CRuliManagerDlg::OnBnClickedRefresh()
{
	CommitDetails();
	const CString keep = (m_curItem >= 0) ? m_lib.items[m_curItem].path : CString();

	int added = 0, removed = 0, relinked = 0;
	{
		CWaitCursor wait;
		BeginLibraryChange();
		m_lib.Refresh(added, removed, &relinked);   // 경로가 바뀐 파일은 기존 정보에 다시 연결
		m_lib.SyncActorsFromVideos();   // 폴더 구조로 찾은 배우/스튜디오를 목록에 추가
		m_lib.SyncNamedFromVideos();
		ResetThumbnails();   // 이미지가 추가/변경됐을 수 있음
		m_lib.Save();
		ApplyFilter();
		SelectPath(keep);
	}

	CString s;
	s.Format(L"새로고침 완료: 경로가 바뀐 파일 %d개 다시 연결, 새 파일 %d개 임시 목록에 추가, 없어진 파일 %d개 제거 (임시 %d개는 정보를 저장하면 DB 등록)",
		relinked, added, removed, m_lib.PendingCount());
	m_staticStatus.SetWindowText(s);
}

// ---------------------------------------------------------------------------
// 파일 작업

void CRuliManagerDlg::OnBnClickedRename()
{
	const int row = GetSelectedRow();
	if (row < 0)
	{
		AfxMessageBox(L"이름을 바꿀 동영상을 선택하세요.", MB_ICONINFORMATION);
		return;
	}
	{
		// 격자 보기: 입력 창으로 이름 변경
		CInputDlg dlg(m_lib.items[m_view[row]].FileName(), this);
		if (dlg.DoModal() == IDOK)
			RenameRow(row, dlg.m_value);
		m_grid.SetFocus();
	}
}

void CRuliManagerDlg::OnLvnBeginLabelEdit(NMHDR* pNMHDR, LRESULT* pResult)
{
	UNREFERENCED_PARAMETER(pNMHDR);
	// 클릭만으로 편집 모드에 들어가지 않도록 [이름 변경]/F2 에서만 허용
	*pResult = m_allowLabelEdit ? FALSE : TRUE;
	m_allowLabelEdit = false;
}

void CRuliManagerDlg::OnLvnEndLabelEdit(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLVDISPINFO* p = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);
	*pResult = FALSE;   // 가상 목록이므로 텍스트는 데이터에서 다시 읽음

	if (!p->item.pszText)   // 취소(ESC)
		return;
	RenameRow(p->item.iItem, p->item.pszText);
}

void CRuliManagerDlg::RenameRow(int row, CString newName)
{
	if (row < 0 || row >= static_cast<int>(m_view.size()))
		return;

	const int idx = m_view[row];
	VideoItem& v = m_lib.items[idx];

	newName.Trim();
	if (newName.IsEmpty())
		return;
	if (newName.FindOneOf(L"\\/:*?\"<>|") >= 0)
	{
		AfxMessageBox(L"파일 이름에 다음 문자는 사용할 수 없습니다.\n\\ / : * ? \" < > |", MB_ICONWARNING);
		return;
	}

	const CString oldName = v.FileName();
	if (newName == oldName)
		return;

	// 확장자를 지운 경우 원래 확장자를 유지
	if (*::PathFindExtensionW(newName) == 0)
		newName += ::PathFindExtensionW(v.path);

	const CString dir = v.path.Left(v.path.GetLength() - oldName.GetLength());
	const CString newPath = dir + newName;

	if (newPath.CompareNoCase(v.path) != 0 && ::PathFileExistsW(newPath))
	{
		AfxMessageBox(L"같은 이름의 파일이 이미 있습니다.", MB_ICONWARNING);
		return;
	}

	const CString oldPath = v.path;
	const CString oldImage = FindImageFor(oldPath);
	if (idx == m_curItem)
		m_preview.Clear();

	if (!MoveFileWithRetry(oldPath, newPath))
	{
		AfxMessageBox(L"이름을 바꾸지 못했습니다.\n" + LastErrorText(::GetLastError()), MB_ICONERROR);
		if (idx == m_curItem) UpdatePreview(idx);
		return;
	}
	m_thumbIndex.erase(CVideoLibrary::MakeKey(oldPath));
	v.path = newPath;

	// 같은 이름의 이미지 파일도 함께 이름 변경
	if (!oldImage.IsEmpty())
	{
		CString newImage;
		if (oldImage.GetLength() > oldPath.GetLength() &&
			_wcsnicmp(oldImage, oldPath, oldPath.GetLength()) == 0)
		{
			// movie.mp4.jpg 형식
			newImage = newPath + oldImage.Mid(oldPath.GetLength());
		}
		else
		{
			// movie.jpg 형식
			CString newStem = newPath;
			::PathRemoveExtensionW(newStem.GetBuffer());
			newStem.ReleaseBuffer();
			newImage = newStem + ::PathFindExtensionW(oldImage);
		}

		if (newImage.CompareNoCase(oldImage) != 0)
		{
			if (::PathFileExistsW(newImage) && newImage.CompareNoCase(oldImage) != 0)
				AfxMessageBox(L"동영상 이름은 바꿨지만, 같은 이름의 이미지 파일이 이미 있어 이미지 이름은 바꾸지 않았습니다.", MB_ICONWARNING);
			else if (!MoveFileWithRetry(oldImage, newImage))
				AfxMessageBox(L"동영상 이름은 바꿨지만, 이미지 이름은 바꾸지 못했습니다.\n" + LastErrorText(::GetLastError()), MB_ICONWARNING);
		}
	}

	m_lib.Save();
	if (idx == m_curItem)
	{
		m_staticName.SetWindowText(v.FileName());
		UpdatePreview(idx);
	}
	m_list.RedrawItems(row, row);
	m_grid.Invalidate(FALSE);
}

void CRuliManagerDlg::OnBnClickedDelete()
{
	// 선택한 영상의 DB 정보(제목/별점/배우/스튜디오/발매일/태그/메모)만 삭제 - 파일은 그대로 둠
	const int row = GetSelectedRow();
	const int idx = GetSelectedItem();
	if (idx < 0)
	{
		AfxMessageBox(L"DB에서 삭제할 동영상을 선택하세요.", MB_ICONINFORMATION);
		return;
	}

	if (IsItemChecked(idx))
	{
		OnVideoDeleteChecked();   // 선택한 영상이 체크되어 있으면 체크한 영상 모두 (메뉴 "DB에서 삭제(건수)" 와 같음)
		return;
	}
	const VideoItem old = m_lib.items[idx];
	const bool exists = ::PathFileExistsW(old.path) != FALSE;
	CString msg;
	if (old.pending)
		msg.Format(L"임시 항목을 목록에서 뺄까요?\n(파일은 삭제되지 않으며, 다음 폴더 스캔 때 다시 임시 항목으로 나타납니다)\n\n%s",
			static_cast<LPCWSTR>(old.path));
	else if (exists)
		msg.Format(L"선택한 영상의 DB 정보를 삭제할까요?\n(파일은 삭제되지 않고 임시 항목으로 돌아갑니다)\n\n%s",
			static_cast<LPCWSTR>(old.path));
	else
		msg.Format(L"선택한 영상의 DB 정보를 삭제할까요?\n(파일이 없어서 목록에서도 빠집니다)\n\n%s",
			static_cast<LPCWSTR>(old.path));
	if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;

	BeginLibraryChange();
	if (!old.pending && exists)
	{
		// 저장된 정보는 모두 지우고 스캔으로 처음 찾은 상태(임시 항목)로 되돌림
		VideoItem fresh;
		fresh.path = old.path;
		fresh.size = old.size;
		fresh.modified = old.modified;
		fresh.pending = true;
		m_lib.ApplyFolderStructure(fresh);   // 폴더 구조/이름의 배우·스튜디오·발매일은 다시 자동 지정
		m_lib.items[idx] = fresh;
	}
	else
	{
		m_lib.items.erase(m_lib.items.begin() + idx);
	}
	m_lib.Save();
	ApplyFilter();

	// 같은 자리(없으면 마지막) 항목 선택
	if (!m_view.empty())
	{
		const int next = (std::min)((std::max)(row, 0), static_cast<int>(m_view.size()) - 1);
		m_list.SetItemState(next, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_list.EnsureVisible(next, FALSE);
	}
}

void CRuliManagerDlg::OnBnClickedOpenDb()
{
	// 라이브러리(DB) 파일이 있는 폴더를 탐색기로 열고 파일을 선택
	CommitDetails();
	m_lib.Save();   // 최신 내용이 파일에 있도록

	const CString path = CVideoLibrary::GetDataFilePath();   // <실행 파일 폴더>\library.vmdb (정보 + 이미지 하나로)
	if (::PathFileExistsW(path))
	{
		PIDLIST_ABSOLUTE pidl = ::ILCreateFromPathW(path);
		if (pidl)
		{
			::SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
			::ILFree(pidl);
			return;
		}
	}

	// 파일이 없으면 폴더만 열기
	CString folder = path;
	::PathRemoveFileSpecW(folder.GetBuffer());
	folder.ReleaseBuffer();
	::ShellExecuteW(m_hWnd, L"open", folder, nullptr, nullptr, SW_SHOWNORMAL);
}

void CRuliManagerDlg::OnBnClickedExplorer()
{
	const int idx = GetSelectedItem();
	if (idx < 0)
	{
		AfxMessageBox(L"동영상을 선택하세요.", MB_ICONINFORMATION);
		return;
	}
	if (IsItemOffline(idx))
		return;   // 연결 안 된 이동식 디스크 영상: 아무 동작 안 함 (더블클릭 · Enter · Space 포함)

	const CString path = m_lib.items[idx].path;
	PIDLIST_ABSOLUTE pidl = ::ILCreateFromPathW(path);
	if (pidl)
	{
		::SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
		::ILFree(pidl);
	}
	else
	{
		CString args = L"/select,\"" + path + L"\"";
		::ShellExecuteW(m_hWnd, L"open", L"explorer.exe", args, nullptr, SW_SHOWNORMAL);
	}
}

void CRuliManagerDlg::OnBnClickedOpenDefault()
{
	const int idx = GetSelectedItem();
	if (idx < 0)
	{
		AfxMessageBox(L"동영상을 선택하세요.", MB_ICONINFORMATION);
		return;
	}
	if (IsItemOffline(idx))
		return;   // 연결 안 된 이동식 디스크 영상: 아무 동작 안 함 (더블클릭 · Enter · Space 포함)

	const CString path = m_lib.items[idx].path;
	if (!::PathFileExistsW(path))
	{
		AfxMessageBox(L"파일이 존재하지 않습니다.\n[새로고침]으로 목록을 갱신하세요.", MB_ICONWARNING);
		return;
	}
	const HINSTANCE h = ::ShellExecuteW(m_hWnd, L"open", path, nullptr, nullptr, SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(h) <= 32)
		AfxMessageBox(L"기본 프로그램으로 열지 못했습니다.", MB_ICONWARNING);
}

// ---------------------------------------------------------------------------
// 오른쪽 클릭 메뉴

void CRuliManagerDlg::OnContextMenu(CWnd* pWnd, CPoint point)
{
	if (pWnd->GetSafeHwnd() == m_actorChips.GetSafeHwnd())
	{
		// 영상 상세의 배우 칩 오른쪽 클릭: 이름/별칭 고르기 메뉴 (칩이 아닌 곳은 메뉴 없음)
		if (point.x == -1 && point.y == -1)
			return;
		CPoint pt = point;
		m_actorChips.ScreenToClient(&pt);
		const int chip = m_actorChips.ChipAt(pt);
		if (chip >= 0)
		{
			m_actorPopup.Hide();
			OnActorChipMenu(chip, point);
		}
		return;
	}
	if (pWnd->GetSafeHwnd() == m_tagChips.GetSafeHwnd())
	{
		// 영상 상세의 태그 칩: [태그 관리...] (오른쪽 클릭한 칩이 있으면 관리 창에서 그 태그 선택)
		CString select;
		if (point.x == -1 && point.y == -1)
		{
			CRect r;
			m_tagChips.GetWindowRect(&r);
			point = CPoint(r.left + DX(10), r.top + r.Height() / 2);
		}
		else
		{
			CPoint pt = point;
			m_tagChips.ScreenToClient(&pt);
			const int chip = m_tagChips.ChipAt(pt);
			if (chip >= 0 && chip < static_cast<int>(m_tagChips.Tags().size()))
				select = m_tagChips.Tags()[chip];
		}
		CMenu menu;
		menu.CreatePopupMenu();
		menu.AppendMenu(MF_STRING, 1, L"태그 관리...");
		const UINT cmd = DarkMenu::Track(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, point.x, point.y, this);
		if (cmd == 1)
			OpenListManager(LIST_TAG, select);
		return;
	}
	if (pWnd->GetSafeHwnd() == m_studioCard.GetSafeHwnd() || (m_labelCard.GetSafeHwnd() && pWnd->GetSafeHwnd() == m_labelCard.GetSafeHwnd()))
	{
		// 영상 상세의 제작사 카드 / 레이블 카드 (따로 한 줄씩): 변경 · 영상 보기 · 관리 · 비우기
		if (m_curItem < 0)
			return;
		const bool labelCard = (m_labelCard.GetSafeHwnd() && pWnd->GetSafeHwnd() == m_labelCard.GetSafeHwnd());
		CString studio;
		m_editStudio.GetWindowText(studio);
		studio.Trim();
		const CString labelName = LabelRowShown() ? m_labelChips.Tags()[0] : CString();
		const bool has = labelCard ? !labelName.IsEmpty() : !studio.IsEmpty();
		const UINT ro = IsItemOffline(m_curItem) ? MF_GRAYED : 0;   // 연결 안 된 이동식 디스크: 변경 · 비우기 막음
		if (point.x == -1 && point.y == -1)
		{
			CRect r;
			pWnd->GetWindowRect(&r);
			point = r.CenterPoint();
		}
		CMenu menu;
		menu.CreatePopupMenu();
		if (labelCard)
		{
			// 레이블 카드: 레이블 = 품번 DB 의 레이블 칸 (영상에서 직접 바꾸지 않음)
			menu.AppendMenu(MF_STRING, 7, L"품번 관리...");
			menu.AppendMenu(MF_STRING | (has ? 0 : MF_GRAYED), 2, L"이 레이블의 영상 보기");
			menu.SetDefaultItem(7);
		}
		else
		{
			menu.AppendMenu(MF_STRING | ro, 1, L"제작사 변경...\t더블클릭");
			menu.AppendMenu(MF_STRING | (has ? 0 : MF_GRAYED), 2, L"이 제작사의 영상 보기");
			menu.AppendMenu(MF_SEPARATOR);
			menu.AppendMenu(MF_STRING, 6, L"제작사 관리...");
			menu.AppendMenu(MF_SEPARATOR);
			menu.AppendMenu(MF_STRING | ((has && !ro) ? 0 : MF_GRAYED), 3, L"제작사 비우기");
			if (!ro)
				menu.SetDefaultItem(1);
		}
		const UINT cmd = DarkMenu::Track(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, point.x, point.y, this);
		if (cmd == 7)
			OnManageSeries();
		else if (cmd == 1)
			OnBnClickedPickStudio();
		else if (cmd == 2)
			OpenStudioCardTarget(labelCard ? 2 : 1);
		else if (cmd == 3)
			ClearStudioCard(labelCard);
		else if (cmd == 6)
			OpenListManager(LIST_STUDIO, (labelCard && !labelName.IsEmpty()) ? labelName : studio);
		return;
	}
	if (pWnd->GetSafeHwnd() == m_actorGrid.GetSafeHwnd())
	{
		int row = m_actorSel;
		if (point.x == -1 && point.y == -1)
		{
			CRect r;
			if (row < 0 || !m_actorGrid.GetTileRect(row, r)) return;
			point = r.CenterPoint();
			m_actorGrid.ClientToScreen(&point);
		}
		else
		{
			CPoint pt = point;
			m_actorGrid.ScreenToClient(&pt);
			row = m_actorGrid.HitTest(pt);
			if (row < 0) { ShowNewItemMenu(point); return; }   // 빈곳: 새 배우
			SelectActorRow(row);
		}
		CMenu menu;
		menu.CreatePopupMenu();
		// 체크한 배우 카드에서 오른쪽 클릭: 체크한 배우 전용 메뉴
		if (row >= 0 && row < static_cast<int>(m_actorRows.size()) && IsActorChecked(m_actorRows[row]))
		{
			const int checkedCount = static_cast<int>(CheckedActorIndices().size());
			CString merge;
			merge.Format(L"배우 병합(%d) - '%s' 기준", checkedCount, static_cast<LPCWSTR>(m_lib.actors[m_actorRows[row]].name));
			menu.AppendMenu(MF_STRING | (checkedCount >= 2 ? 0 : MF_GRAYED), ID_ACTOR_MERGECHECKED, merge);   // 2명 이상 체크했을 때만
			CString del;
			del.Format(L"배우 삭제(%d)\tDel", checkedCount);
			menu.AppendMenu(MF_STRING, ID_ACTOR_DELETECHECKED, del);
			menu.AppendMenu(MF_SEPARATOR);
			menu.AppendMenu(MF_STRING, ID_ACTOR_UNCHECKALL, L"체크 모두 해제");
			DarkMenu::Track(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
			return;
		}
		menu.AppendMenu(MF_STRING, ID_ACTOR_SHOWVIDEOS, L"출연작 보기\tEnter");
		menu.AppendMenu(MF_STRING, ID_ACTOR_EDIT,       L"배우 정보 편집...\tF2");
		menu.AppendMenu(MF_STRING, ID_ACTOR_TEXTINFO,   L"텍스트로 정보 입력...");
		menu.AppendMenu(MF_STRING, ID_ACTOR_PASTEINFO,  L"웹페이지 내용 붙여넣기...");
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, ID_ACTOR_MERGE,      L"다른 배우와 합치기...");
		menu.AppendMenu(MF_STRING, ID_ACTOR_DELETE,     L"배우 삭제\tDel");
		menu.SetDefaultItem(ID_ACTOR_SHOWVIDEOS);
		DarkMenu::Track(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
		return;
	}

	if (pWnd->GetSafeHwnd() == m_catGrid.GetSafeHwnd())
	{
		int row = m_listCat.GetNextItem(-1, LVNI_SELECTED);
		if (point.x == -1 && point.y == -1)
		{
			CRect r;
			if (row < 0 || !m_catGrid.GetTileRect(row, r)) return;
			point = r.CenterPoint();
			m_catGrid.ClientToScreen(&point);
		}
		else
		{
			CPoint pt = point;
			m_catGrid.ScreenToClient(&pt);
			row = m_catGrid.HitTest(pt);
			if (row < 0) { ShowNewItemMenu(point); return; }   // 빈곳: 새 제작사 / 레이블 / 태그
			m_catOwner.GridSetSel(row);
		}
		CMenu menu;
		menu.CreatePopupMenu();
		// 체크한 카드에서 오른쪽 클릭: 체크한 항목 전용 메뉴 (제작사 · 태그 · 컬렉션)
		if (CatChecksEnabled() && row >= 0 && row < static_cast<int>(m_catRows.size()) && IsCatChecked(m_catRows[row].kind, m_catRows[row].value))
		{
			CString del;
			del.Format(L"%s 삭제(%d)\tDel", m_mode == MODE_TAG ? L"태그" : (m_mode == MODE_COLLECTION ? L"컬렉션" : L"제작사 · 레이블"),
				static_cast<int>(CheckedCatRows().size()));
			menu.AppendMenu(MF_STRING, ID_CAT_DELETECHECKED, del);
			menu.AppendMenu(MF_SEPARATOR);
			menu.AppendMenu(MF_STRING, ID_CAT_UNCHECKALL, L"체크 모두 해제");
			DarkMenu::Track(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
			return;
		}
		menu.AppendMenu(MF_STRING, ID_ACTOR_SHOWVIDEOS, L"영상 보기\tEnter");
		menu.AppendMenu(MF_STRING, ID_ACTOR_EDIT,       m_mode == MODE_COLLECTION ? L"편집 (제목 · 커버 · 메모)...\tF2" : L"편집...\tF2");
		if (m_mode == MODE_COLLECTION)
		{
			menu.AppendMenu(MF_SEPARATOR);
			menu.AppendMenu(MF_STRING, ID_COL_NEW, L"새 컬렉션 추가...");
			menu.AppendMenu(MF_STRING, ID_CAT_DELETE, L"컬렉션 삭제\tDel");
		}
		if (m_mode == MODE_TAG)
		{
			menu.AppendMenu(MF_SEPARATOR);
			menu.AppendMenu(MF_STRING, ID_CAT_MERGE, L"다른 태그와 병합...");
			menu.AppendMenu(MF_STRING, ID_CAT_DELETE, L"삭제");
		}
		menu.SetDefaultItem(ID_ACTOR_SHOWVIDEOS);
		DarkMenu::Track(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
		return;
	}

	if (pWnd->GetSafeHwnd() == m_listCat.GetSafeHwnd())
	{
		int row = m_listCat.GetNextItem(-1, LVNI_SELECTED);
		if (point.x == -1 && point.y == -1)
		{
			if (row < 0) return;
			CRect r;
			m_listCat.GetItemRect(row, &r, LVIR_LABEL);
			point = CPoint(r.left + 10, r.bottom);
			m_listCat.ClientToScreen(&point);
		}
		else
		{
			CPoint pt = point;
			m_listCat.ScreenToClient(&pt);
			row = m_listCat.HitTest(pt);
			if (row < 0) { ShowNewItemMenu(point); return; }
			m_listCat.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		}
		CMenu menu;
		menu.CreatePopupMenu();
		menu.AppendMenu(MF_STRING, ID_ACTOR_SHOWVIDEOS, L"영상 보기\tEnter");
		menu.AppendMenu(MF_STRING, ID_ACTOR_EDIT,       m_mode == MODE_COLLECTION ? L"편집 (제목 · 커버 · 메모)...\tF2" : L"편집...\tF2");
		if (m_mode == MODE_COLLECTION)
		{
			menu.AppendMenu(MF_SEPARATOR);
			menu.AppendMenu(MF_STRING, ID_COL_NEW, L"새 컬렉션 추가...");
			menu.AppendMenu(MF_STRING, ID_CAT_DELETE, L"컬렉션 삭제\tDel");
		}
		if (m_mode == MODE_TAG)
		{
			menu.AppendMenu(MF_SEPARATOR);
			menu.AppendMenu(MF_STRING, ID_CAT_MERGE, L"다른 태그와 병합...");
			menu.AppendMenu(MF_STRING, ID_CAT_DELETE, L"삭제");
		}
		menu.SetDefaultItem(ID_ACTOR_SHOWVIDEOS);
		DarkMenu::Track(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
		return;
	}

	const bool fromGrid = (pWnd->GetSafeHwnd() == m_grid.GetSafeHwnd());
	if (pWnd->GetSafeHwnd() != m_list.GetSafeHwnd() && !fromGrid)
	{
		CDialogEx::OnContextMenu(pWnd, point);
		return;
	}

	int row = -1;
	if (point.x == -1 && point.y == -1)   // 키보드(Shift+F10, 메뉴 키)
	{
		row = GetSelectedRow();
		if (row < 0) return;
		CRect r;
		if (fromGrid)
		{
			m_grid.GetTileRect(GridIdxOf(row), r);
			point = r.CenterPoint();
			m_grid.ClientToScreen(&point);
		}
		else
		{
			m_list.GetItemRect(row, &r, LVIR_LABEL);
			point = CPoint(r.left + 10, r.bottom);
			m_list.ClientToScreen(&point);
		}
	}
	else if (fromGrid)
	{
		CPoint pt = point;
		m_grid.ScreenToClient(&pt);
		const int g = m_grid.HitTest(pt);   // 격자 칸
		if (g < 0) { ShowNewItemMenu(point); return; }   // 빈곳: 폴더 추가 등
		CRect card;
		const int z = m_grid.GetCardRect(g, card) ? PartZoneAt(g, card, pt) : -1;
		if (z >= 0)
			SelectViewRow(m_gridParts[g][z]);   // 합쳐 보기: 이미지 위 파트 칸 → 그 파트 메뉴
		else
			GridSetSel(g);
		row = GetSelectedRow();
		// 체크한 영상 카드에서 오른쪽 클릭: 체크한 영상 전용 메뉴
		if (IsVideoCardChecked(g))
		{
			CMenu menu;
			menu.CreatePopupMenu();
			CString del;
			del.Format(L"DB에서 삭제(%d) (파일 유지)\tDel", CheckedItemCount());
			menu.AppendMenu(MF_STRING, ID_VIDEO_DELETECHECKED, del);
			CString prev;
			prev.Format(L"미리보기 만들기(%d)", CheckedItemCount());
			menu.AppendMenu(MF_STRING, ID_VIDEO_MAKEPREVIEWCHECKED, prev);
			menu.AppendMenu(MF_SEPARATOR);
			menu.AppendMenu(MF_STRING, ID_VIDEO_UNCHECKALL, L"체크 모두 해제");
			DarkMenu::Track(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
			return;
		}
	}
	else
	{
		CPoint pt = point;
		m_list.ScreenToClient(&pt);
		row = m_list.HitTest(pt);
		if (row < 0) { if (VideoListActive()) ShowNewItemMenu(point); return; }   // [리스트] 빈 곳: 전체 체크 등
		m_list.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	}
	// [리스트]에서 체크한 영상 줄 오른쪽 클릭: 체크한 영상 전용 메뉴 (격자 카드와 같음)
	if (!fromGrid && row >= 0 && row < static_cast<int>(m_view.size()) && IsItemChecked(m_view[row]))
	{
		CMenu menu;
		menu.CreatePopupMenu();
		CString del;
		del.Format(L"DB에서 삭제(%d) (파일 유지)\tDel", CheckedItemCount());
		menu.AppendMenu(MF_STRING, ID_VIDEO_DELETECHECKED, del);
		CString prev;
		prev.Format(L"미리보기 만들기(%d)", CheckedItemCount());
		menu.AppendMenu(MF_STRING, ID_VIDEO_MAKEPREVIEWCHECKED, prev);
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, ID_VIDEO_UNCHECKALL, L"체크 모두 해제");
		DarkMenu::Track(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
		return;
	}

	ShowVideoContextMenu(row, point);
}

void CRuliManagerDlg::ShowVideoContextMenu(int row, CPoint point)
{
	// 연결 해제된 이동식 디스크의 영상: [DB에서 삭제] 말고는 비활성
	const UINT ro = IsItemOffline((row >= 0 && row < static_cast<int>(m_view.size())) ? m_view[row] : -1) ? MF_GRAYED : 0;
	CMenu menu;
	menu.CreatePopupMenu();
	// 묶음: [재생 · 새로고침 · 탐색기] | [정보 입력] | [스크린샷 · 미리보기] | [컬렉션] | [DB에서 삭제]
	menu.AppendMenu(MF_STRING | ro, IDC_BTN_OPENDEFAULT, L"재생 (기본 플레이어)\t더블클릭");
	menu.AppendMenu(MF_STRING | ro, ID_VIDEO_REFRESHITEM, L"이 영상 새로고침");      // 이미지 · 해상도 · 크기 · 스크린샷 다시 읽기
	menu.AppendMenu(MF_STRING | ro, IDC_BTN_EXPLORER,    L"탐색기에서 보기");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | ro, ID_VIDEO_TEXTINFO,   L"텍스트로 정보 입력...");
	menu.AppendMenu(MF_STRING | ro, ID_VIDEO_PASTEINFO,  L"웹페이지 내용 붙여넣기...");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | ro, ID_VIDEO_SCREENSHOT, L"스크린샷 추출...");   // 추출 중이면 대기열에
	{
		// 미리보기 영상 (이미 있으면 "다시 만들기")
		const int ci = (row >= 0 && row < static_cast<int>(m_view.size())) ? m_view[row] : -1;
		const bool has = (ci >= 0) && ::PathFileExistsW(PreviewFileOf(ci));
		menu.AppendMenu(MF_STRING | ro, ID_VIDEO_MAKEPREVIEW, has ? L"미리보기 다시 만들기" : L"미리보기 만들기");
	}
	menu.AppendMenu(MF_SEPARATOR);
	{
		// [컬렉션에 추가 ▸]: 컬렉션 목록 (이미 들어 있으면 체크 · 누르면 뺌) + [새 컬렉션...]
		m_ctxVideoItem = (row >= 0 && row < static_cast<int>(m_view.size())) ? m_view[row] : -1;
		m_ctxCollections.clear();
		CMenu sub;
		sub.CreatePopupMenu();
		std::vector<int> order;
		for (size_t i = 0; i < m_lib.collectionInfos.size(); ++i)
			order.push_back(static_cast<int>(i));
		std::sort(order.begin(), order.end(), [this](int a, int b)
		{
			return ::StrCmpLogicalW(m_lib.collectionInfos[a].name, m_lib.collectionInfos[b].name) < 0;
		});
		for (int ci : order)
		{
			if (m_ctxCollections.size() >= static_cast<size_t>(ID_COL_ADD_LAST - ID_COL_ADD_FIRST + 1))
				break;
			const CString& cn = m_lib.collectionInfos[ci].name;
			const bool in = (m_ctxVideoItem >= 0) && m_lib.InCollection(m_lib.items[m_ctxVideoItem], cn);
			sub.AppendMenu(MF_STRING | (in ? MF_CHECKED : 0), ID_COL_ADD_FIRST + static_cast<UINT>(m_ctxCollections.size()), cn);
			m_ctxCollections.push_back(cn);
		}
		if (!m_ctxCollections.empty())
			sub.AppendMenu(MF_SEPARATOR);
		sub.AppendMenu(MF_STRING, ID_COL_ADD_NEW, L"새 컬렉션 만들어 추가...");
		menu.AppendMenu(MF_POPUP | ro, reinterpret_cast<UINT_PTR>(sub.Detach()), L"컬렉션에 추가");
		if (m_mode == MODE_COLLECTION && !m_drill.IsEmpty() && m_catKind == CAT_VALUE)
			menu.AppendMenu(MF_STRING | ro, ID_COL_REMOVE, L"이 컬렉션에서 빼기 ('" + m_catValue + L"')");
	}
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, IDC_BTN_DELETE,      L"DB에서 삭제 (파일 유지)\tDel");
	if (!ro)
		menu.SetDefaultItem(IDC_BTN_OPENDEFAULT);
	DarkMenu::Track(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
}

// ---------------------------------------------------------------------------
// 격자 빈곳 오른쪽 클릭 → 새 항목

void CRuliManagerDlg::ShowNewItemMenu(CPoint screenPt)
{
	CMenu menu;
	menu.CreatePopupMenu();
	switch (m_mode)
	{
	case MODE_ACTOR:
		if (m_drill.IsEmpty())
		{
			menu.AppendMenu(MF_STRING, ID_NEW_ACTOR, L"새 배우 추가...");
		}
		else
			menu.AppendMenu(MF_STRING, IDC_BTN_ADDFOLDER, L"폴더 관리...");   // 출연작 화면
		break;
	case MODE_STUDIO:
		if (m_drill.IsEmpty())
		{
			menu.AppendMenu(MF_STRING, ID_NEW_STUDIO, L"새 제작사 추가...");   // 레이블은 품번 관리(품번 DB)의 레이블 칸
		}
		else
			menu.AppendMenu(MF_STRING, IDC_BTN_ADDFOLDER, L"폴더 관리...");
		break;
	case MODE_TAG:
		if (m_drill.IsEmpty())
		{
			menu.AppendMenu(MF_STRING, ID_NEW_TAG, L"새 태그 추가...");
		}
		else
			menu.AppendMenu(MF_STRING, IDC_BTN_ADDFOLDER, L"폴더 관리...");
		break;
	case MODE_COLLECTION:
		if (m_drill.IsEmpty())
			menu.AppendMenu(MF_STRING, ID_COL_NEW, L"새 컬렉션 추가...");
		// 컬렉션 영상 화면: 영상은 영상 카드 오른쪽 클릭 [컬렉션에 추가] 로 (여기는 전체 체크 · 체크 해제만)
		break;
	default:   // 영상: 영상은 폴더 스캔으로 추가
		menu.AppendMenu(MF_STRING, IDC_BTN_ADDFOLDER, L"폴더 관리...");
		menu.AppendMenu(MF_STRING, IDC_BTN_REFRESH,   L"새로고침\tF5");
		break;
	}
	// [전체 체크]: 지금 보이는 카드 모두 (배우 격자 / 제작사 · 태그 · 컬렉션 카드 목록 / 영상 격자 - 지금 페이지)
	if (const int all = CheckAllCount())
	{
		CString t;
		t.Format(L"전체 체크(%d)", all);
		if (menu.GetMenuItemCount() > 0)
			menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, ID_CHECK_ALL, t);
	}
	// 체크한 카드가 있으면: 체크 모두 해제 (배우 격자 / 제작사 · 태그 · 컬렉션 카드 목록 / 영상 격자)
	if (IsActorGridMode())
	{
		const size_t checked = CheckedActorIndices().size();
		if (checked > 0)
		{
			CString t;
			t.Format(L"체크 모두 해제(%d)", static_cast<int>(checked));
			menu.AppendMenu(MF_STRING, ID_ACTOR_UNCHECKALL, t);
		}
	}
	else if (CatChecksEnabled())
	{
		const size_t checked = CheckedCatRows().size();
		if (checked > 0)
		{
			CString t;
			t.Format(L"체크 모두 해제(%d)", static_cast<int>(checked));
			menu.AppendMenu(MF_STRING, ID_CAT_UNCHECKALL, t);
		}
	}
	else if (!IsActorGridMode() && !IsCategoryListMode() && !m_videoChecked.empty())
	{
		CString t;
		t.Format(L"영상 체크 모두 해제(%d)", CheckedItemCount());
		menu.AppendMenu(MF_STRING, ID_VIDEO_UNCHECKALL, t);
	}
	if (menu.GetMenuItemCount() <= 0)
		return;   // 보여 줄 항목 없음 (빈 컬렉션 영상 화면)
	DarkMenu::Track(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, this);
}

bool CRuliManagerDlg::AskNewName(const CString& caption, const CString& prompt, CString& name)
{
	CInputDlg dlg(CString(), this, caption, prompt);
	if (dlg.DoModal() != IDOK)
		return false;
	name = dlg.m_value;
	name.Trim();
	CVideoLibrary::RemoveListCommas(name);   // 구분 쉼표는 이름에 쓸 수 없음
	name.Trim();
	return !name.IsEmpty();
}

void CRuliManagerDlg::OnNewActor()
{
	CommitDetails();
	CString name;
	if (!AskNewName(L"새 배우", L"배우 이름", name))
		return;
	const int exist = m_lib.FindActorByAnyName(name);
	if (exist >= 0)
	{
		AfxMessageBox(L"같은 이름(또는 별칭)의 배우가 이미 있습니다.", MB_ICONINFORMATION);
		OpenActorManager(m_lib.actors[exist].name);
		return;
	}
	ActorInfo a;
	a.name = name;
	m_lib.actors.push_back(a);
	if (!m_lib.Save())
		AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);
	m_actorSelName = name;
	RebuildActorGrid();
	OpenActorManager(name);   // 바로 정보 입력 (사진 · 생년월일 등)
}

void CRuliManagerDlg::OnNewStudio()
{
	CommitDetails();
	CString name;
	if (!AskNewName(L"새 제작사", L"제작사 이름", name))
		return;
	if (m_lib.FindNamed(LIST_STUDIO, name) >= 0 || m_lib.FindLabel(name) >= 0)
	{
		AfxMessageBox(L"같은 이름의 제작사 또는 레이블이 이미 있습니다.", MB_ICONINFORMATION);
		return;
	}
	NamedInfo n;
	n.name = name;
	m_lib.studios.push_back(n);
	if (!m_lib.Save())
		AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);
	m_catSelKind = CAT_VALUE;
	m_catSelValue = name;
	RebuildCategories();
	OpenListManager(LIST_STUDIO, name);   // 이미지 · 레이블 · 시리즈 입력
}

void CRuliManagerDlg::OnNewLabel()
{
	CommitDetails();
	CString name;
	if (!AskNewName(L"새 레이블", L"레이블 이름 (상위 제작사는 다음 창에서 지정)", name))
		return;
	if (m_lib.FindNamed(LIST_STUDIO, name) >= 0 || m_lib.FindLabel(name) >= 0)
	{
		AfxMessageBox(L"같은 이름의 제작사 또는 레이블이 이미 있습니다.", MB_ICONINFORMATION);
		return;
	}
	NamedInfo n;
	n.name = name;   // 상위 제작사 없음 (관리 창에서 지정)
	m_lib.labelInfos.push_back(n);
	if (!m_lib.Save())
		AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);
	m_catSelKind = CAT_LABEL;
	m_catSelValue = name;
	RebuildCategories();
	OpenListManager(LIST_STUDIO, name);   // 관리 창에서 그 레이블 선택 → 상위 · 시리즈 입력
}

void CRuliManagerDlg::OnNewTag()
{
	CommitDetails();
	CString name;
	if (!AskNewName(L"새 태그", L"태그 이름", name))
		return;
	if (m_lib.FindNamed(LIST_TAG, name) >= 0)
	{
		AfxMessageBox(L"같은 이름의 태그가 이미 있습니다.", MB_ICONINFORMATION);
		return;
	}
	NamedInfo n;
	n.name = name;
	m_lib.tagInfos.push_back(n);
	if (!m_lib.Save())
		AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);
	m_catSelKind = CAT_VALUE;
	m_catSelValue = name;
	RebuildCategories();
}

// ---------------------------------------------------------------------------
// 컬렉션 탭: 컬렉션 만들기 · 이름 변경 · 즐겨찾기, 영상 오른쪽 클릭 [컬렉션에 추가 ▸] / [이 컬렉션에서 빼기]

BEGIN_MESSAGE_MAP(CCollectionInfoPanel, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDBLCLK()
END_MESSAGE_MAP()

BOOL CCollectionInfoPanel::Create(CWnd* parent, UINT id)
{
	const CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
	return CWnd::Create(cls, L"", WS_CHILD, CRect(0, 0, 10, 10), parent, id);
}

void CCollectionInfoPanel::SetSubItems(const CString& caption, const std::vector<SubItem>& items, COLORREF tileBack)
{
	m_subCaption = caption;
	m_subs = items;
	m_tileBack = tileBack;
	m_subRects.clear();
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

void CCollectionInfoPanel::SetInfo(const CString& title, const CString& subLine, const CString& memo, bool favorite)
{
	m_title = title;
	m_subLine = subLine;
	m_memo = memo;
	m_memo.Replace(L"\r\n", L"\n");
	m_memo.Replace(L"\n", L"\r\n");
	m_fav = favorite;
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

void CCollectionInfoPanel::EnsureFont()
{
	if (m_bigFont.GetSafeHandle() || !GetParent())
		return;
	LOGFONT lf = {};
	if (CFont* f = GetParent()->GetFont())
		f->GetLogFont(&lf);
	lf.lfHeight = lf.lfHeight * 17 / 10;   // 이름: 기본 글꼴의 1.7배 굵게 (배우 정보 이름처럼)
	lf.lfWeight = FW_BOLD;
	m_bigFont.CreateFontIndirect(&lf);
}

int CCollectionInfoPanel::Layout(CDC* dc, int width, CRect* titleRc, CRect* countRc, CRect* memoRc, CRect* capRc)
{
	EnsureFont();
	const int pad = 4;
	int y = pad;
	CFont* old = dc->SelectObject(&m_bigFont);
	const int titleH = dc->GetTextExtent(L"Ag가").cy;
	if (titleRc) titleRc->SetRect(0, y, width, y + titleH);
	y += titleH + pad;
	dc->SelectObject(GetParent()->GetFont());
	const int lineH = dc->GetTextExtent(L"Ag가").cy;
	if (countRc) countRc->SetRect(0, y, width, y + lineH);
	y += lineH + pad * 2;
	if (!m_memo.IsEmpty())
	{
		CRect mr(0, y, (std::max)(1, width), y);
		dc->DrawText(m_memo, &mr, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX | DT_CALCRECT);
		mr.right = width;
		if (memoRc) *memoRc = mr;
		y = mr.bottom + pad;
	}
	else if (memoRc)
		memoRc->SetRectEmpty();
	// 하위 레이블 칸: "레이블" 글자 + 이미지(16:9) 아래 이름, 폭이 넘치면 다음 줄
	m_subRects.clear();
	if (capRc)
		capRc->SetRectEmpty();
	if (!m_subs.empty())
	{
		y += pad;
		if (capRc) capRc->SetRect(0, y, width, y + lineH);
		y += lineH + pad;
		const int imgH = lineH * 5 / 2;
		const int tileW = (std::max)(imgH * 16 / 9, lineH * 4) + pad * 2;
		const int tileH = pad + imgH + pad + lineH + pad;
		int x = 0;
		for (size_t i = 0; i < m_subs.size(); ++i)
		{
			if (x > 0 && x + tileW > width)
			{
				x = 0;
				y += tileH + pad;
			}
			m_subRects.push_back(CRect(x, y, x + tileW, y + tileH));
			x += tileW + pad;
		}
		y += tileH + pad;
	}
	dc->SelectObject(old);
	return y;
}

int CCollectionInfoPanel::CalcHeight(int width)
{
	if (!GetSafeHwnd())
		return 0;
	CClientDC dc(this);
	return Layout(&dc, width, nullptr, nullptr, nullptr);
}

void CCollectionInfoPanel::OnPaint()
{
	CPaintDC pdc(this);
	CRect client;
	GetClientRect(&client);
	CDC mem;
	mem.CreateCompatibleDC(&pdc);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&pdc, (std::max)(1, client.Width()), (std::max)(1, client.Height()));
	CBitmap* oldBmp = mem.SelectObject(&bmp);
	mem.FillSolidRect(&client, m_back);
	mem.SetBkMode(TRANSPARENT);
	CRect tr, cr, mr, capr;
	Layout(&mem, client.Width(), &tr, &cr, &mr, &capr);
	CFont* old = mem.SelectObject(&m_bigFont);
	int x = 0;
	if (m_fav)
	{
		// 즐겨찾기: 제목 앞 빨간 하트
		const int hs = tr.Height() * 7 / 10;
		VectorIcon::Heart(&mem, CRect(0, tr.top + (tr.Height() - hs) / 2, hs, tr.top + (tr.Height() + hs) / 2), RGB(0xF2, 0x5C, 0x54), 1.0);
		x = hs + 6;
	}
	mem.SetTextColor(m_text);
	TextFB::Draw(&mem, m_title, CRect(x, tr.top, tr.right, tr.bottom), DT_LEFT, true);
	mem.SelectObject(GetParent()->GetFont());
	mem.SetTextColor(m_sub);
	mem.DrawText(m_subLine, &cr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
	if (!mr.IsRectEmpty())
	{
		mem.SetTextColor(m_text);
		CRect r = mr;
		r.bottom = (std::min)(r.bottom, client.bottom);
		mem.DrawText(m_memo, &r, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX | DT_END_ELLIPSIS);
	}
	if (!m_subs.empty())
	{
		mem.SetTextColor(m_sub);
		mem.DrawText(m_subCaption, &capr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
		for (size_t i = 0; i < m_subs.size() && i < m_subRects.size(); ++i)
		{
			const CRect tile = m_subRects[i];
			if (tile.top >= client.bottom)
				break;
			{
				CBrush br(m_tileBack);
				CPen pen(PS_SOLID, 1, m_tileBack);
				CBrush* ob = mem.SelectObject(&br);
				CPen* op = mem.SelectObject(&pen);
				mem.RoundRect(tile, CPoint(8, 8));
				mem.SelectObject(ob);
				mem.SelectObject(op);
			}
			const int lineH = mem.GetTextExtent(L"Ag가").cy;
			const CRect area(tile.left + 4, tile.top + 4, tile.right - 4, tile.bottom - 4 - lineH - 4);
			Gdiplus::Bitmap* img = m_subs[i].img;
			if (img && img->GetWidth() > 0 && img->GetHeight() > 0 && area.Width() > 0 && area.Height() > 0)
			{
				const double lw = img->GetWidth(), lh = img->GetHeight();
				const double scale = (std::min)(area.Width() / lw, area.Height() / lh);
				const int w = (std::max)(1, static_cast<int>(lw * scale));
				const int h = (std::max)(1, static_cast<int>(lh * scale));
				Gdiplus::Graphics g(mem.GetSafeHdc());
				g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
				g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
				g.DrawImage(img, area.left + (area.Width() - w) / 2, area.top + (area.Height() - h) / 2, w, h);
			}
			else
				VectorIcon::Camera(&mem, area, RGB(255, 255, 255), 0.6);   // 이미지 없는 레이블: 카메라 아이콘
			mem.SetTextColor(m_text);
			TextFB::Draw(&mem, m_subs[i].name, CRect(tile.left + 4, tile.bottom - 4 - lineH, tile.right - 4, tile.bottom - 4), DT_CENTER, true);
		}
	}
	mem.SelectObject(old);
	pdc.BitBlt(0, 0, client.Width(), client.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(oldBmp);
}

void CCollectionInfoPanel::OnLButtonDblClk(UINT /*nFlags*/, CPoint point)
{
	for (size_t i = 0; i < m_subRects.size(); ++i)
		if (m_subRects[i].PtInRect(point))
		{
			if (m_onSubDblClick)
				m_onSubDblClick(static_cast<int>(i));   // 레이블 칸 → 그 레이블 영상
			return;
		}
	if (m_onDblClick)
		m_onDblClick();
}

void CRuliManagerDlg::OnNamedHeaderEdit()
{
	if (CollectionHeaderShown())
	{
		RenameSelectedCollection();   // 컬렉션 편집 (제목 · 커버 · 메모)
		return;
	}
	if (m_mode == MODE_STUDIO && !m_drill.IsEmpty() && !m_catValue.IsEmpty())
	{
		OpenListManager(LIST_STUDIO, m_catValue);   // 제작사 / 레이블 관리 창 (그 항목 선택)
		if (NamedHeaderShown())
		{
			UpdateCollectionHeader();
			CRect client;
			GetClientRect(&client);
			LayoutControls(client.Width(), client.Height());
		}
	}
}

void CRuliManagerDlg::UpdateCollectionHeader()
{
	if (!NamedHeaderShown() || !m_colInfo.GetSafeHwnd())
		return;
	m_headerLabels.clear();
	m_headerSubIsStudio = false;
	if (m_mode == MODE_STUDIO)
	{
		// 제작사 / 레이블 영상 화면: 로고 + 이름(서브이름) · 메모, 제작사는 하위 레이블 이미지 칸
		const bool isLabel = (m_catKind == CAT_LABEL);
		const CString name = m_catValue;
		const int idx = isLabel ? m_lib.FindLabel(name) : m_lib.FindNamed(LIST_STUDIO, name);
		const NamedInfo* info = (idx >= 0) ? (isLabel ? &m_lib.labelInfos[idx] : &m_lib.studios[idx]) : nullptr;
		CString subLine = isLabel ? L"레이블" : L"제작사";
		if (info)
		{
			CString subs;
			for (const CString& s : CVideoLibrary::SplitLines(info->subName))
				subs += (subs.IsEmpty() ? CString() : CString(L" / ")) + s;
			if (!subs.IsEmpty())
				subLine += L" (" + subs + L")";
			if (isLabel && !info->parent.IsEmpty())
				subLine += L" · 상위 제작사: " + info->parent;
		}
		if (!m_drillCount.IsEmpty())
			subLine += L" · 영상 " + m_drillCount + L"편";
		m_colInfo.SetInfo(info ? info->name : name, subLine, info ? info->memo : CString(), info && info->favorite);
		std::vector<CCollectionInfoPanel::SubItem> subs;
		if (!isLabel && idx >= 0)
		{
			for (int li : m_lib.LabelsOf(m_lib.studios[idx].name))
			{
				CCollectionInfoPanel::SubItem it;
				it.name = m_lib.labelInfos[li].name;
				it.img = GetStudioLogo(m_lib.labelInfos[li].image);
				subs.push_back(it);
				m_headerLabels.push_back(it.name);
			}
		}
		CString caption = L"레이블 (더블클릭: 그 레이블의 영상)";
		COLORREF tileBack = TCOL(TC_LABELCARD);
		if (isLabel && info)
		{
			// 레이블 영상 화면: 상위 제작사 칸 (더블클릭하면 제작사 영상 화면으로 올라감)
			const int sn = m_lib.FindNamed(LIST_STUDIO, info->parent);
			if (sn >= 0)
			{
				CCollectionInfoPanel::SubItem it;
				it.name = m_lib.studios[sn].name;
				it.img = GetStudioLogo(m_lib.studios[sn].image);
				subs.push_back(it);
				m_headerLabels.push_back(it.name);
				m_headerSubIsStudio = true;
				caption = L"상위 제작사 (더블클릭: 제작사 영상으로)";
				tileBack = kCardBack;   // 제작사 카드 색
			}
		}
		m_colInfo.SetSubItems(caption, subs, tileBack);
		// 로고: 등록 이미지 → 없으면 카메라 아이콘
		bool shown = false;
		if (info && !info->image.IsEmpty() && ::PathFileExistsW(info->image))
			shown = m_colCover.SetImageFile(info->image);
		if (!shown)
		{
			const COLORREF back = isLabel ? TCOL(TC_LABELCARD) : kCardBack;
			m_colCover.SetDefaultImage([back](CDC* dc, const CRect& rc)
			{
				dc->FillSolidRect(rc, back);
				VectorIcon::Camera(dc, rc, RGB(255, 255, 255), 0.5);
			});
		}
		return;
	}
	m_colInfo.SetSubItems(CString(), {}, TCOL(TC_LABELCARD));
	const CString name = m_catValue;
	const int ci = m_lib.FindCollection(name);
	int count = 0, first = -1;
	for (size_t i = 0; i < m_lib.items.size(); ++i)
		if (m_lib.InCollection(m_lib.items[i], name))
		{
			if (first < 0) first = static_cast<int>(i);
			++count;
		}
	CString subLine;
	subLine.Format(L"컬렉션 · 영상 %d편", count);
	m_colInfo.SetInfo(ci >= 0 ? m_lib.collectionInfos[ci].name : name, subLine,
		ci >= 0 ? m_lib.collectionInfos[ci].memo : CString(), ci >= 0 && m_lib.collectionInfos[ci].favorite);
	// 커버: 고유 커버 → 대표 영상(처음 들어 있는 영상)의 이미지 → 컬렉션 아이콘
	bool shown = false;
	if (ci >= 0 && !m_lib.collectionInfos[ci].image.IsEmpty() && ::PathFileExistsW(m_lib.collectionInfos[ci].image))
		shown = m_colCover.SetImageFile(m_lib.collectionInfos[ci].image);
	if (!shown && first >= 0)
	{
		const CString img = FindImageFor(m_lib.items[first].path);
		if (!img.IsEmpty())
			shown = m_colCover.SetImageFile(img, true);   // 영상 이미지: SFW 옵션이면 모자이크
	}
	if (!shown)
	{
		const COLORREF back = kCardBack;
		m_colCover.SetDefaultImage([back](CDC* dc, const CRect& rc)
		{
			dc->FillSolidRect(rc, back);
			VectorIcon::Collection(dc, rc, RGB(255, 255, 255), 0.5);
		});
	}
}

void CRuliManagerDlg::OnNewCollection()
{
	CommitDetails();
	CCollectionEditDlg dlg(this, L"새 컬렉션");
	dlg.m_titleTaken = [this](const CString& t) { return m_lib.FindCollection(t) >= 0; };
	if (dlg.DoModal() != IDOK)
		return;
	const CString name = dlg.m_title;
	NamedInfo n;
	n.name = name;
	n.memo = dlg.m_memo;
	if (!dlg.m_image.IsEmpty())
		n.image = CVideoLibrary::StoreImageCopy(dlg.m_image, L"studios");   // 이미지 보관소의 복사본으로 (원본을 옮겨도 유지)
	m_lib.collectionInfos.push_back(n);
	if (!m_lib.Save())
		AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);
	if (m_mode == MODE_COLLECTION)
	{
		m_catSelKind = CAT_VALUE;
		m_catSelValue = name;
		RebuildCategories();
		UpdateStatus();
	}
}

void CRuliManagerDlg::RenameSelectedCollection()
{
	// 컬렉션 편집: 제목 · 커버 · 메모
	if (m_mode != MODE_COLLECTION || m_catSelKind != CAT_VALUE || m_catSelValue.IsEmpty())
		return;
	int ci = m_lib.FindCollection(m_catSelValue);
	if (ci < 0)
	{
		NamedInfo info;   // 영상에만 적혀 있던 컬렉션 → 목록에 추가
		info.name = m_catSelValue;
		m_lib.collectionInfos.push_back(info);
		ci = static_cast<int>(m_lib.collectionInfos.size()) - 1;
	}
	const CString old = m_lib.collectionInfos[ci].name;
	CCollectionEditDlg dlg(this, L"컬렉션 편집");
	dlg.m_title = old;
	dlg.m_memo = m_lib.collectionInfos[ci].memo;
	dlg.m_image = m_lib.collectionInfos[ci].image;
	dlg.m_titleTaken = [this, ci](const CString& t) { const int o = m_lib.FindCollection(t); return o >= 0 && o != ci; };
	if (dlg.DoModal() != IDOK)
		return;
	if (dlg.m_title != old && !m_lib.RenameCollection(old, dlg.m_title))
	{
		AfxMessageBox(L"같은 제목의 컬렉션이 이미 있습니다.", MB_ICONINFORMATION);
		return;
	}
	NamedInfo& n = m_lib.collectionInfos[ci];
	n.memo = dlg.m_memo;
	if (dlg.m_image.IsEmpty())
		n.image.Empty();
	else if (dlg.m_image.CompareNoCase(n.image) != 0)
	{
		const CString copy = CVideoLibrary::StoreImageCopy(dlg.m_image, L"studios");   // 이미지 보관소의 복사본으로
		if (!copy.IsEmpty())
			n.image = copy;
	}
	m_lib.Save();
	m_catSelValue = n.name;
	if (!m_drill.IsEmpty())
	{
		// 컬렉션 영상 화면에서 편집 (위쪽 커버 · 정보 더블클릭): 제목이 바뀌었으면 화면도 그 이름으로
		m_drill = n.name;
		m_catValue = n.name;
		UpdateCollectionHeader();
		ApplyFilter();
		CRect client;
		GetClientRect(&client);
		LayoutControls(client.Width(), client.Height());   // 메모 길이에 맞춰 위쪽 높이
	}
	RebuildCategories();
}

bool CRuliManagerDlg::IsCollectionFavorite(const CString& name) const
{
	const int n = m_lib.FindCollection(name);
	return n >= 0 && m_lib.collectionInfos[n].favorite;
}

void CRuliManagerDlg::ToggleCollectionFavorite(const CString& name)
{
	int n = m_lib.FindCollection(name);
	if (n < 0)
	{
		NamedInfo info;
		info.name = name;
		m_lib.collectionInfos.push_back(info);
		n = static_cast<int>(m_lib.collectionInfos.size()) - 1;
	}
	m_lib.collectionInfos[n].favorite = !m_lib.collectionInfos[n].favorite;
	m_lib.Save();
	m_catSelKind = CAT_VALUE;
	m_catSelValue = m_lib.collectionInfos[n].name;
	RebuildCategories();   // 즐겨찾기가 앞으로 오도록 다시 정렬
}

void CRuliManagerDlg::DrawCollectionCover(CDC* dc, const CString& name, const CRect& area, COLORREF back)
{
	// 컬렉션 고유 커버 이미지 → 없으면 대표 영상(컬렉션에 처음 들어 있는 영상)의 커버 → 없거나 읽는 중이면 컬렉션 아이콘
	const int ci = m_lib.FindCollection(name);
	if (ci >= 0 && !m_lib.collectionInfos[ci].image.IsEmpty())
	{
		Gdiplus::Bitmap* img = GetStudioLogo(m_lib.collectionInfos[ci].image);   // 경로별 캐시
		if (img && img->GetWidth() > 0 && img->GetHeight() > 0)
		{
			const double lw = img->GetWidth(), lh = img->GetHeight();
			const double scale = (std::min)(area.Width() / lw, area.Height() / lh);
			const int w = (std::max)(1, static_cast<int>(lw * scale));
			const int h = (std::max)(1, static_cast<int>(lh * scale));
			Gdiplus::Graphics g(dc->GetSafeHdc());
			g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
			g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
			g.DrawImage(img, area.left + (area.Width() - w) / 2, area.top + (area.Height() - h) / 2, w, h);
			return;
		}
	}
	CString key = name;
	key.MakeLower();
	auto it = m_collectionCover.find(key);
	if (it != m_collectionCover.end() && it->second >= 0 && it->second < static_cast<int>(m_lib.items.size()) && area.Width() > 0 && area.Height() > 0)
	{
		bool loading = false;
		if (CBitmap* bmp = GetVideoCover(it->second, area.Width(), area.Height(), &loading))
		{
			CDC mem;
			mem.CreateCompatibleDC(dc);
			CBitmap* old = mem.SelectObject(bmp);
			dc->BitBlt(area.left, area.top, area.Width(), area.Height(), &mem, 0, 0, SRCCOPY);
			mem.SelectObject(old);
			return;
		}
		if (loading)
		{
			const COLORREF dim = RGB((255 * 45 + GetRValue(back) * 55) / 100, (255 * 45 + GetGValue(back) * 55) / 100, (255 * 45 + GetBValue(back) * 55) / 100);
			VectorIcon::Collection(dc, area, dim, 0.55);   // 읽는 중: 흐린 아이콘
			return;
		}
	}
	VectorIcon::Collection(dc, area, RGB(255, 255, 255), 0.55);
}

void CRuliManagerDlg::OnCollectionAdd(UINT nID)
{
	const size_t k = nID - ID_COL_ADD_FIRST;
	const int item = m_ctxVideoItem;
	if (k >= m_ctxCollections.size() || item < 0 || item >= static_cast<int>(m_lib.items.size()) || IsItemOffline(item))
		return;
	CommitDetails();
	const CString name = m_ctxCollections[k];
	const bool wasPending = m_lib.items[item].pending;
	// 이미 들어 있으면(메뉴에 체크) 뺌, 아니면 넣음
	const bool in = m_lib.InCollection(m_lib.items[item], name);
	const bool changed = in ? m_lib.RemoveFromCollection(static_cast<size_t>(item), name)
	                        : m_lib.AddToCollection(static_cast<size_t>(item), name);
	if (!changed)
		return;
	if (!m_lib.Save())
		AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);
	CString note;
	note.Format(in ? L"컬렉션 '%s'에서 뺐습니다." : L"컬렉션 '%s'에 추가했습니다.", static_cast<LPCWSTR>(name));
	if (wasPending && !m_lib.items[item].pending)
		note += L" (임시 영상은 정식 등록)";
	MarkCategoriesDirty();
	if (m_mode == MODE_COLLECTION && !m_drill.IsEmpty())
	{
		ApplyFilter();   // 컬렉션 영상 화면: 뺀 영상은 목록에서 사라짐
		UpdateCollectionHeader();   // 영상 수 · 대표 커버
	}
	else
	{
		m_grid.Invalidate(FALSE);
		if (wasPending)
			UpdateStatus();
	}
	m_staticStatus.SetWindowText(note);
}

void CRuliManagerDlg::OnCollectionAddNew()
{
	const int item = m_ctxVideoItem;
	if (item < 0 || item >= static_cast<int>(m_lib.items.size()) || IsItemOffline(item))
		return;
	CommitDetails();
	CString name;
	if (!AskNewName(L"새 컬렉션", L"컬렉션 이름 (같은 이름이 있으면 그 컬렉션에 추가)", name))
		return;
	const bool wasPending = m_lib.items[item].pending;
	m_lib.AddToCollection(static_cast<size_t>(item), name);   // 없는 컬렉션이면 만듦
	if (!m_lib.Save())
		AfxMessageBox(L"라이브러리 파일을 저장하지 못했습니다.", MB_ICONWARNING);
	MarkCategoriesDirty();
	m_grid.Invalidate(FALSE);
	if (wasPending)
		UpdateStatus();
	m_staticStatus.SetWindowText(L"컬렉션 '" + name + L"'에 추가했습니다.");
}

void CRuliManagerDlg::OnCollectionRemove()
{
	const int item = m_ctxVideoItem;
	if (m_mode != MODE_COLLECTION || m_drill.IsEmpty() || m_catKind != CAT_VALUE ||
		item < 0 || item >= static_cast<int>(m_lib.items.size()) || IsItemOffline(item))
		return;
	CommitDetails();
	const CString name = m_catValue;
	if (!m_lib.RemoveFromCollection(static_cast<size_t>(item), name))
		return;
	m_lib.Save();
	MarkCategoriesDirty();
	ApplyFilter();   // 뺀 영상은 목록에서 사라짐
	UpdateCollectionHeader();
	m_staticStatus.SetWindowText(L"컬렉션 '" + name + L"'에서 뺐습니다.");
}

// ---------------------------------------------------------------------------
// 종료 / 키 처리

void CRuliManagerDlg::OnOK()
{
	// Enter 키: 목록에서는 기본 플레이어로 재생, 그 외에는 무시 (대화상자가 닫히지 않도록)
	CWnd* focus = GetFocus();
	if (focus && focus->GetSafeHwnd() == m_actorGrid.GetSafeHwnd())
	{
		if (m_actorSel >= 0)
			DrillIntoActor(m_actorSel);
		return;
	}
	if (focus && (focus->GetSafeHwnd() == m_listCat.GetSafeHwnd() || focus->GetSafeHwnd() == m_catGrid.GetSafeHwnd()))
	{
		DrillIntoCategory(m_listCat.GetNextItem(-1, LVNI_SELECTED));
		return;
	}
	if (focus && (focus->GetSafeHwnd() == m_list.GetSafeHwnd() || focus->GetSafeHwnd() == m_grid.GetSafeHwnd()))
		OnBnClickedOpenDefault();
	else if (focus && (focus->GetSafeHwnd() == m_editTags.GetSafeHwnd() ||
	                   focus->GetSafeHwnd() == m_tagChips.m_edit.GetSafeHwnd() ||
	                   focus->GetSafeHwnd() == m_actorChips.m_edit.GetSafeHwnd() ||
	                   focus->GetSafeHwnd() == m_studioChips.m_edit.GetSafeHwnd() ||
	                   focus->GetSafeHwnd() == m_labelChips.m_edit.GetSafeHwnd() ||
	                   focus->GetSafeHwnd() == m_seriesChips.m_edit.GetSafeHwnd() ||
	                   focus->GetSafeHwnd() == m_editTitle.GetSafeHwnd() ||
	                   focus->GetSafeHwnd() == m_editCode.GetSafeHwnd() ||
	                   focus->GetSafeHwnd() == m_editSeries.GetSafeHwnd() ||
	                   focus->GetSafeHwnd() == m_editActors.GetSafeHwnd() ||
	                   focus->GetSafeHwnd() == m_editVAliases.GetSafeHwnd() ||
	                   focus->GetSafeHwnd() == m_editStudio.GetSafeHwnd()))
		CommitDetails();
}

void CRuliManagerDlg::OnCancel()
{
	// ESC 키로 닫히지 않도록 무시. 종료는 OnClose 에서 처리.
}

// ---------------------------------------------------------------------------
// 배경색 (#202B33)

void CRuliManagerDlg::ApplyColors()
{
	if (!m_bgBrush.GetSafeHandle())
		m_bgBrush.CreateSolidBrush(kBackColor);
	if (!m_editBrush.GetSafeHandle())
		m_editBrush.CreateSolidBrush(kEditColor);

	// 목록 컨트롤
	CListCtrl* lists[] = { &m_list, &m_listCat };
	for (CListCtrl* l : lists)
	{
		l->SetBkColor(kBackColor);
		l->SetTextBkColor(kBackColor);
		l->SetTextColor(kTextColor);
	}

	// 콤보박스 (#394B59) / 날짜 컨트롤 (#1B252C)
	{
		CClientDC dc(this);
		CFont* old = dc.SelectObject(GetFont());
		TEXTMETRIC tm = {};
		dc.GetTextMetrics(&tm);
		dc.SelectObject(old);
		const int itemH = tm.tmHeight + 6;
		const int fieldH = (std::max)(itemH - 2, DY(13) - 6);   // 에디트와 비슷한 높이
		CDarkCombo* combos[] = { &m_comboFilter, &m_comboSort, &m_comboPageSize, &m_comboTmBasis };
		for (CDarkCombo* c : combos)
		{
			c->SetColors(kComboColor, kTextColor, TCOL(TC_SEL));
			c->SetHeights(fieldH, itemH);
		}
	}
	m_dateRelease.SetColors(kDateColor, kTextColor, TCOL(TC_EDGE));

	// 버튼 (#137CBD)
	const UINT buttons[] = {
		IDC_BTN_ADDFOLDER, IDC_BTN_REMOVEFOLDER, IDC_BTN_REFRESH, IDC_BTN_RESCAN, IDC_BTN_ACTORS,
		IDC_BTN_ACTOR_BACK, IDC_BTN_SAVE, IDC_BTN_PICKACTORS, IDC_BTN_PICKVALIASES, IDC_BTN_PICKSTUDIO, IDC_BTN_PICKTAGS,
		IDC_BTN_OPENDEFAULT, IDC_BTN_DELETE, IDC_BTN_EXPLORER, IDC_BTN_SORTDIR, IDC_BTN_OPENDB, IDC_BTN_CHANGEIMAGE, IDC_BTN_SEARCHIMAGE,
		IDC_BTN_APPLYDB, IDC_BTN_EXPORTTXT
	};
	for (size_t i = 0; i < _countof(buttons) && i < _countof(m_darkButtons); ++i)
	{
		if (m_darkButtons[i].GetSafeHwnd() || m_darkButtons[i].Attach(buttons[i], this))
		{
			// 삭제/제거 버튼은 빨간색
			const bool danger = (buttons[i] == IDC_BTN_DELETE || buttons[i] == IDC_BTN_REMOVEFOLDER);
			const bool isSave = (buttons[i] == IDC_BTN_SAVE || buttons[i] == IDC_BTN_APPLYDB);   // 저장 · DB 반영 버튼은 초록색
			m_darkButtons[i].SetColors(danger ? kDangerColor : (isSave ? kSaveColor : kButtonColor), RGB(255, 255, 255), kBackColor);
		}
	}

	// 정렬 방향 버튼: 정렬 콤보와 붙은 한 묶음 (콤보 색 · 오른쪽 칸 모양, 아이콘만)
	for (CDarkButton& b : m_darkButtons)
		if (b.GetSafeHwnd() && b.GetDlgCtrlID() == IDC_BTN_SORTDIR)
		{
			b.SetColors(kComboColor, RGB(255, 255, 255), kBackColor);
			b.SetSegment(CDarkButton::SEG_RIGHT);
		}

	// 제작사 탭 [격자] [리스트] · 영상 탭 [장르숨김] [전체] 토글 (테마 색 다시)
	for (CDarkButton& b : m_studioViewBtns)
		if (b.GetSafeHwnd())
		{
			b.SetColors(kButtonColor, RGB(255, 255, 255), kBackColor);
			b.SetToggle(kComboColor, TCOL(TC_TOGGLETEXT));
		}
	for (CDarkButton& b : m_genreBtns)
		if (b.GetSafeHwnd())
		{
			b.SetColors(kButtonColor, RGB(255, 255, 255), kBackColor);
			b.SetToggle(kComboColor, TCOL(TC_TOGGLETEXT));
		}
	for (CDarkButton& b : m_videoViewBtns)
		if (b.GetSafeHwnd())
		{
			b.SetColors(kButtonColor, RGB(255, 255, 255), kBackColor);
			b.SetToggle(kComboColor, TCOL(TC_TOGGLETEXT));
		}
	for (CDarkButton& b : m_genderBtns)
		if (b.GetSafeHwnd())
		{
			b.SetColors(kButtonColor, RGB(255, 255, 255), kBackColor);
			b.SetToggle(kComboColor, TCOL(TC_TOGGLETEXT));
		}
	for (CDarkButton& b : m_videoModeBtns)
		if (b.GetSafeHwnd())
		{
			b.SetColors(kButtonColor, RGB(255, 255, 255), kBackColor);
			b.SetToggle(kComboColor, TCOL(TC_TOGGLETEXT));
		}
	if (m_list.GetSafeHwnd())
		CDarkDialogTheme::ThemeChild(m_list.GetSafeHwnd());
	if (m_genreBar.GetSafeHwnd())
		m_genreBar.Invalidate(FALSE);
	if (m_btnCopyCode.GetSafeHwnd())
		m_btnCopyCode.SetColors(TCOL(TC_COMBO), RGB(255, 255, 255), kBackColor);   // 품번 [복사]

	// 설정 버튼 (회색 바탕 + 흰 톱니바퀴)
	if (m_btnSettings.GetSafeHwnd() || m_btnSettings.Attach(IDC_BTN_SETTINGS, this))
	{
		m_btnSettings.SetColors(TCOL(TC_COMBO), RGB(255, 255, 255), kBackColor);
		m_btnSettings.SetIcon(CDarkButton::ICON_GEAR);
	}

	// 격자 / 미리보기
	m_grid.SetBackColor(kBackColor);
	m_actorGrid.SetBackColor(kBackColor);
	m_catGrid.SetBackColor(kBackColor);
	m_preview.SetBackColor(TCOL(TC_PREVIEW));

	// 영상 / 배우 / 스튜디오 / 태그 / 컬렉션: 토글 버튼 (선택 #137CBD, 나머지 #394B59)
	for (int i = 0; i < MODE_COUNT; ++i)
	{
		if (m_modeButtons[i].GetSafeHwnd() || m_modeButtons[i].Attach(IDC_RADIO_VIDEO + i, this))
		{
			m_modeButtons[i].SetColors(kButtonColor, RGB(255, 255, 255), kBackColor);
			m_modeButtons[i].SetToggle(kComboColor, TCOL(TC_TOGGLETEXT));
			if (i == MODE_VIDEO)
				m_modeButtons[i].SetIcon(CDarkButton::ICON_PLAY);     // [● ▶ 영상]
			else if (i == MODE_ACTOR)
				m_modeButtons[i].SetIcon(CDarkButton::ICON_PERSON);   // [사람 배우]
			else if (i == MODE_STUDIO)
				m_modeButtons[i].SetIcon(CDarkButton::ICON_CAMERA);   // [카메라 스튜디오]
			else if (i == MODE_TAG)
				m_modeButtons[i].SetIcon(CDarkButton::ICON_TAG);      // [꼬리표 태그]
			else if (i == MODE_COLLECTION)
				m_modeButtons[i].SetIcon(CDarkButton::ICON_COLLECTION);   // [겹친 화면 컬렉션]
		}
	}
	UpdateModeButtons();

	// 페이지 버튼: 지금 페이지는 파란색, 나머지는 회색 (토글)
	for (int i = 0; i < _countof(m_pageButtons); ++i)
	{
		if (m_pageButtons[i].GetSafeHwnd() || m_pageButtons[i].Attach(IDC_PAGE_FIRST + i, this))
		{
			m_pageButtons[i].SetColors(kButtonColor, RGB(255, 255, 255), kBackColor);
			m_pageButtons[i].SetToggle(kComboColor, RGB(220, 226, 232));
		}
	}

	// 라디오 버튼은 테마가 켜져 있으면 글자색을 바꿀 수 없으므로 테마를 끔
	const UINT radios[] = { IDC_RADIO_LISTVIEW, IDC_RADIO_GRIDVIEW };
	for (UINT id : radios)
	{
		if (CWnd* w = GetDlgItem(id))
			::SetWindowTheme(w->GetSafeHwnd(), L"", L"");
	}
	Invalidate();
}

void CRuliManagerDlg::CreateScrollBars()
{
	// 목록 / 분류 목록 / 메모 / 격자의 스크롤바를 #6C7478 로 직접 그림
	struct Target { CWnd* wnd; bool vertical; CDarkScrollBar::TargetType type; };
	const Target targets[] = {
		{ &m_list,      true,  CDarkScrollBar::TARGET_LISTVIEW },
		{ &m_list,      false, CDarkScrollBar::TARGET_LISTVIEW },
		{ &m_listCat,   true,  CDarkScrollBar::TARGET_LISTVIEW },
		{ &m_listCat,   false, CDarkScrollBar::TARGET_LISTVIEW },
		{ &m_editMemo,  true,  CDarkScrollBar::TARGET_EDIT },
		{ &m_grid,      true,  CDarkScrollBar::TARGET_GRID },
		{ &m_actorGrid, true,  CDarkScrollBar::TARGET_GRID },
		{ &m_catGrid,   true,  CDarkScrollBar::TARGET_GRID },
		{ &m_catGrid,   false, CDarkScrollBar::TARGET_GRID },   // 제작사 탭 [리스트] 좌우 스크롤
		{ &m_yearView,  true,  CDarkScrollBar::TARGET_GRID },   // 배우 출연작 [연도별] (세로 · 가로)
		{ &m_yearView,  false, CDarkScrollBar::TARGET_GRID },
	};
	for (size_t i = 0; i < _countof(targets) && i < _countof(m_scrollBars); ++i)
	{
		if (m_scrollBars[i].GetSafeHwnd() || !targets[i].wnd->GetSafeHwnd())
			continue;
		m_scrollBars[i].SetColors(kScrollColor, TCOL(TC_SCROLLTHUMB), TCOL(TC_SCROLLTHUMBHOT));
		m_scrollBars[i].Create(this, targets[i].wnd, targets[i].vertical, targets[i].type);
	}
}

void CRuliManagerDlg::UpdateModeButtons()
{
	for (int i = 0; i < MODE_COUNT; ++i)
	{
		if (m_modeButtons[i].GetSafeHwnd())
			m_modeButtons[i].SetChecked(i == m_mode);
	}
}

void CRuliManagerDlg::SetNameRich(bool on)
{
	if (!m_staticName.GetSafeHwnd())
		return;
	if (on == m_nameRich)
	{
		if (on)
			m_staticName.Invalidate();
		return;
	}
	m_nameRich = on;
	// 직접 그리기(SS_OWNERDRAW) ↔ 일반 왼쪽 정렬 글자(SS_LEFT)
	m_staticName.ModifyStyle(SS_TYPEMASK, on ? SS_OWNERDRAW : SS_LEFT);
	m_staticName.Invalidate();
}

void CRuliManagerDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDis)
{
	if (nIDCtl == IDC_STATIC_CODE_SERIES && lpDis)
	{
		// 품번 오른쪽: 시리즈 라벨 (보통) + 설명 (회색), 세로 가운데 · 넘치면 "…"
		CDC* dc = CDC::FromHandle(lpDis->hDC);
		const CRect rc = lpDis->rcItem;
		dc->FillSolidRect(rc, kBackColor);
		dc->SetBkMode(TRANSPARENT);
		CFont* old = dc->SelectObject(m_staticCodeSeries.GetFont());
		int x = rc.left;
		if (!m_codeSeriesLabel.IsEmpty())
		{
			dc->SetTextColor(kTextColor);
			TextFB::Draw(dc, m_codeSeriesLabel, CRect(x, rc.top, rc.right, rc.bottom), DT_LEFT, true);
			x += TextFB::Width(dc, m_codeSeriesLabel) + DX(6);
		}
		if (!m_codeSeriesDesc.IsEmpty() && x < rc.right - 8)
		{
			dc->SetTextColor(TCOL(TC_SUB2));
			TextFB::Draw(dc, m_codeSeriesDesc, CRect(x, rc.top, rc.right, rc.bottom), DT_LEFT, true);
		}
		dc->SelectObject(old);
		return;
	}
	if (nIDCtl == IDC_STATIC_NAME && m_nameRich && lpDis)
	{
		CDC* dc = CDC::FromHandle(lpDis->hDC);
		const CRect rc = lpDis->rcItem;
		dc->FillSolidRect(rc, kBackColor);
		dc->SetBkMode(TRANSPARENT);
		CFont* base = m_staticName.GetFont();
		if (!m_nameBoldFont.GetSafeHandle() && base)
		{
			LOGFONT lf = {};
			base->GetLogFont(&lf);
			lf.lfWeight = FW_BOLD;
			m_nameBoldFont.CreateFontIndirect(&lf);
		}
		CFont* old = dc->SelectObject(base);
		int x = rc.left;
		// "스튜디오: " (보통)
		dc->SetTextColor(kTextColor);
		const int pw = TextFB::Width(dc, m_nameRichPrefix);
		TextFB::Draw(dc, m_nameRichPrefix, CRect(x, rc.top, rc.right, rc.bottom), DT_LEFT, true);
		x += pw;
		// 이름 (굵게)
		if (m_nameBoldFont.GetSafeHandle())
			dc->SelectObject(&m_nameBoldFont);
		const int nw = TextFB::Width(dc, m_nameRichName);
		TextFB::Draw(dc, m_nameRichName, CRect(x, rc.top, rc.right, rc.bottom), DT_LEFT, true);
		x += nw;
		// "  (서브이름)" (회색, 남는 폭만큼)
		if (!m_nameRichSub.IsEmpty() && x < rc.right - 8)
		{
			dc->SelectObject(base);
			dc->SetTextColor(TCOL(TC_SUB2));
			TextFB::Draw(dc, L"  (" + m_nameRichSub + L")", CRect(x, rc.top, rc.right, rc.bottom), DT_LEFT, true);
		}
		dc->SelectObject(old);
		return;
	}
	CDialogEx::OnDrawItem(nIDCtl, lpDis);
}

HBRUSH CRuliManagerDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (!m_bgBrush.GetSafeHandle())
		return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);

	switch (nCtlColor)
	{
	case CTLCOLOR_DLG:
		return m_bgBrush;

	case CTLCOLOR_EDIT:
		// 에디트 (검색, 제목, 메모 등)
		pDC->SetTextColor(kTextColor);
		pDC->SetBkColor(kEditColor);
		return m_editBrush;

	case CTLCOLOR_STATIC:
	{
		// 읽기 전용/비활성 에디트도 CTLCOLOR_STATIC 을 보냄 → 에디트 배경색 사용
		TCHAR cls[16] = {};
		::GetClassName(pWnd->GetSafeHwnd(), cls, 16);
		if (_tcsicmp(cls, _T("Edit")) == 0)
		{
			pDC->SetTextColor(pWnd->IsWindowEnabled() ? kTextColor : kDisabledText);
			pDC->SetBkColor(kEditColor);
			return m_editBrush;
		}
		pDC->SetTextColor(kTextColor);
		pDC->SetBkColor(kBackColor);
		pDC->SetBkMode(TRANSPARENT);
		return m_bgBrush;
	}

	case CTLCOLOR_BTN:
		pDC->SetBkColor(kBackColor);
		return m_bgBrush;   // 버튼 모서리가 흰색으로 보이지 않도록
	}
	return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CRuliManagerDlg::OnClose()
{
	CommitDetails();
	m_lib.Save();
	// 종료할 때 작업 DB 를 원본(library.vmdb)에 반영하고 작업 DB 삭제
	if (!m_lib.DbBroken())
	{
		if (m_lib.ApplyWorkDb())
			CVideoLibrary::DiscardWorkDb();
		else if (AfxMessageBox(L"DB 에 반영하지 못했습니다 (파일이 사용 중이거나 쓰기 권한 없음).\n"
			L"그래도 종료할까요? (변경 내용은 library.work.vmdb 에 남아 있어 다음 실행 때 반영할 수 있습니다)",
			MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
			return;
	}
	// 종료할 때 이미지 캐시 → 이미지 DB(images.vmdb) 하나로 묶고, 성공했을 때만 캐시 삭제
	bool imagesSaved = false;
	{
		CWaitCursor wait;
		SetDlgItemText(IDC_STATIC_STATUS, L"이미지 DB 저장 중...");
		CString err;
		imagesSaved = CVideoLibrary::SaveImageDb(&err) >= 0;
		if (!imagesSaved && AfxMessageBox(L"이미지 DB 를 저장하지 못했습니다.\n" + err +
			L"\n\n이미지 캐시는 지우지 않고 남겨 두며, 다음 실행 때 다시 합칩니다.\n그래도 종료할까요?",
			MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
			return;
	}
	// 이동식 디스크 영상의 커버 캐시 → 커버 DB(covers.vmdb), 성공했을 때만 캐시 삭제
	StopCoverThreads();   // 캐시에 쓰는 중인 커버가 없게
	bool coversSaved = false;
	{
		CWaitCursor wait;
		CString err;
		coversSaved = m_lib.SaveCoverDb(&err) >= 0;
		if (!coversSaved && AfxMessageBox(L"커버 DB 를 저장하지 못했습니다.\n" + err +
			L"\n\n커버 캐시는 지우지 않고 남겨 둡니다.\n그래도 종료할까요?",
			MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
			return;
	}
	SaveWindowPlacement();
	m_preview.Clear();
	if (imagesSaved)
		CVideoLibrary::ClearImageCache();
	if (coversSaved)
		CVideoLibrary::ClearCoverCache();
	EndDialog(IDCANCEL);
}

void CRuliManagerDlg::SaveWindowPlacement()
{
	WINDOWPLACEMENT wp = { sizeof(wp) };
	if (GetWindowPlacement(&wp))
		AfxGetApp()->WriteProfileBinary(L"Settings", L"WindowPlacement", reinterpret_cast<LPBYTE>(&wp), sizeof(wp));
}

void CRuliManagerDlg::RestoreWindowPlacement()
{
	LPBYTE data = nullptr;
	UINT size = 0;
	if (!AfxGetApp()->GetProfileBinary(L"Settings", L"WindowPlacement", &data, &size) || !data)
		return;
	WINDOWPLACEMENT wp = {};
	const bool ok = (size == sizeof(wp));
	if (ok)
		memcpy(&wp, data, sizeof(wp));
	delete[] data;
	if (!ok || wp.length != sizeof(wp))
		return;

	// 저장된 위치가 지금 연결된 모니터 밖이면(모니터 구성 변경 등) 복원하지 않음
	CRect rc(wp.rcNormalPosition);
	if (rc.Width() < 200 || rc.Height() < 150 || !::MonitorFromRect(&rc, MONITOR_DEFAULTTONULL))
		return;
	if (wp.showCmd == SW_SHOWMINIMIZED || wp.showCmd == SW_MINIMIZE)
		wp.showCmd = SW_SHOWNORMAL;   // 최소화 상태로는 시작하지 않음
	wp.flags = 0;
	SetWindowPlacement(&wp);
}

// ---------------------------------------------------------------------------
// 영상 상세 정보: 태그 아래 출연 배우 카드

void CRuliManagerDlg::UpdateNamedActorStrip(int kind, const CString& name)
{
	if (!m_actorStrip.GetSafeHwnd())
		return;
	const bool was = m_namedActors;
	m_namedActors = (kind != 0);
	if (m_namedActors)
	{
		// 그 제작사 / 레이블 영상의 출연 배우 (출연 편수 많은 순, 같으면 이름 순)
		//  - 제작사: 그 제작사 영상 + 하위 레이블 영상 (레이블의 상위 제작사가 이 제작사)
		std::map<int, int> count;
		for (const VideoItem& v : m_lib.items)
		{
			bool hit = false;
			if (kind == 1)
			{
				hit = !v.studio.IsEmpty() && v.studio.CompareNoCase(name) == 0;
				if (!hit && !v.label.IsEmpty())
				{
					const int li = m_lib.FindLabel(v.label);
					hit = li >= 0 && m_lib.labelInfos[li].parent.CompareNoCase(name) == 0;
				}
			}
			else
				hit = !v.label.IsEmpty() && v.label.CompareNoCase(name) == 0;
			if (!hit)
				continue;
			std::set<int> seen;
			for (const CString& n : CVideoLibrary::SplitList(v.actors))
			{
				const int idx = m_lib.FindActorByAnyName(n);
				if (idx >= 0 && seen.insert(idx).second)
					++count[idx];
			}
		}
		std::vector<int> list;
		for (const auto& kv : count)
			list.push_back(kv.first);
		std::sort(list.begin(), list.end(), [&](int a, int b)
		{
			if (count[a] != count[b])
				return count[a] > count[b];
			return ::StrCmpLogicalW(m_lib.actors[a].name, m_lib.actors[b].name) < 0;
		});
		m_stripActors.swap(list);
		m_actorStrip.SetCount(static_cast<int>(m_stripActors.size()));
		m_actorStrip.Invalidate(FALSE);
	}
	if (was != m_namedActors && m_layoutReady)
	{
		CRect client;
		GetClientRect(&client);
		LayoutControls(client.Width(), client.Height());
	}
}

void CRuliManagerDlg::OnVideoScreenshot()
{
	const int idx = GetSelectedItem();
	if (idx < 0 || IsItemOffline(idx))
		return;
	const CString path = m_lib.items[idx].path;
	if (!::PathFileExistsW(path))
	{
		AfxMessageBox(L"영상 파일이 없습니다.", MB_ICONWARNING);
		return;
	}
	if ((m_shotBusy && path.CompareNoCase(m_shotVideoPath) == 0) || ShotQueuePos(path) > 0)
	{
		AfxMessageBox(L"이 영상은 이미 스크린샷을 추출하는 중이거나 대기열에 있습니다.", MB_ICONINFORMATION);
		return;
	}
	CString def;
	def.Format(L"%d", AfxGetApp()->GetProfileInt(L"Settings", L"ShotCount", 8));
	CInputDlg dlg(def, this, L"스크린샷 추출", L"찍을 장수 (1 ~ 100)");
	if (dlg.DoModal() != IDOK)
		return;
	const int count = _wtoi(dlg.m_value);
	if (count < 1 || count > 100)
	{
		AfxMessageBox(L"1 ~ 100 사이의 숫자를 입력하세요.", MB_ICONWARNING);
		return;
	}
	AfxGetApp()->WriteProfileInt(L"Settings", L"ShotCount", count);
	// 대기열 맨 뒤에 넣음 → 쉬고 있으면 바로 시작
	m_shotQueue.push_back({ path, count });
	if (!m_shotBusy)
	{
		GetWindowText(m_shotTitleBase);   // 처음 시작할 때의 창 제목 (모두 끝나면 되돌림)
		StartNextShot();
	}
	else
	{
		UpdateShotTitle();
		InvalidateShotCards();   // 카드에 "(대기 n)"
	}
}

int CRuliManagerDlg::ShotQueuePos(const CString& path) const
{
	int pos = 0;
	for (const ShotJob& j : m_shotQueue)
	{
		++pos;
		if (j.path.CompareNoCase(path) == 0)
			return pos;
	}
	return 0;
}

void CRuliManagerDlg::UpdateShotTitle()
{
	if (!m_shotBusy)
	{
		SetWindowText(m_shotTitleBase);
		return;
	}
	CString t;
	t.Format(L"%s  -  스크린샷 추출 중 (%d / %d)", static_cast<LPCWSTR>(m_shotTitleBase), m_shotDone, m_shotTotal);
	if (!m_shotQueue.empty())
	{
		CString q;
		q.Format(L" · 대기 %d", static_cast<int>(m_shotQueue.size()));
		t += q;
	}
	SetWindowText(t);
}

void CRuliManagerDlg::StartNextShot()
{
	if (m_shotThread.joinable())
		m_shotThread.join();
	if (m_shotQueue.empty())
	{
		// 모두 끝남
		m_shotBusy = false;
		m_shotVideoPath.Empty();
		UpdateShotTitle();
		InvalidateShotCards();
		return;
	}
	const ShotJob job = m_shotQueue.front();
	m_shotQueue.pop_front();
	m_shotBusy = true;
	m_shotCancel = false;
	m_shotVideoPath = job.path;
	m_shotDone = 0;
	m_shotTotal = job.count;
	UpdateShotTitle();
	InvalidateShotCards();   // 진행 표시 · 대기 순번
	const HWND hwnd = GetSafeHwnd();
	const CString path = job.path;
	const int count = job.count;
	std::atomic<bool>* cancel = &m_shotCancel;   // 창이 닫힐 때 OnDestroy 가 스레드를 기다리므로 유효
	m_shotThread = std::thread([hwnd, path, count, cancel]()
	{
		ShotResult* r = new ShotResult(ExtractScreenshots(path, count, *cancel,
			[hwnd](int done, int total) { ::PostMessageW(hwnd, WM_APP_SHOT_PROGRESS, static_cast<WPARAM>(done), static_cast<LPARAM>(total)); }));
		if (!::PostMessageW(hwnd, WM_APP_SHOT_DONE, 0, reinterpret_cast<LPARAM>(r)))
			delete r;
	});
}

LRESULT CRuliManagerDlg::OnShotProgress(WPARAM wParam, LPARAM lParam)
{
	if (!m_shotBusy)
		return 0;
	m_shotDone = static_cast<int>(wParam);
	m_shotTotal = static_cast<int>(lParam);
	UpdateShotTitle();
	InvalidateShotCards();   // 영상 카드의 진행 표시
	return 0;
}

LRESULT CRuliManagerDlg::OnShotDone(WPARAM, LPARAM lParam)
{
	std::unique_ptr<ShotResult> r(reinterpret_cast<ShotResult*>(lParam));
	if (m_shotThread.joinable())
		m_shotThread.join();
	m_shotThumbs.clear();   // 같은 이름으로 다시 저장했을 수 있음
	m_shotFiles.clear();
	RefreshShotStrip();     // 영상 스크린샷 설정이면 바로 격자에
	StartNextShot();        // 대기열의 다음 영상 (없으면 끝)
	// 끝났을 때 알림 창은 띄우지 않음 (영상을 열지 못한 경우만 알림 - 다음 작업은 이미 시작함)
	if (r && !r->error.IsEmpty())
		AfxMessageBox(CString(::PathFindFileNameW(r->video)) + L"\n\n" + r->error, MB_ICONWARNING);
	return 0;
}

// ---------------------------------------------------------------------------
// 미리보기 영상 (ffmpeg)

CString CRuliManagerDlg::PreviewFileOf(int idx) const
{
	if (idx < 0 || idx >= static_cast<int>(m_lib.items.size()))
		return CString();
	const CString& path = m_lib.items[idx].path;
	return PreviewVideo::PreviewFileFor(path, m_lib.CoverCacheFile(path));
}

void CRuliManagerDlg::StopPreviewPlayer()
{
	if (m_pPrevPlayer->IsPlaying())
		m_pPrevPlayer->Stop();
	if (m_pNextPlayer->IsPlaying())
		m_pNextPlayer->Stop();
	m_nextHoverItem = -1;
	UpdateGridPartOverlay();   // 파트 칸 글자 창도 숨김
}

void CRuliManagerDlg::UpdateGridPartOverlay()
{
	// [격자] · [패키지] 에서 분할 영상을 합친 카드의 미리보기가 재생 중이고 마우스가 그 이미지 위: 재생 창 위에 파트 칸 ([월 모드] 와 같은 그림)
	//  재생 창(격자 자식)은 카드 그림을 가리므로 겹친 투명 글자 창에 그림 - 격자 밖으로 나간 부분은 잘라서 보임
	int g = -1;
	CRect show;
	if (m_pPrevPlayer->IsPlaying() && m_pPrevPlayer->IsWindowVisible() && !VideoWallActive() && !VideoPortraitActive() && !m_pkgPreviewOn
		&& m_grid.GetSafeHwnd() && m_grid.IsWindowVisible() && !IsIconic())
	{
		const int hot = m_grid.HotRow();
		CRect card;
		if (hot >= 0 && m_grid.HotZone() == 1 && m_gridParts.find(hot) != m_gridParts.end() && m_grid.GetCardRect(hot, card))
		{
			CRect a(card.left, card.top, card.right, VideoImageCardActive() ? card.bottom : card.top + m_vcardImgH);   // 이미지 영역 (PreviewItemAt 과 같음)
			m_grid.ClientToScreen(&a);
			CRect gc;
			m_grid.GetClientRect(&gc);
			m_grid.ClientToScreen(&gc);
			// 마우스가 kGridPartIdleMs 동안 멈춰 있으면 숨김 (영상만 보이게) - 다시 움직이면 바로 보임
			CPoint cur;
			::GetCursorPos(&cur);
			const ULONGLONG now = ::GetTickCount64();
			if (cur != m_gridPartMouse)
			{
				m_gridPartMouse = cur;
				m_gridPartMoveTick = now;
			}
			const bool idle = (now - m_gridPartMoveTick >= kGridPartIdleMs);
			if (!idle && a == m_hoverAnchor && show.IntersectRect(a, gc))
			{
				g = hot;
				m_gridPartCard = a;
				m_gridPartCard.OffsetRect(-show.left, -show.top);
			}
		}
	}
	if (g < 0)
	{
		m_gridPartG = -1;
		m_gridPartSig = 0;
		if (m_gridPartOverlay.GetSafeHwnd() && m_gridPartOverlay.IsWindowVisible())
			m_gridPartOverlay.HideOverlay();
		return;
	}
	m_gridPartG = g;
	m_gridPartOverlay.ShowOver(this, show);   // 자리 · 크기가 바뀌었거나 새로 보일 때 다시 그림
	size_t sig = 1469598103934665603ULL;
	auto mix = [&sig](size_t v) { sig = (sig ^ v) * 1099511628211ULL; };
	mix(static_cast<size_t>(g + 1));
	mix(static_cast<size_t>(m_grid.HotPart() + 1));
	mix(static_cast<size_t>(GetSelectedRow() + 1));
	mix(static_cast<size_t>(m_gridPartCard.left + 100000));
	mix(static_cast<size_t>(m_gridPartCard.top + 100000));
	mix(static_cast<size_t>(show.Width()) * 65536 + static_cast<size_t>(show.Height()));
	if (sig != m_gridPartSig)
	{
		m_gridPartSig = sig;
		m_gridPartOverlay.Invalidate(FALSE);
	}
}

bool CRuliManagerDlg::CountHoverAllowed() const
{
	GUITHREADINFO gti = { sizeof(gti) };
	const bool inMenu = ::GetGUIThreadInfo(::GetCurrentThreadId(), &gti) && (gti.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE));
	return !inMenu && ::GetForegroundWindow() == GetSafeHwnd();
}

void CRuliManagerDlg::HotCountBack(CDC* dc, CVideoGrid& grid, int left, int width, int y, int lineH, int pad)
{
	// 카드 아래 집계(아이콘 + 숫자) 한 묶음: 마우스가 그 자리에 있으면 둥근 사각형 배경 (목록 마우스 오버 색)
	//  [패키지] 팝업 카드를 그리는 중이면 팝업 기준 (마우스 좌표 · 영역)
	CWnd* host = m_popupCardDraw ? static_cast<CWnd*>(&m_pkgPopup) : static_cast<CWnd*>(&grid);
	if (!host->GetSafeHwnd() || !host->IsWindowVisible() || !CountHoverAllowed())
		return;
	if (lineH < 0) lineH = m_cardLine;
	if (pad < 0) pad = m_cardPad;
	const CRect hb(left - DX(3), y - pad / 2, left + width + DX(3), y + lineH + pad / 2);
	CPoint pt;
	::GetCursorPos(&pt);
	host->ScreenToClient(&pt);
	CRect gc;
	host->GetClientRect(&gc);
	if (!gc.PtInRect(pt) || !hb.PtInRect(pt))
		return;
	CBrush br(TCOL(TC_HILITE));
	CPen pen(PS_SOLID, 1, TCOL(TC_HILITE));
	CBrush* ob = dc->SelectObject(&br);
	CPen* op = dc->SelectObject(&pen);
	dc->RoundRect(hb, CPoint(DX(3), DX(3)));
	dc->SelectObject(ob);
	dc->SelectObject(op);
}

void CRuliManagerDlg::CheckCountHover()
{
	// 카드 아래 집계 줄 배경 강조: 마우스가 움직이면 그 카드(와 이전 카드)의 아래 줄만 다시 그림 (영상 · 배우 · 제작사 / 태그 / 컬렉션 격자)
	CPoint cur;
	::GetCursorPos(&cur);
	HWND hotGrid = nullptr;
	CRect band;
	if (CountHoverAllowed())
	{
		CVideoGrid* grids[] = { &m_grid, &m_actorGrid, &m_catGrid };
		for (CVideoGrid* gp : grids)
		{
			if (!gp->GetSafeHwnd() || !gp->IsWindowVisible())
				continue;
			CRect gr;
			gp->GetWindowRect(&gr);
			if (!gr.PtInRect(cur))
				continue;
			CPoint pt = cur;
			gp->ScreenToClient(&pt);
			const int g = gp->HitTest(pt);
			CRect card;
			if (g >= 0 && gp->GetCardRect(g, card))
			{
				hotGrid = gp->GetSafeHwnd();
				band = CRect(card.left, card.bottom - (m_cardPad * 2 + m_cardLine + DX(4)), card.right, card.bottom);   // 아래 집계 줄
			}
			break;
		}
	}
	const bool inBand = hotGrid && [&]() { CPoint pt = cur; ::ScreenToClient(hotGrid, &pt); return band.PtInRect(pt) != FALSE; }();
	// 이전 줄 (다른 카드로 갔거나 줄을 벗어남) 지우기
	if (m_countHotGrid && (m_countHotGrid != hotGrid || m_countHotBand != band || !inBand))
	{
		if (::IsWindow(m_countHotGrid))
			::InvalidateRect(m_countHotGrid, &m_countHotBand, FALSE);
		m_countHotGrid = nullptr;
		m_countHotBand.SetRectEmpty();
	}
	if (inBand)
	{
		// 줄 안에서 움직이면 다시 그림 (묶음 사이를 옮겨 다님)
		if (m_countHotGrid != hotGrid || cur != m_countHotPt)
			::InvalidateRect(hotGrid, &band, FALSE);
		m_countHotGrid = hotGrid;
		m_countHotBand = band;
		m_countHotPt = cur;
	}
}

bool CRuliManagerDlg::VideoCardCountZone(int g, const CRect& card, int which, CRect& zone, bool popupCard)
{
	// DrawVideoCard 의 아래 개수 줄(태그 수 · 배우 수 · 물방울)과 같은 계산 - which 0 = 태그 수, 1 = 배우 수 (아이콘 + 숫자 자리)
	if (VideoImageCardActive() && !popupCard)
		return false;   // [패키지] · [월 모드]: 아래 글자 없음
	const int row = GridRowOf(g);
	if (row < 0 || row >= static_cast<int>(m_view.size()))
		return false;
	const VideoItem& v = m_lib.items[m_view[row]];
	const int tags = static_cast<int>(CVideoLibrary::SplitList(v.tags).size());
	const int actors = static_cast<int>(CVideoLibrary::SplitList(v.actors).size());
	const int drops = v.oCount;
	if ((which == 0 && tags <= 0) || (which == 1 && actors <= 0))
		return false;
	CString tText, aText, oText;
	tText.Format(L"%d", tags);
	aText.Format(L"%d", actors);
	oText.Format(L"%d", drops);
	CClientDC dc(&m_grid);
	CFont* of = dc.SelectObject(GetFont());
	const int iconR = (std::max)(4, m_cardLine * 2 / 5);
	const int gapIcon = DX(3), gapGroup = DX(10);
	const int tw = dc.GetTextExtent(tText).cx, aw = dc.GetTextExtent(aText).cx, ow = dc.GetTextExtent(oText).cx;
	dc.SelectObject(of);
	int total = 0;
	if (tags > 0)
		total += iconR * 2 + gapIcon + tw;
	if (actors > 0)
		total += (total > 0 ? gapGroup : 0) + iconR * 2 + gapIcon + aw;
	if (drops > 0)
		total += (total > 0 ? gapGroup : 0) + iconR * 2 + gapIcon + ow;
	int sx = card.left + (card.Width() - total) / 2;
	int w = iconR * 2 + gapIcon + tw;   // 태그 수 자리 (맨 앞)
	if (which == 1)
	{
		if (tags > 0)
			sx += iconR * 2 + gapIcon + tw + gapGroup;
		w = iconR * 2 + gapIcon + aw;
	}
	const int y = card.top + m_vcardH - m_cardPad - m_cardLine;   // 개수 줄 (카드 맨 아래 칸)
	zone = CRect(sx - DX(3), y - m_cardPad / 2, sx + w + DX(3), y + m_cardLine + m_cardPad / 2);
	return true;
}

int CRuliManagerDlg::CastCardH()
{
	return m_stripCardW * 4 / 3 + m_cardLine + DX(6);   // 사진 3:4 + 이름 한 줄
}

void CRuliManagerDlg::DrawCastCard(CDC* dc, int idx, const CRect& rc)
{
	// 출연 배우 팝업 카드: 사진(3:4) + 이름 한 줄만 (별점 · 국기 · 하트 · 나이 없음)
	if (idx < 0 || idx >= static_cast<int>(m_lib.actors.size()))
		return;
	const ActorInfo& a = m_lib.actors[idx];
	const int radius = DX(4);
	{
		CBrush br(kCardBack);
		CPen pen(PS_SOLID, 1, kCardBack);
		CBrush* ob = dc->SelectObject(&br);
		CPen* op = dc->SelectObject(&pen);
		dc->RoundRect(rc, CPoint(radius, radius));
		dc->SelectObject(ob);
		dc->SelectObject(op);
	}
	const CRect img(rc.left, rc.top, rc.right, rc.top + rc.Width() * 4 / 3);
	{
		CRgn clip;
		clip.CreateRoundRectRgn(rc.left, rc.top, rc.right + 1, rc.bottom + 1, radius, radius);
		dc->SelectClipRgn(&clip);
		dc->IntersectClipRect(img);
		if (CBitmap* bmp = GetActorPortrait(idx, img.Width(), img.Height()))
		{
			CDC mem;
			mem.CreateCompatibleDC(dc);
			CBitmap* old = mem.SelectObject(bmp);
			dc->BitBlt(img.left, img.top, img.Width(), img.Height(), &mem, 0, 0, SRCCOPY);
			mem.SelectObject(old);
		}
		else
		{
			dc->FillSolidRect(img, kCardImage);
			DrawDefaultActorImage(dc, img, a);
		}
		dc->SelectClipRgn(nullptr);
	}
	CRect nr(rc.left + DX(3), img.bottom, rc.right - DX(3), rc.bottom);
	dc->SetBkMode(TRANSPARENT);
	dc->SetTextColor(kTextColor);
	CFont* of = dc->SelectObject(GetFont());
	dc->DrawText(a.name, nr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	dc->SelectObject(of);
}

void CRuliManagerDlg::CheckPackageHover()
{
	// [패키지]: 카드에 마우스를 0.25초 올려 두면 [격자] 영상 카드(이미지 + 제목 · 발매일 · 메모 · 태그 / 배우 수)를 팝업
	//  - 자리: 패키지 카드 가운데에 겹쳐서 (화면 밖으로 나가면 안쪽으로), 크기: 지금 확대 단계의 [격자] 카드 크기
	//  - 팝업은 마우스를 통과 (아래 패키지 카드가 클릭 · 더블클릭 · 오른쪽 클릭을 받음), 다른 카드로 가거나 벗어나면 닫음
	int g = -1;
	CRect cardScreen;
	{
		CPoint cur;
		::GetCursorPos(&cur);
		GUITHREADINFO gti = { sizeof(gti) };
		const bool inMenu = ::GetGUIThreadInfo(::GetCurrentThreadId(), &gti) && (gti.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE));
		const bool ok = VideoPortraitActive() && ShowVideoDetail() && !VideoListActive() && !YearViewActive()
			&& m_grid.GetSafeHwnd() && m_grid.IsWindowVisible() && ::GetForegroundWindow() == GetSafeHwnd() && !inMenu;
		if (ok && m_pkgHoverG >= 0 && m_pkgPopup.GetSafeHwnd() && m_pkgPopup.IsWindowVisible())
		{
			// 팝업이 보이는 동안: 마우스가 팝업 안이면 유지 (팝업이 패키지 카드보다 커서 카드 밖이어도) - 패키지 카드가 움직였으면(스크롤) 닫음
			CRect pr, card;
			m_pkgPopup.GetWindowRect(&pr);
			if (pr.PtInRect(cur) && PkgAnchorRect(m_pkgHoverG, card))
			{
				m_grid.ClientToScreen(&card);
				if (card == m_pkgHoverCard)
				{
					const ULONGLONG t = ::GetTickCount64();
					if (t - m_pkgRepaintTick >= 250)
					{
						m_pkgRepaintTick = t;
						m_pkgPopup.Invalidate(FALSE);   // 커버 · 재생 시간을 읽는 중이었으면 다 읽은 뒤 보이게
					}
					return;
				}
			}
		}
		CRect gr;
		if (ok)
			m_grid.GetWindowRect(&gr);
		if (ok && gr.PtInRect(cur))
		{
			CPoint pt = cur;
			m_grid.ScreenToClient(&pt);
			const int hit = m_grid.HitTest(pt);
			const int row = (hit >= 0) ? GridRowOf(hit) : -1;
			CRect card;
			if (row >= 0 && row < static_cast<int>(m_view.size()) && PkgAnchorRect(hit, card) && card.PtInRect(pt))
			{
				g = hit;
				cardScreen = card;
				m_grid.ClientToScreen(&cardScreen);
			}
		}
	}
	const ULONGLONG now = ::GetTickCount64();
	if (g != m_pkgHoverG || (g >= 0 && cardScreen != m_pkgHoverCard))
	{
		// 다른 카드 (또는 카드 밖 · 스크롤로 움직임): 닫고 다시 기다림
		m_pkgHoverG = g;
		m_pkgHoverCard = cardScreen;
		m_pkgHoverTick = now;
		m_pkgPopup.Hide();
		return;
	}
	if (g < 0 || now < m_pkgHoverTick + 250)
		return;
	if (!m_pkgPopup.GetSafeHwnd())
		return;
	if (!m_pkgPopup.IsWindowVisible())
	{
		m_pkgPopup.ShowCentered(cardScreen, CSize(m_vcardW, m_vcardH));
		m_pkgRepaintTick = now;
	}
	else if (now - m_pkgRepaintTick >= 250)
	{
		m_pkgRepaintTick = now;
		m_pkgPopup.Invalidate(FALSE);   // [격자] 크기 커버 · 재생 시간을 읽는 중이었으면 다 읽은 뒤 보이게
	}
}

int CRuliManagerDlg::PkgPartAt(CPoint pt) const
{
	if (m_pkgHoverG < 0)
		return -1;
	auto it = m_gridParts.find(m_pkgHoverG);
	const CRect img(0, 0, m_vcardW, m_vcardImgH);   // 팝업 카드의 커버 영역 (DrawVideoCard 의 img)
	if (it == m_gridParts.end() || it->second.empty() || !img.PtInRect(pt))
		return -1;
	const int n = static_cast<int>(it->second.size());
	return (std::min)(n - 1, (std::max)(0, static_cast<int>(pt.x * n / (std::max)(1, img.Width()))));
}

void CRuliManagerDlg::OnPkgPopupMouse(UINT msg, CPoint pt)
{
	// [패키지] 팝업 카드: 클릭 = 고르기 (분할 영상은 커버 위 파트 칸의 파트), 더블클릭 = 재생, 오른쪽 클릭 = 영상 메뉴
	const int g = m_pkgHoverG;
	const int row = (g >= 0) ? GridRowOf(g) : -1;
	if (row < 0 || row >= static_cast<int>(m_view.size()))
		return;
	auto select = [&]()
	{
		const int z = PkgPartAt(pt);
		if (z >= 0)
			SelectViewRow(m_gridParts[g][z]);   // 누른 칸의 파트
		else
			GridSetSel(g);
		if (m_grid.GetSafeHwnd())
			m_grid.SetFocus();   // 키보드는 격자로
		m_pkgPopup.Invalidate(FALSE);   // 선택 테두리 · 고른 파트
		m_pkgPartSig = 0;               // 파트 칸 (고른 파트 파란 줄) 다시 그림
	};
	switch (msg)
	{
	case WM_LBUTTONDOWN:
		select();
		break;
	case WM_LBUTTONDBLCLK:
		select();
		GridActivate(g);   // 재생 (분할 영상은 고른 파트)
		break;
	case WM_RBUTTONUP:
	{
		select();
		CPoint sp = pt;
		m_pkgPopup.ClientToScreen(&sp);
		ShowVideoContextMenu(GetSelectedRow(), sp);   // 메뉴가 열리면 팝업은 닫힘 (CheckPackageHover)
		break;
	}
	default:
		break;
	}
}

void CRuliManagerDlg::CheckPkgPreview()
{
	// [패키지] 팝업 카드 커버에 마우스: 미리보기 재생 (팝업의 자식 창 - 커버 영역 크기), 분할 영상은 마우스 아래 파트 (끊김 없이 바꿈)
	//  + 분할 영상 파트 칸 (구분선 · 번호 · 올라간 칸 테두리 · 고른 파트 파란 줄) - 마우스가 kGridPartIdleMs 동안 멈추면 숨김
	const bool vis = VideoPortraitActive() && m_pkgHoverG >= 0 && m_pkgPopup.GetSafeHwnd() && m_pkgPopup.IsWindowVisible() && !IsIconic();
	CPoint cur;
	::GetCursorPos(&cur);
	CPoint pt = cur;
	if (m_pkgPopup.GetSafeHwnd())
		m_pkgPopup.ScreenToClient(&pt);
	const CRect img(0, 0, m_vcardW, m_vcardImgH);
	const bool onCover = vis && img.PtInRect(pt) && !m_pkgPopup.IsFading();   // 나타나는 중(투명 효과)에는 재생 창을 만들지 않음 - 다 나타난 뒤 재생
	const int row = (m_pkgHoverG >= 0) ? GridRowOf(m_pkgHoverG) : -1;
	int item = -1;
	int part = -1;
	if (onCover && row >= 0 && row < static_cast<int>(m_view.size()))
	{
		part = PkgPartAt(pt);
		item = (part >= 0) ? m_view[m_gridParts[m_pkgHoverG][part]] : m_view[row];
	}

	// 파트 칸 글자 창
	{
		const ULONGLONG now = ::GetTickCount64();
		if (cur != m_pkgMouse)
		{
			m_pkgMouse = cur;
			m_pkgMoveTick = now;
		}
		const bool idle = (now - m_pkgMoveTick >= kGridPartIdleMs);
		const int hot = (part >= 0 && !idle) ? part : -1;
		if (hot < 0)
		{
			m_pkgPartHot = -1;
			m_pkgPartSig = 0;
			if (m_pkgPartOverlay.GetSafeHwnd() && m_pkgPartOverlay.IsWindowVisible())
				m_pkgPartOverlay.HideOverlay();
		}
		else
		{
			m_pkgPartHot = hot;
			CRect sr = img;
			m_pkgPopup.ClientToScreen(&sr);
			m_pkgPartOverlay.ShowOver(&m_pkgPopup, sr);   // 팝업에 딸린 창 (팝업 위에 보임)
			const size_t sig = (static_cast<size_t>(m_pkgHoverG + 1) * 1000003u) ^ (static_cast<size_t>(hot + 1) * 7919u) ^ (static_cast<size_t>(GetSelectedRow() + 1) * 31u);
			if (sig != m_pkgPartSig)
			{
				m_pkgPartSig = sig;
				m_pkgPartOverlay.Invalidate(FALSE);
			}
		}
	}

	if (item < 0)
	{
		// m_hoverItem 은 [격자] 미리보기(CheckPreviewHover)와 같이 쓰므로 [패키지] 일 때만 비움
		//  (예전: 늘 비워서 [격자] 에서 매번 "다른 카드" 로 보여 미리보기가 시작되지 않던 것)
		if (m_pkgPreviewOn)
		{
			StopPreviewPlayer();
			m_pkgPreviewOn = false;
			m_hoverItem = -1;
		}
		else if (VideoPortraitActive())
			m_hoverItem = -1;
		return;
	}
	if (m_pkgPreviewOn && m_pPrevPlayer->IsPlaying())
	{
		// 재생 중: 다른 파트로 가면 뒤에서 열어 두고 준비되면 바꿔 끼움
		if (item != m_hoverItem)
			StartNextPart(item);
		else if (m_nextHoverItem >= 0)
		{
			m_pNextPlayer->Stop();
			m_nextHoverItem = -1;
		}
		SwapNextPart();
		return;
	}
	m_pkgPreviewOn = false;
	const ULONGLONG now = ::GetTickCount64();
	if (item == m_hoverItem && now < m_hoverTick)
		return;   // 이 파트는 미리보기가 없거나 열지 못함: 잠시 뒤 다시
	m_hoverItem = item;
	m_hoverTick = now + 1000;
	if (IsSfwMosaic() || IsItemOffline(item))
		return;
	if (m_prevBusy && m_lib.items[item].path.CompareNoCase(m_prevBusyPath) == 0)
		return;   // 만드는 중
	const CString file = PreviewFileOf(item);
	if (!::PathFileExistsW(file))
		return;
	if (m_pPrevPlayer->StartAt(&m_pkgPopup, img, file, RGB(0, 0, 0)))
	{
		m_pkgPreviewOn = true;
		m_hoverTick = now;
	}
	else
		m_hoverTick = now + 60000;
}

void CRuliManagerDlg::CheckCastHover()
{
	// 영상 카드(가로 격자) 아래 배우 수에 마우스를 0.25초 올려 두면 카드 아래에 출연 배우 카드를 작게 (여러 명이면 좌우로)
	int item = -1, kind = 0;
	CRect cardScreen, zoneScreen;
	CPoint cur;
	::GetCursorPos(&cur);
	GUITHREADINFO gti = { sizeof(gti) };
	const bool inMenu = ::GetGUIThreadInfo(::GetCurrentThreadId(), &gti) && (gti.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE));
	const bool gridOk = ShowVideoDetail() && !VideoListActive() && !YearViewActive() && !VideoImageCardActive()
		&& m_grid.GetSafeHwnd() && m_grid.IsWindowVisible() && ::GetForegroundWindow() == GetSafeHwnd() && !inMenu;
	bool inPkg = false;   // [패키지] 팝업의 [격자] 카드 아래 개수 자리
	if (VideoPortraitActive() && m_pkgHoverG >= 0 && m_pkgPopup.GetSafeHwnd() && m_pkgPopup.IsWindowVisible() && !m_pkgPopup.IsFading()
		&& ::GetForegroundWindow() == GetSafeHwnd() && !inMenu)
	{
		CRect pr;
		m_pkgPopup.GetWindowRect(&pr);
		const int row = GridRowOf(m_pkgHoverG);
		if (pr.PtInRect(cur) && row >= 0 && row < static_cast<int>(m_view.size()))
		{
			CPoint pt = cur;
			m_pkgPopup.ScreenToClient(&pt);
			const CRect card(0, 0, m_vcardW, m_vcardH);
			CRect zone;
			if (VideoCardCountZone(m_pkgHoverG, card, 1, zone, true) && zone.PtInRect(pt))
				kind = 1;   // 배우 수
			else if (VideoCardCountZone(m_pkgHoverG, card, 0, zone, true) && zone.PtInRect(pt))
				kind = 2;   // 태그 수
			if (kind != 0)
			{
				item = m_view[row];
				cardScreen = card;
				zoneScreen = zone;
				m_pkgPopup.ClientToScreen(&cardScreen);
				m_pkgPopup.ClientToScreen(&zoneScreen);
				inPkg = true;
			}
		}
	}
	if (gridOk)
	{
		CRect gr;
		m_grid.GetWindowRect(&gr);
		if (gr.PtInRect(cur))
		{
			CPoint pt = cur;
			m_grid.ScreenToClient(&pt);
			const int g = m_grid.HitTest(pt);
			const int row = (g >= 0) ? GridRowOf(g) : -1;
			CRect card, zone;
			if (row >= 0 && row < static_cast<int>(m_view.size()) && m_grid.GetCardRect(g, card))
			{
				if (VideoCardCountZone(g, card, 1, zone) && zone.PtInRect(pt))
					kind = 1;   // 배우 수
				else if (VideoCardCountZone(g, card, 0, zone) && zone.PtInRect(pt))
					kind = 2;   // 태그 수
			}
			if (kind != 0)
			{
				item = m_view[row];
				cardScreen = card;
				zoneScreen = zone;
				m_grid.ClientToScreen(&cardScreen);
				m_grid.ClientToScreen(&zoneScreen);
			}
		}
	}
	if (item != m_castItem || kind != m_castKind)
	{
		// 개수 자리 배경 강조: 예전 카드 · 새 카드 다시 그림
		if (!m_castCardRect.IsRectEmpty())
		{
			if (m_castInPkg)
			{
				if (m_pkgPopup.GetSafeHwnd())
					m_pkgPopup.Invalidate(FALSE);   // [패키지] 팝업 카드
			}
			else
				m_grid.InvalidateRect(m_castCardRect, FALSE);
		}
		m_castCardRect.SetRectEmpty();
		m_castInPkg = inPkg;
		if (item >= 0)
		{
			m_castCardRect = cardScreen;
			if (inPkg)
				m_pkgPopup.Invalidate(FALSE);
			else
			{
				m_grid.ScreenToClient(&m_castCardRect);
				m_grid.InvalidateRect(m_castCardRect, FALSE);
			}
		}
		m_castItem = item;
		m_castKind = kind;
		m_castTick = ::GetTickCount64();
		m_castPopup.Hide();
		return;
	}
	if (item < 0 || ::GetTickCount64() < m_castTick + 250)
		return;
	if (m_castPopup.GetSafeHwnd() && m_castPopup.IsWindowVisible())
		return;   // 이미 보임
	const CRect anchorBox(zoneScreen.left, cardScreen.top, zoneScreen.right, cardScreen.bottom);   // 카드 아래 (가로 가운데는 개수 자리)
	if (kind == 2)
	{
		// 태그 수: 태그 칩 ([리스트] 태그 칸과 같은 칩), 폭은 최대 DX(320) 안에서 줄바꿈 - 모두 보이는 높이로
		m_castTags = m_lib.items[item].tags;
		CClientDC dc(this);
		CFont* of = dc.SelectObject(m_listFont.GetSafeHandle() ? &m_listFont : GetFont());   // DrawListTagChips 와 같은 글꼴
		TEXTMETRIC tm = {};
		dc.GetTextMetrics(&tm);
		const int chipH = tm.tmHeight + 6, padX = 7, gap = 4, margin = 4;
		const int maxInner = DX(320);
		int x = 0, lines = 0, widest = 0;
		for (const CString& raw : CVideoLibrary::SplitList(m_castTags))
		{
			CString t = raw;
			t.Trim();
			if (t.IsEmpty())
				continue;
			const int w = (std::min)(maxInner, TextFB::Width(&dc, t) + padX * 2);
			if (lines == 0)
				lines = 1;
			if (x > 0 && x + w > maxInner)
			{
				++lines;
				x = 0;
			}
			x += w;
			widest = (std::max)(widest, x);
			x += gap;
		}
		dc.SelectObject(of);
		if (lines == 0)
			return;
		const int w = widest + margin * 2 + 2;
		const int hgt = lines * (chipH + gap) - gap + margin * 2 + 1;
		m_castPopup.Invalidate(FALSE);
		m_castPopup.ShowNear(anchorBox, CSize(w, hgt), true);
		return;
	}
	// 출연 배우 (DB 에 있는 배우만, 이름 또는 별칭으로)
	std::vector<int> actors;
	for (const CString& n : CVideoLibrary::SplitList(m_lib.items[item].actors))
	{
		const int ai = m_lib.FindActorByAnyName(n);
		if (ai >= 0 && std::find(actors.begin(), actors.end(), ai) == actors.end())
			actors.push_back(ai);
	}
	if (actors.empty())
		return;
	// 화면 폭에 들어가는 만큼 (넘치면 마지막 칸에 "+N")
	MONITORINFO mi = { sizeof(mi) };
	CRect work(0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN));
	if (::GetMonitorInfoW(::MonitorFromPoint(cur, MONITOR_DEFAULTTONEAREST), &mi))
		work = mi.rcWork;
	const int pad = DX(5), gap = DX(4), moreW = DX(28);
	const int fit = (std::max)(1, (work.Width() - pad * 2 + gap) / (m_stripCardW + gap));
	m_castMore = 0;
	if (static_cast<int>(actors.size()) > fit)
	{
		const int keep = (std::max)(1, (work.Width() - pad * 2 - moreW) / (m_stripCardW + gap));
		m_castMore = static_cast<int>(actors.size()) - keep;
		actors.resize(keep);
	}
	m_castActors = actors;
	const int n = static_cast<int>(m_castActors.size());
	const int w = pad * 2 + n * m_stripCardW + (n - 1) * gap + (m_castMore > 0 ? gap + moreW : 0);
	const int h = pad * 2 + CastCardH();
	// 카드 아래 (가로 가운데는 배우 수 자리), 아래 공간이 없으면 위
	m_castPopup.Invalidate(FALSE);
	m_castPopup.ShowNear(anchorBox, CSize(w, h), true);
}

void CRuliManagerDlg::UpdateWallOverlay()
{
	// 글자 창을 격자 안쪽(스크롤바 제외) 화면 자리에 맞춰 보이고 다시 그림 (주 창이 최소화 · 숨김이면 숨김)
	if (!VideoWallActive() || !m_grid.GetSafeHwnd() || !m_grid.IsWindowVisible() || IsIconic() || !IsWindowVisible())
	{
		m_wallOverlay.HideOverlay();
		return;
	}
	CRect rc;
	m_grid.GetClientRect(&rc);
	m_grid.ClientToScreen(&rc);
	m_wallOverlay.ShowOver(this, rc);   // 자리 · 크기가 바뀌었을 때만 다시 그림
	// 내용(보이는 칸 · 자리 · 글자)이 바뀌었을 때만 다시 그림 (예전: 0.5초마다 항상)
	size_t sig = 1469598103934665603ULL;
	auto mix = [&sig](size_t v) { sig = (sig ^ v) * 1099511628211ULL; };
	CRect client;
	m_grid.GetClientRect(&client);
	mix(static_cast<size_t>(client.Width()));
	mix(static_cast<size_t>(client.Height()));
	const int count = GridGetCount();
	for (int g = 0; g < count; ++g)
	{
		CRect card, vis;
		if (!m_grid.GetCardRect(g, card) || !vis.IntersectRect(card, client))
			continue;
		const int row = GridRowOf(g);
		if (row < 0 || row >= static_cast<int>(m_view.size()))
			continue;
		const VideoItem& v = m_lib.items[m_view[row]];
		mix(static_cast<size_t>(m_view[row]));
		mix(static_cast<size_t>(card.left) * 131 + static_cast<size_t>(card.top));
		for (const CString* s : { &v.title, &v.actors, &v.release })
			for (int i = 0; i < s->GetLength(); ++i)
				mix(static_cast<size_t>(s->GetAt(i)));
	}
	if (sig != m_wallOverlaySig)
	{
		m_wallOverlaySig = sig;
		if (m_wallOverlay.GetSafeHwnd())
			m_wallOverlay.Invalidate(FALSE);
	}
}

void CRuliManagerDlg::DrawWallOverlay(CDC* dc, const CRect& /*rc*/)
{
	// 화면에 보이는 칸마다 왼쪽 아래에 제목 · 출연 배우(한글 이름만) · 발매일 (흰 글자 + 검은 그림자, 칸 폭에서 "…") - 글자 창 좌표 = 격자 클라이언트 좌표
	if (!VideoWallActive() || !m_grid.GetSafeHwnd())
		return;
	CRect client;
	m_grid.GetClientRect(&client);
	dc->SetBkMode(TRANSPARENT);
	const int count = GridGetCount();
	auto line = [&](const CString& text, CRect r)
	{
		if (text.IsEmpty())
			return;
		dc->SetTextColor(RGB(0, 0, 0));          // 그림자 (오른쪽 아래 1px)
		CRect sr = r;
		sr.OffsetRect(1, 1);
		TextFB::Draw(dc, text, sr, DT_LEFT, true);
		dc->SetTextColor(RGB(255, 255, 255));    // 글자
		TextFB::Draw(dc, text, r, DT_LEFT, true);
	};
	for (int g = 0; g < count; ++g)
	{
		CRect card;
		if (!m_grid.GetCardRect(g, card))
			continue;
		CRect vis;
		if (!vis.IntersectRect(card, client))
			continue;
		const int row = GridRowOf(g);
		if (row < 0 || row >= static_cast<int>(m_view.size()))
			continue;
		const VideoItem& v = m_lib.items[m_view[row]];
		CString title = v.title;
		title.Trim();
		if (title.IsEmpty())
		{
			title = v.FileName();
			::PathRemoveExtensionW(title.GetBuffer());
			title.ReleaseBuffer();
		}
		// 배우: 한글 이름만 (괄호 앞 이름 - "이름 (日本語名)" → "이름")
		CString actors;
		for (const CString& a : CVideoLibrary::SplitList(v.actors))
		{
			CString n = a;
			int po = n.Find(L'(');
			const int pw = n.Find(L'\xFF08');   // 전각 괄호
			if (pw >= 0 && (po < 0 || pw < po))
				po = pw;
			if (po > 0)
				n = n.Left(po);
			n.Trim();
			if (n.IsEmpty())
				continue;
			actors += (actors.IsEmpty() ? L"" : L", ") + n;
		}
		const int saved = dc->SaveDC();
		dc->IntersectClipRect(card);   // 칸 밖으로 넘치지 않게
		DrawWallPartZones(dc, g, card);   // 분할 영상을 합친 칸: 마우스가 올라가 있으면 파트 칸
		// 왼쪽 아래에 위→아래 순서(제목 / 배우 / 발매일)로 쌓음 - 칸 높이가 모자라면 위쪽 줄(제목)부터 뺌
		const int pad = DX(4);
		struct L { CString text; CFont* font; int h; };
		std::vector<L> lines;
		const L src[3] = { { title, m_videoTitleFont.GetSafeHandle() ? &m_videoTitleFont : GetFont(), 0 },
		                   { actors, m_vcardSubFont.GetSafeHandle() ? &m_vcardSubFont : GetFont(), 0 },
		                   { v.release, m_vcardSubFont.GetSafeHandle() ? &m_vcardSubFont : GetFont(), 0 } };
		for (const L& l : src)
		{
			if (l.text.IsEmpty())
				continue;
			dc->SelectObject(l.font);
			TEXTMETRIC tm = {};
			dc->GetTextMetrics(&tm);
			lines.push_back({ l.text, l.font, static_cast<int>(tm.tmHeight) + 1 });
		}
		int total = 0;
		for (const L& l : lines)
			total += l.h;
		while (!lines.empty() && total > card.Height() - pad)
		{
			total -= lines.front().h;
			lines.erase(lines.begin());
		}
		int y = card.bottom - pad / 2 - total;
		for (const L& l : lines)
		{
			dc->SelectObject(l.font);
			line(l.text, CRect(card.left + pad, y, card.right - pad, y + l.h));
			y += l.h;
		}
		dc->RestoreDC(saved);
	}
}

void CRuliManagerDlg::DrawWallPartZones(CDC* dc, int g, const CRect& card, int hotPartIn)
{
	// 분할 영상을 합친 칸: 마우스가 올라가 있으면 [격자] 처럼 파트 칸 (구분선 + 번호, 올라간 칸 흰 테두리, 고른 파트 아래 파란 줄)
	//  글자 창은 색 키 투명이라 반투명 칠은 못 함 → 선 · 테두리 · 번호(그림자)로
	//  card: dc 좌표의 칸 자리 ([월 모드] 글자 창 = 격자 클라이언트 좌표 / [격자] 미리보기 위 글자 창 = 그 창 좌표)
	{
		auto pit = m_gridParts.find(g);
		const bool show = (hotPartIn != -2) || (m_grid.HotRow() == g && m_grid.HotZone() == 1);   // [패키지] 팝업 카드: 주인이 정한 파트
		if (pit != m_gridParts.end() && show)
		{
			const int n = static_cast<int>(pit->second.size());
			const int hotPart = (hotPartIn != -2) ? hotPartIn : m_grid.HotPart() - 1;
			const int selRow = GetSelectedRow();
			EnsureGdiPlus();
			Gdiplus::Graphics gr(dc->GetSafeHdc());
			gr.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
			Gdiplus::FontFamily family(L"Segoe UI");
			const float fs = (std::max)(9.0f, (std::min)(card.Height() * 0.45f, card.Width() / static_cast<float>(n) * 0.6f));
			Gdiplus::Font font(&family, fs, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
			Gdiplus::StringFormat fmt;
			fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
			fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
			const int edge = (std::max)(2, DX(2));
			for (int pi = 0; pi < n; ++pi)
			{
				const int x0 = card.left + card.Width() * pi / n, x1 = card.left + card.Width() * (pi + 1) / n;
				if (pi > 0)
					dc->FillSolidRect(x0, card.top + 2, 1, card.Height() - 4, RGB(235, 235, 235));   // 구분선
				if (pi == hotPart)
				{
					// 올라간 칸: 흰 테두리
					CRect hr(x0 + 1, card.top + 1, x1 - 1, card.bottom - 1);
					CBrush wb(RGB(255, 255, 255));
					for (int k = 0; k < edge; ++k)
					{
						dc->FrameRect(hr, &wb);
						hr.DeflateRect(1, 1);
					}
				}
				wchar_t num[8];
				swprintf_s(num, L"%d", pi + 1);
				const Gdiplus::RectF cell(static_cast<float>(x0), static_cast<float>(card.top), static_cast<float>(x1 - x0), static_cast<float>(card.Height()));
				Gdiplus::RectF shadow = cell;
				shadow.Offset(2.0f, 2.0f);
				Gdiplus::SolidBrush black(Gdiplus::Color(255, 0, 0, 0));
				Gdiplus::SolidBrush white(Gdiplus::Color(255, pi == hotPart ? 255 : 215, pi == hotPart ? 255 : 215, pi == hotPart ? 255 : 215));
				gr.DrawString(num, -1, &font, shadow, &fmt, &black);
				gr.DrawString(num, -1, &font, cell, &fmt, &white);
				if (pi < static_cast<int>(pit->second.size()) && pit->second[pi] == selRow)
				{
					const int bh = (std::max)(2, DX(3));
					dc->FillSolidRect(x0 + 2, card.bottom - bh - 1, x1 - x0 - 4, bh, kButtonColor);   // 고른 파트
				}
			}
		}
	}
}

void CRuliManagerDlg::StopWallPlayers()
{
	m_wallPlayers.clear();   // 각 재생 창은 소멸자에서 멈추고 닫힘
	m_wallSelItem = -1;
}

void CRuliManagerDlg::SyncWallPlayers()
{
	// [월 모드]: 화면에 보이는 칸마다 미리보기가 있으면 격자의 자식 창으로 소리 없이 반복 재생 (개수 제한 없음)
	//  - 분할 영상을 합친 칸은 미리보기가 있는 첫 파트, 연결 안 된 영상 · 만드는 중인 영상 · SFW 옵션이면 재생 안 함
	//  - 자식 창이라 스크롤하면 같이 움직이고(SW_SCROLLCHILDREN), 마우스는 격자가 받음 (HTTRANSPARENT)
	//  - 고른 칸은 재생 창을 안쪽으로 줄여 선택 테두리가 보이게
	const bool on = VideoWallActive() && ShowVideoDetail() && !YearViewActive() && !VideoListActive()
		&& m_grid.GetSafeHwnd() && m_grid.IsWindowVisible() && !IsSfwMosaic();
	if (!on && !(VideoWallActive() && ShowVideoDetail() && !YearViewActive() && !VideoListActive() && m_grid.GetSafeHwnd() && m_grid.IsWindowVisible()))
	{
		// 월 모드가 아님: 재생 창 · 글자 창 정리
		if (!m_wallPlayers.empty())
			StopWallPlayers();
		m_wallOverlay.HideOverlay();
		return;
	}
	UpdateWallOverlay();   // 글자는 SFW 옵션이어도 표시
	if (!on)
	{
		if (!m_wallPlayers.empty())
			StopWallPlayers();   // SFW: 재생만 안 함
		return;
	}
	const int selG = GridGetSel();
	m_wallSelItem = selG;
	CRect client;
	m_grid.GetClientRect(&client);
	std::set<int> keep;
	const int count = GridGetCount();
	for (int g = 0; g < count; ++g)
	{
		CRect card;
		if (!m_grid.GetCardRect(g, card))
			continue;
		CRect vis;
		if (!vis.IntersectRect(card, client))
			continue;   // 화면 밖 칸은 재생하지 않음
		const int row = GridRowOf(g);
		if (row < 0 || row >= static_cast<int>(m_view.size()))
			continue;
		std::vector<int> rows{ row };
		auto pit = m_gridParts.find(g);
		if (pit != m_gridParts.end())
			rows = pit->second;
		int item = -1;
		CString file;
		for (int r : rows)
		{
			if (r < 0 || r >= static_cast<int>(m_view.size()))
				continue;
			const int idx = m_view[r];
			if (IsItemOffline(idx))
				continue;
			if (m_prevBusy && m_lib.items[idx].path.CompareNoCase(m_prevBusyPath) == 0)
				continue;
			const CString f = PreviewFileOf(idx);
			if (::PathFileExistsW(f))
			{
				item = idx;
				file = f;
				break;
			}
		}
		if (item < 0)
			continue;
		CRect pr = card;
		if (g == selG)
			pr.DeflateRect(3, 3);   // 선택 테두리 자리
		keep.insert(item);
		auto it = m_wallPlayers.find(item);
		if (it == m_wallPlayers.end())
		{
			if (!m_wallPlaying)
				continue;   // 멈춘 동안(뒤에 있음 · 스크롤 중)은 새로 열지 않음 - 다시 재생할 때 엶
			auto player = std::make_unique<CPreviewPlayer>();
			if (player->StartAt(&m_grid, pr, file, RGB(0, 0, 0)))
				m_wallPlayers[item] = std::move(player);
		}
		else
			it->second->MoveTo(pr);
	}
	for (auto it = m_wallPlayers.begin(); it != m_wallPlayers.end(); )
	{
		if (keep.count(it->first))
			++it;
		else
			it = m_wallPlayers.erase(it);   // 화면 밖으로 나갔거나 목록에서 빠짐
	}
}

bool CRuliManagerDlg::WallShouldPlay()
{
	if (IsIconic() || ::GetForegroundWindow() != GetSafeHwnd())
		return false;   // 최소화 · 다른 프로그램(또는 설정 같은 대화상자)을 보는 중
	return ::GetTickCount64() >= m_wallScrollTick + 300;   // 스크롤 중이 아님
}

void CRuliManagerDlg::ApplyWallPlayState(bool play)
{
	m_wallPlaying = play;
	for (auto& kv : m_wallPlayers)
	{
		if (play)
			kv.second->Resume();
		else
			kv.second->Pause();
	}
}

void CRuliManagerDlg::GridScrolled()
{
	if (VideoWallActive())
	{
		// [월 모드]: 스크롤하는 동안은 모두 일시정지 (0.3초 멈추면 타이머가 다시 재생), 벗어난 칸 정리 · 글자 다시
		m_wallScrollTick = ::GetTickCount64();
		if (m_wallPlaying)
			ApplyWallPlayState(false);
		SyncWallPlayers();
	}
	FollowPreviewPopup();   // 스크롤하자마자 팝업을 카드 자리로 (마우스가 벗어났는지는 CheckPreviewHover 가 살핌)
}

bool CRuliManagerDlg::CardAnchorOfItem(int item, CRect& anchorScreen)
{
	if (item < 0 || !m_grid.GetSafeHwnd())
		return false;
	const int count = GridGetCount();
	for (int g = 0; g < count; ++g)
	{
		const int row = GridRowOf(g);
		if (row < 0 || row >= static_cast<int>(m_view.size()))
			continue;
		bool match = (m_view[row] == item);
		if (!match)
		{
			auto it = m_gridParts.find(g);   // 분할 영상을 합친 카드: 파트 중 하나
			if (it != m_gridParts.end())
				for (int r : it->second)
					if (r >= 0 && r < static_cast<int>(m_view.size()) && m_view[r] == item)
						match = true;
		}
		if (!match)
			continue;
		CRect card;
		if (!m_grid.GetCardRect(g, card))
			return false;
		anchorScreen = CRect(card.left, card.top, card.right, VideoImageCardActive() ? card.bottom : card.top + m_vcardImgH);   // 이미지 영역 (PreviewItemAt 과 같음)
		m_grid.ClientToScreen(&anchorScreen);
		return true;
	}
	return false;
}

void CRuliManagerDlg::FollowPreviewPopup()
{
	if (!m_pPrevPlayer->IsPlaying())
		return;
	CRect a;
	CRect gr;
	if (m_grid.GetSafeHwnd())
		m_grid.GetWindowRect(&gr);
	if (!CardAnchorOfItem(m_hoverItem, a) || !gr.PtInRect(a.CenterPoint()))
	{
		// 카드가 이 페이지에서 사라졌거나 격자 밖으로 (절반 넘게) 나감: 닫음 (마우스 아래 카드에서 다시)
		StopPreviewPlayer();
		m_hoverItem = -1;
		m_hoverTick = ::GetTickCount64();
		return;
	}
	if (a != m_hoverAnchor)
	{
		m_hoverAnchor = a;
		CRect rcIn = a;
		m_grid.ScreenToClient(&rcIn);
		m_pPrevPlayer->MoveTo(rcIn);   // 격자 자식 창: 카드 자리로
		if (m_pNextPlayer->IsPlaying())
			m_pNextPlayer->MoveTo(rcIn);   // 여는 중인 다음 파트도
	}
}

void CRuliManagerDlg::StartNextPart(int item)
{
	// 분할 영상 파트 바꾸기: 지금 재생 창은 그대로 두고, 같은 자리 바로 뒤에 다음 파트 재생 창을 숨긴 채 엶 (준비되면 SwapNextPart 가 바꿔 끼움)
	if (item == m_nextHoverItem && m_pNextPlayer->IsPlaying())
		return;   // 이미 여는 중
	if (m_pNextPlayer->IsPlaying())
		m_pNextPlayer->Stop();
	m_nextHoverItem = -1;
	const CString file = (item >= 0 && !IsSfwMosaic() && !IsItemOffline(item)
		&& !(m_prevBusy && m_lib.items[item].path.CompareNoCase(m_prevBusyPath) == 0)) ? PreviewFileOf(item) : CString();
	CRect rcIn;
	if (!file.IsEmpty() && ::PathFileExistsW(file) && m_pPrevPlayer->IsPlaying())
	{
		CWnd* parent = m_pPrevPlayer->GetParent();   // 격자 ([격자]) 또는 팝업 카드 ([패키지])
		m_pPrevPlayer->GetWindowRect(&rcIn);
		parent->ScreenToClient(&rcIn);
		if (m_pNextPlayer->StartAt(parent, rcIn, file, RGB(0, 0, 0)))
		{
			m_pNextPlayer->SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);   // 지금 창 뒤 (보여도 가려짐)
			m_nextHoverItem = item;
			return;
		}
	}
	// 그 파트는 미리보기가 없음 (또는 열지 못함): 예전처럼 닫고 그 파트로 (커버 이미지가 보임)
	StopPreviewPlayer();
	m_hoverItem = item;
	m_hoverTick = ::GetTickCount64();
}

void CRuliManagerDlg::SwapNextPart()
{
	if (m_nextHoverItem < 0 || !m_pNextPlayer->IsPlaying())
		return;
	if (!m_pNextPlayer->IsWindowVisible())
		return;   // 아직 여는 중 (영상이 준비되면 보임 - 지금 창 뒤라 가려져 있음)
	// 준비됨: 지금 창을 닫으면 바로 뒤의 다음 파트가 이어서 보임
	m_pPrevPlayer->Stop();
	std::swap(m_pPrevPlayer, m_pNextPlayer);
	m_hoverItem = m_nextHoverItem;
	m_nextHoverItem = -1;
	m_pPrevPlayer->SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

int CRuliManagerDlg::PreviewItemAt(CPoint screen, CRect& anchorScreen) const
{
	// 마우스 아래 영상 카드 → 항목 (분할 영상을 합친 카드는 이미지 위 파트 칸의 파트)
	//  anchorScreen = 팝업을 겹칠 곳: 카드 이미지 영역 (팝업은 이 크기 그대로)
	CRect gr;
	m_grid.GetWindowRect(&gr);
	if (!gr.PtInRect(screen))
		return -1;
	CPoint pt = screen;
	m_grid.ScreenToClient(&pt);
	const int g = m_grid.HitTest(pt);
	const int row = (g >= 0) ? GridRowOf(g) : -1;
	CRect card;
	if (row < 0 || row >= static_cast<int>(m_view.size()) || !m_grid.GetCardRect(g, card))
		return -1;
	int viewRow = row;
	// 팝업 기준 = 카드의 이미지 영역 (DrawVideoCard · PartZoneAt 과 같음 - [패키지] 는 카드 전체가 이미지)
	CRect anchor(card.left, card.top, card.right, VideoImageCardActive() ? card.bottom : card.top + m_vcardImgH);
	if (!anchor.PtInRect(pt))
		return -1;   // 이미지 영역 밖(제목 · 정보 줄): 미리보기 대상 아님 (팝업은 이미지 영역에만 뜨므로 바로 닫히며 깜빡이던 것)
	const int z = PartZoneAt(g, card, pt);
	auto it = m_gridParts.find(g);
	if (z >= 0 && it != m_gridParts.end() && z < static_cast<int>(it->second.size()))
		viewRow = it->second[z];   // 분할 영상: 그 파트 칸의 파트 (팝업은 이미지 영역 전체에)
	if (viewRow < 0 || viewRow >= static_cast<int>(m_view.size()))
		return -1;
	anchorScreen = anchor;
	m_grid.ClientToScreen(&anchorScreen);
	return m_view[viewRow];
}

void CRuliManagerDlg::CheckPreviewHover()
{
	// 영상 격자(격자 · 패키지) 카드 이미지에 마우스를 올리면 바로 카드 위에 미리보기 팝업 (미리보기 영상이 있는 카드만)
	//  - 팝업이 보이는 동안: 마우스가 팝업 영역 안이면 유지, 벗어나면 닫음 (같은 카드에서는 다시 띄우지 않음 - 다른 카드로 가야 다시)
	//  - 팝업이 아직 안 보일 때(영상 여는 중): 다른 카드로 가면 취소
	//  - 프로그램이 앞에 없거나 메뉴가 열려 있으면 닫음
	//  - [패키지]: 팝업된 [격자] 카드에서 (CheckPkgPreview)
	if (VideoPortraitActive() || m_pkgPreviewOn)
		return;
	int item = -1;
	CRect cardScreen;
	{
		CPoint cur;
		::GetCursorPos(&cur);
		GUITHREADINFO gti = { sizeof(gti) };
		const bool inMenu = ::GetGUIThreadInfo(::GetCurrentThreadId(), &gti) && (gti.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE));
		const bool gridOk = ShowVideoDetail() && !VideoListActive() && !YearViewActive() && !VideoWallActive()   // [월 모드]는 칸마다 자동 재생
			&& !VideoPortraitActive()   // [패키지]: 미리보기 대신 [격자] 영상 카드 팝업 (CheckPackageHover)
			&& m_grid.GetSafeHwnd() && m_grid.IsWindowVisible() && ::GetForegroundWindow() == GetSafeHwnd() && !inMenu;
		if (m_pPrevPlayer->IsPlaying() && m_pPrevPlayer->IsWindowVisible())
		{
			// 카드가 움직였으면(휠 스크롤 등) 팝업도 따라감 - 카드가 사라졌거나 격자 밖으로 나가면 닫힘
			FollowPreviewPopup();
			if (m_pPrevPlayer->IsPlaying())
			{
				CRect popup;
				m_pPrevPlayer->GetWindowRect(&popup);
				if (gridOk && popup.PtInRect(cur))
				{
					// 분할 영상을 합친 카드: 팝업 안에서 다른 파트 칸으로 옮기면 그 파트의 미리보기로 바꿈 (다음 확인 때 바로 엶)
					//  다음 파트는 뒤에서 열어 두고 준비되면 바꿔 끼움 (꺼졌다 켜지지 않게)
					CRect a;
					const int part = PreviewItemAt(cur, a);
					if (part >= 0 && a == m_hoverAnchor)
					{
						if (part != m_hoverItem)
							StartNextPart(part);
						else if (m_nextHoverItem >= 0)
						{
							m_pNextPlayer->Stop();   // 보던 파트로 돌아옴: 여는 중이던 다음 파트 취소
							m_nextHoverItem = -1;
						}
					}
					SwapNextPart();
					return;   // 팝업 안: 그대로 재생
				}
			}
			// 팝업 밖: 닫음 - 보던 카드 위면 다시 띄우지 않고(다른 카드로 가야 다시), 다른 카드 위면 그 카드로 다시 띄움
			StopPreviewPlayer();
			const int playing = m_hoverItem;   // 팝업은 m_hoverItem 카드로 열었음
			CRect dummy;
			m_hoverItem = gridOk ? PreviewItemAt(cur, dummy) : -1;
			m_hoverTick = ::GetTickCount64();
			if (m_hoverItem >= 0 && m_hoverItem == playing)
				m_hoverTick += 0x7FFFFFFFFFFFULL;   // 같은 카드에 있는 동안은 다시 열지 않음
			return;
		}
		if (gridOk)
			item = PreviewItemAt(cur, cardScreen);
	}
	if (item != m_hoverItem)
	{
		// 다른 카드 (또는 카드 밖): 팝업 닫고 다음 확인 때 그 카드로
		m_hoverItem = item;
		m_hoverTick = ::GetTickCount64();
		StopPreviewPlayer();
		return;
	}
	if (item < 0 || m_pPrevPlayer->IsPlaying() || ::GetTickCount64() < m_hoverTick)   // 기다리지 않고 바로 (닫은 카드 다시 열지 않기만 m_hoverTick 으로)
		return;
	if (IsSfwMosaic() || IsItemOffline(item))
		return;   // SFW(모자이크) 옵션 · 연결 안 된 영상은 재생하지 않음
	if (m_prevBusy && m_lib.items[item].path.CompareNoCase(m_prevBusyPath) == 0)
		return;   // 만드는 중
	const CString file = PreviewFileOf(item);
	if (!::PathFileExistsW(file))
		return;
	// 카드 이미지 영역에 바로 그림 ([월 모드] 처럼 격자의 자식 창 - 별도 팝업 창 없음, 격자 밖은 잘림)
	//  같은 크기 · 같은 자리 (확대 배율을 따라감, 영상은 비율 유지 - 남는 곳은 검정)
	m_hoverAnchor = cardScreen;   // 이 카드가 움직이면(스크롤 등) 따라감
	CRect rcIn = cardScreen;
	m_grid.ScreenToClient(&rcIn);
	if (!m_pPrevPlayer->StartAt(&m_grid, rcIn, file, RGB(0, 0, 0)))
		m_hoverTick = ::GetTickCount64() + 60000;   // 열지 못하면 이 카드에서는 한동안 다시 시도하지 않음
	else
		m_pPrevPlayer->SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);   // 도구줄(격자 자식) 아래로 - 스크롤로 겹쳐도 도구줄이 위
}

void CRuliManagerDlg::OnVideoMakePreview()
{
	const int idx = GetSelectedItem();
	if (idx < 0)
		return;
	QueuePreviews({ idx });
}

void CRuliManagerDlg::OnVideoMakePreviewChecked()
{
	std::vector<int> items;
	for (size_t i = 0; i < m_lib.items.size(); ++i)
		if (IsItemChecked(static_cast<int>(i)))
			items.push_back(static_cast<int>(i));
	QueuePreviews(items);
}

void CRuliManagerDlg::QueuePreviews(const std::vector<int>& items)
{
	if (items.empty())
		return;
	m_ffmpegPath = PreviewVideo::FindFfmpeg();
	if (m_ffmpegPath.IsEmpty())
	{
		AfxMessageBox(L"ffmpeg.exe 를 찾을 수 없습니다.\n\n"
			L"ffmpeg.exe 를 프로그램(RuliManager.exe) 폴더에 넣은 뒤 다시 시도하세요.\n"
			L"(프로그램 폴더\\ffmpeg\\bin\\ffmpeg.exe 또는 PATH 에 있어도 됩니다)", MB_ICONINFORMATION);
		return;
	}
	int added = 0, skipped = 0;
	for (int idx : items)
	{
		if (idx < 0 || idx >= static_cast<int>(m_lib.items.size()) || IsItemOffline(idx))
		{
			++skipped;
			continue;
		}
		const CString path = m_lib.items[idx].path;
		bool dup = (m_prevBusy && path.CompareNoCase(m_prevBusyPath) == 0);
		for (const PreviewJob& j : m_prevQueue)
			if (j.path.CompareNoCase(path) == 0)
				dup = true;
		if (dup || !::PathFileExistsW(path))
		{
			++skipped;
			continue;
		}
		m_prevQueue.push_back({ path, PreviewFileOf(idx) });
		++m_prevTotal;
		m_prevStopped = false;   // 멈추는 중에 새로 넣으면 이어서 만듦
		++added;
	}
	if (added == 0)
	{
		if (skipped > 0)
			AfxMessageBox(L"미리보기를 만들 영상이 없습니다.\n(연결 안 된 영상 · 파일이 없는 영상 · 이미 만드는 중인 영상은 건너뜀)", MB_ICONINFORMATION);
		return;
	}
	if (!m_prevBusy)
		StartNextPreview();
	else
		UpdatePreviewStatus();
}

void CRuliManagerDlg::MakeAllPreviews(CWnd* parent, bool regen)
{
	// 미리보기가 없는 영상만 (연결 안 된 영상 · 파일 없는 영상 · 이미 대기 중이거나 만드는 중인 영상은 빼고)
	//  regen = [미리보기 재생성]: 이미 있는 영상도 다시 만듦 (덮어씀)
	const LPCWSTR caption = regen ? L"미리보기 재생성" : L"미리보기 일괄 만들기";
	if (PreviewVideo::FindFfmpeg().IsEmpty())
	{
		::MessageBoxW(parent ? parent->GetSafeHwnd() : GetSafeHwnd(),
			L"ffmpeg.exe 를 찾을 수 없습니다.\n\nffmpeg.exe 를 프로그램(RuliManager.exe) 폴더에 넣은 뒤 다시 시도하세요.\n"
			L"(프로그램 폴더\\ffmpeg\\bin\\ffmpeg.exe 또는 PATH 에 있어도 됩니다)", caption, MB_ICONINFORMATION);
		return;
	}
	CWaitCursor wait;
	std::vector<int> items;
	int has = 0;
	for (size_t i = 0; i < m_lib.items.size(); ++i)
	{
		const int idx = static_cast<int>(i);
		if (IsItemOffline(idx))
			continue;
		const CString& path = m_lib.items[idx].path;
		if (m_prevBusy && path.CompareNoCase(m_prevBusyPath) == 0)
			continue;
		bool queued = false;
		for (const PreviewJob& j : m_prevQueue)
			if (j.path.CompareNoCase(path) == 0) { queued = true; break; }
		if (queued)
			continue;
		if (::PathFileExistsW(PreviewFileOf(idx)))
		{
			++has;
			if (!regen)
				continue;   // 재생성이면 이미 있는 것도 다시
		}
		if (::PathFileExistsW(path))
			items.push_back(idx);
	}
	const HWND owner = parent ? parent->GetSafeHwnd() : GetSafeHwnd();
	if (items.empty())
	{
		CString t;
		if (regen)
			t = L"미리보기를 만들 영상이 없습니다.";
		else
			t.Format(L"미리보기를 새로 만들 영상이 없습니다.\n(이미 미리보기가 있는 영상 %d편)", has);
		::MessageBoxW(owner, t, caption, MB_ICONINFORMATION);
		return;
	}
	CString t;
	if (regen)
		t.Format(L"영상 %d편의 미리보기를 모두 다시 만듭니다.\n(이미 있는 미리보기 %d개는 새로 만든 것으로 바뀜)\n\n"
			L"백그라운드에서 한 편씩 만들고 진행은 아래 상태 줄에 표시됩니다.\n영상이 많으면 오래 걸릴 수 있습니다. 계속할까요?",
			static_cast<int>(items.size()), has);
	else
		t.Format(L"미리보기가 없는 영상 %d편의 미리보기를 만듭니다.\n(이미 있는 영상 %d편은 건너뜀)\n\n"
			L"백그라운드에서 한 편씩 만들고 진행은 아래 상태 줄에 표시됩니다.\n영상이 많으면 오래 걸릴 수 있습니다. 계속할까요?",
			static_cast<int>(items.size()), has);
	if (::MessageBoxW(owner, t, caption, MB_ICONQUESTION | MB_YESNO) != IDYES)
		return;
	QueuePreviews(items);
}

void CRuliManagerDlg::DeleteAllPreviews(CWnd* parent)
{
	const HWND owner = parent ? parent->GetSafeHwnd() : GetSafeHwnd();
	const CString dir = PreviewVideo::GetPreviewDir();
	// 미리보기 파일 (*.mp4) + 만들다 남은 임시 파일 (*.part)
	std::vector<CString> files;
	ULONGLONG bytes = 0;
	for (LPCWSTR pat : { L"\\*.mp4", L"\\*.part" })
	{
		WIN32_FIND_DATAW fd = {};
		HANDLE h = ::FindFirstFileW(dir + pat, &fd);
		if (h == INVALID_HANDLE_VALUE)
			continue;
		do
		{
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;
			files.push_back(dir + L"\\" + fd.cFileName);
			bytes += (static_cast<ULONGLONG>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
		} while (::FindNextFileW(h, &fd));
		::FindClose(h);
	}
	if (files.empty() && !m_prevBusy && m_prevQueue.empty())
	{
		::MessageBoxW(owner, L"지울 미리보기 파일이 없습니다.", L"미리보기 모두 삭제", MB_ICONINFORMATION);
		return;
	}
	CString t;
	t.Format(L"미리보기 파일 %d개 (%.1f MB) 를 모두 삭제합니다.\n영상 파일은 지우지 않습니다.", static_cast<int>(files.size()), bytes / (1024.0 * 1024.0));
	if (m_prevBusy || !m_prevQueue.empty())
		t += L"\n\n미리보기를 만드는 중입니다. 만들기를 멈추고 남은 대기열도 비웁니다.";
	t += L"\n\n계속할까요?";
	if (::MessageBoxW(owner, t, L"미리보기 모두 삭제", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
		return;

	CWaitCursor wait;
	// 재생 · 만들기 멈춤 (파일 잠김 방지) - 만드는 중인 스레드는 ffmpeg 를 끝내고 돌아올 때까지 기다림
	StopPreviewPlayer();
	m_hoverItem = -1;
	m_prevQueue.clear();
	if (m_prevBusy)
	{
		m_prevCancel = true;
		if (m_prevThread.joinable())
			m_prevThread.join();
		// 남은 결과 메시지는 버리고 상태 정리
		MSG msg;
		while (::PeekMessageW(&msg, GetSafeHwnd(), WM_APP_PREVIEW_DONE, WM_APP_PREVIEW_DONE, PM_REMOVE))
			delete reinterpret_cast<PreviewVideo::Result*>(msg.lParam);
		m_prevBusy = false;
		m_prevBusyPath.Empty();
		m_prevDone = m_prevTotal = m_prevOk = 0;
		m_prevFirstError.Empty();
		m_prevStopped = false;
		SetGearSpin(false);
	}
	// 대기열이 비었어도 끝난 작업 스레드가 남아 있으면 정리
	if (m_prevThread.joinable())
		m_prevThread.join();

	int deleted = 0, failed = 0;
	for (const CString& f : files)
		(::DeleteFileW(f) ? deleted : failed)++;
	CString done;
	if (failed == 0)
		done.Format(L"미리보기 파일 %d개를 삭제했습니다.", deleted);
	else
		done.Format(L"미리보기 파일 %d개를 삭제했습니다. (%d개는 지우지 못함 - 사용 중일 수 있습니다)", deleted, failed);
	SetDlgItemText(IDC_STATIC_STATUS, done);
	::MessageBoxW(owner, done, L"미리보기 모두 삭제", failed ? MB_ICONWARNING : MB_ICONINFORMATION);
}

void CRuliManagerDlg::StopPreviews()
{
	// 미리보기 만들기 중단: 남은 대기열은 버리고, 지금 만드는 것은 ffmpeg 를 끝냄 (끝 알림 OnPreviewDone → StartNextPreview 에서 정리 · 톱니바퀴 멈춤)
	if (!m_prevBusy || m_prevStopped)
		return;
	m_prevQueue.clear();
	m_prevStopped = true;
	m_prevCancel = true;
	SetDlgItemText(IDC_STATIC_STATUS, L"미리보기 만들기를 멈추는 중...");
}

void CRuliManagerDlg::SetGearSpin(bool on)
{
	// 설정 버튼 톱니바퀴: 미리보기를 만드는 동안 돌아감 (0.05초마다 9도), 끝나면 제자리
	if (on == m_gearSpinning)
		return;
	m_gearSpinning = on;
	if (on)
		SetTimer(kGearSpinTimer, 50, nullptr);
	else
	{
		KillTimer(kGearSpinTimer);
		m_gearAngle = 0.0;
		if (m_btnSettings.GetSafeHwnd())
		{
			m_btnSettings.SetIconAngle(0.0);
			m_btnSettings.SetTooltip(L"설정");
		}
	}
}

void CRuliManagerDlg::UpdatePreviewStatus()
{
	if (!m_prevBusy)
		return;
	CString t;
	t.Format(L"미리보기 만드는 중 (%d / %d): %s", m_prevDone + 1, m_prevTotal, ::PathFindFileNameW(m_prevBusyPath));
	SetDlgItemText(IDC_STATIC_STATUS, t);
	SetGearSpin(true);
	if (m_btnSettings.GetSafeHwnd())
	{
		CString tip;
		tip.Format(L"설정 - 미리보기 만드는 중 (%d / %d)", m_prevDone + 1, m_prevTotal);
		m_btnSettings.SetTooltip(tip);
	}
}

void CRuliManagerDlg::StartNextPreview()
{
	if (m_prevThread.joinable())
		m_prevThread.join();
	if (m_prevQueue.empty())
	{
		// 모두 끝남: 결과 한 줄 (실패가 있으면 첫 실패 이유를 한 번 알림)
		const bool any = (m_prevTotal > 0);
		CString t;
		if (m_prevStopped)
		{
			t.Format(L"미리보기 만들기를 중단했습니다. (%d편 만듦)", m_prevOk);
			SetDlgItemText(IDC_STATIC_STATUS, t);
		}
		else if (any)
		{
			if (m_prevOk == m_prevTotal)
				t.Format(L"미리보기 영상 %d편을 만들었습니다.", m_prevOk);
			else
				t.Format(L"미리보기 영상: %d편 만듦, %d편 실패", m_prevOk, m_prevTotal - m_prevOk);
			SetDlgItemText(IDC_STATIC_STATUS, t);
		}
		const CString err = m_prevFirstError;
		m_prevStopped = false;
		SetGearSpin(false);   // 톱니바퀴 멈춤
		m_prevBusy = false;
		m_prevBusyPath.Empty();
		m_prevDone = m_prevTotal = m_prevOk = 0;
		m_prevFirstError.Empty();
		if (!err.IsEmpty())
			AfxMessageBox(err, MB_ICONWARNING);
		return;
	}
	const PreviewJob job = m_prevQueue.front();
	m_prevQueue.pop_front();
	m_prevBusy = true;
	m_prevCancel = false;
	m_prevBusyPath = job.path;
	if (m_pPrevPlayer->IsPlaying() && m_pPrevPlayer->File().CompareNoCase(job.out) == 0)
		StopPreviewPlayer();   // 다시 만드는 영상의 미리보기가 재생 중이면 멈춤 (파일 잠김)
	UpdatePreviewStatus();
	const HWND hwnd = GetSafeHwnd();
	const CString ffmpeg = m_ffmpegPath;
	std::atomic<bool>* cancel = &m_prevCancel;   // 창이 닫힐 때 OnDestroy 가 스레드를 기다리므로 유효
	m_prevThread = std::thread([hwnd, ffmpeg, job, cancel]()
	{
		const HRESULT co = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // 영상 길이 읽기 (속성 · Media Foundation)
		const CMediaInfoLabel::Info info = CMediaInfoLabel::ReadInfoCached(job.path);
		const double dur = info.duration100ns > 0 ? static_cast<double>(info.duration100ns) / 1e7 : 0.0;
		PreviewVideo::Result* r = new PreviewVideo::Result(PreviewVideo::Make(ffmpeg, job.path, job.out, dur, *cancel));
		if (SUCCEEDED(co))
			::CoUninitialize();
		if (!::PostMessageW(hwnd, WM_APP_PREVIEW_DONE, 0, reinterpret_cast<LPARAM>(r)))
			delete r;
	});
}

LRESULT CRuliManagerDlg::OnPreviewDone(WPARAM, LPARAM lParam)
{
	std::unique_ptr<PreviewVideo::Result> r(reinterpret_cast<PreviewVideo::Result*>(lParam));
	if (m_prevThread.joinable())
		m_prevThread.join();
	if (r)
	{
		++m_prevDone;
		if (r->ok)
			++m_prevOk;
		else if (!r->canceled && m_prevFirstError.IsEmpty())
			m_prevFirstError = CString(::PathFindFileNameW(r->video)) + L"\n\n" + r->error;
	}
	StartNextPreview();   // 대기열의 다음 영상 (없으면 끝)
	return 0;
}

void CRuliManagerDlg::OnBnClickedDrillViewGrid() { SetActorDrillView(0); }
void CRuliManagerDlg::OnBnClickedDrillViewYear() { SetActorDrillView(1); }

void CRuliManagerDlg::SetActorDrillView(int mode)
{
	// 배우 출연작 화면: [격자] ↔ [연도별] (레지스트리에 저장, 다음에 배우를 열 때도 같은 보기)
	mode = (mode == 1) ? 1 : 0;
	for (int i = 0; i < 2; ++i)
		if (m_drillViewBtns[i].GetSafeHwnd())
			m_drillViewBtns[i].SetChecked(i == mode);
	if (mode == m_actorDrillView)
		return;
	m_actorDrillView = mode;
	AfxGetApp()->WriteProfileInt(L"Settings", L"ActorDrillView", mode);
	m_yearViewActor = -1;   // 다시 만들 때 스크롤을 처음으로
	ApplyFilter();          // 연도별은 페이지를 나누지 않음 → 목록을 다시
	SetupActorCards();      // 격자로 돌아오면 [가로|세로] 값의 칸 크기
	UpdateLeftPane();       // 보이기 / 숨기기 + 다시 배치
	m_grid.Refresh();
	if (mode == 1)
		m_yearView.SetFocus();
}

void CRuliManagerDlg::UpdateDetailBar()
{
	if (!m_detailBar.GetSafeHwnd())
		return;
	// 화살표는 누르면 상세 화면이 움직일 방향: 오른쪽 상세 - 펼침 ▶ (접기) / 접힘 ◀ (펼치기), 왼쪽 상세는 반대
	const bool pointRight = m_detailLeft ? m_detailCollapsed : !m_detailCollapsed;
	m_detailBar.SetWindowText(pointRight ? L"\x25B6" : L"\x25C0");   // ▶ / ◀
	m_detailBar.SetTooltip(m_detailCollapsed ? L"상세 보기 펼치기" : L"상세 보기 숨기기");
	m_detailBar.Invalidate(FALSE);
}

void CRuliManagerDlg::OnBnClickedDetailBar()
{
	m_detailCollapsed = !m_detailCollapsed;
	AfxGetApp()->WriteProfileInt(L"Settings", L"DetailCollapsed", m_detailCollapsed ? 1 : 0);
	UpdateDetailBar();
	CRect client;
	GetClientRect(&client);
	LayoutControls(client.Width(), client.Height());
	Invalidate(FALSE);
	if (VideoWallActive())
		SyncWallPlayers();   // 격자 폭이 바뀌어 칸 배치가 바뀜
}

void CRuliManagerDlg::OnBnClickedCopyCode()
{
	// 품번 칸 글자를 클립보드로 (앞뒤 공백 제외)
	CString code;
	m_editCode.GetWindowText(code);
	code.Trim();
	if (code.IsEmpty())
	{
		::MessageBeep(MB_ICONWARNING);
		return;
	}
	bool ok = false;
	if (OpenClipboard())
	{
		::EmptyClipboard();
		const size_t bytes = (static_cast<size_t>(code.GetLength()) + 1) * sizeof(wchar_t);
		if (HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes))
		{
			if (void* p = ::GlobalLock(mem))
			{
				memcpy(p, static_cast<LPCWSTR>(code), bytes);
				::GlobalUnlock(mem);
				ok = ::SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
			}
			if (!ok)
				::GlobalFree(mem);   // 넘기지 못했으면 직접 해제 (넘겼으면 클립보드 소유)
		}
		::CloseClipboard();
	}
	if (ok)
		SetDlgItemText(IDC_STATIC_STATUS, L"품번 '" + code + L"' 을(를) 클립보드에 복사했습니다.");
	else
		::MessageBeep(MB_ICONWARNING);
}

void CRuliManagerDlg::OnBnClickedGenreHide() { SetGenreShow(false); }
void CRuliManagerDlg::OnBnClickedGenreAll()  { SetGenreShow(true); }

void CRuliManagerDlg::SetGenreShow(bool show)
{
	// [장르숨김] ↔ [전체]: 지금 화면(영상 탭 / 배우 출연작 / 제작사 · 레이블 영상) 값만 바꿔 레지스트리에 저장 - 숨기면 고른 장르도 풀림
	const int s = GenreSlot();
	if (s < 0)
		return;
	for (int i = 0; i < 2; ++i)
		if (m_genreBtns[i].GetSafeHwnd())
			m_genreBtns[i].SetChecked((i == 1) == show);
	if (show == m_genreShow[s])
		return;
	m_genreShow[s] = show;
	static const wchar_t* keys[3] = { L"GenreBar", L"GenreBarActor", L"GenreBarStudio" };
	AfxGetApp()->WriteProfileInt(L"Settings", keys[s], show ? 1 : 0);
	if (!show && !m_genreSel[s].IsEmpty())
	{
		m_genreSel[s].Empty();
		CurPageRef() = 0;
	}
	UpdateLeftPane();   // 칩 줄 보이기 / 숨기기 + 다시 배치
	ApplyFilter();
}

void CRuliManagerDlg::SyncGenreKey()
{
	// 배우 출연작 · 제작사 영상 화면: 다른 배우 · 제작사(레이블)로 들어가면 고른 장르를 풂 (영상 탭은 대상 없음)
	const int s = GenreSlot();
	if (s <= 0)
		return;
	CString key;
	key.Format(L"%d|%s", m_catKind, static_cast<LPCWSTR>(m_catValue));
	if (key != m_genreKey[s])
	{
		m_genreKey[s] = key;
		m_genreSel[s].Empty();
	}
}

void CRuliManagerDlg::UpdateGenreBar(const std::vector<int>& before)
{
	const int slot = GenreSlot();
	if (!m_genreBar.GetSafeHwnd() || slot < 0)
		return;
	const CString& sel = m_genreSel[slot];   // 지금 화면의 고른 태그
	// 태그별 건수 (대소문자 무시, 처음 나온 표기로)
	std::map<CString, CGenreChipBar::Item> counts;   // 소문자 → 표시 이름 · 건수
	for (int i : before)
	{
		std::set<CString> seen;   // 한 영상에 같은 태그가 두 번 있어도 한 번만
		for (const CString& t : CVideoLibrary::SplitList(m_lib.items[i].tags))
		{
			CString name = t;
			name.Trim();
			if (name.IsEmpty())
				continue;
			CString key = name;
			key.MakeLower();
			if (!seen.insert(key).second)
				continue;
			CGenreChipBar::Item& it = counts[key];
			if (it.name.IsEmpty())
				it.name = name;
			++it.count;
		}
	}
	std::vector<CGenreChipBar::Item> items;
	items.reserve(counts.size() + 1);
	for (auto& kv : counts)
		items.push_back(kv.second);
	// 고른 태그가 지금 결과에 없으면(검색 · 별점으로 빠짐) 0건으로 남겨 둠 → 보이고, 풀 수 있게
	if (!sel.IsEmpty())
	{
		CString key = sel;
		key.MakeLower();
		if (counts.find(key) == counts.end())
		{
			CGenreChipBar::Item it;
			it.name = sel;
			items.push_back(it);
		}
	}
	// 건수 많은 순, 같으면 이름 순
	std::sort(items.begin(), items.end(), [](const CGenreChipBar::Item& a, const CGenreChipBar::Item& b)
	{
		if (a.count != b.count)
			return a.count > b.count;
		return a.name.CompareNoCase(b.name) < 0;
	});
	m_genreBar.SetItems(items, static_cast<int>(before.size()), sel);
	// 머리 영역 다시 배치는 ApplyFilter 끝(페이지 버튼까지 정해진 뒤)에서
}

void CRuliManagerDlg::RelayoutToolbarHeader()
{
	// 목록 도구줄(+ 장르 칩 줄)만 다시 배치하고 격자 머리 높이를 맞춤 (칩 줄 높이 · 페이지 버튼이 바뀐 뒤)
	if (!m_layoutReady || !m_toolbarHost || !m_toolbarHost->GetSafeHwnd())
		return;
	CRect hc;
	m_toolbarHost->GetClientRect(&hc);
	const int gap = DX(4);
	const int tbH = LayoutListToolbar(0, gap, hc.Width());
	m_toolbarHost->SetHeaderHeight(tbH + gap);
	if (m_toolbarHost == &m_actorGrid)
		LayoutTreemap();
}

void CRuliManagerDlg::OnGenderButton(UINT nID)
{
	SetActorGender(static_cast<int>(nID) - IDC_ACTOR_GENDER_ALL);
}

void CRuliManagerDlg::SetActorGender(int g)
{
	// 배우 격자 성별 거르기 (레지스트리에 저장) - 첫 페이지부터
	g = (std::max)(0, (std::min)(2, g));
	for (int i = 0; i < 3; ++i)
		if (m_genderBtns[i].GetSafeHwnd())
			m_genderBtns[i].SetChecked(i == g);
	if (g == m_actorGender)
		return;
	m_actorGender = g;
	AfxGetApp()->WriteProfileInt(L"Settings", L"ActorGender", g);
	m_pageActor = 0;
	RebuildActorGrid();
	m_actorGrid.ScrollToTop();
	if (m_actorSel < 0)
		ShowActorInfo(-1);   // 고르던 배우가 걸러져 빠졌으면 오른쪽 정보도 비움
	m_actorGrid.SetFocus();
}

void CRuliManagerDlg::OnActorViewButton(UINT nID)
{
	SetActorView(static_cast<int>(nID) - IDC_ACTOR_VIEW_GRID);
}

void CRuliManagerDlg::SetActorView(int v)
{
	// 배우 탭 [격자] / [트리맵] (레지스트리에 저장) - 트리맵은 페이지 없이 거른 배우 전체
	v = (v == 1) ? 1 : 0;
	for (int i = 0; i < 2; ++i)
		if (m_actorViewBtns[i].GetSafeHwnd())
			m_actorViewBtns[i].SetChecked(i == v);
	if (v == m_actorView)
		return;
	m_actorView = v;
	AfxGetApp()->WriteProfileInt(L"Settings", L"ActorViewMode", v);
	UpdateLeftPane();   // 버튼 · 콤보 · 차트 보이기, 도구줄 다시 배치
	RebuildActorGrid();
	m_actorGrid.ScrollToTop();
	CRect client;
	GetClientRect(&client);
	LayoutControls(client.Width(), client.Height());
	if (v == 1 && m_treemap.GetSafeHwnd())
		m_treemap.SetFocus();
	else
		m_actorGrid.SetFocus();
}

void CRuliManagerDlg::OnCbnSelchangeTreemapBasis()
{
	const int sel = m_comboTmBasis.GetCurSel();
	const int b = (sel == 1) ? 1 : 0;
	if (b == m_treemapBasis)
		return;
	m_treemapBasis = b;
	AfxGetApp()->WriteProfileInt(L"Settings", L"TreemapBasis", b);
	RebuildTreemap();
}

void CRuliManagerDlg::LayoutTreemap()
{
	if (!m_treemap.GetSafeHwnd() || !m_actorGrid.GetSafeHwnd())
		return;
	if (!ActorTreemapActive())
	{
		if (m_treemap.IsWindowVisible())
			m_treemap.ShowWindow(SW_HIDE);
		return;
	}
	CRect hc;
	m_actorGrid.GetClientRect(&hc);
	const int top = (std::min)(static_cast<int>(hc.bottom), m_actorGrid.HeaderHeight());   // 카드가 없어 스크롤 없음
	const int pad = DX(4);
	const CRect want(hc.left + pad, top, hc.right - pad, (std::max)(top, static_cast<int>(hc.bottom) - pad));
	CRect cur;
	m_treemap.GetWindowRect(&cur);
	m_actorGrid.ScreenToClient(&cur);
	if (cur != want)
		m_treemap.MoveWindow(want, TRUE);
	if (!m_treemap.IsWindowVisible())
		m_treemap.ShowWindow(SW_SHOW);
}

void CRuliManagerDlg::RebuildTreemap()
{
	// 배우 [트리맵]: 거른 배우(성별 · 검색) 각각의 값 = 출연작 파일 크기 합 [용량] / 출연작 수 [영상수]
	//  퍼센트는 표시하는 배우 값의 합 기준 (여러 배우가 나오는 영상은 배우마다 셈) - 값이 작은 배우가 많으면 뒤쪽은 "기타" 로 묶음
	if (!m_treemap.GetSafeHwnd() || m_actorView != 1)
		return;
	std::vector<CTreemapView::Item> items;
	items.reserve(m_actorRows.size());
	double total = 0;
	for (size_t row = 0; row < m_actorRows.size(); ++row)
	{
		const ActorInfo& a = m_lib.actors[m_actorRows[row]];
		CString key = a.name;
		key.MakeLower();
		double v = 0;
		if (m_treemapBasis == 0)
		{
			auto it = m_actorBytes.find(key);
			v = (it != m_actorBytes.end()) ? static_cast<double>(it->second) : 0.0;
		}
		else
		{
			auto it = m_actorCounts.find(key);
			v = (it != m_actorCounts.end()) ? static_cast<double>(it->second) : 0.0;
		}
		if (v <= 0)
			continue;
		total += v;
		CTreemapView::Item item;
		// 칸 글자: 한글 이름 / 영어 이름 / 일본어 이름 ("한글(English, 日本語)" 에서 언어별로, 한글이 없으면 괄호 앞 이름이 첫 줄)
		CString ko, en, ja;
		CVideoLibrary::SplitNameByLang(a.name, ko, en, ja);
		if (ko.IsEmpty())
		{
			CString head = a.name;
			int po = head.Find(L'(');
			const int pw = head.Find(L'\xFF08');   // 전각 괄호
			if (pw >= 0 && (po < 0 || pw < po))
				po = pw;
			if (po > 0)
				head = head.Left(po);
			head.Trim();
			ko = head.IsEmpty() ? a.name : head;
			if (ko == en) en.Empty();   // 첫 줄과 같은 이름은 두 번 쓰지 않음
			if (ko == ja) ja.Empty();
		}
		item.label = ko;
		item.sub1 = en;
		item.sub2 = ja;
		item.value = v;
		item.tag = static_cast<int>(row);
		items.push_back(item);
	}
	// 큰 순서로 최대 kMax 명, 나머지는 "기타 (N명)" 한 칸 (너무 작은 칸이 수백 개 생기지 않게)
	const size_t kMax = 80;
	std::sort(items.begin(), items.end(), [](const CTreemapView::Item& x, const CTreemapView::Item& y) { return x.value > y.value; });
	if (items.size() > kMax)
	{
		double rest = 0;
		for (size_t i = kMax; i < items.size(); ++i)
			rest += items[i].value;
		CTreemapView::Item other;
		other.label.Format(L"기타 (%d명)", static_cast<int>(items.size() - kMax));
		other.value = rest;
		other.tag = -1;
		items.resize(kMax);
		items.push_back(other);
	}
	m_treemap.SetItems(std::move(items), total);
	m_treemap.SetSelectedTag(m_actorSel >= 0 ? m_actorSel : -2);
}

void CRuliManagerDlg::OnBnClickedVideoModeGrid()    { SetVideoMode(0); }
void CRuliManagerDlg::OnBnClickedVideoModeList()    { SetVideoMode(1); }
void CRuliManagerDlg::OnBnClickedVideoModePackage() { SetVideoMode(2); }
void CRuliManagerDlg::OnBnClickedVideoModeWall()    { SetVideoMode(3); }
void CRuliManagerDlg::OnBnClickedVideoModeLabel()   { SetVideoMode(4); }

void CRuliManagerDlg::SetVideoMode(int mode)
{
	// [격자] / [리스트] / [패키지]: 지금 영상 화면(영상 탭 / 배우 출연작 / 제작사 / 태그 / 컬렉션 영상) 값만 바꿔 레지스트리에 저장
	const int slot = VideoViewSlot();
	if (slot < 0)
		return;
	mode = (std::max)(0, (std::min)(4, mode));
	if (mode == 4 && slot != 2)
		return;   // [레이블] 은 제작사 · 레이블 영상 화면만
	for (int i = 0; i < 5; ++i)
		if (m_videoModeBtns[i].GetSafeHwnd())
			m_videoModeBtns[i].SetChecked(i == mode);
	if (mode == m_videoView[slot])
		return;
	const bool labelChanged = (mode == 4) != (m_videoView[slot] == 4);
	m_videoView[slot] = mode;
	const bool list = (mode == 1);
	static const wchar_t* vkeys[5] = { L"VideoViewMode", L"VideoViewModeActor", L"VideoViewModeStudio", L"VideoViewModeTag", L"VideoViewModeCollection" };
	AfxGetApp()->WriteProfileInt(L"Settings", vkeys[slot], mode);
	if (list)
		AutoSizeListColumns();
	m_overlayFade.clear();
	StopWallPlayers();   // [월 모드] 재생 창 정리 (월 모드면 아래에서 새 배치로 다시)
	m_pkgPopup.Hide();   // [패키지] · [레이블] 팝업 정리
	m_pkgHoverG = -1;
	SetupActorCards();   // 격자 칸 크기 ([격자] 가로 카드 / [패키지] 세로 카드 / [월 모드] 16:9 이미지 · 여백 0 / [레이블] 폭 전체 줄)
	UpdateLeftPane();    // 목록 보이기 / 격자 머리 영역만 + 다시 배치
	if (labelChanged)
		ApplyFilter();   // [레이블]: 레이블 → 품번 묶음 순서 + 묶음 머리줄 (들어가고 나올 때 순서를 다시)
	m_grid.Refresh();
	SyncWallPlayers();
	UpdateSortArrows();
	const int row = GetSelectedRow();
	if (list)
	{
		if (row >= 0)
			m_list.EnsureVisible(row, FALSE);
		m_list.Invalidate(FALSE);
		m_list.SetFocus();
	}
	else
	{
		m_grid.ScrollToTop();
		if (row >= 0)
			m_grid.OnSelectionChanged();
		m_grid.SetFocus();
	}
}

CString CRuliManagerDlg::ListPartLabel(int itemIdx) const
{
	// 분할 영상: 파일 순번 "(2/3)" - 카드와 같은 기준 (파일 이름 마지막 '_' 오른쪽 숫자, 같은 묶음 파일이 2개 이상일 때만)
	if (itemIdx < 0 || itemIdx >= static_cast<int>(m_lib.items.size()))
		return CString();
	CString key;
	int num = 0;
	if (!SplitPartName(m_lib.items[itemIdx].path, key, num))
		return CString();
	const auto pit = m_partTotal.find(key);
	if (pit == m_partTotal.end() || pit->second <= 1)
		return CString();
	CString part;
	part.Format(L"(%d/%d)", num, pit->second);
	return part;
}

void CRuliManagerDlg::AutoSizeListColumns()
{
	// [리스트] 열 폭 자동: 목록 클라이언트 폭(세로 스크롤바 제외)에 꽉 차게
	//  - 커버(체크 칸 포함) · 별점은 내용에 맞춘 고정 폭
	//  - 나머지는 최소 폭을 먼저 주고, 남는 폭을 비율(가중치)대로 나눔 → 창이 좁아 최소 폭 합보다 작으면 최소 폭 그대로 (가로 스크롤)
	if (!m_list.GetSafeHwnd() || !VideoListActive())
		return;
	CRect client;
	m_list.GetClientRect(&client);
	const int avail = client.Width();
	if (avail <= 0)
		return;
	struct Col { int idx; int minW; int weight; };   // weight 0 = 고정 (minW 그대로)
	const int stateW = ::GetSystemMetrics(SM_CXSMICON) + 6;
	const Col cols[] = {
		{ LC_COVER,    stateW + m_listCoverW + 8, 0 },
		{ LC_CODE,     DX(68),  2 },   // 굵은 큰 글꼴 + 순번 "(2/3)"
		{ LC_RELEASE,  DX(46),  1 },
		{ LC_RATING,   CStarRatingCtrl::StarsWidth(m_listStarBandH, 5) + DX(18) + 8, 0 },
		{ LC_DURATION, DX(38),  1 },
		{ LC_STUDIO,   DX(70),  3 },
		{ LC_ACTORS,   DX(80),  4 },
		{ LC_TAGS,     DX(100), 6 },
	};
	int minSum = 0, weightSum = 0;
	for (const Col& c : cols)
	{
		minSum += c.minW;
		weightSum += c.weight;
	}
	const int extra = (std::max)(0, avail - minSum);
	int given = 0, lastFlex = -1;
	std::vector<int> widths;
	for (size_t i = 0; i < _countof(cols); ++i)
	{
		int w = cols[i].minW;
		if (cols[i].weight > 0 && weightSum > 0)
		{
			const int add = extra * cols[i].weight / weightSum;
			w += add;
			given += add;
			lastFlex = static_cast<int>(i);
		}
		widths.push_back(w);
	}
	if (lastFlex >= 0)
		widths[lastFlex] += extra - given;   // 나눗셈 나머지는 마지막 늘어나는 열(태그)에 → 정확히 꽉 참
	m_list.SetRedraw(FALSE);
	bool changed = false;
	for (size_t i = 0; i < _countof(cols); ++i)
		if (m_list.GetColumnWidth(cols[i].idx) != widths[i])
		{
			m_list.SetColumnWidth(cols[i].idx, widths[i]);
			changed = true;
		}
	m_list.SetRedraw(TRUE);
	if (changed)
		m_list.Invalidate(FALSE);
}

void CRuliManagerDlg::DrawListTagChips(CDC* dc, const CRect& cell, const CString& tags)
{
	// 영상 상세 태그 칩과 같은 색 (밝은 칩 + 어두운 글자, 둥근 모서리), × 없음
	const std::vector<CString> list = CVideoLibrary::SplitList(tags);
	if (list.empty())
		return;
	const int saved = dc->SaveDC();
	dc->IntersectClipRect(cell);
	dc->SelectObject(m_listFont.GetSafeHandle() ? &m_listFont : GetFont());   // [리스트] 글꼴 (+1pt)
	dc->SetBkMode(TRANSPARENT);
	TEXTMETRIC tm = {};
	dc->GetTextMetrics(&tm);
	const int chipH = tm.tmHeight + 6, padX = 7, gap = 4, margin = 4;
	const int left = cell.left + margin, right = cell.right - margin;
	// 줄 수: 칸 높이에 들어가는 만큼 (가운데 정렬)
	const int maxLines = (std::max)(1, (cell.Height() - margin * 2 + gap) / (chipH + gap));
	// 배치 먼저 (넘치는 칩 수 셈)
	struct Chip { CRect rc; CString text; };
	std::vector<Chip> chips;
	int x = left, line = 0;
	size_t shown = 0;
	for (size_t i = 0; i < list.size(); ++i)
	{
		CString t = list[i];
		t.Trim();
		if (t.IsEmpty())
			continue;
		const int w = (std::min)(right - left, TextFB::Width(dc, t) + padX * 2);
		if (x > left && x + w > right)
		{
			++line;
			x = left;
		}
		if (line >= maxLines)
			break;
		chips.push_back({ CRect(x, line * (chipH + gap), x + w, line * (chipH + gap) + chipH), t });
		x += w + gap;
		++shown;
	}
	size_t total = 0;
	for (const CString& t : list) { CString s = t; s.Trim(); if (!s.IsEmpty()) ++total; }
	const int lines = chips.empty() ? 0 : (chips.back().rc.top / (chipH + gap) + 1);
	const int top = cell.top + (cell.Height() - (lines * (chipH + gap) - gap)) / 2;
	CString more;
	if (shown < total)
		more.Format(L"+%d", static_cast<int>(total - shown));
	if (!more.IsEmpty() && !chips.empty())
	{
		// 넘친 칩 수: 마지막 줄 끝에 "+N" (자리가 없으면 마지막 칩을 줄여서)
		const int mw = TextFB::Width(dc, more) + 6;
		Chip& last = chips.back();
		if (last.rc.right + gap + mw > right)
			last.rc.right = (std::max)(static_cast<int>(last.rc.left) + padX * 2 + 8, right - gap - mw);   // LONG → int (형 맞춤)
	}
	CBrush br(TCOL(TC_LIGHT));
	CPen pen(PS_SOLID, 1, TCOL(TC_LIGHT));
	CBrush* ob = dc->SelectObject(&br);
	CPen* op = dc->SelectObject(&pen);
	for (const Chip& c : chips)
	{
		CRect r = c.rc;
		r.OffsetRect(0, top);
		dc->RoundRect(r, CPoint(6, 6));
		dc->SetTextColor(TCOL(TC_DARK));
		CRect tr(r.left + padX, r.top, r.right - padX + 2, r.bottom);
		TextFB::Draw(dc, c.text, tr, DT_LEFT, true);   // 글꼴 대체 (일본어 한자 등)
	}
	dc->SelectObject(ob);
	dc->SelectObject(op);
	if (!more.IsEmpty() && !chips.empty())
	{
		CRect r = chips.back().rc;
		r.OffsetRect(0, top);
		dc->SetTextColor(TCOL(TC_SUB2));
		CRect mr(r.right + gap, r.top, right, r.bottom);
		TextFB::Draw(dc, more, mr, DT_LEFT, false);
	}
	dc->RestoreDC(saved);
}

bool CRuliManagerDlg::ListStarBand(int row, CRect& band)
{
	// 별점 칸 안 왼쪽, 세로 가운데 (높이 = 상세 별점 컨트롤)
	CRect cell;
	if (!m_list.GetSubItemRect(row, LC_RATING, LVIR_BOUNDS, cell))
		return false;
	const int h = (std::min)(static_cast<int>(cell.Height()), m_listStarBandH);
	const int y = cell.top + (cell.Height() - h) / 2;
	band.SetRect(cell.left + 6, y, cell.right - 2, y + h);
	return band.Width() > 0;
}

void CRuliManagerDlg::SetListStarHover(int row, int val)
{
	if (row == m_listStarHoverRow && val == m_listStarHoverVal)
		return;
	const int oldRow = m_listStarHoverRow;
	m_listStarHoverRow = row;
	m_listStarHoverVal = val;
	CRect cell;
	if (oldRow >= 0 && m_list.GetSubItemRect(oldRow, LC_RATING, LVIR_BOUNDS, cell))
		m_list.InvalidateRect(&cell, FALSE);
	if (row >= 0 && row != oldRow && m_list.GetSubItemRect(row, LC_RATING, LVIR_BOUNDS, cell))
		m_list.InvalidateRect(&cell, FALSE);
	else if (row >= 0 && m_list.GetSubItemRect(row, LC_RATING, LVIR_BOUNDS, cell))
		m_list.InvalidateRect(&cell, FALSE);
}

BOOL CRuliManagerDlg::PreTranslateMessage(MSG* pMsg)
{
	// 설정 버튼 오른쪽 클릭: 미리보기를 만드는 중이면 [미리보기 만들기 중단]
	if (pMsg->message == WM_RBUTTONUP && pMsg->hwnd == m_btnSettings.GetSafeHwnd() && m_prevBusy && !m_prevStopped)
	{
		CMenu menu;
		menu.CreatePopupMenu();
		CString t;
		t.Format(L"미리보기 만들기 중단 (%d / %d)", m_prevDone + 1, m_prevTotal);
		menu.AppendMenu(MF_STRING, 1, t);
		CPoint pt;
		::GetCursorPos(&pt);
		if (DarkMenu::Track(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, this) == 1)
			StopPreviews();
		return TRUE;
	}
	// [리스트] 별점 칸 마우스 오버: 상세 별점 컨트롤처럼 마우스 위치까지 별 미리 보기 (오버 중인 줄만 - 누르면 그 줄을 고르고 저장)
	if (pMsg->hwnd == m_list.GetSafeHwnd() && VideoListActive())
	{
		if (pMsg->message == WM_MOUSEMOVE)
		{
			if (!m_listTrackLeave)
			{
				TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_list.GetSafeHwnd(), 0 };
				m_listTrackLeave = ::TrackMouseEvent(&tme) != FALSE;
			}
			LVHITTESTINFO hit = {};
			hit.pt = CPoint(static_cast<short>(LOWORD(pMsg->lParam)), static_cast<short>(HIWORD(pMsg->lParam)));
			const int row = m_list.SubItemHitTest(&hit);
			int val = -1;
			CRect band;
			if (row >= 0 && row < static_cast<int>(m_view.size()) && hit.iSubItem == LC_RATING && !IsItemOffline(m_view[row]) && ListStarBand(row, band))
			{
				const int h = CStarRatingCtrl::HitStarIn(band, hit.pt, 5);
				if (h > 0)
					val = h;
			}
			if (val > 0)
				SetListStarHover(row, val);
			else
				SetListStarHover(-1, -1);
		}
		else if (pMsg->message == WM_MOUSELEAVE)
		{
			m_listTrackLeave = false;
			SetListStarHover(-1, -1);
		}
	}
	return CDialogEx::PreTranslateMessage(pMsg);
}

bool CRuliManagerDlg::ListCoverRect(int row, CRect& rc)
{
	// 첫 열의 글자 자리(체크 칸 · 빈 아이콘 오른쪽) 안에 커버 크기만큼 (세로 가운데)
	CRect label;
	if (!m_list.GetSubItemRect(row, 0, LVIR_LABEL, label))
		return false;
	const int h = (std::min)(static_cast<int>(label.Height()) - 4, m_listCoverH - 4);
	const int w = (std::min)(static_cast<int>(label.Width()) - 4, m_listCoverW);
	if (w <= 0 || h <= 0)
		return false;
	const int x = label.left + 2, y = label.top + (label.Height() - h) / 2;
	rc.SetRect(x, y, x + w, y + h);
	return true;
}

void CRuliManagerDlg::OnNmCustomDrawList(NMHDR* pNMHDR, LRESULT* pResult)
{
	// [리스트]: 기본 그리기(체크 칸 · 선택 색 · 글자)가 끝난 뒤 첫 열에 커버 이미지를 덧그림
	NMLVCUSTOMDRAW* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(pNMHDR);
	*pResult = CDRF_DODEFAULT;
	if (!VideoListActive())
		return;
	switch (cd->nmcd.dwDrawStage)
	{
	case CDDS_PREPAINT:
		*pResult = CDRF_NOTIFYITEMDRAW;
		return;
	case CDDS_ITEMPREPAINT:
		*pResult = CDRF_NOTIFYPOSTPAINT;
		return;
	case CDDS_ITEMPOSTPAINT:
	{
		const int row = static_cast<int>(cd->nmcd.dwItemSpec);
		if (row < 0 || row >= static_cast<int>(m_view.size()))
			return;
		{
			// 별점 열: 상세 별점 컨트롤과 같은 별 (그 칸에 마우스가 있으면 미리 보기)
			CRect band, cell;
			if (ListStarBand(row, band) && m_list.GetSubItemRect(row, LC_RATING, LVIR_BOUNDS, cell))
			{
				CDC* sdc = CDC::FromHandle(cd->nmcd.hdc);
				const int saved = sdc->SaveDC();
				sdc->IntersectClipRect(cell);
				const int shown = (row == m_listStarHoverRow && m_listStarHoverVal >= 0) ? m_listStarHoverVal : m_lib.items[m_view[row]].rating;
				CStarRatingCtrl::DrawStars(sdc, band, shown, 5, RGB(0xFF, 0xC8, 0x1E), RGB(0xE6, 0xEA, 0xEE), kTextColor, m_listFont.GetSafeHandle() ? &m_listFont : GetFont(), !IsItemOffline(m_view[row]));
				sdc->RestoreDC(saved);
			}
		}
		{
			// 품번 열: 굵은 큰 글꼴 + 분할 영상 순번 "(2/3)" 은 회색 (보통 글꼴)
			CRect cell;
			if (m_list.GetSubItemRect(row, LC_CODE, LVIR_BOUNDS, cell))
			{
				CDC* cdc = CDC::FromHandle(cd->nmcd.hdc);
				const int saved = cdc->SaveDC();
				cdc->IntersectClipRect(cell);
				cdc->SetBkMode(TRANSPARENT);
				const VideoItem& cv = m_lib.items[m_view[row]];
				const CString part = ListPartLabel(m_view[row]);
				CRect tr(cell.left + 6, cell.top, cell.right - 4, cell.bottom);
				int x = tr.left;
				if (!cv.code.IsEmpty())
				{
					cdc->SelectObject(m_listCodeFont.GetSafeHandle() ? &m_listCodeFont : &m_listFont);
					cdc->SetTextColor(kTextColor);
					const int partW = part.IsEmpty() ? 0 : TextFB::Width(cdc, L" " + part);
					CRect cr(tr.left, tr.top, (std::max)(static_cast<int>(tr.left), static_cast<int>(tr.right) - partW), tr.bottom);
					TextFB::Draw(cdc, cv.code, cr, DT_LEFT, true);
					x = (std::min)(static_cast<int>(cr.right), static_cast<int>(tr.left) + TextFB::Width(cdc, cv.code));
				}
				if (!part.IsEmpty())
				{
					cdc->SelectObject(m_listFont.GetSafeHandle() ? &m_listFont : GetFont());
					cdc->SetTextColor(TCOL(TC_SUB2));
					CRect pr(x + (cv.code.IsEmpty() ? 0 : 4), tr.top, tr.right, tr.bottom);
					TextFB::Draw(cdc, part, pr, DT_LEFT, false);
				}
				cdc->RestoreDC(saved);
			}
		}
		{
			// 태그 열: 칩 (보기 전용 - 편집은 오른쪽 상세의 태그 칸)
			CRect cell;
			if (m_list.GetSubItemRect(row, LC_TAGS, LVIR_BOUNDS, cell))
				DrawListTagChips(CDC::FromHandle(cd->nmcd.hdc), cell, m_lib.items[m_view[row]].tags);
		}
		CRect rc;
		if (!ListCoverRect(row, rc))
			return;
		CDC* dc = CDC::FromHandle(cd->nmcd.hdc);
		CRect col0;
		m_list.GetSubItemRect(row, 0, LVIR_LABEL, col0);
		const int saved = dc->SaveDC();
		dc->IntersectClipRect(col0);   // 열 폭을 줄여도 옆 열을 덮지 않게
		bool loading = false;
		if (CBitmap* bmp = GetVideoCover(m_view[row], m_listCoverW, m_listCoverH - 4, &loading))
		{
			CDC mem;
			mem.CreateCompatibleDC(dc);
			CBitmap* old = mem.SelectObject(bmp);
			dc->BitBlt(rc.left, rc.top, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);   // 커버 칸 크기 그대로 (열이 좁으면 오른쪽이 잘림)
			mem.SelectObject(old);
		}
		else
		{
			// 읽는 중 / 이미지 없음: 어두운 칸 + 재생 아이콘
			dc->FillSolidRect(rc, kThumbBackColor);
			VectorIcon::Movie(dc, rc, loading ? RGB(0x5C, 0x6A, 0x74) : RGB(0xB0, 0xB8, 0xBE), 0.6);
		}
		dc->RestoreDC(saved);
		return;
	}
	default:
		return;
	}
}

void CRuliManagerDlg::OnNmClickList(NMHDR* pNMHDR, LRESULT* pResult)
{
	// [리스트] 첫 열 체크 칸 클릭 = 그 영상 체크 전환 (가상 목록이라 직접 처리)
	*pResult = 0;
	const NMITEMACTIVATE* p = reinterpret_cast<const NMITEMACTIVATE*>(pNMHDR);
	LVHITTESTINFO hit = {};
	hit.pt = p->ptAction;
	const int row = m_list.SubItemHitTest(&hit);
	if (row >= 0 && row < static_cast<int>(m_view.size()) && hit.iSubItem == LC_RATING && VideoListActive())
	{
		// 별점 열 클릭: 상세 별점 컨트롤을 누른 것과 같음 (같은 별 다시 = 없음, 바로 저장)
		CRect band;
		const int val = ListStarBand(row, band) ? CStarRatingCtrl::HitStarIn(band, p->ptAction, 5) : 0;
		if (val > 0 && m_view[row] == m_curItem && m_starRating.IsWindowEnabled())
		{
			const int now = m_starRating.GetRating();
			m_starRating.SetRating(val == now ? 0 : val);
			SetListStarHover(-1, -1);
			OnDetailsChanged();
			CommitDetails();
			m_grid.Invalidate(FALSE);
			if (row < m_list.GetItemCount())
				m_list.RedrawItems(row, row);
		}
		return;
	}
	if (row < 0 || row >= static_cast<int>(m_view.size()) || !(hit.flags & LVHT_ONITEMSTATEICON))
		return;
	CString key = m_lib.items[m_view[row]].path;
	key.MakeLower();
	if (!m_videoChecked.erase(key))
		m_videoChecked.insert(key);
	m_list.RedrawItems(row, row);
}

void CRuliManagerDlg::OnBnClickedVideoViewH() { SetVideoPortrait(false); }
void CRuliManagerDlg::OnBnClickedVideoViewV() { SetVideoPortrait(true); }

void CRuliManagerDlg::SetVideoPortrait(bool portrait)
{
	// [가로] ↔ [세로] 격자: 지금 화면(영상 탭 / 배우 출연작 / 제작사 영상 / 태그 영상) 값만 바꿔 레지스트리에 저장
	const int s = VideoViewSlot();
	if (s < 0 || s >= 4)
		return;   // (예전 [가로|세로] - 지금은 버튼을 숨겨 [패키지] 로 대신)
	for (int i = 0; i < 2; ++i)
		if (m_videoViewBtns[i].GetSafeHwnd())
			m_videoViewBtns[i].SetChecked((i == 1) == portrait);
	if (portrait == m_videoPortrait[s])
		return;
	m_videoPortrait[s] = portrait;
	static const wchar_t* pkeys[4] = { L"VideoPortrait", L"VideoPortraitActor", L"VideoPortraitStudio", L"VideoPortraitTag" };
	AfxGetApp()->WriteProfileInt(L"Settings", pkeys[s], portrait ? 1 : 0);
	m_overlayFade.clear();
	SetupActorCards();     // 영상 격자 칸 크기 (세로 카드 / 가로 카드)
	m_grid.Refresh();
	m_grid.Invalidate(FALSE);
	if (GridIdxOf(GetSelectedRow()) >= 0)
		m_grid.OnSelectionChanged();   // 고른 카드가 보이게
	m_grid.SetFocus();
}

void CRuliManagerDlg::OnBnClickedStudioViewGrid() { SetStudioView(0); }
void CRuliManagerDlg::OnBnClickedStudioViewList() { SetStudioView(1); }

void CRuliManagerDlg::SetStudioView(int mode)
{
	// 제작사 탭: [격자] ↔ [리스트] (레지스트리에 저장)
	mode = (mode == 1) ? 1 : 0;
	for (int i = 0; i < 2; ++i)
		if (m_studioViewBtns[i].GetSafeHwnd())
			m_studioViewBtns[i].SetChecked(i == mode);
	if (mode == m_studioView)
		return;
	m_studioView = mode;
	AfxGetApp()->WriteProfileInt(L"Settings", L"StudioView", mode);
	m_pageCat = 0;         // 리스트는 페이지 없음 · 격자로 돌아오면 첫 페이지
	UpdateLeftPane();      // 페이지당 개수 콤보 보이기 / 숨기기 + 다시 배치
	RebuildCategories();   // 줄 배치를 다시 (선택 유지)
	m_catGrid.ScrollToTop();
	m_catGrid.OnSelectionChanged();
	m_catGrid.SetFocus();
}

void CRuliManagerDlg::RebuildYearView()
{
	// m_view(출연작, 검색 · 별점 거르기 적용) → 연도 줄(세로) × 1~12월 칸(가로) (발매일 "YYYY-MM-DD" / "YYYY.MM.DD", 월을 모르면 "?" 칸, 연도가 없으면 "날짜 없음" 줄)
	if (!m_yearView.GetSafeHwnd())
		return;
	std::vector<int> rows(m_view.size());
	for (size_t i = 0; i < rows.size(); ++i)
		rows[i] = static_cast<int>(i);
	std::stable_sort(rows.begin(), rows.end(), [this](int a, int b)   // 같은 달 안에서는 발매일 순
	{
		return m_lib.items[m_view[a]].release.Compare(m_lib.items[m_view[b]].release) < 0;
	});
	// 나눠진 영상(파트): 첫 번째 파트 카드만, 나머지 파트는 그 카드에 묶음 (카드 위 1 · 2 … 칸으로 고름)
	std::map<CString, std::vector<std::pair<int, int>>> groups;   // 묶음 → (파트 번호, 행)
	for (int row : rows)
	{
		CString key;
		int num = 0;
		if (SplitPartName(m_lib.items[m_view[row]].path, key, num))
		{
			const auto it = m_partTotal.find(key);
			if (it != m_partTotal.end() && it->second > 1)
				groups[key].push_back({ num, row });
		}
	}
	std::map<int, std::vector<int>> parts;   // 카드 행 → 파트 행들 (번호 순)
	std::set<int> hiddenParts;               // 카드로 따로 그리지 않는 파트 행
	for (auto& kv : groups)
	{
		auto& list = kv.second;
		if (list.size() < 2)
			continue;
		std::sort(list.begin(), list.end());
		std::vector<int> rowsOfGroup;
		for (const auto& pr : list)
			rowsOfGroup.push_back(pr.second);
		for (size_t i = 1; i < rowsOfGroup.size(); ++i)
			hiddenParts.insert(rowsOfGroup[i]);
		parts[rowsOfGroup[0]] = std::move(rowsOfGroup);
	}
	std::map<int, CYearView::Column> years;
	CYearView::Column noDate;
	noDate.title = L"날짜 없음";
	bool anyNoDate = false;
	for (int row : rows)
	{
		if (hiddenParts.count(row))
			continue;   // 2번째 이후 파트는 첫 파트 카드에 묶음
		const CString& rel = m_lib.items[m_view[row]].release;
		int y = 0, mo = 0;
		if (rel.GetLength() >= 4 && iswdigit(rel[0]) && iswdigit(rel[1]) && iswdigit(rel[2]) && iswdigit(rel[3]))
			y = _wtoi(rel.Left(4));
		if (rel.GetLength() >= 7 && iswdigit(rel[5]))
			mo = _wtoi(rel.Mid(5, 2));
		if (y < 1900 || y > 3000)
		{
			noDate.months[12].push_back(row);
			anyNoDate = true;
			continue;
		}
		CYearView::Column& col = years[y];
		if (col.title.IsEmpty())
			col.title.Format(L"%d년", y);
		col.months[(mo >= 1 && mo <= 12) ? mo - 1 : 12].push_back(row);
	}
	std::vector<CYearView::Column> cols;
	for (auto& kv : years)
		cols.push_back(std::move(kv.second));   // 오래된 연도부터
	if (anyNoDate)
		cols.push_back(std::move(noDate));
	// 배우 이름별 카드 배경색: 별칭 변경 이력(… 별칭2 → 별칭1 → 대표 이름, 괄호 부분 빼고 같은 이름은 하나로 — 배우 상세 이력과 같은 순서)의
	// 이름마다 고유색, 영상의 배우 칸에 그 배우가 적힌 이름의 색으로 카드 뒤를 칠함
	std::vector<std::pair<CString, COLORREF>> legend;
	std::map<int, COLORREF> cardColors;
	if (m_drillActorIdx >= 0 && m_drillActorIdx < static_cast<int>(m_lib.actors.size()))
	{
		auto stripParen = [](const CString& name)   // "나기 히카루(Hikaru Nagi)" → "나기 히카루"
		{
			int p = name.Find(L'(');
			const int pw = name.Find(L'\xFF08');
			if (p < 0 || (pw >= 0 && pw < p)) p = pw;
			CString s = (p > 0) ? name.Left(p) : name;
			s.Trim();
			if (s.IsEmpty())
			{
				s = name;
				s.Trim();
			}
			return s;
		};
		static const COLORREF kNameColors[] = {
			RGB(0xE5, 0x73, 0x73), RGB(0x4F, 0xA8, 0xE0), RGB(0x66, 0xBB, 0x6A), RGB(0xFF, 0xA7, 0x26), RGB(0xAB, 0x6F, 0xD0),
			RGB(0xEC, 0x6F, 0xA0), RGB(0x26, 0xA6, 0x9A), RGB(0xC0, 0xCA, 0x33), RGB(0xA1, 0x88, 0x7F), RGB(0x78, 0x90, 0x9C) };
		const ActorInfo& actor = m_lib.actors[m_drillActorIdx];
		std::vector<CString> names;   // 이력 순서 (오래된 이름 → 대표 이름)
		const std::vector<CString> als = CVideoLibrary::SplitList(actor.aliases);
		for (auto it = als.rbegin(); it != als.rend(); ++it)
			names.push_back(stripParen(*it));
		names.push_back(stripParen(actor.name));
		std::vector<CString> uniq;    // 같은 이름은 하나로 (대표 이름 쪽을 남김)
		for (auto it = names.rbegin(); it != names.rend(); ++it)
		{
			bool dup = it->IsEmpty();
			for (const CString& u : uniq)
				if (u.CompareNoCase(*it) == 0) { dup = true; break; }
			if (!dup)
				uniq.insert(uniq.begin(), *it);
		}
		// 색 순서는 배우마다 무작위 (배우 대표 이름으로 정한 시드 → 같은 배우는 언제 열어도 같은 색, 이름이 10개 이하면 서로 다른 색)
		size_t order[_countof(kNameColors)];
		for (size_t i = 0; i < _countof(order); ++i)
			order[i] = i;
		{
			CString seedName = actor.name;
			seedName.MakeLower();
			UINT32 h = 2166136261u;   // FNV-1a
			for (int i = 0; i < seedName.GetLength(); ++i)
			{
				h ^= static_cast<UINT32>(seedName[i]);
				h *= 16777619u;
			}
			UINT32 s = h ? h : 0x9E3779B9u;
			for (size_t i = _countof(order) - 1; i > 0; --i)   // Fisher-Yates (xorshift32)
			{
				s ^= s << 13; s ^= s >> 17; s ^= s << 5;
				std::swap(order[i], order[s % (i + 1)]);
			}
		}
		for (size_t i = 0; i < uniq.size(); ++i)
			legend.push_back({ uniq[i], kNameColors[order[i % _countof(order)]] });
		for (const CYearView::Column& col : cols)
			for (const auto& month : col.months)
				for (int row : month)
				{
					// 이 작품에서 쓴 이름: 참여 별칭(actorAliases)에 이 배우의 별칭이 있으면 그것, 없으면 배우 칸 이름
					//  (배우 칸은 대표 이름으로 정리되어 저장되므로 과거 이름은 참여 별칭에 있음 — NormalizeVideoActors)
					const VideoItem& vi = m_lib.items[m_view[row]];
					std::vector<CString> candidates;
					for (const CString& c : CVideoLibrary::SplitList(vi.actorAliases))
						if (m_lib.FindActorByAnyName(c) == m_drillActorIdx)
							candidates.push_back(c);
					for (const CString& n : CVideoLibrary::SplitList(vi.actors))
					{
						const int who = m_lib.FindActorByAnyName(n);
						if (who < 0 || who == m_drillActorIdx)
							candidates.push_back(n);   // 다른 배우는 빼고
					}
					for (const CString& n : candidates)
					{
						const CString s = stripParen(n);
						bool found = false;
						for (const auto& lg : legend)
							if (lg.first.CompareNoCase(s) == 0) { cardColors[row] = lg.second; found = true; break; }
						if (found)
							break;
					}
				}
	}
	const bool reset = (m_yearViewActor != m_drillActorIdx);
	m_yearViewActor = m_drillActorIdx;
	m_yearCardColors = cardColors;   // 카드 커버의 빈 영역도 같은 색 (m_onDrawCard)
	m_yearView.SetCardColors(std::move(cardColors));
	m_yearView.SetLegend(std::move(legend));
	m_yearView.SetCardSize(YearCardW(), YearCardH());
	m_yearView.SetData(std::move(cols), std::move(parts), reset);
	m_yearView.SetSelected(GetSelectedRow());
}

void CRuliManagerDlg::RefreshShotStrip()
{
	// 영상 스크린샷: <영상 폴더>\Screenshot\ 의 이미지 (파일 이름이 영상 이름으로 시작하는 것이 있으면 그것만, 없으면 폴더 전체)
	if (!m_shotStrip.GetSafeHwnd())
		return;
	std::vector<CString> list;
	if (ShotStripShown() && m_curItem >= 0 && m_curItem < static_cast<int>(m_lib.items.size()) && !IsItemOffline(m_curItem))
	{
		const VideoItem& v = m_lib.items[m_curItem];
		CString dir = v.path;
		::PathRemoveFileSpecW(dir.GetBuffer());
		dir.ReleaseBuffer();
		dir += L"\\Screenshot\\";
		CString stem = v.FileName();
		::PathRemoveExtensionW(stem.GetBuffer());
		stem.ReleaseBuffer();
		std::vector<CString> all, mine;
		WIN32_FIND_DATAW fd = {};
		HANDLE h = ::FindFirstFileW(dir + L"*", &fd);
		if (h != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
					continue;
				CString ext = ::PathFindExtensionW(fd.cFileName);
				ext.MakeLower();
				if (ext != L".jpg" && ext != L".jpeg" && ext != L".png" && ext != L".webp" && ext != L".bmp" && ext != L".gif")
					continue;
				const CString full = dir + fd.cFileName;
				all.push_back(full);
				if (!stem.IsEmpty() && _wcsnicmp(fd.cFileName, stem, stem.GetLength()) == 0)
					mine.push_back(full);
			} while (::FindNextFileW(h, &fd));
			::FindClose(h);
		}
		list.swap(mine.empty() ? all : mine);
		std::sort(list.begin(), list.end(), [](const CString& a, const CString& b) { return ::StrCmpLogicalW(a, b) < 0; });
	}
	// 다른 영상을 고르면 스크롤을 맨 위로 (파일 수가 같아도)
	const CString video = (m_curItem >= 0 && m_curItem < static_cast<int>(m_lib.items.size())) ? m_lib.items[m_curItem].path : CString();
	if (video.CompareNoCase(m_shotVideo) != 0)
	{
		m_shotVideo = video;
		m_shotStrip.ScrollToStart();
	}
	if (list != m_shotFiles)
	{
		m_shotFiles.swap(list);
		m_shotStrip.SetCount(static_cast<int>(m_shotFiles.size()));   // 맨 위로
		if (m_shotThumbs.size() > 300)
			m_shotThumbs.clear();
	}
	m_shotStrip.Invalidate(FALSE);
}

void CRuliManagerDlg::DrawShotCard(CDC* dc, int i, const CRect& rc, bool hot)
{
	// 스크린샷 한 칸: 비율 유지해서 가운데 (칸 크기로 한 번 축소해 보관), 마우스 오버 = 테두리
	dc->FillSolidRect(rc, kThumbBackColor);
	if (i < 0 || i >= static_cast<int>(m_shotFiles.size()))
		return;
	CString key;
	key.Format(L"%s|%d|%d", static_cast<LPCWSTR>(m_shotFiles[i]), rc.Width(), rc.Height());
	auto it = m_shotThumbs.find(key);
	if (it == m_shotThumbs.end())
	{
		std::unique_ptr<CBitmap> bmp;
		CImage img;
		if (LoadImageFileSfw(img, m_shotFiles[i]) && !img.IsNull() && img.GetWidth() > 0 && img.GetHeight() > 0)   // SFW: 모자이크
		{
			const double scale = (std::min)(static_cast<double>(rc.Width()) / img.GetWidth(), static_cast<double>(rc.Height()) / img.GetHeight());
			const int w = (std::max)(1, static_cast<int>(img.GetWidth() * scale + 0.5));
			const int h = (std::max)(1, static_cast<int>(img.GetHeight() * scale + 0.5));
			CDC mem;
			mem.CreateCompatibleDC(dc);
			bmp = std::make_unique<CBitmap>();
			bmp->CreateCompatibleBitmap(dc, rc.Width(), rc.Height());
			CBitmap* old = mem.SelectObject(bmp.get());
			mem.FillSolidRect(0, 0, rc.Width(), rc.Height(), kThumbBackColor);
			mem.SetStretchBltMode(HALFTONE);
			::SetBrushOrgEx(mem.GetSafeHdc(), 0, 0, nullptr);
			img.Draw(mem.GetSafeHdc(), CRect(CPoint((rc.Width() - w) / 2, (rc.Height() - h) / 2), CSize(w, h)));
			mem.SelectObject(old);
		}
		it = m_shotThumbs.emplace(key, std::move(bmp)).first;
	}
	if (it->second)
	{
		CDC mem;
		mem.CreateCompatibleDC(dc);
		CBitmap* old = mem.SelectObject(it->second.get());
		dc->BitBlt(rc.left, rc.top, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
		mem.SelectObject(old);
	}
	else
	{
		dc->SetBkMode(TRANSPARENT);
		dc->SetTextColor(kCardSub);
		CFont* of = dc->SelectObject(GetFont());
		CRect tr = rc;
		dc->DrawText(L"읽을 수 없음", tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		dc->SelectObject(of);
	}
	if (hot)
	{
		CBrush br(TCOL(TC_LIGHT));
		dc->FrameRect(rc, &br);
	}
}

void CRuliManagerDlg::RefreshActorStrip()
{
	if (!m_actorStrip.GetSafeHwnd())
		return;
	if (m_namedActors && m_mode == MODE_STUDIO && m_drill.IsEmpty())
	{
		m_actorStrip.Invalidate(FALSE);   // 제작사 탭 상세의 출연 배우 카드는 그대로 (배우 정보만 다시 그림)
		return;
	}
	CString text;
	m_editActors.GetWindowText(text);
	std::vector<int> list;
	for (const CString& n : CVideoLibrary::SplitList(text))
	{
		const int idx = m_lib.FindActorByAnyName(n);   // 별칭으로 적혀 있어도 그 배우
		if (idx >= 0 && std::find(list.begin(), list.end(), idx) == list.end())
			list.push_back(idx);
	}
	m_stripActors.swap(list);
	m_actorStrip.SetCount(static_cast<int>(m_stripActors.size()));
	m_actorStrip.Invalidate(FALSE);
}

void CRuliManagerDlg::DrawStripCard(CDC* dc, int i, const CRect& rc, bool hot)
{
	if (i < 0 || i >= static_cast<int>(m_stripActors.size()))
		return;
	DrawStripCardFor(dc, m_stripActors[i], rc, hot);
}

void CRuliManagerDlg::DrawStripCardFor(CDC* dc, int idx, const CRect& rc, bool hot, bool stripHeartHot)
{
	if (idx < 0 || idx >= static_cast<int>(m_lib.actors.size()))
		return;
	const ActorInfo& a = m_lib.actors[idx];
	const int radius = DX(4);

	// 카드 배경 (둥근 모서리)
	{
		CBrush br(kCardBack);
		CPen pen(PS_SOLID, 1, kCardBack);
		CBrush* ob = dc->SelectObject(&br);
		CPen* op = dc->SelectObject(&pen);
		dc->RoundRect(rc, CPoint(radius, radius));
		dc->SelectObject(ob);
		dc->SelectObject(op);
	}

	// 사진 (3:4, 위쪽 모서리만 둥글게)
	const CRect img(rc.left, rc.top, rc.right, rc.top + rc.Width() * 4 / 3);
	{
		CRgn clip;
		clip.CreateRoundRectRgn(rc.left, rc.top, rc.right + 1, rc.bottom + 1, radius, radius);
		dc->SelectClipRgn(&clip);
		dc->IntersectClipRect(img);
		if (CBitmap* bmp = GetActorPortrait(idx, img.Width(), img.Height()))
		{
			CDC mem;
			mem.CreateCompatibleDC(dc);
			CBitmap* old = mem.SelectObject(bmp);
			dc->BitBlt(img.left, img.top, img.Width(), img.Height(), &mem, 0, 0, SRCCOPY);
			mem.SelectObject(old);
		}
		else
		{
			dc->FillSolidRect(img, kCardImage);
			DrawDefaultActorImage(dc, img, a);
		}
		dc->SelectClipRgn(nullptr);
	}

	// 별점 리본 (왼쪽 위) / 즐겨찾기 하트 (오른쪽 위, 지정된 경우만)
	DrawRatingRibbon(dc, rc, img, radius, a.rating, GetFont());
	// 국기: 사진 오른쪽 아래 (배우 탭 카드와 같게)
	{
		const int flag = CCountryCombo::FindCountry(a.nationality);
		if (flag >= 0)
		{
			const int fp = (std::max)(3, m_cardPad / 2);
			CCountryCombo::DrawFlag(dc, flag, img.right - fp - CCountryCombo::kFlagW, img.bottom - fp - CCountryCombo::kFlagH);
		}
	}
	{
		// 즐겨찾기 하트: 지정 = 빨간 하트, 하트 자리에 마우스 오버 = 반투명 회색 하트 (누르면 전환)
		const CRect heart = StripHeartRect(rc);
		if (a.favorite)
			DrawHeart(dc, heart, RGB(0xF2, 0x5C, 0x54), 255, true);
		else if (hot && stripHeartHot && m_actorStrip.HotPart() == 1)
			DrawHeart(dc, heart, RGB(0xC8, 0xCC, 0xD0), 170, true);
	}

	// 이름 3줄: 첫 줄 = 괄호 앞 이름(굵게), 둘째·셋째 줄 = 괄호 안 이름 (쉼표마다 줄을 나눔, 없으면 나이)
	CString mainName = a.name, subName;
	{
		int po = a.name.Find(L'(');
		if (po < 0) po = a.name.Find(L'\xFF08');
		if (po > 0)
		{
			int pc = a.name.ReverseFind(L')');
			const int pcW = a.name.ReverseFind(L'\xFF09');
			if (pcW > pc) pc = pcW;
			if (pc < po) pc = a.name.GetLength();
			CString inner = a.name.Mid(po + 1, pc - po - 1);
			inner.Trim();
			CString left = a.name.Left(po);
			left.Trim();
			if (!left.IsEmpty() && !inner.IsEmpty())
			{
				mainName = left;
				subName = inner;
			}
		}
	}
	if (subName.IsEmpty())
	{
		const int age = CVideoLibrary::CalcAge(a.birth);
		if (age >= 0)
			subName.Format(L"%d 살", age);
	}

	CFont* oldFont = dc->SelectObject(&m_cardNameFont);
	dc->SetBkMode(TRANSPARENT);
	const int x = rc.left + DX(3), right = rc.right - DX(3);
	int y = img.bottom + m_cardPad / 2;
	{
		// 성별 기호(♀ 분홍 / ♂ 파랑) + 이름을 한 덩어리로 가운데 정렬
		CString sym;
		COLORREF symColor = kTextColor;
		GenderSymbol(a.gender, sym, symColor);   // ♀ ♂ ⚧ ⚥ ⚲
		int symW = 0;
		if (!sym.IsEmpty())
		{
			dc->SelectObject(&m_cardSymbolFont);
			symW = TextFB::Width(dc, sym) + DX(2);
			dc->SelectObject(&m_cardNameFont);
		}
		const int avail = right - x;
		const int nameW = (std::min)(TextFB::Width(dc, mainName), (std::max)(10, avail - symW));
		const int startX = x + (std::max)(0, (avail - symW - nameW) / 2);
		if (!sym.IsEmpty())
		{
			dc->SelectObject(&m_cardSymbolFont);
			dc->SetTextColor(symColor);
			CRect sr(startX, y, startX + symW, y + m_cardLineB);
			TextFB::Draw(dc, sym, sr, DT_LEFT, false);
			dc->SelectObject(&m_cardNameFont);
		}
		dc->SetTextColor(kTextColor);
		TextFB::Draw(dc, mainName, CRect(startX + symW, y, (std::min)(right, startX + symW + nameW + 1), y + m_cardLineB), DT_LEFT, true);
	}
	y += m_cardLineB;
	if (!subName.IsEmpty())
	{
		// 괄호 안에 쉼표가 있으면 나눠서 줄마다 (최대 2줄, 남는 것은 셋째 줄에 이어서)
		std::vector<CString> parts;
		{
			CString t = subName;
			t.Replace(L'\xFF0C', L',');   // 전각 ，
			t.Replace(L'\x3001', L',');   // 、
			int start = 0;
			for (;;)
			{
				const int comma = t.Find(L',', start);
				CString part = (comma < 0) ? t.Mid(start) : t.Mid(start, comma - start);
				part.Trim();
				if (!part.IsEmpty())
					parts.push_back(part);
				if (comma < 0)
					break;
				start = comma + 1;
			}
		}
		if (parts.size() > 2)
		{
			for (size_t k = 2; k < parts.size(); ++k)
				parts[1] += L", " + parts[k];
			parts.resize(2);
		}
		dc->SelectObject(GetFont());
		dc->SetTextColor(kCardSub);
		for (const CString& part : parts)
		{
			TextFB::Draw(dc, part, CRect(x, y, right, y + m_cardLine), DT_CENTER, true);
			y += m_cardLine;
		}
	}

	// 맨 아래: 구분선 + 제작 당시 나이 (발매일 - 생년월일) - 제작사 탭 상세의 출연 배우 카드는 없음
	if (StripShowsAge())
	{
		const int lineY = rc.bottom - m_cardPad / 2 - m_cardLine - m_cardPad / 2 - 1;
		dc->FillSolidRect(rc.left, lineY, rc.Width(), 1, kCardLine);
		int age = -1;
		SYSTEMTIME rel = {};
		int by = 0, bm = 0, bd = 0;
		const bool hasRelease = (m_curItem >= 0 && m_dateRelease.GetSafeHwnd() && m_dateRelease.GetTime(&rel) == GDT_VALID);
		if (hasRelease && !a.birth.IsEmpty() && swscanf_s(a.birth, L"%d-%d-%d", &by, &bm, &bd) == 3 && by > 0)
		{
			age = rel.wYear - by;
			if (rel.wMonth < bm || (rel.wMonth == bm && rel.wDay < bd))
				--age;   // 발매일에 아직 생일 전
			if (age < 0 || age >= 150)
				age = -1;
		}
		CString text;
		if (age >= 0)
			text.Format(L"제작 당시 나이 %d살", age);
		else
			text = L"제작 당시 나이 -";
		dc->SelectObject(GetFont());
		dc->SetTextColor(age >= 0 ? kCardIcon : kCardSub);
		const int ty = lineY + 1 + m_cardPad / 2;
		TextFB::Draw(dc, text, CRect(rc.left + DX(3), ty, rc.right - DX(3), ty + m_cardLine), DT_CENTER, true);
	}
	dc->SelectObject(oldFont);

	// 마우스 오버: 파란 테두리
	if (hot)
	{
		CPen pen(PS_SOLID, 2, kButtonColor);
		CPen* op = dc->SelectObject(&pen);
		CBrush* ob = static_cast<CBrush*>(dc->SelectStockObject(NULL_BRUSH));
		CRect sr = rc;
		sr.DeflateRect(1, 1);
		dc->RoundRect(sr, CPoint(radius, radius));
		dc->SelectObject(op);
		dc->SelectObject(ob);
	}
}

CRect CRuliManagerDlg::StripHeartRect(const CRect& card)
{
	const int size = (std::max)(12, card.Width() * 20 / 100);
	const int pad = (std::max)(3, DX(3));
	return CRect(card.right - pad - size, card.top + pad, card.right - pad, card.top + pad + size);
}

void CRuliManagerDlg::OpenActorFromStrip(int i)
{
	if (i < 0 || i >= static_cast<int>(m_stripActors.size()))
		return;
	GoToActor(m_stripActors[i]);
}

void CRuliManagerDlg::GoToActor(int idx)
{
	if (idx < 0 || idx >= static_cast<int>(m_lib.actors.size()))
		return;

	CommitDetails();
	const CString name = m_lib.actors[idx].name;   // 저장하면서 목록이 바뀌어도 이름으로 다시 찾음
	m_actorSelName = name;   // 배우 탭으로 바뀌면 이 배우를 선택
	if (m_mode != MODE_ACTOR)
	{
		m_gotoActorName = name;   // 탭 전환은 배우 선택을 풀므로 이 배우만 넘김
		OnModeChanged(IDC_RADIO_ACTOR);
		m_gotoActorName.Empty();
	}
	else if (!m_drill.IsEmpty())
	{
		BackToList();   // 출연작 화면이면 배우 목록으로 나옴
		m_actorSelName = name;
	}
	idx = m_lib.FindActor(name);
	if (idx < 0)
		return;
	if (SelectedActorIndex() != idx)
	{
		// 검색어 때문에 목록에 없으면 검색을 지우고 다시
		m_editSearch.SetWindowText(L"");
		RebuildActorGrid();
	}
	for (size_t row = 0; row < m_actorRows.size(); ++row)
	{
		if (m_actorRows[row] == idx)
		{
			SelectActorRow(static_cast<int>(row));
			m_actorGrid.EnsureVisible(static_cast<int>(row));
			break;
		}
	}
	m_actorGrid.SetFocus();
}

// ---------------------------------------------------------------------------
// 작업 DB 반영

void CRuliManagerDlg::UpdateApplyDbButton()
{
	if (CWnd* w = GetDlgItem(IDC_BTN_APPLYDB))
		w->EnableWindow((m_lib.HasUnappliedChanges() && !m_lib.DbBroken()) || CVideoLibrary::HasUnsavedImages());
}

void CRuliManagerDlg::OnBnClickedApplyDb()
{
	CommitDetails();   // 편집 중인 상세 정보도 먼저 저장
	const bool info = m_lib.HasUnappliedChanges() && !m_lib.DbBroken();
	const bool images = CVideoLibrary::HasUnsavedImages();
	if (!info && !images)
	{
		UpdateApplyDbButton();
		return;
	}
	CWaitCursor wait;
	CString msg;
	bool failed = false;
	// 정보 DB: 작업 DB → library.vmdb
	if (info)
	{
		if (m_lib.ApplyWorkDb())
			msg = L"DB";
		else
		{
			failed = true;
			AfxMessageBox(L"DB 에 반영하지 못했습니다 (파일이 사용 중이거나 쓰기 권한 없음).", MB_ICONWARNING);
		}
	}
	// 이미지 변경: 캐시 → images.vmdb (캐시는 종료 때 지움)
	if (images)
	{
		CString err;
		const int r = CVideoLibrary::SaveImageDb(&err);
		if (r >= 0)
			msg += msg.IsEmpty() ? L"이미지 DB" : L" · 이미지 DB";
		else
		{
			failed = true;
			AfxMessageBox(L"이미지 DB 를 저장하지 못했습니다.\n" + err, MB_ICONWARNING);
		}
	}
	// 이동식 디스크 영상의 커버 캐시도 covers.vmdb 에 (새 커버가 있을 때만, 캐시는 종료 때 지움)
	{
		CString err;
		const int r = m_lib.SaveCoverDb(&err);
		if (r > 0)
			msg += msg.IsEmpty() ? L"커버 DB" : L" · 커버 DB";
		else if (r < 0)
		{
			failed = true;
			AfxMessageBox(L"커버 DB 를 저장하지 못했습니다.\n" + err, MB_ICONWARNING);
		}
	}
	if (!msg.IsEmpty())
		SetDlgItemText(IDC_STATIC_STATUS, msg + (failed ? L" 일부만 반영했습니다." : L" 에 반영했습니다."));
	UpdateApplyDbButton();
}

// ---------------------------------------------------------------------------
// 재 스캔: 새로고침 + 이미 저장된 영상에도 폴더 구조 다시 적용

void CRuliManagerDlg::OnBnClickedRescan()
{
	if (AfxMessageBox(L"재 스캔할까요?\n\n"
		L"등록 폴더를 다시 읽고, 이미 저장된 영상에도 영상 폴더의 정보 txt 와 폴더 구조를 다시 적용해\n"
		L"비어 있는 칸(제목 · 발매일 · 배우 · 제작사 · 태그 · 메모 등)을 채웁니다. (값이 있는 칸은 바꾸지 않음)\n\n"
		L"영상이 많으면 시간이 걸릴 수 있습니다.",
		MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;
	CommitDetails();
	const CString keep = (m_curItem >= 0) ? m_lib.items[m_curItem].path : CString();

	int added = 0, removed = 0, relinked = 0, filled = 0;
	{
		CWaitCursor wait;
		BeginLibraryChange();
		m_lib.Refresh(added, removed, &relinked);   // 새 파일 · 없어진 파일 · 경로가 바뀐 파일
		// 저장된 영상도 폴더/파일 이름에서 비어 있는 배우 · 스튜디오 · 발매일을 다시 채움 (값이 있는 칸은 그대로)
		for (VideoItem& v : m_lib.items)
		{
			const VideoItem before = v;
			m_lib.ApplyFolderStructure(v);   // 영상 폴더 txt → 폴더 구조 순서로 빈칸 채움
			if (v.code != before.code || v.actors != before.actors || v.studio != before.studio || v.release != before.release ||
				v.title != before.title || v.tags != before.tags || v.memo != before.memo ||
				v.rating != before.rating || v.oCount != before.oCount || v.censor != before.censor)
				++filled;
		}
		m_lib.SyncActorsFromVideos();   // 새 배우 추가 (배우 폴더의 사진 · 텍스트 정보 포함)
		m_lib.SyncNamedFromVideos();
		ResetThumbnails();
		m_lib.Save();
		RebuildActorGrid();
		m_catsDirty = true;
		ApplyFilter();
		SelectPath(keep);
		if (m_curItem >= 0)
			ShowDetails(m_curItem);   // 채워진 배우 · 스튜디오 · 발매일을 상세 정보에도
	}

	CString s;
	s.Format(L"재 스캔 완료: txt · 폴더 구조로 정보를 채운 영상 %d개, 경로가 바뀐 파일 %d개 다시 연결, 새 파일 %d개 임시 목록에 추가, 없어진 파일 %d개 제거",
		filled, relinked, added, removed);
	m_staticStatus.SetWindowText(s);
}

// ---------------------------------------------------------------------------
// 정보 txt 생성: 영상 정보 → 영상 폴더\영상이름.txt, 배우 정보 → 배우 폴더\배우폴더이름.txt

void CRuliManagerDlg::OnBnClickedExportTxt()
{
	if (AfxMessageBox(L"배우 · 영상 정보를 txt 파일로 만들까요?\n\n"
		L"· 영상: 저장된 영상마다 같은 폴더에 '영상 이름.txt' (예: ABC-123.mp4 → ABC-123.txt)\n"
		L"· 배우: 배우 폴더가 있는 배우마다 배우 폴더에 '배우 폴더 이름.txt'\n\n"
		L"같은 이름의 txt 가 있으면 덮어씁니다. (내용이 같으면 그대로 둠)",
		MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;
	CommitDetails();   // 편집 중인 내용도 포함

	int vSame = 0, vFail = 0, aSame = 0, aNoFolder = 0, aFail = 0;
	int vWritten = 0, aWritten = 0;
	{
		CWaitCursor wait;
		vWritten = m_lib.ExportVideoInfoTxt(vSame, vFail);
		aWritten = m_lib.ExportActorInfoTxt(aSame, aNoFolder, aFail);
	}
	CString vFailText, aFailText;
	if (vFail) vFailText.Format(L", 실패 %d개", vFail);
	if (aFail) aFailText.Format(L", 실패 %d개", aFail);
	CString msg;
	msg.Format(L"정보 txt 생성 완료\n\n영상: %d개 생성/갱신, %d개 변경 없음%s\n배우: %d개 생성/갱신, %d개 변경 없음, 배우 폴더 없음 %d명%s",
		vWritten, vSame, static_cast<LPCWSTR>(vFailText), aWritten, aSame, aNoFolder, static_cast<LPCWSTR>(aFailText));
	AfxMessageBox(msg, (vFail || aFail) ? MB_ICONWARNING : MB_ICONINFORMATION);
}
