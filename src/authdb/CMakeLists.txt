
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

FILE (GLOB APP_INCLUDE "*.h")
FILE (GLOB APP_SRC "*.cpp")

add_library(authdb STATIC ${APP_SRC} ${APP_INCLUDE})

target_link_libraries(authdb httpd owl)
