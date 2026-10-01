"""Produce Northstar.PS4's overrides of Northstar.Client's Mod Settings menu.

Run from the repository root after Northstar.Client changes:
    python scripts/menus/build_mod_settings_override.py

menu_mod_settings.nut is Northstar.Client's file with PS4 additions marked
"PS4:". On a pad the menu could not be used: focus started in the search box
and could not reach the rows (PS4 only follows explicit navigation links), the
list only scrolled with a mouse wheel, and its text boxes had no keyboard. The
additions:
  - d-pad/stick up and down move between rows that have a control, scrolling
    at the edges; up from the first returns to the search box;
  - L1/R1 page the list (footer buttons).
Text boxes need nothing extra: Cross on a focused text box opens the PS4
system keyboard natively, and the value applies when focus leaves the box, as
on PC. (A script keyboard opened on top of it is refused as busy.)
The rows' vertical nav links are pointed at themselves so the engine does not
also move focus.

mod_setting.res draws the reset marker with vgui/reset, a PC .vtf that the PS4
material system cannot load (retail PS4 textures carry no VTF header), so it
showed as a magenta checkerboard. The override uses the retail vgui/hud/white
as a small grey dot instead.
"""
import os

NL = chr(10)
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CLIENT = os.path.join(ROOT, 'vendor', 'NorthstarMods', 'Northstar.Client', 'mod')
PS4 = os.path.join(ROOT, 'mods', 'Northstar.PS4', 'mod')


def read(path):
    return open(path, encoding='utf-8').read().replace('\r\n', NL)


def replace_once(text, old, new):
    assert text.count(old) == 1, old[:60]
    return text.replace(old, new, 1)


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'w', encoding='utf-8', newline=NL).write(text)
    print('wrote', os.path.relpath(path, ROOT))


# --- menu_mod_settings.nut -------------------------------------------------
script = read(os.path.join(CLIENT, 'scripts', 'vscripts', 'ui', 'menu_mod_settings.nut'))

script = replace_once(script,
    '\t\t\tfile.filterText = Hud_GetUTF8Text( inputField )' + NL +
    '\t\t\tOnFiltersChange()' + NL + '\t\t}' + NL + '\t)' + NL + '}',
    '\t\t\tfile.filterText = Hud_GetUTF8Text( inputField )' + NL +
    '\t\t\tOnFiltersChange()' + NL + '\t\t}' + NL + '\t)' + NL + NL +
    '\tPS4ModSettings_Init() // PS4' + NL + '}')

script = replace_once(script,
    '\t\tRegisterButtonPressedCallback( MOUSE_LEFT, OnClick )' + NL,
    '\t\tRegisterButtonPressedCallback( MOUSE_LEFT, OnClick )' + NL +
    '\t\tPS4ModSettings_RegisterPad() // PS4' + NL)

script = replace_once(script,
    '\tDeregisterButtonPressedCallback( MOUSE_LEFT, OnClick )' + NL,
    '\tDeregisterButtonPressedCallback( MOUSE_LEFT, OnClick )' + NL +
    '\tPS4ModSettings_DeregisterPad() // PS4' + NL +
    # PC writes archived ConVars to the profile when the game shuts down; a
    # PS4 game is usually just closed, so the changed settings are stored now
    # (the engine's own "Store player settings." command).
    '\tClientCommand( "savePlayerConfig" ) // PS4: keep mod settings across restarts' + NL)

