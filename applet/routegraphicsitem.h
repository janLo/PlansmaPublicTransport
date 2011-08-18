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

#ifndef ROUTEGRAPHICSITEM_HEADER
#define ROUTEGRAPHICSITEM_HEADER

#include <QGraphicsWidget>
#include <QPointer>
#include <Plasma/Svg>
#include "global.h"

class DepartureModel;
class KMenu;
class JourneyItem;
class DepartureItem;
class RouteStopTextGraphicsItem;
class StopAction;

/**
 * @brief A QGraphicsWidget showing an icon for a single route stop of a public transport vehicle.
 **/
class RouteStopMarkerGraphicsItem : public QGraphicsWidget {
    Q_OBJECT
    Q_PROPERTY( qreal hoverStep READ hoverStep WRITE setHoverStep )

public:
    /**
     * @brief The type of a RouteStopMarkerGraphicsItem.
     **/
    enum MarkerType {
        DefaultStopMarker = 0, /**< A default route stop marker. */
        IntermediateStopMarker = 1 /**< A route stop marker for intermediate
                * stops that are omitted / not displayed. */
    };

    explicit RouteStopMarkerGraphicsItem( QGraphicsItem* parent = 0,
                                          RouteStopTextGraphicsItem *textItem = 0,
                                          MarkerType markerType = DefaultStopMarker,
                                          RouteStopFlags stopFlags = RouteStopIsIntermediate );

    /** @brief Gets the radius of the marker circle. */
    qreal radius() const;

    /** @brief Gets the marker type of this item. */
    MarkerType markerType() const { return m_markerType; };

    /**
     * @brief Gets flags for the associated stop.
     *
     * This method also checks the model for the currently highlighted stop / home stop and sets
     * RouteStopIsHighlighted / RouteStopIsHomeStop in the return value if neccessary.
     **/
    RouteStopFlags routeStopFlags() const;

    RouteStopTextGraphicsItem *textItem() const { return m_textItem; };

    virtual void paint( QPainter* painter, const QStyleOptionGraphicsItem* option,
                        QWidget* widget = 0 );

    qreal hoverStep() const { return m_hoverStep; };
    void setHoverStep( qreal expandStep );

    virtual QRectF boundingRect() const {
        return QRectF( -radius() * 1.5, -radius() * 1.5, 2 * radius() * 1.5, 2 * radius() * 1.5 );
    };
    virtual QPainterPath shape() const { QPainterPath p; p.addEllipse(boundingRect()); return p; };

    virtual QSizeF sizeHint( Qt::SizeHint which, const QSizeF& constraint = QSizeF() ) const {
        if ( which == Qt::MinimumSize || which == Qt::MaximumSize ) {
            return QSizeF( 2 * radius(), 2 * radius() );
        } else {
            return QGraphicsWidget::sizeHint( which, constraint );
        }
    };

signals:
    void hovered( RouteStopMarkerGraphicsItem *item );
    void unhovered( RouteStopMarkerGraphicsItem *item );

public slots:
    void hover();
    void unhover();

protected:
    virtual void hoverEnterEvent( QGraphicsSceneHoverEvent* event );
    virtual void hoverLeaveEvent( QGraphicsSceneHoverEvent* event );

private:
    qreal m_hoverStep;
    MarkerType m_markerType;
    RouteStopFlags m_stopFlags;
    RouteStopTextGraphicsItem *m_textItem;
};

/**
 * @brief A QGraphicsWidget showing the stop name a single route stop of a public transport vehicle.
 *
 * On hover it expands too show all of the given stop text (if it is too long).
 **/
