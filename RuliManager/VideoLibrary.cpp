#include "pch.h"
#include "VideoLibrary.h"
#include "DbCrypt.h"
#include "CountryCombo.h"
#include <cwctype>
#include <string>

namespace
{
	CString Escape(const CString& s)
	{
		CString r;
		r.Preallocate(s.GetLength() + 8);
		for (int i = 0; i < s.GetLength(); ++i)
		{
			const wchar_t c = s[i];
			switch (c)
			{
			case L'\\': r += L"\\\\"; break;
			case L'\t': r += L"\\t";  break;
			case L'\n': r += L"\\n";  break;
			case L'\r': r += L"\\r";  break;
			default:    r += c;       break;
			}
		}
		return r;
	}

	CString Unescape(const CString& s)
	{
		CString r;
		r.Preallocate(s.GetLength());
		for (int i = 0; i < s.GetLength(); ++i)
		{
			const wchar_t c = s[i];
			if (c == L'\\' && i + 1 < s.GetLength())
			{
				const wchar_t n = s[++i];
				switch (n)
				{
				case L't':  r += L'\t'; break;
				case L'n':  r += L'\n'; break;
				case L'r':  r += L'\r'; break;
				default:    r += n;     break;   // "\\" 포함
				}
			}
			else
			{
				r += c;
			}
		}
		return r;
	}

	std::vector<CString> SplitTabs(const CString& line)
	{
		std::vector<CString> out;
		int start = 0;
		for (;;)
		{
			const int p = line.Find(L'\t', start);
			if (p < 0)
			{
				out.push_back(line.Mid(start));
				break;
			}
			out.push_back(line.Mid(start, p - start));
			start = p + 1;
		}
		return out;
	}
}

namespace
{
	bool g_readPlainDb = false;   // 마지막으로 읽은 DB 파일이 암호화되지 않은 예전 형식이었는지
	bool g_decryptFailed = false; // 암호화된 DB 파일을 풀지 못함 (덮어쓰지 않도록)

	// 예전 형식 DB 텍스트 파일 읽기 (암호화된 파일은 복호화, 평문 UTF-8 파일도 읽음 / 없거나 비었으면 false)
	bool ReadUtf8File(const CString& path, CString& text)
	{
		CStringA buf;
		try
		{
			CFile file;
			if (!file.Open(path, CFile::modeRead | CFile::shareDenyWrite))
				return false;
			const ULONGLONG len = file.GetLength();
			if (len == 0 || len > 512ULL * 1024 * 1024)
				return false;
			std::vector<BYTE> raw(static_cast<size_t>(len));
			const UINT read = file.Read(raw.data(), static_cast<UINT>(len));
			raw.resize(read);
			if (DbCrypt::IsEncrypted(raw.data(), raw.size()))
			{
				std::vector<BYTE> plain;
				if (!DbCrypt::Decrypt(raw.data(), raw.size(), plain))
				{
					g_decryptFailed = true;
					return false;   // 손상되었거나 다른 키로 암호화된 파일
				}
				buf = CStringA(reinterpret_cast<const char*>(plain.data()), static_cast<int>(plain.size()));
				::SecureZeroMemory(plain.data(), plain.size());
			}
			else
			{
				buf = CStringA(reinterpret_cast<const char*>(raw.data()), static_cast<int>(raw.size()));
				g_readPlainDb = true;   // 예전 평문 파일 → 다음 저장 때 암호화
			}
		}
		catch (CException* e)
		{
			e->Delete();
			return false;
		}
		int offset = 0;
		if (buf.GetLength() >= 3 && static_cast<BYTE>(buf[0]) == 0xEF &&
			static_cast<BYTE>(buf[1]) == 0xBB && static_cast<BYTE>(buf[2]) == 0xBF)
			offset = 3;
		const CStringA body = buf.Mid(offset);
		text = CString(CA2W(body, CP_UTF8));
		return true;
	}

}

CString CVideoLibrary::GetPendingFilePath()
{
	return GetAppDataDir() + L"\\pending.tsv";
}

CString CVideoLibrary::GetLegacyLibraryPath()
{
	return GetAppDataDir() + L"\\library.tsv";
}

CString CVideoLibrary::GetCacheDir()
{
	CString dir;
	PWSTR p = nullptr;
	if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)) && p)
		dir = p;
	else
		dir = GetDataDir();
	if (p) ::CoTaskMemFree(p);
	dir += L"\\VideoManager";
	::CreateDirectoryW(dir, nullptr);
	dir += L"\\cache";
	::CreateDirectoryW(dir, nullptr);
	return dir;
}

int CVideoLibrary::PendingCount() const
{
	int n = 0;
	for (const VideoItem& v : items)
		if (v.pending) ++n;
	return n;
}

CString CVideoLibrary::GetWorkDbPath()
{
	return GetDataDir() + L"\\library.work.vmdb";
}

namespace
{
	bool SameFileContent(const CString& a, const CString& b);   // 아래에서 정의

	// src 를 dst 로 원자적으로 교체 (임시 파일에 복사한 뒤 바꿔치기)
	bool ReplaceWithCopy(const CString& src, const CString& dst)
	{
		const CString tmp = dst + L".tmp";
		if (!::CopyFileW(src, tmp, FALSE))
			return false;
		if (!::MoveFileExW(tmp, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			::DeleteFileW(tmp);
			return false;
		}
		return true;
	}
}

bool CVideoLibrary::HasLeftoverWorkDb()
{
	const CString work = GetWorkDbPath();
	if (::GetFileAttributesW(work) == INVALID_FILE_ATTRIBUTES)
		return false;
	if (::GetFileAttributesW(GetDataFilePath()) != INVALID_FILE_ATTRIBUTES && SameFileContent(work, GetDataFilePath()))
	{
		::DeleteFileW(work);   // 원본과 같으면 반영할 것이 없음
		return false;
	}
	return true;
}

void CVideoLibrary::DiscardWorkDb()
{
	::DeleteFileW(GetWorkDbPath());
}

bool CVideoLibrary::ApplyLeftoverWorkDb()
{
	const CString work = GetWorkDbPath();
	if (::GetFileAttributesW(work) == INVALID_FILE_ATTRIBUTES)
		return false;
	if (!ReplaceWithCopy(work, GetDataFilePath()))
		return false;
	::DeleteFileW(work);
	return true;
}

bool CVideoLibrary::ApplyWorkDb()
{
	if (m_dbBroken)
		return false;   // DB 를 읽지 못한 실행에서는 원본을 건드리지 않음
	if (!m_workDirty)
		return true;    // 실행 후 저장한 적 없음 = 반영할 것 없음
	const CString work = GetWorkDbPath();
	if (::GetFileAttributesW(work) == INVALID_FILE_ATTRIBUTES)
	{
		m_workDirty = false;
		return true;    // 저장한 적 없음 = 반영할 것 없음
	}
	if (!ReplaceWithCopy(work, GetDataFilePath()))
		return false;
	m_workDirty = false;
	if (m_onSaved)
		m_onSaved();
	return true;
}

CString CVideoLibrary::GetDataFilePath()
{
	return GetDataDir() + L"\\library.vmdb";
}

CString CVideoLibrary::GetDataDir()
{
	// 실행 파일과 같은 폴더 (Program Files 처럼 쓸 수 없는 곳이면 %APPDATA%\VideoManager)
	static CString cached;
	if (!cached.IsEmpty())
		return cached;
	wchar_t exe[MAX_PATH * 2] = {};
	::GetModuleFileNameW(nullptr, exe, _countof(exe));
	CString dir = exe;
	const int slash = dir.ReverseFind(L'\\');
	if (slash > 0)
		dir = dir.Left(slash);
	// 쓰기 가능한지 확인
	const CString probe = dir + L"\\~vm_write_test.tmp";
	HANDLE h = ::CreateFileW(probe, GENERIC_WRITE | DELETE, 0, nullptr, CREATE_ALWAYS,
		FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
	if (h != INVALID_HANDLE_VALUE)
	{
		::CloseHandle(h);
		cached = dir;
	}
	else
		cached = GetAppDataDir();
	return cached;
}

CString CVideoLibrary::GetAppDataDir()
{
	CString dir;
	PWSTR p = nullptr;
	if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p)) && p)
	{
		dir = p;
	}
	else
	{
		dir = L".";
	}
	if (p) ::CoTaskMemFree(p);

	dir += L"\\VideoManager";
	::CreateDirectoryW(dir, nullptr);
	return dir;
}

namespace
{
	// 저장 파일에는 Image 폴더 안의 이미지를 상대 경로("actors\\...")로 기록 → 실행 폴더째 옮겨도 그대로
	CString ToStoredPath(const CString& path)
	{
		const CString dir = CVideoLibrary::GetImageRoot();
		if (!path.IsEmpty() && CVideoLibrary::IsUnder(path, dir))
			return path.Mid(dir.GetLength() + 1);
		return path;
	}

	CString FromStoredPath(const CString& path)
	{
		if (!path.IsEmpty() && ::PathIsRelativeW(path))
		{
			// 예전 이미지 DB 시절의 "images\\actors\\..." 도 Image 폴더 기준으로
			CString rel = path;
			if (rel.GetLength() > 7 && _wcsnicmp(rel, L"images\\", 7) == 0)
				rel = rel.Mid(7);
			return CVideoLibrary::GetImageRoot() + L"\\" + rel;
		}
		return path;
	}
}

CString CVideoLibrary::GetImageRoot()
{
	const CString dir = GetDataDir() + L"\\Image";
	::CreateDirectoryW(dir, nullptr);
	return dir;
}

CString CVideoLibrary::GetImageStoreDir(LPCWSTR sub)
{
	CString dir = GetImageRoot();
	::CreateDirectoryW(dir, nullptr);
	if (sub && *sub)
	{
		dir += L"\\";
		dir += sub;
		::CreateDirectoryW(dir, nullptr);
	}
	return dir;
}

bool CVideoLibrary::IsInImageStore(const CString& path)
{
	return !path.IsEmpty() && IsUnder(path, GetImageRoot());
}

namespace
{
	bool ReadWholeFile(const CString& path, std::vector<BYTE>& data);   // 아래에서 정의
}

CString CVideoLibrary::StoreImageCopy(const CString& src, LPCWSTR sub, bool force)
{
	if (src.IsEmpty())
		return CString();
	if (!force && IsInImageStore(src))
		return src;                  // 이미 보관소에 있는 복사본
	if (!::PathFileExistsW(src))
		return CString();

	// 원본을 읽어서 암호화한 사본(이름.확장자.vmimg)으로 저장
	std::vector<BYTE> data, enc;
	if (!ReadWholeFile(src, data) || data.empty())
		return CString();
	if (DbCrypt::IsEncrypted(data.data(), data.size()))
		enc.swap(data);                         // 이미 암호화된 파일
	else if (!DbCrypt::Encrypt(data.data(), data.size(), enc))
		return CString();
	::SecureZeroMemory(data.data(), data.size());

	const CString dir = GetImageStoreDir(sub);
	CString stem = ::PathFindFileNameW(src);
	CString ext = ::PathFindExtensionW(src);
	if (ext.CompareNoCase(L".vmimg") == 0)
	{
		// 암호화 사본을 다시 등록: 안쪽 확장자 사용
		::PathRemoveExtensionW(stem.GetBuffer());
		stem.ReleaseBuffer();
		ext = ::PathFindExtensionW(stem);
	}
	::PathRemoveExtensionW(stem.GetBuffer());
	stem.ReleaseBuffer();
	if (stem.GetLength() > 40)
		stem = stem.Left(40);
	ext.MakeLower();

	SYSTEMTIME st = {};
	::GetLocalTime(&st);
	for (int n = 0; n < 1000; ++n)
	{
		CString name;
		name.Format(L"%04d%02d%02d-%02d%02d%02d-%03d", st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
		if (n > 0)
			name.AppendFormat(L"-%d", n);
		const CString dst = dir + L"\\" + name + L"_" + stem + ext + L".vmimg";
		HANDLE h = ::CreateFileW(dst, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);   // 같은 이름이 있으면 실패 → 다음 번호
		if (h != INVALID_HANDLE_VALUE)
		{
			DWORD written = 0;
			const bool ok = ::WriteFile(h, enc.data(), static_cast<DWORD>(enc.size()), &written, nullptr) && written == enc.size();
			::CloseHandle(h);
			if (ok)
				return dst;
			::DeleteFileW(dst);
			return CString();
		}
		const DWORD err = ::GetLastError();
		if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS)
			return CString();
	}
	return CString();
}

bool CVideoLibrary::MigrateImagesToStore()
{
	bool changed = false;
	auto fix = [&changed](CString& path, LPCWSTR sub)
	{
		if (path.IsEmpty() || IsInImageStore(path) || !::PathFileExistsW(path))
			return;                  // 비었거나 이미 복사본이거나 원본이 없음
		const CString copy = StoreImageCopy(path, sub);
		if (!copy.IsEmpty())
		{
			path = copy;
			changed = true;
		}
	};
	for (ActorInfo& a : actors)
		fix(a.photo, L"actors");
	for (NamedInfo& n : studios)
		fix(n.image, L"studios");
	for (NamedInfo& n : labelInfos)
		fix(n.image, L"studios");   // 레이블 이미지도 제작사 폴더에
	// 태그는 이미지 없음
	return changed;
}

bool CVideoLibrary::IsEncryptedFile(const CString& path)
{
	BYTE head[16] = {};
	DWORD read = 0;
	HANDLE h = ::CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	const BOOL ok = ::ReadFile(h, head, sizeof(head), &read, nullptr);
	::CloseHandle(h);
	return ok && DbCrypt::IsEncrypted(head, read);
}

bool CVideoLibrary::EncryptImageStore(std::vector<CString>& plainFiles)
{
	bool changed = false;
	auto fix = [&](CString& path, LPCWSTR sub)
	{
		if (path.IsEmpty() || !IsInImageStore(path) || !::PathFileExistsW(path) || IsEncryptedFile(path))
			return;
		const CString enc = StoreImageCopy(path, sub, true);   // 보관소 안 평문 → 암호화 사본 (.vmimg)
		if (enc.IsEmpty())
			return;
		plainFiles.push_back(path);   // 평문은 저장이 끝난 뒤 지움
		path = enc;
		changed = true;
	};
	for (ActorInfo& a : actors)
		fix(a.photo, L"actors");
	for (NamedInfo& n : studios)
		fix(n.image, L"studios");
	for (NamedInfo& n : labelInfos)
		fix(n.image, L"studios");
	return changed;
}

int CVideoLibrary::CleanupImageStore() const
{
	std::set<CString> used;
	for (const ActorInfo& a : actors)
		if (!a.photo.IsEmpty()) used.insert(MakeKey(a.photo));
	for (int kind = LIST_STUDIO; kind <= LIST_TAG; ++kind)
		for (const NamedInfo& n : NamedList(kind))
			if (!n.image.IsEmpty()) used.insert(MakeKey(n.image));
	for (const NamedInfo& n : labelInfos)
		if (!n.image.IsEmpty()) used.insert(MakeKey(n.image));

	// 보관소 하위 폴더의 파일 중 연결되지 않은 것 모으기
	std::vector<CString> unused;
	const CString root = GetImageStoreDir();
	const LPCWSTR subs[] = { L"actors", L"studios" };   // 태그는 이미지 없음
	for (LPCWSTR sub : subs)
	{
		const CString dir = root + L"\\" + sub;
		WIN32_FIND_DATAW fd = {};
		HANDLE h = ::FindFirstFileW(dir + L"\\*", &fd);
		if (h == INVALID_HANDLE_VALUE)
			continue;
		do
		{
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;
			const CString path = dir + L"\\" + fd.cFileName;
			if (used.find(MakeKey(path)) == used.end())
				unused.push_back(path);
		} while (::FindNextFileW(h, &fd));
		::FindClose(h);
	}
	if (unused.empty())
		return 0;

	// 캐시(DB 파일에서 풀어 둔 것)이므로 바로 지움 → 다음 저장 때 DB 파일에서도 빠짐
	int removed = 0;
	for (const CString& path : unused)
		if (::DeleteFileW(path))
			++removed;
	return removed;
}

bool CVideoLibrary::IsVideoFile(LPCWSTR path)
{
	static const wchar_t* const kExts[] = {
		L".mp4", L".m4v", L".mkv", L".avi", L".wmv", L".mov", L".flv", L".webm",
		L".mpg", L".mpeg", L".ts", L".m2ts", L".mts", L".3gp", L".asf", L".vob"
	};
	LPCWSTR ext = ::PathFindExtensionW(path);
	for (const wchar_t* e : kExts)
	{
		if (_wcsicmp(ext, e) == 0)
			return true;
	}
	return false;
}

CString CVideoLibrary::MakeKey(const CString& path)
{
	CString k = path;
	k.MakeLower();
	return k;
}

bool CVideoLibrary::IsUnder(const CString& path, const CString& folder)
{
	CString prefix = folder;
	if (prefix.Right(1) != L"\\")
		prefix += L"\\";
	return path.GetLength() > prefix.GetLength() &&
		_wcsnicmp(path, prefix, prefix.GetLength()) == 0;
}

int CVideoLibrary::FindByPath(const CString& path) const
{
	for (size_t i = 0; i < items.size(); ++i)
	{
		if (items[i].path.CompareNoCase(path) == 0)
			return static_cast<int>(i);
	}
	return -1;
}

// ---------------------------------------------------------------------------
// 하나로 묶은 DB 파일 (library.vmdb)
//   "VMDB-PACK1" + [u64 길이 + 정보 묶음(암호화)] + [u64 길이 + 이미지 묶음(암호화)]
//   묶음 = 반복 { u32 이름길이, 이름(UTF-8), u64 데이터길이, 데이터 }

namespace
{
	const char kPackMagic[] = "VMDB-PACK1";
	const size_t kPackMagicLen = sizeof(kPackMagic) - 1;

	void PutEntry(std::vector<BYTE>& out, const CStringA& name, const BYTE* data, size_t len)
	{
		const UINT32 nl = static_cast<UINT32>(name.GetLength());
		const UINT64 dl = static_cast<UINT64>(len);
		const size_t at = out.size();
		out.resize(at + 4 + nl + 8 + len);
		memcpy(out.data() + at, &nl, 4);
		memcpy(out.data() + at + 4, static_cast<LPCSTR>(name), nl);
		memcpy(out.data() + at + 4 + nl, &dl, 8);
		if (len)
			memcpy(out.data() + at + 4 + nl + 8, data, len);
	}

	struct Entry { CStringA name; size_t offset = 0; size_t length = 0; };
	bool ParseEntries(const std::vector<BYTE>& in, std::vector<Entry>& out)
	{
		size_t p = 0;
		while (p < in.size())
		{
			if (p + 4 > in.size()) return false;
			UINT32 nl = 0;
			memcpy(&nl, in.data() + p, 4);
			p += 4;
			if (nl > 4096 || p + nl + 8 > in.size()) return false;
			Entry e;
			e.name = CStringA(reinterpret_cast<const char*>(in.data() + p), static_cast<int>(nl));
			p += nl;
			UINT64 dl = 0;
			memcpy(&dl, in.data() + p, 8);
			p += 8;
			if (dl > in.size() - p) return false;
			e.offset = p;
			e.length = static_cast<size_t>(dl);
			p += e.length;
			out.push_back(e);
		}
		return true;
	}

	bool ReadWholeFile(const CString& path, std::vector<BYTE>& data)
	{
		try
		{
			CFile f;
			if (!f.Open(path, CFile::modeRead | CFile::shareDenyWrite))
				return false;
			const ULONGLONG len = f.GetLength();
			if (len > 0x7FFFFFFFULL)
				return false;
			data.resize(static_cast<size_t>(len));
			const UINT read = len ? f.Read(data.data(), static_cast<UINT>(len)) : 0;
			data.resize(read);
			return true;
		}
		catch (CException* e)
		{
			e->Delete();
			return false;
		}
	}

	// 캐시의 이미지 파일 목록 (images\<sub>\파일)
	void ListCacheImages(std::vector<CString>& files, std::vector<WIN32_FIND_DATAW>* infos)
	{
		const CString root = CVideoLibrary::GetImageRoot();
		const LPCWSTR subs[] = { L"actors", L"studios" };   // 태그는 이미지 없음
		for (LPCWSTR sub : subs)
		{
			const CString dir = root + L"\\" + sub;
			WIN32_FIND_DATAW fd = {};
			HANDLE h = ::FindFirstFileW(dir + L"\\*", &fd);
			if (h == INVALID_HANDLE_VALUE)
				continue;
			do
			{
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
					continue;
				files.push_back(CString(L"images\\") + sub + L"\\" + fd.cFileName);
				if (infos) infos->push_back(fd);
			} while (::FindNextFileW(h, &fd));
			::FindClose(h);
		}
	}

	// 묶음 안의 이름이 안전한지 (images/<sub>/<파일> 만 허용)
	bool SafeImageName(const CString& rel)
	{
		if (rel.Find(L"..") >= 0 || rel.Find(L':') >= 0 || rel.Left(1) == L"\\")
			return false;
		const LPCWSTR subs[] = { L"images\\actors\\", L"images\\studios\\" };   // 예전 태그 이미지는 풀지 않음
		for (LPCWSTR s : subs)
		{
			const int n = static_cast<int>(wcslen(s));
			if (rel.GetLength() > n && _wcsnicmp(rel, s, n) == 0 && rel.Mid(n).FindOneOf(L"\\/") < 0)
				return true;
		}
		return false;
	}

