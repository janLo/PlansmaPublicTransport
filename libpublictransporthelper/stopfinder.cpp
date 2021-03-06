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

#include "stopfinder.h"
#include <Plasma/DataEngineManager>

/** @brief Namespace for the publictransport helper library. */
namespace PublicTransport {

class StopSuggesterPrivate
{
public:
    StopSuggesterPrivate( Plasma::DataEngine* _publicTransportEngine )
            : publicTransportEngine(_publicTransportEngine) {};

    Plasma::DataEngine *publicTransportEngine;
    QStringList sourceNames;
};

StopSuggester::StopSuggester( Plasma::DataEngine* publicTransportEngine,
                              QObject* parent )
        : QObject(parent), d_ptr(new StopSuggesterPrivate(publicTransportEngine))
{
}

StopSuggester::~StopSuggester()
{
    delete d_ptr;
}

void StopSuggester::requestSuggestions( const QString &serviceProviderID,
        const QString &stopSubstring, const QString &city,
        RunningRequestOptions runningRequestOptions )
{
    Q_D( StopSuggester );
    if ( runningRequestOptions == AbortRunningRequests ) {
        foreach( const QString &sourceName, d->sourceNames ) {
            d->publicTransportEngine->disconnectSource( sourceName, this );
        }
        d->sourceNames.clear();
    }

    if ( !city.isEmpty() ) { // m_useSeparateCityValue ) {
        d->sourceNames << QString( "Stops %1|stop=%2|city=%3" )
                .arg( serviceProviderID, stopSubstring, city );
    } else {
        d->sourceNames << QString( "Stops %1|stop=%2" )
                .arg( serviceProviderID, stopSubstring );
    }
    d->publicTransportEngine->connectSource( d->sourceNames.last(), this );
}

void StopSuggester::dataUpdated( const QString& sourceName, const Plasma::DataEngine::Data& data )
{
    Q_D( StopSuggester );
    if ( sourceName.startsWith( QLatin1String( "Stops" ), Qt::CaseInsensitive ) ) {
        d->publicTransportEngine->disconnectSource( sourceName, this );
        if ( !d->sourceNames.removeOne( sourceName ) ) {
            kDebug() << "Source" << sourceName << "was aborted";
            return;
        }

        QStringList stopNames;
        QVariantHash stopToStopID;
        QHash<QString, int> stopToStopWeight;
        QVariantList stops = data["stops"].toList();
        foreach ( const QVariant &stopData, stops ) {
            QVariantHash stop = stopData.toHash();
            QString stopName = stop["StopName"].toString();
            QString stopID = stop["StopID"].toString();
            int stopWeight = stop["StopWeight"].toInt();
            stopNames.append( stopName );
            stopToStopID.insert( stopName, stopID );
            stopToStopWeight.insert( stopName, stopWeight );
        }

        if ( !stopNames.isEmpty() ) {
            emit stopSuggestionsReceived( stopNames, stopToStopID, stopToStopWeight );
        } else {
            kDebug() << "nothing found";
        }
    }
}

bool StopSuggester::isRunning() const {
    Q_D( const StopSuggester );
    return !d->sourceNames.isEmpty();
}

class StopFinderPrivate
{
    Q_DECLARE_PUBLIC( StopFinder )

public:
    StopFinderPrivate( StopFinder::Mode _mode, int _resultLimit,
            StopFinder::DeletionPolicy _deletionPolicy, StopFinder *q )
            : mode(_mode), deletionPolicy(_deletionPolicy), q_ptr(q)
    {
        osmFinished = false;
        resultLimit = _resultLimit;
        accuracy = 0;
    };

