#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QInputDialog>

const u32 com_baud_tab[]=//串口波特率表
{
	1200,2400,4800,9600,19200,28800,38400,57600,115200,
	230400,460800,500000,576000,921600,
};

MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::MainWindow)
{
	ui->setupUi(this);

	foreach (const QSerialPortInfo &info, QSerialPortInfo::availablePorts())
	{
		ui->cb_uart->addItem(info.portName());
	}
	for(auto &it:com_baud_tab)
	{
		ui->cb_baud->addItem(QString().sprintf("%d",it));
	}
	ui->cb_baud->setCurrentIndex(8);

	uart=new QSerialPort();
	QObject::connect(uart, SIGNAL(readyRead()), this, SLOT(slot_uart_rx()));
	QObject::connect(this,SIGNAL(signal_modbus_lostlock(u8*,int)),this,SLOT(slot_modbus_lostlock(u8*,int)),Qt::BlockingQueuedConnection); //传指针了，必须阻塞
	QObject::connect(this,SIGNAL(signal_update_a_reg(u8,u16,u16)),this,SLOT(slot_update_a_reg(u8,u16,u16)));
	QObject::connect(this,SIGNAL(signal_modbus_rxpack(u8*,int)),this,SLOT(slot_modbus_rxpack(u8*,int)));
}

MainWindow::~MainWindow()
{
	delete ui;
}

