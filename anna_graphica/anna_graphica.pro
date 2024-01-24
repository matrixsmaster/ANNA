QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++20

SOURCES += \
    ../brain.cpp \
    aboutbox.cpp \
    helpbox.cpp \
    main.cpp \
    mainwnd.cpp \
    settingsdialog.cpp

HEADERS += \
    ../brain.h \
    aboutbox.h \
    helpbox.h \
    mainwnd.h \
    settingsdialog.h

FORMS += \
    aboutbox.ui \
    helpbox.ui \
    mainwnd.ui \
    settingsdialog.ui

DEFINES += _XOPEN_SOURCE=600 GGML_USE_K_QUANTS

win32 {
    DEFINES += _WIN32_WINNT=0x602

    SOURCES += \
        ../clip.cpp \
        ../common.cpp \
        ../ggml-alloc.c \
        ../ggml-backend.c \
        ../ggml.c \
        ../k_quants.c \
        ../llama.cpp \
        ../sampling.cpp

    HEADERS += \
        ../clip.h \
        ../common.h \
        ../ggml-alloc.h \
        ../ggml-backend.h \
        ../ggml.h \
        ../k_quants.h \
        ../llama.h \
        ../sampling.h \
        ../stb_image.h \
        ../unicode.h

    CWARNS = -Wall -Wextra -Wpedantic -Wcast-qual -Wdouble-promotion -Wshadow -Wstrict-prototypes -Wpointer-arith -Wmissing-prototypes -Werror=implicit-int -Wno-unused-function -Wno-deprecated-declarations -Wno-unused-variable -Wno-unused-parameter
    CXXWARNS = -Wall -Wextra -Wpedantic -Wcast-qual -Wmissing-declarations -Wno-unused-function -Wno-multichar -Wno-deprecated-declarations -Wno-unused-variable -Wno-unused-parameter -Wno-format-truncation -Wno-array-bounds -Wno-unused-result

    QMAKE_CFLAGS += $$CWARNS
    QMAKE_CXXFLAGS += $$CXXWARNS

    QMAKE_CFLAGS += -fPIC -march=haswell
    QMAKE_CXXFLAGS += -fPIC -march=haswell

    RC_ICONS = anna1.ico
}

QMAKE_CFLAGS_DEBUG += -O0 -g
QMAKE_CFLAGS_RELEASE += -DNDEBUG -Ofast
QMAKE_CXXFLAGS_DEBUG += -O0 -g
QMAKE_CXXFLAGS_RELEASE += -DNDEBUG -Ofast

linux {
    cublas = $$(USE_CUBLAS)
    message($$cublas)

    DEFINES += _GNU_SOURCE ANNA_USE_MMAP
    QMAKE_CFLAGS += -fPIC -march=native -mtune=native
    QMAKE_CXXFLAGS += -fPIC -march=native -mtune=native
    LIBS += -L.. -lanna

    equals(cublas,1) {
        DEFINES += GGML_USE_CUBLAS
        LIBS += -L/usr/local/cuda/lib64 -L/opt/cuda/lib64 -lcublas -lculibos -lcudart -lcublasLt
    }
}

INCLUDEPATH += $$PWD/../
DEPENDPATH += $$PWD/../

RESOURCES += \
    resources.qrc