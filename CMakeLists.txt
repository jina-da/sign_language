cmake_minimum_required(VERSION 3.16)
project(SignLearnClient VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MinGW에서 한글 소스코드가 깨지지 않도록 UTF-8 지정
if(MSVC)
    add_compile_options(/utf-8)
else()
    add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

# OpenCV
set(OpenCV_DIR "C:/opencv_mingw/x64/mingw/lib")
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

qt_add_executable(SignLearnClient
    MANUAL_FINALIZATION
    main.cpp
    AppController.h
    AppController.cpp
    camera/KeypointClient.h
    camera/KeypointClient.cpp
    network/ProtocolHandler.h
    network/ProtocolHandler.cpp
    network/TcpClient.h
    network/TcpClient.cpp
    widgets/AppStyle.h
    widgets/LoginWidget.h
    widgets/LoginWidget.cpp
    widgets/LoginWidget.ui
    widgets/RegisterWidget.h
    widgets/RegisterWidget.cpp
    widgets/RegisterWidget.ui
    widgets/StudyWidget.h
    widgets/StudyWidget.cpp
    widgets/StudyWidget.ui
    widgets/ReviewWidget.h
    widgets/ReviewWidget.cpp
    widgets/DictWidget.h
    widgets/DictWidget.cpp
    widgets/DictWidget.ui
    widgets/SettingsWidget.h
    widgets/SettingsWidget.cpp
    widgets/SettingsWidget.ui
    widgets/TestWidget.h
    widgets/TestWidget.cpp
    widgets/TestWidget.ui
    widgets/MainWindow.h
    widgets/MainWindow.cpp
    widgets/MainWindow.ui
)

target_link_libraries(SignLearnClient PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    ${OpenCV_LIBS}
)

# MinGW에서 한글 소스코드 깨짐 방지
target_compile_options(SignLearnClient PRIVATE
    -finput-charset=UTF-8
    -fexec-charset=UTF-8
)

target_include_directories(SignLearnClient PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/network
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets
    ${CMAKE_CURRENT_SOURCE_DIR}/camera
)

qt_finalize_executable(SignLearnClient)
