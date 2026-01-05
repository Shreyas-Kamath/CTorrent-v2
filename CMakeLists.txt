cmake_minimum_required(VERSION 3.10)
project(CTorrent)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)
find_package(Boost COMPONENTS beast asio json url REQUIRED)

add_executable(
    ctorrent
    source/main.cpp
    
    source/src/Client.cpp
    source/src/TorrentSession.cpp
    source/src/BEncode.cpp
    source/src/MetadataParser.cpp
    source/src/Utils.cpp
    source/src/HttpsTracker.cpp
    source/src/UdpTracker.cpp
    source/src/TrackerFactory.cpp
    source/src/PeerConnection.cpp
    source/src/PieceManager.cpp
    source/src/FileManager.cpp

    source/http/server.cpp
)

target_include_directories(
    ctorrent PRIVATE
    source/include
    source/http
    ${OPENSSL_INCLUDE_DIR}
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(
    ctorrent PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::asio
    Boost::beast
    Boost::json
    Boost::url
)