/****************************************************************************
* Spore Multiplayer
****************************************************************************/
#include "stdafx.h"
#include "MpCheats.h"
#include "MpAssets.h"
#include "MpAssets.h"
#include "MpAvatars.h"
#include "MpConfig.h"
#include "MpConsole.h"
#include "MpDiscovery.h"
#include "MpHost.h"
#include "MpLog.h"
#include "MpMaster.h"
#include "MpMenu.h"
#include "MpOverlay.h"
#include "MpSession.h"

#include <cstdlib>
#include <string>

namespace SporeMP
{
	namespace
	{
		/// Last list fetched from the master, so mp_join can use it right after
		/// mp_internet without asking again.
		std::vector<PublicSession> g_publicSessions;

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
			// Show the output in both consoles: the Spore one (Ctrl+Shift+C) and the
			// mod console window.
			App::ConsolePrintF("%s", text.c_str());
			Console::Get().Print(text);
		}

		void PrintHelp()
		{
			Print("Spore Multiplayer commands:");
			Print("  mp_menu                  - open the in-game menu");
			Print("  mp_host [port] [name]    - start a session and join it");
			Print("  mp_servers               - list the sessions on the local network");
			Print("  mp_internet              - list the sessions on the master server");
			Print("  mp_join [number]         - join a session from that list");
			Print("  mp_relay <host> <port> <ticket> - join through a relay");
			Print("  mp_connect [host] [port] - connect to a server by address");
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
			Print("  mp_diag                  - write a diagnostic report to the log");
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

		/// Starts a session: launches the dedicated server and connects to it.
		class HostCheat : public ArgScript::ICommand
		{
		public:
			void ParseLine(const ArgScript::Line& line) override
			{
				Session& session = Session::Get();
				Host& host = Host::Get();

				if (session.IsConnected())
				{
					Print("Already connected to a session, use mp_disconnect first");
					return;
				}

				uint16_t port = session.Config().port;
				std::string name = session.Config().sessionName;

				if (line.GetArgumentsCount() >= 2)
				{
					const char* portText = line.GetArgumentAt(1);
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

				const std::string customName = GetRawArguments(line, 2);
				if (!customName.empty()) name = customName;

				std::string error;
				if (!host.StartServer(port, name, session.Config().masterUrl,
					session.Config().relayUrl, error))
				{
					Print("Could not host: " + error);
					return;
				}

				Print("Hosting '" + name + "' on port " + std::to_string(port) +
					", the other players can use mp_servers to find it");

				// Give the server a moment to open its socket.
				session.ConnectDelayed("127.0.0.1", port, 1.5f);
			}

			const char* GetDescription(ArgScript::DescriptionMode mode) const override
			{
				return "Starts a session and joins it. Usage: mp_host [port] [session name]";
			}
		};

		/// Lists the sessions found on the local network.
		class ServersCheat : public ArgScript::ICommand
		{
		public:
			void ParseLine(const ArgScript::Line& line) override
			{
				const auto& sessions = Discovery::Get().GetSessions();

				if (sessions.empty())
				{
					Print("No sessions found on the local network");
					Print("Ask the host to run mp_host, or connect directly with mp_connect <ip> [port]");
					return;
				}

				Print("Sessions found on the local network:");
				for (size_t i = 0; i < sessions.size(); ++i)
				{
					const DiscoveredSession& session = sessions[i];

					Print("  " + std::to_string(i + 1) + ") " + session.name +
						"  " + session.address + ":" + std::to_string(session.port) +
						"  (" + std::to_string(session.players) + "/" + std::to_string(session.maxPlayers) + ")");
				}

				Print("Use mp_join <number> to join one");
			}

			const char* GetDescription(ArgScript::DescriptionMode mode) const override
			{
				return "Lists the Spore Multiplayer sessions found on the local network.";
			}
		};

		/// Connects through a relay, for hosts without port forwarding.
		class RelayCheat : public ArgScript::ICommand
		{
		public:
			void ParseLine(const ArgScript::Line& line) override
			{
				const size_t count = line.GetArgumentsCount();
				if (count < 3)
				{
					Print("Usage: mp_relay <host> <port> <ticket>");
					Print("Get the address and the ticket from the host, or use mp_internet");
					return;
				}

				const char* hostText = line.GetArgumentAt(1);
				const char* portText = line.GetArgumentAt(2);
				const char* ticketText = line.GetArgumentAt(3);

				const int port = portText != nullptr ? std::atoi(portText) : 0;
				if (hostText == nullptr || port <= 0 || port >= 65536 || ticketText == nullptr)
				{
					Print("Usage: mp_relay <host> <port> <ticket>");
					return;
				}

				const std::string relayAddress = std::string(hostText) + ":" + std::to_string(port);

				Print("Connecting through the relay " + relayAddress);

				// Through a relay the host address is irrelevant: everything goes
				// through the relay room.
				Session::Get().Connect("127.0.0.1", 0, relayAddress, ticketText);
			}

			const char* GetDescription(ArgScript::DescriptionMode mode) const override
			{
				return "Connects through a relay server. Usage: mp_relay <host> <port> <ticket>";
			}
		};

		/// Joins a session from the discovery list.
		class JoinCheat : public ArgScript::ICommand		{
		public:
			void ParseLine(const ArgScript::Line& line) override
			{
				const auto& sessions = Discovery::Get().GetSessions();

				// Prefer the local network; fall back to the public list fetched by
				// mp_internet, so the same command works in both cases.
				if (sessions.empty() && !g_publicSessions.empty())
				{
					size_t index = 0;
					if (line.GetArgumentsCount() >= 2)
					{
						const char* indexText = line.GetArgumentAt(1);
						const int value = indexText != nullptr ? std::atoi(indexText) : 0;
						if (value < 1 || value > static_cast<int>(g_publicSessions.size()))
						{
							Print("Usage: mp_join <number>, see mp_internet");
							return;
						}
						index = static_cast<size_t>(value - 1);
					}

					const PublicSession& session = g_publicSessions[index];

					if (!session.relayHost.empty())
					{
						const std::string relayAddress =
							session.relayHost + ":" + std::to_string(session.relayPort);

						Print("Joining '" + session.name + "' through the relay " + relayAddress);
						Session::Get().Connect("127.0.0.1", 0, relayAddress, session.ticket);
					}
					else
					{
						Print("Joining '" + session.name + "' at " + session.host + ":" +
							std::to_string(session.port));
						Session::Get().Connect(session.host, session.port);
					}
					return;
				}

				if (sessions.empty())
				{
					Print("No sessions found. Use mp_servers or mp_internet first");
					return;
				}

				size_t index = 0;
				if (line.GetArgumentsCount() >= 2)
				{
					const char* indexText = line.GetArgumentAt(1);
					const int value = indexText != nullptr ? std::atoi(indexText) : 0;
					if (value < 1 || value > static_cast<int>(sessions.size()))
					{
						Print("Usage: mp_join <number>, see mp_servers");
						return;
					}
					index = static_cast<size_t>(value - 1);
				}

				const DiscoveredSession& session = sessions[index];
				Print("Joining '" + session.name + "' at " + session.address + ":" + std::to_string(session.port));

				Session::Get().Connect(session.address, session.port);
			}

			const char* GetDescription(ArgScript::DescriptionMode mode) const override
			{
				return "Joins a session found on the local network. Usage: mp_join [number]";
			}
		};

		/// Lists the sessions registered on the master server.
		class InternetCheat : public ArgScript::ICommand
		{
		public:
			void ParseLine(const ArgScript::Line& line) override
			{
				const std::string masterUrl = Session::Get().Config().masterUrl;
				if (masterUrl.empty())
				{
					Print("No master server configured, set master= in SporeMultiplayer.ini");
					return;
				}

				Print("Querying " + masterUrl + "...");

				std::vector<PublicSession> sessions;
				std::string error;
				if (!MasterQuery::Fetch(masterUrl, sessions, error))
				{
					Print("Could not reach the master: " + error);
					return;
				}

				if (sessions.empty())
				{
					Print("No public sessions online right now");
					return;
				}

				g_publicSessions = sessions;

				Print("Public sessions (" + std::to_string(sessions.size()) + "):");
				for (size_t i = 0; i < sessions.size(); ++i)
				{
					const PublicSession& session = sessions[i];

					const std::string where = session.relayHost.empty()
						? session.host + ":" + std::to_string(session.port)
						: "via relay " + session.relayHost;

					Print("  " + std::to_string(i + 1) + ") " + session.name +
						"  " + where +
						"  (" + std::to_string(session.players) + "/" + std::to_string(session.maxPlayers) + ")");
				}

				Print("Use mp_join <number> to join one");
			}

			const char* GetDescription(ArgScript::DescriptionMode mode) const override
			{
				return "Lists the sessions registered on the master server.";
			}
		};

		/// Opens the in-game multiplayer menu.
		class MenuCheat : public ArgScript::ICommand
		{
		public:
			void ParseLine(const ArgScript::Line& line) override
			{
				Menu& menu = Menu::Get();

				if (line.GetArgumentsCount() >= 2)
				{
					bool visible = false;
					if (ParseOnOff(line.GetArgumentAt(1), visible))
					{
						menu.SetVisible(visible);
						Print(visible ? "Menu opened" : "Menu closed");
						return;
					}
				}

				menu.Toggle();
				Print(menu.IsVisible() ? "Menu opened" : "Menu closed");
			}

			const char* GetDescription(ArgScript::DescriptionMode mode) const override
			{
				return "Opens the multiplayer menu. Usage: mp_menu [on|off]";
			}
		};

		/// Dumps the full state to the log, for bug reports.
		class DiagCheat : public ArgScript::ICommand
		{
		public:
			void ParseLine(const ArgScript::Line& line) override
			{
				Session& session = Session::Get();

				MP_LOG_INFO("========== DIAGNOSTIC REPORT ==========");

				MP_LOG_INFO("Status: %s  stage=%s  system=%08x",
					session.GetStatusText().c_str(),
					StageName(session.GetLocalStage()),
					session.GetLocalSystemId());

				MP_LOG_INFO("Local: id=%u name='%s' latency=%d ms",
					session.GetLocalId(), session.GetLocalName().c_str(), session.GetLatency());

				MP_LOG_INFO("Session: '%s'  players=%zu",
					session.GetSessionName().c_str(), session.GetPlayers().size());

				for (const RemotePlayer& player : session.GetPlayers())
				{
					MP_LOG_INFO("  player %u '%s' stage=%s system=%08x state=%s pos=(%.1f, %.1f, %.1f)",
						player.id, player.name.c_str(), StageName(player.stage),
						player.systemId, player.hasState ? "yes" : "no",
						player.position.x, player.position.y, player.position.z);
				}

				const auto& discovered = Discovery::Get().GetSessions();
				MP_LOG_INFO("LAN sessions: %zu", discovered.size());
				for (const DiscoveredSession& entry : discovered)
				{
					MP_LOG_INFO("  '%s' %s:%u %d/%d",
						entry.name.c_str(), entry.address.c_str(), entry.port,
						entry.players, entry.maxPlayers);
				}

				MP_LOG_INFO("Avatars spawned: %d", Avatars::Get().GetActiveCount());
				MP_LOG_INFO("Imported creations: %d", AvatarAssets::Get().GetImportedCount());
				MP_LOG_INFO("Hosting server: %s", Host::Get().IsHosting() ? "yes" : "no");
				MP_LOG_INFO("Menu visible: %s", Menu::Get().IsVisible() ? "yes" : "no");

				MP_LOG_INFO("========== END DIAGNOSTIC ==========");

				Print("Diagnostic report written to " + GetLogPath());
				Print("Send that file to the developer");
			}

			const char* GetDescription(ArgScript::DescriptionMode mode) const override
			{
				return "Writes a full diagnostic report to the log file.";
			}
		};

		class DisconnectCheat : public ArgScript::ICommand		{
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
		manager->AddCheat("mp_host", new HostCheat());
		manager->AddCheat("mp_menu", new MenuCheat());
		manager->AddCheat("mp_diag", new DiagCheat());
		manager->AddCheat("mp_servers", new ServersCheat());
		manager->AddCheat("mp_internet", new InternetCheat());
		manager->AddCheat("mp_join", new JoinCheat());
		manager->AddCheat("mp_relay", new RelayCheat());
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
