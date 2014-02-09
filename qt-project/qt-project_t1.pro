TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

DEFINES += REDIRECT_PRINT_RECORD

SOURCES += \
    ../cs222/src/rbf/rbfm.cc \
    ../cs222/src/rbf/pfm.cc \
    ../cs222/src/util/returncodes.cc \
    ../cs222/src/util/dbgout.cc \
    ../cs222/src/rm/rm.cc \
    ../cs222/src/rm/rmtest_1.cc

HEADERS += \
    ../cs222/src/rbf/rbfm.h \
    ../cs222/src/rbf/pfm.h \
    ../cs222/src/util/returncodes.h \
    ../cs222/src/util/dbgout.h \
    ../cs222/src/util/hash.h \
    ../cs222/src/rm/rm.h \
    ../cs222/src/rm/test_util.h \

OTHER_FILES +=
