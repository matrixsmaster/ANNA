QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++20

SOURCES += \
    aboutbox.cpp \
    main.cpp \
    mainwnd.cpp \
    settingsdialog.cpp

HEADERS += \
    ../brain.h \
    aboutbox.h \
    mainwnd.h \
    settingsdialog.h

FORMS += \
    aboutbox.ui \
    mainwnd.ui \
    settingsdialog.ui

DEFINES += _XOPEN_SOURCE=600 _GNU_SOURCE GGML_USE_K_QUANTS

win32 {
    SOURCES += \
        ../brain.cpp \
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
}

QMAKE_CFLAGS += -fPIC -march=native -mtune=native
QMAKE_CXXFLAGS += -fPIC -march=native -mtune=native
QMAKE_CFLAGS_DEBUG += -O0 -g
QMAKE_CFLAGS_RELEASE += -DNDEBUG -Ofast
QMAKE_CXXFLAGS_DEBUG += -O0 -g
QMAKE_CXXFLAGS_RELEASE += -DNDEBUG -Ofast

linux {
    #DEFINES += GGML_USE_CUBLAS
    LIBS += -L.. -lanna
    #LIBS += -L/usr/local/cuda/lib64 -L/opt/cuda/lib64 -lcublas -lculibos -lcudart -lcublasLt
}

INCLUDEPATH += $$PWD/../
DEPENDPATH += $$PWD/../

RESOURCES += \
    resources.qrc