void MainWindow::ui_initial()
{
	regs_create_UI();
	tasks_create_UI();

	chart0 = new QChart();
	QMargins tmpmarg(5,5,5,5);
	chart0->setMargins(tmpmarg);
	chartView0 = new QChartView(chart0);
//	chart0->createDefaultAxes();
//	chart0->axisX()->setRange(0, 100);
//	chart0->axisY()->setRange(-100, 100);
	chartView0->setRubberBand(QChartView::RectangleRubberBand);
	ui->gridLayout_2->addWidget(chartView0,0,0);

	timerid=startTimer(10); //初始化定时器

	ui->te_comm_log->setReadOnly(true);
	//测试
	u8 tb[8]={1,3,0,8,0,2,0xcc,0xba};
	u8 tb10[]={1,0x10,0,8,0,2,4,0,1,0,2,0xcc,0xba};
	u8 rb03[]={1,3,2,8,0,2,4,0xcc,0xba};
	u8 rberr[]={1,3,2,0xcc,0xba};
	ui->te_comm_log->tx_pack(tb,sizeof(tb));
	ui->te_comm_log->rx_pack(rb03,sizeof(rb03));
	ui->te_comm_log->tx_pack(tb10,sizeof(tb10));
	ui->te_comm_log->rx_lostlock(rb03,sizeof(rb03));
	ui->te_comm_log->rx_pack(rberr,sizeof(rberr));

	sttime=com_time_getms();
}
void MainWindow::slot_modbus_lostlock(u8 *p,int n) //modbus模块失锁
{
	ui->te_comm_log->rx_lostlock(p,n); //加入日志
}
void MainWindow::slot_modbus_rxpack(u8 *p,int n) //modbus模块失锁
{
	ui->te_comm_log->rx_pack(p,n); //加入日志
}
void MainWindow::slot_update_a_reg(u8 addr,u16 reg,u16 d) //更新一个寄存器
{
	//首先查找是哪个寄存器
	int i;
	for(i=0;i<regs_list.size();i++)
	{
		if(regs_list[i].addr==addr && regs_list[i].reg==reg)
		{
			regs_list[i].dbuf=d;
			regs_list[i].need_update_UI=1; //加入显示刷新标志
			//加入曲线
			if(regs_list[i].is_curv) //若要显示曲线
			{
				int s_no=regs_list[i].addr*256+regs_list[i].reg;
				if(curv_map.count(s_no)>0 && curv_map[s_no]) //且曲线列表中有
				{
					u32 tmptime=com_time_getms();
					curv_map[s_no]->append(tmptime-sttime,
							regs_list[i].org_2_val(regs_list[i].dbuf));
				}
			}
		}
	}
}
void MainWindow::regs_update_UI_row(int row) //刷新界面：寄存器
{
	ui->tw_regs->item(row, 0)->setText(regs_list[row].name.c_str());
	ui->tw_regs->item(row, 1)->setText(QString().sprintf("%04X",regs_list[row].dbuf));
	ui->tw_regs->item(row, 2)->setText(QString().sprintf("%f",regs_list[row].org_2_val(regs_list[row].dbuf)));
	ui->tw_regs->item(row, 3)->setText(QString().sprintf("%d",regs_list[row].addr));
	ui->tw_regs->item(row, 4)->setText(QString().sprintf("%d",regs_list[row].reg));
	ui->tw_regs->item(row, 5)->setCheckState(regs_list[row].is_curv?Qt::Checked : Qt::Unchecked);
	ui->tw_regs->item(row, 6)->setText(QString().sprintf("%.2f",regs_list[row].d_k));
	ui->tw_regs->item(row, 7)->setText(QString().sprintf("%.2f",regs_list[row].d_off));
}
void MainWindow::regs_update_UI(void) //刷新界面：寄存器，看标志是否需要刷新
{
	for(int i=0;i<regs_list.size();i++)
	{
		if(regs_list[i].need_update_UI)
		{
			regs_update_UI_row(i);
			regs_list[i].need_update_UI=0;
		}
	}
	//在这里刷新曲线的范围
	if(is_auto_fitscreen)
	{
		auto cs=chart0->series();
		if(cs.size()>0)
		{
			int xmax=10000; //时间长度
			float ymin=-10,ymax=10; //数据长度
			for(int i=0;i<cs.size();i++) //每一条线
			{
				QLineSeries *ps=(QLineSeries *)cs.at(i);
				for(int j=0;j<ps->count();j++)
				{
					QPointF qf=ps->at(j);
					if(xmax<qf.rx()) xmax=qf.rx();
					if(ymin>qf.ry()) ymin=qf.ry();
					if(ymax<qf.ry()) ymax=qf.ry();
				}
			}
			chart0->axisX()->setRange(0, xmax);
			chart0->axisY()->setRange(ymin,ymax);
		}
	}
	if(ui->cb_auto_fitscreen->isChecked()) is_auto_fitscreen=1;
	else is_auto_fitscreen=0;
}
void MainWindow::regs_create_UI(void) //从数据更新界面：寄存器列表
{
	ui->tw_regs->clear();
	ui->tw_regs->setColumnCount(8);
	ui->tw_regs->setRowCount(regs_list.size());
	ui->tw_regs->setHorizontalHeaderLabels(QStringList() <<
		"名称"<<"原始值"<<"值"<<"地址"<<"寄存器"<<"曲线"<<"系数"<<"偏移");
	ui->tw_regs->setColumnWidth(0,60);
	ui->tw_regs->setColumnWidth(1,60);
	ui->tw_regs->setColumnWidth(2,70);
	ui->tw_regs->setColumnWidth(3,40);
	ui->tw_regs->setColumnWidth(4,50);
	ui->tw_regs->setColumnWidth(5,35);
	ui->tw_regs->setColumnWidth(6,50);
	ui->tw_regs->setColumnWidth(7,50);

	for(int i=0;i<regs_list.size();i++) //遍历所有的寄存器
	{
		QTableWidgetItem *item;
		item = new QTableWidgetItem(); ui->tw_regs->setItem(i, 0, item);
		item = new QTableWidgetItem(); ui->tw_regs->setItem(i, 1, item);
		item = new QTableWidgetItem(); ui->tw_regs->setItem(i, 2, item);
		item = new QTableWidgetItem(); ui->tw_regs->setItem(i, 3, item);
		item = new QTableWidgetItem(); ui->tw_regs->setItem(i, 4, item);
		item = new QTableWidgetItem(); ui->tw_regs->setItem(i, 5, item);
		item = new QTableWidgetItem(); ui->tw_regs->setItem(i, 6, item);
		item = new QTableWidgetItem(); ui->tw_regs->setItem(i, 7, item);
		regs_update_UI_row(i);
	}
}
void MainWindow::regs_update_data(void) //从界面更新数据：寄存器列表
{
	int row=ui->tw_regs->rowCount();
	for(int i=0;i<row;i++) //对于每一行
	{
		bool b;
		regs_list[i].name=ui->tw_regs->item(i,0)->text().toStdString();
//1、若修改了系数，需要更新值
		float fd_k=ui->tw_regs->item(i, 6)->text().toFloat();
		float fd_off=ui->tw_regs->item(i, 7)->text().toFloat();
		if(fabs(fd_k-regs_list[i].d_k)>0.00001 ||
			fabs(fd_off-regs_list[i].d_off)>0.00001) //若放大倍数/偏移有变化
		{
			regs_list[i].d_k=fd_k;
			regs_list[i].d_off=fd_off;
			ui->tw_regs->item(i, 2)->setText(QString().sprintf("%f",regs_list[i].org_2_val(regs_list[i].dbuf)));
			return ; //否则会干扰后边
		}
		regs_list[i].d_k=fd_k;
		regs_list[i].d_off=fd_off;
//2、若修改值，需要更新值
		u16 td_org=ui->tw_regs->item(i, 1)->text().toInt(&b,16); //原始数值
		u16 td_val=regs_list[i].val_2_org(ui->tw_regs->item(i, 2)->text().toFloat()); //值换算为原始数值
		if(regs_list[i].dbuf != td_org) //原始数值的显示发生了变化
		{
			regs_list[i].dbuf=td_org;
			ui->tw_regs->item(i, 2)->setText(QString().sprintf("%f",regs_list[i].org_2_val(regs_list[i].dbuf)));
		}
		else if(regs_list[i].dbuf != td_val) //若显示值被修改
		{
			regs_list[i].dbuf=td_val;
			ui->tw_regs->item(i, 1)->setText(QString().sprintf("%04X",regs_list[i].dbuf));
		}
//3、若修改了寄存器
		regs_list[i].addr=ui->tw_regs->item(i,3)->text().toInt();
		regs_list[i].reg=ui->tw_regs->item(i, 4)->text().toInt();
//4、曲线
		regs_list[i].is_curv=ui->tw_regs->item(i, 5)->checkState()?1:0;
		int s_no=regs_list[i].addr*256+regs_list[i].reg;
		if(regs_list[i].is_curv) //若要显示曲线
		{
			if(curv_map.count(s_no)<=0) //且曲线列表中没有
			{
				QLineSeries *tmpseries=new QLineSeries(chart0);
				tmpseries->setName(regs_list[i].name.c_str());
				tmpseries->setUseOpenGL(true); //使用OpenGL加速显示
				chart0->addSeries(tmpseries);
				curv_map[s_no]=tmpseries;
				chart0->createDefaultAxes();
			}
		}
		else if(curv_map.count(s_no)>0) //若不显示曲线,且曲线列表中有
		{
			if(curv_map[s_no])
			{
				chart0->removeSeries(curv_map[s_no]);
				delete curv_map[s_no];
			}
			curv_map.erase(s_no);
		}
	}
	//反向查找曲线
	for(auto &it:curv_map)
	{
		int i;
		for(i=0;i<regs_list.size();i++)
		{
			int s_no=regs_list[i].addr*256+regs_list[i].reg;
			if(s_no==it.first) break;//若找到了
		}
		if(i==regs_list.size()) //若没找到
		{ //删除此曲线
			if(it.second)
			{
				chart0->removeSeries(it.second);
				delete it.second;
			}
			curv_map.erase(it.first);
			break; //一次只删一个
		}
	}
}
/////////////////////////////////////////////////////////////////////////
void MainWindow::tasks_update_UI_row(int row) //刷新界面：任务
{
	ui->tw_tasks->item(row, 0)->setCheckState(task_list[row].mdbs_buf.enable?Qt::Checked : Qt::Unchecked);
	ui->tw_tasks->item(row, 1)->setText(task_list[row].name.c_str());
	ui->tw_tasks->item(row, 2)->setText(QString().sprintf("%d",task_list[row].mdbs_buf.addr));
	ui->tw_tasks->item(row, 3)->setText(QString().sprintf("%d",task_list[row].mdbs_buf.st));
	((QComboBox *)(ui->tw_tasks->cellWidget(row, 4)))->setCurrentText(QString().sprintf("%02X",task_list[row].mdbs_buf.type));
	ui->tw_tasks->item(row, 5)->setText(QString().sprintf("%d",task_list[row].mdbs_buf.num));
	ui->tw_tasks->item(row, 6)->setText(
			sFormat("%.1f",tick_2_freq(task_list[row].mdbs_buf.freq)).c_str());
	const char *ttab[]={"正常","错误","无应答"};
	ui->tw_tasks->item(row, 7)->setText(ttab[(int)((task_list[row].mdbs_buf.err+254)/254.1)]);
}
void MainWindow::tasks_update_UI(void) //刷新界面：任务
{
//	for(int i=0;i<task_list.size();i++)
//	{
//		if(task_list[i].need_update_UI) tasks_update_UI_row(i);
//	}
	//任务界面基本只接收指令，数据更新只有状态
	for(int i=0;i<task_list.size();i++) //遍历所有任务
	{
		const char *ttab[]={"正常","错误","无应答"};
		ui->tw_tasks->item(i, 7)->setText(ttab[(int)((task_list[i].mdbs_buf.err+254)/254.1)]);
	}
}
void MainWindow::tasks_create_UI(void) //从数据更新界面：任务列表
{
	ui->tw_tasks->clear();
	ui->tw_tasks->setColumnCount(8);
	ui->tw_tasks->setRowCount(task_list.size());
	ui->tw_tasks->setHorizontalHeaderLabels(QStringList() <<
		"使能"<<"名称"<<"地址"<<"寄存器"<<"类型"<<"数量"<<"频率"<<"状态");
	ui->tw_tasks->setColumnWidth(0,34);
	ui->tw_tasks->setColumnWidth(1,60);
	ui->tw_tasks->setColumnWidth(2,36);
	ui->tw_tasks->setColumnWidth(3,50);
	ui->tw_tasks->setColumnWidth(4,40);
	ui->tw_tasks->setColumnWidth(5,36);
	ui->tw_tasks->setColumnWidth(6,36);
	ui->tw_tasks->setColumnWidth(7,50);
	QStringList tasktype_list; //寄存器类型列表
	tasktype_list.append("06");
	tasktype_list.append("10");
	tasktype_list.append("03");
	tasktype_list.append("04");
	for(int i=0;i<task_list.size();i++) //遍历所有任务
	{
		QTableWidgetItem *item;
		item = new QTableWidgetItem(); ui->tw_tasks->setItem(i, 0, item);
		item = new QTableWidgetItem(); ui->tw_tasks->setItem(i, 1, item);
		item = new QTableWidgetItem(); ui->tw_tasks->setItem(i, 2, item);
		item = new QTableWidgetItem(); ui->tw_tasks->setItem(i, 3, item);
		//item = new QTableWidgetItem(); ui->tw_tasks->setItem(i, 4, item);
		QComboBox *tmpcombo=new QComboBox(); tmpcombo->addItems(tasktype_list);
		ui->tw_tasks->setCellWidget(i,4,tmpcombo);
		item = new QTableWidgetItem(); ui->tw_tasks->setItem(i, 5, item);
		item = new QTableWidgetItem(); ui->tw_tasks->setItem(i, 6, item);
		item = new QTableWidgetItem(); ui->tw_tasks->setItem(i, 7, item);
		item->setFlags(item->flags() & (~(1<<1))); //最后一项不可编辑

		tasks_update_UI_row(i);
	}
}
void MainWindow::tasks_update_data(void) //从界面更新数据：任务列表
{
	int row=ui->tw_tasks->rowCount();
	for(int i=0;i<row;i++) //对于每一行
	{
		bool b;
		task_list[i].mdbs_buf.enable=ui->tw_tasks->item(i, 0)->checkState()==Qt::Checked?1:0;
		task_list[i].name=ui->tw_tasks->item(i, 1)->text().toStdString();
		task_list[i].mdbs_buf.addr=ui->tw_tasks->item(i, 2)->text().toInt();
		task_list[i].mdbs_buf.st=ui->tw_tasks->item(i, 3)->text().toInt();
		task_list[i].mdbs_buf.type=((QComboBox *)(ui->tw_tasks->cellWidget(i, 4)))->currentText().toInt(&b,16);
		task_list[i].mdbs_buf.num=ui->tw_tasks->item(i, 5)->text().toInt();
		float t=ui->tw_tasks->item(i, 6)->text().toFloat();
		task_list[i].mdbs_buf.freq=freq_2_tick(t);
	}
}
void MainWindow::timerEvent(QTimerEvent *event) //100Hz
{
	if(event->timerId() == timerid) //定时器调用，或触发
	{
		task_poll();
		static u32 tick=0;
		if(tick++%30==1)
		{
			regs_update_UI(); //首先看看有没有要更新UI的，然后看是否有UI指令
			regs_update_data(); //将UI数据更新到数据
			tasks_update_UI();
			tasks_update_data(); //将UI数据更新到数据
		}
		if(tick%10==2) //10Hz
		{
		}
	}
}

