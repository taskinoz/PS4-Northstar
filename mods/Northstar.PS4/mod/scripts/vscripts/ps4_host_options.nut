global function PS4HostOptions_Init

// Server half of the private match host options (see ui/ps4_host_options_menu.nut).
//
// PC servers with ns_auth_allow_insecure 0 accept only players Atlas has
// authenticated for them. A PS4 host checks that when it is on the Atlas server
// list (ps4_server_presence.nut, runtime_atlas_server.inl): at 0 it keeps its
// own player (entity 1, the listen server's local client), bots and players
// who joined with a token Atlas issued for this server; at 1 anyone may join,
// as on a PC server with insecure connections on. ns_allow_duplicate_accounts
// is applied natively, when a client connects.
//
// A timer rather than AddCallback_OnClientConnected: in the lobby, where a
// private match starts, CodeCallback_OnClientConnectionCompleted returns before
// running those callbacks.
void function PS4HostOptions_Init()
{
	thread PS4HostOptions_Watch()
}

void function PS4HostOptions_Watch()
{
	while ( true )
	{
		wait 1.0
		if ( GetConVarBool( "ns_auth_allow_insecure" ) )
			continue

		foreach ( entity player in GetPlayerArray() )
		{
			if ( !IsValid( player ) || player.IsBot() || player.GetEntIndex() == 1 )
				continue
			if ( NSPS4_IsClientAtlasAuthenticated( player.GetEntIndex() - 1 ) )
				continue

			print( "[PS4 host] removing " + player.GetPlayerName() + ": insecure connections are off" )
			NSPS4_DisconnectClient( player.GetEntIndex() - 1, "This PS4-hosted match isn't accepting other players. The host can allow them in Host Options." )
		}
	}
}
