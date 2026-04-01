#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <filesystem>
#include <string>
#include <vector>

namespace
{
constexpr int ID_COMBO_IN = 1001;
constexpr int ID_COMBO_OUT = 1002;
constexpr int ID_EDIT_PRESET = 1003;
constexpr int ID_EDIT_MODE = 1004;
constexpr int ID_BTN_START = 1005;
constexpr int ID_BTN_STOP = 1006;
constexpr int ID_STATIC_STATUS = 1007;
constexpr int ID_BTN_REFRESH = 1008;
constexpr int ID_EDIT_TRAIN_AUDIO = 1009;
constexpr int ID_EDIT_TRAIN_NAME = 1010;
constexpr int ID_BTN_TRAIN = 1011;
constexpr int ID_BTN_BROWSE_AUDIO = 1012;
constexpr int ID_COMBO_SAVED_PRESETS = 1013;
constexpr int ID_BTN_REFRESH_PRESETS = 1014;
constexpr int ID_EDIT_LOG = 1015;
constexpr int ID_BTN_OPEN_PRESETS = 1016;
constexpr int ID_EDIT_AI_MODEL = 1017;

PROCESS_INFORMATION g_proc{};
bool g_running = false;

void SetStatus(HWND hWnd, const wchar_t* txt);

struct DeviceEntry
{
    int index = -1;
    std::wstring name;
};

void AppendLog(HWND hWnd, const std::wstring& line)
{
    HWND hLog = GetDlgItem(hWnd, ID_EDIT_LOG);
    if (!hLog) return;

    const int len = GetWindowTextLengthW(hLog);
    SendMessageW(hLog, EM_SETSEL, static_cast<WPARAM>(len), static_cast<LPARAM>(len));
    SendMessageW(hLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
    SendMessageW(hLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L"\r\n"));
}

std::wstring GetPresetsDirectoryW()
{
    wchar_t userProfile[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) > 0)
    {
        return std::wstring(userProfile) + L"\\Documents\\voice-changer\\presets";
    }
    return L".\\presets";
}

void RefreshSavedPresets(HWND hWnd)
{
    HWND combo = GetDlgItem(hWnd, ID_COMBO_SAVED_PRESETS);
    if (!combo) return;

    SendMessageW(combo, CB_RESETCONTENT, 0, 0);

    const std::wstring dir = GetPresetsDirectoryW();
    try
    {
        namespace fs = std::filesystem;
        if (fs::exists(dir))
        {
            for (const auto& e : fs::directory_iterator(dir))
            {
                if (!e.is_regular_file()) continue;
                if (e.path().extension() != L".preset") continue;

                const std::wstring name = e.path().stem().wstring();
                SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
            }
        }
    }
    catch (...)
    {
    }

    if (SendMessageW(combo, CB_GETCOUNT, 0, 0) > 0)
    {
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    }
}

void OpenPresetsFolder(HWND hWnd)
{
    const std::wstring dir = GetPresetsDirectoryW();
    std::filesystem::create_directories(dir);
    const HINSTANCE r = ShellExecuteW(hWnd, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<intptr_t>(r) <= 32)
    {
        SetStatus(hWnd, L"Impossible d'ouvrir le dossier presets");
    }
}

std::wstring Utf8OrAcpToWide(const std::string& s)
{
    if (s.empty()) return L"";

    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n > 0)
    {
        std::wstring out(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), static_cast<int>(s.size()), out.data(), n);
        return out;
    }

    n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return L"";

    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

