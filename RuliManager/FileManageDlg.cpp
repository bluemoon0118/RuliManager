#include "pch.h"
#include "FileManageDlg.h"
#include "Resource.h"

#include <shlwapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <windowsx.h>   // GET_X_LPARAM
#include <algorithm>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	const COLORREF kColFix   = RGB(255, 176, 64);    // 고칠 수 있음 (주황)
	const COLORREF kColBlock = RGB(255, 110, 110);   // 자동 복구 불가 (빨강)
	const COLORREF kColWarn  = RGB(230, 210, 120);   // 확인 필요 (노랑 - 이미지 없음)
	const COLORREF kColNoDb  = RGB(140, 150, 160);   // DB 에 없는 영상 · 임시 (회색)

	bool IsDots(const wchar_t* n) { return wcscmp(n, L".") == 0 || wcscmp(n, L"..") == 0; }

	CString Join(const CString& dir, const CString& name)
	{
		CString d = dir;
		if (d.Right(1) != L"\\")
			d += L"\\";
		return d + name;
	}

	// 폴더 안 하위 폴더 · 파일 이름 (숨김 · 시스템 제외, 이름 순)
	void ListDir(const CString& dir, std::vector<CString>& subDirs, std::vector<CString>& files)
	{
		WIN32_FIND_DATAW fd = {};
		HANDLE h = ::FindFirstFileW(Join(dir, L"*"), &fd);
		if (h == INVALID_HANDLE_VALUE)
			return;
		do
		{
			if (IsDots(fd.cFileName))
				continue;
			if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
				continue;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				subDirs.push_back(fd.cFileName);
			else
				files.push_back(fd.cFileName);
		} while (::FindNextFileW(h, &fd));
		::FindClose(h);
		auto byName = [](const CString& a, const CString& b) { return ::StrCmpLogicalW(a, b) < 0; };
		std::sort(subDirs.begin(), subDirs.end(), byName);
		std::sort(files.begin(), files.end(), byName);
	}

	bool StartsNoCase(const CString& s, const CString& prefix)
	{
		return s.GetLength() >= prefix.GetLength() && _wcsnicmp(s, prefix, prefix.GetLength()) == 0;
	}
}

BEGIN_MESSAGE_MAP(CFileManageDlg, CDialogEx)
	ON_WM_CTLCOLOR()
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_BN_CLICKED(IDC_FM_RESCAN, &CFileManageDlg::OnBnClickedRescan)
	ON_BN_CLICKED(IDC_FM_ONLYBAD, &CFileManageDlg::OnBnClickedOnlyBad)
	ON_BN_CLICKED(IDC_FM_FIXONE, &CFileManageDlg::OnBnClickedFixOne)
	ON_BN_CLICKED(IDC_FM_FIXCHECKED, &CFileManageDlg::OnBnClickedFixChecked)
	ON_BN_CLICKED(IDC_FM_FIXALL, &CFileManageDlg::OnBnClickedFixAll)
	ON_NOTIFY(TVN_SELCHANGED, IDC_FM_TREE, &CFileManageDlg::OnTvnSelChanged)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_FM_TREE, &CFileManageDlg::OnTreeCustomDraw)
	ON_NOTIFY(NM_RCLICK, IDC_FM_TREE, &CFileManageDlg::OnTreeRClick)
END_MESSAGE_MAP()

CFileManageDlg::CFileManageDlg(CVideoLibrary& lib, CWnd* pParent)
	: CDialogEx(IDD_FILE_MANAGE, pParent), m_lib(lib)
{
}

void CFileManageDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_FM_TREE, m_tree);
	DDX_Control(pDX, IDC_FM_DETAIL, m_detail);
	DDX_Control(pDX, IDC_FM_ONLYBAD, m_onlyBad);
}

BOOL CFileManageDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	m_theme.Apply(this);   // 메인 창과 같은 색상

	CRect wr;
	GetWindowRect(&wr);
	m_minSize = CSize(wr.Width() * 3 / 4, wr.Height() * 3 / 4);

	// 체크 상자는 만든 뒤에 켜야 제대로 동작 (템플릿 스타일로 주면 첫 표시가 어긋남)
	m_tree.ModifyStyle(TVS_CHECKBOXES, 0);
	m_tree.ModifyStyle(0, TVS_CHECKBOXES);
	m_tree.SetBkColor(TCOL(TC_EDIT));
	m_tree.SetTextColor(TCOL(TC_TEXT));
	m_tree.SetLineColor(TCOL(TC_BORDER));
	CDarkDialogTheme::ThemeChild(m_tree.GetSafeHwnd());
	m_onlyBad.SetCheck(AfxGetApp()->GetProfileInt(L"Settings", L"FileManageOnlyBad", 0) ? BST_CHECKED : BST_UNCHECKED);

	m_layoutReady = true;
	Layout();

	CWaitCursor wait;
	CheckAll();
	BuildTree();
	ShowDetail();
	return TRUE;
}

HBRUSH CFileManageDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	return m_theme.CtlColor(pDC, pWnd, nCtlColor);
}

void CFileManageDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	if (m_minSize.cx > 0)
	{
		lpMMI->ptMinTrackSize.x = m_minSize.cx;
		lpMMI->ptMinTrackSize.y = m_minSize.cy;
	}
	CDialogEx::OnGetMinMaxInfo(lpMMI);
}

void CFileManageDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialogEx::OnSize(nType, cx, cy);
	Layout();
}

