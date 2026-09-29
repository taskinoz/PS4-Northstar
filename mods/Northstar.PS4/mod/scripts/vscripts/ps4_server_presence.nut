global function PS4ServerPresence_Init

// Reports this server's presence to the runtime once a second, which puts it on
// the Atlas server list as PC's ServerPresenceManager does (the map, playlist,
// max players and player count PC reads from the engine; the runtime adds the
// ns_server_* convars and hostport). See runtime_atlas_server.inl.
void function PS4ServerPresence_Init()
{
	thread PS4ServerPresence_Report()
}

void function PS4ServerPresence_Report()
{
	while ( true )
	{
		NSPS4_UpdateServerPresence( GetMapName(), GetCurrentPlaylistName(), GetCurrentPlaylistVarInt( "max_players", 6 ), GetPlayerArray().len() )
		wait 1.0
	}
}
