#include "pch.h"
#include "RuliManager.h"
#include "NameListDlg.h"
#include "BulkNameDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	std::vector<int> SortedNamedIndices(const std::vector<NamedInfo>& list, const CString& lowerQuery)
	{
		std::vector<int> rows;
		for (size_t i = 0; i < list.size(); ++i)
		{
			if (!lowerQuery.IsEmpty())
			{
				CString hay = list[i].name;
				hay.MakeLower();
				if (hay.Find(lowerQuery) < 0)
					continue;
			}
			rows.push_back(static_cast<int>(i));
		}
		std::sort(rows.begin(), rows.end(), [&](int a, int b)
		{
			return ::StrCmpLogicalW(list[a].name, list[b].name) < 0;
		});
		return rows;
	}
}

// ===========================================================================
// CNameListDlg (스튜디오 / 태그 관리)

CNameListDlg::CNameListDlg(CVideoLibrary& lib, int kind, const CString& selectName, CWnd* pParent)
	: CDialogEx(IDD_NAMELIST, pParent), m_lib(lib), m_kind(kind), m_selectName(selectName)
{
}

void CNameListDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_NL_SEARCH, m_editSearch);
	DDX_Control(pDX, IDC_NL_LIST, m_list);
	DDX_Control(pDX, IDC_NL_IMAGE, m_image);
	DDX_Control(pDX, IDC_NL_NAME, m_editName);
	DDX_Control(pDX, IDC_NL_COUNT, m_staticCount);
	DDX_Control(pDX, IDC_NL_IMAGE_PATH, m_staticImagePath);
	DDX_Control(pDX, IDC_NL_MEMO, m_editMemo);
}

BEGIN_MESSAGE_MAP(CNameListDlg, CDialogEx)
	ON_WM_CTLCOLOR()
	ON_EN_CHANGE(IDC_NL_SEARCH, &CNameListDlg::OnEnChangeSearch)
	ON_EN_CHANGE(IDC_NL_NAME, &CNameListDlg::OnFieldChanged)
	ON_EN_CHANGE(IDC_NL_MEMO, &CNameListDlg::OnFieldChanged)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_NL_LIST, &CNameListDlg::OnLvnItemChanged)
	ON_BN_CLICKED(IDC_NL_NEW, &CNameListDlg::OnBnClickedNew)
	ON_BN_CLICKED(IDC_NL_DELETE, &CNameListDlg::OnBnClickedDelete)
	ON_BN_CLICKED(IDC_NL_BULK, &CNameListDlg::OnBnClickedBulk)
	ON_BN_CLICKED(IDC_NL_SAVE, &CNameListDlg::OnBnClickedSave)
	ON_BN_CLICKED(IDC_NL_IMG_BROWSE, &CNameListDlg::OnBnClickedImageBrowse)
	ON_BN_CLICKED(IDC_NL_IMG_CLEAR, &CNameListDlg::OnBnClickedImageClear)
END_MESSAGE_MAP()

