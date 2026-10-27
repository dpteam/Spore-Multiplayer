/****************************************************************************
* Spore Multiplayer
*
* Lightweight on screen overlay: the connection status, the list of players in
* the session and the last chat lines.
*
* The overlay is a regular UTFWin window with a custom drawable, so it is drawn
* by the game itself and does not need DirectX access.
*
* Chat is typed in the Spore console (Ctrl+Shift+C) with the `mp_say` cheat,
* which means we do not have to implement text input.
****************************************************************************/
#pragma once

#include <Spore\BasicIncludes.h>

#include <vector>

namespace SporeMP
{
	/// Draws the text of the overlay. Using a custom drawable means the text is
	/// rendered with the same font system as the rest of the game.
	class OverlayDrawable : public UTFWin::DefaultDrawable
	{
	public:
		static const uint32_t TYPE = 0x8F3A21C4;

		void SetContent(const std::vector<eastl::string16>& lines, float lineHeight,
			Math::Color textColor, Math::Color backgroundColor);

		void* Cast(uint32_t type) const override;
		void Paint(UTFWin::UIRenderer* pRenderer, const Math::Rectangle& area, const UTFWin::RenderParams& params) override;

	private:
		std::vector<eastl::string16> mLines;
		float mLineHeight = 16.0f;
		Math::Color mTextColor;
		Math::Color mBackgroundColor;
	};

	class Overlay
	{
	public:
		static Overlay& Get();

		void Initialize();
		void Update(float deltaTime);
		void Dispose();

		void SetVisible(bool visible);
		bool IsVisible() const { return mVisible; }

		/// Forces a repaint on the next frame.
		void MarkDirty() { mDirty = true; }

	private:
		Overlay();

		void CreateOverlayWindow();
		void DestroyOverlayWindow();
		void Refresh();

		IWindowPtr mpWindow;
		eastl::intrusive_ptr<OverlayDrawable> mpDrawable;

		bool mVisible = false;
		bool mDirty = true;
		bool mInitialized = false;
		float mRefreshTimer = 0.0f;
	};
}
