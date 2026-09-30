#include "kht.h"
#include "./ui_kht.h"
#include "tablebrowserdialog.h"
#include "tablemanagerdialog.h"
#include <QClipboard>
#include <QMessageBox>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QTimer>
#include <QCompleter>
#include <QMimeData>
#include <QMenu>
#include <QAction>
#include <QGroupBox>
#include <QCheckBox>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QBrush>

const QString Kht::author = "Ruiqi_Wu";
const QString Kht::version = "v1.1";
const QString Kht::email = "rqwu@haut.edu.cn";
const QString Kht::codeURL = "https://github.com/wuruiqi/kht";
const QString Kht::updateURL = "https://github.com/wuruiqi/kht/releases";
const QString Kht::windowTitile = tr("刊会通");
const QString Kht::appDisplayName = tr("刊会通 KanHuiTong");
const QString Kht::logoIconName = ":/image/jcr-logo.jpg";
const QString Kht::datasetName = "jcr.db";  //数据集暂时无法使用资源文件；在程序自启动时，程序的运行目录是C:/WINDOWS/system32而不是程序目录，因此需要结合QApplication::applicationFilePath()修改
const QString Kht::defaultJournal = "National Science Review";

Kht::Kht(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Kht)
{
    ui->setupUi(this);
    this->setWindowTitle(windowTitile);

    //获取程序运行信息
    appName = QApplication::applicationName();//程序名称
    appDir = QDir(QApplication::applicationDirPath());//程序目录（QDir类型）
    appPath = QApplication::applicationFilePath();// 程序路径

    qDebug() << "start check:" << appName << appDir.path() << appPath;

    //设置菜单
    menu = new QMenu();
    menu->addAction(ui->actionSelectTable);
    menu->addAction(ui->actionAbout);
    menu->addSeparator();//添加分隔线
    menu->addAction(ui->actionExit);
    //关联托盘菜单响应
    connect(ui->actionSelectTable, SIGNAL(triggered()), this, SLOT(show_selectTable()));
    connect(ui->actionAbout, SIGNAL(triggered()), this, SLOT(show_about()));
    connect(ui->actionExit, SIGNAL(triggered()), this, SLOT(OnExit()));

    //设置系统托盘
    m_systray.setToolTip(windowTitile);//设置提示文字
    m_systray.setIcon(QIcon(logoIconName));//设置托盘图标
    m_systray.setContextMenu(menu);//托盘菜单项
    m_systray.show();//显示托盘
    connect(&m_systray, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(OnSystemTrayClicked(QSystemTrayIcon::ActivationReason)));//关联托盘事件

    //设置剪切板监听
    connect(QApplication::clipboard(), SIGNAL(dataChanged()), this, SLOT(getClipboard()));

    //初始化期刊数据库
    sqliteDB = new SqliteDB(appDir, datasetName);

    //检索字段增加“CN号”（中文期刊常用检索键；无该字段的表会自动变灰）
    ui->comboBox_searchField->addItem(QStringLiteral("CN号"));

    //设置期刊名称输入框提示文字
    ui->lineEdit_journalName->setPlaceholderText(cueWords[0]);

    //使用默认期刊进行查询，设置界面初始默认显示
//    run(defaultJournal);

    //读取程序运行参数
    settings = new QSettings(author, appName);
    autoStart = settings->value("autoStart").toBool();
    exit2Taskbar = settings->value("exit2Taskbar").toBool();
    monitorClipboard = settings->value("monitorClipboard").toBool();
    autoActivateWindow = settings->value("autoActivateWindow").toBool();
    ui->checkBox_autoStart->setChecked(autoStart);
    ui->checkBox_exit2Taskbar->setChecked(exit2Taskbar);
    ui->checkBox_monitorClipboard->setChecked(monitorClipboard);
    ui->checkBox_autoActivateWindow->setChecked(autoActivateWindow);
    QStringList old_selectedTables = settings->value("selectedTables").toStringList();
    if(old_selectedTables.isEmpty()){
        //默认只勾选：中科院2025分区(FQBJCR2025) 和 2026新锐分区(XR2026)
        selectedTables.clear();
        const QStringList defaultTables = {"FQBJCR2025", "XR2026"};
        for (const QString &item : sqliteDB->getAllTableNames()) {
            if (defaultTables.contains(item)) {
                selectedTables.append(item);
            }
        }
    }
    else{
        //检查选择的数据表与数据库中数据表的一致性，避免数据库升级时删除了一些表
        selectedTables.clear();
        for (const QString &item : sqliteDB->getAllTableNames()) {
            if (old_selectedTables.contains(item)) {
                selectedTables.append(item);
            }
        }
    }
    sqliteDB->selectTableNames(selectedTables);

    //设置期刊输入自动联想
    refreshCompleter();

    //移除底部设置行（其功能整合到顶部导航栏），保持主界面干净
    ui->verticalLayout->removeItem(ui->horizontalLayout_3);
    ui->checkBox_autoStart->hide();
    ui->checkBox_exit2Taskbar->hide();
    ui->checkBox_monitorClipboard->hide();
    ui->checkBox_autoActivateWindow->hide();
    ui->toolButton_list->hide();

    //构建顶部导航栏
    buildNavigationBar();

    //构建主界面数据表勾选面板
    buildTableSelectPanel();

    //初始化数据集选择窗口
    selectTableDialog = new TableSelectorDialog(sqliteDB->getAllTableNames(), selectedTables, this);

    //初始化关于窗口
    aboutDialog = new AboutDialog(appDisplayName, version, author, email, codeURL, updateURL, this);

    //检查程序自启动设置是否有效
    setAutoStart();
}

