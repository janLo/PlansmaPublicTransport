/*
 *   Copyright 2011 Friedrich Pülz <fpuelz@gmx.de>
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
#include "serviceprovidergtfs.h"

// Own includes
#include "serviceproviderdata.h"
#include "serviceproviderglobal.h"
#include "departureinfo.h"
#include "gtfsservice.h"
#include "gtfsrealtime.h"
#include "request.h"

// KDE includes
#include <Plasma/Service>
#include <KTimeZone>
#include <KDateTime>
#include <KStandardDirs>
#include <KDebug>
#include <KTemporaryFile>
#include <KLocalizedString>
#include <KLocale>
#include <KCurrencyCode>
#include <KConfigGroup>
#include <KIO/Job>

// Qt includes
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QFileInfo>
#include <QEventLoop>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>

const qreal ServiceProviderGtfs::PROGRESS_PART_FOR_FEED_DOWNLOAD = 0.1;

ServiceProviderGtfs::ServiceProviderGtfs(
        const ServiceProviderData *data, QObject *parent, const QSharedPointer<KConfig> &cache )
        : ServiceProvider(data, parent, cache), m_service(0)
#ifdef BUILD_GTFS_REALTIME
          , m_tripUpdates(0), m_alerts(0)
#endif
{
    m_state = Initializing;

    QString errorText;
    if ( !GeneralTransitFeedDatabase::initDatabase(data->id(), &errorText) ) {
        kDebug() << "Error initializing the database" << errorText;
        m_state = ErrorInDatabase;
        return;
    }

    // Read provider information cache
    KConfig config( ServiceProviderGlobal::cacheFileName(), KConfig::SimpleConfig );
    KConfigGroup group = config.group( data->id() );
    KConfigGroup gtfsGroup = group.group( "gtfs" );
    bool importFinished = gtfsGroup.readEntry( "feedImportFinished", false );

    QFileInfo fi( GeneralTransitFeedDatabase::databasePath(data->id()) );
    if ( importFinished ) {
        if ( fi.exists() && fi.size() > 10000 ) {
            // Load agency information from database and request GTFS-realtime data
            loadAgencyInformation();
#ifdef BUILD_GTFS_REALTIME
            updateRealtimeData();
#endif
            m_state = Ready;
        } else {
            // Provider cache says the import has been finished, but the database file does not
            // exist or is empty
            gtfsGroup.writeEntry( "feedImportFinished", false );
        }
    }

    // Update database to the current version of the GTFS feed
    updateGtfsData();
}

ServiceProviderGtfs::~ServiceProviderGtfs()
{
    // Free all agency objects
    qDeleteAll( m_agencyCache );

    // Free waiting request objects
    for ( QHash<QString,AbstractRequest*>::ConstIterator it = m_waitingRequests.constBegin();
            it != m_waitingRequests.constEnd(); ++it )
    {
        delete *it;
    }
    m_waitingRequests.clear();

#ifdef BUILD_GTFS_REALTIME
    delete m_tripUpdates;
    delete m_alerts;
#endif
}

bool ServiceProviderGtfs::isTestResultUnchanged( const QString &providerId,
                                                 const QSharedPointer< KConfig > &cache )
{
    // Check if the GTFS feed was modified since the cache was last updated
    const KConfigGroup group = cache->group( providerId );
    if ( !group.hasGroup("gtfs") ) {
        // Not a GTFS provider or modified time not stored yet
        return true;
    }

    const KConfigGroup gtfsGroup = group.group( "gtfs" );
    const KDateTime lastFeedModifiedTime = KDateTime::fromString(
            gtfsGroup.readEntry("feedModifiedTime", QString()) );
    const QString feedUrl = gtfsGroup.readEntry( "feedUrl", QString() );

    QNetworkAccessManager *manager = new QNetworkAccessManager();
    QNetworkRequest request( feedUrl );
    QNetworkReply *reply = manager->head( request );
    const QTime start = QTime::currentTime();

    // Use an event loop to wait for execution of the request
    const int networkTimeout = 1000;
    QEventLoop eventLoop;
    connect( reply, SIGNAL(finished()), &eventLoop, SLOT(quit()) );
    QTimer::singleShot( networkTimeout, &eventLoop, SLOT(quit()) );
    eventLoop.exec();

    // Check if the timeout occured before the request finished
    if ( reply->isRunning() ) {
        kDebug() << "Destroyed or timeout while downloading head of" << feedUrl;
        reply->abort();
        reply->deleteLater();
        manager->deleteLater();
        return false;
    }

    const int time = start.msecsTo( QTime::currentTime() );
    kDebug() << "Waited" << ( time / 1000.0 ) << "seconds for download of" << feedUrl;

    // Read all data, decode it and give it to the script
    const KDateTime feedModifiedTime = KDateTime::fromString(
            reply->header(QNetworkRequest::LastModifiedHeader).toString() ).toUtc();
    reply->deleteLater();
    manager->deleteLater();

    return feedModifiedTime == lastFeedModifiedTime;
}

bool ServiceProviderGtfs::isTestResultUnchanged( const QSharedPointer<KConfig> &cache ) const
{
    return isTestResultUnchanged( id(), cache );
}

bool ServiceProviderGtfs::runTests( QString *errorMessage ) const
{
    if ( m_state == Ready ) {
        // The GTFS feed was successfully imported
        return true;
    }

    if ( hasErrors(errorMessage) ) {
        return false;
    }

    const KUrl feedUrl( m_data->feedUrl() );
    if ( feedUrl.isEmpty() || !feedUrl.isValid() ) {
        if ( errorMessage ) {
            *errorMessage = i18nc("@info/plain", "Invalid GTFS feed URL: %1", m_data->feedUrl());
        }
        return false;
    }

    // No errors found
    return true;
}

bool ServiceProviderGtfs::hasErrors( QString *errorMessage ) const
{
    // ErrorNeedsFeedImport is an error that can easily be solved by downloading the feed,
    // therefore it should not make the provider invalid
    if ( m_state >= 10 && m_state != ErrorNeedsFeedImport ) {
        if ( errorMessage ) {
            *errorMessage = errorMessageForErrorState( m_state );
        }
        return true;
    } else {
        return false;
    }
}

void ServiceProviderGtfs::updateGtfsData()
{
    if ( m_service ) {
        kDebug() << "Is already updating, please wait";
        return;
    }

    if ( m_state == Initializing ) {
        // Called from constructor without imported GTFS feed,
        // do not update, let the user first import the feed explicitly
        m_state = ErrorNeedsFeedImport;
        return;
    } else if ( m_state != Ready ) {
        // Set state to UpdateGtfsFeed, if state was Ready,
        // ie. if the database was already imported and only gets updated now
        m_state = UpdatingGtfsFeed;
        kDebug() << "Updating GTFS database for" << m_data->id() << " please wait";
    } else {
        kDebug() << "Stays ready, updates GTFS database in background";
    }

    m_progress = 0.0;
    m_service = new GtfsService( QString(), this );
    KConfigGroup op = m_service->operationDescription("updateGtfsFeed");
    op.writeEntry( "serviceProviderId", m_data->id() );
    Plasma::ServiceJob *job = m_service->startOperationCall( op );
    connect( job, SIGNAL(result(KJob*)), this, SLOT(importFinished(KJob*)) );
    connect( job, SIGNAL(percent(KJob*,ulong)), this, SLOT(importProgress(KJob*,ulong)) );
}

void ServiceProviderGtfs::importProgress( KJob *job, ulong percent )
{
    Q_UNUSED( job );
    kDebug() << percent << m_data->id() << m_state;
    m_progress = percent / 100.0;
    for ( QHash<QString,AbstractRequest*>::ConstIterator it = m_waitingRequests.constBegin();
          it != m_waitingRequests.constEnd(); ++it )
    {
        emit progress( this, m_progress, i18nc("@info/plain TODO", "Importing GTFS feed"),
                       m_data->feedUrl(), *it );
    }
}

void ServiceProviderGtfs::importFinished( KJob *job )
{
    m_progress = 1.0;
    if ( job->error() != 0 ) {
        // Error while importing
        kDebug() << "ERROR" << m_data->id() << job->errorString();
        ErrorCode errorCode = job->error() == -7 ? ErrorNeedsImport : ErrorDownloadFailed;
        m_state = job->error() == -7 ? ErrorNeedsFeedImport : ErrorReadingFeed;
        for ( QHash<QString,AbstractRequest*>::ConstIterator it = m_waitingRequests.constBegin();
              it != m_waitingRequests.constEnd(); ++it )
        {
            emit errorParsing( this, errorCode, job->errorString(), m_data->feedUrl(), *it );
            delete *it;
        }

//         updateSourceFileValidity(); TODO!
    } else {
        // Succesfully updated GTFS database
        kDebug() << "GTFS feed updated successfully" << m_data->id();
        m_state = Ready;
//         updateSourceFileValidity(); TODO!

        for ( QHash<QString,AbstractRequest*>::ConstIterator it = m_waitingRequests.constBegin();
              it != m_waitingRequests.constEnd(); ++it )
        {
            if ( (*it)->parseMode == ParseForDepartures ) {
                DepartureRequest *request = static_cast<DepartureRequest*>( *it );
                Q_ASSERT( request );
                requestDepartures( *request );
            } else if ( (*it)->parseMode == ParseForArrivals ) {
                ArrivalRequest *request = static_cast<ArrivalRequest*>( *it );
                Q_ASSERT( request );
                requestArrivals( *request );
            } else if ( (*it)->parseMode == ParseForStopSuggestions ) {
                StopSuggestionRequest *request = static_cast<StopSuggestionRequest*>( *it );
                Q_ASSERT( request );
                requestStopSuggestions( *request );
            } else {
                kDebug() << "Finished updating GTFS database, but unknown parse mode in a "
                            "waiting source" << (*it)->parseMode;
            }
            delete *it;
        }
    }

    m_waitingRequests.clear();
    m_service->deleteLater();
    m_service = 0;
}

#ifdef BUILD_GTFS_REALTIME
bool ServiceProviderGtfs::isRealtimeDataAvailable() const
{
    return !m_data->realtimeTripUpdateUrl().isEmpty() || !m_data->realtimeAlertsUrl().isEmpty();
}

void ServiceProviderGtfs::updateRealtimeData()
{
//         m_state = DownloadingFeed;
    if ( !m_data->realtimeTripUpdateUrl().isEmpty() ) {
        KIO::StoredTransferJob *tripUpdatesJob = KIO::storedGet( m_data->realtimeTripUpdateUrl(),
                KIO::Reload, KIO::Overwrite | KIO::HideProgressInfo );
        connect( tripUpdatesJob, SIGNAL(result(KJob*)), this, SLOT(realtimeTripUpdatesReceived(KJob*)) );
        kDebug() << "Updating GTFS-realtime trip update data" << m_data->realtimeTripUpdateUrl();
    }

    if ( !m_data->realtimeAlertsUrl().isEmpty() ) {
        KIO::StoredTransferJob *alertsJob = KIO::storedGet( m_data->realtimeAlertsUrl(),
                KIO::Reload, KIO::Overwrite | KIO::HideProgressInfo );
        connect( alertsJob, SIGNAL(result(KJob*)), this, SLOT(realtimeAlertsReceived(KJob*)) );
        kDebug() << "Updating GTFS-realtime alerts data" << m_data->realtimeAlertsUrl();
    }

    if ( m_data->realtimeTripUpdateUrl().isEmpty() && m_data->realtimeAlertsUrl().isEmpty() ) {
        m_state = Ready;
    }
}

void ServiceProviderGtfs::realtimeTripUpdatesReceived( KJob *job )
{
    KIO::StoredTransferJob *transferJob = qobject_cast<KIO::StoredTransferJob*>( job );
    if ( job->error() != 0 ) {
        kDebug() << "Error downloading GTFS-realtime trip updates:" << job->errorString();
//         m_state = ErrorDownloadingFeed;

//         for ( QHash<QString, JobInfos>::ConstIterator it = m_jobInfos.constBegin();
//               it != m_jobInfos.constEnd(); ++it )
//         {
//             emit errorParsing( this, ErrorDownloadFailed, i18nc("@info/plain TODO",
//                     "Failed to download GTFS feed from <resource>%1</resource>: <message>%2</message>",
//                     m_data->feedUrl(), job->errorString()),
//                     m_data->feedUrl(), m_data->id(),
//                     it->sourceName, it->city, it->stop, it->dataType, it->parseDocumentMode );
//         }
        return;
    }

    delete m_tripUpdates;
    m_tripUpdates = GtfsRealtimeTripUpdate::fromProtocolBuffer( transferJob->data() );

    if ( m_alerts || m_data->realtimeAlertsUrl().isEmpty() ) {
        m_state = Ready;
    }
}

void ServiceProviderGtfs::realtimeAlertsReceived( KJob *job )
{
    KIO::StoredTransferJob *transferJob = qobject_cast<KIO::StoredTransferJob*>( job );
    if ( job->error() != 0 ) {
        kDebug() << "Error downloading GTFS-realtime alerts:" << job->errorString();
//         m_state = ErrorDownloadingFeed;

//         for ( QHash<QString, JobInfos>::ConstIterator it = m_jobInfos.constBegin();
//               it != m_jobInfos.constEnd(); ++it )
//         {
//             emit errorParsing( this, ErrorDownloadFailed, i18nc("@info/plain TODO",
//                     "Failed to download GTFS feed from <resource>%1</resource>: <message>%2</message>",
//                     m_data->feedUrl(), job->errorString()),
//                     m_data->feedUrl(), m_data->id(),
//                     it->sourceName, it->city, it->stop, it->dataType, it->parseDocumentMode );
//         }
        return;
    }

    delete m_alerts;
    m_alerts = GtfsRealtimeAlert::fromProtocolBuffer( transferJob->data() );

    if ( m_tripUpdates || m_data->realtimeTripUpdateUrl().isEmpty() ) {
        m_state = Ready;
    }
}
#endif // BUILD_GTFS_REALTIME

void ServiceProviderGtfs::loadAgencyInformation()
{
    if ( m_state != Ready ) {
        return;
    }

    QSqlQuery query( QSqlDatabase::database(m_data->id()) );
    if ( !query.exec("SELECT * FROM agency") ) {
        kDebug() << "Could not load agency information from database:" << query.lastError();
        return;
    }

    // Clear previously loaded agency data
    qDeleteAll( m_agencyCache );
    m_agencyCache.clear();

    // Read agency records from the database
    QSqlRecord record = query.record();
    const int agencyIdColumn = record.indexOf( "agency_id" );
    const int agencyNameColumn = record.indexOf( "agency_name" );
    const int agencyUrlColumn = record.indexOf( "agency_url" );
    const int agencyTimezoneColumn = record.indexOf( "agency_timezone" );
    const int agencyLanguageColumn = record.indexOf( "agency_lang" );
    const int agencyPhoneColumn = record.indexOf( "agency_phone" );
    while ( query.next() ) {
        AgencyInformation *agency = new AgencyInformation;
        agency->name = query.value( agencyNameColumn ).toString();
        agency->url = query.value( agencyUrlColumn ).toString();
        agency->language = query.value( agencyLanguageColumn ).toString();
        agency->phone = query.value( agencyPhoneColumn ).toString();

        const QString timeZone = query.value(agencyTimezoneColumn).toString();
        agency->timezone = new KTimeZone( timeZone.isEmpty() ? m_data->timeZone() : timeZone );

        const uint id = query.value( agencyIdColumn ).toUInt();
        m_agencyCache.insert( id, agency );
    }
}

int ServiceProviderGtfs::AgencyInformation::timeZoneOffset() const
{
    return timezone && timezone->isValid() ? timezone->currentOffset( Qt::LocalTime ) : 0;
}

qint64 ServiceProviderGtfs::databaseSize() const
{
    QFileInfo fi( GeneralTransitFeedDatabase::databasePath(m_data->id()) );
    return fi.size();
}

ServiceProviderGtfs::AgencyInformation::~AgencyInformation()
{
    delete timezone;
}

QStringList ServiceProviderGtfs::features() const
{
    QStringList list;
    list << "Autocompletion" << "TypeOfVehicle" << "Operator" << "StopID" << "RouteStops"
         << "RouteTimes" << "Arrivals";
#ifdef BUILD_GTFS_REALTIME
    if ( !m_data->realtimeAlertsUrl().isEmpty() ) {
        list << "JourneyNews";
    }
    if ( !m_data->realtimeTripUpdateUrl().isEmpty() ) {
        list << "Delay";
    }
#endif
    return list;
}

QTime ServiceProviderGtfs::timeFromSecondsSinceMidnight(
        int secondsSinceMidnight, QDate *date ) const
{
    const int secondsInOneDay = 60 * 60 * 24;
    while ( secondsSinceMidnight >= secondsInOneDay ) {
        secondsSinceMidnight -= secondsInOneDay;
        if ( date ) {
            date->addDays( 1 );
        }
    }
    return QTime( secondsSinceMidnight / (60 * 60),
                  (secondsSinceMidnight / 60) % 60,
                  secondsSinceMidnight % 60 );
}

bool ServiceProviderGtfs::isGtfsFeedImportFinished()
{
    // Try to load provider information from a cache file
//     KConfig config( ServiceProviderGlobal::cacheFileName(), KConfig::SimpleConfig );
//     KConfigGroup group = config.group( m_data->id() );
//     KConfigGroup gtfsGroup = group.group( "gtfs" );
// TODO
    kDebug() << "NOT YET IMPLEMENTED";
        // Check if the GTFS feed file was modified since the cache was last updated
//         KDateTime gtfsModifiedTime = KDateTime::fromString( grp.readEntry("feedLastModified", QString()) );
// //         QDateTime gtfsModifiedTime = grp.readEntry("feedLastModified", QDateTime());
//         if ( QFileInfo(m_data->feedUrl()).lastModified() /*TODO*/ == gtfsModifiedTime ) {
//             // Return feature list stored in the cache
//             return grp.readEntry("feedImportFinished", false);
//         }

    // No actual cached information about the service provider
    kDebug() << "No up-to-date cache information for service provider" << m_data->id();

    return false;
}