class RouteStopTextGraphicsItem : public QGraphicsWidget {
    Q_OBJECT
    Q_PROPERTY( qreal expandStep READ expandStep WRITE setExpandStep )

public:
    /**
     * @brief Create a new route stop text item.
     *
     * This class visualizes the text shown for a stop in a route.
     * RouteStopMarkerGraphicsItem is responsible for showing a stop icon on a "route line".
     * 
     * RouteGraphicsItem normally is the parent of both classes (this and
     * RouteStopMarkerGraphicsItem). RouteGraphicsItem creates these items for each route stop
     * to be shown.
     *
     * @param parent The parent item.
     * @param model The model which includes the route stop item this RouteStopTextGraphicsItem
     *   visualizes.
     * @param font The font to use.
     * @param baseSize A zooming factor.
     * @param time The time at which the vehicle is at this route stop.
     * @param stopName The name of the stop this RouteStopTextGraphicsItem visualizes.
     * @param minsFromFirstRouteStop The time in minutes the vehicle needs from the first stop of
     *   the route until this route stop.
     * @param stopFlags Flags of this route stop, eg. whether or not this is the current home stop.
     **/
    RouteStopTextGraphicsItem( QGraphicsItem* parent, DepartureModel *model, const QFont &font,
                               qreal baseSize, const QTime &time, const QString &stopName,
                               int minsFromFirstRouteStop,
                               RouteStopFlags stopFlags = RouteStopIsIntermediate );

    /** @brief Gets the stop text (stop name including time information). */
    QString stopText() const { return m_stopText; };

    /** @brief Gets the name of the associated stop. */
    QString stopName() const { return m_stopName; };

    /**
     * @brief Gets flags for the associated stop.
     *
     * This method also checks the model for the currently highlighted stop / home stop and sets
     * RouteStopIsHighlighted / RouteStopIsHomeStop in the return value if neccessary.
     **/
    RouteStopFlags routeStopFlags() const;

    /**
     * @brief Sets information about the new associated stop.
     *
     * @param time The time when the vehicle is at the associated stop.
     *
     * @param stopName The name of the associated stop.
     *
     * @param minsFromFirstRouteStop The time when the vehicle is at the associated stop,
     *   relative to the first stop. At the first stop this is 0. Use the default value
     *   (999999) if the times isn't known.
     **/
    void setStop( const QTime &time, const QString &stopName,
                  int minsFromFirstRouteStop = 999999 );

    qreal expandStep() const { return m_expandStep; };
    void setExpandStep( qreal expandStep );
    qreal baseSize() const { return m_baseSize; };
    void setBaseSize( qreal baseSize );

    virtual void paint( QPainter* painter, const QStyleOptionGraphicsItem* option,
                        QWidget* widget = 0 );

    DepartureModel *model() const;

signals:
    void hovered( RouteStopTextGraphicsItem *item );
    void unhovered( RouteStopTextGraphicsItem *item );

public slots:
    void hover();
    void unhover();

protected:
    virtual void hoverEnterEvent( QGraphicsSceneHoverEvent* event );
    virtual void hoverLeaveEvent( QGraphicsSceneHoverEvent* event );
    virtual void contextMenuEvent( QGraphicsSceneContextMenuEvent* event );

private:
    QString m_stopText;
    QString m_stopName;
    qreal m_expandStep;
    qreal m_baseSize;
    RouteStopFlags m_stopFlags;
    DepartureModel *m_model;
};

/**
 * @brief A QGraphicsWidget showing the route of a public transport vehicle.
 *
 * The route is shown as a thick white line with stop markers on it. For each
 * stop marker the associated stop name is shown with it's departure time.
 * This widget automatically shows/hides stops on size changes.
 **/
class RouteGraphicsItem : public QGraphicsWidget {
    Q_OBJECT

public:
    RouteGraphicsItem( QGraphicsItem* parent, DepartureItem *item,
                       StopAction *copyStopToClipboardAction = 0, StopAction *showInMapAction = 0,
                       StopAction *showDeparturesAction = 0, StopAction *highlightStopAction = 0,
                       StopAction *newFilterViaStopAction = 0 );

    void updateData( DepartureItem *item );

    void setZoomFactor( qreal zoomFactor = 1.0 );
    qreal zoomFactor() const { return m_zoomFactor; };
    inline qreal padding() const { return 5.0; };

    virtual void paint( QPainter* painter, const QStyleOptionGraphicsItem* option,
                        QWidget* widget = 0 );

