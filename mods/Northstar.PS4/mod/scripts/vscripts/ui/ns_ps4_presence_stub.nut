global function NSUpdateGameStateUIStart
global function NSUpdateGameStateClientStart

// PS4 Northstar.PS4 stub: NSUpdateGameStateUIStart / NSUpdateGameStateClientStart
// are Northstar.Client's UICallback.After / ClientCallback.After hooks for
// presence/ui_presence.nut and presence/cl_presence.nut respectively (Discord
// Rich Presence integration). Neither is defined in any Northstar.Client
// script -- confirmed by grep across the real PC mod's whole scripts/vscripts
// tree -- so they must be native, registered by a NorthstarLauncher C++
// presence module this port doesn't implement (not found in
// tools/NorthstarLauncher-reference either, so possibly newer than that
// checkout). Referencing an undefined identifier in a Before/After hook the
// generated PS4 mod hook dispatch calls is a boot-time compile error the
// same as any other undefined variable ("FatalError: ui/_menus.nut: UI
// SCRIPT COMPILE ERROR: Undefined variable "NSUpdateGameStateUIStart""),
// confirmed live 2026-08-15.
//
// Both stubbed here as no-ops. This port has no Discord (or other rich
// presence) integration at all, so there's nothing meaningful for these to
// do; if that ever changes, implement the real native presence bridge in
// launcher/src/runtime.cpp instead of expanding these.
void function NSUpdateGameStateUIStart()
{
}

void function NSUpdateGameStateClientStart()
{
}