Kht::~Kht()
{
    //存储程序运行参数
    settings->setValue("autoStart", autoStart);
    settings->setValue("exit2Taskbar", exit2Taskbar);
    settings->setValue("monitorClipboard", monitorClipboard);
    settings->setValue("autoActivateWindow", autoActivateWindow);
    settings->setValue("selectedTables", selectedTables);

    delete menu;
    delete aboutDialog;
    delete ui;
    delete sqliteDB;
    delete settings;
}

void Kht::on_pushButton_selectJournal_clicked()
{
    run(ui->lineEdit_journalName->text());
}

void Kht::on_lineEdit_journalName_returnPressed()
{
    on_pushButton_selectJournal_clicked();
}

void Kht::run(const QString &input)
{
    //输入简化，首尾空格清除，中间空格均变为1个，便于剪切板复制不精确时有效性
    QString tempJournalName = input.simplified();
    // //忽略重复查询
    // if(journalName == tempJournalName){
    //     return;
    // }
    journalName = tempJournalName;
    //检查输入是否为空
    if(journalName.isEmpty()){
//        ui->lineEdit_journalName->setText(cueWords[0]);
        return;
    }
    //检查输入是否在期刊数据库中（含规范化变体，如全角括号/连字符差异）；
    //如果输入为带'.'的缩写，删除'.'后重新检查
    {
        QString alt = journalName;
        alt.remove('.');
        if(!sqliteDB->hasKey(journalName) && !sqliteDB->hasKey(alt)){
            if(!ui->lineEdit_journalName->text().contains(cueWords[1])){
                ui->lineEdit_journalName->setText(cueWords[1] + ui->lineEdit_journalName->text());
            }
            return;
        }
        if(sqliteDB->hasKey(alt) && alt != journalName)
            journalName = alt;   //命中去点后的写法，后续用该值查询
    }
    //输入正确，执行查询
    qDebug() << "select the journal:" << journalName;
    journalInfo = sqliteDB->getJournalInfo(journalName);
    updateGUI();
}

