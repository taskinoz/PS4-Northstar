global function AddDirectConnectMenu
global function AddDirectConnectMenu_MainMenuFooter
global function InitDirectConnectMenu

struct
{
	var menu
} file

// Northstar UICallback entry points (mod.json: Scripts[0].UICallback).
//
// Before: runs while the UI script list is still initializing menus, so
// AddMenu() here behaves exactly like a vanilla InitMenus() registration.
// After: runs once UIInit has finished, which is the only point where
// GetMenu( "MainMenu" ) is valid, so the footer option that actually opens
// this menu has to be added there.
//
// This is the same split Northstar.Client uses for its own Mods entry
// (menu_ns_modmenu.nut: AddNorthstarModMenu / AddNorthstarModMenu_MainMenuFooter).
void function AddDirectConnectMenu()
{
	AddMenu( "DirectConnectMenu", $"resource/ui/menus/direct_connect.menu", InitDirectConnectMenu )
}

// BUTTON_Y on the main menu is taken by Northstar's own Mods list. BUTTON_X is
// free under Northstar: vanilla's inbox-accept footer option is guarded by
// "#if VANILLA", which Northstar compiles out.
void function AddDirectConnectMenu_MainMenuFooter()
{
	string controllerStr = PrependControllerPrompts( BUTTON_X, "#MENU_DIRECT_CONNECT" )
	AddMenuFooterOption( GetMenu( "MainMenu" ), BUTTON_X, controllerStr, "#MENU_DIRECT_CONNECT", AdvanceToDirectConnectMenu )
}

void function AdvanceToDirectConnectMenu( var button )
{
	AdvanceMenu( GetMenu( "DirectConnectMenu" ) )
}

void function InitDirectConnectMenu()
{
	file.menu = GetMenu( "DirectConnectMenu" )

	AddMenuFooterOption( file.menu, BUTTON_B, "#B_BUTTON_BACK", "#BACK" )

	AddButtonEventHandler( Hud_GetChild( file.menu, "ConnectButton" ), UIE_CLICK, ConnectButton_Activate )
}

// TextEntry does not accept ":", so the address and port are separate fields.
bool function IsLoopbackAddress( string ip )
{
	return ip == "localhost" || ip.find( "127." ) == 0
}

void function ConnectButton_Activate( var button )
{
	// strip() is the helper vanilla and Northstar UI scripts already use
	// (menu_mod_settings.nut) and removes stray leading/trailing whitespace.
	string ip = strip( Hud_GetUTF8Text( Hud_GetChild( file.menu, "ConnectIPTextEntry" ) ) )
	string port = strip( Hud_GetUTF8Text( Hud_GetChild( file.menu, "ConnectPortTextEntry" ) ) )

	// Without this an empty field would run a bare "connect", which drops the
	// player into a failed-connection state with no explanation.
	if ( ip == "" )
	{
		print( "[DIRECT-CONNECT]: no address entered" )
		return
	}

	string server = ip + ( port == "" ? "" : ":" + port )

	// Only relevant when client and server share a machine; on any other
	// address this just forces socket code the engine would not otherwise use.
	if ( IsLoopbackAddress( ip ) )
		ClientCommand( "net_usesocketsforloopback 1" )

	print( "[DIRECT-CONNECT]: connecting to '" + server + "'" )
	ClientCommand( "connect " + server )
}
