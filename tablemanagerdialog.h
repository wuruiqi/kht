#ifndef TABLEMANAGERDIALOG_H
#define TABLEMANAGERDIALOG_H

#include <QDialog>
#include <QStringList>

class SqliteDB;
class QListWidget;

// 期刊数据表管理模块：导入/导出（csv+xlsx）、编辑备注、删除（输入确认）、重新排序
class TableManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TableManagerDialog(SqliteDB *db, QWidget *parent = nullptr);

signals:
    void tablesChanged();   // 数据表发生增删改/排序变化，通知主界面刷新

private slots:
    void importTable();
    void exportTable();
    void editRemark();
    void deleteTable();
    void moveUp();
    void moveDown();

private:
    void reloadList();
    int currentRow() const;
    QString currentTableName() const;
    void selectByName(const QString &name);

    SqliteDB *db;
    QListWidget *list;
    QStringList orderedTables;
};

#endif // TABLEMANAGERDIALOG_H