bool RunListDevices(std::wstring& output)
{
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::wstring base(exePath);
    const size_t p = base.find_last_of(L"\\/");
    if (p != std::wstring::npos) base = base.substr(0, p + 1);
    const std::wstring vcExe = base + L"voice_changer.exe";

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE rd = nullptr;
    HANDLE wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + vcExe + L"\" --list-devices";
    std::wstring mutableCmd = cmd;

    const BOOL ok = CreateProcessW(
        nullptr,
        mutableCmd.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    CloseHandle(wr);
    if (!ok)
    {
        CloseHandle(rd);
        return false;
    }

    std::string raw;
    char buf[512];
    DWORD got = 0;
    while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got > 0)
    {
        raw.append(buf, buf + got);
    }

    WaitForSingleObject(pi.hProcess, 3000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(rd);

    output = Utf8OrAcpToWide(raw);
    return !output.empty();
}

bool ParseDeviceLine(const std::wstring& line, DeviceEntry& out)
{
    if (line.size() < 4 || line[0] != L'[') return false;
    const size_t close = line.find(L']');
    if (close == std::wstring::npos || close <= 1) return false;

    try
    {
        out.index = std::stoi(line.substr(1, close - 1));
    }
    catch (...)
    {
        return false;
    }

    size_t start = close + 1;
    while (start < line.size() && line[start] == L' ') ++start;
    out.name = (start < line.size()) ? line.substr(start) : L"(unknown)";
    return true;
}

void PopulateCombo(HWND combo, const std::vector<DeviceEntry>& entries)
{
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const auto& e : entries)
    {
        std::wstring label = L"[" + std::to_wstring(e.index) + L"] " + e.name;
        const LRESULT idx = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        if (idx >= 0) SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(idx), static_cast<LPARAM>(e.index));
    }
    if (!entries.empty()) SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

bool RefreshDevices(HWND hWnd)
{
    std::wstring out;
    if (!RunListDevices(out))
    {
        SetStatus(hWnd, L"Impossible de lire la liste des peripheriques");
        return false;
    }

    std::vector<DeviceEntry> ins;
    std::vector<DeviceEntry> outs;
    enum class Section { None, Capture, Render } section = Section::None;

    size_t start = 0;
    while (start <= out.size())
    {
        size_t end = out.find(L'\n', start);
        if (end == std::wstring::npos) end = out.size();

        std::wstring line = out.substr(start, end - start);
        while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) line.pop_back();

        if (line.find(L"CAPTURE DEVICES") != std::wstring::npos) section = Section::Capture;
        else if (line.find(L"RENDER DEVICES") != std::wstring::npos) section = Section::Render;
        else
        {
            DeviceEntry e;
            if (ParseDeviceLine(line, e))
            {
                if (section == Section::Capture) ins.push_back(e);
                else if (section == Section::Render) outs.push_back(e);
            }
        }

        if (end == out.size()) break;
        start = end + 1;
    }

    PopulateCombo(GetDlgItem(hWnd, ID_COMBO_IN), ins);
    PopulateCombo(GetDlgItem(hWnd, ID_COMBO_OUT), outs);

    if (ins.empty() || outs.empty())
    {
        SetStatus(hWnd, L"Aucun peripherique detecte");
        return false;
    }

    SetStatus(hWnd, L"Peripheriques charges");
    return true;
}

std::wstring GetControlText(HWND hWnd, int id)
{
    HWND hEdit = GetDlgItem(hWnd, id);
    const int len = GetWindowTextLengthW(hEdit);
    std::wstring out(static_cast<size_t>(len + 1), L'\0');
    GetWindowTextW(hEdit, out.data(), len + 1);
    out.resize(static_cast<size_t>(len));
    return out;
}

void SetStatus(HWND hWnd, const wchar_t* txt)
{
    SetWindowTextW(GetDlgItem(hWnd, ID_STATIC_STATUS), txt);
}

int GetSelectedDeviceIndex(HWND hWnd, int comboId)
{
    HWND combo = GetDlgItem(hWnd, comboId);
    const LRESULT sel = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) return -1;
    return static_cast<int>(SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(sel), 0));
}

bool IsBuiltInPreset(const std::wstring& preset)
{
    return preset == L"girl" || preset == L"demon" || preset == L"robot" || preset == L"radio";
}

