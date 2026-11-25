// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef MVCBLURAYBRIDGE_H
#define MVCBLURAYBRIDGE_H

#include "mvhevcbridge.h"
#include <QCoreApplication>
#include <QDir>
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

struct MvcBlurayProbe {
    bool isMvc = false;
    bool nativeFfmpegMultiview = false;
    bool hasSubtitles = false;
    int leftView = 0;
    int rightView = 1;
    double durationSec = 0.0;
    QString fps = QStringLiteral("24000/1001");
    QString mediaPath;
    QString mountedIsoPath;
    QString summary;
};

class MvcBlurayBridge {
public:
    MvcBlurayBridge() = default;
    ~MvcBlurayBridge() { stop(); }

    static QString findFrimExecutable(const QString &configuredPath) {
        const QString trimmed = configuredPath.trimmed();
        if (!trimmed.isEmpty() && QFileInfo::exists(trimmed) && QFileInfo(trimmed).isFile()) {
            return QFileInfo(trimmed).absoluteFilePath();
        }
        const QStringList names = {
#ifdef Q_OS_WIN
            QStringLiteral("FRIMDecode64.exe"),
            QStringLiteral("FRIMDecode.exe")
#else
            QStringLiteral("FRIMDecode64"),
            QStringLiteral("FRIMDecode")
#endif
        };
        const QString appDir = QCoreApplication::applicationDirPath();
        for (const QString &dir : QStringList{appDir, appDir + "/frim", appDir + "/bin"}) {
            for (const QString &name : names) {
                const QString candidate = QDir(dir).filePath(name);
                if (QFileInfo::exists(candidate)) return candidate;
            }
        }
        for (const QString &name : names) {
            const QString onPath = QStandardPaths::findExecutable(name);
            if (!onPath.isEmpty()) return onPath;
        }
        return QString();
    }

    static bool isCandidateFile(const QString &filePath) {
        const QFileInfo fi(filePath);
        if (!fi.exists() || !fi.isFile()) return false;
        const QString ext = fi.suffix().toLower();
        return ext == "iso" || ext == "ssif" || ext == "mk3d" || ext == "mkv" || ext == "m2ts";
    }

