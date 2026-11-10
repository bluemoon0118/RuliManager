#include "pch.h"
#include "RuliManager.h"
#include "CoverPickDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	const UINT WM_APP_COVER_FRAME  = WM_APP + 0x3C0;   // 작업 스레드 → 창: 장면 (wParam = 정확한 장면이면 1, lParam = VideoFrame*)
	const UINT WM_APP_COVER_OPENED = WM_APP + 0x3C1;   // 작업 스레드 → 창: 영상을 열었음 (wParam = 성공 1 / 실패 0)
	const LONGLONG kSec = CFrameReader::kSecond;
	const LONGLONG kSliderUnit = kSec / 10;              // 슬라이더 한 칸 = 0.1초

	CString TimeText(LONGLONG t)
	{
		if (t < 0)
			t = 0;
		const LONGLONG s = t / kSec;
		CString r;
		r.Format(L"%d:%02d:%02d", static_cast<int>(s / 3600), static_cast<int>((s / 60) % 60), static_cast<int>(s % 60));
		return r;
	}
}

CCoverPickDlg::CCoverPickDlg(const CString& videoPath, CWnd* pParent)
	: CDialogEx(IDD_COVER_PICK, pParent), m_video(videoPath)
{
}

BEGIN_MESSAGE_MAP(CCoverPickDlg, CDialogEx)
	ON_WM_PAINT()
	ON_WM_DESTROY()
	ON_WM_HSCROLL()
	ON_WM_CTLCOLOR()
	ON_MESSAGE(WM_APP_COVER_FRAME, &CCoverPickDlg::OnFrameReady)
	ON_MESSAGE(WM_APP_COVER_OPENED, &CCoverPickDlg::OnOpened)
	ON_BN_CLICKED(IDC_CP_COVER, &CCoverPickDlg::OnBnClickedSaveCover)
	ON_BN_CLICKED(IDC_CP_SHOT, &CCoverPickDlg::OnBnClickedSaveShot)
END_MESSAGE_MAP()

HBRUSH CCoverPickDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	return m_theme.CtlColor(pDC, pWnd, nCtlColor);
}

BOOL CCoverPickDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	SetWindowText(L"커버 만들기 - " + CString(::PathFindFileNameW(m_video)));
	m_slider.SubclassDlgItem(IDC_CP_SLIDER, this);
	m_slider.SetRange(0, 1000);
	m_slider.EnableWindow(FALSE);   // 영상을 열면 켬
	if (CWnd* f = GetDlgItem(IDC_CP_FRAME))
	{
		f->GetWindowRect(&m_frameRc);
		ScreenToClient(&m_frameRc);
		f->ShowWindow(SW_HIDE);   // 자리만 씀 (장면은 창이 직접 그림)
	}
	GetDlgItem(IDC_CP_COVER)->EnableWindow(FALSE);
	GetDlgItem(IDC_CP_SHOT)->EnableWindow(FALSE);
	SetDlgItemText(IDC_CP_TIME, L"여는 중...");
	m_theme.Apply(this, {});

	m_self = GetSafeHwnd();
	m_thread = std::thread([this]() { Worker(); });
	m_slider.SetFocus();
	return FALSE;
}

void CCoverPickDlg::Worker()
{
	// 작업 스레드: 영상을 열고, 요청(최신 것 하나)마다 장면 한 장을 읽어 창에 넘김
	const HRESULT co = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	const bool mf = SUCCEEDED(::MFStartup(MF_VERSION, MFSTARTUP_LITE));
	{
		CFrameReader reader;
		const bool ok = mf && reader.Open(m_video);
		m_duration = ok ? reader.Duration() : 0;
		::PostMessageW(m_self, WM_APP_COVER_OPENED, ok ? 1 : 0, 0);
		for (;;)
		{
			ReqKind kind = rqNone;
			LONGLONG t = 0;
			{
				std::unique_lock<std::mutex> lk(m_mx);
				m_cv.wait(lk, [this]() { return m_quit || m_reqKind != rqNone; });
				if (m_quit)
					break;
				kind = m_reqKind;
				t = m_reqT;
				m_reqKind = rqNone;
			}
			if (!ok)
				continue;
			std::unique_ptr<VideoFrame> f(new VideoFrame());
			bool got = false;
			if (kind == rqKeyframe)
				got = reader.GrabKeyframe(t, *f);
			else if (kind == rqExact)
				got = reader.GrabNext(t, *f);   // 바로 뒤면 이어서 디코딩, 아니면 이동
			else if (kind == rqNext)
				got = reader.Next(*f);
			if (got)
			{
				VideoFrame* raw = f.release();
				if (!::PostMessageW(m_self, WM_APP_COVER_FRAME, kind == rqKeyframe ? 0 : 1, reinterpret_cast<LPARAM>(raw)))
					delete raw;
			}
		}
		reader.Close();
	}
	if (mf)
		::MFShutdown();
	if (SUCCEEDED(co))
		::CoUninitialize();
}

