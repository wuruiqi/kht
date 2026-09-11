#ifndef SQLITEDB_H
#define SQLITEDB_H

#include <QObject>
#include <QDir>
#include <QMultiMap>
#include <QHash>
#include <QtSql/QSqlDatabase>

typedef QPair<QString, QString> Pair;

// 期刊信息字段：表名 + 字段名 + 值，用于在查询结果中区分各表来源
struct JournalField {
    QString table;  // 数据表名
    QString field;  // 字段名
    QString value;  // 字段值
};

class SqliteDB : public QObject
{
    Q_OBJECT
public:
    explicit SqliteDB(const QDir &appDir, const QString &datasetName, QObject *parent = nullptr);
    ~SqliteDB();
    QStringList getAllTableNames();     //  返回当前数据库中包含的所有表的名字
    QStringList getAllJournalNames();   //  返回当前检索字段下的所有值，用于输入联想和判断输入是否有效
    QList<JournalField> getJournalInfo(const QString &value, bool allowSelectAgain = true);
    void selectTableNames(const QStringList &tableNames);    //  更新需要查询的表名称，存储在tableNames中
    void setSearchField(const QString &field);               //  设置检索字段（Journal/ISSN/EISSN）
    QStringList getTablesWithField(const QString &field);    //  返回包含指定字段的表名，用于主界面变灰判断
    static QString tableChineseName(const QString &tableName);   //  返回数据表对应的中文备注名称

    // ---- 数据表管理相关（Module 2）----
    void refreshTableList();    // 重新读取表列表、备注与排序（导入/删除后调用）
    bool readTableData(const QString &tableName, QStringList &headers, QList<QStringList> &rows, QString *errMsg = nullptr);   // 读取某表全部数据
    bool importTable(const QString &tableName, const QStringList &headers, const QList<QStringList> &rows, QString *errMsg = nullptr);   // 导入数据表
    bool deleteTable(const QString &tableName, QString *errMsg = nullptr);   // 删除数据表
    void setTableRemark(const QString &tableName, const QString &remark);    // 设置备注/显示名称
    void setTableOrder(const QStringList &orderedNames);                     // 设置主页数据表排序顺序

private:
    //默认检索字段
    QString defaultPrimaryKeyValue = "Journal";
    //当前检索字段（Journal/ISSN/EISSN）
    QString searchField = "Journal";

    QSqlDatabase database;
    QStringList allTableNames;  // 存储数据库中所有表的名字
    QStringList tableNames;  // 需要查询的表名称
    QList<QStringList> tableFields;  // 存储表对应的字段名称，存储顺序和tableNames一一对应
    QList<Pair> tablePrimaryKeys;    // 存储表（Key）及其对应的主键字段名称(T)，注意一个表可能不止一个主键
    QList<QStringList> allKeyNames;    //存储表及其主键列中的所有值，存储顺序和tablePrimaryKeys一一对应
    QStringList allJournalNamesList;    //组合成当前检索字段下所有值，用于输入联想和判断

    void selectTableFields();   //  根据tableNames，依次查询对应表的字段名称，存储在tableFields中
    void setTablePrimaryKeys(); //  根据当前检索字段，设置表及其对应的主键，存储在tablePrimaryKeys中
    void selectAllJournalNames();    //根据tablePrimaryKeys，查询主键的值作为检索目录，存储在allJournalNames中
    QStringList sortSpecialStrings(const QStringList &input);
    //在字段列表中查找检索字段对应的实际字段名（大小写不敏感，兼容 ISSN/EISSN 合并字段），找不到返回空
    QString findSearchField(const QStringList &fields, const QString &searchField);
    //按当前检索字段的主键执行查询，返回带表名的字段信息
    QList<JournalField> queryJournalInfo(const QString &value);

    // ---- 元数据（备注/排序）相关 ----
    void ensureMetaTable();                              // 建立 __jcr_meta 元数据表
    void loadRemarks();                                  // 从元数据表加载备注与排序到内存缓存
    QStringList sortTables(const QStringList &input);    // 表名排序：显式排序优先，否则按年份/类别

    QHash<QString, int> tableSortOrder;              // 表名 -> 显式排序序号
    static QHash<QString, QString> s_remarks;        // 表名 -> 中文备注（内存缓存，跨实例共享）
signals:

};

#endif // SQLITEDB_H