void Kht::updateGUI()
{
    ui->tableView_journalInformation->setShowGrid(true);
    ui->tableView_journalInformation->setGridStyle(Qt::DashLine);
    ui->tableView_journalInformation->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    QStandardItemModel* model = new QStandardItemModel();
    model->setColumnCount(2);

    QString prevTable;
    QString prevCategory;
    int row = 0;
    for(int i = 0; i < journalInfo.size(); i++){
        const JournalField &info = journalInfo[i];
        // 类别变化时插入家族标题行（外文期刊/中文核心期刊）
        QString category = SqliteDB::categoryOf(info.table);
        if(category != prevCategory){
            QFont familyFont;
            familyFont.setItalic(true);
            QStandardItem* familyItem0 = new QStandardItem(QStringLiteral("── %1 ──").arg(category));
            QStandardItem* familyItem1 = new QStandardItem(QString());
            familyItem0->setBackground(QBrush(QColor(220, 224, 230)));
            familyItem1->setBackground(QBrush(QColor(220, 224, 230)));
            familyItem0->setFont(familyFont);
            familyItem0->setTextAlignment(Qt::AlignCenter);
            familyItem0->setEditable(false);
            familyItem1->setEditable(false);
            model->setItem(row, 0, familyItem0);
            model->setItem(row, 1, familyItem1);
            row++;
            prevCategory = category;
        }
        // 表变化时插入表名标题行（分割线），便于区分各表来源
        if(info.table != prevTable){
            QFont titleFont;
            titleFont.setBold(true);
            QStandardItem* tableTitleItem = new QStandardItem(SqliteDB::tableChineseName(info.table));
            tableTitleItem->setBackground(color_tableHeader);
            tableTitleItem->setForeground(QBrush(Qt::white));
            tableTitleItem->setFont(titleFont);
            tableTitleItem->setTextAlignment(Qt::AlignCenter);
            tableTitleItem->setEditable(false);
            QStandardItem* tableNameItem = new QStandardItem(info.table);
            tableNameItem->setBackground(color_tableHeader);
            tableNameItem->setForeground(QBrush(Qt::white));
            tableNameItem->setFont(titleFont);
            tableNameItem->setTextAlignment(Qt::AlignCenter);
            tableNameItem->setEditable(false);
            model->setItem(row, 0, tableTitleItem);
            model->setItem(row, 1, tableNameItem);
            row++;
            prevTable = info.table;
        }
        QStandardItem* key_item = new QStandardItem(info.field);
        QStandardItem* value_item = new QStandardItem(info.value);
        key_item->setEditable(false);
        value_item->setEditable(false);
        // 设置分割线
        if(info.field.contains("年份")){
            key_item->setBackground(color_header);
            value_item->setBackground(color_header);
        }
        // 重要条目设置底色
        if(info.field.contains("IF Quartile") or
            info.field.contains("CCF推荐类型") or info.field.contains("预警") or
            info.field.contains("大类分区") or info.field.contains("Top") or
            info.field.contains("标注")){
            key_item->setBackground(color_highlight);
            value_item->setBackground(color_highlight);
        }
        model->setItem(row, 0, key_item);
        model->setItem(row, 1, value_item);
        row++;
    }

    ui->tableView_journalInformation->setModel(model);
    ui->lineEdit_journalName->setText(journalName);
    if(autoActivateWindow){
        this->showNormal();
        this->activateWindow(); //激活窗口到前台
    }
}

void Kht::setAutoStart()
{
    QString nativeAppPath = QDir::toNativeSeparators(appPath);
    QString autoStartValue = nativeAppPath + " autoStart";//便于判断程序是否为自启动，注意参数前面有空格
    QString regPath = "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";//无需管理员权限，写入当前用户注册表
    QSettings reg(regPath, QSettings::NativeFormat);
    QString val = reg.value(appName).toString();// 如果此键不存在，则返回的是空字符串
    if(val != autoStartValue & autoStart){
        reg.setValue(appName,autoStartValue);
    }
    else if(val == autoStartValue & !autoStart){//移除自启动
        reg.remove(appName);
    }
}

void Kht::closeEvent(QCloseEvent *event)
{
    if(exit2Taskbar){
        this->hide();
        event->ignore();
    }
    else{
        event->accept();
    }
}

void Kht::getClipboard()
{
    if(monitorClipboard){
        const QClipboard *clipboard = QApplication::clipboard();
        const QMimeData *mimeData = clipboard->mimeData();
        if (mimeData->hasText()) {
            run(clipboard->text());
        }
    }
}

void Kht::on_checkBox_autoStart_stateChanged(int arg1)
{
    autoStart = arg1;
    setAutoStart();
}

void Kht::on_checkBox_exit2Taskbar_stateChanged(int arg1)
{
    exit2Taskbar = arg1;
}