QString ServiceProviderGtfs::errorMessageForErrorState( ServiceProviderGtfs::State errorState ) const
{
    switch ( errorState ) {
    case ErrorDownloadingFeed:
        return i18nc("@info/plain", "Failed to download the GTFS feed from <resource>%1</resource>",
                     m_data->feedUrl());
        break;
    case ErrorReadingFeed:
        return i18nc("@info/plain", "Failed to read the GTFS feed from <resource>%1</resource>",
                     m_data->feedUrl());
        break;
    case ErrorNeedsFeedImport:
        return i18nc("@info/plain", "GTFS feed not imported from <resource>%1</resource>",
                     m_data->feedUrl());
    default:
        kWarning() << "Not an error state" << errorState;
        return QString();
    }
}

bool ServiceProviderGtfs::checkState( const AbstractRequest *request )
{
    if ( m_state == Ready ) {
        return true;
    } else {
        switch ( m_state ) {
        case ErrorDownloadingFeed:
            emit errorParsing( this, ErrorDownloadFailed, errorMessageForErrorState(m_state),
                               m_data->feedUrl(), request );
            break;
        case ErrorReadingFeed:
            emit errorParsing( this, ErrorParsingFailed, errorMessageForErrorState(m_state),
                               m_data->feedUrl(), request );
            break;
        case ErrorNeedsFeedImport: {
            // Check, if the feed is imported now
            // Read provider information cache
            KConfig config( ServiceProviderGlobal::cacheFileName(), KConfig::SimpleConfig );
            KConfigGroup group = config.group( m_data->id() );
            KConfigGroup gtfsGroup = group.group( "gtfs" );
            bool importFinished = gtfsGroup.readEntry( "feedImportFinished", false );
            QFileInfo fi( GeneralTransitFeedDatabase::databasePath(m_data->id()) );
            if ( importFinished && fi.exists() && fi.size() > 30000 ) {
                // Load agency information from database and request GTFS-realtime data
                loadAgencyInformation();
#ifdef BUILD_GTFS_REALTIME
                updateRealtimeData();
#endif
                m_state = Ready;
                return true;
            } else {
                emit errorParsing( this, ErrorNeedsImport, errorMessageForErrorState(m_state),
                                   m_data->feedUrl(), request );
            }
            break;
        }
        case Initializing:
            emit progress( this, 0.0, i18nc("@info/plain",
                    "Initializing GTFS feed database.",
                    m_data->feedUrl()), m_data->feedUrl(), request );
            break;
        case UpdatingGtfsFeed:
            emit progress( this, m_progress, i18nc("@info/plain",
                    "Updating GTFS feed database.",
                    m_data->feedUrl()), m_data->feedUrl(), request );
            break;
        default:
            emit errorParsing( this, ErrorParsingFailed, i18nc("@info/plain", "Busy, please wait.",
                    m_data->feedUrl()), m_data->feedUrl(), request );
            break;
        }

        kDebug() << "State" << m_state;
        if ( m_state == ErrorDownloadingFeed || m_state == ErrorReadingFeed ) {
            // Update database to the current version of the GTFS feed or import it for the first time
            kDebug() << "Restart update";
            updateGtfsData();
        }

        // Store information about the request to report import progress to
        if ( !m_waitingRequests.contains(request->sourceName) ) {
            m_waitingRequests[ request->sourceName ] = request->clone();
        }
        kDebug() << "Wait for GTFS feed download and import";
        return false;
    }
}

