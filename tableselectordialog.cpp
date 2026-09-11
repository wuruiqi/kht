#include "tableselectordialog.h"
#include "ui_tableselectordialog.h"
#include "sqlitedb.h"

#include <QPushButton>
#include <QHBoxLayout>
#include <QHeaderView>

TableSelectorDialog::TableSelectorDialog(const QStringList &tables, const QStringList &selectedTables, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::TableSelectorDialog)
{
    ui->setupUi(this);
    this->setWindowTitle("期刊数据表选择");
    this->resize(480, 460);

    QVBoxLayout *layout = new QVBoxLayout(this);

    // 表格：第一列为可选的数据表名，第二列为中文备注，方便用户直接识别
    tableWidget = new QTableWidget(this);
    tableWidget->setColumnCount(2);
    tableWidget->setHorizontalHeaderLabels(QStringList() << "数据表" << "备注");
    tableWidget->setSelectionMode(QAbstractItemView::NoSelection);
    tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableWidget->verticalHeader()->setVisible(false);
    tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tableWidget->horizontalHeader()->setStretchLastSection(true);
    tableWidget->setAlternatingRowColors(true);

    foreach (const QString &table, tables) {
        int row = tableWidget->rowCount();
        tableWidget->insertRow(row);
        // 第一列：可选中的表名
        QTableWidgetItem *nameItem = new QTableWidgetItem(table);
        nameItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        nameItem->setCheckState(selectedTables.contains(table) ? Qt::Checked : Qt::Unchecked);
        tableWidget->setItem(row, 0, nameItem);
        // 第二列：中文备注
        QTableWidgetItem *remarkItem = new QTableWidgetItem(SqliteDB::tableChineseName(table));
        remarkItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        tableWidget->setItem(row, 1, remarkItem);
    }

    // 全选 / 全部取消按钮
    QHBoxLayout *selectLayout = new QHBoxLayout();
    QPushButton *btnSelectAll = new QPushButton("全选", this);
    QPushButton *btnSelectNone = new QPushButton("全部取消", this);
    selectLayout->addWidget(btnSelectAll);
    selectLayout->addWidget(btnSelectNone);
    selectLayout->addStretch();

    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        Qt::Horizontal, this);

    layout->addWidget(tableWidget);
    layout->addLayout(selectLayout);
    layout->addWidget(buttonBox);

    // 连接信号槽
    connect(btnSelectAll, &QPushButton::clicked, this, &TableSelectorDialog::selectAll);
    connect(btnSelectNone, &QPushButton::clicked, this, &TableSelectorDialog::selectNone);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

TableSelectorDialog::~TableSelectorDialog()
{
    delete ui;
}

// 获取选中表名（按顺序）
const QStringList TableSelectorDialog::selectedTables()
{
    QStringList result;
    for(int i = 0; i < tableWidget->rowCount(); ++i) {
        QTableWidgetItem *item = tableWidget->item(i, 0);
        if(item && item->checkState() == Qt::Checked) {
            result << item->text();
        }
    }
    return result;
}

// 获取全部表名（按顺序）
const QStringList TableSelectorDialog::getAllTables()
{
    QStringList allTableNames;
    for(int i = 0; i < tableWidget->rowCount(); ++i) {
        QTableWidgetItem *item = tableWidget->item(i, 0);
        if(item) {
            allTableNames << item->text();
        }
    }
    return allTableNames;
}

void TableSelectorDialog::selectAll()
{
    setAllChecked(true);
}

void TableSelectorDialog::selectNone()
{
    setAllChecked(false);
}

//根据当前已选表名刷新勾选状态，用于对话框每次打开前与主界面同步
void TableSelectorDialog::setSelectedTables(const QStringList &selectedTables)
{
    for(int i = 0; i < tableWidget->rowCount(); ++i) {
        QTableWidgetItem *item = tableWidget->item(i, 0);
        if(item) {
            item->setCheckState(selectedTables.contains(item->text()) ? Qt::Checked : Qt::Unchecked);
        }
    }
}

void TableSelectorDialog::setAllChecked(bool checked)
{
    for(int i = 0; i < tableWidget->rowCount(); ++i) {
        QTableWidgetItem *item = tableWidget->item(i, 0);
        if(item) {
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        }
    }
}
