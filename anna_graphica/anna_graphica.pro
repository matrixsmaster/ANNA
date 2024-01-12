QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++2a

SOURCES += \
    ../brain.cpp \
    ../clip.cpp \
    ../common.cpp \
    ../ggml-alloc.c \
    ../ggml-backend.c \
    ../ggml.c \
    ../k_quants.c \
    ../llama.cpp \
    ../sampling.cpp \
    main.cpp \
    mainwnd.cpp

HEADERS += \
    ../brain.h \
    ../clip.h \
    ../common.h \
    ../ggml-alloc.h \
    ../ggml-backend.h \
    ../ggml.h \
    ../k_quants.h \
    ../llama.h \
    ../sampling.h \
    ../stb_image.h \
    ../unicode.h \
    mainwnd.h

FORMS += \
    mainwnd.ui

QMAKE_CFLAGS += -D_XOPEN_SOURCE=600 -D_GNU_SOURCE -DGGML_USE_K_QUANTS -fPIC -Wall -Wextra -Wpedantic -Wcast-qual -Wdouble-promotion -Wshadow -Wstrict-prototypes -Wpointer-arith -Wmissing-prototypes -Werror=implicit-int -Wno-unused-function -Wno-deprecated-declarations -Wno-unused-variable -Wno-unused-parameter -march=native -mtune=native
QMAKE_CFLAGS_DEBUG += -O0 -g
QMAKE_CFLAGS_RELEASE += -DNDEBUG -Ofast
QMAKE_CXXFLAGS += -D_XOPEN_SOURCE=600 -D_GNU_SOURCE -DGGML_USE_K_QUANTS -fPIC -Wall -Wextra -Wpedantic -Wcast-qual -Wmissing-declarations -Wno-unused-function -Wno-multichar -Wno-deprecated-declarations -Wno-unused-variable -Wno-unused-parameter -Wno-format-truncation -Wno-array-bounds -march=native -mtune=native
QMAKE_CXXFLAGS_DEBUG += -O0 -g
QMAKE_CXXFLAGS_RELEASE += -DNDEBUG -Ofast

INCLUDEPATH += $$PWD/../
DEPENDPATH += $$PWD/../
