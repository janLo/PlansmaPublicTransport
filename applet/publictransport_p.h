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

#ifndef PUBLICTRANSPORT_PRIVATE_HEADER
#define PUBLICTRANSPORT_PRIVATE_HEADER

#define NO_EXPORT_PLASMA_APPLET

// Own includes
#include "publictransport.h"
#include "departureprocessor.h"
#include "settingsio.h"
#include "departurepainter.h"
#include "departuremodel.h"
#include "titlewidget.h"
#include "timetablewidget.h"
#include "popupicon.h"

// libpublictransporthelper includes
#include <departureinfo.h>

// Plasma includes
#include <Plasma/Svg>
#include <Plasma/Label>

// KDE includes
#include <KStandardDirs>
#include <KToolInvocation>

// Qt includes
#include <QSignalTransition>
#include <QModelIndex>
#include <QStateMachine>
#include <QHistoryState>
#include <QState>
#include <QGraphicsLayout>
#include <QGraphicsLinearLayout>
#include <QLabel>

class MarbleProcess;
class OverlayWidget;
class JourneySearchSuggestionWidget;
class JourneyTimetableWidget;
class TimetableWidget;
class TitleWidget;
class GraphicsPixmapWidget;

namespace Plasma
{
    class Animation;
}
class KSelectAction;

class QActionGroup;
class QParallelAnimationGroup;
class QGraphicsWidget;
class QAbstractTransition;

class ToPropertyTransition : public QSignalTransition
{
public:
    ToPropertyTransition( QObject *sender, const char *signal, QState *source,
                          QObject *propertyObject, const char *targetStateProperty );

    const QObject *propertyObject() const { return m_propertyObject; };
    const char *targetStateProperty() const { return m_property; };
    QState *currentTargetState() const {
        return qobject_cast<QState*>( qvariant_cast<QObject*>(m_propertyObject->property(m_property)) );
    };
    void setTargetStateProperty( const QObject *propertyObject, const char *property );

protected:
    virtual bool eventTest( QEvent *event );

private:
    const QObject *m_propertyObject;
    const char *m_property;
};

/** @brief Private class for the PublicTransportApplet class. */
class PublicTransportAppletPrivate {
public:
    PublicTransportAppletPrivate( PublicTransportApplet *q );

public: // Event handlers
    /** @brief Update GUI and logic to new @p _settings, changes are indicated using @p changed. */
    void onSettingsChanged( const Settings& _settings, SettingsIO::ChangedFlags changed );

    /** @brief Update GUI and action names to new departure arrival list type in current settings. */
    void onDepartureArrivalListTypeChanged();

    /** @brief Tell the models about new home stop name / currently selected stop settings. */
    void onCurrentStopSettingsChanged();

    /** @brief Settings that require a new data request have been changed. */
    void onServiceProviderSettingsChanged();

    /** @brief The applets geometry has changed. */
    void onResized();

    /** @brief The animation that fades out a pixmap widget with an old shapshot has finished. */
    void onOldItemAnimationFinished();

    /** @brief The state of the departure data changed between valid/invalid/waiting. */
    void onDepartureDataStateChanged();

    /** @brief The state of the journey data changed between valid/invalid/waiting. */
    void onJourneyDataStateChanged();

public: // Other functions
    /** @brief Update the text and tooltip of the label shown in the applet. */
    void updateInfoText();

    /** @brief Updates the KMenuAction used for the filters action. */
    void updateFilterMenu();

    /** @brief Updates the KMenuAction used for the journeys menu. */
    void updateJourneyMenu();

    /** @brief Update the color groups according to the currently shown departures. */
    void updateColorGroupSettings();

    /** @brief Update the main icon widget shown in the applets title. */
    void updateDepartureListIcon();

    /** @brief Applies theme properties (colors, fonts, etc.) after a change of the used theme. */
    void applyTheme();

    /** @brief Generates tooltip data and registers this applet at plasma's TooltipManager. */
    void createTooltip();

    /** @brief Updated provider @p data was received from the data engine. */
    void providerDataUpdated( const QVariantHash &data );

    /** @brief Fills the departure data model with the given @p departures.
     *
     * This will stop if the maximum departure count is reached or if all @p departures
     * have been added.
     **/
    void fillModel( const QList<DepartureInfo> &departures );

    /** @brief Fills the journey data model with the given @p journeys.
     *
     * This will stop if all @p journeys have been added.
     **/
    void fillModelJourney( const QList<JourneyInfo> &journeys );

