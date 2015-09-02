/*
    Copyright 2006-2008 by Robert Knight <robertknight@gmail.com>

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

// Own
#include "ViewManager.h"

#include <config-konsole.h>

// Qt
#include <QtCore/QSignalMapper>
#include <QtCore/QStringList>
#include <QMenu>
#include <QAction>
#include <QtDBus/QtDBus>

// KDE
#include <KAcceleratorManager>
#include <KLocalizedString>
#include <KActionCollection>
#include <KConfigGroup>
#include <QLoggingCategory>

// Konsole
#include <windowadaptor.h>

#include "ColorScheme.h"
#include "ColorSchemeManager.h"
#include "Session.h"
#include "TerminalDisplay.h"
#include "SessionController.h"
#include "SessionManager.h"
#include "ProfileManager.h"
#include "ViewContainer.h"
#include "ViewSplitter.h"
#include "Profile.h"
#include "Enumeration.h"
#include "MultiTerminalDisplayManager.h"

using namespace Konsole;

int ViewManager::lastManagerId = 0;

ViewManager::ViewManager(QObject* parent , KActionCollection* collection)
    : QObject(parent)
    , _viewSplitter(0)
    , _actionCollection(collection)
    , _containerSignalMapper(new QSignalMapper(this))
    , _navigationMethod(TabbedNavigation)
    , _navigationVisibility(ViewContainer::AlwaysShowNavigation)
    , _navigationPosition(ViewContainer::NavigationPositionTop)
    , _showQuickButtons(false)
    , _newTabBehavior(PutNewTabAtTheEnd)
    , _navigationStyleSheet(QString())
    , _managerId(0)
{
    // create main view area
    _viewSplitter = new ViewSplitter(0);
    KAcceleratorManager::setNoAccel(_viewSplitter);

    _mtdManager = new MultiTerminalDisplayManager(this);

    // the ViewSplitter class supports both recursive and non-recursive splitting,
    // in non-recursive mode, all containers are inserted into the same top-level splitter
    // widget, and all the divider lines between the containers have the same orientation
    //
    // the ViewManager class is not currently able to handle a ViewSplitter in recursive-splitting
    // mode
    _viewSplitter->setRecursiveSplitting(false);
    _viewSplitter->setFocusPolicy(Qt::NoFocus);

    // setup actions which are related to the views
    setupActions();

    // emit a signal when all of the views held by this view manager are destroyed
    connect(_viewSplitter.data() , &Konsole::ViewSplitter::allContainersEmpty , this , &Konsole::ViewManager::empty);
    connect(_viewSplitter.data() , &Konsole::ViewSplitter::empty , this , &Konsole::ViewManager::empty);

    // listen for addition or removal of views from associated containers
    connect(_containerSignalMapper , static_cast<void(QSignalMapper::*)(QObject*)>(&QSignalMapper::mapped) , this , &Konsole::ViewManager::containerViewsChanged);

    // listen for profile changes
    connect(ProfileManager::instance() , &Konsole::ProfileManager::profileChanged , this, &Konsole::ViewManager::profileChanged);
    connect(SessionManager::instance() , &Konsole::SessionManager::sessionUpdated , this, &Konsole::ViewManager::updateViewsForSession);

    //prepare DBus communication
    new WindowAdaptor(this);
    // TODO: remove this obsolete and bad name
    QDBusConnection::sessionBus().registerObject(QLatin1String("/Konsole"), this);

    _managerId = ++lastManagerId;
    QDBusConnection::sessionBus().registerObject(QLatin1String("/Windows/") + QString::number(_managerId), this);
}

ViewManager::~ViewManager()
{
    // TODO: put this somewhere else?
    delete _mtdManager;
}

int ViewManager::managerId() const
{
    return _managerId;
}

QWidget* ViewManager::activeView() const
{
    ViewContainer* container = _viewSplitter->activeContainer();
    if (container) {
        return container->activeView();
    } else {
        return 0;
    }
}

QWidget* ViewManager::widget() const
{
    return _viewSplitter;
}

void ViewManager::setupActions()
{
    KActionCollection* collection = _actionCollection;

    QAction* nextViewAction = new QAction(i18nc("@action Shortcut entry", "Next Tab") , this);
    QAction* previousViewAction = new QAction(i18nc("@action Shortcut entry", "Previous Tab") , this);
    QAction* lastViewAction = new QAction(i18nc("@action Shortcut entry", "Switch to Last Tab") , this);
    QAction* nextContainerAction = new QAction(i18nc("@action Shortcut entry", "Next View Container") , this);

    QAction* moveViewLeftAction = new QAction(i18nc("@action Shortcut entry", "Move Tab Left") , this);
    QAction* moveViewRightAction = new QAction(i18nc("@action Shortcut entry", "Move Tab Right") , this);

    // list of actions that should only be enabled when there are multiple view
    // containers open
    QList<QAction*> multiViewOnlyActions;
    multiViewOnlyActions << nextContainerAction;

    if (collection) {
        QAction* splitLeftRightAction = new QAction(QIcon::fromTheme(QStringLiteral("view-split-left-right")),
                i18nc("@action:inmenu", "Split View Left/Right"),
                this);
        collection->setDefaultShortcut(splitLeftRightAction, Qt::CTRL + Qt::Key_ParenLeft);
        collection->addAction("split-view-left-right", splitLeftRightAction);
        connect(splitLeftRightAction , &QAction::triggered , this , &Konsole::ViewManager::splitLeftRight);

        QAction* splitTopBottomAction = new QAction(QIcon::fromTheme(QStringLiteral("view-split-top-bottom")) ,
                i18nc("@action:inmenu", "Split View Top/Bottom"), this);
        collection->setDefaultShortcut(splitTopBottomAction, Qt::CTRL + Qt::Key_ParenRight);
        collection->addAction("split-view-top-bottom", splitTopBottomAction);
        connect(splitTopBottomAction , &QAction::triggered , this , &Konsole::ViewManager::splitTopBottom);

        QAction* closeActiveAction = new QAction(i18nc("@action:inmenu Close Active View", "Close Active") , this);
        closeActiveAction->setIcon(QIcon::fromTheme(QStringLiteral("view-close")));
        collection->setDefaultShortcut(closeActiveAction, Qt::CTRL + Qt::SHIFT + Qt::Key_S);
        closeActiveAction->setEnabled(false);
        collection->addAction("close-active-view", closeActiveAction);
        connect(closeActiveAction , &QAction::triggered , this , &Konsole::ViewManager::closeActiveContainer);

        multiViewOnlyActions << closeActiveAction;

        QAction* closeOtherAction = new QAction(i18nc("@action:inmenu Close Other Views", "Close Others") , this);
        collection->setDefaultShortcut(closeOtherAction, Qt::CTRL + Qt::SHIFT + Qt::Key_O);
        closeOtherAction->setEnabled(false);
        collection->addAction("close-other-views", closeOtherAction);
        connect(closeOtherAction , &QAction::triggered , this , &Konsole::ViewManager::closeOtherContainers);

        multiViewOnlyActions << closeOtherAction;

        // Expand & Shrink Active View
        QAction* expandActiveAction = new QAction(i18nc("@action:inmenu", "Expand View") , this);
        collection->setDefaultShortcut(expandActiveAction, Qt::CTRL + Qt::SHIFT + Qt::Key_BracketRight);
        expandActiveAction->setEnabled(false);
        collection->addAction("expand-active-view", expandActiveAction);
        connect(expandActiveAction , &QAction::triggered , this , &Konsole::ViewManager::expandActiveContainer);

        multiViewOnlyActions << expandActiveAction;

        QAction* shrinkActiveAction = new QAction(i18nc("@action:inmenu", "Shrink View") , this);
        collection->setDefaultShortcut(shrinkActiveAction, Qt::CTRL + Qt::SHIFT + Qt::Key_BracketLeft);
        shrinkActiveAction->setEnabled(false);
        collection->addAction("shrink-active-view", shrinkActiveAction);
        connect(shrinkActiveAction , &QAction::triggered , this , &Konsole::ViewManager::shrinkActiveContainer);

        multiViewOnlyActions << shrinkActiveAction;

#if defined(ENABLE_DETACHING)
        QAction* detachViewAction = collection->addAction("detach-view");
        detachViewAction->setIcon(QIcon::fromTheme(QStringLiteral("tab-detach")));
        detachViewAction->setText(i18nc("@action:inmenu", "D&etach Current Tab"));
        // Ctrl+Shift+D is not used as a shortcut by default because it is too close
        // to Ctrl+D - which will terminate the session in many cases
        collection->setDefaultShortcut(detachViewAction, Qt::CTRL + Qt::SHIFT + Qt::Key_H);

        connect(this , &Konsole::ViewManager::splitViewToggle , this , &Konsole::ViewManager::updateDetachViewState);
        connect(detachViewAction , &QAction::triggered , this , &Konsole::ViewManager::detachActiveView);
#endif

        // Next / Previous View , Next Container
        collection->addAction("next-view", nextViewAction);
        collection->addAction("previous-view", previousViewAction);
        collection->addAction("last-tab", lastViewAction);
        collection->addAction("next-container", nextContainerAction);
        collection->addAction("move-view-left", moveViewLeftAction);
        collection->addAction("move-view-right", moveViewRightAction);

        // Switch to tab N shortcuts
        const int SWITCH_TO_TAB_COUNT = 19;
        QSignalMapper* switchToTabMapper = new QSignalMapper(this);
        connect(switchToTabMapper, static_cast<void(QSignalMapper::*)(int)>(&QSignalMapper::mapped), this, &Konsole::ViewManager::switchToView);
        for (int i = 0; i < SWITCH_TO_TAB_COUNT; i++) {
            QAction* switchToTabAction = new QAction(i18nc("@action Shortcut entry", "Switch to Tab %1", i + 1), this);
            switchToTabMapper->setMapping(switchToTabAction, i);
            connect(switchToTabAction, &QAction::triggered, switchToTabMapper, static_cast<void(QSignalMapper::*)()>(&QSignalMapper::map));
            collection->addAction(QString("switch-to-tab-%1").arg(i), switchToTabAction);
        }
    }

    foreach(QAction* action, multiViewOnlyActions) {
        connect(this , &Konsole::ViewManager::splitViewToggle , action , &QAction::setEnabled);
    }
    // keyboard shortcut only actions
    collection->setDefaultShortcut(nextViewAction, Qt::SHIFT + Qt::Key_Right);
    connect(nextViewAction, &QAction::triggered , this , &Konsole::ViewManager::nextView);
    _viewSplitter->addAction(nextViewAction);

    collection->setDefaultShortcut(previousViewAction, Qt::SHIFT + Qt::Key_Left);
    connect(previousViewAction, &QAction::triggered , this , &Konsole::ViewManager::previousView);
    _viewSplitter->addAction(previousViewAction);

    collection->setDefaultShortcut(nextContainerAction, Qt::SHIFT + Qt::Key_Tab);
    connect(nextContainerAction , &QAction::triggered , this , &Konsole::ViewManager::nextContainer);
    _viewSplitter->addAction(nextContainerAction);

    collection->setDefaultShortcut(moveViewLeftAction, Qt::CTRL + Qt::SHIFT + Qt::Key_Left);
    connect(moveViewLeftAction , &QAction::triggered , this , &Konsole::ViewManager::moveActiveViewLeft);
    _viewSplitter->addAction(moveViewLeftAction);

    collection->setDefaultShortcut(moveViewRightAction, Qt::CTRL + Qt::SHIFT + Qt::Key_Right);
    connect(moveViewRightAction , &QAction::triggered , this , &Konsole::ViewManager::moveActiveViewRight);
    _viewSplitter->addAction(moveViewRightAction);

    connect(lastViewAction, &QAction::triggered , this , &Konsole::ViewManager::lastView);
    _viewSplitter->addAction(lastViewAction);


    // multi terminal
    // Menu item for the vertical split of the multi terminal
    QAction * multiTerminalVerAction;
    multiTerminalVerAction = new QAction(QIcon::fromTheme("view-split-left-right"), i18nc("@action:inmenu", "Split Pane &Vertically"), this);
    multiTerminalVerAction->setEnabled(true);
    collection->addAction("multi-terminal-ver", multiTerminalVerAction);
    _viewSplitter->addAction(multiTerminalVerAction);
    collection->setDefaultShortcut(multiTerminalVerAction, Qt::META + Qt::Key_D);
    connect(multiTerminalVerAction, SIGNAL(triggered()), this, SLOT(multiTerminalVertical()));

    // Menu item for the horizontal split of the multi terminal
    QAction * multiTerminalHorAction = new QAction(QIcon::fromTheme("view-split-top-bottom"), i18nc("@action:inmenu", "Split Pane &Horizontally"), this);
    multiTerminalHorAction->setEnabled(true);
    collection->addAction("multi-terminal-hor", multiTerminalHorAction);
    _viewSplitter->addAction(multiTerminalHorAction);
    collection->setDefaultShortcut(multiTerminalHorAction, Qt::META + Qt::CTRL + Qt::Key_D);
    connect(multiTerminalHorAction, SIGNAL(triggered()), this, SLOT(multiTerminalHorizontal()));

    // Menu item for closing a multi terminal
    QAction * closeMultiTerminalAction = new QAction(QIcon::fromTheme("view-close"), i18nc("@action:inmenu", "&Close"), this);
    collection->addAction("multi-terminal-close", closeMultiTerminalAction);
    _viewSplitter->addAction(closeMultiTerminalAction);
    collection->setDefaultShortcut(closeMultiTerminalAction, Qt::CTRL + Qt::Key_W);
    connect(closeMultiTerminalAction, SIGNAL(triggered()), this, SLOT(multiTerminalClose()));


    // Shortcut to move to the MTD to the left
    QAction * goToLeftMtdAction = 0;
    goToLeftMtdAction = collection->addAction("to-left-mtd", this, SLOT(moveToLeftMtd()));
    goToLeftMtdAction->setText(i18n("&Move to closest multi-terminal on the left"));
    // TODO: icon?
    goToLeftMtdAction->setIcon(QIcon::fromTheme("edit-rename"));
    collection->setDefaultShortcut(goToLeftMtdAction, Qt::ALT + Qt::Key_Left);
    // Shortcut to move to the MTD above
    QAction * goToTopMtdAction = 0;
    goToTopMtdAction = collection->addAction("to-top-mtd", this, SLOT(moveToTopMtd()));
    goToTopMtdAction->setText(i18n("&Move to closest multi-terminal above"));
    // TODO: icon?
    goToTopMtdAction->setIcon(QIcon::fromTheme("edit-rename"));
    collection->setDefaultShortcut(goToTopMtdAction, Qt::ALT + Qt::Key_Up);
    // Shortcut to move to the MTD to the right
    QAction * goToRightMtdAction = 0;
    goToRightMtdAction = collection->addAction("to-right-mtd", this, SLOT(moveToRightMtd()));
    goToRightMtdAction->setText(i18n("&Move to closest multi-terminal on the right"));
    // TODO: icon?
    goToRightMtdAction->setIcon(QIcon::fromTheme("edit-rename"));
    collection->setDefaultShortcut(goToRightMtdAction, Qt::ALT + Qt::Key_Right);
    // Shortcut to move to the MTD below
    QAction * goToBottomMtdAction = 0;
    goToBottomMtdAction = collection->addAction("to-bottom-mtd", this, SLOT(moveToBottomMtd()));
    goToBottomMtdAction->setText(i18n("&Move to closest multi-terminal below"));
    // TODO: icon?
    goToBottomMtdAction->setIcon(QIcon::fromTheme("edit-rename"));
    collection->setDefaultShortcut(goToBottomMtdAction, Qt::ALT + Qt::Key_Down);
    // Shortcut to cycle forward
    QAction* cycleForwardMtdAction = new QAction(
        i18nc("@action Shortcut entry", "Cycle to next multi terminal"), this
    );
    collection->addAction("cycle-forward-mtd", cycleForwardMtdAction);
    collection->setDefaultShortcut(
        cycleForwardMtdAction, Qt::CTRL + Qt::Key_QuoteLeft
    );
    connect(cycleForwardMtdAction, &QAction::triggered, this, &Konsole::ViewManager::cycleForwardMtd);
    _viewSplitter->addAction(cycleForwardMtdAction);
}
void ViewManager::switchToView(int index)
{
    Q_ASSERT(index >= 0);
    ViewContainer* container = _viewSplitter->activeContainer();
    Q_ASSERT(container);
    QList<QWidget*> containerViews = container->views();
    if (index >= containerViews.count()) {
        return;
    }
    container->setActiveView(containerViews.at(index));
}
void ViewManager::updateDetachViewState()
{
    if (!_actionCollection)
        return;

    const bool splitView = _viewSplitter->containers().count() >= 2;
    auto activeContainer = _viewSplitter->activeContainer();
    const bool shouldEnable = splitView || (activeContainer && activeContainer->views().count() >= 2);

    QAction* detachAction = _actionCollection->action("detach-view");

    if (detachAction && shouldEnable != detachAction->isEnabled())
        detachAction->setEnabled(shouldEnable);
}
void ViewManager::moveActiveViewLeft()
{
    ViewContainer* container = _viewSplitter->activeContainer();
    Q_ASSERT(container);
    container->moveActiveView(ViewContainer::MoveViewLeft);
}
void ViewManager::moveActiveViewRight()
{
    ViewContainer* container = _viewSplitter->activeContainer();
    Q_ASSERT(container);
    container->moveActiveView(ViewContainer::MoveViewRight);
}
void ViewManager::nextContainer()
{
    _viewSplitter->activateNextContainer();
}

void ViewManager::nextView()
{
    ViewContainer* container = _viewSplitter->activeContainer();

    Q_ASSERT(container);

    container->activateNextView();
}

void ViewManager::previousView()
{
    ViewContainer* container = _viewSplitter->activeContainer();

    Q_ASSERT(container);

    container->activatePreviousView();
}

void ViewManager::lastView()
{
    ViewContainer* container = _viewSplitter->activeContainer();

    Q_ASSERT(container);

    container->activateLastView();
}

void ViewManager::detachActiveView()
{
    // find the currently active view and remove it from its container
    ViewContainer* container = _viewSplitter->activeContainer();

    detachView(container, container->activeView());
}

void ViewManager::detachView(ViewContainer* container, QWidget* widgetView)
{
#if !defined(ENABLE_DETACHING)
    return;
#endif

    MultiTerminalDisplay * viewToDetach = qobject_cast<MultiTerminalDisplay*>(widgetView);

    if (!viewToDetach)
        return;

    QSet<TerminalDisplay*> tds = _mtdManager->getTerminalDisplaysOfContainer(viewToDetach);
    foreach (TerminalDisplay* td, tds) {
        // Every time this signal is emitted, a new window with the given session is created
        // see Application::detachView(Session* session)
        // Also a new ViewManager will be created, how to clone the multi-terminals?
        emit viewDetached(_sessionMap[td]);
        _sessionMap.remove(td);
    }

    // remove the view from this window
    container->removeView(viewToDetach);
    viewToDetach->deleteLater();

    // if the container from which the view was removed is now empty then it can be deleted,
    // unless it is the only container in the window, in which case it is left empty
    // so that there is always an active container
    if (_viewSplitter->containers().count() > 1 &&
            container->views().count() == 0) {
        removeContainer(container);
    }
}

void ViewManager::sessionFinished()
{
    qDebug() << "VINCENZO: calling sessionFinished";
    // if this slot is called after the view manager's main widget
    // has been destroyed, do nothing
    if (!_viewSplitter)
        return;

    Session* session = qobject_cast<Session*>(sender());
    Q_ASSERT(session);

    // TODO: all multi terminals must be removed as well

    // close attached views
    QList<TerminalDisplay*> children = _viewSplitter->findChildren<TerminalDisplay*>();

    foreach(TerminalDisplay* view , children) {
        if (_sessionMap[view] == session) {
            _sessionMap.remove(view);
            view->deleteLater();
        }
    }

    // This is needed to remove this controller from factory() in
    // order to prevent BUG: 185466 - disappearing menu popup
    if (_pluggedController)
        emit unplugController(_pluggedController);
}

void ViewManager::viewActivated(QWidget* view)
{
    Q_ASSERT(view != 0);

    // focus the activated view, this will cause the SessionController
    // to notify the world that the view has been focused and the appropriate UI
    // actions will be plugged in.
    view->setFocus(Qt::OtherFocusReason);
}

void ViewManager::splitLeftRight()
{
    splitView(Qt::Horizontal);
}
void ViewManager::splitTopBottom()
{
    splitView(Qt::Vertical);
}

void ViewManager::splitView(Qt::Orientation orientation)
{
    ViewContainer* container = createContainer();

    // iterate over each session which has a view in the current active
    // container and create a new view for that session in a new container
    // TODO XXX: non va bene: prima era _viewSplitter->activeContainer()->views() che
    //       per un tab container significava tutte le tabs (1 tab per TerminalDisplay).
    //       Ora sono i TerminalDisplay in una singola tab.
    // foreach(QWidget* view,  getTerminalsFromContainer(_viewSplitter->activeContainer())) {
    
    
    // For each view of the container (for each tab): _viewSplitter->activeContainer()->views()
    // Get the tree of MTDs of that Tab
    // Create a widget that contains all the subwidgets (the MTD tree) but uses the same terminal sessions
    // Add this widget (i.e., tab) to the new container (created above)
    
    foreach (QWidget* view, _viewSplitter->activeContainer()->views()) {
        MultiTerminalDisplay* mtd = qobject_cast<MultiTerminalDisplay*>(view);
        if (!mtd) {
            qCritical() << "Cannot cast container view to MultiTerminalDisplay";
            return;
        }

        _mtdManager->cloneMtd(mtd, container);
    }

    _viewSplitter->addContainer(container, orientation);
    emit splitViewToggle(_viewSplitter->containers().count() > 0);

    // focus the new container
    container->containerWidget()->setFocus();

    // ensure that the active view is focused after the split / unsplit
    ViewContainer* activeContainer = _viewSplitter->activeContainer();
    QWidget* activeView = activeContainer ? activeContainer->activeView() : 0;

    if (activeView)
        activeView->setFocus(Qt::OtherFocusReason);
}

void ViewManager::expandActiveContainer()
{
    _viewSplitter->adjustContainerSize(_viewSplitter->activeContainer(), 10);
}
void ViewManager::shrinkActiveContainer()
{
    _viewSplitter->adjustContainerSize(_viewSplitter->activeContainer(), -10);
}

void ViewManager::removeContainer(ViewContainer* container)
{
//     // TODO: remove all the multiterminals
//     // remove session map entries for views in this container
//     foreach(QWidget* view , _mtdManager->getTerminalDisplays()) {
//         TerminalDisplay* display = qobject_cast<TerminalDisplay*>(view);
//         Q_ASSERT(display);
//         _sessionMap.remove(display);
//     }
//     _viewSplitter->removeContainer(container);
//     container->deleteLater();
}

void ViewManager::multiTerminalHorizontal()
{
    // This is called from the menu action
    qDebug() << "ViewManager::multiTerminalHorizontal() ";
    createMultiTerminalView(Qt::Vertical);
}
void ViewManager::multiTerminalVertical()
{
    // This is called from the menu action
    qDebug() << "ViewManager::multiTerminalVertical() ";
    createMultiTerminalView(Qt::Horizontal);
}

void ViewManager::multiTerminalClose()
{
    // TODO: make sure we close the one that has focused.

    MultiTerminalDisplay* containerMtd = qobject_cast<MultiTerminalDisplay*>(_viewSplitter->activeContainer()->activeView());

    // MultiTerminalDisplay with focus
    MultiTerminalDisplay* mtd = _mtdManager->getFocusedMultiTerminalDisplay(containerMtd);
    MultiTerminalDisplay* root = _mtdManager->getRootNode(containerMtd);

    _mtdManager->removeTerminalDisplay(mtd);

//     // TODO: not ok, this closes the application even if other tabs are open (mtdtrees with different roots)
//     if (_mtdManager->getNumberOfNodes(root) == 0) {
//         // no more terminal, let's exit()
//         exit(0);
//     }

    //_mtdManager->dismissMultiTerminals(mtd);
}

void ViewManager::moveToLeftMtd()
{
    moveMtdFocus(MultiTerminalDisplayManager::LEFT);
}

void ViewManager::moveToTopMtd()
{
    moveMtdFocus(MultiTerminalDisplayManager::TOP);
}

void ViewManager::moveToRightMtd()
{
    moveMtdFocus(MultiTerminalDisplayManager::RIGHT);
}

void ViewManager::moveToBottomMtd()
{
    moveMtdFocus(MultiTerminalDisplayManager::BOTTOM);
}

void ViewManager::cycleForwardMtd()
{

    qDebug() << "cycleForwardMtdAction";
    /*
    //TODO:: do it :P
    //moveMtdFocus(MultiTerminalDisplayManager::BOTTOM);
    _mtdManager = new MultiTerminalDisplayManager(this);

    MultiTerminalDisplay* mtd = qobject_cast<MultiTerminalDisplay*>(view);
    MultiTerminalDisplay* nextNode = originalRoot;
    nextNode = sourceTree->traverseTreeAndYeldNodes(nextNode);

    // get next terminal
    // activate next terminal
    qDebug() << "ViewManager::createMultiTerminalView";

    QString currentWorkingDir = activeViewController()->currentDir();

    Profile::Ptr defaultProfile = ProfileManager::instance()->defaultProfile();

    Session* session = SessionManager::instance()->createSession(defaultProfile);

    if (!currentWorkingDir.isEmpty() && defaultProfile->startInCurrentSessionDir())
        session->setInitialWorkingDirectory(currentWorkingDir);

    session->addEnvironmentEntry(QString("KONSOLE_DBUS_WINDOW=/Windows/%1").arg(managerId()));

    connect(session, SIGNAL(finished()), this, SLOT(sessionFinished()), Qt::UniqueConnection);

    TerminalDisplay* display = createTerminalDisplay(session);
    if (session == NULL) {qDebug() << "session was null!"; return;}
    const Profile::Ptr profile = SessionManager::instance()->sessionProfile(session);
    applyProfileToView(display, profile);
    _sessionMap[display] = session;
    session->addView(display);
    createController(session, display);

    MultiTerminalDisplay* containerMtd = qobject_cast<MultiTerminalDisplay*>(
        _viewSplitter->activeContainer()->activeView()
    );
    MultiTerminalDisplay* multiTerminalDisplay = _mtdManager->getFocusedMultiTerminalDisplay(containerMtd);
    _mtdManager->addTerminalDisplay(display, session, multiTerminalDisplay, orientation);
    session->setDarkBackground(colorSchemeForProfile(profile)->hasDarkBackground());
    updateDetachViewState();
    */
}

