#include "pch.h"
#include "MediaInfoLabel.h"
#include "TextDraw.h"
#include <shobjidl.h>
#include <propidl.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mutex>
#include <string>
#include "DbCrypt.h"
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	// FMTID_VideoSummaryInformation {64440491-4C8B-11D1-8B70-080036B11A03}
	const GUID kVideoFmtId = { 0x64440491, 0x4C8B, 0x11D1, { 0x8B, 0x70, 0x08, 0x00, 0x36, 0xB1, 0x1A, 0x03 } };
	const PROPERTYKEY kFrameWidth  = { kVideoFmtId, 3 };   // PKEY_Video_FrameWidth
	const PROPERTYKEY kFrameHeight = { kVideoFmtId, 4 };   // PKEY_Video_FrameHeight
	const PROPERTYKEY kFrameRate   = { kVideoFmtId, 6 };   // PKEY_Video_FrameRate (1000초당 프레임 수)
	// FMTID_AudioSummaryInformation {64440490-4C8B-11D1-8B70-080036B11A03}
	const GUID kMediaFmtId = { 0x64440490, 0x4C8B, 0x11D1, { 0x8B, 0x70, 0x08, 0x00, 0x36, 0xB1, 0x1A, 0x03 } };
	const PROPERTYKEY kDuration    = { kMediaFmtId, 3 };   // PKEY_Media_Duration (100ns 단위)

	ULONGLONG ReadUInt64(IPropertyStore* store, const PROPERTYKEY& key)
	{
		PROPVARIANT pv;
		::PropVariantInit(&pv);
		ULONGLONG value = 0;
		if (SUCCEEDED(store->GetValue(key, &pv)))
		{
			switch (pv.vt)
			{
			case VT_UI8: value = pv.uhVal.QuadPart; break;
			case VT_I8:  value = pv.hVal.QuadPart > 0 ? static_cast<ULONGLONG>(pv.hVal.QuadPart) : 0; break;
			case VT_UI4: value = pv.ulVal; break;
			default: break;
			}
		}
		::PropVariantClear(&pv);
		return value;
	}

	UINT ReadUInt(IPropertyStore* store, const PROPERTYKEY& key)
	{
		PROPVARIANT pv;
		::PropVariantInit(&pv);
		UINT value = 0;
		if (SUCCEEDED(store->GetValue(key, &pv)))
		{
			switch (pv.vt)
			{
			case VT_UI4: value = pv.ulVal; break;
			case VT_I4:  value = pv.lVal > 0 ? static_cast<UINT>(pv.lVal) : 0; break;
			case VT_UI8: value = static_cast<UINT>(pv.uhVal.QuadPart); break;
			case VT_UI2: value = pv.uiVal; break;
			default: break;
			}
		}
		::PropVariantClear(&pv);
		return value;
	}
}

BEGIN_MESSAGE_MAP(CMediaInfoLabel, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

bool CMediaInfoLabel::Create(CWnd* parent, UINT id)
{
	const CString cls = AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_ARROW), nullptr);
	return CWnd::Create(cls, L"", WS_CHILD, CRect(0, 0, 100, 20), parent, id) != FALSE;
}

