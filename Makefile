# HL7Lens - HL7 v2 decoder plugin for Notepad++ - cross-built with mingw-w64 (make x64 / make x86), core tested natively (make test)
CXXFLAGS = -std=c++17 -O2 -DUNICODE -D_UNICODE -Isrc -Inpphdr -Wall -Wno-unknown-pragmas -Wno-cast-function-type -Wno-misleading-indentation -Wno-unused-function
LDFLAGS  = -shared -static -static-libgcc -static-libstdc++ -Wl,--kill-at
LIBS     = -lcomctl32 -lshell32 -luser32 -lgdi32 -lole32 -ladvapi32 -lkernel32
CORE     = src/hl7.cpp src/dict.cpp src/dict_gen.cpp src/vdict_gen.cpp
SRC      = $(CORE) src/assets_gen.cpp src/plugin.cpp

all: x64 x86 test

build/x64 build/x86 build/native:
	mkdir -p $@

x64: build/x64
	x86_64-w64-mingw32-windres -Isrc src/plugin.rc -O coff -o build/x64/plugin.res
	x86_64-w64-mingw32-g++ $(CXXFLAGS) -o build/x64/HL7Lens.dll $(SRC) build/x64/plugin.res src/HL7Lens.def $(LDFLAGS) $(LIBS)
	x86_64-w64-mingw32-g++ $(CXXFLAGS) -static -o build/x64/hl7_cli.exe $(CORE) src/cli.cpp

x86: build/x86
	i686-w64-mingw32-windres -Isrc src/plugin.rc -O coff -o build/x86/plugin.res
	i686-w64-mingw32-g++ $(CXXFLAGS) -o build/x86/HL7Lens.dll $(SRC) build/x86/plugin.res src/HL7Lens.def $(LDFLAGS) $(LIBS)
	i686-w64-mingw32-g++ $(CXXFLAGS) -static -o build/x86/hl7_cli.exe $(CORE) src/cli.cpp

test: build/native
	g++ -std=c++17 -O2 -Wall -Wno-misleading-indentation -Wno-unused-function -o build/native/hl7_cli $(CORE) src/cli.cpp
	python3 tests/test_hl7.py build/native/hl7_cli

data:
	python3 tools/gen_dict.py tools/dictionary.json src/dict_gen.cpp
	python3 tools/gen_assets.py tools/HL7_v2.xml tools/sample.hl7 src/assets_gen.cpp
	python3 tools/gen_vdict.py cpp tools/vdict.json src/vdict_gen.cpp

clean:
	rm -rf build
