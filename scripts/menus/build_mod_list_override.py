"""Produce Northstar.PS4's override of Northstar.Client's Mods list menu.

Run from the repository root after Northstar.Client changes:
    python scripts/menus/build_mod_list_override.py

menu_ns_modmenu.nut is Northstar.Client's file with PS4 additions marked
"PS4". On a pad nothing in the list could be selected: no mod button takes
focus when the menu opens, the buttons have no navigation links (PS4 only
follows explicit links), and the list scrolls with the mouse wheel or the
arrow buttons only. The additions:
  - the first mod on screen is focused when the menu opens;
  - d-pad/stick up and down move between mods, skipping the load-priority
    headers and scrolling at the edges;
  - L1/R1 page the list (footer buttons).
Cross toggles a mod as on PC (the button's own UIE_CLICK handler).
"""
import os

NL = chr(10)
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SOURCE = os.path.join(ROOT, 'vendor', 'NorthstarMods', 'Northstar.Client', 'mod', 'scripts', 'vscripts', 'ui',
                      'menu_ns_modmenu.nut')
TARGET = os.path.join(ROOT, 'mods', 'Northstar.PS4', 'mod', 'scripts', 'vscripts', 'ui', 'menu_ns_modmenu.nut')


def replace_once(text, old, new):
    assert text.count(old) == 1, old[:60]
    return text.replace(old, new, 1)


script = open(SOURCE, encoding='utf-8').read().replace('\r\n', NL)

script = replace_once(script,
    '\tRuiSetString( Hud_GetRui( Hud_GetChild( file.menu, "BtnListReverse" ) ), "buttonText", "" )' + NL + '}',
    '\tRuiSetString( Hud_GetRui( Hud_GetChild( file.menu, "BtnListReverse" ) ), "buttonText", "" )' + NL + NL +
    '\tPS4ModList_Init() // PS4' + NL + '}')

script = replace_once(script,
    '\tRegisterButtonPressedCallback( MOUSE_WHEEL_DOWN, OnScrollDown )' + NL + '}',
    '\tRegisterButtonPressedCallback( MOUSE_WHEEL_DOWN, OnScrollDown )' + NL +
    '\tPS4ModList_RegisterPad() // PS4' + NL +
    '\tthread PS4ModList_FocusFirst() // PS4' + NL + '}')

script = replace_once(script,
    '\t\tDeregisterButtonPressedCallback( MOUSE_WHEEL_DOWN, OnScrollDown )' + NL,
    '\t\tDeregisterButtonPressedCallback( MOUSE_WHEEL_DOWN, OnScrollDown )' + NL +
    '\t\tPS4ModList_DeregisterPad() // PS4' + NL)

PS4_CODE = r'''

// PS4: controller support. Everything below is added by
// scripts/menus/build_mod_list_override.py; see that file for why.

void function PS4ModList_Init()
{
	foreach ( var panel in file.panels )
	{
		var button = Hud_GetChild( panel, "BtnMod" )
		button.SetNavUp( button )
		button.SetNavDown( button )
	}
	AddMenuFooterOption( file.menu, BUTTON_SHOULDER_LEFT, "%[L_SHOULDER|]% Page Up", "", PS4ModList_PageUp )
	AddMenuFooterOption( file.menu, BUTTON_SHOULDER_RIGHT, "%[R_SHOULDER|]% Page Down", "", PS4ModList_PageDown )
}

void function PS4ModList_RegisterPad()
{
	RegisterButtonPressedCallback( BUTTON_DPAD_UP, PS4ModList_Up )
	RegisterButtonPressedCallback( STICK1_UP, PS4ModList_Up )
	RegisterButtonPressedCallback( BUTTON_DPAD_DOWN, PS4ModList_Down )
	RegisterButtonPressedCallback( STICK1_DOWN, PS4ModList_Down )
}

void function PS4ModList_DeregisterPad()
{
	DeregisterButtonPressedCallback( BUTTON_DPAD_UP, PS4ModList_Up )
	DeregisterButtonPressedCallback( STICK1_UP, PS4ModList_Up )
	DeregisterButtonPressedCallback( BUTTON_DPAD_DOWN, PS4ModList_Down )
	DeregisterButtonPressedCallback( STICK1_DOWN, PS4ModList_Down )
}

bool function PS4ModList_IsMod( int index )
{
	return index >= 0 && index < file.mods.len() && !file.mods[ index ].isHeader
}

int function PS4ModList_FocusedSlot()
{
	var focus = GetFocus()
	for ( int slot = 0; slot < file.panels.len(); slot++ )
	{
		if ( Hud_GetChild( file.panels[ slot ], "BtnMod" ) == focus )
			return slot
	}
	return -1
}

bool function PS4ModList_FocusIndex( int index )
{
	if ( !PS4ModList_IsMod( index ) || index < file.scrollOffset || index >= file.scrollOffset + PANELS_LEN )
		return false
	Hud_SetFocused( Hud_GetChild( file.panels[ index - file.scrollOffset ], "BtnMod" ) )
	return true
}

void function PS4ModList_ScrollTo( int offset )
{
	file.scrollOffset = offset
	// Clamps the offset and redraws the rows.
	ValidateScrollOffset()
}

void function PS4ModList_FocusFirstVisible()
{
	for ( int i = file.scrollOffset; i < file.scrollOffset + PANELS_LEN; i++ )
	{
		if ( PS4ModList_FocusIndex( i ) )
			return
	}
}

// The engine gives the menu its previous focus after MENU_OPEN; wait a frame.
void function PS4ModList_FocusFirst()
{
	WaitFrame()
	if ( PS4ModList_FocusedSlot() < 0 )
		PS4ModList_FocusFirstVisible()
}

void function PS4ModList_Move( int direction )
{
	int slot = PS4ModList_FocusedSlot()
	if ( slot < 0 )
	{
		PS4ModList_FocusFirstVisible()
		return
	}
	int target = -1
	for ( int i = slot + file.scrollOffset + direction; i >= 0 && i < file.mods.len(); i += direction )
	{
		if ( PS4ModList_IsMod( i ) )
		{
			target = i
			break
		}
	}
	if ( target < 0 )
		return
	if ( target < file.scrollOffset )
		PS4ModList_ScrollTo( target - 1 ) // keep its header in view
	else if ( target >= file.scrollOffset + PANELS_LEN )
		PS4ModList_ScrollTo( target - PANELS_LEN + 1 )
	PS4ModList_FocusIndex( target )
}

void function PS4ModList_Up( var button )
{
	PS4ModList_Move( -1 )
}

void function PS4ModList_Down( var button )
{
	PS4ModList_Move( 1 )
}

void function PS4ModList_PageUp( var button )
{
	PS4ModList_ScrollTo( file.scrollOffset - ( PANELS_LEN - 1 ) )
	PS4ModList_FocusFirstVisible()
}

void function PS4ModList_PageDown( var button )
{
	PS4ModList_ScrollTo( file.scrollOffset + ( PANELS_LEN - 1 ) )
	PS4ModList_FocusFirstVisible()
}
'''

header = ('// Northstar PS4 override of Northstar.Client/mod/scripts/vscripts/ui/menu_ns_modmenu.nut.' + NL +
          '// Generated by scripts/menus/build_mod_list_override.py: Northstar\'s file with' + NL +
          '// controller support added (lines marked PS4). Do not edit by hand.' + NL + NL)
os.makedirs(os.path.dirname(TARGET), exist_ok=True)
open(TARGET, 'w', encoding='utf-8', newline=NL).write(header + script.rstrip(NL) + NL + PS4_CODE)
print('wrote', os.path.relpath(TARGET, ROOT))
