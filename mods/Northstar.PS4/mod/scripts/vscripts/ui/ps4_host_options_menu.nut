global function PS4HostOptionsMenu_Init

// Host options for a private match on PS4: the settings a PC server takes from
// its config or command line.
//   ns_auth_allow_insecure      - let other players join (see ps4_host_options.nut)
//   ns_allow_duplicate_accounts - PC's -allowdupeaccounts: players signed in
//                                 with the same account, such as one person
//                                 testing on two machines
// Opened with L1 from the private lobby. The values last until the game
// restarts, like console settings on PC; -allowdupeaccounts in
// ns_startup_args.txt starts duplicate accounts on.
void function PS4HostOptionsMenu_Init()
{
	AddMenuFooterOption( GetMenu( "PrivateLobbyMenu" ), BUTTON_SHOULDER_LEFT, "%[L_SHOULDER|]% Host Options", "Host Options", PS4HostOptions_Open, PS4HostOptions_Available )
}

bool function PS4HostOptions_Available()
{
	return IsConnected() && IsPrivateMatch()
}

string function PS4HostOptions_State( string convar )
{
	return GetConVarBool( convar ) ? "On" : "Off"
}

void function PS4HostOptions_Open( var button )
{
	DialogData dialogData
	dialogData.header = "Host Options"
	dialogData.message = "These apply to matches you host.\n\n"
		+ "Other players: anyone can join while this is on. A PS4 host can't check Northstar sign-ins, so with it off only you can play.\n\n"
		+ "Same account on several machines: lets players signed in with the same account join (PC's -allowdupeaccounts)."
	AddDialogButton( dialogData, "Other players: " + PS4HostOptions_State( "ns_auth_allow_insecure" ), PS4HostOptions_ToggleInsecure )
	AddDialogButton( dialogData, "Same account on several machines: " + PS4HostOptions_State( "ns_allow_duplicate_accounts" ), PS4HostOptions_ToggleDuplicates )
	AddDialogButton( dialogData, "#DISMISS" )
	AddDialogFooter( dialogData, "#A_BUTTON_SELECT" )
	AddDialogFooter( dialogData, "#B_BUTTON_BACK" )
	OpenDialog( dialogData )
}

void function PS4HostOptions_ToggleInsecure()
{
	SetConVarBool( "ns_auth_allow_insecure", !GetConVarBool( "ns_auth_allow_insecure" ) )
	thread PS4HostOptions_Reopen()
}

void function PS4HostOptions_ToggleDuplicates()
{
	SetConVarBool( "ns_allow_duplicate_accounts", !GetConVarBool( "ns_allow_duplicate_accounts" ) )
	thread PS4HostOptions_Reopen()
}

// The dialog closes when a button is chosen; open it again with the new state.
void function PS4HostOptions_Reopen()
{
	WaitFrame()
	PS4HostOptions_Open( null )
}
