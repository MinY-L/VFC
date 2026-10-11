#include "mainwindow.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>

namespace {

QString jsonToString(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isDouble())
        return QString::number(value.toDouble(), 'g', 15);
    return {};
}

double jsonToDouble(const QJsonValue &value, bool *ok = nullptr)
{
    if (value.isDouble()) {
        if (ok)
            *ok = true;
        return value.toDouble();
    }
    if (value.isString())
        return value.toString().toDouble(ok);
    if (ok)
        *ok = false;
    return 0.0;
}

qint64 jsonToInt64(const QJsonValue &value, bool *ok = nullptr)
{
    if (value.isDouble()) {
        if (ok)
            *ok = true;
        return static_cast<qint64>(value.toDouble());
    }
    if (value.isString())
        return value.toString().toLongLong(ok);
    if (ok)
        *ok = false;
    return 0;
}

QString formatDuration(double seconds)
{
    if (!(seconds >= 0.0) || !std::isfinite(seconds))
        return QObject::tr("未知");

    const int totalMs = static_cast<int>(std::round(seconds * 1000.0));
    const int ms = totalMs % 1000;
    const int totalSec = totalMs / 1000;
    const int h = totalSec / 3600;
    const int m = (totalSec % 3600) / 60;
    const int s = totalSec % 60;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(ms, 3, 10, QLatin1Char('0'));
}

QString formatBitrate(qint64 bps)
{
    if (bps <= 0)
        return QObject::tr("未知");
    if (bps >= 1000000)
        return QObject::tr("%1 Mbps").arg(bps / 1000000.0, 0, 'f', 2);
    if (bps >= 1000)
        return QObject::tr("%1 kbps").arg(bps / 1000.0, 0, 'f', 1);
    return QObject::tr("%1 bps").arg(bps);
}

QString formatFrameRate(const QString &rate)
{
    if (rate.isEmpty() || rate == QLatin1String("0/0"))
        return QObject::tr("未知");

    const QStringList parts = rate.split(QLatin1Char('/'));
    if (parts.size() == 2) {
        bool okNum = false;
        bool okDen = false;
        const double num = parts.at(0).toDouble(&okNum);
        const double den = parts.at(1).toDouble(&okDen);
        if (okNum && okDen && den != 0.0) {
            const double fps = num / den;
            if (!std::isfinite(fps) || fps <= 0.0)
                return QObject::tr("未知");
            if (std::abs(fps - std::round(fps)) < 0.01)
                return QObject::tr("%1 fps").arg(static_cast<int>(std::round(fps)));
            return QObject::tr("%1 fps").arg(fps, 0, 'f', 2);
        }
    }

    bool ok = false;
    const double fps = rate.toDouble(&ok);
    if (ok && fps > 0.0)
        return QObject::tr("%1 fps").arg(fps, 0, 'f', 2);
    return rate + QStringLiteral(" fps");
}

QString formatCodec(const QJsonObject &stream)
{
    const QString name = jsonToString(stream.value(QStringLiteral("codec_name")));
    if (name.isEmpty())
        return QObject::tr("未知");

    const QString longName = jsonToString(stream.value(QStringLiteral("codec_long_name")));
    if (!longName.isEmpty() && longName != name)
        return QStringLiteral("%1（%2）").arg(name, longName);
    return name;
}

bool isAttachedPic(const QJsonObject &stream)
{
    return stream.value(QStringLiteral("disposition"))
               .toObject()
               .value(QStringLiteral("attached_pic"))
               .toInt() != 0;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_lastDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    setupUi();
}

MainWindow::~MainWindow()
{
    ++m_jobId;
    stopProcessForDestroy(m_probeProcess);
    stopProcessForDestroy(m_thumbProcess);
    cleanupThumbFile(false);
}

