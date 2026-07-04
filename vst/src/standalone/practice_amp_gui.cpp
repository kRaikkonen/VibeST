// Win32 GUI front-end for the white-box OD-1 + Princeton Reverb practice amp.
// Native controls only (no framework): device combos, exclusive-mode toggle,
// sliders for every knob, cab-IR loader. Build:
//   g++ -O3 -std=c++20 -static -mwindows practice_amp_gui.cpp
//       -lcomctl32 -lcomdlg32 -o PrincetonPracticeGUI.exe
#define MA_IMPLEMENTATION
#include "engine_core.hpp"
#include "asio_io.hpp"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <memory>
#include <string>

namespace {

constexpr int IDC_IN = 100, IDC_OUT = 101, IDC_EXCL = 102, IDC_START = 103,
              IDC_ODON = 104, IDC_CAB = 105, IDC_STATUS = 106, IDC_BUF = 107,
              IDC_BACKEND = 108, IDC_INCH = 109, IDC_ECO = 110,
              IDC_PEDAL = 111, IDC_AMP = 112, IDC_PEDALB = 113,
              IDC_ODB = 114, IDC_CABKIND = 115, IDC_DELAYON = 116,
              IDC_EQON = 117, IDC_HPFON = 118, IDC_LPFON = 119,
              IDC_GATEON = 120, IDC_CHORUSON = 121, IDC_ROOMON = 122,
              IDC_EQ0 = 200;              // 200..208 = 9 EQ faders
constexpr int IDC_SLIDER0 = 120;   // 10 sliders: 120..129
constexpr int IDC_VAL0 = 140;      // value labels: 140..149

struct Param {
    const wchar_t* name;
    double lo, hi, def;
};
const Param kParams[23] = {
    {L"Drive", 0, 1, 0.6},        {L"Level", 0, 1, 0.35},
    {L"Volume", 0, 1, 0.83},      {L"Treble", 0, 1, 0.63},
    {L"Bass", 0, 1, 0.43},        {L"Reverb", 0, 1, 0.15},
    {L"Trem Speed", 0, 1, 0.45},  {L"Trem Intensity", 0, 1, 0.0},
    {L"Input Trim", 0, 1.0, 0.29},{L"Master", 0, 0.2, 0.018},
    {L"Tone", 0, 1, 0.5},         // 10: slot A tone
    {L"Drive", 0, 1, 0.5},        // 11: slot B drive
    {L"Tone", 0, 1, 0.5},         // 12: slot B tone
    {L"Level", 0, 1, 0.35},       // 13: slot B level
    {L"Time ms", 20, 1000, 402},  // 14: delay time
    {L"Feedbk", 0, 0.9, 0.351},   // 15: delay feedback
    {L"E.Level", 0, 1.5, 0.6},    // 16: delay repeat level (Boss E.Level)
    {L"Room", 0, 1, 0.25},        // 17: room mic amount
    {L"Width", 0, 1, 0.8},        // 18: room mic width
    {L"Threshold", 0, 1, 0.25},   // 19: noise gate threshold
    {L"Rate", 0, 1, 0.4},         // 20: chorus rate
    {L"Depth", 0, 1, 0.55},       // 21: chorus depth
    {L"Mix", 0, 1, 1.0},          // 22: chorus mix
};
constexpr int NP = 23;
// amp-dependent labels for sliders 2..7
const wchar_t* kAmpLabels[2][6] = {
    {L"Volume", L"Treble", L"Bass", L"Reverb", L"Trem Speed",
     L"Trem Intensity"},
    {L"Gain", L"Treble", L"Bass", L"Middle", L"Presence", L"High Treble"},
};

pa::Engine* gEngine = nullptr;
ma_context gCtx;
ma_device gDev;
bool gRunning = false;
ma_device_info* gPlayback = nullptr;
ma_device_info* gCapture = nullptr;
ma_uint32 gNPlayback = 0, gNCapture = 0;
HWND gSliders[NP], gVals[NP], gParamLbl[NP], gStatus, gInCombo, gOutCombo,
     gExcl, gOdOn, gOdB, gStartBtn, gBufCombo, gBackend, gInCh, gEco,
     gCabLabel, gPedalKind, gPedalKindB, gAmpKind, gCabKind,
     gDelayOn, gEqOn, gHpfOn, gLpfOn, gEqFader[9], gEqVal[9],
     gGateOn, gChorusOn, gRoomOn;
HFONT gFont;
std::wstring gStatusBase;
const int kBufSizes[4] = {64, 128, 256, 512};
pa::AsioIO gAsio;
bool gUsingAsio = false;
std::unique_ptr<pa::Engine> gEnginePtr;
bool gEngineEco = false;

void ensureEngine(bool eco) {
    if (gEnginePtr && eco == gEngineEco) return;
    gEnginePtr = std::make_unique<pa::Engine>(
        pa::springTankIr(eco ? 48000.0 : 96000.0),
        std::vector<double>{}, eco);
    gEngineEco = eco;
    gEngine = gEnginePtr.get();
}

std::wstring widen(const char* utf8) {
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w.data(), n);
    while (!w.empty() && w.back() == 0) w.pop_back();
    return w;
}

