#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryFile>

#include "fall_types.h"
#include "fall_frame_parser.h"
#include "fall_judge_engine.h"
#include "fall_simulator.h"
#include "sound_alert.h"
#include "win_serial_port.h"
#include "mqtt_codec.h"
#include "huawei_iotda_client.h"
#include "event_logger.h"

using namespace qtfall;

class TestQtfallCore : public QObject
{
    Q_OBJECT

private slots:
    // --- FallFrameParser ---
    void testLegacyFrameValidAndRoundtrip();
    void testLegacyFrameRejectsInvalid();
    void testRobustFrameValidAndChecksum();
    void testRobustFrameRejectsBadXor();
    void testStreamingMixedChunksAndOverflow();
    void testStm32DebugLogsParsing();

    // --- FallJudgeEngine ---
    void testJudgeWindowTriggersOnSixHitsWithGeometry();
    void testJudgeGeometryFilterRejectsTallBox();
    void testJudgeHoldAndCooldownLifecycle();
    void testJudgeMuteAndReset();
    void testJudgeSideLyingToggleAndOfflineTimeout();

    // --- MqttCodec ---
    void testMqttRemainingLengthVarint();
    void testMqttConnectAndConnAckRoundtrip();
    void testMqttPublishQos1AndSubscribeRoundtrip();
    void testMqttPartialBufferDecode();

    // --- HuaweiIotdaClient ---
    void testIotdaOneJsonReportFormat();
    void testIotdaCommandParseAndResponse();
    void testIotdaShadowParse();
    void testIotdaParseAppMqttHeader();

    // --- EventLogger ---
    void testEventLoggerCsvAndJsonRoundtrip();

    // --- Simulator, SoundAlert & WinSerialPort ---
    void testSimulatorT3TriggersJudgeEngine();
    void testSoundAlertSynthesizeWavHeader();
    void testWinSerialPortEnumSmoke();
};

void TestQtfallCore::testLegacyFrameValidAndRoundtrip()
{
    FallRawFrame f;
    QVERIFY(FallFrameParser::parseLegacyFrame("483,60,156,386,Fall,0.72", f, 1000));
    QCOMPARE(f.protocol, FrameProtocolKind::Legacy);
    QCOMPARE(f.x, 483);
    QCOMPARE(f.y, 60);
    QCOMPARE(f.w, 156);
    QCOMPARE(f.h, 386);
    QVERIFY(f.isFall);
    QCOMPARE(f.poseClass, PoseClass::Fall);
    QVERIFY(qFuzzyCompare(f.score, 0.72));
    QCOMPARE(f.timestampMs, 1000);

    const QByteArray encoded = FallFrameParser::formatLegacyFrame(120, 240, 260, 140, false, 0.85);
    QCOMPARE(encoded, QByteArray("$120,240,260,140,NoFall,0.85#"));
}

void TestQtfallCore::testLegacyFrameRejectsInvalid()
{
    FallRawFrame f;
    QVERIFY(!FallFrameParser::parseLegacyFrame("10,20,30,40,Fall", f));
    QVERIFY(!FallFrameParser::parseLegacyFrame("10,20,30,40,Fall,1.20", f));
    QVERIFY(!FallFrameParser::parseLegacyFrame("-5,20,30,40,Fall,0.80", f));
    QVERIFY(!FallFrameParser::parseLegacyFrame("10,20,30,40,Unknown,0.80", f));
    QVERIFY(!FallFrameParser::parseLegacyFrame("10,20,30,40,Fall,NaN", f));
}

void TestQtfallCore::testRobustFrameValidAndChecksum()
{
    const QByteArray raw = FallFrameParser::formatRobustFrame(1, 1, 0, 87, 52, 61, 0);
    // Expected format: $F,1,1,0,87,52,61,0*65\r\n (from stm32_fall_uart.c comment!)
    QCOMPARE(raw, QByteArray("$F,1,1,0,87,52,61,0*65\r\n"));

    FallRawFrame f;
    QVERIFY(FallFrameParser::parseRobustFrame("F,1,1,0,87,52,61,0*65", f, 2500));
    QCOMPARE(f.protocol, FrameProtocolKind::Robust);
    QCOMPARE(f.evt, 1);
    QVERIFY(f.isFall);
    QCOMPARE(f.poseClass, PoseClass::Fall);
    QVERIFY(qFuzzyCompare(f.score, 0.87));
    QCOMPARE(f.cx, 52);
    QCOMPARE(f.cy, 61);
    QCOMPARE(f.seq, 0);
}

