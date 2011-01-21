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

#include "stopsettingsdialog.h"

#include "ui_stopConfig.h"

#include "global.h"
#include "htmldelegate.h"
#include "dynamicwidget.h"
#include "stopwidget.h"
#include "locationmodel.h"
#include "serviceprovidermodel.h"
#include "accessorinfodialog.h"

#include <Plasma/Theme>
#include <Plasma/DataEngineManager>
#include <KColorScheme>
#include <KMessageBox>
#include <KFileDialog>
#include <KStandardDirs>
#include <KLineEdit>
#include <knuminput.h>
#include <kdeversion.h>
#if KDE_VERSION >= KDE_MAKE_VERSION(4,3,80)
	#include <knewstuff3/downloaddialog.h>
#else
	#include <knewstuff2/engine.h>
#endif
#ifdef USE_KCATEGORYVIEW
	#include <KCategorizedSortFilterProxyModel>
	#include <KCategorizedView>
	#include <KCategoryDrawer>
#endif

#include <QMenu>
#include <QListView>
#include <QTimeEdit>
#include <QRadioButton>
#include <QSpinBox>
#include <QStringListModel>
#include <QSortFilterProxyModel>
#include <QVBoxLayout>
#include <QProcess>
#include <QXmlSimpleReader>
#if QT_VERSION >= 0x040600
	#include <QParallelAnimationGroup>
	#include <QPropertyAnimation>
	#include <QGraphicsEffect>
#endif

// Private dialog to show a list of stops near the users current position
class NearStopsDialog : public KDialog
{
public:
	NearStopsDialog( const QString &text, QWidget* parent = 0 ) : KDialog( parent ) {
		setButtons( Ok | Cancel );

		QWidget *w = new QWidget;
		QVBoxLayout *layout = new QVBoxLayout;
		m_label = new QLabel( text, this );
		m_label->setWordWrap( true );
		m_listView = new QListView( this );
		m_listView->setSelectionMode( QAbstractItemView::SingleSelection );
		m_listView->setEditTriggers( QAbstractItemView::NoEditTriggers );
		m_listModel = new QStringListModel( QStringList()
				<< i18nc( "@item:inlistbox", "Please Wait..." ), this );
		m_listView->setModel( m_listModel );
		layout->addWidget( m_label );
		layout->addWidget( m_listView );
		w->setLayout( layout );
		setMainWidget( w );

		m_noItem = true;
	};

	QListView *listView() const {
		return m_listView;
	};
	QString selectedStop() const {
		QModelIndex index = m_listView->currentIndex();
		if ( index.isValid() ) {
			return m_listModel->data( index, Qt::DisplayRole ).toString();
		} else {
			return QString();
		}
	};
	QStringListModel *stopsModel() const {
		return m_listModel;
	};

	void addStops( const QStringList &stops ) {
		if ( m_noItem ) {
			// Remove the "waiting for data..." item
			m_listModel->setStringList( QStringList() );
		}

		QStringList oldStops = m_listModel->stringList();
		QStringList newStops;
		newStops << oldStops;
		foreach( const QString &stop, stops ) {
			if ( !newStops.contains(stop) && !stop.isEmpty() ) {
				newStops << stop;
			}
		}
		newStops.removeDuplicates();

		if ( !newStops.isEmpty() ) {
			if ( m_noItem ) {
				m_noItem = false;
				m_listView->setEnabled( true );
			}
			m_listModel->setStringList( newStops );
			m_listModel->sort( 0 );
		} else if ( m_noItem ) {
			m_listModel->setStringList( oldStops );
		}
	};

	bool hasItems() const {
		return !m_noItem;
	};

private:
	QLabel *m_label;
	QListView *m_listView;
	QStringListModel *m_listModel;
	bool m_noItem;
};

// Private class of StopSettingsDialog
class StopSettingsDialogPrivate
{
	Q_DECLARE_PUBLIC( StopSettingsDialog )
	
public:
	// Constructor with given LocationModel and ServiceProviderModel
	StopSettingsDialogPrivate( const StopSettings &_oldStopSettings,
			StopSettingsDialog::Options _options,
			AccessorInfoDialog::Options _accessorInfoDialogOptions,
			QList<int> customSettings, 
			StopSettingsWidgetFactory::Pointer _factory,
			StopSettingsDialog *q )
			: factory(_factory), detailsWidget(0), stopFinder(0), nearStopsDialog(0), 
			modelLocations(0), modelServiceProviders(0),
			modelLocationServiceProviders(0), dataEngineManager(0), q_ptr(q)
	{
		// Load data engines
		dataEngineManager = Plasma::DataEngineManager::self();
		publicTransportEngine = dataEngineManager->loadEngine("publictransport");
		geolocationEngine = dataEngineManager->loadEngine("geolocation");
		osmEngine = dataEngineManager->loadEngine("openstreetmap");
		
		// Create location and service provider models
		modelLocations = new LocationModel( q );
		modelLocations->syncWithDataEngine( publicTransportEngine );
		modelServiceProviders = new ServiceProviderModel( q );
		modelServiceProviders->syncWithDataEngine( publicTransportEngine,
				dataEngineManager->loadEngine("favicons") );
		
		// Store options and given stop settings
		options = _options;
		settings = customSettings;
		accessorInfoDialogOptions = _accessorInfoDialogOptions;
		oldStopSettings = _oldStopSettings;
		
		// Resolve illegal option/setting combinations
		correctOptions();
		correctSettings();
	};

	~StopSettingsDialogPrivate() {
		if ( dataEngineManager ) {
			dataEngineManager->unloadEngine("publictransport");
			dataEngineManager->unloadEngine("geolocation");
			dataEngineManager->unloadEngine("openstreetmap");
			dataEngineManager->unloadEngine("favicons");
		}
	};
	