    bool validateNextStop() {
        Q_Q( StopFinder );
        if ( stopsToBeChecked.isEmpty() || foundStops.count() >= resultLimit ) {
            kDebug() << "No more stops to be checked in the queue or limit reached.";
            return false;
        }

        QString stop = stopsToBeChecked.dequeue();
        kDebug() << "Validate stop" << stop;
        Plasma::DataEngine *engine = Plasma::DataEngineManager::self()->engine("publictransport");
        if ( !city.isEmpty() ) { // useSeparateCityValue ) {
            engine->connectSource( QString("Stops %1|stop=%2|city=%3")
                                   .arg(serviceProviderID, stop, city), q );
        } else {
            engine->connectSource( QString("Stops %1|stop=%2").arg(serviceProviderID, stop), q );
        }

        return true;
    };

    void processGeolocationData( const Plasma::DataEngine::Data &data ) {
        Q_Q( StopFinder );

        countryCode = data["country code"].toString().toLower();
        city = data["city"].toString();
        qreal latitude = data["latitude"].toDouble();
        qreal longitude = data["longitude"].toDouble();
        accuracy = data["accuracy"].toInt();
        emit q->geolocationData( countryCode, city, latitude, longitude, accuracy );

        // Check if a service provider is available for the given country
        Plasma::DataEngine *engine = Plasma::DataEngineManager::self()->engine("publictransport");
        Plasma::DataEngine::Data dataProvider = engine->query( "ServiceProvider " + countryCode );
        if ( dataProvider.isEmpty() ) {
            QString errorMessage = i18nc("@info", "There's no supported "
                                        "service provider for the country you're currently in (%1).\n"
                                        "You can try service providers for other countries, as some of "
                                        "them also provide data for adjacent countries.",
                                        KGlobal::locale()->countryCodeToName( countryCode ));
            kDebug() << "No service provider found for country" << countryCode;
            emit q->error( StopFinder::NoServiceProviderForCurrentCountry, errorMessage );
            emit q->finished();
            if ( deletionPolicy == StopFinder::DeleteWhenFinished ) {
                q->deleteLater();
            }
        } else {
            serviceProviderID = dataProvider["id"].toString();
            Plasma::DataEngine *osmEngine =
                    Plasma::DataEngineManager::self()->engine("openstreetmap");
            if ( osmEngine->isValid() ) {
                // Get stop list near the user from the OpenStreetMap data engine.
                // If the OSM engine isn't available, the city is used as stop name.
                double areaSize = accuracy > 10000 ? 0.5 : 0.02;
                QString sourceName = QString( "%1,%2 %3 publictransportstops" )
                        .arg( latitude ).arg( longitude ).arg( areaSize );
                osmEngine->connectSource( sourceName, q );
            } else {
                kDebug() << "OSM engine not available";
                emit q->error( StopFinder::OpenStreetMapDataEngineNotAvailable,
                        i18nc("@info", "OpenStreetMap data engine not available") );
                emit q->finished();
                if ( deletionPolicy == StopFinder::DeleteWhenFinished ) {
                    q->deleteLater();
                }
            }
        }
    };

    bool processOpenStreetMapData( const Plasma::DataEngine::Data &data ) {
        Q_Q( StopFinder );

        QStringList stops;
        Plasma::DataEngine::Data::const_iterator it = data.constBegin();
    //     it += m_nearStopsDialog->listView()->model()->rowCount(); // Don't readd already added stops
        for ( ; it != data.constEnd(); ++it ) {
            QHash< QString, QVariant > item = it.value().toHash();
            if ( item.contains("name") ) {
                stops << item[ "name" ].toString();
            }
        }
        stops.removeDuplicates();

        if ( mode == StopFinder::ValidatedStopNamesFromOSM ) {
            stopsToBeChecked.append( stops );
            validateNextStop();
        }

        if ( mode == StopFinder::StopNamesFromOSM && !stops.isEmpty() ) {
            emit q->stopsFound( stops, QStringList(), serviceProviderID );
        }

        if ( data.contains("finished") && data["finished"].toBool() ) {
            osmFinished = true;

            if ( mode == StopFinder::StopNamesFromOSM ) {
                if ( stops.isEmpty() ) {
                    kDebug() << "No stops found by OSM for the given position";
                    emit q->error( StopFinder::NoStopsFound,
                            i18nc("@info", "No stops found by OpenStreetMap for the given position") );
                }
                emit q->finished();
                if ( deletionPolicy == StopFinder::DeleteWhenFinished ) {
                    q->deleteLater();
                }
            } //else ( m_mode == ValidatedStopNamesFromOSM && m_s
        }

        return osmFinished;
    };

