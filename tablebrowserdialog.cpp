#include "tablebrowserdialog.h"
#include "sqlitedb.h"
#include "tableio.h"
#include "columnselectordialog.h"

#include <QComboBox>
#include <QTableWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>

TableBrowserDialog::TableBrowserDialog(SqliteDB *db, QWidget *parent)
    : QDialog(parent)
    , db(db)
    , filterColumn(-1)
{
    setWindowTitle(tr("期刊数据表浏览"));
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);
    resize(880, 560);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // 顶部：数据表选择 + 字段筛选
    QHBoxLayout *topLayout = new QHBoxLayout();
    topLayout->addWidget(new QLabel(tr("数据表："), this));
    tableCombo = new QComboBox(this);
    const QStringList tables = db->getAllTableNames();
    for (const QString &t : tables) {
        tableCombo->addItem(db->tableChineseName(t), t);
    }
    tableCombo->setMinimumWidth(280);
    topLayout->addWidget(tableCombo);

    topLayout->addSpacing(12);
    topLayout->addWidget(new QLabel(tr("筛选字段："), this));
    filterFieldCombo = new QComboBox(this);
    filterFieldCombo->setMinimumWidth(140);
    topLayout->addWidget(filterFieldCombo);
    filterValueEdit = new QLineEdit(this);
    filterValueEdit->setPlaceholderText(tr("输入筛选内容…"));
    topLayout->addWidget(filterValueEdit, 1);
    QPushButton *btnFilter = new QPushButton(tr("筛选"), this);
    QPushButton *btnClear = new QPushButton(tr("清除"), this);
    topLayout->addWidget(btnFilter);
    topLayout->addWidget(btnClear);
    mainLayout->addLayout(topLayout);

    // 工具栏：字段显示/隐藏 + 恢复默认 + 记录数 + 导出
    QHBoxLayout *toolLayout = new QHBoxLayout();
    columnButton = new QPushButton(tr("选择显示字段"), this);
    toolLayout->addWidget(columnButton);
    QPushButton *btnRestore = new QPushButton(tr("恢复默认"), this);
    toolLayout->addWidget(btnRestore);
    rowCountLabel = new QLabel(tr("共 0 条记录"), this);
    toolLayout->addWidget(rowCountLabel);
    toolLayout->addStretch();
    QPushButton *btnExportAll = new QPushButton(tr("导出全部"), this);
    QPushButton *btnExportSel = new QPushButton(tr("导出选中行"), this);
    toolLayout->addWidget(btnExportAll);
    toolLayout->addWidget(btnExportSel);
    mainLayout->addLayout(toolLayout);

    // 数据表格：可拖拽列宽
    table = new QTableWidget(this);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    mainLayout->addWidget(table, 1);

    connect(tableCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TableBrowserDialog::onTableChanged);
    connect(btnFilter, &QPushButton::clicked, this, &TableBrowserDialog::applyFilter);
    connect(btnClear, &QPushButton::clicked, this, &TableBrowserDialog::clearFilter);
    connect(filterValueEdit, &QLineEdit::returnPressed, this, &TableBrowserDialog::applyFilter);
    connect(btnExportAll, &QPushButton::clicked, this, &TableBrowserDialog::exportAll);
    connect(btnExportSel, &QPushButton::clicked, this, &TableBrowserDialog::exportSelected);
    connect(columnButton, &QPushButton::clicked, this, &TableBrowserDialog::chooseColumns);
    connect(btnRestore, &QPushButton::clicked, this, &TableBrowserDialog::restoreDefault);

    // 加载第一个数据表
    if (tableCombo->count() > 0)
        loadTableData(tableCombo->itemData(0).toString());
}

void TableBrowserDialog::onTableChanged(int index)
{
    Q_UNUSED(index);
    loadTableData(tableCombo->currentData().toString());
}

void TableBrowserDialog::loadTableData(const QString &tableName)
{
    if (tableName.isEmpty())
        return;
    QString err;
    if (!db->readTableData(tableName, headers, rows, &err)) {
        QMessageBox::warning(this, tr("读取失败"), err);
        return;
    }

    // 重置筛选与隐藏状态
    filterColumn = -1;
    filterText.clear();
    hiddenColumns.clear();
    filteredRows.clear();
    for (int i = 0; i < rows.size(); ++i)
        filteredRows << i;

    // 填充筛选字段下拉
    filterFieldCombo->clear();
    filterFieldCombo->addItems(headers);
    filterValueEdit->clear();

    rebuildView();
    table->resizeColumnsToContents();
}