	void init( const StopSettings &_oldStopSettings, const QStringList &filterConfigurations ) 
	{
		Q_Q( StopSettingsDialog );
		
		// Setup main UI
		uiStop.setupUi( q->mainWidget() );
		
		// Initialize button flags, later User1 and/or Details are added and setButtons() is called
		KDialog::ButtonCodes buttonFlags = KDialog::Ok | KDialog::Cancel;
		
		// Create details widget only if there are detailed settings in d->settings
		if ( !settings.isEmpty() ) {
			// Add widgets for settings
			QFormLayout *detailsLayout = NULL; // Gets created for the first detailed setting
			foreach ( int setting, settings ) {
				if ( setting <= StopNameSetting ) {
					// Default settings are created in uiStop.setupUi()
					continue;
				}
				
				// Create the widget in the factory and get it's label text
				QWidget *widget = factory->widgetWithNameForSetting( setting, 
						factory->isDetailsSetting(setting) ? detailsWidget : q->mainWidget() );
				QString text = factory->textForSetting( setting );
				
				// Check in the factory if the current setting should be added to a details widget
				if ( factory->isDetailsSetting(setting) ) {
					if ( !detailsLayout ) {
						// Create details widget and layout for the first details setting
						detailsLayout = createDetailsWidget();
						
						// Add a details button to toggle the details section
						buttonFlags |= KDialog::Details;
					}
					
					// Add setting widget to the details section
					detailsLayout->addRow( text, widget );
				} else {
					// Add in default layout
					dynamic_cast<QFormLayout*>( q->mainWidget()->layout() )->addRow( text, widget );
				}
				
				// Insert newly added widget into the hash
				settingsWidgets.insert( setting, widget );
			}
			
			if ( settings.contains(FilterConfigurationSetting) ) {
				KComboBox *filterConfiguration = settingWidget<KComboBox>( FilterConfigurationSetting );
				filterConfiguration->addItems( filterConfigurations );
			}
		}
		
		// Add nearby stops button
		if ( options.testFlag(StopSettingsDialog::ShowNearbyStopsButton) ) {
			buttonFlags |= KDialog::User1;
			q->connect( q, SIGNAL(user1Clicked()), q, SLOT(geolocateClicked()) );
		}
		
		// Set dialog buttons (Ok, Cancel + maybe Details and/or User1)
		q->setButtons( buttonFlags );
		
		// Setup options of the nearby stops button (needs to be called after q->setButtons())
		if ( options.testFlag(StopSettingsDialog::ShowNearbyStopsButton) ) {
			q->setButtonIcon( KDialog::User1, KIcon("tools-wizard") );
			q->setButtonText( KDialog::User1, i18nc("@action:button", "Nearby Stops...") );
		}

		// Show/hide accessor info button
		if ( options.testFlag(StopSettingsDialog::ShowAccessorInfoButton) ) {
			uiStop.btnServiceProviderInfo->setIcon( KIcon("help-about") );
			uiStop.btnServiceProviderInfo->setText( QString() );
			q->connect( uiStop.btnServiceProviderInfo, SIGNAL(clicked()),
						q, SLOT(clickedServiceProviderInfo()) );
		} else {
			uiStop.btnServiceProviderInfo->hide();
		}

		// Show/hide install accessor button
		if ( options.testFlag(StopSettingsDialog::ShowInstallAccessorButton) ) {
			QMenu *menu = new QMenu( q );
			menu->addAction( KIcon("get-hot-new-stuff"), 
							 i18nc("@action:inmenu", "Get new service providers..."),
							 q, SLOT(downloadServiceProvidersClicked()) );
			menu->addAction( KIcon("text-xml"),
							 i18nc("@action:inmenu", "Install new service provider from local file..."),
							 q, SLOT(installServiceProviderClicked()) );
			uiStop.downloadServiceProviders->setMenu( menu );
			uiStop.downloadServiceProviders->setIcon( KIcon("get-hot-new-stuff") );
		} else {
			uiStop.downloadServiceProviders->hide();
		}

		// Create stop list widget
		if ( options.testFlag(StopSettingsDialog::ShowStopInputField) ) {
			// Set dialog title for a dialog with stop name input
			q->setWindowTitle( i18nc("@title:window", "Change Stop(s)") );
		
			// Create stop widgets
			stopList = new DynamicLabeledLineEditList(
					DynamicLabeledLineEditList::RemoveButtonsBesideWidgets,
					DynamicLabeledLineEditList::AddButtonBesideFirstWidget,
					DynamicLabeledLineEditList::NoSeparator, QString(), q );
			stopList->setObjectName( QLatin1String("StopList") );
			stopList->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
			q->connect( stopList, SIGNAL(added(QWidget*)), q, SLOT(stopAdded(QWidget*)) );
			q->connect( stopList, SIGNAL(added(QWidget*)), q, SLOT(adjustStopListLayout()) );
			q->connect( stopList, SIGNAL(removed(QWidget*,int)), q, SLOT(adjustStopListLayout()) );
			
			stopList->setLabelTexts( i18nc("@info/plain Label for the read only text labels containing "
					"additional stop names, which are combined with other defined stops (showing "
					"departures/arrivals of all combined stops)", "Combined Stop") + 
					" %1:", QStringList() << "Stop:" );
			stopList->setWidgetCountRange( 1, 3 );
			if ( stopList->addButton() ) {
				stopList->addButton()->setToolTip( i18nc("@info:tooltip",
						"<subtitle>Add another stop.</subtitle><para>"
						"The departures/arrivals of all stops get combined.</para>") );
			}
			stopList->setWhatsThis( i18nc("@info:whatsthis",
					"<para>All departures/arrivals for these stops get <emphasis strong='1'>"
					"displayed combined</emphasis> in the applet.</para>"
					"<para>To add a stop that doesn't get combined with others use the "
					"<interface>Add Stop</interface> button of the main settings dialog.</para>") );

			QVBoxLayout *l = new QVBoxLayout( uiStop.stops );
			l->setContentsMargins( 0, 0, 0, 0 );
			l->addWidget( stopList );
		} else { // if ( !options.testFlag(StopSettingsDialog::ShowStopInputField) )
			// Set dialog title for a dialog without stop name input
			q->setWindowTitle( i18nc("@title:window", "Change Service Provider") );
			uiStop.stops->hide();
		}
		
		// Create model that filters service providers for the current location
		modelLocationServiceProviders = new QSortFilterProxyModel( q );
		modelLocationServiceProviders->setSourceModel( modelServiceProviders );
		modelLocationServiceProviders->setFilterRole( LocationCodeRole );

		#ifdef USE_KCATEGORYVIEW
			KCategorizedSortFilterProxyModel *modelCategorized = new KCategorizedSortFilterProxyModel( q );
			modelCategorized->setCategorizedModel( true );
			modelCategorized->setSourceModel( modelLocationServiceProviders );

			KCategorizedView *serviceProviderView = new KCategorizedView( q );
			#if KDE_VERSION >= KDE_MAKE_VERSION(4,4,60)
				KCategoryDrawerV3 *categoryDrawer = new KCategoryDrawerV3( serviceProviderView );
				serviceProviderView->setCategorySpacing( 10 );
			#else
				KCategoryDrawerV2 *categoryDrawer = new KCategoryDrawerV2( q );
				serviceProviderView->setCategorySpacing( 10 );
			#endif
			serviceProviderView->setCategoryDrawer( categoryDrawer );
			serviceProviderView->setModel( modelCategorized );
			serviceProviderView->setWordWrap( true );
			serviceProviderView->setSelectionMode( QAbstractItemView::SingleSelection );
			
			// If ScrollPerItem is used the view can't be scrolled in QListView::ListMode.
			serviceProviderView->setVerticalScrollMode( QAbstractItemView::ScrollPerPixel );

			uiStop.serviceProvider->setModel( modelCategorized );
			uiStop.serviceProvider->setView( serviceProviderView );
		#else
			uiStop.serviceProvider->setModel( modelLocationServiceProviders );
		#endif
		uiStop.location->setModel( modelLocations );

		// Set html delegate
		if ( options.testFlag(StopSettingsDialog::UseHtmlForLocationConfig) || 
			options.testFlag(StopSettingsDialog::UseHtmlForServiceProviderConfig) ) 
		{
			htmlDelegate = new HtmlDelegate( HtmlDelegate::AlignTextToDecoration, q );
			if ( options.testFlag(StopSettingsDialog::UseHtmlForLocationConfig) ) {
				uiStop.location->setItemDelegate( htmlDelegate );
			}
			if ( options.testFlag(StopSettingsDialog::UseHtmlForServiceProviderConfig) ) {
				uiStop.serviceProvider->setItemDelegate( htmlDelegate );
			}
		}

		// Watch location and service provider for changes
		q->connect( uiStop.location, SIGNAL(currentIndexChanged(int)),
					q, SLOT(locationChanged(int)) );
		q->connect( uiStop.serviceProvider, SIGNAL(currentIndexChanged(int)),
					q, SLOT(serviceProviderChanged(int)) );
		
		// Watch city/stop name(s) for changes
		if ( options.testFlag(StopSettingsDialog::ShowStopInputField) ) {
			q->connect( uiStop.city, SIGNAL(currentIndexChanged(QString)),
						q, SLOT(cityNameChanged(QString)) );
			q->connect( stopList, SIGNAL(textEdited(QString,int)), 
						q, SLOT(stopNameEdited(QString,int)) );
		}

		// Set values of setting widgets
		q->setStopSettings( _oldStopSettings );
		
		// Set focus to the first stop name if shown.
		// Otherwise set focus to the service provider widget.
		if ( options.testFlag(StopSettingsDialog::ShowStopInputField) ) {
			stopList->lineEditWidgets().first()->setFocus(); // Minimum widget count is 1
		} else {
			uiStop.serviceProvider->setFocus();
		}
	};
	
