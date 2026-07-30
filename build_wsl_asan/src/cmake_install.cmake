# Install script for directory: /mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/src

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/common/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/platform/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/logging/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/profiler/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/config/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/threadpool/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/device/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/tensor/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/quant/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/memory/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/json/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/kernel/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/core/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/stream/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/kv/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/backend/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/tokenizer/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/graph/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/planner/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/model/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/adapter/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/executor/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/scheduler/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/context/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_wsl_asan/src/runtime/cmake_install.cmake")
endif()

