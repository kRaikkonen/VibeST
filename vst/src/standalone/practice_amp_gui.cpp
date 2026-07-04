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
              IDC_PEDAL = 111, IDC_AMP = 112;
constexpr int IDC_SLIDER0 = 120;   // 10 sliders: 120..129
constexpr int IDC_VAL0 = 140;      // value labels: 140..149

struct Param {
    const wchar_t* name;
    double lo, hi, def;
};
const Param kParams[11] = {
    {L"Drive", 0, 1, 0.6},        {L"Pedal Level", 0, 1, 0.35},
    {L"Volume", 0, 1, 0.4},       {L"Treble", 0, 1, 0.55},
    {L"Bass", 0, 1, 0.5},         {L"Reverb", 0, 1, 0.25},
    {L"Trem Speed", 0, 1, 0.45},  {L"Trem Intensity", 0, 1, 0.0},
    {L"Input Trim", 0, 1.0, 0.4},{L"Master", 0, 0.2, 0.045},
    {L"Pedal Tone", 0, 1, 0.5},   // index 10
};

pa::Engine* gEngine = nullptr;
ma_context gCtx;
ma_device gDev;
bool gRunning = false;
ma_device_info* gPlayback = nullptr;
ma_device_info* gCapture = nullptr;
ma_uint32 gNPlayback = 0, gNCapture = 0;
HWND gSliders[11], gVals[11], gStatus, gInCombo, gOutCombo, gExcl, gOdOn,
     gStartBtn, gBufCombo, gBackend, gInCh, gEco, gCabLabel, gPedalKind,
     gAmpKind;
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
    c.odOn = SendMessageW(gOdOn, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.odDrive = sliderValue(0);
    c.odLevel = sliderValue(1);
    c.volume = sliderValue(2);
    c.treble = sliderValue(3);
    c.bass = sliderValue(4);
    c.reverb = sliderValue(5);
    c.tremSpeed = sliderValue(6);
    c.tremIntensity = sliderValue(7);
    c.inTrim = sliderValue(8);
    c.master = sliderValue(9);
    c.odTone = sliderValue(10);
    c.pedalKind = static_cast<int>(
        SendMessageW(gPedalKind, CB_GETCURSEL, 0, 0));
    if (c.pedalKind < 0) c.pedalKind = 0;
    c.ampKind = static_cast<int>(SendMessageW(gAmpKind, CB_GETCURSEL, 0, 0));
    if (c.ampKind < 0) c.ampKind = 0;
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
              12, 158, 130, 22, IDC_ECO);
    gCabLabel = mk(hwnd, L"STATIC", L"Cab: built-in C10R voicing", 0,
                   150, 160, 286, 20, 0);

    populateDevices();

    auto slider = [&](int i, int x, int y, int w) {
        mk(hwnd, L"STATIC", kParams[i].name, 0, x, y + 6, 100, 20, 0);
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

    // ---- OVERDRIVE pedal section (visually separate from the amp) --------
    mk(hwnd, L"BUTTON", L"OVERDRIVE / DISTORTION PEDAL",
       BS_GROUPBOX, 8, 186, 436, 190, 0);
    gOdOn = mk(hwnd, L"BUTTON", L"Engage  (off = amp only)",
               BS_AUTOCHECKBOX, 22, 208, 190, 20, IDC_ODON);
    gPedalKind = mk(hwnd, L"COMBOBOX", nullptr, CBS_DROPDOWNLIST,
                    228, 205, 200, 160, IDC_PEDAL);
    for (auto* n : {L"Boss OD-1 (1977)", L"Boss SD-1 (1981)",
                    L"Ibanez TS-808", L"Mad Professor Red"})
        SendMessageW(gPedalKind, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(n));
    SendMessageW(gPedalKind, CB_SETCURSEL, 0, 0);
    slider(0, 22, 236, 224);   // Drive
    slider(10, 22, 270, 224);  // Pedal Tone
    slider(1, 22, 304, 224);   // Pedal Level

    // ---- AMPLIFIER section ----------------------------------------------
    mk(hwnd, L"BUTTON", L"AMPLIFIER", BS_GROUPBOX, 8, 384, 436, 244, 0);
    gAmpKind = mk(hwnd, L"COMBOBOX", nullptr, CBS_DROPDOWNLIST,
                  120, 404, 300, 160, IDC_AMP);
    for (auto* n : {L"Fender Princeton Reverb", L"Marshall Super Lead Plexi"})
        SendMessageW(gAmpKind, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n));
    SendMessageW(gAmpKind, CB_SETCURSEL, 0, 0);
    mk(hwnd, L"STATIC", L"(Plexi: Reverb knob = Mid)", 0, 22, 407, 96, 18, 0);
    for (int i = 2; i <= 7; ++i)
        slider(i, 22, 434 + (i - 2) * 34, 224);

    // ---- LEVELS section --------------------------------------------------
    mk(hwnd, L"BUTTON", L"LEVELS", BS_GROUPBOX, 8, 668, 436, 90, 0);
    slider(8, 22, 690, 224);   // Input Trim
    slider(9, 22, 724, 224);   // Master
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            buildUi(hwnd);
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
            for (int i = 0; i < 11; ++i)
                if (reinterpret_cast<HWND>(lp) == gSliders[i])
                    updateValueLabel(i);
            applyControls();
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_START: startStop(hwnd); return 0;
                case IDC_CAB: loadCabDialog(hwnd); return 0;
                case IDC_ODON: applyControls(); return 0;
                case IDC_PEDAL:
                case IDC_AMP:
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

    RECT r{0, 0, 460, 804};
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