BOOL CNameListDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	m_theme.Apply(this, { IDC_NL_DELETE });   // 메인 창과 같은 색상 (삭제 버튼은 빨간색)
	m_image.SetBackColor(DarkColors::Preview);

	SetWindowText(KindName() + L" 관리");
	SetDlgItemText(IDC_NL_NEW, L"새 " + KindName());

	CRect r(0, 0, 100, 0);
	MapDialogRect(&r);
	const int dlu = r.right;

	m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
	m_list.InsertColumn(0, L"이름", LVCFMT_LEFT, dlu * 115 / 100);
	m_list.InsertColumn(1, L"영상 수", LVCFMT_RIGHT, dlu * 40 / 100);

	m_editSearch.SetCueBanner(L"이름 검색");
	m_image.SetPlaceholder(L"이미지 없음");

	if (m_kind == LIST_TAG)
	{
		// 태그는 이미지·메모 없음: 이미지 영역을 숨기고 이름/영상 수를 위로 올림
		const UINT imageIds[] = { IDC_NL_IMAGE, IDC_NL_IMG_BROWSE, IDC_NL_IMG_CLEAR, IDC_NL_IMAGE_PATH };
		for (UINT id : imageIds)
			GetDlgItem(id)->ShowWindow(SW_HIDE);

		CRect imageRc, nameLblRc;
		GetDlgItem(IDC_NL_IMAGE)->GetWindowRect(&imageRc);
		GetDlgItem(IDC_NL_NAME_LBL)->GetWindowRect(&nameLblRc);
		const int dy = nameLblRc.top - imageRc.top - 2;   // 올릴 거리 (픽셀)

		const UINT moveIds[] = { IDC_NL_NAME_LBL, IDC_NL_NAME, IDC_NL_COUNT_LBL, IDC_NL_COUNT };
		for (UINT id : moveIds)
		{
			CWnd* w = GetDlgItem(id);
			CRect rc;
			w->GetWindowRect(&rc);
			ScreenToClient(&rc);
			rc.OffsetRect(0, -dy);
			w->MoveWindow(&rc);
		}
		// 태그는 메모도 없음
		GetDlgItem(IDC_NL_MEMO_LBL)->ShowWindow(SW_HIDE);
		m_editMemo.ShowWindow(SW_HIDE);
	}

	m_counts = m_lib.CountNamed(m_kind);
	FillList(m_selectName);
	if (m_cur < 0)
		ShowItem(-1);
	return TRUE;
}

int CNameListDlg::CountOf(const CString& name) const
{
	CString key = name;
	key.MakeLower();
	auto it = m_counts.find(key);
	return it != m_counts.end() ? it->second : 0;
}