    /** @brief Gets a list of current departures/arrivals for the selected stop(s).
     *
     * @param includeFiltered Whether or not to include filtered departures in the returned list.
     * @param max -1 to use the maximum departure count from the current settings. Otherwise this
     *   gets used as the maximal number of departures for the returned list.
     **/
    QList< DepartureInfo > mergedDepartureList( bool includeFiltered = false, int max = -1 ) const;

    /** @brief Checks if the state with the given @p stateName is currently active. */
    bool isStateActive( const QString &stateName ) const;

    /** @brief Disconnects a currently connected departure/arrival data source and
     *   connects a new source using the current configuration.
     **/
    void reconnectSource();

    /** @brief Disconnects a currently connected departure/arrival data source. */
    void disconnectSources();

    /** @brief Disconnects a currently connected journey data source. */
    void disconnectJourneySource();

    /** @brief Disconnects a currently connected journey data source and connects
     *   a new source using the current configuration.
     **/
    void reconnectJourneySource( const QString &targetStopName = QString(),
                                 const QDateTime &dateTime = QDateTime::currentDateTime(),
                                 bool stopIsTarget = true, bool timeIsDeparture = true,
                                 bool requestStopSuggestions = false );

    /** @brief Gets the text to be displayed as tooltip for the info label. */
    QString courtesyToolTip() const;

    /**
     * @brief Gets the text to be displayed on the bottom of the timetable.
     * Contains courtesy information and is HTML formatted.
     **/
    QString infoText();

    /** @brief Gets the text to be used as tooltip for the bottom label. */
    QString infoTooltip();

    /** @brief Create a new pixmap widget showing the old appearance of the applet and fades it out. */
    Plasma::Animation* fadeOutOldAppearance();

    /** @brief Create a menu action, which lists all stops in the settings to switch between them.
     *
     * @param parent The parent of the menu action and it's sub-actions.
     * @param destroyOverlayOnTrigger True, if the overlay widget should be
     *   destroyed when triggered. Defaults to false.
     *
     * @return KSelectAction* The created menu action.
     **/
    KSelectAction *createSwitchStopAction( QObject *parent,
                                           bool destroyOverlayOnTrigger = false ) const;

    /** @brief Remove any time and datetime parameters from @p sourceName and return the result. */
    QString stripDateAndTimeValues( const QString& sourceName ) const;

public: // Inline functions, mostly used only once (therefore inline) or very short and used rarely
    /** @brief Create, initialize and connect objects. */
    inline void init() {
        Q_Q( PublicTransportApplet );

        // Read settings
        settings = SettingsIO::readSettings( q->config(), q->globalConfig() );

        // Create and connect the worker thread
        departureProcessor = new DepartureProcessor( q );
        q->connect( departureProcessor, SIGNAL(beginDepartureProcessing(QString)),
                    q, SLOT(beginDepartureProcessing(QString)) );
        q->connect( departureProcessor, SIGNAL(departuresProcessed(QString,QList<DepartureInfo>,QUrl,QDateTime,QDateTime,QDateTime,int)),
                    q, SLOT(departuresProcessed(QString,QList<DepartureInfo>,QUrl,QDateTime,QDateTime,QDateTime,int)) );
        q->connect( departureProcessor, SIGNAL(beginJourneyProcessing(QString)),
                    q, SLOT(beginJourneyProcessing(QString)) );
        q->connect( departureProcessor, SIGNAL(journeysProcessed(QString,QList<JourneyInfo>,QUrl,QDateTime)),
                    q, SLOT(journeysProcessed(QString,QList<JourneyInfo>,QUrl,QDateTime)) );
        q->connect( departureProcessor, SIGNAL(departuresFiltered(QString,QList<DepartureInfo>,QList<DepartureInfo>,QList<DepartureInfo>)),
                    q, SLOT(departuresFiltered(QString,QList<DepartureInfo>,QList<DepartureInfo>,QList<DepartureInfo>)) );
        departureProcessor->setAlarms( settings.alarms() );
        departureProcessor->setFilters( settings.currentFilters() );
        departureProcessor->setColorGroups( settings.currentColorGroups() );

        // Create departure painter and load the vehicle type SVG
        departurePainter = new DeparturePainter( q );
        vehiclesSvg.setImagePath( KGlobal::dirs()->findResource( "data",
                                "plasma_applet_publictransport/vehicles.svg" ) );
        vehiclesSvg.setContainsMultipleImages( true );
        departurePainter->setSvg( &vehiclesSvg );

        // Create popup icon manager
        popupIcon = new PopupIcon( departurePainter, q );
        q->connect( popupIcon, SIGNAL(currentDepartureGroupChanged(int)),
                    q, SLOT(updateTooltip()) );
        q->connect( popupIcon, SIGNAL(currentDepartureGroupIndexChanged(qreal)),
                    q, SLOT(updatePopupIcon()) );
        q->connect( popupIcon, SIGNAL(currentDepartureIndexChanged(qreal)),
                    q, SLOT(updatePopupIcon()) );

        // Setup models and widgets
        setupModels();
        setupWidgets();

        // Setup actions and the state machine
        q->setupActions();
        setupStateMachine();

        // Create tooltip
        createTooltip();
    };

