#include "pch.h"
#include "PluginHost.h"

#include <algorithm>
#include <tlhelp32.h>
#include <set>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// ---------------------------------------------------------------------------
// JSON (작은 파서)

namespace Json
{
	namespace
	{
		struct Parser
		{
			const wchar_t* p;
			const wchar_t* end;

			void Skip()
			{
				while (p < end && (*p == L' ' || *p == L'\t' || *p == L'\r' || *p == L'\n' || *p == 0xFEFF))
					++p;
			}
			bool Lit(LPCWSTR s)
			{
				const size_t n = wcslen(s);
				if (static_cast<size_t>(end - p) < n || wcsncmp(p, s, n) != 0)
					return false;
				p += n;
				return true;
			}
			static int Hex(wchar_t c)
			{
				if (c >= L'0' && c <= L'9') return c - L'0';
				if (c >= L'a' && c <= L'f') return c - L'a' + 10;
				if (c >= L'A' && c <= L'F') return c - L'A' + 10;
				return -1;
			}
			bool Str(CString& out)
			{
				if (p >= end || *p != L'"')
					return false;
				++p;
				out.Empty();
				while (p < end && *p != L'"')
				{
					wchar_t c = *p++;
					if (c == L'\\')
					{
						if (p >= end)
							return false;
						c = *p++;
						switch (c)
						{
						case L'"': case L'\\': case L'/': out += c; break;
						case L'b': out += L'\b'; break;
						case L'f': out += L'\f'; break;
						case L'n': out += L'\n'; break;
						case L'r': out += L'\r'; break;
						case L't': out += L'\t'; break;
						case L'u':
						{
							if (end - p < 4)
								return false;
							int v = 0;
							for (int i = 0; i < 4; ++i)
							{
								const int h = Hex(p[i]);
								if (h < 0)
									return false;
								v = v * 16 + h;
							}
							p += 4;
							out += static_cast<wchar_t>(v);   // 서로게이트 쌍도 그대로 이어 붙이면 UTF-16
							break;
						}
						default:
							return false;
						}
					}
					else
						out += c;
				}
				if (p >= end)
					return false;
				++p;   // 닫는 "
				return true;
			}
			bool Val(Value& v, int depth)
			{
				if (depth > 64)
					return false;
				Skip();
				if (p >= end)
					return false;
				const wchar_t c = *p;
				if (c == L'{')
				{
					++p;
					v.type = Value::tObject;
					Skip();
					if (p < end && *p == L'}') { ++p; return true; }
					for (;;)
					{
						Skip();
						CString key;
						if (!Str(key))
							return false;
						Skip();
						if (p >= end || *p != L':')
							return false;
						++p;
						Value child;
						if (!Val(child, depth + 1))
							return false;
						v.keys.push_back(key);
						v.vals.push_back(std::move(child));
						Skip();
						if (p < end && *p == L',') { ++p; continue; }
						if (p < end && *p == L'}') { ++p; return true; }
						return false;
					}
				}
				if (c == L'[')
				{
					++p;
					v.type = Value::tArray;
					Skip();
					if (p < end && *p == L']') { ++p; return true; }
					for (;;)
					{
						Value child;
						if (!Val(child, depth + 1))
							return false;
						v.arr.push_back(std::move(child));
						Skip();
						if (p < end && *p == L',') { ++p; continue; }
						if (p < end && *p == L']') { ++p; return true; }
						return false;
					}
				}
				if (c == L'"')
				{
					v.type = Value::tString;
					return Str(v.str);
				}
				if (Lit(L"true"))  { v.type = Value::tBool; v.b = true; return true; }
				if (Lit(L"false")) { v.type = Value::tBool; v.b = false; return true; }
				if (Lit(L"null"))  { v.type = Value::tNull; return true; }
				if (c == L'-' || (c >= L'0' && c <= L'9'))
				{
					const wchar_t* s = p;
					while (p < end && (*p == L'-' || *p == L'+' || *p == L'.' || *p == L'e' || *p == L'E' || (*p >= L'0' && *p <= L'9')))
						++p;
					const CString num(s, static_cast<int>(p - s));
					wchar_t* stop = nullptr;
					v.type = Value::tNumber;
					v.num = wcstod(num, &stop);
					return stop && *stop == 0;
				}
				return false;
			}
		};
	}

