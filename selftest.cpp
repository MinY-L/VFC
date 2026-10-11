#include "selftest.h"
#include "mainwindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStringConverter>
#include <QTextStream>
#include <QTimer>

#include <cstdio>

namespace {

bool sameFile(const QString &lhs, const QString &rhs)
{
    return QFileInfo(lhs) == QFileInfo(rhs);
}

bool waitUntilIdle(MainWindow *window, int timeoutMs)
{
    if (!window->isBusy())
        return true;

    QElapsedTimer clock;
    clock.start();

    QEventLoop loop;
    QTimer ticker;
    ticker.setInterval(20);
    QObject::connect(&ticker, &QTimer::timeout, &loop, [&]() {
        if (!window->isBusy() || clock.elapsed() >= timeoutMs)
            loop.quit();
    });
    ticker.start();
    loop.exec();

    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return !window->isBusy();
}

void waitMs(int ms)
{
    if (ms <= 0)
        return;
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

QString selfTestTempRoot()
{
    return QDir(QDir::tempPath()).filePath(QStringLiteral("vfc_selftest"));
}

void cleanupSelfTestTemp()
{
    QDir(selfTestTempRoot()).removeRecursively();
}

QString prepareOutDir(const QString &name)
{
    const QString dir = QDir(selfTestTempRoot()).filePath(name);
    QDir().mkpath(dir);
    QDir d(dir);
    const QStringList files = d.entryList(QDir::Files);
    for (const QString &file : files)
        QFile::remove(d.filePath(file));
    return QFileInfo(dir).absoluteFilePath();
}

bool filesGone(const QStringList &paths, int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < timeoutMs) {
        bool any = false;
        for (const QString &path : paths) {
            if (!path.isEmpty() && QFileInfo::exists(path)) {
                any = true;
                break;
            }
        }
        if (!any)
            return true;
        waitMs(50);
    }
    for (const QString &path : paths) {
        if (!path.isEmpty() && QFileInfo::exists(path))
            return false;
    }
    return true;
}

} // namespace

class SelfTest final : public ISelfTest
{
public:
    int run(MainWindow *window) override
    {
        m_window = window;
        log(QStringLiteral("======== VFC 自测开始 ========"));

        if (!ensureAssets()) {
            fail(QStringLiteral("准备测试素材"),
                 QStringLiteral("无法准备视频素材，请确认已安装 FFmpeg"));
        } else {
            testOneValidVideo(QStringLiteral("单独打开 test.mp4"), m_testMp4);
            testBrokenVideo();
            testOneValidVideo(QStringLiteral("单独打开 中文 测试 视频.mp4"), m_chineseMp4);
            testRapidSwitch();
            testConvertLongToMp4();
            testCancelLeavesNoJunk();
        }

        const QString reportPath =
            QCoreApplication::applicationDirPath() + QStringLiteral("/selftest_result.txt");
        QFile file(reportPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out.setEncoding(QStringConverter::Utf8);
            out << (m_failed == 0 ? QStringLiteral("结果：通过\n") : QStringLiteral("结果：失败\n"))
                << QStringLiteral("通过项数=") << m_passed << "\n"
                << QStringLiteral("失败项数=") << m_failed << "\n";
            for (const QString &line : m_lines)
                out << line << "\n";
        }

        cleanupSelfTestTemp();
        log(QStringLiteral("已清理临时目录：%1").arg(QDir::toNativeSeparators(selfTestTempRoot())));
        log(QStringLiteral("结果已写入：%1").arg(QDir::toNativeSeparators(reportPath)));
        log(QStringLiteral("======== 自测结束：通过 %1 项，失败 %2 项 ========")
                .arg(m_passed)
                .arg(m_failed));
        return m_failed == 0 ? 0 : 1;
    }

private:
    void log(const QString &line)
    {
        m_lines.append(line);
        const QString out = QStringLiteral("[自测] %1").arg(line);
        // 直接写 stderr：Qt Creator「应用程序输出」会实时捕获，无需等自测结束
        fprintf(stderr, "%s\n", qUtf8Printable(out));
        fflush(stderr);
    }

    void pass(const QString &name, const QString &detail = QString())
    {
        ++m_passed;
        log(QStringLiteral("[通过] %1%2")
                .arg(name, detail.isEmpty() ? QString() : QStringLiteral(" | ") + detail));
    }

