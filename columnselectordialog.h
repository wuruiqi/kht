#ifndef COLUMNSELECTORDIALOG_H
#define COLUMNSELECTORDIALOG_H

#include <QDialog>
#include <QSet>
#include <QStringList>

class QTableWidget;

// 字段显示/隐藏选择对话框：勾选表示显示，提供全选/全不选快捷操作
class ColumnSelectorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ColumnSelectorDialog(const QStringList &allHeaders, const QSet<int> &hiddenColumns, QWidget *parent = nullptr);
    QSet<int> hiddenColumns() const;

private slots:
    void selectAll();
    void selectNone();

private:
    void setAllChecked(bool checked);

    QTableWidget *tableWidget;
};

#endif // COLUMNSELECTORDIALOG_H
