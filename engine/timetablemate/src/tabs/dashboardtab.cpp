/*
*   Copyright 2012 Friedrich Pülz <fpuelz@gmx.de>
*
*   This program is free software; you can redistribute it and/or modify
*   it under the terms of the GNU Library General Public License as
*   published by the Free Software Foundation; either version 2 or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details
*
*   You should have received a copy of the GNU Library General Public
*   License along with this program; if not, write to the
*   Free Software Foundation, Inc.,
*   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

// Own includes
#include "dashboardtab.h"
#include "../project.h"

// PublicTransport engine includes
#include <engine/timetableaccessor_info.h>

// KDE includes
#include <KStandardDirs>
#include <KDebug>
#include <kdeclarative.h>

// Qt includes
#include <QDeclarativeView>
#include <qdeclarative.h>
#include <QDeclarativeContext>
#include <QDeclarativeEngine>
#include <QBoxLayout>
#include <QToolButton>
#include <QAction>
#include <QGraphicsEffect>

DashboardTab::DashboardTab( Project *project, QWidget *parent )
        : AbstractTab(project, type(), parent), m_qmlView(0)
{
    // Find the QML file used for the dashboard tab
    const QString fileName = KGlobal::dirs()->findResource( "data", "timetablemate/dashboard.qml" );
    if ( fileName.isEmpty() ) {
        kWarning() << "dashboard.qml not found! Check installation";
        return;
    }
    const QString svgFileName = KGlobal::dirs()->findResource( "data", "timetablemate/dashboard.svg" );

    // Register Project and TimetableAccessorInfo in Qt's meta object system and for QML
    qRegisterMetaType< const TimetableAccessorInfo* >( "const TimetableAccessorInfo*" );
    qRegisterMetaType< Project* >( "Project*" );
    qRegisterMetaType< TestModel* >( "TestModel*" );
    qmlRegisterType< TimetableAccessorInfo, 1 >( "TimetableMate", 1, 0, "TimetableAccessorInfo" );
    qmlRegisterType< Project, 1 >( "TimetableMate", 1, 0, "Project" );
    qmlRegisterType< Tabs, 1 >( "TimetableMate", 1, 0, "Tabs" );
//      qmlRegisterUncreatableType<
//     TEST
    qRegisterMetaType< QToolButton* >( "QToolButton*" );
    qmlRegisterType< QToolButton, 1 >( "TimetableMate", 1, 0, "QToolButton" );

    // Create dashboard widget
    QWidget *container = new QWidget( parent );
    m_qmlView = new QDeclarativeView( container );

    // Install a KDeclarative instance to allow eg. QIcon("icon"), i18n("translate")
    KDeclarative *kdeclarative = new KDeclarative();
    kdeclarative->setDeclarativeEngine( m_qmlView->engine() );
    kdeclarative->initialize();
    kdeclarative->setupBindings();

    m_qmlView->setResizeMode( QDeclarativeView::SizeRootObjectToView );
    m_qmlView->rootContext()->setContextProperty( "project", project );
    m_qmlView->rootContext()->setContextProperty( "info", project->info()->clone(this) );
    m_qmlView->rootContext()->setContextProperty( "svgFileName", svgFileName );

    // Add Plasma QML import paths
    const QStringList importPaths = KGlobal::dirs()->findDirs( "module", "imports" );
    foreach( const QString &importPath, importPaths ) {
        m_qmlView->engine()->addImportPath( importPath );
    }

//     m_qmlView->setHorizontalScrollBarPolicy( Qt::ScrollBarAsNeeded );
//     m_qmlView->setVerticalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    m_qmlView->setSource( fileName );
    QVBoxLayout *layout = new QVBoxLayout( container );
    layout->addWidget( m_qmlView );
    setWidget( container );
}

DashboardTab *DashboardTab::create( Project *project, QWidget *parent )
{
    DashboardTab *tab = new DashboardTab( project, parent );
    return tab;
}