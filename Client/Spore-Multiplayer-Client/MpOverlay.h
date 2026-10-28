/****************************************************************************
* Spore Multiplayer
*
* On screen overlay: the connection status, the list of players, the chat and
* the name tags above the remote avatars.
*
* The overlay is a single full screen UTFWin window with a custom drawable, so
* the game itself renders it (no DirectX access needed). Everything is drawn in
* absolute screen coordinates: the HUD block at the configured position and the
* name tags at the projected position of each avatar.
*
* Chat is typed in the Spore console (Ctrl+Shift+C) with the `mp_say` cheat,
* which means we do not have to implement text input.
****************************************************************************/
#pragma once

#include <Spore\BasicIncludes.h>

#include <vector>

namespace SporeMP
{
	/// A text drawn at an absolute screen position (used for the name tags).
	struct ScreenLabel
	{
		float x = 0.0f;
		float y = 0.0f;
		eastl::string16 text;
	};

	/// Draws the HUD block and the name tags. Using a custom drawable means the
	/// text is rendered with the same font system as the rest of the game.
	class OverlayDrawable : public UTFWin::DefaultDrawable
	{
	public:
		static const uint32_t TYPE = 0x8F3A21C4;

		void SetContent(const std::vector<eastl::string16>& lines, const Math::Rectangle& hudArea,
			float lineHeight, Math::Color textColor, Math::Color backgroundColor,
			const std::vector<ScreenLabel>& labels, Math::Color labelColor);

		void* Cast(uint32_t type) const override;
		void Paint(UTFWin::UIRenderer* pRenderer, const Math::Rectangle& area, const UTFWin::RenderParams& params) override;

	private:
		std::vector<eastl::string16> mLines;
		std::vector<ScreenLabel> mLabels;
		Math::Rectangle mHudArea;
		float mLineHeight = 16.0f;
		Math::Color mTextColor;
		Math::Color mBackgroundColor;
		Math::Color mLabelColor;
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

		/// Window that holds the HUD block; its area is exactly the block, which is
		/// what the game clips the drawing to.
		IWindowPtr mpHudWindow;
		eastl::intrusive_ptr<OverlayDrawable> mpHudDrawable;

		/// Full screen window used only for the name tags, which need to be drawn
		/// anywhere on the screen.
		IWindowPtr mpTagsWindow;
		eastl::intrusive_ptr<OverlayDrawable> mpTagsDrawable;

		bool mVisible = false;
		bool mDirty = true;
		bool mInitialized = false;
		float mRefreshTimer = 0.0f;
	};
}
