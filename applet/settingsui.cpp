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
#include "settingsui.h"

// Own includes
#include "settingsio.h" // For export/import of filter settings

// libpublictransporthelper includes
#include <filterwidget.h>
#include <stopwidget.h>
#include <serviceprovidermodel.h>
#include <locationmodel.h>

// Qt includes
#include <QStandardItemModel>

// KDE+Plasma includes
#include <KTabWidget>
#include <KConfigDialog>
#include <KColorScheme>
#include <KFileDialog>
#include <KInputDialog>
#include <KMessageBox>
#include <Plasma/Theme>

SettingsUiManager::SettingsUiManager( const Settings &settings,
                                      Plasma::DataEngine *publicTransportEngine, Plasma::DataEngine *osmEngine,
                                      Plasma::DataEngine *favIconEngine, Plasma::DataEngine *geolocationEngine,
                                      KConfigDialog *parentDialog, DeletionPolicy deletionPolicy )
    : QObject( parentDialog ), m_deletionPolicy( deletionPolicy ),
      m_configDialog( parentDialog ), m_modelServiceProvider( 0 ),
      m_modelLocations( 0 ), m_stopListWidget( 0 ),
      m_publicTransportEngine( publicTransportEngine ), m_osmEngine( osmEngine ),
      m_favIconEngine( favIconEngine ), m_geolocationEngine( geolocationEngine )
{
    // Store settings that have no associated widgets
    m_currentStopSettingsIndex = settings.currentStopIndex();
    m_hideTargetColumn = settings.hideTargetColumn();

    m_filters = settings.filters();
    m_filterConfigChanged = false;
    m_colorGroup = settings.colorGroups();

    m_alarm = settings.alarms();
    m_lastAlarm = -1;
    m_alarmsChanged = false;

    // Setup tab page widgets
    QWidget *widgetStop = new QWidget;
    QWidget *widgetAdvanced = new QWidget;
    QWidget *widgetAppearance = new QWidget;
    QWidget *widgetFilter = new QWidget;
    QWidget *widgetAlarms = new QWidget;
    m_ui.setupUi( widgetStop );
    m_uiAdvanced.setupUi( widgetAdvanced );
    m_uiAppearance.setupUi( widgetAppearance );
    m_uiFilter.setupUi( widgetFilter );
    m_uiAlarms.setupUi( widgetAlarms );

    // Setup tab widget of the stop settings page
    KTabWidget *tabMain = new KTabWidget;
    tabMain->setObjectName( "generalTabWidget" );
    tabMain->addTab( widgetStop, i18nc( "@title:tab", "&Stop selection" ) );
    tabMain->addTab( widgetAdvanced, i18nc( "@title:tab Advanced settings tab label", "&Advanced" ) );

    // Add settings pages
    m_configDialog->addPage( tabMain, i18nc( "@title:group General settings page name", "General" ),
                             "public-transport-stop" );
    m_configDialog->addPage( widgetAppearance, i18nc( "@title:group", "Appearance" ),
                             "video-display" );
    m_configDialog->addPage( widgetFilter, i18nc( "@title:group", "Filter" ), "view-filter" );
    m_configDialog->addPage( widgetAlarms, i18nc( "@title:group", "Alarms" ), "task-reminder" );

    // Setup model for the service provider combobox
    m_modelServiceProvider = new ServiceProviderModel( this );

    // Setup model for the location combobox
    m_modelLocations = new LocationModel( this );

    // Setup stop widgets
    m_stopListWidget = new StopListWidget( m_ui.stopList, m_modelServiceProvider, settings.stops(),
                                           StopSettingsDialog::ExtendedStopSelection,
                                           ServiceProviderDataDialog::DefaultOptions,
                                           &m_filters );
    m_stopListWidget->setWhatsThis( i18nc( "@info:whatsthis",
            "<subtitle>This shows the stop settings you have set.</subtitle>"
            "<para>The applet shows results for one of them at a time. To switch the "
            "currently used stop setting use the context menu of the applet.</para>"
            "<para>For each stop setting another set of filter configurations can be used. "
            "To edit filter configurations use the <interface>Filter</interface> "
            "section in the settings dialog. You can define a list of stops for "
            "each stop setting that are then displayed combined (eg. stops near "
            "to each other).</para>" ) );
    m_stopListWidget->setCurrentStopSettingIndex( m_currentStopSettingsIndex );
    connect( m_stopListWidget, SIGNAL(changed(int,StopSettings)), this, SLOT(stopSettingsChanged()) );
    connect( m_stopListWidget, SIGNAL(added(QWidget*)), this, SLOT(stopSettingsAdded()) );
    connect( m_stopListWidget, SIGNAL(removed(QWidget*,int)),
             this, SLOT(stopSettingsRemoved(QWidget*,int)) );
    stopSettingsChanged();

    // Add stop list widget
    QVBoxLayout *lStop = new QVBoxLayout( m_ui.stopList );
    lStop->setContentsMargins( 0, 0, 0, 0 );
    lStop->addWidget( m_stopListWidget );

    // Setup filter widgets
    m_uiFilter.filters->setWhatsThis( i18nc( "@info:whatsthis",
            "<subtitle>This shows the filters of the selected filter configuration.</subtitle>"
            "<para>Each filter configuration consists of a name, a list of stops using the filter "
            "configuration, a filter action and a list of filters. Each filter contains a list of "
            "constraints.</para>"
            "<para>A filter matches, if all it's constraints match (logical AND) while a filter "
            "configuration matches, if one of it's filters match (logical OR).</para>"
            "<para>For each filter configuration a list of stops can be set, that use that filter. "
            "Check each stop you want to use the selected filter configuration in the "
            "<interface>Used With</interface> combobox. You can also select the filters to be used "
            "by a specific stop in the stop settings or in the applet itself.</para>"
            "<subtitle>Filter Types</subtitle>"
            "<para><list>"
            "<item><emphasis>Vehicle:</emphasis> Filters by vehicle types.</item>"
            "<item><emphasis>Line String:</emphasis> Filters by transport line strings.</item>"
            "<item><emphasis>Line number:</emphasis> Filters by transport line numbers.</item>"
            "<item><emphasis>Target:</emphasis> Filters by target/origin.</item>"
            "<item><emphasis>Delay:</emphasis> Filters by delay.</item>"
            "</list></para>"
            "<subtitle>Filters that need route data</subtitle>"
            "<para><emphasis>Warning:</emphasis> The following filters need route data, which may "
            "need to be requested explicitly as additional timetable data for your provider. See "
            "the <interface>Advanced</interface> tab in the general settings to adjust, when "
            "additional timetable data should be requested."
            "<list>"
            "<item><emphasis>Via:</emphasis> Filters by intermediate stops.</item>"
            "<item><emphasis>Next Stop:</emphasis> Filters by the next intermediate stop.</item>"
            "</list></para>" ) );
    m_uiFilter.affectedStops->setMultipleSelectionOptions( CheckCombobox::ShowStringList );
    connect( m_uiFilter.filters, SIGNAL( changed() ), this, SLOT( filtersChanged() ) );
    connect( m_uiFilter.affectedStops, SIGNAL( checkedItemsChanged() ),
             this, SLOT( affectedStopsFilterChanged() ) );

    // Setup alarm widgets
    m_uiAlarms.alarmFilter->setWidgetCountRange();
    m_uiAlarms.alarmFilter->removeAllWidgets();
    m_uiAlarms.alarmFilter->setAllowedFilterTypes( QList<FilterType>()
            << FilterByDepartureTime << FilterByDepartureDate << FilterByDayOfWeek << FilterByVehicleType
            << FilterByTarget << FilterByVia << FilterByNextStop << FilterByTransportLine
            << FilterByTransportLineNumber << FilterByDelay );
    m_uiAlarms.alarmFilter->setWidgetCountRange( 1 );
    m_uiAlarms.affectedStops->setMultipleSelectionOptions( CheckCombobox::ShowStringList );
    m_uiAlarms.addAlarm->setIcon( KIcon( "list-add" ) );
    m_uiAlarms.removeAlarm->setIcon( KIcon( "list-remove" ) );
    connect( m_uiAlarms.alarms, SIGNAL( currentIndexChanged( int ) ),
             this, SLOT( currentAlarmChanged( int ) ) );
    connect( m_uiAlarms.addAlarm, SIGNAL( clicked() ), this, SLOT( addAlarmClicked() ) );
    connect( m_uiAlarms.removeAlarm, SIGNAL( clicked() ), this, SLOT( removeAlarmClicked() ) );
    connect( m_uiAlarms.renameAlarm, SIGNAL( clicked() ), this, SLOT( renameAlarmClicked() ) );
    connect( m_uiAlarms.alarmFilter, SIGNAL( changed() ), this, SLOT( alarmChanged() ) );
    connect( m_uiAlarms.alarmType, SIGNAL( currentIndexChanged( int ) ),
             this, SLOT( currentAlarmTypeChanged( int ) ) );
    connect( m_uiAlarms.affectedStops, SIGNAL( checkedItemsChanged() ),
             this, SLOT( affectedStopsAlarmChanged() ) );

    // Set values of the given settings for each page
    setValuesOfAdvancedConfig( settings );
    setValuesOfAppearanceConfig( settings );
    setValuesOfAlarmConfig();
    setValuesOfFilterConfig();
    currentAlarmChanged( m_uiAlarms.alarms->currentIndex() );

    // Connect all widgets with the changed() slot, that enables the apply button
    connect( m_stopListWidget, SIGNAL(changed(int,StopSettings)), this, SLOT(changed()) );
    connect( m_stopListWidget, SIGNAL(added(QWidget*)), this, SLOT(changed()) );
    connect( m_stopListWidget, SIGNAL(removed(QWidget*,int)), this, SLOT(changed()) );
    connect( m_uiAdvanced.maximalNumberOfDepartures, SIGNAL(valueChanged(int)), this, SLOT(changed()) );
    connect( m_uiAdvanced.showArrivals, SIGNAL(toggled(bool)), this, SLOT(changed()) );
    connect( m_uiAdvanced.showDepartures, SIGNAL(toggled(bool)), this, SLOT(changed()) );
    connect( m_uiAdvanced.additionalData, SIGNAL(currentIndexChanged(int)), this, SLOT(changed()) );
    connect( m_uiAlarms.affectedStops, SIGNAL(checkedItemsChanged()), this, SLOT(changed()) );
    connect( m_uiAlarms.alarmFilter, SIGNAL(changed()), this, SLOT(changed()) );
    connect( m_uiAlarms.alarmType, SIGNAL(currentIndexChanged(int)), this, SLOT(changed()) );
    connect( m_uiFilter.affectedStops, SIGNAL(checkedItemsChanged()), this, SLOT(changed()) );
    connect( m_uiFilter.filterAction, SIGNAL(currentIndexChanged(int)), this, SLOT(changed()) );
    connect( m_uiFilter.filters, SIGNAL(changed()), this, SLOT(changed()) );
    connect( m_uiAppearance.shadow, SIGNAL(stateChanged(int)), this, SLOT(changed()) );
    connect( m_uiAppearance.cmbDepartureColumnInfos, SIGNAL(currentIndexChanged(int)), this, SLOT(changed()) );
    connect( m_uiAppearance.colorize, SIGNAL(stateChanged(int)), this, SLOT(changed()) );
    connect( m_uiAppearance.displayTimeBold, SIGNAL(stateChanged(int)), this, SLOT(changed()) );
    connect( m_uiAppearance.font, SIGNAL(currentFontChanged(QFont)), this, SLOT(changed()) );
    connect( m_uiAppearance.linesPerRow, SIGNAL(valueChanged(int)), this, SLOT(changed()) );
    connect( m_uiAppearance.size, SIGNAL(valueChanged(int)), this, SLOT(changed()) );
    connect( m_uiAppearance.radioUseDefaultFont, SIGNAL(toggled(bool)), this, SLOT(changed()) );
    connect( m_uiAppearance.radioUseOtherFont, SIGNAL(toggled(bool)), this, SLOT(changed()) );

    m_uiAlarms.addAlarm->setIcon( KIcon("list-add") );
    m_uiAlarms.removeAlarm->setIcon( KIcon("list-remove") );
    m_uiAlarms.renameAlarm->setIcon( KIcon("edit-rename") );

    m_uiFilter.addFilterConfiguration->setIcon( KIcon("list-add") );
    m_uiFilter.removeFilterConfiguration->setIcon( KIcon("list-remove") );
    m_uiFilter.renameFilterConfiguration->setIcon( KIcon("edit-rename") );

    connect( m_configDialog, SIGNAL(finished()), this, SLOT(configFinished()) );
    connect( m_configDialog, SIGNAL(okClicked()), this, SLOT(configAccepted()) );
    connect( m_configDialog, SIGNAL(applyClicked()), this, SLOT(configAccepted()) );

    connect( m_uiFilter.filterAction, SIGNAL(currentIndexChanged(int)),
             this, SLOT( filterActionChanged(int) ) );

    connect( m_uiFilter.filterConfigurations, SIGNAL(currentIndexChanged(QString)),
             this, SLOT(loadFilterConfiguration(QString)) );
    connect( m_uiFilter.addFilterConfiguration, SIGNAL(clicked()),
             this, SLOT(addFilterConfiguration() ) );
    connect( m_uiFilter.removeFilterConfiguration, SIGNAL(clicked()),
             this, SLOT(removeFilterConfiguration() ) );
    connect( m_uiFilter.renameFilterConfiguration, SIGNAL(clicked()),
             this, SLOT(renameFilterConfiguration() ) );
}