void CNameListDlg::FillList(const CString& selectName)
{
	CString query;
	m_editSearch.GetWindowText(query);
	query.Trim();
	query.MakeLower();

	m_rows = SortedNamedIndices(Items(), query);

	m_loading = true;
	m_list.SetRedraw(FALSE);
	m_list.DeleteAllItems();
	int selRow = -1;
	for (size_t row = 0; row < m_rows.size(); ++row)
	{
		const NamedInfo& n = Items()[m_rows[row]];
		const int i = m_list.InsertItem(static_cast<int>(row), n.name);
		CString c;
		c.Format(L"%d", CountOf(n.name));
		m_list.SetItemText(i, 1, c);
		if (!selectName.IsEmpty() && n.name.CompareNoCase(selectName) == 0)
			selRow = i;
	}
	m_list.SetRedraw(TRUE);
	m_list.Invalidate();
	m_loading = false;

	if (selRow >= 0)
	{
		m_list.SetItemState(selRow, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_list.EnsureVisible(selRow, FALSE);
		ShowItem(m_rows[selRow]);
	}
}

void CNameListDlg::ShowItem(int idx)
{
	m_loading = true;
	m_cur = (idx >= 0 && idx < static_cast<int>(Items().size())) ? idx : -1;
	const BOOL enable = (m_cur >= 0);

	if (m_cur >= 0)
	{
		const NamedInfo& n = Items()[m_cur];
		m_editName.SetWindowText(n.name);
		CString memo = n.memo;
		memo.Replace(L"\r\n", L"\n");
		memo.Replace(L"\n", L"\r\n");
		m_editMemo.SetWindowText(memo);
		CString c;
		c.Format(L"%d편", CountOf(n.name));
		m_staticCount.SetWindowText(c);
		SetImage(n.image);
	}
	else
	{
		m_editName.SetWindowText(L"");
		m_editMemo.SetWindowText(L"");
		m_staticCount.SetWindowText(L"");
		SetImage(CString());
	}

	m_editName.EnableWindow(enable);
	m_editMemo.EnableWindow(enable);
	GetDlgItem(IDC_NL_DELETE)->EnableWindow(enable);
	GetDlgItem(IDC_NL_IMG_BROWSE)->EnableWindow(enable);
	GetDlgItem(IDC_NL_IMG_CLEAR)->EnableWindow(enable);

	m_dirty = false;
	GetDlgItem(IDC_NL_SAVE)->EnableWindow(FALSE);
	m_loading = false;
}

void CNameListDlg::SetImage(const CString& path)
{
	m_imagePath = path;
	if (m_kind == LIST_TAG)
		return;   // 태그는 이미지를 표시하지 않음
	m_image.Clear();
	if (path.IsEmpty())
	{
		m_image.SetPlaceholder(L"이미지 없음");
		m_staticImagePath.SetWindowText(L"");
	}
	else
	{
		if (!::PathFileExistsW(path))
			m_image.SetPlaceholder(L"이미지 파일을 찾을 수 없습니다.");
		else
			m_image.SetImageFile(path);
		m_staticImagePath.SetWindowText(path);
	}
}

void CNameListDlg::OnFieldChanged()
{
	if (m_loading || m_cur < 0)
		return;
	m_dirty = true;
	GetDlgItem(IDC_NL_SAVE)->EnableWindow(TRUE);
}

bool CNameListDlg::Commit()
{
	if (!m_dirty || m_cur < 0 || m_cur >= static_cast<int>(Items().size()))
		return true;

	NamedInfo& n = Items()[m_cur];

	CString name;
	m_editName.GetWindowText(name);
	name.Trim();
	CVideoLibrary::RemoveListCommas(name);   // 구분 쉼표는 사용 불가 (괄호 안 쉼표는 허용)
	if (name.IsEmpty())
	{
		AfxMessageBox(L"이름을 입력하세요.", MB_ICONWARNING);
		m_editName.SetFocus();
		return false;
	}
	const int other = m_lib.FindNamed(m_kind, name);
	if (other >= 0 && other != m_cur)
	{
		AfxMessageBox(L"같은 이름이 이미 있습니다.", MB_ICONWARNING);
		m_editName.SetFocus();
		m_editName.SetSel(0, -1);
		return false;
	}

	if (name != n.name)
	{
		m_lib.RenameNamedInVideos(m_kind, n.name, name);   // 동영상에도 반영
		CString oldKey = n.name, newKey = name;
		oldKey.MakeLower();
		newKey.MakeLower();
		if (oldKey != newKey)
		{
			m_counts[newKey] = CountOf(n.name);
			m_counts.erase(oldKey);
		}
	}
	n.name = name;
	if (m_kind == LIST_STUDIO)
		m_editMemo.GetWindowText(n.memo);   // 태그는 메모 없음
	else
		n.memo.Empty();
	n.image = m_imagePath;

	m_dirty = false;
	m_changed = true;
	GetDlgItem(IDC_NL_SAVE)->EnableWindow(FALSE);

	m_loading = true;
	m_editName.SetWindowText(n.name);
	m_loading = false;

	for (size_t row = 0; row < m_rows.size(); ++row)
	{
		if (m_rows[row] == m_cur)
		{
			m_list.SetItemText(static_cast<int>(row), 0, n.name);
			break;
		}
	}
	return true;
}

void CNameListDlg::OnLvnItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	*pResult = 0;
	if (m_loading)
		return;

	if ((p->uChanged & LVIF_STATE) &&
		(p->uNewState & LVIS_SELECTED) && !(p->uOldState & LVIS_SELECTED) &&
		p->iItem >= 0 && p->iItem < static_cast<int>(m_rows.size()))
	{
		const int idx = m_rows[p->iItem];
		if (idx == m_cur)
			return;
		if (!Commit())
		{
			// 이름 오류: 원래 항목 선택으로 되돌림
			m_loading = true;
			for (size_t row = 0; row < m_rows.size(); ++row)
			{
				if (m_rows[row] == m_cur)
				{
					m_list.SetItemState(static_cast<int>(row), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
					break;
				}
			}
			m_loading = false;
			return;
		}
		ShowItem(idx);
	}
}

void CNameListDlg::OnEnChangeSearch()
{
	if (!Commit())
		return;
	const CString keep = (m_cur >= 0) ? Items()[m_cur].name : CString();
	FillList(keep);
}

void CNameListDlg::OnBnClickedNew()
{
	if (!Commit())
		return;

	const CString base = L"새 " + KindName();
	CString name = base;
	for (int n = 2; m_lib.FindNamed(m_kind, name) >= 0; ++n)
		name.Format(L"%s %d", static_cast<LPCWSTR>(base), n);

	NamedInfo info;
	info.name = name;
	Items().push_back(info);
	m_changed = true;

	m_editSearch.SetWindowText(L"");
	FillList(name);
	m_editName.SetFocus();
	m_editName.SetSel(0, -1);
}

void CNameListDlg::OnBnClickedDelete()
{
	if (m_cur < 0)
		return;

	const CString name = Items()[m_cur].name;
	const int count = CountOf(name);
	CString msg;
	if (count > 0)
		msg.Format(L"%s '%s'을(를) 삭제할까요?\n\n연결된 동영상 %d개에서도 빠집니다.",
			static_cast<LPCWSTR>(KindName()), static_cast<LPCWSTR>(name), count);
	else
		msg.Format(L"%s '%s'을(를) 삭제할까요?", static_cast<LPCWSTR>(KindName()), static_cast<LPCWSTR>(name));
	if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;

	m_lib.RemoveNamedFromVideos(m_kind, name);
	Items().erase(Items().begin() + m_cur);
	CString key = name;
	key.MakeLower();
	m_counts.erase(key);
	m_changed = true;
	m_dirty = false;
	m_cur = -1;

	FillList(CString());
	ShowItem(-1);
}

void CNameListDlg::OnBnClickedBulk()
{
	if (!Commit())
		return;

	CBulkNameDlg dlg(m_lib, m_kind, this);
	if (dlg.DoModal() != IDOK || dlg.m_added.empty())
		return;

	m_changed = true;
	m_counts = m_lib.CountNamed(m_kind);   // 영상에 이미 쓰이던 이름이면 영상 수도 표시
	m_editSearch.SetWindowText(L"");       // 검색 해제 → 목록 갱신
	FillList(dlg.m_added.front());

	CString msg;
	msg.Format(L"%s %d개를 등록했습니다.", static_cast<LPCWSTR>(KindName()), static_cast<int>(dlg.m_added.size()));
	AfxMessageBox(msg, MB_ICONINFORMATION);
}

void CNameListDlg::OnBnClickedSave()
{
	Commit();
}

void CNameListDlg::OnBnClickedImageBrowse()
{
	if (m_cur < 0)
		return;

	CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
		L"이미지 파일 (*.jpg;*.jpeg;*.png;*.webp;*.bmp;*.gif;*.tif;*.tiff)|*.jpg;*.jpeg;*.png;*.webp;*.bmp;*.gif;*.tif;*.tiff|모든 파일 (*.*)|*.*||",
		this);
	if (dlg.DoModal() != IDOK)
		return;

	// 원본 대신 실행 폴더의 Image\studios 에 복사본을 만들어 그것을 등록 (태그는 이미지 없음)
	if (m_kind != LIST_STUDIO)
		return;
	const CString copy = CVideoLibrary::StoreImageCopy(dlg.GetPathName(), L"studios");
	if (copy.IsEmpty())
	{
		AfxMessageBox(L"이미지를 DB 폴더로 복사하지 못했습니다.", MB_ICONWARNING);
		return;
	}
	SetImage(copy);
	OnFieldChanged();
}

