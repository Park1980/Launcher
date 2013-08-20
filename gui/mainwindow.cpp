/* Copyright 2013 MultiMC Contributors
 *
 * Authors: Andrew Okin
 *          Peterix
 *          Orochimarufan <orochimarufan.x3@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QMenu>
#include <QMessageBox>
#include <QInputDialog>
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFileInfo>

#include "osutils.h"
#include "userutils.h"
#include "pathutils.h"

#include "categorizedview.h"
#include "categorydrawer.h"

#include "gui/settingsdialog.h"
#include "gui/newinstancedialog.h"
#include "gui/logindialog.h"
#include "gui/taskdialog.h"
#include "gui/aboutdialog.h"
#include "gui/versionselectdialog.h"
#include "gui/lwjglselectdialog.h"
#include "gui/consolewindow.h"
#include "gui/instancesettings.h"

#include "AppSettings.h"
#include "AppVersion.h"

#include "logic/lists/InstanceList.h"
#include "logic/tasks/LoginTask.h"
#include "logic/BaseInstance.h"
#include "logic/InstanceFactory.h"
#include "logic/MinecraftProcess.h"
#include "logic/OneSixAssets.h"
#include "logic/OneSixUpdate.h"
#include "logic/lists/MinecraftVersionList.h"
#include "logic/lists/LwjglVersionList.h"

#include "instancemodel.h"
#include "instancedelegate.h"
#include "IconPickerDialog.h"

MainWindow::MainWindow ( QWidget *parent ) :
	QMainWindow ( parent ),
	ui ( new Ui::MainWindow ),
	instList ( globalSettings->get ( "InstanceDir" ).toString() )
{
	ui->setupUi ( this );
	
	// Set active instance to null.
	m_activeInst = NULL;
	
	// Create the widget
	view = new KCategorizedView ( ui->centralWidget );
	drawer = new KCategoryDrawer ( view );
	view->setStyleSheet(
		"QListView\
		{\
			background-image: url(:/backgrounds/kitteh);\
			background-attachment: fixed;\
			background-clip: padding;\
			background-position: top right;\
			background-repeat: none;\
			background-color:palette(base);\
		}");
	
	view->setSelectionMode ( QAbstractItemView::SingleSelection );
	//view->setSpacing( KDialog::spacingHint() );
	view->setCategoryDrawer ( drawer );
	view->setCollapsibleBlocks ( true );
	view->setViewMode ( QListView::IconMode );
	view->setFlow ( QListView::LeftToRight );
	view->setWordWrap(true);
	view->setMouseTracking ( true );
	view->viewport()->setAttribute ( Qt::WA_Hover );
	auto delegate = new ListViewDelegate();
	view->setItemDelegate(delegate);
	view->setSpacing(10);
	view->setUniformItemWidths(true);
	view->installEventFilter(this);

	model = new InstanceModel ( instList,this );
	proxymodel = new InstanceProxyModel ( this );
	proxymodel->setSortRole ( KCategorizedSortFilterProxyModel::CategorySortRole );
	proxymodel->setFilterRole ( KCategorizedSortFilterProxyModel::CategorySortRole );
	//proxymodel->setDynamicSortFilter ( true );
	proxymodel->setSourceModel ( model );
	proxymodel->sort ( 0 );

	view->setFrameShape ( QFrame::NoFrame );

	ui->horizontalLayout->addWidget ( view );
	setWindowTitle ( QString ( "MultiMC %1" ).arg ( AppVersion::current.toString() ) );
	// TODO: Make this work with the new settings system.
//	restoreGeometry(settings->getConfig().value("MainWindowGeometry", saveGeometry()).toByteArray());
//	restoreState(settings->getConfig().value("MainWindowState", saveState()).toByteArray());
	view->setModel ( proxymodel );
	connect(view, SIGNAL(doubleClicked(const QModelIndex &)),
        this, SLOT(instanceActivated(const QModelIndex &)));

    connect(view, SIGNAL(clicked(const QModelIndex &)),
        this, SLOT(instanceChanged(const QModelIndex &)));
	
	// Load the instances. FIXME: this is not the place I'd say.
	instList.loadList();
	
	//FIXME: WTF
	if (!MinecraftVersionList::getMainList().isLoaded())
	{
		m_versionLoadTask = MinecraftVersionList::getMainList().getLoadTask();
		startTask(m_versionLoadTask);
	}
	//FIXME: WTF X 2
	if (!LWJGLVersionList::get().isLoaded())
	{
		LWJGLVersionList::get().loadList();
	}
	//FIXME: I guess you get the idea. This is a quick hack.
	assets_downloader = new OneSixAssets();
	assets_downloader->start();
}

MainWindow::~MainWindow()
{
	delete ui;
	delete proxymodel;
	delete model;
	delete drawer;
	delete assets_downloader;
}

bool MainWindow::eventFilter ( QObject* obj, QEvent* ev )
{
	if(obj == view)
	{
		if (ev->type() == QEvent::KeyPress)
		{
			QKeyEvent *keyEvent = static_cast<QKeyEvent*>(ev);
			switch(keyEvent->key())
			{
				case Qt::Key_Enter:
				case Qt::Key_Return:
					on_actionLaunchInstance_triggered();
					return true;
				case Qt::Key_Delete:
					on_actionDeleteInstance_triggered();
					return true;
				case Qt::Key_F5:
					on_actionRefresh_triggered();
					return true;
				case Qt::Key_F2:
					on_actionRenameInstance_triggered();
					return true;
				default:
					break;
			}
		}
	}
	return QMainWindow::eventFilter ( obj, ev );
}


void MainWindow::instanceActivated ( QModelIndex index )
{
	if(!index.isValid())
		return;
	BaseInstance * inst = (BaseInstance *) index.data(InstanceModel::InstancePointerRole).value<void *>();
	doLogin();
}

void MainWindow::on_actionAddInstance_triggered()
{
	if (!MinecraftVersionList::getMainList().isLoaded() &&
		m_versionLoadTask && m_versionLoadTask->isRunning())
	{
		QEventLoop waitLoop;
		waitLoop.connect(m_versionLoadTask, SIGNAL(failed(QString)), SLOT(quit()));
		waitLoop.connect(m_versionLoadTask, SIGNAL(succeeded()), SLOT(quit()));
		waitLoop.exec();
	}
	
	NewInstanceDialog newInstDlg( this );
	if (!newInstDlg.exec())
		return;
	
	BaseInstance *newInstance = NULL;
	
	QString instDirName = DirNameFromString(newInstDlg.instName());
	QString instDir = PathCombine(globalSettings->get("InstanceDir").toString(), instDirName);
	
	auto &loader = InstanceFactory::get();
	
	auto error = loader.createInstance(newInstance, newInstDlg.selectedVersion(), instDir);
	QString errorMsg = QString("Failed to create instance %1: ").arg(instDirName);
	switch (error)
	{
	case InstanceFactory::NoCreateError:
		newInstance->setName(newInstDlg.instName());
		newInstance->setIconKey(newInstDlg.iconKey());
		instList.add(InstancePtr(newInstance));
		return;
	
	case InstanceFactory::InstExists:
		errorMsg += "An instance with the given directory name already exists.";
		QMessageBox::warning(this, "Error", errorMsg);
		break;
		
	case InstanceFactory::CantCreateDir:
		errorMsg += "Failed to create the instance directory.";
		QMessageBox::warning(this, "Error", errorMsg);
		break;
		
	default:
		errorMsg += QString("Unknown instance loader error %1").arg(error);
		QMessageBox::warning(this, "Error", errorMsg);
		break;
	}
}

void MainWindow::on_actionChangeInstIcon_triggered()
{
	BaseInstance* inst = selectedInstance();
	if(!inst)
		return;
	
	IconPickerDialog dlg(this);
	dlg.exec(selectedInstance()->iconKey());
	if(dlg.result() == QDialog::Accepted)
	{
		selectedInstance()->setIconKey(dlg.selectedIconKey);
	}
}


void MainWindow::on_actionChangeInstGroup_triggered()
{
	BaseInstance* inst = selectedInstance();
	if(!inst)
		return;
	
	bool ok = false;
	QString name ( inst->group() );
	name = QInputDialog::getText ( this, tr ( "Group name" ), tr ( "Enter a new group name." ),
									QLineEdit::Normal, name, &ok );
	if(ok)
		inst->setGroup(name);
}


void MainWindow::on_actionViewInstanceFolder_triggered()
{
	QString str = globalSettings->get ( "InstanceDir" ).toString();
	openDirInDefaultProgram ( str );
}

void MainWindow::on_actionRefresh_triggered()
{
	instList.loadList();
}

void MainWindow::on_actionViewCentralModsFolder_triggered()
{
	openDirInDefaultProgram ( globalSettings->get ( "CentralModsDir" ).toString() , true);
}

void MainWindow::on_actionCheckUpdate_triggered()
{

}

void MainWindow::on_actionSettings_triggered()
{
	SettingsDialog dialog ( this );
	dialog.exec();
}

void MainWindow::on_actionReportBug_triggered()
{
	openWebPage ( QUrl ( "http://jira.forkk.net/browse/MMC" ) );
}

void MainWindow::on_actionNews_triggered()
{
	openWebPage ( QUrl ( "http://news.forkk.net/" ) );
}

void MainWindow::on_actionAbout_triggered()
{
	AboutDialog dialog ( this );
	dialog.exec();
}

void MainWindow::on_mainToolBar_visibilityChanged ( bool )
{
	// Don't allow hiding the main toolbar.
	// This is the only way I could find to prevent it... :/
	ui->mainToolBar->setVisible ( true );
}

void MainWindow::on_actionDeleteInstance_triggered()
{
	BaseInstance* inst = selectedInstance();
	if (inst)
	{
		int response = QMessageBox::question(this, "CAREFUL", 
						     QString("This is permanent! Are you sure?\nAbout to delete: ") + inst->name());
		if (response == QMessageBox::Yes)
		{
			QDir(inst->instanceRoot()).removeRecursively();
			instList.loadList();
		}
	}
}

void MainWindow::on_actionRenameInstance_triggered()
{
	BaseInstance* inst = selectedInstance();
	if(inst)
	{
		bool ok = false;
		QString name ( inst->name() );
		name = QInputDialog::getText ( this, tr ( "Instance name" ), tr ( "Enter a new instance name." ),
									   QLineEdit::Normal, name, &ok );
		
		if (name.length() > 0)
		{
			if(ok && name.length() && name.length() <= 25)
				inst->setName(name);
		}
	}
}

void MainWindow::on_actionViewSelectedInstFolder_triggered()
{
	BaseInstance* inst = selectedInstance();
	if(inst)
	{
		QString str = inst->instanceRoot();
		openDirInDefaultProgram ( QDir(str).absolutePath() );
	}
}

void MainWindow::on_actionEditInstMods_triggered()
{
	BaseInstance* inst = selectedInstance();
	if (inst)
	{
		auto dialog = inst->createModEditDialog(this);
		if(dialog)
			dialog->exec();
		dialog->deleteLater();
	}
}

void MainWindow::closeEvent ( QCloseEvent *event )
{
	// Save the window state and geometry.
	// TODO: Make this work with the new settings system.
//	settings->getConfig().setValue("MainWindowGeometry", saveGeometry());
//	settings->getConfig().setValue("MainWindowState", saveState());
	QMainWindow::closeEvent ( event );
}

void MainWindow::on_instanceView_customContextMenuRequested ( const QPoint &pos )
{
	QMenu *instContextMenu = new QMenu ( "Instance", this );

	// Add the actions from the toolbar to the context menu.
	instContextMenu->addActions ( ui->instanceToolBar->actions() );

	instContextMenu->exec ( view->mapToGlobal ( pos ) );
}

BaseInstance* MainWindow::selectedInstance()
{
	QAbstractItemView * iv = view;
	auto smodel = iv->selectionModel();
	QModelIndex mindex;
	if(smodel->hasSelection())
	{
		auto rows = smodel->selectedRows();
		mindex = rows.at(0);
	}
	
	if(mindex.isValid())
	{
		return (BaseInstance *) mindex.data(InstanceModel::InstancePointerRole).value<void *>();
	}
	else
		return nullptr;
}


void MainWindow::on_actionLaunchInstance_triggered()
{
	BaseInstance* inst = selectedInstance();
	if(inst)
	{
		doLogin();
	}
}

void MainWindow::doLogin(const QString& errorMsg)
{
	if (!selectedInstance())
		return;
	
	LoginDialog* loginDlg = new LoginDialog(this, errorMsg);
	loginDlg->exec();
	if(loginDlg->result() == QDialog::Accepted)
	{
		UserInfo uInfo{loginDlg->getUsername(), loginDlg->getPassword()};

		TaskDialog* tDialog = new TaskDialog(this);
		LoginTask* loginTask = new LoginTask(uInfo, tDialog);
		connect(loginTask, SIGNAL(succeeded()),SLOT(onLoginComplete()), Qt::QueuedConnection);
		connect(loginTask, SIGNAL(failed(QString)), SLOT(doLogin(QString)), Qt::QueuedConnection);
		m_activeInst = selectedInstance();
		tDialog->exec(loginTask);
	}
}

void MainWindow::onLoginComplete()
{
	if(!m_activeInst)
		return;
	LoginTask * task = (LoginTask *) QObject::sender();
	m_activeLogin = task->getResult();
	
	BaseUpdate *updateTask = m_activeInst->doUpdate();
	if(!updateTask)
	{
		launchInstance(m_activeInst, m_activeLogin);
	}
	else
	{
		TaskDialog *tDialog = new TaskDialog(this);
		connect(updateTask, SIGNAL(succeeded()),SLOT(onGameUpdateComplete()));
		connect(updateTask, SIGNAL(failed(QString)), SLOT(onGameUpdateError(QString)));
		tDialog->exec(updateTask);
	}
}

void MainWindow::onGameUpdateComplete()
{
	launchInstance(m_activeInst, m_activeLogin);
}

void MainWindow::onGameUpdateError(QString error)
{
	QMessageBox::warning(this, "Error updating instance", error);
}

void MainWindow::launchInstance(BaseInstance *instance, LoginResponse response)
{
	Q_ASSERT_X(instance != NULL, "launchInstance", "instance is NULL");
	
	proc = instance->prepareForLaunch(response.username, response.sessionID);
	if(!proc)
		return;
	
	console = new ConsoleWindow();
	console->show();
	connect(proc, SIGNAL(log(QString, MessageLevel::Enum)), 
			console, SLOT(write(QString, MessageLevel::Enum)));
	proc->launch();
}

void MainWindow::taskStart()
{
	// Nothing to do here yet.
}

void MainWindow::taskEnd()
{
	QObject *sender = QObject::sender();
	if (sender == m_versionLoadTask)
		m_versionLoadTask = NULL;
	
	sender->deleteLater();
}

void MainWindow::startTask(Task *task)
{
	connect(task, SIGNAL(started()), SLOT(taskStart()));
	connect(task, SIGNAL(succeeded()), SLOT(taskEnd()));
	connect(task, SIGNAL(failed(QString)), SLOT(taskEnd()));
	task->startTask();
}


// Create A Desktop Shortcut
void MainWindow::on_actionMakeDesktopShortcut_triggered()
{
	QString name ( "Test" );
	name = QInputDialog::getText ( this, tr ( "MultiMC Shortcut" ), tr ( "Enter a Shortcut Name." ), QLineEdit::Normal, name );

	Util::createShortCut ( Util::getDesktopDir(), QApplication::instance()->applicationFilePath(), QStringList() << "-dl" << QDir::currentPath() << "test", name, "application-x-octet-stream" );

	QMessageBox::warning ( this, "Not useful", "A Dummy Shortcut was created. it will not do anything productive" );
}

// BrowserDialog
void MainWindow::openWebPage ( QUrl url )
{
	QDesktopServices::openUrl(url);
}

void MainWindow::on_actionChangeInstMCVersion_triggered()
{
	if (view->selectionModel()->selectedIndexes().count() < 1)
		return;
	
	BaseInstance *inst = selectedInstance();
	
	VersionSelectDialog vselect(inst->versionList(), this);
	if (vselect.exec() && vselect.selectedVersion())
	{
		inst->setIntendedVersionId(vselect.selectedVersion()->descriptor);
	}
}

void MainWindow::on_actionChangeInstLWJGLVersion_triggered()
{
	BaseInstance *inst = selectedInstance();
	
	if (!inst)
		return;
	
	LWJGLSelectDialog lselect(this);
	lselect.exec();
	if (lselect.result() == QDialog::Accepted)
	{
		
	}
}

void MainWindow::on_actionInstanceSettings_triggered()
{
	if (view->selectionModel()->selectedIndexes().count() < 1)
		return;

	BaseInstance *inst = selectedInstance();
	SettingsObject *s;
	s = &inst->settings();
	InstanceSettings settings(s, this);
	settings.setWindowTitle(QString("Instance settings"));
	settings.exec();
}

void MainWindow::instanceChanged(QModelIndex idx) {
    ui->instanceToolBar->setEnabled(idx.isValid());
}
