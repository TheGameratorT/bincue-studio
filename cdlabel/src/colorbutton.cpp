#include "colorbutton.h"

#include <QColorDialog>

ColorButton::ColorButton(const QString &color, QWidget *parent)
    : QPushButton(parent), m_color(color)
{
    setFixedSize(48, 22);
    apply();
    connect(this, &QPushButton::clicked, this, &ColorButton::pick);
}

void ColorButton::apply()
{
    setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #888;")
                      .arg(m_color));
}

void ColorButton::setColor(const QString &color)
{
    if (!color.isEmpty() && color != m_color) {
        m_color = color;
        apply();
    }
}

void ColorButton::pick()
{
    const QColor chosen =
        QColorDialog::getColor(QColor(m_color), this, tr("Pick colour"));
    if (chosen.isValid()) {
        m_color = chosen.name();
        apply();
        emit colorChanged(m_color);
    }
}
