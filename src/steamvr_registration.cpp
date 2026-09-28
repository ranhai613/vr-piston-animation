#include "steamvr_registration.h"

#include <openvr.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr char kApplicationKey[] = "vr-piston-animation";
constexpr char kManifestFileName[] = "manifest.vrmanifest";
constexpr DWORD kExecutablePathBufferSize = 32768;
constexpr uint32_t kWorkingDirectoryBufferSize = 32768;

std::string applicationErrorMessage( vr::EVRApplicationError error )
{
    const char* const errorName
        = vr::VRApplications()->GetApplicationsErrorNameFromEnum( error );
    return errorName != nullptr ? errorName : "Unknown OpenVR application error";
}

void checkApplicationError( vr::EVRApplicationError error,
                            const char* operation )
{
    if ( error != vr::VRApplicationError_None )
    {
        throw std::runtime_error( std::string( operation ) + ": "
                                  + applicationErrorMessage( error ) );
    }
}

class OpenVrSession
{
public:
    OpenVrSession()
    {
        vr::EVRInitError error = vr::VRInitError_None;
        vr::VR_Init( &error, vr::VRApplication_Utility );
        if ( error != vr::VRInitError_None )
        {
            throw std::runtime_error(
                std::string( "Could not initialize OpenVR: " )
                + vr::VR_GetVRInitErrorAsEnglishDescription( error ) );
        }
    }

    OpenVrSession( const OpenVrSession& ) = delete;
    OpenVrSession& operator=( const OpenVrSession& ) = delete;

    ~OpenVrSession()
    {
        vr::VR_Shutdown();
    }
};

std::filesystem::path executableManifestPath()
{
    std::vector<wchar_t> buffer( MAX_PATH );
    std::wstring executablePath;
    while ( true )
    {
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>( buffer.size() ) );
        if ( length == 0 )
        {
            throw std::runtime_error( "Could not determine executable path." );
        }
        if ( length < buffer.size() )
        {
            executablePath.assign( buffer.data(), length );
            break;
        }
        if ( buffer.size() >= kExecutablePathBufferSize )
        {
            throw std::runtime_error( "Executable path is too long." );
        }
        buffer.resize( std::min( buffer.size() * 2,
                                 static_cast<size_t>(
                                     kExecutablePathBufferSize ) ) );
    }

    return std::filesystem::path( executablePath ).parent_path()
           / kManifestFileName;
}

std::filesystem::path registeredManifestPath()
{
    std::vector<char> workingDirectory( kWorkingDirectoryBufferSize );
    vr::EVRApplicationError error = vr::VRApplicationError_None;
    vr::VRApplications()->GetApplicationPropertyString(
        kApplicationKey,
        vr::VRApplicationProperty_WorkingDirectory_String,
        workingDirectory.data(),
        static_cast<uint32_t>( workingDirectory.size() ),
        &error );
    checkApplicationError( error, "Could not get registered app directory" );

    return std::filesystem::u8path( workingDirectory.data() )
           / kManifestFileName;
}

std::string utf8Path( const std::filesystem::path& path )
{
    return path.u8string();
}

void addManifest( const std::filesystem::path& manifestPath )
{
    const std::string utf8ManifestPath = utf8Path( manifestPath );
    const vr::EVRApplicationError error
        = vr::VRApplications()->AddApplicationManifest(
            utf8ManifestPath.c_str(), false );
    checkApplicationError( error, "Could not register SteamVR manifest" );
}

void removeManifest( const std::filesystem::path& manifestPath )
{
    const std::string utf8ManifestPath = utf8Path( manifestPath );
    const vr::EVRApplicationError error
        = vr::VRApplications()->RemoveApplicationManifest(
            utf8ManifestPath.c_str() );
    checkApplicationError( error, "Could not unregister SteamVR manifest" );
}
} // namespace

namespace steamvr_registration
{
void installManifest()
{
    const auto manifestPath = executableManifestPath();
    if ( !std::filesystem::exists( manifestPath ) )
    {
        throw std::runtime_error( "SteamVR manifest is missing: "
                                  + utf8Path( manifestPath ) );
    }

    [[maybe_unused]] OpenVrSession openVr;
    auto* const applications = vr::VRApplications();
    if ( applications == nullptr )
    {
        throw std::runtime_error( "OpenVR applications interface is unavailable." );
    }

    const bool wasInstalled
        = applications->IsApplicationInstalled( kApplicationKey );
    const bool enableAutoLaunch
        = !wasInstalled
          || applications->GetApplicationAutoLaunch( kApplicationKey );
    if ( wasInstalled )
    {
        removeManifest( registeredManifestPath() );
    }

    addManifest( manifestPath );

    if ( enableAutoLaunch )
    {
        checkApplicationError(
            applications->SetApplicationAutoLaunch( kApplicationKey, true ),
            "Could not enable SteamVR startup setting" );
    }
}

void uninstallManifest()
{
    [[maybe_unused]] OpenVrSession openVr;
    auto* const applications = vr::VRApplications();
    if ( applications == nullptr )
    {
        throw std::runtime_error( "OpenVR applications interface is unavailable." );
    }
    if ( !applications->IsApplicationInstalled( kApplicationKey ) )
    {
        return;
    }

    if ( applications->GetApplicationAutoLaunch( kApplicationKey ) )
    {
        checkApplicationError(
            applications->SetApplicationAutoLaunch( kApplicationKey, false ),
            "Could not disable SteamVR startup setting" );
    }

    removeManifest( registeredManifestPath() );
}
} // namespace steamvr_registration