void TestQtfallCore::testRobustFrameRejectsBadXor()
{
    FallRawFrame f;
    QVERIFY(!FallFrameParser::parseRobustFrame("F,1,1,0,87,52,61,0*FF", f));
    QVERIFY(!FallFrameParser::parseRobustFrame("F,1,1,0,87,52,61*65", f));
}

void TestQtfallCore::testStreamingMixedChunksAndOverflow()
{
    FallFrameParser parser;
    QVector<FallRawFrame> frames;
    QVector<Stm32LogEvent> logs;
    QObject::connect(&parser, &FallFrameParser::frameParsed, [&](const FallRawFrame &fr) { frames.append(fr); });
    QObject::connect(&parser, &FallFrameParser::stm32LogParsed, [&](const Stm32LogEvent &ev) { logs.append(ev); });

    // Split across chunks + mixed STM32 debug line + noise + oversized bad frame
    parser.feedBytes("$100,200,220,", 100);
    parser.feedBytes("130,Fall,0.88#\r\n[ALARM] BEEP ON\r\n", 110);
    parser.feedBytes("$" + QByteArray(90, '9') + "#", 120); // overflow -> err
    parser.feedBytes("$F,0,0,3,91,50,40,7*", 130);
    const quint8 chk = FallFrameParser::computeXor("F,0,0,3,91,50,40,7");
    parser.feedBytes(QByteArray::number(chk, 16).toUpper().rightJustified(2, '0') + "\r\n", 140);

    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames[0].x, 100);
    QVERIFY(frames[0].isFall);
    QCOMPARE(frames[1].poseClass, PoseClass::Standing);
    QVERIFY(!frames[1].isFall);
    QCOMPARE(logs.size(), 1);
    QCOMPARE(logs[0].type, Stm32LogEvent::Type::BeepChanged);
    QCOMPARE(logs[0].beepOn, 1);
    QCOMPARE(parser.okFrameCount(), quint64(2));
    QCOMPARE(parser.errorFrameCount(), quint64(1));
}

void TestQtfallCore::testStm32DebugLogsParsing()
{
    Stm32LogEvent ev;
    QVERIFY(FallFrameParser::parseStm32LogLine(
        "[FALL] ALARM TRIGGERED! hits=6/10 score=0.85 bbox=(483,60,156,386)", ev));
    QCOMPARE(ev.type, Stm32LogEvent::Type::AlarmTriggered);
    QCOMPARE(ev.fallState, 1);
    QCOMPARE(ev.beepOn, 1);
    QCOMPARE(ev.winHits, 6);
    QVERIFY(qFuzzyCompare(ev.confidence, 0.85));
    QVERIFY(ev.hasBbox);
    QCOMPARE(ev.bbox, QRect(483, 60, 156, 386));

    Stm32LogEvent pubEv;
    QVERIFY(FallFrameParser::parseStm32LogLine(
        "[MQTT] Published: {\"services\":[{\"service_id\":\"FallDetect\",\"properties\":{\"fall_state\":1,\"confidence\":0.72,\"bbox\":\"[100,200,250,150]\",\"beep\":1,\"vision_offline\":0}}]}",
        pubEv));
    QCOMPARE(pubEv.type, Stm32LogEvent::Type::MqttPublished);
    QCOMPARE(pubEv.fallState, 1);
    QCOMPARE(pubEv.beepOn, 1);
    QCOMPARE(pubEv.visionOffline, 0);
    QVERIFY(qFuzzyCompare(pubEv.confidence, 0.72));
    QCOMPARE(pubEv.bbox, QRect(100, 200, 250, 150));
}

void TestQtfallCore::testJudgeWindowTriggersOnSixHitsWithGeometry()
{
    FallJudgeEngine eng;
    QSignalSpy alarmSpy(&eng, &FallJudgeEngine::alarmTriggered);

    FallRawFrame hit;
    hit.protocol = FrameProtocolKind::Legacy;
    hit.x = 120; hit.y = 280; hit.w = 240; hit.h = 120; // w > h
    hit.isFall = true; hit.poseClass = PoseClass::Fall; hit.score = 0.82;

    for (int i = 0; i < 5; ++i) {
        eng.processFrame(hit, 1000 + i * 100);
        QCOMPARE(eng.status().sysState, FallSysState::Idle);
        QCOMPARE(eng.status().winHits, i + 1);
    }
    eng.processFrame(hit, 1500); // 6th hit -> trigger!
    QCOMPARE(eng.status().sysState, FallSysState::Alarm);
    QVERIFY(eng.status().fallState);
    QVERIFY(eng.status().beepOn);
    QCOMPARE(eng.status().alarmCount, quint32(1));
    QCOMPARE(alarmSpy.count(), 1);
}

