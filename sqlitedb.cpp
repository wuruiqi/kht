#include "sqlitedb.h"

#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QMessageBox>
#include <QApplication>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QFile>
#include <algorithm>
#include <QSet>

static QString normColumnFor(const QStringList &fields, const QString &field);

// 静态备注缓存定义
QHash<QString, QString> SqliteDB::s_remarks;

// SQL 标识符转义（双引号包裹，内部双引号翻倍），用于动态表名/字段名
static QString quoteIdent(const QString &s)
{
    QString r = s;
    r.replace("\"", "\"\"");
    return "\"" + r + "\"";
}

// 内置数据表的默认中文备注（未在元数据表中自定义时使用）
static QString hardcodedRemark(const QString &tableName)
{
    static const QHash<QString, QString> remarkMap = {
        {"FQBJCR2025",        "中科院分区表2025"},
        {"JCR2025",           "JCR影响因子分区2025"},
        {"JCR2024",           "JCR影响因子分区2024"},
        {"XR2026",            "新锐分区2026"},
        {"XR2026Conferences", "新锐分区2026(会议)"},
        {"CCF2026",           "CCF推荐目录2026"},
        {"CCFT2025",          "CCF计算领域分级目录2025"},
        {"GJQKYJMD2020",      "国际预警期刊名单2020"},
        {"GJQKYJMD2021",      "国际预警期刊名单2021"},
        {"GJQKYJMD2023",      "国际预警期刊名单2023"},
        {"GJQKYJMD2024",      "国际预警期刊名单2024"},
        {"GJQKYJMD2025",      "国际预警期刊名单2025"},
        {"CSCD2017",          "CSCD中国科学引文目录2017-2018"},
        {"CSCD2019",          "CSCD中国科学引文目录2019-2020"},
        {"CSCD2021",          "CSCD中国科学引文目录2021-2022"},
        {"CSCD2023",          "CSCD中国科学引文目录2023-2024"},
        {"CSSCI2014",         "CSSCI南大核心目录2014-2016"},
        {"CSSCI2017",         "CSSCI南大核心目录2017-2018"},
        {"CSSCI2019",         "CSSCI南大核心目录2019-2020"},
        {"CSSCI2021",         "CSSCI南大核心目录2021-2022"},
        {"CSSCI2023",         "CSSCI南大核心目录2023-2024"},
        {"BDHX2014",          "北大中文核心目录2014版"},
        {"BDHX2017",          "北大中文核心目录2017版"},
        {"BDHX2020",          "北大中文核心目录2020版"},
        {"BDHX2023",          "北大中文核心目录2023版"},
    };
    return remarkMap.value(tableName, QString());
}

// 返回数据表所属类别（用于主界面分组展示）：中文核心目录表名以 CSCD/CSSCI/BDHX 开头
QString SqliteDB::categoryOf(const QString &tableName)
{
    static const QStringList chinesePrefixes = {"CSCD", "CSSCI", "BDHX"};
    foreach(const QString &p, chinesePrefixes){
        if(tableName.startsWith(p, Qt::CaseInsensitive))
            return QStringLiteral("中文核心期刊");
    }
    return QStringLiteral("外文期刊");
}

// 检索键规范化：兼容字符折叠（NFKC，全角字母数字括号转半角）、全角标点统一、去所有空白、转小写
QString SqliteDB::normalizeKey(const QString &value)
{
    QString s = value.normalized(QString::NormalizationForm_KC);
    s.replace(QStringLiteral("（"), QStringLiteral("(")).replace(QStringLiteral("）"), QStringLiteral(")"))
     .replace(QStringLiteral("："), QStringLiteral(":")).replace(QStringLiteral("．"), QStringLiteral("."))
     .replace(QStringLiteral("–"), QStringLiteral("-")).replace(QStringLiteral("—"), QStringLiteral("-")).replace(QStringLiteral("－"), QStringLiteral("-"));
    QString out;
    out.reserve(s.size());
    for(const QChar &ch : s){
        if(ch.isSpace())
            continue;
        out.append(ch);
    }
    return out.toLower();
}

// 在规范化基础上去连字符（ISSN/CN号 常见输写差异：1002-4921 / 10024921）
QString SqliteDB::normalizeCompact(const QString &value)
{
    return normalizeKey(value).remove('-');
}

