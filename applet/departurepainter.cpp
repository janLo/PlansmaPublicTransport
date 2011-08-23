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

#include "departurepainter.h"
#include "departuremodel.h"
#include "popupicon.h"
#include <publictransporthelper/departureinfo.h>
#include <publictransporthelper/global.h>

#include <Plasma/Theme>
#include <Plasma/Svg>
#include <Plasma/PaintUtils>
#include <qmath.h>

QString DeparturePainter::iconKey( VehicleType vehicle, DeparturePainter::VehicleIconFlags flags )
{
    QString vehicleKey;
    switch ( vehicle ) {
        case Tram: vehicleKey = "tram"; break;
        case Bus: vehicleKey = "bus"; break;
        case TrolleyBus: vehicleKey = "trolleybus"; break;
        case Subway: vehicleKey = "subway"; break;
        case Metro: vehicleKey = "metro"; break;
        case InterurbanTrain: vehicleKey = "interurbantrain"; break;
        case RegionalTrain: vehicleKey = "regionaltrain"; break;
        case RegionalExpressTrain: vehicleKey = "regionalexpresstrain"; break;
        case InterregionalTrain: vehicleKey = "interregionaltrain"; break;
        case IntercityTrain: vehicleKey = "intercitytrain"; break;
        case HighSpeedTrain: vehicleKey = "highspeedtrain"; break;
        case Feet: vehicleKey = "feet"; break;
        case Ship: vehicleKey = "ship"; break;
        case Plane: vehicleKey = "plane"; break;
        default:
            kDebug() << "Unknown vehicle type" << vehicle;
            return QString();
    }

    // Use monochrome (mostly white) icons
    if ( flags.testFlag(MonochromeIcon) ) {
        vehicleKey.append( "_white" );
    }
    if ( flags.testFlag(EmptyIcon) ) {
        vehicleKey.append( "_empty" );
    }

    return vehicleKey;
}

DeparturePainter::VehicleIconFlags DeparturePainter::iconFlagsFromIconDrawFlags(
        DeparturePainter::VehicleIconDrawFlags flags )
{
    VehicleIconFlags iconFlags = ColoredIcon;
    if ( flags.testFlag(DrawMonochromeIcon) ) {
        iconFlags |= MonochromeIcon;
    }
    if ( flags.testFlag(DrawTransportLine) ) {
        iconFlags |= EmptyIcon;
    }
    return iconFlags;
}

