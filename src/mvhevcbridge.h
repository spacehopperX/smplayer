// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef MVHEVCBRIDGE_H
#define MVHEVCBRIDGE_H

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QObject>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <functional>
#include <memory>

struct MvHevcProbe {
    bool isMvHevc = false;
    int leftView = 1;
    int rightView = 0;
    double durationSec = 0.0;
    QString summary;
};

class MvHevcBridge {
public:
    MvHevcBridge() = default;
    ~MvHevcBridge() { stop(); }

    static QString findExecutable(const QString &configuredFfmpeg, const QString &toolName) {
#ifdef Q_OS_WIN
        const QString exeName = toolName.endsWith(".exe", Qt::CaseInsensitive) ? toolName : (toolName + ".exe");
#else
        const QString exeName = toolName;
#endif
        const QString trimmed = configuredFfmpeg.trimmed();
        if (!trimmed.isEmpty()) {
            const QFileInfo fi(trimmed);
            if (toolName.startsWith("ffmpeg", Qt::CaseInsensitive) && fi.exists() && fi.isFile()) {
                return fi.absoluteFilePath();
            }
            const QString sibling = QDir(fi.absolutePath()).filePath(exeName);
            if (QFileInfo::exists(sibling)) return sibling;
        }

        const QString appDir = QCoreApplication::applicationDirPath();
        for (const QString &dir : QStringList{appDir, appDir + "/ffmpeg", appDir + "/bin"}) {
            const QString candidate = QDir(dir).filePath(exeName);
            if (QFileInfo::exists(candidate)) return candidate;
        }

        const QString onPath = QStandardPaths::findExecutable(toolName);
        if (!onPath.isEmpty()) return onPath;
        return QString();
    }

    static MvHevcProbe probe(const QString &filePath, const QString &configuredFfmpeg) {
        MvHevcProbe result;
        const QFileInfo fi(filePath);
        if (!fi.exists() || !fi.isFile()) return result;

        const QString ext = fi.suffix().toLower();
        if (ext != "mov" && ext != "mp4" && ext != "m4v" && ext != "mkv") return result;

        const QString ffprobe = findExecutable(configuredFfmpeg, "ffprobe");
        if (!ffprobe.isEmpty()) {
            QProcess p;
            p.start(ffprobe, QStringList{
                "-v", "error",
                "-select_streams", "v:0",
                "-show_entries", "stream_side_data=:format=duration",
                "-of", "default=nw=1",
                filePath
            });
            if (p.waitForFinished(3000) && p.exitCode() == 0) {
                const QString out = QString::fromUtf8(p.readAllStandardOutput());
                QStringList viewIds;
                QStringList viewPos;
                for (const QString &rawLine : out.split('\n')) {
                    const QString line = rawLine.trimmed();
                    if (line.startsWith("view_ids_available=")) {
                        viewIds = line.mid(QStringLiteral("view_ids_available=").size()).split(',', Qt::SkipEmptyParts);
                    } else if (line.startsWith("view_pos_available=")) {
                        viewPos = line.mid(QStringLiteral("view_pos_available=").size()).split(',', Qt::SkipEmptyParts);
                    } else if (line.startsWith("duration=")) {
                        bool ok = false;
                        const double dur = line.mid(QStringLiteral("duration=").size()).trimmed().toDouble(&ok);
                        if (ok && dur > 0.0) result.durationSec = dur;
                    }
                }
                if (viewIds.size() >= 2) {
                    result.isMvHevc = true;
                    result.leftView = viewIds[0].toInt();
                    result.rightView = viewIds[1].toInt();
                    if (viewPos.size() >= 2) {
                        for (int i = 0; i < viewIds.size() && i < viewPos.size(); ++i) {
                            const QString pos = viewPos[i].trimmed().toLower();
                            if (pos == "1" || pos == "left") result.leftView = viewIds[i].toInt();
                            else if (pos == "2" || pos == "right") result.rightView = viewIds[i].toInt();
                        }
                    }
                    result.summary = QString("Apple MV-HEVC (ffprobe views L=%1, R=%2 -> Full-TAB)")
                        .arg(result.leftView).arg(result.rightView);
                    return result;
                }
            }
        }

        if (ext != "mkv" && containsMvHevcAtoms(filePath)) {
            result.isMvHevc = true;
            result.leftView = 1;
            result.rightView = 0;
            result.summary = QStringLiteral("Apple MV-HEVC (MP4 lhvC/vexu -> Full-TAB)");
        }
        return result;
    }

