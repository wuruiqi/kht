#include "columnselectordialog.h"

#include <QTableWidget>
#include <QTableWidgetItem>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QDialogButtonBox>

ColumnSelectorDialog::ColumnSelectorDialog(const QStringList &allHeaders, const QSet<int> &hiddenColumns, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("选择显示字段"));
    resize(320, 440);

    QVBoxLayout *layout = new QVBoxLayout(this);

    tableWidget = new QTableWidget(this);
    tableWidget->setColumnCount(1);
    tableWidget->setHorizontalHeaderLabels(QStringList() << tr("字段名"));
    tableWidget->setSelectionMode(QAbstractItemView::NoSelection);
    tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableWidget->verticalHeader()->setVisible(false);
    tableWidget->horizontalHeader()->setStretchLastSection(true);
    tableWidget->setAlternatingRowColors(true);

    for (int i = 0; i < allHeaders.size(); ++i) {
        int row = tableWidget->rowCount();
        tableWidget->insertRow(row);
        QTableWidgetItem *item = new QTableWidgetItem(allHeaders[i]);
        item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setCheckState(hiddenColumns.contains(i) ? Qt::Unchecked : Qt::Checked);
        tableWidget->setItem(row, 0, item);
    }

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *btnSelectAll = new QPushButton(tr("全选"), this);
    QPushButton *btnSelectNone = new QPushButton(tr("全不选"), this);
    btnLayout->addWidget(btnSelectAll);
    btnLayout->addWidget(btnSelectNone);
    btnLayout->addStretch();

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);

    layout->addWidget(tableWidget);
    layout->addLayout(btnLayout);
    layout->addWidget(buttonBox);

    connect(btnSelectAll, &QPushButton::clicked, this, &ColumnSelectorDialog::selectAll);
    connect(btnSelectNone, &QPushButton::clicked, this, &ColumnSelectorDialog::selectNone);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QSet<int> ColumnSelectorDialog::hiddenColumns() const
{
    QSet<int> hidden;
    for (int i = 0; i < tableWidget->rowCount(); ++i) {
        QTableWidgetItem *item = tableWidget->item(i, 0);
        if (item && item->checkState() != Qt::Checked)
            hidden.insert(i);
    }
    return hidden;
}

void ColumnSelectorDialog::selectAll()
{
    setAllChecked(true);
}

void ColumnSelectorDialog::selectNone()
{
    setAllChecked(false);
}

void ColumnSelectorDialog::setAllChecked(bool checked)
{
    for (int i = 0; i < tableWidget->rowCount(); ++i) {
        QTableWidgetItem *item = tableWidget->item(i, 0);
        if (item)
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
}
