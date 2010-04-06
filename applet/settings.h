/*
*   Copyright 2010 Friedrich Pülz <fpuelz@gmx.de>
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

#ifndef SETTINGS_HEADER
#define SETTINGS_HEADER

#include "ui_publicTransportConfig.h"
#include "ui_publicTransportConfigAdvanced.h"
#include "ui_publicTransportFilterConfig.h"
#include "ui_publicTransportAppearanceConfig.h"
#include "ui_alarmConfig.h"

#include "global.h"
#include "filter.h"

#include <Plasma/Theme>
#include <Plasma/DataEngine>

/** Different types of alarms. */
enum AlarmType {
    AlarmRemoveAfterFirstMatch = 0, /**< The alarm will get removed once fired. */
    AlarmApplyToNewDepartures /**< The alarm won't be removed automatically,
	    * ie. it's a recurring alarm. */
};

/** Contains information about an alarm. */
struct AlarmSettings {
    AlarmSettings( const QString &name = "<unnamed>", bool autoGenerated = false ) {
	this->name = name;
	this->autoGenerated = autoGenerated;
	enabled = true;
	type = AlarmRemoveAfterFirstMatch;
    };
    
    QString name; /**< The name of this alarm. */
    bool enabled; /**< Wheather or not this alarm is enabled. */
    bool autoGenerated; /**< Wheather or not this alarm is auto generated.
	    * Alarms that get changed are no longer treated as auto generated. */
    Filter filter; /**< The filter to match alarm departures/arrivals. */
    AlarmType type; /**< The type of this alarm. */
    QList< int > affectedStops; /**< A list of stop settings indices for which
	    * this alarm should be applied. */
    QDateTime lastFired; /**< The time when this alarm was last fired. */
};
typedef QList<AlarmSettings> AlarmSettingsList;
bool operator ==( const AlarmSettings &l, const AlarmSettings &r );

class KConfigDialog;
class QStandardItemModel;
class StopListWidget;
class FilterListWidget;
// class DataSourceTester;
class Settings;
/** Manages the settings dialog. */
class SettingsUiManager : public QObject {
    Q_OBJECT
    public:
	enum DeletionPolicy {
	    DeleteWhenFinished,
	    KeepWhenFinished
	};
	
	SettingsUiManager( const Settings &settings,
			   Plasma::DataEngine *publicTransportEngine,
			   Plasma::DataEngine *osmEngine,
			   Plasma::DataEngine *favIconEngine,
			   Plasma::DataEngine *geolocationEngine,
			   KConfigDialog *parentDialog,
			   DeletionPolicy deletionPolicy = DeleteWhenFinished );

	/** @returns a Settings object with the current settings in the dialog. */
	Settings settings();
	
	static QString translateKey( const QString &key );
	static QString untranslateKey( const QString &translatedKey );

    signals:
	void settingsAccepted( const Settings &settings );
	void settingsFinished();

    public slots:
	void removeAlarms( const AlarmSettingsList &newAlarmSettings,
			   const QList<int> &removedAlarms );
	
    protected slots:
	/** The config dialog has been closed. */
	void configFinished();
	/** Ok pressed in the config dialog. */
	void configAccepted();
	
	/** The data from the data engine was updated. */
	void dataUpdated( const QString& sourceName,
			  const Plasma::DataEngine::Data& data );

	/** Loads the given @p filterConfig into the dialog. */
	void loadFilterConfiguration( const QString &filterConfig );
	/** Adds a new filter configuration. */
	void addFilterConfiguration();
	/** Removes the current filter configuration. */
	void removeFilterConfiguration();
	/** Renames the current filter configuration. */
	void renameFilterConfiguration();
	/** The action of the current filter has been changed. */
	void filterActionChanged( int index );
	void setFilterConfigurationChanged( bool changed = true );
	void updateFilterInfoLabel();
	
	void exportFilterSettings();
	void importFilterSettings();

	/** Another alarm has been selected. */
	void currentAlarmChanged( int row );
	/** The add alarm button has been clicked. */
	void addAlarmClicked();
	/** The remove alarm button has been clicked. */
	void removeAlarmClicked();
	/** The current alarm has been changed. */
	void alarmChanged();
	/** The type of the current alarm has been changed. */
	void currentAlarmTypeChanged( int index );
	/** The list of affected stops of this alarm has been changed. */
	void affectedStopsChanged();
	/** An alarm item has been changed, eg. it's text or checked state. */
	void alarmChanged( QListWidgetItem *item );

