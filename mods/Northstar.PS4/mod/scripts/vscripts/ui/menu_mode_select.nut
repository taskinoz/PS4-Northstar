untyped
global function InitModesMenu
global function NSSetModeCategory

global enum eModeMenuModeCategory
{
	UNKNOWN = 0,
	PVPVE = 1,
	PVE = 2,
	PVP = 3,
	FFA = 4,
	TITAN = 5,
	OTHER = 6,
	CUSTOM = 7

	SIZE
}

// List of blocked modes due to them being unfinished
#if VANILLA
	const array<string> blockedModes = []
#else
	const array<string> blockedModes = [ "fd_easy", "fd_normal", "fd_hard", "fd_master", "fd_insane" ]
#endif

struct ListEntry_t
{
	string mode
	int category
}

// Slider mouse delta buffer
struct
{
	int deltaX = 0
	int deltaY = 0
} mouseDeltaBuffer

struct
{
	int scrollOffset
	var menu

	string searchString
	int searchEnum

	// Table of category overrides
	table<string, int> categoryOverrides

	// List of all modes we know
	array<ListEntry_t> modes

	// Sorted list of modes we want to show with categories included
	array<string> sortedModes

	// PS4: the list row (1-based scriptID) that last had focus, for controller scrolling
	int focusedSlot = 0
	float focusTime = -1.0
} file

const int MODES_PER_PAGE = 15

void function InitModesMenu()
{
	file.menu = GetMenu( "ModesMenu" )

	AddMouseMovementCaptureHandler( Hud_GetChild( file.menu, "MouseMovementCapture" ), UpdateMouseDeltaBuffer )

	AddMenuEventHandler( file.menu, eUIEvent.MENU_CLOSE, OnCloseModesMenu )
	AddMenuEventHandler( file.menu, eUIEvent.MENU_OPEN, OnOpenModesMenu )
	AddButtonEventHandler( Hud_GetChild( file.menu, "BtnModeListUpArrow" ), UIE_CLICK, OnUpArrowSelected )
	AddButtonEventHandler( Hud_GetChild( file.menu, "BtnModeListDownArrow" ), UIE_CLICK, OnDownArrowSelected )

	AddButtonEventHandler( Hud_GetChild( file.menu, "BtnModeLabel" ), UIE_CHANGE, FilterAndUpdateList )
	AddButtonEventHandler( Hud_GetChild( file.menu, "BtnModeSearch" ), UIE_CHANGE, FilterAndUpdateList )
	AddButtonEventHandler( Hud_GetChild( file.menu, "SwtModeLabel" ), UIE_CHANGE, FilterAndUpdateList )

	AddButtonEventHandler( Hud_GetChild( file.menu, "BtnModeFiltersClear" ), UIE_CLICK, OnBtnFiltersClear_Activate )

	array<var> buttons = GetElementsByClassname( file.menu, "ModeSelectorPanel" )
	foreach ( var panel in buttons )
	{
		AddEventHandlerToButton( panel, "BtnMode", UIE_GET_FOCUS, ModeButton_GetFocus )
		AddEventHandlerToButton( panel, "BtnMode", UIE_CLICK, ModeButton_Click )
	}

	Hud_SetText( Hud_GetChild( file.menu, "SwtModeLabel" ), "#MODE_MENU_SWITCH" )
	SetButtonRuiText( Hud_GetChild( file.menu, "SwtModeLabel" ), "" )
	Hud_DialogList_AddListItem( Hud_GetChild( file.menu, "SwtModeLabel" ), "#MODE_MENU_ALL", "-1" )
	for ( int i = 0; i < eModeMenuModeCategory.SIZE; i++ )
	{
		Hud_DialogList_AddListItem( Hud_GetChild( file.menu, "SwtModeLabel" ), GetCategoryStringFromEnum( i ), string( i ) )
	}

	AddMenuFooterOption( file.menu, BUTTON_A, "#A_BUTTON_SELECT" )
	AddMenuFooterOption( file.menu, BUTTON_B, "#B_BUTTON_BACK", "#BACK" )
	// PS4: shoulder buttons page the list, as the mouse wheel does. Literal text:
	// mod localisation files do not load on this port yet, and the retail
	// #LB_BUTTON_BROWSE_PREVPAGE form ("%[L_SHOULDER]%", no pipe) renders blank here.
	AddMenuFooterOption( file.menu, BUTTON_SHOULDER_LEFT, "%[L_SHOULDER|]% Prev Page", "" )
	AddMenuFooterOption( file.menu, BUTTON_SHOULDER_RIGHT, "%[R_SHOULDER|]% Next Page", "" )

	// PS4: record focus arriving on the side column too, so the controller
	// handlers can tell a press the engine already acted on this frame
	foreach ( string name in [ "BtnModeSearch", "SwtModeLabel", "BtnModeFiltersClear" ] )
		AddButtonEventHandler( Hud_GetChild( file.menu, name ), UIE_GET_FOCUS, OnSideColumnFocus )
}