	inline void correctOptions() {
		// Don't show nearby stops button, if the stop input field isn't shown
		if ( !options.testFlag(StopSettingsDialog::ShowStopInputField) && 
			options.testFlag(StopSettingsDialog::ShowNearbyStopsButton) ) 
		{
			options ^= StopSettingsDialog::ShowNearbyStopsButton;
		}
	};
	
	enum SettingsRule {
		RequiredBy, // The setting is required by the option
		IfAndOnlyIf // The setting should set if the option is set, otherwise the setting should also not be set
	};
	
	// Correct the settings list, ie. add/remove flags to/from settings,
	// if an associated StopSettingsDialog::Option was/wasn't used
	inline void correctSettings() {
		if ( !settings.contains(LocationSetting) ) {
			settings.append( LocationSetting );
		}
		if ( !settings.contains(ServiceProviderSetting) ) {
			settings.append( ServiceProviderSetting );
		}
		applyRule( StopNameSetting, IfAndOnlyIf, StopSettingsDialog::ShowStopInputField );
		applyRule( CitySetting, IfAndOnlyIf, StopSettingsDialog::ShowStopInputField );
		applyRule( FilterConfigurationSetting, IfAndOnlyIf, StopSettingsDialog::ShowFilterConfigurationConfig );
		applyRule( AlarmTimeSetting, IfAndOnlyIf, StopSettingsDialog::ShowAlarmTimeConfig );
		applyRule( FirstDepartureConfigModeSetting, IfAndOnlyIf, StopSettingsDialog::ShowFirstDepartureConfig );
	};
	
	// This function ensures a SettingRule
	void applyRule( StopSetting setting, SettingsRule rule, StopSettingsDialog::Option option ) 
	{
		if ( options.testFlag(option) ) {
			// Ensure associated setting widgets are in the settings list
			if ( (rule == RequiredBy || rule == IfAndOnlyIf) && !settings.contains(setting) ) {
// 				kDebug() << setting << "isn't in the list of settings for option" << option;
				settings.append( setting );
			}
		} else if ( settings.contains(setting) && rule == IfAndOnlyIf ) {
// 			kDebug() << setting << "is in the list of settings, but option" << option << "isn't set";
			
			// Ensure associated setting widgets are NOT in the settings list
			settings.removeOne( setting );
		}
	};
	
	template< class WidgetType >
	WidgetType *settingWidget( int setting ) const 
	{
		// Custom widgets created without use of StopSettingsWidgetFactory
		if ( settingsWidgets.contains(setting) ) {
			return qobject_cast<WidgetType*>( settingsWidgets[setting] );
		}
		
		// Default widgets created by uiStop
		switch ( setting ) {
			case LocationSetting:
				return qobject_cast<WidgetType*>( uiStop.location );
			case ServiceProviderSetting:
				return qobject_cast<WidgetType*>( uiStop.serviceProvider );
			case CitySetting:
				return qobject_cast<WidgetType*>( uiStop.city );
			case StopNameSetting:
				return qobject_cast<WidgetType*>( stopList );
				
			default:
				break; // Do nothing
		}
		
		if ( !factory->isDetailsSetting(setting) ) {
			WidgetType *widget = detailsWidget->findChild< WidgetType* >( 
					factory->nameForSetting(setting) );
			if ( !widget ) {
				kDebug() << "No main widget found for" << static_cast<StopSetting>(setting);
			}
			return widget;
		}
		
		// A widget was requested, which is in the detailsWidget
		if ( !detailsWidget ) {
			kDebug() << "Details widget not created yet, no custom settings. Requested" 
					 << static_cast<StopSetting>(setting);
			return NULL;
		}
		
		// Normal widgets created by StopSettingsWidgetFactory
		WidgetType *widget = detailsWidget->findChild< WidgetType* >( 
				factory->nameForSetting(setting) );
		if ( widget ) {
			return widget;
		}
		
		// Sub radio widgets created by StopSettingsWidgetFactory
		widget = detailsWidget->findChild< WidgetType* >( 
				QLatin1String("radio_") + factory->nameForSetting(setting) );
		
		if ( !widget ) {
			kDebug() << "No widget found for" << static_cast<StopSetting>(setting);
		}
		return widget;
	};
	
	// Creates the details widget if it's not already created and returns it's layout
	QFormLayout *createDetailsWidget() {
		Q_Q( StopSettingsDialog );
		QFormLayout *detailsLayout;
		if ( detailsWidget ) {
			detailsLayout = dynamic_cast<QFormLayout*>( detailsWidget->layout() );
		} else {
			detailsWidget = new QWidget( q );
			detailsLayout = new QFormLayout( detailsWidget );
			detailsLayout->setContentsMargins( 0, 0, 0, 0 );
			
			// Add a line to separate details widgets from other dialog widgets
			QFrame *line = new QFrame( detailsWidget );
			line->setFrameShape( QFrame::HLine );
			line->setFrameShadow( QFrame::Sunken );
			detailsLayout->addRow( line );
			
			q->setDetailsWidget( detailsWidget );
		}
		return detailsLayout;
	};
	
	// data is currently only used for StopSettings::FilterConfigurationSetting, should be a
	// QStringList with the names of the available filter configurations
	QWidget *addSettingWidget( int setting, const QVariant &defaultValue, const QVariant &data ) 
	{
		if ( settings.contains(setting) ) {
			kDebug() << "The setting" << static_cast<StopSetting>(setting) 
					 << "has already been added";
			return settingWidget<QWidget>( setting );
		}
		
		// Ensure that the details widget is created
		createDetailsWidget();
		
		// Create the widget in the factory
		QWidget *widget = factory->widgetWithNameForSetting( setting, detailsWidget );
		
		// Use the data argument
		if ( setting == FilterConfigurationSetting ) {
			if ( data.canConvert(QVariant::StringList) ) {
				KComboBox *filterConfiguration = qobject_cast<KComboBox*>( widget );
				filterConfiguration->addItems( data.toStringList() );
			} else {
				kDebug() << "StopSettings::FilterConfigurationSetting needs a QStringList as data "
							"argument to addSettingWidget(), containing the names of the "
							"available filter configurations.";
			}
		}
		
		// Set the widgets value 
		// (to the value stored in the StopSettings object or to the default value).
		QVariant _value = oldStopSettings.hasSetting(setting)
				? oldStopSettings[setting] : defaultValue;
		factory->setValueOfSetting( widget, setting, _value );
		
		return addSettingWidget( setting, factory->textForSetting(setting), widget );
	};
	
	// Without use of StopSettingsWidgetFactory
	QWidget *addSettingWidget( int setting, const QString &label, QWidget *widget ) 
	{
		if ( settings.contains(setting) ) {
			kDebug() << "The setting" << static_cast<StopSetting>(setting) 
					 << "has already been added";
			widget->hide();
			return settingWidget<QWidget>( setting );
		}
		
		QFormLayout *detailsLayout = createDetailsWidget();
		detailsLayout->addRow( label, widget );
		
		settingsWidgets.insert( setting, widget );
		settings << setting;
		return widget;
	};
	
	QVariant valueFromWidget( int setting ) const 
	{
		return factory->valueOfSetting( settingWidget<QWidget>(setting), setting );
	};
	
	void setValueToWidget( int setting ) 
	{
		factory->setValueOfSetting( settingWidget<QWidget>(setting), 
									setting, oldStopSettings[setting] );
	};