void DeparturePainter::paintVehicle( QPainter* painter, VehicleType vehicle,
        const QRectF& rect, const QString& transportLine, int minutesUntilDeparture,
        VehicleIconDrawFlags iconDrawFlags )
{
    bool drawTransportLine = iconDrawFlags.testFlag(DrawTransportLine) && !transportLine.isEmpty()
            && Timetable::Global::generalVehicleType(vehicle) == LocalPublicTransport;
    if ( !drawTransportLine ) {
        // Remove possibly set flag, if the transport line cannot be drawn
        iconDrawFlags &= ~DrawTransportLine;
    }
    VehicleIconFlags iconFlags = iconFlagsFromIconDrawFlags( iconDrawFlags );
    QString vehicleKey = iconKey( vehicle, iconFlags );

    // Resize SVG
    int shadowWidth = qBound( 2, int(rect.width() / 20), 4 );
    m_svg->resize( rect.width() - shadowWidth, rect.height() - shadowWidth );

    QPixmap pixmap( int(rect.width()), int(rect.height()) );
    pixmap.fill( Qt::transparent );
    QPainter p( &pixmap );

    // Draw SVG vehicle type icon
    p.setRenderHint( QPainter::Antialiasing );
    m_svg->paint( &p, shadowWidth / 2, shadowWidth / 2, vehicleKey );

    // Draw transport line string (only for local public transport)
    if ( drawTransportLine ) {
        QString text = transportLine;
        text.replace(' ', QString());

        QFont font = Plasma::Theme::defaultTheme()->font( Plasma::Theme::DefaultFont );
        font.setBold( true );
        if ( text.length() > 2 ) {
            font.setPixelSize( qMax(8, qCeil(1.18 * rect.width() / text.length())) );
        } else {
            font.setPixelSize( rect.width() * 0.5 );
        }
        p.setFont( font );
        QFontMetrics fm( font );
        if ( iconDrawFlags.testFlag(DrawMonochromeIcon) ) {
            // For monochrome icons, draw white text with a dark gray outline
            QPen textOutlinePen( QColor(0, 0, 0, 100) );
            textOutlinePen.setWidthF( qMin(10.0, font.pixelSize() / 5.0) );
            textOutlinePen.setCapStyle( Qt::RoundCap );
            textOutlinePen.setJoinStyle( Qt::RoundJoin );
            QPainterPath textPath;
            textPath.addText( rect.left() + (rect.width() - fm.width(text)) / 2.0,
                            rect.bottom() - (rect.height() - fm.ascent() + fm.descent()) / 2.0,
                            font, text );
            p.setPen( textOutlinePen );
            p.drawPath( textPath );
            p.fillPath( textPath, Qt::white );
        } else {
            // For colored icons simply draw white text
            p.setPen( Qt::white );
            p.drawText( rect, text, QTextOption(Qt::AlignCenter) );
        }
    }

    // Draw icon again, but only partially, dependend on minsToDeparture
    if ( iconDrawFlags.testFlag(DrawTimeGraphics) ) {
        // Draw graphical indication for the time until departure/arrival
        p.setCompositionMode( QPainter::CompositionMode_DestinationIn );
        if ( minutesUntilDeparture >= MAX_MINUTES_UNTIL_DEPARTURE ) {
            // Make the SVG in pixmap 70% transparent
            p.fillRect( pixmap.rect(), QColor(0, 0, 0, 77) );
        } else if ( minutesUntilDeparture > 0 ) {
            // Construct a polygon for the transparency effect visualizing minsToDeparture
            QPolygon polygon;

            // All parts begin with these two points
            const qreal half = pixmap.width() / 2.0;
            const qreal a = (8.0 * minutesUntilDeparture) / MAX_MINUTES_UNTIL_DEPARTURE;
            polygon.append( QPoint(half, half) ); // middle
            polygon.append( QPoint(half, 0) ); // top middle
            if ( minutesUntilDeparture > MAX_MINUTES_UNTIL_DEPARTURE / 8 ) {
                // Paint at least 1/8 black (add point at the top left edge)
                polygon.append( QPoint(0, 0) ); // top left
                if ( minutesUntilDeparture > MAX_MINUTES_UNTIL_DEPARTURE * 3 / 8 ) {
                    // Paint at least 3/8 black (add point at the bottom left edge)
                    polygon.append( QPoint(0, pixmap.height()) ); // bottom left
                    if ( minutesUntilDeparture > MAX_MINUTES_UNTIL_DEPARTURE * 5 / 8 ) {
                        // Paint at least 5/8 black (add point at the bottom right edge)
                        polygon.append( QPoint(pixmap.width(), pixmap.height()) );
                        if ( minutesUntilDeparture > MAX_MINUTES_UNTIL_DEPARTURE * 7 / 8 ) {
                            // Paint [7/8, 8/8] black (add point on the right half of the top side)
                            polygon.append( QPoint(pixmap.width(), 0) ); // top right
                            polygon.append( QPoint(half * (9.0 - a), 0) );
                        } else {
                            // Paint [6/8, 7/8[ black (add point on the right side)
                            polygon.append( QPoint(pixmap.width(), half * (7.0 - a)) );
                        }
                    } else {
                        // Paint [3/8, 5/8[ black (add point on the bottom side)
                        polygon.append( QPoint(half * (a - 3.0), pixmap.height()) );
                    }
                } else { // if ( minsToDeparture > maxMinsToDeparture / 8 ) {
                    // Paint [1/8, 3/8[ black (add point on the left side)
                    polygon.append( QPoint(0, half * (a - 1.0)) );
                }
            } else {
                // Paint [0/8, 1/8[ black (add point on the left half of the top side)
                polygon.append( QPoint(half * (1.0 - a), 0) );
            }

            // Draw 70% transparent parts
            p.setPen( Qt::black );
            p.setBrush( QColor(0, 0, 0, 77) );
            p.drawPolygon( polygon );
            p.end();
        }
    }

    if ( !iconDrawFlags.testFlag(DrawMonochromeIcon) ) {
        // Draw a shadow, but not if a monochrome icon is used
        QImage shadow = pixmap.toImage();
        Plasma::PaintUtils::shadowBlur( shadow, shadowWidth - 1, Qt::black );
        painter->drawImage( rect.topLeft() + QPoint(1, 2), shadow );
    }
    painter->drawPixmap( rect.topLeft(), pixmap );
}

