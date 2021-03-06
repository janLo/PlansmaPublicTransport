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

// Header
#include "scripttab.h"

// Own includes
#include "project.h"
#include "parserenums.h"
#include "javascriptmodel.h"
#include "javascriptparser.h"
#include "javascriptcompletionmodel.h"
#include "../debugger/debuggerstructures.h"
#include "../debugger/debugger.h"
#include "../debugger/breakpointmodel.h"

// Public Transport engine includes
#include <engine/serviceprovider.h>
#include <engine/serviceproviderdata.h>
#include <engine/script/serviceproviderscript.h>

// KDE includes
#include <KLocalizedString>
#include <KAction>
#include <KActionMenu>
#include <KComboBox>
#include <KTextEditor/Document>
#include <KTextEditor/View>
#include <KTextEditor/CodeCompletionInterface>
#include <KTextEditor/MarkInterface>
#include <KTextEditor/TextHintInterface>
#include <KTextEditor/ConfigInterface>
#include <KMessageBox>
#include <KMenu>

// Qt includes
#include <QWidget>
#include <QSortFilterProxyModel>
#include <QVBoxLayout>
#include <QToolButton>
#include <QToolTip>
#include <QApplication>
#include <QTimer>
#include <QFileInfo>
#include <QDir>

ScriptTab::ScriptTab( Project *project, QWidget *parent )
        : AbstractDocumentTab(project, Tabs::Script, parent),
          m_scriptModel(0), m_completionModel(0), m_functionsModel(0), m_functionsWidget(0),
          m_previousFunctionAction(0), m_nextFunctionAction(0), m_backgroundParserTimer(0),
          m_executionLine(-1)
{
    connect( project->debugger(), SIGNAL(continued(QDateTime,bool)),
             this, SLOT(removeExecutionMarker()) );
    connect( project->debugger(), SIGNAL(stopped()), this, SLOT(removeExecutionMarker()) );
}

ScriptTab::~ScriptTab()
{
}

void ScriptTab::setExecutionPosition( int executionLine, int column )
{
    Q_UNUSED( column );
    goToLine( executionLine );

    // Move execution mark
    KTextEditor::MarkInterface *markInterface =
            qobject_cast<KTextEditor::MarkInterface*>( document() );
    if ( !markInterface ) {
        kDebug() << "Cannot mark current execution line, no KTextEditor::MarkInterface";
    } else if ( m_executionLine != executionLine - 1 ) {
        if ( m_executionLine != -1 ) {
            markInterface->removeMark( m_executionLine, KTextEditor::MarkInterface::Execution );
        }
        m_executionLine = executionLine - 1;
        if ( executionLine != -1 ) {
            markInterface->addMark( m_executionLine, KTextEditor::MarkInterface::Execution );
        }
    }
}

void ScriptTab::removeExecutionMarker()
{
    KTextEditor::MarkInterface *iface = qobject_cast<KTextEditor::MarkInterface*>( document() );
    if ( !iface ) {
        kDebug() << "Cannot remove execution mark, no KTextEditor::MarkInterface";
    } else if ( m_executionLine != -1 ) {
        iface->removeMark( m_executionLine, KTextEditor::MarkInterface::Execution );
        m_executionLine = -1;
        slotTitleChanged();
    }
}

KAction *ScriptTab::createNextFunctionAction( QObject *parent )
{
    KAction *nextFunctionAction = new KAction( KIcon("go-next"), // no icon for this so far
            i18nc("@action", "&Next Function"), parent );
    nextFunctionAction->setToolTip( i18nc("@info:tooltip", "Select the next function.") );
//     nextFunctionAction->setVisible( false );
    nextFunctionAction->setShortcut( KShortcut("Ctrl+Alt+PgDown") ); // Same as in KDevelop
//     connect( nextFunctionAction, SIGNAL(triggered(bool)),
//              scriptTab, SLOT(goToNextFunction()) );
    return nextFunctionAction;
}

