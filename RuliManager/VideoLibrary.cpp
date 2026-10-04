#include "pch.h"
#include "VideoLibrary.h"
#include "DbCrypt.h"

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
	// 저장 파일에는 이미지 보관소 안의 이미지를 상대 경로("images\\actors\\...")로 기록 → DB 파일과 함께 이동
	CString ToStoredPath(const CString& path)
	{
		const CString dir = CVideoLibrary::GetCacheDir();
		if (!path.IsEmpty() && CVideoLibrary::IsUnder(path, dir))
			return path.Mid(dir.GetLength() + 1);
		return path;
	}

	CString FromStoredPath(const CString& path)
	{
		if (!path.IsEmpty() && ::PathIsRelativeW(path))
			return CVideoLibrary::GetCacheDir() + L"\\" + path;
		return path;
	}
}

CString CVideoLibrary::GetImageStoreDir(LPCWSTR sub)
{
	CString dir = GetCacheDir() + L"\\images";
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
	return !path.IsEmpty() && IsUnder(path, GetCacheDir() + L"\\images");
}

CString CVideoLibrary::StoreImageCopy(const CString& src, LPCWSTR sub)
{
	if (src.IsEmpty())
		return CString();
	if (IsInImageStore(src))
		return src;                  // 이미 보관소에 있는 복사본
	if (!::PathFileExistsW(src))
		return CString();

	const CString dir = GetImageStoreDir(sub);
	CString stem = ::PathFindFileNameW(src);
	CString ext = ::PathFindExtensionW(src);
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
		const CString dst = dir + L"\\" + name + L"_" + stem + ext;
		if (::CopyFileW(src, dst, TRUE))       // 같은 이름이 있으면 실패 → 다음 번호
		{
			::SetFileAttributesW(dst, FILE_ATTRIBUTE_NORMAL);   // 읽기 전용 속성은 떼어냄
			return dst;
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
	for (NamedInfo& n : tagInfos)
		fix(n.image, L"tags");
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

	// 보관소 하위 폴더의 파일 중 연결되지 않은 것 모으기
	std::vector<CString> unused;
	const CString root = GetImageStoreDir();
	const LPCWSTR subs[] = { L"actors", L"studios", L"tags" };
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
		const CString root = CVideoLibrary::GetCacheDir() + L"\\images";
		const LPCWSTR subs[] = { L"actors", L"studios", L"tags" };
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
		const LPCWSTR subs[] = { L"images\\actors\\", L"images\\studios\\", L"images\\tags\\" };
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

	// 암호화된 이미지 묶음을 풀어서 캐시 폴더에 파일로 씀
	bool UnpackImages(const std::vector<BYTE>& sec)
	{
		if (sec.empty())
			return true;
		std::vector<BYTE> plain;
		std::vector<Entry> imgs;
		if (!DbCrypt::Decrypt(sec.data(), sec.size(), plain) || !ParseEntries(plain, imgs))
			return false;
		const CString root = CVideoLibrary::GetCacheDir();
		for (const Entry& e : imgs)
		{
			const CString rel = CString(CA2W(e.name, CP_UTF8));
			if (!SafeImageName(rel))
				continue;
			CFile f;
			if (f.Open(root + L"\\" + rel, CFile::modeCreate | CFile::modeWrite))
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
		{ GetDataFilePath(), L"library" },
		{ GetImagesDbPath(), L"images" },
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
	if (!ReadWholeFile(GetDataFilePath(), raw) || raw.size() < kPackMagicLen + 16 ||
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

	// 이미지: 캐시를 비우고 images.vmdb (없으면 예전 한 파일 형식의 이미지 묶음) 에서 풀어 놓음
	ClearImageCache();
	GetImageStoreDir(L"actors");
	GetImageStoreDir(L"studios");
	GetImageStoreDir(L"tags");
	bool imagesOk = true;
	bool fromNewFile = false;
	std::vector<BYTE> imgRaw;
	if (ReadWholeFile(GetImagesDbPath(), imgRaw) && imgRaw.size() >= kImgMagicLen + 8 &&
		memcmp(imgRaw.data(), kImgMagic, kImgMagicLen) == 0)
	{
		UINT64 len = 0;
		memcpy(&len, imgRaw.data() + kImgMagicLen, 8);
		if (len <= imgRaw.size() - kImgMagicLen - 8)
		{
			const std::vector<BYTE> sec(imgRaw.begin() + kImgMagicLen + 8, imgRaw.begin() + kImgMagicLen + 8 + static_cast<size_t>(len));
			imagesOk = UnpackImages(sec);
			fromNewFile = true;
		}
		else
			imagesOk = false;
	}
	else if (::GetFileAttributesW(GetImagesDbPath()) != INVALID_FILE_ATTRIBUTES)
		imagesOk = false;   // images.vmdb 가 있는데 형식이 다름 (손상)
	if (!fromNewFile && imagesOk && !secImgOld.empty())
	{
		imagesOk = UnpackImages(secImgOld);
		g_readPlainDb = true;   // 예전 한 파일 형식 → 바로 저장해서 library.vmdb / images.vmdb 로 나눔
	}
	if (!imagesOk)
	{
		g_decryptFailed = true;   // 이미지가 손상 → 저장하지 않음 (덮어쓰기 방지)
		return false;
	}
	// 풀어 놓은 그대로면 다음 저장 때 images.vmdb 를 다시 쓰지 않음 (예전 형식에서 옮긴 경우는 새로 써야 함)
	m_imgSig = fromNewFile ? ImageSignature(nullptr) : CString();
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

	// 1) 이미지가 바뀌었으면(또는 images.vmdb 가 없으면) images.vmdb 를 먼저 씀
	std::vector<CString> files;
	const CString sig = ImageSignature(&files);
	if (sig != m_imgSig || ::GetFileAttributesW(GetImagesDbPath()) == INVALID_FILE_ATTRIBUTES)
	{
		std::vector<BYTE> imgPlain;
		const CString root = GetCacheDir();
		for (const CString& rel : files)
		{
			std::vector<BYTE> data;
			if (ReadWholeFile(root + L"\\" + rel, data))
				PutEntry(imgPlain, CStringA(CW2A(rel, CP_UTF8)), data.data(), data.size());
		}
		std::vector<BYTE> blob;
		if (!imgPlain.empty() && !DbCrypt::Encrypt(imgPlain.data(), imgPlain.size(), blob))
			return false;
		if (!writeFile(GetImagesDbPath(), kImgMagic, kImgMagicLen, blob, nullptr))
			return false;
		m_imgSig = sig;
	}

	// 2) 정보 묶음 → library.vmdb (이미지 묶음 자리는 길이 0)
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
	if (!writeFile(GetDataFilePath(), kPackMagic, kPackMagicLen, secText, &noImages))
		return false;

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

	CString text, pendingText;
	g_readPlainDb = false;
	g_decryptFailed = false;
	m_legacyToMove = false;
	m_imgSig.Empty();
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
	if (::GetFileAttributesW(GetDataFilePath()) != INVALID_FILE_ATTRIBUTES)
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
			CopyTree(oldImages, GetCacheDir() + L"\\images");
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
			if (fields.size() >= 4) n.image = FromStoredPath(Unescape(fields[3]));
			if (fields.size() >= 5) n.favorite = (fields[4] == L"1");
			if (!n.name.IsEmpty() && FindNamed(kind, n.name) < 0)
				NamedList(kind).push_back(n);
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
			v.memo     = Unescape(fields[6]);
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
			if (!v.path.IsEmpty())
				items.push_back(v);
		}
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
			L"\t" + Escape(a.bust) + L"\t" + Escape(a.waist) + L"\t" + Escape(a.hip) + L"\t" + Escape(a.cup) + L"\n";   // 10번째 칸(예전 활동명)은 비워 두고 11번째에 성별
	}
	for (int kind = LIST_STUDIO; kind <= LIST_TAG; ++kind)
	{
		for (const NamedInfo& n : NamedList(kind))
		{
			text += (kind == LIST_STUDIO ? L"S\t" : L"T\t");
			text += Escape(n.name) + L"\t" + (kind == LIST_STUDIO ? Escape(n.memo) : CString()) + L"\t" + Escape(ToStoredPath(n.image)) +
				L"\t" + (n.favorite ? L"1" : L"") + L"\n";
		}
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
		line.Format(L"V\t%s\t%llu\t%llu\t%d\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%d\n",
			static_cast<LPCWSTR>(Escape(v.path)),
			v.size, v.modified, v.rating,
			static_cast<LPCWSTR>(Escape(v.tags)),
			static_cast<LPCWSTR>(Escape(v.memo)),
			static_cast<LPCWSTR>(Escape(v.actors)),
			static_cast<LPCWSTR>(Escape(v.studio)),
			static_cast<LPCWSTR>(Escape(v.release)),
			static_cast<LPCWSTR>(Escape(v.title)),
			static_cast<LPCWSTR>(Escape(v.actorAliases)),
			v.oCount);
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
		v.studio = studio;
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
			continue;
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
			if (index.find(key) == index.end())   // 이름·별칭 어디에도 없을 때만 새 배우
			{
				ActorInfo a;
				a.name = n;
				actors.push_back(a);
				index[key] = static_cast<int>(actors.size()) - 1;
				added = true;
			}
		}
	}
	return added;
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
	return -1;
}

bool CVideoLibrary::SyncNamedFromVideos()
{
	bool added = false;
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
	return added;
}

void CVideoLibrary::RenameNamedInVideos(int kind, const CString& oldName, const CString& newName)
{
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

int CVideoLibrary::RemoveNamedFromVideos(int kind, const CString& name)
{
	int affected = 0;
	for (VideoItem& v : items)
	{
		if (kind == LIST_STUDIO)
		{
			CString s = v.studio;
			s.Trim();
			if (s.CompareNoCase(name) == 0)
			{
				v.studio.Empty();
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
