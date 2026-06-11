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
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kDefaultOscPort = 9001;

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
    double fps = 60.0;
    int oscPort = kDefaultOscPort;
    bool oscEnabled = true;
    bool updateSeated = true;
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

double parsePositiveDouble( const char* value, const char* name )
{
    char* end = nullptr;
    const double parsed = std::strtod( value, &end );
    if ( end == value || *end != '\0' || parsed <= 0.0 )
    {
        throw std::runtime_error( std::string( name )
                                  + " must be a positive number." );
    }
    return parsed;
}

double parseNonNegativeDouble( const char* value, const char* name )
{
    char* end = nullptr;
    const double parsed = std::strtod( value, &end );
    if ( end == value || *end != '\0' || parsed < 0.0 )
    {
        throw std::runtime_error( std::string( name )
                                  + " must be zero or a positive number." );
    }
    return parsed;
}

int parsePort( const char* value, const char* name )
{
    char* end = nullptr;
    const long parsed = std::strtol( value, &end, 10 );
    if ( end == value || *end != '\0' || parsed <= 0 || parsed > 65535 )
    {
        throw std::runtime_error( std::string( name )
                                  + " must be a UDP port from 1 to 65535." );
    }
    return static_cast<int>( parsed );
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

        if ( arg == "--amplitude" )
        {
            options.amplitudeMeters
                = parseNonNegativeDouble( requireValue( "--amplitude" ),
                                          "--amplitude" );
        }
        else if ( arg == "--period" )
        {
            options.periodSeconds
                = parsePositiveDouble( requireValue( "--period" ),
                                       "--period" );
        }
        else if ( arg == "--fps" )
        {
            options.fps = parsePositiveDouble( requireValue( "--fps" ),
                                               "--fps" );
        }
        else if ( arg == "--osc-port" )
        {
            options.oscPort = parsePort( requireValue( "--osc-port" ),
                                         "--osc-port" );
        }
        else if ( arg == "--no-osc" )
        {
            options.oscEnabled = false;
        }
        else if ( arg == "--no-seated" )
        {
            options.updateSeated = false;
        }
        else if ( arg == "--help" || arg == "-h" )
        {
            std::cout
                << "Usage: vr-offset-animation [options]\n\n"
                << "Options:\n"
                << "  --amplitude METERS           Maximum travel in meters "
                   "(default: 1.0)\n"
                << "  --period SECONDS             Cycle duration when OSC "
                   "speed is 1.0 (default: 0.5)\n"
                << "  --fps FPS                    Update rate (default: 60)\n"
                << "  --osc-port PORT              UDP port to listen on "
                   "(default: 9001)\n"
                << "  --no-osc                     Disable OSC listener\n"
                << "  --no-seated                  Do not update seated zero "
                   "pose\n";
            std::exit( 0 );
        }
        else
        {
            throw std::runtime_error( "Unknown option: " + arg );
        }
    }
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
        const double length = std::hypot( controls.requestedVertical,
                                          controls.requestedHorizontal );
        if ( length > 0.000001 )
        {
            controls.directionVertical = controls.requestedVertical;
            controls.directionHorizontal = controls.requestedHorizontal;
        }
    }
    else if ( address == kOscHorizontal )
    {
        controls.requestedHorizontal = clampSigned01( value );
        const double length = std::hypot( controls.requestedVertical,
                                          controls.requestedHorizontal );
        if ( length > 0.000001 )
        {
            controls.directionVertical = controls.requestedVertical;
            controls.directionHorizontal = controls.requestedHorizontal;
        }
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

void restoreAndShutdown( const vr::HmdMatrix34_t& standingBase,
                         const vr::HmdMatrix34_t& seatedBase,
                         bool hasSeatedBase )
{
    if ( vr::VRChaperoneSetup() )
    {
        vr::VRChaperoneSetup()->SetWorkingStandingZeroPoseToRawTrackingPose(
            &standingBase );

        if ( hasSeatedBase )
        {
            vr::VRChaperoneSetup()->SetWorkingSeatedZeroPoseToRawTrackingPose(
                &seatedBase );
        }

        vr::VRChaperoneSetup()->HideWorkingSetPreview();
        vr::VRChaperoneSetup()->RevertWorkingCopy();
    }

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

        std::unique_ptr<OscReceiver> oscReceiver;
        if ( options.oscEnabled )
        {
            oscReceiver = std::make_unique<OscReceiver>( options.oscPort );
        }

        vr::EVRInitError initError = vr::VRInitError_None;
        vr::VR_Init( &initError, vr::VRApplication_Background );
        if ( initError != vr::VRInitError_None )
        {
            std::cerr << "OpenVR init failed: "
                      << vr::VR_GetVRInitErrorAsEnglishDescription( initError )
                      << "\n";
            return 1;
        }

        if ( !vr::VRChaperoneSetup() )
        {
            std::cerr << "IVRChaperoneSetup is unavailable.\n";
            vr::VR_Shutdown();
            return 1;
        }

        vr::VRChaperoneSetup()->RevertWorkingCopy();

        vr::HmdMatrix34_t standingBase{};
        vr::HmdMatrix34_t seatedBase{};
        bool hasSeatedBase = false;
        if ( !vr::VRChaperoneSetup()->GetWorkingStandingZeroPoseToRawTrackingPose(
                 &standingBase ) )
        {
            std::cerr << "Could not read standing zero pose.\n";
            vr::VR_Shutdown();
            return 1;
        }

        if ( options.updateSeated )
        {
            hasSeatedBase = vr::VRChaperoneSetup()
                                ->GetWorkingSeatedZeroPoseToRawTrackingPose(
                                    &seatedBase );
            if ( !hasSeatedBase )
            {
                std::cerr << "Warning: Could not read seated zero pose. "
                             "Continuing with standing-only animation.\n";
            }
        }

        std::cout << "Animating offset. Ctrl+C to stop.\n"
                  << "amplitude=" << options.amplitudeMeters
                  << "m periodAtSpeed1=" << options.periodSeconds
                  << "s fps=" << options.fps << "\n";
        if ( options.oscEnabled )
        {
            std::cout << "Listening for VRChat OSC on UDP "
                      << options.oscPort << "\n"
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
                std::this_thread::sleep_until(
                    now + std::chrono::duration_cast<
                              std::chrono::steady_clock::duration>(
                              frameInterval ) );
                continue;
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

        restoreAndShutdown( standingBase, seatedBase, hasSeatedBase );
        std::cout << "Restored original zero pose.\n";
        return 0;
    }
    catch ( const std::exception& e )
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
