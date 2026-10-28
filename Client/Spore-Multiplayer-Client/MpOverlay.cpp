/****************************************************************************
* Spore Multiplayer
****************************************************************************/
#include "stdafx.h"
#include "MpOverlay.h"
#include "MpAvatars.h"
#include "MpDiscovery.h"
#include "MpLog.h"
#include "MpSession.h"

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
		const Math::Color kLabelColor(255, 240, 180, 255);
		const Math::Color kLabelShadow(0, 0, 0, 170);

		/// Width of the HUD block, in fraction of the screen.
		const float kWidthFraction = 0.34f;

		/// Rough width of one character, used to center the name tags.
		const float kCharWidth = 7.0f;

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

	void OverlayDrawable::SetContent(const std::vector<eastl::string16>& lines, const Math::Rectangle& hudArea,
		float lineHeight, Math::Color textColor, Math::Color backgroundColor,
		const std::vector<ScreenLabel>& labels, Math::Color labelColor)
	{
		mLines = lines;
		mHudArea = hudArea;
		mLineHeight = lineHeight;
		mTextColor = textColor;
		mBackgroundColor = backgroundColor;
		mLabels = labels;
		mLabelColor = labelColor;
	}

	void OverlayDrawable::Paint(UTFWin::UIRenderer* pRenderer, const Math::Rectangle& area, const UTFWin::RenderParams& params)
	{
		if (pRenderer == nullptr) return;

		UTFWin::Graphics2D& graphics = pRenderer->GetGraphics2D();

		// --- HUD block -------------------------------------------------------
		if (!mLines.empty())
		{
			graphics.SetColor(mBackgroundColor);
			graphics.FillRectangle(mHudArea.x1, mHudArea.y1, mHudArea.x2, mHudArea.y2);

			float y = mHudArea.y1 + kPadding;
			for (const eastl::string16& line : mLines)
			{
				if (!line.empty())
				{
					Math::Rectangle lineArea(mHudArea.x1 + kPadding, y, mHudArea.x2 - kPadding, y + mLineHeight);
					graphics.SetColor(mTextColor);
					graphics.DrawStringArea(lineArea, line.c_str(), UTFWin::Graphics2D::kAutoLength);
				}
				y += mLineHeight;
			}
		}

		// --- Name tags -------------------------------------------------------
		for (const ScreenLabel& label : mLabels)
		{
			if (label.text.empty()) continue;

			const float width = kCharWidth * static_cast<float>(label.text.size());
			const float x = label.x - width * 0.5f;
			const float y = label.y;

			Math::Rectangle shadowArea(x + 1.0f, y + 1.0f, x + width + 1.0f, y + kLineHeight + 1.0f);
			graphics.SetColor(kLabelShadow);
			graphics.DrawStringArea(shadowArea, label.text.c_str(), UTFWin::Graphics2D::kAutoLength);

			Math::Rectangle textArea(x, y, x + width, y + kLineHeight);
			graphics.SetColor(mLabelColor);
			graphics.DrawStringArea(textArea, label.text.c_str(), UTFWin::Graphics2D::kAutoLength);
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
		if (mpHudWindow != nullptr) return;

		auto* mainWindow = WindowManager.GetMainWindow();
		if (mainWindow == nullptr)
		{
			MP_LOG_WARN("The main window is not available, the overlay will not be created");
			return;
		}

		const Math::Rectangle& screen = mainWindow->GetRealArea();

		// The HUD block gets its own window: the game clips a window drawing to its
		// area, so the window must match the block exactly.
		mpHudDrawable = new OverlayDrawable();
		mpHudDrawable->SetContent({}, Math::Rectangle(), kLineHeight, kTextColor, kBackgroundColor, {}, kLabelColor);

		auto* hudWindow = new UTFWin::Window();
		hudWindow->SetControlID(id("SporeMultiplayerHud"));
		hudWindow->SetDrawable(mpHudDrawable.get());
		hudWindow->SetFlag(UTFWin::kWinFlagVisible, mVisible);
		hudWindow->SetFlag(UTFWin::kWinFlagIgnoreMouse, true);
		hudWindow->SetFlag(UTFWin::kWinFlagAlwaysInFront, true);
		hudWindow->SetArea(Math::Rectangle(screen.x1 + 10.0f, screen.y1 + 10.0f,
			screen.x1 + 310.0f, screen.y1 + 200.0f));

		mainWindow->AddWindow(hudWindow);
		mpHudWindow = hudWindow;

		// The name tags need to be drawn anywhere on the screen, so they get a
		// transparent full screen window.
		mpTagsDrawable = new OverlayDrawable();
		mpTagsDrawable->SetContent({}, Math::Rectangle(), kLineHeight, kTextColor, kBackgroundColor, {}, kLabelColor);

		auto* tagsWindow = new UTFWin::Window();
		tagsWindow->SetControlID(id("SporeMultiplayerTags"));
		tagsWindow->SetDrawable(mpTagsDrawable.get());
		tagsWindow->SetFlag(UTFWin::kWinFlagVisible, false);
		tagsWindow->SetFlag(UTFWin::kWinFlagIgnoreMouse, true);
		tagsWindow->SetFlag(UTFWin::kWinFlagAlwaysInFront, true);
		tagsWindow->SetArea(Math::Rectangle(screen.x1, screen.y1, screen.x2, screen.y2));

		mainWindow->AddWindow(tagsWindow);
		mpTagsWindow = tagsWindow;

		mDirty = true;
	}

	void Overlay::DestroyOverlayWindow()
	{
		auto disposeWindow = [](IWindowPtr& window)
		{
			if (window == nullptr) return;

			auto* parent = window->GetParent();
			if (parent != nullptr)
			{
				parent->DisposeWindowFamily(window.get());
			}
			window = nullptr;
		};

		disposeWindow(mpTagsWindow);
		disposeWindow(mpHudWindow);

		mpTagsDrawable = nullptr;
		mpHudDrawable = nullptr;
	}

	void Overlay::SetVisible(bool visible)
	{
		mVisible = visible;

		if (mpHudWindow != nullptr)
		{
			mpHudWindow->SetFlag(UTFWin::kWinFlagVisible, visible);
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
		if (mDirty || mRefreshTimer >= 0.1f)
		{
			mRefreshTimer = 0.0f;
			mDirty = false;
			Refresh();
		}
	}

	void Overlay::Refresh()
	{
		if (mpHudWindow == nullptr || mpHudDrawable == nullptr) return;

		Session& session = Session::Get();

		auto* mainWindow = WindowManager.GetMainWindow();
		if (mainWindow == nullptr) return;

		const Math::Rectangle& screen = mainWindow->GetRealArea();

		// --- HUD lines -------------------------------------------------------
		std::vector<eastl::string16> lines;

		if (mVisible)
		{
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
			else
			{
				// Offline: show what can be joined, so the player does not have to
				// type an address.
				const auto& sessions = Discovery::Get().GetSessions();
				if (!sessions.empty())
				{
					lines.push_back(ToText("Sessions found (mp_join <n>):"));

					for (size_t i = 0; i < sessions.size() && i < 5; ++i)
					{
						const DiscoveredSession& discovered = sessions[i];

						eastl::string16 line = ToText("  ");
						line += ToText(std::to_string(i + 1).c_str());
						line += ToText(") ");
						line += ToText(discovered.name);
						line += ToText("  ");
						line += ToText(std::to_string(discovered.players).c_str());
						line += ToText("/");
						line += ToText(std::to_string(discovered.maxPlayers).c_str());
						lines.push_back(line);
					}
				}
				else
				{
					lines.push_back(ToText("mp_host  - start a session"));
					lines.push_back(ToText("mp_servers - find one"));
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
		}

		// The HUD window is resized to the block, because the game clips the
		// drawing to the window area.
		const float width = screen.GetWidth() * kWidthFraction * session.Config().hudScale;
		const float hudX = screen.x1 + screen.GetWidth() * session.Config().hudX;
		const float hudY = screen.y1 + screen.GetHeight() * session.Config().hudY;
		const float hudHeight = kPadding * 2.0f + kLineHeight * static_cast<float>(lines.size());

		Math::Rectangle hudArea(hudX, hudY, hudX + width, hudY + hudHeight);

		mpHudWindow->SetArea(hudArea);
		mpHudWindow->SetFlag(UTFWin::kWinFlagVisible, mVisible);
		mpHudDrawable->SetContent(lines, hudArea, kLineHeight, kTextColor, kBackgroundColor, {}, kLabelColor);
		mpHudWindow->Invalidate();

		// --- Name tags -------------------------------------------------------
		if (mpTagsWindow == nullptr || mpTagsDrawable == nullptr) return;

		std::vector<ScreenLabel> labels;

		if (mVisible && session.IsConnected() && session.Config().showAvatars && session.Config().nameTags)
		{
			for (const RemotePlayer& player : session.GetPlayers())
			{
				if (!player.hasState) continue;

				Math::Point position;
				if (!Avatars::Get().GetScreenPosition(player.id, position)) continue;

				if (position.x < screen.x1 || position.x > screen.x2) continue;
				if (position.y < screen.y1 || position.y > screen.y2) continue;

				ScreenLabel label;
				label.x = position.x;
				label.y = position.y - 28.0f;
				label.text = ToText(player.name);
				labels.push_back(label);
			}
		}

		mpTagsWindow->SetArea(Math::Rectangle(screen.x1, screen.y1, screen.x2, screen.y2));
		mpTagsWindow->SetFlag(UTFWin::kWinFlagVisible, !labels.empty());
		mpTagsDrawable->SetContent({}, Math::Rectangle(), kLineHeight, kTextColor, kBackgroundColor, labels, kLabelColor);
		mpTagsWindow->Invalidate();
	}
}