	/** Updates the service provider model by inserting service provider for the
	* current location. */
	void updateServiceProviderModel( int index ) 
	{
		// Filter service providers for the given locationText
		QString locationCode = uiStop.location->itemData( index, LocationCodeRole ).toString();
		if ( locationCode == "showAll" ) {
			modelLocationServiceProviders->setFilterRegExp( QString() );
		} else {
			modelLocationServiceProviders->setFilterRegExp(
				QString( "%1|international|unknown" ).arg( locationCode ) );
		}
	};
	
	QString currentCityValue() const 
	{
		if ( uiStop.city->isEditable() ) {
			return uiStop.city->lineEdit()->text();
		} else {
			return uiStop.city->currentText();
		}
	};
	
	void requestStopSuggestions( const StopSettings &stopSettings, int stopIndex ) 
	{
		Q_Q( StopSettingsDialog );
		if ( !stopSettings[CitySetting].toString().isEmpty() ) { // m_useSeparateCityValue ) {
			publicTransportEngine->connectSource( QString("Stops %1|stop=%2|city=%3")
					.arg(stopSettings[ServiceProviderSetting].toString(), 
						 stopSettings.stop(stopIndex).name,
						 stopSettings[CitySetting].toString()), q );
		} else {
			publicTransportEngine->connectSource( QString("Stops %1|stop=%2")
					.arg(stopSettings[ServiceProviderSetting].toString(), 
						 stopSettings.stop(stopIndex).name), q );
		}
	};
	
	void processStopSuggestions( const Plasma::DataEngine::Data& data ) 
	{
		if ( !options.testFlag(StopSettingsDialog::ShowStopInputField) ) {
			kDebug() << "Can't use stop suggestions without StopSettingsDialog::ShowStopInputField";
			return;
		}
		
		QStringList stops, weightedStops;
		QHash<QString, QVariant> stopToStopWeight;
		bool hasAtLeastOneWeight = false;
		int count = data["count"].toInt();
		for ( int i = 0; i < count; ++i ) {
			QVariant stopData = data.value( QString("stopName %1").arg(i) );
			if ( !stopData.isValid() ) {
				continue;
			}

			QHash<QString, QVariant> dataMap = stopData.toHash();
			QString sStopName = dataMap["stopName"].toString();
			QString sStopID = dataMap["stopID"].toString();
			int stopWeight = dataMap["stopWeight"].toInt();
			stops.append( sStopName );
			stopToStopWeight.insert( sStopName, stopWeight );

			stopToStopID.insert( sStopName, sStopID );
		}

		// Construct weighted stop list for KCompletion
		foreach( const QString &stop, stops ) {
			int stopWeight = stopToStopWeight[ stop ].toInt();
			if ( stopWeight <= 0 ) {
				stopWeight = 0;
			} else {
				hasAtLeastOneWeight = true;
			}

			weightedStops << QString( "%1:%2" ).arg( stop ).arg( stopWeight );
		}

		KLineEdit *stop = stopList->focusedLineEdit();
		if ( stop ) { // One stop edit line has focus
			kDebug() << "Prepare completion object";
			KCompletion *comp = stop->completionObject();
			comp->setIgnoreCase( true );
			if ( hasAtLeastOneWeight ) {
				comp->setOrder( KCompletion::Weighted );
				comp->insertItems( weightedStops );
			} else {
				comp->setOrder( KCompletion::Insertion );
				comp->insertItems( stops );
			}

			// Complete manually, because the completions are requested asynchronously
			stop->doCompletion( stop->text() );
		} else {
			kDebug() << "No stop line edit has focus, discard received stops.";
		}
	};

	Ui::publicTransportStopConfig uiStop;
// 	Ui::stopConfigDetails uiStopDetails; // setupUi gets only called, if a details widget is shown
	
	StopSettingsDialog::Options options;
	AccessorInfoDialog::Options accessorInfoDialogOptions;
	QList<int> settings;

	StopSettingsWidgetFactory::Pointer factory;
	QWidget *detailsWidget;
	QHash<int, QWidget*> settingsWidgets;
	StopFinder *stopFinder; // To find stops near the users current position (geolocation, osm, ...)
	NearStopsDialog *nearStopsDialog;
	QString stopFinderServiceProviderID;

	StopSettings oldStopSettings; // The last given StopSettings object (used to get settings 
								  // values for widgets that aren't shown)
	LocationModel *modelLocations; // Model of locations
	ServiceProviderModel *modelServiceProviders; // Model of service providers
	QSortFilterProxyModel *modelLocationServiceProviders; // Model of service providers for the current location
	HtmlDelegate *htmlDelegate;
	DynamicLabeledLineEditList *stopList;

	Plasma::DataEngineManager *dataEngineManager;
	Plasma::DataEngine *publicTransportEngine;
	Plasma::DataEngine *osmEngine;
	Plasma::DataEngine *geolocationEngine;

	QHash< QString, QVariant > stopToStopID; /**< A hash with stop names as
				* keys and the corresponding stop IDs as values. */

protected:
	StopSettingsDialog* const q_ptr;
};

StopSettingsDialog::StopSettingsDialog( QWidget *parent, const StopSettings &stopSettings, 
		StopSettingsDialog::Options options, AccessorInfoDialog::Options accessorInfoDialogOptions,
		const QStringList &filterConfigurations, const QList<int> &customSettings, 
		StopSettingsWidgetFactory::Pointer factory ) 
		: KDialog(parent),
		d_ptr(new StopSettingsDialogPrivate(stopSettings, 
			options, accessorInfoDialogOptions, customSettings, factory, this))
{
	Q_D( StopSettingsDialog );
	d->init( stopSettings, filterConfigurations );
}

// StopSettingsDialog::StopSettingsDialog( QWidget* parent, const StopSettings& stopSettings, 
// 		const QList<int> &settings, AccessorInfoDialog::Options accessorInfoDialogOptions, 
// 		const QStringList& filterConfigurations, StopSettingsWidgetFactory::Pointer factory )
// 		: KDialog(parent),
// 		d_ptr(new StopSettingsDialogPrivate(stopSettings, 
// 			settings, accessorInfoDialogOptions, factory, this))
// {
// 	Q_D( StopSettingsDialog );
// 	d->init( stopSettings, filterConfigurations );
// }

StopSettingsDialog::~StopSettingsDialog()
{
	delete d_ptr;
}

StopSettingsDialog *StopSettingsDialog::createSimpleAccessorSelectionDialog(
	QWidget* parent, const StopSettings& stopSettings, StopSettingsWidgetFactory::Pointer factory )
{
	return new StopSettingsDialog( parent, stopSettings, SimpleAccessorSelection,
			AccessorInfoDialog::DefaultOptions, QStringList(), QList<int>(), factory );
}

StopSettingsDialog* StopSettingsDialog::createSimpleStopSelectionDialog(
	QWidget* parent, const StopSettings& stopSettings, StopSettingsWidgetFactory::Pointer factory )
{
	return new StopSettingsDialog( parent, stopSettings, SimpleStopSelection,
			AccessorInfoDialog::DefaultOptions, QStringList(), QList<int>(), factory );
}

StopSettingsDialog* StopSettingsDialog::createExtendedStopSelectionDialog(
	QWidget* parent, const StopSettings& stopSettings,  const QStringList &filterConfigurations,
	StopSettingsWidgetFactory::Pointer factory )
{
	return new StopSettingsDialog( parent, stopSettings, ExtendedStopSelection,
			AccessorInfoDialog::DefaultOptions, filterConfigurations, QList<int>(), factory );
}

