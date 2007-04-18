/*
    Copyright (C) 2006-2007 by Robert Knight <robertknight@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301  USA.
*/

// Qt
#include <QApplication>
#include <QVBoxLayout>

// KDE
#include <KAction>
#include <KActionCollection>
#include <KActionMenu>
#include <KShortcutsDialog>
#include <KLocale>
#include <KMenu>
#include <KMenuBar>
#include <KService>
#include <KToolInvocation>
#include <kstandardaction.h>
#include <KXMLGUIFactory>

// Konsole
#ifndef KONSOLE_PART
    #include "Application.h"
#endif

#include "BookmarkHandler.h"
#include "IncrementalSearchBar.h"
#include "MainWindow.h"
#include "RemoteConnectionDialog.h"
#include "SessionController.h"
#include "SessionList.h"
#include "SessionTypeDialog.h"
#include "ViewManager.h"
#include "ViewSplitter.h"

using namespace Konsole;

MainWindow::MainWindow()
 : KXmlGuiWindow() ,
   _bookmarkHandler(0),
   _pluggedController(0)
{
    // add a small amount of space between the top of the window and the main widget
    // to prevent the menu bar and main widget borders touching (which looks very ugly) in styles
    // where the menu bar has a lower border
    setContentsMargins(0,2,0,0);
   
    // create actions for menus
    setXMLFile("konsole/konsoleui.rc");
    setupActions();

    // create view manager
    // the directory ('konsole') is included in the path here so that the XML
    // file can be found when this code is being used in the Konsole part.
    //setXMLFile("konsole/konsoleui.rc");
    _viewManager = new ViewManager(this,actionCollection());
    connect( _viewManager , SIGNAL(empty()) , this , SLOT(close()) );
    connect( _viewManager , SIGNAL(activeViewChanged(SessionController*)) , this ,
            SLOT(activeViewChanged(SessionController*)) );
    connect( _viewManager , SIGNAL(viewPropertiesChanged(const QList<ViewProperties*>&)) ,
           bookmarkHandler() , SLOT(setViews(const QList<ViewProperties*>&)) ); 

    // create main window widgets
    setupWidgets();

    // create menus
    createGUI();
}

ViewManager* MainWindow::viewManager() const
{
    return _viewManager;
}

void MainWindow::activeViewChanged(SessionController* controller)
{
    if ( _pluggedController == controller )
        return;

    // associated bookmark menu with current session
    bookmarkHandler()->setActiveView(controller);
    disconnect( bookmarkHandler() , SIGNAL(openUrl(const KUrl&)) , 0 , 0 );
    connect( bookmarkHandler() , SIGNAL(openUrl(const KUrl&)) , controller ,
             SLOT(openUrl(const KUrl&)) );

    // set the current session's search bar
    controller->setSearchBar( searchBar() );

    // listen for title changes from the current session
    if ( _pluggedController )
    {
        disconnect( _pluggedController , SIGNAL(titleChanged(ViewProperties*)) 
                     , this , SLOT(activeViewTitleChanged(ViewProperties*)) );
    }

    connect( controller , SIGNAL(titleChanged(ViewProperties*)) , 
            this , SLOT(activeViewTitleChanged(ViewProperties*)) );

    guiFactory()->removeClient(_pluggedController);
    guiFactory()->addClient(controller);

    // update session title to match newly activated session
    activeViewTitleChanged(controller);

    _pluggedController = controller;
}

void MainWindow::activeViewTitleChanged(ViewProperties* properties)
{
    setPlainCaption(properties->title());
}

IncrementalSearchBar* MainWindow::searchBar() const
{
    return _searchBar;
}

