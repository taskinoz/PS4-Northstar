untyped

global function PS4ChatHud_Init

// The in-match text chat panel, as PC shows it.
//
// PC's cl_main_hud.nut sizes IngameTextChat in InitChatHUD (called from
// cl_mapspawn.gnut) and shows or hides it every HUD frame in
// UpdateChatHUDVisibility (cl_player.gnut) - all inside `#if PC_PROG`, so the
// console build never shows it. Northstar.PS4 adds the panel itself
// (keyvalues/resource/ui/hudscripted_mp.res) and this does the rest, with the
// same size and rules: hidden in the lobby and while a menu is open.
void function PS4ChatHud_Init()
{
	if ( IsLobby() )
		return
	thread PS4ChatHud_Setup()
}

// The HUD layout is applied after the CLIENT scripts' init callbacks (PC's
// InitChatHUD runs later, from cl_mapspawn.gnut), so the panel is looked for
// each frame until it exists. A failed lookup is caught: an uncaught one would
// be a script error, and those end the match for this client.
void function PS4ChatHud_Setup()
{
	// The HUD can be rebuilt during a match (it was, on entering spectator),
	// which leaves an old handle invalid; calling Show on it is a script error
	// that ends the match. Look the panel up again whenever that happens.
	while ( true )
	{
		if ( !PS4ChatHud_Run() )
			return
		WaitFrame()
	}
}

// False when the panel never appears.
bool function PS4ChatHud_Run()
{
	local chat = null
	for ( int frame = 0; frame < 1800 && chat == null; frame++ )
	{
		try
		{
			chat = HudElement( "IngameTextChat" )
		}
		catch ( error )
		{
			WaitFrame()
		}
	}
	if ( chat == null )
	{
		printt( "[PS4 chat] IngameTextChat never appeared; in-match chat will not be shown" )
		return false
	}

	// Untyped, like cl_main_hud.nut: Hud and its elements are untyped objects.
	local screenSize = Hud.GetScreenSize()
	local resMultiplier = screenSize[1] / 1080.0
	int width = 630
	int height = 225
	try
	{
		chat.SetSize( width * resMultiplier, height * resMultiplier )
	}
	catch ( error )
	{
		return true
	}

	while ( true )
	{
		try
		{
			if ( clGlobal.isMenuOpen )
				chat.Hide()
			else
				chat.Show()
		}
		catch ( error )
		{
			printt( "[PS4 chat] IngameTextChat was rebuilt; looking it up again" )
			return true
		}
		WaitFrame()
	}
	unreachable
}
