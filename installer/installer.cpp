// Sweepbar installer for Windows x64.
// The plugin binary is embedded as a resource; this program writes it to
//   C:\Program Files\Common Files\VST3\Sweepbar.vst3\Contents\x86_64-win\Sweepbar.vst3
// and registers an uninstaller under Settings > Apps.
//
//   SweepbarSetup.exe              interactive install
//   SweepbarSetup.exe /S           silent install
//   "Uninstall Sweepbar.exe" /uninstall [/S]
#ifndef UNICODE
 #define UNICODE
#endif
#ifndef _UNICODE
 #define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <string>
#include "resource.h"

namespace
{
const wchar_t* kAppName    = L"Sweepbar";
const wchar_t* kVersion    = SWEEPBAR_VERSION_W;
const wchar_t* kPublisher  = L"Sweepbar";
const wchar_t* kUninstKey  = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Sweepbar";
bool silent = false;

std::wstring knownFolder (REFKNOWNFOLDERID id)
{
    PWSTR p = nullptr;
    std::wstring result;
    if (SUCCEEDED (SHGetKnownFolderPath (id, 0, nullptr, &p)))
        result = p;
    CoTaskMemFree (p);
    return result;
}

std::wstring vst3Dir()       { return knownFolder (FOLDERID_ProgramFilesCommon) + L"\\VST3"; }
std::wstring bundleDir()     { return vst3Dir() + L"\\Sweepbar.vst3"; }
std::wstring binaryPath()    { return bundleDir() + L"\\Contents\\x86_64-win\\Sweepbar.vst3"; }
std::wstring appDir()        { return knownFolder (FOLDERID_ProgramFiles) + L"\\Sweepbar"; }
std::wstring uninstallerPath() { return appDir() + L"\\Uninstall Sweepbar.exe"; }

std::wstring selfPath()
{
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW (nullptr, buf, (DWORD) (sizeof (buf) / sizeof (buf[0])));
    return std::wstring (buf, n);
}

std::wstring errorText (DWORD err)
{
    wchar_t* msg = nullptr;
    FormatMessageW (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    nullptr, err, 0, (LPWSTR) &msg, 0, nullptr);
    std::wstring s = msg ? msg : L"Unknown error";
    LocalFree (msg);
    while (! s.empty() && (s.back() == L'\n' || s.back() == L'\r')) s.pop_back();
    return s + L" (code " + std::to_wstring (err) + L")";
}

// ---- dialogs ---------------------------------------------------------------

int dialog (PCWSTR title, PCWSTR main, PCWSTR content, const TASKDIALOG_BUTTON* buttons, UINT numButtons,
            PCWSTR icon, int defaultButton)
{
    if (silent)
        return defaultButton;
    TASKDIALOGCONFIG cfg {};
    cfg.cbSize = sizeof (cfg);
    cfg.hInstance = GetModuleHandleW (nullptr);
    cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_SIZE_TO_CONTENT;
    cfg.pszWindowTitle = title;
    cfg.pszMainIcon = icon;
    cfg.pszMainInstruction = main;
    cfg.pszContent = content;
    cfg.pButtons = buttons;
    cfg.cButtons = numButtons;
    cfg.nDefaultButton = defaultButton;
    if (numButtons == 0)
        cfg.dwCommonButtons = TDCBF_OK_BUTTON;
    int pressed = IDCANCEL;
    if (FAILED (TaskDialogIndirect (&cfg, &pressed, nullptr, nullptr)))
    {
        // Fallback if task dialogs aren't available for some reason.
        const std::wstring text = std::wstring (main) + L"\n\n" + content;
        const UINT type = numButtons > 1 ? MB_OKCANCEL : MB_OK;
        return MessageBoxW (nullptr, text.c_str(), title, type) == IDOK ? defaultButton : IDCANCEL;
    }
    return pressed;
}

void showError (const std::wstring& main, const std::wstring& content)
{
    dialog (L"Sweepbar Setup", main.c_str(), content.c_str(), nullptr, 0, TD_ERROR_ICON, IDOK);
}

// ---- file helpers -----------------------------------------------------------

bool ensureDir (const std::wstring& dir)
{
    const int r = SHCreateDirectoryExW (nullptr, dir.c_str(), nullptr);
    return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
}

DWORD writeResource (int id, const std::wstring& path)
{
    HRSRC res = FindResourceW (nullptr, MAKEINTRESOURCEW (id), RT_RCDATA);
    if (! res) return GetLastError();
    HGLOBAL data = LoadResource (nullptr, res);
    const void* bytes = data ? LockResource (data) : nullptr;
    const DWORD size = SizeofResource (nullptr, res);
    if (! bytes || size == 0) return ERROR_RESOURCE_DATA_NOT_FOUND;

    HANDLE f = CreateFileW (path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return GetLastError();
    DWORD written = 0;
    const BOOL ok = WriteFile (f, bytes, size, &written, nullptr);
    const DWORD err = ok && written == size ? ERROR_SUCCESS : GetLastError();
    CloseHandle (f);
    return err;
}

void setString (HKEY key, PCWSTR name, const std::wstring& value)
{
    RegSetValueExW (key, name, 0, REG_SZ, (const BYTE*) value.c_str(), (DWORD) ((value.size() + 1) * sizeof (wchar_t)));
}

void setDword (HKEY key, PCWSTR name, DWORD value)
{
    RegSetValueExW (key, name, 0, REG_DWORD, (const BYTE*) &value, sizeof (value));
}

// ---- install ----------------------------------------------------------------

int install()
{
    const std::wstring target = bundleDir();
    const bool upgrading = GetFileAttributesW (binaryPath().c_str()) != INVALID_FILE_ATTRIBUTES;

    const std::wstring content =
        L"Sweepbar " + std::wstring (kVersion) + L" will be installed to:\n" + target +
        L"\n\nFL Studio finds plugins in this folder automatically. Close FL Studio before continuing."
        + (upgrading ? L"\n\nAn earlier copy of Sweepbar is already there and will be replaced." : L"");

    const TASKDIALOG_BUTTON buttons[] = { { 100, L"Install" }, { IDCANCEL, L"Cancel" } };
    if (dialog (L"Sweepbar Setup", L"Install the Sweepbar VST3 plugin", content.c_str(),
                buttons, 2, TD_INFORMATION_ICON, 100) != 100)
        return 1;

    const std::wstring binDir = target + L"\\Contents\\x86_64-win";
    if (! ensureDir (binDir) || ! ensureDir (target + L"\\Contents\\Resources"))
    {
        showError (L"Couldn't create the plugin folder", errorText (GetLastError()) + L"\n\n" + binDir);
        return 2;
    }

    // Retry if FL Studio (or another host) still has the old plugin open.
    for (;;)
    {
        const DWORD err = writeResource (IDR_PLUGIN, binaryPath());
        if (err == ERROR_SUCCESS)
            break;
        if (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED || err == ERROR_USER_MAPPED_FILE)
        {
            const TASKDIALOG_BUTTON retry[] = { { IDRETRY, L"Try again" }, { IDCANCEL, L"Cancel" } };
            if (silent || dialog (L"Sweepbar Setup", L"Sweepbar is in use",
                                  L"Close FL Studio (and any other music app using Sweepbar), then click Try again.",
                                  retry, 2, TD_WARNING_ICON, IDRETRY) != IDRETRY)
                return 3;
            continue;
        }
        showError (L"Couldn't copy the plugin", errorText (err) + L"\n\n" + binaryPath());
        return 3;
    }

    // Uninstaller + Settings > Apps entry. Not fatal if this part fails.
    if (ensureDir (appDir()))
    {
        const std::wstring self = selfPath();
        if (_wcsicmp (self.c_str(), uninstallerPath().c_str()) != 0)
            CopyFileW (self.c_str(), uninstallerPath().c_str(), FALSE);
        writeResource (IDR_README, appDir() + L"\\README.txt");

        HKEY key;
        if (RegCreateKeyExW (HKEY_LOCAL_MACHINE, kUninstKey, 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY,
                             nullptr, &key, nullptr) == ERROR_SUCCESS)
        {
            const std::wstring uninst = L"\"" + uninstallerPath() + L"\" /uninstall";
            setString (key, L"DisplayName", kAppName);
            setString (key, L"DisplayVersion", kVersion);
            setString (key, L"Publisher", kPublisher);
            setString (key, L"DisplayIcon", uninstallerPath() + L",0");
            setString (key, L"InstallLocation", target);
            setString (key, L"UninstallString", uninst);
            setString (key, L"QuietUninstallString", uninst + L" /S");
            setDword (key, L"NoModify", 1);
            setDword (key, L"NoRepair", 1);
            HRSRC res = FindResourceW (nullptr, MAKEINTRESOURCEW (IDR_PLUGIN), RT_RCDATA);
            setDword (key, L"EstimatedSize", res ? SizeofResource (nullptr, res) / 1024 + 1024 : 7000);
            RegCloseKey (key);
        }
    }

    const TASKDIALOG_BUTTON done[] = { { 100, L"Done" }, { 101, L"Show plugin folder" } };
    const int pressed = dialog (L"Sweepbar Setup", L"Sweepbar is installed",
        L"Next, in FL Studio:\n"
        L"1.  Open Options > Manage plugins and click Find more plugins.\n"
        L"2.  In the Mixer, select your vocal's insert, click an empty slot and choose Sweepbar.\n"
        L"3.  Press play. The effect fires while the song is playing.\n\n"
        L"To remove it later, use Settings > Apps > Sweepbar.",
        done, 2, TD_INFORMATION_ICON, 100);
    if (pressed == 101)
        ShellExecuteW (nullptr, L"open", vst3Dir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return 0;
}

// ---- uninstall --------------------------------------------------------------

void removeFile (const std::wstring& path)
{
    if (! DeleteFileW (path.c_str()) && GetFileAttributesW (path.c_str()) != INVALID_FILE_ATTRIBUTES)
        MoveFileExW (path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
}

void removeDir (const std::wstring& path)
{
    if (! RemoveDirectoryW (path.c_str()) && GetFileAttributesW (path.c_str()) != INVALID_FILE_ATTRIBUTES)
        MoveFileExW (path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
}

int uninstall()
{
    const TASKDIALOG_BUTTON buttons[] = { { 100, L"Uninstall" }, { IDCANCEL, L"Cancel" } };
    if (dialog (L"Sweepbar Uninstall", L"Remove Sweepbar?",
                L"This removes the Sweepbar plugin from your VST3 folder. Your FL Studio projects are not changed, "
                L"but any that use Sweepbar will show it as missing. Close FL Studio first.",
                buttons, 2, TD_WARNING_ICON, 100) != 100)
        return 1;

    for (;;)
    {
        if (DeleteFileW (binaryPath().c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)
            break;
        const TASKDIALOG_BUTTON retry[] = { { IDRETRY, L"Try again" }, { IDCANCEL, L"Cancel" } };
        if (silent || dialog (L"Sweepbar Uninstall", L"Sweepbar is in use",
                              L"Close FL Studio (and any other music app using Sweepbar), then click Try again.",
                              retry, 2, TD_WARNING_ICON, IDRETRY) != IDRETRY)
            return 3;
    }
    removeDir (bundleDir() + L"\\Contents\\x86_64-win");
    removeDir (bundleDir() + L"\\Contents\\Resources");
    removeDir (bundleDir() + L"\\Contents");
    removeDir (bundleDir());

    RegDeleteKeyExW (HKEY_LOCAL_MACHINE, kUninstKey, KEY_WOW64_64KEY, 0);

    removeFile (appDir() + L"\\README.txt");
    // A running exe can't delete itself; Windows removes it at the next restart.
    removeFile (selfPath());
    removeDir (appDir());

    dialog (L"Sweepbar Uninstall", L"Sweepbar has been removed", L"", nullptr, 0, TD_INFORMATION_ICON, IDOK);
    return 0;
}
} // namespace

int WINAPI wWinMain (HINSTANCE, HINSTANCE, PWSTR, int)
{
    CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc { sizeof (icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx (&icc);

    bool doUninstall = false;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW (GetCommandLineW(), &argc))
    {
        for (int i = 1; i < argc; ++i)
        {
            if (_wcsicmp (argv[i], L"/S") == 0 || _wcsicmp (argv[i], L"/silent") == 0) silent = true;
            if (_wcsicmp (argv[i], L"/uninstall") == 0) doUninstall = true;
        }
        LocalFree (argv);
    }

    const int result = doUninstall ? uninstall() : install();
    CoUninitialize();
    return result;
}
