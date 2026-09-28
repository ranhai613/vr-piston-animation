#include <openvr.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <json.hpp>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kDefaultOscListenPort = 9001;
constexpr int kDefaultVrcOscPort = 9000;
constexpr const char* kConfigFileName = "config.json";
constexpr const char* kSettingsFileName = "last-parameter-values.json";

constexpr const char* kOscEnabled = "/avatar/parameters/VROffsetEnabled";
constexpr const char* kOscVertical = "/avatar/parameters/VROffsetVertical";
constexpr const char* kOscHorizontal = "/avatar/parameters/VROffsetHorizontal";
constexpr const char* kOscSpeed = "/avatar/parameters/VROffsetSpeed";
constexpr const char* kOscAmplitude = "/avatar/parameters/VROffsetAmplitude";

std::atomic_bool g_shouldStop{ false };

struct Options
{
    double amplitudeMeters = 0.3;
    double periodSeconds = 0.3;
    double fps = 120.0;
    int oscListenPort = kDefaultOscListenPort;
    int vrcOscSendPort = kDefaultVrcOscPort;
    bool oscEnabled = true;
    bool updateSeated = true;
    std::string configPath = kConfigFileName;
};

struct OscControls
{
    bool enabled = false;
    double requestedVertical = 1.0;
    double requestedHorizontal = 0.0;
    double directionVertical = 1.0;
    double directionHorizontal = 0.0;
    double speed = 1.0;
    double amplitude = 1.0;
};

void handleSignal( int )
{
    g_shouldStop.store( true );
}

double clamp01( double value )
{
    return std::clamp( value, 0.0, 1.0 );
}

double clampSigned01( double value )
{
    return std::clamp( value, -1.0, 1.0 );
}

void normalizeRequestedDirection( OscControls& controls )
{
    const double length = std::hypot( controls.requestedVertical,
                                      controls.requestedHorizontal );
    if ( length > 0.000001 )
    {
        controls.directionVertical = controls.requestedVertical;
        controls.directionHorizontal = controls.requestedHorizontal;
    }
}

void loadSettings( OscControls& controls )
{
    std::ifstream input( kSettingsFileName );
    if ( !input )
    {
        return;
    }

    try
    {
        nlohmann::json settings;
        input >> settings;

        controls.requestedVertical = clampSigned01(
            settings.value( "vertical", controls.requestedVertical ) );
        controls.requestedHorizontal = clampSigned01(
            settings.value( "horizontal", controls.requestedHorizontal ) );
        controls.speed = clamp01( settings.value( "speed", controls.speed ) );
        controls.amplitude
            = clamp01( settings.value( "amplitude", controls.amplitude ) );
        controls.enabled = false;
        normalizeRequestedDirection( controls );

        std::cout << "Loaded settings from " << kSettingsFileName << "\n";
    }
    catch ( const std::exception& e )
    {
        std::cerr << "Warning: Could not load " << kSettingsFileName << ": "
                  << e.what() << "\n";
    }
}

void saveSettings( const OscControls& controls )
{
    try
    {
        nlohmann::json settings;
        settings["vertical"] = controls.requestedVertical;
        settings["horizontal"] = controls.requestedHorizontal;
        settings["speed"] = controls.speed;
        settings["amplitude"] = controls.amplitude;

        std::ofstream output( kSettingsFileName );
        output << settings.dump( 2 ) << "\n";
        std::cout << "Saved settings to " << kSettingsFileName << "\n";
    }
    catch ( const std::exception& e )
    {
        std::cerr << "Warning: Could not save " << kSettingsFileName << ": "
                  << e.what() << "\n";
    }
}

void writeDefaultConfig( const Options& options )
{
    try
    {
        nlohmann::json config;
        config["amplitudeMeters"] = options.amplitudeMeters;
        config["periodSeconds"] = options.periodSeconds;
        config["fps"] = options.fps;
        config["oscListenPort"] = options.oscListenPort;
        config["vrcOscSendPort"] = options.vrcOscSendPort;
        config["oscEnabled"] = options.oscEnabled;
        config["updateSeated"] = options.updateSeated;

        std::ofstream output( options.configPath );
        output << config.dump( 2 ) << "\n";
        std::cout << "Created default config at " << options.configPath
                  << "\n";
    }
    catch ( const std::exception& e )
    {
        std::cerr << "Warning: Could not create config " << options.configPath
                  << ": " << e.what() << "\n";
    }
}