    /** @brief Create, initialize and connect the departure/journey models. */
    inline void setupModels() {
        Q_Q( PublicTransportApplet );

        StopSettings stop = settings.currentStop();
        model = new DepartureModel( q );
        model->setDepartureArrivalListType( settings.departureArrivalListType() );
        model->setHomeStop( stop.stopList().isEmpty() ? QString() : stop.stop(0).name );
        model->setCurrentStopIndex( settings.currentStopIndex() );
        model->setDepartureColumnSettings( settings.departureTimeFlags() );
        q->connect( model, SIGNAL(alarmFired(DepartureItem*,AlarmSettings)),
                    q, SLOT(alarmFired(DepartureItem*,AlarmSettings)) );
        q->connect( model, SIGNAL(updateAlarms(AlarmSettingsList,QList<int>)),
                    q, SLOT(removeAlarms(AlarmSettingsList,QList<int>)) );
        q->connect( model, SIGNAL(itemsAboutToBeRemoved(QList<ItemBase*>)),
                    q, SLOT(departuresAboutToBeRemoved(QList<ItemBase*>)) );
        q->connect( model, SIGNAL(departuresLeft(QList<DepartureInfo>)),
                    q, SLOT(departuresLeft(QList<DepartureInfo>)) );

        modelJourneys = new JourneyModel( q );
        modelJourneys->setHomeStop( stop.stopList().isEmpty()
                                    ? QString() : stop.stop(0).name );
        modelJourneys->setCurrentStopIndex( settings.currentStopIndex() );
        modelJourneys->setAlarmSettings( settings.alarms() );
        popupIcon->setModel( model );
    }

