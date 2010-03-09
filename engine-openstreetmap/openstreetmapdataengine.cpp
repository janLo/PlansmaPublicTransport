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

#include "openstreetmapdataengine.h"

#include <KLocale>
#include <kio/jobclasses.h>
#include <kio/job.h>
#include <QBuffer>

OpenStreetMapEngine::OpenStreetMapEngine( QObject *parent, const QVariantList &args )
	    : Plasma::DataEngine(parent, args) {
    // Update maximally every 5 mins, openstreetmap data doesn't change too much
    setMinimumPollingInterval( 300000 );
		
    // Fill list of 'short filters'.
    // TODO: Maybe plurals are better here?
    m_shortFilter.insert( "bank", Filter(Node, "amenity=bank") );
    m_shortFilter.insert( "cafe", Filter(Node, "amenity=cafe") );
    m_shortFilter.insert( "cinema", Filter(Node, "amenity=cinema") );
    m_shortFilter.insert( "college", Filter(Node, "amenity=college") );
    m_shortFilter.insert( "fastfood", Filter(Node, "amenity=fast_food") );
    m_shortFilter.insert( "hospital", Filter(Node, "amenity=hospital") );
    m_shortFilter.insert( "library", Filter(Node, "amenity=library") );
    m_shortFilter.insert( "nightclub", Filter(Node, "amenity=nightclub") );
    m_shortFilter.insert( "parking", Filter(Node, "amenity=parking") );
    m_shortFilter.insert( "pharmacy", Filter(Node, "amenity=pharmacy") );
    m_shortFilter.insert( "placeofworship", Filter(Node, "amenity=place_of_worship") );
    m_shortFilter.insert( "police", Filter(Node, "amenity=police") );
    m_shortFilter.insert( "postbox", Filter(Node, "amenity=post_box") );
    m_shortFilter.insert( "postoffice", Filter(Node, "amenity=post_office") );
    m_shortFilter.insert( "pub", Filter(Node, "amenity=pub") );
    m_shortFilter.insert( "publicbuilding", Filter(Node, "amenity=public_building") );
    m_shortFilter.insert( "restaurant", Filter(Node, "amenity=restaurant") );
    m_shortFilter.insert( "school", Filter(Node, "amenity=school") );
    m_shortFilter.insert( "telephone", Filter(Node, "amenity=telephone") );
    m_shortFilter.insert( "theatre", Filter(Node, "amenity=theatre") );
    m_shortFilter.insert( "toilets", Filter(Node, "amenity=toilets") );
    m_shortFilter.insert( "townhall", Filter(Node, "amenity=townhall") );
    m_shortFilter.insert( "university", Filter(Node, "amenity=university") );
    
    m_shortFilter.insert( "water", Filter(Node, "natural=water") );
    m_shortFilter.insert( "forest", Filter(Node, "natural=forest") );
    m_shortFilter.insert( "park", Filter(Node, "natural=park") );
    
    m_shortFilter.insert( "publictransportstops",
			  Filter(Relation, "public_transport=stop_area") );
}

bool OpenStreetMapEngine::sourceRequestEvent( const QString& source ) {
//     kDebug() << "Request" << source;
    setData( source, DataEngine::Data() ); // Create source, TODO: check if [source] is valid?
    return updateSourceEvent( source );
}