void MainWindow::setupActions()
{
    KActionCollection* collection = actionCollection();

    // File Menu
    QAction* newTabAction = collection->addAction("new-tab"); 
    newTabAction->setIcon( KIcon("openterm") );
    newTabAction->setText( i18n("New &Tab") );
    newTabAction->setShortcut( QKeySequence(Qt::CTRL+Qt::SHIFT+Qt::Key_N) );
    connect( newTabAction , SIGNAL(triggered()) , this , SLOT(newTab()) );

    QAction* newWindowAction = collection->addAction("new-window"); 
    newWindowAction->setIcon( KIcon("window-new") );
    newWindowAction->setText( i18n("New &Window") );
    newWindowAction->setShortcut( QKeySequence(Qt::CTRL+Qt::SHIFT+Qt::Key_M) );
    connect( newWindowAction , SIGNAL(triggered()) , this , SLOT(newWindow()) );

    QAction* remoteConnectionAction = collection->addAction("remote-connection");
    remoteConnectionAction->setText( i18n("Remote Connection...") );
    remoteConnectionAction->setIcon( KIcon("network") );
    remoteConnectionAction->setShortcut( QKeySequence(Qt::CTRL+Qt::SHIFT+Qt::Key_R) );
    connect( remoteConnectionAction , SIGNAL(triggered()) , this , SLOT(showRemoteConnectionDialog()) ); 

    QAction* customSessionAction = collection->addAction("custom-session");
    customSessionAction->setText( i18n("Custom Session...") );
    connect( customSessionAction , SIGNAL(triggered()) , this , SLOT(showCustomSessionDialog()) );
   
#ifndef KONSOLE_PART 
    KStandardAction::quit( Application::self() , SLOT(quit()) , collection );
#endif

    // Bookmark Menu
    KActionMenu* bookmarkMenu = new KActionMenu(i18n("&Bookmarks") , collection );
    _bookmarkHandler = new BookmarkHandler( collection , bookmarkMenu->menu() , true , this );
    collection->addAction("bookmark" , bookmarkMenu);

    //TODO - The 'Add Bookmark' menu action currently has a Ctrl+B shortcut by
    // default which cannot be overridden

    // View Menu
    QAction* hideMenuBarAction = collection->addAction("hide-menubar");
    hideMenuBarAction->setText( i18n("Hide MenuBar") );
    connect( hideMenuBarAction , SIGNAL(triggered()) , menuBar() , SLOT(hide()) );

    QAction* mergeAction = collection->addAction("merge-windows");
    mergeAction->setText( i18n("&Merge Windows") );
    connect( mergeAction , SIGNAL(triggered()) , this , SLOT(mergeWindows()) );

    // Settings Menu
    KStandardAction::configureNotifications( 0 , 0 , collection  );
    KStandardAction::keyBindings( this , SLOT(showShortcutsDialog()) , collection  );
}

BookmarkHandler* MainWindow::bookmarkHandler() const
{
    return _bookmarkHandler;
}

void MainWindow::setSessionList(SessionList* list)
{
    sessionListChanged(list->actions());

    connect( list , SIGNAL(sessionSelected(const QString&)) , this , 
            SLOT(sessionSelected(const QString&)) );

    connect( list , SIGNAL(actionsChanged(const QList<QAction*>&)) , this ,
            SLOT(sessionListChanged(const QList<QAction*>&)) );
}

void MainWindow::sessionListChanged(const QList<QAction*>& actions)
{
    unplugActionList("new-session-types");
    plugActionList("new-session-types",actions);
}

void MainWindow::newTab()
{
    emit requestSession(QString(),_viewManager);
}

void MainWindow::newWindow()
{
#ifndef KONSOLE_PART
    Application::self()->newInstance();
#endif
}

void MainWindow::showShortcutsDialog()
{
    KShortcutsDialog::configure( actionCollection() );
}

void MainWindow::sessionSelected(const QString& key)
{
    emit requestSession(key,_viewManager);
}
void MainWindow::showCustomSessionDialog()
{
    SessionTypeDialog dialog(this);
    dialog.exec();
}
void MainWindow::showPreferencesDialog()
{
    Q_ASSERT(0); // not implemented

    // code to invoke the old preferences dialog
    //KToolInvocation::startServiceByDesktopName("konsole",QString());
}

void MainWindow::showRemoteConnectionDialog()
{
    RemoteConnectionDialog dialog(this);
    if ( dialog.exec() == QDialog::Accepted )
        emit requestSession(dialog.sessionKey(),_viewManager);
}

void MainWindow::mergeWindows()
{
    // merges all of the open Konsole windows into this window
    // by merging the view manager associated with the other Konsole windows
    // into this window's view manager

    QListIterator<QWidget*> topLevelIter( QApplication::topLevelWidgets() );

    while (topLevelIter.hasNext())
    {
        MainWindow* window = qobject_cast<MainWindow*>(topLevelIter.next());
        if ( window && window != this )
        {
            _viewManager->merge( window->_viewManager );
            window->deleteLater();
        }
    }
}

void MainWindow::setupWidgets()
{
    QWidget* widget = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout();

    _searchBar = new IncrementalSearchBar( IncrementalSearchBar::AllFeatures , this);
    _searchBar->setVisible(false);

    layout->addWidget( _viewManager->widget() );
    layout->addWidget( _searchBar );
    layout->setMargin(0);
    layout->setSpacing(0);

    widget->setLayout(layout);
    
    setCentralWidget(widget);
}

#include "MainWindow.moc"
