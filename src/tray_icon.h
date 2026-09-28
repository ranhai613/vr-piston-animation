#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <filesystem>
#include <string>

class TrayIcon
{
public:
    TrayIcon( std::atomic_bool& shouldStop,
              std::filesystem::path logPath );
    TrayIcon( const TrayIcon& ) = delete;
    TrayIcon& operator=( const TrayIcon& ) = delete;
    ~TrayIcon();

    void processMessages();
    void setAnimating( bool animating );

private:
    static LRESULT CALLBACK windowProc( HWND window,
                                        UINT message,
                                        WPARAM wParam,
                                        LPARAM lParam );

    void addIcon();
    void updateIcon();
    void showContextMenu();
    void openLog();

    std::atomic_bool& m_shouldStop;
    std::filesystem::path m_logPath;
    std::wstring m_status = L"Idle";
    HWND m_window = nullptr;
    HINSTANCE m_instance = nullptr;
    UINT m_taskbarCreatedMessage = 0;
    bool m_iconAdded = false;
    bool m_animating = false;
};
