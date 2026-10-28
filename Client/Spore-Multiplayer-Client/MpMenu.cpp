/****************************************************************************
* Spore Multiplayer
****************************************************************************/
#include "stdafx.h"
#include "MpMenu.h"
#include "MpDiscovery.h"
#include "MpHost.h"
#include "MpLog.h"
#include "MpMaster.h"
#include "MpSession.h"

#include <Spore\UTFWin\Window.h>
#include <Spore\UTFWin\UIRenderer.h>
#include <Spore\UTFWin\IWindowManager.h>
#include <Spore\UTFWin\IButton.h>
#include <Spore\UTFWin\ButtonDrawableStandard.h>
#include <Spore\UTFWin\Constants.h>

namespace SporeMP
{
	namespace
	{
		// Panel geometry.
		const float kPanelWidth = 460.0f;
		const float kPadding = 16.0f;
		const float kTitleHeight = 26.0f;
		const float kLineHeight = 18.0f;
		const float kButtonHeight = 34.0f;
		const float kButtonSpacing = 6.0f;
		const int kMaxServerButtons = 5;

		const Math::Color kBackground(18, 24, 21, 235);
		const Math::Color kBorder(126, 226, 168, 255);
		const Math::Color kText(232, 240, 234, 255);
		const Math::Color kButtonFill(230, 230, 153, 255);

		// Control IDs, used to know which button was clicked.
		const uint32_t kHostButtonID = id("mpMenuHost");
		const uint32_t kRefreshButtonID = id("mpMenuRefresh");
		const uint32_t kCloseButtonID = id("mpMenuClose");

		uint32_t JoinButtonID(int index)
		{
			char name[32];
			std::snprintf(name, sizeof(name), "mpMenuJoin%d", index);
			return id(name);
		}

		eastl::string16 ToText(const std::string& text)
		{
			return ConvertToString16(text.c_str());
		}

		eastl::string16 ToText(const char* text)
		{
			return ConvertToString16(text);
		}

		/// Routes the button clicks to the menu.
		bool HandleMenuMessage(UTFWin::IWindow* pWindow, const UTFWin::Message& message)
		{
			if (!message.IsType(UTFWin::kMsgButtonClick)) return false;
			if (pWindow == nullptr) return false;

			const uint32_t controlID = pWindow->GetControlID();
			Menu& menu = Menu::Get();

			if (controlID == kHostButtonID)
			{
				menu.OnHostClicked();
				return true;
			}

			if (controlID == kRefreshButtonID)
			{
				menu.OnRefreshClicked();
				return true;
			}

			if (controlID == kCloseButtonID)
			{
				menu.SetVisible(false);
				return true;
			}

			for (int i = 0; i < kMaxServerButtons; ++i)
			{
				if (controlID == JoinButtonID(i))
				{
					menu.OnJoinClicked(i);
					return true;
				}
			}

			return false;
		}
	}

	// ------------------------------------------------------------------------
	// MenuPanelDrawable
	// ------------------------------------------------------------------------

	void* MenuPanelDrawable::Cast(uint32_t type) const
	{
		if (type == TYPE) return const_cast<MenuPanelDrawable*>(this);
		return DefaultDrawable::Cast(type);
	}

	void MenuPanelDrawable::SetContent(const std::vector<eastl::string16>& lines, Math::Color background,
		Math::Color border, Math::Color textColor, float lineHeight)
	{
		mLines = lines;
		mBackground = background;
		mBorder = border;
		mTextColor = textColor;
		mLineHeight = lineHeight;
	}