void CMediaInfoLabel::SetColors(COLORREF back, COLORREF sub, COLORREF text)
{
	m_back = back;
	m_sub = sub;
	m_text = text;
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

namespace
{
	// Windows 속성에 값이 없을 때: Media Foundation 으로 파일을 직접 열어 영상 스트림의 크기 · fps · 전체 길이를 읽음
	//  (MP4 · MOV · M4V · WMV · AVI · MKV(Windows 10 이상) 등 Windows 에서 재생되는 형식)
	void ReadInfoMediaFoundation(const CString& path, CMediaInfoLabel::Info& info)
	{
		static std::once_flag once;
		static bool started = false;
		std::call_once(once, []() { started = SUCCEEDED(::MFStartup(MF_VERSION, MFSTARTUP_LITE)); });   // 한 번만 (끝날 때까지 유지)
		if (!started)
			return;
		// COM 이 아직 없는 스레드면 이 호출 동안만 초기화
		const HRESULT co = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		{
			CComPtr<IMFSourceReader> reader;
			if (SUCCEEDED(::MFCreateSourceReaderFromURL(path, nullptr, &reader)) && reader)
			{
				if (info.duration100ns == 0)
				{
					PROPVARIANT var;
					::PropVariantInit(&var);
					if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &var)) && var.vt == VT_UI8)
						info.duration100ns = var.uhVal.QuadPart;
					::PropVariantClear(&var);
				}
				CComPtr<IMFMediaType> type;
				if (SUCCEEDED(reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &type)) && type)
				{
					UINT32 w = 0, h = 0;
					if ((info.width == 0 || info.height == 0) && SUCCEEDED(::MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h)) && w > 0 && h > 0)
					{
						info.width = w;
						info.height = h;
					}
					UINT32 num = 0, den = 0;
					if (info.fps <= 0 && SUCCEEDED(::MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &num, &den)) && num > 0 && den > 0)
						info.fps = static_cast<double>(num) / den;
				}
			}
		}
		if (SUCCEEDED(co))
			::CoUninitialize();
	}

	// ---------------------------------------------------------------------------
	// Matroska (mkv / webm) 헤더 직접 읽기: Windows 속성 · Media Foundation 이 못 읽은 mkv 용 (코덱 · 설정과 상관없음)
	//  - EBML 요소: [ID (1~4바이트)][크기 (1~8바이트 가변 길이)][내용]
	//  - Segment 안의 Info (TimecodeScale · Duration) 와 Tracks (영상 트랙의 PixelWidth / PixelHeight · DefaultDuration) 만 읽고,
	//    Cluster (실제 영상 데이터) 등은 크기만큼 건너뜀 → 파일 앞부분 조금만 읽음
	class CEbmlFile
	{
	public:
		explicit CEbmlFile(const CString& path)
		{
			m_h = ::CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
			LARGE_INTEGER sz = {};
			if (m_h != INVALID_HANDLE_VALUE && ::GetFileSizeEx(m_h, &sz))
				m_size = static_cast<ULONGLONG>(sz.QuadPart);
		}
		~CEbmlFile() { if (m_h != INVALID_HANDLE_VALUE) ::CloseHandle(m_h); }
		bool Ok() const { return m_h != INVALID_HANDLE_VALUE && m_size > 0; }
		ULONGLONG Size() const { return m_size; }
		ULONGLONG Pos() const { return m_pos; }
		bool Seek(ULONGLONG pos)
		{
			if (pos > m_size)
				return false;
			LARGE_INTEGER li;
			li.QuadPart = static_cast<LONGLONG>(pos);
			if (!::SetFilePointerEx(m_h, li, nullptr, FILE_BEGIN))
				return false;
			m_pos = pos;
			return true;
		}
		bool Read(void* buf, DWORD n)
		{
			DWORD got = 0;
			if (n == 0)
				return true;
			if (!::ReadFile(m_h, buf, n, &got, nullptr) || got != n)
				return false;
			m_pos += n;
			return true;
		}
		// 요소 ID (표시 비트 포함 그대로)
		bool ReadId(UINT32& id)
		{
			BYTE b = 0;
			if (!Read(&b, 1) || b == 0)
				return false;
			int len = 1;
			for (BYTE mask = 0x80; !(b & mask) && len <= 4; mask >>= 1)
				++len;
			if (len > 4)
				return false;
			id = b;
			for (int i = 1; i < len; ++i)
			{
				BYTE c = 0;
				if (!Read(&c, 1))
					return false;
				id = (id << 8) | c;
			}
			return true;
		}
		// 요소 크기 (표시 비트 뺌), unknown = 모든 비트가 1 (크기를 모름)
		bool ReadSize(ULONGLONG& size, bool& unknown)
		{
			BYTE b = 0;
			if (!Read(&b, 1) || b == 0)
				return false;
			int len = 1;
			BYTE mask = 0x80;
			while (!(b & mask) && len <= 8)
			{
				mask >>= 1;
				++len;
			}
			if (len > 8)
				return false;
			ULONGLONG v = b & (mask - 1);
			bool allOnes = (v == static_cast<ULONGLONG>(mask - 1));
			for (int i = 1; i < len; ++i)
			{
				BYTE c = 0;
				if (!Read(&c, 1))
					return false;
				v = (v << 8) | c;
				if (c != 0xFF)
					allOnes = false;
			}
			size = v;
			unknown = allOnes;
			return true;
		}
		bool ReadUInt(ULONGLONG size, ULONGLONG& v)
		{
			if (size == 0 || size > 8)
				return false;
			BYTE buf[8] = {};
			if (!Read(buf, static_cast<DWORD>(size)))
				return false;
			v = 0;
			for (ULONGLONG i = 0; i < size; ++i)
				v = (v << 8) | buf[i];
			return true;
		}
		bool ReadFloat(ULONGLONG size, double& v)
		{
			BYTE buf[8] = {};
			if ((size != 4 && size != 8) || !Read(buf, static_cast<DWORD>(size)))
				return false;
			if (size == 4)
			{
				UINT32 u = (static_cast<UINT32>(buf[0]) << 24) | (static_cast<UINT32>(buf[1]) << 16) | (static_cast<UINT32>(buf[2]) << 8) | buf[3];
				float f = 0;
				memcpy(&f, &u, 4);
				v = f;
			}
			else
			{
				ULONGLONG u = 0;
				for (int i = 0; i < 8; ++i)
					u = (u << 8) | buf[i];
				memcpy(&v, &u, 8);
			}
			return true;
		}
	private:
		HANDLE    m_h = INVALID_HANDLE_VALUE;
		ULONGLONG m_size = 0;
		ULONGLONG m_pos = 0;
	};

	void ReadInfoMatroska(const CString& path, CMediaInfoLabel::Info& info)
	{
		CEbmlFile f(path);
		if (!f.Ok())
			return;
		// EBML 머리 (1A 45 DF A3) 가 아니면 Matroska 아님
		UINT32 id = 0;
		ULONGLONG size = 0;
		bool unknown = false;
		if (!f.ReadId(id) || id != 0x1A45DFA3 || !f.ReadSize(size, unknown) || unknown || !f.Seek(f.Pos() + size))
			return;
		// Segment
		if (!f.ReadId(id) || id != 0x18538067 || !f.ReadSize(size, unknown))
			return;
		const ULONGLONG segEnd = unknown ? f.Size() : (std::min)(f.Size(), f.Pos() + size);

		ULONGLONG timecodeScale = 1000000;   // 기본 1ms (나노초 단위)
		double duration = 0;                 // TimecodeScale 단위
		UINT32 width = 0, height = 0;
		ULONGLONG defaultDuration = 0;       // 영상 프레임 하나 길이 (나노초)
		bool gotInfo = false, gotTracks = false;

		for (int guard = 0; guard < 256 && f.Pos() < segEnd && !(gotInfo && gotTracks); ++guard)
		{
			if (!f.ReadId(id) || !f.ReadSize(size, unknown))
				break;
			const ULONGLONG start = f.Pos();
			if (unknown)
				break;   // 크기를 모르는 요소 (스트리밍 Cluster 등) - 더 못 감
			const ULONGLONG end = start + size;
			if (id == 0x1549A966)   // Info
			{
				gotInfo = true;
				while (f.Pos() < end)
				{
					UINT32 cid = 0;
					ULONGLONG csz = 0;
					bool cu = false;
					if (!f.ReadId(cid) || !f.ReadSize(csz, cu) || cu)
						break;
					const ULONGLONG cend = f.Pos() + csz;
					if (cid == 0x2AD7B1)      { ULONGLONG v = 0; if (f.ReadUInt(csz, v) && v > 0) timecodeScale = v; }
					else if (cid == 0x4489)   { double d = 0; if (f.ReadFloat(csz, d) && d > 0) duration = d; }
					if (!f.Seek(cend))
						break;
				}
			}
			else if (id == 0x1654AE6B)   // Tracks
			{
				gotTracks = true;
				while (f.Pos() < end && width == 0)
				{
					UINT32 tid = 0;
					ULONGLONG tsz = 0;
					bool tu = false;
					if (!f.ReadId(tid) || !f.ReadSize(tsz, tu) || tu)
						break;
					const ULONGLONG tend = f.Pos() + tsz;
					if (tid == 0xAE)   // TrackEntry
					{
						ULONGLONG type = 0, dd = 0;
						UINT32 w = 0, h = 0;
						while (f.Pos() < tend)
						{
							UINT32 eid = 0;
							ULONGLONG esz = 0;
							bool eu = false;
							if (!f.ReadId(eid) || !f.ReadSize(esz, eu) || eu)
								break;
							const ULONGLONG eend = f.Pos() + esz;
							if (eid == 0x83)            f.ReadUInt(esz, type);   // TrackType (1 = 영상)
							else if (eid == 0x23E383)   f.ReadUInt(esz, dd);     // DefaultDuration
							else if (eid == 0xE0)        // Video
							{
								while (f.Pos() < eend)
								{
									UINT32 vid = 0;
									ULONGLONG vsz = 0;
									bool vu = false;
									if (!f.ReadId(vid) || !f.ReadSize(vsz, vu) || vu)
										break;
									const ULONGLONG vend = f.Pos() + vsz;
									ULONGLONG v = 0;
									if (vid == 0xB0 && f.ReadUInt(vsz, v))        w = static_cast<UINT32>(v);   // PixelWidth
									else if (vid == 0xBA && f.ReadUInt(vsz, v))   h = static_cast<UINT32>(v);   // PixelHeight
									if (!f.Seek(vend))
										break;
								}
							}
							if (!f.Seek(eend))
								break;
						}
						if (type == 1 && w > 0 && h > 0)   // 첫 영상 트랙
						{
							width = w;
							height = h;
							defaultDuration = dd;
						}
					}
					if (!f.Seek(tend))
						break;
				}
			}
			if (!f.Seek(end))
				break;
		}

		if (info.duration100ns == 0 && duration > 0)
			info.duration100ns = static_cast<ULONGLONG>(duration * static_cast<double>(timecodeScale) / 100.0);   // 나노초 → 100ns
		if ((info.width == 0 || info.height == 0) && width > 0 && height > 0)
		{
			info.width = width;
			info.height = height;
		}
		if (info.fps <= 0 && defaultDuration > 0)
			info.fps = 1000000000.0 / static_cast<double>(defaultDuration);
	}
}