void function OnSideColumnFocus( var button )
{
	file.focusTime = Time() // PS4
}

void function NSSetModeCategory( string mode, int category )
{
	if ( mode in file.categoryOverrides )
	{
		file.categoryOverrides[ mode ] = category
		printt( "Overwriting category for mode:", mode )
		return
	}

	file.categoryOverrides[ mode ] <- category
}

void function OnBtnFiltersClear_Activate( var b )
{
	file.searchString = ""
	file.searchEnum = -1

	SetConVarInt( "modemenu_mode_filter", -1 )
	Hud_SetText( Hud_GetChild( file.menu, "BtnModeSearch" ), "" )

	file.scrollOffset = 0

	BuildSortedModesArray()
	UpdateListSliderHeight( float( file.sortedModes.len() ) )
	UpdateListSliderPosition( file.sortedModes.len() )
	UpdateVisibleModes()
}

void function FilterAndUpdateList( var n )
{
	file.searchString = Hud_GetUTF8Text( Hud_GetChild( file.menu, "BtnModeSearch" ) )
	file.searchEnum = GetConVarInt( "modemenu_mode_filter" )

	file.scrollOffset = 0

	BuildSortedModesArray()
	UpdateListSliderHeight( float( file.sortedModes.len() ) )
	UpdateListSliderPosition( file.sortedModes.len() )
	UpdateVisibleModes()
}

void function OnOpenModesMenu()
{
	RegisterButtonPressedCallback( MOUSE_WHEEL_UP, OnScrollUp )
	RegisterButtonPressedCallback( MOUSE_WHEEL_DOWN, OnScrollDown )
	// PS4: controller scrolling
	RegisterButtonPressedCallback( BUTTON_SHOULDER_LEFT, OnControllerPageUp )
	RegisterButtonPressedCallback( BUTTON_SHOULDER_RIGHT, OnControllerPageDown )
	RegisterButtonPressedCallback( BUTTON_DPAD_UP, OnControllerUp )
	RegisterButtonPressedCallback( STICK1_UP, OnControllerUp )
	RegisterButtonPressedCallback( BUTTON_DPAD_DOWN, OnControllerDown )
	RegisterButtonPressedCallback( STICK1_DOWN, OnControllerDown )
	RegisterButtonPressedCallback( BUTTON_DPAD_LEFT, OnControllerLeft )
	RegisterButtonPressedCallback( STICK1_LEFT, OnControllerLeft )
	RegisterButtonPressedCallback( BUTTON_DPAD_RIGHT, OnControllerRight )
	RegisterButtonPressedCallback( STICK1_RIGHT, OnControllerRight )
	file.focusedSlot = 0

	// Reset filters
	file.searchString = ""
	file.searchEnum = -1

	// We rebuild the modes array on open menu to make sure
	// all modes get listed
	BuildModesArray()
	BuildSortedModesArray()

	UpdateListSliderHeight( float( file.sortedModes.len() ) )
	UpdateListSliderPosition( file.sortedModes.len() )
	UpdateVisibleModes()

	// Set to the first mode if there's no mode focused
	if ( level.ui.privatematch_mode == 0 )
	{
		array<var> panels = GetElementsByClassname( file.menu, "ModeSelectorPanel" )
		foreach ( var panel in panels )
		{
			if ( Hud_IsEnabled( Hud_GetChild( panel, "BtnMode" ) ) )
			{
				Hud_SetFocused( Hud_GetChild( panel, "BtnMode" ) )
				break
			}
		}
	}

	// PS4: the engine restores the menu's previous focus after this handler,
	// and the list keeps its old scroll position, so on reopening the
	// highlighted row and the focused one could differ (down did nothing).
	thread FocusSelectedModeOnOpen()
}

