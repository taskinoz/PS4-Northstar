#include "northstar_ps4/mod_vpks.h"
#include "northstar_ps4/mod_catalog.h"
#include "northstar_ps4/mod_settings.h"
#include "northstar_ps4/mod_callbacks.h"
#include "northstar_ps4/mod_savefiles.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace northstar::ps4::mods;
int main() {
    ModInfo m{};
    assert(ParseModMetadata(R"({
      // PC JSON allows comments and trailing commas.
      "Name":"Author.Mod", "Version":"1.2.3", "LoadPriority":-10,
      "InitScript":{"InitScript":"init.nut","InitScriptCallback":"Init",},
      "ConVars":[{"Name":"test","DefaultValue":"1"}],
      "Scripts":[{"Path":"ui/a.nut","RunOn":"UI"}],
      "Localisation":["resource/test_%language%.txt",],
    })", m));
    assert(std::strcmp(m.name,"Author.Mod") == 0);
    assert(std::strcmp(m.initScript,"init.nut") == 0);
    assert(std::strcmp(m.initScriptCallback,"Init") == 0);
    assert(m.loadPriority == -10 && m.uiScriptCount == 1 && m.conVarCount == 1 && m.localisationCount == 1);
    assert(IsModEnabled("{}",m));
    assert(!IsModEnabled(R"({"Author.Mod":{"1.2.3":false},"Version":1})",m));
    assert(IsModEnabled(R"({"Author.Mod":{"1.2.2":false},"Version":1})",m));
    assert(IsModEnabled(R"({"Author.Mod":{"1.2.3":true /* comment */},"Version":1})",m));
    assert(!IsModEnabled(R"({"Author.Mod":false})",m));
    assert(IsModEnabled(R"({"Author.Mod":true})",m));
    assert(!IsModEnabled(R"({"Author.Mod":{"1.2.3":"true"}})",m));
    assert(ParseModMetadata(R"({"Name":"Minimal","InitScript":"first.nut"})",m));
    assert(std::strcmp(m.version,"0.0.0") == 0);
    assert(std::strcmp(m.initScript,"first.nut") == 0);
    assert(!ParseModMetadata("{}",m));
    std::string longName = "{\"Name\":\"" + std::string(64,'x') + "\"}";
    assert(!ParseModMetadata(longName.c_str(),m));
    char str[8] = "abcdefg";
    assert(JsonExtractString("\"a\"",str,sizeof(str)) && std::strcmp(str,"a") == 0);
    assert(!JsonExtractString("\"12345678\"",str,sizeof(str)));
    ModDiscovery d{};
    assert(InsertMod(d,"High",99));
    assert(InsertMod(d,"Zulu",0));
    assert(InsertMod(d,"Alpha",0));
    assert(InsertMod(d,"Low",-20));
    assert(InsertMod(d,"High",99) && d.count == 4);
    assert(std::strcmp(d.names[0],"Low") == 0);
    assert(std::strcmp(d.names[1],"Alpha") == 0);
    assert(std::strcmp(d.names[3],"High") == 0);
    assert(!InsertMod(d,"../escape",0));
    assert(!InsertMod(d,"sub/folder",0));
    assert(!InsertMod(d,"C:\\folder",0));
    std::string settings = R"({"Version":1,"Other":{"2":false},"Author.Mod":{"old":false},})";
    assert(SetEnabledSetting(settings, "Author.Mod", "1.2.3", false));
    assert(ParseModMetadata(R"({"Name":"Author.Mod","Version":"1.2.3"})", m));
    assert(!IsModEnabled(settings.c_str(), m));
    assert(settings.find("\"old\":false") != std::string::npos);
    assert(settings.find("\"Other\":{\"2\":false}") != std::string::npos);
    assert(SetEnabledSetting(settings, "Author.Mod", "1.2.3", true));
    assert(IsModEnabled(settings.c_str(), m));
    auto unchanged = settings;
    assert(!SetEnabledSetting(settings, "bad\"name", "1", false) && settings == unchanged);
    assert(!SetEnabledSetting(settings, "Version", "1", false) && settings == unchanged);
    settings = "{}";
    assert(SetEnabledSetting(settings, "Author.Mod", "1.2.3", false));
    assert(!IsModEnabled(settings.c_str(), m));
    settings = "{broken";
    assert(!SetEnabledSetting(settings, "A", "1", false));
    std::vector<ScriptCallback> callbacks;
    assert(ParseScriptCallbacks(R"({"Scripts":[
        {"Path":"a","UICallback":{"Before":"First","After":"Last"}},
        {"Path":"b","ClientCallback":{"Before":"WrongContext"}},
        {"Path":"c","UICallback":{"Before":"Second","Destroy":"Cleanup"}},
    ]})", "UICallback", callbacks));
    assert(callbacks.size() == 2 && callbacks[0].before == "First" && callbacks[1].before == "Second");
    assert(callbacks[0].after == "Last" && callbacks[1].destroy == "Cleanup");
    assert(!ParseScriptCallbacks(R"({"Scripts":[{"UICallback":{"Before":42}}]})", "UICallback", callbacks));
    assert(callbacks.size() == 2); // invalid input does not partially replace the callback set
    assert(ParseScriptCallbacks("{}", "UICallback", callbacks) && callbacks.empty());

    // Safe I/O policy: each mod may only reach paths inside its own folder.
    assert(SavePathSafe("data.json"));
    assert(SavePathSafe("nested/data.json"));
    assert(!SavePathSafe(""));
    assert(SavePathSafe("", true));
    assert(!SavePathSafe("../escape.json"));
    assert(!SavePathSafe("nested/../../escape.json"));
    assert(!SavePathSafe("/absolute.json"));
    assert(!SavePathSafe("nested//data.json"));
    assert(!SavePathSafe("./data.json"));
    assert(!SavePathSafe("back\\slash.json"));
    assert(!SavePathSafe("drive:name.json"));
    assert(!SavePathSafe("caf\xc3\xa9.json")); // non-ASCII, rejected as on PC
    assert(!SavePathSafe(std::string(kMaxSaveRelativePath, 'a').c_str()));
    assert(SaveExtensionAllowed("a.json") && SaveExtensionAllowed("dir/b.txt"));
    assert(!SaveExtensionAllowed("a.exe") && !SaveExtensionAllowed("noextension"));
    assert(!SaveExtensionAllowed("dir.json/file")); // extension must be on the file
    assert(SaveContentsValid("plain", 5) && !SaveContentsValid("a\0b", 3));
    assert(SaveFolderNameSafe("Northstar.Custom"));
    assert(!SaveFolderNameSafe("..") && !SaveFolderNameSafe("") && !SaveFolderNameSafe("a/b"));
    std::string vpkStem;
    assert(ModVpkStem("englishclient_mp_test.bsp.pak000_dir.vpk", vpkStem) && vpkStem == "client_mp_test.bsp");
    assert(!ModVpkStem("client_mp_test.bsp.pak000_000.vpk", vpkStem));
    assert(!ModVpkStem("../englishclient_mp_test.bsp.pak000_dir.vpk", vpkStem));
    assert(VpkPreload(nullptr) && VpkPreload("not-json"));
    assert(!VpkPreload("{}") && VpkPreload(R"({"Preload":true})") && !VpkPreload(R"({"Preload":false})"));
    assert(VpkPreload("{broken") && VpkPreload(R"({"Preload":truegarbage})"));
    assert(!VpkPreload(R"({/*comment*/"Preload":false,})"));
    assert(VpkPreload(R"({"Preload":true,})"));
    assert(!VpkPreload(R"({"Other":{"Preload":true}})"));
    assert(!VpkPreload(R"({"Preload":false,"Preload":true})"));
    assert(!VpkPreload(R"({"Preload":"true"})"));
    assert(VpkPreload(R"({"Preload":false} trailing)"));
    assert(VpkMatchesMount(vpkStem, "vpk_ps4/client_mp_test.bsp"));
    assert(!VpkMatchesMount(vpkStem, "vpk_ps4/client_mp_other.bsp"));
    std::puts("Mod catalog tests passed.");
}
