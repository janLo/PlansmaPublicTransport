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

#include "htmldelegate.h"
#include <QTextDocument>
#include <QTextBlock>
#include <QTextLayout>
#include <QDebug>
#include <qabstractitemview.h>
#include <QTreeView>
#include <QAbstractTextDocumentLayout>
#include <Plasma/PaintUtils>
#include <QPainter>
#include "global.h"
#include <Plasma/Svg>
#include <plasma/framesvg.h>
#include <qmath.h>

HtmlDelegate::HtmlDelegate() : QItemDelegate() {
    m_alignText = false;
}

/*
// TODO: simplify this... (not neede anymore?)
void HtmlDelegate::plainTextToHTMLCharPos( const QString& sHtml, int pos, int* htmlPos ) const
{
    int i = 0;
    bool insideHtmlTag = false;
    *htmlPos = 0;

//     qDebug() << "   HTML " << sHtml << "pos" << pos << "plainTextCount" << plainTextCount;

    // Get pos in html
    if ( pos != 0 )
	while( pos >= 0 && i < sHtml.count() )
	{
	    QChar ch = sHtml[i];
	    if ( ch == '<' )
		insideHtmlTag = true;
	    else if ( pos == 0 )
		break;

	    if ( !insideHtmlTag )
		--pos;
	    ++*htmlPos;
	    ++i;

	    if ( ch == '>' )
		insideHtmlTag = false;
	}

    if ( *htmlPos > 0 )
	--*htmlPos;
}

// TODO: ...and simplify that (not neede anymore?)
void HtmlDelegate::plainTextToHTMLCharCount( const QString &sHtml, int pos, int plainTextCount, int *htmlPos, int *htmlCount ) const
{
    int i = 0;
    bool insideHtmlTag = false;
    *htmlCount = 0;

    plainTextToHTMLCharPos( sHtml, pos, htmlPos );

    i = *htmlPos;
    while( plainTextCount >= 0 && i < sHtml.count() )
    {
	QChar ch = sHtml[i];
	if ( ch == '<' )
	    insideHtmlTag = true;
	else if ( plainTextCount == 0 )
	    break;

	if ( !insideHtmlTag )
	    --plainTextCount;
	++*htmlCount;
	++i;

	if ( ch == '>' )
	    insideHtmlTag = false;
    }

//     qDebug() << "   HTML " << "htmlPos" << *htmlPos << "htmlCount" << *htmlCount;
}*/