KAction *ScriptTab::createPreviousFunctionAction( QObject *parent )
{
    KAction *previousFunctionAction = new KAction( KIcon("go-previous"), // no icon for this so far
            i18nc("@action", "&Previous Function"), parent );
    previousFunctionAction->setToolTip( i18nc("@info:tooltip", "Select the previous function.") );
//     previousFunctionAction->setVisible( false );
    previousFunctionAction->setShortcut( KShortcut("Ctrl+Alt+PgUp") ); // Same as in KDevelop
    return previousFunctionAction;
}

ScriptTab *ScriptTab::create( Project *project, QWidget *parent )
{
    // Create script tab
    ScriptTab *tab = new ScriptTab( project, parent );
    KTextEditor::Document *document = tab->document();
    QWidget *container = new QWidget( parent );
    tab->setWidget( container );

    // Setup actions
    tab->m_previousFunctionAction = createPreviousFunctionAction( tab );
    tab->m_nextFunctionAction = createNextFunctionAction( tab );
    connect( tab->m_previousFunctionAction, SIGNAL(triggered(bool)),
             tab, SLOT(goToPreviousFunction()) );
    connect( tab->m_nextFunctionAction, SIGNAL(triggered(bool)),
             tab, SLOT(goToNextFunction()) );

    // Create widgets/models
    KComboBox *functionsWidget = new KComboBox( container );
    JavaScriptModel *scriptModel = new JavaScriptModel( container );
    QSortFilterProxyModel *functionsModel = new QSortFilterProxyModel( container );
    functionsModel->setSourceModel( scriptModel );
    functionsModel->setFilterRole( Qt::UserRole );
    functionsModel->setFilterFixedString( QString::number(Function) );
    functionsWidget->setModel( functionsModel );
    connect( scriptModel, SIGNAL(showTextHint(KTextEditor::Cursor,QString&)),
             tab, SLOT(showTextHint(KTextEditor::Cursor,QString&)) );
    tab->m_scriptModel = scriptModel;
    tab->m_functionsModel = functionsModel;
    tab->m_functionsWidget = functionsWidget;

    document->setHighlightingMode( "JavaScript" );
    KTextEditor::View *view = tab->defaultView();
    view->setWhatsThis( i18nc("@info:whatsthis",
            "<subtitle>Script File</subtitle>"
            "<para>This shows the script source code. Syntax completion is available for all "
            "functions and strings used by the data engine.</para>"
            "<para>To try out the script functions just click one of the "
            "<interface>Run '<placeholder>function</placeholder>'</interface> buttons.</para>") );

    KTextEditor::CodeCompletionInterface *completionInterface =
            qobject_cast<KTextEditor::CodeCompletionInterface*>( view );
    if ( completionInterface ) {
        // Get the completion shortcut string
        QString completionShortcut;
        if ( !document->views().isEmpty() ) {
            KTextEditor::View *view = document->views().first();
            QAction *completionAction = view->action("tools_invoke_code_completion");
            if ( completionAction ) {
                completionShortcut = completionAction->shortcut().toString(
                                            QKeySequence::NativeText );
            }
        }
        if ( completionShortcut.isEmpty() ) {
            completionShortcut = "unknown"; // Should not happen
        }

        JavaScriptCompletionModel *completionModel =
                new JavaScriptCompletionModel( completionShortcut, document );
        completionInterface->registerCompletionModel( completionModel );
        tab->m_completionModel = completionModel;
    }

    KTextEditor::MarkInterface *markInterface =
            qobject_cast<KTextEditor::MarkInterface*>( document );
    if ( markInterface ) {
        markInterface->setEditableMarks( KTextEditor::MarkInterface::Bookmark |
                                            KTextEditor::MarkInterface::BreakpointActive );
        markInterface->setMarkDescription( KTextEditor::MarkInterface::BreakpointActive,
                i18nc("@info/plain", "Breakpoint") );
        markInterface->setMarkPixmap( KTextEditor::MarkInterface::BreakpointActive,
                KIcon("tools-report-bug").pixmap(16, 16) );
        markInterface->setMarkDescription( KTextEditor::MarkInterface::Execution,
                i18nc("@info/plain", "Execution Line") );
        markInterface->setMarkPixmap( KTextEditor::MarkInterface::Execution,
                KIcon ("go-next").pixmap(16, 16) );
        connect( document, SIGNAL(markChanged(KTextEditor::Document*,KTextEditor::Mark,KTextEditor::MarkInterface::MarkChangeAction)),
                 tab, SLOT(markChanged(KTextEditor::Document*,KTextEditor::Mark,KTextEditor::MarkInterface::MarkChangeAction)) );
        connect( project->debugger()->breakpointModel(), SIGNAL(breakpointAdded(Breakpoint)),
                 tab, SLOT(breakpointAdded(Breakpoint)) );
        connect( project->debugger()->breakpointModel(), SIGNAL(breakpointAboutToBeRemoved(Breakpoint)),
                 tab, SLOT(breakpointAboutToBeRemoved(Breakpoint)) );
    }

    connect( document, SIGNAL(setStatusBarText(QString)),
             tab, SLOT(slotSetStatusBarText(QString)) );
    connect( document, SIGNAL(textChanged(KTextEditor::Document*)),
             tab, SLOT(documentChanged(KTextEditor::Document*)));

    connect( view, SIGNAL(informationMessage(KTextEditor::View*,QString)),
             tab, SLOT(informationMessage(KTextEditor::View*,QString)) );

    KTextEditor::TextHintInterface *textHintInterface =
            qobject_cast<KTextEditor::TextHintInterface*>( view );
    if ( textHintInterface ) {
        textHintInterface->enableTextHints( 250 );
        connect( document->activeView(), SIGNAL(needTextHint(KTextEditor::Cursor,QString&)),
                 scriptModel, SLOT(needTextHint(KTextEditor::Cursor,QString&)) );
        connect( document->activeView(), SIGNAL(contextMenuAboutToShow(KTextEditor::View*,QMenu*)),
                 tab, SLOT(contextMenuAboutToShow(KTextEditor::View*,QMenu*)) );
        scriptModel->setJavaScriptCompletionModel( tab->completionModel() );
    }

    KTextEditor::ConfigInterface *configInterface =
            qobject_cast<KTextEditor::ConfigInterface*>( view );
    if ( configInterface ) {
        configInterface->setConfigValue( "line-numbers", true );
        configInterface->setConfigValue( "icon-bar", true );
        configInterface->setConfigValue( "dynamic-word-wrap", true );
    } else {
        kDebug() << "No KTextEditor::ConfigInterface";
    }

    // Create widgets and layouts
    QToolButton *btnPreviousFunction = new QToolButton( container );
    btnPreviousFunction->setDefaultAction( tab->m_previousFunctionAction );
    QToolButton *btnNextFunction = new QToolButton( container );
    btnNextFunction->setDefaultAction( tab->m_nextFunctionAction );
    QHBoxLayout *layoutScriptTop = new QHBoxLayout();
    layoutScriptTop->setSpacing( 0 );
    layoutScriptTop->addWidget( btnPreviousFunction );
    layoutScriptTop->addWidget( btnNextFunction );
    layoutScriptTop->addWidget( functionsWidget );

    // Main layout: previous/next buttons and a combo box on top, document widget below
    QVBoxLayout *layoutScript = new QVBoxLayout( container );
    layoutScript->setContentsMargins( 0, 2, 0, 0 ); // 2px margin at top
    layoutScript->setSpacing( 0 ); // No space between function combobox and the KTextEditor::View
    layoutScript->addLayout( layoutScriptTop );
    layoutScript->addWidget( view );

    connect( functionsWidget, SIGNAL(currentIndexChanged(int)),
             tab, SLOT(currentFunctionChanged(int)) );
    connect( view, //document->views().first(),
             SIGNAL(cursorPositionChanged(KTextEditor::View*,KTextEditor::Cursor)),
             tab, SLOT(scriptCursorPositionChanged(KTextEditor::View*,KTextEditor::Cursor)) );

    return tab;
}

