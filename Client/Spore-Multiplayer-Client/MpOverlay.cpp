/****************************************************************************
* Spore Multiplayer
****************************************************************************/
#include "stdafx.h"
#include "MpOverlay.h"
#include "MpAvatars.h"
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
		const Math::Color kLabelShadow(0, 0, 0, 160);

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

			graphics.SetColor(mTextColor);

			float y = mHudArea.y1 + kPadding;
			for (const eastl::string16& line : mLines)
			{
				if (!line.empty())
				{
					Math::Rectangle lineArea(mHudArea.x1 + kPadding, y, mHudArea.x2 - kPadding, y + mLineHeight);
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
		if (mpWindow != nullptr) return;

		auto* mainWindow = WindowManager.GetMainWindow();
		if (mainWindow == nullptr)
		{
			MP_LOG_WARN("The main window is not available, the overlay will not be created");
			return;
		}

		mpDrawable = new OverlayDrawable();
		mpDrawable->SetContent({}, Math::Rectangle(), kLineHeight, kTextColor, kBackgroundColor, {}, kLabelColor);

		// The window covers the whole screen so the name tags can be drawn at
		// absolute coordinates. It is transparent and ignores the mouse.
		auto* window = new UTFWin::Window();
		window->SetControlID(id("SporeMultiplayerOverlay"));
		window->SetDrawable(mpDrawable.get());
		window->SetFlag(UTFWin::kWinFlagVisible, mVisible);
		window->SetFlag(UTFWin::kWinFlagIgnoreMouse, true);
		window->SetFlag(UTFWin::kWinFlagAlwaysInFront, true);

		const Math::Rectangle& screen = mainWindow->GetRealArea();
		window->SetArea(Math::Rectangle(screen.x1, screen.y1, screen.x2, screen.y2));

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
		if (mDirty || mRefreshTimer >= 0.1f)
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

		auto* mainWindow = WindowManager.GetMainWindow();
		if (mainWindow == nullptr) return;

		const Math::Rectangle& screen = mainWindow->GetRealArea();
		mpWindow->SetArea(Math::Rectangle(screen.x1, screen.y1, screen.x2, screen.y2));
		mpWindow->SetFlag(UTFWin::kWinFlagVisible, mVisible);

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

		// --- HUD area --------------------------------------------------------
		const float width = screen.GetWidth() * kWidthFraction * session.Config().hudScale;
		const float hudX = screen.x1 + screen.GetWidth() * session.Config().hudX;
		const float hudY = screen.y1 + screen.GetHeight() * session.Config().hudY;
		const float hudHeight = kPadding * 2.0f + kLineHeight * static_cast<float>(lines.size());

		Math::Rectangle hudArea(hudX, hudY, hudX + width, hudY + hudHeight);

		// --- Name tags -------------------------------------------------------
		std::vector<ScreenLabel> labels;

		if (mVisible && session.IsConnected() && session.Config().showAvatars && session.Config().nameTags)
		{
			for (const RemotePlayer& player : session.GetPlayers())
			{
				if (!player.hasState) continue;

				Math::Point position;
				if (!Avatars::Get().GetScreenPosition(player.id, position)) continue;

				// One time diagnostic, so the log shows whether the projection works.
				static int diagnosticCount = 0;
				if (diagnosticCount < 5)
				{
					++diagnosticCount;
					MP_LOG_DEBUG("Screen position of player %u: %.1f, %.1f (screen %.0fx%.0f)",
						player.id, position.x, position.y, screen.GetWidth(), screen.GetHeight());
				}

				// Ignore the points that are not on screen: this also filters out
				// the avatars that are behind the camera.
				if (position.x < screen.x1 || position.x > screen.x2) continue;
				if (position.y < screen.y1 || position.y > screen.y2) continue;

				ScreenLabel label;
				label.x = position.x;
				label.y = position.y - 28.0f;
				label.text = ToText(player.name);
				labels.push_back(label);
			}
		}

		mpDrawable->SetContent(lines, hudArea, kLineHeight, kTextColor, kBackgroundColor, labels, kLabelColor);

		mpWindow->Invalidate();
	}
}