	bool Parse(const CString& text, Value& out)
	{
		Parser ps{ static_cast<LPCWSTR>(text), static_cast<LPCWSTR>(text) + text.GetLength() };
		out = Value();
		if (!ps.Val(out, 0))
			return false;
		ps.Skip();
		return ps.p == ps.end;
	}

	const Value* Value::Get(LPCWSTR key) const
	{
		if (type != tObject)
			return nullptr;
		for (size_t i = 0; i < keys.size() && i < vals.size(); ++i)
			if (keys[i] == key)
				return &vals[i];
		return nullptr;
	}

	CString Value::AsText() const
	{
		switch (type)
		{
		case tString: return str;
		case tBool:   return b ? L"true" : L"false";
		case tNumber:
		{
			CString s;
			if (num == static_cast<double>(static_cast<long long>(num)))
				s.Format(L"%lld", static_cast<long long>(num));
			else
			{
				s.Format(L"%g", num);
				s.Replace(L',', L'.');
			}
			return s;
		}
		case tArray:
		{
			CString s;
			for (const Value& e : arr)
			{
				const CString t = e.AsText();
				if (t.IsEmpty())
					continue;
				if (!s.IsEmpty()) s += L", ";
				s += t;
			}
			return s;
		}
		default:
			return CString();
		}
	}

	CString Value::Text(LPCWSTR key) const
	{
		const Value* v = Get(key);
		if (!v)
			return CString();
		CString t = v->AsText();
		t.Trim();
		return t;
	}

	double Value::Number(LPCWSTR key, double def) const
	{
		const Value* v = Get(key);
		if (!v)
			return def;
		if (v->type == Value::tNumber)
			return v->num;
		if (v->type == Value::tString && !v->str.IsEmpty())
			return _wtof(v->str);
		return def;
	}

	bool Value::Bool(LPCWSTR key, bool def) const
	{
		const Value* v = Get(key);
		if (!v)
			return def;
		if (v->type == Value::tBool)
			return v->b;
		if (v->type == Value::tNumber)
			return v->num != 0;
		return def;
	}

	std::vector<CString> Value::List(LPCWSTR key) const
	{
		std::vector<CString> out;
		const Value* v = Get(key);
		if (!v)
			return out;
		auto add = [&out](CString s)
		{
			s.Trim();
			if (!s.IsEmpty())
				out.push_back(s);
		};
		if (v->type == Value::tArray)
		{
			for (const Value& e : v->arr)
				add(e.AsText());
		}
		else
		{
			const CString s = v->AsText();
			int pos = 0;
			for (;;)
			{
				const CString tok = s.Tokenize(L",", pos);
				if (pos < 0)
					break;
				add(tok);
			}
		}
		return out;
	}

	CString Quote(const CString& s)
	{
		CString out = L"\"";
		for (int i = 0; i < s.GetLength(); ++i)
		{
			const wchar_t c = s[i];
			switch (c)
			{
			case L'"':  out += L"\\\""; break;
			case L'\\': out += L"\\\\"; break;
			case L'\n': out += L"\\n"; break;
			case L'\r': out += L"\\r"; break;
			case L'\t': out += L"\\t"; break;
			default:
				if (c < 0x20)
				{
					CString u;
					u.Format(L"\\u%04x", static_cast<unsigned>(c));
					out += u;
				}
				else
					out += c;
			}
		}
		out += L"\"";
		return out;
	}

	CString QuoteList(const std::vector<CString>& v)
	{
		CString out = L"[";
		for (size_t i = 0; i < v.size(); ++i)
		{
			if (i > 0) out += L",";
			out += Quote(v[i]);
		}
		out += L"]";
		return out;
	}
}

// ---------------------------------------------------------------------------
// 플러그인 목록

CString PluginInfo::TargetName() const
{
	if (target == L"video") return L"영상";
	if (target == L"actor") return L"배우";
	if (target == L"tool")  return L"관리 도구";
	return target;
}

