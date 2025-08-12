# CMakeLists.txt — Tiny Win32 Updater (Augustus)
# Generates a small GUI EXE (wWinMain) with Unicode, static runtime (/MT), and size optimizations.
# Requires CMake >= 3.15 for MSVC_RUNTIME_LIBRARY.

cmake_minimum_required(VERSION 3.15)
project(augustus_updater C)

# Source
add_executable(augustus_updater WIN32 updater.c)

# Unicode
target_compile_definitions(augustus_updater PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)

# Optimize for size
if (MSVC)
  target_compile_options(augustus_updater PRIVATE /O1 /Os /GL /Gy /Zc:wchar_t)
  # Static runtime to avoid VC redistributable dependency (smaller single EXE)
  set_property(TARGET augustus_updater PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  # Link-time optimization and ICF
  target_link_options(augustus_updater PRIVATE /OPT:REF /OPT:ICF)
endif()

# Libraries needed by updater.c
# - version    : GetFileVersionInfo*
# - wininet    : InternetOpen/InternetOpenUrl/InternetReadFile
# - urlmon     : URLDownloadToFileW
# - ole32/oleaut32 : COM init + Shell Automation
# - shell32    : Shell COM objects
# - shlwapi    : Path* helpers

if (MSVC)
  target_link_libraries(augustus_updater PRIVATE version wininet urlmon ole32 oleaut32 shell32 shlwapi)
else()
  # MinGW-w64, etc.
  target_link_libraries(augustus_updater PRIVATE version wininet urlmon ole32 oleaut32 shell32 shlwapi)
  # Force GUI subsystem on MinGW (no console)
  target_link_options(augustus_updater PRIVATE -mwindows)
endif()

# Output folder similar to Visual Studio layout
set_target_properties(augustus_updater PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
