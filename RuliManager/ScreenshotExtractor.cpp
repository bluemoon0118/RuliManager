// 이 파일은 C++/WinRT(얼굴 인식)를 쓰므로 프로젝트 설정에서 미리 컴파일된 헤더 없이 C++20 으로 빌드 (RuliManager.vcxproj)
#include "framework.h"

// Windows 헤더의 min / max 매크로가 C++/WinRT 헤더와 부딪히지 않게 잠시 해제
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.FaceAnalysis.h>
#include <winrt/Windows.Storage.Streams.h>
#pragma pop_macro("max")
#pragma pop_macro("min")

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>
#include "ScreenshotExtractor.h"
#include "FrameReader.h"

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "propsys.lib")

namespace
{
	const LONGLONG kSecond = 10000000LL;   // 100ns 단위 1초
	const LONGLONG kRetryStep = 10 * kSecond;   // 얼굴을 못 찾으면 이만큼 뒤로 다시 찍어 봄 (10초)

	using Frame = VideoFrame;   // 장면 · 장면 읽기는 FrameReader.h/.cpp (커버 만들기 창과 공용)

	// Windows 얼굴 인식: 장면에 얼굴이 하나라도 있으면 true
	class CFaceChecker
	{
	public:
		CFaceChecker()
		{
			try
			{
				using namespace winrt::Windows::Media::FaceAnalysis;
				if (FaceDetector::IsSupported())
					m_detector = FaceDetector::CreateAsync().get();
			}
			catch (...)
			{
				m_detector = nullptr;
			}
		}

		bool Available() const { return m_detector != nullptr; }

		bool HasFace(const Frame& f)
		{
			if (!m_detector || f.w <= 0 || f.h <= 0)
				return true;   // 인식을 못 쓰면 그냥 통과
			try
			{
				using namespace winrt::Windows::Graphics::Imaging;
				// 회색조로, 긴 변 1280 이하로 줄여서 (속도), 폭 · 높이는 짝수
				const double scale = (std::min)(1.0, 1280.0 / (std::max)(f.w, f.h));
				const int gw = (std::max)(2, static_cast<int>(f.w * scale) & ~1);
				const int gh = (std::max)(2, static_cast<int>(f.h * scale) & ~1);
				std::vector<uint8_t> gray(static_cast<size_t>(gw) * gh);
				for (int y = 0; y < gh; ++y)
				{
					const int sy = (std::min)(f.h - 1, static_cast<int>(y / scale));
					const BYTE* row = f.bgra.data() + static_cast<size_t>(sy) * f.w * 4;
					for (int x = 0; x < gw; ++x)
					{
						const int sx = (std::min)(f.w - 1, static_cast<int>(x / scale));
						const BYTE* p = row + sx * 4;
						gray[static_cast<size_t>(y) * gw + x] = static_cast<uint8_t>((p[2] * 77 + p[1] * 150 + p[0] * 29) >> 8);
					}
				}
				winrt::Windows::Storage::Streams::DataWriter writer;
				writer.WriteBytes(winrt::array_view<uint8_t const>(gray.data(), gray.data() + gray.size()));
				SoftwareBitmap bitmap(BitmapPixelFormat::Gray8, gw, gh);
				bitmap.CopyFromBuffer(writer.DetachBuffer());
				const auto faces = m_detector.DetectFacesAsync(bitmap).get();
				return faces.Size() > 0;
			}
			catch (...)
			{
				return true;
			}
		}

	private:
		winrt::Windows::Media::FaceAnalysis::FaceDetector m_detector{ nullptr };
	};

	bool SaveJpeg(const Frame& f, const CString& path)
	{
		CImage img;
		if (!img.Create(f.w, -f.h, 32))   // 위에서 아래로
			return false;
		for (int y = 0; y < f.h; ++y)
			memcpy(img.GetPixelAddress(0, y), f.bgra.data() + static_cast<size_t>(y) * f.w * 4, static_cast<size_t>(f.w) * 4);
		return SUCCEEDED(img.Save(path, Gdiplus::ImageFormatJPEG));
	}

