#include "pch.h"
#include "RuliManager.h"
#include "PluginPickDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// ---------------------------------------------------------------------------
// 선택 상태 (플러그인별 기억)

void PluginPickState::LoadRemembered()
{
	states.clear();
	if (pluginId.IsEmpty())
		return;
	const CString s = AfxGetApp()->GetProfileString(L"Settings", L"PluginPick_" + pluginId, L"");
	int pos = 0;
	for (;;)
	{
		CString tok = s.Tokenize(L"|", pos);
		if (pos < 0)
			break;
		const int eq = tok.Find(L'=');
		if (eq <= 0)
			continue;
		CString k = tok.Left(eq);
		k.Trim();
		if (!k.IsEmpty())
			states[k] = tok.Mid(eq + 1).Trim() == L"1";
	}
}

void PluginPickState::SaveRemembered() const
{
	if (pluginId.IsEmpty())
		return;
	CString s;
	for (const auto& kv : states)
	{
		if (!s.IsEmpty()) s += L"|";
		s += kv.first + (kv.second ? L"=1" : L"=0");
	}
	AfxGetApp()->WriteProfileString(L"Settings", L"PluginPick_" + pluginId, s);
}

void PluginPickState::ApplyDefaults(std::vector<PluginPickRow>& rows) const
{
	for (PluginPickRow& r : rows)
	{
		// 같은 값 줄도 고를 수 있음 (여러 개를 받을 때 다음 영상에 그대로 따라가도록 선택을 남김)
		const auto it = states.find(r.key);
		r.check = (it != states.end()) ? it->second : r.cur.IsEmpty();
	}
}

// ---------------------------------------------------------------------------

CPluginPickDlg::CPluginPickDlg(const CString& caption, const CString& guide, const std::vector<PluginPickRow>& rows, int remaining, CWnd* pParent)
	: CDialogEx(IDD_PLUGIN_PICK, pParent), m_rows(rows), m_caption(caption), m_guide(guide), m_remaining(remaining)
{
}

BEGIN_MESSAGE_MAP(CPluginPickDlg, CDialogEx)
	ON_BN_CLICKED(IDC_PICK_SKIP, &CPluginPickDlg::OnBnClickedSkip)
	ON_BN_CLICKED(IDC_PICK_CHECKALL, &CPluginPickDlg::OnBnClickedCheckAll)
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

