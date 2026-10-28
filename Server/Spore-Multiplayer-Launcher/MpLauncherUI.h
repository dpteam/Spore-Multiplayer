/****************************************************************************
* Spore Multiplayer - Launcher UI
*
* The launcher is a tiny HTTP server that serves this single page and exposes a
* small JSON API. The page is opened in the default browser, which means:
*
*   - no WebView2 runtime to install
*   - no embedded browser to keep up to date
*   - the same HTML can be served on a web site later
*
* The page only talks to the local launcher, which proxies the master server,
* so no cross-origin requests are needed and the master stays private.
****************************************************************************/
#pragma once

namespace SporeMP
{
	/// Returns the HTML of the launcher page.
	const char* GetLauncherHtml();
}