void MainWindow::setupUi()
{
    setWindowTitle(tr("视频格式转换器"));

    auto *central = new QWidget(this);
    setCentralWidget(central);

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);

    auto *top = new QHBoxLayout();
    m_openButton = new QPushButton(tr("打开视频"), this);
    m_openButton->setMinimumHeight(32);
    m_openButton->setCursor(Qt::PointingHandCursor);

    m_fileNameLabel = new QLabel(tr("未选择视频"), this);
    m_fileNameLabel->setWordWrap(true);
    m_fileNameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    top->addWidget(m_openButton);
    top->addWidget(m_fileNameLabel, 1);

    m_thumbLabel = new QLabel(this);
    m_thumbLabel->setFixedSize(320, 180);
    m_thumbLabel->setAlignment(Qt::AlignCenter);
    m_thumbLabel->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    m_thumbLabel->setText(tr("缩略图"));
    m_thumbLabel->setScaledContents(false); // 不让 QLabel 再拉图像，缩放在 setPixmap 前做完
    m_thumbLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto *infoBox = new QGroupBox(tr("媒体信息"), this);
    infoBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto *form = new QFormLayout(infoBox);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);
    form->setContentsMargins(12, 12, 12, 12);

    auto makeValue = [this]() {
        auto *lab = new QLabel(tr("未知"), this);
        lab->setTextInteractionFlags(Qt::TextSelectableByMouse);
        lab->setWordWrap(true);
        return lab;
    };

    m_durationLabel = makeValue();
    m_resolutionLabel = makeValue();
    m_videoCodecLabel = makeValue();
    m_frameRateLabel = makeValue();
    m_bitrateLabel = makeValue();
    m_audioCodecLabel = makeValue();

    form->addRow(tr("时长："), m_durationLabel);
    form->addRow(tr("分辨率："), m_resolutionLabel);
    form->addRow(tr("视频编码："), m_videoCodecLabel);
    form->addRow(tr("帧率："), m_frameRateLabel);
    form->addRow(tr("码率："), m_bitrateLabel);
    form->addRow(tr("音频编码："), m_audioCodecLabel);

    auto *body = new QHBoxLayout();
    body->setSpacing(12);
    body->addWidget(m_thumbLabel, 0, Qt::AlignTop);
    body->addWidget(infoBox, 1, Qt::AlignTop);

    root->addLayout(top);
    root->addLayout(body);
    setupConvertUi(root);

    statusBar()->showMessage(tr("请打开一个视频文件"));
    connect(m_openButton, &QPushButton::clicked, this, &MainWindow::onOpenVideo);
    updateConvertButtons();
    updateOutputPreview();
    resize(820, sizeHint().height());
}

