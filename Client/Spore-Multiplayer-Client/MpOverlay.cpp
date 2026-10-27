/****************************************************************************
* Spore Multiplayer
****************************************************************************/
#include "stdafx.h"
#include "MpOverlay.h"
#include "MpSession.h"
#include "MpLog.h"

#include <Spore\UTFWin\Window.h>
#include <Spore\UTFWin\UIRenderer.h>
#include <Spore\UTFWin\IWindowManager.h>

namespace SporeMP
{
	namespace
	{
		const float kLineHeight = 17.0f;
		const float kPadding = 7.0f;
		const int kMaxPlayersShown = 8;

		const Math::Color kTextColor(235, 235, 235, 255);
		const Math::Color kBackgroundColor(0, 0, 0, 130);
		const Math::Color kAccentColor(120, 220, 255, 255);

		/// Width of the overlay, in fraction of the screen.
		const float kWidthFraction = 0.34f;

		eastl::string16 ToText(const std::string& text)
		{
			return ConvertToString16(text.c_str());
		}

		eastl::string16 ToText(const char* text)
		{
			return ConvertToString16(text);
		}
	}

	// ------------------------------------------------------------------------
	// OverlayDrawable
	// ------------------------------------------------------------------------

	void* OverlayDrawable::Cast(uint32_t type) const
	{
		if (type == TYPE) return const_cast<OverlayDrawable*>(this);
		return DefaultDrawable::Cast(type);
	}

	void OverlayDrawable::SetContent(const std::vector<eastl::string16>& lines, float lineHeight,
		Math::Color textColor, Math::Color backgroundColor)
	{
		mLines = lines;
		mLineHeight = lineHeight;
		mTextColor = textColor;
		mBackgroundColor = backgroundColor;
	}

	void OverlayDrawable::Paint(UTFWin::UIRenderer* pRenderer, const Math::Rectangle& area, const UTFWin::RenderParams& params)
	{
		if (pRenderer == nullptr) return;
		if (mLines.empty()) return;

		UTFWin::Graphics2D& graphics = pRenderer->GetGraphics2D();

		// Background
		graphics.SetColor(mBackgroundColor);
		graphics.FillRectangle(area.x1, area.y1, area.x2, area.y2);

		// Text
		graphics.SetColor(mTextColor);

		float y = area.y1 + kPadding;
		for (const eastl::string16& line : mLines)
		{
			if (!line.empty())
			{
				Math::Rectangle lineArea(area.x1 + kPadding, y, area.x2 - kPadding, y + mLineHeight);
				graphics.DrawStringArea(lineArea, line.c_str(), UTFWin::Graphics2D::kAutoLength);
			}
			y += mLineHeight;
		}
	}

	// ------------------------------------------------------------------------
	// Overlay
	// ------------------------------------------------------------------------

	Overlay::Overlay()
	{
	}

	Overlay& Overlay::Get()
	{
		static Overlay instance;
		return instance;
	}

	void Overlay::Initialize()
	{
		if (mInitialized) return;

		Session& session = Session::Get();
		mVisible = session.Config().hud;

		CreateOverlayWindow();
		mInitialized = true;

		MP_LOG_INFO("Overlay initialized (visible: %s)", mVisible ? "yes" : "no");
	}

	void Overlay::Dispose()
	{
		DestroyOverlayWindow();
		mInitialized = false;
	}

	void Overlay::CreateOverlayWindow()
	{
		if (mpWindow != nullptr) return;

		auto* mainWindow = WindowManager.GetMainWindow();
		if (mainWindow == nullptr)
		{
			MP_LOG_WARN("The main window is not available, the overlay will not be created");
			return;
		}

		mpDrawable = new OverlayDrawable();
		mpDrawable->SetContent({}, kLineHeight, kTextColor, kBackgroundColor);

		auto* window = new UTFWin::Window();
		window->SetControlID(id("SporeMultiplayerOverlay"));
		window->SetDrawable(mpDrawable.get());
		window->SetFlag(UTFWin::kWinFlagVisible, mVisible);
		window->SetFlag(UTFWin::kWinFlagIgnoreMouse, true);
		window->SetFlag(UTFWin::kWinFlagClip, true);

		mainWindow->AddWindow(window);
		mpWindow = window;

		mDirty = true;
	}