	/** The list of stop settings has been changed. */
	void stopSettingsChanged();
	/** A new stop has been added. */
	void stopSettingsAdded();
	/** A stop has been removed. */
	void stopSettingsRemoved( QWidget *widget, int widgetIndex );
	/** The used filter configuration has been changed in the "filter uses"
	* tab of the filters page. */
	void usedFilterConfigChanged( QWidget *widget );

    protected:
	void setValuesOfAdvancedConfig( const Settings &settings );
        void setValuesOfFilterConfig();
        void setValuesOfAlarmConfig();
	void setValuesOfAppearanceConfig( const Settings &settings );

	void initModels(); // init m_modelServiceProvider and m_modelLocations

    private:
	FilterSettings currentFilterSettings() const;
	AlarmSettings currentAlarmSettings( const QString &name = QString() ) const;
	int filterConfigurationIndex( const QString &filterConfig );
	void setAlarmTextColor( QListWidgetItem *item, bool hasAffectedStops = true ) const;
	
	DeletionPolicy m_deletionPolicy;
// 	DataSourceTester *m_dataSourceTester; // Tests data sources
	KConfigDialog *m_configDialog; // Stored for the accessor info dialog as parent
	
	Ui::publicTransportConfig m_ui;
	Ui::publicTransportConfigAdvanced m_uiAdvanced;
	Ui::publicTransportAppearanceConfig m_uiAppearance;
	Ui::publicTransportFilterConfig m_uiFilter;
	Ui::alarmConfig m_uiAlarms;
	
	QStandardItemModel *m_modelServiceProvider; // The model for the service provider combobox in the config dialog
	QStandardItemModel *m_modelLocations; // The model for the location combobox in the config dialog
	Plasma::DataEngine::Data m_serviceProviderData; // Service provider information from the data engine
	QVariantHash m_locationData; // Location information from the data engine.

	StopListWidget *m_stopListWidget;
	Plasma::DataEngine *m_publicTransportEngine, *m_osmEngine,
			   *m_favIconEngine, *m_geolocationEngine;

	int m_currentStopSettingsIndex;
	QStringList m_recentJourneySearches;
	bool m_showHeader, m_hideColumnTarget;
	
	QHash< QString, FilterSettings > m_filterSettings;
	QString m_lastFilterConfiguration; // The last set filter configuration
	bool m_filterConfigChanged; // Whether or not the filter configuration has changed from that defined in the filter configuration with the name [m_filterConfiguration]
	
	AlarmSettingsList m_alarmSettings;
	int m_lastAlarm;
	bool m_alarmsChanged;
};

/** Contains static methods to read/write settings. */
class SettingsIO {
    public:
	enum ChangedFlag {
	    NothingChanged = 0x0000, /**< Nothing has changed. */
	    IsChanged = 0x0001, /**< This flag is set if something has changed. */
	    ChangedServiceProvider = 0x0002, /**< Service provider settings have 
		    * been changed (stop name, service provider, ...). */
	    ChangedDepartureArrivalListType = 0x0004, /**< Changed from showing
		    * departures to arrivals and vice versa. */
	    ChangedStopSettings = 0x0008, /**< Stop settings have been changed. */
	    ChangedFilterSettings = 0x0010, /**< Filter settings have been changed. */
	    ChangedLinesPerRow = 0x0020, /**< The lines per row setting has been changed. */
	    ChangedAlarmSettings = 0x0040 /**< Alarm settings have been changed. */
	};
	Q_DECLARE_FLAGS( ChangedFlags, ChangedFlag );

	/** Read settings from @p cg and @p cgGlobal. */
	static Settings readSettings( KConfigGroup cg, KConfigGroup cgGlobal,
				      Plasma::DataEngine *publicTransportEngine = 0 );
	/** Write changed @p settings to @p cg and @p cgGlobal. @p oldSettings
	* is used to see which settings have been changed. */
	static ChangedFlags writeSettings( const Settings &settings,
		const Settings &oldSettings, KConfigGroup cg, KConfigGroup cgGlobal );
	static void writeNoGuiSettings( const Settings &settings,
					KConfigGroup cg, KConfigGroup cgGlobal );

	/** Read filter configuration from @p cgGlobal. */
	static FilterSettings readFilterConfig( const KConfigGroup &cgGlobal );
	static bool writeFilterConfig( const FilterSettings &filterSettings,
				       const FilterSettings &oldFilterSettings,
				       KConfigGroup cgGlobal );
	/** Write filter configuration @p filterSettings to @p cgGlobal. */
	static void writeFilterConfig( const FilterSettings &filterSettings,
				       KConfigGroup cgGlobal );
};
Q_DECLARE_OPERATORS_FOR_FLAGS( SettingsIO::ChangedFlags );

