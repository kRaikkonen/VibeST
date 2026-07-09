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

// App title / version. Bump the last number on every feature update.
constexpr const wchar_t* kAppTitle = L"VibeST Practice AMP  v0.1.3  by kRaikkonen";
constexpr int IDI_APPICON = 101;   // matches app.rc

constexpr int IDC_IN = 100, IDC_OUT = 101, IDC_EXCL = 102, IDC_START = 103,
              IDC_ODON = 104, IDC_CAB = 105, IDC_STATUS = 106, IDC_BUF = 107,
              IDC_BACKEND = 108, IDC_INCH = 109, IDC_ECO = 110,
              IDC_PEDAL = 111, IDC_AMP = 112, IDC_PEDALB = 113,
              IDC_ODB = 114, IDC_CABKIND = 115, IDC_DELAYON = 116,
              IDC_EQON = 117, IDC_HPFON = 118, IDC_LPFON = 119,
              IDC_GATEON = 120, IDC_CHORUSON = 121, IDC_ROOMON = 122,
              IDC_PRESET = 130, IDC_RECTOMODE = 131, IDC_RECTIFIER = 132,
              IDC_COMPON = 133, IDC_METER = 134, IDC_REVERBON = 135,
              IDC_TUNER = 136, IDC_PITCHON = 137, IDC_TUNERON = 138,
              IDC_TRANSPOSE = 139, IDC_SHIFTEDIT = 150, IDC_SRATE = 152,
              IDC_EQ0 = 200;              // 200..208 = 9 EQ faders
constexpr int IDC_SLIDER0 = 120;   // 10 sliders: 120..129
constexpr int IDC_VAL0 = 140;      // value labels: 140..149

struct Param {
    const wchar_t* name;
    double lo, hi, def;
};
const Param kParams[32] = {
    {L"Drive", 0, 1, 0.6},        {L"Level", 0, 1, 0.35},
    {L"Volume", 0, 1, 0.83},      {L"Treble", 0, 1, 0.63},
    {L"Bass", 0, 1, 0.43},        {L"Reverb", 0, 1, 0.15},
    {L"Trem Speed", 0, 1, 0.45},  {L"Trem Intensity", 0, 1, 0.0},
    {L"Input Trim", 0, 1.0, 0.29},{L"Master", 0, 0.2, 0.088},
    {L"Tone", 0, 1, 0.5},         // 10: slot A tone
    {L"Drive", 0, 1, 0.5},        // 11: slot B drive
    {L"Tone", 0, 1, 0.5},         // 12: slot B tone
    {L"Level", 0, 1, 0.35},       // 13: slot B level
    {L"Time ms", 20, 1000, 294},  // 14: delay time (boot preset)
    {L"Feedbk", 0, 0.9, 0.072},   // 15: delay feedback (boot preset)
    {L"E.Level", 0, 1.5, 0.255},  // 16: delay repeat level (Boss E.Level)
    {L"Room", 0, 1, 0.63},        // 17: room mic amount (boot preset)
    {L"Width", 0, 1, 0.82},       // 18: room mic width (boot preset)
    {L"Thresh", 0, 1, 0.25},      // 19: noise gate threshold
    {L"Rate", 0, 1, 0.4},         // 20: chorus rate
    {L"Depth", 0, 1, 0.55},       // 21: chorus depth
    {L"Mix", 0, 1, 1.0},          // 22: chorus mix
    {L"Sustain", 0, 1, 0.5},      // 23: compressor sustain
    {L"Level", 0, 1, 0.6},        // 24: compressor level
    {L"Decay", 0, 1, 0.6},        // 25: reverb decay
    {L"R.Mix", 0, 1, 0.35},       // 26: reverb mix
    {L"Ref A", 425, 455, 440},    // 27: tuner reference A (Hz, display-only)
    {L"Shift Hz", 415, 470, 440}, // 28: pitch-shift target A (Hz)
    {L"Decay", 0, 1, 0.4},        // 29: noise gate decay/release
    {L"Blend", 0, 1, 1.0},        // 30: compressor dry/wet blend
    {L"Tone", 0, 1, 0.5},         // 31: compressor tone tilt
};
constexpr int NP = 32;
// amp-dependent labels for sliders 2..7
const wchar_t* kAmpLabels[4][6] = {
    {L"Volume", L"Treble", L"Bass", L"Reverb", L"Trem Speed",
     L"Trem Intensity"},
    {L"Gain", L"Treble", L"Middle", L"Bass", L"Presence", L"High Treble"},
    // Dual Rectifier — Vol->Gain, TremSpeed->Master, TremInt->Drive/input-push.
    // Tone knobs shown Treble/Mid/Bass (real amp order): slider4=Mid (fed by
    // c.bass), slider5=Bass (fed by c.reverb) — see the setTone swap in apply().
    {L"Gain", L"Treble", L"Mid", L"Bass", L"Master", L"Drive"},
    // Dumble SSS (clean): Vol->Drive/push, TremSpeed->Volume, TremInt->Input
    {L"Drive", L"Treble", L"Mid", L"Bass", L"Volume", L"Input"},
};

