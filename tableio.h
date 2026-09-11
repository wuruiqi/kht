#ifndef TABLEIO_H
#define TABLEIO_H

#include <QString>
#include <QStringList>
#include <QList>

// 表格文件读写工具：支持 CSV 和 Excel(xlsx) 两种格式
namespace TableIO {

// 读表格文件（根据扩展名自动识别 csv/xlsx），返回表头和数据行
bool readFile(const QString &filePath, QStringList &headers, QList<QStringList> &rows, QString *errMsg = nullptr);

// 写表格文件（根据扩展名自动识别 csv/xlsx）
bool writeFile(const QString &filePath, const QStringList &headers, const QList<QStringList> &rows, QString *errMsg = nullptr);

// 判断扩展名是否受支持
bool isSupported(const QString &filePath);

// 返回文件对话框过滤器
QString fileFilter();

// CSV 读写
bool readCsv(const QString &filePath, QStringList &headers, QList<QStringList> &rows, QString *errMsg = nullptr);
bool writeCsv(const QString &filePath, const QStringList &headers, const QList<QStringList> &rows, QString *errMsg = nullptr);

// xlsx 读写
bool readXlsx(const QString &filePath, QStringList &headers, QList<QStringList> &rows, QString *errMsg = nullptr);
bool writeXlsx(const QString &filePath, const QStringList &headers, const QList<QStringList> &rows, QString *errMsg = nullptr);

} // namespace TableIO

#endif // TABLEIO_H