/** Contains all settings. */
struct Settings {
    Settings() {
	currentStopSettingsIndex = 0;
	filtersEnabled = false;
    };
    
    bool autoUpdate; /**< Wheather or not timetable data should be updated automatically. */
    bool showRemainingMinutes; /**< Whether or not remaining minutes until 
			    * departure should be shown. */
    bool showDepartureTime; /**< Whether or not departure times should be shown. */
    int currentStopSettingsIndex; /**< The index of the current stop settings. */
    QStringList recentJourneySearches; /**< A list of recently used journey searches. */
    
    int linesPerRow; /**< How many lines each row in the tree view should have. */
    int size; /**< The size of the timetable. @note Use @ref sizeFactor to size items. */
    float sizeFactor; /**< A factor to use for item sizes, calculated like this:
			    * (size + 3) * 0.2. */
    int maximalNumberOfDepartures; /**< The maximal number of displayed departures. */
    DepartureArrivalListType departureArrivalListType;
    bool drawShadows; /** Whether or not shadows should be drawn for departure items. */
    bool showHeader; /** Whether or not the header of the departure view should 
		       * be shown. */
    bool hideColumnTarget; /** Whether or not the target/origin column should be 
			     * shown in the departure view. */
    bool useDefaultFont; /**< Whether or not the default plasma theme's font is used. */
    QFont font; /**< The font to be used. */
    bool displayTimeBold; /** Whether or not the time should be displayed bold. */

    QFont sizedFont() const {
	QFont f = font;
	if ( f.pointSize() == -1 ) {
	    int pixelSize = f.pixelSize() * sizeFactor;
	    f.setPixelSize( pixelSize > 0 ? pixelSize : 1 );
	} else {
	    int pointSize = f.pointSize() * sizeFactor;
	    f.setPointSize( pointSize > 0 ? pointSize : 1 );
	}
	return f;
    };

    AlarmSettingsList alarmSettings; /** A list of all alarm settings. */
    
    StopSettingsList stopSettingsList; /** A list of all stop settings. */
    StopSettings currentStopSettings() const {
	if ( !isCurrentStopSettingsIndexValid() ) {
	    kDebug() << "Current stop index invalid" << currentStopSettingsIndex
			<< "Stop settings count:" << stopSettingsList.count();
	    return StopSettings();
	}
	return stopSettingsList[ currentStopSettingsIndex ];
    };

    /** @warning This crashes with invalid stop settings index.
    * @see isCurrentStopSettingsIndexValid */
    StopSettings &currentStopSettings() {
	Q_ASSERT_X( isCurrentStopSettingsIndexValid(), "StopSettings::currentStopSettings",
		    QString("There's no stop settings with index %1 to get a "
		    "reference to").arg(currentStopSettingsIndex).toLatin1() );
	return stopSettingsList[ currentStopSettingsIndex ];
    };

    bool isCurrentStopSettingsIndexValid() const {
	return currentStopSettingsIndex >= 0 &&
	       currentStopSettingsIndex < stopSettingsList.count();
    };

    bool filtersEnabled; /** Whether or not filters are enabled. */
    QHash< QString, FilterSettings > filterSettings; /** A list of all filter settings. */
    FilterSettings currentFilterSettings() const {
	    return filterSettings[ currentStopSettings().filterConfiguration ]; };

    bool checkConfig() {
	// TODO: Check when adding stops in StopSettingsDialog
	if ( stopSettingsList.isEmpty() )
	    return false;
	else {
	    foreach ( const StopSettings &stopSettings, stopSettingsList ) {
		if ( stopSettings.stops.isEmpty() )
		    return false;
		else {
		    foreach ( const QString &stop, stopSettings.stops ) {
			if ( stop.isEmpty() )
			    return false;
		    }
		}
	    }
	}
	return true;

	//     if ( m_useSeparateCityValue && (m_city.isEmpty()
	// 	    || m_stops.isEmpty() || m_stops.first().isEmpty()) )
	// 	emit configurationRequired(true, i18n("Please set a city and a stop."));
	//     else if ( m_stops.isEmpty() || m_stops.first().isEmpty() )
	// 	emit configurationRequired(true, i18n("Please set a stop."));
	//     else if ( m_serviceProvider == "" )
	// 	emit configurationRequired(true, i18n("Please select a service provider."));
	//     else {
	// 	emit configurationRequired(false);
	// 	return true;
	//     }
	//
	//     return false;
    };
};

#endif // SETTINGS_HEADER