// ---- factory presets: set amp + pedals + knobs (raw slider positions; each amp
// reinterprets them per its control map) then push to the engine. ----------------
struct Preset {
    const wchar_t* name; int amp, pa, pb; bool aOn, bOn;
    double vol, treb, bas, rev, tspd, tint, mstr, ad, at, al, bd, bt, bl;
};
// NOTE: the `mstr` field is a Master SLIDER value (0..0.2 position scale; the
// audio taper in applyControls squares it into the actual gain). Since v0.1.4
// the amps are level-calibrated to a common reference (see engine kAmpMakeup),
// so one uniform master position now means one loudness on every amp: 0.088
// (UI 4.4) reproduces the boot-rig listening level everywhere.
const Preset kPresets[] = {
    {L"— Presets —",     0,3,1,true, true,  0.5,0.5,0.5,0.3,0.5,0.0,0.088, 0.5,0.5,0.5,0.5,0.5,0.5},
    {L"Princeton Clean", 0,0,0,false,false, 0.4,0.6,0.5,0.3,0.0,0.0,0.088, 0.5,0.5,0.5,0.5,0.5,0.5},
    // Mid-amps (Plexi/Recto/Dumble): tone order is Treble/Mid/Bass, so the `bas`
    // field now feeds slider4=Mid and `rev` feeds slider5=Bass (swapped vs before).
    {L"Plexi Crunch",    1,0,0,false,false, 0.7,0.6,0.6,0.4,0.5,0.5,0.088, 0.5,0.5,0.5,0.5,0.5,0.5},
    {L"Recto Metal",     2,0,0,false,false, 0.8,0.4,0.9,0.4,0.4,0.3,0.088, 0.5,0.5,0.5,0.5,0.5,0.5},
    {L"Dumble Clean",    3,0,0,false,false, 0.3,0.75,0.9,0.1,0.6,0.3,0.088,0.5,0.5,0.5,0.5,0.5,0.5},
    {L"Klon → Dumble",   3,6,0,true, false, 0.35,0.75,0.9,0.1,0.6,0.3,0.088,0.6,0.6,0.7,0.5,0.5,0.5},
    {L"TS+SD → Recto",   2,3,1,true, true,  0.75,0.4,0.85,0.4,0.4,0.3,0.088,0.5,0.5,0.55,0.5,0.5,0.52},
    // Boot default: Mad Professor Red + Boss SD-1 -> Mesa Dual Rectifier (Modern).
    {L"Recto Metal Rig", 2,5,1,true, true,  0.80,0.75,0.56,0.51,0.59,0.30,0.088, 0.50,0.50,0.50,0.50,0.39,0.50},
};
constexpr int kBootPreset = sizeof(kPresets) / sizeof(kPresets[0]) - 1;   // "Recto Metal Rig"

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
     gGateOn, gChorusOn, gRoomOn, gPreset, gRectoMode, gRectifier, gCompOn, gMeter,
     gReverbOn, gTuner, gTunerNote, gPitchOn, gTunerOn, gTranspose, gShiftEdit, gSrCombo;
HFONT gFont;
HFONT gFontBig = nullptr;   // large bold font for the prominent Master value
RECT  gMasterFrame = {};    // red highlight box drawn around the Master control
float gMeterLevel = 0.0f;   // latest output peak for the owner-drawn clip meter
float gTunerCents = 0.0f;   // latest tuner deviation (cents, -50..+50) for the needle
bool  gTunerActive = false; // a note is currently detected
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
    if (i == 14)                          // delay time keeps real milliseconds
        swprintf(buf, 32, L"%.0f ms", sliderValue(i));
    else if (i == 27 || i == 28)          // tuner reference / shift target keep real Hz
        swprintf(buf, 32, L"%.1f", sliderValue(i));
    else {                                // all other knobs read as amp-style 0.0-10.0
        int pos = static_cast<int>(SendMessageW(gSliders[i], TBM_GETPOS, 0, 0));
        swprintf(buf, 32, L"%.1f", pos / 10.0);
    }
    if (i == 28 && gShiftEdit) SetWindowTextW(gShiftEdit, buf);   // Shift Hz -> editable box
    else SetWindowTextW(gVals[i], buf);
}

