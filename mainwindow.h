#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include <QString>

class QLabel;
class QPushButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onOpenVideo();

private:
    void setupUi();
    void resetMediaInfo();
    void startProbe(const QString &filePath);
    void startThumbnail(const QString &filePath, int seekSeconds);
    void onProbeFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onThumbFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void parseProbeJson(const QByteArray &jsonData);
    void updateBusyState();
    void showStatus(const QString &message, bool isError = false);
    void abandonProcess(QProcess *&process);
    void stopProcessForDestroy(QProcess *process);
    void cleanupThumbFile();
    QString resolveTool(const QString &name) const;

    QPushButton *m_openButton = nullptr;
    QLabel *m_fileNameLabel = nullptr;
    QLabel *m_thumbLabel = nullptr;
    QLabel *m_durationLabel = nullptr;
    QLabel *m_resolutionLabel = nullptr;
    QLabel *m_videoCodecLabel = nullptr;
    QLabel *m_frameRateLabel = nullptr;
    QLabel *m_bitrateLabel = nullptr;
    QLabel *m_audioCodecLabel = nullptr;

    QProcess *m_probeProcess = nullptr;
    QProcess *m_thumbProcess = nullptr;

    QString m_currentFile;
    QString m_lastDir;
    QString m_thumbPath;

    // 递增后忽略过期的 QProcess 回调，避免旧任务写回新文件的结果
    quint64 m_jobId = 0;
    double m_durationSec = -1.0;
    int m_thumbSeekSeconds = 1;
    bool m_thumbRetryUsed = false;
    bool m_probeOk = false;
    bool m_thumbOk = false;
};

#endif // MAINWINDOW_H