void ScriptTab::contextMenuAboutToShow( KTextEditor::View *view, QMenu *menu )
{
    // Get the code node at the current cursor position
    const KTextEditor::Cursor &position = view->cursorPosition();
    CodeNode::Ptr node = m_scriptModel->nodeFromLineNumber( position.line() + 1, position.column(),
                                                            JavaScriptModel::MatchChildren );

    // Remove project actions added previously
    while ( !menu->isEmpty() && (Project::isProjectAction(menu->actions().first()) ||
                                 menu->actions().first()->objectName() == QLatin1String("projectMenuAction") ||
                                 menu->actions().first()->isSeparator()) )
    {
        QAction *oldAction = menu->actions().first();
        menu->removeAction( oldAction );
    }

    // Prepend a separator, additional actions get added to the top of the menu
    QAction *separator = menu->insertSeparator(
            menu->actions().isEmpty() ? 0 : menu->actions().first() );

    // Add an action for "include()" function calls to open the included script file
    if ( node && node->line() == position.line() + 1 ) {
        FunctionCallNode *functionCallNode = node->searchUp<FunctionCallNode>();
        if ( functionCallNode && functionCallNode->function() == QLatin1String("include") ) {
            // Get the argument given to the include() function call
            QString fileName = functionCallNode->arguments()->children().first()->text();

            // Create action and make a queued connection (last parameter).
            // The queued connection is needed, because otherwise the action causes the view
            // to be hidden (opening the new tab), while the menu was not closed already,
            // which leads to a crash
            QAction *openExternalAction = project()->createAndConnectProjectAction(
                    Project::ShowExternalScript, fileName, 0, true );
            menu->insertAction( separator, openExternalAction );
        } else {
            FunctionNode *functionNode = node->searchUp<FunctionNode>();
            if ( functionNode ) {
                if ( functionNode->name() == ServiceProviderScript::SCRIPT_FUNCTION_GETTIMETABLE ) {
                    QAction *runDeparturesTestAction = project()->createAndConnectProjectAction(
                            Project::RunSpecificTest, TestModel::DepartureTest, 0, true );
                    QAction *runArrivalsTestAction = project()->createAndConnectProjectAction(
                            Project::RunSpecificTest, TestModel::ArrivalTest, 0, true );
                    menu->insertAction( separator, runDeparturesTestAction );
                    menu->insertAction( separator, runArrivalsTestAction );
                } else if ( functionNode->name() == ServiceProviderScript::SCRIPT_FUNCTION_GETJOURNEYS ) {
                    QAction *runJounreysTestAction = project()->createAndConnectProjectAction(
                            Project::RunSpecificTest, TestModel::JourneyTest, 0, true );
                    menu->insertAction( separator, runJounreysTestAction );
                } else if ( functionNode->name() == ServiceProviderScript::SCRIPT_FUNCTION_GETSTOPSUGGESTIONS ) {
                    QAction *runStopSuggestionsTestAction = project()->createAndConnectProjectAction(
                            Project::RunSpecificTest, TestModel::StopSuggestionTest, 0, true );
                    menu->insertAction( separator, runStopSuggestionsTestAction );
                } else if ( functionNode->name() == ServiceProviderScript::SCRIPT_FUNCTION_FEATURES ) {
                    QAction *runFeaturesTestAction = project()->createAndConnectProjectAction(
                            Project::RunSpecificTest, TestModel::FeaturesTest, 0, true );
                    menu->insertAction( separator, runFeaturesTestAction );
                }
            }
        }
    }

    // Add project menu
    menu->insertAction( separator, project()->projectSubMenuAction().data() );
}