void loadConfig( Options& options )
{
    std::ifstream input( options.configPath );
    if ( !input )
    {
        writeDefaultConfig( options );
        return;
    }

    try
    {
        nlohmann::json config;
        input >> config;

        options.amplitudeMeters
            = config.value( "amplitudeMeters", options.amplitudeMeters );
        if ( options.amplitudeMeters < 0.0 )
        {
            throw std::runtime_error( "amplitudeMeters must be >= 0." );
        }
        options.periodSeconds
            = config.value( "periodSeconds", options.periodSeconds );
        if ( options.periodSeconds <= 0.0 )
        {
            throw std::runtime_error( "periodSeconds must be > 0." );
        }
        options.fps = config.value( "fps", options.fps );
        if ( options.fps <= 0.0 )
        {
            throw std::runtime_error( "fps must be > 0." );
        }
        options.oscListenPort
            = config.value( "oscListenPort", options.oscListenPort );
        if ( options.oscListenPort <= 0 || options.oscListenPort > 65535 )
        {
            throw std::runtime_error(
                "oscListenPort must be from 1 to 65535." );
        }
        options.vrcOscSendPort
            = config.value( "vrcOscSendPort", options.vrcOscSendPort );
        if ( options.vrcOscSendPort <= 0
             || options.vrcOscSendPort > 65535 )
        {
            throw std::runtime_error(
                "vrcOscSendPort must be from 1 to 65535." );
        }
        options.oscEnabled = config.value( "oscEnabled", options.oscEnabled );
        options.updateSeated
            = config.value( "updateSeated", options.updateSeated );

        std::cout << "Loaded config from " << options.configPath << "\n";
    }
    catch ( const std::exception& e )
    {
        throw std::runtime_error( "Could not load config "
                                  + options.configPath + ": " + e.what() );
    }
}

Options parseOptions( int argc, char** argv )
{
    Options options;
    for ( int i = 1; i < argc; ++i )
    {
        const std::string arg = argv[i];
        auto requireValue = [&]( const char* name ) -> const char*
        {
            if ( i + 1 >= argc )
            {
                throw std::runtime_error( std::string( name )
                                          + " requires a value." );
            }
            return argv[++i];
        };

        if ( arg == "--config" )
        {
            options.configPath = requireValue( "--config" );
        }
        else if ( arg == "--help" || arg == "-h" )
        {
            std::cout
                << "Usage: vr-piston-animation [options]\n\n"
                << "Options:\n"
                << "  --config PATH                Config JSON path "
                   "(default: config.json)\n";
            std::exit( 0 );
        }
        else
        {
            throw std::runtime_error( "Unknown option: " + arg );
        }
    }
    loadConfig( options );
    return options;
}

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

void closeSocket( SocketHandle socket )
{
    closesocket( socket );
}
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;

void closeSocket( SocketHandle socket )
{
    close( socket );
}
#endif

size_t oscPaddedSize( size_t rawSize )
{
    return ( rawSize + 3U ) & ~size_t( 3U );
}

bool readOscString( const char* data,
                    size_t size,
                    size_t& offset,
                    std::string& out )
{
    if ( offset >= size )
    {
        return false;
    }

    const size_t start = offset;
    while ( offset < size && data[offset] != '\0' )
    {
        ++offset;
    }
    if ( offset >= size )
    {
        return false;
    }

    out.assign( data + start, offset - start );
    offset = start + oscPaddedSize( out.size() + 1U );
    return offset <= size;
}

