global function PS4ChatMenu_Init

// Typing chat with a pad: L2 (all) and R2 (team) in the in-game menu and the
// private lobby open the system keyboard, and the text is sent the way PC's
// chat box sends it - through CLIENT script NS_PreSendMessage, so mods'
// OnPreSendMessage callbacks still apply (runtime_chat_ui.inl).
void function PS4ChatMenu_Init()
{
	foreach ( string name in [ "InGameMPMenu", "PrivateLobbyMenu" ] )
	{
		var menu = GetMenu( name )
		AddMenuFooterOption( menu, BUTTON_TRIGGER_LEFT, "%[L_TRIGGER|]% Chat", "Chat", PS4Chat_OpenAll, PS4Chat_Available )
		AddMenuFooterOption( menu, BUTTON_TRIGGER_RIGHT, "%[R_TRIGGER|]% Team Chat", "Team Chat", PS4Chat_OpenTeam, PS4Chat_Available )
	}
}

bool function PS4Chat_Available()
{
	return IsConnected()
}

void function PS4Chat_OpenAll( var button )
{
	PS4Chat_Open( false )
}

void function PS4Chat_OpenTeam( var button )
{
	PS4Chat_Open( true )
}

void function PS4Chat_Open( bool isTeam )
{
	if ( NSPS4_OpenChatKeyboard( isTeam ) )
		thread PS4Chat_WaitForKeyboard()
}

void function PS4Chat_WaitForKeyboard()
{
	int state = NSPS4_UpdateChatKeyboard()
	while ( state == 1 )
	{
		WaitFrame()
		state = NSPS4_UpdateChatKeyboard()
	}

	// In a match the chat panel is hidden while a menu is open, as on PC; close
	// the in-game menu so the message is seen going out.
	if ( state == 2 && GetActiveMenu() == GetMenu( "InGameMPMenu" ) )
		CloseActiveMenu()
}