void MainWindow::setupConvertUi(QLayout *parentLayout)
{
    auto *box = new QGroupBox(tr("转换"), this);
    box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *optionsRow = new QHBoxLayout();
    auto *formatLabel = new QLabel(tr("输出格式："), this);
    m_formatCombo = new QComboBox(this);
    m_formatCombo->addItem(QStringLiteral("MP4"), QStringLiteral("mp4"));
    m_formatCombo->addItem(QStringLiteral("MKV"), QStringLiteral("mkv"));
    m_formatCombo->addItem(QStringLiteral("MOV"), QStringLiteral("mov"));
    m_formatCombo->addItem(QStringLiteral("AVI"), QStringLiteral("avi"));
    m_formatCombo->setMinimumWidth(100);

    auto *qualityLabel = new QLabel(tr("质量："), this);
    m_qualityCombo = new QComboBox(this);
    m_qualityCombo->addItem(tr("高"), 18);
    m_qualityCombo->addItem(tr("中"), 23);
    m_qualityCombo->addItem(tr("低"), 28);
    m_qualityCombo->setCurrentIndex(1);
    m_qualityCombo->setMinimumWidth(100);
    m_qualityCombo->setToolTip(tr("高 = CRF 18，中 = CRF 23，低 = CRF 28"));

    optionsRow->addWidget(formatLabel);
    optionsRow->addWidget(m_formatCombo);
    optionsRow->addSpacing(16);
    optionsRow->addWidget(qualityLabel);
    optionsRow->addWidget(m_qualityCombo);
    optionsRow->addStretch(1);

    auto *dirRow = new QHBoxLayout();
    auto *dirLabel = new QLabel(tr("输出目录："), this);
    m_outputDirEdit = new QLineEdit(this);
    m_outputDirEdit->setPlaceholderText(tr("请选择输出目录"));
    m_outputDirEdit->setClearButtonEnabled(true);
    m_browseDirButton = new QPushButton(tr("浏览…"), this);

    dirRow->addWidget(dirLabel);
    dirRow->addWidget(m_outputDirEdit, 1);
    dirRow->addWidget(m_browseDirButton);

    auto *fileRow = new QHBoxLayout();
    auto *fileLabel = new QLabel(tr("输出文件："), this);
    m_outputFileLabel = new QLabel(tr("未选择视频"), this);
    m_outputFileLabel->setWordWrap(true);
    m_outputFileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fileRow->addWidget(fileLabel, 0, Qt::AlignTop);
    fileRow->addWidget(m_outputFileLabel, 1);

    auto *actionRow = new QHBoxLayout();
    m_startConvertButton = new QPushButton(tr("开始转换"), this);
    m_startConvertButton->setMinimumHeight(32);
    m_startConvertButton->setCursor(Qt::PointingHandCursor);
    m_cancelConvertButton = new QPushButton(tr("取消转换"), this);
    m_cancelConvertButton->setMinimumHeight(32);
    actionRow->addWidget(m_startConvertButton);
    actionRow->addWidget(m_cancelConvertButton);
    actionRow->addStretch(1);

    auto *progressRow = new QHBoxLayout();
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(22);
    m_progressBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_progressLabel = new QLabel(tr("0%"), this);
    m_progressLabel->setMinimumWidth(44);
    m_progressLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    progressRow->addWidget(m_progressBar, 1);
    progressRow->addWidget(m_progressLabel);

    layout->addLayout(optionsRow);
    layout->addLayout(dirRow);
    layout->addLayout(fileRow);
    layout->addLayout(actionRow);
    layout->addLayout(progressRow);
    parentLayout->addWidget(box);

    connect(m_browseDirButton, &QPushButton::clicked, this, &MainWindow::onBrowseOutputDir);
    connect(m_startConvertButton, &QPushButton::clicked, this, &MainWindow::onStartConvert);
    connect(m_cancelConvertButton, &QPushButton::clicked, this, &MainWindow::onCancelConvert);
    connect(m_formatCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        updateOutputPreview();
    });
    connect(m_outputDirEdit, &QLineEdit::textChanged, this, [this](const QString &) {
        updateOutputPreview();
    });
}

void MainWindow::updateConvertButtons()
{
    const bool hasFile = !m_currentFile.isEmpty();
    m_startConvertButton->setEnabled(hasFile && !m_converting);
    m_cancelConvertButton->setEnabled(hasFile && m_converting);
}

void MainWindow::setConvertProgress(int percent)
{
    percent = qBound(0, percent, 100);
    m_progressBar->setValue(percent);
    m_progressLabel->setText(tr("%1%").arg(percent));
}

QString MainWindow::plannedOutputPath() const
{
    if (m_currentFile.isEmpty() || !m_outputDirEdit || !m_formatCombo)
        return {};
    const QString dir = m_outputDirEdit->text().trimmed();
    if (dir.isEmpty())
        return {};
    const QString ext = m_formatCombo->currentData().toString();
    const QString baseName = QFileInfo(m_currentFile).completeBaseName();
    return QDir(dir).filePath(baseName + QStringLiteral("_converted.") + ext);
}

void MainWindow::updateOutputPreview()
{
    if (!m_outputFileLabel)
        return;
    if (m_currentFile.isEmpty()) {
        m_outputFileLabel->setText(tr("未选择视频"));
        return;
    }
    const QString path = plannedOutputPath();
    if (path.isEmpty()) {
        m_outputFileLabel->setText(tr("请先选择输出目录"));
        return;
    }
    m_outputFileLabel->setText(QDir::toNativeSeparators(path));
}

void MainWindow::resetMediaInfo()
{
    m_thumbLabel->setPixmap(QPixmap());
    m_thumbLabel->setText(tr("缩略图"));
    m_durationLabel->setText(tr("未知"));
    m_resolutionLabel->setText(tr("未知"));
    m_videoCodecLabel->setText(tr("未知"));
    m_frameRateLabel->setText(tr("未知"));
    m_bitrateLabel->setText(tr("未知"));
    m_audioCodecLabel->setText(tr("未知"));
}