void SettingsUiManager::changed()
{
    m_configDialog->enableButtonApply( true );
}

void SettingsUiManager::configFinished()
{
    emit settingsFinished();
    if( m_deletionPolicy == DeleteWhenFinished )
        deleteLater();
}

void SettingsUiManager::configAccepted()
{
    emit settingsAccepted( settings() );
}

void SettingsUiManager::removeAlarms( const AlarmSettingsList & /*newAlarmSettings*/,
                                      const QList<int> &/*removedAlarms*/ )
{
//     TODO
//     foreach ( int alarmIndex, removedAlarms ) {
//     m_alarm.removeAt( alarmIndex );
//     disconnect( m_uiAlarms.alarms, SIGNAL(currentRowChanged(int)),
//             this, SLOT(currentAlarmChanged(int)) );
//     delete m_uiAlarms.alarms->takeItem( m_uiAlarms.alarms->currentRow() );
//     connect( m_uiAlarms.alarms, SIGNAL(currentRowChanged(int)),
//         this, SLOT(currentAlarmChanged(int)) );
//     m_lastAlarm = m_uiAlarms.alarms->currentRow();
//     setValuesOfAlarmConfig();
//     }

//     alarmChanged();
}

void SettingsUiManager::alarmChanged( int index )
{
    Q_UNUSED( index );
    m_alarmsChanged = true; // Leave values of autoGenerated and lastFired
}