double sliderValue(int i) {
    int pos = static_cast<int>(SendMessageW(gSliders[i], TBM_GETPOS, 0, 0));
    return kParams[i].lo + (kParams[i].hi - kParams[i].lo) * pos / 100.0;
}

void updateValueLabel(int i) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%.3f", sliderValue(i));
    SetWindowTextW(gVals[i], buf);
}

void applyControls() {
    if (!gEngine) return;
    pa::Controls c;
    c.aOn = SendMessageW(gOdOn, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.bOn = SendMessageW(gOdB, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.aDrive = sliderValue(0);
    c.aLevel = sliderValue(1);
    c.volume = sliderValue(2);
    c.treble = sliderValue(3);
    c.bass = sliderValue(4);
    c.reverb = sliderValue(5);
    c.tremSpeed = sliderValue(6);
    c.tremIntensity = sliderValue(7);
    c.inTrim = sliderValue(8);
    c.master = sliderValue(9);
    c.aTone = sliderValue(10);
    c.bDrive = sliderValue(11);
    c.bTone = sliderValue(12);
    c.bLevel = sliderValue(13);
    c.aKind = static_cast<int>(SendMessageW(gPedalKind, CB_GETCURSEL, 0, 0));
    if (c.aKind < 0) c.aKind = 0;
    c.bKind = static_cast<int>(SendMessageW(gPedalKindB, CB_GETCURSEL, 0, 0));
    if (c.bKind < 0) c.bKind = 0;
    c.ampKind = static_cast<int>(SendMessageW(gAmpKind, CB_GETCURSEL, 0, 0));
    if (c.ampKind < 0) c.ampKind = 0;
    c.cabKind = static_cast<int>(SendMessageW(gCabKind, CB_GETCURSEL, 0, 0));
    if (c.cabKind < 0) c.cabKind = 0;
    c.delayOn = SendMessageW(gDelayOn, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.delayMs = sliderValue(14);
    c.delayFb = sliderValue(15);
    c.delayMix = sliderValue(16);
    c.roomAmount = sliderValue(17);
    c.roomWidth = sliderValue(18);
    c.eqOn = SendMessageW(gEqOn, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.eqHpfOn = SendMessageW(gHpfOn, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.eqLpfOn = SendMessageW(gLpfOn, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.gateOn = SendMessageW(gGateOn, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.gateThresh = sliderValue(19);
    c.chorusOn = SendMessageW(gChorusOn, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.chorusRate = sliderValue(20);
    c.chorusDepth = sliderValue(21);
    c.chorusMix = sliderValue(22);
    c.roomOn = SendMessageW(gRoomOn, BM_GETCHECK, 0, 0) == BST_CHECKED;
    for (int b = 0; b < 9; ++b) {   // faders: 0..100 -> -12..+12 dB
        int pos = static_cast<int>(SendMessageW(gEqFader[b], TBM_GETPOS, 0, 0));
        c.eqDb[b] = (50 - pos) * 12.0 / 50.0;   // top = +12, mid = 0
    }
    // Heavy tone-stack rebuild is done here OUTSIDE the audio lock (it is
    // lock-free / double-buffered), so frantic pot dragging can't starve the
    // audio callback. The rest of apply() is a handful of cheap assignments.
    gEngine->amp.setTone(c.treble, c.bass, c.volume);
    std::lock_guard<std::mutex> lk(gEngine->mtx);
    gEngine->apply(c);
}

bool backendIsAsio() {
    return SendMessageW(gBackend, CB_GETCURSEL, 0, 0) == 0
           && !gAsio.ids.empty();
}

void populateDevices() {
    SendMessageW(gInCombo, CB_RESETCONTENT, 0, 0);
    SendMessageW(gOutCombo, CB_RESETCONTENT, 0, 0);
    if (backendIsAsio()) {
        for (auto& n : gAsio.names)
            SendMessageW(gInCombo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(widen(n.c_str()).c_str()));
        SendMessageW(gOutCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(
            L"(same ASIO device)"));
        EnableWindow(gOutCombo, FALSE);
        EnableWindow(gExcl, FALSE);
    } else {
        EnableWindow(gOutCombo, TRUE);
        EnableWindow(gExcl, TRUE);
        SendMessageW(gInCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"(default)"));
        SendMessageW(gOutCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"(default)"));
        for (ma_uint32 i = 0; i < gNCapture; ++i)
            SendMessageW(gInCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(
                widen(gCapture[i].name).c_str()));
        for (ma_uint32 i = 0; i < gNPlayback; ++i)
            SendMessageW(gOutCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(
                widen(gPlayback[i].name).c_str()));
    }
    SendMessageW(gInCombo, CB_SETCURSEL, 0, 0);
    SendMessageW(gOutCombo, CB_SETCURSEL, 0, 0);
}

void startStop(HWND hwnd) {
    if (gRunning) {
        if (gUsingAsio) gAsio.stop();
        else ma_device_uninit(&gDev);
        gRunning = false;
        gUsingAsio = false;
        SetWindowTextW(gStartBtn, L"Start");
        SetWindowTextW(gStatus, L"stopped");
        return;
    }
    int bufSel = static_cast<int>(SendMessageW(gBufCombo, CB_GETCURSEL, 0, 0));
    ma_uint32 period = static_cast<ma_uint32>(
        kBufSizes[(bufSel >= 0 && bufSel < 4) ? bufSel : 1]);
    bool eco = SendMessageW(gEco, BM_GETCHECK, 0, 0) == BST_CHECKED;
    ensureEngine(eco);
    applyControls();
    gEngine->drops.store(0);
    char info[160] = {};
    if (backendIsAsio()) {
        int sel = static_cast<int>(SendMessageW(gInCombo, CB_GETCURSEL, 0, 0));
        int chSel = static_cast<int>(
            SendMessageW(gInCh, CB_GETCURSEL, 0, 0));
        if (chSel < 0) chSel = 0;
        if (!gAsio.start(sel, period, gEngine, info, sizeof(info), chSel)) {
            std::wstring msg = widen(info);
            SetWindowTextW(gStatus, msg.c_str());
            msg += L"\n\nClose the DAW / anything holding the ASIO driver, "
                   L"or try another buffer size.";
            MessageBoxW(hwnd, msg.c_str(), L"ASIO error", MB_ICONERROR);
            return;
        }
        gUsingAsio = true;
    } else {
        int inSel = static_cast<int>(
            SendMessageW(gInCombo, CB_GETCURSEL, 0, 0));
        int outSel = static_cast<int>(
            SendMessageW(gOutCombo, CB_GETCURSEL, 0, 0));
        const ma_device_id* inId =
            (inSel > 0 && inSel <= static_cast<int>(gNCapture))
                ? &gCapture[inSel - 1].id : nullptr;
        const ma_device_id* outId =
            (outSel > 0 && outSel <= static_cast<int>(gNPlayback))
                ? &gPlayback[outSel - 1].id : nullptr;
        bool excl = SendMessageW(gExcl, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (pa::startDevice(&gCtx, &gDev, gEngine, inId, outId, excl,
                            info, sizeof(info), period) != MA_SUCCESS) {
            SetWindowTextW(gStatus, L"device init FAILED");
            MessageBoxW(hwnd, L"Could not open the audio device.\n"
                        L"Close other apps using it and retry.",
                        L"Audio error", MB_ICONERROR);
            return;
        }
    }
    gRunning = true;
    SetWindowTextW(gStartBtn, L"Stop");
    gStatusBase = widen(info);
    SetWindowTextW(gStatus, gStatusBase.c_str());
}

void loadCabDialog(HWND hwnd) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Impulse responses (*.wav;*.flac)\0*.wav;*.flac\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    char utf8[MAX_PATH * 3] = {};
    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), nullptr,
                        nullptr);
    auto ir = pa::loadCabWav(utf8);
    size_t taps = ir.size();
    std::lock_guard<std::mutex> lk(gEngine->mtx);
    gEngine->setCab(std::move(ir));
    if (gEngine->useCabIr) {
        const wchar_t* base = wcsrchr(path, L'\\');
        base = base ? base + 1 : path;
        wchar_t msg[300];
        swprintf(msg, 300, L"Cab IR: %s  (%zu taps, loaded OK)", base, taps);
        SetWindowTextW(gCabLabel, msg);
    } else {
        SetWindowTextW(gCabLabel, L"Cab IR load FAILED - built-in C10R voicing");
    }
}

HWND mk(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style,
        int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             x, y, w, h, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                             nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);
    return c;
}

void buildUi(HWND hwnd) {
    gFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    mk(hwnd, L"STATIC", L"Driver:", 0, 12, 14, 60, 20, 0);
    gBackend = mk(hwnd, L"COMBOBOX", nullptr, CBS_DROPDOWNLIST,
                  76, 10, 200, 120, IDC_BACKEND);
    SendMessageW(gBackend, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(
        gAsio.ids.empty() ? L"ASIO (no drivers found)" : L"ASIO"));
    SendMessageW(gBackend, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"WASAPI"));
    SendMessageW(gBackend, CB_SETCURSEL, gAsio.ids.empty() ? 1 : 0, 0);
    mk(hwnd, L"STATIC", L"In ch:", 0, 288, 14, 40, 20, 0);
    gInCh = mk(hwnd, L"COMBOBOX", nullptr, CBS_DROPDOWNLIST,
               330, 10, 60, 120, IDC_INCH);
    SendMessageW(gInCh, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"1"));
    SendMessageW(gInCh, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"2"));
    SendMessageW(gInCh, CB_SETCURSEL, 1, 0);   // Solo/2i2: jack input = ch 2

    mk(hwnd, L"STATIC", L"Input:", 0, 12, 44, 60, 20, 0);
    gInCombo = mk(hwnd, L"COMBOBOX", nullptr,
                  CBS_DROPDOWNLIST | WS_VSCROLL, 76, 40, 360, 200, IDC_IN);
    mk(hwnd, L"STATIC", L"Output:", 0, 12, 74, 60, 20, 0);
    gOutCombo = mk(hwnd, L"COMBOBOX", nullptr,
                   CBS_DROPDOWNLIST | WS_VSCROLL, 76, 70, 360, 200, IDC_OUT);

    gExcl = mk(hwnd, L"BUTTON", L"Exclusive", BS_AUTOCHECKBOX,
               12, 104, 90, 22, IDC_EXCL);
    SendMessageW(gExcl, BM_SETCHECK, BST_CHECKED, 0);
    mk(hwnd, L"STATIC", L"Buffer:", 0, 108, 108, 46, 20, 0);
    gBufCombo = mk(hwnd, L"COMBOBOX", nullptr,
                   CBS_DROPDOWNLIST, 156, 104, 66, 160, IDC_BUF);
    for (int b : kBufSizes) {
        wchar_t t[8];
        swprintf(t, 8, L"%d", b);
        SendMessageW(gBufCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t));
    }
    SendMessageW(gBufCombo, CB_SETCURSEL, 1, 0);   // default 128
    gStartBtn = mk(hwnd, L"BUTTON", L"Start", BS_PUSHBUTTON,
                   232, 102, 90, 26, IDC_START);
    mk(hwnd, L"BUTTON", L"Load Cab IR...", BS_PUSHBUTTON,
       330, 102, 106, 26, IDC_CAB);
    gStatus = mk(hwnd, L"STATIC", L"stopped", 0, 12, 132, 424, 20,
                 IDC_STATUS);

    gEco = mk(hwnd, L"BUTTON", L"Eco (low CPU)", BS_AUTOCHECKBOX,
              12, 158, 110, 22, IDC_ECO);
    gGateOn = mk(hwnd, L"BUTTON", L"Noise Gate", BS_AUTOCHECKBOX,
                 128, 158, 96, 22, IDC_GATEON);
    SendMessageW(gGateOn, BM_SETCHECK, BST_CHECKED, 0);   // preset: on
    mk(hwnd, L"STATIC", L"Cab:", 0, 232, 162, 32, 18, 0);
    gCabKind = mk(hwnd, L"COMBOBOX", nullptr, CBS_DROPDOWNLIST,
                  266, 158, 150, 140, IDC_CABKIND);
    for (auto* n : {L"Jensen C10R 1x10", L"Greenback 2x12",
                    L"Greenback 4x12"})
        SendMessageW(gCabKind, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n));
    SendMessageW(gCabKind, CB_SETCURSEL, 2, 0);           // preset: GB 4x12
    gCabLabel = mk(hwnd, L"STATIC", L"(built-in)", 0, 224, 182, 100, 16, 0);

    populateDevices();

    auto slider = [&](int i, int x, int y, int w) {
        gParamLbl[i] = mk(hwnd, L"STATIC", kParams[i].name, 0, x, y + 6,
                          100, 20, 0);
        gSliders[i] = mk(hwnd, TRACKBAR_CLASSW, nullptr,
                         TBS_HORZ | TBS_AUTOTICKS, x + 104, y, w, 28,
                         IDC_SLIDER0 + i);
        SendMessageW(gSliders[i], TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        int pos = static_cast<int>((kParams[i].def - kParams[i].lo)
            / (kParams[i].hi - kParams[i].lo) * 100.0 + 0.5);
        SendMessageW(gSliders[i], TBM_SETPOS, TRUE, pos);
        gVals[i] = mk(hwnd, L"STATIC", L"", 0, x + 104 + w + 6, y + 6, 52, 20,
                      IDC_VAL0 + i);
        updateValueLabel(i);
    };

    // ---- PEDALBOARD: two stackable slots, A feeds B ------------------------
    mk(hwnd, L"BUTTON", L"PEDALBOARD  (A → B → amp)",
       BS_GROUPBOX, 8, 186, 436, 262, 0);
    auto pedalCombo = [&](int id, int x, int y) {
        HWND cb = mk(hwnd, L"COMBOBOX", nullptr, CBS_DROPDOWNLIST,
                     x, y, 190, 160, id);
        for (auto* n : {L"Boss OD-1 (1977)", L"Boss SD-1 (1981)",
                        L"Ibanez TS-808", L"Mad Professor Red"})
            SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n));
        return cb;
    };
    gOdOn = mk(hwnd, L"BUTTON", L"Slot A", BS_AUTOCHECKBOX,
               22, 208, 70, 20, IDC_ODON);
    SendMessageW(gOdOn, BM_SETCHECK, BST_CHECKED, 0);   // preset: on
    gPedalKind = pedalCombo(IDC_PEDAL, 100, 205);
    SendMessageW(gPedalKind, CB_SETCURSEL, 0, 0);
    slider(0, 22, 232, 224);   // A Drive
    slider(10, 22, 262, 224);  // A Tone
    slider(1, 22, 292, 224);   // A Level

    gOdB = mk(hwnd, L"BUTTON", L"Slot B", BS_AUTOCHECKBOX,
              22, 326, 70, 20, IDC_ODB);
    SendMessageW(gOdB, BM_SETCHECK, BST_CHECKED, 0);     // preset: on
    gPedalKindB = pedalCombo(IDC_PEDALB, 100, 323);
    SendMessageW(gPedalKindB, CB_SETCURSEL, 3, 0);   // preset: Mad Red
    slider(11, 22, 350, 224);  // B Drive
    slider(12, 22, 380, 224);  // B Tone
    slider(13, 22, 410, 224);  // B Level

    // ---- AMPLIFIER section ----------------------------------------------
    mk(hwnd, L"BUTTON", L"AMPLIFIER", BS_GROUPBOX, 8, 456, 436, 240, 0);
    gAmpKind = mk(hwnd, L"COMBOBOX", nullptr, CBS_DROPDOWNLIST,
                  22, 476, 250, 160, IDC_AMP);
    for (auto* n : {L"Fender Princeton Reverb", L"Marshall Super Lead Plexi"})
        SendMessageW(gAmpKind, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n));
    SendMessageW(gAmpKind, CB_SETCURSEL, 0, 0);
    for (int i = 2; i <= 7; ++i)
        slider(i, 22, 504 + (i - 2) * 30, 224);

    // ---- LEVELS + noise gate --------------------------------------------
    mk(hwnd, L"BUTTON", L"LEVELS  &  NOISE GATE", BS_GROUPBOX,
       8, 704, 436, 128, 0);
    slider(8, 22, 726, 224);   // Input Trim
    slider(9, 22, 758, 224);   // Master
    slider(19, 22, 794, 224);  // Noise Gate threshold

    // ================= RIGHT COLUMN: FX chain ============================
    int rx = 456;
    mk(hwnd, L"BUTTON", L"CE-2 CHORUS  (after amp)",
       BS_GROUPBOX, rx, 186, 436, 128, 0);
    gChorusOn = mk(hwnd, L"BUTTON", L"Engage", BS_AUTOCHECKBOX,
                   rx + 14, 206, 120, 20, IDC_CHORUSON);
    slider(20, rx + 14, 230, 190);   // Rate
    slider(21, rx + 14, 258, 190);   // Depth
    slider(22, rx + 14, 286, 190);   // Mix

    mk(hwnd, L"BUTTON", L"BOSS DIGITAL DELAY  (amp → cab)",
       BS_GROUPBOX, rx, 322, 436, 128, 0);
    gDelayOn = mk(hwnd, L"BUTTON", L"Engage", BS_AUTOCHECKBOX,
                  rx + 14, 342, 120, 20, IDC_DELAYON);
    SendMessageW(gDelayOn, BM_SETCHECK, BST_CHECKED, 0);   // preset: on
    slider(14, rx + 14, 366, 190);   // Time ms
    slider(15, rx + 14, 394, 190);   // Feedback
    slider(16, rx + 14, 422, 190);   // Mix

    mk(hwnd, L"BUTTON", L"STUDIO ROOM MIC  (stereo)",
       BS_GROUPBOX, rx, 458, 436, 96, 0);
    gRoomOn = mk(hwnd, L"BUTTON", L"Engage", BS_AUTOCHECKBOX,
                 rx + 300, 478, 120, 20, IDC_ROOMON);
    SendMessageW(gRoomOn, BM_SETCHECK, BST_CHECKED, 0);    // preset: on
    slider(17, rx + 14, 478, 190);   // Room amount
    slider(18, rx + 14, 508, 190);   // Width

    // ---- 9-band GRAPHIC EQ (the reference pedal) -------------------------
    mk(hwnd, L"BUTTON", L"GRAPHIC EQ  (stereo, signal tail)",
       BS_GROUPBOX, rx, 562, 436, 330, 0);
    gEqOn = mk(hwnd, L"BUTTON", L"On", BS_AUTOCHECKBOX,
               rx + 14, 582, 50, 20, IDC_EQON);
    SendMessageW(gEqOn, BM_SETCHECK, BST_CHECKED, 0);
    gHpfOn = mk(hwnd, L"BUTTON", L"HPF", BS_AUTOCHECKBOX,
                rx + 300, 582, 56, 20, IDC_HPFON);
    SendMessageW(gHpfOn, BM_SETCHECK, BST_CHECKED, 0);     // preset: on
    gLpfOn = mk(hwnd, L"BUTTON", L"LPF", BS_AUTOCHECKBOX,
                rx + 366, 582, 56, 20, IDC_LPFON);
    SendMessageW(gLpfOn, BM_SETCHECK, BST_CHECKED, 0);     // preset: on
    const wchar_t* efn[9] = {L"75", L"150", L"250", L"400", L"800",
                             L"1.5k", L"4.5k", L"8k", L"12k"};
    double eqDef[9] = {0, 2.6, 2.4, 0, 0, -2.2, 2.6, 4.3, -0.4};   // preset
    for (int b = 0; b < 9; ++b) {
        int x = rx + 20 + b * 46;
        gEqVal[b] = mk(hwnd, L"STATIC", L"0.0", SS_CENTER, x - 6, 608, 44, 16,
                       0);
        gEqFader[b] = mk(hwnd, TRACKBAR_CLASSW, nullptr,
                         TBS_VERT | TBS_AUTOTICKS | TBS_BOTH, x, 628, 30, 218,
                         IDC_EQ0 + b);
        SendMessageW(gEqFader[b], TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(gEqFader[b], TBM_SETPOS, TRUE,
                     50 - static_cast<int>(eqDef[b] * 50.0 / 12.0));
        mk(hwnd, L"STATIC", efn[b], SS_CENTER, x - 6, 850, 44, 16, 0);
        wchar_t v[8];
        swprintf(v, 8, L"%+.1f", eqDef[b]);
        SetWindowTextW(gEqVal[b], v);
    }
}

