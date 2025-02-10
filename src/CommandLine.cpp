#include "CommandLine.h"

#ifndef DEBUG_MODE
#include <SessionLockQt/command.h>
#endif

#include <QDBusInterface>
#include <QDate>
#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QLocale>
#include <QSettings>
#include <QTimer>

using namespace Qt::StringLiterals;

CommandLine::CommandLine(QObject *parent)
  : QObject(parent)
  , m_currentDate(QLocale().toString(QDate::currentDate()))
  , m_userName(QString())
  , m_backgroundImagePath(QUrl("qrc:/image/gangdamu.png"))
  , m_opacity(0.6)
  , m_greetd(nullptr)
  , m_status(LoginStatus::Start)
  , m_userIcon(QUrl("qrc:/image/account.svg"))
  , m_command(QString())
  , m_isAuthing(false)
  , m_env(QStringList())
{
    QSettings setting;
    QString user = setting.value("user").toString();
    if (!user.isEmpty()) {
        setUserName(user);
    }
    connectToGreetd();
}

void
CommandLine::setPassword(const QString &password)
{
    m_password = password;
    Q_EMIT passwordChanged();
}

void
CommandLine::setUserName(const QString &userName)
{
    if (m_userName == userName) {
        return;
    }
    m_userName = userName;

    if (auto iconP = QString("/var/lib/AccountsService/icons/%1").arg(m_userName);
        QFile(iconP).exists()) {
        m_userIcon = QUrl::fromLocalFile(iconP);
    } else {
        m_userIcon = QUrl("qrc:/image/account.svg");
    }
    Q_EMIT userIconChanged();
    Q_EMIT userNameChanged();
}

void
CommandLine::setCommand(const QString &command)
{
    m_command = command;
    Q_EMIT commandChanged();
}

void
CommandLine::setEnv(const QStringList &env)
{
    m_env = env;
    Q_EMIT envChanged();
}

void
CommandLine::connectToGreetd()
{
    QString path = qgetenv("GREETD_SOCK");
    if (path.isEmpty()) {
        qWarning() << "Unable to retrieve GREETD_SOCK";
        m_errorMessage = "Cannot connect to greetd";
        Q_EMIT errorMessageChanged();
        return;
    }
    m_greetd = new QLocalSocket(this);
    m_greetd->connectToServer(path, QIODevice::ReadWrite | QIODevice::Unbuffered);
    m_greetd->waitForConnected();

    connect(m_greetd, &QLocalSocket::readyRead, this, &CommandLine::handleDataRead);
}

void
CommandLine::handleDataRead()
{
    QByteArray data = m_greetd->readAll();
    data.remove(0, 4);
    QJsonObject document = QJsonDocument::fromJson(data).object();
    QString authtype     = document["type"].toString();

    if (authtype == "auth_message") {
        QString auth_message_type = document["auth_message_type"].toString();
        if (auth_message_type == "secret") {
            handleAuthPasswordMessage();
            return;
        } else {
            // NOTE: I DO NOT CARE
            handleAuthMessageNone();
            return;
        }
    } else if (authtype == "error") {
        QString error_type  = document["error_type"].toString();
        QString description = document["description"].toString();
        m_errorMessage      = QString("%1: %2").arg(error_type).arg(description);
        Q_EMIT errorMessageChanged();
        handleAuthError();
        return;
    } else if (authtype == "success") {
        handleSuccessded();
    }
}

void
CommandLine::handleAuthMessageNone()
{
    QVariantMap request;

    request["type"] = "post_auth_message_response";
    QJsonDocument json;

    json.setObject(QJsonObject::fromVariantMap(request));
    roundtrip(json.toJson().simplified());
}

void
CommandLine::handleSuccessded()
{
    switch (m_status) {
    case Errored: {
        m_status = CancelSessionSuccessded;
        break;
    }
    case TryToLoginSession: {
        m_status = TryToStartSession;
        QVariantMap request;

        request["type"] = "start_session";
        request["cmd"]  = m_command.split(' ');
        request["env"]  = m_env;
        QJsonDocument json;

        json.setObject(QJsonObject::fromVariantMap(request));
        roundtrip(json.toJson().simplified());
        break;
    }
    case TryToStartSession: {
        m_status = LoginSuccessded;
    }
    default:
        break;
    }
}

void
CommandLine::handleAuthError()
{
    m_status = LoginStatus::Errored;
    QVariantMap request;

    request["type"] = "cancel_session";
    QJsonDocument json;

    json.setObject(QJsonObject::fromVariantMap(request));
    roundtrip(json.toJson().simplified());
    m_isAuthing = false;
    Q_EMIT isAuthingChanged();
}

void
CommandLine::handleAuthPasswordMessage()
{
    m_status    = LoginStatus::TryToLoginSession;
    m_isAuthing = true;
    Q_EMIT isAuthingChanged();
    QVariantMap request;

    request["type"]     = "post_auth_message_response";
    request["response"] = m_password;
    QJsonDocument json;

    json.setObject(QJsonObject::fromVariantMap(request));
    roundtrip(json.toJson().simplified());
}

void
CommandLine::UnLock()
{
#ifndef DEBUG_MODE
    ExtSessionLockV1Qt::Command::instance()->unLockScreen();
#endif
    QTimer::singleShot(0, qApp, [] { QGuiApplication::quit(); });
}

void
CommandLine::tryLogin()
{
    if (!m_greetd) {
        return;
    }
    QVariantMap request;

    request["type"]     = "create_session";
    request["username"] = m_userName;
    QJsonDocument json;

    json.setObject(QJsonObject::fromVariantMap(request));

    roundtrip(json.toJson().simplified());
}

void
CommandLine::roundtrip(const QString &payload)
{
    qint32 length = payload.length();
    m_greetd->write((const char *)&length, sizeof(length));
    m_greetd->write(payload.toUtf8());
}

void
CommandLine::RequestLogin()
{
    if (m_password.isEmpty()) {
        m_errorMessage = "password is needed";
        Q_EMIT errorMessageChanged();
        return;
    }
    tryLogin();
}

void
CommandLine::RequestShutDown()
{
    QDBusInterface login1{"org.freedesktop.login1"_L1,
                          "/org/freedesktop/login1"_L1,
                          "org.freedesktop.login1.Manager"_L1,
                          QDBusConnection::systemBus(),
                          this};
    auto result = login1.asyncCallWithArgumentList("PowerOff", {true});
    if (result.isError()) {
        qWarning() << "Cannot PowerOff" << result.error();
    }
}
