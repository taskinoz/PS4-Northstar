// Northstar.PS4: the in-match text chat panel. The console HUD only defines
// IngameTextChat for [$WINDOWS], so on PS4 SayText messages had no "match"
// chat panel to go to (Northstar.Client also looks it up by name).
Resource/UI/HudScripted_mp.res
{
	IngameTextChat [$GAMECONSOLE]
	{
		ControlName				CBaseHudChat
		InheritProperties		ChatBox

		destination				"match"

		visible 				0

		pin_to_sibling			Screen
		pin_corner_to_sibling	TOP_LEFT
		pin_to_sibling_corner	TOP_LEFT
		xpos					-45
		ypos					-616
	}
}
