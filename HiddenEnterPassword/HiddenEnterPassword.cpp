#include <windows.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <fstream>
#include <string>
#include <thread>
#include <fcntl.h>
#include <io.h>
#include "json.hpp"

using json = nlohmann::json;

// Глобальные переменные
std::wstring g_windowTitle;
std::wstring g_password;
int g_editCount = 0;
bool g_passwordEntered = false;

// Преобразование UTF-8 строки в std::wstring
std::wstring Utf8ToWide(const std::string& str)
{
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size_needed);
    return wstr;
}

// Отправка текста через эмуляцию нажатий клавиш
void SendText(const std::wstring& text)
{
    for (wchar_t ch : text) {
        SHORT vk = VkKeyScanW(ch);
        if (vk == -1) continue;

        INPUT inputs[2] = {};

        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = vk & 0xFF;

        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = vk & 0xFF;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

        SendInput(2, inputs, sizeof(INPUT));
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}

// Установка автозапуска программы
void SetAutoRun()
{
    HKEY hKey;
    const wchar_t* runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    if (RegOpenKeyExW(HKEY_CURRENT_USER, runKey, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        DWORD dataSize = (DWORD)((wcslen(exePath) + 1) * sizeof(wchar_t));
        RegSetValueExW(hKey, L"HiddenEnterPassword", 0, REG_SZ, (BYTE*)exePath, dataSize);
        RegCloseKey(hKey);
    }
}

// Загрузка конфигурации из файла
bool LoadConfig()
{
    std::ifstream file("config.json", std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    try {
        json j = json::parse(content);
        if (!j["password"].is_string() || !j["window_title"].is_string()) {
            return false;
        }

        g_password = Utf8ToWide(j["password"].get<std::string>());
        g_windowTitle = Utf8ToWide(j["window_title"].get<std::string>());
    }
    catch (...) {
        return false;
    }

    return true;
}

// Перебор дочерних окон
BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam)
{
    wchar_t className[256];
    GetClassNameW(hwnd, className, sizeof(className) / sizeof(wchar_t));

    if (wcscmp(className, L"TEdit") == 0) {
        g_editCount++;
        if (g_editCount == 2) {
            SetFocus(hwnd);
            SendText(g_password);
        }
    }
    else if (wcscmp(className, L"TButton") == 0) {
        wchar_t buttonText[256];
        GetWindowTextW(hwnd, buttonText, sizeof(buttonText) / sizeof(wchar_t));
        if (wcscmp(buttonText, L"OK") == 0) {
            SendMessageW(hwnd, BM_CLICK, 0, 0);
        }
    }

    return TRUE;
}

// Перебор всех окон
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    wchar_t title[512];
    GetWindowTextW(hwnd, title, sizeof(title) / sizeof(wchar_t));

    if (g_windowTitle == title) {
        if (!g_passwordEntered) {
            g_editCount = 0;
            SetForegroundWindow(hwnd);
            SetActiveWindow(hwnd);
            BringWindowToTop(hwnd);

            EnumChildWindows(hwnd, EnumChildProc, 0);
            g_passwordEntered = true;
        }
    }

    return TRUE;
}

// Проверка наличия окна
bool IsWindowPresent()
{
    bool found = false;
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        wchar_t title[512];
        GetWindowTextW(hwnd, title, sizeof(title) / sizeof(wchar_t));
        if (g_windowTitle == title) {
            *((bool*)lParam) = true;
            return FALSE;
        }
        return TRUE;
        }, (LPARAM)&found);
    return found;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)

{
    if (!LoadConfig()) {
        return 1;
    }

    SetAutoRun();

    while (true) {
        EnumWindows(EnumWindowsProc, 0);

        if (!IsWindowPresent()) {
            g_passwordEntered = false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    return 0;
}