void ViewManager::moveMtdFocus(MultiTerminalDisplayManager::Directions direction)
{
    qDebug() << "moveMtdFocus";
    MultiTerminalDisplay* containerMtd = qobject_cast<MultiTerminalDisplay*>(_viewSplitter->activeContainer()->activeView());
    MultiTerminalDisplay* focusMtd = _mtdManager->getFocusedMultiTerminalDisplay(containerMtd);
    TerminalDisplay* td = _mtdManager->getTerminalDisplayTo(focusMtd, direction, containerMtd);
    if (td) {
        td->setFocus();
    }
}

void ViewManager::closeActiveContainer()
{
    // only do something if there is more than one container active
    if (_viewSplitter->containers().count() > 1) {
        ViewContainer* container = _viewSplitter->activeContainer();

        removeContainer(container);

        // focus next container so that user can continue typing
        // without having to manually focus it themselves
        nextContainer();
    }
}

void ViewManager::closeOtherContainers()
{
    ViewContainer* active = _viewSplitter->activeContainer();

    foreach(ViewContainer* container, _viewSplitter->containers()) {
        if (container != active)
            removeContainer(container);
    }
}

SessionController* ViewManager::createController(Session* session , TerminalDisplay* view)
{
    qDebug() << "VINCENZO: createController";
    // create a new controller for the session, and ensure that this view manager
    // is notified when the view gains the focus
    SessionController* controller = new SessionController(session, view, this);
    connect(controller , &Konsole::SessionController::focused , this , &Konsole::ViewManager::controllerChanged);
    connect(session , &Konsole::Session::destroyed , controller , &Konsole::SessionController::deleteLater);
    connect(session , &Konsole::Session::primaryScreenInUse , controller , &Konsole::SessionController::setupPrimaryScreenSpecificActions);
    connect(session , &Konsole::Session::selectionChanged , controller , &Konsole::SessionController::selectionChanged);
    connect(view , &Konsole::TerminalDisplay::destroyed , controller , &Konsole::SessionController::deleteLater);

    // if this is the first controller created then set it as the active controller
    if (!_pluggedController)
        controllerChanged(controller);

    return controller;
}