	// 예전에 만든 "<영상 이름>_NN.jpg" (NN = 숫자) 지우기
	void DeleteOldShots(const CString& dir, const CString& stem)
	{
		WIN32_FIND_DATAW fd = {};
		HANDLE h = ::FindFirstFileW(dir + stem + L"_*.jpg", &fd);
		if (h == INVALID_HANDLE_VALUE)
			return;
		do
		{
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;
			CString name = fd.cFileName;
			if (name.GetLength() <= stem.GetLength() + 5)
				continue;
			const CString num = name.Mid(stem.GetLength() + 1, name.GetLength() - stem.GetLength() - 5);   // "_" 와 ".jpg" 사이
			bool digits = !num.IsEmpty();
			for (int i = 0; i < num.GetLength(); ++i)
				if (num[i] < L'0' || num[i] > L'9') { digits = false; break; }
			if (digits)
				::DeleteFileW(dir + name);
		} while (::FindNextFileW(h, &fd));
		::FindClose(h);
	}
}

ShotResult ExtractScreenshots(const CString& videoPath, int count, const CString& folderName, const std::atomic<bool>& cancel,
	const std::function<void(int done, int total)>& progress)
{
	ShotResult res;
	res.video = videoPath;
	if (count <= 0)
		return res;
	bool apartment = false;
	try
	{
		winrt::init_apartment(winrt::apartment_type::multi_threaded);   // 이 스레드: COM / WinRT (얼굴 인식의 .get() 대기는 STA 에서 쓸 수 없음)
		apartment = true;
	}
	catch (...)
	{
	}
	const bool mf = SUCCEEDED(::MFStartup(MF_VERSION, MFSTARTUP_LITE));
	{
		CFrameReader reader;
		if (!mf || !reader.Open(videoPath))
			res.error = L"영상을 열지 못했습니다 (지원하지 않는 형식이거나 파일을 읽을 수 없음).";
		else
		{
			CString dir = videoPath;
			::PathRemoveFileSpecW(dir.GetBuffer());
			dir.ReleaseBuffer();
			dir += L"\\" + (folderName.IsEmpty() ? CString(L"Screenshot") : folderName) + L"\\";   // <품번>_Screenshot
			res.folder = dir;
			::SHCreateDirectoryExW(nullptr, dir, nullptr);
			CString stem = ::PathFindFileNameW(videoPath);
			::PathRemoveExtensionW(stem.GetBuffer());
			stem.ReleaseBuffer();
			DeleteOldShots(dir, stem);

			CFaceChecker faces;
			res.faceDetect = faces.Available();
			const LONGLONG interval = reader.Duration() / (count + 2);   // 맨 앞 · 맨 뒤 칸은 버림
			for (int k = 1; k <= count; ++k)
			{
				if (cancel)
				{
					res.canceled = true;
					break;
				}
				const LONGLONG segStart = interval * k;
				const LONGLONG segEnd = interval * (k + 1);
				Frame first;
				if (!reader.Grab(segStart, first))
				{
					++res.failed;
					if (progress) progress(k, count);
					continue;
				}
				const Frame* chosen = &first;
				Frame later;
				bool found = faces.HasFace(first);
				for (LONGLONG t = segStart + kRetryStep; !found && t < segEnd && !cancel; t += kRetryStep)   // 사람이 없으면 10초씩 뒤로
				{
					if (reader.Grab(t, later) && faces.HasFace(later))   // 10초 뒤는 이어서 디코딩하는 것보다 키프레임으로 이동하는 쪽이 빠름 (키프레임 간격이 보통 10초보다 짧음)
					{
						chosen = &later;
						found = true;
					}
				}
				if (!found)
					++res.noFace;   // 칸 시작 장면을 그대로
				CString file;
				file.Format(L"%s_%02d.jpg", static_cast<LPCWSTR>(stem), k);
				if (SaveJpeg(*chosen, dir + file))
					++res.saved;
				else
					++res.failed;
				if (progress)
					progress(k, count);
			}
		}
	}
	if (mf)
		::MFShutdown();
	if (apartment)
		winrt::uninit_apartment();
	return res;
}
