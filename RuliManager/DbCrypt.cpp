#include "pch.h"
#include "DbCrypt.h"
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	const char kMagic[] = "VMDB-AES1";
	const size_t kMagicLen = sizeof(kMagic) - 1;
	const size_t kIvLen = 16;

	// 고정 키 원문 (바꾸면 기존 암호화 DB를 읽을 수 없음)
	const char kSecret[] = "VideoManager::LibraryKey::7f3a9c21-4be8-4d0e-9a6f-d15c2b8e6a40";

	bool Sha256(const BYTE* data, ULONG len, BYTE out[32])
	{
		BCRYPT_ALG_HANDLE alg = nullptr;
		if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
			return false;
		BCRYPT_HASH_HANDLE hash = nullptr;
		bool ok = BCRYPT_SUCCESS(::BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0)) &&
			BCRYPT_SUCCESS(::BCryptHashData(hash, const_cast<PUCHAR>(data), len, 0)) &&
			BCRYPT_SUCCESS(::BCryptFinishHash(hash, out, 32, 0));
		if (hash) ::BCryptDestroyHash(hash);
		::BCryptCloseAlgorithmProvider(alg, 0);
		return ok;
	}

	// AES-256-CBC 키 핸들 만들기
	bool OpenKey(BCRYPT_ALG_HANDLE& alg, BCRYPT_KEY_HANDLE& key)
	{
		alg = nullptr;
		key = nullptr;
		BYTE keyBytes[32] = {};
		if (!Sha256(reinterpret_cast<const BYTE*>(kSecret), static_cast<ULONG>(sizeof(kSecret) - 1), keyBytes))
			return false;
		if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0)))
			return false;
		if (!BCRYPT_SUCCESS(::BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
				reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
				static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_CBC)), 0)) ||
			!BCRYPT_SUCCESS(::BCryptGenerateSymmetricKey(alg, &key, nullptr, 0, keyBytes, sizeof(keyBytes), 0)))
		{
			::SecureZeroMemory(keyBytes, sizeof(keyBytes));
			::BCryptCloseAlgorithmProvider(alg, 0);
			alg = nullptr;
			return false;
		}
		::SecureZeroMemory(keyBytes, sizeof(keyBytes));
		return true;
	}

	void CloseKey(BCRYPT_ALG_HANDLE alg, BCRYPT_KEY_HANDLE key)
	{
		if (key) ::BCryptDestroyKey(key);
		if (alg) ::BCryptCloseAlgorithmProvider(alg, 0);
	}
}

namespace DbCrypt
{
	bool IsEncrypted(const BYTE* data, size_t len)
	{
		return len >= kMagicLen + kIvLen && memcmp(data, kMagic, kMagicLen) == 0;
	}

	bool Encrypt(const BYTE* plain, size_t len, std::vector<BYTE>& out)
	{
		BCRYPT_ALG_HANDLE alg = nullptr;
		BCRYPT_KEY_HANDLE key = nullptr;
		if (!OpenKey(alg, key))
			return false;

		BYTE iv[kIvLen] = {};
		bool ok = BCRYPT_SUCCESS(::BCryptGenRandom(nullptr, iv, sizeof(iv), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
		ULONG cipherLen = 0;
		BYTE ivWork[kIvLen];
		if (ok)
		{
			memcpy(ivWork, iv, kIvLen);
			ok = BCRYPT_SUCCESS(::BCryptEncrypt(key, const_cast<PUCHAR>(plain), static_cast<ULONG>(len), nullptr,
				ivWork, kIvLen, nullptr, 0, &cipherLen, BCRYPT_BLOCK_PADDING));
		}
		if (ok)
		{
			out.assign(kMagicLen + kIvLen + cipherLen, 0);
			memcpy(out.data(), kMagic, kMagicLen);
			memcpy(out.data() + kMagicLen, iv, kIvLen);
			memcpy(ivWork, iv, kIvLen);   // 크기 계산 때 IV 가 바뀌었을 수 있음
			ULONG written = 0;
			ok = BCRYPT_SUCCESS(::BCryptEncrypt(key, const_cast<PUCHAR>(plain), static_cast<ULONG>(len), nullptr,
				ivWork, kIvLen, out.data() + kMagicLen + kIvLen, cipherLen, &written, BCRYPT_BLOCK_PADDING));
			if (ok)
				out.resize(kMagicLen + kIvLen + written);
		}
		CloseKey(alg, key);
		return ok;
	}

	bool Decrypt(const BYTE* data, size_t len, std::vector<BYTE>& plain)
	{
		if (!IsEncrypted(data, len))
			return false;
		BCRYPT_ALG_HANDLE alg = nullptr;
		BCRYPT_KEY_HANDLE key = nullptr;
		if (!OpenKey(alg, key))
			return false;

		BYTE iv[kIvLen];
		memcpy(iv, data + kMagicLen, kIvLen);
		const BYTE* cipher = data + kMagicLen + kIvLen;
		const ULONG cipherLen = static_cast<ULONG>(len - kMagicLen - kIvLen);
		plain.assign(cipherLen, 0);
		ULONG written = 0;
		const bool ok = cipherLen > 0 && BCRYPT_SUCCESS(::BCryptDecrypt(key, const_cast<PUCHAR>(cipher), cipherLen, nullptr,
			iv, kIvLen, plain.data(), cipherLen, &written, BCRYPT_BLOCK_PADDING));
		if (ok)
			plain.resize(written);
		else
			plain.clear();
		CloseKey(alg, key);
		return ok;
	}
}