QWidget* StopSettingsDialog::addSettingWidget( int setting, 
		const QVariant& defaultValue, const QVariant& data )
{
	Q_D( StopSettingsDialog );
	return d->addSettingWidget( setting, defaultValue, data );
}

QWidget* StopSettingsDialog::addSettingWidget( int setting, 
		const QString& label, QWidget* widget )
{
	Q_D( StopSettingsDialog );
	return d->addSettingWidget( setting, label, widget );
}

StopSettingsWidgetFactory::Pointer StopSettingsDialog::factory() const
{
	Q_D( const StopSettingsDialog );
	return d->factory;
}

void StopSettingsDialog::setStopCountRange(int minCount, int maxCount)
{
	Q_D( StopSettingsDialog );
	if ( !d->options.testFlag(ShowStopInputField) ) {
		kDebug() << "Can't set stop count range without StopSettingsDialog::ShowStopInputField";
		return;
	}
	d->stopList->setWidgetCountRange( minCount, maxCount );
}

void StopSettingsDialog::setStopSettings( const StopSettings& stopSettings )
{
	Q_D( StopSettingsDialog );
	d->oldStopSettings = stopSettings;

	// Set location first (because it filters service providers
	QModelIndex locationIndex = d->modelLocations->indexOfLocation(
			stopSettings[LocationSetting].toString().isEmpty() 
			? KGlobal::locale()->country() : stopSettings[LocationSetting].toString() );
	if ( locationIndex.isValid() ) {
		d->uiStop.location->setCurrentIndex( locationIndex.row() );
	} else {
		d->uiStop.location->setCurrentIndex( 0 );
	}

	// Get service provider index
	QModelIndexList indices = d->uiStop.serviceProvider->model()->match(
			d->uiStop.serviceProvider->model()->index(0, 0), ServiceProviderIdRole,
			stopSettings[ServiceProviderSetting].toString(), 1, Qt::MatchFixedString );
	QModelIndex serviceProviderIndex;
	if ( !indices.isEmpty() ) {
		serviceProviderIndex = indices.first();
	} else {
		kDebug() << "Service provider not found" << stopSettings.get<QString>(ServiceProviderSetting);
	}
	
	// Set values of settings to the settings widgets
	foreach ( int setting, d->settings ) {
		switch ( setting ) {
		case LocationSetting: {
			// Already done above
			break;
		}
		case ServiceProviderSetting: {
			if ( serviceProviderIndex.isValid() ) {
				d->uiStop.serviceProvider->setCurrentIndex( serviceProviderIndex.row() );
			}
			break;
		} 
		case StopNameSetting:
			d->stopList->setLineEditTexts( stopSettings.stops() );
			break;
		case CitySetting: {
			if ( !serviceProviderIndex.isValid() ) {
				continue;
			}
			QVariantHash curServiceProviderData = d->uiStop.serviceProvider->model()->data(
					serviceProviderIndex, ServiceProviderDataRole ).toHash();
			if ( curServiceProviderData["useSeparateCityValue"].toBool() ) {
				if ( curServiceProviderData["onlyUseCitiesInList"].toBool() ) {
					d->uiStop.city->setCurrentItem( stopSettings[CitySetting].toString() );
				} else {
					d->uiStop.city->setEditText( stopSettings[CitySetting].toString() );
				}
			} else {
				d->uiStop.city->setCurrentItem( QString() );
			}
			break;
		} 
		case FirstDepartureConfigModeSetting:
			d->setValueToWidget( TimeOffsetOfFirstDepartureSetting );
			d->setValueToWidget( TimeOfFirstDepartureSetting );
			
			d->factory->setValueOfSetting( d->settingWidget<QWidget>(
					FirstDepartureConfigModeSetting), FirstDepartureConfigModeSetting,
					d->oldStopSettings[FirstDepartureConfigModeSetting] );
			break;
		case FilterConfigurationSetting: {
			QString trFilterConfiguration = Global::translateFilterKey(
				stopSettings[FilterConfigurationSetting].toString() );
		
			KComboBox *filterConfiguration = d->settingWidget<KComboBox>( 
					FilterConfigurationSetting );
			if ( filterConfiguration->contains(trFilterConfiguration) ) {
				filterConfiguration->setCurrentItem( trFilterConfiguration );
			}
			break;
		} 
		default:
			// Set the value of a custom setting
			d->setValueToWidget( setting );
			break;
		}
	}
}

StopSettings StopSettingsDialog::stopSettings() const
{
	Q_D( const StopSettingsDialog );
	
	StopSettings stopSettings;
	QVariantHash serviceProviderData = d->uiStop.serviceProvider->itemData(
			d->uiStop.serviceProvider->currentIndex(), ServiceProviderDataRole ).toHash();
	stopSettings.set( ServiceProviderSetting, serviceProviderData["id"].toString() );
	stopSettings.set( LocationSetting, d->uiStop.location->itemData(
			d->uiStop.location->currentIndex(), LocationCodeRole).toString() );

	if ( d->options.testFlag(ShowStopInputField) ) {
		stopSettings.setStops( d->stopList->lineEditTexts() );
		
		if ( serviceProviderData["useSeparateCityValue"].toBool() ) {
			stopSettings.set( CitySetting, d->currentCityValue() );
		}
	} else {
		stopSettings.setStops( d->oldStopSettings.stopList() );
		stopSettings.set( CitySetting, d->oldStopSettings[CitySetting] );
	}
	
	QStringList stopIDs;
	foreach( const QString &stop, stopSettings.stops() ) {
		if ( d->stopToStopID.contains(stop) ) {
			stopSettings.setIdOfStop( stop, d->stopToStopID[stop].toString() );
// 			stopIDs << d->stopToStopID[stop].toString();
		} else if ( d->oldStopSettings.stops().contains(stop) ) {
			int index = d->oldStopSettings.stops().indexOf(stop);
			stopSettings.setIdOfStop( stop, d->oldStopSettings.stop(index).id );
// 			if ( index < d->oldStopSettings.stopIDs().count() ) {
// 				stopIDs << d->oldStopSettings.stopIDs()[index];
// 			} else {
// 				stopIDs << stop; 
// 			}
		} /*else {
			stopIDs << stop;
		}*/
	}
// 	stopSettings.setStopIDs( stopIDs );
	
	if ( d->options.testFlag(ShowFilterConfigurationConfig) ) {
		KComboBox *filterConfiguration = d->settingWidget<KComboBox>( 
				FilterConfigurationSetting );
		Q_ASSERT_X( filterConfiguration, "StopSettingsDialogPrivate::init", 
					"No KComboBox with name \"filterConfiguration\" found." );
		stopSettings.set( FilterConfigurationSetting,
				Global::untranslateFilterKey(filterConfiguration->currentText()) );
	} else if ( d->oldStopSettings.hasSetting(FilterConfigurationSetting) ) {
		stopSettings.set( FilterConfigurationSetting,
				d->oldStopSettings[FilterConfigurationSetting].toString() );
	}
	
	if ( d->options.testFlag(ShowFirstDepartureConfig) ) {
		stopSettings.set( TimeOffsetOfFirstDepartureSetting,
				d->valueFromWidget(TimeOffsetOfFirstDepartureSetting) );
		stopSettings.set( TimeOfFirstDepartureSetting,
				d->valueFromWidget(TimeOfFirstDepartureSetting) );
		stopSettings.set( FirstDepartureConfigModeSetting,
				d->valueFromWidget(FirstDepartureConfigModeSetting) );
	} else {
		if ( d->oldStopSettings.hasSetting(TimeOffsetOfFirstDepartureSetting) ) {
			stopSettings.set( TimeOffsetOfFirstDepartureSetting,
					d->oldStopSettings[TimeOffsetOfFirstDepartureSetting].toInt() );
		}
		if ( d->oldStopSettings.hasSetting(TimeOfFirstDepartureSetting) ) {
			stopSettings.set( TimeOfFirstDepartureSetting,
					d->oldStopSettings[TimeOfFirstDepartureSetting].toTime() );
		}
		if ( d->oldStopSettings.hasSetting(FirstDepartureConfigModeSetting) ) {
			stopSettings.set( FirstDepartureConfigModeSetting, 
					d->oldStopSettings[FirstDepartureConfigModeSetting].toInt() );
		}
	}
	
	if ( d->options.testFlag(ShowAlarmTimeConfig) ) {
		QSpinBox *alarmTime = d->settingWidget<QSpinBox>( AlarmTimeSetting );
		Q_ASSERT_X( alarmTime, "StopSettingsDialogPrivate::init", 
					"No QSpinBox for AlarmTimeSetting found." );
		stopSettings.set( AlarmTimeSetting, alarmTime->value() );
	} else if ( d->oldStopSettings.hasSetting(AlarmTimeSetting) ) {
		stopSettings.set( AlarmTimeSetting,
				d->oldStopSettings[AlarmTimeSetting].toInt() );
	}
	
	// Add settings of other settings widgets
	for ( QHash<int, QWidget*>::const_iterator it = d->settingsWidgets.constBegin();
		 it != d->settingsWidgets.constEnd(); ++it )
	{
// 		kDebug() << "Extended widget setting" << it.key() 
// 				 << d->factory->valueOfSetting(it.value(), it.key()) << it.value();
		stopSettings.set( it.key(), d->factory->valueOfSetting(it.value(), it.key()) );
	}

	return stopSettings;
}

