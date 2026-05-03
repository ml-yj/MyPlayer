#pragma once

class QApplication;

class ApplicationBootstrap
{
public:

    explicit ApplicationBootstrap(QApplication& app);

    int Run(int argc, char* argv[]);

private:

    void RegisterMetaTypes() const;

    QApplication& app_;
};