    /** @brief Create, initialize and connect the state machine and it's states. */
    inline void setupStateMachine() {
        Q_Q( PublicTransportApplet );

        // Create the state machine
        stateMachine = new QStateMachine( q );

        // Create parallel main state and sub group states
        QState *mainStateGroup = new QState( QState::ParallelStates, stateMachine );
        QState *providerStateGroup = new QState( mainStateGroup );
        QState *viewStateGroup = new QState( mainStateGroup );
        QState *departureDataStateGroup = new QState( mainStateGroup );
        QState *journeyDataStateGroup = new QState( mainStateGroup );
        states.insert( "mainStateGroup", mainStateGroup );
        states.insert( "providerStateGroup", providerStateGroup );
        states.insert( "viewStateGroup", viewStateGroup );
        states.insert( "departureDataStateGroup", departureDataStateGroup );
        states.insert( "journeyDataStateGroup", journeyDataStateGroup );

        // Create View states
        QState *actionButtonsState = new QState( viewStateGroup );
        QState *departureViewState = new QState( viewStateGroup );
        QState *intermediateDepartureViewState = new QState( viewStateGroup );
        QState *journeyStateGroup = new QState( viewStateGroup );
        QState *journeyViewState = new QState( journeyStateGroup );
        QState *journeysUnsupportedViewState = new QState( journeyStateGroup );
        QState *journeySearchState = new QState( journeyStateGroup );
        states.insert( "actionButtons", actionButtonsState );
        states.insert( "departureView", departureViewState );
        states.insert( "intermediateDepartureView", intermediateDepartureViewState );
        states.insert( "journeyStateGroup", journeyStateGroup );
        states.insert( "journeyView", journeyViewState );
        states.insert( "journeysUnsupportedView", journeysUnsupportedViewState );
        states.insert( "journeySearch", journeySearchState );

        viewStateGroup->setInitialState( departureViewState );
        QHistoryState *lastMainState = new QHistoryState( viewStateGroup );
        lastMainState->setDefaultState( departureViewState );

        // Create sub states of the departure list state for arrivals/departures
        QState *departureState = new QState( departureViewState );
        QState *arrivalState = new QState( departureViewState );
        departureViewState->setInitialState( settings.departureArrivalListType() == DepartureList
                                             ? departureState : arrivalState );
        QHistoryState *lastDepartureListState = new QHistoryState( departureViewState );
        lastDepartureListState->setDefaultState( departureState );

        // Create provider states
        QState *providerReadyState = new QState( providerStateGroup );
        QState *providerErrorState = new QState( providerStateGroup );
        providerStateGroup->setInitialState( providerReadyState ); // TODO TEST
        states.insert( "providerReady", providerReadyState );
        states.insert( "providerError", providerErrorState );

        // Create departure data states
        QState *departureDataWaitingState = new QState( departureDataStateGroup );
        QState *departureDataValidState = new QState( departureDataStateGroup );
        QState *departureDataInvalidState = new QState( departureDataStateGroup );
        departureDataStateGroup->setInitialState( departureDataValidState );
        states.insert( "departureDataWaiting", departureDataWaitingState );
        states.insert( "departureDataValid", departureDataValidState );
        states.insert( "departureDataInvalid", departureDataInvalidState );

        // Create journey data states
        QState *journeyDataWaitingState = new QState( journeyDataStateGroup );
        QState *journeyDataValidState = new QState( journeyDataStateGroup );
        QState *journeyDataInvalidState = new QState( journeyDataStateGroup );
        journeyDataStateGroup->setInitialState( journeyDataWaitingState );
        states.insert( "journeyDataWaiting", journeyDataWaitingState );
        states.insert( "journeyDataValid", journeyDataValidState );
        states.insert( "journeyDataInvalid", journeyDataInvalidState );

        // "Search Journeys..." action transitions to the journey search view (state "journeySearch").
        // If journeys aren't supported by the current service provider, a message gets displayed
        // and the target state is "journeysUnsupportedView". The target states of these transitions
        // get dynamically adjusted when the service provider settings change
        journeySearchTransition1 =
            new ToPropertyTransition( q->action("searchJourneys"), SIGNAL(triggered()),
                                      actionButtonsState, q, "supportedJourneySearchState" );
        journeySearchTransition2 =
            new ToPropertyTransition( q->action("searchJourneys"), SIGNAL(triggered()),
                                      departureViewState, q, "supportedJourneySearchState" );
        journeySearchTransition3 =
            new ToPropertyTransition( q->action("searchJourneys"), SIGNAL(triggered()),
                                      journeyViewState, q, "supportedJourneySearchState" );

        actionButtonsState->addTransition(
            q->action( "showDepartures" ), SIGNAL(triggered()), departureState );
        actionButtonsState->addTransition(
            q->action( "showArrivals" ), SIGNAL(triggered()), arrivalState );
        actionButtonsState->addTransition(
            q, SIGNAL(cancelActionButtons()), lastMainState );
        actionButtonsState->addTransition(
            q->action( "backToDepartures" ), SIGNAL(triggered()), lastDepartureListState );
        actionButtonsState->addTransition(
            q, SIGNAL(journeySearchFinished()), journeyViewState );
        departureViewState->addTransition(
            titleWidget, SIGNAL(iconClicked()), actionButtonsState );
        departureViewState->addTransition(
            q->action( "showActionButtons" ), SIGNAL(triggered()), actionButtonsState );
        journeyViewState->addTransition(
            q->action( "showActionButtons" ), SIGNAL(triggered()), actionButtonsState );
        journeySearchState->addTransition(
            q, SIGNAL(journeySearchFinished()), journeyViewState );

        // Direct transition from departure view to journey view using a favorite/recent journey action
        departureViewState->addTransition(
            q, SIGNAL(journeySearchFinished()), journeyViewState );

        // Add a transition to the intermediate departure list state.
        // Gets triggered by eg. the context menus of route stop items.
        departureViewState->addTransition(
            q, SIGNAL(intermediateDepartureListRequested(QString)),
            intermediateDepartureViewState );

        // Add transitions to departure list and to arrival list
        departureViewState->addTransition(
            q->action("showDepartures"), SIGNAL(triggered()), departureState );
        departureViewState->addTransition(
            q->action("showArrivals"), SIGNAL(triggered()), arrivalState );

        intermediateDepartureViewState->addTransition(
            titleWidget, SIGNAL(iconClicked()), lastMainState );
        intermediateDepartureViewState->addTransition(
            q->action("backToDepartures"), SIGNAL(triggered()), lastMainState );

        journeySearchState->addTransition(
            titleWidget, SIGNAL(iconClicked()), lastMainState );
        journeySearchState->addTransition(
            titleWidget, SIGNAL(closeIconClicked()), lastMainState );

        journeyViewState->addTransition(
            titleWidget, SIGNAL(iconClicked()), actionButtonsState );
        journeyViewState->addTransition(
            titleWidget, SIGNAL(closeIconClicked()), lastDepartureListState );
        journeyViewState->addTransition(
            q->action("backToDepartures"), SIGNAL(triggered()), lastDepartureListState );

        providerReadyState->addTransition( q, SIGNAL(providerNotReady()), providerErrorState );
        providerErrorState->addTransition( q, SIGNAL(providerReady()), providerReadyState );

        departureDataWaitingState->addTransition(
            q, SIGNAL(validDepartureDataReceived()), departureDataValidState );
        departureDataWaitingState->addTransition(
            q, SIGNAL(invalidDepartureDataReceived()), departureDataInvalidState );
        departureDataValidState->addTransition(
            q, SIGNAL(requestedNewDepartureData()), departureDataWaitingState );
        departureDataInvalidState->addTransition(
            q, SIGNAL(requestedNewDepartureData()), departureDataWaitingState );

        journeyDataWaitingState->addTransition(
            q, SIGNAL(validJourneyDataReceived()), journeyDataValidState );
        journeyDataWaitingState->addTransition(
            q, SIGNAL(invalidJourneyDataReceived()), journeyDataInvalidState );
        journeyDataValidState->addTransition(
            q, SIGNAL(requestedNewJourneyData()), journeyDataWaitingState );
        journeyDataInvalidState->addTransition(
            q, SIGNAL(requestedNewJourneyData()), journeyDataWaitingState );

        q->connect( actionButtonsState, SIGNAL(entered()),
                    q, SLOT(showActionButtons()) );
        q->connect( actionButtonsState, SIGNAL(exited()),
                    q, SLOT(destroyOverlay()) );

        q->connect( departureViewState, SIGNAL(entered()),
                    q, SLOT(showDepartureList()) );

        q->connect( arrivalState, SIGNAL(entered()), q, SLOT(showArrivals()) );
        q->connect( departureState, SIGNAL(entered()), q, SLOT(showDepartures()) );

        q->connect( journeySearchState, SIGNAL(entered() ),
                    q, SLOT(showJourneySearch()) );
        q->connect( journeySearchState, SIGNAL(exited()),
                    q, SLOT(exitJourneySearch()) );
        q->connect( journeysUnsupportedViewState, SIGNAL(entered()),
                    q, SLOT(showJourneysUnsupportedView()) );
        q->connect( journeyViewState, SIGNAL(entered()),
                    q, SLOT(showJourneyList()) );
        q->connect( journeyViewState, SIGNAL(exited()),
                    q, SLOT(disconnectJourneySource()) );

        q->connect( intermediateDepartureViewState, SIGNAL(entered()),
                    q, SLOT(showIntermediateDepartureList()) );
        q->connect( intermediateDepartureViewState, SIGNAL(exited()),
                    q, SLOT(removeIntermediateStopSettings()) );

        q->connect( departureViewState, SIGNAL(entered() ),
                    q, SLOT(setAssociatedApplicationUrlForDepartures()) );
        q->connect( journeyViewState, SIGNAL(entered() ),
                    q, SLOT(setAssociatedApplicationUrlForJourneys()) );

        q->connect( departureDataWaitingState, SIGNAL(entered()),
                    q, SLOT(departureDataStateChanged()) );
        q->connect( departureDataInvalidState, SIGNAL(entered()),
                    q, SLOT(departureDataStateChanged()) );
        q->connect( departureDataValidState, SIGNAL(entered()),
                    q, SLOT(departureDataStateChanged()) );

        q->connect( journeyDataWaitingState, SIGNAL(entered()),
                    q, SLOT(journeyDataStateChanged()) );
        q->connect( journeyDataInvalidState, SIGNAL(entered()),
                    q, SLOT(journeyDataStateChanged()) );
        q->connect( journeyDataValidState, SIGNAL(entered()),
                    q, SLOT(journeyDataStateChanged()) );

        stateMachine->setInitialState( mainStateGroup );
        stateMachine->start();
    };

