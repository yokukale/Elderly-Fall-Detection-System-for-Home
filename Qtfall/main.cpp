#include "falling.h"

#include <QApplication>
#include <QFont>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setApplicationName(QStringLiteral("Qtfall"));
    a.setOrganizationName(QStringLiteral("FallGuard"));
    a.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QFont f(QStringLiteral("Microsoft YaHei UI"), 9);
    a.setFont(f);

    falling w;
    w.show();
    return QApplication::exec();
}