namespace PluginHost
{
	CString ToUtf16(const std::string& bytes)
	{
		if (bytes.empty())
			return CString();
		const char* p = bytes.data();
		int n = static_cast<int>(bytes.size());
		if (n >= 3 && static_cast<unsigned char>(p[0]) == 0xEF && static_cast<unsigned char>(p[1]) == 0xBB && static_cast<unsigned char>(p[2]) == 0xBF)
		{
			p += 3;
			n -= 3;
		}
		if (n <= 0)
			return CString();
		UINT cp = CP_UTF8;
		if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, p, n, nullptr, 0) == 0)
			cp = CP_ACP;   // UTF-8 이 아니면 ANSI (CP949 등)
		const int len = ::MultiByteToWideChar(cp, 0, p, n, nullptr, 0);
		CString w;
		if (len > 0)
		{
			::MultiByteToWideChar(cp, 0, p, n, w.GetBuffer(len), len);
			w.ReleaseBuffer(len);
		}
		return w;
	}

	std::string ToUtf8(const CString& text)
	{
		if (text.IsEmpty())
			return std::string();
		const int n = ::WideCharToMultiByte(CP_UTF8, 0, text, text.GetLength(), nullptr, 0, nullptr, nullptr);
		std::string s(static_cast<size_t>((std::max)(0, n)), '\0');
		if (n > 0)
			::WideCharToMultiByte(CP_UTF8, 0, text, text.GetLength(), &s[0], n, nullptr, nullptr);
		return s;
	}

	CString PluginsDir(bool create)
	{
		wchar_t exe[MAX_PATH * 2] = {};
		::GetModuleFileNameW(nullptr, exe, _countof(exe));
		CString dir(exe);
		const int slash = dir.ReverseFind(L'\\');
		if (slash > 0)
			dir = dir.Left(slash);
		const CString exeDir = dir;
		// 개발 중에는 x64\Debug → 프로젝트 폴더의 plugins (설명서 help 와 같은 규칙)
		CString probe = dir;
		for (int up = 0; up <= 3; ++up)
		{
			const CString cand = probe + L"\\plugins";
			if (::PathIsDirectoryW(cand))
				return cand;
			const int s = probe.ReverseFind(L'\\');
			if (s < 0)
				break;
			probe = probe.Left(s);
		}
		const CString def = exeDir + L"\\plugins";
		if (create)
			::CreateDirectoryW(def, nullptr);
		return def;
	}

	namespace
	{
		bool ReadAll(const CString& file, std::string& out)
		{
			out.clear();
			HANDLE h = ::CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
			if (h == INVALID_HANDLE_VALUE)
				return false;
			LARGE_INTEGER sz = {};
			if (!::GetFileSizeEx(h, &sz) || sz.QuadPart > 4 * 1024 * 1024)
			{
				::CloseHandle(h);
				return false;
			}
			out.resize(static_cast<size_t>(sz.QuadPart));
			DWORD got = 0;
			const BOOL ok = out.empty() || ::ReadFile(h, &out[0], static_cast<DWORD>(out.size()), &got, nullptr);
			::CloseHandle(h);
			out.resize(got);
			return ok != FALSE;
		}

	}

	std::vector<CString> LoadDisabled()
	{
		std::vector<CString> out;
		const CString s = AfxGetApp()->GetProfileString(L"Settings", L"PluginsDisabled", L"");
		int pos = 0;
		for (;;)
		{
			CString tok = s.Tokenize(L"|", pos);
			if (pos < 0)
				break;
			tok.Trim();
			if (!tok.IsEmpty())
				out.push_back(tok);
		}
		return out;
	}

	void SaveDisabled(const std::vector<CString>& disabledIds)
	{
		CString s;
		for (const CString& id : disabledIds)
		{
			if (!s.IsEmpty()) s += L"|";
			s += id;
		}
		AfxGetApp()->WriteProfileString(L"Settings", L"PluginsDisabled", s);
	}

	std::vector<PluginInfo> LoadAll()
	{
		std::vector<PluginInfo> out;
		const CString root = PluginsDir();
		const std::vector<CString> disabled = LoadDisabled();
		WIN32_FIND_DATAW fd = {};
		HANDLE f = ::FindFirstFileW(root + L"\\*", &fd);
		if (f == INVALID_HANDLE_VALUE)
			return out;
		do
		{
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.')
				continue;
			PluginInfo p;
			p.id = fd.cFileName;
			p.dir = root + L"\\" + fd.cFileName;
			p.name = p.id;
			const CString manifest = p.dir + L"\\plugin.json";
			if (!::PathFileExistsW(manifest))
				continue;   // plugin.json 이 없는 폴더는 플러그인이 아님
			std::string bytes;
			Json::Value v;
			if (!ReadAll(manifest, bytes))
				p.error = L"plugin.json 을 읽지 못했습니다.";
			else if (!Json::Parse(ToUtf16(bytes), v) || !v.IsObject())
				p.error = L"plugin.json 형식 오류 (JSON 객체가 아님)";
			else
			{
				if (!v.Text(L"name").IsEmpty())
					p.name = v.Text(L"name");
				p.version = v.Text(L"version");
				p.author = v.Text(L"author");
				p.description = v.Text(L"description");
				p.run = v.Text(L"run");
				p.target = v.Text(L"target");
				p.target.MakeLower();
				p.menu = v.Text(L"menu");
				if (p.menu.IsEmpty())
					p.menu = p.name;
				p.api = static_cast<int>(v.Number(L"api", 1));
				p.timeout = (std::max)(0, static_cast<int>(v.Number(L"timeout", 120)));
				p.keepBehind = v.Bool(L"keepBehind", true);
				if (p.run.IsEmpty())
					p.error = L"\"run\" (실행 명령) 이 없습니다.";
				else if (p.target != L"video" && p.target != L"actor" && p.target != L"tool")
					p.error = L"\"target\" 은 video / actor / tool 중 하나여야 합니다.";
				else if (p.api > kApiVersion)
				{
					p.error.Format(L"이 프로그램보다 새 API(%d) 용입니다 (지원: %d).", p.api, kApiVersion);
				}
			}
			for (const CString& d : disabled)
				if (d.CompareNoCase(p.id) == 0)
					p.enabled = false;
			out.push_back(p);
		} while (::FindNextFileW(f, &fd));
		::FindClose(f);
		std::sort(out.begin(), out.end(), [](const PluginInfo& a, const PluginInfo& b)
		{
			return ::StrCmpLogicalW(a.name, b.name) < 0;
		});
		return out;
	}

	std::vector<PluginInfo> LoadUsable(LPCWSTR target)
	{
		std::vector<PluginInfo> out;
		for (const PluginInfo& p : LoadAll())
			if (p.Usable() && p.target == target)
				out.push_back(p);
		return out;
	}
}

