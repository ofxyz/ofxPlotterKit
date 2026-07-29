# GCC 9+ moved std::experimental::filesystem into a separate libstdc++fs.a.
# On MSYS2/UCRT64 this library lives in $(MSYSTEM_PREFIX)/lib and is not
# linked automatically, so we add it explicitly.
PROJECT_LDFLAGS = -lstdc++fs

