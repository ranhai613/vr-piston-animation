#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include "tray_icon.h"

#include <iostream>
#include <stdexcept>
#include <utility>

namespace
{
constexpr wchar_t kWindowClassName[] = L"VRPistonAnimationTrayWindow";
constexpr wchar_t kWindowTitle[] = L"VR Piston Animation";
constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT kTrayIconId = 1;
constexpr UINT kOpenLogCommand = 1001;
constexpr UINT kExitCommand = 1002;

void setTooltip( NOTIFYICONDATAW& iconData, const std::wstring& tooltip )
{
    constexpr size_t capacity
        = sizeof( iconData.szTip ) / sizeof( iconData.szTip[0] );
    size_t copied = 0;
    while ( copied + 1 < capacity && copied < tooltip.size() )
    {
        iconData.szTip[copied] = tooltip[copied];
        ++copied;
    }
    iconData.szTip[copied] = L'\0';
}
} // namespace

TrayIcon::TrayIcon( std::atomic_bool& shouldStop,
                    std::filesystem::path logPath )
    : m_shouldStop( shouldStop )
    , m_logPath( std::move( logPath ) )
    , m_instance( GetModuleHandleW( nullptr ) )
    , m_taskbarCreatedMessage( RegisterWindowMessageW( L"TaskbarCreated" ) )
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof( windowClass );
    windowClass.lpfnWndProc = &TrayIcon::windowProc;
    windowClass.hInstance = m_instance;
    windowClass.lpszClassName = kWindowClassName;
    if ( RegisterClassExW( &windowClass ) == 0
         && GetLastError() != ERROR_CLASS_ALREADY_EXISTS )
    {
        throw std::runtime_error( "Could not register tray window class." );
    }

    m_window = CreateWindowExW( 0,
                                kWindowClassName,
                                kWindowTitle,
                                0,
                                0,
                                0,
                                0,
                                0,
                                nullptr,
                                nullptr,
                                m_instance,
                                this );
    if ( m_window == nullptr )
    {
        UnregisterClassW( kWindowClassName, m_instance );
        throw std::runtime_error( "Could not create tray window." );
    }

    try
    {
        addIcon();
    }
    catch ( ... )
    {
        DestroyWindow( m_window );
        UnregisterClassW( kWindowClassName, m_instance );
        m_window = nullptr;
        throw;
    }
}

TrayIcon::~TrayIcon()
{
    if ( m_iconAdded )
    {
        NOTIFYICONDATAW iconData{};
        iconData.cbSize = sizeof( iconData );
        iconData.hWnd = m_window;
        iconData.uID = kTrayIconId;
        Shell_NotifyIconW( NIM_DELETE, &iconData );
    }
    if ( m_window != nullptr )
    {
        DestroyWindow( m_window );
        UnregisterClassW( kWindowClassName, m_instance );
    }
}

void TrayIcon::addIcon()
{
    NOTIFYICONDATAW iconData{};
    iconData.cbSize = sizeof( iconData );
    iconData.hWnd = m_window;
    iconData.uID = kTrayIconId;
    iconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    iconData.uCallbackMessage = kTrayCallbackMessage;
    iconData.hIcon = LoadIconW( nullptr, MAKEINTRESOURCEW( 32512 ) );
    const std::wstring tooltip = L"VR Piston Animation - " + m_status;
    setTooltip( iconData, tooltip );

    if ( !Shell_NotifyIconW( NIM_ADD, &iconData ) )
    {
        throw std::runtime_error( "Could not add the tray icon." );
    }
    m_iconAdded = true;
}

void TrayIcon::updateIcon()
{
    if ( !m_iconAdded )
    {
        return;
    }

    NOTIFYICONDATAW iconData{};
    iconData.cbSize = sizeof( iconData );
    iconData.hWnd = m_window;
    iconData.uID = kTrayIconId;
    iconData.uFlags = NIF_TIP;
    const std::wstring tooltip = L"VR Piston Animation - " + m_status;
    setTooltip( iconData, tooltip );
    Shell_NotifyIconW( NIM_MODIFY, &iconData );
}