void CFileManageDlg::Layout()
{
	// 위 줄 · 아래 버튼 줄은 그대로, 트리(62%) | 상세 정보가 가운데를 채움
	if (!m_layoutReady || !m_tree.GetSafeHwnd())
		return;
	CRect c;
	GetClientRect(&c);
	CRect u(7, 6, 14, 24);   // 여백 · 위 줄 높이 (대화상자 단위 → 픽셀)
	MapDialogRect(&u);
	const int m = u.left, top = u.bottom;
	CRect b(0, 0, 14, 14);
	MapDialogRect(&b);
	const int btnH = b.Height(), gap = m;
	const int bottomY = c.bottom - m - btnH;
	const int treeW = (c.Width() - m * 3) * 62 / 100;
	m_tree.MoveWindow(m, top, treeW, bottomY - gap - top);
	m_detail.MoveWindow(m * 2 + treeW, top, c.Width() - m * 3 - treeW, bottomY - gap - top);
	const UINT bottoms[] = { IDC_FM_FIXONE, IDC_FM_FIXCHECKED, IDC_FM_FIXALL, IDC_FM_RULE };
	for (UINT id : bottoms)
	{
		if (CWnd* w = GetDlgItem(id))
		{
			CRect r;
			w->GetWindowRect(&r);
			ScreenToClient(&r);
			const int dy = (id == IDC_FM_RULE) ? (btnH - r.Height()) / 2 : 0;
			w->SetWindowPos(nullptr, r.left, bottomY + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}
	if (CWnd* w = GetDlgItem(IDCANCEL))
	{
		CRect r;
		w->GetWindowRect(&r);
		ScreenToClient(&r);
		w->SetWindowPos(nullptr, c.right - m - r.Width(), bottomY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
	if (CWnd* w = GetDlgItem(IDC_FM_SUMMARY))
	{
		CRect r;
		w->GetWindowRect(&r);
		ScreenToClient(&r);
		w->SetWindowPos(nullptr, 0, 0, (std::max)(10, static_cast<int>(c.right) - m - static_cast<int>(r.left)), r.Height(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
	Invalidate();
}

// ---------------------------------------------------------------------------
// 검사

CString CFileManageDlg::SafeName(const CString& s)
{
	// 폴더 · 파일 이름에 못 쓰는 글자는 "_" 로, 끝의 공백 · 점은 뺌
	CString r = s;
	const wchar_t bad[] = { L'\\', L'/', L':', L'*', L'?', L'"', L'<', L'>', L'|' };
	for (wchar_t c : bad)
		r.Replace(c, L'_');
	r.Trim();
	r.TrimRight(L". ");
	return r;
}

CString CFileManageDlg::ActualPath(const CString& path)
{
	// "H:\a\b.mp4" 의 각 단계를 디스크에 있는 실제 이름(대소문자)으로 - 하나라도 없으면 그대로
	if (path.GetLength() < 3 || path[1] != L':' || path[2] != L'\\')
		return path;
	CString cur = path.Left(2);
	cur.MakeUpper();
	int start = 3;
	while (start < path.GetLength())
	{
		int slash = path.Find(L'\\', start);
		if (slash < 0)
			slash = path.GetLength();
		const CString comp = path.Mid(start, slash - start);
		if (!comp.IsEmpty())
		{
			WIN32_FIND_DATAW fd = {};
			HANDLE h = ::FindFirstFileW(cur + L"\\" + comp, &fd);
			if (h == INVALID_HANDLE_VALUE)
				return path;
			::FindClose(h);
			cur += L"\\";
			cur += fd.cFileName;
		}
		start = slash + 1;
	}
	return cur;
}

int CFileManageDlg::SyncPathCase()
{
	// 검사는 대소문자까지 보므로, DB 에 적힌 경로가 실제 폴더 · 파일과 대소문자만 다르면 (예: DB "req-381\req-381.mp4", 실제 "REQ-381\REQ-381.mp4")
	//  화면에는 멀쩡해 보이는데 "고칠 수 있음" 으로 나오고, 고쳐도 디스크 이름이 이미 같아 바뀌지 않던 것 → DB 경로를 실제로 맞춤 (파일은 그대로)
	int n = 0;
	for (size_t i = 0; i < m_lib.items.size(); ++i)
	{
		const VideoItem& v = m_lib.items[i];
		if (v.pending)
			continue;
		const CString actual = ActualPath(v.path);
		if (actual != v.path && actual.CompareNoCase(v.path) == 0)
		{
			Log(L"DB 경로 대소문자 맞춤\t" + v.path + L"\t→\t" + actual);
			SetItemPath(static_cast<int>(i), actual);
			++n;
		}
	}
	return n;
}

CString CFileManageDlg::DirOf(const CString& path)
{
	CString d = path.Left(static_cast<int>(::PathFindFileNameW(path) - static_cast<LPCWSTR>(path)));
	d.TrimRight(L"\\");
	return d;
}

CString CFileManageDlg::StemOf(const CString& name)
{
	const int dot = name.ReverseFind(L'.');
	return (dot > 0) ? name.Left(dot) : name;
}

bool CFileManageDlg::IsImageExt(const CString& ext)
{
	static const wchar_t* const kExts[] = { L".jpg", L".jpeg", L".png", L".webp", L".bmp", L".gif", L".tif", L".tiff", L".svg" };
	for (const wchar_t* e : kExts)
		if (ext.CompareNoCase(e) == 0)
			return true;
	return false;
}

CString CFileManageDlg::FindImage(const CString& videoPath)
{
	// 메인 창 FindImageFor 와 같은 순서: 같은 이름 → 확장자 포함 이름 → '_' 왼쪽이 같은 이름
	static const wchar_t* const kExts[] = { L".jpg", L".jpeg", L".png", L".webp", L".bmp", L".gif", L".tif", L".tiff", L".svg" };
	const CString ext = ::PathFindExtensionW(videoPath);
	const CString stem = videoPath.Left(videoPath.GetLength() - ext.GetLength());
	for (const wchar_t* e : kExts)
		if (::PathFileExistsW(stem + e))
			return stem + e;
	for (const wchar_t* e : kExts)
		if (::PathFileExistsW(videoPath + e))
			return videoPath + e;
	const CString dir = DirOf(videoPath);
	CString key = ::PathFindFileNameW(stem);
	const int us = key.Find(L'_');
	if (us >= 0)
		key = key.Left(us);
	key.Trim();
	if (key.IsEmpty())
		return CString();
	std::vector<CString> subs, files;
	ListDir(dir, subs, files);
	for (const CString& f : files)
	{
		if (!IsImageExt(::PathFindExtensionW(f)))
			continue;
		CString k = StemOf(f);
		const int u2 = k.Find(L'_');
		if (u2 >= 0)
			k = k.Left(u2);
		k.Trim();
		if (k.CompareNoCase(key) == 0)
			return Join(dir, f);
	}
	return CString();
}

void CFileManageDlg::CheckItem(int item, Entry& e, bool keepActor) const
{
	e = Entry();
	e.item = item;
	const VideoItem& v = m_lib.items[item];
	if (v.pending)
	{
		e.state = ST_PENDING;   // 임시 항목: 정보가 아직 없음 (표시만)
		e.problems = L"· 임시 항목 (정보를 저장하지 않은 영상) - 정보를 저장하면 검사";
		return;
	}
	for (const CString& f : m_lib.folders)
		if (CVideoLibrary::IsUnder(v.path, f) && f.GetLength() > e.root.GetLength())
			e.root = f;
	e.root.TrimRight(L"\\");
	if (!e.root.IsEmpty())
		e.root = ActualPath(e.root);   // 등록 폴더 글자도 실제 대소문자로 (규칙 경로 비교용)
	if (!::PathFileExistsW(v.path))
	{
		e.state = ST_OFFLINE;   // 연결 안 된 디스크 · 옮겨진 파일: 검사하지 않음
		e.problems = L"· 파일이 없음 (연결 안 된 디스크이거나 다른 곳으로 옮겨짐)\r\n";
		return;
	}

	CString block;
	e.code = v.code;
	e.code.Trim();
	if (e.code.IsEmpty())
		block += L"· 품번 없음 - 자동 복구 불가\r\n";
	CString date;
	if (v.release.GetLength() == 10 && v.release[4] == L'-' && v.release[7] == L'-')
	{
		date = v.release;
		date.Replace(L'-', L'.');
	}
	else
		block += L"· 발매일 없음 - 자동 복구 불가\r\n";
	const std::vector<CString> actors = CVideoLibrary::SplitList(v.actors);
	CString actorName;
	if (keepActor)
	{
		// 배우 폴더 = 지금 영상이 들어 있는 등록 폴더 바로 아래 폴더 (DB 배우와 상관없이 그대로)
		if (!e.root.IsEmpty())
		{
			CString rel = DirOf(v.path);
			rel = rel.GetLength() > e.root.GetLength() ? rel.Mid(e.root.GetLength() + 1) : CString();
			const int slash = rel.Find(L'\\');
			actorName = (slash >= 0) ? rel.Left(slash) : rel;
		}
		if (actorName.IsEmpty())
			block += L"· 등록 폴더 바로 아래 영상이라 그대로 둘 배우 폴더가 없음\r\n";
	}
	else if (actors.empty())
		block += L"· 배우 없음 - 자동 복구 불가\r\n";
	else if (actors.size() > 1)
	{
		CString s;
		s.Format(L"· 배우 여러 명 (%d명) - 자동 복구에서 제외\r\n", static_cast<int>(actors.size()));
		block += s;
	}
	else
	{
		const int ai = m_lib.FindActorByAnyName(actors[0]);
		actorName = (ai >= 0) ? m_lib.actors[ai].name : actors[0];   // 별칭으로 적혀 있어도 배우 이름
	}
	if (e.root.IsEmpty())
		block += L"· 등록 폴더 밖의 영상\r\n";
	if (!block.IsEmpty())
	{
		// 배우 여러 명: 지금 배우 폴더가 그 배우 중 하나이고 아래([발매일]품번\품번)가 규칙과 같으면 "규칙 맞음"
		//  (배우 폴더를 그대로 두고 맞추기로 고친 영상이 계속 빨갛게 남지 않게 - 자동 복구 대상은 그대로 제외)
		if (!keepActor && actors.size() > 1 && block.Find(L"배우 여러 명") >= 0 && block.Find(L"품번 없음") < 0 && block.Find(L"발매일 없음") < 0 && !e.root.IsEmpty())
		{
			Entry k;
			CheckItem(item, k, true);
			if (k.state == ST_OK || k.state == ST_WARN)
			{
				const int folderActor = m_lib.FindActorByAnyName(k.actorDir);
				bool mine = false;
				for (const CString& a : actors)
					if (folderActor >= 0 && m_lib.FindActorByAnyName(a) == folderActor)
						mine = true;
				if (mine)
				{
					e = k;   // 그 배우 폴더 아래 규칙 그대로
					return;
				}
			}
		}
		e.state = ST_BLOCK;
		e.problems = block;
		e.image = FindImage(v.path);
		return;
	}

	// 규칙 경로
	e.actorDir = keepActor ? actorName : SafeName(actorName);   // 그대로 둘 배우 폴더는 실제 이름 그대로
	e.videoDir = L"[" + date + L"]" + SafeName(e.code);
	e.targetDir = Join(Join(e.root, e.actorDir), e.videoDir);
	CString partKey;
	int num = 0;
	CVideoLibrary::PartGroupKey(v.path, partKey, num);
	CString stem = SafeName(e.code);
	if (num > 0)
	{
		CString s;
		s.Format(L"_%d", num);
		stem += s;
	}
	const CString ext = ::PathFindExtensionW(v.path);
	e.targetPath = Join(e.targetDir, stem + ext);

	CString probs;
	const CString curDir = DirOf(v.path);
	CString rel = curDir.GetLength() > e.root.GetLength() ? curDir.Mid(e.root.GetLength() + 1) : CString();   // 등록 폴더 아래 폴더 단계
	if (rel != e.actorDir + L"\\" + e.videoDir)
	{
		std::vector<CString> comps;
		int start = 0;
		while (start < rel.GetLength())
		{
			int slash = rel.Find(L'\\', start);
			if (slash < 0)
				slash = rel.GetLength();
			comps.push_back(rel.Mid(start, slash - start));
			start = slash + 1;
		}
		if (comps.size() == 2)
		{
			if (comps[0] != e.actorDir)
				probs += L"· 배우 폴더: '" + comps[0] + L"' → '" + e.actorDir + L"'\r\n";
			if (comps[1] != e.videoDir)
				probs += L"· 영상 폴더: '" + comps[1] + L"' → '" + e.videoDir + L"'\r\n";
		}
		else
			probs += L"· 폴더 위치: '" + (rel.IsEmpty() ? CString(L"(등록 폴더 바로 아래)") : rel) + L"' → '" + e.actorDir + L"\\" + e.videoDir + L"'\r\n";
	}
	const CString fileName = ::PathFindFileNameW(v.path);
	if (fileName != stem + ext)
		probs += L"· 파일 이름: '" + fileName + L"' → '" + stem + ext + L"'\r\n";
	e.image = FindImage(v.path);
	bool imageMissing = false;
	if (e.image.IsEmpty())
	{
		if (num <= 1)   // 분할 영상은 첫 파트(또는 대표)만 이미지가 있으면 됨
			imageMissing = true;
	}
	else
	{
		const CString img = ::PathFindFileNameW(e.image);
		const CString iext = ::PathFindExtensionW(img);
		const CString want = (StemOf(img).CompareNoCase(StemOf(fileName)) == 0 || img.CompareNoCase(fileName + iext) == 0) ? stem : SafeName(e.code);
		if (img != want + iext)
			probs += L"· 이미지 이름: '" + img + L"' → '" + want + iext + L"'\r\n";
	}
	if (!probs.IsEmpty())
	{
		e.state = ST_FIX;
		e.problems = probs;
		if (imageMissing)
			e.problems += L"· 이미지 없음 (자동으로 만들 수 없음)\r\n";
	}
	else if (imageMissing)
	{
		e.state = ST_WARN;
		e.problems = L"· 이미지 없음 (폴더 · 파일 이름은 규칙과 같음)\r\n";
	}
	else
		e.state = ST_OK;
}

void CFileManageDlg::CheckAll()
{
	if (SyncPathCase() > 0)
	{
		m_changed = true;
		m_lib.Save();   // DB 경로 대소문자만 실제로 맞춤
	}
	m_entries.clear();
	m_entryByPath.clear();
	for (size_t i = 0; i < m_lib.items.size(); ++i)
	{
		Entry e;
		CheckItem(static_cast<int>(i), e);
		CString key = m_lib.items[i].path;
		key.MakeLower();
		m_entryByPath[key] = static_cast<int>(m_entries.size());
		m_entries.push_back(e);
	}
	UpdateSummary();
}

void CFileManageDlg::UpdateSummary()
{
	int counts[7] = {};
	for (const Entry& e : m_entries)
		++counts[e.state];
	CString s;
	s.Format(L"규칙 맞음 %d · 고칠 수 있음 %d · 자동 복구 불가 %d · 이미지 없음 %d · 임시 항목 %d · 파일 없음 %d",
		counts[ST_OK], counts[ST_FIX], counts[ST_BLOCK], counts[ST_WARN], counts[ST_PENDING], counts[ST_OFFLINE]);
	SetDlgItemText(IDC_FM_SUMMARY, s);
}

// ---------------------------------------------------------------------------
// 트리

void CFileManageDlg::BuildTree()
{
	m_tree.SetRedraw(FALSE);
	m_tree.DeleteAllItems();
	m_nodes.clear();
	m_nodes.reserve(4096);
	for (const CString& rootRaw : m_lib.folders)
	{
		CString root = rootRaw;
		root.TrimRight(L"\\");
		Node n;
		n.path = root;
		n.dir = true;
		if (!::PathIsDirectoryW(root))
		{
			n.offline = true;
			m_nodes.push_back(n);
			HTREEITEM h = m_tree.InsertItem(root + L"  (연결 안 됨)", TVI_ROOT, TVI_LAST);
			m_tree.SetItemData(h, m_nodes.size() - 1);
			continue;
		}
		m_nodes.push_back(n);
		const size_t idx = m_nodes.size() - 1;
		HTREEITEM h = m_tree.InsertItem(root, TVI_ROOT, TVI_LAST);
		m_tree.SetItemData(h, idx);
		const int bad = AddDir(h, root, 0);
		m_nodes[idx].bad = bad;
		if (bad > 0)
		{
			CString t;
			t.Format(L"%s  (문제 %d)", static_cast<LPCWSTR>(root), bad);
			m_tree.SetItemText(h, t);
		}
		m_tree.Expand(h, TVE_EXPAND);
	}
	m_tree.SetRedraw(TRUE);
	m_tree.Invalidate();
}

int CFileManageDlg::AddDir(HTREEITEM parent, const CString& dir, int depth)
{
	// 하위 폴더 + 영상 파일 (문제 있는 것만 보기면 문제 없는 영상 · 폴더는 뺌) - 반환: 아래의 문제 영상 수
	if (depth > 12)
		return 0;
	const bool onlyBad = (m_onlyBad.GetCheck() == BST_CHECKED);
	std::vector<CString> subs, files;
	ListDir(dir, subs, files);
	int bad = 0;
	for (const CString& s : subs)
	{
		if (s.CompareNoCase(L"$RECYCLE.BIN") == 0 || s.CompareNoCase(L"System Volume Information") == 0)
			continue;
		Node n;
		n.path = Join(dir, s);
		n.dir = true;
		m_nodes.push_back(n);
		const size_t idx = m_nodes.size() - 1;
		HTREEITEM h = m_tree.InsertItem(s, parent, TVI_LAST);
		m_tree.SetItemData(h, idx);
		const int b = AddDir(h, Join(dir, s), depth + 1);
		m_nodes[idx].bad = b;
		if (onlyBad && b == 0)
		{
			m_tree.DeleteItem(h);
			continue;
		}
		if (b > 0)
		{
			CString t;
			t.Format(L"%s  (문제 %d)", static_cast<LPCWSTR>(s), b);
			m_tree.SetItemText(h, t);
			if (onlyBad)
				m_tree.Expand(h, TVE_EXPAND);
		}
		bad += b;
	}
	for (const CString& f : files)
	{
		const CString path = Join(dir, f);
		if (!CVideoLibrary::IsVideoFile(path))
			continue;
		CString key = path;
		key.MakeLower();
		auto it = m_entryByPath.find(key);
		Node n;
		n.path = path;
		n.entry = (it != m_entryByPath.end()) ? it->second : -1;
		CString text = f;
		const State st = (n.entry >= 0) ? m_entries[n.entry].state : ST_NODB;
		switch (st)
		{
		case ST_FIX:
		{
			const Entry& e = m_entries[n.entry];
			text += L"   → " + e.actorDir + L"\\" + e.videoDir + L"\\" + CString(::PathFindFileNameW(e.targetPath));
			n.bad = 1;
			break;
		}
		case ST_BLOCK:
		{
			CString first = m_entries[n.entry].problems;
			const int nl = first.Find(L"\r\n");
			if (nl > 0)
				first = first.Left(nl);
			first.TrimLeft(L"· ");
			text += L"   ✕ " + first;
			n.bad = 1;
			break;
		}
		case ST_WARN:
			text += L"   ! 이미지 없음";
			n.bad = 1;
			break;
		case ST_PENDING:
			text += L"   (임시 항목)";
			break;
		case ST_NODB:
			text += L"   (DB 에 없음)";
			break;
		default:
			break;
		}
		if (onlyBad && n.bad == 0)
			continue;
		m_nodes.push_back(n);
		HTREEITEM h = m_tree.InsertItem(text, parent, TVI_LAST);
		m_tree.SetItemData(h, m_nodes.size() - 1);
		bad += n.bad;
	}
	return bad;
}

void CFileManageDlg::OnTreeCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMTVCUSTOMDRAW* cd = reinterpret_cast<NMTVCUSTOMDRAW*>(pNMHDR);
	*pResult = CDRF_DODEFAULT;
	if (cd->nmcd.dwDrawStage == CDDS_PREPAINT)
	{
		*pResult = CDRF_NOTIFYITEMDRAW;
		return;
	}
	if (cd->nmcd.dwDrawStage != CDDS_ITEMPREPAINT)
		return;
	const size_t idx = static_cast<size_t>(cd->nmcd.lItemlParam);
	if (idx >= m_nodes.size())
		return;
	const Node& n = m_nodes[idx];
	COLORREF col = CLR_INVALID;
	if (n.offline)
		col = kColNoDb;
	else if (n.dir)
		col = (n.bad > 0) ? kColFix : CLR_INVALID;
	else if (n.entry < 0)
		col = kColNoDb;
	else
	{
		switch (m_entries[n.entry].state)
		{
		case ST_FIX:     col = kColFix; break;
		case ST_BLOCK:   col = kColBlock; break;
		case ST_WARN:    col = kColWarn; break;
		case ST_PENDING: col = kColNoDb; break;
		default: break;
		}
	}
	if (col != CLR_INVALID && !(cd->nmcd.uItemState & CDIS_SELECTED))
		cd->clrText = col;
}

void CFileManageDlg::OnTreeRClick(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	// 오른쪽 클릭한 항목을 고르고 메뉴: [폴더 열기] (파일이면 그 파일이 있는 폴더) · [탐색기에서 보기] (파일 선택) · [이 항목 고치기]
	*pResult = 1;   // 기본 처리(WM_CONTEXTMENU) 안 함
	const DWORD pos = ::GetMessagePos();
	CPoint screen(GET_X_LPARAM(pos), GET_Y_LPARAM(pos));
	CPoint pt = screen;
	m_tree.ScreenToClient(&pt);
	UINT flags = 0;
	HTREEITEM h = m_tree.HitTest(pt, &flags);
	if (!h)
		return;
	m_tree.SelectItem(h);
	const size_t idx = static_cast<size_t>(m_tree.GetItemData(h));
	if (idx >= m_nodes.size())
		return;
	const Node n = m_nodes[idx];   // 메뉴 처리 중 트리를 다시 만들 수 있으므로 사본
	const CString folder = n.dir ? n.path : DirOf(n.path);
	std::vector<int> fixable;
	CollectFixable(h, fixable, false, false);
	std::vector<int> keepable;   // 배우 여러 명이라 복구 불가 → 배우 폴더 그대로 두고 아래만 맞출 수 있는 영상
	CollectKeepActor(h, keepable);

	enum { CMD_OPEN = 1, CMD_SELECT, CMD_FIX, CMD_KEEPACTOR };
	CMenu menu;
	menu.CreatePopupMenu();
	const UINT off = (n.offline || !::PathIsDirectoryW(folder)) ? MF_GRAYED : 0;
	menu.AppendMenu(MF_STRING | off, CMD_OPEN, n.dir ? L"폴더 열기" : L"폴더 열기 (파일이 있는 폴더)");
	if (!n.dir)
		menu.AppendMenu(MF_STRING | (::PathFileExistsW(n.path) ? 0 : MF_GRAYED), CMD_SELECT, L"탐색기에서 보기 (파일 선택)");
	menu.AppendMenu(MF_SEPARATOR);
	CString fixText;
	if (n.dir)
		fixText.Format(L"이 폴더 아래 고치기 (%d개)", static_cast<int>(fixable.size()));
	else
		fixText = L"이 항목 고치기";
	menu.AppendMenu(MF_STRING | (fixable.empty() ? MF_GRAYED : 0), CMD_FIX, fixText);
	if (!keepable.empty())
	{
		CString keepText;
		if (n.dir)
			keepText.Format(L"배우 여러 명 영상: 배우 폴더는 그대로 두고 아래만 맞추기 (%d개)", static_cast<int>(keepable.size()));
		else
			keepText = L"배우 폴더는 그대로 두고 아래만 맞추기 ([발매일]품번\\품번)";
		menu.AppendMenu(MF_STRING, CMD_KEEPACTOR, keepText);
	}
	const UINT cmd = DarkMenu::Track(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, screen.x, screen.y, this);
	switch (cmd)
	{
	case CMD_OPEN:
		::ShellExecuteW(GetSafeHwnd(), L"open", folder, nullptr, nullptr, SW_SHOWNORMAL);
		break;
	case CMD_SELECT:
	{
		// 탐색기를 열고 그 파일을 선택
		PIDLIST_ABSOLUTE pidl = nullptr;
		if (SUCCEEDED(::SHParseDisplayName(n.path, nullptr, &pidl, 0, nullptr)) && pidl)
		{
			::SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
			::CoTaskMemFree(pidl);
		}
		else
			::ShellExecuteW(GetSafeHwnd(), L"open", folder, nullptr, nullptr, SW_SHOWNORMAL);
		break;
	}
	case CMD_FIX:
		RunFix(fixable, true);
		break;
	case CMD_KEEPACTOR:
		RunFix(keepable, true, true);
		break;
	default:
		break;
	}
}

void CFileManageDlg::CollectKeepActor(HTREEITEM h, std::vector<int>& items)
{
	// h 와 그 아래: 배우 여러 명이라 "자동 복구 불가" 인 영상 중, 배우 폴더를 그대로 두면 고칠 것이 있는 영상
	const size_t idx = static_cast<size_t>(m_tree.GetItemData(h));
	if (idx < m_nodes.size())
	{
		const Node& n = m_nodes[idx];
		if (!n.dir && n.entry >= 0 && m_entries[n.entry].state == ST_BLOCK)
		{
			const int item = m_entries[n.entry].item;
			if (CVideoLibrary::SplitList(m_lib.items[item].actors).size() > 1)
			{
				Entry e;
				CheckItem(item, e, true);
				if (e.state == ST_FIX)
					items.push_back(item);
			}
		}
	}
	for (HTREEITEM k = m_tree.GetChildItem(h); k; k = m_tree.GetNextSiblingItem(k))
		CollectKeepActor(k, items);
}

void CFileManageDlg::OnTvnSelChanged(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	*pResult = 0;
	ShowDetail();
}

void CFileManageDlg::ShowDetail()
{
	HTREEITEM h = m_tree.GetSelectedItem();
	const size_t idx = h ? static_cast<size_t>(m_tree.GetItemData(h)) : static_cast<size_t>(-1);
	CString t;
	if (idx >= m_nodes.size())
	{
		t = L"트리에서 폴더나 영상을 고르세요.\r\n\r\n"
			L"규칙: 등록 폴더\\배우\\[YYYY.MM.DD]품번\\품번.mp4 · 품번.jpg\r\n"
			L"(분할 영상은 품번_2.mp4 처럼 순번 유지)\r\n\r\n"
			L"색: 주황 = 고칠 수 있음 · 빨강 = 자동 복구 불가 · 노랑 = 이미지 없음 · 회색 = DB 에 없음 / 임시 항목\r\n\r\n"
			L"체크한 폴더는 그 아래 영상 모두가 대상입니다.";
		m_detail.SetWindowText(t);
		return;
	}
	const Node& n = m_nodes[idx];
	if (n.dir)
	{
		t = L"폴더: " + n.path + L"\r\n\r\n";
		if (n.offline)
			t += L"연결 안 된 폴더입니다.";
		else
		{
			CString s;
			s.Format(L"아래의 문제 있는 영상: %d개", n.bad);
			t += s;
			if (n.bad > 0)
			{
				t += L"\r\n\r\n[선택 항목 고치기] = 이 폴더 아래의 고칠 수 있는 영상 모두\r\n";
				CString list;
				int count = 0;
				AppendFolderProblems(h, list, count);
				t += list;
				if (count > 30)
					t += L"\r\n...";
			}
		}
		m_detail.SetWindowText(t);
		return;
	}
	t = L"파일: " + n.path + L"\r\n\r\n";
	if (n.entry < 0)
	{
		t += L"DB 에 없는 영상입니다 (새로고침하면 임시 항목으로 추가됨).";
		m_detail.SetWindowText(t);
		return;
	}
	const Entry& e = m_entries[n.entry];
	const VideoItem& v = m_lib.items[e.item];
	static const wchar_t* const kState[] = { L"규칙 맞음", L"고칠 수 있음", L"자동 복구 불가", L"이미지 없음 (확인 필요)", L"임시 항목", L"DB 에 없음", L"파일 없음" };
	t += L"상태: " + CString(kState[e.state]) + L"\r\n";
	t += L"DB: 품번 " + (v.code.IsEmpty() ? CString(L"(없음)") : v.code) + L" · 발매일 " + (v.release.IsEmpty() ? CString(L"(없음)") : v.release)
		+ L"\r\n    배우 " + (v.actors.IsEmpty() ? CString(L"(없음)") : v.actors) + L"\r\n";
	if (!e.targetPath.IsEmpty())
		t += L"\r\n규칙 경로:\r\n" + e.targetPath + L"\r\n";
	t += L"\r\n이미지: " + (e.image.IsEmpty() ? CString(L"(없음)") : CString(::PathFindFileNameW(e.image))) + L"\r\n";
	if (!e.problems.IsEmpty())
		t += L"\r\n문제:\r\n" + e.problems;
	if (e.state == ST_BLOCK && CVideoLibrary::SplitList(v.actors).size() > 1)
	{
		// 배우 여러 명: 배우 폴더를 그대로 두면 어떻게 되는지
		Entry k;
		CheckItem(e.item, k, true);
		if (k.state == ST_FIX)
			t += L"\r\n배우 폴더를 그대로 두면:\r\n" + k.targetPath + L"\r\n" + k.problems
				+ L"(오른쪽 클릭 → [배우 폴더는 그대로 두고 아래만 맞추기])\r\n";
		else if (k.state == ST_OK || k.state == ST_WARN)
			t += L"\r\n배우 폴더를 그대로 두면 아래([발매일]품번\\품번)는 규칙과 같음\r\n";
	}
	m_detail.SetWindowText(t);
}

void CFileManageDlg::AppendFolderProblems(HTREEITEM h, CString& out, int& count)
{
	// 폴더 아래 문제 영상마다 "파일 이름" + 문제 줄 (최대 30개)
	for (HTREEITEM c = m_tree.GetChildItem(h); c; c = m_tree.GetNextSiblingItem(c))
	{
		const size_t idx = static_cast<size_t>(m_tree.GetItemData(c));
		if (idx >= m_nodes.size())
			continue;
		const Node& n = m_nodes[idx];
		if (n.dir)
		{
			if (n.bad > 0)
				AppendFolderProblems(c, out, count);
			continue;
		}
		if (n.bad == 0 || n.entry < 0)
			continue;
		if (++count > 30)
			return;
		out += L"\r\n" + CString(::PathFindFileNameW(n.path)) + L"\r\n" + m_entries[n.entry].problems;
	}
}

void CFileManageDlg::OnBnClickedRescan()
{
	CWaitCursor wait;
	CheckAll();
	BuildTree();
	ShowDetail();
}

void CFileManageDlg::OnBnClickedOnlyBad()
{
	AfxGetApp()->WriteProfileInt(L"Settings", L"FileManageOnlyBad", m_onlyBad.GetCheck() == BST_CHECKED ? 1 : 0);
	CWaitCursor wait;
	BuildTree();
	ShowDetail();
}

void CFileManageDlg::CollectFixable(HTREEITEM h, std::vector<int>& items, bool onlyChecked, bool parentChecked)
{
	// h 와 그 아래에서 고칠 수 있는 영상 (onlyChecked: 체크한 영상 · 체크한 폴더 아래 영상만)
	for (HTREEITEM c = h; c; c = nullptr)
	{
		const size_t idx = static_cast<size_t>(m_tree.GetItemData(c));
		const bool checked = parentChecked || (m_tree.GetCheck(c) != FALSE);
		if (idx < m_nodes.size())
		{
			const Node& n = m_nodes[idx];
			if (!n.dir && n.entry >= 0 && m_entries[n.entry].state == ST_FIX && (!onlyChecked || checked))
				items.push_back(m_entries[n.entry].item);
		}
		for (HTREEITEM k = m_tree.GetChildItem(c); k; k = m_tree.GetNextSiblingItem(k))
			CollectFixable(k, items, onlyChecked, checked);
	}
}

void CFileManageDlg::OnBnClickedFixOne()
{
	HTREEITEM h = m_tree.GetSelectedItem();
	if (!h)
	{
		AfxMessageBox(L"고칠 폴더나 영상을 트리에서 고르세요.", MB_ICONINFORMATION);
		return;
	}
	std::vector<int> items;
	CollectFixable(h, items, false, false);
	if (items.empty())
	{
		AfxMessageBox(L"고른 항목에는 자동으로 고칠 수 있는 영상이 없습니다.", MB_ICONINFORMATION);
		return;
	}
	RunFix(items, true);
}

void CFileManageDlg::OnBnClickedFixChecked()
{
	std::vector<int> items;
	for (HTREEITEM r = m_tree.GetRootItem(); r; r = m_tree.GetNextSiblingItem(r))
		CollectFixable(r, items, true, false);
	if (items.empty())
	{
		AfxMessageBox(L"체크한 항목 중 자동으로 고칠 수 있는 영상이 없습니다.\n(체크한 폴더는 그 아래 영상 모두가 대상)", MB_ICONINFORMATION);
		return;
	}
	RunFix(items, true);
}

void CFileManageDlg::OnBnClickedFixAll()
{
	std::vector<int> items;
	for (const Entry& e : m_entries)
		if (e.state == ST_FIX)
			items.push_back(e.item);
	if (items.empty())
	{
		AfxMessageBox(L"자동으로 고칠 수 있는 영상이 없습니다.", MB_ICONINFORMATION);
		return;
	}
	RunFix(items, true);
}

void CFileManageDlg::RunFix(const std::vector<int>& itemsIn, bool confirm, bool keepActor)
{
	std::vector<int> items = itemsIn;
	std::sort(items.begin(), items.end());
	items.erase(std::unique(items.begin(), items.end()), items.end());
	if (confirm)
	{
		CString msg;
		if (items.size() == 1)
		{
			Entry e;
			CheckItem(items[0], e, keepActor);
			msg = CString(keepActor ? L"배우 폴더는 그대로 두고, 그 아래 [발매일]품번 폴더 · 파일 이름만 규칙에 맞춥니다.\n\n지금: " : L"이 영상의 폴더 · 파일을 규칙에 맞게 옮기거나 이름을 바꿉니다.\n\n지금: ")
				+ m_lib.items[items[0]].path + L"\n규칙: " + e.targetPath + L"\n\n계속할까요?";
		}
		else
		{
			msg.Format(L"영상 %d개의 폴더 · 파일을 규칙에 맞게 옮기거나 이름을 바꿉니다.\n(영상 전용 폴더는 스크린샷 · txt 등과 함께 폴더째 옮김)\n\n계속할까요?", static_cast<int>(items.size()));
			if (keepActor)
				msg = L"[배우 폴더 그대로] " + msg;
		}
		if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION) != IDYES)
			return;
	}
	CWaitCursor wait;
	int done = 0;
	CString errors;
	int errCount = 0;
	for (int item : items)
	{
		if (item < 0 || item >= static_cast<int>(m_lib.items.size()))
			continue;
		CString err;
		const CString before = m_lib.items[item].path;
		if (FixItem(item, err, keepActor))
			++done;
		else if (!err.IsEmpty())
		{
			++errCount;
			if (errCount <= 15)
				errors += L"· " + CString(::PathFindFileNameW(before)) + L": " + err + L"\n";
		}
	}
	if (done > 0)
	{
		m_changed = true;
		m_lib.Save();   // 옮긴 경로를 바로 저장
	}
	CheckAll();
	BuildTree();
	ShowDetail();
	CString msg;
	msg.Format(L"고침: %d개", done);
	if (errCount > 0)
	{
		CString s;
		s.Format(L"\n못 고침: %d개\n\n", errCount);
		msg += s + errors;
		if (errCount > 15)
			msg += L"...";
	}
	AfxMessageBox(msg, errCount > 0 ? MB_ICONWARNING : MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// 파일 작업

void CFileManageDlg::Log(const CString& line)
{
	const CString file = Join(CVideoLibrary::GetDataDir(), L"filemanage.log");
	CStdioFile f;
	if (!f.Open(file, CFile::modeCreate | CFile::modeNoTruncate | CFile::modeWrite | CFile::typeBinary | CFile::shareDenyWrite))
		return;
	f.SeekToEnd();
	if (f.GetLength() == 0)
	{
		const BYTE bom[] = { 0xEF, 0xBB, 0xBF };
		f.Write(bom, 3);
	}
	SYSTEMTIME st;
	::GetLocalTime(&st);
	CString s;
	s.Format(L"%04d-%02d-%02d %02d:%02d:%02d\t%s\r\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, static_cast<LPCWSTR>(line));
	CT2A utf8(s, CP_UTF8);
	f.Write(static_cast<const char*>(utf8), static_cast<UINT>(strlen(utf8)));
}

void CFileManageDlg::SetItemPath(int item, const CString& newPath)
{
	CString& p = m_lib.items[item].path;
	if (p == newPath)
		return;
	const CString old = p;
	p = newPath;
	if (m_onPathChanged)
		m_onPathChanged(old, newPath);
}

void CFileManageDlg::UpdatePrefix(const CString& oldDir, const CString& newDir)
{
	// oldDir 아래에 있던 DB 영상 경로를 newDir 아래로
	for (size_t i = 0; i < m_lib.items.size(); ++i)
	{
		const CString& p = m_lib.items[i].path;
		if (CVideoLibrary::IsUnder(p, oldDir))
			SetItemPath(static_cast<int>(i), newDir + p.Mid(oldDir.GetLength()));
	}
}

bool CFileManageDlg::MoveOne(const CString& from, const CString& to, CString& err)
{
	if (from == to)
		return true;
	if (from.CompareNoCase(to) != 0 && ::PathFileExistsW(to))
	{
		err = L"이미 있음: " + to;
		return false;
	}
	if (!::MoveFileExW(from, to, 0))
	{
		CString s;
		s.Format(L"옮기기 실패 (오류 %u): ", ::GetLastError());
		err = s + from;
		return false;
	}
	Log(from + L"\t→\t" + to);
	return true;
}

void CFileManageDlg::RemoveEmptyParents(CString dir, const CString& root)
{
	// 비게 된 폴더를 등록 폴더 바로 아래까지 지움 (비어 있지 않으면 RemoveDirectory 가 실패 → 멈춤)
	while (CVideoLibrary::IsUnder(dir, root))
	{
		if (!::RemoveDirectoryW(dir))
			break;
		Log(L"빈 폴더 삭제\t" + dir);
		dir = DirOf(dir);
	}
}

bool CFileManageDlg::EnsureExactDir(const CString& parent, const CString& name, CString& err)
{
	// parent\name 폴더: 대소문자만 다른 폴더가 있으면 이름을 맞추고(아래 DB 경로도), 없으면 만듦
	const CString want = Join(parent, name);
	WIN32_FIND_DATAW fd = {};
	HANDLE h = ::FindFirstFileW(want, &fd);
	if (h != INVALID_HANDLE_VALUE)
	{
		::FindClose(h);
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			err = L"같은 이름의 파일이 있음: " + want;
			return false;
		}
		const CString actual = Join(parent, fd.cFileName);
		if (actual != want)
		{
			if (!MoveOne(actual, want, err))
				return false;
			UpdatePrefix(actual, want);
		}
		return true;
	}
	if (::SHCreateDirectoryExW(nullptr, want, nullptr) != ERROR_SUCCESS && !::PathIsDirectoryW(want))
	{
		err = L"폴더를 만들 수 없음: " + want;
		return false;
	}
	return true;
}

bool CFileManageDlg::IsDedicatedFolder(const CString& dir, const CString& code, const CString& root, const CString& target) const
{
	// 그 영상 전용 폴더인지: 등록 폴더가 아니고, 폴더 이름에 품번이 있고, 안(하위 폴더 포함)의 영상이 모두 그 품번
	//  (규칙 폴더가 이 폴더 안이거나 이 폴더가 규칙 폴더 안이면 폴더째 옮길 수 없음)
	if (!CVideoLibrary::IsUnder(dir, root) || CVideoLibrary::IsUnder(target, dir) || CVideoLibrary::IsUnder(dir, target))
		return false;
	CString name = ::PathFindFileNameW(dir);
	CString lname = name, lcode = code;
	lname.MakeLower();
	lcode.MakeLower();
	if (lname.Find(lcode) < 0 && CVideoLibrary::ExtractCode(name).CompareNoCase(code) != 0)
		return false;
	std::vector<CString> stack = { dir };
	int videos = 0;
	while (!stack.empty())
	{
		const CString d = stack.back();
		stack.pop_back();
		std::vector<CString> subs, files;
		ListDir(d, subs, files);
		for (const CString& s : subs)
			stack.push_back(Join(d, s));
		for (const CString& f : files)
		{
			if (!CVideoLibrary::IsVideoFile(f))
				continue;
			++videos;
			const CString stem = StemOf(f);
			const CString c = CVideoLibrary::ExtractCode(stem);
			if (c.IsEmpty() ? !StartsNoCase(stem, code) : c.CompareNoCase(code) != 0)
				return false;   // 다른 품번 영상이 있음
		}
	}
	return videos > 0;
}

bool CFileManageDlg::FixItem(int item, CString& err, bool keepActor)
{
	Entry e;
	CheckItem(item, e, keepActor);
	if (e.state == ST_OK || e.state == ST_WARN)
		return true;    // 이미 맞음 (같은 묶음을 고치면서 함께 고쳐짐)
	if (e.state != ST_FIX)
	{
		err = L"자동 복구 대상 아님";
		return false;
	}
	if (m_isBusy && m_isBusy(m_lib.items[item].path))
	{
		err = L"스크린샷 추출 · 미리보기 만들기 중 (끝난 뒤 다시)";
		return false;
	}

	// 같은 폴더의 같은 묶음(분할 영상 파트) DB 영상
	auto groupOf = [this](int it) -> std::vector<int>
	{
		CString key;
		int num = 0;
		CVideoLibrary::PartGroupKey(m_lib.items[it].path, key, num);
		std::vector<int> g;
		for (size_t j = 0; j < m_lib.items.size(); ++j)
		{
			if (m_lib.items[j].pending)
				continue;
			CString k2;
			int n2 = 0;
			CVideoLibrary::PartGroupKey(m_lib.items[j].path, k2, n2);
			if (k2 == key)
				g.push_back(static_cast<int>(j));
		}
		return g;
	};
	std::vector<int> group = groupOf(item);
	for (int g : group)
		if (m_isBusy && m_isBusy(m_lib.items[g].path))
		{
			err = L"같은 묶음 영상이 작업 중 (끝난 뒤 다시)";
			return false;
		}

	// 1) 배우 폴더 (대소문자만 다르면 이름 맞춤, 없으면 만듦)
	if (!EnsureExactDir(e.root, e.actorDir, err))
		return false;

	// 2) 영상 폴더 → 규칙 폴더
	const CString T = e.targetDir;
	const CString C = DirOf(m_lib.items[item].path);
	if (C != T)
	{
		if (C.CompareNoCase(T) == 0)
		{
			if (!MoveOne(C, T, err))   // 대소문자만 다름
				return false;
			UpdatePrefix(C, T);
		}
		else if (IsDedicatedFolder(C, e.code, e.root, T))
		{
			if (!::PathFileExistsW(T))
			{
				if (!MoveOne(C, T, err))   // 폴더째 (스크린샷 · txt · 자막 등 함께)
					return false;
				UpdatePrefix(C, T);
			}
			else
			{
				// 규칙 폴더가 이미 있음: 안의 것을 하나씩 옮김 (같은 이름이 있으면 그것만 남김)
				std::vector<CString> subs, files;
				ListDir(C, subs, files);
				bool anyFail = false;
				for (const CString& s : subs)
				{
					CString e2;
					if (MoveOne(Join(C, s), Join(T, s), e2))
						UpdatePrefix(Join(C, s), Join(T, s));
					else
					{
						anyFail = true;
						err = e2;
					}
				}
				for (const CString& f : files)
				{
					CString e2;
					const CString from = Join(C, f), to = Join(T, f);
					if (MoveOne(from, to, e2))
					{
						for (size_t j = 0; j < m_lib.items.size(); ++j)
							if (m_lib.items[j].path.CompareNoCase(from) == 0)
								SetItemPath(static_cast<int>(j), to);
					}
					else
					{
						anyFail = true;
						err = e2;
					}
				}
				if (anyFail && DirOf(m_lib.items[item].path) != T)
					return false;
				err.Empty();
			}
			RemoveEmptyParents(C, e.root);   // 비게 된 예전 영상 폴더 · 배우 폴더
		}
		else
		{
			// 여러 영상이 있는 폴더: 규칙 폴더를 만들고 이 묶음의 영상 · 이미지 · 같은 이름 파일만 옮김
			if (::SHCreateDirectoryExW(nullptr, T, nullptr) != ERROR_SUCCESS && !::PathIsDirectoryW(T))
			{
				err = L"폴더를 만들 수 없음: " + T;
				return false;
			}
			const CString sharedImage = FindImage(m_lib.items[item].path);
			std::vector<CString> subs, files;
			ListDir(C, subs, files);
			for (int g : group)
			{
				const CString vp = m_lib.items[g].path;
				const CString vname = ::PathFindFileNameW(vp);
				const CString vstem = StemOf(vname);
				for (const CString& f : files)
				{
					// 같은 이름(확장자만 다름) · 확장자 포함 이름(movie.mp4.jpg)
					const bool same = (StemOf(f).CompareNoCase(vstem) == 0) || (StartsNoCase(f, vname + L".") && IsImageExt(::PathFindExtensionW(f)));
					if (!same)
						continue;
					const CString from = Join(C, f);
					if (!::PathFileExistsW(from))
						continue;
					CString e2;
					if (!MoveOne(from, Join(T, f), e2))
					{
						if (from.CompareNoCase(vp) == 0)
						{
							err = e2;
							return false;
						}
						continue;
					}
					if (from.CompareNoCase(vp) == 0)
						SetItemPath(g, Join(T, f));
				}
			}
			// '_' 왼쪽이 같은 이름의 이미지(ABC-123_cover.jpg)는 원래 자리에 남아 있으면 함께
			if (!sharedImage.IsEmpty() && ::PathFileExistsW(sharedImage) && DirOf(sharedImage).CompareNoCase(C) == 0)
			{
				CString e2;
				MoveOne(sharedImage, Join(T, ::PathFindFileNameW(sharedImage)), e2);
			}
			RemoveEmptyParents(C, e.root);   // 비었으면 지움
		}
	}

	// 3) 규칙 폴더 안에서 이름 맞추기: 영상 · 같은 이름 파일 → 품번(_순번), 이미지 → 품번(_순번).확장자
	group = groupOf(item);
	for (int g : group)
	{
		const CString vp = m_lib.items[g].path;
		if (DirOf(vp) != T)
			continue;
		CString key;
		int num = 0;
		CVideoLibrary::PartGroupKey(vp, key, num);
		CString newStem = SafeName(e.code);
		if (num > 0)
		{
			CString s;
			s.Format(L"_%d", num);
			newStem += s;
		}
		const CString vname = ::PathFindFileNameW(vp);
		const CString vstem = StemOf(vname);
		std::vector<CString> subs, files;
		ListDir(T, subs, files);
		for (const CString& f : files)
		{
			CString target;
			if (StemOf(f).CompareNoCase(vstem) == 0)
				target = newStem + CString(::PathFindExtensionW(f));            // 같은 이름 (영상 · 이미지 · 자막 · txt)
			else if (StartsNoCase(f, vname + L".") && IsImageExt(::PathFindExtensionW(f)))
				target = newStem + CString(::PathFindExtensionW(f));            // movie.mp4.jpg → 품번.jpg
			else
				continue;
			if (target == f)
				continue;
			const CString from = Join(T, f), to = Join(T, target);
			CString e2;
			if (MoveOne(from, to, e2))
			{
				if (from.CompareNoCase(vp) == 0)
					SetItemPath(g, to);
			}
			else if (from.CompareNoCase(vp) == 0)
			{
				err = e2;
				return false;
			}
		}
	}
	// 디스크 이름은 이미 맞는데 DB 경로만 대소문자가 다르면 DB 를 실제로
	for (int g : group)
	{
		const CString actual = ActualPath(m_lib.items[g].path);
		if (actual != m_lib.items[g].path && actual.CompareNoCase(m_lib.items[g].path) == 0)
			SetItemPath(g, actual);
	}
	// 대표(품번) 이미지가 없으면 '_' 왼쪽이 품번인 이미지(ABC-123_cover.jpg)를 품번.확장자로
	{
		const CString base = Join(T, SafeName(e.code));
		bool has = false;
		std::vector<CString> subs, files;
		ListDir(T, subs, files);
		for (const CString& f : files)
			if (IsImageExt(::PathFindExtensionW(f)) && StemOf(f) == SafeName(e.code))
				has = true;
		if (!has)
		{
			for (const CString& f : files)
			{
				if (!IsImageExt(::PathFindExtensionW(f)))
					continue;
				CString k = StemOf(f);
				const int us = k.Find(L'_');
				if (us >= 0)
					k = k.Left(us);
				k.Trim();
				if (k.CompareNoCase(e.code) != 0)
					continue;
				CString e2;
				MoveOne(Join(T, f), base + CString(::PathFindExtensionW(f)), e2);
				break;
			}
		}
	}
	Entry after;
	CheckItem(item, after, keepActor);
	if (after.state == ST_FIX)
	{
		err = L"일부만 고침 (이름이 겹치는 파일 등 - 상세 정보 확인)";
		return false;
	}
	return true;
}
