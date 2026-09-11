#include "tablemanagerdialog.h"
#include "sqlitedb.h"
#include "tableio.h"

#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QLineEdit>
#include <QFileInfo>
#include <QRegularExpression>

TableManagerDialog::TableManagerDialog(SqliteDB *db, QWidget *parent)
    : QDialog(parent)
    , db(db)
{
    setWindowTitle(tr("期刊数据表管理"));
    resize(560, 520);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QLabel *titleLabel = new QLabel(tr("数据表列表（点击选中后进行管理）："), this);
    mainLayout->addWidget(titleLabel);

    list = new QListWidget(this);
    mainLayout->addWidget(list, 1);

    // 命名格式说明（导入要求）
    QLabel *ruleLabel = new QLabel(
        tr("【导入命名格式与要求】\n"
           "① 表名推荐「类别前缀 + 4位年份」，如 JCR2026、FQBJCR2026、XR2027、CCF2027、GJQKYJMD2026；\n"
           "② 表名仅含字母/数字/下划线，不能以数字开头，不能与现有表重名；\n"
           "③ 数据文件首行为表头，必须包含 Journal（期刊名称）字段；\n"
           "④ 支持 .csv 与 .xlsx 两种格式。"), this);
    ruleLabel->setWordWrap(true);
    ruleLabel->setStyleSheet("color:#555; background:#f7f7f7; border:1px solid #ddd; padding:6px;");
    mainLayout->addWidget(ruleLabel);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *btnImport = new QPushButton(tr("导入"), this);
    QPushButton *btnExport = new QPushButton(tr("导出"), this);
    QPushButton *btnRemark = new QPushButton(tr("编辑备注"), this);
    QPushButton *btnDelete = new QPushButton(tr("删除"), this);
    QPushButton *btnUp = new QPushButton(tr("上移"), this);
    QPushButton *btnDown = new QPushButton(tr("下移"), this);
    QPushButton *btnClose = new QPushButton(tr("关闭"), this);
    btnLayout->addWidget(btnImport);
    btnLayout->addWidget(btnExport);
    btnLayout->addWidget(btnRemark);
    btnLayout->addWidget(btnDelete);
    btnLayout->addStretch();
    btnLayout->addWidget(btnUp);
    btnLayout->addWidget(btnDown);
    btnLayout->addWidget(btnClose);
    mainLayout->addLayout(btnLayout);

    connect(btnImport, &QPushButton::clicked, this, &TableManagerDialog::importTable);
    connect(btnExport, &QPushButton::clicked, this, &TableManagerDialog::exportTable);
    connect(btnRemark, &QPushButton::clicked, this, &TableManagerDialog::editRemark);
    connect(btnDelete, &QPushButton::clicked, this, &TableManagerDialog::deleteTable);
    connect(btnUp, &QPushButton::clicked, this, &TableManagerDialog::moveUp);
    connect(btnDown, &QPushButton::clicked, this, &TableManagerDialog::moveDown);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);

    reloadList();
}

void TableManagerDialog::reloadList()
{
    orderedTables = db->getAllTableNames();
    list->clear();
    for (const QString &t : orderedTables) {
        QListWidgetItem *item = new QListWidgetItem(
            QStringLiteral("%1  (%2)").arg(db->tableChineseName(t), t));
        item->setData(Qt::UserRole, t);
        list->addItem(item);
    }
}

int TableManagerDialog::currentRow() const
{
    return list->currentRow();
}

QString TableManagerDialog::currentTableName() const
{
    QListWidgetItem *item = list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void TableManagerDialog::selectByName(const QString &name)
{
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->data(Qt::UserRole).toString() == name) {
            list->setCurrentRow(i);
            return;
        }
    }
}