void CNameListDlg::OnBnClickedImageClear()
{
	if (m_cur < 0)
		return;
	SetImage(CString());
	OnFieldChanged();
}

void CNameListDlg::OnOK()
{
	Commit();   // Enter: 저장만 (메모 칸에서는 줄바꿈)
}

void CNameListDlg::OnCancel()
{
	if (!Commit())
		return;
	CDialogEx::OnCancel();
}

// ===========================================================================
// CNamePickDlg (스튜디오 / 태그 선택)

CNamePickDlg::CNamePickDlg(CVideoLibrary& lib, int kind, const CString& current, CWnd* pParent)
	: CDialogEx(IDD_NAME_PICK, pParent), m_lib(lib), m_kind(kind), m_multi(kind == LIST_TAG)
{
	m_order = CVideoLibrary::SplitList(current);
	if (!m_multi && m_order.size() > 1)
		m_order.resize(1);
}

void CNamePickDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_PICK_SEARCH, m_editSearch);
	DDX_Control(pDX, IDC_PICK_LIST, m_list);
	DDX_Control(pDX, IDC_PICK_NEWNAME, m_editNew);
	DDX_Control(pDX, IDC_PICK_SELECTED, m_staticSelected);
}

BEGIN_MESSAGE_MAP(CNamePickDlg, CDialogEx)
	ON_WM_CTLCOLOR()
	ON_EN_CHANGE(IDC_PICK_SEARCH, &CNamePickDlg::OnEnChangeSearch)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_PICK_LIST, &CNamePickDlg::OnLvnItemChanged)
	ON_NOTIFY(NM_DBLCLK, IDC_PICK_LIST, &CNamePickDlg::OnNmDblclk)
	ON_BN_CLICKED(IDC_PICK_ADD, &CNamePickDlg::OnBnClickedAdd)
