#pragma once
#include "northstar_ps4/server_list.h"
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>

namespace northstar::ps4 {

struct MainMenuPromoData {
    std::string newInfoTitle1;
    std::string newInfoTitle2;
    std::string newInfoTitle3;
    std::string largeButtonTitle;
    std::string largeButtonText;
    std::string largeButtonUrl;
    int largeButtonImageIndex = 0;
    std::string smallButton1Title;
    std::string smallButton1Url;
    int smallButton1ImageIndex = 0;
    std::string smallButton2Title;
    std::string smallButton2Url;
    int smallButton2ImageIndex = 0;
};

inline bool PromoInteger(const char* value, int& out) noexcept {
    if (!value) return false;
    value = mods::JsonSkipWs(value);
    errno = 0;
    char* end = nullptr;
    const long long parsed = std::strtoll(value, &end, 10);
    if (end == value || errno != 0 || parsed < INT_MIN || parsed > INT_MAX) return false;
    const char* tail = mods::JsonSkipWs(end);
    if (*tail != ',' && *tail != '}' && *tail != '\0') return false;
    out = static_cast<int>(parsed);
    return true;
}

inline bool ParseMainMenuPromos(const char* json, MainMenuPromoData& out) {
    out = MainMenuPromoData{};
    if (!json || *mods::JsonSkipWs(json) != '{' || mods::JsonFindMember(json, "error")) return false;
    const char* newInfo = mods::JsonFindMember(json, "newInfo");
    const char* large = mods::JsonFindMember(json, "largeButton");
    const char* small1 = mods::JsonFindMember(json, "smallButton1");
    const char* small2 = mods::JsonFindMember(json, "smallButton2");
    return newInfo && large && small1 && small2 &&
        mods::DecodeJsonString(mods::JsonFindMember(newInfo, "Title1"), out.newInfoTitle1) &&
        mods::DecodeJsonString(mods::JsonFindMember(newInfo, "Title2"), out.newInfoTitle2) &&
        mods::DecodeJsonString(mods::JsonFindMember(newInfo, "Title3"), out.newInfoTitle3) &&
        mods::DecodeJsonString(mods::JsonFindMember(large, "Title"), out.largeButtonTitle) &&
        mods::DecodeJsonString(mods::JsonFindMember(large, "Text"), out.largeButtonText) &&
        mods::DecodeJsonString(mods::JsonFindMember(large, "Url"), out.largeButtonUrl) &&
        PromoInteger(mods::JsonFindMember(large, "ImageIndex"), out.largeButtonImageIndex) &&
        mods::DecodeJsonString(mods::JsonFindMember(small1, "Title"), out.smallButton1Title) &&
        mods::DecodeJsonString(mods::JsonFindMember(small1, "Url"), out.smallButton1Url) &&
        PromoInteger(mods::JsonFindMember(small1, "ImageIndex"), out.smallButton1ImageIndex) &&
        mods::DecodeJsonString(mods::JsonFindMember(small2, "Title"), out.smallButton2Title) &&
        mods::DecodeJsonString(mods::JsonFindMember(small2, "Url"), out.smallButton2Url) &&
        PromoInteger(mods::JsonFindMember(small2, "ImageIndex"), out.smallButton2ImageIndex);
}

} // namespace northstar::ps4