void SettingsUiManager::currentAlarmChanged( int row )
{
    if( row != -1 ) {
        if( m_alarmsChanged && m_lastAlarm != -1 ) {
            // Store to last edited alarm settings
            if( m_lastAlarm < m_alarm.count() ) {
                m_alarm[ m_lastAlarm ] = currentAlarmSettings(
                                                     m_uiAlarms.alarms->model()->data(
                                                             m_uiAlarms.alarms->model()->index( m_lastAlarm, 0 ) ).toString() );
            } else {
                kDebug() << "m_lastAlarm is bad" << m_lastAlarm;
            }
        }

        disconnect( m_uiAlarms.alarmType, SIGNAL( currentIndexChanged( int ) ),
                    this, SLOT( currentAlarmTypeChanged( int ) ) );
        disconnect( m_uiAlarms.affectedStops, SIGNAL( checkedItemsChanged() ),
                    this, SLOT( affectedStopsAlarmChanged() ) );
        setValuesOfAlarmConfig();
        connect( m_uiAlarms.alarmType, SIGNAL( currentIndexChanged( int ) ),
                 this, SLOT( currentAlarmTypeChanged( int ) ) );
        connect( m_uiAlarms.affectedStops, SIGNAL( checkedItemsChanged() ),
                 this, SLOT( affectedStopsAlarmChanged() ) );

        setAlarmTextColor( m_uiAlarms.alarms->currentIndex(),
                           m_uiAlarms.affectedStops->hasCheckedItems() );
        m_alarmsChanged = false;
    } else {
        setValuesOfAlarmConfig();
    }

    m_lastAlarm = row;
}

void SettingsUiManager::addAlarmClicked()
{
    // Get an unused name for the new alarm
    QString name = i18nc( "@info/plain Default name of a new alarm", "New Alarm" );
    int i = 2;
    while( m_alarm.hasName( name ) ) {
        name = i18nc( "@info/plain Default name of a new alarm, if other default names are "
                      "already used", "New Alarm %1", i );
        ++i;
    }

    bool ok;
    do {
        name = KInputDialog::getText( i18nc( "@title:window", "Choose a Name" ),
                                      i18nc( "@label:textbox", "Name of the new Alarm:" ), name,
                                      &ok, m_configDialog, new QRegExpValidator( QRegExp( "[^\\*&]*" ), this ) );
        if( !ok || name.isNull() ) {
            return; // Canceled
        }
        if( m_alarm.hasName( name ) ) {
            KMessageBox::information( m_configDialog, i18nc( "@info/plain",
                                      "There is already an alarm with the name <resource>%1</resource>. "
                                      "Please choose another one.", name ) );
        } else {
            // Got a valid name, done with asking for a name
            break;
        }
    } while( true );

    // Append new alarm settings
    AlarmSettings alarm = AlarmSettings( name );
    m_alarm << alarm;

    disconnect( m_uiAlarms.alarms, SIGNAL( currentIndexChanged( int ) ),
                this, SLOT( currentAlarmChanged( int ) ) );
    QAbstractItemModel *model = m_uiAlarms.alarms->model();
    int row = model->rowCount();
    model->insertRow( row );
    QModelIndex index = model->index( row, 0 );
    model->setData( index, name, Qt::DisplayRole );
    setAlarmTextColor( row, !alarm.affectedStops.isEmpty() );
    connect( m_uiAlarms.alarms, SIGNAL( currentIndexChanged( int ) ),
             this, SLOT( currentAlarmChanged( int ) ) );

    m_uiAlarms.alarms->setCurrentIndex( row );

    setValuesOfAlarmConfig();
    changed();
}

void SettingsUiManager::removeAlarmClicked()
{
    if( m_uiAlarms.alarms->currentIndex() == -1 ) {
        return;
    }

    m_alarm.removeAt( m_uiAlarms.alarms->currentIndex() );
    disconnect( m_uiAlarms.alarms, SIGNAL( currentIndexChanged( int ) ),
                this, SLOT( currentAlarmChanged( int ) ) );
    m_uiAlarms.alarms->removeItem( m_uiAlarms.alarms->currentIndex() );
    connect( m_uiAlarms.alarms, SIGNAL( currentIndexChanged( int ) ),
             this, SLOT( currentAlarmChanged( int ) ) );
    m_lastAlarm = m_uiAlarms.alarms->currentIndex();
    currentAlarmChanged( m_lastAlarm );

    alarmChanged();
    changed();
}

void SettingsUiManager::renameAlarmClicked()
{
    if( m_uiAlarms.alarms->currentIndex() == -1 ) {
        return;
    }

    int currentIndex = m_uiAlarms.alarms->currentIndex();
    AlarmSettings currentAlarm = m_alarm[ currentIndex ];
    bool ok;
    QString newAlarmName = KInputDialog::getText( i18nc( "@title:window", "Choose a Name" ),
                           i18nc( "@label:textbox", "New Name of the Alarm:" ), currentAlarm.name,
                           &ok, m_configDialog, new QRegExpValidator( QRegExp( "[^\\*&]*" ), this ) );
    if( !ok || newAlarmName.isNull() ) {
        return; // Canceled
    }

    // Get key name of the current filter configuration
    if( newAlarmName == currentAlarm.name ) {
        return; // Not changed, but the old name was accepted
    }

    // Check if the new name is valid.
    // '*' or '&' is also not allowed in the name but that's already validated by a QRegExpValidator.
    if( newAlarmName.isEmpty() ) {
        KMessageBox::information( m_configDialog, i18nc( "@info", "Empty names are not allowed." ) );
        return;
    }

    // Check if the new name is already used and ask if it should be overwritten
    if( m_alarm.hasName( newAlarmName )
            && KMessageBox::warningYesNo( m_configDialog,
                                          i18nc( "@info", "<warning>There is already an alarm configuration with the name "
                                                  "<resource>%1</resource>.</warning><nl/>Do you want to overwrite it?",
                                                  newAlarmName ) )
            != KMessageBox::Yes ) {
        return; // No (don't overwrite) pressed
    }

    // Remove alarm settings with old name
    m_alarm.removeByName( currentAlarm.name );

    // Change the name to the new one and reinsert
    currentAlarm.name = newAlarmName;
    m_alarm.insert( currentIndex, currentAlarm );

    // Update name in the combobox
    m_uiAlarms.alarms->model()->setData( m_uiAlarms.alarms->model()->index( currentIndex, 0 ),
                                         newAlarmName, Qt::DisplayRole );
    changed();
}