    void fail(const QString &name, const QString &detail)
    {
        ++m_failed;
        log(QStringLiteral("[失败] %1 | %2").arg(name, detail));
    }

    bool requireExists(const QString &name, const QString &path)
    {
        if (QFileInfo::exists(path))
            return true;
        fail(name, QStringLiteral("找不到测试文件：%1").arg(QDir::toNativeSeparators(path)));
        return false;
    }

    void testOneValidVideo(const QString &name, const QString &path)
    {
        if (!requireExists(name, path))
            return;

        log(QStringLiteral("开始：%1").arg(name));
        m_window->loadVideo(path);
        log(QStringLiteral("正在等待解析与抽帧…"));
        if (!waitUntilIdle(m_window, 15000)) {
            fail(name, QStringLiteral("等待超时，仍在处理"));
            return;
        }

        QStringList errors;
        if (!sameFile(m_window->currentFilePath(), path)) {
            errors << QStringLiteral("当前文件不匹配，实际为：%1")
                          .arg(QDir::toNativeSeparators(m_window->currentFilePath()));
        }
        if (!m_window->lastProbeOk())
            errors << QStringLiteral("读取视频信息未成功");
        if (!m_window->lastThumbOk())
            errors << QStringLiteral("缩略图未成功");

        if (errors.isEmpty())
            pass(name, QDir::toNativeSeparators(path));
        else
            fail(name, errors.join(QStringLiteral("；")));
    }

    void testBrokenVideo()
    {
        const QString name = QStringLiteral("单独打开 损坏视频.mp4");
        if (!requireExists(name, m_brokenMp4))
            return;

        log(QStringLiteral("开始：%1").arg(name));
        m_window->loadVideo(m_brokenMp4);
        log(QStringLiteral("正在等待损坏文件被拒绝…"));
        if (!waitUntilIdle(m_window, 15000)) {
            fail(name, QStringLiteral("等待超时，仍在处理（程序可能卡住）"));
            return;
        }

        QStringList errors;
        if (!sameFile(m_window->currentFilePath(), m_brokenMp4)) {
            errors << QStringLiteral("当前文件不匹配，实际为：%1")
                          .arg(QDir::toNativeSeparators(m_window->currentFilePath()));
        }
        if (m_window->lastProbeOk())
            errors << QStringLiteral("损坏视频不应解析成功");
        if (m_window->lastThumbOk())
            errors << QStringLiteral("损坏视频不应抽出缩略图");

        if (errors.isEmpty())
            pass(name, QStringLiteral("已拒绝损坏文件且未崩溃"));
        else
            fail(name, errors.join(QStringLiteral("；")));
    }

    void testRapidSwitch()
    {
        const QString name = QStringLiteral("快速连续打开 test.mp4 与 中文 测试 视频.mp4");
        if (!requireExists(name, m_testMp4) || !requireExists(name, m_chineseMp4))
            return;

        log(QStringLiteral("开始：%1").arg(name));
        m_window->loadVideo(m_testMp4);
        m_window->loadVideo(m_chineseMp4);

        QEventLoop delay;
        QTimer::singleShot(40, &delay, &QEventLoop::quit);
        delay.exec();
        m_window->loadVideo(m_testMp4);
        m_window->loadVideo(m_chineseMp4);
        log(QStringLiteral("正在等待最终结果稳定…"));

        if (!waitUntilIdle(m_window, 15000)) {
            fail(name, QStringLiteral("等待超时，仍在处理"));
            return;
        }

        QStringList errors;
        if (!sameFile(m_window->currentFilePath(), m_chineseMp4)) {
            errors << QStringLiteral("最终文件应为中文测试视频，实际为：%1")
                          .arg(QDir::toNativeSeparators(m_window->currentFilePath()));
        }
        if (!m_window->lastProbeOk())
            errors << QStringLiteral("最后一次读取视频信息未成功");
        if (!m_window->lastThumbOk())
            errors << QStringLiteral("最后一次缩略图未成功");

        if (errors.isEmpty())
            pass(name, QStringLiteral("最终稳定在中文测试视频"));
        else
            fail(name, errors.join(QStringLiteral("；")));
    }