    bool start(const QString &sourceFile,
               const MvHevcProbe &info,
               const QString &configuredFfmpeg,
               QString *errorOut = nullptr,
               std::function<void(const QString &)> onBridgeNote = nullptr,
               double startSec = 0.0) {
        stop();
        const QString ffmpeg = findExecutable(configuredFfmpeg, "ffmpeg");
        if (ffmpeg.isEmpty()) {
            if (errorOut) {
                *errorOut = QStringLiteral("FFmpeg executable not found. Install FFmpeg 9+ or set its path in 3D Output settings.");
            }
            return false;
        }

        QTcpServer probeServer;
        if (!probeServer.listen(QHostAddress::LocalHost, 0)) {
            if (errorOut) *errorOut = QStringLiteral("Failed to reserve a local TCP port for the MV-HEVC bridge.");
            return false;
        }
        const quint16 port = probeServer.serverPort();
        probeServer.close();

        const QString filter = QString("[0:v:view:%1][0:v:view:%2]vstack=inputs=2[v]")
            .arg(info.leftView).arg(info.rightView);
        const QString listenUrl = QString("tcp://127.0.0.1:%1?listen=1").arg(port);

        QStringList args;
        args << "-hide_banner"
             << "-loglevel" << "error"
             << "-nostdin";
        if (startSec > 0.0) {
            args << "-ss" << QString::number(startSec, 'f', 3);
        }
        args << "-i" << sourceFile
             << "-filter_complex" << filter
             << "-map" << "[v]"
             << "-map" << "0:a:0?"
             << "-c:v" << "rawvideo"
             << "-pix_fmt" << "yuv420p"
             << "-c:a" << "pcm_s16le";
        if (startSec > 0.0) {
            args << "-output_ts_offset" << QString::number(startSec, 'f', 3);
        }
        args << "-f" << "nut"
             << listenUrl;

        intentionalStop = false;
        stderrLog.clear();
        noteCallback = std::move(onBridgeNote);

        auto process = std::make_unique<QProcess>();
        process->setProcessChannelMode(QProcess::MergedChannels);
        QProcess *rawProc = process.get();
        QObject::connect(rawProc, &QProcess::readyReadStandardOutput, [this, rawProc]() {
            const QString chunk = QString::fromUtf8(rawProc->readAllStandardOutput()).trimmed();
            if (!chunk.isEmpty()) {
                if (!stderrLog.isEmpty()) stderrLog += '\n';
                stderrLog += chunk;
                if (stderrLog.size() > 1024) stderrLog = stderrLog.right(1024);
            }
        });
        QObject::connect(rawProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                         [this, rawProc](int exitCode, QProcess::ExitStatus status) {
            if (intentionalStop) return;
            const QString tail = QString::fromUtf8(rawProc->readAllStandardOutput()).trimmed();
            if (!tail.isEmpty()) {
                if (!stderrLog.isEmpty()) stderrLog += '\n';
                stderrLog += tail;
            }
            if ((exitCode != 0 || status != QProcess::NormalExit) && noteCallback) {
                const QString msg = stderrLog.isEmpty()
                    ? QString("FFmpeg exited early (code %1)").arg(exitCode)
                    : QString("FFmpeg exited (%1): %2").arg(exitCode).arg(stderrLog);
                noteCallback(msg);
            }
        });

        process->start(ffmpeg, args);
        if (!process->waitForStarted(3000)) {
            if (errorOut) *errorOut = QString("Failed to launch FFmpeg: %1").arg(process->errorString());
            return false;
        }

        proc = std::move(process);
        activeUrl = QString("tcp://127.0.0.1:%1").arg(port);
        activeSource = sourceFile;
        activeSummary = info.summary;
        activeProbe = info;
        activeSeekOffsetSec = startSec;
        return true;
    }

    void stop() {
        intentionalStop = true;
        if (proc) {
            if (proc->state() != QProcess::NotRunning) {
                proc->terminate();
                if (!proc->waitForFinished(800)) {
                    proc->kill();
                    proc->waitForFinished(500);
                }
            }
            proc.reset();
        }
        activeUrl.clear();
        activeSource.clear();
        activeSummary.clear();
        activeProbe = MvHevcProbe();
        activeSeekOffsetSec = 0.0;
    }

    bool isActive() const { return !activeUrl.isEmpty(); }
    bool isActiveUrl(const QString &url) const { return !activeUrl.isEmpty() && url == activeUrl; }
    QString url() const { return activeUrl; }
    QString sourceFile() const { return activeSource; }
    QString summary() const { return activeSummary; }
    double durationSec() const { return activeProbe.durationSec; }
    double seekOffsetSec() const { return activeSeekOffsetSec; }
    const MvHevcProbe &probeInfo() const { return activeProbe; }

private:
    static bool containsMvHevcAtoms(const QString &filePath) {
        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly)) return false;
        const qint64 chunkSize = 2 * 1024 * 1024;
        const QByteArray head = f.read(chunkSize);
        if (head.contains("lhvC") || head.contains("vexu")) return true;
        if (f.size() > chunkSize) {
            f.seek(qMax<qint64>(0, f.size() - chunkSize));
            const QByteArray tail = f.read(chunkSize);
            if (tail.contains("lhvC") || tail.contains("vexu")) return true;
        }
        return false;
    }

    std::unique_ptr<QProcess> proc;
    QString activeUrl;
    QString activeSource;
    QString activeSummary;
    MvHevcProbe activeProbe;
    double activeSeekOffsetSec = 0.0;
    QString stderrLog;
    std::function<void(const QString &)> noteCallback;
    bool intentionalStop = false;
};

#endif