void Kht::on_checkBox_autoActivateWindow_stateChanged(int arg1)
{
    autoActivateWindow = arg1;
}

void Kht::on_checkBox_monitorClipboard_stateChanged(int arg1)
{
    monitorClipboard = arg1;
}

int Kht::OnSystemTrayClicked(QSystemTrayIcon::ActivationReason reason)
{
    if(reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick){
        // 显示主窗口
        this->showNormal();
    }
    return 0;
}

int Kht::OnExit()
{
    QApplication::exit(0);
    return 0;
}

void Kht::on_lineEdit_journalName_textEdited(const QString &arg1)
{
    //编辑时自动清除任何提示文字（请检查期刊名称！/请至少选择一个表！等），保证输入无需手动删除
    QString text = arg1;
    bool changed = false;
    for(const QString &cue : cueWords){
        if(text.contains(cue)){
            text.remove(cue);
            changed = true;
        }
    }
    if(changed){
        ui->lineEdit_journalName->setText(text);
    }
}

void Kht::on_toolButton_list_clicked()
{
    QPoint pos;
    pos.setX(0);
    pos.setY(ui->toolButton_list->sizeHint().height());
    menu->exec(ui->toolButton_list->mapToGlobal(pos));
}

//检索字段下拉切换（期刊名称/ISSN/EISSN）
void Kht::on_comboBox_searchField_currentIndexChanged(int index)
{
    QString fieldName;
    QString placeholder;
    switch(index){
    case 0: fieldName = "Journal"; placeholder = "请输入期刊名称！"; break;
    case 1: fieldName = "ISSN";    placeholder = "请输入ISSN！";      break;
    case 2: fieldName = "EISSN";   placeholder = "请输入EISSN！";     break;
    case 3: fieldName = QStringLiteral("CN号"); placeholder = QStringLiteral("请输入CN号！"); break;
    default: fieldName = "Journal"; placeholder = "请输入期刊名称！"; break;
    }
    ui->lineEdit_journalName->setPlaceholderText(placeholder);

    //更新检索字段
    sqliteDB->setSearchField(fieldName);
    //无对应字段的表变灰不可选
    updateTableAvailability(fieldName);

    //刷新输入联想
    refreshCompleter();

    //清空输入和查询结果，等待用户按新检索字段输入
    ui->lineEdit_journalName->clear();
    journalInfo.clear();
    updateGUI();
}

//根据当前检索字段，更新主界面各数据表勾选项的可用状态（无对应字段的表变灰）
void Kht::updateTableAvailability(const QString &field)
{
    const QStringList availableTables = sqliteDB->getTablesWithField(field);
    for(QCheckBox *cb : tableCheckBoxes){
        const QString table = cb->property("tableName").toString();
        cb->setEnabled(availableTables.contains(table));
    }
}

//选择需要查询的表后，更新查询信息、当前查询结果和期刊输入自动联想
void Kht::show_selectTable()
{
    // selectTableDialog->show();
    selectTableDialog->setSelectedTables(selectedTables);//打开前同步当前主界面勾选状态
    if(selectTableDialog->exec() == QDialog::Accepted) {
        selectedTables = selectTableDialog->selectedTables();
        updateTableCheckBoxes();//同步主界面勾选状态
        applySelectedTables();
    }
}

