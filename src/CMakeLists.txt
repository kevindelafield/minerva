cmake_minimum_required(VERSION 3.5)
project(minerva)
cmake_policy(SET CMP0003 NEW)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

IF (AARCH_TOOLCHAIN_DIR)
  
  SET(CMAKE_CXX_FLAGS "-Werror")

  include_directories(../thirdparty/alsa/include ../thirdparty/pugixml-arm/include ../thirdparty/jsoncpp-arm/include ../thirdparty/uuid-arm/include ../thirdparty/curl-7.77.0-arm/include ../thirdparty/openssl-1.1.1k-arm/include)
  
  find_library(UUID_LIBRARY NO_DEFAULT_PATH NAMES uuid PATHS ../thirdparty/uuid-arm/lib )
  find_library(CURL_LIBRARY NO_DEFAULT_PATH NAMES curl PATHS ../thirdparty/curl-7.77.0-arm/lib)
  find_library(CRYPTO_LIBRARY NO_DEFAULT_PATH NAMES crypto PATHS ../thirdparty/openssl-1.1.1k-arm/lib)
  find_library(SSL_LIBRARY NO_DEFAULT_PATH NAMES ssl PATHS ../thirdparty/openssl-1.1.1k-arm/lib)
  find_library(NGHTTP2_LIBRARY NO_DEFAULT_PATH NAMES nghttp2 PATHS ../thirdparty/dependencies/arm/lib)
  find_library(IDN2_LIBRARY NO_DEFAULT_PATH NAMES idn2 PATHS ../thirdparty/dependencies/arm/lib)
  find_library(SSH2_LIBRARY NO_DEFAULT_PATH NAMES ssh2 PATHS ../thirdparty/dependencies/arm/lib)
  find_library(Z_LIBRARY NO_DEFAULT_PATH NAMES z PATHS ../thirdparty/dependencies/arm/lib)
  find_library(UNISTRING_LIBRARY NO_DEFAULT_PATH NAMES unistring PATHS ../thirdparty/dependencies/arm/lib)
  find_library(PUGIXML_LIBRARY NO_DEFAULT_PATH NAMES pugixml PATHS ../thirdparty/pugixml-arm/lib)
  find_library(JSONCPP_LIBRARY NO_DEFAULT_PATH NAMES jsoncpp PATHS ../thirdparty/jsoncpp-arm/lib)
  
  SET(CMAKE_C_COMPILER ${AARCH_TOOLCHAIN_DIR}/bin/aarch64-linux-gnu-gcc)
  SET(CMAKE_CXX_COMPILER ${AARCH_TOOLCHAIN_DIR}/bin/aarch64-linux-gnu-g++)
  SET(CMAKE_ASM_COMPILER ${AARCH_TOOLCHAIN_DIR}/bin/aarch64-linux-gnu-g++)
  SET(CMAKE_LINKER ${AARCH_TOOLCHAIN_DIR}/bin/aarch64-linux-gnu-ld)
  IF (DEBUG)
    SET(CMAKE_CXX_FLAGS_DEBUG "-O0")
    SET(CMAKE_C_FLAGS_DEBUG "-O0")
  ENDIF()
ELSE()

  SET(CMAKE_CXX_FLAGS "-fPIC -Werror")

  find_library(UUID_LIBRARY NAMES uuid)
  find_library(CURL_LIBRARY NAMES curl)
  find_library(CRYPTO_LIBRARY crypto)
  find_library(SSL_LIBRARY NAMES ssl)
  find_library(NGHTTP2_LIBRARY NAMES nghttp2)
  find_library(IDN2_LIBRARY NAMES idn2)
  find_library(SSH2_LIBRARY NAMES ssh2)
  find_library(Z_LIBRARY NAMES z)
  find_library(UNISTRING_LIBRARY unistring)
  find_library(JSONCPP_LIBRARY NAMES jsoncpp)
  find_library(PUGIXML_LIBRARY NAMES pugixml)
ENDIF() 

SET (CMAKE_CXX_CPPCHECK ${CMAKE_SOURCE_DIR}/../tools/runclang.sh "-std=c++17")
SET (CMAKE_C_CPPCHECK ${CMAKE_SOURCE_DIR}/../tools/runclang.sh)

include_directories(.)

add_subdirectory(util)

add_subdirectory(owl)

add_subdirectory(httpd)

add_subdirectory(authdb)

add_subdirectory(shield)

add_subdirectory(www)

add_subdirectory(test)