void ServiceProviderGtfs::requestDepartures( const DepartureRequest &request )
{
    requestDeparturesOrArrivals( &request );
}

void ServiceProviderGtfs::requestArrivals( const ArrivalRequest &request )
{
    requestDeparturesOrArrivals( &request );
}

void ServiceProviderGtfs::requestDeparturesOrArrivals( const DepartureRequest *request )
{
    if ( !checkState(request) ) {
        return;
    }

    QSqlQuery query( QSqlDatabase::database(m_data->id()) );
    query.setForwardOnly( true ); // Don't cache records

    // TODO If it is known that [stop] contains a stop ID testing for it's ID in stop_times is not necessary!
    // Try to get the ID for the given stop (fails, if it already is a stop ID). Only select
    // stops, no stations (with one or more sub stops) by requiring 'location_type=0',
    // location_type 1 is for stations.
    // It's fast, because 'stop_name' is part of a compound index in the database.
    uint stopId;
    QString stopValue = request->stop;
    stopValue.replace( '\'', "\'\'" );
    if ( !query.exec("SELECT stops.stop_id FROM stops "
                     "WHERE stop_name='" + stopValue + "' "
                     "AND (location_type IS NULL OR location_type=0)") )
    {
        // Check of the error is a "disk I/O error", ie. the database file may have been deleted
        checkForDiskIoErrorInDatabase( query.lastError(), request );

        kDebug() << query.lastError();
        kDebug() << query.executedQuery();
        return;
    }

    QSqlRecord stopRecord = query.record();
    if ( query.next() ) {
        stopId = query.value( query.record().indexOf("stop_id") ).toUInt();
    } else {
        bool ok;
        stopId = request->stop.toUInt( &ok );
        if ( !ok ) {
            kDebug() << "No stop with the given name or id found (needs the exact name):"
                     << request->stop;
            emit errorParsing( this, ErrorParsingFailed /*TODO*/,
                    "No stop with the given name or id found (needs the exact name): "
                    + request->stop,
                    QUrl(), request );
            return;
        }
    }

// This creates a temporary table to calculate min/max fares for departures.
// These values should be added into the db while importing, doing it here takes too long
//     const QString createJoinedFareTable = "CREATE TEMPORARY TABLE IF NOT EXISTS tmp_fares AS "
//             "SELECT * FROM fare_rules JOIN fare_attributes USING (fare_id);";
//     if ( !query.prepare(createJoinedFareTable) || !query.exec() ) {
//         kDebug() << "Error while creating a temporary table fore min/max fare calculation:"
//                  << query.lastError();
//         kDebug() << query.executedQuery();
//         return;
//     }

    // Query the needed departure info from the database.
    // It's fast, because all JOINs are done using INTEGER PRIMARY KEYs and
    // because 'stop_id' and 'departure_time' are part of a compound index in the database.
    // Sorting by 'arrival_time' may be a bit slower because is has no index in the database,
    // but if arrival_time values do not differ too much from the deaprture_time values, they
    // are also already sorted.
    // The tables 'calendar' and 'calendar_dates' are also fully implemented by the query below.
    // TODO: Create a new (temporary) table for each connected departure/arrival source and use
    //       that (much smaller) table here for performance reasons
    const QString routeSeparator = "||";
    const QTime time = request->dateTime.time();
    const QString queryString = QString(
            "SELECT times.departure_time, times.arrival_time, times.stop_headsign, "
                   "routes.route_type, routes.route_short_name, routes.route_long_name, "
                   "trips.trip_headsign, routes.agency_id, stops.stop_id, trips.trip_id, "
                   "routes.route_id, times.stop_sequence, "
                   "( SELECT group_concat(route_stop.stop_name, '%5') AS route_stops "
                     "FROM stop_times AS route_times INNER JOIN stops AS route_stop USING (stop_id) "
                     "WHERE route_times.trip_id=times.trip_id AND route_times.stop_sequence %4= times.stop_sequence "
                     "ORDER BY departure_time ) AS route_stops, "
                   "( SELECT group_concat(route_times.departure_time, '%5') AS route_times "
                     "FROM stop_times AS route_times "
                     "WHERE route_times.trip_id=times.trip_id AND route_times.stop_sequence %4= times.stop_sequence "
                     "ORDER BY departure_time ) AS route_times "
//                    "( SELECT min(price) FROM tmp_fares WHERE origin_id=stops.zone_id AND price>0 ) AS min_price, "
//                    "( SELECT max(price) FROM tmp_fares WHERE origin_id=stops.zone_id ) AS max_price, "
//                    "( SELECT currency_type FROM tmp_fares WHERE origin_id=stops.zone_id LIMIT 1 ) AS currency_type "
            "FROM stops INNER JOIN stop_times AS times USING (stop_id) "
                       "INNER JOIN trips USING (trip_id) "
                       "INNER JOIN routes USING (route_id) "
                       "LEFT JOIN calendar USING (service_id) "
                       "LEFT JOIN calendar_dates ON (trips.service_id=calendar_dates.service_id "
                                                    "AND strftime('%Y%m%d')=calendar_dates.date) "
            "WHERE stop_id=%1 AND departure_time>%2 "
                  "AND (calendar_dates.date IS NULL " // No matching record in calendar_dates table for today
                       "OR NOT (calendar_dates.exception_type=2)) " // Journey is not removed today
                  "AND (calendar.weekdays IS NULL " // No matching record in calendar table => always available
                       "OR (strftime('%Y%m%d') BETWEEN calendar.start_date " // Current date is in the range...
                                              "AND calendar.end_date " // ...where the service is available...
                           "AND substr(calendar.weekdays, strftime('%w') + 1, 1)='1') " // ...and it's available at the current weekday
                       "OR (calendar_dates.date IS NOT NULL " // Or there is a matching record in calendar_dates for today...
                           "AND calendar_dates.exception_type=1)) " // ...and this record adds availability of the service for today
            "ORDER BY departure_time "
            "LIMIT %3" )
            .arg( stopId )
            .arg( time.hour() * 60 * 60 + time.minute() * 60 + time.second() )
            .arg( request->maxCount )
            .arg( request->parseMode == ParseForArrivals ? '<' : '>' ) // For arrivals route_stops/route_times need stops before the home stop
            .arg( routeSeparator );
    if ( !query.prepare(queryString) || !query.exec() ) {
        kDebug() << "Error while querying for departures:" << query.lastError();
        kDebug() << query.executedQuery();
        return;
    }
    if ( query.size() == 0 ) {
        kDebug() << "Got an empty record";
        return;
    }
    kDebug() << "Query executed";
    kDebug() << query.executedQuery();

    QSqlRecord record = query.record();
    const int agencyIdColumn = record.indexOf( "agency_id" );
    const int tripIdColumn = record.indexOf( "trip_id" );
    const int routeIdColumn = record.indexOf( "route_id" );
    const int stopIdColumn = record.indexOf( "stop_id" );
    const int arrivalTimeColumn = record.indexOf( "arrival_time" );
    const int departureTimeColumn = record.indexOf( "departure_time" );
    const int routeShortNameColumn = record.indexOf( "route_short_name" );
    const int routeLongNameColumn = record.indexOf( "route_long_name" );
    const int routeTypeColumn = record.indexOf( "route_type" );
    const int tripHeadsignColumn = record.indexOf( "trip_headsign" );
    const int stopSequenceColumn = record.indexOf( "stop_sequence" );
    const int stopHeadsignColumn = record.indexOf( "stop_headsign" );
    const int routeStopsColumn = record.indexOf( "route_stops" );
    const int routeTimesColumn = record.indexOf( "route_times" );
//     const int fareMinPriceColumn = record.indexOf( "min_price" );
//     const int fareMaxPriceColumn = record.indexOf( "max_price" );
//     const int fareCurrencyColumn  = record.indexOf( "currency_type" );

    // Prepare agency information, if only one is given, it is used for all records
    AgencyInformation *agency = 0;
    if ( m_agencyCache.count() == 1 ) {
        agency = m_agencyCache.values().first();
    }

    // Create a list of DepartureInfo objects from the query result
    DepartureInfoList departures;
    while ( query.next() ) {
        QDate arrivalDate = request->dateTime.date();
        QDate departureDate = request->dateTime.date();

        // Load agency information from cache
        const QVariant agencyIdValue = query.value( agencyIdColumn );
        if ( m_agencyCache.count() > 1 ) {
            Q_ASSERT( agencyIdValue.isValid() ); // GTFS says, that agency_id can only be null, if there is only one agency
            agency = m_agencyCache[ agencyIdValue.toUInt() ];
        }

        // Time values are stored as seconds since midnight of the associated date
        int arrivalTimeValue = query.value(arrivalTimeColumn).toInt();
        int departureTimeValue = query.value(departureTimeColumn).toInt();

        QDateTime arrivalTime( arrivalDate,
                               timeFromSecondsSinceMidnight(arrivalTimeValue, &arrivalDate) );
        QDateTime departureTime( departureDate,
                                 timeFromSecondsSinceMidnight(departureTimeValue, &departureDate) );

        // Apply timezone offset
        int offsetSeconds = agency ? agency->timeZoneOffset() : 0;
        if ( offsetSeconds != 0 ) {
            arrivalTime.addSecs( offsetSeconds );
            departureTime.addSecs( offsetSeconds );
        }

        TimetableData data;
        data[ Enums::DepartureDateTime ] = request->parseMode == ParseForArrivals ? arrivalTime : departureTime;
        data[ Enums::TypeOfVehicle ] = vehicleTypeFromGtfsRouteType( query.value(routeTypeColumn).toInt() );
        data[ Enums::Operator ] = agency ? agency->name : QString();

        const QString transportLine = query.value(routeShortNameColumn).toString();
        data[ Enums::TransportLine ] = !transportLine.isEmpty() ? transportLine
                         : query.value(routeLongNameColumn).toString();

        const QString tripHeadsign = query.value(tripHeadsignColumn).toString();
        data[ Enums::Target ] = !tripHeadsign.isEmpty() ? tripHeadsign
                         : query.value(stopHeadsignColumn).toString();

        const QStringList routeStops = query.value(routeStopsColumn).toString().split( routeSeparator );
        if ( routeStops.isEmpty() ) {
            // This happens, if the current departure is actually no departure, but an arrival at
            // the target station and vice versa for arrivals.
            continue;
        }
        data[ Enums::RouteStops ] = routeStops;
        data[ Enums::RouteExactStops ] = routeStops.count();

        const QStringList routeTimeValues = query.value(routeTimesColumn).toString().split( routeSeparator );
        QVariantList routeTimes;
        foreach ( const QString routeTimeValue, routeTimeValues ) {
            routeTimes << timeFromSecondsSinceMidnight( routeTimeValue.toInt(), &arrivalDate );
        }
        data[ Enums::RouteTimes ] = routeTimes;

//         const QString symbol = KCurrencyCode( query.value(fareCurrencyColumn).toString() ).defaultSymbol();
//         data[ Pricing ] = KGlobal::locale()->formatMoney(
//                 query.value(fareMinPriceColumn).toDouble(), symbol ) + " - " +
//                 KGlobal::locale()->formatMoney( query.value(fareMaxPriceColumn).toDouble(), symbol );

#ifdef BUILD_GTFS_REALTIME
        if ( m_alerts ) {
            QStringList journeyNews;
            QString journeyNewsLink;
            foreach ( const GtfsRealtimeAlert &alert, *m_alerts ) {
                if ( alert.isActiveAt(QDateTime::currentDateTime()) ) {
                    journeyNews << alert.description;
                    journeyNewsLink = alert.url;
                }
            }
            if ( !journeyNews.isEmpty() ) {
                data[ Enums::JourneyNews ] = journeyNews.join( ", " );
                data[ Enums::JourneyNewsLink ] = journeyNewsLink;
            }
        }

        if ( m_tripUpdates ) {
            uint tripId = query.value(tripIdColumn).toUInt();
            uint routeId = query.value(routeIdColumn).toUInt();
            uint stopId = query.value(stopIdColumn).toUInt();
            uint stopSequence = query.value(stopSequenceColumn).toUInt();
            foreach ( const GtfsRealtimeTripUpdate &tripUpdate, *m_tripUpdates ) {
                if ( (tripUpdate.tripId > 0 && tripId == tripUpdate.tripId) ||
                     (tripUpdate.routeId > 0 && routeId == tripUpdate.routeId) ||
                     (tripUpdate.tripId <= 0 && tripUpdate.routeId <= 0) )
                {
                    kDebug() << "tripId or routeId matched or not queried!";
                    foreach ( const GtfsRealtimeStopTimeUpdate &stopTimeUpdate,
                              tripUpdate.stopTimeUpdates )
                    {
                        if ( (stopTimeUpdate.stopId > 0 && stopId == stopTimeUpdate.stopId) ||
                             (stopTimeUpdate.stopSequence > 0 &&
                              stopSequence == stopTimeUpdate.stopSequence) ||
                             (stopTimeUpdate.stopId <= 0 && stopTimeUpdate.stopSequence <= 0) )
                        {
                            kDebug() << "stopId matched or stopsequence matched or not queried!";
                            // Found a matching stop time update
                            kDebug() << "Delays:" << stopTimeUpdate.arrivalDelay << stopTimeUpdate.departureDelay;
                        }
                    }
                }
            }
        }
#endif

        // Create new departure information object and add it to the departure list.
        // Do not use any corrections in the DepartureInfo constructor, because all values
        // from the database are already in the correct format
        departures << DepartureInfoPtr( new DepartureInfo(data, PublicTransportInfo::NoCorrection) );
    }

    // TODO Do not use a list of pointers here, maybe use data sharing for PublicTransportInfo/StopInfo?
    // The objects in departures are deleted in a connected slot in the data engine...
    const ArrivalRequest *arrivalRequest = dynamic_cast< const ArrivalRequest* >( request );
    if ( arrivalRequest ) {
        emit arrivalListReceived( this, QUrl(), departures, GlobalTimetableInfo(), *arrivalRequest );
    } else {
        emit departureListReceived( this, QUrl(), departures, GlobalTimetableInfo(), *request );
    }
}