	// 폴더 안의 파일을 지움 (하위 폴더 포함)
	void DeleteTree(const CString& dir, bool removeSelf)
	{
		WIN32_FIND_DATAW fd = {};
		HANDLE h = ::FindFirstFileW(dir + L"\\*", &fd);
		if (h != INVALID_HANDLE_VALUE)
		{
			do
			{
				const CString name = fd.cFileName;
				if (name == L"." || name == L"..")
					continue;
				const CString full = dir + L"\\" + name;
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
					DeleteTree(full, true);
				else
				{
					::SetFileAttributesW(full, FILE_ATTRIBUTE_NORMAL);
					::DeleteFileW(full);
				}
			} while (::FindNextFileW(h, &fd));
			::FindClose(h);
		}
		if (removeSelf)
			::RemoveDirectoryW(dir);
	}

	// 예전 images 폴더 → 캐시로 복사
	void CopyTree(const CString& src, const CString& dst)
	{
		::CreateDirectoryW(dst, nullptr);
		WIN32_FIND_DATAW fd = {};
		HANDLE h = ::FindFirstFileW(src + L"\\*", &fd);
		if (h == INVALID_HANDLE_VALUE)
			return;
		do
		{
			const CString name = fd.cFileName;
			if (name == L"." || name == L"..")
				continue;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				CopyTree(src + L"\\" + name, dst + L"\\" + name);
			else
				::CopyFileW(src + L"\\" + name, dst + L"\\" + name, FALSE);
		} while (::FindNextFileW(h, &fd));
		::FindClose(h);
	}
}

void CVideoLibrary::ClearImageCache()
{
	DeleteTree(GetCacheDir() + L"\\images", true);
}

CString CVideoLibrary::ImageSignature(std::vector<CString>* files)
{
	std::vector<CString> names;
	std::vector<WIN32_FIND_DATAW> infos;
	ListCacheImages(names, &infos);
	CString sig;
	for (size_t i = 0; i < names.size(); ++i)
		sig.AppendFormat(L"%s|%u|%u|%u;", static_cast<LPCWSTR>(names[i]), infos[i].nFileSizeLow,
			infos[i].ftLastWriteTime.dwLowDateTime, infos[i].ftLastWriteTime.dwHighDateTime);
	if (files)
		*files = names;
	return sig;
}

namespace
{
	const char kImgMagic[] = "VMDB-IMGS1";
	const size_t kImgMagicLen = sizeof(kImgMagic) - 1;

	// 예전 이미지 DB(암호화된 이미지 묶음)를 풀어서 실행 폴더의 Image 폴더에 파일로 씀 (이미 있는 파일은 그대로)
	bool UnpackImages(const std::vector<BYTE>& sec)
	{
		if (sec.empty())
			return true;
		std::vector<BYTE> plain;
		std::vector<Entry> imgs;
		if (!DbCrypt::Decrypt(sec.data(), sec.size(), plain) || !ParseEntries(plain, imgs))
			return false;
		const CString root = CVideoLibrary::GetImageRoot();
		for (const Entry& e : imgs)
		{
			const CString rel = CString(CA2W(e.name, CP_UTF8));
			if (!SafeImageName(rel))
				continue;
			const CString dst = root + L"\\" + rel.Mid(7);   // "images\\" 를 뗀 actors\\... 경로
			if (::PathFileExistsW(dst))
				continue;
			CFile f;
			if (f.Open(dst, CFile::modeCreate | CFile::modeWrite))
			{
				f.Write(plain.data() + e.offset, static_cast<UINT>(e.length));
				f.Close();
			}
		}
		::SecureZeroMemory(plain.data(), plain.size());
		return true;
	}
}

CString CVideoLibrary::GetImagesDbPath()
{
	return GetDataDir() + L"\\images.vmdb";
}

CString CVideoLibrary::GetBackupDir()
{
	const CString dir = GetDataDir() + L"\\backup";
	::CreateDirectoryW(dir, nullptr);
	return dir;
}

namespace
{
	// 두 파일의 내용이 같은지 (크기 먼저 비교 후 바이트 비교)
	bool SameFileContent(const CString& a, const CString& b)
	{
		WIN32_FILE_ATTRIBUTE_DATA fa = {}, fb = {};
		if (!::GetFileAttributesExW(a, GetFileExInfoStandard, &fa) || !::GetFileAttributesExW(b, GetFileExInfoStandard, &fb))
			return false;
		if (fa.nFileSizeHigh != fb.nFileSizeHigh || fa.nFileSizeLow != fb.nFileSizeLow)
			return false;
		CFile f1, f2;
		if (!f1.Open(a, CFile::modeRead | CFile::shareDenyNone) || !f2.Open(b, CFile::modeRead | CFile::shareDenyNone))
			return false;
		std::vector<BYTE> b1(64 * 1024), b2(64 * 1024);
		for (;;)
		{
			const UINT n1 = f1.Read(b1.data(), static_cast<UINT>(b1.size()));
			const UINT n2 = f2.Read(b2.data(), static_cast<UINT>(b2.size()));
			if (n1 != n2 || memcmp(b1.data(), b2.data(), n1) != 0)
				return false;
			if (n1 == 0)
				return true;
		}
	}

	// backup 폴더에서 prefix_*.vmdb 파일 이름 목록 (이름 = 날짜·시각 순)
	std::vector<CString> ListBackups(const CString& dir, const CString& prefix)
	{
		std::vector<CString> names;
		WIN32_FIND_DATAW fd = {};
		HANDLE h = ::FindFirstFileW(dir + L"\\" + prefix + L"_*.vmdb", &fd);
		if (h != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
					names.push_back(fd.cFileName);
			} while (::FindNextFileW(h, &fd));
			::FindClose(h);
		}
		std::sort(names.begin(), names.end(), [](const CString& x, const CString& y) { return x.CompareNoCase(y) < 0; });
		return names;
	}
}

int CVideoLibrary::BackupDbFiles(int keep)
{
	keep = (std::max)(1, keep);
	SYSTEMTIME st = {};
	::GetLocalTime(&st);
	CString stamp;
	stamp.Format(L"%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

	int made = 0;
	const struct { CString src; LPCWSTR prefix; } files[] = {
		{ GetDataFilePath(), L"library" },   // 이미지는 Image 폴더에 파일로 있으므로 정보 DB 만
	};
	CString dir;
	for (const auto& f : files)
	{
		if (!::PathFileExistsW(f.src))
			continue;
		if (dir.IsEmpty())
			dir = GetBackupDir();
		std::vector<CString> names = ListBackups(dir, f.prefix);
		// 가장 최근 백업과 같으면 새로 만들지 않음 (그냥 켜고 끄기만 한 경우)
		if (!names.empty() && SameFileContent(f.src, dir + L"\\" + names.back()))
			continue;
		const CString dst = dir + L"\\" + f.prefix + L"_" + stamp + L".vmdb";
		if (::CopyFileW(f.src, dst, FALSE))
		{
			++made;
			names.push_back(CString(f.prefix) + L"_" + stamp + L".vmdb");
		}
		// 오래된 백업 정리 (최근 keep 개만)
		while (static_cast<int>(names.size()) > keep)
		{
			::DeleteFileW(dir + L"\\" + names.front());
			names.erase(names.begin());
		}
	}
	return made;
}

bool CVideoLibrary::ReadPackedDb(CString& text, CString& pending)
{
	// library.vmdb: "VMDB-PACK1" + [정보 묶음] + [이미지 묶음 - 예전 한 파일 형식일 때만, 지금은 길이 0]
	std::vector<BYTE> raw;
	if (!ReadWholeFile(GetWorkDbPath(), raw) || raw.size() < kPackMagicLen + 16 ||
		memcmp(raw.data(), kPackMagic, kPackMagicLen) != 0)
	{
		g_decryptFailed = true;
		return false;
	}
	size_t p = kPackMagicLen;
	auto section = [&](std::vector<BYTE>& sec) -> bool
	{
		if (p + 8 > raw.size()) return false;
		UINT64 len = 0;
		memcpy(&len, raw.data() + p, 8);
		p += 8;
		if (len > raw.size() - p) return false;
		sec.assign(raw.begin() + p, raw.begin() + p + static_cast<size_t>(len));
		p += static_cast<size_t>(len);
		return true;
	};
	std::vector<BYTE> secText, secImgOld;
	std::vector<BYTE> plainText;
	std::vector<Entry> entries;
	if (!section(secText) || !DbCrypt::Decrypt(secText.data(), secText.size(), plainText) ||
		!ParseEntries(plainText, entries))
	{
		g_decryptFailed = true;
		return false;
	}
	section(secImgOld);   // 없거나 길이 0 이면 비어 있음
	for (const Entry& e : entries)
	{
		const CStringA body(reinterpret_cast<const char*>(plainText.data() + e.offset), static_cast<int>(e.length));
		if (e.name == "library.tsv")
			text = CString(CA2W(body, CP_UTF8));
		else if (e.name == "pending.tsv")
			pending = CString(CA2W(body, CP_UTF8));
	}

	// 이미지: 이제 실행 폴더의 Image 폴더에 파일로 보관 (이미지 DB 없음)
	//  - 예전 images.vmdb (또는 예전 한 파일 형식의 이미지 묶음) 가 있으면 Image 폴더로 풀고 images.vmdb 는 .bak 으로
	GetImageStoreDir(L"actors");
	GetImageStoreDir(L"studios");
	std::vector<BYTE> imgRaw;
	if (ReadWholeFile(GetImagesDbPath(), imgRaw) && imgRaw.size() >= kImgMagicLen + 8 &&
		memcmp(imgRaw.data(), kImgMagic, kImgMagicLen) == 0)
	{
		UINT64 len = 0;
		memcpy(&len, imgRaw.data() + kImgMagicLen, 8);
		if (len <= imgRaw.size() - kImgMagicLen - 8)
		{
			const std::vector<BYTE> sec(imgRaw.begin() + kImgMagicLen + 8, imgRaw.begin() + kImgMagicLen + 8 + static_cast<size_t>(len));
			if (UnpackImages(sec))
				::MoveFileExW(GetImagesDbPath(), GetImagesDbPath() + L".bak", MOVEFILE_REPLACE_EXISTING);
		}
	}
	if (!secImgOld.empty() && UnpackImages(secImgOld))
		g_readPlainDb = true;   // 예전 한 파일 형식 → 바로 저장해서 이미지 묶음 자리를 비움
	return true;
}

bool CVideoLibrary::WritePackedDb(const CString& text, const CString& pending) const
{
	auto writeFile = [](const CString& path, const char* magic, size_t magicLen,
		const std::vector<BYTE>& sec1, const std::vector<BYTE>* sec2) -> bool
	{
		const CString tmp = path + L".tmp";
		try
		{
			CFile f;
			if (!f.Open(tmp, CFile::modeCreate | CFile::modeWrite))
				return false;
			f.Write(magic, static_cast<UINT>(magicLen));
			const UINT64 l1 = sec1.size();
			f.Write(&l1, 8);
			if (l1) f.Write(sec1.data(), static_cast<UINT>(sec1.size()));
			if (sec2)
			{
				const UINT64 l2 = sec2->size();
				f.Write(&l2, 8);
				if (l2) f.Write(sec2->data(), static_cast<UINT>(sec2->size()));
			}
			f.Close();
		}
		catch (CException* e)
		{
			e->Delete();
			return false;
		}
		return ::MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING) != FALSE;
	};

	// 정보 묶음 → library.vmdb (이미지 묶음 자리는 길이 0, 이미지는 Image 폴더에 파일로)
	std::vector<BYTE> plain;
	{
		const CStringA a(CW2A(text, CP_UTF8));
		const CStringA b(CW2A(pending, CP_UTF8));
		PutEntry(plain, "library.tsv", reinterpret_cast<const BYTE*>(static_cast<LPCSTR>(a)), static_cast<size_t>(a.GetLength()));
		PutEntry(plain, "pending.tsv", reinterpret_cast<const BYTE*>(static_cast<LPCSTR>(b)), static_cast<size_t>(b.GetLength()));
	}
	std::vector<BYTE> secText;
	const bool okText = DbCrypt::Encrypt(plain.data(), plain.size(), secText);
	::SecureZeroMemory(plain.data(), plain.size());
	if (!okText)
		return false;
	const std::vector<BYTE> noImages;
	if (!writeFile(GetWorkDbPath(), kPackMagic, kPackMagicLen, secText, &noImages))   // 실행 중에는 작업 DB 에만 저장
		return false;
	m_workDirty = true;
	if (m_onSaved)
		m_onSaved();

	// 예전 파일(library.tsv / pending.tsv / images 폴더)은 처음 한 번 옮긴 뒤 .bak 으로 남겨 둠
	if (m_legacyToMove)
	{
		m_legacyToMove = false;
		const CString dir = GetAppDataDir();
		::MoveFileExW(dir + L"\\library.tsv", dir + L"\\library.tsv.bak", MOVEFILE_REPLACE_EXISTING);
		::MoveFileExW(dir + L"\\pending.tsv", dir + L"\\pending.tsv.bak", MOVEFILE_REPLACE_EXISTING);
		if (::GetFileAttributesW(dir + L"\\images") != INVALID_FILE_ATTRIBUTES)
			::MoveFileExW(dir + L"\\images", dir + L"\\images.bak", 0);
	}
	return true;
}

bool CVideoLibrary::Load()
{
	items.clear();
	folders.clear();
	actors.clear();
	studios.clear();
	tagInfos.clear();
	labelInfos.clear();
	std::vector<std::pair<CString, CString>> legacyLabels;   // 예전 형식: S 줄 7번째 칸의 레이블 목록 (제작사, 레이블)

	CString text, pendingText;
	g_readPlainDb = false;
	g_decryptFailed = false;
	m_legacyToMove = false;
	bool haveLibrary = false;
	bool havePending = false;
	{
		// 예전 위치(%APPDATA%\VideoManager)의 library.vmdb → 실행 파일 폴더로 복사하고 예전 것은 .bak
		const CString oldDb = GetAppDataDir() + L"\\library.vmdb";
		if (GetDataFilePath().CompareNoCase(oldDb) != 0 &&
			::GetFileAttributesW(GetDataFilePath()) == INVALID_FILE_ATTRIBUTES &&
			::GetFileAttributesW(oldDb) != INVALID_FILE_ATTRIBUTES)
		{
			if (::CopyFileW(oldDb, GetDataFilePath(), TRUE))
				::MoveFileExW(oldDb, oldDb + L".bak", MOVEFILE_REPLACE_EXISTING);
		}
	}
	// 원본 DB 를 작업 DB(library.work.vmdb)로 복사해서 사용 — 실행 중 저장은 작업 DB 에만, [DB 반영] / 종료 때 원본에 반영
	if (::GetFileAttributesW(GetDataFilePath()) != INVALID_FILE_ATTRIBUTES)
		::CopyFileW(GetDataFilePath(), GetWorkDbPath(), FALSE);
	m_workDirty = false;
	if (::GetFileAttributesW(GetWorkDbPath()) != INVALID_FILE_ATTRIBUTES)
	{
		// 하나로 묶은 DB (정보 + 임시 목록 + 이미지)
		haveLibrary = ReadPackedDb(text, pendingText);
		havePending = haveLibrary && !pendingText.IsEmpty();
	}
	else
	{
		// 예전 형식: library.tsv + pending.tsv + images 폴더 → 읽고, 첫 저장 때 library.vmdb 로 묶음
		haveLibrary = ReadUtf8File(GetLegacyLibraryPath(), text);
		havePending = ReadUtf8File(GetPendingFilePath(), pendingText);
		ClearImageCache();
		const CString oldImages = GetAppDataDir() + L"\\images";
		if (::GetFileAttributesW(oldImages) != INVALID_FILE_ATTRIBUTES)
			CopyTree(oldImages, GetImageRoot());
		if (haveLibrary || havePending)
			m_legacyToMove = true;
		g_readPlainDb = g_readPlainDb || m_legacyToMove;   // 바로 저장해서 묶도록
	}

	int pos = 0;
	const int total = text.GetLength();
	while (pos < total)
	{
		int end = text.Find(L'\n', pos);
		if (end < 0) end = total;
		CString line = text.Mid(pos, end - pos);
		pos = end + 1;

		line.TrimRight(L"\r");
		if (line.IsEmpty() || line[0] == L'#')
			continue;

		const std::vector<CString> fields = SplitTabs(line);
		if (fields[0] == L"F" && fields.size() >= 2)
		{
			folders.push_back(Unescape(fields[1]));
		}
		else if ((fields[0] == L"S" || fields[0] == L"T") && fields.size() >= 2)
		{
			const int kind = (fields[0] == L"S") ? LIST_STUDIO : LIST_TAG;
			NamedInfo n;
			n.name = Unescape(fields[1]);
			if (fields.size() >= 3 && kind == LIST_STUDIO) n.memo = Unescape(fields[2]);   // 태그는 메모 없음
			if (fields.size() >= 4 && kind == LIST_STUDIO) n.image = FromStoredPath(Unescape(fields[3]));   // 태그는 이미지 없음
			if (fields.size() >= 5) n.favorite = (fields[4] == L"1");
			if (fields.size() >= 6 && kind == LIST_STUDIO)   // 서브이름 (스튜디오만, 줄바꿈 구분)
			{
				n.subName = Unescape(fields[5]);
				if (n.subName.Find(L'\n') < 0 && n.subName.Find(L',') >= 0)
					n.subName = JoinLines(SplitList(n.subName));   // 예전 형식(쉼표 구분) → 줄바꿈 구분
			}
			if (fields.size() >= 7 && kind == LIST_STUDIO)   // 예전 형식: 레이블 이름 목록 (줄바꿈 구분) → L 줄 항목으로 옮김
				for (const CString& l : SplitLines(Unescape(fields[6])))
					legacyLabels.push_back({ n.name, l });
			if (fields.size() >= 8 && kind == LIST_STUDIO)   // 시리즈 (제작사, 줄바꿈 구분)
				n.series = JoinSeries(ParseSeries(Unescape(fields[7])));
			if (fields.size() >= 9 && kind == LIST_STUDIO)   // 링크 URL (줄바꿈 구분)
				n.urls = JoinUrls(SplitUrls(Unescape(fields[8])));
			if (!n.name.IsEmpty() && FindNamed(kind, n.name) < 0)
				NamedList(kind).push_back(n);
		}
		else if (fields[0] == L"L" && fields.size() >= 2)
		{
			// 레이블: 이름 / 메모 / 이미지 / 즐겨찾기 / 서브이름 / 상위 제작사
			NamedInfo n;
			n.name = Unescape(fields[1]);
			if (fields.size() >= 3) n.memo = Unescape(fields[2]);
			if (fields.size() >= 4) n.image = FromStoredPath(Unescape(fields[3]));
			if (fields.size() >= 5) n.favorite = (fields[4] == L"1");
			if (fields.size() >= 6) n.subName = Unescape(fields[5]);
			if (fields.size() >= 7) n.parent = Unescape(fields[6]);
			if (fields.size() >= 8) n.series = JoinSeries(ParseSeries(Unescape(fields[7])));   // 시리즈 (한 줄에 하나)
			if (fields.size() >= 9) n.urls = JoinUrls(SplitUrls(Unescape(fields[8])));        // 링크 URL
			if (!n.name.IsEmpty() && FindLabel(n.name) < 0)
				labelInfos.push_back(n);
		}
		else if (fields[0] == L"A" && fields.size() >= 2)
		{
			ActorInfo a;
			a.name = Unescape(fields[1]);
			if (fields.size() >= 3) a.aliases = Unescape(fields[2]);
			if (fields.size() >= 4) a.birth   = Unescape(fields[3]);
			if (fields.size() >= 5) a.photo   = FromStoredPath(Unescape(fields[4]));
			if (fields.size() >= 6) a.memo    = Unescape(fields[5]);
			if (fields.size() >= 7) a.nationality = Unescape(fields[6]);
			if (fields.size() >= 8) a.height  = Unescape(fields[7]);
			if (fields.size() >= 9) a.debut   = Unescape(fields[8]);
			if (fields.size() >= 10)   // 예전 "활동명" 필드 → 별칭으로 합침
				a.aliases = JoinList(SplitList(a.aliases + L"," + Unescape(fields[9])));
			if (fields.size() >= 11)   // 성별
				a.gender = Unescape(fields[10]);
			if (fields.size() >= 12)   // 마지막으로 고른 별칭
				a.lastAlias = Unescape(fields[11]);
			if (fields.size() >= 13)   // 은퇴일
				a.retire = Unescape(fields[12]);
			if (fields.size() >= 14)   // 별점
				a.rating = (std::max)(0, (std::min)(5, _wtoi(fields[13])));
			if (fields.size() >= 15)   // 즐겨찾기
				a.favorite = (fields[14] == L"1");
			if (fields.size() >= 19)   // 치수 (B / W / H) + 컵
			{
				a.bust  = Unescape(fields[15]);
				a.waist = Unescape(fields[16]);
				a.hip   = Unescape(fields[17]);
				a.cup   = Unescape(fields[18]);
			}
			if (fields.size() >= 20)   // 링크 URL (줄바꿈 구분)
				a.urls = Unescape(fields[19]);
			if (!a.name.IsEmpty() && FindActor(a.name) < 0)
				actors.push_back(a);
		}
		else if (fields[0] == L"V" && fields.size() >= 7)
		{
			VideoItem v;
			v.path     = Unescape(fields[1]);
			v.size     = _wcstoui64(fields[2], nullptr, 10);
			v.modified = _wcstoui64(fields[3], nullptr, 10);
			v.rating   = (std::min)(5, (std::max)(0, _wtoi(fields[4])));
			v.tags     = Unescape(fields[5]);
			// fields[6] = 예전 영상 메모 (영상 메모 기능 삭제 - 읽지 않음, 다음 저장 때 빈칸으로 기록)
			if (fields.size() >= 8)          // v1 이후 추가된 배우 필드 (이전 파일과 호환)
				v.actors = Unescape(fields[7]);
			if (fields.size() >= 9)          // 스튜디오 필드
				v.studio = Unescape(fields[8]);
			if (fields.size() >= 10)         // 발매일 필드
				v.release = Unescape(fields[9]);
			if (fields.size() >= 11)         // 제목 필드
				v.title = Unescape(fields[10]);
			if (fields.size() >= 12)         // 참여 별칭 필드
				v.actorAliases = Unescape(fields[11]);
			if (fields.size() >= 13)         // 물방울 카운트 필드
				v.oCount = (std::max)(0, _wtoi(fields[12]));
			if (fields.size() >= 14)         // 품번 필드
				v.code = Unescape(fields[13]);
			if (fields.size() >= 15)         // 레이블 필드
				v.label = Unescape(fields[14]);
			if (fields.size() >= 16)         // 시리즈 필드
				v.series = Unescape(fields[15]);
			if (!v.path.IsEmpty())
				items.push_back(v);
		}
	}

	for (const auto& p : legacyLabels)
	{
		if (FindLabel(p.second) >= 0 || p.second.CompareNoCase(p.first) == 0)
			continue;
		NamedInfo n;
		n.name = p.second;
		n.parent = p.first;
		labelInfos.push_back(n);
	}

	// 임시 목록 (스캔으로 찾았지만 아직 정보를 저장하지 않은 파일)
	if (havePending)
	{
		std::set<CString> known;
		for (const VideoItem& v : items)
			known.insert(MakeKey(v.path));

		int ppos = 0;
		const int ptotal = pendingText.GetLength();
		while (ppos < ptotal)
		{
			int end = pendingText.Find(L'\n', ppos);
			if (end < 0) end = ptotal;
			CString line = pendingText.Mid(ppos, end - ppos);
			ppos = end + 1;
			line.TrimRight(L"\r");
			if (line.IsEmpty() || line[0] == L'#')
				continue;
			const std::vector<CString> fields = SplitTabs(line);
			if (fields[0] != L"P" || fields.size() < 4)
				continue;
			VideoItem v;
			v.path = Unescape(fields[1]);
			v.size = _wcstoui64(fields[2], nullptr, 10);
			v.modified = _wcstoui64(fields[3], nullptr, 10);
			v.pending = true;
			ApplyFolderStructure(v);   // 임시 항목의 배우/스튜디오는 폴더 구조에서 다시 계산
			if (!v.path.IsEmpty() && known.insert(MakeKey(v.path)).second)
				items.push_back(v);
		}
	}
	m_loadedPlainText = g_readPlainDb;
	m_dbBroken = g_decryptFailed;
	return haveLibrary;
}