std::wstring BuildCommandLine(HWND hWnd)
{
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::wstring base(exePath);
    const size_t p = base.find_last_of(L"\\/");
    if (p != std::wstring::npos) base = base.substr(0, p + 1);

    const std::wstring vcExe = base + L"voice_changer.exe";

    const int inIdx = GetSelectedDeviceIndex(hWnd, ID_COMBO_IN);
    const int outIdx = GetSelectedDeviceIndex(hWnd, ID_COMBO_OUT);
    const std::wstring preset = GetControlText(hWnd, ID_EDIT_PRESET);
    const std::wstring mode = GetControlText(hWnd, ID_EDIT_MODE);
    const std::wstring aiModel = GetControlText(hWnd, ID_EDIT_AI_MODEL);

    std::wstring cmd = L"\"" + vcExe + L"\"";
    cmd += L" --in-index " + std::to_wstring(inIdx);
    cmd += L" --out-index " + std::to_wstring(outIdx);
    cmd += L" --mode " + mode;

    if (mode == L"ai" && !preset.empty() && !IsBuiltInPreset(preset))
    {
        cmd += L" --load-preset \"" + preset + L"\"";
    }
    else
    {
        cmd += L" --preset \"" + preset + L"\"";
    }

    if (mode == L"ai")
    {
        cmd += L" --ai-profile bright --ai-blend 0.7 --ai-intensity 0.5";
        if (!aiModel.empty())
        {
            cmd += L" --ai-model \"" + aiModel + L"\"";
        }
    }

    return cmd;
}

std::wstring BuildTrainCommandLine(HWND hWnd)
{
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::wstring base(exePath);
    const size_t p = base.find_last_of(L"\\/");
    if (p != std::wstring::npos) base = base.substr(0, p + 1);

    const std::wstring vcExe = base + L"voice_changer.exe";
    const std::wstring audioPath = GetControlText(hWnd, ID_EDIT_TRAIN_AUDIO);
    const std::wstring presetName = GetControlText(hWnd, ID_EDIT_TRAIN_NAME);

    std::wstring cmd = L"\"" + vcExe + L"\"";
    cmd += L" --fine-tune-audio \"" + audioPath + L"\"";
    cmd += L" --preset-name \"" + presetName + L"\"";
    cmd += L" --mode ai --ai-profile bright --ai-blend 1.0 --ai-intensity 1.0";
    return cmd;
}

bool BrowseAudioFile(HWND hWnd)
{
    wchar_t fileBuf[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"WAV files (*.wav)\0*.wav\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!GetOpenFileNameW(&ofn)) return false;

    SetWindowTextW(GetDlgItem(hWnd, ID_EDIT_TRAIN_AUDIO), fileBuf);
    return true;
}

bool StartTraining(HWND hWnd)
{
    const std::wstring audioPath = GetControlText(hWnd, ID_EDIT_TRAIN_AUDIO);
    const std::wstring presetName = GetControlText(hWnd, ID_EDIT_TRAIN_NAME);

    if (audioPath.empty() || presetName.empty())
    {
        SetStatus(hWnd, L"Renseigner le fichier audio et le nom du preset");
        return false;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE rd = nullptr;
    HANDLE wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0))
    {
        SetStatus(hWnd, L"Erreur creation pipe de logs");
        return false;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::wstring cmd = BuildTrainCommandLine(hWnd);
    std::wstring mutableCmd = cmd;

    SetStatus(hWnd, L"Entrainement en cours...");
    AppendLog(hWnd, L"[train] " + cmd);

    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        CloseHandle(wr);
        CloseHandle(rd);
        SetStatus(hWnd, L"Erreur lancement fine-tuning");
        return false;
    }

    CloseHandle(wr);

    std::string raw;
    char buf[512];
    DWORD got = 0;
    while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got > 0)
    {
        raw.append(buf, buf + got);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(rd);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    std::wstring out = Utf8OrAcpToWide(raw);
    if (!out.empty())
    {
        size_t start = 0;
        while (start <= out.size())
        {
            size_t end = out.find(L'\n', start);
            if (end == std::wstring::npos) end = out.size();
            std::wstring line = out.substr(start, end - start);
            while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) line.pop_back();
            if (!line.empty()) AppendLog(hWnd, line);
            if (end == out.size()) break;
            start = end + 1;
        }
    }

    SetWindowTextW(GetDlgItem(hWnd, ID_EDIT_PRESET), presetName.c_str());
    SetWindowTextW(GetDlgItem(hWnd, ID_EDIT_MODE), L"ai");

    RefreshSavedPresets(hWnd);

    if (exitCode == 0)
    {
        SetStatus(hWnd, L"Fine-tuning termine et preset sauvegarde");
        return true;
    }

    SetStatus(hWnd, L"Echec fine-tuning (voir logs)");
    return true;
}