QPixmap DeparturePainter::createMainIconPixmap( const QSize &size ) const
{
    QPixmap pixmap( size );
    pixmap.fill( Qt::transparent );

    const QString mainIconKey = "stop_white";
    if ( !m_svg->hasElement(mainIconKey) ) {
        kDebug() << "SVG element" << mainIconKey << "not found";
        return pixmap;
    }

    QPainter painter( &pixmap );
    m_svg->resize( size );
    m_svg->paint( &painter, 0, 0, mainIconKey );
    painter.end();

    return pixmap;
}

QPixmap DeparturePainter::createDeparturesPixmap( DepartureItem *departure,
        const QSize &size, VehicleIconDrawFlags iconDrawFlags )
{
    QPixmap pixmap( size );
    pixmap.fill( Qt::transparent );
    QPainter p( &pixmap );
    p.setRenderHints( QPainter::Antialiasing | QPainter::TextAntialiasing
                    | QPainter::SmoothPixmapTransform );
    const DepartureInfo *data = departure->departureInfo();
    int minsToDeparture = qCeil( QDateTime::currentDateTime().secsTo(
            data->predictedDeparture()) / 60.0 );

    paintVehicle( &p, data->vehicleType(), pixmap.rect(), data->lineString(),
                  minsToDeparture, iconDrawFlags );

    if ( iconDrawFlags.testFlag(DrawTimeText) ) {
        QString text;
        if ( minsToDeparture < -1 ) {
            text.append( i18nc("Indicating the departure time of an already left vehicle", "left") );
        } else if ( minsToDeparture < 0 ) {
            text.append( i18nc("Indicating the departure time of a currently leaving vehicle", "leaving") );
        } else if ( minsToDeparture == 0 ) {
            text.append( i18nc("Indicating the departure time of a vehicle, that will leave now", "now") );
        } else if ( minsToDeparture >= 60 * 24 ) {
            text.append( i18np("1 day", "%1 days", qRound(minsToDeparture / (6 * 24)) / 10.0) );
        } else if ( minsToDeparture >= 60 ) {
            text.append( i18np("1 hour", "%1 hours", qRound(minsToDeparture / 6) / 10.0) );
        } else {
            text.append( i18np("1 min.", "%1 min.", minsToDeparture) );
        }

        QFont font = Plasma::Theme::defaultTheme()->font( Plasma::Theme::DefaultFont );
        font.setPixelSize( qBound(KGlobalSettings::smallestReadableFont().pixelSize(),
                                  int(pixmap.width() * 0.3), 36) );
        font.setBold( true );
        p.setFont( font );
        QFontMetrics fm( font );
        QRectF textRect( 0, 0, pixmap.width(), pixmap.height() );

        int textWidth = fm.width( text );
        if ( textWidth > textRect.width() ) {
            // Show only the number of minutes, if the whole text doesn't fit
            text = QString::number( minsToDeparture );
            textWidth = fm.width( text );
        }

        text = fm.elidedText( text, Qt::ElideRight, textRect.width() * 1.05 );
        if ( iconDrawFlags.testFlag(DrawMonochromeIcon) ) {
            QPen textOutlinePen( QColor(0, 0, 0, 150) );
            textOutlinePen.setWidthF( qMin(6.0, font.pixelSize() / 3.0) );
            textOutlinePen.setCapStyle( Qt::RoundCap );
            textOutlinePen.setJoinStyle( Qt::RoundJoin );
            QPen textOutlineFinePen( QColor(0, 0, 0, 225) );
            textOutlineFinePen.setCosmetic( true );
            QPainterPath textPath;
            textPath.addText( textRect.left() + (textRect.width() - textWidth) / 2.5,
                            textRect.bottom() - textOutlinePen.width(), font, text );
            p.setPen( textOutlinePen );
            p.drawPath( textPath );
            p.setPen( textOutlineFinePen );
            p.drawPath( textPath );
            p.fillPath( textPath, Qt::white );
        } else {
            QRectF haloRect( textRect.left() + (textRect.width() - textWidth) / 2,
                             textRect.bottom() - fm.height(), textWidth, fm.height() );
            haloRect = haloRect.intersected( textRect ).adjusted( 3, 3, -3, -3 );
            Plasma::PaintUtils::drawHalo( &p, haloRect );

            QTextOption option( Qt::AlignHCenter | Qt::AlignBottom );
            option.setWrapMode( QTextOption::NoWrap );
            p.drawText( textRect, text, option );
        }
    }

    p.end();
    return pixmap;
}