    /** @brief Create, initialize and connect widgets used for the applet. */
    inline void setupWidgets() {
        Q_Q( PublicTransportApplet );

        Q_ASSERT_X( !graphicsWidget, "PublicTransportPrivate::setupWidgets",
                    "This function should only be called once and the main graphics widget "
                    "should not be created somewhere else." );
        graphicsWidget = new QGraphicsWidget( q );
        graphicsWidget->setMinimumSize( 150, 150 ); // TODO allow smaller sizes, if zoom factor is small
        graphicsWidget->setPreferredSize( 400, 300 );
        q->connect( graphicsWidget, SIGNAL(geometryChanged()), q, SLOT(appletResized()) );

        // Create a child graphics widget, eg. to apply a blur effect to it
        // but not to an overlay widget (which then gets a child of graphicsWidget).
        mainGraphicsWidget = new QGraphicsWidget( graphicsWidget );
        mainGraphicsWidget->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
        QGraphicsLinearLayout *mainLayout = new QGraphicsLinearLayout( Qt::Vertical );
        mainLayout->setContentsMargins( 0, 0, 0, 0 );
        mainLayout->addItem( mainGraphicsWidget );
        graphicsWidget->setLayout( mainLayout );

        // Create the title widget and connect slots
        titleWidget = new TitleWidget( ShowDepartureArrivalListTitle,
                &settings, currentServiceProviderFeatures.contains("ProvidesJourneys"),
                mainGraphicsWidget );
        q->connect( titleWidget, SIGNAL(journeySearchInputFinished(QString)),
                    q, SLOT(journeySearchInputFinished(QString)) );
        q->connect( titleWidget, SIGNAL(journeySearchListUpdated(QList<JourneySearchItem>)),
                    q, SLOT(journeySearchListUpdated(QList<JourneySearchItem>)) ); // TODO Unused, remove?

        labelInfo = new Plasma::Label( mainGraphicsWidget );
        labelInfo->setAlignment( Qt::AlignVCenter | Qt::AlignRight );
        q->connect( labelInfo, SIGNAL(linkActivated(QString)),
                    KToolInvocation::self(), SLOT(invokeBrowser(QString)) );
        QLabel *_labelInfo = labelInfo->nativeWidget();
        _labelInfo->setOpenExternalLinks( true );
        _labelInfo->setWordWrap( true );
        _labelInfo->setText( infoText() );

        // Watch for tooltip events and update the tooltip before showing it
        _labelInfo->installEventFilter( q );

        // Create timetable item for departures/arrivals
        timetable = new TimetableWidget( settings.drawShadows()
                ? PublicTransportWidget::DrawShadowsOrHalos : PublicTransportWidget::NoOption,
                mainGraphicsWidget );
        timetable->setModel( model );
        timetable->setSvg( &vehiclesSvg );
        q->connect( timetable, SIGNAL(expandedStateChanged(PublicTransportGraphicsItem*,bool)),
                    q, SLOT(expandedStateChanged(PublicTransportGraphicsItem*,bool)) );
        q->connect( timetable, SIGNAL(contextMenuRequested(PublicTransportGraphicsItem*,QPointF)),
                    q, SLOT(departureContextMenuRequested(PublicTransportGraphicsItem*,QPointF)) );
        q->connect( timetable, SIGNAL(requestStopAction(StopAction::Type,QString,QString)),
                    q, SLOT(requestStopAction(StopAction::Type,QString,QString)) );

        QGraphicsLinearLayout *layout = new QGraphicsLinearLayout( Qt::Vertical );
        layout->setContentsMargins( 0, 0, 0, 0 );
        layout->setSpacing( 0 );
        layout->addItem( titleWidget );
        layout->addItem( timetable );
        layout->addItem( labelInfo );
        layout->setAlignment( labelInfo, Qt::AlignRight | Qt::AlignVCenter );
        mainGraphicsWidget->setLayout( layout );

        q->registerAsDragHandle( mainGraphicsWidget );
        q->registerAsDragHandle( titleWidget->titleWidget() );

        // To make the link clickable (don't let plasma eat click events for dragging)
        labelInfo->installSceneEventFilter( q );

        // Apply properties of the plasma theme
        applyTheme();
    };