PS4_CODE = r'''

// PS4: controller support. Everything below is added by
// scripts/menus/build_mod_settings_override.py; see that file for why.

array<string> function PS4ModSettings_ControlNames()
{
	return [ "EnumSelectButton", "Slider", "TextEntrySetting", "ColorPickerButton", "OpenCustomMenu" ]
}

void function PS4ModSettings_Init()
{
	var search = Hud_GetChild( file.menu, "BtnModsSearch" )
	search.SetNavUp( search )
	search.SetNavDown( search )
	foreach ( var panel in file.modPanels )
	{
		foreach ( string name in PS4ModSettings_ControlNames() )
		{
			var child = Hud_GetChild( panel, name )
			child.SetNavUp( child )
			child.SetNavDown( child )
		}
	}
	AddMenuFooterOption( file.menu, BUTTON_SHOULDER_LEFT, "%[L_SHOULDER|]% Page Up", "", PS4ModSettings_PageUp )
	AddMenuFooterOption( file.menu, BUTTON_SHOULDER_RIGHT, "%[R_SHOULDER|]% Page Down", "", PS4ModSettings_PageDown )
}

void function PS4ModSettings_RegisterPad()
{
	RegisterButtonPressedCallback( BUTTON_DPAD_UP, PS4ModSettings_Up )
	RegisterButtonPressedCallback( STICK1_UP, PS4ModSettings_Up )
	RegisterButtonPressedCallback( BUTTON_DPAD_DOWN, PS4ModSettings_Down )
	RegisterButtonPressedCallback( STICK1_DOWN, PS4ModSettings_Down )
}

void function PS4ModSettings_DeregisterPad()
{
	DeregisterButtonPressedCallback( BUTTON_DPAD_UP, PS4ModSettings_Up )
	DeregisterButtonPressedCallback( STICK1_UP, PS4ModSettings_Up )
	DeregisterButtonPressedCallback( BUTTON_DPAD_DOWN, PS4ModSettings_Down )
	DeregisterButtonPressedCallback( STICK1_DOWN, PS4ModSettings_Down )
}

bool function PS4ModSettings_IsSettingRow( int index )
{
	if ( index < 0 || index >= file.filteredList.len() )
		return false
	ConVarData c = file.filteredList[ index ]
	return !c.isEmptySpace && !c.isModName && !c.isCategoryName
}

// The control a visible row offers to the pad, or null.
var function PS4ModSettings_RowControl( int slot )
{
	if ( slot < 0 || slot >= BUTTONS_PER_PAGE || !PS4ModSettings_IsSettingRow( slot + file.scrollOffset ) )
		return null
	var panel = file.modPanels[ slot ]
	foreach ( string name in PS4ModSettings_ControlNames() )
	{
		var child = Hud_GetChild( panel, name )
		if ( Hud_IsVisible( child ) )
			return child
	}
	return null
}

int function PS4ModSettings_FocusedSlot()
{
	var focus = GetFocus()
	if ( focus == null )
		return -1
	for ( int slot = 0; slot < file.modPanels.len(); slot++ )
	{
		var panel = file.modPanels[ slot ]
		foreach ( string name in PS4ModSettings_ControlNames() )
		{
			if ( Hud_GetChild( panel, name ) == focus )
				return slot
		}
		if ( Hud_GetChild( panel, "ResetModToDefault" ) == focus )
			return slot
	}
	return -1
}

void function PS4ModSettings_ScrollTo( int offset )
{
	int maxOffset = file.filteredList.len() - BUTTONS_PER_PAGE
	if ( offset > maxOffset )
		offset = maxOffset
	if ( offset < 0 )
		offset = 0
	file.scrollOffset = offset
	// Rebuilds the rows and focuses the search box.
	UpdateList()
	UpdateListSliderPosition()
}

bool function PS4ModSettings_FocusIndex( int index )
{
	if ( index < file.scrollOffset || index >= file.scrollOffset + BUTTONS_PER_PAGE )
		return false
	var control = PS4ModSettings_RowControl( index - file.scrollOffset )
	if ( control == null )
		return false
	Hud_SetFocused( control )
	return true
}

void function PS4ModSettings_Move( int direction )
{
	int slot = PS4ModSettings_FocusedSlot()
	if ( slot < 0 && direction < 0 )
		return
	int from = slot < 0 ? file.scrollOffset - 1 : slot + file.scrollOffset
	int target = -1
	for ( int i = from + direction; i >= 0 && i < file.filteredList.len(); i += direction )
	{
		if ( PS4ModSettings_IsSettingRow( i ) )
		{
			target = i
			break
		}
	}
	if ( target < 0 )
	{
		if ( direction < 0 )
		{
			PS4ModSettings_ScrollTo( 0 )
			Hud_SetFocused( Hud_GetChild( file.menu, "BtnModsSearch" ) )
		}
		return
	}
	if ( target < file.scrollOffset )
		PS4ModSettings_ScrollTo( target - 1 )
	else if ( target >= file.scrollOffset + BUTTONS_PER_PAGE )
		PS4ModSettings_ScrollTo( target - BUTTONS_PER_PAGE + 1 )
	PS4ModSettings_FocusIndex( target )
}

void function PS4ModSettings_Up( var button )
{
	PS4ModSettings_Move( -1 )
}

void function PS4ModSettings_Down( var button )
{
	PS4ModSettings_Move( 1 )
}

void function PS4ModSettings_Page( int delta )
{
	PS4ModSettings_ScrollTo( file.scrollOffset + delta )
	for ( int i = file.scrollOffset; i < file.scrollOffset + BUTTONS_PER_PAGE; i++ )
	{
		if ( PS4ModSettings_FocusIndex( i ) )
			return
	}
}

void function PS4ModSettings_PageUp( var button )
{
	PS4ModSettings_Page( -( BUTTONS_PER_PAGE - 1 ) )
}

void function PS4ModSettings_PageDown( var button )
{
	PS4ModSettings_Page( BUTTONS_PER_PAGE - 1 )
}
'''

header = ('// Northstar PS4 override of Northstar.Client/mod/scripts/vscripts/ui/menu_mod_settings.nut.' + NL +
          '// Generated by scripts/menus/build_mod_settings_override.py: Northstar\'s file with' + NL +
          '// controller support added (lines marked PS4). Do not edit by hand.' + NL + NL)
write(os.path.join(PS4, 'scripts', 'vscripts', 'ui', 'menu_mod_settings.nut'),
      header + script.rstrip(NL) + NL + PS4_CODE)

# --- mod_setting.res --------------------------------------------------------
res = read(os.path.join(CLIENT, 'resource', 'ui', 'menus', 'panels', 'mod_setting.res'))
old = ('\t\t"image" "vgui/reset"' + NL + '\t\t"scaleImage" "1"' + NL +
       '\t\t"drawColor" "180 180 180 255" // vanilla label color' + NL +
       '\t\t"visible" "0"' + NL + '\t\t"wide" "30"' + NL + '\t\t"tall" "30"')
new = ('\t\t// PS4: vgui/reset is a PC .vtf the PS4 cannot load; a small dot instead.' + NL +
       '\t\t"image" "vgui/hud/white"' + NL + '\t\t"scaleImage" "1"' + NL +
       '\t\t"drawColor" "180 180 180 255" // vanilla label color' + NL +
       '\t\t"visible" "0"' + NL + '\t\t"wide" "10"' + NL + '\t\t"tall" "10"')
res = replace_once(res, old, new)
write(os.path.join(PS4, 'resource', 'ui', 'menus', 'panels', 'mod_setting.res'),
      '// Northstar PS4 override of Northstar.Client/resource/ui/menus/panels/mod_setting.res.' + NL +
      '// Generated by scripts/menus/build_mod_settings_override.py. Do not edit by hand.' + NL + res)
