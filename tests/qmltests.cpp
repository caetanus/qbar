#include "qmltests.h"

#include "src/customtool/customtoolmodel.h"

#include <algorithm>
#include <QPainter>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QtQml/qqml.h>
#include <QtQuickTest>

class TestIconProvider final : public QQuickImageProvider {
public:
    TestIconProvider()
        : QQuickImageProvider(QQuickImageProvider::Pixmap)
    {
    }

    QPixmap requestPixmap(const QString &, QSize *size, const QSize &requestedSize) override
    {
        const int side = std::max(1, requestedSize.width() > 0 ? requestedSize.width() : 24);
        QPixmap pixmap(side, side);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setBrush(QColor(QStringLiteral("#ffffff")));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QRectF(2, 2, side - 4, side - 4));
        painter.end();

        if (size != nullptr) {
            *size = pixmap.size();
        }
        return pixmap;
    }
};

QBarQuickTestSetup::QBarQuickTestSetup()
{
    qmlRegisterType<CustomToolModel>("QBar", 1, 0, "CustomToolModel");
}

void QBarQuickTestSetup::qmlEngineAvailable(QQmlEngine *engine)
{
    engine->addImageProvider(QStringLiteral("themeicon"), new TestIconProvider);
}

QUICK_TEST_MAIN_WITH_SETUP(qbarqml, QBarQuickTestSetup)