void ServiceProviderGtfs::requestStopSuggestions( const StopSuggestionRequest &request )
{
    if ( !checkState(&request) ) {
        return;
    }

    QSqlQuery query( QSqlDatabase::database(m_data->id()) );
    query.setForwardOnly( true );
    QString stopValue = request.stop;
    stopValue.replace( '\'', "\'\'" );
    if ( !query.prepare(QString("SELECT * FROM stops WHERE stop_name LIKE '%%2%' LIMIT %1")
                        .arg(STOP_SUGGESTION_LIMIT).arg(stopValue))
         || !query.exec() )
    {
        // Check of the error is a "disk I/O error", ie. the database file may have been deleted
        checkForDiskIoErrorInDatabase( query.lastError(), &request );
        kDebug() << query.lastError();
        kDebug() << query.executedQuery();
        return;
    }

    QSqlRecord record = query.record();
    const int stopIdColumn = record.indexOf( "stop_id" );
    const int stopNameColumn = record.indexOf( "stop_name" );
//     int stopLatitudeColumn = record.indexOf( "stop_lat" ); TODO
//     int stopLongitudeColumn = record.indexOf( "stop_lon" );

    StopInfoList stops;
    while ( query.next() ) {
        const QString stopName = query.value(stopNameColumn).toString();

        // Compute a weight value for the found stop name.
        // The less different the found stop name is compared to the search string, the higher
        // it's weight gets. If the found name equals the search string, the weight becomes 100.
        // Use 84 as maximal starting weight value (if stopName doesn't equal the search string),
        // because maximally 15 bonus points are added which makes 99, less than total equality 100.
        int weight = stopName == request.stop ? 100
                : 84 - qMin( 84, qAbs(stopName.length() - request.stop.length()) );

        if ( weight < 100 && stopName.startsWith(request.stop) ) {
            // 15 weight points bonus if the found stop name starts with the search string
            weight = qMin( 100, weight + 15 );
        }
        if ( weight < 100 ) {
            // Test if the search string is the start of a new word in stopName
            // Start at 2, because startsWith is already tested above and at least a space must
            // follow to start a new word
            int pos = stopName.indexOf( request.stop, 2, Qt::CaseInsensitive );

            if ( pos != -1 && stopName[pos - 1].isSpace() ) {
                // 10 weight points bonus if a word in the found stop name
                // starts with the search string
                weight = qMin( 100, weight + 10 );
            }
        }
        stops << StopInfoPtr( new StopInfo(stopName, query.value(stopIdColumn).toString(),
                                           weight, request.city) );
    }

    if ( stops.isEmpty() ) {
        kDebug() << "No stop names found";
    }
    emit stopListReceived( this, QUrl(), stops, request );
}