	void Overlay::DestroyOverlayWindow()
	{
		if (mpWindow != nullptr)
		{
			auto* parent = mpWindow->GetParent();
			if (parent != nullptr)
			{
				parent->DisposeWindowFamily(mpWindow.get());
			}
			mpWindow = nullptr;
		}

		mpDrawable = nullptr;
	}

	void Overlay::SetVisible(bool visible)
	{
		mVisible = visible;

		if (mpWindow != nullptr)
		{
			mpWindow->SetFlag(UTFWin::kWinFlagVisible, visible);
		}

		mDirty = true;
	}

	void Overlay::Update(float deltaTime)
	{
		if (!mInitialized) return;

		Session& session = Session::Get();
		if (session.Config().hud != mVisible && !mVisible)
		{
			// The configuration was changed from the console.
			SetVisible(session.Config().hud);
		}

		// The text only needs to be rebuilt a few times per second; the chat is
		// not that fast and repainting every frame wastes CPU.
		mRefreshTimer += deltaTime;
		if (mDirty || mRefreshTimer >= 0.25f)
		{
			mRefreshTimer = 0.0f;
			mDirty = false;
			Refresh();
		}
	}

	void Overlay::Refresh()
	{
		if (mpWindow == nullptr || mpDrawable == nullptr) return;

		Session& session = Session::Get();
		if (!mVisible)
		{
			mpWindow->SetFlag(UTFWin::kWinFlagVisible, false);
			return;
		}

		mpWindow->SetFlag(UTFWin::kWinFlagVisible, true);

		// Rebuild the lines.
		std::vector<eastl::string16> lines;

		const std::string status = session.GetStatusText();
		eastl::string16 title = ToText("SPORE MULTIPLAYER  -  ");
		title += ToText(status);
		lines.push_back(title);

		if (session.IsConnected())
		{
			eastl::string16 sessionLine = ToText("Session: ");
			sessionLine += ToText(session.GetSessionName());
			sessionLine += ToText("   Ping: ");
			sessionLine += ToText(std::to_string(session.GetLatency()).c_str());
			sessionLine += ToText(" ms");
			lines.push_back(sessionLine);

			eastl::string16 selfLine = ToText("You: ");
			selfLine += ToText(session.GetLocalName());
			lines.push_back(selfLine);
			const auto& players = session.GetPlayers();
			eastl::string16 playersLine = ToText("Players (");
			playersLine += ToText(std::to_string(players.size() + 1).c_str());
			playersLine += ToText("):");
			lines.push_back(playersLine);

			int shown = 0;
			for (const RemotePlayer& player : players)
			{
				if (shown >= kMaxPlayersShown) break;

				eastl::string16 line = ToText("  ");
				line += ToText(player.name);
				line += ToText("  [");
				line += ToText(StageName(player.stage));
				line += ToText("]");
				lines.push_back(line);
				++shown;
			}
		}

		// Chat log
		const auto& chat = session.GetChatLog();
		const int maxLines = session.GetChatLineCount();
		if (maxLines > 0 && !chat.empty())
		{
			lines.push_back(ToText(""));

			int start = static_cast<int>(chat.size()) - maxLines;
			if (start < 0) start = 0;

			for (int i = start; i < static_cast<int>(chat.size()); ++i)
			{
				const ChatLine& entry = chat[i];

				eastl::string16 line;
				if (!entry.system)
				{
					line += ToText("<");
					line += ToText(entry.sender);
					line += ToText("> ");
				}
				line += ToText(entry.text);
				lines.push_back(line);
			}
		}

		mpDrawable->SetContent(lines, kLineHeight, kTextColor, kBackgroundColor);

		// Place the window using the configuration fractions, so the overlay
		// adapts to any resolution.
		auto* mainWindow = WindowManager.GetMainWindow();
		if (mainWindow != nullptr)
		{
			const Math::Rectangle& screen = mainWindow->GetRealArea();
			const float width = screen.GetWidth() * kWidthFraction * session.Config().hudScale;

			const float x = screen.x1 + screen.GetWidth() * session.Config().hudX;
			const float y = screen.y1 + screen.GetHeight() * session.Config().hudY;
			const float height = kPadding * 2.0f + kLineHeight * static_cast<float>(lines.size());

			mpWindow->SetArea(Math::Rectangle(x, y, x + width, y + height));
		}

		mpWindow->Invalidate();
	}
}
