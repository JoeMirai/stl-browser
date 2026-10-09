#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDataStream>
#include <QSignalSpy>
#include <QComboBox>
#include <QPushButton>
#include <QToolBar>
#include <QSettings>
#include "loader.h"
#include "window.h"

class BrowserTests : public QObject {
    Q_OBJECT
    QByteArray ascii = "solid test\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\nendsolid test\n";
    void write(const QString& path, const QByteArray& bytes) { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(bytes), qint64(bytes.size())); }
    void checkLoader(const QString& path, bool valid, bool empty = false) {
        Loader loader(nullptr,path,false);
        int triangles = -1;
        connect(&loader,&Loader::got_mesh,this,[&](Mesh* m,bool) { triangles = m->triCount(); delete m; });
        QSignalSpy bad(&loader,&Loader::error_bad_stl);
        QSignalSpy noTriangles(&loader,&Loader::error_empty_mesh);
        loader.start(); QVERIFY(loader.wait(5000));
        QCoreApplication::processEvents();
        if (empty) QCOMPARE(noTriangles.count(),1);
        else if (valid) QTRY_COMPARE_WITH_TIMEOUT(triangles,1,1000);
        else QCOMPARE(bad.count(),1);
    }
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("STLBrowserTests");
        QCoreApplication::setApplicationName("isolated");
        QSettings().clear();
    }
    void asciiAndMalformed() {
        QTemporaryDir d;
        write(d.filePath("valid.stl"),ascii); checkLoader(d.filePath("valid.stl"),true);
        write(d.filePath("bad.stl"),"solid bad\nfacet normal 0 0 1\nouter loop\nvertex\n"); checkLoader(d.filePath("bad.stl"),false);
        write(d.filePath("bad.stl"),QByteArray(ascii).replace("1 0 0","1 junk 0")); checkLoader(d.filePath("bad.stl"),false);
        write(d.filePath("bad.stl"),"x"); checkLoader(d.filePath("bad.stl"),false);
        write(d.filePath("empty.stl"),"solid empty\nendsolid empty\n"); checkLoader(d.filePath("empty.stl"),false,true);
    }
    void binary() {
        QTemporaryDir d;
        QByteArray bytes(80,0); bytes.replace(0,5,"solid");
        QDataStream stream(&bytes,QIODevice::Append); stream.setByteOrder(QDataStream::LittleEndian); stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
        stream << quint32(1);
        for (float f : {0.f,0.f,1.f,0.f,0.f,0.f,1.f,0.f,0.f,0.f,1.f,0.f}) stream << f;
        stream << quint16(0);
        write(d.filePath("binary.stl"),bytes); checkLoader(d.filePath("binary.stl"),true);
        bytes.chop(1); write(d.filePath("binary.stl"),bytes); checkLoader(d.filePath("binary.stl"),false);
        QByteArray overflow(84,0); overflow[80]=char(0xff); overflow[81]=char(0xff); overflow[82]=char(0xff); overflow[83]=char(0xff);
        write(d.filePath("overflow.stl"),overflow); checkLoader(d.filePath("overflow.stl"),false);
    }
    void naturalSortAndFiltering() {
        QTemporaryDir d;
        for (const char* n : {"part10.stl","part2.STL","part1.stl","ignored.3mf"}) write(d.filePath(n),ascii);
        QDir().mkdir(d.filePath("nested")); write(d.filePath("nested/hidden.stl"),ascii);
        QStringList paths = Window::filesInFolder(d.path());
        QCOMPARE(paths.size(),3);
        QCOMPARE(QFileInfo(paths[0]).fileName(),QString("part1.stl"));
        QCOMPARE(QFileInfo(paths[1]).fileName(),QString("part2.STL"));
        QCOMPARE(QFileInfo(paths[2]).fileName(),QString("part10.stl"));
    }
    void cacheInvalidation() {
        QTemporaryDir d; QString file=d.filePath("a.stl"); write(file,ascii);
        QString before=Window::thumbnailKey(file,"classic");
        QVERIFY(before!=Window::thumbnailKey(file,"wireframe"));
        write(file,ascii+"\n"); QVERIFY(before!=Window::thumbnailKey(file,"classic"));
    }
    void navigationAndRapidSelection() {
        QTemporaryDir d;
        for (int i=0;i<5;++i) write(d.filePath(QString("part%1.stl").arg(i)),ascii);
        Window w; w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.load_stl(d.path()));
        auto* strip=w.findChild<QListWidget*>("thumbnailStrip");
        QCOMPARE(strip->count(),5); QCOMPARE(strip->currentRow(),0); QVERIFY(!w.load_prev());
        for (int i=0;i<4;++i) QVERIFY(w.load_next());
        QCOMPARE(strip->currentRow(),4); QVERIFY(!w.load_next());
        QTRY_VERIFY_WITH_TIMEOUT(w.findChild<QLabel*>("fileInfo")->text().contains("1 triangles"),10000);
        QVERIFY(w.windowTitle().contains("part4.stl"));
        for (int i=0;i<4;++i) QVERIFY(w.load_prev());
        QTRY_VERIFY_WITH_TIMEOUT(w.findChild<QLabel*>("fileInfo")->text().contains("1 triangles"),10000);
        QTRY_VERIFY_WITH_TIMEOUT(!strip->item(0)->data(Qt::UserRole).toString().isEmpty(),10000);
        auto* viewer=w.findChild<Canvas*>("viewer");
        viewer->set_drawMode(shaded); QTest::qWait(50);
        QImage solid=viewer->grabFramebuffer();
        viewer->set_drawMode(solidwireframe); QTest::qWait(50);
        QImage edges=viewer->grabFramebuffer();
        QVERIFY(!solid.isNull() && !edges.isNull());
        QVERIFY(solid != edges);
        QSignalSpy closed(&w, &Window::closed);
        w.close();
        QCOMPARE(closed.count(),1);
        QVERIFY(!w.isVisible());
    }
    void settingsTooltipsAndPersistence() {
        Window w; w.show();
        for (QAction* a : w.findChild<QToolBar*>()->actions()) if (a->text()=="Settings…") a->trigger();
        auto* dialog=w.findChild<QDialog*>("settingsDialog"); QVERIFY(dialog);
        auto combos=dialog->findChildren<QComboBox*>(); QVERIFY(!combos.isEmpty());
        for (QWidget* field : dialog->findChildren<QWidget*>()) {
            if (qobject_cast<QComboBox*>(field) || qobject_cast<QCheckBox*>(field) || qobject_cast<QAbstractSpinBox*>(field) || qobject_cast<QPushButton*>(field)) QVERIFY2(!field->toolTip().isEmpty(),qPrintable(field->metaObject()->className()));
        }
        QCOMPARE(combos.first()->count(),int(DRAWMODECOUNT));
        combos.first()->setCurrentIndex(3); QCOMPARE(QSettings().value("drawMode").toInt(),3);
        dialog->close(); QCoreApplication::processEvents();
        Window another; QCOMPARE(QSettings().value("drawMode").toInt(),3);
    }
    void emptyAndBadFileKeepNavigation() {
        QTemporaryDir d; Window w; w.show(); QVERIFY(!w.load_stl(d.path()));
        write(d.filePath("a.stl"),"broken"); write(d.filePath("b.stl"),ascii);
        QVERIFY(w.load_stl(d.path()));
        QTest::qWait(300); QVERIFY(w.load_next());
        QTRY_VERIFY_WITH_TIMEOUT(w.findChild<QLabel*>("fileInfo")->text().contains("1 triangles"),10000);
    }
    void largeFolderLoadsOnlyVisibleThumbnails() {
        QTemporaryDir d;
        for(int i=0;i<1000;++i) write(d.filePath(QString("part%1.stl").arg(i)),ascii);
        Window w; w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w)); QVERIFY(w.load_stl(d.path()));
        auto* strip=w.findChild<QListWidget*>("thumbnailStrip"); QCOMPARE(strip->count(),1000);
        QTRY_VERIFY_WITH_TIMEOUT(!strip->item(0)->data(Qt::UserRole).toString().isEmpty(),10000);
        QTest::qWait(700);
        int rendered=0;
        for(int i=0;i<strip->count();++i) rendered+=!strip->item(i)->data(Qt::UserRole).toString().isEmpty();
        QVERIFY(rendered>0); QVERIFY(rendered<20);
        strip->setCurrentRow(999);
        QTRY_VERIFY_WITH_TIMEOUT(w.findChild<QLabel*>("fileInfo")->text().contains("1 triangles"),10000);
        QTRY_VERIFY_WITH_TIMEOUT(!strip->item(999)->data(Qt::UserRole).toString().isEmpty(),10000);
        QTest::qWait(500);
        QVERIFY(strip->item(0)->data(Qt::UserRole).toString().isEmpty());
    }
    void localSonicPreview() {
        QString folder = qEnvironmentVariable("STL_BROWSER_SAMPLE_DIR");
        if (folder.isEmpty()) QSKIP("Optional local fixture folder not specified");
        QSettings().clear();
        Window w; w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.load_stl(folder));
        auto* strip = w.findChild<QListWidget*>("thumbnailStrip");
        int head = -1;
        for(int i=0;i<strip->count();++i) if(strip->item(i)->text().contains("HEAD")) head=i;
        if(head>=0) strip->setCurrentRow(head);
        QTRY_VERIFY_WITH_TIMEOUT(w.findChild<QLabel*>("fileInfo")->text().contains("triangles"),15000);
        QTRY_VERIFY_WITH_TIMEOUT(!strip->currentItem()->data(Qt::UserRole).toString().isEmpty(),15000);
        QTest::qWait(1000);
        QVERIFY(w.grab().save("/tmp/stl-browser-preview.png"));
        auto* viewer=w.findChild<Canvas*>("viewer");
        QVERIFY(viewer && viewer->isValid());
        for(int mode=0;mode<DRAWMODECOUNT;++mode) {
            viewer->set_drawMode(static_cast<DrawMode>(mode));
            QTest::qWait(50);
            QImage image=viewer->grabFramebuffer(); QVERIFY(!image.isNull());
            QVERIFY(image.save(QString("/tmp/stl-browser-shading-%1.png").arg(mode)));
        }
    }
    void cleanupTestCase() { QSettings().clear(); }
};
QTEST_MAIN(BrowserTests)
#include "browser-tests.moc"
