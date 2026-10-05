#include "rebirths_patch.hpp"
#include "prepare_game.hpp"
#include "prepare_feature_settings.hpp"
#include <windows.h>
#include <shlobj.h>
#include <atomic>
#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace rebirths::prepare;
using rebirths::FeatureId;
namespace {
constexpr int kGame = 100, kBrowseGame = 101, kAssets = 102, kBrowseAssets = 103, kPrepare = 104,
              kCancel = 105, kRollback = 106, kUninstall = 107, kPatch4GB = 108, kRestoreExe = 109,
              kAssetsOn = 110, kFF = 111, kSkip = 112, kBattle = 113, kChapter = 114,
              kMovement = 115, kStatus = 116, kCg = 117, kTexture = 118, kTutorial = 119, kNepstation = 120;
constexpr UINT kDone = WM_APP + 1, kProgress = WM_APP + 2;
struct FeatureControl { FeatureId feature; int control; };
// Layout stays local; names, settings bindings and support come from the catalog.
constexpr FeatureControl featureControls[] = {
    {FeatureId::UncompressedAssets, kAssetsOn}, {FeatureId::FastTextureConversion, kTexture},
    {FeatureId::AdvFastForward, kFF}, {FeatureId::AdvAutoSkip, kSkip},
    {FeatureId::SkipTutorials, kTutorial}, {FeatureId::BattleLoadDelaySkip, kBattle},
    {FeatureId::NepstationSkip, kNepstation},
    {FeatureId::SkipChapterIntros, kChapter}, {FeatureId::DungeonMovementFix, kMovement},
};
constexpr bool CompleteFeatureControls() {
    if (std::size(featureControls) != std::size(FeatureBindings)) return false;
    for (const auto& binding : FeatureBindings) {
        size_t count = 0;
        for (const auto& control : featureControls)
            if (control.feature == binding.id) ++count;
        if (count != 1) return false;
    }
    return true;
}
static_assert(CompleteFeatureControls(), "Every managed feature needs exactly one UI control");
HWND mainWindow{}, gameBox{}, assetBox{}, statusBox{};
std::vector<std::filesystem::path> gameFolders;
std::unique_ptr<Game> selected;
std::atomic<bool> cancelled{false}, busy{false};
std::wstring Wide(const std::string& text) {
    int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), count);
    result.pop_back();
    return result;
}
void Status(const std::wstring& text) {
    SetWindowTextW(statusBox, text.c_str());
}
std::wstring Field(HWND box) {
    int length = GetWindowTextLengthW(box);
    std::wstring result(length + 1, L'\0');
    GetWindowTextW(box, result.data(), length + 1);
    result.resize(length);
    return result;
}
void Check(int id, bool value) {
    SendMessageW(GetDlgItem(mainWindow, id), BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
}
bool Checked(int id) {
    return SendMessageW(GetDlgItem(mainWindow, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
}
void Enabled(int id, bool value) {
    EnableWindow(GetDlgItem(mainWindow, id), value);
}
std::filesystem::path OtherProfilePath(std::filesystem::path path, bool half) {
    const std::wstring suffix = L"-cg24v1";
    auto leaf = path.filename().wstring();
    if (half) {
        if (leaf == L"assets")
            leaf = L"cg24-v1";
        else if (leaf.size() < suffix.size() || leaf.substr(leaf.size() - suffix.size()) != suffix)
            leaf += suffix;
    } else if (leaf == L"cg24-v1")
        leaf = L"assets";
    else if (leaf.size() >= suffix.size() && leaf.substr(leaf.size() - suffix.size()) == suffix)
        leaf.resize(leaf.size() - suffix.size());
    else
        leaf += L"-raw";
    path.replace_filename(leaf);
    return path;
}
std::filesystem::path Folder(HWND owner, const wchar_t* title) {
    BROWSEINFOW info{};
    info.hwndOwner = owner;
    info.lpszTitle = title;
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    auto item = SHBrowseForFolderW(&info);
    if (!item)
        return {};
    wchar_t path[32768]{};
    bool ok = SHGetPathFromIDListW(item, path) != FALSE;
    CoTaskMemFree(item);
    return ok ? std::filesystem::path(path) : std::filesystem::path();
}
void Refresh() {
    if (selected) {
        try {
            auto s = ReadSettings(*selected);
            SetWindowTextW(assetBox, s.assets.c_str());
            for (const auto& binding : featureControls)
                Check(binding.control, SettingValue(s, binding.feature));
            Check(kCg, s.transformProfile == TransformProfile::AdvCgHalf24V1);
            Status(L"Ready: " + selected->directory.wstring());
        } catch (const std::exception& e) {
            Status(L"Settings error: " + Wide(e.what()));
        }
    }
    for (const auto& binding : featureControls) {
        const auto& feature = rebirths::FeatureSpecFor(binding.feature);
        const bool supported = selected && rebirths::Supports(binding.feature,
                                      static_cast<rebirths::GameId>(selected->id));
        const bool visible = selected ? supported : std::all_of(rebirths::AllGameIds.begin(),
            rebirths::AllGameIds.end(), [&](auto game) { return rebirths::Supports(feature, game); });
        ShowWindow(GetDlgItem(mainWindow, binding.control), visible ? SW_SHOW : SW_HIDE);
        Enabled(binding.control, supported && !busy);
    }
    for (int id : {kPrepare, kRollback, kUninstall, kPatch4GB, kRestoreExe, kBrowseAssets,
                   kCg})
        Enabled(id, selected && !busy);
    Enabled(kPatch4GB, selected && !busy && selected->originalExecutable);
    Enabled(kRestoreExe, selected && !busy && selected->ntcoreExecutable);
    Enabled(kCancel, busy);
    Enabled(kGame, !busy);
    Enabled(kBrowseGame, !busy);
}
void OpenFolder(const std::filesystem::path& path) {
    try {
        auto game = OpenGame(path);
        selected = std::make_unique<Game>(std::move(game));
        auto found = std::find(gameFolders.begin(), gameFolders.end(), selected->directory);
        if (found == gameFolders.end()) {
            gameFolders.push_back(selected->directory);
            SendMessageW(gameBox, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(selected->directory.c_str()));
            found = std::prev(gameFolders.end());
        }
        SendMessageW(gameBox, CB_SETCURSEL, found - gameFolders.begin(), 0);
        Refresh();
    } catch (const std::exception& e) {
        MessageBoxW(mainWindow, Wide(e.what()).c_str(), L"Unsupported game folder",
                    MB_OK | MB_ICONERROR);
    }
}
Settings SettingsFromUi() {
    auto s = ReadSettings(*selected);
    auto activeAssets = s.assets;
    s.assets = Field(assetBox);
    for (const auto& binding : featureControls)
        SettingValue(s, binding.feature) = Checked(binding.control);
    if (Checked(kCg))
        s.transformProfile = TransformProfile::AdvCgHalf24V1;
    else if (s.assets != activeAssets)
        s.transformProfile = TransformProfile::Raw;
    return s;
}
void Begin(int action) {
    if (!selected || busy)
        return;
    auto game = *selected;
    Settings settings;
    if (action == kPrepare)
        try {
            settings = SettingsFromUi();
        } catch (const std::exception& e) {
            MessageBoxW(mainWindow, Wide(e.what()).c_str(), L"Settings error",
                        MB_OK | MB_ICONERROR);
            return;
        }
    cancelled = false;
    busy = true;
    Refresh();
    Status(L"Working...");
    std::thread([game = std::move(game), settings = std::move(settings), action]() {
        auto* outcome = new std::wstring;
        try {
            switch (action) {
            case kPrepare:
                PrepareAndInstall(
                    game, settings,
                    [](const Progress& p) {
                        auto* update = new std::wstring(p.stage + L" " + p.current + L" " +
                                                        std::to_wstring(p.completed) + L"/" +
                                                        std::to_wstring(p.total));
                        PostMessageW(mainWindow, kProgress, 0, reinterpret_cast<LPARAM>(update));
                    },
                    [] { return cancelled.load(); });
                break;
            case kRollback:
                Rollback(game);
                break;
            case kUninstall:
                Uninstall(game);
                break;
            case kPatch4GB:
                Apply4GB(game);
                break;
            case kRestoreExe:
                RestoreOriginalExe(game);
                break;
            }
            *outcome = L"Completed successfully.";
        } catch (const std::exception& e) {
            *outcome = L"Failed: " + Wide(e.what());
        }
        PostMessageW(mainWindow, kDone, 0, reinterpret_cast<LPARAM>(outcome));
    }).detach();
}
HWND Control(const wchar_t* cls, const wchar_t* label, DWORD style, int x, int y, int width,
             int height, int id) {
    auto h = CreateWindowExW(0, cls, label, WS_CHILD | WS_VISIBLE | style, x, y, width, height,
                             mainWindow, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                             GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return h;
}
LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_CREATE: {
        mainWindow = hwnd;
        Control(L"STATIC", L"Game folder", 0, 16, 14, 400, 18, 0);
        gameBox = Control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 16, 34, 545, 300, kGame);
        Control(L"BUTTON", L"Browse...", BS_PUSHBUTTON, 570, 34, 100, 27, kBrowseGame);
        Control(L"STATIC", L"Prepared assets destination", 0, 16, 76, 400, 18, 0);
        assetBox = Control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 16, 96, 545, 25, kAssets);
        Control(L"BUTTON", L"Browse...", BS_PUSHBUTTON, 570, 96, 100, 27, kBrowseAssets);
        Control(L"BUTTON", rebirths::FeatureSpecFor(FeatureId::UncompressedAssets).uiLabel,
                BS_AUTOCHECKBOX, 16, 137, 220, 22, kAssetsOn);
        Control(L"BUTTON", rebirths::FeatureSpecFor(FeatureId::FastTextureConversion).uiLabel,
                BS_AUTOCHECKBOX, 260, 137, 250, 22,
                kTexture);
        Control(L"BUTTON", L"Downscale large ADV CGs (24 MiB+)", BS_AUTOCHECKBOX, 16, 167, 350, 22,
                kCg);
        Control(L"BUTTON", rebirths::FeatureSpecFor(FeatureId::AdvFastForward).uiLabel,
                BS_AUTOCHECKBOX, 16, 197, 220, 22, kFF);
        Control(L"BUTTON", rebirths::FeatureSpecFor(FeatureId::AdvAutoSkip).uiLabel,
                BS_AUTOCHECKBOX, 260, 197, 220, 22, kSkip);
        Control(L"BUTTON", rebirths::FeatureSpecFor(FeatureId::NepstationSkip).uiLabel,
                BS_AUTOCHECKBOX, 510, 197, 160, 22, kNepstation);
        Control(L"BUTTON", rebirths::FeatureSpecFor(FeatureId::BattleLoadDelaySkip).uiLabel,
                BS_AUTOCHECKBOX, 16, 227, 250, 22, kBattle);
        Control(L"BUTTON", rebirths::FeatureSpecFor(FeatureId::SkipChapterIntros).uiLabel,
                BS_AUTOCHECKBOX, 16, 227, 250, 22, kChapter);
        Control(L"BUTTON", rebirths::FeatureSpecFor(FeatureId::DungeonMovementFix).uiLabel,
                BS_AUTOCHECKBOX, 16, 227, 250, 22, kMovement);
        Control(L"BUTTON", rebirths::FeatureSpecFor(FeatureId::SkipTutorials).uiLabel,
                BS_AUTOCHECKBOX, 280, 227, 220, 22, kTutorial);
        Control(L"BUTTON", L"Prepare / Resume", BS_PUSHBUTTON, 16, 271, 180, 34, kPrepare);
        Control(L"BUTTON", L"Cancel", BS_PUSHBUTTON, 206, 271, 110, 34, kCancel);
        Control(L"BUTTON", L"Rollback proxy update", BS_PUSHBUTTON, 16, 320, 190, 31, kRollback);
        Control(L"BUTTON", L"Uninstall proxy", BS_PUSHBUTTON, 216, 320, 170, 31, kUninstall);
        Control(L"BUTTON", L"Apply 4GB patch", BS_PUSHBUTTON, 16, 369, 180, 31, kPatch4GB);
        Control(L"BUTTON", L"Restore original EXE", BS_PUSHBUTTON, 206, 369, 180, 31, kRestoreExe);
        statusBox = Control(L"STATIC", L"Select a supported game folder.", SS_LEFT, 16, 424, 650,
                            60, kStatus);
        try {
            for (auto& path : DetectGames()) {
                gameFolders.push_back(path);
                SendMessageW(gameBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(path.c_str()));
            }
            if (!gameFolders.empty())
                OpenFolder(gameFolders.front());
        } catch (...) {
        }
        Refresh();
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == kGame && HIWORD(w) == CBN_SELCHANGE) {
            auto index = SendMessageW(gameBox, CB_GETCURSEL, 0, 0);
            if (index >= 0 && static_cast<size_t>(index) < gameFolders.size())
                OpenFolder(gameFolders[index]);
        } else if (id == kBrowseGame) {
            auto path = Folder(hwnd, L"Select a Re;Birth or Sega Hard Girls game folder");
            if (!path.empty())
                OpenFolder(path);
        } else if (id == kBrowseAssets) {
            auto path = Folder(hwnd, L"Select an assets destination");
            if (!path.empty())
                SetWindowTextW(assetBox, path.c_str());
        } else if (id == kCg && HIWORD(w) == BN_CLICKED) {
            auto next = OtherProfilePath(Field(assetBox), Checked(kCg));
            SetWindowTextW(assetBox, next.c_str());
        } else if (id == kCancel)
            cancelled = true;
        else if (id >= kPrepare && id <= kRestoreExe && id != kCancel)
            Begin(id);
        return 0;
    }
    case kProgress: {
        std::unique_ptr<std::wstring> update(reinterpret_cast<std::wstring*>(l));
        if (busy)
            Status(*update);
        return 0;
    }
    case kDone: {
        std::unique_ptr<std::wstring> result(reinterpret_cast<std::wstring*>(l));
        busy = false;
        if (selected)
            try {
                selected = std::make_unique<Game>(OpenGame(selected->directory));
            } catch (...) {
            }
        Refresh();
        Status(*result);
        return 0;
    }
    case WM_CLOSE:
        if (busy) {
            cancelled = true;
            Status(L"Cancelling. Please wait until the current file finishes.");
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW cls{};
    cls.lpfnWndProc = Proc;
    cls.hInstance = instance;
    cls.lpszClassName = L"RebirthsPreparer";
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&cls);
    auto window = CreateWindowExW(
        0, cls.lpszClassName, L"Re;Birth release preparer",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN, CW_USEDEFAULT,
        CW_USEDEFAULT, 700, 530, nullptr, nullptr, instance, nullptr);
    ShowWindow(window, show);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CoUninitialize();
    return 0;
}
