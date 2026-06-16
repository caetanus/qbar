#include "sparkline.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGNode>
#include <algorithm>

Sparkline::Sparkline(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

void Sparkline::setValues(const QVariantList &values)
{
    m_values = values;
    emit changed();
    update();
}

void Sparkline::setLineColor(const QColor &color)
{
    if (m_lineColor == color) {
        return;
    }
    m_lineColor = color;
    emit changed();
    update();
}

void Sparkline::setFillColor(const QColor &color)
{
    if (m_fillColor == color) {
        return;
    }
    m_fillColor = color;
    emit changed();
    update();
}

void Sparkline::setLineWidth(qreal width)
{
    if (qFuzzyCompare(m_lineWidth, width)) {
        return;
    }
    m_lineWidth = width;
    emit changed();
    update();
}

void Sparkline::setMaxValue(qreal value)
{
    if (qFuzzyCompare(m_maxValue, value)) {
        return;
    }
    m_maxValue = value;
    emit changed();
    update();
}

void Sparkline::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        update();
    }
}

QSGNode *Sparkline::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    delete oldNode;

    const int count = m_values.size();
    const float w = static_cast<float>(width());
    const float h = static_cast<float>(height());
    if (count < 1 || w <= 0.0f || h <= 0.0f) {
        return nullptr;
    }

    double maxValue = m_maxValue;
    if (maxValue <= 0.0) {
        for (const QVariant &value : m_values) {
            maxValue = std::max(maxValue, value.toDouble());
        }
    }
    if (maxValue <= 0.0) {
        maxValue = 1.0;
    }

    const auto pointAt = [&](int i) -> QPointF {
        const float x = count == 1 ? w : (static_cast<float>(i) * w) / static_cast<float>(count - 1);
        const double normalized = std::clamp(m_values.at(i).toDouble() / maxValue, 0.0, 1.0);
        const float y = h - static_cast<float>(normalized) * h;
        return {x, y};
    };

    auto *root = new QSGNode;

    if (m_fillColor.alpha() > 0) {
        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), count * 2);
        geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        QSGGeometry::Point2D *vertices = geometry->vertexDataAsPoint2D();
        for (int i = 0; i < count; ++i) {
            const QPointF p = pointAt(i);
            vertices[i * 2].set(static_cast<float>(p.x()), h);
            vertices[i * 2 + 1].set(static_cast<float>(p.x()), static_cast<float>(p.y()));
        }
        auto *material = new QSGFlatColorMaterial;
        material->setColor(m_fillColor);
        auto *node = new QSGGeometryNode;
        node->setGeometry(geometry);
        node->setMaterial(material);
        node->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
        root->appendChildNode(node);
    }

    if (m_lineColor.alpha() > 0 && count >= 2) {
        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), count);
        geometry->setDrawingMode(QSGGeometry::DrawLineStrip);
        geometry->setLineWidth(static_cast<float>(m_lineWidth));
        QSGGeometry::Point2D *vertices = geometry->vertexDataAsPoint2D();
        for (int i = 0; i < count; ++i) {
            const QPointF p = pointAt(i);
            vertices[i].set(static_cast<float>(p.x()), static_cast<float>(p.y()));
        }
        auto *material = new QSGFlatColorMaterial;
        material->setColor(m_lineColor);
        auto *node = new QSGGeometryNode;
        node->setGeometry(geometry);
        node->setMaterial(material);
        node->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
        root->appendChildNode(node);
    }

    return root;
}