bool CVideoLibrary::Save() const
{
	if (m_dbBroken)
		return false;   // 암호화된 DB 를 읽지 못했으면 빈 내용으로 덮어쓰지 않음

	CString text = L"#VideoManager library v1\n";
	for (const CString& f : folders)
	{
		text += L"F\t";
		text += Escape(f);
		text += L"\n";
	}
	auto RatingField = [](int r) { CString t; if (r > 0) t.Format(L"%d", r); return t; };
	for (const ActorInfo& a : actors)
	{
		text += L"A\t" + Escape(a.name) + L"\t" + Escape(a.aliases) + L"\t" + Escape(a.birth) +
			L"\t" + Escape(ToStoredPath(a.photo)) + L"\t" + Escape(a.memo) + L"\t" + Escape(a.nationality) +
			L"\t" + Escape(a.height) + L"\t" + Escape(a.debut) +
			L"\t\t" + Escape(a.gender) + L"\t" + Escape(a.lastAlias) + L"\t" + Escape(a.retire) +
			L"\t" + RatingField(a.rating) + L"\t" + (a.favorite ? L"1" : L"") +
			L"\t" + Escape(a.bust) + L"\t" + Escape(a.waist) + L"\t" + Escape(a.hip) + L"\t" + Escape(a.cup) +
			L"\t" + Escape(a.urls) + L"\n";   // 20번째 칸: 링크 URL   // 10번째 칸(예전 활동명)은 비워 두고 11번째에 성별
	}
	for (int kind = LIST_STUDIO; kind <= LIST_TAG; ++kind)
	{
		for (const NamedInfo& n : NamedList(kind))
		{
			text += (kind == LIST_STUDIO ? L"S\t" : L"T\t");
			text += Escape(n.name) + L"\t" + (kind == LIST_STUDIO ? Escape(n.memo) : CString()) + L"\t" + (kind == LIST_STUDIO ? Escape(ToStoredPath(n.image)) : CString()) +
				L"\t" + (n.favorite ? L"1" : L"") + L"\t" + (kind == LIST_STUDIO ? Escape(n.subName) : CString()) +
				L"\t\t" + (kind == LIST_STUDIO ? Escape(n.series) : CString()) +
				L"\t" + (kind == LIST_STUDIO ? Escape(n.urls) : CString()) + L"\n";   // 6번째 칸: 서브이름, 7번째 칸: (예전 레이블 목록 - 비움), 8번째 칸: 시리즈, 9번째 칸: 링크
		}
	}
	for (const NamedInfo& n : labelInfos)   // 레이블: L 이름 메모 이미지 즐겨찾기 서브이름 상위제작사
	{
		text += L"L\t" + Escape(n.name) + L"\t" + Escape(n.memo) + L"\t" + Escape(ToStoredPath(n.image)) +
			L"\t" + (n.favorite ? L"1" : L"") + L"\t" + Escape(n.subName) + L"\t" + Escape(n.parent) +
			L"\t" + Escape(n.series) + L"\t" + Escape(n.urls) + L"\n";   // 8번째 칸: 시리즈, 9번째 칸: 링크
	}
	CString pendingText = L"#VideoManager pending v1 (스캔으로 찾은 새 파일 - 정보를 저장하면 library.tsv 로 옮겨짐)\n";
	for (const VideoItem& v : items)
	{
		if (v.pending)
		{
			CString p;
			p.Format(L"P\t%s\t%llu\t%llu\n", static_cast<LPCWSTR>(Escape(v.path)), v.size, v.modified);
			pendingText += p;
			continue;   // 임시 항목은 정식 DB에 쓰지 않음
		}
		CString line;
		line.Format(L"V\t%s\t%llu\t%llu\t%d\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%d\t%s\t%s\t%s\n",   // 15번째 칸: 레이블, 16번째 칸: 시리즈
			static_cast<LPCWSTR>(Escape(v.path)),
			v.size, v.modified, v.rating,
			static_cast<LPCWSTR>(Escape(v.tags)),
			static_cast<LPCWSTR>(Escape(v.memo)),
			static_cast<LPCWSTR>(Escape(v.actors)),
			static_cast<LPCWSTR>(Escape(v.studio)),
			static_cast<LPCWSTR>(Escape(v.release)),
			static_cast<LPCWSTR>(Escape(v.title)),
			static_cast<LPCWSTR>(Escape(v.actorAliases)),
			v.oCount,
			static_cast<LPCWSTR>(Escape(v.code)),
			static_cast<LPCWSTR>(Escape(v.label)),
			static_cast<LPCWSTR>(Escape(v.series)));
		text += line;
	}

	// 정보 + 임시 목록 + 이미지를 파일 하나(library.vmdb)에 (임시 파일에 먼저 쓰고 교체)
	return WritePackedDb(text, pendingText);
}

void CVideoLibrary::ScanFolder(const CString& folder, std::vector<VideoItem>& out, int depth)
{
	if (depth > 32)
		return;

	CString base = folder;
	if (base.Right(1) != L"\\")
		base += L"\\";

	WIN32_FIND_DATAW fd = {};
	HANDLE h = ::FindFirstFileExW(base + L"*", FindExInfoBasic, &fd,
		FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
	if (h == INVALID_HANDLE_VALUE)
		return;

	do
	{
		if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
			continue;

		const CString full = base + fd.cFileName;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			// 정션/심볼릭 링크는 무한 루프 방지를 위해 건너뜀
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
				ScanFolder(full, out, depth + 1);
		}
		else if (IsVideoFile(full))
		{
			VideoItem v;
			v.path     = full;
			v.size     = (static_cast<ULONGLONG>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
			v.modified = (static_cast<ULONGLONG>(fd.ftLastWriteTime.dwHighDateTime) << 32) |
			             fd.ftLastWriteTime.dwLowDateTime;
			out.push_back(v);
		}
	} while (::FindNextFileW(h, &fd));

	::FindClose(h);
}

CString CVideoLibrary::FindBracketDate(const CString& text)
{
	int pos = 0;
	while ((pos = text.Find(L'[', pos)) >= 0)
	{
		const int close = text.Find(L']', pos + 1);
		if (close < 0)
			break;
		CString inner = text.Mid(pos + 1, close - pos - 1);
		inner.Trim();
		pos = pos + 1;

		CString digits;
		bool ok = false;
		if (inner.GetLength() == 8)                       // [YYYYMMDD]
		{
			digits = inner;
			ok = true;
		}
		else if (inner.GetLength() == 10)                 // [YYYY.MM.DD]
		{
			const wchar_t s1 = inner[4], s2 = inner[7];
			if (s1 == s2 && (s1 == L'.' || s1 == L'-'))
			{
				digits = inner.Left(4) + inner.Mid(5, 2) + inner.Mid(8, 2);
				ok = true;
			}
		}
		if (!ok)
			continue;
		for (int i = 0; i < digits.GetLength(); ++i)
		{
			if (digits[i] < L'0' || digits[i] > L'9') { ok = false; break; }
		}
		if (!ok)
			continue;

		const int y = _wtoi(digits.Left(4));
		const int m = _wtoi(digits.Mid(4, 2));
		const int d = _wtoi(digits.Mid(6, 2));
		static const int mdays[] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
		if (y < 1900 || y > 2100 || m < 1 || m > 12 || d < 1 || d > mdays[m - 1])
			continue;
		const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
		if (m == 2 && d == 29 && !leap)
			continue;

		CString r;
		r.Format(L"%04d-%02d-%02d", y, m, d);
		return r;
	}
	return CString();
}

void CVideoLibrary::ApplyFolderStructure(VideoItem& v) const
{
	// 영상 폴더의 텍스트 파일(항목: 값)을 먼저 반영 → 남은 빈칸은 아래 폴더 구조로
	{
		const CString txt = FindVideoTextFile(v.path);
		if (!txt.IsEmpty())
			ApplyVideoTextInfo(v, txt);
	}
	// 품번: 비어 있으면 파일 이름 → 영상 폴더 이름에서 찾기 (예: SONE-479.mp4, [2024.12.10]SONE-479)
	if (v.code.IsEmpty())
	{
		CString stem = v.FileName();
		::PathRemoveExtensionW(stem.GetBuffer());
		stem.ReleaseBuffer();
		v.code = ExtractCode(stem);
		if (v.code.IsEmpty())
		{
			const CString dir = v.path.Left(static_cast<int>(::PathFindFileNameW(v.path) - static_cast<LPCWSTR>(v.path)));
			CString folder = dir;
			folder.TrimRight(L"\\");
			v.code = ExtractCode(::PathFindFileNameW(folder));
		}
	}

	// 영상이 속한 등록 폴더 (여러 개가 겹치면 가장 깊은 폴더)
	CString root;
	for (const CString& f : folders)
	{
		if (IsUnder(v.path, f) && f.GetLength() > root.GetLength())
			root = f;
	}
	if (root.IsEmpty())
		return;

	CString rel = v.path.Mid(root.GetLength());
	rel.TrimLeft(L"\\");
	std::vector<CString> dirs;        // 등록 폴더 아래 하위 폴더 이름들 (파일 이름 제외)
	int start = 0;
	for (;;)
	{
		const int slash = rel.Find(L'\\', start);
		if (slash < 0)
			break;
		CString part = rel.Mid(start, slash - start);
		RemoveListCommas(part);       // 구분 쉼표는 사용 불가 (괄호 안 쉼표는 이름의 일부)
		part.Trim();
		dirs.push_back(part);
		start = slash + 1;
	}

	// 발매일: 영상 파일 이름 → 영상 폴더 이름 순서로 [YYYY.MM.DD] / [YYYYMMDD] 찾기
	if (v.release.IsEmpty())
	{
		CString date = FindBracketDate(v.FileName());
		if (date.IsEmpty() && !dirs.empty())
			date = FindBracketDate(dirs.back());
		v.release = date;
	}

	// 마지막 폴더는 영상 파일을 담은 "영상 폴더" → 제외
	if (!dirs.empty())
		dirs.pop_back();

	CString studio, actor;
	if (dirs.size() == 1)
	{
		actor = dirs[0];              // 폴더\배우\영상\영상.mp4
	}
	else if (dirs.size() >= 2)
	{
		studio = dirs[0];             // 폴더\스튜디오\배우\영상\영상.mp4
		actor  = dirs[1];
	}
	if (!actor.IsEmpty() && v.actors.IsEmpty())
	{
		v.actors = actor;
		NormalizeVideoActors(v);   // 폴더 이름이 배우의 별칭이면 배우 이름 + 참여 별칭으로
	}
	if (!studio.IsEmpty() && v.studio.IsEmpty())
	{
		const int n = FindStudioLoose(studio);   // 폴더 이름이 서브이름이어도 그 스튜디오로
		v.studio = (n >= 0) ? studios[n].name : studio;
		if (n < 0)
			ResolveVideoLabel(v);   // 폴더 이름이 레이블이면 상위 스튜디오 + 레이블
	}
}

// ---------------------------------------------------------------------------
// 분할 파일 묶음 (같은 정보 사용)

bool CVideoLibrary::PartGroupKey(const CString& path, CString& key, int& num)
{
	// 파일 이름(확장자 제외)의 마지막 '_' 오른쪽이 숫자(4자리 이하)면 순번: "ABC-123_2.mp4" → 키 "폴더\abc-123", 순번 2
	// 순번이 없으면 키 = 폴더 + 파일 이름 전체 ("ABC-123.mp4" 도 "ABC-123_2.mp4" 와 같은 묶음)
	const CString folder = path.Left(static_cast<int>(::PathFindFileNameW(path) - static_cast<LPCWSTR>(path)));
	CString name = ::PathFindFileNameW(path);
	const int ext = name.ReverseFind(L'.');
	if (ext > 0)
		name = name.Left(ext);
	num = 0;
	const int us = name.ReverseFind(L'_');
	if (us > 0 && us + 1 < name.GetLength() && name.GetLength() - us - 1 <= 4)
	{
		const CString right = name.Mid(us + 1);
		if (right.SpanIncluding(L"0123456789") == right)
		{
			num = _wtoi(right);
			name = name.Left(us);
		}
	}
	name.Trim();
	key = folder + name;
	key.MakeLower();
	return num > 0;
}

CString CVideoLibrary::PartGroupPath(const CString& path)
{
	CString key;
	int num = 0;
	if (!PartGroupKey(path, key, num))
		return path;
	const CString folder = path.Left(static_cast<int>(::PathFindFileNameW(path) - static_cast<LPCWSTR>(path)));
	CString name = ::PathFindFileNameW(path);
	const CString ext = ::PathFindExtensionW(name);
	name = name.Left(name.GetLength() - ext.GetLength());
	name = name.Left(name.ReverseFind(L'_'));
	name.Trim();
	return folder + name + ext;
}

std::vector<size_t> CVideoLibrary::PartSiblings(size_t idx) const
{
	std::vector<size_t> out;
	if (idx >= items.size())
		return out;
	CString key, k;
	int num = 0;
	PartGroupKey(items[idx].path, key, num);
	for (size_t i = 0; i < items.size(); ++i)
	{
		if (i == idx)
			continue;
		PartGroupKey(items[i].path, k, num);
		if (k == key)
			out.push_back(i);
	}
	return out;
}

namespace
{
	// 분할 파일끼리 같이 쓰는 정보
	void CopyPartInfo(VideoItem& dst, const VideoItem& src)
	{
		dst.code         = src.code;
		dst.title        = src.title;
		dst.rating       = src.rating;
		dst.oCount       = src.oCount;
		dst.release      = src.release;
		dst.actors       = src.actors;
		dst.actorAliases = src.actorAliases;
		dst.studio       = src.studio;
		dst.label        = src.label;
		dst.series       = src.series;
		dst.tags         = src.tags;
	}
	bool SamePartInfo(const VideoItem& a, const VideoItem& b)
	{
		return a.code == b.code && a.title == b.title && a.rating == b.rating && a.oCount == b.oCount &&
			a.release == b.release && a.actors == b.actors && a.actorAliases == b.actorAliases &&
			a.studio == b.studio && a.label == b.label && a.series == b.series && a.tags == b.tags;
	}
}

int CVideoLibrary::SyncPartGroup(size_t idx)
{
	if (idx >= items.size())
		return 0;
	int changed = 0;
	for (size_t i : PartSiblings(idx))
	{
		VideoItem& s = items[i];
		if (SamePartInfo(s, items[idx]) && s.pending == items[idx].pending)
			continue;
		CopyPartInfo(s, items[idx]);
		if (!items[idx].pending)
			s.pending = false;   // 한 파일을 저장하면 묶음 전체가 정식 DB 로
		++changed;
	}
	return changed;
}

int CVideoLibrary::UnifyPartGroups()
{
	// 묶음별로 모음 (파일이 2개 이상인 묶음만)
	std::map<CString, std::vector<size_t>> groups;
	CString key;
	int num = 0;
	for (size_t i = 0; i < items.size(); ++i)
	{
		PartGroupKey(items[i].path, key, num);
		groups[key].push_back(i);
	}
	int changed = 0;
	for (auto& g : groups)
	{
		std::vector<size_t>& list = g.second;
		if (list.size() < 2)
			continue;
		// 기준: 정식 등록된 파일 우선, 그중 순번이 가장 앞선(이름 순) 파일
		std::sort(list.begin(), list.end(), [this](size_t a, size_t b)
		{
			if (items[a].pending != items[b].pending)
				return !items[a].pending;
			return items[a].path.CompareNoCase(items[b].path) < 0;
		});
		VideoItem merged = items[list[0]];
		// 기준 파일에 빈 칸이 있으면 다른 파일의 값으로 채움 (등록된 파일 먼저)
		for (size_t k = 1; k < list.size(); ++k)
		{
			const VideoItem& o = items[list[k]];
			if (o.pending && !merged.pending)
				continue;   // 임시 파일의 자동 지정 값으로 등록된 정보를 채우지 않음
			if (merged.code.IsEmpty())         merged.code = o.code;
			if (merged.title.IsEmpty())        merged.title = o.title;
			if (merged.rating == 0)            merged.rating = o.rating;
			if (merged.oCount == 0)            merged.oCount = o.oCount;
			if (merged.release.IsEmpty())      merged.release = o.release;
			if (merged.actors.IsEmpty())     { merged.actors = o.actors; merged.actorAliases = o.actorAliases; }
			if (merged.studio.IsEmpty())     { merged.studio = o.studio; merged.label = o.label; merged.series = o.series; }
			else if (merged.label.IsEmpty() && merged.studio == o.studio) merged.label = o.label;
			if (merged.series.IsEmpty() && merged.label == o.label) merged.series = o.series;
			if (merged.tags.IsEmpty())         merged.tags = o.tags;
		}
		for (size_t i : list)
		{
			VideoItem& v = items[i];
			const bool registerIt = (v.pending && !merged.pending);
			if (SamePartInfo(v, merged) && !registerIt)
				continue;
			CopyPartInfo(v, merged);
			if (registerIt)
				v.pending = false;
			++changed;
		}
	}
	return changed;
}

int CVideoLibrary::MergeScanned(const std::vector<VideoItem>& scanned)
{
	std::map<CString, size_t> index;
	for (size_t i = 0; i < items.size(); ++i)
		index[MakeKey(items[i].path)] = i;

	int added = 0;
	for (const VideoItem& s : scanned)
	{
		const CString key = MakeKey(s.path);
		auto it = index.find(key);
		if (it == index.end())
		{
			VideoItem item = s;
			item.pending = true;   // 새로 찾은 파일은 임시 목록으로 (정보 저장 시 정식 DB 등록)
			ApplyFolderStructure(item);   // 폴더 구조로 배우/스튜디오 자동 지정
			items.push_back(item);
			index[key] = items.size() - 1;
			++added;
		}
		else
		{
			// 기존 항목: 태그/메모/별점은 유지하고 파일 정보만 갱신
			items[it->second].size     = s.size;
			items[it->second].modified = s.modified;
		}
	}
	if (added > 0)
		UnifyPartGroups();   // 새 분할 파일은 이미 등록된 같은 묶음의 정보를 같이 사용
	return added;
}

int CVideoLibrary::AddFolder(const CString& folder)
{
	CString f = folder;
	f.TrimRight(L"\\");
	if (f.GetLength() == 2 && f[1] == L':')
		f += L"\\";   // 드라이브 루트 (예: D:\)

	bool exists = false;
	for (const CString& x : folders)
	{
		if (x.CompareNoCase(f) == 0) { exists = true; break; }
	}
	if (!exists)
		folders.push_back(f);

	std::vector<VideoItem> scanned;
	ScanFolder(f, scanned, 0);
	RelinkMoved(scanned);   // 다른 폴더에서 옮겨 온 파일이면 기존 정보에 다시 연결
	return MergeScanned(scanned);
}

void CVideoLibrary::RemoveFolder(size_t index)
{
	if (index >= folders.size())
		return;

	const CString removed = folders[index];
	folders.erase(folders.begin() + index);

	items.erase(std::remove_if(items.begin(), items.end(),
		[&](const VideoItem& v)
		{
			if (!IsUnder(v.path, removed))
				return false;
			for (const CString& other : folders)
			{
				if (IsUnder(v.path, other))
					return false;
			}
			return true;
		}), items.end());
}

int CVideoLibrary::RelinkMoved(const std::vector<VideoItem>& scanned)
{
	std::set<CString> known;      // DB 에 있는 경로
	for (const VideoItem& v : items)
		known.insert(MakeKey(v.path));
	std::set<CString> found;      // 스캔에서 찾은 경로
	for (const VideoItem& s : scanned)
		found.insert(MakeKey(s.path));

	// 새로 찾은 파일 (DB 에 없는 경로) = 옮겨진 파일 후보
	std::vector<const VideoItem*> fresh;
	for (const VideoItem& s : scanned)
		if (known.count(MakeKey(s.path)) == 0)
			fresh.push_back(&s);
	if (fresh.empty())
		return 0;
	std::vector<bool> used(fresh.size(), false);

	// 없어진 항목: 저장된 항목을 먼저 (정보가 있는 쪽을 우선 연결)
	std::vector<size_t> missing;
	for (int pass = 0; pass < 2; ++pass)
		for (size_t i = 0; i < items.size(); ++i)
		{
			const VideoItem& v = items[i];
			if (v.pending != (pass == 1))
				continue;
			if (found.count(MakeKey(v.path)) == 0 && ::GetFileAttributesW(v.path) == INVALID_FILE_ATTRIBUTES)
				missing.push_back(i);
		}

	int relinked = 0;
	for (size_t mi : missing)
	{
		VideoItem& v = items[mi];
		if (v.size == 0)
			continue;
		const CString name = v.FileName();
		int best = -1, bestScore = 0;
		for (size_t k = 0; k < fresh.size(); ++k)
		{
			if (used[k] || fresh[k]->size != v.size)
				continue;
			int score = 0;
			if (fresh[k]->FileName().CompareNoCase(name) == 0) score += 2;   // 같은 이름 (폴더만 옮김)
			if (fresh[k]->modified == v.modified && v.modified != 0) score += 1;   // 같은 수정 시각 (이름만 바꿈)
			if (score > bestScore)
			{
				bestScore = score;
				best = static_cast<int>(k);
			}
		}
		if (best < 0)
			continue;
		used[best] = true;
		v.path = fresh[best]->path;           // 정보는 그대로, 경로만 새 위치로
		v.size = fresh[best]->size;
		v.modified = fresh[best]->modified;
		++relinked;
	}
	return relinked;
}

int CVideoLibrary::RelinkOnStartup()
{
	bool anyMissing = false;
	for (const VideoItem& v : items)
		if (::GetFileAttributesW(v.path) == INVALID_FILE_ATTRIBUTES) { anyMissing = true; break; }
	if (!anyMissing)
		return 0;   // 모두 제자리면 스캔하지 않음 (시작 속도)

	std::vector<VideoItem> scanned;
	for (const CString& f : folders)
		if (::GetFileAttributesW(f) != INVALID_FILE_ATTRIBUTES)
			ScanFolder(f, scanned, 0);
	return RelinkMoved(scanned);
}

void CVideoLibrary::Refresh(int& added, int& removed, int* relinked)
{
	std::vector<VideoItem> scanned;
	for (const CString& f : folders)
		ScanFolder(f, scanned, 0);

	// 옮겨지거나 이름이 바뀐 파일은 기존 항목에 다시 연결 (지우고 새로 추가하지 않음)
	const int moved = RelinkMoved(scanned);
	if (relinked)
		*relinked = moved;

	std::set<CString> found;
	for (const VideoItem& s : scanned)
		found.insert(MakeKey(s.path));

	const size_t before = items.size();
	items.erase(std::remove_if(items.begin(), items.end(),
		[&](const VideoItem& v)
		{
			if (found.count(MakeKey(v.path)) != 0 || ::GetFileAttributesW(v.path) != INVALID_FILE_ATTRIBUTES)
				return false;
			// 등록 폴더 자체에 접근할 수 없으면(외장 드라이브 분리 등) 지우지 않음
			for (const CString& f : folders)
				if (IsUnder(v.path, f) && ::GetFileAttributesW(f) == INVALID_FILE_ATTRIBUTES)
					return false;
			return true;
		}), items.end());

	removed = static_cast<int>(before - items.size());
	added = MergeScanned(scanned);
}

// ---------------------------------------------------------------------------
// 배우

namespace
{
	bool IsOpenParen(wchar_t c)  { return c == L'(' || c == L'\xFF08'; }   // ( （
	bool IsCloseParen(wchar_t c) { return c == L')' || c == L'\xFF09'; }   // ) ）
}

int CVideoLibrary::FindListComma(const CString& text, int start)
{
	int depth = 0;
	for (int i = 0; i < text.GetLength(); ++i)
	{
		const wchar_t c = text[i];
		if (IsOpenParen(c))
			++depth;
		else if (IsCloseParen(c))
			depth = (std::max)(0, depth - 1);
		else if (c == L',' && depth == 0 && i >= start)
			return i;
	}
	return -1;
}

int CVideoLibrary::ReverseFindListComma(const CString& text, int before)
{
	int found = -1;
	int depth = 0;
	const int n = (std::min)(before, text.GetLength());
	for (int i = 0; i < n; ++i)
	{
		const wchar_t c = text[i];
		if (IsOpenParen(c))
			++depth;
		else if (IsCloseParen(c))
			depth = (std::max)(0, depth - 1);
		else if (c == L',' && depth == 0)
			found = i;
	}
	return found;
}

void CVideoLibrary::RemoveListCommas(CString& text)
{
	for (int p = FindListComma(text, 0); p >= 0; p = FindListComma(text, p))
		text.Delete(p);
}

std::vector<CString> CVideoLibrary::SplitList(const CString& text)
{
	std::vector<CString> out;
	int pos = 0;
	for (;;)
	{
		const int p = FindListComma(text, pos);
		CString t = (p < 0) ? text.Mid(pos) : text.Mid(pos, p - pos);
		t.Trim();
		if (!t.IsEmpty())
		{
			bool dup = false;
			for (const CString& x : out)
			{
				if (x.CompareNoCase(t) == 0) { dup = true; break; }
			}
			if (!dup)
				out.push_back(t);
		}
		if (p < 0) break;
		pos = p + 1;
	}
	return out;
}

CString CVideoLibrary::JoinList(const std::vector<CString>& list)
{
	CString out;
	for (const CString& s : list)
	{
		if (!out.IsEmpty()) out += L", ";
		out += s;
	}
	return out;
}

int CVideoLibrary::FindActor(const CString& name) const
{
	for (size_t i = 0; i < actors.size(); ++i)
	{
		if (actors[i].name.CompareNoCase(name) == 0)
			return static_cast<int>(i);
	}
	return -1;
}

int CVideoLibrary::FindActorByAnyName(const CString& name) const
{
	const int exact = FindActor(name);
	if (exact >= 0)
		return exact;
	for (size_t i = 0; i < actors.size(); ++i)
	{
		for (const CString& s : SplitList(actors[i].aliases))
		{
			if (s.CompareNoCase(name) == 0)
				return static_cast<int>(i);
		}
	}
	return -1;
}

std::map<CString, int> CVideoLibrary::ActorNameIndex() const
{
	std::map<CString, int> index;
	for (size_t i = 0; i < actors.size(); ++i)
	{
		CString key = actors[i].name;
		key.MakeLower();
		index.emplace(key, static_cast<int>(i));
	}
	for (size_t i = 0; i < actors.size(); ++i)
	{
		for (const CString& s : SplitList(actors[i].aliases))
		{
			CString key = s;
			key.MakeLower();
			index.emplace(key, static_cast<int>(i));   // 이미 있는 이름은 덮어쓰지 않음
		}
	}
	return index;
}

CString CVideoLibrary::ActorKeyOf(const std::map<CString, int>& index, const CString& videoName) const
{
	CString key = videoName;
	key.MakeLower();
	auto it = index.find(key);
	if (it != index.end() && it->second >= 0 && it->second < static_cast<int>(actors.size()))
	{
		key = actors[it->second].name;
		key.MakeLower();
	}
	return key;
}

bool CVideoLibrary::NormalizeVideoActors(VideoItem& v) const
{
	std::vector<CString> names = SplitList(v.actors);
	std::vector<CString> credited = SplitList(v.actorAliases);
	bool changed = false;

	// 1) 배우 칸의 별칭 표기 → 본래 이름, 별칭은 참여 별칭으로
	for (CString& n : names)
	{
		if (FindActor(n) >= 0)
			continue;
		const int idx = FindActorByAnyName(n);
		if (idx < 0)
		{
			// 이름 · 별칭과 그대로 같지는 않지만 한글 / 영어 / 일어 이름 중 하나가 같으면 같은 배우
			//  (예: 폴더 이름 "나기 히카루" → 배우 "나기 히카루(Hikaru Nagi, 凪ひかる)") - 별칭이 아니므로 참여 별칭에는 넣지 않음
			const int part = FindActorByNamePart(n);
			if (part >= 0)
			{
				n = actors[part].name;
				changed = true;
			}
			continue;
		}
		credited.push_back(n);
		n = actors[idx].name;
		changed = true;
	}

	// 2) 배우 칸에 없는 배우의 참여 별칭은 뺌 (어느 배우의 별칭도 아닌 값은 그대로 둠)
	std::vector<CString> kept;
	for (const CString& c : credited)
	{
		const int idx = FindActorByAnyName(c);
		bool keep = (idx < 0);
		for (const CString& n : names)
		{
			if (idx >= 0 && actors[idx].name.CompareNoCase(n) == 0) { keep = true; break; }
		}
		if (keep)
			kept.push_back(c);
	}

	const CString newActors = JoinList(SplitList(JoinList(names)));    // 중복 제거
	const CString newAliases = JoinList(SplitList(JoinList(kept)));
	if (newActors != v.actors || newAliases != v.actorAliases)
	{
		v.actors = newActors;
		v.actorAliases = newAliases;
		changed = true;
	}
	return changed;
}

bool CVideoLibrary::NormalizeAllVideoActors()
{
	bool changed = false;
	for (VideoItem& v : items)
	{
		if (NormalizeVideoActors(v))
			changed = true;
	}
	return changed;
}

CString CVideoLibrary::FindActorFolderImage(const CString& videoPath, const CString& actorName)
{
	// 영상의 상위 폴더 중 이름이 배우 이름과 같은 폴더(폴더\배우\영상\영상.mp4 의 "배우")를 찾음
	static const wchar_t* const kExts[] = { L".jpg", L".jpeg", L".png", L".webp", L".bmp", L".gif", L".tif", L".tiff" };
	CString dir = videoPath;
	for (int depth = 0; depth < 8; ++depth)
	{
		const int slash = dir.ReverseFind(L'\\');
		if (slash <= 2)
			break;                      // 드라이브 루트까지 올라감
		dir = dir.Left(slash);
		CString name = ::PathFindFileNameW(dir);
		RemoveListCommas(name);         // 폴더 구조 배우 이름과 같은 방식으로 정리
		name.Trim();
		if (name.CompareNoCase(actorName) != 0)
			continue;

		// 배우 폴더 바로 아래의 이미지 파일 (하위 폴더는 보지 않음)
		std::vector<CString> images;
		WIN32_FIND_DATAW fd = {};
		HANDLE h = ::FindFirstFileW(dir + L"\\*", &fd);
		if (h != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
					continue;
				const LPCWSTR ext = ::PathFindExtensionW(fd.cFileName);
				for (const wchar_t* e : kExts)
				{
					if (_wcsicmp(ext, e) == 0) { images.push_back(fd.cFileName); break; }
				}
			} while (::FindNextFileW(h, &fd));
			::FindClose(h);
		}
		if (images.empty())
			return CString();
		// 우선순위: 배우(폴더) 이름 / folder / poster / profile / cover 와 같은 이름 → 없으면 이름 순 첫 번째
		std::sort(images.begin(), images.end(), [](const CString& a, const CString& b) { return ::StrCmpLogicalW(a, b) < 0; });
		const CString folderName = ::PathFindFileNameW(dir);
		const CString preferred[] = { folderName, actorName, L"folder", L"poster", L"profile", L"cover", L"actor" };
		for (const CString& p : preferred)
		{
			for (const CString& img : images)
			{
				CString stem = img;
				::PathRemoveExtensionW(stem.GetBuffer());
				stem.ReleaseBuffer();
				if (stem.CompareNoCase(p) == 0)
					return dir + L"\\" + img;
			}
		}
		return dir + L"\\" + images.front();
	}
	return CString();
}