bool StartBackend(HWND hWnd)
{
    if (g_running) return true;

    const int inIdx = GetSelectedDeviceIndex(hWnd, ID_COMBO_IN);
    const int outIdx = GetSelectedDeviceIndex(hWnd, ID_COMBO_OUT);
    if (inIdx < 0 || outIdx < 0)
    {
        SetStatus(hWnd, L"Selection de peripherique invalide");
        return false;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);

    std::wstring cmd = BuildCommandLine(hWnd);
    std::wstring mutableCmd = cmd;

    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &g_proc))
    {
        SetStatus(hWnd, L"Erreur lancement voice_changer.exe");
        return false;
    }

    g_running = true;
    SetStatus(hWnd, L"Backend en cours d'execution");
    return true;
}

void StopBackend(HWND hWnd)
{
    if (!g_running) return;

    TerminateProcess(g_proc.hProcess, 0);
    CloseHandle(g_proc.hThread);
    CloseHandle(g_proc.hProcess);
    g_proc = {};
    g_running = false;

    SetStatus(hWnd, L"Backend arrete");
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        CreateWindowW(L"STATIC", L"Input device:", WS_CHILD | WS_VISIBLE, 20, 20, 120, 20, hWnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL, 140, 20, 240, 300, hWnd, reinterpret_cast<HMENU>(ID_COMBO_IN), nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Output device:", WS_CHILD | WS_VISIBLE, 20, 55, 120, 20, hWnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL, 140, 55, 240, 300, hWnd, reinterpret_cast<HMENU>(ID_COMBO_OUT), nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Preset:", WS_CHILD | WS_VISIBLE, 20, 90, 120, 20, hWnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"EDIT", L"macron_v1", WS_CHILD | WS_VISIBLE | WS_BORDER, 140, 90, 120, 24, hWnd, reinterpret_cast<HMENU>(ID_EDIT_PRESET), nullptr, nullptr);
        CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL, 140, 125, 120, 200, hWnd, reinterpret_cast<HMENU>(ID_COMBO_SAVED_PRESETS), nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Use selected", WS_CHILD | WS_VISIBLE, 270, 125, 110, 24, hWnd, reinterpret_cast<HMENU>(ID_BTN_REFRESH_PRESETS), nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Open presets folder", WS_CHILD | WS_VISIBLE, 270, 155, 110, 24, hWnd, reinterpret_cast<HMENU>(ID_BTN_OPEN_PRESETS), nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Mode (dsp/ai):", WS_CHILD | WS_VISIBLE, 20, 185, 120, 20, hWnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"EDIT", L"ai", WS_CHILD | WS_VISIBLE | WS_BORDER, 140, 185, 120, 24, hWnd, reinterpret_cast<HMENU>(ID_EDIT_MODE), nullptr, nullptr);

        CreateWindowW(L"STATIC", L"AI model (.onnx):", WS_CHILD | WS_VISIBLE, 20, 220, 120, 20, hWnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 140, 220, 240, 24, hWnd, reinterpret_cast<HMENU>(ID_EDIT_AI_MODEL), nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Train audio:", WS_CHILD | WS_VISIBLE, 20, 255, 120, 20, hWnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"EDIT", L"..\\audio_samples\\macron-voeux.wav", WS_CHILD | WS_VISIBLE | WS_BORDER, 140, 255, 170, 24, hWnd, reinterpret_cast<HMENU>(ID_EDIT_TRAIN_AUDIO), nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE, 320, 255, 60, 24, hWnd, reinterpret_cast<HMENU>(ID_BTN_BROWSE_AUDIO), nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Train preset:", WS_CHILD | WS_VISIBLE, 20, 290, 120, 20, hWnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"EDIT", L"macron_v1", WS_CHILD | WS_VISIBLE | WS_BORDER, 140, 290, 170, 24, hWnd, reinterpret_cast<HMENU>(ID_EDIT_TRAIN_NAME), nullptr, nullptr);

        CreateWindowW(L"BUTTON", L"Refresh devices", WS_CHILD | WS_VISIBLE, 270, 90, 110, 24, hWnd, reinterpret_cast<HMENU>(ID_BTN_REFRESH), nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Train + Save", WS_CHILD | WS_VISIBLE, 320, 290, 60, 24, hWnd, reinterpret_cast<HMENU>(ID_BTN_TRAIN), nullptr, nullptr);

        CreateWindowW(L"BUTTON", L"Start", WS_CHILD | WS_VISIBLE, 20, 330, 100, 30, hWnd, reinterpret_cast<HMENU>(ID_BTN_START), nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Stop", WS_CHILD | WS_VISIBLE, 130, 330, 100, 30, hWnd, reinterpret_cast<HMENU>(ID_BTN_STOP), nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Pret", WS_CHILD | WS_VISIBLE, 20, 370, 360, 24, hWnd, reinterpret_cast<HMENU>(ID_STATIC_STATUS), nullptr, nullptr);
        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            20, 400, 360, 90, hWnd, reinterpret_cast<HMENU>(ID_EDIT_LOG), nullptr, nullptr);

        RefreshDevices(hWnd);
        RefreshSavedPresets(hWnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_BTN_START:
            StartBackend(hWnd);
            return 0;
        case ID_BTN_STOP:
            StopBackend(hWnd);
            return 0;
        case ID_BTN_REFRESH:
            RefreshDevices(hWnd);
            return 0;
        case ID_BTN_BROWSE_AUDIO:
            BrowseAudioFile(hWnd);
            return 0;
        case ID_BTN_TRAIN:
            StartTraining(hWnd);
            return 0;
        case ID_BTN_REFRESH_PRESETS:
        {
            HWND combo = GetDlgItem(hWnd, ID_COMBO_SAVED_PRESETS);
            const LRESULT sel = SendMessageW(combo, CB_GETCURSEL, 0, 0);
            if (sel != CB_ERR)
            {
                wchar_t name[256] = {};
                SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(sel), reinterpret_cast<LPARAM>(name));
                SetWindowTextW(GetDlgItem(hWnd, ID_EDIT_PRESET), name);
                SetWindowTextW(GetDlgItem(hWnd, ID_EDIT_MODE), L"ai");
                SetStatus(hWnd, L"Preset IA selectionne");
            }
            return 0;
        }
        case ID_BTN_OPEN_PRESETS:
            OpenPresetsFolder(hWnd);
            return 0;
        default:
            break;
        }
        break;

    case WM_CLOSE:
        StopBackend(hWnd);
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow)
{
    const wchar_t* clsName = L"VoiceChangerUIMvpWindow";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = clsName;

    if (!RegisterClassExW(&wc)) return 1;

    HWND hWnd = CreateWindowExW(
        0,
        clsName,
        L"Voice Changer UI MVP",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        430,
        580,
        nullptr,
        nullptr,
        hInst,
        nullptr
    );

    if (!hWnd) return 1;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