// ---------------------------------------------------------------------------
// 디스크 캐시 (mediainfo.vmdb): 영상마다 해상도 · fps · 재생 시간을 저장해 두고 다음 실행에 다시 씀
//  - 파일 크기와 수정 시각이 저장할 때와 같으면 파일을 다시 열지 않음 (시작할 때 영상 카드마다 파일을 열던 CPU · 디스크 사용 줄임)
//  - 형식: DbCrypt 암호화한 UTF-8 글자, 줄마다 "경로(소문자)\t크기\t수정시각\t가로\t세로\tfps*1000\t재생시간(100ns)"
namespace
{
	struct DiskEntry { ULONGLONG size = 0, mtime = 0; CMediaInfoLabel::Info info; };
	std::mutex g_diskMx;
	std::map<CString, DiskEntry> g_disk;   // 경로(소문자) → 정보
	CString g_diskFile;
	bool g_diskDirty = false;

	bool FileStamp(const CString& path, ULONGLONG& size, ULONGLONG& mtime)
	{
		WIN32_FILE_ATTRIBUTE_DATA fa = {};
		if (!::GetFileAttributesExW(path, GetFileExInfoStandard, &fa) || (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			return false;
		size = (static_cast<ULONGLONG>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
		mtime = (static_cast<ULONGLONG>(fa.ftLastWriteTime.dwHighDateTime) << 32) | fa.ftLastWriteTime.dwLowDateTime;
		return true;
	}
}

void CMediaInfoLabel::LoadDiskCache(const CString& file)
{
	std::lock_guard<std::mutex> lock(g_diskMx);
	g_diskFile = file;
	g_disk.clear();
	g_diskDirty = false;
	CFile f;
	if (!f.Open(file, CFile::modeRead | CFile::shareDenyWrite))
		return;
	const ULONGLONG len = f.GetLength();
	if (len == 0 || len > 512ULL * 1024 * 1024)
		return;
	std::vector<BYTE> raw(static_cast<size_t>(len));
	if (f.Read(raw.data(), static_cast<UINT>(raw.size())) != raw.size())
		return;
	f.Close();
	std::vector<BYTE> plain;
	if (!DbCrypt::Decrypt(raw.data(), raw.size(), plain) || plain.empty())
		return;   // 못 풀면 빈 캐시로 시작 (끝날 때 새로 씀)
	const CString text = CString(CA2W(std::string(reinterpret_cast<const char*>(plain.data()), plain.size()).c_str(), CP_UTF8));
	int pos = 0;
	while (pos < text.GetLength())
	{
		int nl = text.Find(L'\n', pos);
		if (nl < 0) nl = text.GetLength();
		const CString line = text.Mid(pos, nl - pos);
		pos = nl + 1;
		if (line.IsEmpty() || line[0] == L'#')
			continue;
		CString cols[7];
		int c = 0, start = 0;
		for (int i = 0; i <= line.GetLength() && c < 7; ++i)
			if (i == line.GetLength() || line[i] == L'\t')
			{
				cols[c++] = line.Mid(start, i - start);
				start = i + 1;
			}
		if (c < 7 || cols[0].IsEmpty())
			continue;
		DiskEntry e;
		e.size = _wcstoui64(cols[1], nullptr, 10);
		e.mtime = _wcstoui64(cols[2], nullptr, 10);
		e.info.width = static_cast<UINT>(_wcstoui64(cols[3], nullptr, 10));
		e.info.height = static_cast<UINT>(_wcstoui64(cols[4], nullptr, 10));
		e.info.fps = static_cast<double>(_wcstoui64(cols[5], nullptr, 10)) / 1000.0;
		e.info.duration100ns = _wcstoui64(cols[6], nullptr, 10);
		e.info.ok = (e.info.width > 0 && e.info.height > 0) || e.info.fps > 0 || e.info.duration100ns > 0;
		if (e.info.ok)
			g_disk[cols[0]] = e;
	}
}

void CMediaInfoLabel::SaveDiskCache(const std::set<CString>* keep)
{
	std::lock_guard<std::mutex> lock(g_diskMx);
	if (g_diskFile.IsEmpty())
		return;
	if (keep)
	{
		// DB 에 없는 영상(지웠거나 옮긴 파일)의 기록은 버림
		for (auto it = g_disk.begin(); it != g_disk.end();)
		{
			if (keep->count(it->first) == 0)
			{
				it = g_disk.erase(it);
				g_diskDirty = true;
			}
			else
				++it;
		}
	}
	if (!g_diskDirty)
		return;
	CString text = L"#MediaInfo\t1\n";
	for (const auto& kv : g_disk)
	{
		const DiskEntry& e = kv.second;
		text.AppendFormat(L"%s\t%llu\t%llu\t%u\t%u\t%llu\t%llu\n", static_cast<LPCWSTR>(kv.first), e.size, e.mtime,
			e.info.width, e.info.height, static_cast<ULONGLONG>(e.info.fps * 1000.0 + 0.5), e.info.duration100ns);
	}
	const CW2A utf8(text, CP_UTF8);
	const size_t n = strlen(utf8);
	std::vector<BYTE> enc;
	if (!DbCrypt::Encrypt(reinterpret_cast<const BYTE*>(static_cast<LPCSTR>(utf8)), n, enc))
		return;
	// 임시 파일에 쓰고 바꿔치기 (쓰는 도중 끊겨도 예전 캐시는 남음)
	const CString tmp = g_diskFile + L".tmp";
	try
	{
		CFile f(tmp, CFile::modeCreate | CFile::modeWrite);
		f.Write(enc.data(), static_cast<UINT>(enc.size()));
		f.Close();
	}
	catch (CException* ex)
	{
		ex->Delete();
		::DeleteFileW(tmp);
		return;
	}
	if (::MoveFileExW(tmp, g_diskFile, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		g_diskDirty = false;
	else
		::DeleteFileW(tmp);
}

void CMediaInfoLabel::ForgetDisk(const CString& path)
{
	CString key = path;
	key.MakeLower();
	std::lock_guard<std::mutex> lock(g_diskMx);
	if (g_disk.erase(key))
		g_diskDirty = true;
}

CMediaInfoLabel::Info CMediaInfoLabel::ReadInfoCached(const CString& path)
{
	CString key = path;
	key.MakeLower();
	ULONGLONG size = 0, mtime = 0;
	const bool stamped = FileStamp(path, size, mtime);
	if (!stamped)
		return Info();   // 파일 없음
	{
		std::lock_guard<std::mutex> lock(g_diskMx);
		auto it = g_disk.find(key);
		if (it != g_disk.end())
		{
			const CMediaInfoLabel::Info& ci = it->second.info;
			const bool partial = (ci.width == 0 || ci.height == 0 || ci.duration100ns == 0);
			// 저장해 둔 값 (파일을 열지 않음) - 단, 값이 일부만 있는 것(예전에 mkv 해상도를 못 읽은 것 등)은 실행마다 한 번 다시 읽어 봄
			static std::set<CString> retried;
			if (it->second.size == size && it->second.mtime == mtime && (!partial || !retried.insert(key).second))
				return ci;
			g_disk.erase(it);             // 파일이 바뀜 / 다시 읽어 봄
			g_diskDirty = true;
		}
	}
	const Info info = ReadInfo(path);
	if (info.ok)
	{
		std::lock_guard<std::mutex> lock(g_diskMx);
		DiskEntry e;
		e.size = size;
		e.mtime = mtime;
		e.info = info;
		g_disk[key] = e;
		g_diskDirty = true;
	}
	return info;
}

CMediaInfoLabel::Info CMediaInfoLabel::ReadInfo(const CString& path)
{
	Info info;
	if (path.IsEmpty() || !::PathFileExistsW(path))
		return info;
	IPropertyStore* store = nullptr;
	if (SUCCEEDED(::SHGetPropertyStoreFromParsingName(path, nullptr, GPS_DEFAULT, IID_PPV_ARGS(&store))) && store)
	{
		info.width = ReadUInt(store, kFrameWidth);
		info.height = ReadUInt(store, kFrameHeight);
		const UINT rate = ReadUInt(store, kFrameRate);
		info.fps = rate / 1000.0;
		info.duration100ns = ReadUInt64(store, kDuration);
		store->Release();
	}
	// 속성에 없는 값(해상도 · fps · 재생 시간)은 파일을 직접 열어서 채움
	if (info.width == 0 || info.height == 0 || info.duration100ns == 0 || info.fps <= 0)
		ReadInfoMediaFoundation(path, info);
	// 그래도 모자라면 Matroska(mkv / webm) 헤더를 직접 읽음 (Windows 의 mkv 지원 · 코덱과 상관없음, Matroska 가 아니면 아무것도 안 함)
	if (info.width == 0 || info.height == 0 || info.duration100ns == 0 || info.fps <= 0)
		ReadInfoMatroska(path, info);
	info.ok = (info.width > 0 && info.height > 0) || info.fps > 0 || info.duration100ns > 0;
	return info;
}

CString CMediaInfoLabel::ResolutionText(UINT width, UINT height)
{
	if (width == 0 || height == 0)
		return CString();
	// 가로가 넓은 영상(1920x800 등)도 1080p 로 보이도록 16:9 기준 높이와 실제 높이 중 큰 값
	const UINT eff = (std::max)(height, width * 9 / 16);
	static const UINT kStd[] = { 4320, 2160, 1440, 1080, 720, 576, 480, 360, 240 };
	for (UINT s : kStd)
	{
		if (eff >= s * 92 / 100 && eff <= s * 108 / 100)
		{
			CString t;
			t.Format(L"%up", s);
			return t;
		}
	}
	CString t;
	t.Format(L"%up", height);
	return t;
}

CString CMediaInfoLabel::GradeText(UINT width, UINT height, CString* resText, COLORREF* color)
{
	if (width == 0 || height == 0)
		return CString();
	if (height > width)
		std::swap(width, height);   // 세로 영상은 가로 기준으로
	// ResolutionText 와 같은 기준 높이 (가로가 넓은 1920x800 등도 FHD), 약간의 오차 허용
	const UINT eff = (std::max)(height, width * 9 / 16);
	struct Grade { UINT minEff; const wchar_t* text; const wchar_t* res; COLORREF color; };
	static const Grade kGrades[] = {
		{ 4000, L"UHD", L"8K",    RGB(0xF2, 0xC1, 0x4E) },   // 4320p
		{ 2000, L"UHD", L"4K",    RGB(0xF2, 0xC1, 0x4E) },   // 2160p
		{ 1330, L"QHD", L"1440p", RGB(0xB3, 0x8C, 0xFF) },   // 1440p
		{ 1000, L"FHD", L"1080p", RGB(0x5A, 0xA9, 0xFF) },   // 1080p
		{  660, L"HD",  L"720p",  RGB(0x4F, 0xC3, 0xB5) },   // 720p
		{    0, L"SD",  nullptr,  RGB(0x9A, 0xA5, 0xAE) },   // 720p 미만 (큰 글자는 480p 등 실제 기준 높이)
	};
	for (const Grade& g : kGrades)
	{
		if (eff >= g.minEff)
		{
			if (color)
				*color = g.color;
			if (resText)
				*resText = g.res ? CString(g.res) : ResolutionText(width, height);
			return g.text;
		}
	}
	return CString();
}

CString CMediaInfoLabel::FpsText(double fps)
{
	if (fps <= 0)
		return CString();
	CString t;
	t.Format(L"%.2f", fps);
	// 끝의 0 과 소수점 정리: 30.00 → 30, 25.50 → 25.5
	while (t.Find(L'.') >= 0 && (t.Right(1) == L"0" || t.Right(1) == L"."))
	{
		const bool dot = (t.Right(1) == L".");
		t.Truncate(t.GetLength() - 1);
		if (dot)
			break;
	}
	return t;
}

CString CMediaInfoLabel::DurationText(ULONGLONG duration100ns)
{
	if (duration100ns == 0)
		return CString();
	const ULONGLONG sec = duration100ns / 10000000ULL;
	ULONGLONG minutes = (sec + 30) / 60;   // 반올림
	if (minutes == 0)
		minutes = 1;
	CString t;
	t.Format(L"%llu분", minutes);
	return t;
}

void CMediaInfoLabel::SetFile(const CString& path)
{
	if (path.CompareNoCase(m_path) == 0 && !path.IsEmpty())
		return;
	m_path = path;
	m_info = Info();
	if (!path.IsEmpty())
	{
		CString key = path;
		key.MakeLower();
		auto it = m_cache.find(key);
		if (it != m_cache.end())
			m_info = it->second;
		else
		{
			m_info = ReadInfoCached(path);   // 디스크 캐시 먼저
			m_cache[key] = m_info;
		}
	}
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

void CMediaInfoLabel::Forget(const CString& path)
{
	CString key = path;
	key.MakeLower();
	m_cache.erase(key);
	ForgetDisk(path);   // 디스크 캐시도 (다음에 파일을 다시 읽음)
	if (m_path.CompareNoCase(path) == 0)
		m_path.Empty();   // 같은 경로로 SetFile 해도 다시 읽게
}

void CMediaInfoLabel::ForgetFailed()
{
	for (auto it = m_cache.begin(); it != m_cache.end();)
	{
		if (!it->second.ok)
			it = m_cache.erase(it);
		else
			++it;
	}
	if (!m_info.ok)
		m_path.Empty();   // 같은 경로로 SetFile 해도 다시 읽게
}

CFont* CMediaInfoLabel::BaseFont()
{
	CWnd* parent = GetParent();
	return parent ? parent->GetFont() : nullptr;
}

void CMediaInfoLabel::EnsureBold()
{
	CFont* base = BaseFont();
	if (base && !m_bold.GetSafeHandle())
	{
		LOGFONT lf = {};
		base->GetLogFont(&lf);
		lf.lfWeight = FW_BOLD;
		m_bold.CreateFontIndirect(&lf);
	}
}

void CMediaInfoLabel::BuildParts(CString& left, CString& sep, CString& res) const
{
	left.Empty(); sep.Empty(); res.Empty();
	if (!m_info.ok)
		return;
	const CString fps = FpsText(m_info.fps);
	res = ResolutionText(m_info.width, m_info.height);
	// 왼쪽(회색): 총 재생 시간 + fps  예) "123분 | fps: 29.97"
	left = DurationText(m_info.duration100ns);
	if (!fps.IsEmpty())
		left += (left.IsEmpty() ? L"" : L" | ") + CString(L"fps: ") + fps;
	if (!left.IsEmpty() && !res.IsEmpty())
		sep = L" | ";
}

int CMediaInfoLabel::NeededWidth()
{
	CString left, sep, res;
	BuildParts(left, sep, res);
	CFont* base = BaseFont();
	if (!base || (left.IsEmpty() && res.IsEmpty()) || !GetSafeHwnd())
		return 0;
	EnsureBold();
	CClientDC dc(this);
	CFont* old = dc.SelectObject(base);
	int w = (left.IsEmpty() ? 0 : dc.GetTextExtent(left).cx) + (sep.IsEmpty() ? 0 : dc.GetTextExtent(sep).cx);
	dc.SelectObject(&m_bold);
	w += res.IsEmpty() ? 0 : dc.GetTextExtent(res).cx;
	dc.SelectObject(old);
	return w + 2;
}

void CMediaInfoLabel::OnPaint()
{
	CPaintDC paint(this);
	CRect rc;
	GetClientRect(&rc);
	CDC mem;
	mem.CreateCompatibleDC(&paint);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&paint, (std::max)(1, rc.Width()), (std::max)(1, rc.Height()));
	CBitmap* oldBmp = mem.SelectObject(&bmp);
	mem.FillSolidRect(rc, m_back);

	CFont* base = BaseFont();
	EnsureBold();

	if (m_info.ok && base)
	{
		CString left, sep, res;
		BuildParts(left, sep, res);

		CFont* old = mem.SelectObject(base);
		const int leftW = left.IsEmpty() ? 0 : mem.GetTextExtent(left).cx;
		const int sepW = sep.IsEmpty() ? 0 : mem.GetTextExtent(sep).cx;
		mem.SelectObject(&m_bold);
		const int resW = res.IsEmpty() ? 0 : mem.GetTextExtent(res).cx;

		mem.SetBkMode(TRANSPARENT);
		int x = rc.right - (leftW + sepW + resW);   // 오른쪽 정렬
		mem.SelectObject(base);
		mem.SetTextColor(m_sub);
		if (leftW)
		{
			mem.DrawText(left, CRect(x, rc.top, x + leftW, rc.bottom), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			x += leftW;
		}
		if (sepW)
		{
			mem.DrawText(sep, CRect(x, rc.top, x + sepW, rc.bottom), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			x += sepW;
		}
		if (resW)
		{
			mem.SelectObject(&m_bold);
			mem.SetTextColor(m_text);
			mem.DrawText(res, CRect(x, rc.top, x + resW, rc.bottom), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		}
		mem.SelectObject(old);
	}

	paint.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(oldBmp);
}
