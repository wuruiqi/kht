#include "tableio.h"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtCore/private/qzipwriter_p.h>
#include <QtCore/private/qzipreader_p.h>

namespace TableIO {

// ---------- 通用 ----------

bool isSupported(const QString &filePath)
{
    QString ext = QFileInfo(filePath).suffix().toLower();
    return (ext == "csv" || ext == "xlsx");
}

QString fileFilter()
{
    return QStringLiteral("表格文件 (*.csv *.xlsx);;CSV 文件 (*.csv);;Excel 文件 (*.xlsx)");
}

bool readFile(const QString &filePath, QStringList &headers, QList<QStringList> &rows, QString *errMsg)
{
    QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext == "csv")
        return readCsv(filePath, headers, rows, errMsg);
    if (ext == "xlsx")
        return readXlsx(filePath, headers, rows, errMsg);
    if (errMsg) *errMsg = QStringLiteral("不支持的文件格式：%1").arg(ext);
    return false;
}

bool writeFile(const QString &filePath, const QStringList &headers, const QList<QStringList> &rows, QString *errMsg)
{
    QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext == "csv")
        return writeCsv(filePath, headers, rows, errMsg);
    if (ext == "xlsx")
        return writeXlsx(filePath, headers, rows, errMsg);
    if (errMsg) *errMsg = QStringLiteral("不支持的文件格式：%1").arg(ext);
    return false;
}

// ---------- CSV ----------

// 解析一行 CSV（处理引号包裹、转义引号、字段内换行）
static QStringList parseCsvLine(const QString &line, bool &inQuotes, QString &pending)
{
    QStringList fields;
    QString field;
    bool quote = inQuotes;  // 延续上一行的引号状态
    int i = 0;
    if (quote && !pending.isEmpty()) {
        field = pending;
        pending.clear();
    }
    for (; i < line.length(); ++i) {
        QChar c = line.at(i);
        if (quote) {
            if (c == '"') {
                // 检查是否转义引号 ""
                if (i + 1 < line.length() && line.at(i + 1) == '"') {
                    field += '"';
                    ++i;
                } else {
                    quote = false;
                }
            } else {
                field += c;
            }
        } else {
            if (c == '"') {
                quote = true;
            } else if (c == ',') {
                fields << field;
                field.clear();
            } else {
                field += c;
            }
        }
    }
    if (quote) {
        // 引号未闭合（字段含换行），保存到 pending
        pending = field;
        inQuotes = true;
        return QStringList();
    }
    fields << field;
    inQuotes = false;
    return fields;
}

bool readCsv(const QString &filePath, QStringList &headers, QList<QStringList> &rows, QString *errMsg)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errMsg) *errMsg = QStringLiteral("无法打开文件：%1").arg(filePath);
        return false;
    }
    QByteArray raw = file.readAll();
    file.close();
    // 去掉 UTF-8 BOM
    if (raw.startsWith("\xEF\xBB\xBF"))
        raw.remove(0, 3);
    QString content = QString::fromUtf8(raw);

    headers.clear();
    rows.clear();
    bool inQuotes = false;
    QString pending;
    QStringList lines = content.split('\n');
    bool isFirstLine = true;

    for (int li = 0; li < lines.size(); ++li) {
        QString line = lines.at(li);
        // 去掉末尾 \r
        if (line.endsWith('\r'))
            line.chop(1);
        QStringList fields = parseCsvLine(line, inQuotes, pending);
        if (inQuotes) {
            // 字段含换行，继续拼接下一行
            continue;
        }
        // 跳过完全空白的行（仅含一个空字段）
        if (fields.size() == 1 && fields.at(0).trimmed().isEmpty())
            continue;
        if (isFirstLine) {
            headers = fields;
            isFirstLine = false;
        } else {
            rows << fields;
        }
    }
    return true;
}

static QString csvEscape(const QString &field)
{
    if (field.contains(',') || field.contains('"') || field.contains('\n') || field.contains('\r')) {
        QString escaped = field;
        escaped.replace('"', "\"\"");
        return "\"" + escaped + "\"";
    }
    return field;
}

bool writeCsv(const QString &filePath, const QStringList &headers, const QList<QStringList> &rows, QString *errMsg)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errMsg) *errMsg = QStringLiteral("无法写入文件：%1").arg(filePath);
        return false;
    }
    // 写 UTF-8 BOM（便于 Excel 识别中文）
    file.write("\xEF\xBB\xBF");
    QTextStream ts(&file);
    ts.setEncoding(QStringConverter::Utf8);

    auto writeLine = [&ts](const QStringList &fields) {
        QStringList escaped;
        for (const QString &f : fields)
            escaped << csvEscape(f);
        ts << escaped.join(',') << "\n";
    };
    writeLine(headers);
    for (const QStringList &row : rows)
        writeLine(row);
    file.close();
    return true;
}

