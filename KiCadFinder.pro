QT += core gui sql widgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
CONFIG += lrelease embed_translations

TARGET = KiCadFinder
TEMPLATE = app

VERSION = 1.0.0
DEFINES += APP_VERSION=\\\"$$VERSION\\\"
DEFINES += APP_NAME=\\\"$$TARGET\\\"

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    databasemanager.cpp \
    syncworker.cpp

HEADERS += \
    mainwindow.h \
    databasemanager.h \
    syncworker.h

RESOURCES += \
    res.qrc

RC_FILE = app.rc

TRANSLATIONS += \
    KiCadFinder_ru.ts