void TableManagerDialog::importTable()
{
    QString file = QFileDialog::getOpenFileName(this, tr("导入数据表"), QString(), TableIO::fileFilter());
    if (file.isEmpty())
        return;

    QStringList headers;
    QList<QStringList> rows;
    QString err;
    if (!TableIO::readFile(file, headers, rows, &err)) {
        QMessageBox::warning(this, tr("导入失败"), err);
        return;
    }
    if (headers.isEmpty() || rows.isEmpty()) {
        QMessageBox::warning(this, tr("导入失败"), tr("文件为空或缺少数据行。"));
        return;
    }

    // 校验必须包含 Journal 字段
    bool hasJournal = false;
    for (const QString &h : headers) {
        if (h.compare(QStringLiteral("Journal"), Qt::CaseInsensitive) == 0) {
            hasJournal = true;
            break;
        }
    }
    if (!hasJournal) {
        QMessageBox::warning(this, tr("字段校验"),
            tr("数据表头中缺少 Journal（期刊名称）字段，无法导入。\n请检查文件后重试。"));
        return;
    }

    // 输入表名（默认用文件名）
    QString defaultName = QFileInfo(file).baseName();
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("数据表命名"),
        tr("请输入数据表名称（推荐：类别前缀 + 4位年份，如 JCR2026）："),
        QLineEdit::Normal, defaultName, &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    name = name.trimmed();

    // 校验命名格式
    static const QRegularExpression re("^[A-Za-z_][A-Za-z0-9_]*$");
    if (!re.match(name).hasMatch()) {
        QMessageBox::warning(this, tr("命名格式错误"),
            tr("表名只能由字母、数字、下划线组成，且不能以数字开头。"));
        return;
    }
    if (db->getAllTableNames().contains(name)) {
        QMessageBox::warning(this, tr("命名冲突"), tr("已存在同名数据表：%1").arg(name));
        return;
    }

    if (!db->importTable(name, headers, rows, &err)) {
        QMessageBox::warning(this, tr("导入失败"), err);
        return;
    }

    reloadList();
    selectByName(name);
    emit tablesChanged();
    QMessageBox::information(this, tr("导入成功"),
        tr("已导入数据表 %1，共 %2 条记录。").arg(name).arg(rows.size()));
}

void TableManagerDialog::exportTable()
{
    QString tn = currentTableName();
    if (tn.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("请先在列表中选择一个数据表。"));
        return;
    }
    QString file = QFileDialog::getSaveFileName(this, tr("导出数据表"), tn + QStringLiteral(".csv"), TableIO::fileFilter());
    if (file.isEmpty())
        return;
    if (!file.contains('.'))
        file += QStringLiteral(".csv");

    QStringList headers;
    QList<QStringList> rows;
    QString err;
    if (!db->readTableData(tn, headers, rows, &err)) {
        QMessageBox::warning(this, tr("导出失败"), err);
        return;
    }
    if (!TableIO::writeFile(file, headers, rows, &err)) {
        QMessageBox::warning(this, tr("导出失败"), err);
        return;
    }
    QMessageBox::information(this, tr("导出成功"),
        tr("已导出 %1 条记录到：\n%2").arg(rows.size()).arg(file));
}

void TableManagerDialog::editRemark()
{
    QString tn = currentTableName();
    if (tn.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("请先在列表中选择一个数据表。"));
        return;
    }
    bool ok = false;
    QString remark = QInputDialog::getText(this, tr("编辑备注"),
        tr("请输入该数据表的备注/显示名称："),
        QLineEdit::Normal, db->tableChineseName(tn), &ok);
    if (!ok)
        return;
    remark = remark.trimmed();
    if (remark.isEmpty())
        remark = tn;
    db->setTableRemark(tn, remark);
    reloadList();
    selectByName(tn);
    emit tablesChanged();
}

void TableManagerDialog::deleteTable()
{
    QString tn = currentTableName();
    if (tn.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("请先在列表中选择一个数据表。"));
        return;
    }
    // 强确认：要求输入数据表名
    bool ok = false;
    QString input = QInputDialog::getText(this, tr("删除确认"),
        tr("删除后不可恢复！\n请输入数据表名「%1」以确认删除：").arg(tn),
        QLineEdit::Normal, QString(), &ok);
    if (!ok)
        return;
    if (input.trimmed() != tn) {
        QMessageBox::warning(this, tr("已取消"), tr("输入的数据表名不匹配，删除已取消。"));
        return;
    }
    QString err;
    if (!db->deleteTable(tn, &err)) {
        QMessageBox::warning(this, tr("删除失败"), err);
        return;
    }
    reloadList();
    emit tablesChanged();
    QMessageBox::information(this, tr("删除成功"), tr("已删除数据表 %1。").arg(tn));
}

void TableManagerDialog::moveUp()
{
    int row = currentRow();
    if (row <= 0)
        return;
    QString moved = orderedTables[row - 1];
    orderedTables.swapItemsAt(row, row - 1);
    db->setTableOrder(orderedTables);
    reloadList();
    selectByName(moved);
    emit tablesChanged();
}

void TableManagerDialog::moveDown()
{
    int row = currentRow();
    if (row < 0 || row >= orderedTables.size() - 1)
        return;
    QString moved = orderedTables[row + 1];
    orderedTables.swapItemsAt(row, row + 1);
    db->setTableOrder(orderedTables);
    reloadList();
    selectByName(moved);
    emit tablesChanged();
}