bool ServiceProviderGtfs::checkForDiskIoErrorInDatabase( const QSqlError &error,
                                                         const AbstractRequest *request )
{
    Q_UNUSED( request );

    // Check of the error is a "disk I/O" error or a "no such table" error, ie. the database file
    // may have been deleted.
    // The error numbers (1, 10) is database dependend and works with SQLITE
    if ( error.number() == 10 || error.number() == 1 ) {
        kDebug() << "Disk I/O error reported from database, recreate the database";

        m_state = Initializing;
        QString errorText;
        if ( !GeneralTransitFeedDatabase::initDatabase(m_data->id(), &errorText) ) {
            kDebug() << "Error initializing the database" << errorText;
            m_state = ErrorInDatabase;
//             updateSourceFileValidity(); TODO!
            return true;
        }

        QFileInfo fi( GeneralTransitFeedDatabase::databasePath(m_data->id()) );
        if ( !fi.exists() || fi.size() < 50000 ) {
            // If the database does not exist or is too small, get information about the GTFS feed,
            // download it, create the database and import the feed into it
//             statFeed();
        } else {
            loadAgencyInformation();
#ifdef BUILD_GTFS_REALTIME
            updateRealtimeData();
#endif
        }
        return true;
    } else {
        return false;
    }
}

Enums::VehicleType ServiceProviderGtfs::vehicleTypeFromGtfsRouteType(
        int gtfsRouteType ) const
{
    switch ( gtfsRouteType ) {
    case 0: // Tram, Streetcar, Light rail. Any light rail or street level system within a metropolitan area.
        return Enums::Tram;
    case 1: // Subway, Metro. Any underground rail system within a metropolitan area.
        return Enums::Subway;
    case 2: // Rail. Used for intercity or long-distance travel.
        return Enums::IntercityTrain;
    case 3: // Bus. Used for short- and long-distance bus routes.
        return Enums::Bus;
    case 4: // Ferry. Used for short- and long-distance boat service.
        return Enums::Ferry;
    case 5: // Cable car. Used for street-level cable cars where the cable runs beneath the car.
        return Enums::TrolleyBus;
    case 6: // Gondola, Suspended cable car. Typically used for aerial cable cars where the car is suspended from the cable.
        return Enums::Unknown; // TODO Add new type to VehicleType: eg. Gondola
    case 7: // Funicular. Any rail system designed for steep inclines.
        return Enums::Unknown; // TODO Add new type to VehicleType: eg. Funicular
    default:
        return Enums::Unknown;
    }
}