void MainWindow::closeEvent(QCloseEvent *event)
{
}

void MainWindow::slot_uart_rx() //串口接收
{
	char buf[128];
	int n=1;
	while(n)
	{
		n=uart->read(buf,sizeof(buf));
		main_md.pack((u8*)buf,n);
	}
}

/////////////////////////////////////////////////////////////////////////
//界面响应
void MainWindow::on_bt_open_uart_clicked()
{
	if(ui->bt_open_uart->text()=="打开串口")
	{
		uart->setPortName(ui->cb_uart->currentText());
		uart->setBaudRate(ui->cb_baud->currentText().toInt());
		if(uart->open(QIODevice::ReadWrite))
		{
			ui->bt_open_uart->setText("关闭串口");
		}
	}
	else
	{
		uart->close();
		ui->bt_open_uart->setText("打开串口");
	}
}
void MainWindow::on_bt_clear_data_clicked() //清除数据
{
	ui->te_comm_log->clear();
	curv_map.clear();
	chart0->removeAllSeries(); //去掉所有曲线
	sttime=com_time_getms();
}
void MainWindow::on_bt_fitscreen_clicked() //适应屏幕
{
	is_auto_fitscreen=2;
}
////////////////////////////////////////////////////////////////////////////
//					任务部分
////////////////////////////////////////////////////////////////////////////
void MainWindow::on_bt_start_task_clicked() //开始周期任务
{
	if(ui->bt_start_task->text()=="开始周期任务")
	{
		task_start();
		if(is_running==1)
		{
			ui->bt_start_task->setText("结束周期任务");
			ui->bt_send->setEnabled(false);
		}
	}
	else
	{
		task_stop();
		ui->bt_start_task->setText("开始周期任务");
		ui->bt_send->setEnabled(true);
	}
}
void MainWindow::on_bt_add_task_clicked() //添加任务
{
	CMTask tt;
	tt.mdbs_buf.enable=0;
	task_list.push_back(tt);
	tasks_create_UI();
}