//构建主界面数据表勾选面板（按数据集类别分为外文期刊/中文核心期刊两组，受顶部范围切换过滤）
void Kht::buildTableSelectPanel()
{
    tableSelectGroupBox = new QGroupBox(tr("选择数据表（勾选后即时生效）"), this);
    QVBoxLayout *groupLayout = new QVBoxLayout(tableSelectGroupBox);

    //全选 / 全不选快捷按键
    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *btnSelectAll = new QPushButton(tr("全选"), tableSelectGroupBox);
    QPushButton *btnSelectNone = new QPushButton(tr("全不选"), tableSelectGroupBox);
    btnLayout->addWidget(btnSelectAll);
    btnLayout->addWidget(btnSelectNone);
    btnLayout->addStretch();
    groupLayout->addLayout(btnLayout);

    //按类别分组：外文期刊 / 中文核心期刊；各组内以中文备注名称显示，3列网格
    foreignGroupBox = new QGroupBox(tr("外文期刊（分区/影响因子/CCF/预警）"), tableSelectGroupBox);
    chineseGroupBox = new QGroupBox(tr("中文核心期刊（CSCD/CSSCI/北大核心）"), tableSelectGroupBox);
    QGridLayout *gridForeign = new QGridLayout(foreignGroupBox);
    QGridLayout *gridChinese = new QGridLayout(chineseGroupBox);
    const int columns = 3;
    tableCheckBoxes.clear();
    int rowF = 0, rowC = 0, colF = 0, colC = 0;
    const QStringList allTables = sqliteDB->getAllTableNames();
    for(const QString &table : allTables){
        QCheckBox *cb = new QCheckBox(SqliteDB::tableChineseName(table));
        cb->setToolTip(table);
        cb->setProperty("tableName", table);
        cb->setChecked(selectedTables.contains(table));
        connect(cb, &QCheckBox::toggled, this, &Kht::onTableSelectToggled);
        if(SqliteDB::categoryOf(table) == QStringLiteral("中文核心期刊")){
            gridChinese->addWidget(cb, rowC, colC);
            if(++colC >= columns){ colC = 0; ++rowC; }
            cb->setParent(chineseGroupBox);
        } else {
            gridForeign->addWidget(cb, rowF, colF);
            if(++colF >= columns){ colF = 0; ++rowF; }
        }
        tableCheckBoxes.append(cb);
    }
    QVBoxLayout *subLayout = new QVBoxLayout();
    subLayout->addWidget(foreignGroupBox);
    subLayout->addWidget(chineseGroupBox);
    groupLayout->addLayout(subLayout);

    connect(btnSelectAll, &QPushButton::clicked, this, &Kht::selectAllTables);
    connect(btnSelectNone, &QPushButton::clicked, this, &Kht::selectNoTables);

    //追加到主布局末尾（顶部导航栏、检索区、结果表之后）
    ui->verticalLayout->addWidget(tableSelectGroupBox);

    //按当前范围过滤分组可见性
    onScopeChanged(scopeCombo ? scopeCombo->currentIndex() : 0);
}

//数据集范围切换：仅过滤分组面板可见性，不改变已勾选的表
void Kht::onScopeChanged(int index)
{
    if(!foreignGroupBox || !chineseGroupBox)
        return;
    foreignGroupBox->setVisible(index != 2);       // 2 = 中文核心期刊
    chineseGroupBox->setVisible(index != 1);       // 1 = 外文期刊
}

//根据 selectedTables 同步主界面勾选状态
void Kht::updateTableCheckBoxes()
{
    for(QCheckBox *cb : tableCheckBoxes){
        const QString table = cb->property("tableName").toString();
        QSignalBlocker blocker(cb);
        cb->setChecked(selectedTables.contains(table));
    }
}

//应用当前选中的数据表
void Kht::applySelectedTables()
{
    sqliteDB->selectTableNames(selectedTables);
    refreshCompleter();

    if(selectedTables.isEmpty()){
        ui->lineEdit_journalName->setText(cueWords[2]);
        return;
    }
    //有表被选中：清除"请至少选择一个表！"等提示文字，恢复默认placeholder（请输入期刊名称）
    QString input = ui->lineEdit_journalName->text();
    if(input == cueWords[2] || input == cueWords[0]){
        ui->lineEdit_journalName->clear();
        input.clear();
    }
    //清除"请检查期刊名称！"前缀
    if(input.startsWith(cueWords[1])){
        input = input.mid(cueWords[1].length());
        ui->lineEdit_journalName->setText(input);
    }
    //仅当输入框为有效检索值时才重新查询
    if(!input.isEmpty()){
        run(input);
    }
}

//主界面勾选项状态变化
void Kht::onTableSelectToggled(bool checked)
{
    Q_UNUSED(checked);
    selectedTables.clear();
    for(QCheckBox *cb : tableCheckBoxes){
        if(cb->isChecked()){
            selectedTables << cb->property("tableName").toString();
        }
    }
    applySelectedTables();
}

