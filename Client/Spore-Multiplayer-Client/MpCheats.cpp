/****************************************************************************
* Spore Multiplayer
****************************************************************************/
#include "stdafx.h"
#include "MpCheats.h"
#include "MpAvatars.h"
#include "MpConfig.h"
#include "MpLog.h"
#include "MpOverlay.h"
#include "MpSession.h"

#include <cstdlib>
#include <string>

namespace SporeMP
{
	namespace
	{
		/// Joins the arguments of an ArgScript line starting at `fromArgument`.
		/// Used for chat messages and names, which can contain spaces.
		std::string GetRawArguments(const ArgScript::Line& line, size_t fromArgument)
		{
			const size_t count = line.GetArgumentsCount();
			std::string result;

			for (size_t i = fromArgument; i < count; ++i)
			{
				const char* argument = line.GetArgumentAt(i);
				if (argument == nullptr) continue;

				if (!result.empty()) result += ' ';
				result += argument;
			}

			return result;
		}

		bool ParseOnOff(const char* text, bool& dst)
		{
			if (text == nullptr) return false;

			const std::string value(text);
			if (value == "on" || value == "1" || value == "true" || value == "yes")
			{
				dst = true;
				return true;
			}
			if (value == "off" || value == "0" || value == "false" || value == "no")
			{
				dst = false;
				return true;
			}
			return false;
		}

		void Print(const std::string& text)
		{
			App::ConsolePrintF("%s", text.c_str());
		}

		void PrintHelp()
		{
			Print("Spore Multiplayer commands:");
			Print("  mp_connect [host] [port] - connect to a server (default: the ini values)");
			Print("  mp_disconnect            - leave the session");
			Print("  mp_name <name>           - change your name");
			Print("  mp_say <text>            - send a chat message");
			Print("  mp_status                - connection state, ping, avatars");
			Print("  mp_list                  - list the players in the session");
			Print("  mp_hud [on|off]          - show or hide the overlay");
			Print("  mp_avatars [on|off]      - show or hide the other players in the world");
			Print("  mp_tags [on|off]         - show or hide the names above the avatars");
			Print("  mp_reload                - reload SporeMultiplayer.ini");
			Print("  mp_help                  - this list");
		}

		// --------------------------------------------------------------------
		// Cheat implementations
		// --------------------------------------------------------------------

		class ConnectCheat : public ArgScript::ICommand
		{
		public:
			void ParseLine(const ArgScript::Line& line) override
			{
				Session& session = Session::Get();
				const size_t count = line.GetArgumentsCount();

				std::string host = session.Config().host;
				uint16_t port = session.Config().port;

				if (count >= 2)
				{
					const char* hostText = line.GetArgumentAt(1);
					if (hostText != nullptr) host = hostText;
				}

				if (count >= 3)
				{
					const char* portText = line.GetArgumentAt(2);
					if (portText != nullptr)
					{
						const int value = std::atoi(portText);
						if (value > 0 && value < 65536)
						{
							port = static_cast<uint16_t>(value);
						}
						else
						{
							Print("Invalid port");
							return;
						}
					}
				}

				session.Connect(host, port);
			}

			const char* GetDescription(ArgScript::DescriptionMode mode) const override
			{
				return "Connects to a Spore Multiplayer server. Usage: mp_connect [host] [port]";
			}
		};

		class DisconnectCheat : public ArgScript::ICommand
		{
		public:
			void ParseLine(const ArgScript::Line& line) override
			{
				Session::Get().Disconnect();
			}

			const char* GetDescription(ArgScript::DescriptionMode mode) const override
			{
				return "Leaves the multiplayer session.";
			}
		};

		class NameCheat : public ArgScript::ICommand
		{
		public:
			void ParseLine(const ArgScript::Line& line) override
			{
				const std::string name = GetRawArguments(line, 1);
				if (name.empty())
				{
					Print("Usage: mp_name <name>");
					return;
				}

				Session::Get().SetName(name);
			}

			const char* GetDescription(ArgScript::DescriptionMode mode) const override
			{
				return "Changes the name shown to the other players. Usage: mp_name <name>";
			}
		};

		class SayCheat : public ArgScript::ICommand
		{
		public:
			void ParseLine(const ArgScript::Line& line) override
			{
				const std::string text = GetRawArguments(line, 1);
				if (text.empty())
				{
					Print("Usage: mp_say <text>");
					return;
				}

				Session::Get().SendChat(text);
			}

			const char* GetDescription(ArgScript::DescriptionMode mode) const override
			{
				return "Sends a chat message to the other players. Usage: mp_say <text>";
			}
		};

		class StatusCheat : public ArgScript::ICommand
		{
		public:
			void ParseLine(const ArgScript::Line& line) override
			{
				Session& session = Session::Get();
				Avatars& avatars = Avatars::Get();

				std::string status = "Multiplayer: " + session.GetStatusText();
				if (session.IsConnected())
				{
					status += "  session '" + session.GetSessionName() + "'";
					status += "  ping " + std::to_string(session.GetLatency()) + " ms";
					status += "  players " + std::to_string(session.GetPlayers().size() + 1);
					status += "  avatars " + std::to_string(avatars.GetActiveCount());
				}
				Print(status);
			}

			const char* GetDescription(ArgScript::DescriptionMode mode) const override
			{
				return "Shows the multiplayer connection state.";
			}
		};

