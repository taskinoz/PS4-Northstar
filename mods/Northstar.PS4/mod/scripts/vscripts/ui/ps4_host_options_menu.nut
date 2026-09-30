global function PS4HostOptionsMenu_Init

// Host options for a private match on PS4: the settings a PC server takes from
// its config or command line.
//   ns_report_server_to_masterserver - list the lobby in the server browser
//   ns_server_name, ns_server_desc   - how it is listed
//   ns_server_password               - asked for when joining from the browser
//   ns_auth_allow_insecure           - let other players join (see ps4_host_options.nut)
//   ns_allow_duplicate_accounts      - PC's -allowdupeaccounts: players signed in
//                                      with the same account, such as one person
//                                      testing on two machines
// Opened with L1 from the private lobby. Text is typed on the system keyboard.
// NSPS4_SetHostOption saves each change, so the options last across restarts
// (runtime_atlas_server.inl); startup arguments still override them.
// A dialog has four buttons, so the listing has its own dialog.
const int PS4_SERVER_NAME_MAX = 63 // NorthstarLauncher's limit
const int PS4_SERVER_DESC_MAX = 255
const int PS4_SERVER_PASSWORD_MAX = 128 // Atlas's limit

struct
{
	// The button focused when a dialog opens again after one was chosen.
	int focus = 0
} file

void function PS4HostOptionsMenu_Init()
{
	AddMenuFooterOption( GetMenu( "PrivateLobbyMenu" ), BUTTON_SHOULDER_LEFT, "%[L_SHOULDER|]% Host Options", "Host Options", PS4HostOptions_OpenFromFooter, PS4HostOptions_Available )
}

bool function PS4HostOptions_Available()
{
	return IsConnected() && IsPrivateMatch()
}

string function PS4HostOptions_State( string convar )
{
	return GetConVarBool( convar ) ? "On" : "Off"
}

// A button shows at most `length` bytes of the text, cut between characters.
string function PS4HostOptions_Short( string text, int length )
{
	if ( text.len() <= length )
		return text
	int end = length
	while ( end > 0 && ( expect int( text[end] ) & 0xc0 ) == 0x80 )
		end--
	return text.slice( 0, end ) + "..."
}

void function PS4HostOptions_OpenFromFooter( var button )
{
	file.focus = 0
	PS4HostOptions_Open( button )
}

void function PS4HostOptions_Open( var button )
{
	DialogData dialogData
	dialogData.header = "Host Options"
	dialogData.message = "These apply to matches you host.\n\n"
		+ "Server browser: list this lobby so players on PC and PS4 can find it, and set its name and password.\n\n"
		+ "Other players: anyone can join while this is on. With it off, only you and players who join through the server browser can play.\n\n"
		+ "Same account on several machines: lets players signed in with the same account join (PC's -allowdupeaccounts)."
	AddDialogButton( dialogData, "Server browser: " + ( GetConVarBool( "ns_report_server_to_masterserver" ) ? "Listed" : "Not listed" ), PS4HostOptions_OpenListing, "", file.focus == 0 )
	AddDialogButton( dialogData, "Other players: " + PS4HostOptions_State( "ns_auth_allow_insecure" ), PS4HostOptions_ToggleInsecure, "", file.focus == 1 )
	AddDialogButton( dialogData, "Same account on several machines: " + PS4HostOptions_State( "ns_allow_duplicate_accounts" ), PS4HostOptions_ToggleDuplicates, "", file.focus == 2 )
	AddDialogFooter( dialogData, "#A_BUTTON_SELECT" )
	AddDialogFooter( dialogData, "#B_BUTTON_BACK" )
	OpenDialog( dialogData )
}

void function PS4HostOptions_OpenListing()
{
	file.focus = 0
	thread PS4HostOptions_Reopen( PS4HostOptions_Listing )
}