void Kht::selectAllTables()
{
    //仅勾选当前检索字段下可用的表（enabled），变灰的表保持原样
    for(QCheckBox *cb : tableCheckBoxes){
        if(cb->isEnabled()){
            QSignalBlocker blocker(cb);
            cb->setChecked(true);
        }
    }
    selectedTables.clear();
    for(QCheckBox *cb : tableCheckBoxes){
        if(cb->isChecked()){
            selectedTables << cb->property("tableName").toString();
        }
    }
    applySelectedTables();
}

void Kht::selectNoTables()
{
    //仅取消当前检索字段下可用的表（enabled），变灰的表保持原样
    for(QCheckBox *cb : tableCheckBoxes){
        if(cb->isEnabled()){
            QSignalBlocker blocker(cb);
            cb->setChecked(false);
        }
    }
    selectedTables.clear();
    for(QCheckBox *cb : tableCheckBoxes){
        if(cb->isChecked()){
            selectedTables << cb->property("tableName").toString();
        }
    }
    applySelectedTables();
}

void Kht::show_about()
{
    aboutDialog->show();
}

//刷新期刊名称输入自动联想
void Kht::refreshCompleter()
{
    QCompleter *old = ui->lineEdit_journalName->completer();
    if (old) {
        old->deleteLater();
    }
    QCompleter *pCompleter = new QCompleter(sqliteDB->getAllJournalNames(), this);
    pCompleter->setFilterMode(Qt::MatchContains);
    pCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    ui->lineEdit_journalName->setCompleter(pCompleter);
}

//返回当前检索字段名
QString Kht::currentSearchField() const
{
    switch(ui->comboBox_searchField->currentIndex()){
    case 1: return "ISSN";
    case 2: return "EISSN";
    case 3: return QStringLiteral("CN号");
    default: return "Journal";
    }
}

