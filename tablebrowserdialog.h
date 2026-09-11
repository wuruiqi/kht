#ifndef TABLEBROWSERDIALOG_H
#define TABLEBROWSERDIALOG_H

#include <QDialog>
#include <QStringList>
#include <QList>
#include <QSet>

class SqliteDB;
class QComboBox;
class QTableWidget;
class QLineEdit;
class QPushButton;
class QLabel;

// 期刊数据表浏览模块：查看各表全部数据，支持字段显示/隐藏、按字段筛选、拖拽列宽、导出
class TableBrowserDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TableBrowserDialog(SqliteDB *db, QWidget *parent = nullptr);

private slots:
    void onTableChanged(int index);
    void applyFilter();
    void clearFilter();
    void exportAll();
    void exportSelected();
    void chooseColumns();    // 打开字段显示/隐藏对话框
    void restoreDefault();   // 恢复默认显示（全字段、清筛选、恢复列宽）

private:
    void loadTableData(const QString &tableName);
    void rebuildView();
    QStringList visibleHeaders() const;
    QList<int> visibleColumnIndexes() const;
    void exportRows(const QList<int> &origRowIndexes, const QString &title);

    SqliteDB *db;
    QComboBox *tableCombo;
    QTableWidget *table;
    QComboBox *filterFieldCombo;
    QLineEdit *filterValueEdit;
    QPushButton *columnButton;
    QLabel *rowCountLabel;

    QStringList headers;       // 完整表头
    QList<QStringList> rows;   // 完整数据
    QList<int> filteredRows;   // 当前视图行 -> 原始行索引
    QSet<int> hiddenColumns;   // 隐藏列索引
    int filterColumn;          // 筛选列（-1 表示未筛选）
    QString filterText;
};

#endif // TABLEBROWSERDIALOG_H
