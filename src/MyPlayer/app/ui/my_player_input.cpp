
#include "my_player.h"

#include "../../ui/chrome/player_chrome_controller.h"

#include "../controller/playback/playback_controller.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMouseEvent>

void MyPlayer::mouseMoveEvent(QMouseEvent* e)
{

    const bool forward = !playerChromeController || playerChromeController->HandleMouseMove(e);

    if (forward)
        QWidget::mouseMoveEvent(e);
}

void MyPlayer::mousePressEvent(QMouseEvent* e)
{

    const bool forward = !playerChromeController || playerChromeController->HandleMousePress(e);

    if (forward)
        QWidget::mousePressEvent(e);
}

bool MyPlayer::eventFilter(QObject* obj, QEvent* e)
{

    if (playerChromeController && playerChromeController->HandleEventFilter(obj, e))
        return true;

    return QWidget::eventFilter(obj, e);
}

void MyPlayer::SliderPress()
{

    if (playbackController)
        playbackController->SliderPress();
}

void MyPlayer::SliderRelease()
{

    if (playbackController)
        playbackController->SliderRelease();
}

void MyPlayer::keyPressEvent(QKeyEvent* e)
{

    if (playerChromeController && playerChromeController->HandleKeyPress(e))
        return;

    QWidget::keyPressEvent(e);
}

void MyPlayer::mouseDoubleClickEvent(QMouseEvent* e)
{
    Q_UNUSED(e);

    if (playerChromeController)
        playerChromeController->HandleMouseDoubleClick();
}

void MyPlayer::mouseReleaseEvent(QMouseEvent* e)
{

    if (playerChromeController)
        playerChromeController->HandleMouseRelease();

    QWidget::mouseReleaseEvent(e);
}

void MyPlayer::dragEnterEvent(QDragEnterEvent* e)
{

    if (playerChromeController && playerChromeController->HandleDragEnter(e))
        return;

    QWidget::dragEnterEvent(e);
}

void MyPlayer::dropEvent(QDropEvent* e)
{

    if (playerChromeController && playerChromeController->HandleDrop(e))
        return;

    QWidget::dropEvent(e);
}