void HtmlDelegate::paint ( QPainter* painter, const QStyleOptionViewItem& option,
			   const QModelIndex& index ) const {
    QRect rect = option.rect;

    drawBackground( painter, option, index );
    if ( index.data( TextBackgroundRole ).isValid() ) {
	QStringList data = index.data( TextBackgroundRole ).toStringList();

	// TODO: load the svg only once and change when the current theme is changed
	Plasma::FrameSvg *svg = new Plasma::FrameSvg( parent() );
	// 	Plasma::Svg *svg = new Plasma::Svg(  );
	if ( Plasma::Theme::defaultTheme()->currentThemeHasImage("widgets/frame") )
	    svg->setImagePath( "widgets/frame" );
	else
	    svg->setImagePath( "widgets/tooltip" );
	svg->setElementPrefix( data.at(0) );
	svg->resizeFrame( rect.size() );

	if ( data.contains("drawFrameForWholeRow") ) {
	    svg->setEnabledBorders(Plasma::FrameSvg::TopBorder | Plasma::FrameSvg::BottomBorder);
	    if ( index.column() == 0 )
		svg->setEnabledBorders(svg->enabledBorders() | Plasma::FrameSvg::LeftBorder);
	    else if ( index.column() == index.model()->columnCount() - 1 )
		svg->setEnabledBorders(svg->enabledBorders() | Plasma::FrameSvg::RightBorder);
	}

// 	painter->save();
// 	QPixmap bgPixmap( rect.size() );
// 	QPainter p( &bgPixmap );
// // 	painter->setClipRegion( svg->mask().translated(rect.topLeft()) );
// // 	bgPixmap.fill( Qt::transparent );
// // 	p.translate( -rect.topLeft() );
// // 	drawBackground( &p, option, index );
// 	p.fillRect( 0, 0, rect.width(), rect.height(), Qt::red );
//
// 	p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
// 	p.drawPixmap( 0, 0, svg->alphaMask() );
// 	p.end();
// 	painter->drawPixmap( rect, bgPixmap );
// 	painter->restore();

	svg->paintFrame( painter, rect.topLeft() );
    }
//     else
// 	drawBackground( painter, option, index );

//     int size = qMin(rect.width(), rect.height());
    QString formattedText = index.data( FormattedTextRole ).toString();
    QString text = formattedText.isEmpty() ? index.data( Qt::DisplayRole ).toString() : formattedText;
//     qDebug() << index.data( Qt::DecorationRole );
    
    QSize iconSize;
    if ( index.data( IconSizeRole ).isValid() )
	iconSize = index.data( IconSizeRole ).toSize();
    else
	iconSize = option.decorationSize;
    
    int margin = 4, padding = 2;
    QRect displayRect;
    if ( index.data( Qt::DecorationRole ).isValid()
	    && !index.data( Qt::DecorationRole ).value<QIcon>().isNull() ) {
	QIcon icon = index.data( Qt::DecorationRole ).value<QIcon>();
	QPoint topLeft;

	DecorationPosition decorationPos;
	if ( index.data( DecorationPositionRole ).isValid() )
	    decorationPos = static_cast<DecorationPosition>(
		    index.data( DecorationPositionRole ).toInt() );
	else
	    decorationPos = Left;
	
	if ( decorationPos == Left ) {
	    topLeft = option.rect.topLeft() +
		    QPoint(margin, (rect.height() - iconSize.height()) / 2);
	    displayRect = QRect( rect.topLeft() +
		    QPoint(margin + iconSize.width() + padding, 0), rect.bottomRight() );
	} else { // decorationPos == Right
	    topLeft = rect.topRight() + QPoint(-margin - iconSize.width(),
					       (rect.height() - iconSize.height()) / 2);
	    displayRect = QRect( rect.topLeft(), rect.bottomRight() -
		    QPoint(margin + iconSize.width() + padding, 0) );
	}

	QRect decorationRect( topLeft, topLeft + QPoint(iconSize.width(), iconSize.height()) );
	drawDecoration( painter, option, decorationRect, icon.pixmap(iconSize) );
    } else { // no decoration
	displayRect = rect;
	if ( m_alignText && !index.data(GroupTitleRole).isValid() )
	    displayRect.adjust(margin + iconSize.width() + padding, 0, 0, 0);
    }

    if ( index.data(DrawBabkgroundGradientRole).isValid() ) {
	QPixmap pixmap( option.rect.width(), option.rect.height() / 2 );
	pixmap.fill( Qt::transparent );
	QPainter p( &pixmap );
	
	QLinearGradient bgGradient( 0, 0, 0, 1 );
	bgGradient.setCoordinateMode( QGradient::ObjectBoundingMode );
	bgGradient.setColorAt( 0, QColor(72, 72, 72, 0) );
	bgGradient.setColorAt( 1, QColor(72, 72, 72, 40) );
	p.fillRect( 0, 0, option.rect.width(), option.rect.height() / 2, bgGradient );

	// Fade out left and right
	QLinearGradient alphaGradient1( 0, 0, 1, 0 );
	QLinearGradient alphaGradient2( 1, 0, 0, 0 );
	alphaGradient1.setCoordinateMode( QGradient::ObjectBoundingMode );
	alphaGradient2.setCoordinateMode( QGradient::ObjectBoundingMode );
	alphaGradient1.setColorAt( 0, QColor(0, 0, 0, 0) );
	alphaGradient1.setColorAt( 1, QColor(0, 0, 0, 255) );
	alphaGradient2.setColorAt( 0, QColor(0, 0, 0, 0) );
	alphaGradient2.setColorAt( 1, QColor(0, 0, 0, 255) );
	p.setCompositionMode( QPainter::CompositionMode_DestinationIn );
	p.fillRect( 0, 0, option.rect.width() / 5, option.rect.height() / 2, alphaGradient1 );
	p.fillRect( option.rect.right() - option.rect.width() / 5 - option.rect.left(), 0,
		    option.rect.width() / 5 + 1, option.rect.height() / 2, alphaGradient2 );
	p.end();
	
	painter->drawPixmap( rect.left(), rect.top() + rect.height() / 2, pixmap );
    }

    if ( index.data(GroupTitleRole).isValid() )
	drawDisplayWithShadow( painter, option, displayRect, text, true );
    else
	drawDisplayWithShadow( painter, option, displayRect, text );

//     painter->save();
//     painter->setRenderHint(QPainter::Antialiasing, true);
//     if (option.state & QStyle::State_Selected)
//     painter->restore();

//     QItemDelegate::paint ( painter, option, index );
}

