#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include <QString>

class QComboBox;
class QLabel;
class QLayout;
class QLineEdit;
class QPixmap;
class QProgressBar;
class QPushButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void loadVideo(const QString &filePath);
    bool isBusy() const;
    bool lastProbeOk() const { return m_probeOk; }
    bool lastThumbOk() const { return m_thumbOk; }
    QString currentFilePath() const { return m_currentFile; }

private slots:
    void onOpenVideo();
    void onBrowseOutputDir();
    void onStartConvert();
    void onCancelConvert();

private:
    void setupUi();
    void setupConvertUi(QLayout *parentLayout);
    void resetMediaInfo();
    void updateConvertButtons();
    void updateOutputPreview();
    void setConvertProgress(int percent);
    QString plannedOutputPath() const;
    void startProbe(const QString &filePath);
    void startThumbnail(const QString &filePath, int seekSeconds);
    void onProbeFinished(QProcess *process, int exitCode, QProcess::ExitStatus exitStatus);
    void onThumbFinished(QProcess *process, int exitCode, QProcess::ExitStatus exitStatus);
    void parseProbeJson(const QByteArray &jsonData);
    void updateBusyState();
    void showStatus(const QString &message, bool isError = false);
    void abandonProcess(QProcess *&process);
    void stopProcessForDestroy(QProcess *process);
    void cleanupThumbFile(bool delayed = true);
    void applyThumbnailPixmap(const QPixmap &pixmap);
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

    QComboBox *m_formatCombo = nullptr;
    QComboBox *m_qualityCombo = nullptr;
    QLineEdit *m_outputDirEdit = nullptr;
    QLabel *m_outputFileLabel = nullptr;
    QPushButton *m_browseDirButton = nullptr;
    QPushButton *m_startConvertButton = nullptr;
    QPushButton *m_cancelConvertButton = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_progressLabel = nullptr;

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
    bool m_converting = false;

    mutable QString m_cachedFfprobe;
    mutable QString m_cachedFfmpeg;
};

#endif // MAINWINDOW_H