void MainWindow::onOpenVideo()
{
    const QString filter = tr(
        "视频文件 (*.mp4 *.mkv *.avi *.mov *.wmv *.flv *.webm *.m4v "
        "*.ts *.m2ts *.mts *.mpeg *.mpg *.3gp *.3g2 *.ogv *.rmvb *.rm "
        "*.asf *.f4v *.vob *.divx *.m3u8);;所有文件 (*.*)");

    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("打开视频"),
        m_lastDir,
        filter);
    if (path.isEmpty())
        return;

    m_lastDir = QFileInfo(path).absolutePath();
    loadVideo(path);
}

void MainWindow::onBrowseOutputDir()
{
    QString startDir = m_outputDirEdit->text().trimmed();
    if (startDir.isEmpty()) {
        startDir = m_lastDir.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
            : m_lastDir;
    }

    const QString dir = QFileDialog::getExistingDirectory(this, tr("选择输出目录"), startDir);
    if (dir.isEmpty())
        return;
    m_outputDirEdit->setText(QDir::toNativeSeparators(dir));
}

void MainWindow::onStartConvert()
{
    if (m_currentFile.isEmpty() || m_converting)
        return;

    const QString dir = m_outputDirEdit->text().trimmed();
    if (dir.isEmpty() || !QDir(dir).exists()) {
        showStatus(tr("请选择有效的输出目录"), true);
        return;
    }

    showStatus(tr("转换功能即将接入"));
}

void MainWindow::onCancelConvert()
{
    if (!m_converting)
        return;
}

void MainWindow::loadVideo(const QString &filePath)
{
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        showStatus(tr("文件不存在：%1").arg(QDir::toNativeSeparators(filePath)), true);
        return;
    }

    m_currentFile = info.absoluteFilePath();

    // 使进行中的探测/抽帧回调全部失效，再异步杀掉旧进程，不阻塞界面
    ++m_jobId;
    m_probeOk = false;
    m_thumbOk = false;
    m_thumbRetryUsed = false;
    m_durationSec = -1.0;
    m_thumbSeekSeconds = 1;

    abandonProcess(m_probeProcess);
    abandonProcess(m_thumbProcess);
    cleanupThumbFile(true);
    resetMediaInfo();

    m_fileNameLabel->setText(QDir::toNativeSeparators(m_currentFile));
    if (m_outputDirEdit->text().trimmed().isEmpty())
        m_outputDirEdit->setText(QDir::toNativeSeparators(info.absolutePath()));
    updateConvertButtons();
    updateOutputPreview();
    showStatus(tr("正在读取视频信息…"));
    startProbe(m_currentFile);
}

bool MainWindow::isBusy() const
{
    return (m_probeProcess && m_probeProcess->state() != QProcess::NotRunning)
    || (m_thumbProcess && m_thumbProcess->state() != QProcess::NotRunning)
        || m_converting;
}

void MainWindow::startProbe(const QString &filePath)
{
    const quint64 jobId = m_jobId;

    abandonProcess(m_probeProcess);
    m_probeProcess = new QProcess(this);
    m_probeProcess->setProcessChannelMode(QProcess::SeparateChannels);

    QProcess *proc = m_probeProcess;
    connect(proc, &QProcess::errorOccurred, this,
            [this, jobId](QProcess::ProcessError error) {
        if (jobId != m_jobId)
            return;
        if (error == QProcess::FailedToStart) {
            showStatus(tr("无法启动 ffprobe，请确认已安装 FFmpeg 并将其加入 PATH"), true);
            updateBusyState();
        }
    });

    connect(proc, &QProcess::finished, this,
            [this, jobId, proc](int exitCode, QProcess::ExitStatus exitStatus) {
        if (jobId != m_jobId)
            return;
        onProbeFinished(proc, exitCode, exitStatus);
    });

    // 路径作为独立参数传入，中文、空格都由 QProcess 正确引用，禁止拼进一个命令字符串
    QStringList args;
    args << QStringLiteral("-v") << QStringLiteral("quiet")
         << QStringLiteral("-print_format") << QStringLiteral("json")
         << QStringLiteral("-show_format")
         << QStringLiteral("-show_streams")
         << filePath;

    m_probeProcess->start(resolveTool(QStringLiteral("ffprobe")), args);
    updateBusyState();
}

