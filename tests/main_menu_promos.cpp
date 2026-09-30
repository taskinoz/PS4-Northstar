#include "northstar_ps4/main_menu_promos.h"
#include <cassert>
#include <cstdio>

using namespace northstar::ps4;

int main() {
    const char* liveShape = R"json({
      "newInfo":{"Title1":"one","Title2":"two","Title3":"update \u263a"},
      "largeButton":{"Title":"Update","Text":"Required","Url":"https://example.test/a","ImageIndex":12},
      "smallButton1":{"Title":"Discord","Url":"https://example.test/b","ImageIndex":22},
      "smallButton2":{"Title":"Wiki","Url":"https://example.test/c","ImageIndex":12}
    })json";
    MainMenuPromoData data;
    assert(ParseMainMenuPromos(liveShape, data));
    assert(data.newInfoTitle1 == "one");
    assert(data.newInfoTitle3 == "update \xe2\x98\xba");
    assert(data.largeButtonTitle == "Update" && data.largeButtonImageIndex == 12);
    assert(data.smallButton1Title == "Discord" && data.smallButton1ImageIndex == 22);
    assert(data.smallButton2Title == "Wiki" && data.smallButton2ImageIndex == 12);

    assert(!ParseMainMenuPromos("{\"error\":{\"msg\":\"no\"}}", data));
    assert(!ParseMainMenuPromos("{}", data));
    assert(!ParseMainMenuPromos(
        "{\"newInfo\":{\"Title1\":\"\",\"Title2\":\"\",\"Title3\":\"\"},"
        "\"largeButton\":{\"Title\":\"\",\"Text\":\"\",\"Url\":\"\",\"ImageIndex\":1.5},"
        "\"smallButton1\":{\"Title\":\"\",\"Url\":\"\",\"ImageIndex\":1},"
        "\"smallButton2\":{\"Title\":\"\",\"Url\":\"\",\"ImageIndex\":1}}", data));
    std::puts("main_menu_promos tests passed");
}