void StopSettingsDialog::geolocateClicked()
{
	Q_D( StopSettingsDialog );
	d->stopFinder = new StopFinder( StopFinder::ValidatedStopNamesFromOSM,
									d->publicTransportEngine, d->osmEngine, d->geolocationEngine,
									25, StopFinder::DeleteWhenFinished, this );
	connect( d->stopFinder, SIGNAL(geolocationData(QString,QString,qreal,qreal,int)),
			 this, SLOT(stopFinderGeolocationData(QString,QString,qreal,qreal,int)) );
	connect( d->stopFinder, SIGNAL(error(StopFinder::Error,QString)),
			 this, SLOT(stopFinderError(StopFinder::Error,QString)) );
	connect( d->stopFinder, SIGNAL(finished()), this, SLOT(stopFinderFinished()) );
	connect( d->stopFinder, SIGNAL(stopsFound(QStringList,QStringList,QString)),
			 this, SLOT(stopFinderFoundStops(QStringList,QStringList,QString)) );

	d->stopFinder->start();
}

void StopSettingsDialog::stopFinderError( StopFinder::Error /*error*/, const QString& errorMessage )
{
	Q_D( StopSettingsDialog );
	if ( d->nearStopsDialog ) {
		d->nearStopsDialog->close();
		d->nearStopsDialog = NULL;

		KMessageBox::information( this, errorMessage );
	}
}

void StopSettingsDialog::stopFinderFinished()
{
	Q_D( StopSettingsDialog );
	d->stopFinder = NULL; // Deletes itself when finished

	// Close dialog and show info if no stops could be found
	if ( d->nearStopsDialog && !d->nearStopsDialog->hasItems() ) {
		d->nearStopsDialog->close();
		d->nearStopsDialog = NULL;

		// Get data from the geolocation data engine
		Plasma::DataEngine::Data dataGeo = d->geolocationEngine->query( "location" );
		QString country = dataGeo["country code"].toString().toLower();
		QString city = dataGeo["city"].toString();

		KMessageBox::information( this,
				i18nc("@info", "No stop could be found for your current position (%2 in %1).\n"
				"<note>This doesn't mean that there is no public transport "
				"stop near you. Try setting the stop name manually.</note>",
				KGlobal::locale()->countryCodeToName( country ), city) );
	}
}

void StopSettingsDialog::stopFinderFoundStops( const QStringList& stops,
        const QStringList& stopIDs, const QString &serviceProviderID )
{
	Q_D( StopSettingsDialog );
	for ( int i = 0; i < qMin(stops.count(), stopIDs.count()); ++i ) {
		d->stopToStopID.insert( stops[i], stopIDs[i] );
	}
	d->stopFinderServiceProviderID = serviceProviderID;

	if ( d->nearStopsDialog ) {
		d->nearStopsDialog->addStops( stops );
	}
}

void StopSettingsDialog::stopFinderGeolocationData( const QString& countryCode,
        const QString& city, qreal /*latitude*/, qreal /*longitude*/, int accuracy )
{
	Q_D( StopSettingsDialog );
	d->nearStopsDialog = new NearStopsDialog( accuracy > 10000
			? i18nc("@info", "These stops <emphasis strong='1'>may</emphasis> be near you, "
							 "but your position couldn't be determined exactly (city: %1, "
							 "country: %2). Choose one of them or cancel.",
							 city, KGlobal::locale()->countryCodeToName(countryCode))
			: i18nc("@info", "These stops have been found to be near you (city: %1, "
							 "country: %2). Choose one of them or cancel.",
							 city, KGlobal::locale()->countryCodeToName(countryCode)),
			this );
	d->nearStopsDialog->setModal( true );
	d->nearStopsDialog->listView()->setDisabled( true );
	connect( d->nearStopsDialog, SIGNAL(finished(int)), this, SLOT(nearStopsDialogFinished(int)) );
	d->nearStopsDialog->show();
}

void StopSettingsDialog::nearStopsDialogFinished( int result )
{
	Q_D( StopSettingsDialog );
	if ( result == KDialog::Accepted ) {
		QString stop = d->nearStopsDialog->selectedStop();
		d->stopFinder->deleteLater();
		d->stopFinder = NULL;

		if ( stop.isNull() ) {
			kDebug() << "No stop selected";
		} else {
			StopSettings settings = stopSettings();
			Plasma::DataEngine::Data geoData = d->geolocationEngine->query( "location" );
			settings.set( CitySetting, geoData["city"].toString() );
			settings.set( LocationSetting, geoData["country code"].toString() );
			settings.set( ServiceProviderSetting, d->stopFinderServiceProviderID );
			if ( d->stopToStopID.contains(stop) ) {
				settings.setStop( Stop(stop, d->stopToStopID[stop].toString()) );
			} else {
				settings.setStop( stop );
			}
// 			settings.setStop( stop );
// 			if ( d->stopToStopID.contains(stop) ) {
// 				settings.setStopID( d->stopToStopID[stop].toString() );
// 			} else {
// 				settings.setStopIDs( QStringList() );
// 			}
			setStopSettings( settings );
		}
	}

//     delete m_nearStopsDialog; // causes a crash (already deleted..?)
	d->nearStopsDialog = NULL;
}

void StopSettingsDialog::accept()
{
	Q_D( StopSettingsDialog );
	if ( d->options.testFlag(ShowStopInputField) ) {
		d->stopList->removeEmptyLineEdits();

		QStringList stops = d->stopList->lineEditTexts();
		int indexOfFirstEmpty = stops.indexOf( QString() );
		if ( indexOfFirstEmpty != -1 ) {
			KMessageBox::information( this, i18nc( "@info", "Empty stop names are not allowed." ) );
			d->stopList->lineEditWidgets()[ indexOfFirstEmpty ]->setFocus();
		} else {
			KDialog::accept();
		}
	} else {
		KDialog::accept();
	}
}

void StopSettingsDialog::resizeEvent( QResizeEvent *event )
{
	KDialog::resizeEvent( event );
	adjustStopListLayout();
}