	void MenuPanelDrawable::Paint(UTFWin::UIRenderer* pRenderer, const Math::Rectangle& area, const UTFWin::RenderParams& params)
	{
		if (pRenderer == nullptr) return;

		UTFWin::Graphics2D& graphics = pRenderer->GetGraphics2D();

		graphics.SetColor(mBackground);
		graphics.FillRectangle(area.x1, area.y1, area.x2, area.y2);

		graphics.SetColor(mBorder);
		graphics.FillBorder(area.x1, area.y1, area.x2, area.y2, 2.0f);

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
	// Menu
	// ------------------------------------------------------------------------

	Menu::Menu()
	{
	}

	Menu& Menu::Get()
	{
		static Menu instance;
		return instance;
	}

	void Menu::Initialize()
	{
		if (mInitialized) return;

		mInitialized = true;
		mStatus = "Нажмите \"Захостить\" или обновите список";
	}

	void Menu::Dispose()
	{
		Destroy();
		mInitialized = false;
	}

	void Menu::Toggle()
	{
		SetVisible(!mVisible);
	}

	void Menu::SetVisible(bool visible)
	{
		if (visible == mVisible) return;

		mVisible = visible;

		if (visible)
		{
			Create();
			Refresh();
		}
		else
		{
			Destroy();
		}
	}

	void Menu::Create()
	{
		if (mpPanel != nullptr) return;

		auto* mainWindow = WindowManager.GetMainWindow();
		if (mainWindow == nullptr) return;

		const Math::Rectangle& screen = mainWindow->GetRealArea();

		// Panel height: header lines plus the action buttons; the server buttons
		// only take space when they are actually shown.
		const float panelHeight = kPadding * 2 + kLineHeight * 4 + 12 +
			(kButtonHeight + kButtonSpacing) * (kMaxServerButtons + 1);

		const float x = screen.x1 + (screen.GetWidth() - kPanelWidth) * 0.5f;
		const float y = screen.y1 + (screen.GetHeight() - panelHeight) * 0.5f;

		mpDrawable = new MenuPanelDrawable();
		mpDrawable->SetContent({}, kBackground, kBorder, kText, kLineHeight);

		auto* panel = new UTFWin::Window();
		panel->SetControlID(id("SporeMultiplayerMenu"));
		panel->SetDrawable(mpDrawable.get());
		panel->SetFlag(UTFWin::kWinFlagVisible, true);
		panel->SetFlag(UTFWin::kWinFlagAlwaysInFront, true);
		panel->SetArea(Math::Rectangle(x, y, x + kPanelWidth, y + panelHeight));

		mainWindow->AddWindow(panel);
		mpPanel = panel;

		// Buttons are laid out from the bottom of the panel upwards. Coordinates are
		// relative to the panel, because child windows are positioned inside their
		// parent.
		float buttonY = panelHeight - kPadding - kButtonHeight;

		const float buttonWidth = (kPanelWidth - kPadding * 2 - kButtonSpacing * 2) / 3.0f;

		AddButton(u"Захостить", kHostButtonID,
			kPadding, buttonY, buttonWidth, kButtonHeight);
		AddButton(u"Обновить", kRefreshButtonID,
			kPadding + buttonWidth + kButtonSpacing, buttonY, buttonWidth, kButtonHeight);
		AddButton(u"Закрыть", kCloseButtonID,
			kPadding + (buttonWidth + kButtonSpacing) * 2, buttonY, buttonWidth, kButtonHeight);

		buttonY -= kButtonHeight + kButtonSpacing;

		// Server buttons, from the bottom up so the first server is on top.
		for (int i = kMaxServerButtons - 1; i >= 0; --i)
		{
			char label[64];
			std::snprintf(label, sizeof(label), "Сервер %d", i + 1);

			// Hidden until a server occupies the slot (see Refresh()).
			UTFWin::IWindow* button = AddButton(ToText(label).c_str(), JoinButtonID(i),
				kPadding, buttonY, kPanelWidth - kPadding * 2, kButtonHeight);

			if (button != nullptr)
			{
				button->SetFlag(UTFWin::kWinFlagVisible, false);
			}

			buttonY -= kButtonHeight + kButtonSpacing;
		}

		MP_LOG_INFO("In-game menu created");
	}

	UTFWin::IWindow* Menu::AddButton(const char16_t* caption, uint32_t controlID,
		float x, float y, float width, float height)
	{
		if (mpPanel == nullptr) return nullptr;

		// The class manager creates the same button class the game uses, so the
		// native drawable, hover and click behaviour come for free.
		auto* window = (UTFWin::IWindow*)ClassManager.Create(UTFWin::IButton::WinButton_ID);
		if (window == nullptr)
		{
			MP_LOG_WARN("Could not create a WinButton");
			return nullptr;
		}

		window->SetControlID(controlID);
		window->SetCaption(caption);
		window->SetArea(Math::Rectangle(x, y, x + width, y + height));
		window->SetFlag(UTFWin::kWinFlagVisible, true);
		window->SetFlag(UTFWin::kWinFlagEnabled, true);
		window->SetFillColor(kButtonFill);
		window->SetDrawable(new UTFWin::ButtonDrawableStandard());
		window->SetTextFontID(0x00aebb69);

		auto* button = object_cast<UTFWin::IButton>(window);
		if (button != nullptr)
		{
			for (uint32_t i = 0; i < 8; ++i)
			{
				button->SetCaptionColor(static_cast<UTFWin::StateIndices>(i), Math::Color::BLACK);
			}
		}

		window->AddWinProc(HandleMenuMessage, UTFWin::kEventFlagBasicInput | UTFWin::kEventFlagAdvanced);

		mpPanel->AddWindow(window);
		mButtons.push_back(window);

		return window;
	}

	void Menu::Destroy()
	{
		for (auto& button : mButtons)
		{
			if (button != nullptr && button->GetParent() != nullptr)
			{
				button->GetParent()->RemoveWindow(button.get());
			}
		}
		mButtons.clear();

		if (mpPanel != nullptr)
		{
			auto* parent = mpPanel->GetParent();
			if (parent != nullptr)
			{
				parent->DisposeWindowFamily(mpPanel.get());
			}
			mpPanel = nullptr;
		}

		mpDrawable = nullptr;
	}

	void Menu::Update(float deltaTime)
	{
		if (!mInitialized) return;

		// The menu is rebuilt only when it is open, so nothing runs while playing.
		if (!mVisible) return;

		static float timer = 0.0f;
		timer += deltaTime;

		// Refresh the list every couple of seconds while the menu is open.
		if (mDirty || timer >= 2.0f)
		{
			timer = 0.0f;
			mDirty = false;
			Refresh();
		}
	}

	void Menu::Refresh()
	{
		if (mpPanel == nullptr || mpDrawable == nullptr) return;

		Session& session = Session::Get();

		mLocalServers = Discovery::Get().GetSessions();

		std::vector<eastl::string16> lines;

		lines.push_back(ToText("SPORE MULTIPLAYER"));

		eastl::string16 statusLine = ToText("Состояние: ");
		statusLine += ToText(session.GetStatusText());
		if (session.IsConnected())
		{
			statusLine += ToText("   Игроков: ");
			statusLine += ToText(std::to_string(session.GetPlayers().size() + 1).c_str());
		}
		lines.push_back(statusLine);

		lines.push_back(ToText(mStatus));

		// Server buttons: label them with the real session names.
		int shown = 0;
		for (size_t i = 0; i < mLocalServers.size() && shown < kMaxServerButtons; ++i, ++shown)
		{
			if (shown >= static_cast<int>(mButtons.size())) break;

			// mButtons layout: 3 action buttons first, then the server buttons.
			const size_t buttonIndex = 3 + (kMaxServerButtons - 1 - shown);
			if (buttonIndex >= mButtons.size()) continue;

			const DiscoveredSession& discovered = mLocalServers[i];
			eastl::string16 caption = ToText(discovered.name);
			caption += ToText("  ");
			caption += ToText(std::to_string(discovered.players).c_str());
			caption += ToText("/");
			caption += ToText(std::to_string(discovered.maxPlayers).c_str());

			mButtons[buttonIndex]->SetCaption(caption.c_str());
			mButtons[buttonIndex]->SetFlag(UTFWin::kWinFlagVisible, true);
		}

		// Hide the buttons that have no server behind them.
		for (int i = shown; i < kMaxServerButtons; ++i)
		{
			const size_t buttonIndex = 3 + (kMaxServerButtons - 1 - i);
			if (buttonIndex >= mButtons.size()) continue;
			mButtons[buttonIndex]->SetFlag(UTFWin::kWinFlagVisible, false);
		}

		lines.push_back(ToText(""));
		lines.push_back(ToText("Серверы в локальной сети:"));

		mpDrawable->SetContent(lines, kBackground, kBorder, kText, kLineHeight);
		mpPanel->Invalidate();
	}

	void Menu::OnHostClicked()
	{
		Session& session = Session::Get();

		if (session.IsConnected())
		{
			mStatus = "Уже подключены, сначала отключитесь";
			mDirty = true;
			return;
		}

		Host& host = Host::Get();

		std::string error;
		if (!host.StartServer(session.Config().port, session.Config().sessionName,
			session.Config().masterUrl, session.Config().relayUrl, error))
		{
			mStatus = "Не удалось захостить: " + error;
			mDirty = true;
			return;
		}

		mStatus = "Сервер запущен, подключаюсь...";
		session.ConnectDelayed("127.0.0.1", session.Config().port, 1.5f);
		mDirty = true;
	}

	void Menu::OnRefreshClicked()
	{
		Session& session = Session::Get();

		mStatus = "Обновление списка...";

		// Fetch the public list on demand: the menu is open, so a short pause is
		// acceptable and the result is shown immediately.
		std::vector<PublicSession> sessions;
		std::string error;

		if (!session.Config().masterUrl.empty() &&
			MasterQuery::Fetch(session.Config().masterUrl, sessions, error))
		{
			mPublicServers = sessions;
			mStatus = "Публичных серверов: " + std::to_string(sessions.size());
		}
		else
		{
			mPublicServers.clear();
			mStatus = "Мастер недоступен: " + error;
		}

		mDirty = true;
	}

	void Menu::OnJoinClicked(int index)
	{
		Session& session = Session::Get();

		if (index < 0 || index >= static_cast<int>(mLocalServers.size()))
		{
			mStatus = "Нет сервера с таким номером";
			mDirty = true;
			return;
		}

		const DiscoveredSession& discovered = mLocalServers[index];

		mStatus = "Подключение к " + discovered.name + "...";
		mDirty = true;

		session.Connect(discovered.address, discovered.port);
		SetVisible(false);
	}
}