void ViewManager::controllerChanged(SessionController* controller)
{
    qDebug() << "VINCENZO: controllerchanged";
    if (controller == _pluggedController)
        return;

    _viewSplitter->setFocusProxy(controller->view());

    _pluggedController = controller;
    emit activeViewChanged(controller);
}

SessionController* ViewManager::activeViewController() const
{
    return _pluggedController;
}

IncrementalSearchBar* ViewManager::searchBar() const
{
    return _viewSplitter->activeSplitter()->activeContainer()->searchBar();
}

TerminalDisplay* ViewManager::createAndSetupTerminalDisplay(Session* session)
{
    if (session == NULL) {qDebug() << "session was null!"; return NULL;}
    TerminalDisplay* td = createTerminalDisplay(session);
    const Profile::Ptr profile = SessionManager::instance()->sessionProfile(session);
    applyProfileToView(td, profile);
    createController(session, td);
    _sessionMap[td] = session;
    session->addView(td);
    return td;
}


void ViewManager::createView(Session* session, ViewContainer* container, int index)
{
    // notify this view manager when the session finishes so that its view
    // can be deleted
    //
    // Use Qt::UniqueConnection to avoid duplicate connection
    connect(session, &Konsole::Session::finished, this, &Konsole::ViewManager::sessionFinished, Qt::UniqueConnection);

    TerminalDisplay* display = createTerminalDisplay(session);

    // TODO: container here is the container of tabs, while what we want is the view of a single tab
    // Maybe it is container->activeView(); ?
    MultiTerminalDisplay* multiTerminalDisplay = _mtdManager->createRootTerminalDisplay(display, session, container);

    const Profile::Ptr profile = SessionManager::instance()->sessionProfile(session);
    applyProfileToView(display, profile);

    // set initial size
    const QSize& preferredSize = session->preferredSize();
    // FIXME: +1 is needed here for getting the expected rows
    // Note that the display shouldn't need to take into account the tabbar.
    // However, it appears that taking into account the tabbar is needed.
    // If tabbar is not visible, no +1 is needed here; however, depending on
    // settings/tabbar style, +2 might be needed.
    // 1st attempt at fixing the above:
    // Guess if tabbar will NOT be visible; ignore ShowNavigationAsNeeded
    int heightAdjustment = 0;
    if (_navigationVisibility != ViewContainer::AlwaysHideNavigation) {
        heightAdjustment = 2;
    }

    display->setSize(preferredSize.width(), preferredSize.height() + heightAdjustment);
    ViewProperties* properties = createController(session, display);

    _sessionMap[display] = session;

    container->addView(multiTerminalDisplay, properties, index);
    session->addView(display);

    // tell the session whether it has a light or dark background
    session->setDarkBackground(colorSchemeForProfile(profile)->hasDarkBackground());

    if (container == _viewSplitter->activeContainer()) {
        container->setActiveView(multiTerminalDisplay);
        display->setFocus(Qt::OtherFocusReason);
    }

    updateDetachViewState();
}