    static MvcBlurayProbe probe(const QString &filePath, const QString &configuredFfmpeg) {
        MvcBlurayProbe res;
        if (!isCandidateFile(filePath)) return res;

        const QFileInfo fi(filePath);
        const QString ext = fi.suffix().toLower();
        QString targetMedia = fi.absoluteFilePath();

        if (ext == "iso") {
#ifdef Q_OS_WIN
            const QString driveLetter = mountIso(fi.absoluteFilePath());
            if (driveLetter.isEmpty()) return res;
            const QString largestSsif = findLargestSsif(driveLetter + ":/BDMV/STREAM/SSIF");
            if (largestSsif.isEmpty()) {
                dismountIso(fi.absoluteFilePath());
                return res;
            }
            res.isMvc = true;
            res.mountedIsoPath = fi.absoluteFilePath();
            res.mediaPath = largestSsif;
            targetMedia = largestSsif;
            res.summary = QString("Blu-ray 3D ISO (%1 -> Full-TAB)").arg(QFileInfo(largestSsif).fileName());
#else
            return res;
#endif
        } else if (ext == "ssif") {
            res.isMvc = true;
            res.mediaPath = targetMedia;
            res.summary = QStringLiteral("Blu-ray 3D SSIF (H.264 MVC -> Full-TAB)");
        } else {
            res.mediaPath = targetMedia;
        }

        const QString ffprobe = MvHevcBridge::findExecutable(configuredFfmpeg, "ffprobe");
        if (!ffprobe.isEmpty()) {
            QProcess p;
            p.start(ffprobe, QStringList{
                "-v", "error",
                "-show_entries", "stream=codec_type,codec_name,profile,r_frame_rate:stream_side_data=:format=duration",
                "-of", "default=nw=1",
                targetMedia
            });
            if (p.waitForFinished(5000) && p.exitCode() == 0) {
                const QString out = QString::fromUtf8(p.readAllStandardOutput());
                QStringList viewIds;
                QStringList viewPos;
                bool hasStereoProfile = false;
                QString firstFps;

                for (const QString &rawLine : out.split('\n')) {
                    const QString line = rawLine.trimmed();
                    if (line == "codec_type=subtitle") {
                        res.hasSubtitles = true;
                    } else if (line.startsWith("view_ids_available=")) {
                        viewIds = line.mid(QStringLiteral("view_ids_available=").size()).split(',', Qt::SkipEmptyParts);
                    } else if (line.startsWith("view_pos_available=")) {
                        viewPos = line.mid(QStringLiteral("view_pos_available=").size()).split(',', Qt::SkipEmptyParts);
                    } else if (line.startsWith("profile=")) {
                        const QString prof = line.mid(QStringLiteral("profile=").size()).toLower();
                        if (prof.contains("stereo high") || prof.contains("multiview high") || prof.contains("mvc")) {
                            hasStereoProfile = true;
                        }
                    } else if (line.startsWith("r_frame_rate=") && firstFps.isEmpty()) {
                        const QString rate = line.mid(QStringLiteral("r_frame_rate=").size()).trimmed();
                        if (!rate.isEmpty() && rate != "0/0") firstFps = rate;
                    } else if (line.startsWith("duration=") && res.durationSec <= 0.0) {
                        bool ok = false;
                        const double dur = line.mid(QStringLiteral("duration=").size()).trimmed().toDouble(&ok);
                        if (ok && dur > 0.0) res.durationSec = dur;
                    }
                }

                if (!firstFps.isEmpty()) res.fps = firstFps;

                if (viewIds.size() >= 2) {
                    res.isMvc = true;
                    res.nativeFfmpegMultiview = true;
                    res.leftView = viewIds[0].toInt();
                    res.rightView = viewIds[1].toInt();
                    if (viewPos.size() >= 2) {
                        for (int i = 0; i < viewIds.size() && i < viewPos.size(); ++i) {
                            const QString pos = viewPos[i].trimmed().toLower();
                            if (pos == "1" || pos == "left") res.leftView = viewIds[i].toInt();
                            else if (pos == "2" || pos == "right") res.rightView = viewIds[i].toInt();
                        }
                    }
                    res.summary = QString("H.264 MVC (FFmpeg multiview L=%1, R=%2 -> Full-TAB)")
                        .arg(res.leftView).arg(res.rightView);
                } else if (hasStereoProfile || ext == "mk3d") {
                    res.isMvc = true;
                    if (res.summary.isEmpty()) {
                        res.summary = QStringLiteral("H.264 MVC (FRIMDecode64 -> Full-TAB)");
                    }
                }
            }
        } else if (ext == "mk3d") {
            res.isMvc = true;
            res.summary = QStringLiteral("H.264 MVC MK3D (FRIMDecode64 -> Full-TAB)");
        }

        if (!res.isMvc && !res.mountedIsoPath.isEmpty()) {
            dismountIso(res.mountedIsoPath);
            res.mountedIsoPath.clear();
        }
        return res;
    }

