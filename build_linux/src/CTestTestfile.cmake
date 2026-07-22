# CMake generated Testfile for 
# Source directory: /mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/src
# Build directory: /mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_linux/src
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[hummingbird_api]=] "/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/build_linux/src/test_hummingbird")
set_tests_properties([=[hummingbird_api]=] PROPERTIES  _BACKTRACE_TRIPLES "/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/src/CMakeLists.txt;39;add_test;/mnt/c/Users/praya/OneDrive/Documents/Project/Hummingbird/src/CMakeLists.txt;0;")
subdirs("common")
subdirs("platform")
subdirs("logging")
subdirs("profiler")
subdirs("config")
subdirs("threadpool")
subdirs("device")
subdirs("tensor")
subdirs("quant")
subdirs("memory")
subdirs("kernel")
subdirs("core")
subdirs("stream")
subdirs("kv")
subdirs("backend")
subdirs("tokenizer")
subdirs("graph")
subdirs("model")
subdirs("executor")
subdirs("scheduler")
subdirs("context")
subdirs("runtime")