void SettingsUiManager::alarmChanged()
{
    int row = m_uiAlarms.alarms->currentIndex();
    if( row != -1 ) {
        // Reenable this alarm for all departures if changed
        m_alarm[ row ].lastFired = QDateTime();

        // Changed alarms are no longer consired auto generated.
        // Only auto generated alarms can be removed using the applet's context menu
        m_alarm[ row ].autoGenerated = false;
    }
    m_alarmsChanged = true;

    m_uiAlarms.removeAlarm->setDisabled( m_alarm.isEmpty() );
    m_uiAlarms.renameAlarm->setDisabled( m_alarm.isEmpty() );
}

void SettingsUiManager::currentAlarmTypeChanged( int index )
{
    Q_UNUSED( index );
    // Make font bold if a recurring alarm is selected TODO
//     QListWidgetItem *item = m_uiAlarms.alarms->currentItem();
//     QFont font = item->font();
//     font.setBold( static_cast<AlarmType>( index ) != AlarmRemoveAfterFirstMatch );
//     item->setFont( font );

    alarmChanged();
}

void SettingsUiManager::affectedStopsFilterChanged()
{
    kDebug() << "Affected stops changed!";
    setFilterConfigurationChanged();
    m_filters.set( currentFilterSettings() );
    setFilterConfigurationChanged( false );
}

void SettingsUiManager::affectedStopsAlarmChanged()
{
    setAlarmTextColor( m_uiAlarms.alarms->currentIndex(),
                       m_uiAlarms.affectedStops->hasCheckedItems() );

    alarmChanged();
}

void SettingsUiManager::setAlarmTextColor( int index, bool hasAffectedStops ) const
{
    // Use negative text color if no affected stop is selected
    QColor color = !hasAffectedStops
                   ? KColorScheme( QPalette::Active ).foreground( KColorScheme::NegativeText ).color()
                   : KColorScheme( QPalette::Active ).foreground( KColorScheme::NormalText ).color();
//             TODO TEST
    m_uiAlarms.alarms->model()->setData( m_uiAlarms.alarms->model()->index( index, 0 ),
                                         QVariant::fromValue( color ), Qt::TextColorRole );
    QPalette p = m_uiAlarms.affectedStops->palette();
    KColorScheme::adjustForeground( p,
            hasAffectedStops ? KColorScheme::NormalText : KColorScheme::NegativeText,
            QPalette::ButtonText, KColorScheme::Button );
    m_uiAlarms.affectedStops->setPalette( p );
}

void SettingsUiManager::stopSettingsAdded()
{
    StopSettings stopSettings = m_stopListWidget->stopSettingsList().last();
    QString text = stopSettings.stops().join( ", " );

    // Add " in CITY" if a city value is given
    if( !stopSettings.get<QString>( CitySetting ).isEmpty() ) {
        text += " in " + stopSettings.get<QString>( CitySetting );
    }
    m_uiFilter.affectedStops->addItem( text );
    m_uiAlarms.affectedStops->addItem( text );

    // Adjust color group settings list
    m_colorGroup << ColorGroupSettingsList();

    updateStopNamesInWidgets();
}

void SettingsUiManager::stopSettingsRemoved( QWidget *, int widgetIndex )
{
    // Store current alarm settings if they are changed
    if( m_alarmsChanged && m_uiAlarms.alarms->currentIndex() != -1 ) {
        m_alarm[m_uiAlarms.alarms->currentIndex()] = currentAlarmSettings();
    }

    // Adjust stop indices in alarm settings
    for( int i = m_alarm.count() - 1; i >= 0; --i ) {
        AlarmSettings &alarm = m_alarm[ i ];
        for( int n = alarm.affectedStops.count() - 1; n >= 0; --n ) {
            if( alarm.affectedStops[n] == widgetIndex ) {
                alarm.affectedStops.removeAt( n );
            } else if( alarm.affectedStops[n] > widgetIndex ) {
                --alarm.affectedStops[n];
            }
        }
    }

    // Adjust stop indices in filter settings
    for( int i = m_filters.count() - 1; i >= 0; --i ) {
        FilterSettings &filters = m_filters[ i ];
        QSet<int> changedIndices;
        for( QSet<int>::iterator it = filters.affectedStops.begin();
                it != filters.affectedStops.end(); ++it ) {
            int stopIndex = *it;
            if( stopIndex == widgetIndex ) {
                it = filters.affectedStops.erase( it );
            } else if( stopIndex > widgetIndex ) {
                changedIndices << --stopIndex;
                it = filters.affectedStops.erase( it );
            }

            if( it == filters.affectedStops.end() ) {
                break; // TODO Make a do or while loop here?
            }
        }
        filters.affectedStops.unite( changedIndices );
    }

    // Adjust color group settings list
    m_colorGroup.removeAt( widgetIndex );

    updateStopNamesInWidgets();
}

void SettingsUiManager::stopSettingsChanged()
{
    updateStopNamesInWidgets();
}

