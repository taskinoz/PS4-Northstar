global function AIHarness_Init
struct
{
    bool started = false
    bool httpComplete = false
    string httpResult = ""
} file

// menu_ns_serverbrowser.nut's CORE_MODS, which is file-local there.
const array<string> AI_HARNESS_CORE_MODS = [ "Northstar.Client", "Northstar.Coop", "Northstar.CustomServers", "Northstar.Custom" ]

bool function AIHarness_HasModVersion( string name, string version )
{
    foreach ( ModInfo mod in NSGetModInformation( name ) )
        if ( mod.version == version )
            return true
    return false
}

void function AIHarness_HttpSuccess( HttpRequestResponse response )
{
    file.httpResult = format( "success|%d|%d", response.statusCode, response.body.len() )
    file.httpComplete = true
}

void function AIHarness_HttpFailure( HttpRequestFailure failure )
{
    file.httpResult = format( "failure|%d|%s", failure.errorCode, failure.errorMessage )
    file.httpComplete = true
}

// The server browser's join, minus the list UI: authenticate, fetch the
// verified list if a required mod is missing, download through DownloadMod
// (the browser's own dialog), switch client-required mods to match the server,
// ReloadMods, connect. Returns a summary of what changed.
string function AIHarness_Join( string text )
{
    NSRequestServerList()
    while ( NSIsRequestingServerList() )
        WaitFrame()
    ServerInfo server
    bool found = false
    foreach ( ServerInfo candidate in NSGetGameServers() )
    {
        if ( candidate.name.find( text ) != null )
        {
            server = candidate
            found = true
            break
        }
    }
    if ( !found )
        throw "No listed server name contains: " + text
    if ( server.requiresPassword )
        throw "Server needs a password"

    NSTryAuthWithServer( server.index, "" )
    while ( NSIsAuthenticatingWithServer() )
        WaitFrame()
    if ( !NSWasAuthSuccessful() )
        throw NSGetAuthFailReason()

    array<RequiredModInfo> missing
    foreach ( RequiredModInfo mod in server.requiredMods )
        if ( !AI_HARNESS_CORE_MODS.contains( mod.name ) && !AIHarness_HasModVersion( mod.name, mod.version ) )
            missing.append( mod )
    string summary = "server=" + server.name + " missing=" + missing.len()
    if ( missing.len() > 0 )
    {
        if ( !GetConVarBool( "allow_mod_auto_download" ) )
            throw "Required mods missing and allow_mod_auto_download is 0"
        FetchVerifiedModsManifesto()
        foreach ( RequiredModInfo mod in missing )
        {
            if ( !NSIsModDownloadable( mod.name, mod.version ) )
                throw mod.name + " " + mod.version + " is not verified"
            if ( !DownloadMod( mod ) )
            {
                DisplayModDownloadErrorDialog( mod.name )
                throw "Download failed: " + mod.name + " state " + NSGetModInstallState().status
            }
            summary += " downloaded=" + mod.name
        }
    }

    // ConnectToServer's reconciliation.
    foreach ( ModInfo mod in NSGetModsInformation() )
    {
        if ( !mod.requiredOnClient || !mod.enabled || AI_HARNESS_CORE_MODS.contains( mod.name ) )
            continue
        bool required = false
        foreach ( RequiredModInfo need in server.requiredMods )
            if ( need.name == mod.name && need.version == mod.version )
                required = true
        if ( !required )
        {
            NSSetModEnabled( mod.name, mod.version, false )
            summary += " disabled=" + mod.name
        }
    }
    foreach ( RequiredModInfo need in server.requiredMods )
    {
        if ( AI_HARNESS_CORE_MODS.contains( need.name ) )
            continue
        NSSetModEnabled( need.name, need.version, true )
        summary += " enabled=" + need.name
    }
    ReloadMods()
    NSConnectToAuthedServer()
    return summary
}

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
                string json = ""
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
                // Round-trips the command text through the natives, for testing them.
                else if ( action == "json" )
                    json = EncodeJSON( DecodeJSON( NSAIHarnessField( raw, "command" ), true ) )
                // Reads a convar; the value comes back in the reply's json field.
                else if ( action == "cvar" )
                    json = GetConVarString( NSAIHarnessField( raw, "command" ) )
                // "<playlist>|<name>|<fallback>": reads the effective playlist
                // variable so console override tests can verify engine state.
                else if ( action == "playlistvar" )
                {
                    array<string> parts = split( NSAIHarnessField( raw, "command" ), "|" )
                    if ( parts.len() != 3 )
                        throw "Expected <playlist>|<name>|<fallback>"
                    json = GetPlaylistVarOrUseValue( parts[0], parts[1], parts[2] )
                }
                // Exercises every scalar CSV datatable accessor plus a row
                // search against a table shipped by Northstar.CustomServers.
                else if ( action == "datatable" )
                {
                    var dataTable = GetDataTable( $"datatable/burn_meter_rewards.rpak" )
                    int refColumn = GetDataTableColumnByName( dataTable, "itemRef" )
                    int row = GetDataTableRowMatchingStringValue( dataTable, refColumn, "burnmeter_maphack" )
                    bool selectable = GetDataTableBool( dataTable, row, GetDataTableColumnByName( dataTable, "selectable" ) )
                    json = format( "%d|%s|%d|%.3f|%d", GetDatatableRowCount( dataTable ),
                        GetDataTableString( dataTable, row, refColumn ),
                        GetDataTableInt( dataTable, row, GetDataTableColumnByName( dataTable, "cost" ) ),
                        GetDataTableFloat( dataTable, row, GetDataTableColumnByName( dataTable, "activationCost" ) ),
                        selectable ? 1 : 0 )
                }
                // Development-only fixture action for vector return packing and
                // exact vector row matching. The fixture is not in release mods.
                else if ( action == "datatablevector" )
                {
                    var dataTable = GetDataTable( $"datatable/ps4_vector_fixture.rpak" )
                    int column = GetDataTableColumnByName( dataTable, "origin" )
                    vector value = GetDataTableVector( dataTable, 1, column )
                    int row = GetDataTableRowMatchingVectorValue( dataTable, column, value )
                    json = format( "%.3f|%.3f|%.3f|%d", value.x, value.y, value.z, row )
                }
                // Resolves a localisation token; the text comes back in the json field.
                else if ( action == "localize" )
                    json = Localize( NSAIHarnessField( raw, "command" ) )
                // Starts a real script HTTP request and waits for its deferred
                // callback. This is the acceptance test for the native host-
                // frame queue drain; no script polling bridge is involved.
                else if ( action == "http" )
                {
                    file.httpComplete = false
                    file.httpResult = ""
                    if ( !NSHttpGet( NSAIHarnessField( raw, "command" ), {},
                            AIHarness_HttpSuccess, AIHarness_HttpFailure ) )
                        throw "HTTP request was not started"
                    float deadline = Time() + 65.0
                    while ( !file.httpComplete && Time() < deadline )
                        WaitFrame()
                    if ( !file.httpComplete )
                        throw "HTTP callback timed out"
                    json = file.httpResult
                }
                // Starts a request and immediately rebuilds the UI VM. A delayed
                // endpoint must finish against the retired generation and be
                // dropped natively rather than call either the old or new VM.
                else if ( action == "httpretire" )
                {
                    file.httpComplete = false
                    file.httpResult = ""
                    if ( !NSHttpGet( NSAIHarnessField( raw, "command" ), {},
                            AIHarness_HttpSuccess, AIHarness_HttpFailure ) )
                        throw "HTTP request was not started"
                    result = "queued"
                    ReloadMods()
                }
                // Joins the first listed server whose name contains the command
                // text, downloading and enabling its required mods the way the
                // server browser does (OnServerSelected_Threaded, ConnectToServer).
                else if ( action == "join" )
                {
                    json = AIHarness_Join( NSAIHarnessField( raw, "command" ) )
                    result = "queued"
                }
                // "<name>|<version>": the join's download and enable steps for
                // one verified mod, then ReloadMods, without a server.
                else if ( action == "download" )
                {
                    array<string> parts = split( NSAIHarnessField( raw, "command" ), "|" )
                    if ( parts.len() != 2 )
                        throw "Expected <name>|<version>"
                    RequiredModInfo mod
                    mod.name = parts[0]
                    mod.version = parts[1]
                    json = "had=" + AIHarness_HasModVersion( mod.name, mod.version )
                    if ( !AIHarness_HasModVersion( mod.name, mod.version ) )
                    {
                        FetchVerifiedModsManifesto()
                        if ( !NSIsModDownloadable( mod.name, mod.version ) )
                            throw mod.name + " " + mod.version + " is not verified"
                        if ( !DownloadMod( mod ) )
                            throw "Download failed: state " + NSGetModInstallState().status
                    }
                    NSSetModEnabled( mod.name, mod.version, true )
                    ReloadMods()
                    foreach ( ModInfo info in NSGetModInformation( mod.name ) )
                        json += " " + info.version + " enabled=" + info.enabled + " remote=" + info.isRemote
                    result = "queued"
                }
                else if ( action != "status" )
                    throw "Unknown harness action"
                NSAIHarnessReply( EncodeJSON( { id = id, status = result, action = action, connected = IsConnected(), lobby = IsLobby(), level = GetActiveLevel(), playlist = IsConnected() ? GetCurrentPlaylistName() : "", privateMatch = IsConnected() && IsPrivateMatch(), json = json } ) )
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
