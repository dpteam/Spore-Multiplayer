/****************************************************************************
* Spore Multiplayer
*
* Console commands. They are the primary way of controlling the mod, because
* the Spore console already provides text input:
*
*   mp_connect [host] [port]   connect to a server
*   mp_disconnect              leave the session
*   mp_name <name>             change your name
*   mp_say <text>              send a chat message
*   mp_status                  connection state, ping, players
*   mp_list                    list the players in the session
*   mp_hud [on|off]            show/hide the overlay
*   mp_avatars [on|off]        show/hide the other players in the world
*   mp_reload                  reload SporeMultiplayer.ini
*   mp_help                    list the commands
****************************************************************************/
#pragma once

#include <Spore\BasicIncludes.h>

namespace SporeMP
{
	/// Registers all the `mp_` cheats.
	void AddCheats();
}
