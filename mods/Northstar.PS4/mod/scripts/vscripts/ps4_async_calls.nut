global function PS4AsyncCalls_Init

// Runs the calls native code queued for this VM, once a frame, on the VM's own
// thread. PC runs them from the engine's host frame
// (SquirrelManager::ProcessMessageBuffer); script HTTP request callbacks
// (NSHandleSuccessfulHttpRequest, NSHandleFailedHttpRequest) arrive this way.
void function PS4AsyncCalls_Init()
{
	thread PS4AsyncCalls_Run()
}

void function PS4AsyncCalls_Run()
{
	while ( true )
	{
		WaitFrame()
		NSPS4_RunAsyncCalls()
	}
}