void TrayIcon::setAnimating( bool animating )
{
    if ( m_animating == animating )
    {
        return;
    }

    m_animating = animating;
    m_status = m_animating ? L"Animating" : L"Idle";
    updateIcon();
}

void TrayIcon::processMessages()
{
    MSG message{};
    while ( PeekMessageW( &message, nullptr, 0, 0, PM_REMOVE ) )
    {
        if ( message.message == WM_QUIT )
        {
            m_shouldStop.store( true );
            continue;
        }
        TranslateMessage( &message );
        DispatchMessageW( &message );
    }
}

void TrayIcon::showContextMenu()
{
    HMENU menu = CreatePopupMenu();
    if ( menu == nullptr )
    {
        return;
    }

    const std::wstring status = L"Status: " + m_status;
    AppendMenuW( menu, MF_STRING | MF_GRAYED, 0, status.c_str() );
    AppendMenuW( menu, MF_SEPARATOR, 0, nullptr );
    AppendMenuW( menu, MF_STRING, kOpenLogCommand, L"Open log" );
    AppendMenuW( menu, MF_STRING, kExitCommand, L"Exit" );

    POINT cursor{};
    GetCursorPos( &cursor );
    SetForegroundWindow( m_window );
    TrackPopupMenu( menu,
                    TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_BOTTOMALIGN,
                    cursor.x,
                    cursor.y,
                    0,
                    m_window,
                    nullptr );
    PostMessageW( m_window, WM_NULL, 0, 0 );
    DestroyMenu( menu );
}

void TrayIcon::openLog()
{
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW( m_window,
                       L"open",
                       m_logPath.c_str(),
                       nullptr,
                       nullptr,
                       SW_SHOWNORMAL ) );
    if ( result <= 32 )
    {
        MessageBoxW( m_window,
                     L"Could not open the log file.",
                     kWindowTitle,
                     MB_OK | MB_ICONWARNING );
    }
}

LRESULT CALLBACK TrayIcon::windowProc( HWND window,
                                       UINT message,
                                       WPARAM wParam,
                                       LPARAM lParam )
{
    auto* const self = reinterpret_cast<TrayIcon*>(
        GetWindowLongPtrW( window, GWLP_USERDATA ) );
    if ( message == WM_NCCREATE )
    {
        const auto* const createStruct
            = reinterpret_cast<const CREATESTRUCTW*>( lParam );
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>( createStruct->lpCreateParams ) );
        return DefWindowProcW( window, message, wParam, lParam );
    }

    if ( self == nullptr )
    {
        return DefWindowProcW( window, message, wParam, lParam );
    }

    if ( message == self->m_taskbarCreatedMessage )
    {
        self->m_iconAdded = false;
        try
        {
            self->addIcon();
        }
        catch ( const std::exception& error )
        {
            std::cerr << "Could not restore tray icon: " << error.what()
                      << "\n";
        }
        return 0;
    }

    switch ( message )
    {
    case kTrayCallbackMessage:
        if ( lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU
             || lParam == WM_LBUTTONDBLCLK )
        {
            self->showContextMenu();
        }
        return 0;
    case WM_COMMAND:
        switch ( LOWORD( wParam ) )
        {
        case kOpenLogCommand:
            self->openLog();
            return 0;
        case kExitCommand:
            self->m_shouldStop.store( true );
            return 0;
        default:
            break;
        }
        break;
    case WM_QUERYENDSESSION:
        self->m_shouldStop.store( true );
        return TRUE;
    case WM_ENDSESSION:
        if ( wParam != 0 )
        {
            self->m_shouldStop.store( true );
        }
        return 0;
    case WM_CLOSE:
        self->m_shouldStop.store( true );
        return 0;
    default:
        break;
    }

    return DefWindowProcW( window, message, wParam, lParam );
}
