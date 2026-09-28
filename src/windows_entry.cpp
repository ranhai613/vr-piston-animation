#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include "app_paths.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

int runApplication( int argc, char** argv );

namespace
{
std::filesystem::path executableDirectory()
{
    std::vector<wchar_t> buffer( 32768 );
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>( buffer.size() ) );
    if ( length == 0 || length >= buffer.size() )
    {
        throw std::runtime_error( "Could not determine executable path." );
    }
    return std::filesystem::path(
               std::wstring( buffer.data(), static_cast<size_t>( length ) ) )
        .parent_path();
}

std::filesystem::path logFilePath()
{
    const DWORD requiredLength
        = GetEnvironmentVariableW( L"LOCALAPPDATA", nullptr, 0 );
    std::filesystem::path root;
    if ( requiredLength == 0 )
    {
        root = executableDirectory();
    }
    else
    {
        std::wstring localAppData( requiredLength, L'\0' );
        const DWORD written = GetEnvironmentVariableW(
            L"LOCALAPPDATA", localAppData.data(), requiredLength );
        if ( written == 0 || written >= requiredLength )
        {
            throw std::runtime_error( "Could not read LOCALAPPDATA." );
        }
        localAppData.resize( written );
        root = std::filesystem::path( localAppData );
    }

    return root / L"VR Piston Animation" / L"Logs"
           / L"vr-piston-animation.log";
}

std::string toAnsi( const wchar_t* value )
{
    const int requiredLength = WideCharToMultiByte(
        CP_ACP, 0, value, -1, nullptr, 0, nullptr, nullptr );
    if ( requiredLength <= 0 )
    {
        throw std::runtime_error( "Could not read command-line arguments." );
    }

    std::string result( static_cast<size_t>( requiredLength ), '\0' );
    const int written = WideCharToMultiByte( CP_ACP,
                                            0,
                                            value,
                                            -1,
                                            result.data(),
                                            requiredLength,
                                            nullptr,
                                            nullptr );
    if ( written <= 0 )
    {
        throw std::runtime_error( "Could not read command-line arguments." );
    }
    result.resize( static_cast<size_t>( written - 1 ) );
    return result;
}

class StreamRedirect
{
public:
    explicit StreamRedirect( std::ofstream& logFile )
        : m_coutBuffer( std::cout.rdbuf( logFile.rdbuf() ) )
        , m_cerrBuffer( std::cerr.rdbuf( logFile.rdbuf() ) )
        , m_coutFlags( std::cout.flags() )
    {
        std::cout << std::unitbuf;
    }

    StreamRedirect( const StreamRedirect& ) = delete;
    StreamRedirect& operator=( const StreamRedirect& ) = delete;

    ~StreamRedirect()
    {
        std::cout.flush();
        std::cerr.flush();
        std::cout.rdbuf( m_coutBuffer );
        std::cerr.rdbuf( m_cerrBuffer );
        std::cout.flags( m_coutFlags );
    }

private:
    std::streambuf* m_coutBuffer;
    std::streambuf* m_cerrBuffer;
    std::ios::fmtflags m_coutFlags;
};

struct LocalArgvDeleter
{
    void operator()( LPWSTR* arguments ) const
    {
        if ( arguments != nullptr )
        {
            LocalFree( arguments );
        }
    }
};

bool isInstallerAction( int argc, char** argv )
{
    return argc == 2
           && ( std::string( argv[1] ) == "--install-steamvr"
                || std::string( argv[1] ) == "--uninstall-steamvr" );
}

void showStartupError( const std::filesystem::path& logPath )
{
    std::wstring message
        = L"VR Piston Animation could not start. See the log file for details.";
    if ( !logPath.empty() )
    {
        message += L"\n\n";
        message += logPath.wstring();
    }
    MessageBoxW( nullptr,
                 message.c_str(),
                 L"VR Piston Animation",
                 MB_OK | MB_ICONERROR );
}
} // namespace

std::filesystem::path getApplicationLogPath()
{
    return logFilePath();
}

int WINAPI wWinMain( HINSTANCE, HINSTANCE, PWSTR, int )
{
    std::filesystem::path logPath;
    std::ofstream logFile;
    HANDLE singleInstance = nullptr;
    try
    {
        logPath = getApplicationLogPath();
        std::filesystem::create_directories( logPath.parent_path() );
        const bool needsBom
            = !std::filesystem::exists( logPath )
              || std::filesystem::file_size( logPath ) == 0;
        logFile.open( logPath, std::ios::binary | std::ios::app );
        if ( !logFile )
        {
            throw std::runtime_error( "Could not open the log file." );
        }
        if ( needsBom )
        {
            logFile.write( "\xEF\xBB\xBF", 3 );
        }
        logFile << "\r\n--- VR Piston Animation started ---\r\n";

        StreamRedirect redirect( logFile );

        int argc = 0;
        std::unique_ptr<wchar_t*, LocalArgvDeleter> wideArgv(
            CommandLineToArgvW( GetCommandLineW(), &argc ) );
        if ( wideArgv == nullptr )
        {
            throw std::runtime_error( "Could not parse command-line arguments." );
        }

        std::vector<std::string> arguments;
        arguments.reserve( static_cast<size_t>( argc ) );
        for ( int i = 0; i < argc; ++i )
        {
            arguments.push_back( toAnsi( wideArgv.get()[i] ) );
        }

        std::vector<char*> argv;
        argv.reserve( arguments.size() );
        for ( auto& argument : arguments )
        {
            argv.push_back( argument.data() );
        }

        if ( !isInstallerAction( argc, argv.data() )
             && !( argc == 2 && arguments[1] == "--help" )
             && !( argc == 2 && arguments[1] == "-h" ) )
        {
            singleInstance = CreateMutexW(
                nullptr, TRUE, L"Local\\VRPistonAnimationSingleInstance" );
            if ( singleInstance == nullptr )
            {
                throw std::runtime_error(
                    "Could not create the single-instance mutex." );
            }
            if ( GetLastError() == ERROR_ALREADY_EXISTS )
            {
                CloseHandle( singleInstance );
                MessageBoxW( nullptr,
                             L"VR Piston Animation is already running. Look for its tray icon.",
                             L"VR Piston Animation",
                             MB_OK | MB_ICONINFORMATION );
                return 0;
            }
        }

        const int result = runApplication( argc, argv.data() );
        if ( result != 0 && !isInstallerAction( argc, argv.data() ) )
        {
            showStartupError( logPath );
        }
        if ( singleInstance != nullptr )
        {
            CloseHandle( singleInstance );
        }
        return result;
    }
    catch ( const std::exception& error )
    {
        if ( singleInstance != nullptr )
        {
            CloseHandle( singleInstance );
        }
        if ( logFile )
        {
            logFile << "Startup error: " << error.what() << "\r\n";
            logFile.flush();
        }
        showStartupError( logPath );
        return 1;
    }
}
