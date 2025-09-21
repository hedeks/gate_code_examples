TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        communicationmanager.cpp \
        gate.cpp \
        instance.cpp \
        instancebuilder.cpp \
        instancemanager.cpp \
        main.cpp \
        systemconfig.cpp

HEADERS += \
    communicationmanager.h \
    gate.h \
    instance.h \
    instancebuilder.h \
    instancemanager.h \
    systemconfig.h
