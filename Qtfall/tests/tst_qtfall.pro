QT += testlib network
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

win32: LIBS += -lwinmm -ladvapi32 -lsetupapi

INCLUDEPATH += .. ../protocol ../core ../comm

SOURCES += \
    tst_qtfall.cpp \
    ../protocol/fall_frame_parser.cpp \
    ../core/fall_judge_engine.cpp \
    ../core/fall_simulator.cpp \
    ../core/sound_alert.cpp \
    ../core/event_logger.cpp \
    ../comm/win_serial_port.cpp \
    ../comm/mqtt_codec.cpp \
    ../comm/mqtt_client.cpp \
    ../comm/huawei_iotda_client.cpp

HEADERS += \
    ../protocol/fall_types.h \
    ../protocol/fall_frame_parser.h \
    ../core/fall_judge_engine.h \
    ../core/fall_simulator.h \
    ../core/sound_alert.h \
    ../core/event_logger.h \
    ../comm/win_serial_port.h \
    ../comm/mqtt_codec.h \
    ../comm/mqtt_client.h \
    ../comm/huawei_iotda_client.h