// PS4: one frame after opening, scroll the selected mode into the middle of the
// list and focus it, so focus, highlight and scroll position agree.
void function FocusSelectedModeOnOpen()
{
	WaitFrame()
	if ( GetActiveMenu() != file.menu || file.sortedModes.len() == 0 )
		return
	string selected = ""
	try
	{
		selected = PrivateMatch_GetSelectedMode()
	}
	catch ( ex )
	{
	}
	int index = file.sortedModes.find( selected )
	if ( index < 0 )
		index = 0
	int maxOffset = file.sortedModes.len() - MODES_PER_PAGE
	if ( maxOffset < 0 )
		maxOffset = 0
	int offset = index - MODES_PER_PAGE / 2
	if ( offset > maxOffset )
		offset = maxOffset
	if ( offset < 0 )
		offset = 0
	file.scrollOffset = offset
	UpdateVisibleModes()
	UpdateListSliderPosition( file.sortedModes.len() )
	FocusSlot( index - offset + 1, 1 )
}

void function OnCloseModesMenu()
{
	try
	{
		DeregisterButtonPressedCallback( MOUSE_WHEEL_UP, OnScrollUp )
		DeregisterButtonPressedCallback( MOUSE_WHEEL_DOWN, OnScrollDown )
		DeregisterButtonPressedCallback( BUTTON_SHOULDER_LEFT, OnControllerPageUp )
		DeregisterButtonPressedCallback( BUTTON_SHOULDER_RIGHT, OnControllerPageDown )
		DeregisterButtonPressedCallback( BUTTON_DPAD_UP, OnControllerUp )
		DeregisterButtonPressedCallback( STICK1_UP, OnControllerUp )
		DeregisterButtonPressedCallback( BUTTON_DPAD_DOWN, OnControllerDown )
		DeregisterButtonPressedCallback( STICK1_DOWN, OnControllerDown )
		DeregisterButtonPressedCallback( BUTTON_DPAD_LEFT, OnControllerLeft )
		DeregisterButtonPressedCallback( STICK1_LEFT, OnControllerLeft )
		DeregisterButtonPressedCallback( BUTTON_DPAD_RIGHT, OnControllerRight )
		DeregisterButtonPressedCallback( STICK1_RIGHT, OnControllerRight )
	}
	catch ( ex )
	{
	}
}

// PS4: controller support. The list shows MODES_PER_PAGE rows and scrolls
// only with the mouse wheel or the slider, so on a controller every mode past
// the first page was unreachable. Moving past the last row (or above the first,
// while there is more above) now scrolls by one and keeps focus on that row,
// the way the server browser's dummy buttons do; the shoulder buttons page.
var function ModeButtonForSlot( int slot )
{
	foreach ( var panel in GetElementsByClassname( file.menu, "ModeSelectorPanel" ) )
	{
		if ( int( Hud_GetScriptID( panel ) ) == slot )
			return Hud_GetChild( panel, "BtnMode" )
	}
	return null
}

int function VisibleSlotCount()
{
	int remaining = file.sortedModes.len() - file.scrollOffset
	return remaining < MODES_PER_PAGE ? remaining : MODES_PER_PAGE
}