void ViewManager::createView(Session* session)
{
    // create the default container
    if (_viewSplitter->containers().count() == 0) {
        // TODO: not happening
        qDebug() << "void ViewManager::createView, we are creating the default container";
        ViewContainer* container = createContainer();
        _viewSplitter->addContainer(container, Qt::Vertical);
    }

    // new tab will be put at the end by default.
    int index = -1;

    if (_newTabBehavior == PutNewTabAfterCurrentTab) {
        QWidget* view = activeView();
        if (view) {
            QList<QWidget*> views = _viewSplitter->activeContainer()->views();
            index = views.indexOf(view) + 1;
        }
    }

    // iterate over the view containers owned by this view manager
    // and create a new terminal display for the session in each of them, along with
    // a controller for the session/display pair
    // TODO: this means that if a view manager is split into two view containers and a new
    // tab is created, the tab will go in both views. So the same must happen for multi
    // terminal
    foreach(ViewContainer* container,  _viewSplitter->containers()) {
        createView(session, container, index);
    }
}

void ViewManager::createMultiTerminalView(Qt::Orientation orientation)
{
    qDebug() << "ViewManager::createMultiTerminalView";

    QString currentWorkingDir = activeViewController()->currentDir();

    Profile::Ptr defaultProfile = ProfileManager::instance()->defaultProfile();

    Session* session = SessionManager::instance()->createSession(defaultProfile);

    if (!currentWorkingDir.isEmpty() && defaultProfile->startInCurrentSessionDir())
        session->setInitialWorkingDirectory(currentWorkingDir);

    session->addEnvironmentEntry(QString("KONSOLE_DBUS_WINDOW=/Windows/%1").arg(managerId()));

    connect(session, SIGNAL(finished()), this, SLOT(sessionFinished()), Qt::UniqueConnection);

    TerminalDisplay* display = createTerminalDisplay(session);
    if (session == NULL) {qDebug() << "session was null!"; return;}
    const Profile::Ptr profile = SessionManager::instance()->sessionProfile(session);
    applyProfileToView(display, profile);
    _sessionMap[display] = session;
    session->addView(display);
    createController(session, display);

    MultiTerminalDisplay* containerMtd = qobject_cast<MultiTerminalDisplay*>(
        _viewSplitter->activeContainer()->activeView()
    );
    MultiTerminalDisplay* multiTerminalDisplay = _mtdManager->getFocusedMultiTerminalDisplay(containerMtd);
    _mtdManager->addTerminalDisplay(display, session, multiTerminalDisplay, orientation);


    session->setDarkBackground(colorSchemeForProfile(profile)->hasDarkBackground());

    updateDetachViewState();
}