void ScriptTab::informationMessage( KTextEditor::View*, const QString &message )
{
    project()->emitInformationMessage( message );
}

void ScriptTab::slotSetStatusBarText( const QString &message )
{
    project()->emitInformationMessage( message );
}

void ScriptTab::toggleBreakpoint( int lineNumber )
{
    lineNumber = lineNumber == -1
            ? document()->views().first()->cursorPosition().line() + 1 : lineNumber;
    Debugger::Debugger *debugger = project()->debugger();
    lineNumber = debugger->getNextBreakableLineNumber( fileName(), lineNumber );
    debugger->breakpointModel()->toggleBreakpoint( fileName(), lineNumber );
}

void ScriptTab::breakpointAdded( const Breakpoint &breakpoint )
{
    if ( breakpoint.fileName() != fileName() ) {
        return;
    }

    KTextEditor::MarkInterface *markInterface =
            qobject_cast<KTextEditor::MarkInterface*>( document() );
    if ( !markInterface ) {
        kDebug() << "Cannot mark breakpoint, no KTextEditor::MarkInterface";
    } else {
        disconnect( document(), SIGNAL(markChanged(KTextEditor::Document*,KTextEditor::Mark,KTextEditor::MarkInterface::MarkChangeAction)),
                    this, SLOT(markChanged(KTextEditor::Document*,KTextEditor::Mark,KTextEditor::MarkInterface::MarkChangeAction)) );
        markInterface->setMark( breakpoint.lineNumber() - 1,
                                KTextEditor::MarkInterface::BreakpointActive );
        connect( document(), SIGNAL(markChanged(KTextEditor::Document*,KTextEditor::Mark,KTextEditor::MarkInterface::MarkChangeAction)),
                 this, SLOT(markChanged(KTextEditor::Document*,KTextEditor::Mark,KTextEditor::MarkInterface::MarkChangeAction)) );
    }
}