// 打开字段显示/隐藏对话框（勾选表示显示，支持全选/全不选）
void TableBrowserDialog::chooseColumns()
{
    ColumnSelectorDialog dlg(headers, hiddenColumns, this);
    if (dlg.exec() == QDialog::Accepted) {
        hiddenColumns = dlg.hiddenColumns();
        rebuildView();
        table->resizeColumnsToContents();
    }
}

// 恢复默认显示：显示全部字段、清除筛选、恢复列宽
void TableBrowserDialog::restoreDefault()
{
    filterColumn = -1;
    filterText.clear();
    filterValueEdit->clear();
    hiddenColumns.clear();
    filteredRows.clear();
    for (int i = 0; i < rows.size(); ++i)
        filteredRows << i;
    rebuildView();
    table->resizeColumnsToContents();
}

QList<int> TableBrowserDialog::visibleColumnIndexes() const
{
    QList<int> vis;
    for (int c = 0; c < headers.size(); ++c)
        if (!hiddenColumns.contains(c))
            vis << c;
    return vis;
}

QStringList TableBrowserDialog::visibleHeaders() const
{
    QStringList h;
    for (int c : visibleColumnIndexes())
        h << headers[c];
    return h;
}

void TableBrowserDialog::rebuildView()
{
    const QList<int> vis = visibleColumnIndexes();
    table->setColumnCount(vis.size());
    table->setHorizontalHeaderLabels(visibleHeaders());
    table->setRowCount(filteredRows.size());
    for (int r = 0; r < filteredRows.size(); ++r) {
        const QStringList &src = rows[filteredRows[r]];
        for (int c = 0; c < vis.size(); ++c) {
            int srcCol = vis[c];
            QString val = (srcCol < src.size()) ? src[srcCol] : QString();
            table->setItem(r, c, new QTableWidgetItem(val));
        }
    }
    rowCountLabel->setText(tr("共 %1 条记录").arg(filteredRows.size()));
}

void TableBrowserDialog::applyFilter()
{
    filterColumn = filterFieldCombo->currentIndex();
    filterText = filterValueEdit->text();
    filteredRows.clear();
    if (filterText.isEmpty() || filterColumn < 0) {
        for (int i = 0; i < rows.size(); ++i)
            filteredRows << i;
    } else {
        for (int i = 0; i < rows.size(); ++i) {
            const QStringList &r = rows[i];
            if (filterColumn < r.size() && r[filterColumn].contains(filterText, Qt::CaseInsensitive))
                filteredRows << i;
        }
    }
    rebuildView();
}

void TableBrowserDialog::clearFilter()
{
    filterValueEdit->clear();
    filterColumn = -1;
    filterText.clear();
    filteredRows.clear();
    for (int i = 0; i < rows.size(); ++i)
        filteredRows << i;
    rebuildView();
}

void TableBrowserDialog::exportAll()
{
    QList<int> all;
    for (int i = 0; i < rows.size(); ++i)
        all << i;
    exportRows(all, tr("导出全部"));
}

void TableBrowserDialog::exportSelected()
{
    QList<int> sel;
    const QModelIndexList selIdx = table->selectionModel()->selectedRows();
    for (const QModelIndex &idx : selIdx) {
        int row = idx.row();
        if (row >= 0 && row < filteredRows.size())
            sel << filteredRows[row];
    }
    if (sel.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("请先选中要导出的行（可按住 Ctrl 多选）。"));
        return;
    }
    exportRows(sel, tr("导出选中行"));
}

void TableBrowserDialog::exportRows(const QList<int> &origRowIndexes, const QString &title)
{
    if (origRowIndexes.isEmpty())
        return;
    QString defaultName = tableCombo->currentData().toString() + QStringLiteral(".csv");
    QString file = QFileDialog::getSaveFileName(this, title, defaultName, TableIO::fileFilter());
    if (file.isEmpty())
        return;
    if (!file.contains('.'))
        file += QStringLiteral(".csv");

    QList<QStringList> out;
    for (int idx : origRowIndexes) {
        if (idx >= 0 && idx < rows.size())
            out << rows[idx];
    }
    QString err;
    if (!TableIO::writeFile(file, headers, out, &err)) {
        QMessageBox::warning(this, tr("导出失败"), err);
        return;
    }
    QMessageBox::information(this, tr("导出成功"),
        tr("已导出 %1 条记录到：\n%2").arg(out.size()).arg(file));
}
