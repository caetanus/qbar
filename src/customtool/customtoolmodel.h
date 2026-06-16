#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class CustomToolModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString command READ command WRITE setCommand NOTIFY commandChanged)
    Q_PROPERTY(QVariantList arguments READ arguments WRITE setArguments NOTIFY argumentsChanged)
    Q_PROPERTY(QString workingDirectory READ workingDirectory WRITE setWorkingDirectory NOTIFY workingDirectoryChanged)
    Q_PROPERTY(int intervalMs READ intervalMs WRITE setIntervalMs NOTIFY intervalMsChanged)
    Q_PROPERTY(bool waybarFormat READ waybarFormat WRITE setWaybarFormat NOTIFY waybarFormatChanged)
    Q_PROPERTY(QString format READ format WRITE setFormat NOTIFY formatChanged)
    Q_PROPERTY(QVariantMap formatIcons READ formatIcons WRITE setFormatIcons NOTIFY formatIconsChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString text READ text NOTIFY textChanged)
    Q_PROPERTY(QString icon READ icon NOTIFY iconChanged)
    Q_PROPERTY(QString displayText READ displayText NOTIFY displayTextChanged)
    Q_PROPERTY(QString tooltip READ tooltip NOTIFY tooltipChanged)
    Q_PROPERTY(QString alt READ alt NOTIFY altChanged)
    Q_PROPERTY(QString className READ className NOTIFY classNameChanged)
    Q_PROPERTY(double percentage READ percentage NOTIFY percentageChanged)
    Q_PROPERTY(QString rawOutput READ rawOutput NOTIFY rawOutputChanged)

public:
    explicit CustomToolModel(QObject *parent = nullptr);

    QString command() const { return m_command; }
    QVariantList arguments() const { return m_arguments; }
    QString workingDirectory() const { return m_workingDirectory; }
    int intervalMs() const { return m_intervalMs; }
    bool waybarFormat() const { return m_waybarFormat; }
    QString format() const { return m_format; }
    QVariantMap formatIcons() const { return m_formatIcons; }
    bool available() const { return m_available; }
    bool loading() const { return m_loading; }
    QString text() const { return m_text; }
    QString icon() const { return m_icon; }
    QString displayText() const { return m_displayText; }
    QString tooltip() const { return m_tooltip; }
    QString alt() const { return m_alt; }
    QString className() const { return m_className; }
    double percentage() const { return m_percentage; }
    QString rawOutput() const { return m_rawOutput; }

public slots:
    void refresh();
    // Runs a waybar on-click/on-scroll command via `sh -c`, detached.
    Q_INVOKABLE void runAction(const QString &command);

    void setCommand(const QString &command);
    void setArguments(const QVariantList &arguments);
    void setWorkingDirectory(const QString &workingDirectory);
    void setIntervalMs(int intervalMs);
    void setWaybarFormat(bool waybarFormat);
    void setFormat(const QString &format);
    void setFormatIcons(const QVariantMap &formatIcons);

signals:
    void commandChanged();
    void argumentsChanged();
    void workingDirectoryChanged();
    void intervalMsChanged();
    void waybarFormatChanged();
    void formatChanged();
    void formatIconsChanged();
    void availableChanged();
    void loadingChanged();
    void textChanged();
    void iconChanged();
    void displayTextChanged();
    void tooltipChanged();
    void altChanged();
    void classNameChanged();
    void percentageChanged();
    void rawOutputChanged();

private:
    void startProcess();
    void scheduleTimer();
    void finishWithOutput(const QString &output, bool success);
    void recomputeDisplayText();
    void setAvailable(bool available);
    void setLoading(bool loading);
    void setText(const QString &text);
    void setIcon(const QString &icon);
    void setTooltip(const QString &tooltip);
    void setAlt(const QString &alt);
    void setClassName(const QString &className);
    void setPercentage(double percentage);
    void setRawOutput(const QString &rawOutput);

    QString m_command;
    QVariantList m_arguments;
    QString m_workingDirectory;
    int m_intervalMs = 0;
    bool m_waybarFormat = true;
    QString m_format = QStringLiteral("{}");
    QVariantMap m_formatIcons;
    bool m_available = false;
    bool m_loading = false;
    QString m_text;
    QString m_icon;
    QString m_displayText;
    QString m_tooltip;
    QString m_alt;
    QString m_className;
    double m_percentage = 0.0;
    QString m_rawOutput;
    QProcess m_process;
    QTimer m_timer;
};
