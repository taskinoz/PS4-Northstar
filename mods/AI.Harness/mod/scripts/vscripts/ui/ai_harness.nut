global function AIHarness_Init
struct { bool started = false } file

void function AIHarness_Init()
{
    if ( file.started )
        return
    file.started = true
    thread AIHarness_Poll()
}

void function AIHarness_Poll()
{
    wait 2.0
    print( "[AI.Harness] ready" )
    while ( true )
    {
        string id = ""
        try
        {
            string raw = NSAIHarnessRead()
            if ( raw != "" )
            {
                id = NSAIHarnessField( raw, "id" )
                string action = NSAIHarnessField( raw, "action" )
                string result = "completed"
                if ( action == "launch" )
                {
                    SetConVarString( "communities_hostname", "" )
                    NSTryAuthWithLocalServer()
                    float deadline = Time() + 30.0
                    while ( NSIsAuthenticatingWithServer() && Time() < deadline )
                        WaitFrame()
                    if ( NSIsAuthenticatingWithServer() )
                        throw "Local authentication timed out"
                    if ( !NSWasAuthSuccessful() )
                        throw NSGetAuthFailReason()
                    NSCompleteAuthWithLocalServer()
                    if ( GetConVarString( "mp_gamemode" ) == "solo" )
                        SetConVarString( "mp_gamemode", "tdm" )
                    CloseAllDialogs()
                    ClientCommand( "setplaylist tdm" )
                    ClientCommand( "map mp_lobby" )
                    result = "queued"
                }
                else if ( action == "console" )
                {
                    ClientCommand( NSAIHarnessField( raw, "command" ) )
                    result = "queued"
                }
                else if ( action == "menu" )
                    AdvanceMenu( GetMenu( NSAIHarnessField( raw, "menu" ) ) )
                else if ( action == "back" )
                    CloseActiveMenu()
                else if ( action != "status" )
                    throw "Unknown harness action"
                NSAIHarnessReply( EncodeJSON( { id = id, status = result, action = action, connected = IsConnected(), lobby = IsLobby(), level = GetActiveLevel() } ) )
                print( "[AI.Harness] " + action + " " + result )
            }
        }
        catch ( error )
        {
            print( "[AI.Harness] command failed: " + error )
            try { NSAIHarnessReply( EncodeJSON( { id = id, status = "error", message = string( error ) } ) ) }
            catch ( replyError ) { print( "[AI.Harness] reply failed" ) }
        }
        wait 0.25
    }
}