SqliteDB::SqliteDB(const QDir &appDir, const QString &datasetName, QObject *parent) : QObject(parent)
{
    //连接SQLite3数据库"jcr.db"，该数据集应放在运行目录下
    database = QSqlDatabase::addDatabase("QSQLITE");
    QString dbPath = appDir.absoluteFilePath(datasetName);
    database.setDatabaseName(dbPath);
    //数据库文件不存在时不自动创建空库（保持只读提示）；存在时以读写方式打开以支持数据表管理
    if (!QFile::exists(dbPath))
    {
        database.setConnectOptions("QSQLITE_OPEN_READONLY");
    }
    //    qDebug() << database;
    if (!database.open())
    {
        qWarning() << "Error: Failed to connect database." << __FUNCTION__ << database.lastError();
        QMessageBox::warning(QApplication::activeWindow(), "期刊信息数据库缺失！", database.lastError().text());
    }
    else
    {
        qDebug() << "Successed to connect database.";
    }

    //加载元数据（备注/排序）；元数据表不存在时跳过，避免首次运行写库导致启动变慢
    loadRemarks();

    allTableNames = database.tables();
    //过滤内部元数据表
    QStringList filtered;
    foreach(const QString &t, allTableNames){
        if (t.startsWith("__") || t == "sqlite_sequence")
            continue;
        filtered << t;
    }
    allTableNames = filtered;
    // 按显式排序 + 年份/类别排序
    allTableNames = sortTables(allTableNames);
    //selectTableNames(allTableNames);	//避免启动时执行两次
}

SqliteDB::~SqliteDB()
{
    if(database.isOpen()){
        database.close();
    }
}

QStringList SqliteDB::getAllTableNames()
{
    return allTableNames;
}

QStringList SqliteDB::getAllJournalNames()
{
    return allJournalNamesList;
}

// 设置检索字段（Journal/ISSN/EISSN），并重建主键和联想目录
void SqliteDB::setSearchField(const QString &field)
{
    if(searchField == field)
        return;
    searchField = field;
    setTablePrimaryKeys();
    selectAllJournalNames();
}