    bool start(const MvcBlurayProbe &info,
               const QString &configuredFfmpeg,
               const QString &configuredFrim,
               bool burnStereoPgs = true,
               int pgsDepthPx = 8,
               bool swapEyes = false,
               QString *errorOut = nullptr,
               std::function<void(const QString &)> onBridgeNote = nullptr,
               double startSec = 0.0) {
        const bool keepExistingMount = (!info.mountedIsoPath.isEmpty() && info.mountedIsoPath == mountedIso);
        stop(keepExistingMount);
        mountedIso = info.mountedIsoPath;

        const QString ffmpeg = MvHevcBridge::findExecutable(configuredFfmpeg, "ffmpeg");
        if (ffmpeg.isEmpty()) {
            if (errorOut) *errorOut = QStringLiteral("FFmpeg executable not found (required for H.264 MVC / Blu-ray 3D bridge).");
            stop(keepExistingMount);
            return false;
        }

        QTcpServer probeServer;
        if (!probeServer.listen(QHostAddress::LocalHost, 0)) {
            if (errorOut) *errorOut = QStringLiteral("Failed to reserve a local TCP port for the H.264 MVC bridge.");
            stop(keepExistingMount);
            return false;
        }
        const quint16 port = probeServer.serverPort();
        probeServer.close();

        const QString listenUrl = QString("tcp://127.0.0.1:%1?listen=1").arg(port);
        const bool overlayPgs = (burnStereoPgs && info.hasSubtitles);
        const int halfShift = (swapEyes ? -pgsDepthPx : pgsDepthPx) / 2;

        intentionalStop = false;
        stderrLog.clear();
        noteCallback = std::move(onBridgeNote);

        if (info.nativeFfmpegMultiview) {
            QString filter;
            if (overlayPgs) {
                filter = QString(
                    "[0:s:0]split=2[subL][subR];"
                    "[0:v:view:%1][subL]overlay=x=%3:y=0:shortest=0[L];"
                    "[0:v:view:%2][subR]overlay=x=%4:y=0:shortest=0[R];"
                    "[L][R]vstack=inputs=2[v]")
                    .arg(info.leftView).arg(info.rightView)
                    .arg(halfShift).arg(-halfShift);
            } else {
                filter = QString("[0:v:view:%1][0:v:view:%2]vstack=inputs=2[v]")
                    .arg(info.leftView).arg(info.rightView);
            }

            QStringList args;
            args << "-hide_banner" << "-loglevel" << "error" << "-nostdin";
            if (startSec > 0.0) {
                args << "-ss" << QString::number(startSec, 'f', 3);
            }
            args << "-i" << info.mediaPath
                 << "-filter_complex" << filter
                 << "-map" << "[v]"
                 << "-map" << "0:a:0?"
                 << "-c:v" << "rawvideo" << "-pix_fmt" << "yuv420p"
                 << "-c:a" << "pcm_s16le"
                 << "-f" << "nut"
                 << listenUrl;

            auto mux = std::make_unique<QProcess>();
            attachStderrLogger(mux.get(), "FFmpeg MVC");
            mux->start(ffmpeg, args);
            if (!mux->waitForStarted(3000)) {
                if (errorOut) *errorOut = QString("Failed to launch FFmpeg MVC bridge: %1").arg(mux->errorString());
                stop(keepExistingMount);
                return false;
            }
            muxProc = std::move(mux);
        } else {
            const QString frim = findFrimExecutable(configuredFrim);
            if (frim.isEmpty()) {
                if (errorOut) {
                    *errorOut = QStringLiteral("FRIMDecode64.exe not found. Set FRIMDecode64 path in 3D Output settings (with libmfxsw64.dll) or use an FFmpeg build with native H.264 MVC multiview.");
                }
                stop(keepExistingMount);
                return false;
            }

            auto demux = std::make_unique<QProcess>();
            auto decoder = std::make_unique<QProcess>();
            auto mux = std::make_unique<QProcess>();

            demux->setStandardOutputProcess(decoder.get());
            decoder->setStandardOutputProcess(mux.get());
            attachStderrLogger(mux.get(), "FFmpeg MVC mux");

            QStringList demuxArgs;
            demuxArgs << "-hide_banner" << "-loglevel" << "error" << "-nostdin";
            if (startSec > 0.0) {
                demuxArgs << "-ss" << QString::number(startSec, 'f', 3);
            }
            demuxArgs << "-i" << info.mediaPath
                      << "-map" << "0:v:0"
                      << "-c:v" << "copy"
                      << "-bsf:v" << "h264_mp4toannexb"
                      << "-f" << "h264"
                      << "pipe:1";

            QStringList frimArgs;
            frimArgs << "-sw" << "-i:mvc" << "-" << "-tab" << "-o" << "-";

            QStringList muxArgs;
            muxArgs << "-hide_banner" << "-loglevel" << "error" << "-nostdin"
                    << "-f" << "rawvideo" << "-pix_fmt" << "yuv420p"
                    << "-s" << "1920x2160" << "-r" << info.fps
                    << "-i" << "pipe:0";
            if (startSec > 0.0) {
                muxArgs << "-ss" << QString::number(startSec, 'f', 3);
            }
            muxArgs << "-i" << info.mediaPath;

            if (overlayPgs) {
                const QString filter = QString(
                    "[0:v:0]split=2[vTop][vBot];"
                    "[vTop]crop=1920:1080:0:0[vL];"
                    "[vBot]crop=1920:1080:0:1080[vR];"
                    "[1:s:0]split=2[subL][subR];"
                    "[vL][subL]overlay=x=%1:y=0:shortest=0[L];"
                    "[vR][subR]overlay=x=%2:y=0:shortest=0[R];"
                    "[L][R]vstack=inputs=2[v]")
                    .arg(halfShift).arg(-halfShift);
                muxArgs << "-filter_complex" << filter
                        << "-map" << "[v]";
            } else {
                muxArgs << "-map" << "0:v:0";
            }

            muxArgs << "-map" << "1:a:0?"
                    << "-c:v" << "rawvideo" << "-pix_fmt" << "yuv420p"
                    << "-c:a" << "pcm_s16le"
                    << "-f" << "nut"
                    << listenUrl;

            mux->start(ffmpeg, muxArgs);
            decoder->start(frim, frimArgs);
            demux->start(ffmpeg, demuxArgs);

            if (!mux->waitForStarted(3000) || !decoder->waitForStarted(3000) || !demux->waitForStarted(3000)) {
                if (errorOut) *errorOut = QStringLiteral("Failed to launch the FRIMDecode64 / FFmpeg real-time MVC pipeline.");
                demuxProc = std::move(demux);
                frimProc = std::move(decoder);
                muxProc = std::move(mux);
                stop(keepExistingMount);
                return false;
            }

            demuxProc = std::move(demux);
            frimProc = std::move(decoder);
            muxProc = std::move(mux);
        }

        activeUrl = QString("tcp://127.0.0.1:%1").arg(port);
        activeSummary = overlayPgs
            ? (info.summary + QStringLiteral(" + 3D PGS subs"))
            : info.summary;
        activeProbe = info;
        activeSeekOffsetSec = startSec;
        return true;
    }

