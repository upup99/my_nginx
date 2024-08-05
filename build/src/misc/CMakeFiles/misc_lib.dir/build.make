# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.23

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/local/cmake/bin/cmake

# The command to remove a file.
RM = /usr/local/cmake/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /root/code/nginx

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /root/code/nginx/build

# Include any dependencies generated for this target.
include src/misc/CMakeFiles/misc_lib.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include src/misc/CMakeFiles/misc_lib.dir/compiler_depend.make

# Include the progress variables for this target.
include src/misc/CMakeFiles/misc_lib.dir/progress.make

# Include the compile flags for this target's objects.
include src/misc/CMakeFiles/misc_lib.dir/flags.make

src/misc/CMakeFiles/misc_lib.dir/ngx_c_memory.cpp.o: src/misc/CMakeFiles/misc_lib.dir/flags.make
src/misc/CMakeFiles/misc_lib.dir/ngx_c_memory.cpp.o: ../src/misc/ngx_c_memory.cpp
src/misc/CMakeFiles/misc_lib.dir/ngx_c_memory.cpp.o: src/misc/CMakeFiles/misc_lib.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/root/code/nginx/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object src/misc/CMakeFiles/misc_lib.dir/ngx_c_memory.cpp.o"
	cd /root/code/nginx/build/src/misc && /opt/rh/devtoolset-9/root/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT src/misc/CMakeFiles/misc_lib.dir/ngx_c_memory.cpp.o -MF CMakeFiles/misc_lib.dir/ngx_c_memory.cpp.o.d -o CMakeFiles/misc_lib.dir/ngx_c_memory.cpp.o -c /root/code/nginx/src/misc/ngx_c_memory.cpp

src/misc/CMakeFiles/misc_lib.dir/ngx_c_memory.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/misc_lib.dir/ngx_c_memory.cpp.i"
	cd /root/code/nginx/build/src/misc && /opt/rh/devtoolset-9/root/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /root/code/nginx/src/misc/ngx_c_memory.cpp > CMakeFiles/misc_lib.dir/ngx_c_memory.cpp.i

src/misc/CMakeFiles/misc_lib.dir/ngx_c_memory.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/misc_lib.dir/ngx_c_memory.cpp.s"
	cd /root/code/nginx/build/src/misc && /opt/rh/devtoolset-9/root/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /root/code/nginx/src/misc/ngx_c_memory.cpp -o CMakeFiles/misc_lib.dir/ngx_c_memory.cpp.s

src/misc/CMakeFiles/misc_lib.dir/ngx_c_threadpool.cpp.o: src/misc/CMakeFiles/misc_lib.dir/flags.make
src/misc/CMakeFiles/misc_lib.dir/ngx_c_threadpool.cpp.o: ../src/misc/ngx_c_threadpool.cpp
src/misc/CMakeFiles/misc_lib.dir/ngx_c_threadpool.cpp.o: src/misc/CMakeFiles/misc_lib.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/root/code/nginx/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Building CXX object src/misc/CMakeFiles/misc_lib.dir/ngx_c_threadpool.cpp.o"
	cd /root/code/nginx/build/src/misc && /opt/rh/devtoolset-9/root/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT src/misc/CMakeFiles/misc_lib.dir/ngx_c_threadpool.cpp.o -MF CMakeFiles/misc_lib.dir/ngx_c_threadpool.cpp.o.d -o CMakeFiles/misc_lib.dir/ngx_c_threadpool.cpp.o -c /root/code/nginx/src/misc/ngx_c_threadpool.cpp

src/misc/CMakeFiles/misc_lib.dir/ngx_c_threadpool.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/misc_lib.dir/ngx_c_threadpool.cpp.i"
	cd /root/code/nginx/build/src/misc && /opt/rh/devtoolset-9/root/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /root/code/nginx/src/misc/ngx_c_threadpool.cpp > CMakeFiles/misc_lib.dir/ngx_c_threadpool.cpp.i

src/misc/CMakeFiles/misc_lib.dir/ngx_c_threadpool.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/misc_lib.dir/ngx_c_threadpool.cpp.s"
	cd /root/code/nginx/build/src/misc && /opt/rh/devtoolset-9/root/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /root/code/nginx/src/misc/ngx_c_threadpool.cpp -o CMakeFiles/misc_lib.dir/ngx_c_threadpool.cpp.s

