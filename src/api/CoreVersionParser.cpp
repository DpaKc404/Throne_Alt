// SPDX-License-Identifier: GPL-2.0-or-later
// ═══════════════════════════════════════════════════════════════════════════════
// src/api/CoreVersionParser.cpp — Implementation
// ═══════════════════════════════════════════════════════════════════════════════
#include "include/api/CoreVersionParser.hpp"
#include "include/global/Configs.hpp"

#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QRegularExpression>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent>

// ─── Private implementation ──────────────────────────────────────────────────
class CoreVersionParser::Impl {
public:
    mutable QMutex mutex;
    CoreVersionInfo cached;
    QTimer *periodicTimer = nullptr;
};

// ─── Singleton ───────────────────────────────────────────────────────────────
CoreVersionParser *CoreVersionParser::instance() {
    static CoreVersionParser s_instance;
    return &s_instance;
}

CoreVersionParser::CoreVersionParser(QObject *parent)
    : QObject(parent), d(new Impl) {}

CoreVersionInfo CoreVersionParser::cachedInfo() const {
    QMutexLocker lk(&d->mutex);
    return d->cached;
}

// ─── Async version query ─────────────────────────────────────────────────────
// Spawns the core binary with "--version" in a thread-pool worker.
// Parses stdout with regex, updates cache, emits on main thread.
void CoreVersionParser::requestVersions() {
    (void) QtConcurrent::run([this] {
        CoreVersionInfo info;

        auto corePath = Configs::FindCoreRealPath();
        if (corePath.isEmpty()) {
            QMetaObject::invokeMethod(this, [this] {
                emit parseError(QStringLiteral("Core binary not found on disk"));
            });
            return;
        }

        // ── sing-box version ─────────────────────────────────────────────
        {
            QProcess proc;
            proc.setProgram(corePath);
            proc.setArguments({QStringLiteral("--version")});
            proc.start(QIODevice::ReadOnly);
            // 5-second guard — core should respond instantly
            if (proc.waitForFinished(5000)) {
                auto output = QString::fromUtf8(proc.readAllStandardOutput());
                // Example: "sing-box: 1.13.2" or "sing-box version 1.13.2"
                static const QRegularExpression rxSingbox(
                    QStringLiteral(R"(sing-box[:\s]+(?:version\s+)?([\d.]+(?:-\w+)?))")
                );
                auto m = rxSingbox.match(output);
                if (m.hasMatch()) {
                    info.singboxVersion = m.captured(1);
                    info.singboxAvailable = true;
                    info.singboxStatus = QStringLiteral("stopped"); // binary exists
                }

                // Example: "Xray-core: 1.251208.0" or "Xray 1.251208.0"
                static const QRegularExpression rxXray(
                    QStringLiteral(R"(Xray(?:-core)?[:\s]+([\d.]+))")
                );
                auto mx = rxXray.match(output);
                if (mx.hasMatch()) {
                    info.xrayVersion = mx.captured(1);
                    info.xrayAvailable = true;
                    info.xrayStatus = QStringLiteral("stopped");
                }
            } else {
                info.singboxStatus = QStringLiteral("error");
                info.xrayStatus = QStringLiteral("error");
            }
        }

        // ── Detect running status via the RPC port ───────────────────────
        // If the core process is currently managed by us, DataStore tracks it.
        // We check the atomic flag rather than probing the port, to avoid
        // side-effects.
        if (Configs::dataStore && Configs::dataStore->core_running) {
            if (info.singboxAvailable)
                info.singboxStatus = QStringLiteral("running");
            if (info.xrayAvailable)
                info.xrayStatus = QStringLiteral("running");
        }

        // ── Update cache & emit ──────────────────────────────────────────
        {
            QMutexLocker lk(&d->mutex);
            d->cached = info;
        }

        QMetaObject::invokeMethod(this, [this, info] {
            emit versionParsed(info);
        });
    });
}

// ─── Periodic polling ────────────────────────────────────────────────────────
void CoreVersionParser::startPeriodicCheck(int intervalMs) {
    if (!d->periodicTimer) {
        d->periodicTimer = new QTimer(this);
        connect(d->periodicTimer, &QTimer::timeout, this, &CoreVersionParser::requestVersions);
    }
    d->periodicTimer->start(intervalMs);
    requestVersions(); // immediate first query
}

void CoreVersionParser::stopPeriodicCheck() {
    if (d->periodicTimer)
        d->periodicTimer->stop();
}