bool CVideoLibrary::SyncActorsFromVideos()
{
	bool added = false;
	std::map<CString, int> index = ActorNameIndex();
	for (const VideoItem& v : items)
	{
		for (const CString& n : SplitList(v.actors))
		{
			CString key = n;
			key.MakeLower();
			if (index.find(key) == index.end() && FindActorByNamePart(n) < 0)   // 이름·별칭 어디에도 없고, 언어 단위 이름도 같은 배우가 없을 때만 새 배우
			{
				ActorInfo a;
				a.name = n;
				// 배우 폴더에 이미지 파일이 있으면 배우 사진으로 (Image\actors 에 암호화 사본)
				const CString img = FindActorFolderImage(v.path, n);
				if (!img.IsEmpty())
					a.photo = StoreImageCopy(img, L"actors");
				// 배우 폴더에 텍스트 파일(항목: 값)이 있으면 배우 정보로
				const CString folder = FindActorFolder(v.path, n);
				if (!folder.IsEmpty())
				{
					const CString txt = FindActorTextFile(folder, n);
					if (!txt.IsEmpty())
						ApplyActorTextInfo(a, txt);
				}
				actors.push_back(a);
				index[key] = static_cast<int>(actors.size()) - 1;
				added = true;
			}
		}
	}
	// 배우 이름과 언어 단위로 같은 별칭(예: "나기 히카루(凪ひかる)")은 뺌
	if (CleanupActorAliases())
		added = true;
	// 언어 단위로 같은 이름인 영상의 배우 표기는 그 배우 이름으로 정리, 정보 없는 중복 배우는 합침
	if (NormalizeAllVideoActors())
		added = true;
	if (MergeEmptyDuplicateActors() > 0)
		added = true;
	// 이미 등록된 배우 중 사진이 없는 배우도 배우 폴더의 이미지로 채움
	if (FillMissingActorPhotos())
		added = true;
	return added;
}

bool CVideoLibrary::CleanupActorAliases()
{
	bool changed = false;
	for (ActorInfo& a : actors)
	{
		std::vector<CString> kept, dropped;
		for (const CString& al : SplitList(a.aliases))
		{
			bool same = SameNameByLang(al, a.name);
			for (const CString& k : kept)
				if (!same && SameNameByLang(al, k)) same = true;
			if (same)
				dropped.push_back(al);
			else
				kept.push_back(al);
		}
		if (dropped.empty())
			continue;
		a.aliases = JoinList(kept);
		for (const CString& d : dropped)
			if (a.lastAlias.CompareNoCase(d) == 0)
				a.lastAlias.Empty();
		// 영상의 참여 별칭에 빠진 별칭이 있으면 같이 뺌 (배우 이름으로 표시)
		for (VideoItem& v : items)
		{
			std::vector<CString> credited = SplitList(v.actorAliases);
			const size_t before = credited.size();
			credited.erase(std::remove_if(credited.begin(), credited.end(), [&dropped](const CString& c)
			{
				for (const CString& d : dropped)
					if (c.CompareNoCase(d) == 0) return true;
				return false;
			}), credited.end());
			if (credited.size() != before)
				v.actorAliases = JoinList(credited);
		}
		changed = true;
	}
	return changed;
}

// ---------------------------------------------------------------------------
// 성별

namespace
{
	const wchar_t* const kGenders[] = { L"", L"여성", L"남성", L"트랜스젠더 여성", L"트랜스젠더 남성", L"인터섹스", L"논바이너리" };
}

int CVideoLibrary::GenderCount()
{
	return static_cast<int>(_countof(kGenders));
}

CString CVideoLibrary::GenderAt(int i)
{
	return (i >= 0 && i < GenderCount()) ? CString(kGenders[i]) : CString();
}

CString CVideoLibrary::NormalizeGender(const CString& text)
{
	CString v = text;
	v.Trim();
	v.MakeLower();
	if (v.IsEmpty())
		return CString();
	for (int i = 1; i < GenderCount(); ++i)   // 저장 값 그대로
		if (v == CString(kGenders[i]))
			return kGenders[i];
	CString c = v;   // 공백 · 하이픈 · 밑줄 제거본
	c.Remove(L' '); c.Remove(L'-'); c.Remove(L'_');
	auto has = [&c](LPCWSTR s) { return c.Find(s) >= 0; };

	// 논바이너리 / 인터섹스 먼저 (male · female 글자가 섞여 있어도)
	if (has(L"nonbinary") || has(L"논바이너리") || has(L"넌바이너리") || has(L"enby") || has(L"ノンバイナリ") || has(L"genderqueer") || c == L"nb" || c == L"x")
		return L"논바이너리";
	if (has(L"intersex") || has(L"인터섹스") || has(L"インターセックス") || has(L"間性"))
		return L"인터섹스";
	// 트랜스젠더: 여성(MTF) / 남성(FTM)
	const bool trans = has(L"trans") || has(L"트랜스") || has(L"トランス") || has(L"mtf") || has(L"ftm");
	if (trans)
	{
		if (has(L"mtf") || has(L"female") || has(L"woman") || has(L"여") || has(L"女"))
			return L"트랜스젠더 여성";
		if (has(L"ftm") || has(L"male") || has(L"man") || has(L"남") || has(L"男"))
			return L"트랜스젠더 남성";
		return CString();   // 방향을 알 수 없음
	}
	if (has(L"여") || has(L"female") || has(L"woman") || c == L"f" || has(L"女"))
		return L"여성";
	if (has(L"남") || has(L"male") || c == L"man" || c == L"m" || has(L"男"))
		return L"남성";
	return CString();
}

bool CVideoLibrary::IsFemaleLike(const CString& g)
{
	return g == L"여성" || g == L"트랜스젠더 여성";
}

bool CVideoLibrary::IsMaleLike(const CString& g)
{
	return g == L"남성" || g == L"트랜스젠더 남성";
}

int CVideoLibrary::MergeEmptyDuplicateActors()
{
	// 예: 스캔으로 생긴 "나기 히카루"(사진 · 정보 없음) + 등록된 "나기 히카루(Hikaru Nagi, 凪ひかる)" → 앞의 배우를 뒤의 배우로 합침
	//  정보가 하나라도 있는 배우는 다른 사람일 수 있으므로 합치지 않음
	int merged = 0;
	for (size_t i = 0; i < actors.size(); )
	{
		const ActorInfo& a = actors[i];
		const bool empty = HasNoActorInfo(a) && a.photo.IsEmpty() && a.aliases.IsEmpty() && a.memo.IsEmpty() &&
			a.gender.IsEmpty() && a.rating == 0 && !a.favorite;
		const int target = empty ? FindActorByNamePart(a.name, static_cast<int>(i)) : -1;
		if (target < 0)
		{
			++i;
			continue;
		}
		const CString from = a.name, to = actors[target].name;
		RenameActorInVideos(from, to);
		actors.erase(actors.begin() + i);
		++merged;
	}
	return merged;
}

bool CVideoLibrary::FillMissingActorPhotos()
{
	// 사진이 없는(또는 사진 파일이 없어진) 배우 → 그 배우가 나오는 영상들의 경로에서 배우 폴더 이미지 찾기
	std::vector<int> need;
	for (size_t i = 0; i < actors.size(); ++i)
	{
		const CString& p = actors[i].photo;
		if (p.IsEmpty() || !::PathFileExistsW(p) || HasNoActorInfo(actors[i]))
			need.push_back(static_cast<int>(i));   // 사진이 없거나 배우 정보가 비어 있음
	}
	if (need.empty())
		return false;

	// 배우별 출연 영상 경로 (이름·별칭으로 연결)
	const std::map<CString, int> index = ActorNameIndex();
	std::map<int, std::vector<CString>> videos;
	for (const VideoItem& v : items)
	{
		for (const CString& n : SplitList(v.actors))
		{
			CString key = n;
			key.MakeLower();
			auto it = index.find(key);
			if (it != index.end())
				videos[it->second].push_back(v.path);
		}
	}

	bool changed = false;
	for (int idx : need)
	{
		auto vit = videos.find(idx);
		if (vit == videos.end())
			continue;
		ActorInfo& a = actors[idx];
		std::vector<CString> names = { a.name };   // 폴더 이름이 별칭일 수도 있음
		for (const CString& al : SplitList(a.aliases))
			names.push_back(al);
		const bool needPhoto = a.photo.IsEmpty() || !::PathFileExistsW(a.photo);
		if (HasNoActorInfo(a))
		{
			// 배우 정보가 비어 있으면 배우 폴더의 텍스트 파일에서 읽기
			std::set<CString> triedFolders;
			for (const CString& path : vit->second)
			{
				bool done = false;
				for (const CString& nm : names)
				{
					CString folder = FindActorFolder(path, nm);
					if (folder.IsEmpty())
						continue;
					CString fk = folder;
					fk.MakeLower();
					if (!triedFolders.insert(fk).second)
						continue;
					const CString txt = FindActorTextFile(folder, nm);
					if (!txt.IsEmpty() && ApplyActorTextInfo(a, txt))
					{
						changed = true;
						done = true;
						break;
					}
				}
				if (done)
					break;
			}
		}
		if (!needPhoto)
			continue;
		std::set<CString> triedDirs;               // 같은 영상 폴더는 한 번만
		CString found;
		for (const CString& path : vit->second)
		{
			CString dir = path.Left(static_cast<int>(::PathFindFileNameW(path) - static_cast<LPCWSTR>(path)));
			dir.MakeLower();
			if (!triedDirs.insert(dir).second)
				continue;
			for (const CString& nm : names)
			{
				found = FindActorFolderImage(path, nm);
				if (!found.IsEmpty())
					break;
			}
			if (!found.IsEmpty())
				break;
		}
		if (found.IsEmpty())
			continue;
		const CString copy = StoreImageCopy(found, L"actors");   // Image\actors 에 암호화 사본
		if (!copy.IsEmpty())
		{
			a.photo = copy;
			changed = true;
		}
	}
	return changed;
}

