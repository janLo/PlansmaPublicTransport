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

#ifndef STOPSETTINGSDIALOG_HEADER
#define STOPSETTINGSDIALOG_HEADER

/** @file
* @brief Contains the StopSettingsDialog.
* 
* @author Friedrich Pülz <fpuelz@gmx.de> */

#include "publictransporthelper_export.h"

// These three are included to use it's enums in the constructor
#include "stopfinder.h"
#include "accessorinfodialog.h"
#include "stopsettings.h"

#include <KDialog>
#include <Plasma/DataEngine> // For Plasma::DataEngine::Data

#if KDE_VERSION >= KDE_MAKE_VERSION(4,4,0)
// #define USE_KCATEGORYVIEW // Remove the comment to use KCategoryView for the service provider combobox
#endif

class StopSettings;
class StopSettingsDialogPrivate;
class LocationModel;
class ServiceProviderModel;
class DynamicLabeledLineEditList;
class HtmlDelegate;
class KLineEdit;
class NearStopsDialog;

/**
 * @brief This dialog can be used to select all needed information for a public transport stop 
 *   (location, service provider and stop name).
 * 
 * With the options given in the constructor widgets can be shown or hidden. The dialog can also
 * be used to only select a service provider by not using @ref ShowStopInputField.
 * 
 * There are some static convenience function that create a StopSettingsDialog for special purposes.
 * They all use the default constructor.
 * @par
 * @ref createSimpleAccessorSelectionDialog to create a simple accessor selection dialog. It shows
 * two comboboxes to select a location and a service provider. The location combobox is used to
 * filter the shown service providers.
 * @par
 * @ref createSimpleStopSelectionDialog to create a simple stop selection dialog. In addition to
 * the simple accessor selection option it shows a widget to edit the stop name(s), with 
 * autocompletion if the service provider supports that.
 * @par
 * @ref createExtendedStopSelectionDialog to create an extended stop selection dialog with extended
 * settings to select a filter configuration, the first departure time and the alarm time for 
 * the stop.
 * 
 * It's very easy to use this dialog:
 * @code
 * // Create a dialog
 * StopSettingsDialog *dialog = StopSettingsDialog::createSimpleStopSelectionDialog( parent );
 * 
 * // Show the dialog
 * if ( dialog->exec == KDialog::Accepted ) {
 *   // Get the settings made in the dialog if it was accepted
 *   StopSettings stopSettings = dialog->stopSettings();
 * 
 *   // TODO: Do something with the stop settings
 *   QString stopName = stopSettings.stop( 0 ).name; // There's at least one stop name in the list,
 *                                                   // because minimum widget count is 1 by default
 *   QString serviceProviderID = stopSettings[ ServiceProviderSetting ].toString();
 * }
 * @endcode
 * 
 * To change the minimal and maximal number of stop names use @ref setStopCountRange, default
 * is 1 - 3 stop names. It can be useful to allow more than one stop name, eg. to show a list of
 * departures/arrivals for two stops in one list, when the stops are very close.
 * 
 * You can use a custom widget factory for custom settings (shown in the details section).
 * A custom widget factory must be given in the constructor (or one of the <em>create...</em> 
 * functions). See @ref StopSettingsWidgetFactory for more information on how to create a custom
 * widget factory. Custom settings use @ref UserSetting or higher. You can create
 * any widget you like and add it to the details section of this dialog. The factory also gives
 * label texts and object names (to find the widgets in the dialog again using 
 * QObject::findChild) and is used to get/set values of the custom widget (using QVariant).
 * Values of custom setting widgets are automatically returned by @ref stopSettings.
 * @ref setStopSettings uses custom settings to set the values of associated widgets.
 * 
 * You can add custom settings widgets in the default constructor or later using 
 * @ref addSettingWidget.
 * 
 * @see Option
 * @see StopSettingsDialog::StopSettingsDialog
 * @see AccessorInfoDialog
 **/
class PUBLICTRANSPORTHELPER_EXPORT StopSettingsDialog : public KDialog {
	Q_OBJECT
	
public:
	/**
	 * @brief Options for the stop settings dialog.
	 * 
	 * With these options widgets can be shown/hidden and usage of the @ref HtmlDelegate can be
	 * toggled on/off.
	 * 
	 * Some enumerables are combined into a single enumberable for convenience, eg. 
	 * SimpleServiceProviderSelection, SimpleStopSelection or ExtendedStopSelection.
	 **/
	enum Option {
		NoOption = 0x0000, /**< No options, ie. the simplest possible StopSettingsDialog. No
				* stop input field, only location and service provider selection. */
		
