#pragma once

#include <QColor>
#include <QQuickItem>
#include <QVariantList>

// GPU-rendered sparkline: draws one series as an area fill + polyline directly in
// the scene graph (QSGGeometry), so per-tick updates cost ~no CPU and no texture
// upload — unlike Canvas. Stack two for multi-series graphs (e.g. down/up).
class Sparkline : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QVariantList values READ values WRITE setValues NOTIFY changed)
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY changed)
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor NOTIFY changed)
    Q_PROPERTY(qreal lineWidth READ lineWidth WRITE setLineWidth NOTIFY changed)
    // 0 = auto-scale to the series' own maximum.
    Q_PROPERTY(qreal maxValue READ maxValue WRITE setMaxValue NOTIFY changed)

public:
    explicit Sparkline(QQuickItem *parent = nullptr);

    QVariantList values() const { return m_values; }
    void setValues(const QVariantList &values);
    QColor lineColor() const { return m_lineColor; }
    void setLineColor(const QColor &color);
    QColor fillColor() const { return m_fillColor; }
    void setFillColor(const QColor &color);
    qreal lineWidth() const { return m_lineWidth; }
    void setLineWidth(qreal width);
    qreal maxValue() const { return m_maxValue; }
    void setMaxValue(qreal value);

signals:
    void changed();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    QVariantList m_values;
    QColor m_lineColor = Qt::white;
    QColor m_fillColor = Qt::transparent;
    qreal m_lineWidth = 1.5;
    qreal m_maxValue = 0.0;
};