void CCoverPickDlg::Request(ReqKind kind, LONGLONG t)
{
	const LONGLONG dur = m_duration;
	if (dur <= 0 || !m_thread.joinable())
		return;
	{
		std::lock_guard<std::mutex> lk(m_mx);
		m_reqKind = kind;   // 앞의 요청이 아직 남아 있으면 덮어씀 (최신 것만)
		m_reqT = (std::max)(0LL, (std::min)(t, dur - kSec / 10));
	}
	m_cv.notify_one();
}

void CCoverPickDlg::StopWorker()
{
	if (m_thread.joinable())
	{
		{
			std::lock_guard<std::mutex> lk(m_mx);
			m_quit = true;
		}
		m_cv.notify_one();
		m_thread.join();   // 읽던 장면이 끝나면 멈춤
	}
	// 남은 장면 메시지 정리
	MSG msg;
	while (m_self && ::PeekMessageW(&msg, m_self, WM_APP_COVER_FRAME, WM_APP_COVER_FRAME, PM_REMOVE))
		delete reinterpret_cast<VideoFrame*>(msg.lParam);
}

void CCoverPickDlg::OnDestroy()
{
	StopWorker();
	CDialogEx::OnDestroy();
}

void CCoverPickDlg::OnCancel()
{
	StopWorker();
	CDialogEx::OnCancel();
}

void CCoverPickDlg::OnOK()
{
	OnBnClickedSaveCover();   // Enter: 커버로 저장 (창은 그대로)
}

LRESULT CCoverPickDlg::OnOpened(WPARAM wParam, LPARAM)
{
	const LONGLONG dur = m_duration;
	if (!wParam || dur <= 0)
	{
		m_error = L"영상을 열지 못했습니다.\n(Windows 에 코덱이 없는 형식이거나 파일을 읽을 수 없음)";
		SetDlgItemText(IDC_CP_TIME, L"");
		InvalidateRect(m_frameRc, FALSE);
		return 0;
	}
	const LONGLONG steps = dur / kSliderUnit;
	m_slider.SetRange(0, steps > INT_MAX ? INT_MAX : static_cast<int>(steps), TRUE);
	m_slider.SetLineSize(10);    // 1초
	m_slider.SetPageSize(100);   // 10초
	m_slider.EnableWindow(TRUE);
	GetDlgItem(IDC_CP_COVER)->EnableWindow(TRUE);
	GetDlgItem(IDC_CP_SHOT)->EnableWindow(TRUE);
	const LONGLONG start = dur / 5;   // 처음은 길이의 20% 지점
	SetSliderTime(start);
	Request(rqExact, start);
	m_slider.SetFocus();
	return 0;
}

LRESULT CCoverPickDlg::OnFrameReady(WPARAM wParam, LPARAM lParam)
{
	std::unique_ptr<VideoFrame> f(reinterpret_cast<VideoFrame*>(lParam));
	if (!f)
		return 0;
	m_frame = std::move(f);
	m_frameExact = (wParam != 0);
	if (m_frameExact && ::GetCapture() != m_slider.GetSafeHwnd())
		SetSliderTime(m_frame->ts);   // 다음 장면 · 끝 근처 등: 슬라이더를 실제 장면 시각으로
	UpdateTimeText();
	InvalidateRect(m_frameRc, FALSE);
	return 0;
}

LONGLONG CCoverPickDlg::SliderTime() const
{
	return static_cast<LONGLONG>(m_slider.GetPos()) * kSliderUnit;
}

void CCoverPickDlg::SetSliderTime(LONGLONG t)
{
	m_slider.SetPos(static_cast<int>(t / kSliderUnit));
}

void CCoverPickDlg::UpdateTimeText()
{
	const LONGLONG dur = m_duration;
	CString s = (m_frame ? TimeText(m_frame->ts) : CString(L"-")) + L" / " + TimeText(dur);
	if (m_frame && !m_frameExact)
		s += L"   (빠른 보기 - 놓으면 정확한 장면)";
	else if (m_frame)
	{
		CString size;
		size.Format(L"   %d × %d", m_frame->w, m_frame->h);
		s += size;
	}
	SetDlgItemText(IDC_CP_TIME, s);
}

void CCoverPickDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	if (pScrollBar && pScrollBar->GetSafeHwnd() == m_slider.GetSafeHwnd())
	{
		if (nSBCode == TB_THUMBTRACK)
			Request(rqKeyframe, SliderTime());   // 드래그 중: 키프레임 한 장 (빠름)
		else if (nSBCode == TB_ENDTRACK)
			Request(rqExact, SliderTime());      // 놓음 · 클릭 · 키: 그 시각의 정확한 장면
		return;
	}
	CDialogEx::OnHScroll(nSBCode, nPos, pScrollBar);
}