    void processPublicTransportData( const Plasma::DataEngine::Data &data ) {
        Q_Q( StopFinder );

        QString stop, stopID;
        QVariantList stops = data["stops"].toList();
        foreach ( const QVariant &stopData, stops ) {
            QVariantHash stopHash = stopData.toHash();
            stop = stopHash["StopName"].toString();
            stopID = stopHash["StopID"].toString();
            break;
        }
        if ( !stop.isEmpty() ) {
            foundStops << stop;
            foundStopIDs << stopID;

            emit q->stopsFound( QStringList() << stop, QStringList() << stopID, serviceProviderID );
        } else {
            kDebug() << "nothing found";
        }

        if ( !validateNextStop() && osmFinished ) {
            kDebug() << "Last stop validated and OSM engine is finished."
                    << foundStops.count() << "stops found.";
    //     emit stopsFound( m_foundStops, m_foundStopIDs, m_serviceProviderID );
            emit q->finished();
            if ( deletionPolicy == StopFinder::DeleteWhenFinished ) {
                q->deleteLater();
            }
        }
    };

    StopFinder::Mode mode;
    StopFinder::DeletionPolicy deletionPolicy;

    QStringList foundStops, foundStopIDs;
    QQueue< QString > stopsToBeChecked;

    int resultLimit;
    bool osmFinished;
    QString countryCode;
    QString city;
    QString serviceProviderID;
    int accuracy;

protected:
    StopFinder* const q_ptr;
};

StopFinder::StopFinder( StopFinder::Mode mode, int resultLimit, DeletionPolicy deletionPolicy,
        QObject* parent )
        : QObject(parent), d_ptr(new StopFinderPrivate(mode, resultLimit, deletionPolicy, this))
{
    Plasma::DataEngineManager *manager = Plasma::DataEngineManager::self();
    manager->loadEngine("publictransport");
    manager->loadEngine("geolocation");
    manager->loadEngine("openstreetmap");
}

StopFinder::~StopFinder()
{
    delete d_ptr;
    Plasma::DataEngineManager *manager = Plasma::DataEngineManager::self();
    manager->unloadEngine("publictransport");
    manager->unloadEngine("geolocation");
    manager->unloadEngine("openstreetmap");
}

void StopFinder::start()
{
    Plasma::DataEngine *engine = Plasma::DataEngineManager::self()->engine("geolocation");
    engine->connectSource( "location", this );
}

void StopFinder::dataUpdated( const QString& sourceName, const Plasma::DataEngine::Data& data )
{
    Q_D( StopFinder );

    if ( sourceName.startsWith(QLatin1String("Stops"), Qt::CaseInsensitive) ) {
        Plasma::DataEngine *engine = Plasma::DataEngineManager::self()->engine("publictransport");
        engine->disconnectSource( sourceName, this );
        d->processPublicTransportData( data );
    } else if ( sourceName == QLatin1String("location") ) {
        Plasma::DataEngine *engine = Plasma::DataEngineManager::self()->engine("geolocation");
        engine->disconnectSource( sourceName, this );
        d->processGeolocationData( data );
    } else if ( sourceName.contains("publictransportstops") ) {
        bool finished = d->processOpenStreetMapData( data );
        if ( finished || (d->foundStops.count() + d->stopsToBeChecked.count()) >= d->resultLimit ) {
            Plasma::DataEngine *engine = Plasma::DataEngineManager::self()->engine("openstreetmap");
            engine->disconnectSource( sourceName, this );
        }
    }
}

StopFinder::Mode StopFinder::mode() const {
    Q_D( const StopFinder );
    return d->mode;
}

} // namespace Timetable