void setSlider(int i, double v) {
    int pos = static_cast<int>(std::lround(
        (v - kParams[i].lo) / (kParams[i].hi - kParams[i].lo) * 100.0));
    pos = pos < 0 ? 0 : (pos > 100 ? 100 : pos);
    SendMessageW(gSliders[i], TBM_SETPOS, TRUE, pos);
    updateValueLabel(i);
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
    {   // Master: audio (square-law) taper, like a real volume pot. The raw
        // linear 0..0.2 mapping crammed all useful travel into the first two
        // ticks on hot amps; squaring the position spreads loudness evenly.
        double mp = sliderValue(9) / 0.2;          // 0..1 position
        c.master = 0.2 * mp * mp;                  // same 0.2 max as before
    }
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
    c.rectoMode = static_cast<int>(SendMessageW(gRectoMode, CB_GETCURSEL, 0, 0));
    if (c.rectoMode < 0) c.rectoMode = 2;
    c.rectType = static_cast<int>(SendMessageW(gRectifier, CB_GETCURSEL, 0, 0));
    if (c.rectType < 0) c.rectType = 0;
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
    c.gateDecay = sliderValue(29);
    c.chorusOn = SendMessageW(gChorusOn, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.chorusRate = sliderValue(20);
    c.chorusDepth = sliderValue(21);
    c.chorusMix = sliderValue(22);
    c.roomOn = SendMessageW(gRoomOn, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.compOn = SendMessageW(gCompOn, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.compSustain = sliderValue(23);
    c.compLevel = sliderValue(24);
    c.compBlend = sliderValue(30);
    c.compTone = sliderValue(31);
    c.reverbOn = SendMessageW(gReverbOn, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.reverbDecay = sliderValue(25);
    c.reverbMix = sliderValue(26);
    c.pitchOn = SendMessageW(gPitchOn, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.pitchTarget = sliderValue(28);
    c.tunerOn = SendMessageW(gTunerOn, BM_GETCHECK, 0, 0) == BST_CHECKED;
    {   // digital capo: combo index 0..24 -> -12..+12 semitones
        int ti = static_cast<int>(SendMessageW(gTranspose, CB_GETCURSEL, 0, 0));
        c.transpose = (ti < 0 ? 12 : ti) - 12;
    }
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
        InvalidateRect(gStartBtn, nullptr, FALSE);   // repaint green
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
        int srSel = static_cast<int>(SendMessageW(gSrCombo, CB_GETCURSEL, 0, 0));
        unsigned devRate = (srSel == 1) ? 44100u : 48000u;
        if (!gAsio.start(sel, period, gEngine, info, sizeof(info), chSel, devRate)) {
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
    InvalidateRect(gStartBtn, nullptr, FALSE);   // repaint red
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
    gFontBig = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
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
               12, 104, 82, 22, IDC_EXCL);
    SendMessageW(gExcl, BM_SETCHECK, BST_CHECKED, 0);
    mk(hwnd, L"STATIC", L"Buffer:", 0, 98, 108, 44, 20, 0);
    gBufCombo = mk(hwnd, L"COMBOBOX", nullptr,
                   CBS_DROPDOWNLIST, 144, 104, 58, 160, IDC_BUF);
    for (int b : kBufSizes) {
        wchar_t t[8];
        swprintf(t, 8, L"%d", b);
        SendMessageW(gBufCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t));
    }
    SendMessageW(gBufCombo, CB_SETCURSEL, 1, 0);   // default 128
    // sample-rate selector: 48 kHz engine-native, or 44.1 kHz (resampled) to
    // match a 44.1 playback chain. Next to Buffer where audio settings live.
    mk(hwnd, L"STATIC", L"Rate:", 0, 210, 108, 38, 20, 0);
    gSrCombo = mk(hwnd, L"COMBOBOX", nullptr, CBS_DROPDOWNLIST, 250, 104, 84, 160, IDC_SRATE);
    for (auto* n : {L"48 kHz", L"44.1 kHz"})
        SendMessageW(gSrCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n));
    SendMessageW(gSrCombo, CB_SETCURSEL, 0, 0);   // default 48 kHz (engine-native, no resample)
    gStartBtn = mk(hwnd, L"BUTTON", L"Start", BS_OWNERDRAW,   // green=Start / red=Stop
                   344, 102, 90, 26, IDC_START);
    gStatus = mk(hwnd, L"STATIC", L"stopped", 0, 12, 132, 424, 20,
                 IDC_STATUS);

    gEco = mk(hwnd, L"BUTTON", L"Eco (low CPU)", BS_AUTOCHECKBOX,
              12, 158, 108, 22, IDC_ECO);
    SendMessageW(gEco, BM_SETCHECK, BST_CHECKED, 0);   // default: on (48 kHz, avoids ASIO overrun)
    mk(hwnd, L"STATIC", L"Cab:", 0, 126, 162, 28, 18, 0);
    gCabKind = mk(hwnd, L"COMBOBOX", nullptr, CBS_DROPDOWNLIST,
                  156, 158, 148, 140, IDC_CABKIND);
    for (auto* n : {L"Jensen C10R 1x10", L"Greenback 2x12",
                    L"Greenback 4x12"})
        SendMessageW(gCabKind, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n));
    SendMessageW(gCabKind, CB_SETCURSEL, 2, 0);           // preset: GB 4x12
    mk(hwnd, L"BUTTON", L"Load Cab IR...", BS_PUSHBUTTON,
       310, 156, 124, 26, IDC_CAB);
    gCabLabel = mk(hwnd, L"STATIC", L"(built-in)", 0, 314, 184, 120, 14, 0);

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
    // compact slider variant (custom label + track widths) for the two narrow
    // side-by-side panels (Noise Gate | Compressor).
    auto sliderN = [&](int i, int x, int y, int lblW, int trkW) {
        gParamLbl[i] = mk(hwnd, L"STATIC", kParams[i].name, 0, x, y + 5, lblW, 18, 0);
        gSliders[i] = mk(hwnd, TRACKBAR_CLASSW, nullptr, TBS_HORZ,
                         x + lblW + 4, y, trkW, 24, IDC_SLIDER0 + i);
        SendMessageW(gSliders[i], TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        int pos = static_cast<int>((kParams[i].def - kParams[i].lo)
            / (kParams[i].hi - kParams[i].lo) * 100.0 + 0.5);
        SendMessageW(gSliders[i], TBM_SETPOS, TRUE, pos);
        gVals[i] = mk(hwnd, L"STATIC", L"", 0, x + lblW + 4 + trkW + 4, y + 5, 40, 18,
                      IDC_VAL0 + i);
        updateValueLabel(i);
    };

    // ============ LEFT COLUMN: signal chain, input -> amp =================
    // ---- TUNER + SHIFT POSE (input stage) --------------------------------
    mk(hwnd, L"BUTTON", L"TUNER  &  SHIFT POSE  (input)",
       BS_GROUPBOX, 8, 186, 436, 108, 0);
    gTuner = mk(hwnd, L"STATIC", nullptr, SS_OWNERDRAW, 22, 204, 290, 24, IDC_TUNER);
    gTunerNote = mk(hwnd, L"STATIC", L"--", SS_CENTER, 322, 206, 116, 22, 0);
    slider(27, 22, 232, 130);   // Ref A: tuner calibration (425-455 Hz)
    gTunerOn = mk(hwnd, L"BUTTON", L"Tuner On", BS_AUTOCHECKBOX,
                  330, 232, 110, 22, IDC_TUNERON);
    SendMessageW(gTunerOn, BM_SETCHECK, BST_UNCHECKED, 0);   // default: off (saves CPU)
    slider(28, 22, 262, 130);   // Shift Hz: pitch-shift target A (editable box below)
    gShiftEdit = mk(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER, 262, 263, 60, 22, IDC_SHIFTEDIT);
    updateValueLabel(28);       // populate the edit box now that it exists
    gPitchOn = mk(hwnd, L"BUTTON", L"Shift Pose", BS_AUTOCHECKBOX,
                  330, 262, 110, 22, IDC_PITCHON);

    // ---- NOISE GATE | COMPRESSOR (before the drives), side by side --------
    mk(hwnd, L"BUTTON", L"NOISE GATE", BS_GROUPBOX, 8, 298, 214, 138, 0);
    gGateOn = mk(hwnd, L"BUTTON", L"On", BS_AUTOCHECKBOX, 152, 314, 56, 20, IDC_GATEON);
    SendMessageW(gGateOn, BM_SETCHECK, BST_CHECKED, 0);   // preset: on
    sliderN(19, 16, 334, 54, 84);   // Threshold
    sliderN(29, 16, 360, 54, 84);   // Decay

    mk(hwnd, L"BUTTON", L"COMPRESSOR", BS_GROUPBOX, 226, 298, 218, 138, 0);
    gCompOn = mk(hwnd, L"BUTTON", L"On", BS_AUTOCHECKBOX, 378, 314, 56, 20, IDC_COMPON);
    sliderN(23, 234, 334, 54, 84);  // Sustain
    sliderN(24, 234, 360, 54, 84);  // Level
    sliderN(30, 234, 386, 54, 84);  // Blend
    sliderN(31, 234, 412, 54, 84);  // Tone

    // ---- PEDALBOARD: two stackable slots, A feeds B ----------------------
    mk(hwnd, L"BUTTON", L"PEDALBOARD  (A → B → amp)",
       BS_GROUPBOX, 8, 440, 436, 258, 0);
    auto pedalCombo = [&](int id, int x, int y) {
        HWND cb = mk(hwnd, L"COMBOBOX", nullptr, CBS_DROPDOWNLIST,
                     x, y, 190, 160, id);
        for (auto* n : {L"Boss OD-1 (1977)",
                        L"Boss SD-1  (white-box)", L"Boss SD-1  (hybrid)",
                        L"Ibanez TS-808  (white-box)", L"Ibanez TS-808  (hybrid)",
                        L"Mad Professor Red  (schematic)", L"Klon Centaur  (schematic)",
                        L"Marshall Bluesbreaker  (schematic)"})
            SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n));
        return cb;
    };
    gOdOn = mk(hwnd, L"BUTTON", L"Slot A", BS_AUTOCHECKBOX,
               22, 460, 70, 20, IDC_ODON);
    SendMessageW(gOdOn, BM_SETCHECK, BST_CHECKED, 0);   // preset: on
    gPedalKind = pedalCombo(IDC_PEDAL, 100, 457);
    SendMessageW(gPedalKind, CB_SETCURSEL, 3, 0);   // preset: TS-808 white-box
    slider(0, 22, 484, 224);   // A Drive
    slider(10, 22, 514, 224);  // A Tone
    slider(1, 22, 544, 224);   // A Level

    gOdB = mk(hwnd, L"BUTTON", L"Slot B", BS_AUTOCHECKBOX,
              22, 578, 70, 20, IDC_ODB);
    SendMessageW(gOdB, BM_SETCHECK, BST_CHECKED, 0);     // preset: on
    gPedalKindB = pedalCombo(IDC_PEDALB, 100, 575);
    SendMessageW(gPedalKindB, CB_SETCURSEL, 1, 0);   // preset: SD-1 white-box
    slider(11, 22, 602, 224);  // B Drive
    slider(12, 22, 632, 224);  // B Tone
    slider(13, 22, 662, 224);  // B Level

    // ---- AMPLIFIER section ----------------------------------------------
    mk(hwnd, L"BUTTON", L"AMPLIFIER", BS_GROUPBOX, 8, 702, 436, 238, 0);
    gAmpKind = mk(hwnd, L"COMBOBOX", nullptr, CBS_DROPDOWNLIST,
                  22, 722, 250, 160, IDC_AMP);
    for (auto* n : {L"Fender Princeton Reverb", L"Marshall Super Lead Plexi",
                    L"Mesa Dual Rectifier", L"Dumble Steel String Singer"})
        SendMessageW(gAmpKind, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n));
    SendMessageW(gAmpKind, CB_SETCURSEL, 0, 0);
    for (int i = 2; i <= 7; ++i)
        slider(i, 22, 748 + (i - 2) * 30, 224);

    // ---- MASTER VOLUME (chain tail) — big value + red highlight frame ----
    mk(hwnd, L"BUTTON", L"MASTER VOLUME", BS_GROUPBOX, 8, 944, 436, 58, 0);
    slider(9, 22, 966, 220);   // Master (label + big value enlarged below)
    SendMessageW(gParamLbl[9], WM_SETFONT, reinterpret_cast<WPARAM>(gFontBig), TRUE);
    MoveWindow(gParamLbl[9], 22, 966, 100, 28, TRUE);
    MoveWindow(gVals[9], 356, 962, 80, 34, TRUE);
    SendMessageW(gVals[9], WM_SETFONT, reinterpret_cast<WPARAM>(gFontBig), TRUE);
    gMasterFrame = { 6, 942, 446, 1004 };   // red highlight box around the section

    // ---- top-right: factory presets + Recto voicing + output meter -------
    mk(hwnd, L"STATIC", L"Preset:", SS_LEFT, 578, 12, 48, 16, 0);
    gPreset = mk(hwnd, L"COMBOBOX", nullptr, CBS_DROPDOWNLIST, 626, 9, 250, 300, IDC_PRESET);
    for (const auto& pr : kPresets)
        SendMessageW(gPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(pr.name));
    SendMessageW(gPreset, CB_SETCURSEL, kBootPreset, 0);   // show the boot preset
    mk(hwnd, L"STATIC", L"Recto Mode (Dual Rect only):", SS_LEFT, 452, 44, 200, 16, 0);
    gRectoMode = mk(hwnd, L"COMBOBOX", nullptr, CBS_DROPDOWNLIST, 656, 41, 220, 120, IDC_RECTOMODE);
    for (auto* n : {L"Raw", L"Vintage", L"Modern"})
        SendMessageW(gRectoMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n));
    SendMessageW(gRectoMode, CB_SETCURSEL, 2, 0);
    mk(hwnd, L"STATIC", L"Rectifier:", SS_LEFT, 578, 74, 74, 16, 0);
    gRectifier = mk(hwnd, L"COMBOBOX", nullptr, CBS_DROPDOWNLIST, 656, 71, 220, 120, IDC_RECTIFIER);
    for (auto* n : {L"Diode (tight)", L"Spongy (sag)"})
        SendMessageW(gRectifier, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n));
    SendMessageW(gRectifier, CB_SETCURSEL, 0, 0);
    // output level meter: fills green, -> yellow near clip, -> red pinned at the top
    mk(hwnd, L"STATIC", L"Output:", SS_LEFT, 578, 107, 74, 16, 0);
    gMeter = mk(hwnd, L"STATIC", nullptr, SS_OWNERDRAW, 656, 104, 220, 18, IDC_METER);
    // input trim (chain front) + digital capo (transpose), top-right.
    // (Master lives in its own highlighted box at the bottom-left.)
    slider(8, 578, 130, 130);   // Input Trim
    mk(hwnd, L"STATIC", L"Capo:", SS_LEFT, 578, 164, 44, 18, 0);
    gTranspose = mk(hwnd, L"COMBOBOX", nullptr, CBS_DROPDOWNLIST,
                    624, 160, 130, 340, IDC_TRANSPOSE);
    for (int s = -12; s <= 12; ++s) {   // ±12 frets = ±1 octave
        wchar_t e[24];
        if (s == 0) swprintf(e, 24, L"0  (off)");
        else swprintf(e, 24, L"%+d fret%s", s, (s == 1 || s == -1) ? L"" : L"s");
        SendMessageW(gTranspose, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(e));
    }
    SendMessageW(gTranspose, CB_SETCURSEL, 12, 0);   // index 12 = 0 semitones (off)

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

    mk(hwnd, L"BUTTON", L"DIGITAL REVERB  (Freeverb, after delay)",
       BS_GROUPBOX, rx, 458, 436, 100, 0);
    gReverbOn = mk(hwnd, L"BUTTON", L"Engage", BS_AUTOCHECKBOX,
                   rx + 14, 478, 120, 20, IDC_REVERBON);
    slider(25, rx + 14, 502, 190);   // Decay
    slider(26, rx + 14, 530, 190);   // R.Mix

    mk(hwnd, L"BUTTON", L"STUDIO ROOM MIC  (stereo)",
       BS_GROUPBOX, rx, 562, 436, 100, 0);
    gRoomOn = mk(hwnd, L"BUTTON", L"Engage", BS_AUTOCHECKBOX,
                 rx + 14, 582, 120, 20, IDC_ROOMON);
    SendMessageW(gRoomOn, BM_SETCHECK, BST_CHECKED, 0);    // preset: on
    slider(17, rx + 14, 606, 190);   // Room amount
    slider(18, rx + 14, 634, 190);   // Width

    // ---- 9-band GRAPHIC EQ (the reference pedal) -------------------------
    mk(hwnd, L"BUTTON", L"GRAPHIC EQ  (stereo, signal tail)",
       BS_GROUPBOX, rx, 666, 436, 330, 0);
    gEqOn = mk(hwnd, L"BUTTON", L"On", BS_AUTOCHECKBOX,
               rx + 14, 686, 50, 20, IDC_EQON);
    SendMessageW(gEqOn, BM_SETCHECK, BST_CHECKED, 0);
    gHpfOn = mk(hwnd, L"BUTTON", L"HPF", BS_AUTOCHECKBOX,
                rx + 300, 686, 56, 20, IDC_HPFON);
    SendMessageW(gHpfOn, BM_SETCHECK, BST_CHECKED, 0);     // preset: on
    gLpfOn = mk(hwnd, L"BUTTON", L"LPF", BS_AUTOCHECKBOX,
                rx + 366, 686, 56, 20, IDC_LPFON);
    SendMessageW(gLpfOn, BM_SETCHECK, BST_CHECKED, 0);     // preset: on
    const wchar_t* efn[9] = {L"75", L"150", L"250", L"400", L"800",
                             L"1.5k", L"4.5k", L"8k", L"12k"};
    double eqDef[9] = {0, 2.6, 2.4, 0, 0, -2.2, 2.6, 4.3, -0.4};   // preset
    for (int b = 0; b < 9; ++b) {
        int x = rx + 20 + b * 46;
        gEqVal[b] = mk(hwnd, L"STATIC", L"0.0", SS_CENTER, x - 6, 712, 44, 16,
                       0);
        gEqFader[b] = mk(hwnd, TRACKBAR_CLASSW, nullptr,
                         TBS_VERT | TBS_AUTOTICKS | TBS_BOTH, x, 732, 30, 218,
                         IDC_EQ0 + b);
        SendMessageW(gEqFader[b], TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(gEqFader[b], TBM_SETPOS, TRUE,
                     50 - static_cast<int>(eqDef[b] * 50.0 / 12.0));
        mk(hwnd, L"STATIC", efn[b], SS_CENTER, x - 6, 954, 44, 16, 0);
        wchar_t v[8];
        swprintf(v, 8, L"%+.1f", eqDef[b]);
        SetWindowTextW(gEqVal[b], v);
    }
}

