// SPDX-License-Identifier: GPL-2.0-or-later
#include "include/ui/core/AsyncBackendBridge.hpp"
#include "include/api/RPC.h"
#include "include/global/Configs.hpp"

#include <QThreadPool>
#include <QtConcurrent>

AsyncBackendBridge *AsyncBackendBridge::instance() {
    static AsyncBackendBridge s_instance;
    return &s_instance;
}

AsyncBackendBridge::AsyncBackendBridge(QObject *parent)
    : QObject(parent) {}

// Helper: run a lambda on QThreadPool, invoke `onResult` on main thread via signal.
template <typename Func>
void AsyncBackendBridge::runAsync(Func &&func) {
    QtConcurrent::run(QThreadPool::globalInstance(), std::forward<Func>(func));
}

// ---------------------------------------------------------------------------
// Core lifecycle
// ---------------------------------------------------------------------------

void AsyncBackendBridge::startCore(const libcore::LoadConfigReq &req) {
    auto reqCopy = req; // capture by value for thread safety
    QtConcurrent::run(QThreadPool::globalInstance(), [this, reqCopy]() {
        QString err = API::defaultClient->Start(reqCopy);
        if (err.isEmpty())
            emit coreStarted();
        else
            emit coreStartFailed(err);
    });
}

void AsyncBackendBridge::stopCore() {
    QtConcurrent::run(QThreadPool::globalInstance(), [this]() {
        QString err = API::defaultClient->Stop();
        if (err.isEmpty())
            emit coreStopped();
        else
            emit backendError(QStringLiteral("stopCore"), err);
    });
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

void AsyncBackendBridge::queryStats() {
    QtConcurrent::run(QThreadPool::globalInstance(), [this]() {
        auto resp = API::defaultClient->QueryStats();
        emit statsReady(resp);
    });
}

void AsyncBackendBridge::queryConnections() {
    QtConcurrent::run(QThreadPool::globalInstance(), [this]() {
        auto resp = API::defaultClient->ListConnections();
        emit connectionsReady(resp);
    });
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void AsyncBackendBridge::runLatencyTest(const libcore::TestReq &req) {
    auto reqCopy = req;
    QtConcurrent::run(QThreadPool::globalInstance(), [this, reqCopy]() {
        auto resp = API::defaultClient->Test(reqCopy);
        emit latencyTestDone(resp);
    });
}

void AsyncBackendBridge::stopTests() {
    QtConcurrent::run(QThreadPool::globalInstance(), [this]() {
        API::defaultClient->StopTests();
    });
}

void AsyncBackendBridge::runSpeedTest(const libcore::SpeedTestRequest &req) {
    auto reqCopy = req;
    QtConcurrent::run(QThreadPool::globalInstance(), [this, reqCopy]() {
        auto resp = API::defaultClient->SpeedTest(reqCopy);
        emit speedTestDone(resp);
    });
}

void AsyncBackendBridge::querySpeedTestResults() {
    QtConcurrent::run(QThreadPool::globalInstance(), [this]() {
        auto resp = API::defaultClient->QueryCurrentSpeedTests();
        emit speedTestProgress(resp);
    });
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

void AsyncBackendBridge::checkConfig(const QString &config) {
    QString cfgCopy = config;
    QtConcurrent::run(QThreadPool::globalInstance(), [this, cfgCopy]() {
        QString err = API::defaultClient->CheckConfig(cfgCopy);
        emit configCheckResult(err);
    });
}

// ---------------------------------------------------------------------------
// DNS
// ---------------------------------------------------------------------------

void AsyncBackendBridge::setSystemDNS(bool clear) {
    QtConcurrent::run(QThreadPool::globalInstance(), [this, clear]() {
        QString err = API::defaultClient->SetSystemDNS(clear);
        emit systemDNSSet(err);
    });
}