// Re-focuses a row after the list moved under it. Waits a frame so it runs
// after the engine's own navigation for the same press.
void function FocusSlotAfterScroll( int slot, int direction )
{
	WaitFrame()
	if ( GetActiveMenu() != file.menu )
		return
	FocusSlot( slot, direction )
}

// Focuses a row. A category header's button is disabled, so this steps toward
// the first enabled row in `direction`.
void function FocusSlot( int slot, int direction )
{
	int count = VisibleSlotCount()
	for ( int tries = 0; tries < count; tries++ )
	{
		if ( slot < 1 || slot > count )
			break
		var button = ModeButtonForSlot( slot )
		if ( button != null && Hud_IsEnabled( button ) )
		{
			Hud_SetFocused( button )
			ModeButton_GetFocus( button )
			return
		}
		slot += direction
	}
}

// On PS4 the engine moves focus with the d-pad only along explicit
// navUp/navDown/navLeft/navRight links in the .menu file (the Direct Connect
// menu needed them to work on a pad). The mode rows are nested panels with no
// links, so a pad could not move between them at all, and nothing links the list
// to the search/filter column either. Movement is therefore done here: through
// the list, scrolling at its edges; through the column; and between the two.
void function OnControllerRight( var button )
{
	if ( file.focusTime == Time() )
		return
	if ( FocusedListSlot() != 0 )
		Hud_SetFocused( Hud_GetChild( file.menu, "BtnModeSearch" ) )
}

void function OnControllerLeft( var button )
{
	if ( file.focusTime == Time() )
		return
	// The filter switch may use left/right to change its value, so it keeps them.
	var focus = GetFocus()
	if ( focus != Hud_GetChild( file.menu, "BtnModeSearch" ) && focus != Hud_GetChild( file.menu, "BtnModeLabel" ) &&
		focus != Hud_GetChild( file.menu, "BtnModeFiltersClear" ) )
		return
	int slot = file.focusedSlot
	int count = VisibleSlotCount()
	if ( slot < 1 || slot > count )
		slot = 1
	FocusSlot( slot, 1 )
	if ( FocusedListSlot() == 0 )
		FocusSlot( slot, -1 )
}

void function OnControllerDown( var button )
{
	MoveVertically( 1 )
}

void function OnControllerUp( var button )
{
	MoveVertically( -1 )
}

void function MoveVertically( int direction )
{
	// Guard in case the engine does navigate for some control: focus that
	// already moved this frame was that navigation, not a press to handle.
	if ( file.focusTime == Time() )
		return
	int slot = FocusedListSlot()
	if ( slot == 0 )
	{
		MoveInSideColumn( direction )
		return
	}
	int next = NextEnabledSlot( slot, direction )
	if ( next != 0 )
	{
		FocusSlot( next, direction )
		return
	}
	// At the edge of the visible rows: scroll one row at a time until an
	// enabled row (a mode, not a category header) reaches that edge.
	for ( int guard = 0; guard < MODES_PER_PAGE; guard++ )
	{
		if ( direction > 0 && file.scrollOffset + MODES_PER_PAGE >= file.sortedModes.len() )
			return
		if ( direction < 0 && file.scrollOffset == 0 )
			return
		if ( direction > 0 )
			OnDownArrowSelected( null )
		else
			OnUpArrowSelected( null )
		int edge = direction > 0 ? VisibleSlotCount() : 1
		var edgeButton = ModeButtonForSlot( edge )
		if ( edgeButton != null && Hud_IsEnabled( edgeButton ) )
		{
			FocusSlot( edge, direction )
			return
		}
	}
}

// The list row (1-based) that has focus, or 0 when focus is elsewhere.
int function FocusedListSlot()
{
	var focus = GetFocus()
	if ( focus == null )
		return 0
	int count = VisibleSlotCount()
	for ( int slot = 1; slot <= count; slot++ )
	{
		if ( ModeButtonForSlot( slot ) == focus )
			return slot
	}
	return 0
}

