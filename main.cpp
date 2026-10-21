#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QLocale>
#include <QSettings>
#include <QTranslator>
#include <QLibraryInfo>
#include <QDir>
#include <QDebug>

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

namespace {

// "system" — автоопределение, иначе "en" / "ru"
constexpr const char *kSettingsLanguage = "language";

// Возвращает эффективный код языка ("en" / "ru") с учётом "system".
QString resolveLanguage()
{
    QSettings s;
    QString lang = s.value(kSettingsLanguage, "system").toString();
    if (lang == "en" || lang == "ru")
        return lang;
    const QString sys = QLocale::system().name().left(2).toLower();
    return (sys == "ru") ? "ru" : "en";
}

// Пытается загрузить первый существующий .qm из списка кандидатов.
bool tryLoad(QTranslator *t, const QStringList &candidates)
{
    for (const QString &c : candidates) {
        if (t->load(c)) {
            qInfo() << "Loaded translation:" << c;
            return true;
        }
    }
    return false;
}

void installTranslators(QApplication &app, const QString &lang)
{
    // ---- 1) Переводы самого приложения (встроены embed_translations) ----
    // Реальный ресурс — ":/i18n/<basename>.qm".
    // basename берётся из имени .ts. Для совместимости пробуем несколько
    // вариантов: и <lang>, и <lang>_<COUNTRY>.
    auto *appTr = new QTranslator(&app);
    const QStringList appCandidates = {
        QStringLiteral(":/i18n/KiCadFinder_%1.qm").arg(lang),
        QStringLiteral(":/i18n/KiCadFinder_%1_RU.qm").arg(lang),
        QStringLiteral(":/i18n/KiCadFinder_%1_ru.qm").arg(lang),
        QStringLiteral(":/i18n/KiCadFinder_%1_en.qm").arg(lang),
    };
    if (tryLoad(appTr, appCandidates)) {
        app.installTranslator(appTr);
    } else {
        qWarning() << "Application translation not found for lang =" << lang
                   << "; tried:" << appCandidates;
        delete appTr;
    }

    // ---- 2) Стандартные диалоги Qt (OK/Cancel/...) — qtbase_<lang>.qm ----
    auto *qtTr = new QTranslator(&app);
    const QString qtFile = QStringLiteral("qtbase_%1.qm").arg(lang);
    const QStringList qtDirs = {
        QCoreApplication::applicationDirPath() + "/translations",   // ← обычно здесь (windeployqt)
        QCoreApplication::applicationDirPath(),
        QLibraryInfo::path(QLibraryInfo::TranslationsPath)
    };
    bool loaded = false;
    for (const QString &d : qtDirs) {
        if (qtTr->load(qtFile, d)) {
            loaded = true;
            qInfo() << "Loaded Qt translation:" << qtFile << "from" << d;
            break;
        }
    }
    if (loaded)
        app.installTranslator(qtTr);
    else
        delete qtTr;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QCoreApplication::setOrganizationName("KiCadFinder");
    QCoreApplication::setApplicationName("KiCadFinder");
    QCoreApplication::setApplicationVersion(APP_VERSION);

    a.setWindowIcon(QIcon(":/icon.ico"));

    installTranslators(a, resolveLanguage());

    MainWindow w;
    w.show();
    return a.exec();
}