// 判断输入值是否命中任一已选表的检索键（含规范化变体，用于输入校验）
bool SqliteDB::hasKey(const QString &value)
{
    foreach(const QStringList &keyNames, allKeyNames){
        if(keyNames.contains(value, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

// 返回包含指定字段的表名（用于主界面变灰判断）
QStringList SqliteDB::getTablesWithField(const QString &field)
{
    QStringList result;
    QSqlQuery query;
    foreach(const QString &table, allTableNames){
        if(!database.isOpen())
            continue;
        QStringList fields;
        QString select = "PRAGMA table_info(" + table + ")";
        if(query.exec(select)){
            while(query.next()){
                fields << query.value(1).toString();
            }
        }
        if(!findSearchField(fields, field).isEmpty()){
            result << table;
        }
    }
    return result;
}

// 按当前检索字段的主键执行查询，返回带表名的字段信息
QList<JournalField> SqliteDB::queryJournalInfo(const QString &value)
{
    Q_ASSERT(allKeyNames.size() == tablePrimaryKeys.size());

    QList<JournalField> journalInfo;
    QList<QString> journalInfoFieldNames;
    QSqlQuery query;
    for(int i = 0; i < allKeyNames.size(); i++){
        if(allKeyNames[i].contains(value, Qt::CaseInsensitive)){
            const QString &table = tablePrimaryKeys[i].first;
            const QString &primaryKey = tablePrimaryKeys[i].second;
            if(database.isOpen()){
                const QStringList &fields = tableFields[tableNames.indexOf(table)];
                QString normCol = normColumnFor(fields, searchField);
                QString normValue;
                if(!normCol.isEmpty()){
                    //ISSN/EISSN/CN号 用去连字符的紧凑规范化，其余用通用规范化
                    if(searchField.compare("Journal", Qt::CaseInsensitive)==0)
                        normValue = normalizeKey(value);
                    else
                        normValue = normalizeCompact(value);
                }
                if (!query.prepare("select * from " + quoteIdent(table) + " where " +
                                   (normCol.isEmpty()
                                        ? ((primaryKey.contains('/')
                                                ? quoteIdent(primaryKey) + " like '%' || ? || '%' COLLATE NOCASE"   //合并字段（如 ISSN/EISSN），值以“/”分隔，用模糊匹配
                                                : quoteIdent(primaryKey) + " = ? COLLATE NOCASE"))                  //设置查询不区分大小写
                                        : quoteIdent(normCol) + " = ? COLLATE NOCASE"))){
                    qWarning() << "Error: Failed to prepare select " << table << __FUNCTION__ << query.lastError().text();
                }
                query.addBindValue(normCol.isEmpty() ? QVariant(value) : QVariant(normValue));
                if (!query.exec()){
                    qWarning() << "Error: Failed to select " << table << __FUNCTION__ << query.lastError().text();
                }
                //CCF推荐期刊中不同领域存在重复的期刊
                while (query.next()){
                    QStringList fieldNames = tableFields[tableNames.indexOf(table)];
                    foreach(const QString &fieldName, fieldNames){
                        //跳过内部规范化辅助列，不在结果中显示
                        if(fieldName.startsWith("__"))
                            continue;
                        QString fieldValue = query.value(fieldName).toString();
                        if(fieldValue.isEmpty() || fieldValue.isNull())
                            continue;
                        //排除字段名称重复的数据，主要是避免defaultPrimaryKeyValue（Journal字段）重复出现
                        if(!journalInfoFieldNames.contains(fieldName) || fieldName != defaultPrimaryKeyValue){
                            JournalField info;
                            info.table = table;
                            info.field = fieldName;
                            info.value = fieldValue;
                            journalInfo << info;
                            journalInfoFieldNames << fieldName;
                        }
                    }
                }
            }
        }
    }
    return journalInfo;
}

QList<JournalField> SqliteDB::getJournalInfo(const QString &value, bool allowSelectAgain)
{
    Q_ASSERT(allJournalNamesList.contains(value, Qt::CaseInsensitive));
    QList<JournalField> journalInfo = queryJournalInfo(value);

    //查询输入不是期刊全称（或当前检索字段不是Journal，如ISSN/EISSN）时，自动进行二次查询，
    //先定位到Journal全称，再临时切换到Journal字段重查，显示完整信息;allowSelectAgain避免进入死循环
    if(allowSelectAgain and journalInfo.size() > 0){
        bool needReselect = false;
        if(searchField == defaultPrimaryKeyValue){
            //Journal模式：首条字段不是Journal，说明输入的是缩写（第一字段），需要二次查询
            needReselect = (journalInfo[0].field != defaultPrimaryKeyValue);
        }
        else{
            //ISSN/EISSN模式：总是二次查询，转为Journal全称后显示完整信息
            needReselect = true;
        }
        if(needReselect){
            foreach(const JournalField &info, journalInfo){
                if(info.field == defaultPrimaryKeyValue){
                    QString savedField = searchField;
                    setSearchField(defaultPrimaryKeyValue);
                    journalInfo = queryJournalInfo(info.value);
                    setSearchField(savedField);
                    qInfo() << "auto select" << info.value;
                    break;
                }
            }
        }
    }
    return journalInfo;
}

void SqliteDB::selectTableNames(const QStringList &selectedtableNames)
{
    tableNames = selectedtableNames;
    // qDebug() << allTableNames;
    // qDebug() << tableNames;
    selectTableFields();
    setTablePrimaryKeys();
    selectAllJournalNames();
}

void SqliteDB::selectTableFields()
{
    tableFields.clear();
    QSqlQuery query;
    foreach(const QString &table, tableNames){
        QStringList fieldNames;
        if(database.isOpen()){
            QString select = "PRAGMA table_info(" + table + ")";
            if (!query.exec(select)){
                qWarning() << "Error: Failed to selectTableFields." << table << __FUNCTION__ << database.lastError();
            }
            while (query.next()){
                QString fieldName = query.value(1).toString();  //  返回格式为：“字段序号、字段名称、字段类型”，这里只提取字段名称
                fieldNames << fieldName;
            }
        }
        tableFields << fieldNames;
    }
    //    qDebug() << tableFields;

    Q_ASSERT(tableNames.size() == tableFields.size());
}

// 根据检索字段与表字段，返回该表对应的规范化辅助列名（导入时自动生成，形如 __norm_journal），无则返回空
static QString normColumnFor(const QStringList &fields, const QString &field)
{
    auto has = [&fields](const QString &n){
        foreach(const QString &f, fields) if(f.compare(n, Qt::CaseInsensitive)==0) return true;
        return false;
    };
    if(field.compare("Journal", Qt::CaseInsensitive)==0){
        return has("__norm_journal") ? QStringLiteral("__norm_journal") : QString();
    }
    if(field.compare("CN号", Qt::CaseInsensitive)==0 || field.compare("CN", Qt::CaseInsensitive)==0){
        return has("__norm_cn") ? QStringLiteral("__norm_cn") : QString();
    }
    if(field.compare("ISSN", Qt::CaseInsensitive)==0){
        return has("__norm_issn") ? QStringLiteral("__norm_issn") : QString();
    }
    if(field.compare("EISSN", Qt::CaseInsensitive)==0){
        if(has("__norm_eissn")) return QStringLiteral("__norm_eissn");
        return has("__norm_issn") ? QStringLiteral("__norm_issn") : QString();
    }
    return QString();
}

void SqliteDB::setTablePrimaryKeys()
{
    tablePrimaryKeys.clear();
    Q_ASSERT(tableNames.size() == tableFields.size());

    for(int i = 0; i < tableNames.size(); i++){
        if(searchField == defaultPrimaryKeyValue){
            //Journal模式：Journal字段作为主键；若第一个字段不是Journal，则第一个字段也作为主键（用于缩写检索）
            if(tableFields[i].contains(defaultPrimaryKeyValue)){
                tablePrimaryKeys << Pair(tableNames[i], defaultPrimaryKeyValue);
            }
            if(tableFields[i][0] != defaultPrimaryKeyValue){
                tablePrimaryKeys << Pair(tableNames[i], tableFields[i][0]);
            }
        }
        else{
            //ISSN/EISSN模式：仅包含该字段的表参与检索
            QString actualField = findSearchField(tableFields[i], searchField);
            if(!actualField.isEmpty()){
                tablePrimaryKeys << Pair(tableNames[i], actualField);
            }
        }
    }
    //    qDebug() << tablePrimaryKeys;
}

void SqliteDB::selectAllJournalNames()
{
    //规范化去重集合：已收录名字的规范化键（跨表累计，避免 O(n^2) 比较）
    QSet<QString> normalizedExisting;
    normalizedExisting.reserve(allJournalNamesList.size() * 2);
    foreach(const QString &existing, allJournalNamesList)
        normalizedExisting.insert(normalizeKey(existing));
    allKeyNames.clear();
    allJournalNamesList.clear();
    QSqlQuery query;
    foreach(const Pair &pair, tablePrimaryKeys){
        const QString &table = pair.first;
        const QString &primaryKey = pair.second;
        QStringList keyNames;
        int rawCount = 0;   // 原始键值数量（联想列表只取原始写法）
        if(database.isOpen()){
            QString select = "select " + quoteIdent(primaryKey) + " from " + quoteIdent(table);
            if (!query.exec(select)){
                qWarning() << "Error: Failed to select" << table << __FUNCTION__ << database.lastError();
            }
            while (query.next()){
                QString journalName = query.value(0).toString();
                keyNames << journalName;
            }
            rawCount = keyNames.size();
            //规范化键值也纳入检索目录（含 __norm 辅助列的表），支持全半角/括号/连字符变体输入
            const QStringList &fields = tableFields[tableNames.indexOf(table)];
            QString normCol = normColumnFor(fields, searchField);
            if(!normCol.isEmpty()){
                if (query.exec("select " + quoteIdent(normCol) + " from " + quoteIdent(table))){
                    while (query.next()){
                        QString nv = query.value(0).toString();
                        if(!nv.isEmpty())
                            keyNames << nv;
                    }
                }
            }
        }
        allKeyNames << keyNames;
        //输入提示项仅保留原始写法，并用规范化比较去重（避免全半角变体产生重复提示）
        //已有名字的规范化结果只算一次放入哈希集合，避免 O(n^2) 重复规范化导致启动卡死
        for(int ki = 0; ki < rawCount && ki < keyNames.size(); ++ki){
            const QString &keyName = keyNames.at(ki);
            QString nk = normalizeKey(keyName);
            if(!normalizedExisting.contains(nk)){
                allJournalNamesList << keyName;
                normalizedExisting.insert(nk);
            }
        }
    }
    qDebug() << allJournalNamesList.length();

    Q_ASSERT(allKeyNames.size() == tablePrimaryKeys.size());
}

// 在字段列表中查找检索字段对应的实际字段名（大小写不敏感，兼容 ISSN/EISSN 合并字段），找不到返回空
QString SqliteDB::findSearchField(const QStringList &fields, const QString &searchField)
{
    // 精确匹配（大小写不敏感）
    foreach(const QString &f, fields){
        if(f.compare(searchField, Qt::CaseInsensitive) == 0){
            return f;
        }
    }
    // 特殊：ISSN 或 EISSN 检索时，兼容 "ISSN/EISSN" 合并字段
    if(searchField.compare("ISSN", Qt::CaseInsensitive) == 0 ||
       searchField.compare("EISSN", Qt::CaseInsensitive) == 0){
        foreach(const QString &f, fields){
            if(f.compare("ISSN/EISSN", Qt::CaseInsensitive) == 0){
                return f;
            }
        }
    }
    // 特殊：CN号 检索时，兼容字段名 "CN"（如 XR2026 表）
    if(searchField.compare("CN号", Qt::CaseInsensitive) == 0){
        foreach(const QString &f, fields){
            if(f.compare("CN", Qt::CaseInsensitive) == 0){
                return f;
            }
        }
    }
    return QString();
}

// 表名排序：时间（年份）由近及远；同年按类别顺序 中科院/新锐分区表 -> JCR分区表 -> CCF推荐 -> 预警期刊
QStringList SqliteDB::sortSpecialStrings(const QStringList &input) {
    struct StringItem {
        QString original;  // 原始字符串
        QString prefix;    // 提取的前缀
        int year = 0;      // 提取的年份
        int category = INT_MAX;  // 类别优先级
    };
    // 类别优先级：中科院分区表/新锐分区表 -> JCR -> CCF推荐 -> 预警期刊
    const QHash<QString, int> kCategoryPriority = {
        {"FQBJCR", 0},   // 中科院分区表
        {"XR",     0},   // 新锐分区表（同为分区表类）
        {"JCR",    1},   // JCR分区表
        {"CCF",    2},   // CCF推荐目录
        {"CCFT",   2},   // CCF计算领域分级目录
        {"GJQKYJMD",3},  // 国际预警期刊名单
        {"CSCD",    4},  // CSCD中国科学引文目录
        {"CSSCI",   5},  // CSSCI南大核心目录
        {"BDHX",    6},  // 北大中文核心目录
    };
    // 正则表达式提取前缀和4位年份
    const QRegularExpression kPattern("^(\\D+)(\\d{4})"); // 非数字前缀 + 4位年份
    // 解析所有字符串
    QList<StringItem> items;
    for (const QString &s : input) {
        QRegularExpressionMatch match = kPattern.match(s);
        if (match.hasMatch()) {
            StringItem item;
            item.original = s;
            item.prefix = match.captured(1);
            item.year = match.captured(2).toInt();
            item.category = kCategoryPriority.value(item.prefix, INT_MAX); // 未定义前缀设为最低优先级
            items.append(item);
        } else {
            // 无法解析的项放在末尾
            items.append({s, s, 0, INT_MAX});
        }
    }
    // 自定义排序规则
    std::sort(items.begin(), items.end(), [](const StringItem &a, const StringItem &b) {
        // 1. 年份降序（时间由近及远）
        if (a.year != b.year) return a.year > b.year;
        // 2. 同年按类别优先级升序
        if (a.category != b.category) return a.category < b.category;
        // 3. 同类别按原始字符串升序
        return a.original < b.original;
    });
    // 提取排序后的结果
    QStringList result;
    for (const auto &item : items) {
        result << item.original;
    }
    return result;
}

// 返回数据表对应的中文备注名称，用于数据表选择界面显示
// 优先级：元数据表自定义备注 > 内置默认备注 > 原始表名
QString SqliteDB::tableChineseName(const QString &tableName)
{
    if (s_remarks.contains(tableName) && !s_remarks.value(tableName).isEmpty())
        return s_remarks.value(tableName);
    QString h = hardcodedRemark(tableName);
    if (!h.isEmpty())
        return h;
    return tableName;
}

// ---- 元数据（备注/排序）相关 ----

void SqliteDB::ensureMetaTable()
{
    QSqlQuery query;
    query.exec("CREATE TABLE IF NOT EXISTS __jcr_meta (table_name TEXT PRIMARY KEY, remark TEXT, sort_order INTEGER)");
}

void SqliteDB::loadRemarks()
{
    s_remarks.clear();
    tableSortOrder.clear();
    if (!database.isOpen())
        return;
    // 元数据表不存在则跳过（首次运行尚未建立，避免启动时写库）
    QSqlQuery check;
    if (!check.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='__jcr_meta'"))
        return;
    if (!check.next())
        return;
    QSqlQuery query;
    if (query.exec("SELECT table_name, remark, sort_order FROM __jcr_meta")) {
        while (query.next()) {
            QString tn = query.value(0).toString();
            QString r = query.value(1).toString();
            QVariant so = query.value(2);
            if (!r.isEmpty())
                s_remarks[tn] = r;
            if (!so.isNull())
                tableSortOrder[tn] = so.toInt();
        }
    }
}

void SqliteDB::refreshTableList()
{
    allTableNames = database.tables();
    QStringList filtered;
    foreach(const QString &t, allTableNames){
        if (t.startsWith("__") || t == "sqlite_sequence")
            continue;
        filtered << t;
    }
    allTableNames = filtered;
    loadRemarks();
    allTableNames = sortTables(allTableNames);
}

// 表名排序：有显式排序序号（tableSortOrder）的按序号升序排在最前，其余按年份/类别回退排序
QStringList SqliteDB::sortTables(const QStringList &input)
{
    QStringList withOrder, withoutOrder;
    for (const QString &t : input) {
        if (tableSortOrder.contains(t))
            withOrder << t;
        else
            withoutOrder << t;
    }
    std::sort(withOrder.begin(), withOrder.end(), [this](const QString &a, const QString &b){
        return tableSortOrder.value(a) < tableSortOrder.value(b);
    });
    withoutOrder = sortSpecialStrings(withoutOrder);
    return withOrder + withoutOrder;
}

void SqliteDB::setTableRemark(const QString &tableName, const QString &remark)
{
    ensureMetaTable();
    s_remarks[tableName] = remark;
    QSqlQuery query;
    query.prepare("INSERT OR REPLACE INTO __jcr_meta(table_name, remark, sort_order) VALUES(?,?,?)");
    query.addBindValue(tableName);
    query.addBindValue(remark);
    if (tableSortOrder.contains(tableName))
        query.addBindValue(tableSortOrder[tableName]);
    else
        query.addBindValue(QVariant());   // 绑定 NULL，保留已有排序为空
    query.exec();
}

void SqliteDB::setTableOrder(const QStringList &orderedNames)
{
    ensureMetaTable();
    tableSortOrder.clear();
    for (int i = 0; i < orderedNames.size(); ++i)
        tableSortOrder[orderedNames[i]] = i + 1;

    QSqlQuery query;
    for (int i = 0; i < orderedNames.size(); ++i) {
        query.prepare("INSERT OR REPLACE INTO __jcr_meta(table_name, remark, sort_order) VALUES(?,?,?)");
        query.addBindValue(orderedNames[i]);
        query.addBindValue(tableChineseName(orderedNames[i]));
        query.addBindValue(i + 1);
        query.exec();
    }
    allTableNames = sortTables(allTableNames);
}

// ---- 数据表读写/导入/删除 ----

bool SqliteDB::readTableData(const QString &tableName, QStringList &headers, QList<QStringList> &rows, QString *errMsg)
{
    if (!database.isOpen()) {
        if (errMsg) *errMsg = QStringLiteral("数据库未打开");
        return false;
    }
    QSqlQuery query;
    QStringList fields;
    if (!query.exec("PRAGMA table_info(" + quoteIdent(tableName) + ")")) {
        if (errMsg) *errMsg = query.lastError().text();
        return false;
    }
    while (query.next())
        fields << query.value(1).toString();
    headers = fields;
    rows.clear();
    if (!query.exec("SELECT * FROM " + quoteIdent(tableName))) {
        if (errMsg) *errMsg = query.lastError().text();
        return false;
    }
    while (query.next()) {
        QStringList row;
        for (int i = 0; i < fields.size(); ++i)
            row << query.value(i).toString();
        rows << row;
    }
    return true;
}

bool SqliteDB::importTable(const QString &tableName, const QStringList &headers, const QList<QStringList> &rows, QString *errMsg)
{
    if (!database.isOpen()) {
        if (errMsg) *errMsg = QStringLiteral("数据库未打开");
        return false;
    }
    if (headers.isEmpty()) {
        if (errMsg) *errMsg = QStringLiteral("表头为空");
        return false;
    }

    QStringList colDefs, colNames;
    for (const QString &h : headers) {
        QString c = h.trimmed();
        if (c.isEmpty())
            c = QStringLiteral("column");
        colNames << c;
        colDefs << quoteIdent(c) + QStringLiteral(" TEXT");
    }

    // 自动生成规范化辅助列（__norm_*，内部列，界面不显示）：
    // 支持全半角/括号/空白/连字符变体检索；仅当对应源列存在时生成
    QList<QPair<QString, int>> normCols;   // (辅助列名, 源列下标)
    for (int i = 0; i < headers.size(); ++i) {
        QString h = headers.at(i).trimmed();
        QString normCol;
        bool compact = false;
        if (h.compare("Journal", Qt::CaseInsensitive) == 0) {
            normCol = QStringLiteral("__norm_journal");
        } else if (h.compare("ISSN", Qt::CaseInsensitive) == 0
                   || h.compare("ISSN/EISSN", Qt::CaseInsensitive) == 0) {
            normCol = QStringLiteral("__norm_issn");
            compact = true;
        } else if (h.compare("EISSN", Qt::CaseInsensitive) == 0) {
            normCol = QStringLiteral("__norm_eissn");
            compact = true;
        } else if (h.compare("CN号", Qt::CaseInsensitive) == 0
                   || h.compare("CN", Qt::CaseInsensitive) == 0) {
            normCol = QStringLiteral("__norm_cn");
            compact = true;
        }
        if (!normCol.isEmpty() && !colNames.contains(normCol)) {
            normCols << qMakePair(normCol, compact ? i : i);
            colNames << normCol;
            colDefs << quoteIdent(normCol) + QStringLiteral(" TEXT");
        }
    }
    QSqlQuery query;
    if (!query.exec("CREATE TABLE " + quoteIdent(tableName) + " (" + colDefs.join(", ") + ")")) {
        if (errMsg) *errMsg = query.lastError().text();
        return false;
    }

    QStringList qNames, qPlaceholders;
    for (const QString &c : colNames) {
        qNames << quoteIdent(c);
        qPlaceholders << QStringLiteral("?");
    }
    QString insertSql = "INSERT INTO " + quoteIdent(tableName) + " (" + qNames.join(", ") + ") VALUES (" + qPlaceholders.join(", ") + ")";
    const int origColCount = headers.size();

    database.transaction();
    QSqlQuery ins;
    for (const QStringList &row : rows) {
        ins.prepare(insertSql);
        for (int i = 0; i < colNames.size(); ++i) {
            if (i < origColCount) {
                ins.addBindValue(i < row.size() ? row[i] : QString());
            } else {
                // 规范化辅助列：由对应源列计算
                QString normCol = colNames.at(i);
                int srcIdx = -1;
                for (const auto &p : normCols) {
                    if (p.first.compare(normCol, Qt::CaseInsensitive) == 0) { srcIdx = p.second; break; }
                }
                QString srcVal = (srcIdx >= 0 && srcIdx < row.size()) ? row.at(srcIdx) : QString();
                bool compact = !normCol.endsWith("journal", Qt::CaseInsensitive);
                ins.addBindValue(compact ? normalizeCompact(srcVal) : normalizeKey(srcVal));
            }
        }
        if (!ins.exec()) {
            database.rollback();
            query.exec("DROP TABLE IF EXISTS " + quoteIdent(tableName));
            if (errMsg) *errMsg = ins.lastError().text();
            return false;
        }
        ins.finish();
    }
    if (!database.commit()) {
        database.rollback();
        query.exec("DROP TABLE IF EXISTS " + quoteIdent(tableName));
        if (errMsg) *errMsg = database.lastError().text();
        return false;
    }

    setTableRemark(tableName, tableName);   // 默认备注为表名，可在管理模块中修改
    refreshTableList();
    return true;
}

bool SqliteDB::deleteTable(const QString &tableName, QString *errMsg)
{
    if (!database.isOpen()) {
        if (errMsg) *errMsg = QStringLiteral("数据库未打开");
        return false;
    }
    QSqlQuery query;
    if (!query.exec("DROP TABLE IF EXISTS " + quoteIdent(tableName))) {
        if (errMsg) *errMsg = query.lastError().text();
        return false;
    }
    query.prepare("DELETE FROM __jcr_meta WHERE table_name = ?");
    query.addBindValue(tableName);
    query.exec();
    s_remarks.remove(tableName);
    tableSortOrder.remove(tableName);
    refreshTableList();
    return true;
}