// The next visible row after `slot` in `direction` holding a mode, or 0.
int function NextEnabledSlot( int slot, int direction )
{
	int count = VisibleSlotCount()
	for ( int next = slot + direction; next >= 1 && next <= count; next += direction )
	{
		var button = ModeButtonForSlot( next )
		if ( button != null && Hud_IsEnabled( button ) )
			return next
	}
	return 0
}

// The search box, filter switch and clear button stack vertically.
void function MoveInSideColumn( int direction )
{
	array<var> column = [
		Hud_GetChild( file.menu, "BtnModeSearch" ),
		Hud_GetChild( file.menu, "SwtModeLabel" ),
		Hud_GetChild( file.menu, "BtnModeFiltersClear" )
	]
	var focus = GetFocus()
	if ( focus == Hud_GetChild( file.menu, "BtnModeLabel" ) )
		focus = column[0]
	int index = column.find( focus )
	if ( index < 0 )
		return
	int next = index + direction
	if ( next >= 0 && next < column.len() )
		Hud_SetFocused( column[next] )
}

void function OnControllerPageDown( var button )
{
	int slot = file.focusedSlot > 0 ? file.focusedSlot : 1
	OnScrollDown( null )
	thread FocusSlotAfterScroll( slot, -1 )
}

void function OnControllerPageUp( var button )
{
	int slot = file.focusedSlot > 0 ? file.focusedSlot : 1
	OnScrollUp( null )
	thread FocusSlotAfterScroll( slot, 1 )
}

string function GetCategoryStringFromEnum( int category )
{
	switch ( category )
	{
		case eModeMenuModeCategory.PVPVE:
			return "#MODE_MENU_PVPVE"

		case eModeMenuModeCategory.PVE:
			return "#MODE_MENU_PVE"

		case eModeMenuModeCategory.PVP:
			return "#MODE_MENU_PVP"

		case eModeMenuModeCategory.FFA:
			return "#MODE_MENU_FFA"

		case eModeMenuModeCategory.TITAN:
			return "#MODE_MENU_TITAN_ONLY"

		case eModeMenuModeCategory.OTHER:
			return "#MODE_MENU_OTHER"

		case eModeMenuModeCategory.CUSTOM:
			return "#MODE_MENU_CUSTOM"
	}

	return "#MODE_MENU_UNKNOWN"
}

void function BuildModesArray()
{
	file.modes.clear()

	foreach ( string mode in GetPrivateMatchModes() )
	{
		ListEntry_t entry
		entry.mode = mode
		entry.category = eModeMenuModeCategory.UNKNOWN

		switch ( mode )
		{
			case "aitdm":
			case "at":
				entry.category = eModeMenuModeCategory.PVPVE
				break

			case "fd_easy":
			case "fd_normal":
			case "fd_hard":
			case "fd_master":
			case "fd_insane":
				entry.category = eModeMenuModeCategory.PVE
				break

			case "tdm":
			case "ctf":
			case "mfd":
			case "ps":
			case "cp":
			case "speedball":
			case "rocket_lf":
			case "holopilot_lf":
				entry.category = eModeMenuModeCategory.PVP
				break

			case "ffa":
			case "fra":
				entry.category = eModeMenuModeCategory.FFA
				break

			case "lts":
			case "ttdm":
			case "attdm":
			case "turbo_ttdm":
			case "alts":
			case "turbo_lts":
				entry.category = eModeMenuModeCategory.TITAN
				break

			case "coliseum":
			case "sp_coop":
				entry.category = eModeMenuModeCategory.OTHER
				break

			case "chamber":
			case "hidden":
			case "sns":
			case "fw":
			case "gg":
			case "tt":
			case "inf":
			case "kr":
			case "fastball":
			case "hs":
			case "ctf_comp":
			case "tffa":
				entry.category = eModeMenuModeCategory.CUSTOM
				break
		}

		file.modes.append( entry )
	}
}