BOOL CPluginPickDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	SetWindowText(m_caption);
	SetDlgItemText(IDC_PICK_GUIDE, m_guide);

	// 배치 (DLU → 픽셀)
	auto du = [this](int x, int y, int w, int h)
	{
		CRect r(x, y, x + w, y + h);
		MapDialogRect(&r);
		return r;
	};
	const int W = 480, M = 7;
	// 왼쪽부터: [체크 + 항목] [가져온 값] [→ (체크하면)] [지금 값] - 가져온 값이 지금 값 자리로 들어가는 방향
	const int colLabel = M, wLabel = 84;
	const int colNew = colLabel + wLabel + 4, wNew = 186;
	const int colArrow = colNew + wNew + 2, wArrow = 14;
	const int colCur = colArrow + wArrow + 2, wCur = W - M - colCur;
	const HFONT font = static_cast<HFONT>(GetFont()->GetSafeHandle());
	// 코드로 만드는 컨트롤은 CWnd 로 연결하지 않음 (테마가 체크 칸을 어두운 모양으로 바꿀 수 있게) - 창이 닫힐 때 같이 없어짐
	auto make = [&](LPCWSTR cls, const CString& text, DWORD style, const CRect& rc, UINT id) -> HWND
	{
		HWND h = ::CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, rc.left, rc.top, rc.Width(), rc.Height(),
			GetSafeHwnd(), reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), AfxGetInstanceHandle(), nullptr);
		if (h)
			::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
		return h;
	};
	int y = 30;
	make(L"STATIC", L"항목", SS_NOPREFIX, du(colLabel + 12, y, wLabel - 12, 9), static_cast<UINT>(IDC_STATIC));
	make(L"STATIC", L"가져온 값 (고칠 수 있음)", SS_NOPREFIX, du(colNew, y, wNew, 9), static_cast<UINT>(IDC_STATIC));
	make(L"STATIC", L"지금 값", SS_NOPREFIX, du(colCur, y, wCur, 9), static_cast<UINT>(IDC_STATIC));
	y += 12;
	for (size_t i = 0; i < m_rows.size(); ++i)
	{
		const PluginPickRow& r = m_rows[i];
		const UINT id = RowId(i);
		if (r.image)
		{
			// 이미지 줄 (맨 아래): [체크 + 항목 · 크기] [가져온 이미지] [→] [지금 이미지], 썸네일 더블클릭 = 원본 보기
			const int imgH = 76;
			y += 3;
			HWND chk = make(L"BUTTON", r.label, WS_TABSTOP | BS_AUTOCHECKBOX, du(colLabel, y + 1, wLabel, 11), id);
			if (chk)
				::SendMessageW(chk, BM_SETCHECK, r.check ? BST_CHECKED : BST_UNCHECKED, 0);
			auto sizeText = [](const CString& path) -> CString
			{
				CImage img;
				if (path.IsEmpty() || !LoadImageFile(img, path) || img.IsNull())
					return CString();
				CString s;
				s.Format(L"%d × %d", img.GetWidth(), img.GetHeight());
				return s;
			};
			make(L"STATIC", L"가져온: " + sizeText(r.value), SS_NOPREFIX, du(colLabel + 12, y + 15, wLabel - 12, 9), static_cast<UINT>(IDC_STATIC));
			if (!r.cur.IsEmpty())
				make(L"STATIC", L"지금: " + sizeText(r.cur), SS_NOPREFIX, du(colLabel + 12, y + 26, wLabel - 12, 9), static_cast<UINT>(IDC_STATIC));
			make(L"STATIC", L"더블클릭 = 원본 보기", SS_NOPREFIX, du(colLabel + 12, y + 40, wLabel - 12, 9), static_cast<UINT>(IDC_STATIC));
			auto setup = [&](CImagePreview& pv, const CString& path, const CRect& rc, UINT cid, LPCWSTR caption)
			{
				pv.Create(L"", WS_CHILD | WS_VISIBLE | SS_NOTIFY, rc, this, cid);
				pv.SetBackColor(TCOL(TC_EDIT));
				if (path.IsEmpty() || !pv.SetImageFile(path, true))
					pv.SetPlaceholder(L"(이미지 없음)");
				const CString file = path, cap = caption;
				pv.m_onDblClick = [this, file, cap]()
				{
					if (!file.IsEmpty())
						m_viewer.Open(this, file, true, cap);   // SFW 옵션이 켜져 있으면 모자이크
				};
			};
			setup(m_imgNew, r.value, du(colNew, y, wNew, imgH), id + 2, L"가져온 이미지");
			HWND arrow = make(L"STATIC", L"→", SS_CENTER | SS_NOPREFIX, du(colArrow, y + imgH / 2 - 5, wArrow, 11), id + 3);
			if (arrow && !r.check)
				::ShowWindow(arrow, SW_HIDE);
			setup(m_imgCur, r.cur, du(colCur, y, wCur, imgH), id + 1, L"지금 이미지");
			y += imgH + 4;
			continue;
		}
		HWND chk = make(L"BUTTON", r.same ? r.label + L" (같음)" : r.label, WS_TABSTOP | BS_AUTOCHECKBOX, du(colLabel, y + 1, wLabel, 11), id);
		if (chk)
			::SendMessageW(chk, BM_SETCHECK, r.check ? BST_CHECKED : BST_UNCHECKED, 0);
		make(L"EDIT", r.value, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, du(colNew, y, wNew, 12), id + 2);
		HWND arrow = make(L"STATIC", L"→", SS_CENTER | SS_NOPREFIX, du(colArrow, y + 1, wArrow, 11), id + 3);   // 체크하면 보임 (가져온 값 → 지금 값)
		if (arrow && !r.check)
			::ShowWindow(arrow, SW_HIDE);
		make(L"EDIT", r.cur.IsEmpty() ? CString(L"(비어 있음)") : r.cur, WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, du(colCur, y, wCur, 12), id + 1);
		y += 15;
	}

	// 아래쪽: [나머지에도 적용] · [모두 체크] · [적용] [건너뛰기] [취소]
	y += 4;
	if (CWnd* all = GetDlgItem(IDC_PICK_ALL))
	{
		CString t;
		t.Format(L"나머지 %d개에도 같은 항목 적용 (창 없이)", m_remaining);
		all->SetWindowText(t);
		all->MoveWindow(du(M, y + 2, 260, 10));
		all->ShowWindow(m_remaining > 0 ? SW_SHOW : SW_HIDE);
	}
	y += 16;
	const int bw = 52, bh = 14;
	if (CWnd* b = GetDlgItem(IDC_PICK_CHECKALL)) b->MoveWindow(du(M, y, 64, bh));
	if (CWnd* b = GetDlgItem(IDCANCEL))          b->MoveWindow(du(W - M - bw, y, bw, bh));
	if (CWnd* b = GetDlgItem(IDC_PICK_SKIP))
	{
		b->MoveWindow(du(W - M - bw * 2 - 4, y, bw, bh));
		b->ShowWindow(m_remaining > 0 ? SW_SHOW : SW_HIDE);   // 하나만 받을 때는 [취소] 와 같음
	}
	if (CWnd* b = GetDlgItem(IDOK))
		b->MoveWindow(du(W - M - bw * (m_remaining > 0 ? 3 : 2) - 4 * (m_remaining > 0 ? 2 : 1), y, bw, bh));
	if (CWnd* g = GetDlgItem(IDC_PICK_GUIDE))
		g->MoveWindow(du(M, M, W - M * 2, 20));

	// 창 크기를 줄 수에 맞춤
	const CRect client = du(0, 0, W, y + bh + M);
	CRect wr = client;
	::AdjustWindowRectEx(&wr, GetStyle(), FALSE, GetExStyle());
	SetWindowPos(nullptr, 0, 0, wr.Width(), wr.Height(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	CenterWindow(GetParent());

	m_theme.Apply(this, {});   // 코드로 만든 컨트롤까지 어두운 색상
	if (CWnd* ok = GetDlgItem(IDOK))
		ok->SetFocus();
	return FALSE;
}

void CPluginPickDlg::Collect()
{
	for (size_t i = 0; i < m_rows.size(); ++i)
	{
		const UINT id = RowId(i);
		PluginPickRow& r = m_rows[i];
		r.check = IsDlgButtonChecked(id) == BST_CHECKED;
		if (r.image)
			continue;   // 이미지 줄은 경로를 고치지 않음
		CString v;
		GetDlgItemText(id + 2, v);
		v.Trim();
		r.value = v;
	}
	m_applyAll = (m_remaining > 0) && IsDlgButtonChecked(IDC_PICK_ALL) == BST_CHECKED;
}

void CPluginPickDlg::OnOK()
{
	Collect();
	m_result = IDOK;
	CDialogEx::OnOK();
}

void CPluginPickDlg::OnBnClickedSkip()
{
	m_result = IDC_PICK_SKIP;
	EndDialog(IDCANCEL);
}

void CPluginPickDlg::OnCancel()
{
	m_result = IDCANCEL;
	CDialogEx::OnCancel();
}

void CPluginPickDlg::OnBnClickedCheckAll()
{
	// 하나라도 꺼져 있으면 모두 켜고, 모두 켜져 있으면 모두 끔 (같음 줄 포함)
	bool allOn = true;
	for (size_t i = 0; i < m_rows.size(); ++i)
		if (IsDlgButtonChecked(RowId(i)) != BST_CHECKED)
			allOn = false;
	for (size_t i = 0; i < m_rows.size(); ++i)
	{
		CheckDlgButton(RowId(i), allOn ? BST_UNCHECKED : BST_CHECKED);
		UpdateArrow(i);
	}
}

void CPluginPickDlg::UpdateArrow(size_t i)
{
	if (CWnd* a = GetDlgItem(RowId(i) + 3))
		a->ShowWindow(IsDlgButtonChecked(RowId(i)) == BST_CHECKED ? SW_SHOW : SW_HIDE);
}

BOOL CPluginPickDlg::OnCommand(WPARAM wParam, LPARAM lParam)
{
	// 줄의 체크 칸을 누르면 화살표 보이기 / 숨기기
	const UINT id = LOWORD(wParam);
	if (HIWORD(wParam) == BN_CLICKED && id >= kRowIdBase && id < RowId(m_rows.size()) && (id - kRowIdBase) % kRowIdStep == 0)
		UpdateArrow((id - kRowIdBase) / kRowIdStep);
	return CDialogEx::OnCommand(wParam, lParam);
}

HBRUSH CPluginPickDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
	{
		// 화살표는 강조색
		const UINT id = pWnd ? static_cast<UINT>(pWnd->GetDlgCtrlID()) : 0;
		if (id >= kRowIdBase && id < RowId(m_rows.size()) && (id - kRowIdBase) % kRowIdStep == 3)
			pDC->SetTextColor(TCOL(TC_ACCENT));
		return br;
	}
	return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}