void TestQtfallCore::testJudgeGeometryFilterRejectsTallBox()
{
    FallJudgeEngine eng;
    FallRawFrame tallFall;
    tallFall.x = 120; tallFall.y = 100; tallFall.w = 110; tallFall.h = 300; // w <= h
    tallFall.isFall = true; tallFall.poseClass = PoseClass::Fall; tallFall.score = 0.90;

    for (int i = 0; i < 10; ++i) {
        eng.processFrame(tallFall, 1000 + i * 100);
    }
    QCOMPARE(eng.status().sysState, FallSysState::Idle);
    QCOMPARE(eng.status().winHits, 0);
}

void TestQtfallCore::testJudgeHoldAndCooldownLifecycle()
{
    FallJudgeEngine eng;
    FallJudgeConfig cfg = eng.config();
    cfg.alarmHoldMs = 3000;
    cfg.cooldownMs = 1500;
    eng.setConfig(cfg);

    FallRawFrame hit;
    hit.w = 220; hit.h = 120; hit.isFall = true; hit.poseClass = PoseClass::Fall; hit.score = 0.80;
    for (int i = 0; i < 6; ++i) eng.processFrame(hit, 1000 + i * 50);
    QCOMPARE(eng.status().sysState, FallSysState::Alarm);

    // NoFall frames during hold period must NOT cancel alarm
    FallRawFrame normal;
    normal.w = 100; normal.h = 240; normal.isFall = false; normal.poseClass = PoseClass::NoFall; normal.score = 0.90;
    eng.processFrame(normal, 2500);
    eng.onTick(2500);
    QCOMPARE(eng.status().sysState, FallSysState::Alarm);

    // After hold expires (1250 + 3000 = 4250) -> Cooldown
    eng.onTick(4300);
    QCOMPARE(eng.status().sysState, FallSysState::Cooldown);
    QVERIFY(!eng.status().fallState);
    QVERIFY(!eng.status().beepOn);

    // During cooldown, Fall frames must NOT trigger alarm
    for (int i = 0; i < 8; ++i) eng.processFrame(hit, 4400 + i * 50);
    QCOMPARE(eng.status().sysState, FallSysState::Cooldown);

    // After cooldown expires (4300 + 1500 = 5800) -> Idle
    eng.onTick(5900);
    QCOMPARE(eng.status().sysState, FallSysState::Idle);
}

void TestQtfallCore::testJudgeMuteAndReset()
{
    FallJudgeEngine eng;
    FallRawFrame hit;
    hit.w = 220; hit.h = 120; hit.isFall = true; hit.poseClass = PoseClass::Fall; hit.score = 0.85;
    for (int i = 0; i < 6; ++i) eng.processFrame(hit, 1000 + i * 50);
    QVERIFY(eng.status().beepOn);
    QVERIFY(eng.status().fallState);

    eng.mute(1500);
    QVERIFY(!eng.status().beepOn);
    QVERIFY(eng.status().fallState); // stays in alarm hold, buzzer silenced (matches app_fall.c)

    eng.resetAlarm(1600);
    QCOMPARE(eng.status().sysState, FallSysState::Idle);
    QVERIFY(!eng.status().fallState);
    QVERIFY(!eng.status().beepOn);
    QCOMPARE(eng.status().winHits, 0);
}

