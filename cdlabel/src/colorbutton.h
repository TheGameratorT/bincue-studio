// A button that shows its current colour and opens a colour picker.
#pragma once

#include <QPushButton>

class ColorButton : public QPushButton {
    Q_OBJECT
public:
    explicit ColorButton(const QString &color = QStringLiteral("#000000"),
                         QWidget *parent = nullptr);

    QString color() const { return m_color; }
    void setColor(const QString &color);

signals:
    void colorChanged(const QString &color);

private:
    void apply();
    void pick();

    QString m_color;
};