void SettingsUiManager::updateStopNamesInWidgets()
{
    StopSettingsList stopSettingsList = m_stopListWidget->stopSettingsList();

    // Get a string for each stop setting
    QStringList stopLabels;
    foreach( const StopSettings &stopSettings, stopSettingsList ) {
        QString text = stopSettings.stops().join( ", " );
        if( !stopSettings.get<QString>( CitySetting ).isEmpty() ) {
            text += " in " + stopSettings.get<QString>( CitySetting );
        }
        stopLabels << text;
    }

    // Update stop list in the filter settings page
    disconnect( m_uiFilter.affectedStops, SIGNAL(checkedItemsChanged()),
                this, SLOT(affectedStopsFilterChanged()) );
    m_uiFilter.affectedStops->clear();
    m_uiFilter.affectedStops->addItems( stopLabels );

    // Get index of filter settings
    int index = -1;
    QString filterConfiguration = m_uiFilter.filterConfigurations->currentText();
    for( int i = 0; i < m_filters.count(); ++i ) {
        if( m_filters[i].name == filterConfiguration ) {
            index = i;
            kDebug() << "Filter configuration found at" << index << filterConfiguration;
            break;
        }
    }
    if( index != -1 ) {
        kDebug() << "Update affected stops in GUI of" << index << m_filters[index].name
                 << m_filters[index].affectedStops;
        kDebug() << "From (old GUI settings)" << m_uiFilter.affectedStops->checkedRows();

        m_uiFilter.affectedStops->setCheckedRows( m_filters[index].affectedStops.toList() );
    }
    connect( m_uiFilter.affectedStops, SIGNAL(checkedItemsChanged()),
             this, SLOT(affectedStopsFilterChanged()) );

    // Update stop list in the alarm settings page
    disconnect( m_uiAlarms.affectedStops, SIGNAL(checkedItemsChanged()),
                this, SLOT(affectedStopsAlarmChanged()) );
    m_uiAlarms.affectedStops->clear();
    m_uiAlarms.affectedStops->addItems( stopLabels );
    if( m_uiAlarms.alarms->currentIndex() != -1 ) {
        m_uiAlarms.affectedStops->setCheckedRows(
            m_alarm[m_uiAlarms.alarms->currentIndex()].affectedStops );
    }
    connect( m_uiAlarms.affectedStops, SIGNAL(checkedItemsChanged()),
             this, SLOT(affectedStopsAlarmChanged()) );
}

void SettingsUiManager::usedFilterConfigChanged( QWidget *widget )
{
    disconnect( m_stopListWidget, SIGNAL(changed(int,StopSettings)),
                this, SLOT(stopSettingsChanged()) );
    disconnect( m_stopListWidget, SIGNAL(added(QWidget*)), this, SLOT(stopSettingsAdded()) );
    disconnect( m_stopListWidget, SIGNAL(removed(QWidget*,int)),
                this, SLOT(stopSettingsRemoved(QWidget*,int )) );

    int index = widget->objectName().mid( 14 ).toInt();
    StopSettingsList stopSettingsList = m_stopListWidget->stopSettingsList();
    if( stopSettingsList.count() > index ) {
        stopSettingsList[ index ].set( FilterConfigurationSetting,
                                       qobject_cast<KComboBox *>( widget )->currentText() );
        m_stopListWidget->setStopSettingsList( stopSettingsList );
    }

    connect( m_stopListWidget, SIGNAL(changed(int,StopSettings)),
             this, SLOT(stopSettingsChanged()) );
    connect( m_stopListWidget, SIGNAL(added(QWidget*)), this, SLOT(stopSettingsAdded()) );
    connect( m_stopListWidget, SIGNAL(removed(QWidget*,int)),
             this, SLOT(stopSettingsRemoved(QWidget*,int)) );
}

void SettingsUiManager::setValuesOfAdvancedConfig( const Settings &settings )
{
    m_uiAdvanced.showDepartures->setChecked( settings.departureArrivalListType() == DepartureList );
    m_uiAdvanced.showArrivals->setChecked( settings.departureArrivalListType() == ArrivalList );
    m_uiAdvanced.maximalNumberOfDepartures->setValue( settings.maximalNumberOfDepartures() );
    switch ( settings.additionalDataRequestType() ) {
    case Settings::NeverRequestAdditionalData:
        m_uiAppearance.cmbDepartureColumnInfos->setCurrentIndex( 2 );
    case Settings::RequestAdditionalDataDirectly:
        m_uiAppearance.cmbDepartureColumnInfos->setCurrentIndex( 0 );
    case Settings::RequestAdditionalDataWhenNeeded:
    default:
        m_uiAppearance.cmbDepartureColumnInfos->setCurrentIndex( 1 );
    }
}

void SettingsUiManager::setValuesOfAppearanceConfig( const Settings &settings )
{
    m_uiAppearance.linesPerRow->setValue( settings.linesPerRow() );
    m_uiAppearance.size->setValue( Settings::sizeFromSizeFactor( settings.sizeFactor() ) );
    if ( settings.departureTimeFlags().testFlag(Settings::ShowTimeAndRemainingTime) ) {
        m_uiAppearance.cmbDepartureColumnInfos->setCurrentIndex( 0 );
    } else if ( settings.departureTimeFlags().testFlag(Settings::ShowRemainingTime) ) {
        m_uiAppearance.cmbDepartureColumnInfos->setCurrentIndex( 2 );
    } else {
        m_uiAppearance.cmbDepartureColumnInfos->setCurrentIndex( 1 );
    }
    m_uiAppearance.displayTimeBold->setChecked( settings.displayDepartureTimeBold() );

    m_uiAppearance.shadow->setChecked( settings.drawShadows() );
    m_uiAppearance.radioUseDefaultFont->setChecked( settings.useThemeFont() );
    m_uiAppearance.radioUseOtherFont->setChecked( !settings.useThemeFont() );
    m_uiAppearance.font->setCurrentFont( settings.font() );
    m_uiAppearance.colorize->setChecked( settings.colorize() );
}

void SettingsUiManager::setValuesOfAlarmConfig()
{
    kDebug() << "Set Alarm Values, in list:" << m_uiAlarms.alarms->count()
             << "in variable:" << m_alarm.count();

    disconnect( m_uiAlarms.alarms, SIGNAL( currentIndexChanged( int ) ),
                this, SLOT( currentAlarmChanged( int ) ) );
    int row = m_uiAlarms.alarms->currentIndex();
    m_uiAlarms.alarms->clear();

    QAbstractItemModel *model = m_uiAlarms.alarms->model();
    for( int i = 0; i < m_alarm.count(); ++i ) {
        const AlarmSettings &alarm = m_alarm[i];

        model->insertRow( i );
        QModelIndex index = model->index( i, 0 );
        model->setData( index, alarm.name, Qt::DisplayRole );
        setAlarmTextColor( i, !alarm.affectedStops.isEmpty() );

        QFont font = m_uiAlarms.alarms->font();
        // TODO: Update this on alarm type change
        font.setBold( alarm.type != AlarmRemoveAfterFirstMatch );
        model->setData( index, font, Qt::FontRole );
    }
    if( row < m_alarm.count() && row != -1 ) {
        m_uiAlarms.alarms->setCurrentIndex( row );
    } else if( !m_alarm.isEmpty() ) {
        m_uiAlarms.alarms->setCurrentIndex( row = 0 );
    }

    // Load currently selected alarm, if any
    if( row < m_alarm.count() && row != -1 ) {
        const AlarmSettings alarm = m_alarm.at( row );
        disconnect( m_uiAlarms.alarmType, SIGNAL( currentIndexChanged( int ) ),
                    this, SLOT( currentAlarmTypeChanged( int ) ) );
        m_uiAlarms.alarmType->setCurrentIndex( static_cast<int>( alarm.type ) );
        connect( m_uiAlarms.alarmType, SIGNAL( currentIndexChanged( int ) ),
                 this, SLOT( currentAlarmTypeChanged( int ) ) );

        disconnect( m_uiAlarms.affectedStops, SIGNAL( checkedItemsChanged() ),
                    this, SLOT( affectedStopsAlarmChanged() ) );
        m_uiAlarms.affectedStops->setCheckedRows( alarm.affectedStops );
        connect( m_uiAlarms.affectedStops, SIGNAL( checkedItemsChanged() ),
                 this, SLOT( affectedStopsAlarmChanged() ) );

        disconnect( m_uiAlarms.alarmFilter, SIGNAL( changed() ), this, SLOT( alarmChanged() ) );
        m_uiAlarms.alarmFilter->setFilter( alarm.filter );
        connect( m_uiAlarms.alarmFilter, SIGNAL( changed() ), this, SLOT( alarmChanged() ) );
    }

    bool enableWidgets = !m_alarm.isEmpty();
    m_uiAlarms.removeAlarm->setEnabled( enableWidgets );
    m_uiAlarms.renameAlarm->setEnabled( enableWidgets );
    m_uiAlarms.lblAlarms->setEnabled( enableWidgets );
    m_uiAlarms.alarms->setEnabled( enableWidgets );
    m_uiAlarms.lblAffectedStops->setEnabled( enableWidgets );
    m_uiAlarms.affectedStops->setEnabled( enableWidgets );
    m_uiAlarms.lblAlarmType->setEnabled( enableWidgets );
    m_uiAlarms.alarmType->setEnabled( enableWidgets );
    m_uiAlarms.grpAlarmFilters->setEnabled( enableWidgets );

    connect( m_uiAlarms.alarms, SIGNAL( currentIndexChanged( int ) ),
             this, SLOT( currentAlarmChanged( int ) ) );
}

