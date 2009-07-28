
#include <QDebug>
#include <QVariant>
#include "departureinfo.h"
#include <qmath.h>

JourneyInfo::JourneyInfo( const QString &operatorName, const QVariantList& vehicleTypesVariant, const QDateTime& departure, const QDateTime& arrival, const QString& pricing, const QString& startStopName, const QString& targetStopName, int duration, int changes, const QString &journeyNews ) {
    QList<VehicleType> vehicleTypes;
    foreach( QVariant vehicleType, vehicleTypesVariant )
	vehicleTypes.append( static_cast<VehicleType>(vehicleType.toInt()) );
    init( operatorName, vehicleTypes, departure, arrival, pricing, startStopName, targetStopName, duration, changes, journeyNews );
}

void JourneyInfo::init( const QString &operatorName, const QList< VehicleType >& vehicleTypes, const QDateTime& departure, const QDateTime& arrival, const QString& pricing, const QString& startStopName, const QString& targetStopName, int duration, int changes, const QString &journeyNews ) {
    this->operatorName = operatorName;
    this->vehicleTypes = vehicleTypes;
    this->departure = departure;
    this->arrival = arrival;
    this->pricing = pricing;
    this->startStopName = startStopName;
    this->targetStopName = targetStopName;
    this->duration = duration;
    this->changes = changes;
    this->journeyNews = journeyNews;
}

QList< QVariant > JourneyInfo::vehicleTypesVariant() const {
    QList<QVariant> vehicleTypesVariant;
    foreach( VehicleType vehicleType, vehicleTypes )
	vehicleTypesVariant.append( static_cast<int>(vehicleType) );
    return vehicleTypesVariant;
}

QString JourneyInfo::durationToDepartureString( bool toArrival ) const {
    int totalSeconds = QDateTime::currentDateTime().secsTo( toArrival ? arrival : departure );
//     if ( -totalSeconds / 3600 >= 23 )
// 	totalSeconds += 24 * 3600;

    int seconds = totalSeconds % 60;
    totalSeconds -= seconds;
    if (seconds > 0)
	totalSeconds += 60;
    if (totalSeconds < 0)
	return i18n("depart is in the past");

    return Global::durationString(totalSeconds).replace(' ', "&nbsp;");
}


QString DepartureInfo::durationString () const {
    int totalSeconds = QDateTime::currentDateTime().secsTo( predictedDeparture() );
    if ( -totalSeconds / 3600 >= 23 )
	totalSeconds += 24 * 3600;
    int seconds = totalSeconds % 60;
    totalSeconds -= seconds;
    if (seconds > 0)
	totalSeconds += 60;
    int remainingDelay = delayType() == Delayed ? delay : 0;
    if (totalSeconds < 0) {
	if (!(delayType() == Delayed && -totalSeconds > delay * 60))
	    return i18n("depart is in the past");
	else
	    remainingDelay -= qFloor( (float)totalSeconds / 60.0f );
    }

    int minutes = (totalSeconds / 60) % 60;
    int hours = totalSeconds / 3600;
    QString str;

    if ( delayType() == Delayed ) {
	if (hours > 0) {
	    if (minutes > 0)
		str = i18nc("h:mm + delay in minutes", "%1:%2 + %3 minutes", hours, QString("%1").arg(minutes, 2, 10, QLatin1Char('0')), remainingDelay);
	    else
		str = i18ncp("Hour(s) + delay in minutes", "%1 hour + %2 minutes", "in %1 hours + %2 minutes", hours, remainingDelay);
	} else if (minutes > 0)
	    str = i18nc("Minute(s) + delay in minutes", "%1 + %2 minutes", minutes, remainingDelay);
	else if (hours == 0 && minutes == 0)
	    str = i18nc("Now + delay in minutes", "now + %1 minutes", remainingDelay );
	else
	    str = i18nc("+ remaining delay in minutes", "+ %1 minutes", remainingDelay );
    } else {
	if (hours > 0) {
	    if (minutes > 0)
		str = i18nc("h:mm", "%1:%2 hours", hours, QString("%1").arg(minutes, 2, 10, QLatin1Char('0')));
	    else
		str = i18np("%1 hour", "%1 hours", hours);
	}
	else if (minutes > 0)
	    str = i18np("%1 minute", "%1 minutes", minutes);
	else
	    str = i18n("now");
    }

    return str.replace(' ', "&nbsp;");;
}

void DepartureInfo::init ( const QString &operatorName, const QString &line, const QString &target, const QDateTime &departure, VehicleType lineType, LineServices lineServices, const QString &platform, int delay, const QString &delayReason, const QString &journeyNews ) {
    QRegExp rx ( "[0-9]*$" );
    rx.indexIn ( line );
    if ( rx.isValid() )
        this->lineNumber = rx.cap().toInt();
    else
        this->lineNumber = 0;

    this->operatorName = operatorName;
    this->lineString = line;
    this->target = target;
    this->departure = departure;
    this->vehicleType = lineType;
    this->lineServices = lineServices;
    this->platform = platform;
    this->delay = delay;
    this->delayReason = delayReason;
    this->journeyNews = journeyNews;
}

bool operator< ( const JourneyInfo& ji1, const JourneyInfo& ji2 ) {
    return ji1.departure < ji2.departure;
}

bool operator < ( const DepartureInfo &di1, const DepartureInfo &di2 ) {
    return di1.predictedDeparture() < di2.predictedDeparture();
}