		ShowStopInputField = 0x0001, /**< Shows a stop input field and for some service providers
				* also a city input field. If this flag isn't set, only a location and a 
				* service provider may be selected. The dialog title is adjusted accordingly
				* (from change stop(s) to change service provider). */
		ShowNearbyStopsButton = 0x0002, /**< Show a dialog button to (try to) get a list of 
				* public transport stops near the users current position.
				* Does nothing if @ref ShowStopInputField isn't also set. */
		ShowAccessorInfoButton = 0x0004, /**< Show a button on the right of the service
				* provider combobox which opens an @ref AccessorInfoDialog. */
		ShowInstallAccessorButton = 0x0008, /**< Shows a button on the right of the service 
				* provider combobox to install new accessors (lokal files or via GHNS). */
				
		ShowFilterConfigurationConfig = 0x0010, /**< Shows a combobox in a details section to 
				* select a filter configuration. It offers all filter configurations given in the 
				* constructor. */
		ShowAlarmTimeConfig = 0x0020, /**< Shows an input field in a details section to edit the
				* time in minutes before the actual departure/arrival at which alarms should be 
				* triggered. */
		ShowFirstDepartureConfig = 0x0040, /**< Shows widgets in a details section to configure 
				* the first shown  departure/arrival. It adds two radio buttons: 
				* <em>"Relative to current time"</em> (with an integer input field to edit the 
				* time in minutes from now) and <em>"At custom time"</em> (with a time input 
				* field to edit the first departure/arrival time directly. */
				
		UseHtmlForLocationConfig = 0x0100, /**< Uses an @ref HtmlDelegate to draw the items of
				* the location combobox. */
		UseHtmlForServiceProviderConfig = 0x0200, /**< Uses an @ref HtmlDelegate to draw the items 
				* of the service provider combobox. */
				
		UseHtmlEverywhere = UseHtmlForLocationConfig | UseHtmlForServiceProviderConfig,
				/** Combination of UseHtmlForLocationConfig and UseHtmlForServiceProviderConfig. */
		ShowAllDetailsWidgets = ShowInstallAccessorButton | ShowFilterConfigurationConfig |
				ShowAlarmTimeConfig | ShowFirstDepartureConfig, /** Shows all widgets of the 
				* details section. Combination of ShowInstallAccessorButton, 
				* ShowFilterConfigurationConfig, ShowAlarmTimeConfig and 
				* ShowFirstDepartureConfig. */
		
		SimpleAccessorSelection = ShowAccessorInfoButton | ShowInstallAccessorButton | 
				UseHtmlEverywhere, /**< Options for a simple accessor selection dialog. 
				* It doesn't show stop selection fields, only widgets associated to service
				* provider selection, including an accessor info button and a button to install
				* new accessors. */
		SimpleStopSelection = SimpleAccessorSelection | ShowStopInputField | 
				ShowNearbyStopsButton, /** Extends SimpleServiceProviderSelection with a stop 
				* input field and a button to search for nearby stops. */
		ExtendedStopSelection = SimpleStopSelection | ShowAllDetailsWidgets, /**< Extends 
				* SimpleStopSelection with ShowAllDetailsWidgets. */
				
		DefaultOptions = SimpleStopSelection /**< Default options. */
	};
	Q_DECLARE_FLAGS(Options, Option);
	
