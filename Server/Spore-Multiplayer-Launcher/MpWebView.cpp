/****************************************************************************
* Spore Multiplayer - Launcher window
****************************************************************************/
#include "MpWebView.h"

#include <windows.h>
#include <objbase.h>
#include <wrl.h>
#include <WebView2.h>

#include <string>

#pragma comment(lib, "WebView2Loader.dll.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace SporeMP
{
	namespace
	{
		// Window and WebView state. There is a single launcher window, so file
		// statics keep the code short.
		HWND g_window = nullptr;
		ComPtr<ICoreWebView2Environment> g_environment;
		ComPtr<ICoreWebView2Controller> g_controller;
		ComPtr<ICoreWebView2> g_webView;
		bool g_closed = false;
		bool g_ready = false;

		const wchar_t* kWindowClass = L"SporeMultiplayerLauncherWindow";

		/// Converts an UTF-8 string to a wide string.
		std::wstring ToWide(const std::string& text)
		{
			if (text.empty()) return std::wstring();

			const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
			if (length <= 1) return std::wstring();

			std::wstring result(static_cast<size_t>(length - 1), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &result[0], length);
			return result;
		}

		void ResizeWebView()
		{
			if (g_controller == nullptr || g_window == nullptr) return;

			RECT bounds;
			GetClientRect(g_window, &bounds);
			g_controller->put_Bounds(bounds);
		}

		LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			switch (message)
			{
			case WM_SIZE:
				ResizeWebView();
				return 0;

			case WM_DESTROY:
				g_closed = true;
				PostQuitMessage(0);
				return 0;

			default:
				// The window class is registered with RegisterClassExW, so the window
				// is a Unicode window. DefWindowProc must be the W variant: with the
				// project built as MultiByte, plain DefWindowProc is the ANSI one and
				// would truncate every WM_SETTEXT to its first character.
				return DefWindowProcW(hwnd, message, wParam, lParam);
			}
		}

		bool CreateWindowForWebView(const std::wstring& title, std::string& error)
		{
			HINSTANCE instance = GetModuleHandle(nullptr);

			WNDCLASSEXW windowClass;
			std::memset(&windowClass, 0, sizeof(windowClass));
			windowClass.cbSize = sizeof(windowClass);
			windowClass.lpfnWndProc = WindowProc;
			windowClass.hInstance = instance;
			windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
			windowClass.hbrBackground = CreateSolidBrush(RGB(13, 18, 16));
			windowClass.lpszClassName = kWindowClass;

			if (RegisterClassExW(&windowClass) == 0)
			{
				const DWORD lastError = GetLastError();
				if (lastError != ERROR_CLASS_ALREADY_EXISTS)
				{
					error = "RegisterClassEx failed";
					return false;
				}
			}

			// A comfortable default size, centred on the work area.
			int width = 1000;
			int height = 760;

			RECT workArea;
			if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0))
			{
				width = (workArea.right - workArea.left) * 2 / 3;
				height = (workArea.bottom - workArea.top) * 3 / 4;

				if (width < 760) width = 760;
				if (height < 560) height = 560;
			}

			g_window = CreateWindowExW(
				0, kWindowClass, title.c_str(),
				WS_OVERLAPPEDWINDOW,
				CW_USEDEFAULT, CW_USEDEFAULT, width, height,
				nullptr, nullptr, instance, nullptr);
			if (g_window == nullptr)
			{
				error = "CreateWindowEx failed";
				return false;
			}

			ShowWindow(g_window, SW_SHOW);
			UpdateWindow(g_window);
			return true;
		}
	}

	WebViewStatus ShowWebViewWindow(const std::string& url, std::string& error)
	{
		if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
		{
			error = "CoInitializeEx failed";
			return WebViewStatus::Failed;
		}

		const std::wstring title = L"Spore Multiplayer";
		if (!CreateWindowForWebView(title, error))
		{
			return WebViewStatus::Failed;
		}

		// The environment creation fails when the WebView2 runtime is missing.
		const HRESULT environmentResult = CreateCoreWebView2EnvironmentWithOptions(
			nullptr, nullptr, nullptr,
			Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
				[](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT
				{
					if (FAILED(result) || environment == nullptr) return result;

					g_environment = environment;

					environment->CreateCoreWebView2Controller(g_window,
						Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
							[](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT
							{
								if (FAILED(controllerResult) || controller == nullptr) return controllerResult;

								g_controller = controller;
								controller->get_CoreWebView2(&g_webView);

								ResizeWebView();

								// The launcher page does not need dev tools, context
								// menus or the browser accelerators.
								ComPtr<ICoreWebView2Settings> settings;
								if (SUCCEEDED(g_webView->get_Settings(&settings)) && settings != nullptr)
								{
									settings->put_AreDefaultContextMenusEnabled(FALSE);
									settings->put_AreDevToolsEnabled(FALSE);
									settings->put_IsStatusBarEnabled(FALSE);
								}

								g_ready = true;
								return S_OK;
							}).Get());
					return S_OK;
				}).Get());

		if (FAILED(environmentResult))
		{
			// HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) means "runtime missing".
			if (g_window != nullptr)
			{
				DestroyWindow(g_window);
				g_window = nullptr;
			}

			error = "The WebView2 runtime is not installed";
			return WebViewStatus::RuntimeMissing;
		}

		// Wait for the controller, pumping messages so the callbacks can run.
		const DWORD deadline = GetTickCount() + 10000;
		while (!g_ready && !g_closed && GetTickCount() < deadline)
		{
			MSG message;
			while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&message);
				DispatchMessage(&message);
			}

			if (!g_ready) Sleep(10);
		}

		if (!g_ready)
		{
			if (g_window != nullptr)
			{
				DestroyWindow(g_window);
				g_window = nullptr;
			}

			error = "The WebView2 controller did not initialise";
			return WebViewStatus::Failed;
		}

		g_webView->Navigate(ToWide(url).c_str());

		// Keep the page from opening new windows or navigating away.
		EventRegistrationToken token;
		g_webView->add_NewWindowRequested(
			Callback<ICoreWebView2NewWindowRequestedEventHandler>(
				[](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT
				{
					args->put_Handled(TRUE);
					return S_OK;
				}).Get(), &token);

		return WebViewStatus::Ok;
	}

	void RunWebViewLoop()
	{
		MSG message;
		while (!g_closed && GetMessage(&message, nullptr, 0, 0))
		{
			TranslateMessage(&message);
			DispatchMessage(&message);
		}
	}

	void ShutdownWebViewWindow()
	{
		if (g_controller != nullptr)
		{
			g_controller->Close();
			g_controller = nullptr;
		}

		g_webView = nullptr;
		g_environment = nullptr;

		if (g_window != nullptr)
		{
			DestroyWindow(g_window);
			g_window = nullptr;
		}

		CoUninitialize();
	}
}