    bool loadLongVideo(const QString &name)
    {
        if (!requireExists(name, m_longMp4))
            return false;
        m_window->loadVideo(m_longMp4);
        log(QStringLiteral("正在打开 long.mp4…"));
        if (!waitUntilIdle(m_window, 20000)) {
            fail(name, QStringLiteral("打开 long.mp4 超时"));
            return false;
        }
        if (!m_window->lastProbeOk()) {
            fail(name, QStringLiteral("long.mp4 信息读取失败"));
            return false;
        }
        return true;
    }

    void testConvertLongToMp4()
    {
        const QString name = QStringLiteral("转换 long.mp4 为 MP4");
        if (!loadLongVideo(name))
            return;

        const QString outDir = prepareOutDir(QStringLiteral("convert_mp4"));
        m_window->setConvertOutputDir(outDir);
        if (!m_window->setConvertFormat(QStringLiteral("mp4"))) {
            fail(name, QStringLiteral("无法选择 MP4 格式"));
            return;
        }
        m_window->setConvertQualityCrf(28);

        const QString expected = m_window->plannedOutputPath();
        log(QStringLiteral("开始：%1 → %2")
                .arg(name, QDir::toNativeSeparators(expected)));
        if (!m_window->startConversion()) {
            fail(name, QStringLiteral("未能开始转换"));
            return;
        }

        log(QStringLiteral("正在等待转换完成…"));
        if (!waitUntilIdle(m_window, 180000)) {
            m_window->cancelConversion();
            fail(name, QStringLiteral("转换超时"));
            return;
        }

        const QString dest = m_window->lastConvertOutputPath();
        const QString part = m_window->convertTempPath();
        QStringList errors;
        if (!m_window->lastConvertOk())
            errors << QStringLiteral("转换未成功");
        if (dest.isEmpty() || !QFileInfo::exists(dest))
            errors << QStringLiteral("找不到输出文件：%1").arg(QDir::toNativeSeparators(dest));
        else if (QFileInfo(dest).size() < 1024)
            errors << QStringLiteral("输出文件过小");
        if (!part.isEmpty() && QFileInfo::exists(part))
            errors << QStringLiteral("仍残留临时文件：%1").arg(QDir::toNativeSeparators(part));
        if (QFileInfo(dest).suffix().compare(QLatin1String("mp4"), Qt::CaseInsensitive) != 0)
            errors << QStringLiteral("输出不是 MP4");

        if (errors.isEmpty())
            pass(name, QDir::toNativeSeparators(dest));
        else
            fail(name, errors.join(QStringLiteral("；")));
    }

    void testCancelLeavesNoJunk()
    {
        const QString name = QStringLiteral("转换中取消 long.mp4，不留残缺文件");
        if (!loadLongVideo(name))
            return;

        const QString outDir = prepareOutDir(QStringLiteral("convert_cancel"));
        m_window->setConvertOutputDir(outDir);
        m_window->setConvertFormat(QStringLiteral("mp4"));
        m_window->setConvertQualityCrf(28);

        log(QStringLiteral("开始：%1").arg(name));
        if (!m_window->startConversion()) {
            fail(name, QStringLiteral("未能开始转换"));
            return;
        }

        const QString dest = m_window->lastConvertOutputPath();
        QString part = m_window->convertTempPath();
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < 1000) {
            part = m_window->convertTempPath();
            if (!part.isEmpty() && QFileInfo::exists(part))
                break;
            waitMs(40);
        }
        log(QStringLiteral("转换已启动，正在取消…"));
        m_window->cancelConversion();

        if (!waitUntilIdle(m_window, 15000)) {
            fail(name, QStringLiteral("取消后仍在处理"));
            return;
        }

        if (!filesGone({dest, part}, 2500)) {
            QStringList leftover;
            if (QFileInfo::exists(dest))
                leftover << QDir::toNativeSeparators(dest);
            if (!part.isEmpty() && QFileInfo::exists(part))
                leftover << QDir::toNativeSeparators(part);
            fail(name, QStringLiteral("取消后仍有残缺文件：%1").arg(leftover.join(QStringLiteral("；"))));
            return;
        }

        const QStringList stray = QDir(outDir).entryList(
            QStringList{QStringLiteral("*.part.*"), QStringLiteral("*_converted*"), QStringLiteral("*.part")},
            QDir::Files);
        if (!stray.isEmpty()) {
            fail(name, QStringLiteral("输出目录仍有残留：%1").arg(stray.join(QStringLiteral("，"))));
            return;
        }
        if (m_window->isConverting() || m_window->lastConvertOk()) {
            fail(name, QStringLiteral("取消后状态不正确"));
            return;
        }