END_MESSAGE_MAP()

BOOL CNamePickDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	m_theme.Apply(this);   // 메인 창과 같은 색상

	const CString kindName = (m_kind == LIST_STUDIO) ? L"스튜디오" : L"태그";
	SetWindowText(kindName + (m_multi ? L" 선택 (여러 개 가능)" : L" 선택 (하나만)"));
	SetDlgItemText(IDC_PICK_NEWLABEL, L"새 " + kindName);

	// 현재 값이 목록에 없으면 추가 (보통은 이미 동기화되어 있음)
	for (const CString& n : m_order)
	{
		if (m_lib.FindNamed(m_kind, n) < 0)
		{
			NamedInfo info;
			info.name = n;
			m_lib.NamedList(m_kind).push_back(info);
			m_added = true;
		}
	}

	CRect rc;
	m_list.GetClientRect(&rc);
	m_list.SetExtendedStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	m_list.InsertColumn(0, L"이름", LVCFMT_LEFT, rc.Width() - ::GetSystemMetrics(SM_CXVSCROLL));

	m_editSearch.SetCueBanner(L"이름 검색");
	m_editNew.SetCueBanner(L"목록에 없는 이름");

	FillList();
	UpdateSelectedText();
	m_editSearch.SetFocus();
	return FALSE;
}

bool CNamePickDlg::IsChecked(const CString& name) const
{
	for (const CString& n : m_order)
	{
		if (n.CompareNoCase(name) == 0)
			return true;
	}
	return false;
}

void CNamePickDlg::SetChecked(const CString& name, bool checked)
{
	if (checked)
	{
		if (!m_multi)
			m_order.clear();   // 스튜디오는 하나만
		if (!IsChecked(name))
			m_order.push_back(name);
	}
	else
	{
		m_order.erase(std::remove_if(m_order.begin(), m_order.end(),
			[&](const CString& n) { return n.CompareNoCase(name) == 0; }), m_order.end());
	}
}

void CNamePickDlg::FillList()
{
	CString query;
	m_editSearch.GetWindowText(query);
	query.Trim();
	query.MakeLower();

	const std::vector<NamedInfo>& items = m_lib.NamedList(m_kind);
	const std::vector<int> rows = SortedNamedIndices(items, query);

	m_filling = true;
	m_list.SetRedraw(FALSE);
	m_list.DeleteAllItems();
	for (size_t row = 0; row < rows.size(); ++row)
	{
		const NamedInfo& n = items[rows[row]];
		const int i = m_list.InsertItem(static_cast<int>(row), n.name);
		m_list.SetItemData(i, static_cast<DWORD_PTR>(rows[row]));
		m_list.SetCheck(i, IsChecked(n.name) ? TRUE : FALSE);
	}
	m_list.SetRedraw(TRUE);
	m_list.Invalidate();
	m_filling = false;
}