void MainWindow::onProbeFinished(QProcess *process, int exitCode, QProcess::ExitStatus exitStatus)
{
    updateBusyState();
    if (!process || process->error() == QProcess::FailedToStart)
        return;

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        showStatus(tr("无法读取该文件的媒体信息，请确认文件是有效视频"), true);
        return;
    }

    const QByteArray json = process->readAllStandardOutput();
    if (json.trimmed().isEmpty()) {
        showStatus(tr("ffprobe 未返回任何信息"), true);
        return;
    }

    parseProbeJson(json);
}

void MainWindow::parseProbeJson(const QByteArray &jsonData)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        showStatus(tr("解析视频信息失败：%1").arg(parseError.errorString()), true);
        return;
    }

    const QJsonObject root = doc.object();
    const QJsonObject format = root.value(QStringLiteral("format")).toObject();
    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();

    QJsonObject video;
    QJsonObject audio;
    for (const QJsonValue &item : streams) {
        if (!item.isObject())
            continue;
        const QJsonObject stream = item.toObject();
        const QString type = jsonToString(stream.value(QStringLiteral("codec_type")));
        if (type == QLatin1String("video")) {
            if (isAttachedPic(stream))
                continue;
            if (video.isEmpty())
                video = stream;
        } else if (type == QLatin1String("audio")) {
            if (audio.isEmpty())
                audio = stream;
        }
    }

    bool durationOk = false;
    double duration = jsonToDouble(format.value(QStringLiteral("duration")), &durationOk);
    if (!durationOk || duration < 0.0)
        duration = jsonToDouble(video.value(QStringLiteral("duration")), &durationOk);
    if (durationOk && duration >= 0.0) {
        m_durationSec = duration;
        m_durationLabel->setText(formatDuration(duration));
    } else {
        m_durationSec = -1.0;
        m_durationLabel->setText(tr("未知"));
    }

    if (video.isEmpty()) {
        m_resolutionLabel->setText(tr("无"));
        m_videoCodecLabel->setText(tr("无"));
        m_frameRateLabel->setText(tr("无"));
    } else {
        const int width = video.value(QStringLiteral("width")).toInt();
        const int height = video.value(QStringLiteral("height")).toInt();
        if (width > 0 && height > 0)
            m_resolutionLabel->setText(tr("%1 × %2").arg(width).arg(height));
        else
            m_resolutionLabel->setText(tr("未知"));

        m_videoCodecLabel->setText(formatCodec(video));

        QString fps = jsonToString(video.value(QStringLiteral("avg_frame_rate")));
        if (fps.isEmpty() || fps == QLatin1String("0/0"))
            fps = jsonToString(video.value(QStringLiteral("r_frame_rate")));
        m_frameRateLabel->setText(formatFrameRate(fps));
    }

    bool bitrateOk = false;
    qint64 bitrate = jsonToInt64(format.value(QStringLiteral("bit_rate")), &bitrateOk);
    if (!bitrateOk || bitrate <= 0)
        bitrate = jsonToInt64(video.value(QStringLiteral("bit_rate")), &bitrateOk);
    m_bitrateLabel->setText(bitrateOk ? formatBitrate(bitrate) : tr("未知"));

    if (audio.isEmpty())
        m_audioCodecLabel->setText(tr("无"));
    else
        m_audioCodecLabel->setText(formatCodec(audio));

    m_probeOk = true;

    const int seekSeconds = (m_durationSec >= 0.0 && m_durationSec < 1.0) ? 0 : 1;
    showStatus(tr("正在提取缩略图…"));
    startThumbnail(m_currentFile, seekSeconds);
}