void CVideoLibrary::RenameActorInVideos(const CString& oldName, const CString& newName)
{
	for (VideoItem& v : items)
	{
		std::vector<CString> list = SplitList(v.actors);
		bool changed = false;
		for (CString& n : list)
		{
			if (n.CompareNoCase(oldName) == 0) { n = newName; changed = true; }
		}
		if (changed)
			v.actors = JoinList(SplitList(JoinList(list)));   // 중복 제거
	}
}

int CVideoLibrary::RemoveActorFromVideos(const CString& name)
{
	// 이름뿐 아니라 별칭으로 연결된 표기도 함께 뺌
	const std::map<CString, int> index = ActorNameIndex();
	CString target = name;
	target.MakeLower();
	int affected = 0;
	for (VideoItem& v : items)
	{
		std::vector<CString> list = SplitList(v.actors);
		const size_t before = list.size();
		list.erase(std::remove_if(list.begin(), list.end(),
			[&](const CString& n) { return n.CompareNoCase(name) == 0 || ActorKeyOf(index, n) == target; }), list.end());
		if (list.size() != before)
		{
			v.actors = JoinList(list);
			++affected;
		}
	}
	return affected;
}

int CVideoLibrary::CountVideosWithActor(const CString& name) const
{
	const std::map<CString, int> index = ActorNameIndex();
	CString target = name;
	target.MakeLower();
	int count = 0;
	for (const VideoItem& v : items)
	{
		for (const CString& n : SplitList(v.actors))
		{
			if (n.CompareNoCase(name) == 0 || ActorKeyOf(index, n) == target) { ++count; break; }
		}
	}
	return count;
}

CString CVideoLibrary::FindCodeOnDate(const CString& actorName, const CString& date) const
{
	const int idx = FindVideoOnDate(actorName, date);
	return (idx >= 0) ? items[idx].code : CString();
}

int CVideoLibrary::FindVideoOnDate(const CString& actorName, const CString& date) const
{
	if (actorName.IsEmpty() || date.IsEmpty())
		return -1;
	const std::map<CString, int> index = ActorNameIndex();
	CString target = actorName;
	target.MakeLower();
	int best = -1;
	for (size_t i = 0; i < items.size(); ++i)
	{
		const VideoItem& v = items[i];
		if (v.code.IsEmpty() || v.release.CompareNoCase(date) != 0)
			continue;
		for (const CString& n : SplitList(v.actors))
		{
			if (n.CompareNoCase(actorName) == 0 || ActorKeyOf(index, n) == target)
			{
				// 여러 개면 품번 순 첫 번째, 같은 품번(분할 파일)이면 경로 순 첫 번째
				const int c = (best < 0) ? -1 : v.code.CompareNoCase(items[best].code);
				if (best < 0 || c < 0 || (c == 0 && v.path.CompareNoCase(items[best].path) < 0))
					best = static_cast<int>(i);
				break;
			}
		}
	}
	return best;
}

int CVideoLibrary::CalcAge(const CString& birth)
{
	int y = 0, m = 0, d = 0;
	if (birth.IsEmpty() || swscanf_s(birth, L"%d-%d-%d", &y, &m, &d) != 3 || y < 1)
		return -1;

	SYSTEMTIME now = {};
	::GetLocalTime(&now);
	int age = now.wYear - y;
	if (now.wMonth < m || (now.wMonth == m && now.wDay < d))
		--age;   // 올해 생일이 아직 안 지남
	return (age >= 0 && age < 150) ? age : -1;
}

CString CVideoLibrary::AgeText(const CString& birth)
{
	const int age = CalcAge(birth);
	CString s;
	if (age >= 0)
		s.Format(L"%s (%d세)", static_cast<LPCWSTR>(birth), age);
	else
		s = birth;
	return s;
}

// ---------------------------------------------------------------------------
// 스튜디오 / 태그

std::vector<CString> CVideoLibrary::VideoNamedValues(const VideoItem& v, int kind)
{
	if (kind == LIST_TAG)
		return SplitList(v.tags);

	std::vector<CString> out;
	CString s = v.studio;
	s.Trim();
	if (!s.IsEmpty())
		out.push_back(s);
	return out;
}

int CVideoLibrary::FindNamed(int kind, const CString& name) const
{
	const std::vector<NamedInfo>& list = NamedList(kind);
	for (size_t i = 0; i < list.size(); ++i)
	{
		if (list[i].name.CompareNoCase(name) == 0)
			return static_cast<int>(i);
	}
	// 스튜디오는 서브이름(보조 표기)으로도 찾음
	if (kind == LIST_STUDIO && !name.IsEmpty())
	{
		for (size_t i = 0; i < list.size(); ++i)
		{
			if (list[i].subName.IsEmpty())
				continue;
			for (const CString& sub : SplitLines(list[i].subName))   // 서브이름 여러 개 (줄바꿈 구분)
				if (sub.CompareNoCase(name) == 0)
					return static_cast<int>(i);
		}
	}
	return -1;
}

bool CVideoLibrary::SyncNamedFromVideos()
{
	bool added = false;
	// 영상의 스튜디오가 서브이름으로 적혀 있으면 스튜디오 이름으로 바꿈 (같은 스튜디오로 묶이도록)
	for (VideoItem& v : items)
	{
		// 스튜디오 칸의 레이블 이름 → 상위 스튜디오 + 레이블, 레이블만 있으면 상위 스튜디오
		const CString oldStudio = v.studio, oldLabel = v.label;
		ResolveVideoLabel(v);
		if (v.studio != oldStudio || v.label != oldLabel)
			added = true;

		CString s = v.studio;
		s.Trim();
		if (s.IsEmpty())
			continue;
		const int n = FindStudioLoose(s);   // 서브이름 · 공백/대소문자 차이도 같은 스튜디오
		if (n >= 0 && studios[n].name != v.studio)
		{
			v.studio = studios[n].name;
			added = true;
		}
	}
	for (const VideoItem& v : items)
	{
		for (int kind = LIST_STUDIO; kind <= LIST_TAG; ++kind)
		{
			for (const CString& n : VideoNamedValues(v, kind))
			{
				if (FindNamed(kind, n) < 0)
				{
					NamedInfo info;
					info.name = n;
					NamedList(kind).push_back(info);
					added = true;
				}
			}
		}
	}
	// 영상의 시리즈: 레이블이 비어 있으면 그 시리즈를 가진 레이블로 채움 (레이블의 상위 제작사도), 등록 표기로 맞춤
	for (VideoItem& v : items)
	{
		v.series.Trim();
		if (v.series.IsEmpty() && !v.code.IsEmpty())
		{
			// 품번 접두어가 등록된 시리즈면 그 시리즈로 (레이블 · 제작사는 아래에서 채움)
			const CString s = SeriesForCode(v.code);
			if (!s.IsEmpty()) { v.series = s; added = true; }
		}
		if (v.series.IsEmpty())
			continue;
		CString sname;
		const int li = FindLabelOfSeries(v.series, &sname);
		if (li < 0)
		{
			// 제작사의 시리즈: 표기 맞춤, 제작사가 비어 있으면 그 제작사로
			const int sn = FindStudioOfSeries(v.series, &sname);
			if (sn < 0)
				continue;
			if (v.series != sname) { v.series = sname; added = true; }
			if (v.studio.IsEmpty()) { v.studio = studios[sn].name; added = true; }
			continue;
		}
		if (v.series != sname) { v.series = sname; added = true; }
		if (v.label.IsEmpty())
		{
			v.label = labelInfos[li].name;
			if (v.studio.IsEmpty())
			{
				const int owner = FindNamed(LIST_STUDIO, labelInfos[li].parent);
				if (owner >= 0) v.studio = studios[owner].name;
			}
			added = true;
		}
	}
	// 영상의 레이블 → 그 영상 스튜디오의 레이블 목록에 추가 (없을 때만)
	for (const VideoItem& v : items)
	{
		CString lb = v.label;
		lb.Trim();
		if (lb.IsEmpty() || v.studio.IsEmpty())
			continue;
		const int n = FindStudioLoose(v.studio);
		if (n < 0 || lb.CompareNoCase(studios[n].name) == 0)
			continue;
		if (FindLabel(lb) < 0)   // 레이블 이름은 전체에서 하나 (이미 다른 제작사에 있으면 그대로)
		{
			NamedInfo info;
			info.name = lb;
			info.parent = studios[n].name;
			labelInfos.push_back(info);
			added = true;
		}
	}
	// 영상의 시리즈 → 어느 레이블 · 제작사에도 없으면 그 영상 레이블의 시리즈로 (레이블이 없으면 제작사의 시리즈로)
	for (const VideoItem& v : items)
	{
		if (v.series.IsEmpty() || FindLabelOfSeries(v.series) >= 0 || FindStudioOfSeries(v.series) >= 0)
			continue;
		NamedInfo* owner = nullptr;
		const int li = v.label.IsEmpty() ? -1 : FindLabel(v.label);
		if (li >= 0)
			owner = &labelInfos[li];
		else
		{
			const int sn = v.studio.IsEmpty() ? -1 : FindNamed(LIST_STUDIO, v.studio);
			if (sn >= 0)
				owner = &studios[sn];
		}
		if (!owner)
			continue;
		std::vector<SeriesInfo> list = ParseSeries(owner->series);
		SeriesInfo si;
		si.name = v.series;
		list.push_back(si);
		owner->series = JoinSeries(list);
		added = true;
	}
	return added;
}

int CVideoLibrary::FindLabel(const CString& name) const
{
	CString nm = name;
	nm.Trim();
	if (nm.IsEmpty())
		return -1;
	for (size_t i = 0; i < labelInfos.size(); ++i)
		if (labelInfos[i].name.CompareNoCase(nm) == 0)
			return static_cast<int>(i);
	for (size_t i = 0; i < labelInfos.size(); ++i)
		for (const CString& s : SplitLines(labelInfos[i].subName))
			if (s.CompareNoCase(nm) == 0)
				return static_cast<int>(i);
	return -1;
}

std::vector<int> CVideoLibrary::LabelsOf(const CString& studioName) const
{
	std::vector<int> out;
	if (studioName.IsEmpty())
		return out;
	for (size_t i = 0; i < labelInfos.size(); ++i)
		if (labelInfos[i].parent.CompareNoCase(studioName) == 0)
			out.push_back(static_cast<int>(i));
	std::sort(out.begin(), out.end(), [this](int a, int b)
	{
		return ::StrCmpLogicalW(labelInfos[a].name, labelInfos[b].name) < 0;
	});
	return out;
}

std::vector<CString> CVideoLibrary::LabelNamesOf(const CString& studioName) const
{
	std::vector<CString> out;
	for (int i : LabelsOf(studioName))
		out.push_back(labelInfos[i].name);
	return out;
}

int CVideoLibrary::CountLabelVideos(const CString& labelName) const
{
	int n = 0;
	for (const VideoItem& v : items)
		if (!v.label.IsEmpty() && v.label.CompareNoCase(labelName) == 0)
			++n;
	return n;
}

void CVideoLibrary::RenameNamedInVideos(int kind, const CString& oldName, const CString& newName)
{
	if (kind == LIST_STUDIO)   // 레이블의 상위 제작사 이름도
		for (NamedInfo& lb : labelInfos)
			if (lb.parent.CompareNoCase(oldName) == 0)
				lb.parent = newName;
	for (VideoItem& v : items)
	{
		if (kind == LIST_STUDIO)
		{
			CString s = v.studio;
			s.Trim();
			if (s.CompareNoCase(oldName) == 0)
				v.studio = newName;
		}
		else
		{
			std::vector<CString> list = SplitList(v.tags);
			bool changed = false;
			for (CString& n : list)
			{
				if (n.CompareNoCase(oldName) == 0) { n = newName; changed = true; }
			}
			if (changed)
				v.tags = JoinList(SplitList(JoinList(list)));
		}
	}
}

int CVideoLibrary::FindLabelOfSeries(const CString& series, CString* seriesName) const
{
	CString nm = series;
	nm.Trim();
	if (nm.IsEmpty())
		return -1;
	for (size_t i = 0; i < labelInfos.size(); ++i)
		for (const CString& s : SeriesNames(labelInfos[i].series))
			if (s.CompareNoCase(nm) == 0)
			{
				if (seriesName) *seriesName = s;
				return static_cast<int>(i);
			}
	return -1;
}

namespace
{
	const wchar_t kSeriesSep = L'\x241F';   // 시리즈 칸 구분 문자 (␟, 이름에 쓰이지 않는 기호)
}

std::vector<SeriesInfo> CVideoLibrary::ParseSeries(const CString& text)
{
	std::vector<SeriesInfo> out;
	CString t = text;
	t.Replace(L"\r\n", L"\n");
	int pos = 0;
	while (pos <= t.GetLength())
	{
		int nl = t.Find(L'\n', pos);
		if (nl < 0) nl = t.GetLength();
		const CString line = t.Mid(pos, nl - pos);
		pos = nl + 1;
		SeriesInfo s;
		int fpos = 0, fi = 0;
		for (;;)
		{
			const int sep = line.Find(kSeriesSep, fpos);
			CString f = (sep < 0) ? line.Mid(fpos) : line.Mid(fpos, sep - fpos);
			f.Trim();
			if (fi == 0) s.name = f; else if (fi == 1) s.code = f; else if (fi == 2) s.label = f; else if (fi == 3) s.desc = f;
			++fi;
			if (sep < 0) break;
			fpos = sep + 1;
		}
		if (!s.code.IsEmpty())
		{
			// 예전 형식 (이름 ␟ 품번 ␟ …): 시리즈 = 품번
			s.name = s.code;
			s.code.Empty();
		}
		if (s.name.IsEmpty())
			continue;
		bool dup = false;
		for (const SeriesInfo& o : out)
			if (o.name.CompareNoCase(s.name) == 0) { dup = true; break; }
		if (!dup)
			out.push_back(s);
	}
	return out;
}

CString CVideoLibrary::JoinSeries(const std::vector<SeriesInfo>& list)
{
	CString out;
	for (const SeriesInfo& s : list)
	{
		CString name = s.name;
		name.Trim();
		if (name.IsEmpty())
			continue;
		auto clean = [](CString f) { f.Replace(L'\n', L' '); f.Replace(L'\r', L' '); f.Remove(kSeriesSep); f.Trim(); return f; };
		CString line = clean(name);
		const CString label = clean(s.label), desc = clean(s.desc);
		if (!label.IsEmpty() || !desc.IsEmpty())
			line += CString(kSeriesSep) + kSeriesSep + label + kSeriesSep + desc;   // 둘째 칸(예전 품번)은 비움
		if (!out.IsEmpty()) out += L"\n";
		out += line;
	}
	return out;
}

std::vector<CString> CVideoLibrary::SeriesNames(const CString& text)
{
	std::vector<CString> out;
	for (const SeriesInfo& s : ParseSeries(text))
		out.push_back(s.name);
	return out;
}

CString CVideoLibrary::SeriesForCode(const CString& code) const
{
	SeriesInfo s;
	return SeriesInfoForCode(code, s) ? s.name : CString();
}

bool CVideoLibrary::SeriesInfoForCode(const CString& code, SeriesInfo& out) const
{
	CString c = code;
	c.Trim();
	c.MakeUpper();
	if (c.IsEmpty())
		return false;
	// 품번의 접두어: '-' 앞 (없으면 앞쪽 영문자)
	CString prefix;
	const int dash = c.Find(L'-');
	if (dash > 0)
		prefix = c.Left(dash);
	else
		for (int i = 0; i < c.GetLength() && iswalpha(c[i]); ++i) prefix += c[i];
	auto match = [&](const std::vector<SeriesInfo>& list) -> bool
	{
		for (const SeriesInfo& s : list)
		{
			for (CString p : SplitList(s.name))   // 시리즈 이름 = 품번 접두어 (쉼표로 여러 개도 가능)
			{
				p.Trim();
				p.MakeUpper();
				p.TrimRight(L'-');
				if (p.IsEmpty())
					continue;
				if (p == prefix || (p.Find(L'-') > 0 && c.Left(p.GetLength()) == p))   // "SONE" 또는 "SONE-4" 처럼 더 긴 접두어
				{
					out = s;
					return true;
				}
			}
		}
		return false;
	};
	for (const NamedInfo& lb : labelInfos)
		if (match(ParseSeries(lb.series))) return true;
	for (const NamedInfo& st : studios)
		if (match(ParseSeries(st.series))) return true;
	return false;
}

int CVideoLibrary::FindStudioOfSeries(const CString& series, CString* seriesName) const
{
	CString nm = series;
	nm.Trim();
	if (nm.IsEmpty())
		return -1;
	for (size_t i = 0; i < studios.size(); ++i)
		for (const CString& s : SeriesNames(studios[i].series))
			if (s.CompareNoCase(nm) == 0)
			{
				if (seriesName) *seriesName = s;
				return static_cast<int>(i);
			}
	return -1;
}

std::vector<CString> CVideoLibrary::AllSeries() const
{
	std::vector<CString> out;
	std::set<CString> seen;
	auto add = [&](const CString& s)
	{
		CString k = s;
		k.Trim();
		if (k.IsEmpty()) return;
		CString lk = k;
		lk.MakeLower();
		if (seen.insert(lk).second) out.push_back(k);
	};
	for (const NamedInfo& st : studios)
		for (const CString& s : SeriesNames(st.series))
			add(s);
	for (const NamedInfo& lb : labelInfos)
		for (const CString& s : SeriesNames(lb.series))
			add(s);
	for (const VideoItem& v : items)
		add(v.series);
	std::sort(out.begin(), out.end(), [](const CString& a, const CString& b) { return ::StrCmpLogicalW(a, b) < 0; });
	return out;
}

void CVideoLibrary::RenameLabelInVideos(const CString& oldName, const CString& newName)
{
	for (VideoItem& v : items)
		if (!v.label.IsEmpty() && v.label.CompareNoCase(oldName) == 0)
			v.label = newName;
}

int CVideoLibrary::RemoveLabel(int labelIdx)
{
	if (labelIdx < 0 || labelIdx >= static_cast<int>(labelInfos.size()))
		return 0;
	const CString name = labelInfos[labelIdx].name;
	int affected = 0;
	for (VideoItem& v : items)
		if (!v.label.IsEmpty() && v.label.CompareNoCase(name) == 0) { v.label.Empty(); ++affected; }
	labelInfos.erase(labelInfos.begin() + labelIdx);
	return affected;
}

void CVideoLibrary::SetLabelParent(int labelIdx, const CString& parent)
{
	if (labelIdx < 0 || labelIdx >= static_cast<int>(labelInfos.size()))
		return;
	NamedInfo& lb = labelInfos[labelIdx];
	if (lb.parent == parent)
		return;
	lb.parent = parent;
	if (parent.IsEmpty())
		return;   // 상위 없음으로 바꾸면 영상의 제작사는 그대로
	for (VideoItem& v : items)
		if (!v.label.IsEmpty() && v.label.CompareNoCase(lb.name) == 0)
			v.studio = parent;   // 레이블은 상위 제작사를 따라감
}

int CVideoLibrary::StudioToLabel(int studioIdx, const CString& parent)
{
	if (studioIdx < 0 || studioIdx >= static_cast<int>(studios.size()))
		return -1;
	NamedInfo n = studios[studioIdx];
	n.parent = parent;
	for (VideoItem& v : items)
	{
		if (v.studio.CompareNoCase(n.name) == 0)
		{
			v.studio = parent;   // 상위 없음이면 제작사 칸은 비움
			v.label = n.name;
		}
	}
	studios.erase(studios.begin() + studioIdx);
	labelInfos.push_back(n);
	return static_cast<int>(labelInfos.size()) - 1;
}

int CVideoLibrary::LabelToStudio(int labelIdx)
{
	if (labelIdx < 0 || labelIdx >= static_cast<int>(labelInfos.size()))
		return -1;
	NamedInfo n = labelInfos[labelIdx];
	n.parent.Empty();
	for (VideoItem& v : items)
	{
		if (!v.label.IsEmpty() && v.label.CompareNoCase(n.name) == 0)
		{
			v.studio = n.name;
			v.label.Empty();
		}
	}
	labelInfos.erase(labelInfos.begin() + labelIdx);
	studios.push_back(n);
	return static_cast<int>(studios.size()) - 1;
}

int CVideoLibrary::RemoveNamedFromVideos(int kind, const CString& name)
{
	int affected = 0;
	if (kind == LIST_STUDIO)   // 제작사를 지우면 그 레이블도 지움
		labelInfos.erase(std::remove_if(labelInfos.begin(), labelInfos.end(),
			[&](const NamedInfo& lb) { return lb.parent.CompareNoCase(name) == 0; }), labelInfos.end());
	for (VideoItem& v : items)
	{
		if (kind == LIST_STUDIO)
		{
			CString s = v.studio;
			s.Trim();
			if (s.CompareNoCase(name) == 0)
			{
				v.studio.Empty();
				v.label.Empty();   // 레이블은 스튜디오 하위
				++affected;
			}
		}
		else
		{
			std::vector<CString> list = SplitList(v.tags);
			const size_t before = list.size();
			list.erase(std::remove_if(list.begin(), list.end(),
				[&](const CString& n) { return n.CompareNoCase(name) == 0; }), list.end());
			if (list.size() != before)
			{
				v.tags = JoinList(list);
				++affected;
			}
		}
	}
	return affected;
}

std::map<CString, int> CVideoLibrary::CountNamed(int kind) const
{
	std::map<CString, int> counts;
	for (const VideoItem& v : items)
	{
		for (const CString& n : VideoNamedValues(v, kind))
		{
			CString key = n;
			key.MakeLower();
			++counts[key];
		}
	}
	return counts;
}

// ---------------------------------------------------------------------------
// 배우 폴더의 텍스트 파일(항목: 값)에서 배우 정보 읽기

