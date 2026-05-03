#pragma once

#include <QMouseEvent>
#include <QSlider>

class Slider : public QSlider
{
    Q_OBJECT

public:
    explicit Slider(QWidget* parent = nullptr) : QSlider(parent) {}
    ~Slider() override = default;

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        updateValueFromMouse(e);
        e->accept();
        emit sliderPressed();
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (e->buttons() & Qt::LeftButton)
            updateValueFromMouse(e);
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        updateValueFromMouse(e);
        emit sliderReleased();
    }

private:
    void updateValueFromMouse(QMouseEvent* e)
    {
        if (!e || width() <= 0)
            return;

        double pos = static_cast<double>(e->pos().x()) / static_cast<double>(width());
        if (pos < 0.0)
            pos = 0.0;
        if (pos > 1.0)
            pos = 1.0;
        setValue(static_cast<int>(pos * maximum()));
    }
};