void MainWindow::startThumbnail(const QString &filePath, int seekSeconds)
{
    const quint64 jobId = m_jobId;
    m_thumbSeekSeconds = seekSeconds;

    cleanupThumbFile();
    m_thumbPath = QDir::temp().filePath(
        QStringLiteral("vfc_thumb_%1_%2.jpg")
            .arg(QCoreApplication::applicationPid())
            .arg(QDateTime::currentMSecsSinceEpoch()));

    abandonProcess(m_thumbProcess);
    m_thumbProcess = new QProcess(this);
    m_thumbProcess->setProcessChannelMode(QProcess::SeparateChannels);

    QProcess *proc = m_thumbProcess;
    connect(proc, &QProcess::errorOccurred, this,
            [this, jobId](QProcess::ProcessError error) {
        if (jobId != m_jobId)
            return;
        if (error == QProcess::FailedToStart) {
            showStatus(tr("无法启动 ffmpeg，请确认已安装 FFmpeg 并将其加入 PATH"), true);
            updateBusyState();
        }
    });

    connect(proc, &QProcess::finished, this,
            [this, jobId, proc](int exitCode, QProcess::ExitStatus exitStatus) {
        if (jobId != m_jobId)
            return;
        onThumbFinished(proc, exitCode, exitStatus);
    });

    QStringList args;
    args << QStringLiteral("-ss") << QString::number(seekSeconds)
         << QStringLiteral("-i") << filePath
         << QStringLiteral("-frames:v") << QStringLiteral("1")
         << QStringLiteral("-vf") << QStringLiteral("scale=640:-1")
         << QStringLiteral("-y")
         << m_thumbPath;

    m_thumbProcess->start(resolveTool(QStringLiteral("ffmpeg")), args);
    updateBusyState();
}

void MainWindow::onThumbFinished(QProcess *process, int exitCode, QProcess::ExitStatus exitStatus)
{
    updateBusyState();
    if (!process || process->error() == QProcess::FailedToStart)
        return;

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        if (m_thumbSeekSeconds != 0 && !m_thumbRetryUsed) {
            m_thumbRetryUsed = true;
            showStatus(tr("正在重试提取缩略图…"));
            startThumbnail(m_currentFile, 0);
            return;
        }
        showStatus(m_probeOk ? tr("视频信息已加载，但缩略图提取失败")
                             : tr("缩略图提取失败"),
                   true);
        return;
    }

    QPixmap pixmap(m_thumbPath);
    cleanupThumbFile();
    if (pixmap.isNull()) {
        showStatus(tr("缩略图文件无法显示"), true);
        return;
    }

    applyThumbnailPixmap(pixmap);
    m_thumbOk = true;
    showStatus(tr("已加载：%1").arg(QFileInfo(m_currentFile).fileName()));
}

void MainWindow::updateBusyState()
{
    setCursor(isBusy() ? Qt::BusyCursor : Qt::ArrowCursor);
}

void MainWindow::showStatus(const QString &message, bool isError)
{
    statusBar()->showMessage(message, isError ? 8000 : 6000);
}

void MainWindow::abandonProcess(QProcess *&process)
{
    if (!process)
        return;
    process->disconnect();
    if (process->state() != QProcess::NotRunning)
        process->kill();
    process->deleteLater();
    process = nullptr;
}

void MainWindow::stopProcessForDestroy(QProcess *process)
{
    if (!process)
        return;
    process->disconnect();
    if (process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(500);
    }
}

void MainWindow::cleanupThumbFile(bool delayed)
{
    if (m_thumbPath.isEmpty())
        return;
    const QString leftover = m_thumbPath;
    m_thumbPath.clear();
    if (!delayed) {
        QFile::remove(leftover);
        return;
    }
    // 快速连开时旧 ffmpeg 可能还占着文件，稍后再删，避免阻塞 UI
    QTimer::singleShot(2000, [leftover]() { QFile::remove(leftover); });
}

