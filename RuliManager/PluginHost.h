#pragma once

#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <utility>
#include <string>

// ---------------------------------------------------------------------------
// 외부 프로그램 플러그인
//  - plugins\<폴더>\plugin.json (UTF-8) 에 이름 · 실행 명령 · 대상(video / actor / tool) · 메뉴 이름 등을 적음
//  - 실행: 플러그인 폴더를 작업 폴더로 자식 프로세스 실행 → stdin 으로 요청 JSON 한 덩어리 (UTF-8) → 닫음
//          stdout 으로 한 줄에 JSON 하나: {"progress":40,"message":"..."} = 진행, {"item":{...}} = 결과 한 편 (바로 적용),
//          {"ok":true,...} = 끝 (마지막 것, 예전 방식의 "items" 한꺼번에도 됨), 그 밖의 줄 = 기록
//  - 플러그인이 띄운 하위 프로세스(크롬 드라이버 등)까지 작업 개체(Job)로 묶어 중단 · 시간 초과 때 함께 종료

namespace Json
{
	// 아주 작은 JSON 값 (파싱 · 읽기 전용) - 숫자는 double, 문자열은 CString
	struct Value
	{
		enum Type { tNull, tBool, tNumber, tString, tArray, tObject };
		Type    type = tNull;
		bool    b = false;
		double  num = 0;
		CString str;
		std::vector<Value> arr;
		std::vector<CString> keys;   // 객체: keys[i] ↔ vals[i]
		std::vector<Value> vals;

		bool IsObject() const { return type == tObject; }
		bool IsArray() const { return type == tArray; }
		const Value* Get(LPCWSTR key) const;          // 객체의 키 (없으면 nullptr)
		CString Text(LPCWSTR key) const;              // 문자열 · 숫자 · true/false → 글자, 배열 → 쉼표로 이음, 없으면 빈 문자열
		double  Number(LPCWSTR key, double def) const;
		bool    Bool(LPCWSTR key, bool def) const;
		std::vector<CString> List(LPCWSTR key) const; // 배열 → 글자 목록, 문자열 → 쉼표로 나눔
		CString AsText() const;
	};
	bool Parse(const CString& text, Value& out);       // 실패하면 false
	CString Quote(const CString& s);                   // "..." (이스케이프 포함)
	CString QuoteList(const std::vector<CString>& v);  // ["a","b"]
}

struct PluginInfo
{
	CString id;          // 폴더 이름 (켜기 / 끄기 기억 키)
	CString dir;         // 폴더 전체 경로
	CString name;        // 표시 이름
	CString version;
	CString author;
	CString description;
	CString run;         // 실행 명령 (예: "python fetch.py") - 첫 낱말이 플러그인 폴더 안 파일이면 전체 경로로 바꿔 실행
	CString target;      // "video" / "actor" / "tool"
	CString menu;        // 메뉴에 보일 이름 (없으면 name)
	int     api = 1;
	int     timeout = 120;   // 초, 0 = 제한 없음
	bool    keepBehind = true;   // 플러그인이 띄운 창(셀레늄 크롬 등)을 처음 뜰 때 RuliManager 뒤로 (plugin.json "keepBehind": false 로 끔)
	bool    enabled = true;  // 설정 [플러그인] 탭 체크 (레지스트리 Settings\PluginsDisabled 에 끈 것만 기록)
	CString error;           // plugin.json 오류 (있으면 실행 불가)

	bool Usable() const { return error.IsEmpty() && enabled; }
	CString TargetName() const;   // "영상" / "배우" / "관리 도구"
};

namespace PluginHost
{
	const int kApiVersion = 1;
	CString PluginsDir(bool create = false);           // 실행 파일 폴더부터 위로 3단계에서 plugins 폴더 (없으면 실행 파일 폴더\plugins)
	std::vector<PluginInfo> LoadAll();                 // 폴더를 훑어 plugin.json 읽기 + 켜기 / 끄기 상태 (이름 순)
	std::vector<PluginInfo> LoadUsable(LPCWSTR target);// 켜져 있고 오류 없는 그 대상의 플러그인만
	std::vector<CString> LoadDisabled();               // 끈 플러그인 id (레지스트리 Settings\PluginsDisabled, | 로 구분)
	void SaveDisabled(const std::vector<CString>& disabledIds);
	CString ToUtf16(const std::string& bytes);         // UTF-8 (아니면 ANSI) → 글자
	std::string ToUtf8(const CString& text);
}

// 플러그인 한 번 실행 (작업 스레드) - 진행 · 끝을 창에 PostMessage
class CPluginRun
{
public:
	struct Event
	{
		enum Kind { Progress, Log, Item, Done };
		Kind    kind = Log;
		int     percent = -1;     // Progress: 0 ~ 100 (-1 = 모름)
		CString text;             // Progress: 메시지, Log: 한 줄, Item: 그 줄 원문 ({"item":{...}}), Done: 오류 메시지 (성공이면 비어 있음)
		bool    ok = false;       // Done: 결과 JSON 의 ok
		CString resultJson;       // Done: 마지막 결과 줄 원문
		DWORD   exitCode = 0;
		bool    canceled = false;
		bool    timedOut = false;
	};

	CPluginRun() = default;
	~CPluginRun();
	CPluginRun(const CPluginRun&) = delete;
	CPluginRun& operator=(const CPluginRun&) = delete;

	// hwnd 에 msg 로 Event* 를 보냄 (lParam, 받는 쪽이 delete)
	bool Start(const PluginInfo& p, const CString& requestJson, HWND hwnd, UINT msg, CString& err);
	void Cancel(bool timedOut = false);   // 하위 프로세스까지 종료
	bool Running() const { return m_running; }
	// 일시 정지 / 다시 실행: 플러그인(과 하위 프로세스)의 모든 스레드를 멈춤 - 결과를 처리하는 동안(채울 항목 고르기 창 등) 다음 작업이 진행되지 않게
	void Pause(bool pause);
	std::vector<DWORD> ProcessIds() const;   // 플러그인과 하위 프로세스 (작업 개체 안 모든 프로세스)
	bool Paused() const { return m_paused; }

private:
	void Worker(std::string request);
	void Post(Event* e);

	HWND   m_hwnd = nullptr;
	UINT   m_msg = 0;
	HANDLE m_process = nullptr;
	HANDLE m_job = nullptr;
	HANDLE m_stdinWrite = nullptr;
	HANDLE m_stdoutRead = nullptr;
	std::thread m_thread;
	std::atomic<bool> m_running { false };
	std::atomic<bool> m_canceled { false };
	std::atomic<bool> m_timedOut { false };
	bool m_paused = false;
	std::vector<DWORD> m_pausedThreads;   // Pause 로 멈춘 스레드 (다시 실행할 때 그것만)
};
