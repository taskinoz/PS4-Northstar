# Native API inventory

PC baseline: NorthstarLauncher `4df8857814dd683147f1cc5fdae0b3b419a7f1cf`. Regenerate with `python scripts/Update-NorthstarApiInventory.py`; verify with `--check`.

This inventories explicit registrations, **not implementation or runtime compatibility**. Compare handler bodies and PS4 signatures before promoting any goal. Shared engine builtin overrides, ConVars and plugin interfaces are separate feature goals in [GOALS.md](GOALS.md).

| PC native | PC return / arguments | PC context | PS4 handler/context | PC source |
|---|---|---|---|---|
| `NSSendMessage` | `void` / `string message, bool isIngame, bool isTeam` | `ScriptContext::CLIENT` | `SendMessage` / `kCtxClient`; `ServerSendMessage` / `kCtxServer` | `client/chatcommand.cpp:19` |
| `NSFetchVerifiedModsManifesto` | `void` / `` | `ScriptContext::SERVER \| ScriptContext::CLIENT \| ScriptContext::UI` | `DownloadUnavailable` / `kCtxAll` | `mods/autodownload/moddownloader.cpp:763` |
| `NSIsModDownloadable` | `bool` / `string name, string version` | `ScriptContext::SERVER \| ScriptContext::CLIENT \| ScriptContext::UI` | `Authenticated` / `kCtxAll` | `mods/autodownload/moddownloader.cpp:770` |
| `NSDownloadMod` | `void` / `string name, string version` | `ScriptContext::SERVER \| ScriptContext::CLIENT \| ScriptContext::UI` | `DownloadUnavailable` / `kCtxAll` | `mods/autodownload/moddownloader.cpp:790` |
| `NSGetModInstallState` | `ModInstallState` / `` | `ScriptContext::SERVER \| ScriptContext::CLIENT \| ScriptContext::UI` | `ModInstallState` / `kCtxAll` | `mods/autodownload/moddownloader.cpp:802` |
| `NSCancelModDownload` | `void` / `` | `ScriptContext::SERVER \| ScriptContext::CLIENT \| ScriptContext::UI` | `CancelDownload` / `kCtxAll` | `mods/autodownload/moddownloader.cpp:831` |
| `NSSaveFile` | `void` / `string file, string data` | `ScriptContext::SERVER \| ScriptContext::CLIENT \| ScriptContext::UI` | `SaveFile` / `kCtxAll` | `mods/modsavefiles.cpp:253` |
| `NSSaveJSONFile` | `void` / `string file, table data` | `ScriptContext::SERVER \| ScriptContext::CLIENT \| ScriptContext::UI` | `SaveJsonFile` / `kCtxAll` | `mods/modsavefiles.cpp:307` |
| `NS_InternalLoadFile` | `int` / `string file` | `ScriptContext::SERVER \| ScriptContext::CLIENT \| ScriptContext::UI` | `LoadFile` / `kCtxAll` | `mods/modsavefiles.cpp:363` |
| `NSDoesFileExist` | `bool` / `string file` | `ScriptContext::SERVER \| ScriptContext::CLIENT \| ScriptContext::UI` | `FileExists` / `kCtxAll` | `mods/modsavefiles.cpp:393` |
| `NSGetFileSize` | `int` / `string file` | `ScriptContext::SERVER \| ScriptContext::CLIENT \| ScriptContext::UI` | `FileSize` / `kCtxAll` | `mods/modsavefiles.cpp:417` |
| `NSDeleteFile` | `void` / `string file` | `ScriptContext::SERVER \| ScriptContext::CLIENT \| ScriptContext::UI` | `DeleteSaveFile` / `kCtxAll` | `mods/modsavefiles.cpp:451` |
| `NS_InternalGetAllFiles` | `array<string>` / `string path` | `ScriptContext::CLIENT \| ScriptContext::UI \| ScriptContext::SERVER` | `GetAllFiles` / `kCtxAll` | `mods/modsavefiles.cpp:475` |
| `NSIsFolder` | `bool` / `string path` | `ScriptContext::CLIENT \| ScriptContext::UI \| ScriptContext::SERVER` | `IsFolder` / `kCtxAll` | `mods/modsavefiles.cpp:514` |
| `NSGetTotalSpaceRemaining` | `int` / `` | `ScriptContext::CLIENT \| ScriptContext::UI \| ScriptContext::SERVER` | `SpaceRemaining` / `kCtxAll` | `mods/modsavefiles.cpp:549` |
| `NSChatWrite` | `void` / `int context, string text` | `ScriptContext::CLIENT` | `ChatWrite` / `kCtxClient` | `scripts/client/clientchathooks.cpp:49` |
| `NSChatWriteRaw` | `void` / `int context, string text` | `ScriptContext::CLIENT` | `ChatWriteRaw` / `kCtxClient` | `scripts/client/clientchathooks.cpp:58` |
| `NSChatWriteLine` | `void` / `int context, string text` | `ScriptContext::CLIENT` | `ChatWriteLine` / `kCtxClient` | `scripts/client/clientchathooks.cpp:67` |
| `NSGetCursorPosition` | `vector ornull` / `` | `ScriptContext::UI` | `CursorPosition` / `kCtxUi` | `scripts/client/cursorposition.cpp:4` |
| `NSRequestCustomMainMenuPromos` | `void` / `` | `ScriptContext::UI` | `RequestPromos` / `kCtxUi` | `scripts/client/scriptmainmenupromos.cpp:24` |
| `NSHasCustomMainMenuPromoData` | `bool` / `` | `ScriptContext::UI` | `Authenticated` / `kCtxUi` | `scripts/client/scriptmainmenupromos.cpp:31` |
| `NSGetCustomMainMenuPromoData` | `var` / `int promoDataKey` | `ScriptContext::UI` | `PromoData` / `kCtxUi` | `scripts/client/scriptmainmenupromos.cpp:37` |
| `NSGetModsInformation` | `array<ModInfo>` / `` | `ScriptContext::SERVER \| ScriptContext::CLIENT \| ScriptContext::UI` | `GetMods` / `kCtxAll` | `scripts/client/scriptmodmenu.cpp:53` |
| `NSGetModInformation` | `array<ModInfo>` / `string modName` | `ScriptContext::SERVER \| ScriptContext::CLIENT \| ScriptContext::UI` | `GetMod` / `kCtxAll` | `scripts/client/scriptmodmenu.cpp:65` |
| `NSGetModNames` | `array<string>` / `` | `ScriptContext::SERVER \| ScriptContext::CLIENT \| ScriptContext::UI` | `GetNames` / `kCtxAll` | `scripts/client/scriptmodmenu.cpp:82` |
| `NSSetModEnabled` | `void` / `string modName, string modVersion, bool enabled` | `ScriptContext::SERVER \| ScriptContext::CLIENT \| ScriptContext::UI` | `SetEnabled` / `kCtxAll` | `scripts/client/scriptmodmenu.cpp:95` |
| `NSReloadMods` | `void` / `` | `ScriptContext::UI` | `ReloadMods` / `kCtxUi` | `scripts/client/scriptmodmenu.cpp:119` |
| `NSIsMasterServerAuthenticated` | `bool` / `` | `ScriptContext::UI` | `MasterServerAuthenticated` / `kCtxUi` | `scripts/client/scriptoriginauth.cpp:6` |
| `NSGetMasterServerAuthResult` | `MasterServerAuthResult` / `` | `ScriptContext::UI` | `AuthResult` / `kCtxUi` | `scripts/client/scriptoriginauth.cpp:21` |
| `NSRequestServerList` | `void` / `` | `ScriptContext::UI` | `RequestServers` / `kCtxUi` | `scripts/client/scriptserverbrowser.cpp:9` |
| `NSIsRequestingServerList` | `bool` / `` | `ScriptContext::UI` | `Authenticated` / `kCtxUi` | `scripts/client/scriptserverbrowser.cpp:16` |
| `NSMasterServerConnectionSuccessful` | `bool` / `` | `ScriptContext::UI` | `MasterServerAuthenticated` / `kCtxUi` | `scripts/client/scriptserverbrowser.cpp:22` |
| `NSGetServerCount` | `int` / `` | `ScriptContext::UI` | `ServerCount` / `kCtxUi` | `scripts/client/scriptserverbrowser.cpp:28` |
| `NSClearRecievedServerList` | `void` / `` | `ScriptContext::UI` | `ClearServers` / `kCtxUi` | `scripts/client/scriptserverbrowser.cpp:34` |
| `NSTryAuthWithServer` | `void` / `int serverIndex, string password = ''` | `ScriptContext::UI` | `TryRemoteAuth` / `kCtxUi` | `scripts/client/scriptserverbrowser.cpp:43` |
| `NSIsAuthenticatingWithServer` | `bool` / `` | `ScriptContext::UI` | `Authenticated` / `kCtxUi` | `scripts/client/scriptserverbrowser.cpp:75` |
| `NSWasAuthSuccessful` | `bool` / `` | `ScriptContext::UI` | `AuthSuccessful` / `kCtxUi` | `scripts/client/scriptserverbrowser.cpp:81` |
| `NSConnectToAuthedServer` | `void` / `` | `ScriptContext::UI` | `CompleteAuth` / `kCtxUi` | `scripts/client/scriptserverbrowser.cpp:87` |
| `NSTryAuthWithLocalServer` | `void` / `` | `ScriptContext::UI` | `TryLocalAuth` / `kCtxUi` | `scripts/client/scriptserverbrowser.cpp:117` |
| `NSCompleteAuthWithLocalServer` | `void` / `` | `ScriptContext::UI` | `CompleteLocalAuth` / `kCtxUi` | `scripts/client/scriptserverbrowser.cpp:126` |
| `NSGetAuthFailReason` | `string` / `` | `ScriptContext::UI` | `AuthFailReason` / `kCtxUi` | `scripts/client/scriptserverbrowser.cpp:137` |
| `NSGetGameServers` | `array<ServerInfo>` / `` | `ScriptContext::UI` | `GameServers` / `kCtxUi` | `scripts/client/scriptserverbrowser.cpp:143` |
| `DecodeJSON` | `table` / `string json, bool fatalParseErrors = false` | `ScriptContext::UI \| ScriptContext::CLIENT \| ScriptContext::SERVER` | `DecodeJson` / `kCtxAll` | `scripts/scriptjson.cpp:197` |
| `EncodeJSON` | `string` / `table data` | `ScriptContext::UI \| ScriptContext::CLIENT \| ScriptContext::SERVER` | `EncodeJson` / `kCtxAll` | `scripts/scriptjson.cpp:232` |
| `StringToAsset` | `asset` / `string assetName` | `ScriptContext::UI \| ScriptContext::CLIENT \| ScriptContext::SERVER` | `ToAsset` / `kCtxAll` | `scripts/scriptutility.cpp:6` |
| `NSGetLocalPlayerUID` | `string` / `` | `ScriptContext::UI \| ScriptContext::CLIENT \| ScriptContext::SERVER` | `ServerLocalPlayerUid` / `kCtxAll` | `scripts/scriptutility.cpp:18` |
| `NSEarlyWritePlayerPersistenceForLeave` | `void` / `entity player` | `ScriptContext::SERVER` | `ServerWritePersistenceForLeave` / `kCtxServer` | `scripts/server/miscserverscript.cpp:11` |
| `NSIsWritingPlayerPersistence` | `bool` / `` | `ScriptContext::SERVER` | `ServerWritingPersistence` / `kCtxServer` | `scripts/server/miscserverscript.cpp:34` |
| `NSIsPlayerLocalPlayer` | `bool` / `entity player` | `ScriptContext::SERVER` | `ServerIsLocalPlayer` / `kCtxServer` | `scripts/server/miscserverscript.cpp:40` |
| `NSIsDedicated` | `bool` / `` | `ScriptContext::SERVER` | `ServerIsDedicated` / `kCtxServer` | `scripts/server/miscserverscript.cpp:56` |
| `NSDisconnectPlayer` | `bool` / `entity player, string reason` | `ScriptContext::SERVER` | `ServerDisconnectPlayer` / `kCtxServer` | `scripts/server/miscserverscript.cpp:62` |
| `NSSendClientPrint` | `void` / `entity player, string msg` | `ScriptContext::SERVER` | `ServerSendClientPrint` / `kCtxServer` | `scripts/server/miscserverscript.cpp:103` |
| `GetUserInfoKVString_Internal` | `string` / `entity player, string key, string defaultValue = \"\"` | `ScriptContext::SERVER` | `UserInfoKvString` / `kCtxServer` | `scripts/server/scriptuserinfo.cpp:6` |
| `GetUserInfoKVAsset_Internal` | `asset` / `entity player, string key, asset defaultValue = $\"\"` | `ScriptContext::SERVER` | `UserInfoKvAsset` / `kCtxServer` | `scripts/server/scriptuserinfo.cpp:26` |
| `GetUserInfoKVInt_Internal` | `int` / `entity player, string key, int defaultValue = 0` | `ScriptContext::SERVER` | `UserInfoKvPrimitive` / `kCtxServer` | `scripts/server/scriptuserinfo.cpp:47` |
| `GetUserInfoKVFloat_Internal` | `float` / `entity player, string key, float defaultValue = 0` | `ScriptContext::SERVER` | `UserInfoKvPrimitive` / `kCtxServer` | `scripts/server/scriptuserinfo.cpp:67` |
| `GetUserInfoKVBool_Internal` | `bool` / `entity player, string key, bool defaultValue = false` | `ScriptContext::SERVER` | `UserInfoKvPrimitive` / `kCtxServer` | `scripts/server/scriptuserinfo.cpp:87` |
| `NSSendMessage` | `void` / `int playerIndex, string text, bool isTeam` | `ScriptContext::SERVER` | `SendMessage` / `kCtxClient`; `ServerSendMessage` / `kCtxServer` | `server/serverchathooks.cpp:123` |
| `NSBroadcastMessage` | `void` / `int fromPlayerIndex, int toPlayerIndex, string text, bool isTeam, bool isDead, int messageType` | `ScriptContext::SERVER` | `ServerBroadcastMessage` / `kCtxServer` | `server/serverchathooks.cpp:134` |
| `NSGetCurrentModName` | `string` / `` | `ScriptContext::UI \| ScriptContext::CLIENT \| ScriptContext::SERVER` | `CurrentModName` / `kCtxAll` | `squirrel/squirrel.cpp:652` |
| `NSGetCallingModName` | `string` / `int depth = 0` | `ScriptContext::UI \| ScriptContext::CLIENT \| ScriptContext::SERVER` | `CallingModName` / `kCtxAll` | `squirrel/squirrel.cpp:672` |
| `NSGetLoadedMapNames` | `array<string>` / `` | `ScriptContext::UI \| ScriptContext::CLIENT \| ScriptContext::SERVER` | `ServerLoadedMapNames` / `kCtxAll` | `util/printmaps.cpp:160` |

## Interpretation

Empty arrays, default arguments, constant false/true and logged no-ops are adapters. In particular, HTTP/downloads/server-list, server chat/disconnect/userinfo and persistence-write registrations do not establish working implementations. `kCtxAll` means UI/CLIENT/SERVER. Duplicate names can have different signatures by context (NSSendMessage).

Coverage: 62 explicit PC ADD_SQFUNC declarations; 67 distinct names in the PS4 registration table. These counts measure different things and are not a completion percentage.
