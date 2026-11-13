QT += widgets network

CONFIG += c++17

win32: LIBS += -lwinmm -ladvapi32 -lsetupapi

INCLUDEPATH += \
    $$PWD \
    $$PWD/protocol \
    $$PWD/core \
    $$PWD/comm \
    $$PWD/widgets

SOURCES += \
    main.cpp \
    falling.cpp \
    protocol/fall_frame_parser.cpp \
    core/fall_judge_engine.cpp \
    core/fall_simulator.cpp \
    core/event_logger.cpp \
    core/sound_alert.cpp \
    comm/win_serial_port.cpp \
    comm/mqtt_codec.cpp \
    comm/mqtt_client.cpp \
    comm/huawei_iotda_client.cpp \
    widgets/vision_canvas_widget.cpp \
    widgets/confidence_chart_widget.cpp \
    widgets/window_slots_widget.cpp

HEADERS += \
    falling.h \
    protocol/fall_types.h \
    protocol/fall_frame_parser.h \
    core/fall_judge_engine.h \
    core/fall_simulator.h \
    core/event_logger.h \
    core/sound_alert.h \
    comm/win_serial_port.h \
    comm/mqtt_codec.h \
    comm/mqtt_client.h \
    comm/huawei_iotda_client.h \
    widgets/vision_canvas_widget.h \
    widgets/confidence_chart_widget.h \
    widgets/window_slots_widget.h

FORMS += \
    falling.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