ViewContainer* ViewManager::createContainer()
{
    ViewContainer* container = 0;

    qDebug() << "VINCENZO: navigationMethod: " << _navigationMethod;

    switch (_navigationMethod) {
    case TabbedNavigation: {
        TabbedViewContainer* tabbedContainer = new TabbedViewContainer(_navigationPosition, this, _viewSplitter);
        container = tabbedContainer;

        connect(tabbedContainer, &TabbedViewContainer::detachTab, this, &ViewManager::detachView);
        connect(tabbedContainer, &TabbedViewContainer::closeTab, this, &ViewManager::closeTabFromContainer);
    }
    break;
    case NoNavigation:
    default:
        container = new StackedViewContainer(_viewSplitter);
    }

    // FIXME: these code feels duplicated
    container->setNavigationVisibility(_navigationVisibility);
    container->setNavigationPosition(_navigationPosition);
    container->setStyleSheet(_navigationStyleSheet);
    if (_showQuickButtons) {
        container->setFeatures(container->features()
                               | ViewContainer::QuickNewView
                               | ViewContainer::QuickCloseView);
    } else {
        container->setFeatures(container->features()
                               & ~ViewContainer::QuickNewView
                               & ~ViewContainer::QuickCloseView);
    }

    // connect signals and slots
    connect(container , &Konsole::ViewContainer::viewAdded , _containerSignalMapper , static_cast<void(QSignalMapper::*)()>(&QSignalMapper::map));
    connect(container , &Konsole::ViewContainer::viewRemoved , _containerSignalMapper , static_cast<void(QSignalMapper::*)()>(&QSignalMapper::map));
    _containerSignalMapper->setMapping(container, container);

    connect(container, static_cast<void(ViewContainer::*)()>(&Konsole::ViewContainer::newViewRequest), this, static_cast<void(ViewManager::*)()>(&Konsole::ViewManager::newViewRequest));
    connect(container, static_cast<void(ViewContainer::*)(Profile::Ptr)>(&Konsole::ViewContainer::newViewRequest), this, static_cast<void(ViewManager::*)(Profile::Ptr)>(&Konsole::ViewManager::newViewRequest));
    connect(container, &Konsole::ViewContainer::moveViewRequest, this , &Konsole::ViewManager::containerMoveViewRequest);
    connect(container , &Konsole::ViewContainer::viewRemoved , this , &Konsole::ViewManager::viewDestroyed);
    connect(container , &Konsole::ViewContainer::activeViewChanged , this , &Konsole::ViewManager::viewActivated);

    return container;
}