uint32_t readBigEndianU32( const char* data, size_t offset )
{
    return ( static_cast<uint32_t>( static_cast<unsigned char>( data[offset] ) )
             << 24 )
           | ( static_cast<uint32_t>(
                   static_cast<unsigned char>( data[offset + 1] ) )
               << 16 )
           | ( static_cast<uint32_t>(
                   static_cast<unsigned char>( data[offset + 2] ) )
               << 8 )
           | static_cast<uint32_t>(
               static_cast<unsigned char>( data[offset + 3] ) );
}

float readBigEndianFloat( const char* data, size_t offset )
{
    const uint32_t bits = readBigEndianU32( data, offset );
    float value = 0.0f;
    std::memcpy( &value, &bits, sizeof( value ) );
    return value;
}

bool parseOscBool( const std::string& tags,
                   const char* data,
                   size_t size,
                   size_t offset,
                   bool& out )
{
    if ( tags == ",T" )
    {
        out = true;
        return true;
    }
    if ( tags == ",F" )
    {
        out = false;
        return true;
    }
    if ( ( tags == ",i" || tags == ",f" ) && offset + 4 <= size )
    {
        if ( tags == ",i" )
        {
            out = readBigEndianU32( data, offset ) != 0;
        }
        else
        {
            out = readBigEndianFloat( data, offset ) != 0.0f;
        }
        return true;
    }
    return false;
}

bool parseOscFloat( const std::string& tags,
                    const char* data,
                    size_t size,
                    size_t offset,
                    double& out )
{
    if ( tags == ",f" && offset + 4 <= size )
    {
        out = readBigEndianFloat( data, offset );
        return true;
    }
    if ( tags == ",i" && offset + 4 <= size )
    {
        out = static_cast<int32_t>( readBigEndianU32( data, offset ) );
        return true;
    }
    return false;
}

void appendOscString( std::vector<char>& message, const std::string& value )
{
    message.insert( message.end(), value.begin(), value.end() );
    message.push_back( '\0' );
    while ( message.size() % 4 != 0 )
    {
        message.push_back( '\0' );
    }
}

void appendBigEndianFloat( std::vector<char>& message, float value )
{
    uint32_t bits = 0;
    std::memcpy( &bits, &value, sizeof( bits ) );
    bits = htonl( bits );
    const auto* bytes = reinterpret_cast<const char*>( &bits );
    message.insert( message.end(), bytes, bytes + sizeof( bits ) );
}

