// Minimal C Win32 Augustus updater
// Includes necessary headers for WinINet and Shell Dispatch

#include <windows.h>
#include <winver.h>
#include <wininet.h>
#include <urlmon.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shldisp.h>
#include <stdio.h>
#include <tchar.h>
#include <strsafe.h>

#pragma comment(lib, "version.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

#define OWNER L"Keriew"
#define REPO L"augustus"
#define BRANCH L"master"
#define ZIP_URL L"https://augustus.josecadete.net/download/latest/development/windows"
#define API_URL L"https://api.github.com/repos/Keriew/augustus/commits/master"
// 1) Read ProductVersion string (try en-US first, then fall back to any translation)
static BOOL read_product_version(const wchar_t *exe, wchar_t *out, DWORD cchOut)
{
    DWORD dummy = 0;
    DWORD size = GetFileVersionInfoSizeW(exe, &dummy);
    if (!size)
        return FALSE;

    BYTE *data = (BYTE *)HeapAlloc(GetProcessHeap(), 0, size);
    if (!data)
        return FALSE;

    BOOL ok = FALSE;
    if (GetFileVersionInfoW(exe, 0, size, data))
    {
        WCHAR *verStr = NULL;
        UINT verLen = 0;

        // Try fixed en-US Unicode block first: 0409 (en-US), 04B0 (Unicode)
        if (VerQueryValueW(data, L"\\StringFileInfo\\040904B0\\ProductVersion",
                           (LPVOID *)&verStr, &verLen) &&
            verStr && verLen)
        {
            wcsncpy_s(out, cchOut, verStr, _TRUNCATE);
            ok = TRUE;
        }
        else
        {
            // Fallback: enumerate translations
            typedef struct
            {
                WORD wLanguage;
                WORD wCodePage;
            } LANGANDCODEPAGE;
            LANGANDCODEPAGE *lpTranslate = NULL;
            UINT cbTranslate = 0;

            if (VerQueryValueW(data, L"\\VarFileInfo\\Translation",
                               (LPVOID *)&lpTranslate, &cbTranslate) &&
                cbTranslate)
            {
                UINT n = cbTranslate / sizeof(LANGANDCODEPAGE);
                for (UINT i = 0; i < n && !ok; ++i)
                {
                    WCHAR sub[64];
                    swprintf_s(sub, 64, L"\\StringFileInfo\\%04x%04x\\ProductVersion",
                               lpTranslate[i].wLanguage, lpTranslate[i].wCodePage);
                    if (VerQueryValueW(data, sub, (LPVOID *)&verStr, &verLen) && verStr && verLen)
                    {
                        wcsncpy_s(out, cchOut, verStr, _TRUNCATE);
                        ok = TRUE;
                    }
                }
            }
        }
    }

    HeapFree(GetProcessHeap(), 0, data);
    return ok;
}

// 2) Extract the 7-char commit suffix from the ProductVersion
static BOOL commit_from_product_version(const wchar_t *pv, wchar_t *short7, DWORD cchShort7)
{
    if (!pv)
        return FALSE;
    const wchar_t *dash = wcschr(pv, L'-');
    if (!dash || wcslen(dash + 1) < 7)
        return FALSE;
    wcsncpy_s(short7, cchShort7, dash + 1, 7);
    return TRUE;
}