CString CVideoLibrary::FindActorFolder(const CString& videoPath, const CString& actorName)
{
	CString dir = videoPath;
	for (int depth = 0; depth < 8; ++depth)
	{
		const int slash = dir.ReverseFind(L'\\');
		if (slash <= 2)
			break;
		dir = dir.Left(slash);
		CString name = ::PathFindFileNameW(dir);
		RemoveListCommas(name);
		name.Trim();
		if (name.CompareNoCase(actorName) == 0)
			return dir;
	}
	return CString();
}

namespace
{
	const wchar_t kVideoTxtHeader[] = L"# RuliManager 영상 정보";
	const wchar_t kActorTxtHeader[] = L"# RuliManager 배우 정보";

	// 텍스트 파일 읽기: UTF-8(BOM 유무) / UTF-16 LE·BE(BOM) / 그 외는 시스템 코드 페이지(한국어 Windows 는 CP949)
	bool ReadTextAuto(const CString& path, CString& out)
	{
		std::vector<BYTE> d;
		if (!ReadWholeFile(path, d) || d.empty() || d.size() > 1024 * 1024)
			return false;
		if (d.size() >= 2 && d[0] == 0xFF && d[1] == 0xFE)
		{
			out = CString(reinterpret_cast<const wchar_t*>(d.data() + 2), static_cast<int>((d.size() - 2) / 2));
			return true;
		}
		if (d.size() >= 2 && d[0] == 0xFE && d[1] == 0xFF)
		{
			std::wstring w;
			for (size_t i = 2; i + 1 < d.size(); i += 2)
				w.push_back(static_cast<wchar_t>((d[i] << 8) | d[i + 1]));
			out = w.c_str();
			return true;
		}
		size_t start = (d.size() >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) ? 3 : 0;
		const char* p = reinterpret_cast<const char*>(d.data() + start);
		const int n = static_cast<int>(d.size() - start);
		UINT cp = CP_UTF8;
		if (start == 0 && ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, p, n, nullptr, 0) == 0)
			cp = CP_ACP;   // UTF-8 이 아니면 ANSI(CP949 등)
		const int len = ::MultiByteToWideChar(cp, 0, p, n, nullptr, 0);
		if (len <= 0)
			return false;
		std::wstring w(static_cast<size_t>(len), L'\0');
		::MultiByteToWideChar(cp, 0, p, n, &w[0], len);
		out = w.c_str();
		return true;
	}

	// 날짜 → "YYYY-MM-DD" (1998-02-17 / 1998.2.17 / 1998/02/17 / 1998년 2월 17일 / 19980217 / 1998-02 → 1998-02-01)
	CString ParseDateText(const CString& s)
	{
		std::vector<int> nums;
		CString digits;
		for (int i = 0; i <= s.GetLength(); ++i)
		{
			const wchar_t ch = (i < s.GetLength()) ? s[i] : L' ';
			if (ch >= L'0' && ch <= L'9')
				digits += ch;
			else if (!digits.IsEmpty())
			{
				nums.push_back(_wtoi(digits));
				if (digits.GetLength() == 8 && nums.size() == 1)
				{
					const int v = nums.back();
					nums.back() = v / 10000;
					nums.push_back(v / 100 % 100);
					nums.push_back(v % 100);
				}
				digits.Empty();
				if (nums.size() >= 3)
					break;
			}
		}
		if (nums.size() < 2 || nums[0] < 1900 || nums[0] > 2100 || nums[1] < 1 || nums[1] > 12)
			return CString();
		const int day = (nums.size() >= 3 && nums[2] >= 1 && nums[2] <= 31) ? nums[2] : 1;
		CString r;
		r.Format(L"%04d-%02d-%02d", nums[0], nums[1], day);
		return r;
	}

	// 문자열 안의 첫 숫자 (없으면 0)
	int FirstNumber(const CString& s, int from = 0)
	{
		int i = from;
		while (i < s.GetLength() && !(s[i] >= L'0' && s[i] <= L'9'))
			++i;
		CString d;
		while (i < s.GetLength() && s[i] >= L'0' && s[i] <= L'9')
			d += s[i++];
		return d.IsEmpty() ? 0 : _wtoi(d);
	}

	// 컵 문자 (A~Q) — "I컵", "I cup", "(I)" 등에서
	CString ParseCup(const CString& s)
	{
		for (int i = 0; i < s.GetLength(); ++i)
		{
			wchar_t ch = s[i];
			if (ch >= L'ａ' && ch <= L'ｚ') ch = static_cast<wchar_t>(ch - L'ａ' + L'a');
			if (ch >= L'Ａ' && ch <= L'Ｚ') ch = static_cast<wchar_t>(ch - L'Ａ' + L'A');
			const wchar_t up = static_cast<wchar_t>(towupper(ch));
			if (up >= L'A' && up <= L'Q')
			{
				// 앞뒤가 다른 영문자가 아니어야 함 (예: "Bust" 의 B 제외)
				const bool prevAlpha = i > 0 && iswalpha(s[i - 1]) && s[i - 1] < 0x80;
				const bool nextAlpha = i + 1 < s.GetLength() && iswalpha(s[i + 1]) && s[i + 1] < 0x80
					&& !(towlower(s[i + 1]) == L'c' && i + 3 < s.GetLength() && towlower(s[i + 2]) == L'u');   // "Icup"
				if (!prevAlpha && !nextAlpha)
					return CString(up);
			}
		}
		return CString();
	}

	// 국적 이름 → 국적 목록의 이름 (일본 / Japan / 日本 / JP / 일본인 → 일본)
	CString ParseCountry(CString s)
	{
		s.Trim();
		if (s.IsEmpty())
			return s;
		if (CCountryCombo::FindCountry(s) >= 0)
		{
			int count = 0;
			return CCountryCombo::Countries(count)[CCountryCombo::FindCountry(s)].name;
		}
		if (s.Right(1) == L"인" && CCountryCombo::FindCountry(s.Left(s.GetLength() - 1)) >= 0)
			return ParseCountry(s.Left(s.GetLength() - 1));
		static const struct { LPCWSTR key; LPCWSTR name; } kMap[] = {
			{ L"japan", L"일본" }, { L"japanese", L"일본" }, { L"日本", L"일본" }, { L"한국", L"대한민국" },
			{ L"korea", L"대한민국" }, { L"south korea", L"대한민국" }, { L"korean", L"대한민국" }, { L"韓国", L"대한민국" },
			{ L"china", L"중국" }, { L"chinese", L"중국" }, { L"中国", L"중국" }, { L"taiwan", L"대만" }, { L"台湾", L"대만" },
			{ L"hong kong", L"홍콩" }, { L"香港", L"홍콩" }, { L"usa", L"미국" }, { L"united states", L"미국" },
			{ L"america", L"미국" }, { L"アメリカ", L"미국" }, { L"uk", L"영국" }, { L"united kingdom", L"영국" },
			{ L"russia", L"러시아" }, { L"ロシア", L"러시아" }, { L"thailand", L"태국" }, { L"タイ", L"태국" },
			{ L"philippines", L"필리핀" }, { L"vietnam", L"베트남" },
		};
		for (const auto& m : kMap)
		{
			if (s.CompareNoCase(m.key) == 0)
				return m.name;
		}
		return s;   // 목록에 없는 국적은 그대로 (국기 없이 표시)
	}

	// 항목 이름 정리: 공백·기호 제거 + 소문자
	CString NormKey(CString k)
	{
		CString r;
		for (int i = 0; i < k.GetLength(); ++i)
		{
			const wchar_t ch = k[i];
			if (ch == L' ' || ch == L'\t' || ch == L'　' || ch == L'-' || ch == L'_' || ch == L'.' || ch == L'*' || ch == L'#'
				|| ch == L'[' || ch == L']' || ch == L'【' || ch == L'】' || ch == L'(' || ch == L')')
				continue;
			r += static_cast<wchar_t>(towlower(ch));
		}
		return r;
	}

	// 비교용: 공백 제거 + 소문자
	CString CompactLower(const CString& s)
	{
		CString r;
		for (int i = 0; i < s.GetLength(); ++i)
		{
			const wchar_t ch = s[i];
			if (ch == L' ' || ch == L'\t' || ch == L'　')
				continue;
			r += static_cast<wchar_t>(towlower(ch));
		}
		return r;
	}

	// 다른 이름이 배우 이름과 같은지: 전체 이름, 괄호 앞 이름, 괄호 안의 각 이름(쉼표 구분) 중 하나와 같으면 true (공백·대소문자 무시)
	//  예: 배우 "나기 히카루(Hikaru Nagi, 凪ひかる)" → "나기 히카루" / "Hikaru Nagi" / "凪ひかる" / "나기히카루" 는 같은 이름
	bool IsSameAsActorName(const CString& alias, const CString& actorName)
	{
		// 언어 단위 비교: "나기 히카루(凪ひかる)" 는 배우 "나기 히카루(Hikaru Nagi, 凪ひかる)" 와 한글(또는 일어) 이름이 같으므로 같은 이름
		if (CompactLower(alias).IsEmpty())
			return true;
		return CVideoLibrary::SameNameByLang(alias, actorName);
	}

	bool KeyIs(const CString& k, std::initializer_list<LPCWSTR> names)
	{
		for (LPCWSTR n : names)
			if (k == n)
				return true;
		return false;
	}
}

bool CVideoLibrary::ApplyActorTextInfo(ActorInfo& a, const CString& file)
{
	CString text;
	if (!ReadTextAuto(file, text))
		return false;
	return ApplyActorText(a, text);
}

void CVideoLibrary::ClearActorTextFields(ActorInfo& a)
{
	a.birth.Empty(); a.height.Empty(); a.bust.Empty(); a.waist.Empty(); a.hip.Empty(); a.cup.Empty();
	a.nationality.Empty(); a.gender.Empty(); a.debut.Empty(); a.retire.Empty(); a.aliases.Empty(); a.urls.Empty();
}

bool CVideoLibrary::ApplyActorText(ActorInfo& a, CString text)
{
	if (text.Find(kVideoTxtHeader) >= 0)
		return false;   // 이 프로그램이 만든 영상 정보 파일은 배우 정보로 읽지 않음
	text.Replace(L"\r\n", L"\n");
	text.Replace(L'\r', L'\n');

	bool changed = false;
	auto setIfEmpty = [&changed](CString& field, const CString& value)
	{
		if (field.IsEmpty() && !value.IsEmpty())
		{
			field = value;
			changed = true;
		}
	};

	int pos = 0;
	while (pos <= text.GetLength())
	{
		int nl = text.Find(L'\n', pos);
		if (nl < 0) nl = text.GetLength();
		CString line = text.Mid(pos, nl - pos);
		pos = nl + 1;
		line.Trim();
		if (line.IsEmpty())
			continue;
		// "항목: 값" / "항목：값" / "항목=값" / "항목<탭>값"
		int sep = -1;
		const wchar_t seps[] = { L':', L'：', L'=', L'\t' };
		for (wchar_t s : seps)
		{
			const int at = line.Find(s);
			if (at > 0 && (sep < 0 || at < sep))
				sep = at;
		}
		if (sep <= 0)
			continue;
		const CString key = NormKey(line.Left(sep));
		CString val = line.Mid(sep + 1);
		val.Trim();
		if (val.IsEmpty() || val == L"-" || val.CompareNoCase(L"n/a") == 0 || val == L"不明" || val == L"없음")
			continue;

		if (KeyIs(key, { L"생년월일", L"생일", L"출생", L"출생일", L"birthday", L"birth", L"birthdate", L"born", L"dob", L"dateofbirth", L"生年月日", L"誕生日" }))
			setIfEmpty(a.birth, ParseDateText(val));
		else if (KeyIs(key, { L"키", L"신장", L"height", L"身長" }))
		{
			const int h = FirstNumber(val);
			if (h >= 100 && h <= 250) { CString t; t.Format(L"%d", h); setIfEmpty(a.height, t); }
		}
		else if (KeyIs(key, { L"치수", L"쓰리사이즈", L"3사이즈", L"사이즈", L"신체사이즈", L"measurements", L"measurement", L"sizes", L"threesizes", L"bwh", L"スリーサイズ", L"サイズ" }))
		{
			// "B105 / W59 / H88", "105-59-88", "B105(I) W59 H88", "105I-59-88"
			int b = 0, w = 0, hh = 0;
			CString up = val;
			up.MakeUpper();
			const int pb = up.Find(L'B'), pw = up.Find(L'W'), ph = up.Find(L'H');
			if (pb >= 0 && pw > pb && ph > pw)
			{
				b = FirstNumber(up, pb); w = FirstNumber(up, pw); hh = FirstNumber(up, ph);
			}
			else if (FirstNumber(up) > 0)
			{
				b = FirstNumber(up);
				int i = up.Find(std::to_wstring(b).c_str()) + static_cast<int>(std::to_wstring(b).size());
				w = FirstNumber(up, i);
				i = up.Find(std::to_wstring(w).c_str(), i) + static_cast<int>(std::to_wstring(w).size());
				hh = FirstNumber(up, i);
			}
			auto num = [](int v) { CString t; if (v >= 40 && v <= 200) t.Format(L"%d", v); return t; };
			setIfEmpty(a.bust, num(b));
			setIfEmpty(a.waist, num(w));
			setIfEmpty(a.hip, num(hh));
			// 치수 안의 컵 ("B105(I)" / "105I")
			const int paren = val.Find(L'(');
			if (paren >= 0)
				setIfEmpty(a.cup, ParseCup(val.Mid(paren)));
		}
		else if (KeyIs(key, { L"가슴", L"바스트", L"bust", L"バスト" }))
		{
			CString t; const int v = FirstNumber(val); if (v >= 40 && v <= 200) t.Format(L"%d", v);
			setIfEmpty(a.bust, t);
			const int paren = val.Find(L'(');
			if (paren >= 0) setIfEmpty(a.cup, ParseCup(val.Mid(paren)));
		}
		else if (KeyIs(key, { L"허리", L"웨이스트", L"waist", L"ウエスト" }))
		{
			CString t; const int v = FirstNumber(val); if (v >= 30 && v <= 150) t.Format(L"%d", v);
			setIfEmpty(a.waist, t);
		}
		else if (KeyIs(key, { L"엉덩이", L"힙", L"hip", L"hips", L"ヒップ" }))
		{
			CString t; const int v = FirstNumber(val); if (v >= 40 && v <= 200) t.Format(L"%d", v);
			setIfEmpty(a.hip, t);
		}
		else if (KeyIs(key, { L"컵", L"컵사이즈", L"cup", L"cupsize", L"カップ", L"ブラ" }))
			setIfEmpty(a.cup, ParseCup(val));
		else if (KeyIs(key, { L"국적", L"국가", L"출신", L"출신지", L"nationality", L"country", L"国籍", L"出身", L"出身地" }))
			setIfEmpty(a.nationality, ParseCountry(val));
		else if (KeyIs(key, { L"성별", L"gender", L"sex", L"性別" }))
			setIfEmpty(a.gender, NormalizeGender(val));
		else if (KeyIs(key, { L"데뷔", L"데뷔일", L"debut", L"careerstart", L"activefrom", L"デビュー", L"デビュー日" }))
			setIfEmpty(a.debut, ParseDateText(val));
		else if (KeyIs(key, { L"은퇴", L"은퇴일", L"retire", L"retired", L"careerend", L"引退", L"引退日" }))
			setIfEmpty(a.retire, ParseDateText(val));
		else if (KeyIs(key, { L"별칭", L"별명", L"다른이름", L"예명", L"aliases", L"alias", L"aka", L"別名", L"旧芸名" }))
		{
			CString v = val;
			v.Replace(L'、', L',');
			v.Replace(L'，', L',');
			v.Replace(L'/', L',');
			v.Replace(L'#', L',');    // "#이름1 #이름2" 처럼 # 로 구분된 다른 이름
			v.Replace(L'＃', L',');   // 전각 ＃
			std::vector<CString> list = SplitList(a.aliases);
			std::vector<CString> found = SplitList(v);
			std::reverse(found.begin(), found.end());   // 파일에 적힌 순서의 반대로 추가 (마지막 이름부터)
			for (CString n : found)
			{
				n.Trim();
				if (n.IsEmpty() || IsSameAsActorName(n, a.name))   // 배우 이름(괄호 앞 이름 · 괄호 안 이름 포함)과 같으면 추가 안 함
					continue;
				bool dup = false;
				for (const CString& e : list)
					if (CompactLower(e) == CompactLower(n) || SameNameByLang(e, n)) { dup = true; break; }   // 공백·대소문자만 다른 것, 언어 단위로 같은 이름도 중복
				if (!dup)
				{
					list.push_back(n);
					changed = true;
				}
			}
			a.aliases = JoinList(list);
		}
		else if (KeyIs(key, { L"메모", L"설명", L"소개", L"memo", L"note", L"notes", L"details", L"bio", L"プロフィール" }))
			setIfEmpty(a.memo, val);
		else if (KeyIs(key, { L"url", L"urls", L"링크", L"주소", L"홈페이지", L"사이트", L"website", L"site", L"homepage", L"link", L"links",
		                      L"sns", L"twitter", L"x", L"instagram", L"인스타그램", L"트위터", L"公式", L"公式サイト", L"リンク" }))
		{
			// URL: 여러 줄 / 한 줄에 여러 개(공백 · | 구분) 모두 추가 (이미 있으면 건너뜀)
			std::vector<CString> list = SplitUrls(a.urls);
			for (const CString& u : SplitUrls(val))
			{
				bool dup = false;
				for (const CString& e : list)
					if (e.CompareNoCase(u) == 0) { dup = true; break; }
				if (!dup) { list.push_back(u); changed = true; }
			}
			a.urls = JoinUrls(list);
		}
	}
	return changed;
}

std::vector<CString> CVideoLibrary::SplitUrls(const CString& text)
{
	std::vector<CString> out;
	CString t = text;
	t.Replace(L"\r", L"\n");
	t.Replace(L'\t', L'\n');
	t.Replace(L' ', L'\n');
	t.Replace(L'|', L'\n');
	t.Replace(L'\x3000', L'\n');
	int start = 0;
	for (;;)
	{
		CString u = t.Tokenize(L"\n", start);
		if (start < 0)
			break;
		u.Trim();
		u.Trim(L",;");
		if (u.IsEmpty())
			continue;
		bool dup = false;
		for (const CString& e : out)
			if (e.CompareNoCase(u) == 0) { dup = true; break; }
		if (!dup)
			out.push_back(u);
	}
	return out;
}

std::vector<CString> CVideoLibrary::SplitLines(const CString& text)
{
	std::vector<CString> out;
	CString t = text;
	t.Replace(L"\r", L"\n");
	int start = 0;
	for (;;)
	{
		CString line = t.Tokenize(L"\n", start);
		if (start < 0)
			break;
		line.Trim();
		if (line.IsEmpty())
			continue;
		bool dup = false;
		for (const CString& e : out)
			if (e.CompareNoCase(line) == 0) { dup = true; break; }
		if (!dup)
			out.push_back(line);
	}
	return out;
}

CString CVideoLibrary::JoinLines(const std::vector<CString>& lines)
{
	CString r;
	for (const CString& l : lines)
	{
		if (!r.IsEmpty()) r += L"\n";
		r += l;
	}
	return r;
}

CString CVideoLibrary::JoinUrls(const std::vector<CString>& urls)
{
	CString r;
	for (const CString& u : urls)
	{
		if (!r.IsEmpty()) r += L"\n";
		r += u;
	}
	return r;
}

bool CVideoLibrary::HasNoActorInfo(const ActorInfo& a)
{
	return a.birth.IsEmpty() && a.height.IsEmpty() && a.nationality.IsEmpty() && a.debut.IsEmpty()
		&& a.retire.IsEmpty() && a.bust.IsEmpty() && a.waist.IsEmpty() && a.hip.IsEmpty() && a.cup.IsEmpty();
}

CString CVideoLibrary::FindActorTextFile(const CString& dir, const CString& actorName)
{
	std::vector<CString> files;
	WIN32_FIND_DATAW fd = {};
	HANDLE h = ::FindFirstFileW(dir + L"\\*.txt", &fd);
	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				files.push_back(fd.cFileName);
		} while (::FindNextFileW(h, &fd));
		::FindClose(h);
	}
	if (files.empty())
		return CString();
	std::sort(files.begin(), files.end(), [](const CString& x, const CString& y) { return ::StrCmpLogicalW(x, y) < 0; });
	const CString folderName = ::PathFindFileNameW(dir);
	const CString preferred[] = { folderName, actorName, L"profile", L"info", L"actor", L"프로필", L"정보" };
	for (const CString& p : preferred)
	{
		for (const CString& f : files)
		{
			CString stem = f;
			::PathRemoveExtensionW(stem.GetBuffer());
			stem.ReleaseBuffer();
			if (stem.CompareNoCase(p) == 0)
				return dir + L"\\" + f;
		}
	}
	return dir + L"\\" + files.front();
}

// ---------------------------------------------------------------------------
// 배우 / 영상 정보를 txt 로 내보내기 (같은 폴더, UTF-8, "항목: 값" — 배우 txt 는 다시 읽어 들일 수 있는 형식)

namespace
{
	// 내용이 같으면 쓰지 않음 (수정 시각 유지). 반환: 1 = 씀, 0 = 같아서 건너뜀, -1 = 실패
	int WriteTextIfChanged(const CString& path, const CString& text)
	{
		const CStringA utf8(CW2A(text, CP_UTF8));
		std::vector<BYTE> data = { 0xEF, 0xBB, 0xBF };
		data.insert(data.end(), reinterpret_cast<const BYTE*>(static_cast<LPCSTR>(utf8)),
			reinterpret_cast<const BYTE*>(static_cast<LPCSTR>(utf8)) + utf8.GetLength());
		std::vector<BYTE> old;
		if (ReadWholeFile(path, old) && old == data)
			return 0;
		HANDLE h = ::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE)
			return -1;
		DWORD written = 0;
		const bool ok = ::WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
		::CloseHandle(h);
		return ok ? 1 : -1;
	}

	void AddLine(CString& t, LPCWSTR key, const CString& value)
	{
		if (value.IsEmpty())
			return;
		CString v = value;
		v.Replace(L"\r\n", L" ");
		v.Replace(L'\n', L' ');
		t += key;
		t += L": ";
		t += v;
		t += L"\r\n";
	}
}