void ViewManager::setNavigationMethod(NavigationMethod method)
{
    _navigationMethod = method;

    KActionCollection* collection = _actionCollection;

    if (collection) {
        // FIXME: The following disables certain actions for the KPart that it
        // doesn't actually have a use for, to avoid polluting the action/shortcut
        // namespace of an application using the KPart (otherwise, a shortcut may
        // be in use twice, and the user gets to see an "ambiguous shortcut over-
        // load" error dialog). However, this approach sucks - it's the inverse of
        // what it should be. Rather than disabling actions not used by the KPart,
        // a method should be devised to only enable those that are used, perhaps
        // by using a separate action collection.

        const bool enable = (_navigationMethod != NoNavigation);
        QAction* action;

        action = collection->action("next-view");
        if (action) action->setEnabled(enable);

        action = collection->action("previous-view");
        if (action) action->setEnabled(enable);

        action = collection->action("last-tab");
        if (action) action->setEnabled(enable);

        action = collection->action("split-view-left-right");
        if (action) action->setEnabled(enable);

        action = collection->action("split-view-top-bottom");
        if (action) action->setEnabled(enable);

        action = collection->action("rename-session");
        if (action) action->setEnabled(enable);

        action = collection->action("move-view-left");
        if (action) action->setEnabled(enable);

        action = collection->action("move-view-right");
        if (action) action->setEnabled(enable);
        
        action = collection->action("multi-terminal");
        if (action) action->setEnabled(enable);
    }
}

void ViewManager::containerMoveViewRequest(int index, int id, bool& moved, TabbedViewContainer* sourceTabbedContainer)
{
    ViewContainer* container = qobject_cast<ViewContainer*>(sender());
    SessionController* controller = qobject_cast<SessionController*>(ViewProperties::propertiesById(id));

    if (!controller)
        return;

    // do not move the last tab in a split view.
    if (sourceTabbedContainer) {
        QPointer<ViewContainer> sourceContainer = qobject_cast<ViewContainer*>(sourceTabbedContainer);

        if (_viewSplitter->containers().contains(sourceContainer)) {
            return;
        } else {
            ViewManager* sourceViewManager = sourceTabbedContainer->connectedViewManager();

            // do not remove the last tab on the window
            if (qobject_cast<ViewSplitter*>(sourceViewManager->widget())->containers().size() > 1) {
                return;
            }
        }
    }

    createView(controller->session(), container, index);
    controller->session()->refresh();
    moved = true;
}



ViewManager::NavigationMethod ViewManager::navigationMethod() const
{
    return _navigationMethod;
}