src/misc/CMakeFiles/misc_lib.dir/ngx_c_crc32.cpp.o: src/misc/CMakeFiles/misc_lib.dir/flags.make
src/misc/CMakeFiles/misc_lib.dir/ngx_c_crc32.cpp.o: ../src/misc/ngx_c_crc32.cpp
src/misc/CMakeFiles/misc_lib.dir/ngx_c_crc32.cpp.o: src/misc/CMakeFiles/misc_lib.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/root/code/nginx/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_3) "Building CXX object src/misc/CMakeFiles/misc_lib.dir/ngx_c_crc32.cpp.o"
	cd /root/code/nginx/build/src/misc && /opt/rh/devtoolset-9/root/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT src/misc/CMakeFiles/misc_lib.dir/ngx_c_crc32.cpp.o -MF CMakeFiles/misc_lib.dir/ngx_c_crc32.cpp.o.d -o CMakeFiles/misc_lib.dir/ngx_c_crc32.cpp.o -c /root/code/nginx/src/misc/ngx_c_crc32.cpp

src/misc/CMakeFiles/misc_lib.dir/ngx_c_crc32.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/misc_lib.dir/ngx_c_crc32.cpp.i"
	cd /root/code/nginx/build/src/misc && /opt/rh/devtoolset-9/root/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /root/code/nginx/src/misc/ngx_c_crc32.cpp > CMakeFiles/misc_lib.dir/ngx_c_crc32.cpp.i

src/misc/CMakeFiles/misc_lib.dir/ngx_c_crc32.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/misc_lib.dir/ngx_c_crc32.cpp.s"
	cd /root/code/nginx/build/src/misc && /opt/rh/devtoolset-9/root/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /root/code/nginx/src/misc/ngx_c_crc32.cpp -o CMakeFiles/misc_lib.dir/ngx_c_crc32.cpp.s

# Object files for target misc_lib
misc_lib_OBJECTS = \
"CMakeFiles/misc_lib.dir/ngx_c_memory.cpp.o" \
"CMakeFiles/misc_lib.dir/ngx_c_threadpool.cpp.o" \
"CMakeFiles/misc_lib.dir/ngx_c_crc32.cpp.o"

# External object files for target misc_lib
misc_lib_EXTERNAL_OBJECTS =

../lib/libmisc_lib.a: src/misc/CMakeFiles/misc_lib.dir/ngx_c_memory.cpp.o
../lib/libmisc_lib.a: src/misc/CMakeFiles/misc_lib.dir/ngx_c_threadpool.cpp.o
../lib/libmisc_lib.a: src/misc/CMakeFiles/misc_lib.dir/ngx_c_crc32.cpp.o
../lib/libmisc_lib.a: src/misc/CMakeFiles/misc_lib.dir/build.make
../lib/libmisc_lib.a: src/misc/CMakeFiles/misc_lib.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/root/code/nginx/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_4) "Linking CXX static library ../../../lib/libmisc_lib.a"
	cd /root/code/nginx/build/src/misc && $(CMAKE_COMMAND) -P CMakeFiles/misc_lib.dir/cmake_clean_target.cmake
	cd /root/code/nginx/build/src/misc && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/misc_lib.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
src/misc/CMakeFiles/misc_lib.dir/build: ../lib/libmisc_lib.a
.PHONY : src/misc/CMakeFiles/misc_lib.dir/build

src/misc/CMakeFiles/misc_lib.dir/clean:
	cd /root/code/nginx/build/src/misc && $(CMAKE_COMMAND) -P CMakeFiles/misc_lib.dir/cmake_clean.cmake
.PHONY : src/misc/CMakeFiles/misc_lib.dir/clean

src/misc/CMakeFiles/misc_lib.dir/depend:
	cd /root/code/nginx/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /root/code/nginx /root/code/nginx/src/misc /root/code/nginx/build /root/code/nginx/build/src/misc /root/code/nginx/build/src/misc/CMakeFiles/misc_lib.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : src/misc/CMakeFiles/misc_lib.dir/depend

