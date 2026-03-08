// SPDX-License-Identifier: GPL-2.0-or-later
#include "include/ui/core/TranslationManager.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QEvent>
#include <QLibraryInfo>
#include <QLocale>
#include <QWidget>
#include <QWindow>

// Static singleton
TranslationManager *TranslationManager::instance() {
    static TranslationManager s_instance;
    return &s_instance;
}

TranslationManager::TranslationManager(QObject *parent)
    : QObject(parent) {}

TranslationManager::~TranslationManager() {
    removeCurrentTranslator();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void TranslationManager::initialize() {
    // lang/ directory is located next to the application binary.
    m_langDir = QCoreApplication::applicationDirPath() + QStringLiteral("/lang");

    QDir dir(m_langDir);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    scanDirectory();

    // Watch the directory for .qm files added/removed at runtime.
    m_watcher = new QFileSystemWatcher(this);
    m_watcher->addPath(m_langDir);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &TranslationManager::rescanLanguages);
}

QString TranslationManager::translationsPath() const {
    return m_langDir;
}

QMap<QString, LanguageInfo> TranslationManager::availableLanguages() const {
    return m_languages;
}

QString TranslationManager::currentLocale() const {
    return m_currentLocale;
}

bool TranslationManager::switchLanguage(const QString &locale) {
    if (locale == m_currentLocale)
        return true;

    removeCurrentTranslator();

    if (locale.isEmpty()) {
        // English / no translation — just remove translators.
        m_currentLocale.clear();
        QLocale::setDefault(QLocale(QLocale::English));
        emit languageChanged(m_currentLocale);
        // Broadcast LanguageChange to all top-level widgets.
        for (QWidget *w : QApplication::topLevelWidgets()) {
            QEvent ev(QEvent::LanguageChange);
            QCoreApplication::sendEvent(w, &ev);
        }
        return true;
    }

    auto it = m_languages.find(locale);
    if (it == m_languages.end())
        return false;

    installTranslator(it->filePath);
    m_currentLocale = locale;
    QLocale::setDefault(QLocale(locale));

    emit languageChanged(m_currentLocale);

    // Propagate LanguageChange event to all open widgets so they retranslate.
    for (QWidget *w : QApplication::topLevelWidgets()) {
        QEvent ev(QEvent::LanguageChange);
        QCoreApplication::sendEvent(w, &ev);
    }
    return true;
}

void TranslationManager::rescanLanguages() {
    const auto oldKeys = m_languages.keys();
    scanDirectory();
    if (oldKeys != m_languages.keys()) {
        emit availableLanguagesChanged();
    }
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void TranslationManager::scanDirectory() {
    m_languages.clear();

    QDir dir(m_langDir);
    if (!dir.exists())
        return;

    // Enumerate all *.qm files. Expected naming: <locale>.qm  (e.g. ru_RU.qm)
    const QStringList filters{QStringLiteral("*.qm")};
    QDirIterator it(m_langDir, filters, QDir::Files);
    while (it.hasNext()) {
        it.next();
        const QString baseName = it.fileInfo().completeBaseName(); // "ru_RU"
        LanguageInfo info;
        info.locale = baseName;
        info.displayName = displayNameForLocale(baseName);
        info.filePath = it.filePath();
        m_languages.insert(baseName, info);
    }
}

void TranslationManager::installTranslator(const QString &qmPath) {
    m_appTranslator = new QTranslator(this);
    if (m_appTranslator->load(qmPath)) {
        QCoreApplication::installTranslator(m_appTranslator);
    } else {
        delete m_appTranslator;
        m_appTranslator = nullptr;
    }

    // Also try to load Qt's own translation for standard dialogs.
    const QString qtTransDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    m_qtTranslator = new QTranslator(this);
    const QString locale = QFileInfo(qmPath).completeBaseName();
    if (m_qtTranslator->load(QStringLiteral("qtbase_") + locale, qtTransDir)) {
        QCoreApplication::installTranslator(m_qtTranslator);
    } else {
        delete m_qtTranslator;
        m_qtTranslator = nullptr;
    }
}

void TranslationManager::removeCurrentTranslator() {
    if (m_appTranslator) {
        QCoreApplication::removeTranslator(m_appTranslator);
        delete m_appTranslator;
        m_appTranslator = nullptr;
    }
    if (m_qtTranslator) {
        QCoreApplication::removeTranslator(m_qtTranslator);
        delete m_qtTranslator;
        m_qtTranslator = nullptr;
    }
}

QString TranslationManager::displayNameForLocale(const QString &locale) {
    QLocale loc(locale);
    // "Русский", "中文 (简体)", etc. — capitalize the native name.
    QString name = loc.nativeLanguageName();
    if (name.isEmpty())
        name = locale;
    else
        name[0] = name[0].toUpper();
    return name;
}