    /** @brief Clears the departure list received from the data engine and displayed by the applet. */
    inline void clearDepartures() {
        departureInfos.clear(); // Clear data from data engine
        model->clear(); // Clear data to be displayed
    };

    /** @brief Clears the journey list received from the data engine and displayed by the applet. */
    inline void clearJourneys() {
        journeyInfos.clear(); // Clear data from data engine
        modelJourneys->clear(); // Clear data to be displayed
    };

protected:
    QGraphicsWidget *graphicsWidget, *mainGraphicsWidget;
    GraphicsPixmapWidget *oldItem;
    TitleWidget *titleWidget; // A widget used as the title of the applet.
    Plasma::Label *labelInfo; // A label used to display additional information.

    TimetableWidget *timetable; // The graphics widget showing the departure/arrival board.
    JourneyTimetableWidget *journeyTimetable; // The graphics widget showing journeys.

    Plasma::Label *labelJourneysNotSupported; // A label used to display an info about unsupported journey search.
    JourneySearchSuggestionWidget *listStopSuggestions; // A list of stop suggestions for the current input.
    OverlayWidget *overlay;
    Plasma::Svg vehiclesSvg; // An SVG containing SVG elements for vehicle types.

    DepartureModel *model; // The model containing the departures/arrivals.
    QHash< QString, QList<DepartureInfo> > departureInfos; // List of current departures/arrivals for each stop.
    PopupIcon *popupIcon;
    QParallelAnimationGroup *titleToggleAnimation; // Hiding/Showing the title on resizing
    QHash< int, QString > stopIndexToSourceName; // A hash from the stop index to the source name.
    QStringList currentSources; // Current source names at the publictransport data engine.
    int runningUpdateRequests; // The number of currently running update requests.
    QTimer *updateTimer; // A timer used to enable the update action again