void TestQtfallCore::testJudgeSideLyingToggleAndOfflineTimeout()
{
    FallJudgeEngine eng;
    FallJudgeConfig cfg = eng.config();
    cfg.offlineTimeoutMs = 5000;
    cfg.treatSideLyingAsFall = false;
    eng.setConfig(cfg);

    FallRawFrame side;
    side.w = 240; side.h = 110; side.isFall = false; side.poseClass = PoseClass::SideLying; side.score = 0.80;
    for (int i = 0; i < 6; ++i) eng.processFrame(side, 1000 + i * 50);
    QCOMPARE(eng.status().sysState, FallSysState::Idle);

    cfg.treatSideLyingAsFall = true;
    eng.setConfig(cfg);
    for (int i = 0; i < 6; ++i) eng.processFrame(side, 2000 + i * 50);
    QCOMPARE(eng.status().sysState, FallSysState::Alarm);

    // Offline detection after 5000ms without frames
    eng.onTick(7250);
    QVERIFY(eng.status().visionOffline);

    // Any valid frame brings it back online
    eng.processFrame(side, 7500);
    QVERIFY(!eng.status().visionOffline);

    // Parsed Robust SideLying frame (state=0, cls=1) must keep horizontal placeholder bbox & trigger when enabled
    eng.resetAlarm(8000);
    const QByteArray robustSideRaw = FallFrameParser::formatRobustFrame(1, 0, 1, 84, 48, 65, 2);
    FallRawFrame parsedSide;
    QVERIFY(FallFrameParser::parseRobustFrame(robustSideRaw.mid(1).trimmed(), parsedSide, 8100));
    QCOMPARE(parsedSide.poseClass, PoseClass::SideLying);
    QVERIFY(parsedSide.w > parsedSide.h);
    for (int i = 0; i < 6; ++i) eng.processFrame(parsedSide, 8200 + i * 50);
    QCOMPARE(eng.status().sysState, FallSysState::Alarm);
}

void TestQtfallCore::testMqttRemainingLengthVarint()
{
    QCOMPARE(MqttCodec::encodeRemainingLength(0), QByteArray("\x00", 1));
    QCOMPARE(MqttCodec::encodeRemainingLength(127), QByteArray("\x7F", 1));
    QCOMPARE(MqttCodec::encodeRemainingLength(128), QByteArray("\x80\x01", 2));
    QCOMPARE(MqttCodec::encodeRemainingLength(16383), QByteArray("\xFF\x7F", 2));

    int len = 0, used = 0;
    QVERIFY(MqttCodec::decodeRemainingLength(QByteArray("\x80\x01", 2), 0, len, used));
    QCOMPARE(len, 128);
    QCOMPARE(used, 2);
}

void TestQtfallCore::testMqttConnectAndConnAckRoundtrip()
{
    const QByteArray pkt = MqttCodec::encodeConnect("cid_01", "user_01", "secret_pw", 30, true);
    QVERIFY(pkt.size() > 14);
    QCOMPARE(static_cast<quint8>(pkt[0]), quint8(0x10));
    QVERIFY(pkt.contains("MQTT"));
    QVERIFY(pkt.contains("cid_01"));
    QVERIFY(pkt.contains("user_01"));
    QVERIFY(pkt.contains("secret_pw"));

    QByteArray connack("\x20\x02\x00\x00", 4);
    MqttDecodedPacket dec;
    QCOMPARE(MqttCodec::tryDecodePacket(connack, dec), MqttCodec::DecodeStatus::Ok);
    QCOMPARE(dec.type, MqttPacketType::ConnAck);
    QCOMPARE(dec.connAckReturnCode, quint8(0));
    QVERIFY(connack.isEmpty());
}

void TestQtfallCore::testMqttPublishQos1AndSubscribeRoundtrip()
{
    const QByteArray payload(200, 'A'); // > 127 bytes -> 2-byte remaining length
    QByteArray pub = MqttCodec::encodePublish("$oc/devices/dev1/sys/properties/report", payload, 1, 0x1234, false);

    MqttDecodedPacket dec;
    QCOMPARE(MqttCodec::tryDecodePacket(pub, dec), MqttCodec::DecodeStatus::Ok);
    QCOMPARE(dec.type, MqttPacketType::Publish);
    QCOMPARE(dec.qos, 1);
    QCOMPARE(dec.packetId, quint16(0x1234));
    QCOMPARE(dec.topic, QStringLiteral("$oc/devices/dev1/sys/properties/report"));
    QCOMPARE(dec.payload, payload);

    const QByteArray sub = MqttCodec::encodeSubscribe(0x0042, "$oc/devices/dev1/sys/commands/#", 1);
    QCOMPARE(static_cast<quint8>(sub[0]), quint8(0x82));
    QVERIFY(sub.contains("$oc/devices/dev1/sys/commands/#"));
}