void MainWindow::applyThumbnailPixmap(const QPixmap &pixmap)
{
    const QSize box = m_thumbLabel->contentsRect().size();
    if (box.isEmpty() || pixmap.isNull()) {
        m_thumbLabel->setPixmap(QPixmap());
        m_thumbLabel->setText(tr("缩略图"));
        return;
    }

    const qreal dpr = m_thumbLabel->devicePixelRatioF();
    QPixmap scaled = pixmap.scaled(box * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    m_thumbLabel->setText(QString());
    m_thumbLabel->setPixmap(scaled);
}

QString MainWindow::resolveTool(const QString &name) const
{
    const bool wantProbe = name.startsWith(QLatin1String("ffprobe"), Qt::CaseInsensitive);
    const bool wantMpeg = name.startsWith(QLatin1String("ffmpeg"), Qt::CaseInsensitive);
    if (wantProbe && !m_cachedFfprobe.isEmpty())
        return m_cachedFfprobe;
    if (wantMpeg && !m_cachedFfmpeg.isEmpty())
        return m_cachedFfmpeg;

#ifdef Q_OS_WIN
    const QString exe = name.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)
                            ? name
                            : name + QStringLiteral(".exe");
#else
    const QString exe = name;
#endif

    auto absIfExists = [](const QString &path) -> QString {
        const QFileInfo info(path);
        if (info.exists() && info.isFile())
            return info.absoluteFilePath();
        return {};
    };

    auto searchPathList = [&](const QString &pathValue) -> QString {
        const QStringList dirs = pathValue.split(QDir::listSeparator(), Qt::SkipEmptyParts);
        for (QString dir : dirs) {
            dir = dir.trimmed();
            if (dir.size() >= 2 && dir.startsWith(QLatin1Char('"')) && dir.endsWith(QLatin1Char('"')))
                dir = dir.mid(1, dir.size() - 2);
            if (dir.isEmpty())
                continue;
            const QString hit = absIfExists(QDir(dir).filePath(exe));
            if (!hit.isEmpty())
                return hit;
        }
        return {};
    };

    auto remember = [&](const QString &hit) -> QString {
        if (hit.isEmpty())
            return {};
        if (wantProbe)
            m_cachedFfprobe = hit;
        else if (wantMpeg)
            m_cachedFfmpeg = hit;
        return hit;
    };

    const QString appDir = QCoreApplication::applicationDirPath();
    for (const QString &candidate : {
             QDir(appDir).filePath(exe),
             QDir(appDir).filePath(QStringLiteral("ffmpeg/bin/") + exe),
             QDir(appDir).filePath(QStringLiteral("tools/") + exe),
         }) {
        const QString hit = absIfExists(candidate);
        if (!hit.isEmpty())
            return remember(hit);
    }

    // 当前进程 PATH（从终端或重启后的 Qt Creator 运行时能直接命中 winget 注入的目录）
    const QString fromEnv = searchPathList(qEnvironmentVariable("PATH"));
    if (!fromEnv.isEmpty())
        return remember(fromEnv);

#ifdef Q_OS_WIN
    // Qt Creator 若在 winget install 之前就已启动，进程 PATH 可能是旧的；改读注册表里的用户/系统 PATH
    const QSettings userEnv(QStringLiteral("HKEY_CURRENT_USER\\Environment"), QSettings::NativeFormat);
    const QString fromUserPath = searchPathList(userEnv.value(QStringLiteral("Path")).toString());
    if (!fromUserPath.isEmpty())
        return remember(fromUserPath);

    const QSettings machineEnv(
        QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"),
        QSettings::NativeFormat);
    const QString fromMachinePath = searchPathList(machineEnv.value(QStringLiteral("Path")).toString());
    if (!fromMachinePath.isEmpty())
        return remember(fromMachinePath);

    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty()) {
        const QString fromLinks = absIfExists(
            QDir(localAppData).filePath(QStringLiteral("Microsoft/WinGet/Links/") + exe));
        if (!fromLinks.isEmpty())
            return remember(fromLinks);

        const QDir packages(QDir(localAppData).filePath(QStringLiteral("Microsoft/WinGet/Packages")));
        const QStringList ffmpegPkgs = packages.entryList(
            QStringList{QStringLiteral("*FFmpeg*"), QStringLiteral("*ffmpeg*")},
            QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &pkg : ffmpegPkgs) {
            const QDir pkgDir(packages.filePath(pkg));
            const QStringList builds = pkgDir.entryList(QStringList{QStringLiteral("ffmpeg*")},
                                                       QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &build : builds) {
                const QString hit = absIfExists(pkgDir.filePath(build + QStringLiteral("/bin/") + exe));
                if (!hit.isEmpty())
                    return remember(hit);
            }
            const QString inBin = absIfExists(pkgDir.filePath(QStringLiteral("bin/") + exe));
            if (!inBin.isEmpty())
                return remember(inBin);
            const QString inRoot = absIfExists(pkgDir.filePath(exe));
            if (!inRoot.isEmpty())
                return remember(inRoot);
        }
    }
#endif

    // 最后仍交给 QProcess 自己查 PATH；找不到会走 FailedToStart
    return name;
}
