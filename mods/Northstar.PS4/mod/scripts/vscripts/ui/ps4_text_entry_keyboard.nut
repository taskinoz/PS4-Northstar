global function PS4TextEntryKeyboard_Init

// Text boxes on a pad. PC types into Northstar's text boxes with a keyboard; a
// PS4 has none, so selecting one only moved focus into it and nothing could be
// typed. Here a text box that gains focus opens the system keyboard holding its
// text (NSPS4_OpenTextInput, runtime_chat_ui.inl). Done writes the text back
// and returns focus to the row's button, so the menu's own lose-focus handler
// applies the value exactly as it does on PC. Cancel leaves the text as it was.
//
// Covered: Custom Match Settings (score/time limits and the other number
// settings), where Northstar's handler sends setplaylistvaroverrides on
// UIE_LOSE_FOCUS.

struct
{
	bool open = false
} file

void function PS4TextEntryKeyboard_Init()
{
	var menu = GetMenu( "CustomMatchSettingsMenu" )
	foreach ( var textPanel in GetElementsByClassname( menu, "MatchSettingTextEntry" ) )
		Hud_AddEventHandler( textPanel, UIE_GET_FOCUS, PS4TextEntryKeyboard_MatchSettingFocused )
}

void function PS4TextEntryKeyboard_MatchSettingFocused( var textPanel )
{
	if ( file.open )
		return

	var menu = GetMenu( "CustomMatchSettingsMenu" )
	var button = GetElementsByClassname( menu, "MatchSettingButton" )[ int( Hud_GetScriptID( textPanel ) ) ]
	thread PS4TextEntryKeyboard_Edit( textPanel, button, "Match setting" )
}

void function PS4TextEntryKeyboard_Edit( var textPanel, var returnFocus, string title )
{
	file.open = true
	if ( !NSPS4_OpenTextInput( title, Hud_GetUTF8Text( textPanel ), 64, false ) )
	{
		file.open = false
		return
	}

	int state = NSPS4_UpdateTextInput()
	while ( state == 1 )
	{
		WaitFrame()
		state = NSPS4_UpdateTextInput()
	}

	if ( state == 2 )
		Hud_SetText( textPanel, strip( NSPS4_GetTextInput() ) )

	// Leaving the text box runs the menu's own UIE_LOSE_FOCUS handler.
	Hud_SetFocused( returnFocus )
	WaitFrame()
	file.open = false
}
