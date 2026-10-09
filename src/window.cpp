#include "window.h"
#include <QToolBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QSettings>
#include <QCollator>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QScrollBar>
#include <QStandardPaths>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QDialog>
#include <QFormLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QColorDialog>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QSignalBlocker>
#include <QSaveFile>
#include <algorithm>
#include <functional>
#include <QCloseEvent>

namespace {
QColor settingColor(const char* key, const char* fallback) {
    QColor color(QSettings().value(key, fallback).toString());
    return color.isValid() ? color : QColor(fallback);
}
}
Window::Window(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("STL Browser");
    setWindowIcon(QIcon(":/qt/icons/fstl_64x64.png"));
    setAcceptDrops(true);
    QSurfaceFormat format;
    format.setVersion(2, 1);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    canvas = new Canvas(format, this);
    canvas->setObjectName("viewer");
    canvas->setFocusPolicy(Qt::StrongFocus);
    canvas->setToolTip("Left drag: rotate · Right drag: pan · Wheel: zoom · ← / →: previous / next STL");
    thumbnailCanvas = new Canvas(format);
    thumbnailCanvas->setAttribute(Qt::WA_DontShowOnScreen);
    thumbnailCanvas->setAttribute(Qt::WA_QuitOnClose, false);
    thumbnailCanvas->resize(192, 192);
    buildUi();
    applySettings();
    thumbnailTimer.setSingleShot(true);
    connect(&thumbnailTimer, &QTimer::timeout, this, &Window::nextThumbnail);
    connect(&watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        QString old = requestedFile;
        rebuildStrip(directory);
        int row = files.indexOf(old);
        if (row < 0 && !files.isEmpty()) row = 0;
        if (row >= 0) selectFile(row);
        else { requestedFile.clear(); ++selectionGeneration; if (selectedLoader) selectedLoader->requestInterruption(); canvas->clearMesh(); showError("No readable STL files in this folder"); updateNavigation(); }
    });
    resize(1000, 720);
    restoreGeometry(QSettings().value("windowGeometry").toByteArray());
    canvas->set_status("Open an STL file or folder to browse models");
}
Window::~Window() {
    closing = true;
    thumbnailTimer.stop();
    for (Loader* loader : {selectedLoader, thumbnailLoader}) {
        if (loader) { loader->requestInterruption(); loader->wait(); }
    }
    // Drain queued mesh ownership transfers after workers have stopped.
    QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
    delete thumbnailCanvas;
}
QStringList Window::filesInFolder(const QString& folder) {
    QStringList result;
    QDirIterator it(folder, QDir::Files | QDir::Readable | QDir::Hidden);
    while (it.hasNext()) {
        it.next();
        if (it.fileInfo().suffix().compare("stl", Qt::CaseInsensitive) == 0)
            result << it.fileInfo().absoluteFilePath();
    }
    // The C locale disables numeric collation on some Qt builds.
    QCollator collator(QLocale(QLocale::English, QLocale::UnitedStates));
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(result.begin(), result.end(), [&](const QString& a, const QString& b) {
        int cmp = collator.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName());
        return cmp == 0 ? a < b : cmp < 0;
    });
    return result;
}
QString Window::thumbnailKey(const QString& path, const QByteArray& appearance) {
    QFileInfo f(path);
    QByteArray key = f.absoluteFilePath().toUtf8() + '\0' + QByteArray::number(f.size())
        + ':' + QByteArray::number(f.lastModified().toMSecsSinceEpoch()) + ':' + appearance;
    return QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex());
}
void Window::buildUi() {
    auto* toolbar = addToolBar("Browse");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto add = [&](const QString& text, const QString& tip, std::function<void()> fn) {
        QAction* action = toolbar->addAction(text);
        action->setToolTip(tip);
        connect(action, &QAction::triggered, this, fn);
        return action;
    };
    add("Open STL…", "Choose an STL file and browse the other STL files in its folder.", [this] {
        QString path = QFileDialog::getOpenFileName(this, "Open STL", directory, "STL models (*.stl *.STL);;All files (*)");
        if (!path.isEmpty()) load_stl(path);
    })->setShortcut(QKeySequence::Open);
    add("Open folder…", "Browse STL files directly inside a folder. Subfolders are not scanned.", [this] {
        QString path = QFileDialog::getExistingDirectory(this, "Open folder", directory);
        if (!path.isEmpty()) load_stl(path);
    });
    toolbar->addSeparator();
    previous = add("← Previous", "Open the previous STL in filename order. Shortcut: Left arrow.", [this] { load_prev(); });
    next = add("Next →", "Open the next STL in filename order. Shortcut: Right arrow.", [this] { load_next(); });
    previous->setShortcut(Qt::Key_Left); next->setShortcut(Qt::Key_Right);
    previous->setShortcutContext(Qt::WindowShortcut); next->setShortcutContext(Qt::WindowShortcut);
    add("Reset view", "Fit the model and return to the starting camera angle. Shortcut: Home.", [this] { canvas->common_view_change(isoview); canvas->common_view_change(centerview); })->setShortcut(Qt::Key_Home);
    add("Angle view", "Inspect the model from an isometric angle. Shortcut: I.", [this] { canvas->common_view_change(isoview); canvas->common_view_change(centerview); })->setShortcut(Qt::Key_I);
    toolbar->addSeparator();
    add("Settings…", "Change shading, colors, lighting, camera behavior, and thumbnail size.", [this] { showSettings(); });
    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    layout->addWidget(canvas, 1);
    strip = new QListWidget;
    strip->setObjectName("thumbnailStrip");
    strip->setViewMode(QListView::IconMode);
    strip->setFlow(QListView::LeftToRight);
    strip->setWrapping(false);
    strip->setMovement(QListView::Static);
    strip->setResizeMode(QListView::Adjust);
    strip->setSelectionMode(QAbstractItemView::SingleSelection);
    strip->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    strip->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    strip->setSpacing(6);
    strip->setFocusPolicy(Qt::NoFocus);
    strip->setStyleSheet("QListWidget { background: #20272d; color: #e6edf3; border: 0; border-top: 1px solid #44505a; } QListWidget::item { border-radius: 6px; padding: 4px; } QListWidget::item:selected { background: #285d72; border: 1px solid #70c7e7; }");
    strip->setToolTip("Click a thumbnail to open its STL. Scroll horizontally to browse more models.");
    layout->addWidget(strip);
    connect(strip, &QListWidget::currentRowChanged, this, &Window::selectFile);
    connect(strip->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { scheduleThumbnail(); });
    setCentralWidget(central);
    info = new QLabel("No folder open"); info->setObjectName("fileInfo");
    statusBar()->addWidget(info, 1);
    updateNavigation();
}
bool Window::load_stl(const QString& input, bool) {
    if (input.isEmpty()) return false;
    QFileInfo f(input);
    QString folder = f.isDir() ? f.absoluteFilePath() : f.absolutePath();
    rebuildStrip(folder);
    if (files.isEmpty()) {
        requestedFile.clear(); ++selectionGeneration;
        if (selectedLoader) selectedLoader->requestInterruption();
        canvas->clearMesh();
        showError("No readable STL files in this folder"); updateNavigation(); return false;
    }
    int row = f.isDir() ? 0 : files.indexOf(f.absoluteFilePath());
    if (row < 0) { showError("Choose a readable .stl file or a folder"); return false; }
    selectFile(row);
    return true;
}
void Window::rebuildStrip(const QString& folder) {
    QStringList updated = filesInFolder(folder);
    if (folder == directory && updated == files) return;
    if (!watcher.directories().isEmpty()) watcher.removePaths(watcher.directories());
    directory = folder; files = updated;
    if (QFileInfo(directory).isDir()) watcher.addPath(directory);
    QSignalBlocker block(strip);
    strip->clear(); failedThumbnails.clear();
    placeholder = QPixmap(192,192); placeholder.fill(QColor("#303b44"));
    for (const QString& path : files) {
        auto* item = new QListWidgetItem(QIcon(placeholder), QFileInfo(path).fileName(), strip);
        item->setToolTip(path + "\nClick to open this model");
        item->setTextAlignment(Qt::AlignHCenter);
    }
    scheduleThumbnail();
}
void Window::selectFile(int row) {
    if (row < 0 || row >= files.size()) return;
    requestedFile = files[row]; ++selectionGeneration;
    { QSignalBlocker block(strip); strip->setCurrentRow(row); }
    strip->scrollToItem(strip->item(row));
    updateNavigation();
    canvas->set_status("Loading " + QFileInfo(requestedFile).fileName());
    if (selectedLoader) selectedLoader->requestInterruption();
    if (thumbnailLoader) thumbnailLoader->requestInterruption();
    startSelected();
}
void Window::startSelected() {
    if (closing || selectedLoader || thumbnailLoader || requestedFile.isEmpty()) return;
    const QString path = requestedFile;
    const quint64 generation = selectionGeneration;
    auto* loader = new Loader(this, path, false);
    selectedLoader = loader;
    auto fail = [this, generation](const QString& message) {
        if (generation == selectionGeneration) { canvas->clearMesh(); showError(message); }
    };
    connect(loader, &Loader::error_bad_stl, this, [fail] { fail("Invalid or damaged STL file — use the arrows to continue"); });
    connect(loader, &Loader::error_empty_mesh, this, [fail] { fail("This STL contains no triangles — use the arrows to continue"); });
    connect(loader, &Loader::error_missing_file, this, [fail] { fail("Unable to read this file — use the arrows to continue"); });
    connect(loader, &Loader::got_mesh, this, [this, generation](Mesh* mesh, bool) {
        if (closing || generation != selectionGeneration) { delete mesh; return; }
        const int count = mesh->triCount();
        canvas->load_mesh(mesh, false);
        canvas->clear_status();
        info->setText(QString("%1 / %2  ·  %3  ·  %4 triangles").arg(strip->currentRow()+1).arg(files.size()).arg(QFileInfo(requestedFile).fileName()).arg(count));
    });
    connect(loader, &QThread::finished, this, [this, loader, generation] {
        selectedLoader = nullptr; loader->deleteLater();
        if (closing) return;
        if (generation != selectionGeneration) startSelected();
        else scheduleThumbnail();
    });
    loader->start(QThread::NormalPriority);
}
void Window::updateNavigation() {
    int row = files.indexOf(requestedFile);
    previous->setEnabled(row > 0);
    next->setEnabled(row >= 0 && row + 1 < files.size());
    info->setText(row < 0 ? QString("%1 STL files").arg(files.size()) : QString("%1 / %2  ·  %3").arg(row+1).arg(files.size()).arg(QFileInfo(requestedFile).fileName()));
    setWindowTitle(row < 0 ? "STL Browser" : QFileInfo(requestedFile).fileName() + " — STL Browser");
}
bool Window::load_prev() { int row = files.indexOf(requestedFile); if (row <= 0) return false; selectFile(row-1); return true; }
bool Window::load_next() { int row = files.indexOf(requestedFile); if (row < 0 || row+1 >= files.size()) return false; selectFile(row+1); return true; }
void Window::showError(const QString& text) { canvas->set_status(text); statusBar()->showMessage(text, 8000); }
QByteArray Window::appearanceKey() const {
    QSettings s;
    QByteArray key("renderer-v2:192");
    for (const char* name : {"drawMode", "modelColor", "backgroundColor", "classicBackground", "ambientColor", "directiveColor", "ambientFactor", "directiveFactor", "currentLightDirection", "projection"})
        key += QByteArray(name) + '=' + s.value(name).toString().toUtf8() + ';';
    return key;
}
QString Window::cachePath(const QString& key) const {
    QString folder = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbnails";
    QDir().mkpath(folder);
    return folder + '/' + key + ".png";
}
void Window::scheduleThumbnail() { if (!closing) thumbnailTimer.start(100); }
void Window::nextThumbnail() {
    if (closing || selectedLoader || thumbnailLoader || files.isEmpty()) return;
    const QByteArray appearance = appearanceKey();
    // QListWidget icons also own pixmaps; release icons outside the viewport
    // so browsing a large folder cannot bypass the bounded QCache.
    for (int row=0; row<strip->count(); ++row) {
        auto* item = strip->item(row);
        if (!strip->visualItemRect(item).intersects(strip->viewport()->rect()) && row!=strip->currentRow()) {
            if (item->data(Qt::UserRole).isValid()) {
                item->setIcon(QIcon(placeholder)); item->setData(Qt::UserRole,QVariant());
            }
        }
    }
    for (int row = 0; row < strip->count(); ++row) {
        auto* item = strip->item(row);
        if (!strip->visualItemRect(item).intersects(strip->viewport()->rect())) continue;
        const QString path = files[row];
        const QString key = thumbnailKey(path, appearance);
        if (item->data(Qt::UserRole).toString() == key || failedThumbnails.contains(key)) continue;
        if (auto* cached = thumbnails.object(key)) {
            item->setIcon(QIcon(*cached)); item->setData(Qt::UserRole, key); continue;
        }
        QPixmap cached(cachePath(key));
        if (!cached.isNull()) {
            thumbnails.insert(key, new QPixmap(cached), std::max(1, cached.width()*cached.height()*4/1024));
            item->setIcon(QIcon(cached)); item->setData(Qt::UserRole, key); continue;
        }
        const quint64 generation = selectionGeneration;
        auto* loader = new Loader(this, path, false);
        thumbnailLoader = loader;
        auto fail = [this, key] { failedThumbnails.insert(key); };
        connect(loader, &Loader::error_bad_stl, this, fail);
        connect(loader, &Loader::error_empty_mesh, this, fail);
        connect(loader, &Loader::error_missing_file, this, fail);
        connect(loader, &Loader::got_mesh, this, [this, path, key, appearance, generation](Mesh* mesh, bool) {
            if (closing || generation != selectionGeneration || appearance != appearanceKey() || !files.contains(path)) { delete mesh; return; }
            if (!thumbnailCanvas->isVisible()) thumbnailCanvas->show();
            if (!thumbnailCanvas->isValid()) { delete mesh; failedThumbnails.insert(key); return; }
            thumbnailCanvas->load_mesh(mesh, false);
            QImage image = thumbnailCanvas->grabFramebuffer();
            thumbnailCanvas->clearMesh();
            if (image.isNull()) { failedThumbnails.insert(key); return; }
            QPixmap pix = QPixmap::fromImage(image);
            thumbnails.insert(key, new QPixmap(pix), std::max(1, pix.width()*pix.height()*4/1024));
            QSaveFile file(cachePath(key));
            if (file.open(QIODevice::WriteOnly) && image.save(&file, "PNG")) file.commit();
            int currentRow = files.indexOf(path);
            if (currentRow >= 0) { strip->item(currentRow)->setIcon(QIcon(pix)); strip->item(currentRow)->setData(Qt::UserRole, key); }
            trimDiskCache();
        });
        connect(loader, &QThread::finished, this, [this, loader, generation] {
            thumbnailLoader = nullptr; loader->deleteLater();
            if (closing) return;
            if (generation != selectionGeneration) startSelected();
            else scheduleThumbnail();
        });
        loader->start(QThread::LowPriority);
        return;
    }
}
void Window::trimDiskCache() {
    QDir dir(QFileInfo(cachePath("unused")).absolutePath());
    auto entries = dir.entryInfoList({"*.png"}, QDir::Files, QDir::Time | QDir::Reversed);
    qint64 size = 0;
    for (const auto& f : entries) size += f.size();
    for (const auto& f : entries) { if (size <= 200*1024*1024) break; if (QFile::remove(f.absoluteFilePath())) size -= f.size(); }
}
void Window::applySettings() {
    QSettings s;
    int mode = qBound(0, s.value("drawMode", 0).toInt(), 4);
    for (Canvas* c : {canvas, thumbnailCanvas}) {
        c->set_drawMode(static_cast<DrawMode>(mode));
        c->setAppearance(settingColor("modelColor", "#ffffff"), settingColor("backgroundColor", "#173b46"), s.value("classicBackground", true).toBool());
        c->setAmbientColor(settingColor("ambientColor", "#38ccff"));
        c->setDirectiveColor(settingColor("directiveColor", "#ffffff"));
        c->setAmbientFactor(s.value("ambientFactor", 0.67).toDouble());
        c->setDirectiveFactor(s.value("directiveFactor", 0.5).toDouble());
        c->setCurrentLightDirection(qBound(0, s.value("currentLightDirection", 1).toInt(), c->getNameDir().size()-1));
        c->view_perspective(s.value("projection", "perspective").toString() == "perspective" ? Canvas::P_PERSPECTIVE : Canvas::P_ORTHOGRAPHIC, false);
    }
    canvas->setResetTransformOnLoad(s.value("resetTransformOnLoad", true).toBool());
    canvas->invert_zoom(s.value("invertZoom", false).toBool());
    canvas->draw_axes(s.value("drawAxes", false).toBool());
    thumbnailCanvas->setResetTransformOnLoad(true);
    thumbnailCanvas->draw_axes(false);
    thumbnailCanvas->clear_status();
    thumbnailSize = qBound(64, s.value("thumbnailSize", 96).toInt(), 160);
    strip->setIconSize(QSize(thumbnailSize, thumbnailSize));
    strip->setGridSize(QSize(thumbnailSize+36, thumbnailSize+42));
    strip->setFixedHeight(thumbnailSize+68);
    if (thumbnailLoader) thumbnailLoader->requestInterruption();
    thumbnails.clear(); failedThumbnails.clear();
    for (int i=0; i<strip->count(); ++i) strip->item(i)->setData(Qt::UserRole, QVariant());
    scheduleThumbnail();
}
void Window::showSettings() {
    if (findChild<QDialog*>("settingsDialog")) return;
    auto* dialog = new QDialog(this);
    dialog->setObjectName("settingsDialog"); dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("STL Browser settings");
    auto* form = new QFormLayout(dialog);
    auto row = [form](const QString& label, QWidget* widget, const QString& tip) {
        widget->setToolTip(tip); widget->setAccessibleName(label); form->addRow(label, widget);
        if (auto* l = form->labelForField(widget)) l->setToolTip(tip);
    };
    auto combo = [&](const char* key, const QString& label, const QStringList& values, int current, const QString& tip) {
        auto* c = new QComboBox; c->addItems(values); c->setCurrentIndex(current); row(label, c, tip);
        connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, key](int i) { QSettings().setValue(key, i); applySettings(); });
    };
    combo("drawMode", "Shading", {"Classic fstl", "Wireframe", "Surface angle", "Custom lighting", "Solid + wireframe"}, QSettings().value("drawMode",0).toInt(), "Classic matches fstl. Wireframe shows edges. Surface angle colors face orientation. Custom lighting enables the light controls below. Solid + wireframe overlays subtle triangle edges on a shaded surface.");
    auto color = [&](const char* key, const char* fallback, const QString& label, const QString& tip) {
        auto* b = new QPushButton(settingColor(key, fallback).name()); row(label, b, tip);
        connect(b, &QPushButton::clicked, dialog, [this,b,key,fallback,dialog] {
            QColor c = QColorDialog::getColor(settingColor(key,fallback), dialog, "Choose color");
            if (c.isValid()) { QSettings().setValue(key,c.name()); b->setText(c.name()); applySettings(); }
        });
    };
    color("modelColor", "#ffffff", "Model tint", "Tint the model in Classic, Custom lighting, or Solid + wireframe mode. White preserves the original fstl colors. Orientation and wireframe modes use their own colors.");
    color("backgroundColor", "#173b46", "Background color", "Solid viewport background color. Turn off Classic background to use this color.");
    auto check = [&](const char* key, const QString& label, bool fallback, const QString& tip) {
        auto* c = new QCheckBox; c->setChecked(QSettings().value(key,fallback).toBool()); row(label,c,tip);
        connect(c,&QCheckBox::toggled,this,[this,key](bool b) { QSettings().setValue(key,b); applySettings(); });
    };
    check("classicBackground", "Classic background", true, "Use fstl’s blue gradient background. Turn off to use a solid custom color.");
    color("ambientColor", "#38ccff", "Ambient light color", "Color illuminating the whole model in Custom lighting mode.");
    color("directiveColor", "#ffffff", "Directional light color", "Color of the directional light in Custom lighting mode.");
    auto strength = [&](const char* key, const QString& label, double fallback, const QString& tip) {
        auto* spin = new QDoubleSpinBox; spin->setRange(0,2); spin->setSingleStep(0.05); spin->setValue(QSettings().value(key,fallback).toDouble()); row(label,spin,tip);
        connect(spin,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this,key](double v) { QSettings().setValue(key,v); applySettings(); });
    };
    strength("ambientFactor","Ambient strength",0.67,"Brightness of overall illumination in Custom lighting mode. Lower values deepen shadows.");
    strength("directiveFactor","Directional strength",0.5,"Brightness of directional lighting in Custom lighting mode. Higher values emphasize face angles.");
    QStringList directions; for (const auto& d : canvas->getNameDir()) directions << d;
    combo("currentLightDirection","Light direction",directions,canvas->getCurrentLightDirection(),"Direction the light comes from in Custom lighting mode, relative to the view.");
    auto* projection = new QComboBox; projection->addItems({"Perspective","Orthographic"}); projection->setCurrentIndex(QSettings().value("projection","perspective").toString()=="perspective"?0:1);
    row("Projection",projection,"Perspective shows depth. Orthographic keeps parallel edges parallel for inspecting shapes.");
    connect(projection,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int i) { QSettings().setValue("projection",i==0?"perspective":"orthographic"); applySettings(); });
    auto* size = new QSpinBox; size->setRange(64,160); size->setSingleStep(16); size->setSuffix(" px"); size->setValue(thumbnailSize);
    row("Thumbnail size",size,"Size of the bottom previews. Smaller previews show more files at once.");
    connect(size,QOverload<int>::of(&QSpinBox::valueChanged),this,[this](int value) { QSettings().setValue("thumbnailSize",value); applySettings(); });
    check("resetTransformOnLoad","Reset rotation on file change",true,"Return to the initial camera angle when opening another STL. Turn off to compare models at the same rotation; each model still fits the viewport.");
    check("invertZoom","Invert zoom",false,"Reverse the mouse wheel’s zoom direction.");
    check("drawAxes","Show axes and dimensions",false,"Show coordinate axes, triangle count, and model bounds. STL files do not encode physical units.");
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close | QDialogButtonBox::RestoreDefaults); form->addRow(buttons);
    buttons->button(QDialogButtonBox::Close)->setToolTip("Close settings. Changes are already saved.");
    buttons->button(QDialogButtonBox::RestoreDefaults)->setToolTip("Restore the app’s original shading, colors, camera, and thumbnail settings.");
    connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::close);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults),&QPushButton::clicked,this,[this,dialog] {
        QSettings s; for (const char* k : {"drawMode","modelColor","backgroundColor","classicBackground","ambientColor","directiveColor","ambientFactor","directiveFactor","currentLightDirection","projection","thumbnailSize","resetTransformOnLoad","invertZoom","drawAxes"}) s.remove(k);
        applySettings(); dialog->setObjectName("oldSettingsDialog"); dialog->close(); showSettings();
    });
    dialog->show();
}
void Window::dragEnterEvent(QDragEnterEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (urls.size()!=1 || !urls.first().isLocalFile()) return;
    QFileInfo f(urls.first().toLocalFile());
    if (f.isDir() || f.suffix().compare("stl",Qt::CaseInsensitive)==0) event->acceptProposedAction();
}
void Window::dropEvent(QDropEvent* event) { load_stl(event->mimeData()->urls().first().toLocalFile()); }
void Window::closeEvent(QCloseEvent* event) {
    QSettings().setValue("windowGeometry",saveGeometry());
    thumbnailTimer.stop();
    thumbnailCanvas->hide();
    QMainWindow::closeEvent(event);
    if (event->isAccepted()) emit closed();
}
