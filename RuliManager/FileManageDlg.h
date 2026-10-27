#pragma once

#include "VideoLibrary.h"
#include "DarkTheme.h"
#include <functional>
#include <map>
#include <vector>

// ---------------------------------------------------------------------------
// 파일 관리 창 ([목록 관리 ▾] → [파일 관리...])
//  - 등록 폴더(폴더 관리)를 실제 폴더 · 영상 파일 트리로 보여 줌
//  - 기본 규칙: 등록 폴더\배우\[YYYY.MM.DD]품번\(품번.mp4, 품번.jpg)
//    · 배우 = DB 배우 이름 그대로 (별칭으로 적혀 있어도 배우 이름), 분할 영상은 품번_2.mp4 처럼 순번 유지
//  - DB 에 저장된 영상은 DB 정보(품번 · 발매일 · 배우) 기준으로 폴더 · 파일 이름이 다른 곳을 표시
//    · 품번 · 발매일 · 배우가 없거나 배우가 여러 명이면 "자동 복구 불가" (표시만)
//  - 고치기: 선택 항목 / 체크한 항목 / 고칠 수 있는 것 모두
//    · 영상 폴더가 그 영상 전용(폴더 이름에 품번, 안의 영상이 모두 그 품번)이면 폴더째 옮김 (스크린샷 · txt · 자막 등 함께)
//    · 아니면 규칙 폴더를 만들고 영상 · 이미지 · 같은 이름 파일(자막 등)만 옮김
//    · 옮긴 뒤 영상 · 이미지 · 같은 이름 파일을 품번으로 이름 바꿈, 비게 된 예전 폴더는 지움
//    · 옮긴 경로는 DB 에 바로 저장, 기록은 <DB 폴더>\filemanage.log
class CFileManageDlg : public CDialogEx
{
public:
	CFileManageDlg(CVideoLibrary& lib, CWnd* pParent = nullptr);

	bool m_changed = false;                                                   // 파일을 옮겼음 (DB 경로가 바뀜)
	std::function<void(const CString& oldPath, const CString& newPath)> m_onPathChanged;   // 영상 경로가 바뀜 (미리보기 파일 등 따라 옮기기)
	std::function<bool(const CString& path)> m_isBusy;                       // 그 영상이 작업 중(스크린샷 · 미리보기 만들기)이면 true → 건너뜀

protected:
	enum State { ST_OK = 0, ST_FIX, ST_BLOCK, ST_WARN, ST_PENDING, ST_NODB, ST_OFFLINE };   // OFFLINE: 파일이 없음 (연결 안 된 디스크 등)
	struct Entry
	{
		int     item = -1;
		State   state = ST_OK;
		CString root;            // 속한 등록 폴더
		CString code;            // 품번
		CString actorDir;        // 배우 폴더 이름 (규칙)
		CString videoDir;        // 영상 폴더 이름 (규칙) [YYYY.MM.DD]품번
		CString targetDir;       // 규칙 폴더 전체 경로
		CString targetPath;      // 규칙 파일 경로
		CString image;           // 지금 이미지 (없으면 빈칸)
		CString problems;        // 문제 (줄마다 "· ...")
	};
	struct Node
	{
		CString path;
		bool    dir = false;
		int     entry = -1;      // m_entries 번호 (영상 파일, DB 에 있을 때)
		int     bad = 0;         // 폴더: 아래의 문제 영상 수 / 영상: 문제 있으면 1
		bool    offline = false; // 등록 폴더가 연결 안 됨
	};

	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();

	CVideoLibrary& m_lib;
	CTreeCtrl m_tree;
	CEdit     m_detail;
	CButton   m_onlyBad;
	CDarkDialogTheme m_theme;
	std::vector<Entry> m_entries;
	std::map<CString, int> m_entryByPath;   // 경로(소문자) → m_entries
	std::vector<Node> m_nodes;               // 트리 항목 데이터 (lParam = 번호)
	CSize m_minSize;
	bool  m_layoutReady = false;

	void   CheckAll();                                   // DB 영상마다 규칙 검사 → m_entries
	void   CheckItem(int item, Entry& e, bool keepActor = false) const;   // keepActor: 배우 폴더는 지금 것 그대로 (배우 여러 명 · 없음도 검사) - 아래 [발매일]품번\품번 만
	void   BuildTree();
	int    AddDir(HTREEITEM parent, const CString& dir, int depth);
	void   ShowDetail();
	void   UpdateSummary();
	void   CollectFixable(HTREEITEM h, std::vector<int>& items, bool onlyChecked, bool parentChecked);
	void   RunFix(const std::vector<int>& items, bool confirm, bool keepActor = false);
	bool   FixItem(int item, CString& err, bool keepActor = false);
	void   CollectKeepActor(HTREEITEM h, std::vector<int>& items);   // 배우 여러 명이라 복구 불가지만 배우 폴더를 그대로 두면 고칠 수 있는 영상
	void   Layout();

	// 파일 작업
	static CString SafeName(const CString& s);
	static CString ActualPath(const CString& path);   // 디스크의 실제 대소문자로 (없으면 그대로)
	int    SyncPathCase();                            // DB 경로가 실제 파일과 대소문자만 다르면 DB 를 실제로 맞춤 - 반환: 맞춘 수
	void   AppendFolderProblems(HTREEITEM h, CString& out, int& count);   // 폴더 상세: 아래 문제 영상의 문제 목록
	static CString DirOf(const CString& path);
	static CString StemOf(const CString& name);
	static CString FindImage(const CString& videoPath);
	static bool    IsImageExt(const CString& ext);
	bool   IsDedicatedFolder(const CString& dir, const CString& code, const CString& root, const CString& target) const;
	void   UpdatePrefix(const CString& oldDir, const CString& newDir);
	void   SetItemPath(int item, const CString& newPath);
	bool   MoveOne(const CString& from, const CString& to, CString& err);
	void   RemoveEmptyParents(CString dir, const CString& root);
	bool   EnsureExactDir(const CString& parent, const CString& name, CString& err);
	void   Log(const CString& line);

	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg void OnBnClickedRescan();
	afx_msg void OnBnClickedOnlyBad();
	afx_msg void OnBnClickedFixOne();
	afx_msg void OnBnClickedFixChecked();
	afx_msg void OnBnClickedFixAll();
	afx_msg void OnTvnSelChanged(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnTreeCustomDraw(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnTreeRClick(NMHDR* pNMHDR, LRESULT* pResult);   // 오른쪽 클릭: 폴더 열기 · 탐색기에서 보기 · 이 항목 고치기
	DECLARE_MESSAGE_MAP()
};
