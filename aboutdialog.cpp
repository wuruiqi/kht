#include "aboutdialog.h"
#include "ui_aboutdialog.h"
#include <QDesktopServices>
#include <QUrl>

AboutDialog::AboutDialog(const QString &appName, const QString &version, const QString &author, const QString &email, const QString &codeURL, const QString &updateURL, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AboutDialog)
{
    ui->setupUi(this);

    this->appName = appName;
    this->version = version;
    this->author = author;
    this->email = email;
    this->codeURL = codeURL;
    this->updateURL = updateURL;

    this->setWindowFlags(this->windowFlags()&~Qt::WindowMinMaxButtonsHint);
    this->setWindowTitle(appName);
    ui->label_version->setText(appName + ", " + version + "版");
    ui->label_author->setText(tr("作者：%1  ·  %2").arg(author).arg(email));
    ui->pushButton_email->setToolTip(email);
    ui->pushButton_code->setToolTip(codeURL);
    ui->pushButton_update->setToolTip(updateURL);
}

AboutDialog::~AboutDialog()
{
    delete ui;
}

void AboutDialog::on_pushButton_email_clicked()
{
    QDesktopServices::openUrl(QUrl("mailto:"+email));
}

void AboutDialog::on_pushButton_code_clicked()
{
    QDesktopServices::openUrl(QUrl(codeURL));
}

void AboutDialog::on_pushButton_OK_clicked()
{
    this->close();
}

void AboutDialog::on_pushButton_update_clicked()
{
    QDesktopServices::openUrl(QUrl(updateURL));
}