QPixmap DeparturePainter::createAlarmPixmap( DepartureItem* departure, const QSize &size )
{
    QPixmap pixmap = createDeparturesPixmap( departure, size );
    int iconSize = pixmap.width() / 2;
    QPixmap pixmapAlarmIcon = KIcon( "task-reminder" ).pixmap( iconSize );
    QPainter p( &pixmap );
    // Draw alarm icon in the top-right corner.
    p.drawPixmap( pixmap.width() - iconSize - 1, 1, pixmapAlarmIcon );
    p.end();

    return pixmap;
}

QPixmap DeparturePainter::createPopupIcon( PopupIcon *popupIconData, DepartureModel* model,
                                           const QSize &size )
{
    int startDepartureGroupIndex = popupIconData->startDepartureGroupIndex();
    int endDepartureGroupIndex = popupIconData->endDepartureGroupIndex();
    qreal departureGroupIndex = popupIconData->departureGroupIndex();
    qreal departureIndex = popupIconData->departureIndex();
    QPixmap pixmap;
    if ( qFuzzyCompare((qreal)qFloor(departureGroupIndex), departureGroupIndex) ) {
        // If departureGroupIndex is an integer draw without transition between groups
        departureGroupIndex = qBound( model->hasAlarms() ? -1 : 0,
                qFloor(departureGroupIndex), popupIconData->departureGroups()->count() - 1 );

        if ( departureGroupIndex < 0 ) {
            pixmap = createAlarmPixmap( model->nextAlarmDeparture(), size );
        } else {
            QList<DepartureItem*> group = popupIconData->currentDepartureGroup();
            if ( group.isEmpty() ) {
                return pixmap;
            }
            if ( qFuzzyCompare((qreal)qFloor(departureIndex), departureIndex) ) {
                // If departureIndex is an integer draw without transition between departures
                // in the current group
                pixmap = createDeparturesPixmap( group[qFloor(departureIndex) % group.count()], size );
            } else {
                // Draw transition between two departures in the current group
                int startDepartureIndex = qFloor( departureIndex ) % group.count();
                int endDepartureIndex = startDepartureIndex + 1;
                const qreal transition = qBound( 0.0, (departureIndex - startDepartureIndex)
                        / (endDepartureIndex - startDepartureIndex), 1.0 );
                endDepartureIndex %= group.count();
                QPixmap startPixmap = createDeparturesPixmap( group[startDepartureIndex], size );
                QPixmap endPixmap = createDeparturesPixmap( group[endDepartureIndex], size );

                // Make end pixmap transparent
                QColor alpha( 0, 0, 0 );
                alpha.setAlphaF( transition * transition );
                QPainter pEnd( &endPixmap );
                pEnd.setCompositionMode( QPainter::CompositionMode_DestinationIn );
                pEnd.fillRect( startPixmap.rect(), alpha );
                pEnd.end();

                // Mix transparent start and end pixmaps
                pixmap = QPixmap( size );
                pixmap.fill( Qt::transparent );
                QPainter p( &pixmap );
                p.drawPixmap( pixmap.rect(), startPixmap );
                p.setCompositionMode( QPainter::CompositionMode_DestinationOut );
                p.fillRect( pixmap.rect(), alpha );
                p.setCompositionMode( QPainter::CompositionMode_Plus );
                p.drawPixmap( pixmap.rect(), endPixmap );
                p.end();
            }
        }
    } else {
        // Draw transition between two departure groups
        const DepartureGroupList *departureGroups = popupIconData->departureGroups();
        startDepartureGroupIndex = qBound( model->hasAlarms() ? -1 : 0,
                startDepartureGroupIndex, departureGroups->count() - 1 );
        endDepartureGroupIndex = qBound( model->hasAlarms() ? -1 : 0,
                endDepartureGroupIndex, departureGroups->count() - 1 );
        const QList<DepartureItem*> startGroup = startDepartureGroupIndex < 0
                ? QList<DepartureItem*>() : departureGroups->at( startDepartureGroupIndex );
        const QList<DepartureItem*> endGroup = endDepartureGroupIndex < 0
                ? QList<DepartureItem*>() : departureGroups->at( endDepartureGroupIndex );

//         qDebug() << "Animate from" << startDepartureGroupIndex << "to" << endDepartureGroupIndex << "|" << departureGroupIndex;
//         qDebug() << "  or from" << departureGroups->keys()[startDepartureGroupIndex]
//                  << "to" << departureGroups->keys()[endDepartureGroupIndex];
        QPixmap startPixmap = startDepartureGroupIndex < 0
                ? createAlarmPixmap(model->nextAlarmDeparture(), size)
                : createDeparturesPixmap(startGroup[qFloor(departureIndex) % startGroup.count()], size);
        QPixmap endPixmap = endDepartureGroupIndex < 0
                ? createAlarmPixmap(model->nextAlarmDeparture(), size)
                : createDeparturesPixmap(endGroup.first(), size);
        pixmap = QPixmap( size );
        pixmap.fill( Qt::transparent );
        QPainter p( &pixmap );
        p.setRenderHints( QPainter::Antialiasing | QPainter::SmoothPixmapTransform );

        qreal transition, startSize, endSize;
        if ( endDepartureGroupIndex > startDepartureGroupIndex  ) {
            // Move forward to next departure
            transition = qBound( 0.0, (departureGroupIndex - startDepartureGroupIndex)
                    / (endDepartureGroupIndex - startDepartureGroupIndex), 1.0 );
        } else {
            // Mave backward to previous departure
            transition = 1.0 - qBound( 0.0, (startDepartureGroupIndex - departureGroupIndex)
                    / (startDepartureGroupIndex - endDepartureGroupIndex), 1.0 );
            qSwap( startPixmap, endPixmap );
        }
        startSize = (1.0 + 0.25 * transition) * pixmap.width();
        endSize = transition * pixmap.width();

        p.drawPixmap( (pixmap.width() - endSize) / 2 + pixmap.width() * (1.0 - transition) / 2.0,
                      (pixmap.height() - endSize) / 2, endSize, endSize, endPixmap );

        QPixmap startTransitionPixmap( pixmap.size() );
        startTransitionPixmap.fill( Qt::transparent );
        QPainter p2( &startTransitionPixmap );
        p2.drawPixmap( 0, 0, pixmap.width(), pixmap.height(), startPixmap );

        // Make startTransitionPixmap more transparent (for fading)
        p2.setCompositionMode( QPainter::CompositionMode_DestinationIn );
        p2.fillRect( startTransitionPixmap.rect(), QColor(0, 0, 0, 255 * (1.0 - transition * transition)) );
        p2.end();

        p.setTransform( QTransform().rotate(transition * 90, Qt::YAxis) );
        p.drawPixmap( (pixmap.width() - startSize) / 2 - pixmap.width() * transition / 5.0,
                    (pixmap.height() - startSize) / 2,
                    startSize, startSize, startTransitionPixmap );
        p.end();
    }

    return pixmap;
}