        pass(name, QStringLiteral("已取消且无残缺文件"));
    }

    bool runFfmpeg(const QStringList &args, int timeoutMs)
    {
        QProcess process;
        process.start(m_ffmpeg, args);
        if (!process.waitForStarted(5000)) {
            log(QStringLiteral("无法启动 ffmpeg：%1").arg(m_ffmpeg));
            return false;
        }
        if (!process.waitForFinished(timeoutMs)) {
            process.kill();
            process.waitForFinished(500);
            log(QStringLiteral("ffmpeg 生成素材超时"));
            return false;
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            const QString err = QString::fromUtf8(process.readAllStandardError()).trimmed();
            log(QStringLiteral("ffmpeg 生成失败：%1").arg(err.right(200)));
            return false;
        }
        return true;
    }

    bool generateValidMp4(const QString &path, int seconds, int width, int height)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        const QString lavfi = QStringLiteral("testsrc2=size=%1x%2:rate=25:duration=%3")
                                  .arg(width)
                                  .arg(height)
                                  .arg(seconds);
        QStringList args;
        args << QStringLiteral("-y")
             << QStringLiteral("-hide_banner")
             << QStringLiteral("-loglevel") << QStringLiteral("error")
             << QStringLiteral("-f") << QStringLiteral("lavfi")
             << QStringLiteral("-i") << lavfi
             << QStringLiteral("-c:v") << QStringLiteral("libx264")
             << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
             << QStringLiteral("-preset") << QStringLiteral("ultrafast")
             << QStringLiteral("-crf") << QStringLiteral("28")
             << path;
        return runFfmpeg(args, 60000) && QFileInfo::exists(path) && QFileInfo(path).size() > 512;
    }

    bool generateBrokenMp4(const QString &path)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        out.write(QStringLiteral("这不是视频文件，只是纯文本冒充的损坏视频。\n").toUtf8());
        out.write("This is not a video file.\n");
        out.close();
        return QFileInfo::exists(path) && QFileInfo(path).size() > 0;
    }

    bool generateAsset(const QString &title, const QString &path, bool broken)
    {
        log(QStringLiteral("正在生成 %1 …").arg(title));
        const bool ok = broken ? generateBrokenMp4(path)
                               : generateValidMp4(path,
                                                  title.contains(QLatin1String("long")) ? 20 : 2,
                                                  title.contains(QLatin1String("long")) ? 1280 : 640,
                                                  title.contains(QLatin1String("long")) ? 720 : 360);
        if (!ok) {
            log(QStringLiteral("生成失败：%1").arg(QDir::toNativeSeparators(path)));
            return false;
        }
        log(QStringLiteral("已生成：%1").arg(QDir::toNativeSeparators(path)));
        return true;
    }

    bool ensureAssets()
    {
        m_ffmpeg = m_window->ffmpegToolPath();
        m_assetsDir = QDir(selfTestTempRoot()).filePath(QStringLiteral("assets"));
        QDir().mkpath(m_assetsDir);

        m_testMp4 = QDir(m_assetsDir).filePath(QStringLiteral("test.mp4"));
        m_brokenMp4 = QDir(m_assetsDir).filePath(QStringLiteral("损坏视频.mp4"));
        m_chineseMp4 = QDir(m_assetsDir).filePath(QStringLiteral("中文 测试 视频.mp4"));
        m_longMp4 = QDir(m_assetsDir).filePath(QStringLiteral("long.mp4"));

        bool ok = true;
        ok = generateAsset(QStringLiteral("test.mp4"), m_testMp4, false) && ok;
        ok = generateAsset(QStringLiteral("损坏视频.mp4"), m_brokenMp4, true) && ok;
        ok = generateAsset(QStringLiteral("中文 测试 视频.mp4"), m_chineseMp4, false) && ok;
        ok = generateAsset(QStringLiteral("long.mp4"), m_longMp4, false) && ok;
        return ok;
    }

    MainWindow *m_window = nullptr;
    QString m_ffmpeg;
    QString m_assetsDir;
    QString m_testMp4;
    QString m_brokenMp4;
    QString m_chineseMp4;
    QString m_longMp4;
    int m_passed = 0;
    int m_failed = 0;
    QStringList m_lines;
};

ISelfTest *createSelfTest()
{
    return new SelfTest;
}