//构建顶部导航栏：扁平样式，左起依次为 浏览/管理/设置/关于
void Kht::buildNavigationBar()
{
    QWidget *navBar = new QWidget(this);
    navBar->setObjectName("navBar");
    navBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    navBar->setStyleSheet(
        "QWidget#navBar { background-color: #f5f6f8; border-bottom: 1px solid #e3e6ea; }"
        "QPushButton { border: none; background: transparent; padding: 8px 16px;"
        "              color: #333333; font-size: 9pt; border-radius: 4px; }"
        "QPushButton:hover { background: #e8ecf1; }"
        "QPushButton:pressed { background: #dde3ea; }"
    );

    QHBoxLayout *nav = new QHBoxLayout(navBar);
    nav->setContentsMargins(8, 0, 8, 0);
    nav->setSpacing(2);

    //数据集范围切换（全部/外文期刊/中文核心期刊），用于过滤下方勾选面板
    scopeCombo = new QComboBox(navBar);
    scopeCombo->addItem(tr("全部数据"));
    scopeCombo->addItem(tr("外文期刊"));
    scopeCombo->addItem(tr("中文核心期刊"));
    scopeCombo->setToolTip(tr("按数据集类别过滤下方数据表勾选面板"));
    nav->addWidget(new QLabel(tr("范围:"), navBar));
    nav->addWidget(scopeCombo);
    nav->addSpacing(12);
    connect(scopeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &Kht::onScopeChanged);

    //导航项（设置/关于紧随其后）
    QPushButton *btnBrowse = new QPushButton(tr("浏览"), navBar);
    QPushButton *btnManage = new QPushButton(tr("管理"), navBar);
    nav->addWidget(btnBrowse);
    nav->addWidget(btnManage);

    //设置菜单（整合原底部4个设置项）
    QToolButton *btnSettings = new QToolButton(navBar);
    btnSettings->setText(tr("设置"));
    btnSettings->setPopupMode(QToolButton::MenuButtonPopup);
    QMenu *settingsMenu = new QMenu(btnSettings);
    QAction *actAutoStart = settingsMenu->addAction(tr("开机自启动到托盘"));
    QAction *actExit2Taskbar = settingsMenu->addAction(tr("关闭到托盘"));
    QAction *actMonitorClipboard = settingsMenu->addAction(tr("监听剪切板"));
    QAction *actAutoActivate = settingsMenu->addAction(tr("自动激活窗口"));
    actAutoStart->setCheckable(true);
    actExit2Taskbar->setCheckable(true);
    actMonitorClipboard->setCheckable(true);
    actAutoActivate->setCheckable(true);
    btnSettings->setMenu(settingsMenu);

    QPushButton *btnAbout = new QPushButton(tr("关于"), navBar);
    nav->addWidget(btnSettings);
    nav->addWidget(btnAbout);

    connect(btnBrowse, &QPushButton::clicked, this, &Kht::browseTables);
    connect(btnManage, &QPushButton::clicked, this, &Kht::manageTables);
    connect(btnAbout, &QPushButton::clicked, this, &Kht::show_about);

    //同步菜单勾选状态与设置项（先设值再连接，避免回环）
    actAutoStart->setChecked(ui->checkBox_autoStart->isChecked());
    actExit2Taskbar->setChecked(ui->checkBox_exit2Taskbar->isChecked());
    actMonitorClipboard->setChecked(ui->checkBox_monitorClipboard->isChecked());
    actAutoActivate->setChecked(ui->checkBox_autoActivateWindow->isChecked());
    connect(actAutoStart, &QAction::toggled, this, [this](bool on){ ui->checkBox_autoStart->setChecked(on); });
    connect(actExit2Taskbar, &QAction::toggled, this, [this](bool on){ ui->checkBox_exit2Taskbar->setChecked(on); });
    connect(actMonitorClipboard, &QAction::toggled, this, [this](bool on){ ui->checkBox_monitorClipboard->setChecked(on); });
    connect(actAutoActivate, &QAction::toggled, this, [this](bool on){ ui->checkBox_autoActivateWindow->setChecked(on); });

    //插入到主布局顶部：导航栏铺满顶栏（顶到窗口最左边和最上边），内容区整体下移一行
    ui->gridLayout->setContentsMargins(0, 0, 0, 0);
    ui->gridLayout->removeItem(ui->gridLayout->itemAtPosition(0, 0));
    ui->gridLayout->addWidget(navBar, 0, 0);
    ui->gridLayout->addLayout(ui->verticalLayout, 1, 0);
    ui->gridLayout->setColumnStretch(0, 1);
    ui->gridLayout->setRowStretch(1, 1);
}

//打开数据表浏览模块
void Kht::browseTables()
{
    TableBrowserDialog *dlg = new TableBrowserDialog(sqliteDB, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

//打开数据表管理模块
void Kht::manageTables()
{
    TableManagerDialog *dlg = new TableManagerDialog(sqliteDB, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &TableManagerDialog::tablesChanged, this, &Kht::reloadTables);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

//数据表增删/排序后刷新主界面
void Kht::reloadTables()
{
    sqliteDB->refreshTableList();
    const QStringList allTables = sqliteDB->getAllTableNames();

    //过滤已删除的表
    QStringList valid;
    for (const QString &t : selectedTables) {
        if (allTables.contains(t))
            valid << t;
    }
    selectedTables = valid;

    //重建主界面勾选面板
    if (tableSelectGroupBox) {
        ui->verticalLayout->removeWidget(tableSelectGroupBox);
        tableSelectGroupBox->deleteLater();
        tableSelectGroupBox = nullptr;
        tableCheckBoxes.clear();
    }
    buildTableSelectPanel();

    //重建数据表选择对话框
    if (selectTableDialog)
        selectTableDialog->deleteLater();
    selectTableDialog = new TableSelectorDialog(allTables, selectedTables, this);

    //应用选中的表并刷新联想
    sqliteDB->selectTableNames(selectedTables);
    updateTableAvailability(currentSearchField());
    refreshCompleter();

    //重新查询或清空
    QString input = ui->lineEdit_journalName->text();
    for (const QString &cue : cueWords)
        input.remove(cue);
    input = input.simplified();
    if (!input.isEmpty() && selectedTables.size() > 0) {
        run(input);
    } else {
        ui->lineEdit_journalName->clear();
        journalInfo.clear();
        updateGUI();
    }
}