void ViewManager::containerViewsChanged(QObject* container)
{
    if (_viewSplitter && container == _viewSplitter->activeContainer()) {
        emit viewPropertiesChanged(viewProperties());
    }
}

void ViewManager::viewDestroyed(QWidget* view)
{
    // Note: the received QWidget has already been destroyed, so
    // using dynamic_cast<> or qobject_cast<> does not work here
    TerminalDisplay* display = static_cast<TerminalDisplay*>(view);
    Q_ASSERT(display);

    // 1. detach view from session
    // 2. if the session has no views left, close it
    Session* session = _sessionMap[ display ];
    _sessionMap.remove(display);
    if (session) {
        if (session->views().count() == 0)
            session->close();
    }
    //we only update the focus if the splitter is still alive
    if (_viewSplitter) {
        updateDetachViewState();
    }
    // The below causes the menus  to be messed up
    // Only happens when using the tab bar close button
//    if (_pluggedController)
//        emit unplugController(_pluggedController);
}

TerminalDisplay* ViewManager::createTerminalDisplay(Session* session)
{
    TerminalDisplay* display = new TerminalDisplay(0);
    display->setRandomSeed(session->sessionId() * 31);

    return display;
}

const ColorScheme* ViewManager::colorSchemeForProfile(const Profile::Ptr profile)
{
    const ColorScheme* colorScheme = ColorSchemeManager::instance()->
                                     findColorScheme(profile->colorScheme());
    if (!colorScheme)
        colorScheme = ColorSchemeManager::instance()->defaultColorScheme();
    Q_ASSERT(colorScheme);

    return colorScheme;
}

void ViewManager::applyProfileToView(TerminalDisplay* view , const Profile::Ptr profile)
{
    Q_ASSERT(profile);

    emit updateWindowIcon();

    // load color scheme
    ColorEntry table[TABLE_COLORS];
    const ColorScheme* colorScheme = colorSchemeForProfile(profile);
    colorScheme->getColorTable(table , view->randomSeed());
    view->setColorTable(table);
    view->setOpacity(colorScheme->opacity());
    view->setWallpaper(colorScheme->wallpaper());

    // load font
    view->setAntialias(profile->antiAliasFonts());
    view->setBoldIntense(profile->boldIntense());
    view->setVTFont(profile->font());

    // set scroll-bar position
    int scrollBarPosition = profile->property<int>(Profile::ScrollBarPosition);

    if (scrollBarPosition == Enum::ScrollBarLeft)
        view->setScrollBarPosition(Enum::ScrollBarLeft);
    else if (scrollBarPosition == Enum::ScrollBarRight)
        view->setScrollBarPosition(Enum::ScrollBarRight);
    else if (scrollBarPosition == Enum::ScrollBarHidden)
        view->setScrollBarPosition(Enum::ScrollBarHidden);

    bool scrollFullPage = profile->property<bool>(Profile::ScrollFullPage);
    view->setScrollFullPage(scrollFullPage);

    // show hint about terminal size after resizing
    view->setShowTerminalSizeHint(profile->showTerminalSizeHint());

    // terminal features
    view->setBlinkingCursorEnabled(profile->blinkingCursorEnabled());
    view->setBlinkingTextEnabled(profile->blinkingTextEnabled());

    int tripleClickMode = profile->property<int>(Profile::TripleClickMode);
    view->setTripleClickMode(Enum::TripleClickModeEnum(tripleClickMode));

    view->setAutoCopySelectedText(profile->autoCopySelectedText());
    view->setUnderlineLinks(profile->underlineLinksEnabled());
    view->setControlDrag(profile->property<bool>(Profile::CtrlRequiredForDrag));
    view->setBidiEnabled(profile->bidiRenderingEnabled());
    view->setLineSpacing(profile->lineSpacing());
    view->setTrimTrailingSpaces(profile->property<bool>(Profile::TrimTrailingSpacesInSelectedText));

    view->setOpenLinksByDirectClick(profile->property<bool>(Profile::OpenLinksByDirectClickEnabled));

    int middleClickPasteMode = profile->property<int>(Profile::MiddleClickPasteMode);
    if (middleClickPasteMode == Enum::PasteFromX11Selection)
        view->setMiddleClickPasteMode(Enum::PasteFromX11Selection);
    else if (middleClickPasteMode == Enum::PasteFromClipboard)
        view->setMiddleClickPasteMode(Enum::PasteFromClipboard);

    // margin/center - these are hard-fixed ATM
    view->setMargin(1);
    view->setCenterContents(false);

    // cursor shape
    int cursorShape = profile->property<int>(Profile::CursorShape);

    if (cursorShape == Enum::BlockCursor)
        view->setKeyboardCursorShape(Enum::BlockCursor);
    else if (cursorShape == Enum::IBeamCursor)
        view->setKeyboardCursorShape(Enum::IBeamCursor);
    else if (cursorShape == Enum::UnderlineCursor)
        view->setKeyboardCursorShape(Enum::UnderlineCursor);

    // cursor color
    if (profile->useCustomCursorColor()) {
        const QColor& cursorColor = profile->customCursorColor();
        view->setKeyboardCursorColor(cursorColor);
    } else {
        // an invalid QColor is used to inform the view widget to
        // draw the cursor using the default color( matching the text)
        view->setKeyboardCursorColor(QColor());
    }

    // word characters
    view->setWordCharacters(profile->wordCharacters());

    // bell mode
    view->setBellMode(profile->property<int>(Profile::BellMode));

    // mouse wheel zoom
    view->setMouseWheelZoom(profile->mouseWheelZoomEnabled());
}

void ViewManager::updateViewsForSession(Session* session)
{
    const Profile::Ptr profile = SessionManager::instance()->sessionProfile(session);

    foreach(TerminalDisplay* view, _sessionMap.keys(session)) {
        applyProfileToView(view, profile);
    }
}

void ViewManager::profileChanged(Profile::Ptr profile)
{
    // update all views associated with this profile
    QHashIterator<TerminalDisplay*, Session*> iter(_sessionMap);
    while (iter.hasNext()) {
        iter.next();

        // if session uses this profile, update the display
        if (iter.key() != 0 &&
                iter.value() != 0 &&
                SessionManager::instance()->sessionProfile(iter.value()) == profile) {
            applyProfileToView(iter.key(), profile);
        }
    }
}

QList<ViewProperties*> ViewManager::viewProperties() const
{
    QList<ViewProperties*> list;

    ViewContainer* container = _viewSplitter->activeContainer();

    Q_ASSERT(container);

    foreach(QWidget* view, _mtdManager->getTerminalDisplays()) {
        ViewProperties* properties = container->viewProperties(view);
        Q_ASSERT(properties);
        list << properties;
    }

    return list;
}