// ---------------------------------------------------------------------------
// 실행

namespace
{
	// 실행 명령의 첫 낱말이 플러그인 폴더 안 파일이면 전체 경로로 (CreateProcess 는 자식의 작업 폴더에서 프로그램을 찾지 않음)
	CString ResolveCommand(const CString& run, const CString& dir)
	{
		CString cmd = run;
		cmd.Trim();
		CString first, rest;
		if (!cmd.IsEmpty() && cmd[0] == L'"')
		{
			const int q = cmd.Find(L'"', 1);
			if (q < 0)
				return cmd;
			first = cmd.Mid(1, q - 1);
			rest = cmd.Mid(q + 1);
		}
		else
		{
			const int sp = cmd.Find(L' ');
			first = (sp < 0) ? cmd : cmd.Left(sp);
			rest = (sp < 0) ? CString() : cmd.Mid(sp);
		}
		if (first.IsEmpty() || !::PathIsRelativeW(first))
			return cmd;
		const CString local = dir + L"\\" + first;
		if (::PathFileExistsW(local) && !::PathIsDirectoryW(local))
			return L"\"" + local + L"\"" + rest;
		return cmd;
	}
}

CPluginRun::~CPluginRun()
{
	if (m_running)
		Cancel();
	if (m_thread.joinable())
		m_thread.join();
	for (HANDLE* h : { &m_process, &m_job, &m_stdinWrite, &m_stdoutRead })
	{
		if (*h)
		{
			::CloseHandle(*h);
			*h = nullptr;
		}
	}
}