void ScriptTab::breakpointAboutToBeRemoved( const Breakpoint &breakpoint )
{
    if ( breakpoint.fileName() != fileName() ) {
        return;
    }

    KTextEditor::MarkInterface *markInterface =
            qobject_cast<KTextEditor::MarkInterface*>( document() );
    if ( !markInterface ) {
        kDebug() << "Cannot mark breakpoint, no KTextEditor::MarkInterface";
    } else {
        disconnect( document(), SIGNAL(markChanged(KTextEditor::Document*,KTextEditor::Mark,KTextEditor::MarkInterface::MarkChangeAction)),
                    this, SLOT(markChanged(KTextEditor::Document*,KTextEditor::Mark,KTextEditor::MarkInterface::MarkChangeAction)) );
        markInterface->removeMark( breakpoint.lineNumber() - 1,
                                   KTextEditor::MarkInterface::BreakpointActive );
        connect( document(), SIGNAL(markChanged(KTextEditor::Document*,KTextEditor::Mark,KTextEditor::MarkInterface::MarkChangeAction)),
                 this, SLOT(markChanged(KTextEditor::Document*,KTextEditor::Mark,KTextEditor::MarkInterface::MarkChangeAction)) );
    }
}

void ScriptTab::markChanged( KTextEditor::Document *document, const KTextEditor::Mark &mark,
                             KTextEditor::MarkInterface::MarkChangeAction action )
{
    if ( mark.type == KTextEditor::MarkInterface::BreakpointActive ) {
        Debugger::BreakpointModel *breakpointModel = project()->debugger()->breakpointModel();
        if ( action == KTextEditor::MarkInterface::MarkAdded ) {
            Debugger::Debugger *debugger = project()->debugger();
            const int lineNumber = debugger->getNextBreakableLineNumber( fileName(), mark.line + 1 );
            if ( mark.line + 1 != lineNumber ) {
                KTextEditor::MarkInterface *markInterface =
                        qobject_cast<KTextEditor::MarkInterface*>( document );
                markInterface->removeMark( mark.line, mark.type );
            }

            if ( breakpointModel->breakpointState(fileName(), lineNumber) ==
                 Debugger::Breakpoint::NoBreakpoint )
            {
                toggleBreakpoint( lineNumber );
            }
        } else { // Mark removed
            if ( breakpointModel->breakpointState(fileName(), mark.line + 1) !=
                 Debugger::Breakpoint::NoBreakpoint )
            {
                toggleBreakpoint( mark.line + 1 );
            }
        }
    }
}

void ScriptTab::currentFunctionChanged( int index )
{
    QModelIndex functionIndex = m_functionsModel->index( index, 0 );
    CodeNode::Ptr node = m_scriptModel->nodeFromIndex(
            m_functionsModel->mapToSource(functionIndex) );
    FunctionNode::Ptr function = node.dynamicCast<FunctionNode>();
    if ( function ) {
        goToLine( function->line() );
    }

    updateNextPreviousFunctionActions();
}