void function PS4HostOptions_Listing()
{
	bool listing = GetConVarBool( "ns_report_server_to_masterserver" )
	string status = "Not listed."
	if ( listing )
		status = NSPS4_IsServerListed() ? "Listed in the server browser." : "Not listed yet. Registering can take a few seconds; it fails if UDP port 37015 isn't forwarded to this machine."

	DialogData dialogData
	dialogData.header = "Server Browser"
	dialogData.message = status + "\n\n"
		+ "The name and description show in the server browser and change within a few seconds. "
		+ "Players who join from the browser are asked for the password; a new password lists the server again."
	AddDialogButton( dialogData, "Listed: " + ( listing ? "On" : "Off" ), PS4HostOptions_ToggleListing, "", file.focus == 0 )
	AddDialogButton( dialogData, "Name: " + PS4HostOptions_Short( GetConVarString( "ns_server_name" ), 32 ), PS4HostOptions_EditName, "", file.focus == 1 )
	AddDialogButton( dialogData, "Description: " + PS4HostOptions_Short( GetConVarString( "ns_server_desc" ), 32 ), PS4HostOptions_EditDescription, "", file.focus == 2 )
	AddDialogButton( dialogData, "Password: " + ( GetConVarString( "ns_server_password" ) == "" ? "None" : "Set" ), PS4HostOptions_EditPassword, "", file.focus == 3 )
	AddDialogFooter( dialogData, "#A_BUTTON_SELECT" )
	AddDialogFooter( dialogData, "#B_BUTTON_BACK" )
	OpenDialog( dialogData )
}

void function PS4HostOptions_Toggle( string convar, int focus, void functionref() reopen )
{
	file.focus = focus
	NSPS4_SetHostOption( convar, GetConVarBool( convar ) ? "0" : "1" )
	thread PS4HostOptions_Reopen( reopen )
}

void function PS4HostOptions_ToggleInsecure()
{
	PS4HostOptions_Toggle( "ns_auth_allow_insecure", 1, PS4HostOptions_OpenMain )
}

void function PS4HostOptions_ToggleDuplicates()
{
	PS4HostOptions_Toggle( "ns_allow_duplicate_accounts", 2, PS4HostOptions_OpenMain )
}

void function PS4HostOptions_ToggleListing()
{
	PS4HostOptions_Toggle( "ns_report_server_to_masterserver", 0, PS4HostOptions_Listing )
}

void function PS4HostOptions_OpenMain()
{
	PS4HostOptions_Open( null )
}

void function PS4HostOptions_EditName()
{
	file.focus = 1
	thread PS4HostOptions_Edit( "Server name", "ns_server_name", PS4_SERVER_NAME_MAX, false )
}

void function PS4HostOptions_EditDescription()
{
	file.focus = 2
	thread PS4HostOptions_Edit( "Server description", "ns_server_desc", PS4_SERVER_DESC_MAX, false )
}

// The password is typed hidden, starting empty: Done with nothing typed
// removes it, cancelling keeps the current one.
void function PS4HostOptions_EditPassword()
{
	file.focus = 3
	thread PS4HostOptions_Edit( "Server password (empty for none)", "ns_server_password", PS4_SERVER_PASSWORD_MAX, true )
}

void function PS4HostOptions_Edit( string title, string convar, int maxLength, bool secret )
{
	WaitFrame()
	if ( !NSPS4_OpenTextInput( title, secret ? "" : GetConVarString( convar ), maxLength, secret ) )
	{
		PS4HostOptions_Listing()
		return
	}

	int state = NSPS4_UpdateTextInput()
	while ( state == 1 )
	{
		WaitFrame()
		state = NSPS4_UpdateTextInput()
	}

	if ( state == 2 )
	{
		string text = strip( NSPS4_GetTextInput() )
		// Atlas refuses a listing without a name.
		if ( text != "" || convar != "ns_server_name" )
			NSPS4_SetHostOption( convar, text )
	}
	PS4HostOptions_Listing()
}

// The dialog closes when a button is chosen; open it again with the new state.
void function PS4HostOptions_Reopen( void functionref() open )
{
	WaitFrame()
	open()
}