void relabelAmp() {
    int a = static_cast<int>(SendMessageW(gAmpKind, CB_GETCURSEL, 0, 0));
    if (a < 0 || a > 3) a = 0;
    for (int i = 2; i <= 7; ++i)
        SetWindowTextW(gParamLbl[i], kAmpLabels[a][i - 2]);
}

void applyPreset(int i) {
    if (i <= 0 || i >= static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]))) return;
    const Preset& p = kPresets[i];
    SendMessageW(gAmpKind, CB_SETCURSEL, p.amp, 0);
    SendMessageW(gPedalKind, CB_SETCURSEL, p.pa, 0);
    SendMessageW(gPedalKindB, CB_SETCURSEL, p.pb, 0);
    SendMessageW(gOdOn, BM_SETCHECK, p.aOn ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(gOdB, BM_SETCHECK, p.bOn ? BST_CHECKED : BST_UNCHECKED, 0);
    setSlider(2, p.vol);  setSlider(3, p.treb); setSlider(4, p.bas);  setSlider(5, p.rev);
    setSlider(6, p.tspd); setSlider(7, p.tint); setSlider(9, p.mstr);
    setSlider(0, p.ad);   setSlider(10, p.at);  setSlider(1, p.al);
    setSlider(11, p.bd);  setSlider(12, p.bt);  setSlider(13, p.bl);
    // Graphic EQ is only on for the boot preset; every other preset loads with
    // it off (a flat, un-EQ'd starting point).
    SendMessageW(gEqOn, BM_SETCHECK, i == kBootPreset ? BST_CHECKED : BST_UNCHECKED, 0);
    relabelAmp();
    applyControls();
}

