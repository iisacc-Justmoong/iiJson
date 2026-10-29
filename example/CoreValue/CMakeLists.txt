cmake_minimum_required(VERSION 3.31)
# iisdk:consumer-application
project(iiJsonCoreValue VERSION 0.1.1 LANGUAGES CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
find_package(iiJson 0.1 CONFIG REQUIRED)
find_package(iiSDKCore 0.14 CONFIG REQUIRED COMPONENTS Contracts)
add_library(iiJsonCoreValue INTERFACE)
add_library(iiJson::CoreValue ALIAS iiJsonCoreValue)
set_target_properties(iiJsonCoreValue PROPERTIES EXPORT_NAME CoreValue)
target_compile_features(iiJsonCoreValue INTERFACE cxx_std_23)
target_include_directories(iiJsonCoreValue INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/App>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(iiJsonCoreValue INTERFACE iiJson::iiJson iiSDKCore::Contracts)
if(BUILD_TESTING)
    add_executable(iiJsonCoreValueTests tests/interop.cpp)
    target_link_libraries(iiJsonCoreValueTests PRIVATE iiJson::CoreValue)
    add_test(NAME iiJson.CoreValue COMMAND iiJsonCoreValueTests)
    set_tests_properties(iiJson.CoreValue PROPERTIES TIMEOUT 30)
endif()
set(package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/iiJsonCoreValue")
install(TARGETS iiJsonCoreValue EXPORT iiJsonCoreValueTargets)
install(EXPORT iiJsonCoreValueTargets NAMESPACE iiJson:: DESTINATION "${package_dir}")
install(FILES App/iiJson/CoreValue.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiJson)
configure_package_config_file(cmake/iiJsonCoreValueConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/iiJsonCoreValueConfig.cmake" INSTALL_DESTINATION "${package_dir}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/iiJsonCoreValueConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiJsonCoreValueConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/iiJsonCoreValueConfigVersion.cmake" DESTINATION "${package_dir}")