void HtmlDelegate::drawDecoration( QPainter* painter, const QStyleOptionViewItem& option,
				   const QRect& rect, const QPixmap& pixmap ) const {
    // Add 2 pixels around the pixmap to have enough space for the shadow
    QRect rcBuffer = rect.adjusted( -2, -2, 2, 2 );
    QPixmap bufferPixmap( rcBuffer.size() );
    bufferPixmap.fill( Qt::transparent );

    QPainter p( &bufferPixmap );
    QRect rcPixmap = rect.translated( -rect.topLeft() ).translated( 2, 2 );
    QItemDelegate::drawDecoration( &p, option, rcPixmap, pixmap );

    // Draw shadow
    QColor shadowColor;
    if ( qGray(option.palette.foreground().color().rgb()) > 192 )
        shadowColor = Qt::white;
    else
        shadowColor = Qt::darkGray;

    QImage shadow = bufferPixmap.toImage();
    Plasma::PaintUtils::shadowBlur( shadow, 2, shadowColor );

    if ( shadowColor == Qt::white )
        painter->drawImage( rcBuffer.topLeft(), shadow );
    else
        painter->drawImage( rcBuffer.topLeft() + QPoint(1, 2), shadow );

    painter->drawPixmap( rcBuffer.topLeft(), bufferPixmap );
}

