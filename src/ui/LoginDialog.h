// SPDX-License-Identifier: MIT
#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QRadioButton;
class QCheckBox;

namespace lightcord {

class LoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoginDialog(QWidget *parent = nullptr);

    QString token() const;
    bool isBot() const;
    bool remember() const;

    void setError(const QString &error);
    void setAccountType(bool bot);

private:
    void updateHints();

    QRadioButton *m_userRadio = nullptr;
    QRadioButton *m_botRadio = nullptr;
    QLineEdit *m_tokenEdit = nullptr;
    QCheckBox *m_rememberCheck = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLabel *m_warningLabel = nullptr;
    QLabel *m_errorLabel = nullptr;
};

} // namespace lightcord