CString CVideoLibrary::VideoInfoText(const VideoItem& v)
{
	CString t = CString(kVideoTxtHeader) + L"\r\n";
	AddLine(t, L"파일", v.FileName());
	AddLine(t, L"품번", v.code);
	AddLine(t, L"제목", v.title);
	AddLine(t, L"발매일", v.release);
	if (v.rating > 0) { CString r; r.Format(L"%d", v.rating); AddLine(t, L"별점", r); }
	AddLine(t, L"배우", v.actors);
	AddLine(t, L"참여 별칭", v.actorAliases);
	AddLine(t, L"제작사", v.studio);
	AddLine(t, L"레이블", v.label);
	AddLine(t, L"시리즈", v.series);
	AddLine(t, L"태그", v.tags);
	if (v.oCount > 0) { CString o; o.Format(L"%d", v.oCount); AddLine(t, L"물방울", o); }
	return t;
}

void CVideoLibrary::ClearVideoTextFields(VideoItem& v)
{
	v.code.Empty(); v.title.Empty(); v.release.Empty(); v.rating = 0; v.oCount = 0;
	v.actors.Empty(); v.actorAliases.Empty(); v.studio.Empty(); v.label.Empty(); v.series.Empty(); v.tags.Empty();
}

CString CVideoLibrary::ActorInfoText(const ActorInfo& a)
{
	CString t = CString(kActorTxtHeader) + L"\r\n";
	AddLine(t, L"이름", a.name);
	{
		// 다른 이름: 읽어 들일 때 순서를 뒤집으므로 거꾸로 써 둠 (다시 읽으면 지금 순서)
		std::vector<CString> al = SplitList(a.aliases);
		CString s;
		for (auto it = al.rbegin(); it != al.rend(); ++it)
		{
			if (!s.IsEmpty()) s += L" ";
			s += L"#" + *it;
		}
		AddLine(t, L"다른이름", s);
	}
	AddLine(t, L"성별", a.gender);
	AddLine(t, L"생년월일", a.birth);
	AddLine(t, L"국적", a.nationality);
	if (!a.height.IsEmpty()) AddLine(t, L"키", a.height + L"cm");
	if (!a.bust.IsEmpty() || !a.waist.IsEmpty() || !a.hip.IsEmpty())
	{
		CString m;
		m.Format(L"B%s / W%s / H%s", static_cast<LPCWSTR>(a.bust), static_cast<LPCWSTR>(a.waist), static_cast<LPCWSTR>(a.hip));
		AddLine(t, L"치수", m);
	}
	AddLine(t, L"컵", a.cup);
	AddLine(t, L"데뷔", a.debut);
	AddLine(t, L"은퇴", a.retire);
	if (a.rating > 0) { CString r; r.Format(L"%d", a.rating); AddLine(t, L"별점", r); }
	if (a.favorite) AddLine(t, L"즐겨찾기", L"예");
	for (const CString& u : SplitUrls(a.urls))
		AddLine(t, L"URL", u);   // 링크는 한 줄에 하나씩
	AddLine(t, L"메모", a.memo);   // 한 줄로
	return t;
}

int CVideoLibrary::ExportVideoInfoTxt(int& unchanged, int& failed) const
{
	int written = 0;
	unchanged = failed = 0;
	for (const VideoItem& v : items)
	{
		if (v.pending || v.path.IsEmpty() || !::PathFileExistsW(v.path))
			continue;   // 임시 항목 · 없는 파일은 제외
		CString t = CString(kVideoTxtHeader) + L"\r\n";
		AddLine(t, L"파일", v.FileName());
		AddLine(t, L"품번", v.code);
		AddLine(t, L"제목", v.title);
		AddLine(t, L"발매일", v.release);
		if (v.rating > 0) { CString r; r.Format(L"%d", v.rating); AddLine(t, L"별점", r); }
		AddLine(t, L"배우", v.actors);
		AddLine(t, L"참여 별칭", v.actorAliases);
		AddLine(t, L"제작사", v.studio);
		AddLine(t, L"레이블", v.label);
		AddLine(t, L"시리즈", v.series);
		AddLine(t, L"태그", v.tags);
		if (v.oCount > 0) { CString o; o.Format(L"%d", v.oCount); AddLine(t, L"물방울", o); }
		// 영상과 같은 폴더, 같은 이름 (ABC-123.mp4 → ABC-123.txt)
		CString path = v.path;
		const int ext = static_cast<int>(::PathFindExtensionW(path) - static_cast<LPCWSTR>(path));
		if (ext > 0) path = path.Left(ext);
		const int r = WriteTextIfChanged(path + L".txt", t);
		if (r > 0) ++written; else if (r == 0) ++unchanged; else ++failed;
	}
	return written;
}

int CVideoLibrary::ExportActorInfoTxt(int& unchanged, int& noFolder, int& failed) const
{
	int written = 0;
	unchanged = noFolder = failed = 0;
	// 배우별 출연 영상 경로
	const std::map<CString, int> index = ActorNameIndex();
	std::map<int, std::vector<CString>> videos;
	for (const VideoItem& v : items)
	{
		for (const CString& n : SplitList(v.actors))
		{
			CString key = n;
			key.MakeLower();
			auto it = index.find(key);
			if (it != index.end())
				videos[it->second].push_back(v.path);
		}
	}
	for (size_t i = 0; i < actors.size(); ++i)
	{
		const ActorInfo& a = actors[i];
		// 배우 폴더 찾기 (배우 이름 또는 별칭과 같은 상위 폴더)
		CString folder;
		auto vit = videos.find(static_cast<int>(i));
		if (vit != videos.end())
		{
			std::vector<CString> names = { a.name };
			for (const CString& al : SplitList(a.aliases))
				names.push_back(al);
			for (const CString& path : vit->second)
			{
				for (const CString& nm : names)
				{
					folder = FindActorFolder(path, nm);
					if (!folder.IsEmpty())
						break;
				}
				if (!folder.IsEmpty())
					break;
			}
		}
		if (folder.IsEmpty())
		{
			++noFolder;
			continue;
		}

		CString t = CString(kActorTxtHeader) + L"\r\n";
		AddLine(t, L"이름", a.name);
		{
			// 다른 이름: 읽어 들일 때 순서를 뒤집으므로 거꾸로 써 둠 (다시 읽으면 지금 순서)
			std::vector<CString> al = SplitList(a.aliases);
			CString s;
			for (auto it = al.rbegin(); it != al.rend(); ++it)
			{
				if (!s.IsEmpty()) s += L" ";
				s += L"#" + *it;
			}
			AddLine(t, L"다른이름", s);
		}
		AddLine(t, L"성별", a.gender);
		AddLine(t, L"생년월일", a.birth);
		AddLine(t, L"국적", a.nationality);
		if (!a.height.IsEmpty()) AddLine(t, L"키", a.height + L"cm");
		if (!a.bust.IsEmpty() || !a.waist.IsEmpty() || !a.hip.IsEmpty())
		{
			CString m;
			m.Format(L"B%s / W%s / H%s", static_cast<LPCWSTR>(a.bust), static_cast<LPCWSTR>(a.waist), static_cast<LPCWSTR>(a.hip));
			AddLine(t, L"치수", m);
		}
		AddLine(t, L"컵", a.cup);
		AddLine(t, L"데뷔", a.debut);
		AddLine(t, L"은퇴", a.retire);
		if (a.rating > 0) { CString r; r.Format(L"%d", a.rating); AddLine(t, L"별점", r); }
		if (a.favorite) AddLine(t, L"즐겨찾기", L"예");
		for (const CString& u : SplitUrls(a.urls))
			AddLine(t, L"URL", u);   // 링크는 한 줄에 하나씩
		AddLine(t, L"메모", a.memo);   // 한 줄로

		const CString path = folder + L"\\" + ::PathFindFileNameW(folder) + L".txt";   // 배우 폴더 이름.txt
		const int r = WriteTextIfChanged(path, t);
		if (r > 0) ++written; else if (r == 0) ++unchanged; else ++failed;
	}
	return written;
}

// ---------------------------------------------------------------------------
// 영상 폴더의 텍스트 파일(항목: 값)에서 영상 정보 읽기

CString CVideoLibrary::FindVideoTextFile(const CString& videoPath)
{
	if (videoPath.IsEmpty())
		return CString();
	const CString dir = videoPath.Left(static_cast<int>(::PathFindFileNameW(videoPath) - static_cast<LPCWSTR>(videoPath)));
	CString stem = ::PathFindFileNameW(videoPath);
	::PathRemoveExtensionW(stem.GetBuffer());
	stem.ReleaseBuffer();

	// 0) 상위 폴더(영상 폴더) 이름 기준: D:\영상\ABC-123\xxx.mp4 → D:\영상\ABC-123\ABC-123.txt
	//    같은 이름이 없으면 폴더 이름의 '_' 왼쪽과 txt 이름의 '_' 왼쪽이 같은 txt (이름 순 첫 번째). 배우 정보 파일은 제외
	{
		CString folderName = dir;
		folderName.TrimRight(L"\\/");
		folderName = ::PathFindFileNameW(folderName);
		folderName.Trim();
		if (!folderName.IsEmpty() && folderName.Find(L':') < 0)   // 드라이브 루트(D:) 제외
		{
			auto notActorTxt = [](const CString& f)
			{
				CString text;
				return !ReadTextAuto(f, text) || text.Find(kActorTxtHeader) < 0;
			};
			if (::PathFileExistsW(dir + folderName + L".txt") && notActorTxt(dir + folderName + L".txt"))
				return dir + folderName + L".txt";
			auto leftKey = [](const CString& s) { const int us = s.Find(L'_'); CString k = (us >= 0) ? s.Left(us) : s; k.Trim(); return k; };
			const CString fkey = leftKey(folderName);
			std::vector<CString> txts;
			WIN32_FIND_DATAW fd0 = {};
			HANDLE h0 = ::FindFirstFileW(dir + L"*.txt", &fd0);
			if (h0 != INVALID_HANDLE_VALUE)
			{
				do
				{
					if (!(fd0.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
						txts.push_back(fd0.cFileName);
				} while (::FindNextFileW(h0, &fd0));
				::FindClose(h0);
			}
			std::sort(txts.begin(), txts.end(), [](const CString& x, const CString& y) { return ::StrCmpLogicalW(x, y) < 0; });
			for (const CString& f : txts)
			{
				CString t = f;
				::PathRemoveExtensionW(t.GetBuffer());
				t.ReleaseBuffer();
				if (!fkey.IsEmpty() && leftKey(t).CompareNoCase(fkey) == 0 && notActorTxt(dir + f))
					return dir + f;
			}
		}
	}

	// 1) 같은 이름 (ABC-123.mp4 → ABC-123.txt), 2) 확장자 포함 (ABC-123.mp4.txt)
	if (::PathFileExistsW(dir + stem + L".txt"))
		return dir + stem + L".txt";
	if (::PathFileExistsW(videoPath + L".txt"))
		return videoPath + L".txt";

	// 3) '_' 왼쪽이 같은 이름 (ABC-123_1080p.mp4 ↔ ABC-123.txt / ABC-123_info.txt)
	auto keyOf = [](const CString& s) { const int us = s.Find(L'_'); CString k = (us >= 0) ? s.Left(us) : s; k.Trim(); return k; };
	const CString key = keyOf(stem);
	std::vector<CString> all;
	WIN32_FIND_DATAW fd = {};
	HANDLE h = ::FindFirstFileW(dir + L"*.txt", &fd);
	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				all.push_back(fd.cFileName);
		} while (::FindNextFileW(h, &fd));
		::FindClose(h);
	}
	std::sort(all.begin(), all.end(), [](const CString& a, const CString& b) { return ::StrCmpLogicalW(a, b) < 0; });
	for (const CString& f : all)
	{
		CString s = f;
		::PathRemoveExtensionW(s.GetBuffer());
		s.ReleaseBuffer();
		if (!key.IsEmpty() && keyOf(s).CompareNoCase(key) == 0)
			return dir + f;
	}

	// 4) 영상 폴더에 영상이 하나뿐이고 txt 도 하나뿐이면 그 파일 (배우 정보 파일은 제외)
	if (all.size() == 1)
	{
		int videos = 0;
		h = ::FindFirstFileW(dir + L"*", &fd);
		if (h != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && IsVideoFile(fd.cFileName))
					++videos;
			} while (::FindNextFileW(h, &fd) && videos < 2);
			::FindClose(h);
		}
		if (videos == 1)
		{
			CString text;
			if (ReadTextAuto(dir + all[0], text) && text.Find(kActorTxtHeader) < 0)
				return dir + all[0];
		}
	}
	return CString();
}

namespace
{
	// 이름 조각의 언어: 한글 / 영어(라틴) / 일어(가나 · 한자) / 섞임
	enum NameLang { LANG_KO = 0, LANG_EN = 1, LANG_JA = 2, LANG_MIX = 3 };

	int DetectNameLang(const CString& s)
	{
		bool ko = false, en = false, ja = false;
		for (int i = 0; i < s.GetLength(); ++i)
		{
			const wchar_t c = s[i];
			if ((c >= 0xAC00 && c <= 0xD7A3) || (c >= 0x1100 && c <= 0x11FF) || (c >= 0x3130 && c <= 0x318F))
				ko = true;
			else if ((c >= 0x3040 && c <= 0x30FF) || (c >= 0x31F0 && c <= 0x31FF) || (c >= 0xFF66 && c <= 0xFF9F) ||
			         (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0xF900 && c <= 0xFAFF))
				ja = true;
			else if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= 0x00C0 && c <= 0x024F) ||
			         (c >= 0xFF21 && c <= 0xFF3A) || (c >= 0xFF41 && c <= 0xFF5A))
				en = true;
		}
		const int kinds = (ko ? 1 : 0) + (en ? 1 : 0) + (ja ? 1 : 0);
		if (kinds != 1)
			return LANG_MIX;
		return ko ? LANG_KO : (en ? LANG_EN : LANG_JA);
	}

	// 비교 키: 영어는 단어 순서 무시(Hikaru Nagi = Nagi Hikaru, 점 · 하이픈도 공백 취급), 나머지는 공백 제거 + 소문자
	CString NameKey(const CString& s, int lang)
	{
		if (lang != LANG_EN)
			return CompactLower(s);
		CString t = s;
		t.MakeLower();
		const wchar_t seps[] = { L'.', L'-', L'\x00B7', L'\x30FB', L'_', L'\x3000' };
		for (wchar_t c : seps)
			t.Replace(c, L' ');
		std::vector<CString> words;
		int start = 0;
		for (;;)
		{
			CString w = t.Tokenize(L" ", start);
			if (start < 0)
				break;
			w.Trim();
			if (!w.IsEmpty())
				words.push_back(w);
		}
		std::sort(words.begin(), words.end());
		CString k;
		for (const CString& w : words)
			k += w;
		return k;
	}

	struct NamePart { CString key; int lang; };

	// 이름의 비교용 조각들: 전체, 괄호 앞 이름, 괄호 안의 각 이름 (언어별 키)
	//  예: "나기 히카루(Hikaru Nagi, 凪ひかる)" / "나기 히카루, (Hikaru Nagi, 凪ひかる)"
	//      → { 전체(섞임), "나기히카루"(한글), "hikarunagi"(영어, 단어 정렬), "凪ひかる"(일어) }
	std::vector<NamePart> NameParts(const CString& name)
	{
		std::vector<NamePart> parts;
		auto add = [&parts](CString s)
		{
			s.Trim(L" \t,，、;；");
			const int lang = DetectNameLang(s);
			const CString k = NameKey(s, lang);
			if (k.IsEmpty())
				return;
			for (const NamePart& p : parts)
				if (p.key == k && p.lang == lang) return;
			parts.push_back({ k, lang });
		};
		add(name);
		int po = name.Find(L'(');
		const int poW = name.Find(L'（');
		if (po < 0 || (poW >= 0 && poW < po)) po = poW;
		if (po < 0)
			return parts;
		add(name.Left(po));   // "한글이름, (" 처럼 괄호 앞 쉼표가 있어도 떼고 비교
		int pc = name.ReverseFind(L')');
		const int pcW = name.ReverseFind(L'）');
		if (pcW > pc) pc = pcW;
		if (pc < po) pc = name.GetLength();
		CString inner = name.Mid(po + 1, pc - po - 1);
		inner.Replace(L'，', L',');
		inner.Replace(L'、', L',');
		inner.Replace(L'/', L',');
		int start = 0;
		for (;;)
		{
			const int comma = inner.Find(L',', start);
			add((comma < 0) ? inner.Mid(start) : inner.Mid(start, comma - start));
			if (comma < 0)
				break;
			start = comma + 1;
		}
		return parts;
	}
}

int CVideoLibrary::FindActorByNamePart(const CString& name, int exclude) const
{
	// 출연자 이름의 한글 / 영어 / 일어 조각 중 하나라도 배우 이름(또는 별칭)의 같은 언어 조각과 같으면 그 배우
	//  - 같은 언어끼리만 비교, 한 언어가 같으면 다른 언어 이름은 비교하지 않고 동일인으로 봄
	//  - 언어 우선순위: 한글 → 일어 → 영어 → 전체 (여러 배우가 걸리면 먼저 맞은 언어의 배우)
	const std::vector<NamePart> mine = NameParts(name);
	if (mine.empty())
		return -1;
	std::vector<std::vector<NamePart>> cands(actors.size());
	for (size_t i = 0; i < actors.size(); ++i)
	{
		cands[i] = NameParts(actors[i].name);
		for (const CString& al : SplitList(actors[i].aliases))
			for (const NamePart& p : NameParts(al))
				cands[i].push_back(p);
	}
	const int order[] = { LANG_KO, LANG_JA, LANG_EN, LANG_MIX };
	for (int lang : order)
	{
		for (const NamePart& m : mine)
		{
			if (m.lang != lang)
				continue;
			for (size_t i = 0; i < actors.size(); ++i)
				if (static_cast<int>(i) != exclude)
				for (const NamePart& c : cands[i])
					if (c.lang == lang && c.key == m.key)
						return static_cast<int>(i);
		}
	}
	return -1;
}

int CVideoLibrary::FindLabelLoose(const CString& label) const
{
	CString nm = label;
	nm.Trim();
	if (nm.IsEmpty())
		return -1;
	auto loose = [](const CString& s)
	{
		CString k = CompactLower(s);
		k.Remove(L'-'); k.Remove(L'.'); k.Remove(L'_'); k.Remove(L'\x30FB'); k.Remove(L'\x00B7');
		return k;
	};
	// 그대로(대소문자 무시) 먼저, 다음에 공백 · 기호 무시 - 레이블 이름 · 서브이름
	for (int pass = 0; pass < 2; ++pass)
	{
		const CString key = pass ? loose(nm) : nm;
		if (key.IsEmpty())
			continue;
		for (size_t i = 0; i < labelInfos.size(); ++i)
		{
			const NamedInfo& lb = labelInfos[i];
			std::vector<CString> names = SplitLines(lb.subName);
			names.insert(names.begin(), lb.name);
			for (const CString& l : names)
				if (pass ? (loose(l) == key) : (l.CompareNoCase(key) == 0))
					return static_cast<int>(i);
		}
	}
	return -1;
}

int CVideoLibrary::FindStudioByLabel(const CString& label, CString* labelName) const
{
	const int li = FindLabelLoose(label);
	if (li < 0)
		return -1;
	const int owner = FindNamed(LIST_STUDIO, labelInfos[li].parent);   // 상위 제작사가 없는 레이블이면 -1
	if (owner >= 0 && labelName)
		*labelName = labelInfos[li].name;
	return owner;
}

void CVideoLibrary::ResolveVideoLabel(VideoItem& v) const
{
	v.studio.Trim();
	v.label.Trim();
	// 제작사 칸에 등록된 레이블 이름이 적혀 있으면 (제작사 이름이 아닐 때) → 상위 제작사(없으면 빈칸) + 레이블
	if (!v.studio.IsEmpty() && FindStudioLoose(v.studio) < 0)
	{
		const int li = FindLabelLoose(v.studio);
		if (li >= 0)
		{
			if (v.label.IsEmpty() || FindLabelLoose(v.label) == li)
				v.label = labelInfos[li].name;
			const int owner = FindNamed(LIST_STUDIO, labelInfos[li].parent);
			v.studio = (owner >= 0) ? studios[owner].name : CString();
		}
	}
	if (v.label.IsEmpty())
		return;
	// 레이블이 제작사 이름과 같으면 레이블은 비움
	if (!v.studio.IsEmpty() && (v.label.CompareNoCase(v.studio) == 0 ||
		(FindStudioLoose(v.label) >= 0 && FindStudioLoose(v.label) == FindStudioLoose(v.studio))))
	{
		v.label.Empty();
		return;
	}
	const int li = FindLabelLoose(v.label);
	if (li < 0)
		return;
	const int owner = FindNamed(LIST_STUDIO, labelInfos[li].parent);
	if (v.studio.IsEmpty() && owner >= 0)
		v.studio = studios[owner].name;   // 레이블만 있으면 상위 제작사 채움
	if (owner < 0 || FindStudioLoose(v.studio) == owner)
		v.label = labelInfos[li].name;    // 등록된 표기로 (상위 없는 레이블은 어느 제작사와도 같이 씀)
}