    void stop(bool keepMount = false) {
        intentionalStop = true;
        stopProcess(demuxProc);
        stopProcess(frimProc);
        stopProcess(muxProc);
        if (!keepMount && !mountedIso.isEmpty()) {
            dismountIso(mountedIso);
            mountedIso.clear();
        }
        activeUrl.clear();
        activeSummary.clear();
        if (!keepMount) {
            activeProbe = MvcBlurayProbe();
        }
        activeSeekOffsetSec = 0.0;
    }

    bool isActive() const { return !activeUrl.isEmpty(); }
    bool isActiveUrl(const QString &url) const { return !activeUrl.isEmpty() && url == activeUrl; }
    QString url() const { return activeUrl; }
    QString summary() const { return activeSummary; }
    double durationSec() const { return activeProbe.durationSec; }
    double seekOffsetSec() const { return activeSeekOffsetSec; }
    const MvcBlurayProbe &probeInfo() const { return activeProbe; }

private:
    void attachStderrLogger(QProcess *rawProc, const QString &label) {
        if (!rawProc) return;
        rawProc->setProcessChannelMode(QProcess::MergedChannels);
        QObject::connect(rawProc, &QProcess::readyReadStandardOutput, [this, rawProc]() {
            const QString chunk = QString::fromUtf8(rawProc->readAllStandardOutput()).trimmed();
            if (!chunk.isEmpty()) {
                if (!stderrLog.isEmpty()) stderrLog += '\n';
                stderrLog += chunk;
                if (stderrLog.size() > 1024) stderrLog = stderrLog.right(1024);
            }
        });
        QObject::connect(rawProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                         [this, rawProc, label](int exitCode, QProcess::ExitStatus status) {
            if (intentionalStop) return;
            const QString tail = QString::fromUtf8(rawProc->readAllStandardOutput()).trimmed();
            if (!tail.isEmpty()) {
                if (!stderrLog.isEmpty()) stderrLog += '\n';
                stderrLog += tail;
            }
            if ((exitCode != 0 || status != QProcess::NormalExit) && noteCallback) {
                const QString msg = stderrLog.isEmpty()
                    ? QString("%1 exited early (code %2)").arg(label).arg(exitCode)
                    : QString("%1 exited (%2): %3").arg(label).arg(exitCode).arg(stderrLog);
                noteCallback(msg);
            }
        });
    }

