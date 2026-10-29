cmake_minimum_required(VERSION 3.31)
# iisdk:consumer-application
project(iiJsonCoreValueInstalledConsumer LANGUAGES CXX)
include(CTest)
find_package(iiJsonCoreValue 0.1 CONFIG REQUIRED)
add_executable(iiJsonCoreValueInstalled ../interop.cpp)
target_link_libraries(iiJsonCoreValueInstalled PRIVATE iiJson::CoreValue)
add_test(NAME iiJson.InstalledCoreValue COMMAND iiJsonCoreValueInstalled)
set_tests_properties(iiJson.InstalledCoreValue PROPERTIES TIMEOUT 30)
