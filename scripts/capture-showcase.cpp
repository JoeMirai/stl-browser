// Capture real application windows; no generated or composited UI.
#include "window.h"
#include <QtTest>
#include <QSettings>
#include <QDir>
#include <QToolBar>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QApplication>

int main(int argc, char** argv) {
    QApplication app(argc,argv);
    if(argc!=3) { qWarning("Usage: capture-showcase MODEL_FOLDER OUTPUT_FOLDER"); return 2; }
    QCoreApplication::setOrganizationName("STLBrowserShowcase");
    QCoreApplication::setApplicationName("capture");
    QSettings settings; settings.clear(); settings.setValue("thumbnailSize",160);
    settings.setValue("drawMode",int(solidwireframe));
    QDir output(argv[2]); output.mkpath(".");
    Window window; window.resize(1200,820); window.show();
    if(!QTest::qWaitForWindowExposed(&window)) return 3;
    auto* strip=window.findChild<QListWidget*>("thumbnailStrip");
    auto* viewer=window.findChild<Canvas*>("viewer");
    auto* info=window.findChild<QLabel*>("fileInfo");
    auto load=[&](const QString& name) {
        if(!window.load_stl(QDir(argv[1]).filePath(name))) return false;
        for(int attempts=0;attempts<400;++attempts) {
            QTest::qWait(50);
            bool ready=info->text().contains("triangles");
            for(int i=0;i<strip->count();++i) ready &= !strip->item(i)->data(Qt::UserRole).toString().isEmpty();
            if(ready) return true;
        }
        return false;
    };
    for (const auto& name : {QString("3DBenchy.stl"),QString("Stanford Bunny.stl"),QString("Utah Teapot.stl")}) {
        if(!load(name)) return 4;
        viewer->common_view_change(isoview); viewer->common_view_change(centerview);
        // Zoom through the actual wheel interaction, keeping the model centered.
        QPoint center=viewer->rect().center();
        QWheelEvent zoom(center, viewer->mapToGlobal(center), QPoint(), QPoint(0,-220),
                         Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(viewer, &zoom);
        QTest::qWait(100);
        QString slug = name.startsWith("3D") ? "benchy" : name.startsWith("Stanford") ? "bunny" : "teapot";
        if(!window.grab().save(output.filePath(slug+"-solid-wireframe.png"))) return 5;
        if(slug=="benchy") {
            // Capture the angle-color mode with the same camera for comparison.
            for(auto* action:window.findChild<QToolBar*>()->actions()) if(action->text()=="Settings…") action->trigger();
            auto* dialog=window.findChild<QDialog*>("settingsDialog");
            auto* shading=dialog->findChildren<QComboBox*>().first();
            shading->setCurrentIndex(int(surfaceangle));
            dialog->close(); QTest::qWait(800);
            if(!window.grab().save(output.filePath("benchy-surface-angle.png"))) return 6;
            QTest::qWait(50);
            for(auto* action:window.findChild<QToolBar*>()->actions()) if(action->text()=="Settings…") action->trigger();
            dialog=window.findChild<QDialog*>("settingsDialog");
            shading=dialog->findChildren<QComboBox*>().first(); shading->setCurrentIndex(int(solidwireframe));
            QTest::qWait(800);
            if(!window.grab().save(output.filePath("settings.png"))) return 7;
            // Separate native settings window, with the explanatory tooltip visible.
            dialog->resize(590,dialog->height());
            QTest::qWait(100);
            if(!dialog->grab().save(output.filePath("settings.png"))) return 7;
            dialog->close(); QTest::qWait(100);
        }
    }
    settings.clear();
    return 0;
}