		class ListCheat : public ArgScript::ICommand
		{
		public:
			void ParseLine(const ArgScript::Line& line) override
			{
				Session& session = Session::Get();
				if (!session.IsConnected())
				{
					Print("Not connected to a session");
					return;
				}

				Print("Players in '" + session.GetSessionName() + "':");
				Print(std::string("  ") + session.GetLocalName() + " (you) [" +
					StageName(session.GetLocalStage()) + "]");

				for (const RemotePlayer& player : session.GetPlayers())
				{
					Print("  " + player.name + " [" + StageName(player.stage) + "]");
				}
			}

			const char* GetDescription(ArgScript::DescriptionMode mode) const override
			{
				return "Lists the players in the session.";
			}
		};

		class HudCheat : public ArgScript::ICommand
		{
		public:
			void ParseLine(const ArgScript::Line& line) override
			{
				Session& session = Session::Get();
				Overlay& overlay = Overlay::Get();

				bool visible = !overlay.IsVisible();
				if (line.GetArgumentsCount() >= 2)
				{
					if (!ParseOnOff(line.GetArgumentAt(1), visible))
					{
						Print("Usage: mp_hud [on|off]");
						return;
					}
				}

				session.Config().hud = visible;
				overlay.SetVisible(visible);
				Print(visible ? "Overlay enabled" : "Overlay disabled");
			}

			const char* GetDescription(ArgScript::DescriptionMode mode) const override
			{
				return "Shows or hides the multiplayer overlay. Usage: mp_hud [on|off]";
			}
		};

		class AvatarsCheat : public ArgScript::ICommand
		{
		public:
			void ParseLine(const ArgScript::Line& line) override
			{
				Session& session = Session::Get();

				bool enabled = !session.Config().showAvatars;
				if (line.GetArgumentsCount() >= 2)
				{
					if (!ParseOnOff(line.GetArgumentAt(1), enabled))
					{
						Print("Usage: mp_avatars [on|off]");
						return;
					}
				}

				session.Config().showAvatars = enabled;
				if (!enabled)
				{
					Avatars::Get().Clear();
				}

				Print(enabled ? "Remote avatars enabled" : "Remote avatars disabled");
			}

			const char* GetDescription(ArgScript::DescriptionMode mode) const override
			{
				return "Shows or hides the other players in the world. Usage: mp_avatars [on|off]";
			}
		};

		class TagsCheat : public ArgScript::ICommand
		{
		public:
			void ParseLine(const ArgScript::Line& line) override
			{
				Session& session = Session::Get();

				bool enabled = !session.Config().nameTags;
				if (line.GetArgumentsCount() >= 2)
				{
					if (!ParseOnOff(line.GetArgumentAt(1), enabled))
					{
						Print("Usage: mp_tags [on|off]");
						return;
					}
				}

				session.Config().nameTags = enabled;
				Overlay::Get().MarkDirty();
				Print(enabled ? "Name tags enabled" : "Name tags disabled");

				// Also verify that the projection works in the current stage.
				Avatars::DebugProjectLocalAvatar();
			}

			const char* GetDescription(ArgScript::DescriptionMode mode) const override
			{
				return "Shows or hides the names above the other players. Usage: mp_tags [on|off]";
			}
		};

		class ReloadCheat : public ArgScript::ICommand		{
		public:
			void ParseLine(const ArgScript::Line& line) override
			{
				Session& session = Session::Get();

				ClientConfig config = session.Config();
				if (!LoadClientConfig(session.GetDirectory() + "SporeMultiplayer.ini", config))
				{
					Print("Could not read SporeMultiplayer.ini");
					return;
				}

				session.Config() = config;

				Overlay::Get().SetVisible(config.hud);
				if (!config.showAvatars)
				{
					Avatars::Get().Clear();
				}

				Print("Configuration reloaded");
			}

			const char* GetDescription(ArgScript::DescriptionMode mode) const override
			{
				return "Reloads SporeMultiplayer.ini.";
			}
		};

		class HelpCheat : public ArgScript::ICommand
		{
		public:
			void ParseLine(const ArgScript::Line& line) override
			{
				PrintHelp();
			}

			const char* GetDescription(ArgScript::DescriptionMode mode) const override
			{
				return "Lists the Spore Multiplayer commands.";
			}
		};
	}

	void AddCheats()
	{
		App::ICheatManager* manager = App::ICheatManager::Get();
		if (manager == nullptr)
		{
			MP_LOG_ERROR("The cheat manager is not available, the commands were not registered");
			return;
		}

		manager->AddCheat("mp_connect", new ConnectCheat());
		manager->AddCheat("mp_disconnect", new DisconnectCheat());
		manager->AddCheat("mp_name", new NameCheat());
		manager->AddCheat("mp_say", new SayCheat());
		manager->AddCheat("mp_status", new StatusCheat());
		manager->AddCheat("mp_list", new ListCheat());
		manager->AddCheat("mp_hud", new HudCheat());
		manager->AddCheat("mp_avatars", new AvatarsCheat());
		manager->AddCheat("mp_tags", new TagsCheat());
		manager->AddCheat("mp_reload", new ReloadCheat());
		manager->AddCheat("mp_help", new HelpCheat());

		MP_LOG_INFO("Registered the multiplayer commands");
	}
}