bool CPluginRun::Start(const PluginInfo& p, const CString& requestJson, HWND hwnd, UINT msg, CString& err)
{
	err.Empty();
	if (m_running || m_thread.joinable())
	{
		err = L"이미 실행 중입니다.";
		return false;
	}
	m_hwnd = hwnd;
	m_msg = msg;
	m_canceled = false;
	m_timedOut = false;

	SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
	HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
	if (!::CreatePipe(&inRead, &inWrite, &sa, 1 << 20) || !::CreatePipe(&outRead, &outWrite, &sa, 1 << 16))
	{
		err = L"파이프를 만들지 못했습니다.";
		for (HANDLE h : { inRead, inWrite, outRead, outWrite })
			if (h) ::CloseHandle(h);
		return false;
	}
	// 부모 쪽 끝은 자식에게 상속하지 않음 (상속되면 자식이 끝나도 읽기가 끝나지 않음)
	::SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
	::SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

	// 파이썬 플러그인이 한글 · 일본어를 UTF-8 로 주고받도록 (부모 환경 변수 → 자식이 물려받음)
	::SetEnvironmentVariableW(L"PYTHONIOENCODING", L"utf-8");
	::SetEnvironmentVariableW(L"PYTHONUTF8", L"1");
	CString api;
	api.Format(L"%d", PluginHost::kApiVersion);
	::SetEnvironmentVariableW(L"RULIMANAGER_API", api);
	::SetEnvironmentVariableW(L"RULIMANAGER_PLUGIN_DIR", p.dir);

	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	si.hStdInput = inRead;
	si.hStdOutput = outWrite;
	si.hStdError = outWrite;   // 오류 출력도 같은 줄로 (JSON 이 아닌 줄 = 기록)
	PROCESS_INFORMATION pi = {};
	CString cmd = ResolveCommand(p.run, p.dir);
	std::vector<wchar_t> buf(cmd.GetString(), cmd.GetString() + cmd.GetLength() + 1);
	const BOOL created = ::CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
		CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, p.dir, &si, &pi);
	::CloseHandle(inRead);
	::CloseHandle(outWrite);
	if (!created)
	{
		const DWORD e = ::GetLastError();
		::CloseHandle(inWrite);
		::CloseHandle(outRead);
		err.Format(L"실행하지 못했습니다 (오류 %lu).\n\n명령: %s\n폴더: %s\n\n파이썬 플러그인이면 python 이 설치되어 있고 PATH 에 등록되어 있는지 확인하세요.",
			e, static_cast<LPCWSTR>(cmd), static_cast<LPCWSTR>(p.dir));
		return false;
	}
	// 작업 개체: 플러그인이 띄운 하위 프로세스까지 한꺼번에 종료할 수 있게
	m_job = ::CreateJobObjectW(nullptr, nullptr);
	if (m_job)
	{
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {};
		li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		::SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &li, sizeof(li));
		if (!::AssignProcessToJobObject(m_job, pi.hProcess))
		{
			::CloseHandle(m_job);
			m_job = nullptr;
		}
	}
	::ResumeThread(pi.hThread);
	::CloseHandle(pi.hThread);
	m_process = pi.hProcess;
	m_stdinWrite = inWrite;
	m_stdoutRead = outRead;
	m_running = true;
	m_thread = std::thread(&CPluginRun::Worker, this, PluginHost::ToUtf8(requestJson));
	return true;
}

void CPluginRun::Cancel(bool timedOut)
{
	if (!m_running)
		return;
	if (timedOut)
		m_timedOut = true;
	m_canceled = true;
	Pause(false);   // 멈춰 있으면 풀고 종료
	if (m_job)
		::TerminateJobObject(m_job, 1);
	else if (m_process)
		::TerminateProcess(m_process, 1);
}

void CPluginRun::Pause(bool pause)
{
	if (pause == m_paused)
		return;
	if (!pause)
	{
		// 멈춘 스레드만 다시 실행
		for (DWORD tid : m_pausedThreads)
		{
			if (HANDLE t = ::OpenThread(THREAD_SUSPEND_RESUME, FALSE, tid))
			{
				::ResumeThread(t);
				::CloseHandle(t);
			}
		}
		m_pausedThreads.clear();
		m_paused = false;
		return;
	}
	if (!m_running || !m_process)
		return;
	// 작업 개체 안의 모든 프로세스 (플러그인 + 크롬 드라이버 등 하위 프로세스)
	std::set<DWORD> pids;
	for (DWORD id : ProcessIds())
		pids.insert(id);
	HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return;
	THREADENTRY32 te = {};
	te.dwSize = sizeof(te);
	if (::Thread32First(snap, &te))
	{
		do
		{
			if (pids.count(te.th32OwnerProcessID) == 0)
				continue;
			if (HANDLE t = ::OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID))
			{
				if (::SuspendThread(t) != static_cast<DWORD>(-1))
					m_pausedThreads.push_back(te.th32ThreadID);
				::CloseHandle(t);
			}
		} while (::Thread32Next(snap, &te));
	}
	::CloseHandle(snap);
	m_paused = true;
}

std::vector<DWORD> CPluginRun::ProcessIds() const
{
	std::vector<DWORD> out;
	if (!m_process)
		return out;
	out.push_back(::GetProcessId(m_process));
	if (m_job)
	{
		std::vector<BYTE> buf(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + sizeof(ULONG_PTR) * 256);
		JOBOBJECT_BASIC_PROCESS_ID_LIST* list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buf.data());
		list->NumberOfAssignedProcesses = 256;
		if (::QueryInformationJobObject(m_job, JobObjectBasicProcessIdList, list, static_cast<DWORD>(buf.size()), nullptr))
			for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i)
			{
				const DWORD id = static_cast<DWORD>(list->ProcessIdList[i]);
				if (std::find(out.begin(), out.end(), id) == out.end())
					out.push_back(id);
			}
	}
	return out;
}