// On amp switch, load that amp's default knob positions — each amp's knobs mean
// different things (Recto Gain/Mid/Master/Drive vs Princeton Vol/Treble/Bass...),
// so re-seat them to sensible values. kPresets index amp+1 (Princeton..Dumble).
void loadAmpPreset(int amp) {
    int pi = amp + 1, npr = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));
    if (pi < 1 || pi >= npr) return;
    const Preset& p = kPresets[pi];
    setSlider(2, p.vol);  setSlider(3, p.treb); setSlider(4, p.bas); setSlider(5, p.rev);
    setSlider(6, p.tspd); setSlider(7, p.tint); setSlider(9, p.mstr);
    if (amp == 2) { SendMessageW(gRectoMode, CB_SETCURSEL, 2, 0);   // Recto -> Modern + Diode
                    SendMessageW(gRectifier, CB_SETCURSEL, 0, 0); }
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            buildUi(hwnd);
            applyPreset(kBootPreset);   // boot into "Recto Metal Rig" (relabels + applies)
            SetTimer(hwnd, 1, 1000, nullptr);
            SetTimer(hwnd, 2, 55, nullptr);   // fast output-meter refresh
            return 0;
        case WM_TIMER:
            if (wp == 2) {   // fast output-meter + tuner refresh
                gMeterLevel = (gRunning && gEngine) ? gEngine->outPeak.load() : 0.0f;
                InvalidateRect(gMeter, nullptr, FALSE);
                float hz = (gRunning && gEngine) ? gEngine->tunerHz.load() : 0.0f;
                if (hz > 20.0f) {
                    double refA = sliderValue(27);                 // user reference A
                    double midi = 69.0 + 12.0 * std::log2(hz / refA);
                    int n = static_cast<int>(std::lround(midi));
                    double cents = (midi - n) * 100.0;
                    static const wchar_t* nm[12] = {L"C", L"C#", L"D", L"D#", L"E", L"F",
                                                    L"F#", L"G", L"G#", L"A", L"A#", L"B"};
                    int ni = ((n % 12) + 12) % 12, oct = n / 12 - 1;
                    gTunerCents = static_cast<float>(cents); gTunerActive = true;
                    wchar_t t[48];
                    swprintf(t, 48, L"%s%d  %+.0fc  %.0fHz", nm[ni], oct, cents, hz);
                    SetWindowTextW(gTunerNote, t);
                } else {
                    gTunerActive = false; gTunerCents = 0.0f;
                    SetWindowTextW(gTunerNote, L"--");
                }
                InvalidateRect(gTuner, nullptr, FALSE);
                return 0;
            }
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
                case IDC_SHIFTEDIT:   // manual Hz entry for Shift Pose
                    if (HIWORD(wp) == EN_KILLFOCUS) {
                        wchar_t t[32]; GetWindowTextW(gShiftEdit, t, 32);
                        double hz = wcstod(t, nullptr);
                        if (hz >= 415.0 && hz <= 470.0) { setSlider(28, hz); applyControls(); }
                        else updateValueLabel(28);   // out of range/garbage -> revert
                    }
                    return 0;
                case IDC_ODON:
                case IDC_ODB:
                case IDC_DELAYON:
                case IDC_EQON:
                case IDC_HPFON:
                case IDC_LPFON:
                case IDC_GATEON:      // were MISSING -> toggling Gate/Chorus/Room did
                case IDC_CHORUSON:    // nothing until a slider drag fired applyControls
                case IDC_ROOMON:
                case IDC_COMPON:
                case IDC_REVERBON:
                case IDC_PITCHON:
                case IDC_TUNERON: applyControls(); return 0;
                case IDC_AMP:
                    if (HIWORD(wp) == CBN_SELCHANGE) {
                        int a = static_cast<int>(SendMessageW(gAmpKind, CB_GETCURSEL, 0, 0));
                        relabelAmp();
                        loadAmpPreset(a);   // load that amp's default knobs (labels differ)
                        applyControls();
                    }
                    return 0;
                case IDC_PEDAL:
                case IDC_PEDALB:
                case IDC_CABKIND:
                    if (HIWORD(wp) == CBN_SELCHANGE) applyControls();
                    return 0;
                case IDC_PRESET:
                    if (HIWORD(wp) == CBN_SELCHANGE)
                        applyPreset(static_cast<int>(
                            SendMessageW(gPreset, CB_GETCURSEL, 0, 0)));
                    return 0;
                case IDC_RECTOMODE:
                case IDC_RECTIFIER:
                case IDC_TRANSPOSE:
                    if (HIWORD(wp) == CBN_SELCHANGE) applyControls();
                    return 0;
                case IDC_BACKEND:
                    if (HIWORD(wp) == CBN_SELCHANGE) populateDevices();
                    return 0;
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (gMasterFrame.right > gMasterFrame.left) {   // red highlight around Master
                HBRUSH rb = CreateSolidBrush(RGB(220, 30, 30));
                RECT f = gMasterFrame;
                for (int k = 0; k < 3; ++k) { FrameRect(hdc, &f, rb); InflateRect(&f, -1, -1); }
                DeleteObject(rb);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DRAWITEM: {
            auto* d = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (d->CtlID == IDC_START) {   // green when Start (stopped), red when Stop (running)
                RECT r = d->rcItem;
                bool pressed = (d->itemState & ODS_SELECTED) != 0;
                COLORREF bg = gRunning ? RGB(205, 45, 45) : RGB(40, 165, 70);
                if (pressed) bg = gRunning ? RGB(165, 30, 30) : RGB(30, 135, 55);
                HBRUSH b = CreateSolidBrush(bg);
                FillRect(d->hDC, &r, b); DeleteObject(b);
                FrameRect(d->hDC, &r, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
                HFONT old = reinterpret_cast<HFONT>(SelectObject(d->hDC, gFont));
                SetBkMode(d->hDC, TRANSPARENT);
                SetTextColor(d->hDC, RGB(255, 255, 255));
                DrawTextW(d->hDC, gRunning ? L"Stop" : L"Start", -1, &r,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(d->hDC, old);
                if (d->itemState & ODS_FOCUS) {
                    RECT fr = r; InflateRect(&fr, -3, -3); DrawFocusRect(d->hDC, &fr);
                }
                return TRUE;
            }
            if (d->CtlID == IDC_METER) {
                RECT r = d->rcItem;
                FillRect(d->hDC, &r, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                float pk = gMeterLevel > 1.0f ? 1.0f : gMeterLevel;
                RECT fill = r; fill.right = r.left + static_cast<int>((r.right - r.left) * pk);
                COLORREF c = pk >= 0.98f ? RGB(225, 30, 30)
                           : (pk >= 0.88f ? RGB(235, 190, 0) : RGB(35, 190, 70));
                HBRUSH b = CreateSolidBrush(c);
                FillRect(d->hDC, &fill, b); DeleteObject(b);
                if (gMeterLevel >= 0.995f) {   // clip: red block pinned at the top
                    RECT cap = r; cap.left = r.right - 10;
                    HBRUSH rb = CreateSolidBrush(RGB(255, 0, 0));
                    FillRect(d->hDC, &cap, rb); DeleteObject(rb);
                }
            } else if (d->CtlID == IDC_TUNER) {   // tuner: flat <- center -> sharp needle
                RECT r = d->rcItem;
                FillRect(d->hDC, &r, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                int w = r.right - r.left, cx = r.left + w / 2;
                RECT ctr = { cx - 2, r.top, cx + 2, r.bottom };   // in-tune center line
                HBRUSH cbz = CreateSolidBrush(RGB(90, 90, 90));
                FillRect(d->hDC, &ctr, cbz); DeleteObject(cbz);
                if (gTunerActive) {
                    double frac = gTunerCents / 50.0;             // ±50c = full scale
                    if (frac > 1.0) frac = 1.0; if (frac < -1.0) frac = -1.0;
                    int nx = cx + static_cast<int>(frac * (w / 2 - 6));
                    float ac = gTunerCents < 0 ? -gTunerCents : gTunerCents;
                    COLORREF c = ac <= 5.0f ? RGB(40, 210, 80)
                               : (ac <= 15.0f ? RGB(235, 190, 0) : RGB(225, 60, 40));
                    RECT nd = { nx - 4, r.top + 2, nx + 4, r.bottom - 2 };
                    HBRUSH nb = CreateSolidBrush(c);
                    FillRect(d->hDC, &nd, nb); DeleteObject(nb);
                }
            }
            return TRUE;
        }
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

    // Keep full CPU clock when minimized / in the background. On Windows 11 the
    // OS applies "EcoQoS" power throttling to windows that lose the foreground,
    // parking the process on efficiency cores at a lower clock -> the audio
    // callback misses its deadline -> dropouts/crackle. Opt out of execution-
    // speed throttling and nudge the priority class up so audio stays glitch-free
    // whether the window is on top or minimized. (Looked up dynamically so it
    // still links on older toolchains that lack SetProcessInformation.)
    {
        typedef struct { ULONG Version, ControlMask, StateMask; } PwrThrottle;
        typedef BOOL(WINAPI * SetProcInfo)(HANDLE, int, LPVOID, DWORD);
        auto spi = reinterpret_cast<SetProcInfo>(GetProcAddress(
            GetModuleHandleW(L"kernel32.dll"), "SetProcessInformation"));
        if (spi) {
            PwrThrottle pt{};
            pt.Version = 1;         // PROCESS_POWER_THROTTLING_CURRENT_VERSION
            pt.ControlMask = 0x1;   // PROCESS_POWER_THROTTLING_EXECUTION_SPEED
            pt.StateMask = 0;       // 0 = throttling OFF (always run full speed)
            spi(GetCurrentProcess(), 4 /*ProcessPowerThrottling*/, &pt, sizeof(pt));
        }
        SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    }

    if (ma_context_init(nullptr, 0, nullptr, &gCtx) != MA_SUCCESS) return 1;
    ma_context_get_devices(&gCtx, &gPlayback, &gNPlayback, &gCapture,
                           &gNCapture);
    gAsio.refresh();

    ensureEngine(true);   // default Eco / low-CPU (48 kHz) to avoid ASIO overrun

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"VibeSTPracticeAmp";
    wc.hIcon = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON),
                                             IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    wc.hIconSm = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON),
                                               IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    RegisterClassExW(&wc);

    RECT r{0, 0, 908, 1006};
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU
                     | WS_MINIMIZEBOX, FALSE);
    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName,
        kAppTitle,
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