	/**
	 * @brief Creates a new stop settings dialog.
	 * 
	 * You can also create a StopSettingsDialog by using one of the static member functions
	 * @ref createSimpleAccessorSelectionDialog, @ref createSimpleStopSelectionDialog or
	 * @ref createExtendedStopSelectionDialog. These functions use this constructor to create
	 * the dialog object.
	 * For most configurability use this constructor with your options.
	 * 
	 * If you want to use a custom @p factory <em>MyStopSettingsWidgetFactory</em> to create 
	 * custom widgets given by @p settings or later in @ref addSettingWidget 
	 * the @p factory argument looks like this: 
	 * @code MySettingsWidgetFactory::Pointer::create() @endcode
	 * 
	 * For each setting given in @p settings widgets get created and placed 
	 * in a details section in the given order. The given @p factory is used to create the widgets.
	 * If you want to use @ref StopSettings::UserSetting here, you need to give a custom @p factory, 
	 * which can create the widget for the given setting.
	 * You can also add settings widgets later using @ref addSettingWidget.
	 * Settings that are controllable via @ref Option aren't created if the associated
	 * option isn't set, even if they are in this list and vice versa. For example
	 * @ref StopSettings::FilterConfigurationSetting in this list only causes the creation of 
	 * filter configuration widgets, if @ref StopSettingsDialog::ShowFilterConfigurationConfig 
	 * is set. If @ref StopSettingsDialog::ShowFilterConfigurationConfig is set, but 
	 * @ref StopSettings::FilterConfigurationSetting isn't in the list it is appended to the list.
	 * 
	 * The argument @p settings can be used to change the order of the default settings 
	 * (< StopSettings::UserSetting) and/or to include custom setting widgets.
	 * 
	 * @param parent The parent widget of the dialog. Default is 0.
	 *
	 * @param stopSettings The stop settings to initialize the dialog with. This is passed to 
	 *   @ref setStopSettings. It can also be used to initialize the values of custom settings
	 *   widgets in @p settings (using @ref StopSettingsWidgetFactory::setValueOfSetting).
	 * 
	 * @param options Options for the stop settings dialog.
	 * 
	 * @param accessorInfoDialogOptions Options for the accessor info dialog, only used if
	 *   @p options has the flag @ref ShowAccessorInfoButton set.
	 * 
	 * @param filterConfigurations A list of configured filter configurations to choose from
	 *   for @ref StopSettigns::FilterConfigurationSetting.
	 * 
	 * @param customSettings A list of @ref StopSetting to create widgets for.
	 *   You can also add custom widgets later using @ref addSettingWidget (but you need to give 
	 *   your custom @p factory in the constructor).
	 *   This list can be used to change the order of the default detailed settings widgets
	 *   (eg. for FilterConfigurationSetting). If a detailed setting is required by an option
	 *   in @p options and it isn't in this list, it gets appended to the list (eg. 
	 *   ShowFilterConfigurationConfig requires FilterConfigurationSetting). A detailed setting
	 *   can also be removed from this list if the associated option is missing from @p options
	 *   (eg. FilterConfigurationSetting gets removed from this list if 
	 *   ShowFilterConfigurationConfig isn't set in @p options).
	 *   That means that if you use both default and custom detailed widgets and the associated 
	 *   default detailed settings aren't in this list, they get appended (and shown below the
	 *   custom settings). If you want the default settings to be shown above the custom ones,
	 *   you need to add them before the custom settings in this list.
	 * 
	 * @param factory A pointer (QSharedPointer) to an object derived from 
	 *   @ref StopSettingsWidgetFactory, which can create the given @p customSettings by calling 
	 *   @ref StopSettingsWidgetFactory::widgetForSetting. You can use the <em>Pointer</em>
	 *   typedef to create your custom widget factory, eg. <em>MyFactory::Pointer::create()</em>.
	 *   By default the standard factory is used, which can create widgets to configure the alarm 
	 *   time, the first departure time and the used filter configuration.
	 *   You need to give the factory in the constructor, it's not possible to change the used
	 *   factory later.
	 **/
	explicit StopSettingsDialog( QWidget *parent = 0,
			const StopSettings &stopSettings = StopSettings(), 
			StopSettingsDialog::Options options = DefaultOptions,
			AccessorInfoDialog::Options accessorInfoDialogOptions = AccessorInfoDialog::DefaultOptions,
			const QStringList &filterConfigurations = QStringList(),
			const QList<int> &customSettings = QList<int>(),
			StopSettingsWidgetFactory::Pointer factory = StopSettingsWidgetFactory::Pointer::create() );
	
	/**
	 * @brief Simple Desctructor.
	 **/
	virtual ~StopSettingsDialog();
	
	/**
	 * @brief Creates a new accessor selection dialog, which is a StopSettingsDialog with
	 *   the option @ref SimpleAccessorSelection. 
	 * 
	 * It provides a button to install new accessors.
	 *
	 * This function is provided for convenience, it just calls the constructor of 
	 * StopSettingsDialog.
	 * 
	 * @param parent The parent widget of the dialog. Default is 0.
	 * 
	 * @param stopSettings The stop settings to initialize the dialog with.
	 * 
	 * @param factory A pointer (QSharedPointer) to an object derived from 
	 *   @ref StopSettingsWidgetFactory, which can create widgets for (custom) settings by calling 
	 *   @ref StopSettingsWidgetFactory::widgetForSetting. You can use the <em>Pointer</em>
	 *   typedef to create your custom widget factory, eg. <em>MyFactory::Pointer::create()</em>.
	 *   By default the standard factory is used, which can create widgets to configure the alarm 
	 *   time, the first departure time and the used filter configuration.
	 *   You need to give the factory in the constructor, it's not possible to change the used
	 *   factory later.
	 * 
	 * @return The newly created service provider selection dialog.
	 **/
	static StopSettingsDialog *createSimpleAccessorSelectionDialog( 
			QWidget *parent = 0, const StopSettings &stopSettings = StopSettings(),
			StopSettingsWidgetFactory::Pointer factory = StopSettingsWidgetFactory::Pointer::create() );
	