bool OpenStreetMapEngine::updateSourceEvent( const QString& source ) {
    foreach ( JobInfo jobInfo, m_jobInfos ) {
	if ( jobInfo.sourceName == source ) {
	    kDebug() << "Source gets already updated" << source;
	    return true;
	}
    }
    kDebug() << "Update" << source;
    
    // Parse the source name
    // "[longitude],[latitude] ([mapArea]) ([element] [filter]|[short-filter])"
    // mapArea should be < 0.5, otherwise parsing could take some time.
    int pos = source.indexOf( " " );
    int pos2 = source.indexOf( " ", pos + 1 );
    if ( pos == -1 )
	return false;

    // First comes latitude,longitute
    QStringList values = source.left( pos ).split( "," );
    if ( values.count() != 2 )
	return false;
    double latitude = values[0].toDouble(); // Latitude first
    double longitude = values[1].toDouble();
    double mapBoxSize; // Size of the area, in which to search

    // Then the size of the area to search in
    // or the elements to filter, which can be "node", "way" or "relation".
    // Can also be a 'short filter', see below (search area and element omitted)
    QString maybeElement = source.mid( pos + 1, pos2 - pos - 1 ).toLower();
    bool ok;
    mapBoxSize = maybeElement.toDouble( &ok );
    
    QString element;
    if ( ok ) {
	// Area given, use next word as element or 'short filter'
	int pos3 = source.indexOf( " ", pos2 + 1 );
	maybeElement = pos3 == -1 ? source.mid( pos2 + 1 ).toLower()
		       : source.mid( pos2 + 1, pos3 - pos2 - 1 ).toLower();
	pos2 = pos3;

	// Prevent too big areas
	if ( mapBoxSize > maxAreaSize )
	    mapBoxSize = maxAreaSize;
    } else {
	// Use default area size
	mapBoxSize = defautAreaSize;
    }

    QString sFilter;
    OsmReader::ResultFlags resultFlags = OsmReader::AllResults;
    if ( m_shortFilter.contains(maybeElement) ) {
	// Replace 'short filters', like "hospital" -> "amenity=hospital" (with element="node")
	Filter filter = m_shortFilter[ maybeElement ];
	element = elementToString( filter.element );
	sFilter = filter.filter;

	if ( maybeElement == "publictransportstops" )
	    resultFlags |= OsmReader::OnlyResultsWithNameAttribute;
    } else {
	// A custom filter
	element = maybeElement;
	sFilter = source.mid( pos2 + 1 ).trimmed();
    }

    // Tell visualizations that not all data has been read
    setData( source, "finished", false );
    
    // Build url
    QString osmUrl = QString( "%1%2[%3][bbox=%4,%5,%6,%7]" )
	    .arg( baseUrl(), element, sFilter )
	    .arg( longitude - mapBoxSize/2 ).arg( latitude - mapBoxSize/2 )
	    .arg( longitude + mapBoxSize/2 ).arg( latitude + mapBoxSize/2 );
    kDebug() << "URL:" << osmUrl;

    // Start download
    KIO::TransferJob *job = KIO::get( osmUrl, KIO::NoReload, KIO::HideProgressInfo );
    connect( job, SIGNAL(data(KIO::Job*,QByteArray)),
	     this, SLOT(data(KIO::Job*,QByteArray)) );
    connect( job, SIGNAL(result(KJob*)), this, SLOT(finished(KJob*)) );

    // Store source name and reader associated with the job
    OsmReader *osmReader = new OsmReader( source, resultFlags );
    connect( osmReader, SIGNAL(chunkRead(OsmReader*,Plasma::DataEngine::Data)),
	     this, SLOT(osmChunkRead(OsmReader*,Plasma::DataEngine::Data)) );
    connect( osmReader, SIGNAL(finishedReading(OsmReader*,Plasma::DataEngine::Data)),
	     this, SLOT(osmFinishedReading(OsmReader*,Plasma::DataEngine::Data)) );
    m_jobInfos.insert( job, JobInfo(source, osmReader) );
    return true;
}

void OpenStreetMapEngine::data( KIO::Job* job, const QByteArray& ba ) {
    JobInfo &jobInfo = m_jobInfos[ job ]; // Get associated infos

    jobInfo.osmReader->addData( ba );
    if ( jobInfo.readStarted ) {
	// Continue reading
	jobInfo.osmReader->resumeReading(); 
    } else {
	// Start reading if not already started
	jobInfo.readStarted = true;
	jobInfo.osmReader->read();
    }
}

void OpenStreetMapEngine::finished( KJob* job ) {
    // Remove the finished job from the job info hash
    m_jobInfos.remove( job );
}

void OpenStreetMapEngine::osmChunkRead( OsmReader *osmReader,
					const Plasma::DataEngine::Data &data ) {
    // Update data
    if ( !data.isEmpty() )
	setData( osmReader->associatedSourceName(), data );
}

void OpenStreetMapEngine::osmFinishedReading( OsmReader* osmReader,
					      const Plasma::DataEngine::Data& data ) {
    // Update data
    if ( !data.isEmpty() )
	setData( osmReader->associatedSourceName(), data );
    
    // Tell visualizations that all data has been read
    setData( osmReader->associatedSourceName(), "finished", true );

    // Kill still running jobs (probably receiving a NULL string)
    QHash< KJob*, JobInfo >::const_iterator it;
    for( it = m_jobInfos.constBegin(); it != m_jobInfos.constEnd(); ++it ) {
	if ( it.value().osmReader == osmReader ) {
	    // Don't get more data, when the XML reader has completed reading
	    // otherwise the data engine crashes (because osmReader gets deleted here)
	    it.key()->kill( KJob::EmitResult );
	    break;
	}
    }

    // Delete the finished reader
    osmReader->deleteLater();
}

QString OpenStreetMapEngine::elementToString( OpenStreetMapEngine::Element element ) const {
    switch ( element ) {
	case Node: 	return "node";
	case Relation: 	return "relation";
	case Way: 	return "way";

	default:
	    kDebug() << "Element unknown";
	    return "";
    }
}


K_EXPORT_PLASMA_DATAENGINE(openstreetmap, OpenStreetMapEngine)

#include "build/openstreetmapdataengine.moc"
