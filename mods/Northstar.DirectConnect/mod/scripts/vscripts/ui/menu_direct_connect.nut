global function AddDirectConnectMenu
global function InitDirectConnectMenu

struct
{
	var menu
} file

// Northstar UICallback entry point (mod.json: Scripts[0].UICallback.Before).
// Runs before the UI script list finishes initializing menus, so AddMenu()
// here behaves exactly like a vanilla InitMenus() registration -- this is
// the same pattern Northstar.Client uses for ConnectWithPasswordMenu
// (see menu_ns_connect_password.nut) and ModListMenu (menu_ns_modmenu.nut).
void function AddDirectConnectMenu()
{
	AddMenu( "DirectConnectMenu", $"resource/ui/menus/direct_connect.menu", InitDirectConnectMenu )
}

void function InitDirectConnectMenu()
{
	file.menu = GetMenu( "DirectConnectMenu" )

	AddMenuFooterOption( file.menu, BUTTON_B, "#B_BUTTON_BACK", "#BACK" )

	AddButtonEventHandler( Hud_GetChild( file.menu, "ConnectButton" ), UIE_CLICK, ConnectButton_Activate )
}

void function ConnectButton_Activate( var button )
{
	// TextEntry no likey ":" so had to divide them and do funky logic
	string ip = Hud_GetUTF8Text( Hud_GetChild( file.menu, "ConnectIPTextEntry" ) )
	string port = Hud_GetUTF8Text( Hud_GetChild( file.menu, "ConnectPortTextEntry" ) )

	bool usePort = port == "" ? false : true

	string server = ip + ( usePort ? ":" + port : "" )

	print( "[DIRECT-CONNECT]: Connecting to '" + server + "'" )
	ClientCommand( "sv_cheats 1" )
	ClientCommand( "net_usesocketsforloopback 1" )
	ClientCommand( "connect " + server )
}