void SettingsUiManager::setValuesOfFilterConfig()
{
    kDebug() << "Set GUI Values";
    if( m_uiFilter.filterConfigurations->currentIndex() == -1 ) {
        kDebug() << "No filter configuration selected, select first one now";
        m_uiFilter.filterConfigurations->setCurrentIndex( 0 );
    }

    // Build list of filter configuration names
    QStringList filterConfigs = m_filters.names();

    // Store selected filter configuration
    QString currentFilterConfiguration = m_uiFilter.filterConfigurations->currentText();

    // Clear the list of filter configurations and add the new ones.
    // The currentIndexChanged signal is disconnected meanwhile,
    // because the filter configuration doesn't need to be reloaded.
    disconnect( m_uiFilter.filterConfigurations, SIGNAL( currentIndexChanged( QString ) ),
                this, SLOT( loadFilterConfiguration( QString ) ) );
    m_uiFilter.filterConfigurations->clear();
    m_uiFilter.filterConfigurations->addItems( filterConfigs );
    if( currentFilterConfiguration.isEmpty() ) {
        m_uiFilter.filterConfigurations->setCurrentIndex( 0 );
    } else {
        m_uiFilter.filterConfigurations->setCurrentItem( currentFilterConfiguration );
    }
    connect( m_uiFilter.filterConfigurations, SIGNAL( currentIndexChanged( QString ) ),
             this, SLOT( loadFilterConfiguration( QString ) ) );

    if( currentFilterConfiguration.isEmpty() ) {
        currentFilterConfiguration = m_uiFilter.filterConfigurations->currentText();
        kDebug() << "No Item Selected" << currentFilterConfiguration;
    }
    kDebug() << "Filter configuration selected" << currentFilterConfiguration;

    bool enableWidgets = m_uiFilter.filterConfigurations->count() != 0;
    m_uiFilter.lblAffectedStops->setEnabled( enableWidgets );
    m_uiFilter.affectedStops->setEnabled( enableWidgets );
    m_uiFilter.lblFilterAction->setEnabled( enableWidgets );
    m_uiFilter.filterAction->setEnabled( enableWidgets );
    m_uiFilter.grpFilterCriteria->setEnabled( enableWidgets );
    m_uiFilter.filterConfigurations->setEnabled( enableWidgets );
    m_uiFilter.removeFilterConfiguration->setEnabled( enableWidgets );
    m_uiFilter.renameFilterConfiguration->setEnabled( enableWidgets );
    if( enableWidgets ) {
        QString filterConfiguration = currentFilterConfiguration;
        FilterSettings filters = m_filters.byName( filterConfiguration );
        m_uiFilter.filterAction->setCurrentIndex( static_cast<int>(filters.filterAction) );

        disconnect( m_uiFilter.affectedStops, SIGNAL( checkedItemsChanged() ),
                    this, SLOT( affectedStopsFilterChanged() ) );
        m_uiFilter.affectedStops->setCheckedRows( filters.affectedStops.toList() );
        connect( m_uiFilter.affectedStops, SIGNAL( checkedItemsChanged() ),
                 this, SLOT( affectedStopsFilterChanged() ) );

        // Clear old filter widgets
        int minWidgetCount = m_uiFilter.filters->minimumWidgetCount();
        int maxWidgetCount = m_uiFilter.filters->maximumWidgetCount();
        m_uiFilter.filters->setWidgetCountRange();
        m_uiFilter.filters->removeAllWidgets();

        // Setup FilterWidgets from m_filters
        foreach( const Filter & filter, filters.filters ) {
            m_uiFilter.filters->addFilter( filter );
        }

        int added = m_uiFilter.filters->setWidgetCountRange( minWidgetCount, maxWidgetCount );
        setFilterConfigurationChanged( added != 0 );
    }
}

