#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QMainWindow>
#include <QProcess>
#include <QString>

class QComboBox;
class QFormLayout;
class QGroupBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QResizeEvent;
class QTimer;
class QVBoxLayout;
class ThumbView;

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

    void setConvertOutputDir(const QString &dir);
    bool setConvertFormat(const QString &format);
    bool setConvertQualityCrf(int crf);
    bool startConversion();
    void cancelConversion();
    QString plannedOutputPath() const;
    QString lastConvertOutputPath() const { return m_convertOutputPath; }
    QString convertTempPath() const { return m_convertTempPath; }
    bool lastConvertOk() const { return m_convertOk; }
    bool isConverting() const { return m_converting; }
    QString ffmpegToolPath() const { return resolveTool(QStringLiteral("ffmpeg")); }

private slots:
    void onOpenVideo();
    void onBrowseOutputDir();
    void onStartConvert();
    void onCancelConvert();
    void onOpenOutputFile();
    void onClearLog();
    void onSmoothProgressTick();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUi();
    void setupConvertUi(QVBoxLayout *parentLayout);
    void updateMediaInfoLayout();
    void clearInfoColumn(QFormLayout *form);
    void resetMediaInfo();
    void updateConvertButtons();
    void updateOutputPreview();
    void setConvertProgress(int percent);
    void setConvertProgressRatio(double ratio);
    QString uniqueOutputPath(const QString &preferred) const;
    QString partPathFor(const QString &output) const;
    QStringList buildConvertArgs(const QString &input,
                                 const QString &output,
                                 const QString &format,
                                 int crf) const;
    void startConvert(const QString &output, const QString &format, int crf);
    void onConvertFinished(QProcess *process, int exitCode, QProcess::ExitStatus exitStatus);
    void onConvertStdout(QProcess *process);
    void parseConvertProgressLine(const QString &line);
    void finishConvert(bool success, const QString &message, bool isError);
    void stopConvertProcess();
    void removeIncompleteOutput();
    bool promoteConvertOutput();
    void startProbe(const QString &filePath);
    void startThumbnail(const QString &filePath, int seekSeconds);
    void onProbeFinished(QProcess *process, int exitCode, QProcess::ExitStatus exitStatus);
    void onThumbFinished(QProcess *process, int exitCode, QProcess::ExitStatus exitStatus);
    void parseProbeJson(const QByteArray &jsonData);
    void updateBusyState();
    void showStatus(const QString &message, bool isError = false);
    void appendLog(const QString &message, bool isError = false);
    void setupLogUi(QVBoxLayout *parentLayout);
    void abandonProcess(QProcess *&process);
    void stopProcessForDestroy(QProcess *process);
    void cleanupThumbFile(bool delayed = true);
    QString resolveTool(const QString &name) const;

    QPushButton *m_openButton = nullptr;
    QLabel *m_fileNameLabel = nullptr;
    ThumbView *m_thumbLabel = nullptr;
    QGroupBox *m_infoBox = nullptr;
    QHBoxLayout *m_infoOuter = nullptr;
    QFormLayout *m_infoCol0 = nullptr;
    QFormLayout *m_infoCol1 = nullptr;
    QList<QLabel *> m_infoTitles;
    QList<QLabel *> m_infoValues;
    int m_infoFirstColCount = 0;
    bool m_updatingInfoLayout = false;
    QGroupBox *m_logBox = nullptr;
    QGroupBox *m_convertBox = nullptr;
    QLabel *m_durationLabel = nullptr;
    QLabel *m_fileSizeLabel = nullptr;
    QLabel *m_containerLabel = nullptr;
    QLabel *m_resolutionLabel = nullptr;
    QLabel *m_pixelFormatLabel = nullptr;
    QLabel *m_videoCodecLabel = nullptr;
    QLabel *m_frameRateLabel = nullptr;
    QLabel *m_bitrateLabel = nullptr;
    QLabel *m_audioCodecLabel = nullptr;
    QLabel *m_audioParamsLabel = nullptr;
    QPlainTextEdit *m_logEdit = nullptr;
    QPushButton *m_clearLogButton = nullptr;

    QComboBox *m_formatCombo = nullptr;
    QComboBox *m_qualityCombo = nullptr;
    QLineEdit *m_outputDirEdit = nullptr;
    QLabel *m_outputFileLabel = nullptr;
    QPushButton *m_browseDirButton = nullptr;
    QPushButton *m_startConvertButton = nullptr;
    QPushButton *m_cancelConvertButton = nullptr;
    QPushButton *m_openOutputButton = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_progressLabel = nullptr;
    QTimer *m_progressSmoothTimer = nullptr;

    QProcess *m_probeProcess = nullptr;
    QProcess *m_thumbProcess = nullptr;
    QProcess *m_convertProcess = nullptr;

    QString m_currentFile;
    QString m_lastDir;
    QString m_thumbPath;
    QString m_convertOutputPath;
    QString m_convertTempPath;

    // 递增后忽略过期的 QProcess 回调，避免旧任务写回新文件的结果
    quint64 m_jobId = 0;
    quint64 m_convertJobId = 0;
    double m_durationSec = -1.0;
    int m_thumbSeekSeconds = 1;
    bool m_thumbRetryUsed = false;
    bool m_probeOk = false;
    bool m_thumbOk = false;
    bool m_converting = false;
    bool m_convertCancelRequested = false;
    bool m_convertOk = false;

    double m_progressTimeSec = 0.0;
    double m_progressSpeed = 1.0;
    QElapsedTimer m_progressClock;
    QByteArray m_convertProgressBuf;
    QByteArray m_convertErrorBuf;

    mutable QString m_cachedFfprobe;
    mutable QString m_cachedFfmpeg;
};

#endif // MAINWINDOW_H