void StopSettingsDialog::adjustStopListLayout()
{
	Q_D( StopSettingsDialog );
	if ( !d->options.testFlag(ShowStopInputField) ) {
		return;
	}
	
	// Align elements in m_stopList with the main dialog's layout (at least a bit ;))
	QWidgetList labelList = QWidgetList() << d->uiStop.lblLocation
			<< d->uiStop.lblServiceProvider << d->uiStop.lblCity;
	int maxLabelWidth = 0;
	foreach( QWidget *w, labelList ) {
		if ( w->width() > maxLabelWidth ) {
			maxLabelWidth = w->width();
		}
	}
	QLabel *label = d->stopList->labelFor( d->stopList->lineEditWidgets().first() );
	label->setMinimumWidth( maxLabelWidth );
	label->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
}

void StopSettingsDialog::stopAdded( QWidget *lineEdit )
{
	// Enable completer for new line edits
	KLineEdit *edit = qobject_cast< KLineEdit* >( lineEdit );
	edit->setCompletionMode( KGlobalSettings::CompletionPopup );
}

void StopSettingsDialog::stopNameEdited( const QString&, int widgetIndex )
{
	Q_D( StopSettingsDialog );
	d->requestStopSuggestions( stopSettings(), widgetIndex );
}

void StopSettingsDialog::dataUpdated( const QString& sourceName,
                                      const Plasma::DataEngine::Data& data )
{
	Q_D( StopSettingsDialog );
	if ( sourceName.startsWith(QLatin1String("Stops")) ) {
		// Stop suggestions data
		if ( data.value("error").toBool() ) {
			kDebug() << "Stop suggestions error";
			// TODO: Handle error somehow?
		} else if ( data.value("receivedPossibleStopList").toBool() ) {
			d->processStopSuggestions( data );
		}
	}
}

void StopSettingsDialog::locationChanged( int index )
{
	Q_D( StopSettingsDialog );
	
	d->updateServiceProviderModel( index );

	// Select default accessor of the selected location
	QString locationCode = d->uiStop.location->itemData( index, LocationCodeRole ).toString();
	Plasma::DataEngine::Data locationData = d->publicTransportEngine->query( "Locations" );
	QString defaultServiceProviderId =
	    locationData[locationCode].toHash()["defaultAccessor"].toString();
	if ( !defaultServiceProviderId.isEmpty() ) {
		QModelIndexList indices = d->uiStop.serviceProvider->model()->match(
				d->uiStop.serviceProvider->model()->index(0, 0), ServiceProviderIdRole,
				defaultServiceProviderId, 1, Qt::MatchFixedString );
		if ( !indices.isEmpty() ) {
			int curServiceProviderIndex = indices.first().row();
			d->uiStop.serviceProvider->setCurrentIndex( curServiceProviderIndex );
			serviceProviderChanged( curServiceProviderIndex );
		}
	}
}

void StopSettingsDialog::serviceProviderChanged( int index )
{
	Q_D( StopSettingsDialog );
	QVariantHash serviceProviderData = d->uiStop.serviceProvider->model()->index( index, 0 )
			.data( ServiceProviderDataRole ).toHash();

// TODO: Show warning message in main config dialog, if not all selected stops support arrivals
//     bool supportsArrivals = serviceProviderData["features"].toStringList().contains("Arrivals");
//     m_ui.showArrivals->setEnabled( supportsArrivals );
//     if ( !supportsArrivals )
// 	m_ui.showDepartures->setChecked( true );

	if ( d->options.testFlag(ShowStopInputField) ) {
		bool useSeparateCityValue = serviceProviderData["useSeparateCityValue"].toBool();
		if ( useSeparateCityValue ) {
			d->uiStop.city->clear();
			QStringList cities = serviceProviderData["cities"].toStringList();
			if ( !cities.isEmpty() ) {
				cities.sort();
				d->uiStop.city->addItems( cities );
				d->uiStop.city->setEditText( cities.first() );
			}
			d->uiStop.city->setEditable( !serviceProviderData["onlyUseCitiesInList"].toBool() );
		} else {
			d->uiStop.city->setEditText( QString() );
		}
		d->uiStop.lblCity->setVisible( useSeparateCityValue );
		d->uiStop.city->setVisible( useSeparateCityValue );
	}
}

void StopSettingsDialog::cityNameChanged( const QString& /*cityName*/ )
{
//     QVariantHash serviceProviderData = m_uiStop.serviceProvider->model()->index(
// 	    m_uiStop.serviceProvider->currentIndex(), 0 )
// 	    .data( ServiceProviderDataRole ).toHash();
//     bool useSeparateCityValue = serviceProviderData["useSeparateCityValue"].toBool();
//     QString serviceProviderID = serviceProviderData["id"].toString();
//     if ( !useSeparateCityValue )
// 	return; // City value not used by service provider

//     QString testSource = QString("Stops %1|stop=%2|city=%3").arg( serviceProviderID )
// 	    .arg( m_uiStop.stop->text() ).arg( cityName );
//     m_dataSourceTester->setTestSource( testSource ); TODO
}

void StopSettingsDialog::clickedServiceProviderInfo()
{
	Q_D( const StopSettingsDialog );
	QVariantHash serviceProviderData = d->uiStop.serviceProvider->model()->index(
	                                       d->uiStop.serviceProvider->currentIndex(), 0 )
	                                   .data( ServiceProviderDataRole ).toHash();
	AccessorInfoDialog *infoDialog = new AccessorInfoDialog( serviceProviderData,
			d->uiStop.serviceProvider->itemIcon(d->uiStop.serviceProvider->currentIndex()), 
			d->accessorInfoDialogOptions, this );
	infoDialog->show();
}

void StopSettingsDialog::downloadServiceProvidersClicked( )
{	
	if ( KMessageBox::warningContinueCancel( this,
				i18nc( "@info", "The downloading may currently not work as expected, sorry." ) )
				== KMessageBox::Cancel ) {
		return;
	}

#if KDE_VERSION >= KDE_MAKE_VERSION(4,3,80)
	KNS3::DownloadDialog *dialog = new KNS3::DownloadDialog( "publictransport.knsrc", this );
	dialog->exec();
	kDebug() << "KNS3 Results: " << dialog->changedEntries().count();

	KNS3::Entry::List installed = dialog->installedEntries();
	foreach( const KNS3::Entry &entry, installed )
	kDebug() << entry.name() << entry.installedFiles();

//     if ( !dialog->changedEntries().isEmpty() )
// 	currentServiceProviderIndex();

	delete dialog;
#else
	Q_D( StopSettingsDialog );
	
	KNS::Engine engine( this );
	if ( engine.init( "publictransport2.knsrc" ) ) {
		KNS::Entry::List entries = engine.downloadDialogModal( this );

		kDebug() << entries.count();
		if ( entries.size() > 0 ) {
			foreach( KNS::Entry *entry, entries ) {
				// Downloaded file has the name "hotstuff-access" which is wrong (maybe it works
				// better with archives). So rename the file to the right name from the payload:
				QString filename = entry->payload().representation()
				                   .remove( QRegExp( "^.*\\?file=" ) ).remove( QRegExp( "&site=.*$" ) );
				QStringList installedFiles = entry->installedFiles();

				kDebug() << "installedFiles =" << installedFiles;
				if ( !installedFiles.isEmpty() ) {
					QString installedFile = installedFiles[0];

					QString path = KUrl( installedFile ).path().remove( QRegExp( "/[^/]*$" ) ) + '/';
					QFile( installedFile ).rename( path + filename );

					kDebug() << "Rename" << installedFile << "to" << path + filename;
				}
			}

			// Get a list of with the location of each service provider (locations can be contained multiple times)
// 	    Plasma::DataEngine::Data serviceProviderData =
// 		    m_publicTransportEngine->query("ServiceProviders");
			// TODO: Update "ServiceProviders"-data source in the data engine.
			// TODO: Update country list (group titles in the combo box)
// 	    foreach ( QString serviceProviderName, serviceProviderData.keys() )  {
// 		QHash< QString, QVariant > curServiceProviderData = m_serviceProviderData.value(serviceProviderName).toHash();
// 		countries << curServiceProviderData["country"].toString();
// 	    }
			d->updateServiceProviderModel();
		}
	}
#endif
}