void CNamePickDlg::UpdateSelectedText()
{
	CString s = L"선택: ";
	s += m_order.empty() ? CString(L"(없음)") : CVideoLibrary::JoinList(m_order);
	m_staticSelected.SetWindowText(s);
}

void CNamePickDlg::OnEnChangeSearch()
{
	FillList();
}

void CNamePickDlg::OnLvnItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	*pResult = 0;
	if (m_filling || p->iItem < 0 || !(p->uChanged & LVIF_STATE))
		return;

	const UINT oldImg = p->uOldState & LVIS_STATEIMAGEMASK;
	const UINT newImg = p->uNewState & LVIS_STATEIMAGEMASK;
	if (oldImg == newImg || oldImg == 0 || newImg == 0)
		return;

	const std::vector<NamedInfo>& items = m_lib.NamedList(m_kind);
	const int idx = static_cast<int>(m_list.GetItemData(p->iItem));
	if (idx < 0 || idx >= static_cast<int>(items.size()))
		return;

	const bool checked = (newImg == INDEXTOSTATEIMAGEMASK(2));
	SetChecked(items[idx].name, checked);

	// 하나만 선택: 다른 체크 해제
	if (checked && !m_multi)
	{
		m_filling = true;
		for (int i = 0; i < m_list.GetItemCount(); ++i)
		{
			if (i != p->iItem && m_list.GetCheck(i))
				m_list.SetCheck(i, FALSE);
		}
		m_filling = false;
	}
	UpdateSelectedText();
}

void CNamePickDlg::OnNmDblclk(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMITEMACTIVATE* p = reinterpret_cast<NMITEMACTIVATE*>(pNMHDR);
	if (p->iItem >= 0)
		m_list.SetCheck(p->iItem, !m_list.GetCheck(p->iItem));
	*pResult = 0;
}

void CNamePickDlg::OnBnClickedAdd()
{
	CString name;
	m_editNew.GetWindowText(name);
	name.Trim();
	CVideoLibrary::RemoveListCommas(name);   // 괄호 안 쉼표는 허용
	if (name.IsEmpty())
	{
		m_editNew.SetFocus();
		return;
	}

	std::vector<NamedInfo>& items = m_lib.NamedList(m_kind);
	const int existing = m_lib.FindNamed(m_kind, name);
	if (existing >= 0)
	{
		name = items[existing].name;
	}
	else
	{
		NamedInfo info;
		info.name = name;
		items.push_back(info);
		m_added = true;
	}

	SetChecked(name, true);
	m_editNew.SetWindowText(L"");
	m_editSearch.SetWindowText(L"");
	FillList();
	UpdateSelectedText();

	for (int i = 0; i < m_list.GetItemCount(); ++i)
	{
		const int idx = static_cast<int>(m_list.GetItemData(i));
		if (items[idx].name.CompareNoCase(name) == 0)
		{
			m_list.EnsureVisible(i, FALSE);
			m_list.SetItemState(i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			break;
		}
	}
}

void CNamePickDlg::OnOK()
{
	CWnd* focus = GetFocus();
	if (focus && focus->GetSafeHwnd() == m_editNew.GetSafeHwnd())
	{
		OnBnClickedAdd();
		return;
	}
	if (focus && focus->GetSafeHwnd() == m_editSearch.GetSafeHwnd())
		return;

	m_result = CVideoLibrary::JoinList(m_order);
	CDialogEx::OnOK();
}

HBRUSH CNameListDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
		return br;
	return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}

HBRUSH CNamePickDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = m_theme.CtlColor(pDC, pWnd, nCtlColor))
		return br;
	return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}