BOOL GetProductVersionCommit(LPCWSTR exePath, WCHAR *commit, size_t cchCommit)
{
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(exePath, &handle);
    if (!size)
        return FALSE;

    BYTE *data = (BYTE *)HeapAlloc(GetProcessHeap(), 0, size);
    if (!data)
        return FALSE;

    if (!GetFileVersionInfoW(exePath, 0, size, data))
    {
        HeapFree(GetProcessHeap(), 0, data);
        return FALSE;
    }

    BOOL result = FALSE;
    WCHAR productVersion[256] = {0};

    // Try the improved read_product_version function first
    if (read_product_version(exePath, productVersion, 256))
    {
        // Look for commit hash after the last dash
        LPWSTR dash = wcsrchr(productVersion, L'-');
        if (dash && wcslen(dash + 1) >= 7)
        {
            wcsncpy_s(commit, cchCommit, dash + 1, 7);
            commit[7] = 0;
            result = TRUE;
        }
    }

    // Fallback: try different translation blocks if the first method failed
    if (!result)
    {
        struct LANGANDCODEPAGE
        {
            WORD wLanguage;
            WORD wCodePage;
        } *lpTranslate;
        UINT cbTranslate;

        if (VerQueryValueW(data, L"\\VarFileInfo\\Translation", (LPVOID *)&lpTranslate, &cbTranslate))
        {
            UINT numTranslations = cbTranslate / sizeof(struct LANGANDCODEPAGE);

            // Try all available translations
            for (UINT i = 0; i < numTranslations && !result; i++)
            {
                WCHAR subBlock[64];
                StringCchPrintfW(subBlock, 64, L"\\StringFileInfo\\%04x%04x\\ProductVersion",
                                 lpTranslate[i].wLanguage, lpTranslate[i].wCodePage);

                LPWSTR pv = NULL;
                UINT pvLen = 0;
                if (VerQueryValueW(data, subBlock, (LPVOID *)&pv, &pvLen) && pv && pvLen > 0)
                {
                    // Look for commit hash after the last dash
                    LPWSTR dash = wcsrchr(pv, L'-');
                    if (dash && wcslen(dash + 1) >= 7)
                    {
                        wcsncpy_s(commit, cchCommit, dash + 1, 7);
                        commit[7] = 0;
                        result = TRUE;
                        break;
                    }
                }
            }
        }
    }

    HeapFree(GetProcessHeap(), 0, data);
    return result;
}