int function SortModesAlphabetize( string a, string b )
{
	a = Localize( GetGameModeDisplayName( a ) )
	b = Localize( GetGameModeDisplayName( b ) )

	if ( a > b )
		return 1

	if ( a < b )
		return -1

	return 0
}

void function BuildSortedModesArray()
{
	file.sortedModes.clear()

	// Build sorted list of categories
	array<string> categories
	for ( int i = 0; i < eModeMenuModeCategory.SIZE; i++ )
	{
		if ( file.searchEnum != -1 && file.searchEnum != i )
			continue

		categories.append( GetCategoryStringFromEnum( i ) )
	}

	categories.sort( SortStringAlphabetize )

	// Build final list of mixed modes and categories
	foreach ( string category in categories )
	{
		// Build sorted list of modes in category
		array<string> modes
		foreach ( ListEntry_t entry in file.modes )
		{
			int iCategory = entry.category
			if ( entry.mode in file.categoryOverrides )
				iCategory = file.categoryOverrides[ entry.mode ]

			if ( GetCategoryStringFromEnum( iCategory ) != category )
				continue

			string mode = entry.mode

			if ( file.searchString != "" && Localize( GetGameModeDisplayName( mode ) ).tolower().find( file.searchString.tolower() ) == null )
				continue

			if ( !modes.contains( mode ) )
				modes.append( mode )
		}

		modes.sort( SortModesAlphabetize )

		if ( modes.len() == 0 )
			continue

		// Add to final list we then display
		file.sortedModes.append( category )
		foreach ( string mode in modes )
			file.sortedModes.append( mode )
	}
}

// //////////////////////////
// Slider
// //////////////////////////
void function UpdateMouseDeltaBuffer( int x, int y )
{
	mouseDeltaBuffer.deltaX += x
	mouseDeltaBuffer.deltaY += y

	SliderBarUpdate()
}

void function FlushMouseDeltaBuffer()
{
	mouseDeltaBuffer.deltaX = 0
	mouseDeltaBuffer.deltaY = 0
}

void function SliderBarUpdate()
{
	if ( file.sortedModes.len() < MODES_PER_PAGE )
		return

	var sliderButton = Hud_GetChild( file.menu, "BtnModeListSlider" )
	var sliderPanel = Hud_GetChild( file.menu, "BtnModeListSliderPanel" )
	var movementCapture = Hud_GetChild( file.menu, "MouseMovementCapture" )

	Hud_SetFocused( sliderButton )

	int[ 2 ] screenSize = GetScreenSize()
	float minYPos = -40.0 * ( screenSize[ 1 ] / 1080.0 )
	float maxHeight = 596.0 * ( screenSize[ 1 ] / 1080.0 )
	float maxYPos = minYPos - ( maxHeight - Hud_GetHeight( sliderPanel ) )
	float useableSpace = maxHeight - Hud_GetHeight( sliderPanel )

	float jump = minYPos - ( useableSpace / ( float( file.sortedModes.len() ) ) )

	// got local from official respaw scripts, without untyped throws an error
	local pos = Hud_GetPos( sliderButton )[ 1 ]
	local newPos = pos - mouseDeltaBuffer.deltaY
	FlushMouseDeltaBuffer()

	if ( newPos < maxYPos )
		newPos = maxYPos
	if ( newPos > minYPos )
		newPos = minYPos

	Hud_SetPos( sliderButton, 2, newPos )
	Hud_SetPos( sliderPanel, 2, newPos )
	Hud_SetPos( movementCapture, 2, newPos )

	file.scrollOffset = -int( ( ( newPos - minYPos ) / useableSpace ) * ( file.sortedModes.len() - MODES_PER_PAGE ) )
	UpdateVisibleModes()
}