void HtmlDelegate::drawDisplayWithShadow( QPainter* painter,
					  const QStyleOptionViewItem& option,
					  const QRect& rect, const QString& text,
					  bool bigContrastShadow ) const {
    painter->setRenderHint(QPainter::Antialiasing);

    int margin = 3;
    int lineCount = 0, maxLineCount = qMax( qFloor(rect.height() / option.fontMetrics.lineSpacing()), 1 );
    QRect textRect = rect.adjusted(margin, 0, -margin, 0);
    QTextDocument document;
    document.setDefaultFont( option.font );
    QColor textColor = option.palette.foreground().color();
//     qDebug() << "HtmlDelegate::drawDisplay" << text << textColor;
    QTextOption textOption( option.displayAlignment );
    textOption.setTextDirection( option.direction );
    if ( maxLineCount == 1 )
	textOption.setWrapMode( QTextOption::NoWrap );
    else if ( text.contains("<br>") )
	textOption.setWrapMode( QTextOption::ManualWrap );
    else if ( text.count(' ') == 0 )
	textOption.setWrapMode( QTextOption::WrapAtWordBoundaryOrAnywhere );
    else
	textOption.setWrapMode( QTextOption::WordWrap );
    document.setDefaultTextOption( textOption );

    QString sStyleSheet = QString("body { color:rgba(%1,%2,%3,%4); margin-left: %5px; margin-right: %5px; }")
	.arg( textColor.red() )
	.arg( textColor.green() )
	.arg( textColor.blue() )
	.arg( textColor.alpha() )
	.arg( margin );
    document.setDefaultStyleSheet( sStyleSheet );

    QString sText = text;
    // Here "<br-wrap>" is a special line break that doesn't change wrapping behaviour
    sText.replace( "<br-wrap>", "<br>" );
    if ( sText.indexOf("<body>") == -1 )
	sText = sText.prepend("<body>").append("</body>");

    document.setHtml( sText );
    document.setDocumentMargin( 0 );
    document.documentLayout();

    // From the tasks plasmoid
    // Create the alpha gradient for the fade out effect
    QLinearGradient alphaGradient(0, 0, 1, 0);
    alphaGradient.setCoordinateMode(QGradient::ObjectBoundingMode);
    if ( option.direction == Qt::LeftToRight )
    {
        alphaGradient.setColorAt(0, QColor(0, 0, 0, 255));
        alphaGradient.setColorAt(1, QColor(0, 0, 0, 0));
    } else
    {
        alphaGradient.setColorAt(0, QColor(0, 0, 0, 0));
        alphaGradient.setColorAt(1, QColor(0, 0, 0, 255));
    }
    // Right-aligned text would be aligned far too far on the right ;)
    if ( maxLineCount == 1 && !option.displayAlignment.testFlag(Qt::AlignRight) &&
	!option.displayAlignment.testFlag(Qt::AlignCenter) &&
	!option.displayAlignment.testFlag(Qt::AlignHCenter) )
	document.setPageSize( QSize(99999, textRect.height()) );
    else
	document.setPageSize( textRect.size() );

    for ( int b = 0; b < document.blockCount(); ++b )
	lineCount += document.findBlockByLineNumber( b ).layout()->lineCount();
//     qDebug() << lineCount << text << "max=" << maxLineCount;
    if ( lineCount > maxLineCount )
	lineCount = maxLineCount;
//     qDebug() << rect.height() << "/" << option.fontMetrics.lineSpacing() << "=" << rect.height() / option.fontMetrics.lineSpacing() << ", lineCount=" << lineCount;

    int textHeight = lineCount * option.fontMetrics.lineSpacing() - option.fontMetrics.leading();

    QPointF position = textHeight < textRect.height() ?
	QPointF(0, (textRect.height() - textHeight) / 2.0f/* + (option.fontMetrics.tightBoundingRect("M").height() - option.fontMetrics.xHeight())*/) : QPointF(0, 0);
    QList<QRect> fadeRects;
    int fadeWidth = 30;

    QPixmap pixmap( textRect.size() );
    pixmap.fill( Qt::transparent );

    QPainter p( &pixmap );
    p.setPen( painter->pen() );

    for ( int b = 0; b < document.blockCount(); ++b ) {
	QTextLayout *textLayout = document.findBlockByLineNumber( b ).layout();

	for ( int l = 0; l < textLayout->lineCount(); ++l ) {
	    QTextLine textLine = textLayout->lineAt( l );
// 	    if ( l == maxLineCount - 1 ) // make last line "infinitly" long, but doesn't work when not layouting..
// 		textLine.setLineWidth(9999999); // also doesn't work for right-aligned text!
	    textLine.draw( &p, position );

	    // Add a fade out rect to the list if the line is too long
	    if ( textLine.naturalTextWidth() + 5 > textRect.width() ) {
		int x = int(qMin(textLine.naturalTextWidth(), (qreal)textRect.width())) - fadeWidth + 5;
		int y = int(textLine.position().y() + position.y());
		QRect r = QStyle::visualRect(textLayout->textOption().textDirection(), pixmap.rect(),
					    QRect(x, y, fadeWidth, int(textLine.height())));
		fadeRects.append(r);
	    }
	}
    }

    // Reduce the alpha in each fade out rect using the alpha gradient
    if ( !fadeRects.isEmpty() ) {
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        foreach (const QRect &rect, fadeRects)
            p.fillRect(rect, alphaGradient);
    }
    p.end();

    QColor shadowColor;
    if ( qGray(textColor.rgb()) > 192 )
        shadowColor = Qt::black;
    else if ( bigContrastShadow && qGray(option.palette.background().color().rgb()) > 192 )
	shadowColor = Qt::darkGray;
    else
        shadowColor = Qt::white;

    QImage shadow = pixmap.toImage();
    Plasma::PaintUtils::shadowBlur( shadow, /*bigContrastShadow ? 4 :*/ 2, shadowColor );

    if ( shadowColor == Qt::white )
        painter->drawImage( rect.topLeft(), shadow );
    else
        painter->drawImage( rect.topLeft() + QPoint(1, 2), shadow );

    painter->drawPixmap( rect.topLeft(), pixmap );

//     QItemDelegate::drawDisplay( painter, option, rect, text );
}

QSize HtmlDelegate::sizeHint ( const QStyleOptionViewItem& option,
			       const QModelIndex& index ) const {
    QSize size = QItemDelegate::sizeHint ( option, index );

    if ( index.data( LinesPerRowRole ).isValid() ) {
	int lines = qMax( index.data( LinesPerRowRole ).toInt(), 1 );
// 	size.setHeight( option.fontMetrics.lineSpacing() * lines + 4 );
	int height = option.fontMetrics.boundingRect( 0, 0, size.width(), 999999, 0, "AlpfIgj(" ).height();
	// size.setHeight( (option.fontMetrics.lineSpacing() * 1.2f) * lines + 4 );
	size.setHeight( (height + option.fontMetrics.leading()) * lines + 4 );
    }
    else
	size.setHeight( option.fontMetrics.lineSpacing() + 4 );

    return size;
}