// An XML content handler that only looks for <scriptFile>-tags
class Handler : public QXmlContentHandler {
public:
	Handler() {
		m_isInScriptTag = false;
	};
	
	QString scriptFile() const { return m_scriptFile; };
	
    virtual bool startDocument() { return true; };
    virtual bool endDocument() { return true; };
    virtual bool characters( const QString& ch ) {
		if ( m_isInScriptTag ) {
			kDebug() << "SCRIPT CONTENT:" << ch;
			m_scriptFile = ch;
		}
		return true;
	};
	
    virtual bool startElement( const QString& namespaceURI, const QString& localName, 
							   const QString& qName, const QXmlAttributes& atts ) {
		Q_UNUSED( namespaceURI )
		Q_UNUSED( localName )
		Q_UNUSED( atts )
		if ( !m_isInScriptTag && qName.compare(QLatin1String("script"), Qt::CaseInsensitive) == 0 ) {
			m_isInScriptTag = true;
		}
		return true;
	};
	
    virtual bool endElement( const QString& namespaceURI, const QString& localName, 
							 const QString& qName ) {
		Q_UNUSED( namespaceURI )
		Q_UNUSED( localName )
		if ( m_isInScriptTag && qName.compare(QLatin1String("script"), Qt::CaseInsensitive) == 0 ) {
			m_isInScriptTag = false;
		}
		return true;
	};
	
    virtual QString errorString() const { return QString(); };
    virtual bool startPrefixMapping( const QString&, const QString& ) { return true; };
    virtual bool endPrefixMapping( const QString& ) { return true; };
    virtual bool ignorableWhitespace( const QString& ) { return true; };
    virtual bool processingInstruction( const QString&, const QString& ) { return true; };
    virtual void setDocumentLocator( QXmlLocator* ) {};
    virtual bool skippedEntity( const QString& ) { return true; };
	
private:
	bool m_isInScriptTag;
	QString m_scriptFile;
};

void StopSettingsDialog::installServiceProviderClicked()
{
	QString fileName = KFileDialog::getOpenFileName( KUrl(), "*.xml", this );
	if ( !fileName.isEmpty() ) {
		QStringList dirs = KGlobal::dirs()->findDirs( "data",
		                   "plasma_engine_publictransport/accessorInfos/" );
		if ( dirs.isEmpty() ) {
			return;
		}

		QFile file( fileName );
		QFileInfo fi( file );
		QString sourceDir = fi.dir().path() + '/';
		QString targetDir = dirs[0]; // First is a local path in ~/.kde4/share/...
		QString targetFileName = targetDir + fi.fileName();
		
		// Read XML file for a script file
		QXmlSimpleReader reader;
		QXmlInputSource *source = new QXmlInputSource( &file );
		
		Handler *handler = new Handler;
		reader.setContentHandler( handler );
		bool ok = reader.parse( source );
		if ( !ok || handler->scriptFile().isEmpty() ) {
			int result = KMessageBox::warningContinueCancel( this, i18nc("@info", 
					"The XML file couldn't be read to look for an associated script file "
					"or the script-tag is empty (wrong XML file). "
					"If the accessor uses a script file for parsing it may not work.") );
			if ( result == KMessageBox::Cancel ) {
				delete handler;
				return;
			}
		} else if ( !QFile::exists(sourceDir + handler->scriptFile()) ) {
			int result = KMessageBox::warningContinueCancel( this, i18nc("@info", 
					"The script file referenced in the XML file couldn't be found: "
					"<filename>%1</filename>. "
					"If the accessor uses a script file for parsing it may not work.", 
					sourceDir + handler->scriptFile()) );
			if ( result == KMessageBox::Cancel ) {
				delete handler;
				return;
			}
		} else {
			QString scriptFileName = handler->scriptFile();
			QString targetScriptFileName = targetDir + scriptFileName;
			if ( QFile::exists(targetScriptFileName) ) {
				int result = KMessageBox::warningYesNo( this,
						i18nc("@info", "The file <filename>%1</filename> already exists. "
									   "Do you want to overwrite it?", targetScriptFileName),
						i18nc("@title:window", "Overwrite") );
				
				if ( result == KMessageBox::Yes ) {
					// "Overwrite" file by first removing it here and copying the new file over it
					QFile::remove( targetScriptFileName );
				}
			}
			
			QFile::copy( sourceDir + scriptFileName, targetScriptFileName );
		}
		
		delete handler;
		
		if ( QFile::exists(targetFileName) ) {
			int result = KMessageBox::warningYesNoCancel( this,
					i18nc("@info", "The file <filename>%1</filename> already exists. "
								   "Do you want to overwrite it?", targetFileName),
					i18nc("@title:window", "Overwrite") );
			
			if ( result == KMessageBox::Cancel ) {
				return;
			} else if ( result == KMessageBox::Yes ) {
				// "Overwrite" file by first removing it here and copying the new file over it
				QFile::remove( targetFileName );
			}
		}
		
		kDebug() << "PublicTransportSettings::installServiceProviderClicked"
				 << "Install file" << fileName << "to" << targetDir;
		file.copy( targetFileName );
// 		QProcess::execute( "kdesu", QStringList() << QString( "cp %1 %2" ).arg( fileName ).arg( targetDir ) );
	}
}

QDebug& operator<<( QDebug debug, StopSettingsDialog::Option option )
{
	switch ( option ) {
		case StopSettingsDialog::NoOption:
			return debug << "NoOption";
		case StopSettingsDialog::ShowStopInputField:
			return debug << "ShowStopInputField";
		case StopSettingsDialog::ShowNearbyStopsButton:
			return debug << "ShowNearbyStopsButton";
		case StopSettingsDialog::ShowAccessorInfoButton:
			return debug << "ShowAccessorInfoButton";
		case StopSettingsDialog::ShowInstallAccessorButton:
			return debug << "ShowInstallAccessorButton";
		case StopSettingsDialog::ShowFilterConfigurationConfig:
			return debug << "ShowFilterConfigurationConfig";
		case StopSettingsDialog::ShowAlarmTimeConfig:
			return debug << "ShowAlarmTimeConfig";
		case StopSettingsDialog::ShowFirstDepartureConfig:
			return debug << "ShowFirstDepartureConfig";
		case StopSettingsDialog::UseHtmlForLocationConfig:
			return debug << "UseHtmlForLocationConfig";
		case StopSettingsDialog::UseHtmlForServiceProviderConfig:
			return debug << "UseHtmlForServiceProviderConfig";
		case StopSettingsDialog::UseHtmlEverywhere:
			return debug << "UseHtmlEverywhere";
		case StopSettingsDialog::ShowAllDetailsWidgets:
			return debug << "ShowAllDetailsWidgets";
		case StopSettingsDialog::SimpleAccessorSelection:
			return debug << "SimpleAccessorSelection";
		case StopSettingsDialog::SimpleStopSelection:
			return debug << "SimpleStopSelection";
		case StopSettingsDialog::ExtendedStopSelection:
			return debug << "ExtendedStopSelection";
				
		default:
			return debug << "Option unknown" << option;
	}
}