Settings SettingsUiManager::settings()
{
    Settings ret;

    // Set stop settings list (general settings page)
    ret.setStops( m_stopListWidget->stopSettingsList() );

    // Set stored "no-Gui" settings (without widgets in the configuration dialog)
    ret.setColorGroups( m_colorGroup );
    ret.setCurrentStop( m_currentStopSettingsIndex );
    if( ret.currentStopIndex() >= ret.stops().count() ) {
        ret.setCurrentStop( ret.stops().count() - 1 );
    }

    // Set filter settings list and update stored settings if there are changes in the GUI widgets
    if( m_filterConfigChanged ) {
        m_filters.set( currentFilterSettings() );
    }
    ret.setFilters( m_filters );

    // Set alarm settings list and update stored settings if there are changes in the GUI widgets
    if( m_alarmsChanged && m_uiAlarms.alarms->currentIndex() != -1 ) {
        m_alarm[ m_uiAlarms.alarms->currentIndex() ] = currentAlarmSettings();
    }
    ret.setAlarms( m_alarm );

    // Set advanced settings
    if( m_uiAdvanced.showArrivals->isChecked() ) {
        ret.setDepartureArrivalListType( ArrivalList );
    } else {
        ret.setDepartureArrivalListType( DepartureList );
    }

    Settings::SettingsFlags flags = Settings::NoSettingsFlags;
    if ( m_hideTargetColumn ) {
        flags |= Settings::HideTargetColumn;
    }
    if ( m_uiAppearance.shadow->isChecked() ) {
        flags |= Settings::DrawShadows;
    }
    if ( m_uiAppearance.radioUseDefaultFont->isChecked() ) {
        flags |= Settings::UseThemeFont;
    }
    if ( m_uiAppearance.colorize->isChecked() ) {
        flags |= Settings::ColorizeDepartureGroups;
    }
    ret.setSettingsFlags( flags );

    switch ( m_uiAdvanced.additionalData->currentIndex() ) {
    case 2:
        ret.setAdditionalDataRequestType( Settings::NeverRequestAdditionalData );
        break;
    case 0:
        ret.setAdditionalDataRequestType( Settings::RequestAdditionalDataDirectly );
        break;
    case 1:
        ret.setAdditionalDataRequestType( Settings::RequestAdditionalDataWhenNeeded );
        break;
    default:
        kDebug() << "Unknown additional data request type index:" << m_uiAdvanced.additionalData->currentIndex()
                 << "Using default type:" << Settings::DefaultAdditionalDataRequestType;
        ret.setAdditionalDataRequestType( Settings::DefaultAdditionalDataRequestType );
        break;
    }

    // Set appearance settings
    Settings::DepartureTimeFlags timeFlags = Settings::DoNotShowDepartureTime;
    if ( m_uiAppearance.cmbDepartureColumnInfos->currentIndex() != 1 ) {
        timeFlags |= Settings::ShowRemainingTime;
    }
    if ( m_uiAppearance.cmbDepartureColumnInfos->currentIndex() <= 1 ) {
        timeFlags |= Settings::ShowDepartureTime;
    }
    if ( m_uiAppearance.displayTimeBold->checkState() == Qt::Checked ) {
        timeFlags |= Settings::DisplayDepartureTimeBold;
    }
    ret.setDepartureTimeFlags( timeFlags );

    ret.setMaximalNumberOfDepartures( m_uiAdvanced.maximalNumberOfDepartures->value() );
    ret.setLinesPerRow( m_uiAppearance.linesPerRow->value() );
    ret.setSizeFactor( Settings::sizeFactorFromSize(m_uiAppearance.size->value()) );
    if( flags.testFlag(Settings::UseThemeFont) ) {
        ret.setFont( Plasma::Theme::defaultTheme()->font(Plasma::Theme::DefaultFont) );
    } else {
        QFont font = ret.font();
        font.setFamily( m_uiAppearance.font->currentFont().family() );
        ret.setFont( font );
    }

    return ret;
}

FilterSettings SettingsUiManager::currentFilterSettings() const
{
    FilterSettings filters;
    filters.filterAction = static_cast< FilterAction >( m_uiFilter.filterAction->currentIndex() );
    filters.affectedStops = m_uiFilter.affectedStops->checkedRows().toSet();
    filters.filters = m_uiFilter.filters->filters();
    filters.name = m_uiFilter.filterConfigurations->currentText();
    return filters;
}

AlarmSettings SettingsUiManager::currentAlarmSettings( const QString &name ) const
{
    Q_ASSERT( m_uiAlarms.alarms->currentIndex() != -1 );

    AlarmSettings alarm;
    int row = m_uiAlarms.alarms->findText( name );
    if( row >= 0 && row < m_alarm.count() ) {
        alarm = m_alarm[ row ];
    } else {
        kDebug() << "No existing alarm settings found for the current alarm" << name;
    }
//     alarm.enabled = m_uiAlarms.alarms->currentItem()->checkState() == Qt::Checked;
    alarm.name = name.isNull() ? m_uiAlarms.alarms->currentText() : name;
    alarm.affectedStops = m_uiAlarms.affectedStops->checkedRows();
    alarm.type = static_cast<AlarmType>( m_uiAlarms.alarmType->currentIndex() );
    alarm.filter = m_uiAlarms.alarmFilter->filter();
    return alarm;
}

void SettingsUiManager::loadFilterConfiguration( const QString &filterConfig )
{
    if( filterConfig.isEmpty() ) {
        return;
    }

    if( filterConfig == m_lastFilterConfiguration ) {
        return; // Selected the same filter configuration again
    }

    if( m_filterConfigChanged && !m_lastFilterConfiguration.isEmpty() ) {
        // Store to previously selected filter configuration
        FilterSettings filters = currentFilterSettings();
        filters.name = m_lastFilterConfiguration;
        m_filters.set( filters );
    }

    m_lastFilterConfiguration = filterConfig;
    setValuesOfFilterConfig();
    setFilterConfigurationChanged( false );
}

void SettingsUiManager::addFilterConfiguration()
{
    // Get an unused filter configuration name
    QString newFilterConfig = i18nc( "@info/plain Default name of a new filter configuration",
                                     "New Configuration" );
    int i = 2;
    while( m_filters.hasName( newFilterConfig ) ) {
        newFilterConfig = i18nc( "@info/plain Default name of a new filter configuration "
                                 "if the other default names are already used",
                                 "New Configuration %1", i );
        ++i;
    }

    bool ok;
    do {
        newFilterConfig = KInputDialog::getText( i18nc( "@title:window", "Choose a Name" ),
                          i18nc( "@label:textbox", "Name of the new Filter Configuration:" ), newFilterConfig,
                          &ok, m_configDialog, new QRegExpValidator( QRegExp( "[^\\*&]*" ), this ) );
        if( !ok || newFilterConfig.isNull() ) {
            return; // Canceled
        }
        if( m_filters.hasName( newFilterConfig ) ) {
            KMessageBox::information( m_configDialog, i18nc( "@info/plain",
                                      "There is already a filter configuration with the name <resource>%1</resource>. "
                                      "Please choose another one.", newFilterConfig ) );
        } else {
            // Got a valid name, done with asking for a name
            break;
        }
    } while( true );

    // Append new filter settings
    FilterSettings filters;
    filters.name = newFilterConfig;
    m_filters << filters;

    m_uiFilter.filterConfigurations->setCurrentItem( newFilterConfig, true );
    setFilterConfigurationChanged();
    changed();
}