void TestQtfallCore::testMqttPartialBufferDecode()
{
    QByteArray full = MqttCodec::encodePublish("t/a", QByteArray("hello"), 0, 0, false);
    QByteArray partial = full.left(4);
    MqttDecodedPacket dec;
    QCOMPARE(MqttCodec::tryDecodePacket(partial, dec), MqttCodec::DecodeStatus::NeedMoreData);
    partial.append(full.mid(4));
    QCOMPARE(MqttCodec::tryDecodePacket(partial, dec), MqttCodec::DecodeStatus::Ok);
    QCOMPARE(dec.payload, QByteArray("hello"));
}

void TestQtfallCore::testIotdaOneJsonReportFormat()
{
    FallStatusSnapshot st;
    st.fallState = true;
    st.confidence = 0.725;
    st.x = 483; st.y = 60; st.w = 156; st.h = 386;
    st.beepOn = true;
    st.visionOffline = false;

    QCOMPARE(HuaweiIotdaClient::buildPropertyReportTopic("dev01"),
             QStringLiteral("$oc/devices/dev01/sys/properties/report"));

    const QByteArray jsonBytes = HuaweiIotdaClient::buildPropertyReportPayload("FallDetect", st);
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
    QVERIFY(doc.isObject());
    const QJsonObject svc = doc.object()["services"].toArray().at(0).toObject();
    QCOMPARE(svc["service_id"].toString(), QStringLiteral("FallDetect"));
    const QJsonObject props = svc["properties"].toObject();
    QCOMPARE(props["fall_state"].toInt(), 1);
    QVERIFY(qFuzzyCompare(props["confidence"].toDouble(), 0.73));
    QCOMPARE(props["bbox"].toString(), QStringLiteral("[483,60,156,386]"));
    QCOMPARE(props["beep"].toInt(), 1);
    QCOMPARE(props["vision_offline"].toInt(), 0);
}

void TestQtfallCore::testIotdaCommandParseAndResponse()
{
    IotdaCommand cmd;
    QVERIFY(HuaweiIotdaClient::parseCommandMessage(
        "$oc/devices/dev01/sys/commands/request_id=abc-123",
        R"({"service_id":"FallDetect","command_name":"MUTE","paras":{}})",
        cmd));
    QCOMPARE(cmd.requestId, QStringLiteral("abc-123"));
    QCOMPARE(cmd.commandName, QStringLiteral("MUTE"));
    QCOMPARE(HuaweiIotdaClient::buildCommandRespTopic("dev01", cmd.requestId),
             QStringLiteral("$oc/devices/dev01/sys/commands/response/request_id=abc-123"));

    const QByteArray resp = HuaweiIotdaClient::buildCommandResponsePayload(0, "muted");
    const QJsonDocument doc = QJsonDocument::fromJson(resp);
    QCOMPARE(doc.object()["result_code"].toInt(), 0);
    QCOMPARE(doc.object()["paras"].toObject()["result"].toString(), QStringLiteral("muted"));
}

void TestQtfallCore::testIotdaShadowParse()
{
    const QByteArray shadowJson = R"({
        "device_id": "dev01",
        "shadow": [{
            "service_id": "FallDetect",
            "reported": {
                "properties": {
                    "fall_state": 1,
                    "confidence": 0.91,
                    "bbox": "[120,240,260,130]",
                    "beep": 1,
                    "vision_offline": 0
                },
                "event_time": "20261113T090000Z"
            }
        }]
    })";
    FallStatusSnapshot snap;
    QString eventTime;
    QVERIFY(HuaweiIotdaClient::parseShadowResponse(shadowJson, snap, eventTime));
    QVERIFY(snap.fallState);
    QVERIFY(snap.beepOn);
    QVERIFY(!snap.visionOffline);
    QVERIFY(qFuzzyCompare(snap.confidence, 0.91));
    QCOMPARE(snap.x, 120);
    QCOMPARE(snap.y, 240);
    QCOMPARE(snap.w, 260);
    QCOMPARE(snap.h, 130);
    QCOMPARE(eventTime, QStringLiteral("20261113T090000Z"));
}

