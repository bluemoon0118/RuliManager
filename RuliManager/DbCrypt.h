#pragma once

#include <vector>

// DB 파일(library.tsv / pending.tsv) 암호화
//  - AES-256-CBC (Windows CNG / bcrypt), 키 = 프로그램 안의 고정 문자열의 SHA-256
//  - 파일 형식: "VMDB-AES1" (9바이트) + IV 16바이트 + 암호문 (PKCS#7 패딩)
//  - 메모장 등으로 내용을 볼 수 없게 하는 용도 (프로그램을 분석하면 풀 수 있음)
namespace DbCrypt
{
	bool IsEncrypted(const BYTE* data, size_t len);
	bool Encrypt(const BYTE* plain, size_t len, std::vector<BYTE>& out);
	bool Decrypt(const BYTE* data, size_t len, std::vector<BYTE>& plain);
}
