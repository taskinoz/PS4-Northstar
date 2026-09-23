global function NSPS4_ClientCommand

// PS4 Northstar.PS4 bridge for NSConnectToAuthedServer.
//
// On PC that native sets serverfilter to the Atlas auth token and then queues
// `connect ip:port` straight into the engine's command buffer. This port has no
// working native command dispatch (the profiled address sends commands to the
// server instead of running them locally; see runtime_console.inl), but UI
// script's ClientCommand runs them fine.
//
// So the native does its half - validating the address, setting serverfilter -
// and calls this global to queue the connect. A script function rather than
// ClientCommand directly, because the runtime's function lookup is proven on
// script closures, not on engine natives.
void function NSPS4_ClientCommand( string command )
{
	ClientCommand( command )
}
