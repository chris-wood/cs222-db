TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

INCLUDEPATH += ../cs222/inc/boost_1_55_0/

SOURCES += \
    ../cs222/src/rbf/rbftest.cc \
    ../cs222/src/rbf/rbfm.cc \
    ../cs222/src/rbf/pfm.cc \
    ../cs222/src/rbf/returncodes.cpp

HEADERS += \
    ../cs222/src/rbf/rbfm.h \
    ../cs222/src/rbf/pfm.h \
    ../cs222/src/rbf/returncodes.h