void CPluginRun::Post(Event* e)
{
	if (!m_hwnd || !::PostMessageW(m_hwnd, m_msg, 0, reinterpret_cast<LPARAM>(e)))
		delete e;
}

void CPluginRun::Worker(std::string request)
{
	// 1) 요청 보내기 → stdin 닫기 (플러그인은 끝까지 읽으면 됨)
	{
		size_t sent = 0;
		while (sent < request.size())
		{
			DWORD w = 0;
			const DWORD chunk = static_cast<DWORD>((std::min)(request.size() - sent, static_cast<size_t>(1 << 16)));
			if (!::WriteFile(m_stdinWrite, request.data() + sent, chunk, &w, nullptr) || w == 0)
				break;
			sent += w;
		}
		::CloseHandle(m_stdinWrite);
		m_stdinWrite = nullptr;
	}

	// 2) 한 줄씩 읽기
	CString result;
	bool haveResult = false, ok = false;
	auto handleLine = [&](std::string raw)
	{
		while (!raw.empty() && (raw.back() == '\r' || raw.back() == '\n'))
			raw.pop_back();
		CString line = PluginHost::ToUtf16(raw);
		line.Trim();
		if (line.IsEmpty())
			return;
		Json::Value v;
		if (line[0] == L'{' && Json::Parse(line, v) && v.IsObject())
		{
			if (const Json::Value* item = v.Get(L"item"))
			{
				if (item->IsObject())
				{
					// 결과 한 편 → 실행 창이 바로 적용 (플러그인은 다음 것을 계속)
					Event* e = new Event;
					e->kind = Event::Item;
					e->text = line;
					Post(e);
					return;
				}
			}
			if (v.Get(L"ok"))
			{
				result = line;
				haveResult = true;
				ok = v.Bool(L"ok", false);
				return;
			}
			if (v.Get(L"progress") || v.Get(L"message"))
			{
				Event* e = new Event;
				e->kind = Event::Progress;
				e->percent = v.Get(L"progress") ? static_cast<int>(v.Number(L"progress", -1)) : -1;
				e->text = v.Text(L"message");
				Post(e);
				return;
			}
		}
		Event* e = new Event;
		e->kind = Event::Log;
		e->text = line;
		Post(e);
	};
	std::string pending;
	char chunk[4096];
	for (;;)
	{
		DWORD got = 0;
		if (!::ReadFile(m_stdoutRead, chunk, sizeof(chunk), &got, nullptr) || got == 0)
			break;   // 플러그인이 끝남 (또는 중단으로 끊김)
		pending.append(chunk, got);
		size_t nl;
		while ((nl = pending.find('\n')) != std::string::npos)
		{
			handleLine(pending.substr(0, nl));
			pending.erase(0, nl + 1);
		}
		if (pending.size() > (8u << 20))
		{
			handleLine(pending);   // 줄바꿈 없이 너무 긴 출력
			pending.clear();
		}
	}
	if (!pending.empty())
		handleLine(pending);

	// 3) 끝
	DWORD code = 0;
	if (m_process)
	{
		::WaitForSingleObject(m_process, 5000);
		::GetExitCodeProcess(m_process, &code);
	}
	Event* e = new Event;
	e->kind = Event::Done;
	e->exitCode = code;
	e->canceled = m_canceled;
	e->timedOut = m_timedOut;
	e->resultJson = result;
	e->ok = haveResult && ok && !m_canceled;
	if (m_timedOut)
		e->text = L"시간 제한을 넘겨 중단했습니다.";
	else if (m_canceled)
		e->text = L"중단했습니다.";
	else if (!haveResult)
		e->text.Format(L"플러그인이 결과({\"ok\": ...})를 보내지 않았습니다. (종료 코드 %lu)", code);
	else if (!ok)
	{
		Json::Value v;
		Json::Parse(result, v);
		e->text = v.Text(L"error");
		if (e->text.IsEmpty())
			e->text = v.Text(L"message");
		if (e->text.IsEmpty())
			e->text = L"플러그인이 실패를 알렸습니다.";
	}
	m_running = false;
	Post(e);
}