BOOL CCoverPickDlg::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN && m_duration > 0)
	{
		const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
		if (pMsg->wParam == VK_LEFT || pMsg->wParam == VK_RIGHT)
		{
			// ← → 1초, Shift 10초 (보이는 장면 시각 기준)
			const LONGLONG base = m_frame ? m_frame->ts : SliderTime();
			const LONGLONG step = (shift ? 10 : 1) * kSec * (pMsg->wParam == VK_LEFT ? -1 : 1);
			const LONGLONG t = (std::max)(0LL, (std::min)(base + step, static_cast<LONGLONG>(m_duration) - kSec / 10));
			SetSliderTime(t);
			Request(rqExact, t);
			return TRUE;
		}
		if (pMsg->wParam == VK_OEM_PERIOD || pMsg->wParam == VK_DECIMAL)
		{
			Request(rqNext, 0);   // 다음 장면 한 장
			return TRUE;
		}
	}
	return CDialogEx::PreTranslateMessage(pMsg);
}

void CCoverPickDlg::OnPaint()
{
	CPaintDC dc(this);
	if (m_frameRc.IsRectEmpty())
		return;
	// 장면 자리: 검은 바탕 + 비율 유지해서 가운데 (메모리 DC 로 그려 깜빡임 없이)
	CDC mem;
	mem.CreateCompatibleDC(&dc);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&dc, m_frameRc.Width(), m_frameRc.Height());
	CBitmap* old = mem.SelectObject(&bmp);
	const CRect area(0, 0, m_frameRc.Width(), m_frameRc.Height());
	mem.FillSolidRect(area, RGB(0, 0, 0));
	if (m_frame && m_frame->w > 0 && m_frame->h > 0)
	{
		const double scale = (std::min)(static_cast<double>(area.Width()) / m_frame->w, static_cast<double>(area.Height()) / m_frame->h);
		const int dw = (std::max)(1, static_cast<int>(m_frame->w * scale));
		const int dh = (std::max)(1, static_cast<int>(m_frame->h * scale));
		const int dx = (area.Width() - dw) / 2, dy = (area.Height() - dh) / 2;
		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth = m_frame->w;
		bi.bmiHeader.biHeight = -m_frame->h;   // 위에서 아래로
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		mem.SetStretchBltMode(HALFTONE);
		::SetBrushOrgEx(mem.GetSafeHdc(), 0, 0, nullptr);
		::StretchDIBits(mem.GetSafeHdc(), dx, dy, dw, dh, 0, 0, m_frame->w, m_frame->h, m_frame->bgra.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
	}
	else
	{
		mem.SetBkMode(TRANSPARENT);
		mem.SetTextColor(RGB(200, 200, 200));
		CFont* font = GetFont();
		CFont* oldFont = font ? mem.SelectObject(font) : nullptr;
		CRect tr = area;
		const CString text = m_error.IsEmpty() ? CString(L"영상을 여는 중...") : m_error;
		CRect calc = tr;
		mem.DrawText(text, calc, DT_CENTER | DT_WORDBREAK | DT_CALCRECT);
		tr.top += (tr.Height() - calc.Height()) / 2;
		mem.DrawText(text, tr, DT_CENTER | DT_WORDBREAK);
		if (oldFont)
			mem.SelectObject(oldFont);
	}
	dc.BitBlt(m_frameRc.left, m_frameRc.top, m_frameRc.Width(), m_frameRc.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(old);
}

void CCoverPickDlg::OnBnClickedSaveCover()
{
	if (!m_frame || !m_onSaveCover)
		return;
	wchar_t tmpDir[MAX_PATH] = {};
	::GetTempPathW(MAX_PATH, tmpDir);
	CString tmp;
	tmp.Format(L"%sRuliManager_cover_%u.jpg", tmpDir, ::GetCurrentProcessId());
	if (!SaveFrameJpeg(*m_frame, tmp, 92))
	{
		AfxMessageBox(L"장면을 JPEG 으로 저장하지 못했습니다.", MB_ICONWARNING);
		return;
	}
	const bool ok = m_onSaveCover(tmp);   // 확인 · 적용은 호출 쪽
	::DeleteFileW(tmp);
	if (ok)
		SetDlgItemText(IDC_CP_GUIDE, L"커버로 저장했습니다 (" + TimeText(m_frame->ts) + L").");
}

void CCoverPickDlg::OnBnClickedSaveShot()
{
	if (!m_frame || m_shotDir.IsEmpty())
		return;
	// <영상 이름>_M01.jpg … 비어 있는 다음 번호 (자동 추출이 지우는 "<영상 이름>_숫자.jpg" 와 겹치지 않게 M)
	::SHCreateDirectoryExW(nullptr, m_shotDir, nullptr);
	CString stem = ::PathFindFileNameW(m_video);
	::PathRemoveExtensionW(stem.GetBuffer());
	stem.ReleaseBuffer();
	CString name;
	for (int n = 1; n < 1000; ++n)
	{
		name.Format(L"%s_M%02d.jpg", stem.GetString(), n);
		if (!::PathFileExistsW(m_shotDir + name))
			break;
	}
	if (!SaveFrameJpeg(*m_frame, m_shotDir + name, 92))
	{
		AfxMessageBox(L"스크린샷을 저장하지 못했습니다.\n" + m_shotDir + name, MB_ICONWARNING);
		return;
	}
	SetDlgItemText(IDC_CP_GUIDE, L"스크린샷 저장: " + name);
	if (m_onShotSaved)
		m_onShotSaved();
}