void ScriptTab::scriptCursorPositionChanged( KTextEditor::View *view,
                                             const KTextEditor::Cursor &cursor )
{
    Q_UNUSED( view );
    bool wasBlocked = m_functionsWidget->blockSignals( true );
    CodeNode::Ptr node = m_scriptModel->nodeFromLineNumber( cursor.line() + 1 );
    if ( node ) {
        QModelIndex index = m_scriptModel->indexFromNode( node );
        QModelIndex functionIndex = m_functionsModel->mapFromSource( index );
        m_functionsWidget->setCurrentIndex( functionIndex.row() );
        updateNextPreviousFunctionActions();
    }
    m_functionsWidget->blockSignals( wasBlocked );
}

void ScriptTab::showTextHint( const KTextEditor::Cursor &position, QString &text )
{
    KTextEditor::View *activeView = document()->activeView();
    const QPoint pointInView = activeView->cursorToCoordinate( position );
    const QPoint pointGlobal = activeView->mapToGlobal( pointInView );
    QToolTip::showText( pointGlobal, text );
}

void ScriptTab::updateNextPreviousFunctionActions()
{
    const int count = m_functionsModel->rowCount();
    const int functionIndex = m_functionsWidget->currentIndex();
    bool enablePreviousFunctionAction, enableNextFunctionAction;
    if ( functionIndex == -1 ) {
        int currentLine = document()->activeView()->cursorPosition().line() + 1;
        FunctionNode::Ptr previousNode = m_scriptModel->nodeBeforeLineNumber( currentLine, Function )
                .dynamicCast<FunctionNode>();
        FunctionNode::Ptr nextNode = m_scriptModel->nodeAfterLineNumber( currentLine, Function )
                .dynamicCast<FunctionNode>();
        enablePreviousFunctionAction = previousNode;
        enableNextFunctionAction = nextNode;
    } else {
        enablePreviousFunctionAction = count > 1 && functionIndex > 0;
        enableNextFunctionAction = count > 1 && functionIndex != count - 1;
    }

    m_previousFunctionAction->setEnabled( enablePreviousFunctionAction );
    m_nextFunctionAction->setEnabled( enableNextFunctionAction );
    canGoToPreviousFunctionChanged( enablePreviousFunctionAction );
    canGoToNextFunctionChanged( enableNextFunctionAction );
}

QIcon ScriptTab::icon() const
{
    if ( isModified() ) {
        return AbstractTab::icon();
    } else {
        return project()->scriptIcon(); // TEST KIcon( m_document->mimeType().replace('/', '-') );
    }
}

void ScriptTab::documentChanged( KTextEditor::Document *document )
{
    Q_UNUSED( document );

    if ( !m_backgroundParserTimer ) {
        m_backgroundParserTimer = new QTimer( this );
        m_backgroundParserTimer->setSingleShot( true );
        connect( m_backgroundParserTimer, SIGNAL(timeout()), this, SLOT(parseScript()) );
    }

    // Begin parsing after delay
    m_backgroundParserTimer->start( 500 );

//     setModified();
}

bool ScriptTab::save()
{
    if ( !isModified() ) {
        return true;
    }

//     if ( project()->scriptFileName().isEmpty() ) {
    if ( fileName().isEmpty() ) {
        if ( project()->filePath().isEmpty() ) {
            KMessageBox::error( this, i18nc("@info", "Save the project first") );
            return false;
        }

        const QString fileName = QFileInfo(project()->filePath()).dir().absolutePath()
                + '/' + project()->serviceProviderId() + ".js";
        if ( !document()->saveAs(fileName) ) {
            KMessageBox::error( this,
                    i18nc("@info", "Cannot save script to <filename>%1</filename>", fileName) );
            return false;
        }

        ServiceProviderData *newInfo = project()->provider()->data()->clone();
        newInfo->setScriptFile( fileName );
        project()->setProviderData( newInfo );
        project()->save( this );
//     } else if ( !document()->saveAs(project()->scriptFileName()) ) {
    } else if ( !document()->saveAs(fileName()) ) {
        KMessageBox::error( this, i18nc("@info",
                "Cannot save script to <filename>%1</filename>", fileName()) );
    }

    return true;
}

