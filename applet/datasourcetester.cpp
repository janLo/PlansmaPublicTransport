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

#include "datasourcetester.h"


void DataSourceTester::connectTestSource()
{
    if ( !m_testSource.isEmpty() ) {
        m_publicTransportEngine->connectSource( m_testSource, this );
    }
}

void DataSourceTester::disconnectTestSource()
{
    if ( !m_testSource.isEmpty() ) {
        m_publicTransportEngine->disconnectSource( m_testSource, this );
        m_testSource = "";
    }
}

void DataSourceTester::setTestSource( const QString& sourceName )
{
    disconnectTestSource();

    m_testSource = sourceName;
    connectTestSource();
}

QString DataSourceTester::stopToStopID( const QString& stopName )
{
    return m_mapStopToStopID.value( stopName, "" ).toString();
}

void DataSourceTester::clearStopToStopIdMap()
{
    m_mapStopToStopID.clear();
}

void DataSourceTester::dataUpdated( const QString& sourceName, const Plasma::DataEngine::Data& data )
{
    Q_UNUSED( sourceName );
    if ( data.isEmpty() ) {
        return;
    }
    disconnectTestSource();

    // Check for errors from the data engine
    if ( data.value( "error" ).toBool() ) {
        emit testResult( Error, i18nc( "@info/plain", "The stop name is invalid." ), QVariant(), QVariant() );
    } else {
        // Check if we got a possible stop list or a journey list
        if ( data.contains("stops") ) {
            processTestSourcePossibleStopList( data );
        } else {
            // List of journeys received
            disconnectTestSource();
            emit testResult( JourneyListReceived, QVariant(), QVariant(), QVariant() );
        }
    }
}

void DataSourceTester::processTestSourcePossibleStopList( const Plasma::DataEngine::Data& data )
{
    disconnectTestSource();

    QStringList stopNames;
    QVariantHash stopToStopID;
    QVariantHash stopToStopWeight;
    QVariantList stops = data["stops"].toList();
    foreach ( const QVariant &stopData, stops ) {
        QVariantHash stop = stopData.toHash();
        QString sStopName = stop["StopName"].toString();
        QString sStopID = stop["StopID"].toString();
        int stopWeight = stop["StopWeight"].toInt();
        stopNames.append( sStopName );
        stopToStopID.insert( sStopName, sStopID );
        stopToStopWeight.insert( sStopName, stopWeight );

        m_mapStopToStopID.insert( sStopName, sStopID );
    }
    emit testResult( PossibleStopsReceived, stopNames, stopToStopID, stopToStopWeight );
}