// ---------- XLSX 写 ----------

static bool isNumber(const QString &s)
{
    static QRegularExpression re("^-?\\d+(\\.\\d+)?$");
    return re.match(s.trimmed()).hasMatch();
}

static QByteArray makeContentTypes()
{
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.writeStartDocument();
    w.writeStartElement("Types");
    w.writeAttribute("xmlns", "http://schemas.openxmlformats.org/package/2006/content-types");
    w.writeEmptyElement("Default");
    w.writeAttribute("Extension", "rels");
    w.writeAttribute("ContentType", "application/vnd.openxmlformats-package.relationships+xml");
    w.writeEmptyElement("Default");
    w.writeAttribute("Extension", "xml");
    w.writeAttribute("ContentType", "application/xml");
    w.writeEmptyElement("Override");
    w.writeAttribute("PartName", "/xl/workbook.xml");
    w.writeAttribute("ContentType", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml");
    w.writeEmptyElement("Override");
    w.writeAttribute("PartName", "/xl/worksheets/sheet1.xml");
    w.writeAttribute("ContentType", "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml");
    w.writeEndElement();
    w.writeEndDocument();
    return out;
}

static QByteArray makeRootRels()
{
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.writeStartDocument();
    w.writeStartElement("Relationships");
    w.writeAttribute("xmlns", "http://schemas.openxmlformats.org/package/2006/relationships");
    w.writeEmptyElement("Relationship");
    w.writeAttribute("Id", "rId1");
    w.writeAttribute("Type", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument");
    w.writeAttribute("Target", "xl/workbook.xml");
    w.writeEndElement();
    w.writeEndDocument();
    return out;
}

static QByteArray makeWorkbook()
{
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.writeStartDocument();
    w.writeStartElement("workbook");
    w.writeAttribute("xmlns", "http://schemas.openxmlformats.org/spreadsheetml/2006/main");
    w.writeAttribute("xmlns:r", "http://schemas.openxmlformats.org/officeDocument/2006/relationships");
    w.writeStartElement("sheets");
    w.writeEmptyElement("sheet");
    w.writeAttribute("name", "Sheet1");
    w.writeAttribute("sheetId", "1");
    w.writeAttribute("r:id", "rId1");
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndDocument();
    return out;
}

static QByteArray makeWorkbookRels()
{
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.writeStartDocument();
    w.writeStartElement("Relationships");
    w.writeAttribute("xmlns", "http://schemas.openxmlformats.org/package/2006/relationships");
    w.writeEmptyElement("Relationship");
    w.writeAttribute("Id", "rId1");
    w.writeAttribute("Type", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet");
    w.writeAttribute("Target", "worksheets/sheet1.xml");
    w.writeEndElement();
    w.writeEndDocument();
    return out;
}

// 列号转字母（1 -> A, 26 -> Z, 27 -> AA）
static QString columnName(int col)
{
    QString name;
    while (col > 0) {
        int rem = (col - 1) % 26;
        name.prepend(QChar('A' + rem));
        col = (col - 1) / 26;
    }
    return name;
}

static QByteArray makeSheet(const QStringList &headers, const QList<QStringList> &rows)
{
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.writeStartDocument();
    w.writeStartElement("worksheet");
    w.writeAttribute("xmlns", "http://schemas.openxmlformats.org/spreadsheetml/2006/main");
    w.writeStartElement("sheetData");

    int rowIndex = 0;
    auto writeRow = [&](const QStringList &values) {
        ++rowIndex;
        w.writeStartElement("row");
        w.writeAttribute("r", QString::number(rowIndex));
        for (int c = 0; c < values.size(); ++c) {
            const QString &v = values.at(c);
            QString ref = columnName(c + 1) + QString::number(rowIndex);
            w.writeStartElement("c");
            w.writeAttribute("r", ref);
            if (v.isEmpty()) {
                // 空单元格，直接结束
            } else if (isNumber(v)) {
                w.writeStartElement("v");
                w.writeCharacters(v.trimmed());
                w.writeEndElement();
            } else {
                w.writeAttribute("t", "inlineStr");
                w.writeStartElement("is");
                w.writeStartElement("t");
                w.writeCharacters(v);
                w.writeEndElement();
                w.writeEndElement();
            }
            w.writeEndElement();
        }
        w.writeEndElement();
    };

    writeRow(headers);
    for (const QStringList &row : rows)
        writeRow(row);

    w.writeEndElement(); // sheetData
    w.writeEndElement(); // worksheet
    w.writeEndDocument();
    return out;
}

bool writeXlsx(const QString &filePath, const QStringList &headers, const QList<QStringList> &rows, QString *errMsg)
{
    QZipWriter zip(filePath);
    if (zip.status() != QZipWriter::NoError) {
        if (errMsg) *errMsg = QStringLiteral("无法创建文件：%1").arg(filePath);
        return false;
    }
    zip.setCreationPermissions(QFile::ReadOwner | QFile::WriteOwner);

    zip.addFile(QStringLiteral("[Content_Types].xml"), makeContentTypes());
    zip.addFile(QStringLiteral("_rels/.rels"), makeRootRels());
    zip.addFile(QStringLiteral("xl/workbook.xml"), makeWorkbook());
    zip.addFile(QStringLiteral("xl/_rels/workbook.xml.rels"), makeWorkbookRels());
    zip.addFile(QStringLiteral("xl/worksheets/sheet1.xml"), makeSheet(headers, rows));

    zip.close();
    if (zip.status() != QZipWriter::NoError) {
        if (errMsg) *errMsg = QStringLiteral("写入 xlsx 失败");
        return false;
    }
    return true;
}

// ---------- XLSX 读 ----------

// 列字母转号（A -> 1, Z -> 26, AA -> 27）
static int columnNumber(const QString &ref)
{
    int col = 0;
    for (QChar c : ref) {
        if (c.isLetter()) {
            col = col * 26 + (c.toUpper().unicode() - 'A' + 1);
        } else {
            break;
        }
    }
    return col;
}

bool readXlsx(const QString &filePath, QStringList &headers, QList<QStringList> &rows, QString *errMsg)
{
    QZipReader zip(filePath);
    if (zip.status() != QZipReader::NoError) {
        if (errMsg) *errMsg = QStringLiteral("无法打开文件：%1").arg(filePath);
        return false;
    }

    // 读取共享字符串表（可选）
    QStringList sharedStrings;
    QByteArray sstData = zip.fileData(QStringLiteral("xl/sharedStrings.xml"));
    if (!sstData.isEmpty()) {
        QXmlStreamReader sr(sstData);
        QString current;
        bool inSi = false;
        while (!sr.atEnd()) {
            sr.readNext();
            if (sr.isStartElement()) {
                if (sr.name() == QStringLiteral("si"))
                    inSi = true;
                else if (sr.name() == QStringLiteral("t") && inSi)
                    current += sr.readElementText();
            } else if (sr.isEndElement()) {
                if (sr.name() == QStringLiteral("si")) {
                    sharedStrings << current;
                    current.clear();
                    inSi = false;
                }
            }
        }
    }

    // 读取第一个工作表
    QByteArray sheetData = zip.fileData(QStringLiteral("xl/worksheets/sheet1.xml"));
    zip.close();
    if (sheetData.isEmpty()) {
        if (errMsg) *errMsg = QStringLiteral("xlsx 中未找到工作表");
        return false;
    }

    headers.clear();
    rows.clear();
    QStringList currentRow;
    int currentRowIndex = -1;
    bool inCell = false;
    bool cellIsString = false;   // t="s"
    bool cellIsInline = false;   // t="inlineStr"
    QString cellRef;
    QString cellValue;
    int targetColumn = 0;

    QXmlStreamReader sr(sheetData);
    while (!sr.atEnd()) {
        sr.readNext();
        if (sr.isStartElement()) {
            QString name = sr.name().toString();
            if (name == QStringLiteral("row")) {
                currentRow.clear();
                currentRowIndex = sr.attributes().value("r").toInt();
                targetColumn = 0;
            } else if (name == QStringLiteral("c")) {
                cellRef = sr.attributes().value("r").toString();
                QString t = sr.attributes().value("t").toString();
                cellIsString = (t == "s");
                cellIsInline = (t == "inlineStr");
                cellValue.clear();
                inCell = true;
            } else if (name == QStringLiteral("v") && inCell) {
                cellValue = sr.readElementText();
            } else if (name == QStringLiteral("t") && inCell && cellIsInline) {
                cellValue += sr.readElementText();
            }
        } else if (sr.isEndElement()) {
            QString name = sr.name().toString();
            if (name == QStringLiteral("c") && inCell) {
                // 计算该单元格的列号，空单元格补空串
                int col = columnNumber(cellRef) - 1;
                while ((int)currentRow.size() < col)
                    currentRow << QString();
                QString finalValue;
                if (cellIsString) {
                    int idx = cellValue.toInt();
                    finalValue = (idx >= 0 && idx < sharedStrings.size()) ? sharedStrings.at(idx) : QString();
                } else {
                    finalValue = cellValue;
                }
                currentRow << finalValue;
                inCell = false;
            } else if (name == QStringLiteral("row")) {
                if (currentRowIndex > 0) {
                    rows << currentRow;
                }
                currentRow.clear();
                currentRowIndex = -1;
            }
        }
    }

    // 第一行作为表头
    if (!rows.isEmpty()) {
        headers = rows.takeFirst();
    }
    return true;
}

} // namespace TableIO