void ViewManager::saveSessions(KConfigGroup& group)
{
    // find all unique session restore IDs
    QList<int> ids;
    QHash<Session*, int> unique;

    // first: sessions in the active container, preserving the order
    ViewContainer* container = _viewSplitter->activeContainer();
    Q_ASSERT(container);
    TerminalDisplay* activeview = qobject_cast<TerminalDisplay*>(container->activeView());

    QListIterator<QWidget*> viewIter(_mtdManager->getTerminalDisplays());
    int tab = 1;
    while (viewIter.hasNext()) {
        TerminalDisplay* view = qobject_cast<TerminalDisplay*>(viewIter.next());
        Q_ASSERT(view);
        Session* session = _sessionMap[view];
        ids << SessionManager::instance()->getRestoreId(session);
        if (view == activeview) group.writeEntry("Active", tab);
        unique.insert(session, 1);
        tab++;
    }

    // second: all other sessions, in random order
    // we don't want to have sessions restored that are not connected
    foreach(Session * session, _sessionMap) {
        if (!unique.contains(session)) {
            ids << SessionManager::instance()->getRestoreId(session);
            unique.insert(session, 1);
        }
    }

    group.writeEntry("Sessions", ids);
}

void ViewManager::restoreSessions(const KConfigGroup& group)
{
    QList<int> ids = group.readEntry("Sessions", QList<int>());
    int activeTab  = group.readEntry("Active", 0);
    TerminalDisplay* display = 0;

    int tab = 1;
    foreach(int id, ids) {
        Session* session = SessionManager::instance()->idToSession(id);
        createView(session);
        if (!session->isRunning())
            session->run();
        if (tab++ == activeTab)
            display = qobject_cast<TerminalDisplay*>(activeView());
    }

    if (display) {
        _viewSplitter->activeContainer()->setActiveView(display);
        display->setFocus(Qt::OtherFocusReason);
    }

    if (ids.isEmpty()) { // Session file is unusable, start default Profile
        Profile::Ptr profile = ProfileManager::instance()->defaultProfile();
        Session* session = SessionManager::instance()->createSession(profile);
        createView(session);
        if (!session->isRunning())
            session->run();
    }
}

uint qHash(QPointer<TerminalDisplay> display)
{
    return qHash((TerminalDisplay*)display);
}

int ViewManager::sessionCount()
{
    return this->_sessionMap.size();
}

int ViewManager::currentSession()
{
    QHash<TerminalDisplay*, Session*>::iterator i;
    for (i = this->_sessionMap.begin(); i != this->_sessionMap.end(); ++i)
        if (i.key()->isVisible())
            return i.value()->sessionId();
    return -1;
}

int ViewManager::newSession()
{
    Profile::Ptr profile = ProfileManager::instance()->defaultProfile();
    Session* session = SessionManager::instance()->createSession(profile);

    this->createView(session);
    session->run();

    return session->sessionId();
}

int ViewManager::newSession(QString profile, QString directory)
{
    const QList<Profile::Ptr> profilelist = ProfileManager::instance()->allProfiles();
    Profile::Ptr profileptr = ProfileManager::instance()->defaultProfile();

    for (int i = 0; i < profilelist.size(); ++i) {
        if (profilelist.at(i)->name() == profile) {
            profileptr = profilelist.at(i);
            break;
        }
    }

    Session* session = SessionManager::instance()->createSession(profileptr);
    session->setInitialWorkingDirectory(directory);

    this->createView(session);
    session->run();

    return session->sessionId();
}

QString ViewManager::defaultProfile()
{
    return ProfileManager::instance()->defaultProfile()->name();
}

QStringList ViewManager::profileList()
{
    return ProfileManager::instance()->availableProfileNames();
}

void ViewManager::nextSession()
{
    this->nextView();
}

void ViewManager::prevSession()
{
    this->previousView();
}

void ViewManager::moveSessionLeft()
{
    this->moveActiveViewLeft();
}

void ViewManager::moveSessionRight()
{
    this->moveActiveViewRight();
}

void ViewManager::setTabWidthToText(bool useTextWidth)
{
    ViewContainer* container = _viewSplitter->activeContainer();
    Q_ASSERT(container);
    container->setNavigationTextMode(useTextWidth);
}

void ViewManager::closeTabFromContainer(ViewContainer* container, QWidget* tab)
{
    // TODO: dismiss all the multi terminals in this tab
    // TODO: The argument should not be TAB, but the MTD with focus
    MultiTerminalDisplay* root = qobject_cast<MultiTerminalDisplay*>(tab);
    _mtdManager->dismissMultiTerminals(root);
//     SessionController* controller = qobject_cast<SessionController*>(container->viewProperties(root));
//     Q_ASSERT(controller);
//     if (controller)
//         controller->closeSession();
}

void ViewManager::setNavigationVisibility(int visibility)
{
    _navigationVisibility =
        static_cast<ViewContainer::NavigationVisibility>(visibility);

    foreach(ViewContainer* container, _viewSplitter->containers()) {
        container->setNavigationVisibility(_navigationVisibility);
    }
}

void ViewManager::setNavigationPosition(int position)
{
    _navigationPosition =
        static_cast<ViewContainer::NavigationPosition>(position);

    foreach(ViewContainer* container, _viewSplitter->containers()) {
        Q_ASSERT(container->supportedNavigationPositions().contains(_navigationPosition));
        container->setNavigationPosition(_navigationPosition);
    }
}

void ViewManager::setNavigationStyleSheet(const QString& styleSheet)
{
    _navigationStyleSheet = styleSheet;

    foreach(ViewContainer* container, _viewSplitter->containers()) {
        container->setStyleSheet(_navigationStyleSheet);
    }
}

void ViewManager::setShowQuickButtons(bool show)
{
    _showQuickButtons = show;

    foreach(ViewContainer* container, _viewSplitter->containers()) {
        if (_showQuickButtons) {
            container->setFeatures(container->features()
                                   | ViewContainer::QuickNewView
                                   | ViewContainer::QuickCloseView);
        } else {
            container->setFeatures(container->features()
                                   & ~ViewContainer::QuickNewView
                                   & ~ViewContainer::QuickCloseView);
        }
    }
}


void ViewManager::setNavigationBehavior(int behavior)
{
    _newTabBehavior = static_cast<NewTabBehavior>(behavior);
}

//<<<<<<< HEAD
//=======
// TODO: remove this...
QList<QWidget*> ViewManager::getTerminalsFromContainer(ViewContainer *container) const
{
    return _mtdManager->getTerminalDisplays();
}

#include "ViewManager.moc"

//>>>>>>> vincepii/master