bool sendOscMessageToVrc( const std::vector<char>& message, int port )
{
    const SocketHandle socketHandle = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    if ( socketHandle == kInvalidSocket )
    {
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons( static_cast<uint16_t>( port ) );
    inet_pton( AF_INET, "127.0.0.1", &address.sin_addr );

    const int result = sendto(
        socketHandle,
        message.data(),
        static_cast<int>( message.size() ),
        0,
        reinterpret_cast<sockaddr*>( &address ),
        sizeof( address ) );

    closeSocket( socketHandle );
    return result >= 0;
}

void sendOscFloatToVrc( const char* address, double value, int port )
{
    std::vector<char> message;
    appendOscString( message, address );
    appendOscString( message, ",f" );
    appendBigEndianFloat( message, static_cast<float>( value ) );
    if ( !sendOscMessageToVrc( message, port ) )
    {
        std::cerr << "Warning: Could not send OSC float " << address << "\n";
    }
}

void sendOscBoolToVrc( const char* address, bool value, int port )
{
    std::vector<char> message;
    appendOscString( message, address );
    appendOscString( message, value ? ",T" : ",F" );
    if ( !sendOscMessageToVrc( message, port ) )
    {
        std::cerr << "Warning: Could not send OSC bool " << address << "\n";
    }
}

void sendStartupSettingsToVrc( const OscControls& controls, int port )
{
    sendOscBoolToVrc( kOscEnabled, false, port );
    sendOscFloatToVrc( kOscVertical, controls.requestedVertical, port );
    sendOscFloatToVrc( kOscHorizontal, controls.requestedHorizontal, port );
    sendOscFloatToVrc( kOscSpeed, controls.speed, port );
    sendOscFloatToVrc( kOscAmplitude, controls.amplitude, port );
    std::cout << "Sent startup settings to VRChat OSC on UDP "
              << port << "\n";
}

void applyOscMessage( const char* data,
                      size_t size,
                      OscControls& controls )
{
    size_t offset = 0;
    std::string address;
    std::string tags;

    if ( !readOscString( data, size, offset, address )
         || !readOscString( data, size, offset, tags ) )
    {
        return;
    }

    if ( address == kOscEnabled )
    {
        bool value = false;
        if ( parseOscBool( tags, data, size, offset, value ) )
        {
            controls.enabled = value;
        }
        return;
    }

    double value = 0.0;
    if ( !parseOscFloat( tags, data, size, offset, value ) )
    {
        return;
    }

    if ( address == kOscVertical )
    {
        controls.requestedVertical = clampSigned01( value );
        normalizeRequestedDirection( controls );
    }
    else if ( address == kOscHorizontal )
    {
        controls.requestedHorizontal = clampSigned01( value );
        normalizeRequestedDirection( controls );
    }
    else if ( address == kOscSpeed )
    {
        controls.speed = clamp01( value );
    }
    else if ( address == kOscAmplitude )
    {
        controls.amplitude = clamp01( value );
    }
}

class OscReceiver
{
public:
    explicit OscReceiver( int port )
    {
#ifdef _WIN32
        WSADATA wsaData{};
        const int startupResult = WSAStartup( MAKEWORD( 2, 2 ), &wsaData );
        if ( startupResult != 0 )
        {
            throw std::runtime_error( "WSAStartup failed." );
        }
        m_wsaStarted = true;
#endif

        m_socket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
        if ( m_socket == kInvalidSocket )
        {
            throw std::runtime_error( "Could not create UDP socket." );
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl( INADDR_ANY );
        address.sin_port = htons( static_cast<uint16_t>( port ) );

        if ( bind( m_socket,
                   reinterpret_cast<sockaddr*>( &address ),
                   sizeof( address ) )
             != 0 )
        {
            throw std::runtime_error( "Could not bind UDP port "
                                      + std::to_string( port ) + "." );
        }

#ifdef _WIN32
        u_long nonBlocking = 1;
        ioctlsocket( m_socket, FIONBIO, &nonBlocking );
#else
        const int flags = fcntl( m_socket, F_GETFL, 0 );
        fcntl( m_socket, F_SETFL, flags | O_NONBLOCK );
#endif
    }

    OscReceiver( const OscReceiver& ) = delete;
    OscReceiver& operator=( const OscReceiver& ) = delete;

    ~OscReceiver()
    {
        if ( m_socket != kInvalidSocket )
        {
            closeSocket( m_socket );
        }
#ifdef _WIN32
        if ( m_wsaStarted )
        {
            WSACleanup();
        }
#endif
    }

    void poll( OscControls& controls )
    {
        std::array<char, 1024> buffer{};
        while ( true )
        {
            const int received = recv( m_socket,
                                       buffer.data(),
                                       static_cast<int>( buffer.size() ),
                                       0 );
            if ( received <= 0 )
            {
                break;
            }
            applyOscMessage( buffer.data(),
                             static_cast<size_t>( received ),
                             controls );
        }
    }

private:
    SocketHandle m_socket = kInvalidSocket;
#ifdef _WIN32
    bool m_wsaStarted = false;
#endif
};

vr::HmdMatrix34_t withAnimatedOffset( const vr::HmdMatrix34_t& base,
                                      double verticalMeters,
                                      double forwardMeters )
{
    auto result = base;

    // Match OVRAS' convention: offsets are applied along the current basis
    // axes, not blindly along raw tracking axes.
    result.m[0][3] += base.m[0][1] * static_cast<float>( verticalMeters );
    result.m[1][3] += base.m[1][1] * static_cast<float>( verticalMeters );
    result.m[2][3] += base.m[2][1] * static_cast<float>( verticalMeters );

    result.m[0][3] += base.m[0][2] * static_cast<float>( forwardMeters );
    result.m[1][3] += base.m[1][2] * static_cast<float>( forwardMeters );
    result.m[2][3] += base.m[2][2] * static_cast<float>( forwardMeters );

    return result;
}

bool readWorkingBases( vr::HmdMatrix34_t& standingBase,
                       vr::HmdMatrix34_t& seatedBase,
                       bool wantSeated,
                       bool& hasSeatedBase )
{
    hasSeatedBase = false;
    if ( !vr::VRChaperoneSetup()->GetWorkingStandingZeroPoseToRawTrackingPose(
             &standingBase ) )
    {
        return false;
    }

    if ( wantSeated )
    {
        hasSeatedBase
            = vr::VRChaperoneSetup()->GetWorkingSeatedZeroPoseToRawTrackingPose(
                &seatedBase );
    }
    return true;
}

bool initializeOpenVr()
{
    vr::EVRInitError initError = vr::VRInitError_None;
    vr::VR_Init( &initError, vr::VRApplication_Background );
    if ( initError != vr::VRInitError_None )
    {
        std::cerr << "OpenVR initialization failed: "
                  << vr::VR_GetVRInitErrorAsEnglishDescription( initError )
                  << "\n";
        return false;
    }

    if ( !vr::VRChaperoneSetup() )
    {
        std::cerr << "IVRChaperoneSetup is unavailable after initialization.\n";
        return false;
    }

    return true;
}

bool reinitializeOpenVr()
{
    vr::VR_Shutdown();
    return initializeOpenVr();
}

void removeOwnOffsetFromCurrentWorkingPose( double currentY,
                                            double currentZ,
                                            bool updateSeated )
{
    if ( !vr::VRChaperoneSetup() )
    {
        return;
    }

    vr::HmdMatrix34_t standingCurrent{};
    vr::HmdMatrix34_t seatedCurrent{};
    bool hasSeatedCurrent = false;
    if ( !readWorkingBases(
             standingCurrent, seatedCurrent, updateSeated, hasSeatedCurrent ) )
    {
        return;
    }

    const auto restoredStanding
        = withAnimatedOffset( standingCurrent, -currentY, -currentZ );
    vr::VRChaperoneSetup()->SetWorkingStandingZeroPoseToRawTrackingPose(
        &restoredStanding );

    if ( hasSeatedCurrent )
    {
        const auto restoredSeated
            = withAnimatedOffset( seatedCurrent, -currentY, -currentZ );
        vr::VRChaperoneSetup()->SetWorkingSeatedZeroPoseToRawTrackingPose(
            &restoredSeated );
    }

    vr::VRChaperoneSetup()->ShowWorkingSetPreview();
}

void shutdownOpenVr()
{
    vr::VR_Shutdown();
}
} // namespace

int main( int argc, char** argv )
{
    try
    {
        const Options options = parseOptions( argc, argv );

        std::signal( SIGINT, handleSignal );
        std::signal( SIGTERM, handleSignal );

        OscControls controls;
        loadSettings( controls );

        std::unique_ptr<OscReceiver> oscReceiver;
        if ( options.oscEnabled )
        {
            oscReceiver
                = std::make_unique<OscReceiver>( options.oscListenPort );
            sendStartupSettingsToVrc( controls, options.vrcOscSendPort );
        }

        if ( !initializeOpenVr() )
        {
            return 1;
        }

        vr::HmdMatrix34_t standingBase{};
        vr::HmdMatrix34_t seatedBase{};
        bool hasSeatedBase = false;

        std::cout << "Animating offset. Ctrl+C to stop.\n"
                  << "amplitude=" << options.amplitudeMeters
                  << "m periodAtSpeed1=" << options.periodSeconds
                  << "s fps=" << options.fps << "\n";
        if ( options.oscEnabled )
        {
            std::cout << "Listening for VRChat OSC on UDP "
                      << options.oscListenPort << "\n"
                      << "  " << kOscEnabled << " bool\n"
                      << "  " << kOscVertical << " float -1..1\n"
                      << "  " << kOscHorizontal << " float -1..1\n"
                      << "  " << kOscSpeed << " float 0..1\n"
                      << "  " << kOscAmplitude << " float 0..1\n";
        }

        auto lastFrame = std::chrono::steady_clock::now();
        const auto frameInterval
            = std::chrono::duration<double>( 1.0 / options.fps );
        double phase = 0.0;
        bool wasEnabled = false;
        double currentY = 0.0;
        double currentZ = 0.0;

        while ( !g_shouldStop.load() )
        {
            const auto now = std::chrono::steady_clock::now();
            const double deltaSeconds
                = std::chrono::duration<double>( now - lastFrame ).count();
            lastFrame = now;

            if ( oscReceiver )
            {
                oscReceiver->poll( controls );
            }

            if ( !controls.enabled )
            {
                if ( wasEnabled )
                {
                    removeOwnOffsetFromCurrentWorkingPose(
                        currentY, currentZ, options.updateSeated );
                    currentY = 0.0;
                    currentZ = 0.0;
                    wasEnabled = false;
                }
                std::this_thread::sleep_until(
                    now + std::chrono::duration_cast<
                              std::chrono::steady_clock::duration>(
                              frameInterval ) );
                continue;
            }

            if ( !wasEnabled )
            {
                // Re-initialize OpenVR to clear the working pose cache
                if ( !reinitializeOpenVr() )
                {
                    std::this_thread::sleep_until(
                        now + std::chrono::duration_cast<
                                  std::chrono::steady_clock::duration>(
                                  frameInterval ) );
                    continue;
                }
                vr::HmdMatrix34_t enabledStandingBase{};
                vr::HmdMatrix34_t enabledSeatedBase{};
                bool enabledHasSeatedBase = false;
                if ( !readWorkingBases( enabledStandingBase,
                                        enabledSeatedBase,
                                        options.updateSeated,
                                        enabledHasSeatedBase ) )
                {
                    std::cerr << "Warning: Could not refresh standing zero pose. "
                                 "Skipping this frame.\n";
                    std::this_thread::sleep_until(
                        now + std::chrono::duration_cast<
                                  std::chrono::steady_clock::duration>(
                                  frameInterval ) );
                    continue;
                }
                standingBase = enabledStandingBase;
                seatedBase = enabledSeatedBase;
                hasSeatedBase = enabledHasSeatedBase;
                currentY = 0.0;
                currentZ = 0.0;
                phase = 0.0;
                if ( options.updateSeated && !hasSeatedBase )
                {
                    std::cerr << "Warning: Could not read seated zero pose. "
                                 "Continuing with standing-only animation.\n";
                }
                wasEnabled = true;
            }

            phase += ( controls.speed / options.periodSeconds ) * 2.0 * kPi
                     * deltaSeconds;

            const double directionLength = std::hypot(
                controls.directionVertical, controls.directionHorizontal );

            double verticalDirection = 0.0;
            double horizontalDirection = 0.0;
            if ( directionLength > 0.000001 )
            {
                verticalDirection = controls.directionVertical / directionLength;
                horizontalDirection
                    = controls.directionHorizontal / directionLength;
            }

            const double wave = std::sin( phase );
            const double amplitude
                = options.amplitudeMeters * controls.amplitude * wave;
            const double y = amplitude * verticalDirection;
            const double z = amplitude * horizontalDirection;
            currentY = y;
            currentZ = z;

            const auto standingAnimated
                = withAnimatedOffset( standingBase, y, z );
            vr::VRChaperoneSetup()->SetWorkingStandingZeroPoseToRawTrackingPose(
                &standingAnimated );

            if ( hasSeatedBase )
            {
                const auto seatedAnimated
                    = withAnimatedOffset( seatedBase, y, z );
                vr::VRChaperoneSetup()
                    ->SetWorkingSeatedZeroPoseToRawTrackingPose(
                        &seatedAnimated );
            }

            vr::VRChaperoneSetup()->ShowWorkingSetPreview();

            std::this_thread::sleep_until(
                now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                          frameInterval ) );
        }

        if ( wasEnabled )
        {
            removeOwnOffsetFromCurrentWorkingPose(
                currentY, currentZ, options.updateSeated );
        }
        saveSettings( controls );
        shutdownOpenVr();
        std::cout << "Removed own offset and shut down.\n";
        return 0;
    }
    catch ( const std::exception& e )
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
