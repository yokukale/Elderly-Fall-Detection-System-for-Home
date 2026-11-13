#include "falling.h"
#include "widgets/confidence_chart_widget.h"
#include "widgets/vision_canvas_widget.h"
#include "widgets/window_slots_widget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QtTest>

using namespace qtfall;

class QtFallUiTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("QtfallTest"));
        QCoreApplication::setApplicationName(QStringLiteral("QtfallUiSmoke"));
        QStandardPaths::setTestModeEnabled(true);

        const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        const QString cfgDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        if (!dataDir.isEmpty()) QDir(dataDir).removeRecursively();
        if (!cfgDir.isEmpty()) QDir(cfgDir).removeRecursively();
    }

    void testUiStructureTabSwitchAndCustomPaint()
    {
        falling win;
        win.show();
        QTest::qWait(50);

        auto *chkSound = win.findChild<QCheckBox *>(QStringLiteral("chkSoundAlert"));
        auto *tabMain = win.findChild<QTabWidget *>(QStringLiteral("tabMain"));
        auto *canvas = win.findChild<VisionCanvasWidget *>(QStringLiteral("canvasVision"));
        auto *chart = win.findChild<ConfidenceChartWidget *>(QStringLiteral("chartConfidence"));
        auto *slotsWidget = win.findChild<WindowSlotsWidget *>(QStringLiteral("widgetWindowSlots"));

        QVERIFY(chkSound != nullptr);
        QVERIFY(tabMain != nullptr);
        QVERIFY(canvas != nullptr);
        QVERIFY(chart != nullptr);
        QVERIFY(slotsWidget != nullptr);

        chkSound->setChecked(false);
        QCOMPARE(tabMain->count(), 4);

        for (int i = 0; i < tabMain->count(); ++i) {
            tabMain->setCurrentIndex(i);
            QApplication::processEvents();
            QCOMPARE(tabMain->currentIndex(), i);
        }
        tabMain->setCurrentIndex(0);
        QApplication::processEvents();

        const QPixmap pCanvas = canvas->grab();
        const QPixmap pChart = chart->grab();
        const QPixmap pSlots = slotsWidget->grab();
        QVERIFY(!pCanvas.isNull() && pCanvas.width() > 0 && pCanvas.height() > 0);
        QVERIFY(!pChart.isNull() && pChart.width() > 0 && pChart.height() > 0);
        QVERIFY(!pSlots.isNull() && pSlots.width() > 0 && pSlots.height() > 0);

        QVERIFY(win.close());
    }

    void testSimulationAlarmMuteResetAndHistory()
    {
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).removeRecursively();

        falling win;
        win.show();

        auto *chkSound = win.findChild<QCheckBox *>(QStringLiteral("chkSoundAlert"));
        auto *tabMain = win.findChild<QTabWidget *>(QStringLiteral("tabMain"));
        auto *lblSysBadge = win.findChild<QLabel *>(QStringLiteral("lblSysBadge"));
        auto *lblAlarmCount = win.findChild<QLabel *>(QStringLiteral("lblAlarmCountValue"));
        auto *lblBeepPill = win.findChild<QLabel *>(QStringLiteral("lblBeepPill"));
        auto *lblBboxInfo = win.findChild<QLabel *>(QStringLiteral("lblBboxInfo"));
        auto *btnSimT3 = win.findChild<QPushButton *>(QStringLiteral("btnSimT3"));
        auto *btnSimStop = win.findChild<QPushButton *>(QStringLiteral("btnSimStop"));
        auto *spinSimFps = win.findChild<QSpinBox *>(QStringLiteral("spinSimFps"));
        auto *comboSimProto = win.findChild<QComboBox *>(QStringLiteral("comboSimProtocol"));
        auto *btnQuickMute = win.findChild<QPushButton *>(QStringLiteral("btnQuickMute"));
        auto *btnQuickReset = win.findChild<QPushButton *>(QStringLiteral("btnQuickReset"));
        auto *tableRecent = win.findChild<QTableWidget *>(QStringLiteral("tableRecentEvents"));
        auto *tableHistory = win.findChild<QTableWidget *>(QStringLiteral("tableHistory"));
        auto *canvas = win.findChild<VisionCanvasWidget *>(QStringLiteral("canvasVision"));
        auto *chart = win.findChild<ConfidenceChartWidget *>(QStringLiteral("chartConfidence"));
        auto *slotsWidget = win.findChild<WindowSlotsWidget *>(QStringLiteral("widgetWindowSlots"));

        QVERIFY(chkSound && tabMain && lblSysBadge && lblAlarmCount && lblBeepPill && lblBboxInfo);
        QVERIFY(btnSimT3 && btnSimStop && spinSimFps && comboSimProto);
        QVERIFY(btnQuickMute && btnQuickReset && tableRecent && tableHistory);
        QVERIFY(canvas && chart && slotsWidget);

        chkSound->setChecked(false);
        QVERIFY(lblSysBadge->text().contains(QStringLiteral("IDLE")));

        tabMain->setCurrentIndex(2);
        comboSimProto->setCurrentIndex(1); // Robust
        spinSimFps->setValue(30);
        btnSimT3->click();
        tabMain->setCurrentIndex(0);

        QTRY_VERIFY_WITH_TIMEOUT(lblSysBadge->text().contains(QStringLiteral("ALARM")), 3000);
        btnSimStop->click();

        QCOMPARE(lblAlarmCount->text(), QStringLiteral("1 次"));
        QCOMPARE(lblBeepPill->text(), QStringLiteral("蜂鸣器: 报警鸣响"));
        QVERIFY(lblBboxInfo->text().contains(QStringLiteral("Robust")));
        QVERIFY(tableRecent->rowCount() >= 1);
        QCOMPARE(tableRecent->item(0, 2)->text(), QStringLiteral("跌倒报警"));

        // 绘制更新后的控件
        QVERIFY(!canvas->grab().isNull());
        QVERIFY(!chart->grab().isNull());
        QVERIFY(!slotsWidget->grab().isNull());

        btnQuickMute->click();
        QCOMPARE(lblBeepPill->text(), QStringLiteral("蜂鸣器: 静音"));
        QVERIFY(lblSysBadge->text().contains(QStringLiteral("ALARM")));

        btnQuickReset->click();
        QVERIFY(lblSysBadge->text().contains(QStringLiteral("IDLE")));
        QCOMPARE(tableRecent->item(0, 2)->text(), QStringLiteral("系统复位 (RESET)"));
        QVERIFY(tableHistory->rowCount() >= 3);

        QVERIFY(win.close());
    }

    void testSideLyingStrategyAndManualInjection()
    {
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).removeRecursively();

        falling win;
        win.show();

        auto *chkSound = win.findChild<QCheckBox *>(QStringLiteral("chkSoundAlert"));
        auto *chkSideLying = win.findChild<QCheckBox *>(QStringLiteral("chkSideLyingAsFall"));
        auto *spinWinLen = win.findChild<QSpinBox *>(QStringLiteral("spinWinLen"));
        auto *spinHitTh = win.findChild<QSpinBox *>(QStringLiteral("spinHitTh"));
        auto *btnApply = win.findChild<QPushButton *>(QStringLiteral("btnApplyParams"));
        auto *btnDefault = win.findChild<QPushButton *>(QStringLiteral("btnDefaultParams"));
        auto *spinX = win.findChild<QSpinBox *>(QStringLiteral("spinManX"));
        auto *spinY = win.findChild<QSpinBox *>(QStringLiteral("spinManY"));
        auto *spinW = win.findChild<QSpinBox *>(QStringLiteral("spinManW"));
        auto *spinH = win.findChild<QSpinBox *>(QStringLiteral("spinManH"));
        auto *dspinScore = win.findChild<QDoubleSpinBox *>(QStringLiteral("dspinManScore"));
        auto *comboState = win.findChild<QComboBox *>(QStringLiteral("comboManState"));
        auto *btnInject = win.findChild<QPushButton *>(QStringLiteral("btnInjectFrame"));
        auto *lblSysBadge = win.findChild<QLabel *>(QStringLiteral("lblSysBadge"));
        auto *lblPoseValue = win.findChild<QLabel *>(QStringLiteral("lblPoseValue"));
        auto *progressConf = win.findChild<QProgressBar *>(QStringLiteral("progressConfidence"));

        QVERIFY(chkSound && chkSideLying && spinWinLen && spinHitTh && btnApply && btnDefault);
        QVERIFY(spinX && spinY && spinW && spinH && dspinScore && comboState && btnInject);
        QVERIFY(lblSysBadge && lblPoseValue && progressConf);

        chkSound->setChecked(false);
        chkSideLying->setChecked(true);
        spinWinLen->setValue(4);
        spinHitTh->setValue(3);
        btnApply->click();

        spinX->setValue(180);
        spinY->setValue(290);
        spinW->setValue(240);
        spinH->setValue(110);
        dspinScore->setValue(0.84);
        comboState->setCurrentIndex(2); // SideLying (仅Robust)

        for (int i = 0; i < 3; ++i) {
            btnInject->click();
            QApplication::processEvents();
        }

        QVERIFY(lblSysBadge->text().contains(QStringLiteral("ALARM")));
        QVERIFY(lblPoseValue->text().contains(QStringLiteral("侧卧")));
        QCOMPARE(progressConf->value(), 84);

        btnDefault->click();
        QCOMPARE(spinWinLen->value(), 10);
        QCOMPARE(spinHitTh->value(), 6);
        QCOMPARE(chkSideLying->isChecked(), false);

        QVERIFY(win.close());
    }
};

QTEST_MAIN(QtFallUiTests)
#include "tst_ui.moc"