    JourneyModel *modelJourneys; // The model for journeys from or to the "home stop".
    QList<JourneyInfo> journeyInfos; // List of current journeys.
    QString currentJourneySource; // Current source name for journeys at the publictransport data engine.
    QString journeyTitleText;

    QString lastSecondStopName; // The last used second stop name for journey search.
    QDateTime lastJourneyDateTime; // The last used date and time for journey search.

    QDateTime lastSourceUpdate; // The last update of the data source inside the data engine.
    QDateTime nextAutomaticSourceUpdate; // The next automatic update of the data source.
    QDateTime minManualSourceUpdateTime; // The minimal next (manual) update time.
    QUrl urlDeparturesArrivals, urlJourneys; // Urls to set as associated application urls,
            // when switching from/to journey mode.

    Settings settings; // Current applet settings.
    int originalStopIndex; // Index of the stop before showing an intermediate list via context menu.
    QVariantHash currentProviderData; // The last received provider data, including it's current state
    QStringList currentServiceProviderFeatures;

    QPersistentModelIndex clickedItemIndex; // Index of the clicked item in departure view
            // for the context menu actions.

    QActionGroup *filtersGroup; // An action group to toggle filter configurations.
    QActionGroup *colorFiltersGroup; // An action group to toggle color groups.

    DepartureProcessor *departureProcessor; // Applies filters, alarms and creates departure data structures in threads
    DeparturePainter *departurePainter; // Paints departures

    // State machine, states and dynamic transitions
    QStateMachine *stateMachine;
    QHash< QString, QState* > states; // Stores states by name
    QAbstractTransition *journeySearchTransition1;
    QAbstractTransition *journeySearchTransition2;
    QAbstractTransition *journeySearchTransition3;

    MarbleProcess *marble;

private:
    PublicTransportApplet *q_ptr;
    Q_DECLARE_PUBLIC( PublicTransportApplet )
};

class StopDataConnection : public QObject {
    Q_OBJECT
public:
    StopDataConnection( Plasma::DataEngine *engine, const QString &providerId,
                        const QString &stopName, QObject *parent = 0 );

signals:
    void stopDataReceived( const QVariantHash &stopData );
    void stopDataReceived( const QString &stopName, bool coordinatesAreValid, qreal lon, qreal lat );

public slots:
    /** @brief New @p data arrived from the data engine for source @p sourceName. */
    void dataUpdated( const QString &sourceName, const Plasma::DataEngine::Data &data );
};

#endif // Multiple inclusion guard
