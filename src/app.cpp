#include <QDebug>
#include <QDir>
#include <QFileOpenEvent>

#include "app.h"
#include "window.h"

App::App(int& argc, char* argv[]) : QApplication(argc, argv), window(new Window())
{
    connect(window, &Window::closed, this, &QCoreApplication::quit);
    if (argc > 1) {
        const auto args = QCoreApplication::arguments();
        QString filename = args.at(1);
        if (filename.startsWith("~")) {
            filename.replace(0, 1, QDir::homePath());
        }
        window->load_stl(filename);
    } else {
        // Start empty until a file or folder is opened.
    }
    window->show();
}

App::~App()
{
    delete window;
}

bool App::event(QEvent* e)
{
    if (e->type() == QEvent::FileOpen) {
        window->load_stl(static_cast<QFileOpenEvent*>(e)->file());
        return true;
    } else {
        return QApplication::event(e);
    }
}