    static void stopProcess(std::unique_ptr<QProcess> &proc) {
        if (!proc) return;
        if (proc->state() != QProcess::NotRunning) {
            proc->terminate();
            if (!proc->waitForFinished(700)) {
                proc->kill();
                proc->waitForFinished(500);
            }
        }
        proc.reset();
    }

    static QString escapePsSingleQuote(QString s) {
        return s.replace("'", "''");
    }

    static QString mountIso(const QString &isoPath) {
#ifdef Q_OS_WIN
        const QString cmd = QString(
            "$img = Mount-DiskImage -ImagePath '%1' -PassThru; "
            "Start-Sleep -Milliseconds 300; "
            "($img | Get-Volume | Select-Object -First 1 -ExpandProperty DriveLetter)")
            .arg(escapePsSingleQuote(QDir::toNativeSeparators(isoPath)));
        QProcess p;
        p.start("powershell.exe", QStringList{"-NoProfile", "-NonInteractive", "-Command", cmd});
        if (!p.waitForFinished(8000) || p.exitCode() != 0) return QString();
        const QString letter = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        return (letter.size() == 1 && letter[0].isLetter()) ? letter : QString();
#else
        Q_UNUSED(isoPath);
        return QString();
#endif
    }

    static void dismountIso(const QString &isoPath) {
#ifdef Q_OS_WIN
        if (isoPath.isEmpty()) return;
        const QString cmd = QString("Dismount-DiskImage -ImagePath '%1' -ErrorAction SilentlyContinue")
            .arg(escapePsSingleQuote(QDir::toNativeSeparators(isoPath)));
        QProcess::execute("powershell.exe", QStringList{"-NoProfile", "-NonInteractive", "-Command", cmd});
#else
        Q_UNUSED(isoPath);
#endif
    }

    static QString findLargestSsif(const QString &ssifDirPath) {
        QDir dir(ssifDirPath);
        if (!dir.exists()) return QString();
        const QFileInfoList files = dir.entryInfoList(QStringList{"*.ssif", "*.SSIF"}, QDir::Files);
        QFileInfo best;
        for (const QFileInfo &fi : files) {
            if (!best.exists() || fi.size() > best.size()) best = fi;
        }
        return best.exists() ? best.absoluteFilePath() : QString();
    }

    std::unique_ptr<QProcess> demuxProc;
    std::unique_ptr<QProcess> frimProc;
    std::unique_ptr<QProcess> muxProc;
    QString mountedIso;
    QString activeUrl;
    QString activeSummary;
    MvcBlurayProbe activeProbe;
    double activeSeekOffsetSec = 0.0;
    QString stderrLog;
    std::function<void(const QString &)> noteCallback;
    bool intentionalStop = false;
};

#endif
