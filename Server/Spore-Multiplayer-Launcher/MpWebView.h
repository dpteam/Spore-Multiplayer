/****************************************************************************
* Spore Multiplayer - Launcher window
*
* Hosts the launcher UI in a WebView2 control, which is what modern Windows
* apps use (and what the DPTDOOM launcher does): the UI is plain HTML, but it
* looks and feels like a native window.
*
* The local HTTP server is still there and is used for two things:
*   - it is the origin of the page (WebView2 navigates to http://127.0.0.1:port)
*   - if WebView2 is not installed, the default browser opens that same URL
*
* So there is exactly one UI to maintain, and a graceful fallback for machines
* without the WebView2 runtime.
****************************************************************************/
#pragma once

#include <string>

namespace SporeMP
{
	/// Result of trying to create the WebView2 window.
	enum class WebViewStatus
	{
		/// The window was created and the page is loading.
		Ok,
		/// The WebView2 runtime is not installed.
		RuntimeMissing,
		/// Something else failed; the message explains what.
		Failed,
	};

	/// Creates the launcher window and navigates it to `url`.
	/// @param error Filled with a message when the result is not Ok.
	WebViewStatus ShowWebViewWindow(const std::string& url, std::string& error);

	/// Runs the window message loop until the window is closed.
	/// Must be called after ShowWebViewWindow returned Ok.
	void RunWebViewLoop();

	/// Releases the WebView2 objects.
	void ShutdownWebViewWindow();
}