void TestQtfallCore::testIotdaParseAppMqttHeader()
{
    const QString header = R"(
        #define MQTT_BROKER_HOST "example.iotda.cn"
        #define MQTT_BROKER_PORT 1883U
        #define MQTT_CLIENT_ID "cid_test"
        #define MQTT_USERNAME "user_test"
        #define MQTT_PASSWORD "pw_test"
        #define HUAWEI_DEVICE_ID "dev_test"
        #define HUAWEI_SERVICE_ID "FallDetect"
    )";
    HuaweiCloudConfig cfg;
    QVERIFY(HuaweiIotdaClient::parseAppMqttHeader(header, cfg));
    QCOMPARE(cfg.brokerHost, QStringLiteral("example.iotda.cn"));
    QCOMPARE(cfg.brokerPort, quint16(1883));
    QCOMPARE(cfg.clientId, QStringLiteral("cid_test"));
    QCOMPARE(cfg.username, QStringLiteral("user_test"));
    QCOMPARE(cfg.password, QStringLiteral("pw_test"));
    QCOMPARE(cfg.deviceId, QStringLiteral("dev_test"));
    QCOMPARE(cfg.serviceId, QStringLiteral("FallDetect"));
}

void TestQtfallCore::testEventLoggerCsvAndJsonRoundtrip()
{
    EventLogger logger;
    AlarmEventRecord r;
    r.timestamp = QDateTime::fromString("2026-11-13T17:30:00", Qt::ISODate);
    r.source = QStringLiteral("串口/K230");
    r.eventType = QStringLiteral("跌倒报警");
    r.confidence = 0.88;
    r.bbox = QRect(100, 200, 260, 140);
    r.note = QStringLiteral("note, with \"comma\"");
    logger.addRecord(r);
    QCOMPARE(logger.count(), 1);

    const QByteArray csv = EventLogger::formatCsvBytes(logger.records());
    QVERIFY(csv.startsWith("\xEF\xBB\xBF")); // UTF-8 BOM for Excel
    QVERIFY(csv.contains("\"note, with \"\"comma\"\"\""));

    QTemporaryFile tmp;
    QVERIFY(tmp.open());
    const QString path = tmp.fileName();
    tmp.close();

    QVERIFY(logger.saveJson(path));

    EventLogger reloaded;
    QVERIFY(reloaded.loadJson(path));
    QCOMPARE(reloaded.count(), 1);
    QCOMPARE(reloaded.records().at(0).eventType, QStringLiteral("跌倒报警"));
    QCOMPARE(reloaded.records().at(0).bbox, QRect(100, 200, 260, 140));
}

void TestQtfallCore::testSimulatorT3TriggersJudgeEngine()
{
    FallSimulator sim;
    FallFrameParser parser;
    FallJudgeEngine eng;

    QObject::connect(&sim, &FallSimulator::frameGenerated, [&](const QByteArray &raw, const QString &) {
        parser.feedBytes(raw, 1000);
    });
    QObject::connect(&parser, &FallFrameParser::frameParsed, [&](const FallRawFrame &f) {
        eng.processFrame(f, 1000);
    });

    sim.start(FallSimulator::Scenario::QuickFall, false, 10);
    sim.stop(); // 手动步进 12 帧（4 帧站立 + 8 帧跌倒）
    for (int i = 0; i < 12; ++i) {
        QVERIFY(!sim.stepFrame().isEmpty());
    }
    QCOMPARE(eng.status().sysState, FallSysState::Alarm);
    QVERIFY(eng.status().fallState);

    // Robust 模式再测一次
    FallJudgeEngine engRobust;
    FallFrameParser parserRobust;
    QObject::connect(&parserRobust, &FallFrameParser::frameParsed, [&](const FallRawFrame &f) {
        engRobust.processFrame(f, 2000);
    });
    sim.start(FallSimulator::Scenario::QuickFall, true, 10);
    sim.stop();
    for (int i = 0; i < 12; ++i) {
        const QByteArray raw = sim.stepFrame();
        parserRobust.feedBytes(raw, 2000);
    }
    QCOMPARE(engRobust.status().sysState, FallSysState::Alarm);
}

void TestQtfallCore::testSoundAlertSynthesizeWavHeader()
{
    const QByteArray wav = SoundAlert::synthesizeAlertWav(500, 22050);
    QVERIFY(wav.startsWith("RIFF"));
    QCOMPARE(wav.mid(8, 4), QByteArray("WAVE"));
    QCOMPARE(wav.mid(36, 4), QByteArray("data"));
    QCOMPARE(wav.size(), 44 + (500 * 22050 / 1000) * 2);
}

void TestQtfallCore::testWinSerialPortEnumSmoke()
{
    const auto ports = WinSerialPort::availablePorts();
    for (const auto &p : ports) {
        QVERIFY(p.portName.startsWith(QStringLiteral("COM")));
    }
}

QTEST_MAIN(TestQtfallCore)
#include "tst_qtfall.moc"