void relabelAmp() {
    int a = static_cast<int>(SendMessageW(gAmpKind, CB_GETCURSEL, 0, 0));
    if (a < 0 || a > 1) a = 0;
    for (int i = 2; i <= 7; ++i)
        SetWindowTextW(gParamLbl[i], kAmpLabels[a][i - 2]);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            buildUi(hwnd);
            relabelAmp();
            applyControls();          // push the default preset to the engine
            SetTimer(hwnd, 1, 1000, nullptr);
            return 0;
        case WM_TIMER:
            if (gRunning && gEngine) {
                auto db = [](float v) {
                    return v > 1e-6f ? 20.0 * std::log10(v) : -99.0;
                };
                wchar_t s[300];
                swprintf(s, 300,
                         L"%s | load %d%% | drops %d | in %.0f dB | "
                         L"out %.0f dB",
                         gStatusBase.c_str(), gEngine->loadPct.load(),
                         gEngine->drops.load(),
                         db(gEngine->inPeak.load()),
                         db(gEngine->outPeak.load()));
                SetWindowTextW(gStatus, s);
            }
            return 0;
        case WM_HSCROLL:
            for (int i = 0; i < NP; ++i)
                if (reinterpret_cast<HWND>(lp) == gSliders[i])
                    updateValueLabel(i);
            applyControls();
            return 0;
        case WM_VSCROLL:   // the 9 vertical EQ faders
            for (int b = 0; b < 9; ++b)
                if (reinterpret_cast<HWND>(lp) == gEqFader[b]) {
                    int pos = static_cast<int>(
                        SendMessageW(gEqFader[b], TBM_GETPOS, 0, 0));
                    wchar_t v[8];
                    swprintf(v, 8, L"%+.1f", (50 - pos) * 12.0 / 50.0);
                    SetWindowTextW(gEqVal[b], v);
                }
            applyControls();
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_START: startStop(hwnd); return 0;
                case IDC_CAB: loadCabDialog(hwnd); return 0;
                case IDC_ODON:
                case IDC_ODB:
                case IDC_DELAYON:
                case IDC_EQON:
                case IDC_HPFON:
                case IDC_LPFON: applyControls(); return 0;
                case IDC_AMP:
                    if (HIWORD(wp) == CBN_SELCHANGE) {
                        relabelAmp();
                        applyControls();
                    }
                    return 0;
                case IDC_PEDAL:
                case IDC_PEDALB:
                case IDC_CABKIND:
                    if (HIWORD(wp) == CBN_SELCHANGE) applyControls();
                    return 0;
                case IDC_BACKEND:
                    if (HIWORD(wp) == CBN_SELCHANGE) populateDevices();
                    return 0;
            }
            return 0;
        case WM_DESTROY:
            if (gRunning) {
                if (gUsingAsio) gAsio.stop();
                else ma_device_uninit(&gDev);
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nShow) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    if (ma_context_init(nullptr, 0, nullptr, &gCtx) != MA_SUCCESS) return 1;
    ma_context_get_devices(&gCtx, &gPlayback, &gNPlayback, &gCapture,
                           &gNCapture);
    gAsio.refresh();

    ensureEngine(false);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"PrincetonPractice";
    RegisterClassW(&wc);

    RECT r{0, 0, 908, 912};
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU
                     | WS_MINIMIZEBOX, FALSE);
    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName,
        L"Princeton Reverb + OD-1  (white-box circuit simulation)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, nShow);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    ma_context_uninit(&gCtx);
    return 0;
}
