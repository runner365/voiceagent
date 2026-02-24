include(ExternalProject)

set(EXTRA_CFLAGS -I${PREFIX_DIR}/include)
set(EXTRA_LDFLAGS -L${PREFIX_DIR}/lib)
set(EXTRA_LIBS "-lm -pthread -ldl")

add_subdirectory(libuv)
add_subdirectory(yaml-cpp)
add_subdirectory(sherpa-onnx)

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
ExternalProject_Add(openssl
    URL ${CMAKE_CURRENT_SOURCE_DIR}/openssl-3.5.4.tar.gz
    SOURCE_DIR  ${CMAKE_CURRENT_BINARY_DIR}/openssl
    BUILD_IN_SOURCE 1
    # On macOS arm64, Apple's assembler may fail on certain OpenSSL ASM sources.
    # Use no-asm to avoid AES/ARM ASM build errors while still enabling EC.
    CONFIGURE_COMMAND
        ./Configure --prefix=${PREFIX_DIR} darwin64-arm64-cc enable-ec no-asm
        --libdir=${PREFIX_DIR}/lib
    BUILD_COMMAND make -j 4
    INSTALL_COMMAND make install_sw
    )
else()
ExternalProject_Add(openssl
    URL ${CMAKE_CURRENT_SOURCE_DIR}/openssl-3.5.4.tar.gz
    SOURCE_DIR  ${CMAKE_CURRENT_BINARY_DIR}/openssl
    BUILD_IN_SOURCE 1
    # Enable EC support explicitly and allow assembly optimizations.
    CONFIGURE_COMMAND
        ./Configure --prefix=${PREFIX_DIR} darwin64-x86_64-cc enable-ec no-asm
        --libdir=${PREFIX_DIR}/lib
    BUILD_COMMAND make -j 4
    INSTALL_COMMAND make install_sw
    )
endif()

else()
ExternalProject_Add(openssl
    URL ${CMAKE_CURRENT_SOURCE_DIR}/openssl-3.5.4.tar.gz
    SOURCE_DIR  ${CMAKE_CURRENT_BINARY_DIR}/openssl
    BUILD_IN_SOURCE 1
    # Enable EC explicitly; keep static build if desired, but ensure EC/X25519 are available in the build.
    CONFIGURE_COMMAND
        ./Configure --prefix=${PREFIX_DIR} no-shared linux-x86_64 enable-ec
        --libdir=${PREFIX_DIR}/lib
    BUILD_COMMAND make -j 4
    INSTALL_COMMAND make install_sw
    )
endif()

# Build libsrtp from bundled tarball (if present)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/libsrtp.tar.gz)
    ExternalProject_Add(srtp2-ext
        URL ${CMAKE_CURRENT_SOURCE_DIR}/libsrtp.tar.gz
        SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/libsrtp
        BUILD_IN_SOURCE 1
        # Ensure libsrtp is configured and built only after OpenSSL is installed
        DEPENDS openssl
    # Use cmake -E env to set environment variables so arguments are not injected with extra quotes
    # Run './configure' from the extracted SOURCE_DIR (ExternalProject will run this command in SOURCE_DIR)
    # Enable OpenSSL support and AES-GCM (AEAD) support for WebRTC
    # Supply PKG_CONFIG_PATH so libsrtp's configure can locate OpenSSL installed into PREFIX_DIR
    # Also pass CPPFLAGS/LIBS for a robust fallback on platforms without pkg-config
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
        PKG_CONFIG_PATH=${PREFIX_DIR}/lib/pkgconfig
        CPPFLAGS=-I${PREFIX_DIR}/include
        LDFLAGS=-L${PREFIX_DIR}/lib
        CFLAGS=${EXTRA_CFLAGS}
        ./configure --prefix=${PREFIX_DIR} --libdir=${PREFIX_DIR}/lib --enable-openssl
        BUILD_COMMAND make -j 4
        INSTALL_COMMAND make install
        )
endif()

# Build nasm assembler
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/nasm-3.01.tar.gz)
    ExternalProject_Add(nasm
        URL ${CMAKE_CURRENT_SOURCE_DIR}/nasm-3.01.tar.gz
        SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/nasm
        BUILD_IN_SOURCE 1
        CONFIGURE_COMMAND ./configure --prefix=${PREFIX_DIR}
        BUILD_COMMAND make -j 4
        INSTALL_COMMAND make install
        )
endif()

# Build libopus
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/opus-1.6.tar.gz)
    ExternalProject_Add(libopus
        URL ${CMAKE_CURRENT_SOURCE_DIR}/opus-1.6.tar.gz
        SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/libopus
        BUILD_IN_SOURCE 1
        CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
            CFLAGS=${EXTRA_CFLAGS}
            LDFLAGS=${EXTRA_LDFLAGS}
            ./configure --prefix=${PREFIX_DIR} --libdir=${PREFIX_DIR}/lib --disable-shared
        BUILD_COMMAND make -j 4
        INSTALL_COMMAND make install
        )
endif()

# Build x264
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/x264-master.tar.bz2)
    ExternalProject_Add(x264
        URL ${CMAKE_CURRENT_SOURCE_DIR}/x264-master.tar.bz2
        SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/x264
        BUILD_IN_SOURCE 1
        DEPENDS nasm
        CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
            PATH=${PREFIX_DIR}/bin:$ENV{PATH}
            CFLAGS=${EXTRA_CFLAGS}
            LDFLAGS=${EXTRA_LDFLAGS}
            ./configure --prefix=${PREFIX_DIR} --libdir=${PREFIX_DIR}/lib --enable-static --disable-shared
        BUILD_COMMAND make -j 4
        INSTALL_COMMAND make install
        )
endif()

# Build libffmpeg (depends on x264, libopus, and nasm)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/libffmpeg.tar.gz OR EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg.tar.gz)
    set(FFMPEG_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/libffmpeg.tar.gz)
    if(NOT EXISTS ${FFMPEG_SOURCE})
        set(FFMPEG_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg.tar.gz)
    endif()
    
    ExternalProject_Add(libffmpeg
        URL ${FFMPEG_SOURCE}
        SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/libffmpeg
        BUILD_IN_SOURCE 1
        DEPENDS nasm x264 libopus
        CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
            PATH=${PREFIX_DIR}/bin:$ENV{PATH}
            PKG_CONFIG_PATH=${PREFIX_DIR}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}
            CFLAGS=-I${PREFIX_DIR}/include
            LDFLAGS=-L${PREFIX_DIR}/lib
            ./configure 
            --prefix=${PREFIX_DIR}
            --libdir=${PREFIX_DIR}/lib
            --enable-static
            --disable-shared
            --enable-gpl
            --enable-libx264
            --enable-libopus
            --disable-programs
        BUILD_COMMAND make -j 4
        INSTALL_COMMAND make install
        )
endif()