BOOL GetLatestCommitFromGitHub(WCHAR *commit, size_t cchCommit)
{
    HINTERNET hInet = InternetOpenW(L"augustus-updater", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet)
        return FALSE;
    HINTERNET hUrl = InternetOpenUrlW(hInet, API_URL, L"User-Agent: augustus-updater\r\n", -1, INTERNET_FLAG_RELOAD, 0);
    if (!hUrl)
    {
        InternetCloseHandle(hInet);
        return FALSE;
    }
    char buf[4096];
    DWORD read;
    if (!InternetReadFile(hUrl, buf, sizeof(buf) - 1, &read) || read == 0)
    {
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInet);
        return FALSE;
    }
    buf[read] = 0;
    char *p = strstr(buf, "\"sha\":\"");
    if (!p || strlen(p) < 15)
    {
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInet);
        return FALSE;
    }
    p += 7; // skip "sha":"
    for (int i = 0; i < 7 && p[i]; ++i)
        commit[i] = (WCHAR)p[i];
    commit[7] = 0;
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInet);
    return TRUE;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR lpCmd, int nShow)
{
    WCHAR exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    PathAppendW(exePath, L"augustus.exe");
    if (!PathFileExistsW(exePath))
    {
        MessageBoxW(NULL, L"augustus.exe not found.", L"Updater", MB_ICONERROR);
        return 1;
    }
    WCHAR localCommit[16] = L"", remoteCommit[16] = L"";

    if (!GetProductVersionCommit(exePath, localCommit, 16))
    {
        wcscpy_s(localCommit, 16, L"unknown");

        // Debug: try to read the raw ProductVersion to see what's actually there
        WCHAR rawVersion[256] = {0};
        if (read_product_version(exePath, rawVersion, 256))
        {
            WCHAR debugMsg[512];
            StringCchPrintfW(debugMsg, 512, L"Could not extract commit from version info.\n\nRaw ProductVersion: %s\n\nExpected format: x.x.x-<7char-commit>", rawVersion);
            MessageBoxW(NULL, debugMsg, L"Version Debug Info", MB_ICONINFORMATION);
        }
        else
        {
            MessageBoxW(NULL, L"Could not read any ProductVersion info from augustus.exe", L"Version Debug Info", MB_ICONINFORMATION);
        }
    }
    if (!GetLatestCommitFromGitHub(remoteCommit, 16))
    {
        if (MessageBoxW(NULL, L"Could not check GitHub. Update anyway?", L"Updater", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return 0;
    }
    else
    {
        if (_wcsicmp(localCommit, remoteCommit) == 0)
        {
            if (MessageBoxW(NULL, L"You are already on the latest build.\n\nWould you like to launch Augustus now?", L"Updater", MB_YESNO | MB_ICONQUESTION) == IDYES)
            {
                // Launch augustus.exe
                STARTUPINFOW si = {0};
                PROCESS_INFORMATION pi = {0};
                si.cb = sizeof(si);

                if (CreateProcessW(exePath, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
                {
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                }
                else
                {
                    MessageBoxW(NULL, L"Failed to launch Augustus.", L"Error", MB_ICONERROR);
                }
            }
            return 0;
        }
        WCHAR msg[256];
        StringCchPrintfW(msg, 256, L"Installed commit: %s\nLatest commit: %s\nUpdate now?", localCommit, remoteCommit);
        if (MessageBoxW(NULL, msg, L"Update Available", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return 0;
    }
    // Use simple path for zip file in current directory
    WCHAR zipPath[] = L"update.zip";

    // Delete existing zip if present
    DeleteFileW(zipPath);

    HRESULT hr = URLDownloadToFileW(NULL, L"https://augustus.josecadete.net/download/latest/development/windows", zipPath, 0, NULL);
    if (FAILED(hr))
    {
        WCHAR errorMsg[256];
        StringCchPrintfW(errorMsg, 256, L"Failed to download update. Error code: 0x%08X", hr);
        MessageBoxW(NULL, errorMsg, L"Updater", MB_ICONERROR);
        return 1;
    }

    // Verify the zip file exists and has content
    if (!PathFileExistsW(zipPath))
    {
        MessageBoxW(NULL, L"Downloaded zip file not found!", L"Updater", MB_ICONERROR);
        return 1;
    }

    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (GetFileAttributesExW(zipPath, GetFileExInfoStandard, &fileInfo))
    {
        if (fileInfo.nFileSizeLow == 0 && fileInfo.nFileSizeHigh == 0)
        {
            MessageBoxW(NULL, L"Downloaded zip file is empty!", L"Updater", MB_ICONERROR);
            return 1;
        }
    }
    // Get the current timestamp of augustus.exe before extraction
    WIN32_FILE_ATTRIBUTE_DATA originalFileInfo;
    BOOL hasOriginalFile = GetFileAttributesExW(L"augustus.exe", GetFileExInfoStandard, &originalFileInfo);

    // Extract the zip file using a more reliable method
    BOOL extractionSucceeded = FALSE;

    // Method 1: Try using Windows built-in expand command
    WCHAR cmdLine[512];
    StringCchPrintfW(cmdLine, 512, L"expand \"%s\" -F:* .", zipPath);

    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        WaitForSingleObject(pi.hProcess, 10000); // Wait up to 10 seconds
        DWORD exitCode;
        if (GetExitCodeProcess(pi.hProcess, &exitCode))
        {
            if (exitCode == 0)
            {
                // Verify extraction by checking if augustus.exe was actually modified
                WIN32_FILE_ATTRIBUTE_DATA newFileInfo;
                if (PathFileExistsW(L"augustus.exe") &&
                    GetFileAttributesExW(L"augustus.exe", GetFileExInfoStandard, &newFileInfo))
                {
                    // Check if the file was modified (different timestamp or size)
                    if (!hasOriginalFile ||
                        CompareFileTime(&originalFileInfo.ftLastWriteTime, &newFileInfo.ftLastWriteTime) != 0 ||
                        originalFileInfo.nFileSizeLow != newFileInfo.nFileSizeLow)
                    {
                        extractionSucceeded = TRUE;
                    }
                }
            }
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    // Method 2: If expand fails, try PowerShell
    if (!extractionSucceeded)
    {
        StringCchPrintfW(cmdLine, 512, L"powershell -Command \"Expand-Archive -Path '%s' -DestinationPath '.' -Force\"", zipPath);

        STARTUPINFOW si2 = {0};
        PROCESS_INFORMATION pi2 = {0};
        si2.cb = sizeof(si2);
        si2.dwFlags = STARTF_USESHOWWINDOW;
        si2.wShowWindow = SW_HIDE;

        if (CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si2, &pi2))
        {
            WaitForSingleObject(pi2.hProcess, 15000); // Wait up to 15 seconds
            DWORD exitCode;
            if (GetExitCodeProcess(pi2.hProcess, &exitCode))
            {
                if (exitCode == 0)
                {
                    // Verify extraction by checking if augustus.exe was actually modified
                    WIN32_FILE_ATTRIBUTE_DATA newFileInfo;
                    if (PathFileExistsW(L"augustus.exe") &&
                        GetFileAttributesExW(L"augustus.exe", GetFileExInfoStandard, &newFileInfo))
                    {
                        // Check if the file was modified (different timestamp or size)
                        if (!hasOriginalFile ||
                            CompareFileTime(&originalFileInfo.ftLastWriteTime, &newFileInfo.ftLastWriteTime) != 0 ||
                            originalFileInfo.nFileSizeLow != newFileInfo.nFileSizeLow)
                        {
                            extractionSucceeded = TRUE;
                        }
                    }
                }
            }
            CloseHandle(pi2.hProcess);
            CloseHandle(pi2.hThread);
        }
    }

    // Method 3: If both fail, show detailed debug information
    if (!extractionSucceeded)
    {
        WCHAR debugMsg[768];
        WIN32_FILE_ATTRIBUTE_DATA currentFileInfo;
        BOOL currentExists = GetFileAttributesExW(L"augustus.exe", GetFileExInfoStandard, &currentFileInfo);

        StringCchPrintfW(debugMsg, 768,
                         L"Both extraction methods failed.\n\n"
                         L"Zip file size: %u bytes\n"
                         L"Zip exists: %s\n"
                         L"Augustus.exe exists: %s\n"
                         L"Augustus.exe size: %u bytes\n"
                         L"File was modified: %s\n\n"
                         L"Try manually extracting update.zip to see what's inside.",
                         fileInfo.nFileSizeLow,
                         PathFileExistsW(zipPath) ? L"Yes" : L"No",
                         currentExists ? L"Yes" : L"No",
                         currentExists ? currentFileInfo.nFileSizeLow : 0,
                         (hasOriginalFile && currentExists &&
                          CompareFileTime(&originalFileInfo.ftLastWriteTime, &currentFileInfo.ftLastWriteTime) != 0)
                             ? L"Yes"
                             : L"No");
        MessageBoxW(NULL, debugMsg, L"Extraction Debug Info", MB_ICONINFORMATION);
    }

    if (extractionSucceeded)
    {
        // Clean up downloaded zip file only if extraction succeeded
        DeleteFileW(zipPath);

        if (MessageBoxW(NULL, L"Update completed successfully!\nAugustus.exe has been updated.\n\nWould you like to launch Augustus now?", L"Updater", MB_YESNO | MB_ICONQUESTION) == IDYES)
        {
            // Launch augustus.exe
            STARTUPINFOW si = {0};
            PROCESS_INFORMATION pi = {0};
            si.cb = sizeof(si);

            if (CreateProcessW(exePath, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
            {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
            else
            {
                MessageBoxW(NULL, L"Update successful, but failed to launch Augustus.", L"Updater", MB_ICONWARNING);
            }
        }
    }
    else
    {
        // Don't delete the zip file so user can manually inspect it
        MessageBoxW(NULL, L"Failed to extract update.\n\nThe zip file 'update.zip' has been left in the current directory for manual inspection.\n\nYou can try extracting it manually or check if it's a valid zip file.", L"Updater", MB_ICONERROR);
        return 1;
    }
    return 0;
}