void SettingsUiManager::removeFilterConfiguration()
{
    int index = m_uiFilter.filterConfigurations->currentIndex();
    if( index == -1 ) {
        kDebug() << "No selection, nothing to delete";
        return;
    }

    // Show a warning
    QString currentFilterConfiguration = m_uiFilter.filterConfigurations->currentText();
    if( KMessageBox::warningContinueCancel( m_configDialog,
                                            i18nc( "@info", "<warning>This will permanently delete the selected filter "
                                                    "configuration <resource>%1</resource>.</warning>", currentFilterConfiguration ),
                                            QString(), KStandardGuiItem::cont(), KStandardGuiItem::cancel(),
                                            "deleteFilterSettings" )
            != KMessageBox::Continue ) {
        return; // Cancel clicked
    }

    // Remove filter configuration from the filter settings list
    m_filters.removeByName( currentFilterConfiguration );

    // Remove filter configuration from the UI filter list
    // but without calling loadFilterConfiguration here, therefore the disconnect
    disconnect( m_uiFilter.filterConfigurations, SIGNAL( currentIndexChanged( QString ) ),
                this, SLOT( loadFilterConfiguration( QString ) ) );
    m_uiFilter.filterConfigurations->removeItem( index );
    connect( m_uiFilter.filterConfigurations, SIGNAL( currentIndexChanged( QString ) ),
             this, SLOT( loadFilterConfiguration( QString ) ) );

    // Select default filter configuration
    if( index >= m_uiFilter.filterConfigurations->count() ) {
        index = m_uiFilter.filterConfigurations->count() - 1;
    }
    if( index != -1 ) {
        m_uiFilter.filterConfigurations->setCurrentIndex( index );
    } else {
        setValuesOfFilterConfig();
    }

    changed();
}

void SettingsUiManager::renameFilterConfiguration()
{
    QString currentFilterConfiguration = m_uiFilter.filterConfigurations->currentText();
    bool ok;
    QString newFilterConfig = KInputDialog::getText( i18nc( "@title:window", "Choose a Name" ),
                              i18nc( "@label:textbox", "New Name of the Filter Configuration:" ), currentFilterConfiguration,
                              &ok, m_configDialog, new QRegExpValidator( QRegExp( "[^\\*&]*" ), this ) );
    if( !ok || newFilterConfig.isNull() ) {
        return; // Canceled
    }

    // Get key name of the current filter configuration
    if( newFilterConfig == currentFilterConfiguration ) {
        return; // Not changed, but the old name was accepted
    }

    // Check if the new name is valid.
    // '*' or '&' is also not allowed in the name but that's already validated by a QRegExpValidator.
    if( newFilterConfig.isEmpty() ) {
        KMessageBox::information( m_configDialog, i18nc( "@info", "Empty names are not allowed." ) );
        return;
    }

    // Check if the new name is already used and ask if it should be overwritten
    if( m_filters.hasName( newFilterConfig )
            && KMessageBox::warningYesNo( m_configDialog,
                                          i18nc( "@info", "<warning>There is already a filter configuration with the name "
                                                  "<resource>%1</resource>.</warning><nl/>Do you want to overwrite it?",
                                                  newFilterConfig ) )
            != KMessageBox::Yes ) {
        return; // No (don't overwrite) pressed
    }

    // Remove the filter configuration from the old key name
    // and add it with the new key name
    FilterSettings filters = m_filters.byName( currentFilterConfiguration );
    m_filters.removeByName( currentFilterConfiguration );
    filters.name = newFilterConfig;
    m_filters.set( filters );

    // Remove old name from the list of filter configurations and add the new one.
    // The currentIndexChanged signal is disconnected while changing the name,
    // because the filter configuration doesn't need to be reloaded.
    disconnect( m_uiFilter.filterConfigurations, SIGNAL( currentIndexChanged( QString ) ),
                this, SLOT( loadFilterConfiguration( QString ) ) );
    int index = m_uiFilter.filterConfigurations->currentIndex();
    if( index == -1 ) {
        kDebug() << "Removed filter config not found in list" << currentFilterConfiguration;
    } else {
        m_uiFilter.filterConfigurations->removeItem( index );
    }
    m_uiFilter.filterConfigurations->setCurrentItem( newFilterConfig, true );
    m_lastFilterConfiguration = newFilterConfig;
    connect( m_uiFilter.filterConfigurations, SIGNAL( currentIndexChanged( QString ) ),
             this, SLOT( loadFilterConfiguration( QString ) ) );

    // Update filter configuration name in stop settings
    StopSettingsList stopSettingsList = m_stopListWidget->stopSettingsList();
    for( int i = 0; i < stopSettingsList.count(); ++i ) {
        if( stopSettingsList[i].get<QString>( FilterConfigurationSetting ) == currentFilterConfiguration ) {
            stopSettingsList[i].set( FilterConfigurationSetting, newFilterConfig );
        }
    }
    m_stopListWidget->setStopSettingsList( stopSettingsList );
    changed();

    // Update widgets containing a list of filter configuration names
    // NOTE not needed any longer (because of pointers to m_filters
//     updateFilterConfigurationLists();
}

void SettingsUiManager::filterActionChanged( int index )
{
    FilterAction filterAction = static_cast< FilterAction >( index );

    // Store to last edited filter settings
    QString currentFilterConfiguration = m_uiFilter.filterConfigurations->currentText();
    FilterSettings filters = m_filters.byName( currentFilterConfiguration );
    filters.filterAction = filterAction;
    m_filters.set( filters );

    kDebug() << "Filter configuration changed to" << filterAction;
//     setFilterConfigurationChanged();
}

void SettingsUiManager::filtersChanged()
{
    kDebug() << "Filters changed, directly write them to m_filters";
    m_filters.set( currentFilterSettings() );
}

void SettingsUiManager::setFilterConfigurationChanged( bool changed )
{
    if( m_filterConfigChanged == changed ) {
        return;
    }

    bool noFilter = m_filters.isEmpty();
    m_uiFilter.filterConfigurations->setDisabled( noFilter );
    m_uiFilter.removeFilterConfiguration->setDisabled( noFilter );
    m_uiFilter.renameFilterConfiguration->setDisabled( noFilter );

    m_filterConfigChanged = changed;
}

int SettingsUiManager::filterConfigurationIndex( const QString &filterConfig )
{
    int index = m_uiFilter.filterConfigurations->findText( filterConfig );
    if( index == -1 ) {
        kWarning() << "Item" << filterConfig << "not found!";
    }
    return index;
}

void SettingsUiManager::exportFilterSettings()
{
    QString fileName = KFileDialog::getSaveFileName(
                           KUrl( "kfiledialog:///filters" ), QString(), m_configDialog,
                           i18nc( "@title:window", "Export Filter Settings" ) );
    if( fileName.isEmpty() ) {
        return;
    }

    KConfig config( fileName, KConfig::SimpleConfig );
    SettingsIO::writeFilterConfig( currentFilterSettings(), config.group( QString() ) );
}

void SettingsUiManager::importFilterSettings()
{
    QString fileName = KFileDialog::getOpenFileName(
                           KUrl( "kfiledialog:///filters" ), QString(), m_configDialog,
                           i18nc( "@title:window", "Import Filter Settings" ) );
    if( fileName.isEmpty() ) {
        return;
    }

    KConfig config( fileName, KConfig::SimpleConfig );
    FilterSettings filters = SettingsIO::readFilterConfig( config.group( QString() ) );
//     TODO: Set filters in GUI
}
// kate: indent-mode cstyle; indent-width 4; replace-tabs on;
