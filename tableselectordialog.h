#ifndef TABLESELECTORDIALOG_H
#define TABLESELECTORDIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QDialogButtonBox>

namespace Ui {
class TableSelectorDialog;
}

class TableSelectorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TableSelectorDialog(const QStringList &tables, const QStringList &selectedTables, QWidget *parent = nullptr);
    ~TableSelectorDialog();
    const QStringList selectedTables();
    const QStringList getAllTables();
    void setSelectedTables(const QStringList &selectedTables);

private slots:
    void selectAll();
    void selectNone();

private:
    void setAllChecked(bool checked);

    Ui::TableSelectorDialog *ui;
    QTableWidget *tableWidget;
};

#endif // TABLESELECTORDIALOG_H