	/**
	 * @brief Creates a new stimple stop selection dialog, which is a StopSettingsDialog with
	 *   the option @ref SimpleStopSelection. 
	 * 
	 * It provides a button to install new accessors and a button to find stops near the users 
	 * current position.
	 *
	 * This function is provided for convenience, it just calls the constructor of 
	 * StopSettingsDialog.
	 * 
	 * @param parent The parent widget of the dialog. Default is 0.
	 * 
	 * @param stopSettings The stop settings to initialize the dialog with.
	 * 
	 * @param factory A pointer (QSharedPointer) to an object derived from 
	 *   @ref StopSettingsWidgetFactory, which can create widgets for (custom) settings by calling 
	 *   @ref StopSettingsWidgetFactory::widgetForSetting. You can use the <em>Pointer</em>
	 *   typedef to create your custom widget factory, eg. <em>MyFactory::Pointer::create()</em>.
	 *   By default the standard factory is used, which can create widgets to configure the alarm 
	 *   time, the first departure time and the used filter configuration.
	 *   You need to give the factory in the constructor, it's not possible to change the used
	 *   factory later.
	 * 
	 * @return The newly created service provider selection dialog.
	 **/
	static StopSettingsDialog *createSimpleStopSelectionDialog( 
			QWidget *parent = 0, const StopSettings &stopSettings = StopSettings(),
			StopSettingsWidgetFactory::Pointer factory = StopSettingsWidgetFactory::Pointer::create() );
	
	/**
	 * @brief Creates a new extended stop selection dialog, which is a StopSettingsDialog with
	 *   the option @ref ExtendedStopSelection. 
	 * 
	 * It provides a details section with additional settings, a button to install new accessors 
	 * and a button to find stops near the users current position.
	 *
	 * This function is provided for convenience, it just calls the constructor of 
	 * StopSettingsDialog.
	 * 
	 * @param parent The parent widget of the dialog. Default is 0.
	 * 
	 * @param stopSettings The stop settings to initialize the dialog with.
	 * 
	 * @param filterConfigurations A list of configured filter configurations to choose from
	 *   for @ref StopSettigns::FilterConfigurationSetting.
	 * 
	 * @param factory A pointer (QSharedPointer) to an object derived from 
	 *   @ref StopSettingsWidgetFactory, which can create widgets for (custom) settings by calling 
	 *   @ref StopSettingsWidgetFactory::widgetForSetting. You can use the <em>Pointer</em>
	 *   typedef to create your custom widget factory, eg. <em>MyFactory::Pointer::create()</em>.
	 *   By default the standard factory is used, which can create widgets to configure the alarm 
	 *   time, the first departure time and the used filter configuration.
	 *   You need to give the factory in the constructor, it's not possible to change the used
	 *   factory later.
	 * 
	 * @return The newly created service provider selection dialog.
	 **/
	static StopSettingsDialog *createExtendedStopSelectionDialog(
			QWidget *parent = 0, const StopSettings &stopSettings = StopSettings(),
			const QStringList &filterConfigurations = QStringList(),
			StopSettingsWidgetFactory::Pointer factory = StopSettingsWidgetFactory::Pointer::create() );

	/** 
	 * @brief Adds a widget for the given @p setting in the details section using the 
	 *   StopSettingsWidgetFactory given in the constructor.
	 * 
	 * If @p setting is StopSettings::UserSetting or higher, the StopSettingsWidgetFactory 
	 * object given in the constructor should be a class derived from StopSettingsWidgetFactory
	 * which can create a widget for the given @p setting by overwriting
	 * @p StopSettingsWidgetFactory::widgetForStopSetting.
	 * 
	 * @note If there already is a widget for @p setting, the widget isn't created but the
	 *   already existent widget is returned.
	 * 
	 * @param setting The setting to create a widget for.
	 * 
	 * @param defaultValue The value to set for the created widget if there isn't already a value
	 *   in the StopSettings object. The value is set using the 
	 *   @ref StopSettingsWidgetFactory::setValueOfSetting.
	 * 
	 * @param data Can contain data for the widget created by StopSettingsWidgetFactory, like 
	 *   a QStringList for a combobox.
	 */
	QWidget *addSettingWidget( int setting, const QVariant &defaultValue, 
							   const QVariant &data = QVariant() );
	