void MainWindow::on_bt_del_task_clicked() //删除任务
{
	//删除当前选中的任务
	int cr=ui->tw_tasks->currentRow();
	if(cr>=0)
	{
		task_list.erase(task_list.begin()+cr);
		tasks_create_UI();
	}
}
////////////////////////////////////////////////////////////////////////////
//					寄存器部分
////////////////////////////////////////////////////////////////////////////
void MainWindow::on_bt_add_reg_clicked() //添加寄存器
{
	CMReg tt;
	regs_list.push_back(tt);
	regs_create_UI();
}

void MainWindow::on_bt_del_reg_clicked() //删除寄存器
{
	//删除当前选中的任务
	int cr=ui->tw_regs->currentRow();
	if(cr>=0)
	{
		regs_list.erase(regs_list.begin()+cr);
		regs_create_UI();
	}
}
////////////////////////////////////////////////////////////////////////////
//					帮助
////////////////////////////////////////////////////////////////////////////
void MainWindow::on_bt_help_clicked() //帮助
{
	QFile nFile(":/readme.md");
	if(!nFile.open(QFile::ReadOnly))
	{
		qDebug() << "could not open file for reading";
		return;
	}
	string nText =nFile.readAll().data();
	QMessageBox::about(this,"关于软件",nText.c_str());
	//QMessageBox::about(this,"关于软件","<b>asdf</b>qwer,1234<br/><span style=\"color:red\">poiuj</span>");
}
