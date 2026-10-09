#ifndef WINDOW_H
#define WINDOW_H
#include <QMainWindow>
#include <QCache>
#include <QListWidget>
#include <QTimer>
#include <QFileSystemWatcher>
#include <QLabel>
#include <QAction>
#include "canvas.h"
#include "loader.h"

class Window : public QMainWindow
{
    Q_OBJECT
public:
    explicit Window(QWidget* parent = nullptr);
    ~Window() override;
    bool load_stl(const QString& filename, bool reload = false);
    bool load_prev();
    bool load_next();
    static QStringList filesInFolder(const QString& folder);
    static QString thumbnailKey(const QString& path, const QByteArray& appearance);
signals:
    void closed();
protected:
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;
    void closeEvent(QCloseEvent*) override;
private:
    void buildUi();
    void selectFile(int row);
    void startSelected();
    void rebuildStrip(const QString& directory);
    void scheduleThumbnail();
    void nextThumbnail();
    void updateNavigation();
    void applySettings();
    void showSettings();
    void trimDiskCache();
    void showError(const QString& text);
    QString cachePath(const QString& key) const;
    QByteArray appearanceKey() const;
    Canvas* canvas = nullptr;
    Canvas* thumbnailCanvas = nullptr;
    QListWidget* strip = nullptr;
    QLabel* info = nullptr;
    QAction* previous = nullptr;
    QAction* next = nullptr;
    QTimer thumbnailTimer;
    QFileSystemWatcher watcher;
    QCache<QString, QPixmap> thumbnails{32768};
    QSet<QString> failedThumbnails;
    Loader* selectedLoader = nullptr;
    Loader* thumbnailLoader = nullptr;
    QPixmap placeholder;
    QString directory;
    QStringList files;
    QString requestedFile;
    quint64 selectionGeneration = 0;
    int thumbnailSize = 96;
    bool closing = false;
};
#endif
