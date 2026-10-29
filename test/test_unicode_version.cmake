# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The Maul Unicode version a program reads from Maul Window: installs this
# build into a prefix of its own, then configures two programs, one that
# finds the installed package (maul-window_UNICODE_VERSION) and one that
# builds Maul Window from source with add_subdirectory
# (MAUL_WINDOW_UNICODE_VERSION); each fails unless it reads EXPECTED.
# Both find Maul Unicode in the prefix, where the install put it when the
# build fetched it, so neither fetches anything.
#
# cmake -DSOURCE=<source> -DBINARY=<build> -DEXPECTED=<version>
#       -DGENERATOR=<generator> -DTOOLSET=<toolset> -DPLATFORM=<platform>
#       -DCOMPILER=<C compiler> -DCONFIG=<configuration> -P test_unicode_version.cmake
#
# The programs take the build's generator, with its toolset and platform
# where it has them (Visual Studio), and its C compiler otherwise.

set(work ${BINARY}/unicode-version)
file(REMOVE_RECURSE ${work})
set(install ${CMAKE_COMMAND} --install ${BINARY} --prefix ${work}/prefix)
set(generate -G ${GENERATOR})
if(CONFIG)
    list(APPEND install --config ${CONFIG})
endif()
if(TOOLSET)
    list(APPEND generate -T ${TOOLSET})
endif()
if(PLATFORM)
    list(APPEND generate -A ${PLATFORM})
endif()
if(NOT GENERATOR MATCHES "^Visual Studio")
    list(APPEND generate -DCMAKE_C_COMPILER=${COMPILER})
endif()
execute_process(COMMAND ${install} OUTPUT_QUIET RESULT_VARIABLE status)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "installing the build failed: ${status}")
endif()

# Configures a program whose CMakeLists.txt is body; fails the test when
# its configuration fails.
function(configure name body)
    file(WRITE ${work}/${name}/CMakeLists.txt
         "cmake_minimum_required(VERSION 3.25)\nproject(${name} LANGUAGES C)\n${body}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -S ${work}/${name} -B ${work}/${name}/build ${generate}
                -DCMAKE_PREFIX_PATH=${work}/prefix
        OUTPUT_VARIABLE output ERROR_VARIABLE output RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name}: ${output}")
    endif()
    message(STATUS "${name}: read Maul Unicode ${EXPECTED}")
endfunction()

configure(installed "
find_package(maul-window REQUIRED CONFIG)
if(NOT maul-window_UNICODE_VERSION STREQUAL \"${EXPECTED}\")
    message(FATAL_ERROR \"the package names Maul Unicode '\${maul-window_UNICODE_VERSION}'\")
endif()
")
configure(source "
add_subdirectory(\"${SOURCE}\" window)
if(NOT MAUL_WINDOW_UNICODE_VERSION STREQUAL \"${EXPECTED}\")
    message(FATAL_ERROR \"the source names Maul Unicode '\${MAUL_WINDOW_UNICODE_VERSION}'\")
endif()
")
