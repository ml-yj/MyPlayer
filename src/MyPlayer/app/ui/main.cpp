

#include "../bootstrap/application_bootstrap.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    ApplicationBootstrap bootstrap(app);
    return bootstrap.Run(argc, argv);
}