    const DepartureItem *item() const { return m_item.data(); };

protected:
    virtual void resizeEvent( QGraphicsSceneResizeEvent* event );
    void arrangeStopItems();
    virtual inline void showEvent( QShowEvent* ) { arrangeStopItems(); };

private:
    QPointer<DepartureItem> m_item;
    QList<RouteStopMarkerGraphicsItem*> m_markerItems;
    QList<RouteStopTextGraphicsItem*> m_textItems;
    qreal m_zoomFactor;
    qreal m_textAngle;
    qreal m_maxTextWidth;
    StopAction *m_copyStopToClipboardAction;
    StopAction *m_showInMapAction;
    StopAction *m_showDeparturesAction;
    StopAction *m_highlightStopAction;
    StopAction *m_newFilterViaStopAction;
};

class JourneyRouteGraphicsItem;
/**
 * @brief A QGraphicsWidget showing a single route stop of a journey.
 **/
class JourneyRouteStopGraphicsItem : public QGraphicsWidget {
    Q_OBJECT

public:
    JourneyRouteStopGraphicsItem( JourneyRouteGraphicsItem* parent, const QPixmap &vehiclePixmap,
                                  const QString &text, RouteStopFlags routeStopFlags,
                                  const QString &stopName );

    void setText( const QString &text );

    QRectF infoTextRect() const;

    /**
     * @brief Gets flags for the associated stop.
     *
     * This method also checks the model for the currently highlighted stop / home stop and sets
     * RouteStopIsHighlighted / RouteStopIsHomeStop in the return value if neccessary.
     **/
    RouteStopFlags routeStopFlags() const;

    QString stopName() const { return m_stopName; };

    void setZoomFactor( qreal zoomFactor = 1.0 );
    qreal zoomFactor() const { return m_zoomFactor; };

    virtual void paint( QPainter* painter, const QStyleOptionGraphicsItem* option,
                        QWidget* widget = 0 );

protected:
    virtual QSizeF sizeHint( Qt::SizeHint which, const QSizeF& constraint = QSizeF() ) const;
    virtual void contextMenuEvent( QGraphicsSceneContextMenuEvent* event );
    virtual void hoverLeaveEvent( QGraphicsSceneHoverEvent* /*event*/ ) { update(); };

private:
    JourneyRouteGraphicsItem *m_parent;
    QPixmap m_vehiclePixmap;
    QTextDocument *m_infoTextDocument;

    RouteStopFlags m_stopFlags;
    QString m_stopName;
    qreal m_zoomFactor;
};

/**
 * @brief A QGraphicsWidget showing the route of a journey.
 **/
class JourneyRouteGraphicsItem : public QGraphicsWidget {
    Q_OBJECT

public:
    JourneyRouteGraphicsItem( QGraphicsItem *parent, JourneyItem *item, Plasma::Svg *svg,
                              StopAction *copyStopToClipboardAction = 0,
                              StopAction *showInMapAction = 0,
                              StopAction *requestJourneyToStopAction = 0,
                              StopAction *requestJourneyFromStopAction = 0 );

    void updateData( JourneyItem *item );

    void setZoomFactor( qreal zoomFactor = 1.0 ) { m_zoomFactor = zoomFactor; };
    qreal zoomFactor() const { return m_zoomFactor; };
    inline qreal padding() const { return 5.0; };
    Plasma::Svg *svg() const { return m_svg; };
    JourneyItem *journeyItem() const { return m_item; };

    virtual void paint( QPainter* painter, const QStyleOptionGraphicsItem* option,
                        QWidget* widget = 0 );

private:
    QPointer<JourneyItem> m_item;
    Plasma::Svg *m_svg;
    qreal m_zoomFactor;
    QList<JourneyRouteStopGraphicsItem*> m_routeItems;
    StopAction *m_copyStopToClipboardAction;
    StopAction *m_showInMapAction;
    StopAction *m_requestJourneyToStopAction;
    StopAction *m_requestJourneyFromStopAction;
};

#endif // Multiple inclusion guard