	/** 
	 * @brief Adds a widget for the given @p setting in the details section without 
	 *   using a StopSettingsWidgetFactory.
	 * 
	 * @note If there already is a widget for @p setting, the @p widget gets hidden and the
	 *   already existent widget is returned.
	 * 
	 * @param label The text for the QLabel, which gets created for the new @p widget.
	 * 
	 * @param widget The widget to add.
	 * 
	 * @param setting The setting to create a widget for.
	 */
	QWidget *addSettingWidget( int setting, const QString &label, QWidget *widget );
	
	/** @returns the current stop settings of the dialog. */
	StopSettings stopSettings() const;
	
	/** @brief Sets the values of the widgets according to @p stopSettings. */
	void setStopSettings( const StopSettings &stopSettings );
	
	/**
	 * @brief Sets the minimum and maximum count of @ref StopWidget in this dialog.
	 * 
	 * The user can dynamically add/remove stop widgets by using the add/remove buttons.
	 * It can be useful to allow more than one stop widget, eg. to show a list of
	 * departures/arrivals for two stops in one list, when the stops are very close (and the user 
	 * wants to see all public transport departing/arriving around his home).
	 *
	 * @param minCount The minimum count of stop widgets. Default is 1.
	 * 
	 * @param maxCount The maximum count of stop widgets. Default is 3.
	 **/
	void setStopCountRange( int minCount = 1, int maxCount = 3 );
	
	/**
	 * @brief Gets a pointer to the @ref StopSettingsWidgetFactory, used to create widgets for
	 *   settings (@ref StopSetting).
	 * 
	 * The factory can only be set in the constructor.
	 * 
	 * @note LocationSetting, ServiceProviderSetting, CitySetting and StopNameSetting aren't 
	 *   created by the factory, but by using a .ui file.
	 *
	 * @return A pointer to the used StopSettingsWidgetFactory object. It's actually a 
	 *   QSharedPointer around StopSettingsWidgetFactory.
	 **/
	StopSettingsWidgetFactory::Pointer factory() const;

protected Q_SLOTS:
	/** @brief Another service provider has been selected.
	 * 
	 * @param index The index of the newly selected service provider. */
	void serviceProviderChanged( int index );
	/** @brief The city name has been changed. 
	 * 
	 * @param cityName The new city name. */
	void cityNameChanged( const QString &cityName );
	/** @brief Another location has been selected.
	 * 
	 * @param index The index of the newly selected location. */
	void locationChanged( int index );
	/** @brief The info button has been clicked. This shows information about the
	 * currently selected service provider in a dialog. */
	void clickedServiceProviderInfo();
	/** @brief The nearby stops button was clicked. */
	void geolocateClicked();
	/** @brief The dialog showing stops near the user was closed
	 *   (after clicking the nearby stops button). */
	void nearStopsDialogFinished( int result );
	/** @brief The stop name has been edited.
	 * 
	 * @param text The new stop name.
	 * 
	 * @param widgetIndex The index of the edited stop name. */
	void stopNameEdited( const QString &text, int widgetIndex );
	/** @brief Another combined stop has been added (eg. by clicking the add button.
	 *
	 * @param lineEdit The line edit widget that was added. */
	void stopAdded( QWidget *lineEdit );
	/** @brief The menu item to use GHNS to download new service providers was clicked. */
	void downloadServiceProvidersClicked();
	/** @brief The menu item to install a local accessor info XML was clicked. */
	void installServiceProviderClicked();
	void adjustStopListLayout();

	void stopFinderGeolocationData( const QString &countryCode, const QString &city,
			      qreal latitude, qreal longitude, int accuracy );
	void stopFinderError( StopFinder::Error error, const QString &errorMessage );
	void stopFinderFinished();
	void stopFinderFoundStops( const QStringList &stops, const QStringList &stopIDs,
				   const QString &serviceProviderID );

	/** @brief The data from the data engine was updated. */
	void dataUpdated( const QString& sourceName, const Plasma::DataEngine::Data &data );

protected:
	virtual void resizeEvent( QResizeEvent* );
	virtual void accept();

	StopSettingsDialogPrivate* const d_ptr;

private:
	Q_DECLARE_PRIVATE( StopSettingsDialog )
	Q_DISABLE_COPY( StopSettingsDialog )
};
Q_DECLARE_OPERATORS_FOR_FLAGS(StopSettingsDialog::Options);

QDebug &operator <<(QDebug debug, StopSettingsDialog::Option option);

#endif // Multiple inclusion guard