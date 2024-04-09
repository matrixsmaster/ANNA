QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++20

SOURCES += \
    ../aria.cpp \
    ../lscs.cpp \
    aboutbox.cpp \
    busybox.cpp \
    helpbox.cpp \
    main.cpp \
    mainwnd.cpp \
    rqpeditor.cpp \
    settingsdialog.cpp

HEADERS += \
    ../aria.h \
    ../aria_binds.h \
    ../lfs.h \
    ../lscs.h \
    ../md5calc.h \
    ../netclient.h \
    ../server/base64m.h \
    ../server/codec.h \
    ../vecstore.h \
    aboutbox.h \
    busybox.h \
    helpbox.h \
    mainwnd.h \
    mainwnd_tabs.h \
    rqpeditor.h \
    settingsdialog.h

FORMS += \
    aboutbox.ui \
    busybox.ui \
    helpbox.ui \
    mainwnd.ui \
    rqpeditor.ui \
    settingsdialog.ui

DEFINES += _XOPEN_SOURCE=600

QMAKE_CFLAGS += -fPIC
QMAKE_CXXFLAGS += -fPIC

win32 {
    DEFINES += _WIN32_WINNT=0x602

    SOURCES += \
        ../brain.cpp \
        ../clip.cpp \
        ../common.cpp \
        ../ggml-alloc.c \
        ../ggml-backend.c \
        ../ggml.c \
        ../ggml-quants.c \
        ../grammar-parser.cpp \
        ../llama.cpp \
        ../netclient.cpp \
        ../sampling.cpp \
        ../lua/lapi.c \
        ../lua/lauxlib.c \
        ../lua/lbaselib.c \
        ../lua/lcode.c \
        ../lua/lcorolib.c \
        ../lua/lctype.c \
        ../lua/ldblib.c \
        ../lua/ldebug.c \
        ../lua/ldo.c \
        ../lua/ldump.c \
        ../lua/lfunc.c \
        ../lua/lgc.c \
        ../lua/linit.c \
        ../lua/liolib.c \
        ../lua/llex.c \
        ../lua/lmathlib.c \
        ../lua/lmem.c \
        ../lua/loadlib.c \
        ../lua/lobject.c \
        ../lua/lopcodes.c \
        ../lua/loslib.c \
        ../lua/lparser.c \
        ../lua/lstate.c \
        ../lua/lstring.c \
        ../lua/lstrlib.c \
        ../lua/ltable.c \
        ../lua/ltablib.c \
        ../lua/ltm.c \
        ../lua/lundump.c \
        ../lua/lutf8lib.c \
        ../lua/lvm.c \
        ../lua/lzio.c

    HEADERS += \
        ../brain.h \
        ../clip.h \
        ../common.h \
        ../dtypes.h \
        ../ggml-alloc.h \
        ../ggml-backend.h \
        ../ggml.h \
        ../ggml-backend-impl.h \
        ../ggml-impl.h \
        ../ggml-quants.h \
        ../grammar-parser.h \
        ../llama.h \
        ../sampling.h \
        ../stb_image.h \
        ../unicode.h \
        ../lua/lapi.h \
        ../lua/lauxlib.h \
        ../lua/lcode.h \
        ../lua/lctype.h \
        ../lua/ldebug.h \
        ../lua/ldo.h \
        ../lua/lfunc.h \
        ../lua/lgc.h \
        ../lua/ljumptab.h \
        ../lua/llex.h \
        ../lua/llimits.h \
        ../lua/lmem.h \
        ../lua/lobject.h \
        ../lua/lopcodes.h \
        ../lua/lopnames.h \
        ../lua/lparser.h \
        ../lua/lprefix.h \
        ../lua/lstate.h \
        ../lua/lstring.h \
        ../lua/ltable.h \
        ../lua/ltm.h \
        ../lua/lua.h \
        ../lua/lua.hpp \
        ../lua/luaconf.h \
        ../lua/lualib.h \
        ../lua/lundump.h \
        ../lua/lvm.h \
        ../lua/lzio.h

    CWARNS = -Wall -Wextra -Wpedantic -Wcast-qual -Wdouble-promotion -Wshadow -Wstrict-prototypes -Wpointer-arith -Wmissing-prototypes -Werror=implicit-int -Wno-unused-function -Wno-deprecated-declarations -Wno-unused-variable -Wno-unused-parameter
    CXXWARNS = -Wall -Wextra -Wpedantic -Wcast-qual -Wmissing-declarations -Wno-unused-function -Wno-multichar -Wno-deprecated-declarations -Wno-unused-variable -Wno-unused-parameter -Wno-format-truncation -Wno-array-bounds -Wno-unused-result

    QMAKE_CFLAGS += $$CWARNS -mno-ms-bitfields
    QMAKE_CXXFLAGS += $$CXXWARNS -mno-ms-bitfields

    QMAKE_LFLAGS_WINDOWS += -Wl,--stack,100000000

    oldcpu = $$(ANNA_OLDCPU)
    #message($$oldcpu)
    !equals(oldcpu,1) {
        message("Making Haswell build")
        QMAKE_CFLAGS += -march=haswell
        QMAKE_CXXFLAGS += -march=haswell
    }

    LIBS += -lws2_32

    RC_ICONS = anna1.ico
}

QMAKE_CFLAGS_DEBUG += -O0 -g
QMAKE_CFLAGS_RELEASE += -DNDEBUG -Ofast
QMAKE_CXXFLAGS_DEBUG += -O0 -g
QMAKE_CXXFLAGS_RELEASE += -DNDEBUG -Ofast

linux {
    cublas = $$(USE_CUBLAS)
    #message($$cublas)

    DEFINES += _GNU_SOURCE ANNA_USE_MMAP
    QMAKE_CFLAGS += -march=native -mtune=native
    QMAKE_CXXFLAGS += -march=native -mtune=native
    LIBS += -L.. -lanna -L../lua -llua

    equals(cublas,1) {
        DEFINES += GGML_USE_CUBLAS
        LIBS += -L/usr/local/cuda/lib64 -L/opt/cuda/lib64 -lcuda -lcublas -lculibos -lcudart -lcublasLt
    }
}

INCLUDEPATH += $$PWD/.. $$PWD/../server $$PWD/../lua
DEPENDPATH += $$PWD/.. $$PWD/../server $$PWD/../lua

RESOURCES += \
    resources.qrc