void function UpdateListSliderHeight( float modes )
{
	var sliderButton = Hud_GetChild( file.menu, "BtnModeListSlider" )
	var sliderPanel = Hud_GetChild( file.menu, "BtnModeListSliderPanel" )
	var movementCapture = Hud_GetChild( file.menu, "MouseMovementCapture" )

	int[ 2 ] screenSize = GetScreenSize()
	float maxHeight = 596.0 * ( screenSize[ 1 ] / 1080.0 )
	float minHeight = 80.0 * ( screenSize[ 1 ] / 1080.0 )

	float height = maxHeight * ( MODES_PER_PAGE / modes )

	if ( height > maxHeight )
		height = maxHeight
	if ( height < minHeight )
		height = minHeight

	Hud_SetHeight( sliderButton, height )
	Hud_SetHeight( sliderPanel, height )
	Hud_SetHeight( movementCapture, height )
}

void function UpdateListSliderPosition( int modes )
{
	if ( modes < MODES_PER_PAGE )
		return

	var sliderButton = Hud_GetChild( file.menu, "BtnModeListSlider" )
	var sliderPanel = Hud_GetChild( file.menu, "BtnModeListSliderPanel" )
	var movementCapture = Hud_GetChild( file.menu, "MouseMovementCapture" )

	float minYPos = -40.0 * ( GetScreenSize()[ 1 ] / 1080.0 )
	float useableSpace = ( 596.0 * ( GetScreenSize()[ 1 ] / 1080.0 ) - Hud_GetHeight( sliderPanel ) )

	float jump = minYPos - ( useableSpace / ( float( modes ) - MODES_PER_PAGE ) * file.scrollOffset )

	if ( jump > minYPos )
		jump = minYPos

	Hud_SetPos( sliderButton, 2, jump )
	Hud_SetPos( sliderPanel, 2, jump )
	Hud_SetPos( movementCapture, 2, jump )
}

void function OnScrollDown( var button )
{
	if ( file.sortedModes.len() <= MODES_PER_PAGE )
		return
	file.scrollOffset += 5
	if ( file.scrollOffset + MODES_PER_PAGE > file.sortedModes.len() )
	{
		file.scrollOffset = file.sortedModes.len() - MODES_PER_PAGE
	}
	UpdateVisibleModes()
	UpdateListSliderPosition( file.sortedModes.len() )
}

void function OnScrollUp( var button )
{
	file.scrollOffset -= 5
	if ( file.scrollOffset < 0 )
	{
		file.scrollOffset = 0
	}
	UpdateVisibleModes()
	UpdateListSliderPosition( file.sortedModes.len() )
}

void function OnDownArrowSelected( var button )
{
	if ( file.sortedModes.len() <= MODES_PER_PAGE )
		return
	file.scrollOffset += 1
	if ( file.scrollOffset + MODES_PER_PAGE > file.sortedModes.len() )
	{
		file.scrollOffset = file.sortedModes.len() - MODES_PER_PAGE
	}

	UpdateVisibleModes()
	UpdateListSliderPosition( file.sortedModes.len() )
}

void function OnUpArrowSelected( var button )
{
	file.scrollOffset -= 1
	if ( file.scrollOffset < 0 )
	{
		file.scrollOffset = 0
	}

	UpdateVisibleModes()
	UpdateListSliderPosition( file.sortedModes.len() )
}

bool function IsStringCategory( string str )
{
	return GetGameModeDisplayName( str ) == ""
}

// ///////////////////////////
// LIST
// ///////////////////////////

