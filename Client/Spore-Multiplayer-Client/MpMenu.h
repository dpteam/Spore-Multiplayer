/****************************************************************************
* Spore Multiplayer
*
* A real in-game menu, built with the game's own UI toolkit (UTFWin).
*
* This replaces the console-only flow with something that looks like part of
* Spore: a panel with buttons for hosting, refreshing the list and joining a
* session, drawn over the game with the same font and input handling.
*
* The buttons are created through the class manager (IButton::WinButton_ID),
* which is how the game itself makes buttons, so they get the native look,
* hover and click behaviour for free.
*
* Toggled with the `mp_menu` cheat.
****************************************************************************/
#pragma once

#include <Spore\BasicIncludes.h>

#include <string>
#include <vector>

#include "MpDiscovery.h"
#include "MpMaster.h"

namespace SporeMP
{
	/// Draws the panel background, the title and the status lines.
	class MenuPanelDrawable : public UTFWin::DefaultDrawable
	{
	public:
		static const uint32_t TYPE = 0x7C21B4A9;

		void SetContent(const std::vector<eastl::string16>& lines, Math::Color background,
			Math::Color border, Math::Color textColor, float lineHeight);

		void* Cast(uint32_t type) const override;
		void Paint(UTFWin::UIRenderer* pRenderer, const Math::Rectangle& area, const UTFWin::RenderParams& params) override;

	private:
		std::vector<eastl::string16> mLines;
		Math::Color mBackground;
		Math::Color mBorder;
		Math::Color mTextColor;
		float mLineHeight = 18.0f;
	};

	class Menu
	{
	public:
		static Menu& Get();

		void Initialize();
		void Update(float deltaTime);
		void Dispose();

		void Toggle();
		void SetVisible(bool visible);
		bool IsVisible() const { return mVisible; }

		/// Called by the buttons.
		void OnHostClicked();
		void OnRefreshClicked();
		void OnJoinClicked(int index);

	private:
		Menu();

		void Create();
		void Destroy();
		void Refresh();

		/// Creates a native button inside the panel.
		UTFWin::IWindow* AddButton(const char16_t* caption, uint32_t controlID,
			float x, float y, float width, float height);

		IWindowPtr mpPanel;
		eastl::intrusive_ptr<MenuPanelDrawable> mpDrawable;
		eastl::vector<IWindowPtr> mButtons;

		/// Servers shown in the menu, captured when the list is refreshed.
		std::vector<DiscoveredSession> mLocalServers;
		std::vector<PublicSession> mPublicServers;

		bool mVisible = false;
		bool mInitialized = false;
		bool mDirty = true;
		std::string mStatus;
	};
}
