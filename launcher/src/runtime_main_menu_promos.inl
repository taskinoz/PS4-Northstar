// Northstar main-menu announcements and spotlight buttons. PC fetches this
// asynchronously from <master>/client/mainmenupromos and makes the UI script
// wait on NSHasCustomMainMenuPromoData before reading the thirteen fields.

constexpr std::size_t kPromoBufferSize = 64 * 1024;
std::atomic<int> promoFetchState{kFetchIdle};
MainMenuPromoData* fetchedPromos = nullptr; // worker-owned until kFetchReady release
MainMenuPromoData* mainMenuPromos = nullptr; // UI thread only
bool promoFetchOk = false;       // worker-owned until kFetchReady release
bool hasMainMenuPromos = false;  // UI thread only
char promoBuffer[kPromoBufferSize];

void* MainMenuPromoWorker(void*) noexcept {
    char url[256];
    std::snprintf(url, sizeof(url), "%s/client/mainmenupromos", kMasterServerUrl);
    int status = 0;
    promoFetchOk = false;
    *fetchedPromos = MainMenuPromoData{};
    promoBuffer[0] = '\0';
    const bool got = HttpGet(url, promoBuffer, sizeof(promoBuffer), status);
    const std::size_t length = std::strlen(promoBuffer);
    if (!got || status != 200) {
        LogFormat("[NorthstarPS4] main-menu promos failed: transport=%d status=%d\n", got ? 1 : 0, status);
    } else if (length >= sizeof(promoBuffer) - 1) {
        LogFormat("[NorthstarPS4] main-menu promos failed: response exceeds %zu bytes\n", sizeof(promoBuffer));
    } else {
        promoFetchOk = ParseMainMenuPromos(promoBuffer, *fetchedPromos);
        LogFormat("[NorthstarPS4] main-menu promos parsed=%d bytes=%zu\n", promoFetchOk ? 1 : 0, length);
    }
    promoFetchState.store(kFetchReady, std::memory_order_release);
    return nullptr;
}

void PublishMainMenuPromos() {
    if (promoFetchState.load(std::memory_order_acquire) != kFetchReady) return;
    hasMainMenuPromos = promoFetchOk;
    if (promoFetchOk) *mainMenuPromos = std::move(*fetchedPromos);
    *fetchedPromos = MainMenuPromoData{};
    promoFetchState.store(kFetchIdle, std::memory_order_release);
}

int RequestPromos(void*) {
    // DT_INIT intentionally skips the C++ global constructor table. Allocate
    // these non-trivial objects on first use instead of relying on zero-filled
    // std::string objects behaving like constructed instances.
    if (!fetchedPromos) fetchedPromos = new MainMenuPromoData;
    if (!mainMenuPromos) mainMenuPromos = new MainMenuPromoData;
    PublishMainMenuPromos();
    int expected = kFetchIdle;
    if (!promoFetchState.compare_exchange_strong(expected, kFetchRequesting)) return 0;
    hasMainMenuPromos = false;
    OrbisPthread thread{};
    if (!InitHttpTransport() ||
        scePthreadCreate(&thread, nullptr, MainMenuPromoWorker, nullptr, "NSMainMenuPromo") != 0) {
        LogFormat("[NorthstarPS4] main-menu promos failed: could not start request\n");
        promoFetchOk = false;
        promoFetchState.store(kFetchReady, std::memory_order_release);
        return 0;
    }
    scePthreadDetach(thread);
    return 0;
}

int HasPromos(void* vm) {
    PublishMainMenuPromos();
    Boolean(vm, hasMainMenuPromos);
    return 1;
}

int PromoData(void* vm) {
    PublishMainMenuPromos();
    if (!hasMainMenuPromos || Arg(vm, 1).tag != kSqInteger) return 0;
    switch (static_cast<int>(Arg(vm, 1).value)) {
    case 0: String(vm, mainMenuPromos->newInfoTitle1.c_str()); break;
    case 1: String(vm, mainMenuPromos->newInfoTitle2.c_str()); break;
    case 2: String(vm, mainMenuPromos->newInfoTitle3.c_str()); break;
    case 3: String(vm, mainMenuPromos->largeButtonTitle.c_str()); break;
    case 4: String(vm, mainMenuPromos->largeButtonText.c_str()); break;
    case 5: String(vm, mainMenuPromos->largeButtonUrl.c_str()); break;
    case 6: Integer(vm, mainMenuPromos->largeButtonImageIndex); break;
    case 7: String(vm, mainMenuPromos->smallButton1Title.c_str()); break;
    case 8: String(vm, mainMenuPromos->smallButton1Url.c_str()); break;
    case 9: Integer(vm, mainMenuPromos->smallButton1ImageIndex); break;
    case 10: String(vm, mainMenuPromos->smallButton2Title.c_str()); break;
    case 11: String(vm, mainMenuPromos->smallButton2Url.c_str()); break;
    case 12: Integer(vm, mainMenuPromos->smallButton2ImageIndex); break;
    default: return 0;
    }
    return 1;
}