void function UpdateVisibleModes()
{
	// ensures that we only ever show enough buttons for the number of modes we have
	array<var> buttons = GetElementsByClassname( GetMenu( "ModesMenu" ), "ModeSelectorPanel" )
	foreach ( var panel in buttons )
	{
		Hud_SetEnabled( panel, false )
		Hud_SetVisible( panel, false )
		Hud_SetText( Hud_GetChild( panel, "Header" ), "" )
		Hud_SetText( Hud_GetChild( panel, "BtnMode" ), "" )
		SetButtonRuiText( Hud_GetChild( panel, "BtnMode" ), "" )
	}

	for ( int i = 0; i < MODES_PER_PAGE; i++ )
	{
		if ( i + file.scrollOffset >= file.sortedModes.len() )
			break

		// Setup locals
		var panel = buttons[ i ]
		var button = Hud_GetChild( panel, "BtnMode" )
		var header = Hud_GetChild( panel, "Header" )

		int modeIndex = i + file.scrollOffset
		string mode = file.sortedModes[ modeIndex ]

		bool bIsCategory = IsStringCategory( mode )
		mode = bIsCategory ? mode : GetGameModeDisplayName( mode )

		// Show the panel
		Hud_SetEnabled( panel, true )
		Hud_SetVisible( panel, true )
		Hud_SetLocked( button, false )

		if ( bIsCategory )
		{
			Hud_SetText( header, mode )
			Hud_SetEnabled( button, false )
		}
		else
		{
			Hud_SetEnabled( button, true )
			SetButtonRuiText( button, mode )

			if ( blockedModes.contains( file.sortedModes[ modeIndex ] ) )
				Hud_SetLocked( button, true )

			if ( PrivateMatch_IsValidMapModeCombo( PrivateMatch_GetSelectedMap(), mode ) )
			{
				Hud_SetLocked( button, true )
				SetButtonRuiText( button, mode )
			}
		}
	}
}

void function ModeButton_GetFocus( var button )
{
	file.focusedSlot = int( Hud_GetScriptID( Hud_GetParent( button ) ) ) // PS4
	file.focusTime = Time() // PS4
	int modeId = int( Hud_GetScriptID( Hud_GetParent( button ) ) ) + file.scrollOffset - 1

	var nextModeImage = Hud_GetChild( file.menu, "NextModeImage" )
	var nextModeIcon = Hud_GetChild( file.menu, "ModeIconImage" )
	var nextModeName = Hud_GetChild( file.menu, "NextModeName" )
	var nextModeDesc = Hud_GetChild( file.menu, "NextModeDesc" )

	if ( modeId > file.sortedModes.len() )
		return

	string modeName = file.sortedModes[ modeId ]

	asset playlistImage = GetPlaylistImage( modeName )
	RuiSetImage( Hud_GetRui( nextModeImage ), "basicImage", playlistImage )
	RuiSetImage( Hud_GetRui( nextModeIcon ), "basicImage", GetPlaylistThumbnailImage( modeName ) )
	Hud_SetText( nextModeName, GetGameModeDisplayName( modeName ) )

	string mapName = PrivateMatch_GetSelectedMap()
	bool mapSupportsMode = PrivateMatch_IsValidMapModeCombo( mapName, modeName )
	if ( IsFDMode( modeName ) ) // HACK!
		Hud_SetText( nextModeDesc, Localize( "#FD_PLAYERS_DESC", Localize( GetGameModeDisplayHint( modeName ) ) ) )
	else
		Hud_SetText( nextModeDesc, GetGameModeDisplayHint( modeName ) )
}

void function ModeButton_Click( var button )
{
	// this never activates on custom servers, but keeping it for parity with official
	if ( !AmIPartyLeader() && GetPartySize() > 1 )
		return

	if ( Hud_IsLocked( button ) )
		return

	int modeID = int( Hud_GetScriptID( Hud_GetParent( button ) ) ) + file.scrollOffset - 1

	string modeName = file.sortedModes[ modeID ]

	// on modded servers set us to the first map for that mode automatically
	// need this for coliseum mainly which is literally impossible to select without this
	if ( !PrivateMatch_IsValidMapModeCombo( PrivateMatch_GetSelectedMap(), modeName ) )
	{
		ClientCommand( "SetCustomMap " + GetPrivateMatchMapsForMode( modeName )[ 0 ] )
	}
	// set it
	ClientCommand( "PrivateMatchSetMode " + modeName )
	CloseActiveMenu()
}