void ScriptTab::parseScript()
{
    // Delete timer, which might have called this method
    delete m_backgroundParserTimer;
    m_backgroundParserTimer = 0;

    // Parse the script
    JavaScriptParser parser( document()->text() );

    KTextEditor::MarkInterface *markInterface =
            qobject_cast<KTextEditor::MarkInterface*>( document() );
    if ( markInterface ) {
        markInterface->clearMarks(); // TODO Only remove error marks

        if ( parser.hasError() ) {
            markInterface->addMark( parser.errorLine() - 1, KTextEditor::MarkInterface::Error );
            if ( parser.errorAffectedLine() != -1 ) {
                markInterface->addMark( parser.errorAffectedLine() - 1,
                                KTextEditor::MarkInterface::Warning );
            }

            emit syntaxErrorFound( i18nc("@info:status",
                    "Syntax error in line %1, column %2: <message>%3</message>",
                    parser.errorLine(), parser.errorColumn(), parser.errorMessage()) );
//             setError( ScriptSyntaxError, i18nc("@info:status",
//                       "Syntax error in line %1, column %2: <message>%3</message>",
//                       parser.errorLine(), parser.errorColumn(), parser.errorMessage()) );
//             document()->views().first()->setCursorPosition( parser.errorCursor() );
//             project()->emitInformationMessage( i18nc("@info:status",
//                     "Syntax error in line %1, column %2: <message>%3</message>",
//                     parser.errorLine(), parser.errorColumn(), parser.errorMessage()),
//                     KMessageWidget::Error, 0 );
        }
    } else {
        project()->emitInformationMessage( i18nc("@info:status", "No syntax errors found."),
                                           KMessageWidget::Positive );
    }

    // Update the model with the parsed nodes
    bool wasBlocked = m_functionsWidget->blockSignals( true );
    m_scriptModel->setNodes( parser.nodes() );
    m_functionsWidget->blockSignals( wasBlocked );

    // Update selected function in the function combobox
    scriptCursorPositionChanged( document()->views().first(),
                                 document()->views().first()->cursorPosition() );
    // Update next/previous function actions enabled state
    updateNextPreviousFunctionActions();

//     project()->debugger()->loadScript( project()->scriptText(), project()->provider()->data() );
}

void ScriptTab::goToLine( int lineNumber )
{
    KTextEditor::View *view = document()->activeView();
    view->blockSignals( true );
    view->setCursorPosition( KTextEditor::Cursor(lineNumber - 1, 0) );
    view->blockSignals( false );
}

void ScriptTab::goToPreviousFunction()
{
    if ( m_functionsWidget->currentIndex() == -1 ) {
        FunctionNode::Ptr node = m_scriptModel->nodeBeforeLineNumber(
                document()->activeView()->cursorPosition().line() + 1, Function)
                .dynamicCast<FunctionNode>();
        if ( node ) {
            QModelIndex index = m_scriptModel->indexFromNode( node );
            m_functionsWidget->setCurrentIndex(
                    m_functionsModel->mapFromSource(index).row() );
            return;
        }
    }

    m_functionsWidget->setCurrentIndex( m_functionsWidget->currentIndex() - 1 );
}

void ScriptTab::goToNextFunction()
{
    if ( m_functionsWidget->currentIndex() == -1 ) {
        FunctionNode::Ptr node = m_scriptModel->nodeAfterLineNumber(
                document()->activeView()->cursorPosition().line() + 1, Function)
                .dynamicCast<FunctionNode>();
        if ( node ) {
            QModelIndex index = m_scriptModel->indexFromNode( node );
            m_functionsWidget->setCurrentIndex(
                    m_functionsModel->mapFromSource(index).row() );
            return;
        }
    }

    m_functionsWidget->setCurrentIndex( m_functionsWidget->currentIndex() + 1 );
}