int CVideoLibrary::FindStudioLoose(const CString& name) const
{
	CString nm = name;
	nm.Trim();
	if (nm.IsEmpty())
		return -1;
	const int exact = FindNamed(LIST_STUDIO, nm);   // 이름 → 서브이름 (대소문자 무시)
	if (exact >= 0)
		return exact;
	// 공백 · 하이픈 · 점 · 밑줄 · 대소문자 차이 무시  예) "S1 NO.1 STYLE" = "s1no1style"
	auto loose = [](const CString& s)
	{
		CString k = CompactLower(s);
		k.Remove(L'-'); k.Remove(L'.'); k.Remove(L'_'); k.Remove(L'\x30FB'); k.Remove(L'\x00B7');
		return k;
	};
	const CString key = loose(nm);
	if (key.IsEmpty())
		return -1;
	for (size_t i = 0; i < studios.size(); ++i)
	{
		if (loose(studios[i].name) == key)
			return static_cast<int>(i);
		for (const CString& sub : SplitLines(studios[i].subName))
			if (loose(sub) == key)
				return static_cast<int>(i);
	}
	return -1;
}

bool CVideoLibrary::SameNameByLang(const CString& a, const CString& b)
{
	const std::vector<NamePart> pa = NameParts(a), pb = NameParts(b);
	for (const NamePart& x : pa)
		for (const NamePart& y : pb)
			if (x.lang == y.lang && x.key == y.key)
				return true;
	return false;
}

bool CVideoLibrary::ApplyVideoTextInfo(VideoItem& v, const CString& file) const
{
	CString text;
	if (!ReadTextAuto(file, text))
		return false;
	return ApplyVideoText(v, text);
}

bool CVideoLibrary::ApplyVideoText(VideoItem& v, CString text) const
{
	if (text.Find(kActorTxtHeader) >= 0)
		return false;   // 배우 정보 파일은 영상 정보로 읽지 않음
	text.Replace(L"\r\n", L"\n");
	text.Replace(L'\r', L'\n');

	bool changed = false;
	auto setIfEmpty = [&changed](CString& field, const CString& value)
	{
		if (field.IsEmpty() && !value.IsEmpty())
		{
			field = value;
			changed = true;
		}
	};
	// 목록 값: 쉼표 · 、 · / · # · | 로 나눔 (괄호 안 쉼표는 이름의 일부)
	auto toList = [](CString s) -> CString
	{
		s.Replace(L'、', L',');
		s.Replace(L'，', L',');
		s.Replace(L'/', L',');
		s.Replace(L'|', L',');
		s.Replace(L'#', L',');
		s.Replace(L'＃', L',');
		return JoinList(SplitList(s));
	};

	// 배우 목록: "한글이름, (English Name, なまえ)" 처럼 괄호 앞에 쉼표가 있어도 한 사람으로 (괄호로 시작하는 항목은 앞 이름에 붙임)
	auto toActorList = [&toList](const CString& s) -> CString
	{
		std::vector<CString> out;
		for (const CString& e : SplitList(toList(s)))
		{
			const bool paren = !e.IsEmpty() && (e[0] == L'(' || e[0] == L'（');
			if (paren && !out.empty() && out.back().FindOneOf(L"(（") < 0)
				out.back() += e;
			else
				out.push_back(e);
		}
		return JoinList(out);
	};

	bool actorsSet = false;
	int pos = 0;
	while (pos <= text.GetLength())
	{
		int nl = text.Find(L'\n', pos);
		if (nl < 0) nl = text.GetLength();
		CString line = text.Mid(pos, nl - pos);
		pos = nl + 1;
		line.Trim();
		if (line.IsEmpty() || (line[0] == L'#' && line.Find(L':') < 0))   // 머리말 줄(# ...)
			continue;
		int sep = -1;
		const wchar_t seps[] = { L':', L'：', L'=', L'\t' };
		for (wchar_t s : seps)
		{
			const int at = line.Find(s);
			if (at > 0 && (sep < 0 || at < sep))
				sep = at;
		}
		if (sep <= 0)
			continue;
		const CString key = NormKey(line.Left(sep));
		CString val = line.Mid(sep + 1);
		val.Trim();

		// 메모: 영상 메모 기능은 삭제 - 읽지 않고 건너뜀 (값이 비어 있으면 다음 줄부터 끝까지가 메모이므로 끝까지 건너뜀)
		if (KeyIs(key, { L"메모", L"설명", L"줄거리", L"소개", L"memo", L"note", L"notes", L"plot", L"description", L"story", L"内容", L"あらすじ", L"作品紹介" }))
		{
			if (val.IsEmpty() && pos <= text.GetLength())
			{
				val = text.Mid(pos);
				val.Trim();
				pos = text.GetLength() + 1;
			}
			continue;
		}
		if (val.IsEmpty() || val == L"-" || val.CompareNoCase(L"n/a") == 0)
			continue;

		if (KeyIs(key, { L"품번", L"코드", L"작품번호", L"code", L"id", L"dvdid", L"num", L"品番", L"品番号" }))
			setIfEmpty(v.code, val);
		else if (KeyIs(key, { L"제목", L"타이틀", L"title", L"タイトル", L"作品名" }))
			setIfEmpty(v.title, val);
		else if (KeyIs(key, { L"발매일", L"출시일", L"발매", L"release", L"releasedate", L"date", L"発売日", L"配信開始日", L"公開日" }))
			setIfEmpty(v.release, ParseDateText(val));
		else if (KeyIs(key, { L"별점", L"평점", L"rating", L"評価" }))
		{
			int r = FirstNumber(val);
			if (val.Find(L'★') >= 0) { r = 0; for (int i = 0; i < val.GetLength(); ++i) if (val[i] == L'★') ++r; }
			if (v.rating == 0 && r >= 1 && r <= 5) { v.rating = r; changed = true; }
		}
		else if (KeyIs(key, { L"배우", L"출연", L"출연자", L"출연배우", L"여배우", L"actor", L"actors", L"actress", L"cast", L"出演者", L"出演", L"女優" }))
		{
			if (v.actors.IsEmpty())
			{
				v.actors = toActorList(val);
				actorsSet = !v.actors.IsEmpty();
				changed = changed || actorsSet;
			}
		}
		else if (KeyIs(key, { L"참여별칭", L"별칭" }))
			setIfEmpty(v.actorAliases, toActorList(val));
		else if (KeyIs(key, { L"시리즈", L"series", L"シリーズ" }))
		{
			CString s = val;
			s.TrimLeft(L'#');
			s.Trim();
			setIfEmpty(v.series, s);
		}
		else if (KeyIs(key, { L"레이블", L"상표", L"label", L"レーベル" }))
		{
			CString s = val;
			s.TrimLeft(L'#');
			s.Trim();
			setIfEmpty(v.label, s);
		}
		else if (KeyIs(key, { L"스튜디오", L"제작사", L"메이커", L"studio", L"maker", L"publisher", L"メーカー", L"スタジオ" }))
		{
			// 스튜디오는 하나: 값 전체가 등록 스튜디오(이름 · 서브이름)면 그대로, 아니면 쉼표 · / 로 나눈 조각 중 등록된 것,
			// 그것도 없으면 첫 조각 (쉼표가 들어간 스튜디오 이름도 지원)
			CString s = val;
			s.Trim();
			if (FindStudioLoose(s) < 0)
			{
				CString t = s;
				t.Replace(L'、', L',');
				t.Replace(L'，', L',');
				t.Replace(L'/', L',');
				t.Replace(L'|', L',');
				std::vector<CString> parts = SplitList(t);
				CString pick = parts.empty() ? s : parts[0];
				for (const CString& p : parts)
					if (FindStudioLoose(p) >= 0) { pick = p; break; }
				s = pick;
				s.Trim();
			}
			setIfEmpty(v.studio, s);
		}
		else if (KeyIs(key, { L"태그", L"장르", L"키워드", L"tag", L"tags", L"genre", L"genres", L"keywords", L"ジャンル", L"タグ" }))
		{
			// 태그는 "/" 를 구분자로 쓰지 않음 (예: "OL/사무원" 같은 태그 이름 그대로) - 쉼표 · 、 · ， · | · # 로만 나눔
			CString t = val;
			t.Replace(L'、', L',');
			t.Replace(L'，', L',');
			t.Replace(L'|', L',');
			t.Replace(L'#', L',');
			t.Replace(L'＃', L',');
			setIfEmpty(v.tags, JoinList(SplitList(t)));
		}
		else if (KeyIs(key, { L"물방울", L"ocount", L"o" }))
		{
			const int o = FirstNumber(val);
			if (v.oCount == 0 && o > 0) { v.oCount = o; changed = true; }
		}
	}
	if (actorsSet)
	{
		// 출연자 이름이 등록 배우의 한글 / 영어 / 일어 이름 조각과 같으면 그 배우로 연결
		//  (이름 · 별칭과 그대로 같은 경우는 아래 NormalizeVideoActors 가 배우 이름 + 참여 별칭으로 정리)
		std::vector<CString> list = SplitList(v.actors), out;
		for (const CString& n : list)
		{
			CString name = n;
			if (FindActorByAnyName(n) < 0)
			{
				const int idx = FindActorByNamePart(n);
				if (idx >= 0)
					name = actors[idx].name;
			}
			bool dup = false;
			for (const CString& o : out)
				if (o.CompareNoCase(name) == 0) { dup = true; break; }
			if (!dup)
				out.push_back(name);
		}
		v.actors = JoinList(out);
		NormalizeVideoActors(v);   // 별칭으로 적힌 배우 → 배우 이름 + 참여 별칭
	}

	// 스튜디오 · 태그: 목록에 이미 있는 이름이면 목록의 표기(대소문자)로 맞춤.
	// 목록에 없는 이름은 그대로 두고, 스캔 뒤 SyncNamedFromVideos() 가 스튜디오 / 태그 목록에 새로 추가함
	if (!v.studio.IsEmpty())
	{
		const int n = FindStudioLoose(v.studio);   // 스튜디오 이름 · 서브이름 (공백 · 대소문자 차이도)
		if (n >= 0)
			v.studio = studios[n].name;
	}
	ResolveVideoLabel(v);   // 스튜디오 칸의 레이블 이름 → 상위 스튜디오 + 레이블, 레이블만 있으면 상위 스튜디오
	if (!v.tags.IsEmpty())
	{
		std::vector<CString> list = SplitList(v.tags), out;
		for (CString& t : list)
		{
			const int n = FindNamed(LIST_TAG, t);
			const CString name = (n >= 0) ? tagInfos[n].name : t;
			bool dup = false;
			for (const CString& o : out)
				if (o.CompareNoCase(name) == 0) { dup = true; break; }
			if (!dup)
				out.push_back(name);
		}
		v.tags = JoinList(out);
	}
	return changed;
}

// ---------------------------------------------------------------------------
// 웹페이지 복사 글 → "항목: 값"

CString CVideoLibrary::ParsePastedVideoText(const CString& raw)
{
	// 줄 나누기 (빈 줄도 위치 표시용으로 유지)
	CString text = raw;
	text.Replace(L"\r\n", L"\n");
	text.Replace(L'\r', L'\n');
	text.Replace(L'\t', L' ');
	text.Replace(L'\x00A0', L' ');
	std::vector<CString> lines;
	{
		int pos = 0;
		while (pos <= text.GetLength())
		{
			int nl = text.Find(L'\n', pos);
			if (nl < 0) nl = text.GetLength();
			CString l = text.Mid(pos, nl - pos);
			l.Trim();
			lines.push_back(l);
			pos = nl + 1;
		}
	}
	auto splitKey = [](const CString& line, CString& key, CString& val) -> bool
	{
		int sep = -1;
		for (wchar_t c : { L':', L'\xFF1A' })   // : / ：
		{
			const int p = line.Find(c);
			if (p > 0 && (sep < 0 || p < sep)) sep = p;
		}
		if (sep <= 0 || sep > 20)
			return false;
		key = NormKey(line.Left(sep));
		val = line.Mid(sep + 1);
		val.Trim();
		return !key.IsEmpty();
	};
	auto isCode  = [](const CString& k) { return KeyIs(k, { L"품번", L"품번호", L"작품번호", L"code", L"id", L"品番", L"品番号", L"dvdid", L"num" }); };
	auto isTitle = [](const CString& k) { return KeyIs(k, { L"제목", L"타이틀", L"title", L"タイトル", L"作品名" }); };
	auto isDate  = [](const CString& k) { return KeyIs(k, { L"출시", L"출시일", L"발매", L"발매일", L"release", L"releasedate", L"発売日", L"配信開始日", L"商品発売日", L"公開日" }); };
	auto isCast  = [](const CString& k) { return KeyIs(k, { L"출연", L"출연자", L"배우", L"여배우", L"출연배우", L"actor", L"actors", L"actress", L"cast", L"出演者", L"出演", L"女優" }); };
	auto isMaker = [](const CString& k) { return KeyIs(k, { L"제작사", L"메이커", L"studio", L"maker", L"メーカー", L"スタジオ" }); };
	auto isLabel = [](const CString& k) { return KeyIs(k, { L"레이블", L"상표", L"label", L"レーベル" }); };
	auto isSeries = [](const CString& k) { return KeyIs(k, { L"시리즈", L"series", L"シリーズ" }); };
	auto isGenre = [](const CString& k) { return KeyIs(k, { L"장르", L"장르상세", L"태그", L"genre", L"genres", L"tag", L"tags", L"ジャンル", L"タグ" }); };
	auto hasHangul = [](const CString& s) { for (int i = 0; i < s.GetLength(); ++i) if (s[i] >= 0xAC00 && s[i] <= 0xD7A3) return true; return false; };

	CString code, title, date, maker, label, series;
	std::vector<CString> cast, genres;
	auto addGenre = [&genres](CString g)
	{
		g.Trim();
		g.TrimLeft(L'#');
		g.Trim();
		if (g.IsEmpty() || g.GetLength() > 30) return;
		for (const CString& e : genres) if (e.CompareNoCase(g) == 0) return;
		genres.push_back(g);
	};
	auto addCast = [&cast](CString c)
	{
		c.Trim();
		c.TrimLeft(L'#');
		c.Trim();
		if (c.IsEmpty() || c.GetLength() > 60) return;
		for (const CString& e : cast) if (e.CompareNoCase(c) == 0) return;
		cast.push_back(c);
	};

	int codeLine = -1;
	for (size_t i = 0; i < lines.size(); ++i)
	{
		const CString& line = lines[i];
		if (line.IsEmpty())
			continue;
		CString key, val;
		const bool kv = splitKey(line, key, val);
		// "▶ 장르 상세" 처럼 콜론 없는 장르 머리말도 장르 목록 시작으로
		CString head = line;
		head.TrimLeft(L"▶▷■□●○◆◇・*#- ");
		const bool genreHead = (!kv && KeyIs(NormKey(head), { L"장르상세", L"장르", L"ジャンル", L"genre", L"genres", L"태그", L"tags" }));
		if (kv && isCode(key))        { if (code.IsEmpty()) { code = val; codeLine = static_cast<int>(i); } }
		else if (kv && isTitle(key))  { if (title.IsEmpty()) title = val; }
		else if (kv && isDate(key))   { if (date.IsEmpty()) date = val; }
		else if (kv && isMaker(key))  { if (maker.IsEmpty()) { maker = val; maker.TrimLeft(L'#'); maker.Trim(); } }
		else if (kv && isLabel(key))  { if (label.IsEmpty()) { label = val; label.TrimLeft(L'#'); label.Trim(); } }
		else if (kv && isSeries(key)) { if (series.IsEmpty() && val != L"-" && val != L"----") { series = val; series.TrimLeft(L'#'); series.Trim(); } }
		else if (kv && isCast(key))
		{
			CString v = val;
			v.Replace(L'#', L',');
			v.Replace(L'、', L',');
			v.Replace(L'，', L',');
			v.Replace(L'/', L',');
			if (!hasHangul(v))   // 일본어 페이지: 공백으로 이름 구분
			{
				v.Replace(L'\x3000', L',');
			}
			for (const CString& c : SplitList(v))
				addCast(c);
		}
		else if ((kv && isGenre(key)) || genreHead)
		{
			if (kv && !val.IsEmpty())
			{
				// 같은 줄에 나열: 쉼표 · 、 구분 (없으면 일본어는 공백 구분)
				CString v = val;
				v.Replace(L'、', L',');
				v.Replace(L'，', L',');
				if (v.Find(L',') < 0 && !hasHangul(v))
				{
					v.Replace(L'\x3000', L',');
					v.Replace(L' ', L',');
				}
				for (const CString& g : SplitList(v))
					addGenre(g);
			}
			else
			{
				// 다음 줄들이 장르 (빈 줄 · 머리말(▶) · "항목:" 줄 · 긴 줄이 나오면 끝)
				size_t j = i + 1;
				while (j < lines.size() && lines[j].IsEmpty()) ++j;
				for (; j < lines.size(); ++j)
				{
					const CString& g = lines[j];
					if (g.IsEmpty() || g[0] == L'▶' || g[0] == L'■' || g.GetLength() > 30)
						break;
					CString k2, v2;
					if (splitKey(g, k2, v2))
						break;
					addGenre(g);
				}
				i = j - 1;
			}
		}
	}

	// 품번이 없으면 본문에서 찾기
	if (code.IsEmpty())
	{
		for (const CString& l : lines)
		{
			code = ExtractCode(l);
			if (!code.IsEmpty()) break;
		}
	}
	// 제목이 없으면 (AVDBS 배치) "품번:" 다음 줄의 긴 글
	if (title.IsEmpty() && codeLine >= 0)
	{
		for (size_t j = static_cast<size_t>(codeLine) + 1; j < lines.size() && j < static_cast<size_t>(codeLine) + 4; ++j)
		{
			CString k, v;
			if (lines[j].IsEmpty() || splitKey(lines[j], k, v)) continue;
			if (lines[j].GetLength() >= 6 && lines[j].Find(L"프로필") < 0)
			{
				title = lines[j];
				break;
			}
		}
	}
	// 출연자가 한 명이면 "품번 / 한글 / 일어 / 영어" 줄의 이름들로 "한글(English, 日本語)"
	if (cast.size() == 1)
	{
		for (const CString& l : lines)
		{
			if (l.Find(L'/') < 0 || code.IsEmpty() || CompactLower(l).Find(CompactLower(code)) != 0)
				continue;
			std::vector<CString> parts;
			int start = 0;
			for (;;)
			{
				CString p = l.Tokenize(L"/", start);
				if (start < 0) break;
				p.Trim();
				if (!p.IsEmpty()) parts.push_back(p);
			}
			if (parts.size() >= 3 && parts[1].CompareNoCase(cast[0]) == 0)
			{
				CString inner;
				if (parts.size() >= 4) inner = parts[3];
				if (!parts[2].IsEmpty()) { if (!inner.IsEmpty()) inner += L", "; inner += parts[2]; }
				cast[0] = parts[1] + L"(" + inner + L")";
			}
			break;
		}
	}

	CString out;
	auto add = [&out](LPCWSTR k, const CString& v) { CString t = v; t.Trim(); if (t.IsEmpty()) return; out += k; out += L": "; out += t; out += L"\r\n"; };
	add(L"품번", code);
	add(L"제목", title);
	add(L"발매일", date);
	add(L"배우", JoinList(cast));
	// 제작사 → 스튜디오, 레이블 → 레이블 (제작사가 없으면 레이블을 스튜디오 칸에 → 등록된 레이블이면 상위 스튜디오로 바뀜)
	if (!maker.IsEmpty())
	{
		add(L"제작사", maker);
		if (label.CompareNoCase(maker) != 0)
			add(L"레이블", label);
	}
	else
		add(L"제작사", label);
	add(L"시리즈", series);
	add(L"태그", JoinList(genres));
	return out;
}

CString CVideoLibrary::ExtractCode(const CString& name)
{
	// 영문 2~6자 + (- 또는 _ 또는 없음) + 숫자 2~5자  →  "ABC-123" (대문자)
	//  - [2024.12.10] 같은 대괄호 날짜 · 해상도(1080p) 는 숫자 앞에 영문이 없어 걸리지 않음
	const int n = name.GetLength();
	for (int i = 0; i < n; ++i)
	{
		auto isAlpha = [](wchar_t ch) { return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z'); };
		auto isDigit = [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; };
		if (!isAlpha(name[i]) || (i > 0 && (isAlpha(name[i - 1]) || isDigit(name[i - 1]))))
			continue;   // 단어 시작에서만
		int j = i;
		while (j < n && isAlpha(name[j])) ++j;
		const int letters = j - i;
		if (letters < 2 || letters > 6)
			continue;
		int k = j;
		if (k < n && (name[k] == L'-' || name[k] == L'_' || name[k] == L' '))
			++k;
		int m = k;
		while (m < n && isDigit(name[m])) ++m;
		const int digits = m - k;
		if (digits < 2 || digits > 5 || (m < n && isAlpha(name[m]) && !(m + 1 >= n || !isAlpha(name[m + 1]))))
			continue;   // 숫자 뒤에 영문 단어가 이어지면(예: 1080p 이외의 긴 단어) 제외
		CString prefix = name.Mid(i, letters);
		prefix.MakeUpper();
		if (prefix == L"MP" || prefix == L"HD" || prefix == L"FHD" || prefix == L"UHD" || prefix == L"CD" || prefix == L"PART")
			continue;   // 품번이 아닌 흔한 표기
		return prefix + L"-" + name.Mid(k, digits);
	}
	return CString();
}

int CVideoLibrary::FillMissingCodes()
{
	int filled = 0;
	for (VideoItem& v : items)
	{
		if (!v.code.IsEmpty())
			continue;
		CString stem = v.FileName();
		::PathRemoveExtensionW(stem.GetBuffer());
		stem.ReleaseBuffer();
		CString code = ExtractCode(stem);
		if (code.IsEmpty())
		{
			CString folder = v.path.Left(static_cast<int>(::PathFindFileNameW(v.path) - static_cast<LPCWSTR>(v.path)));
			folder.TrimRight(L"\\");
			code = ExtractCode(::PathFindFileNameW(folder));
		}
		if (!code.IsEmpty())
		{
			v.code = code;
			++filled;
		}
	}
	return filled;
}
