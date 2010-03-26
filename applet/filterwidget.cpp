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

#include "filterwidget.h"

#include "filter.h"
#include "departureinfo.h"
#include "checkcombobox.h"

#include <QLabel>
#include <KGlobal>
#include <KLocale>

FilterWidget::FilterWidget( QWidget* parent,
			    AbstractDynamicWidgetContainer::SeparatorOptions seperatorOptions )
	    : AbstractDynamicLabeledWidgetContainer( RemoveButtonsBesideWidgets,
	      AddButtonBesideFirstWidget, seperatorOptions, QString(), parent ) {
    m_allowedFilterTypes << FilterByVehicleType << FilterByTarget << FilterByVia
	    << FilterByTransportLine << FilterByTransportLineNumber << FilterByDelay;
    setWidgetCountRange( 1, 10, false );
    setAutoRaiseButtons( true );
    setRemoveButtonIcon( "edit-delete" );
}

FilterWidget::FilterWidget( const QList<FilterType> &allowedFilterTypes, QWidget* parent,
			    AbstractDynamicWidgetContainer::SeparatorOptions seperatorOptions )
	    : AbstractDynamicLabeledWidgetContainer( RemoveButtonsBesideWidgets,
	      AddButtonBesideFirstWidget, seperatorOptions, QString(), parent ) {
    if ( allowedFilterTypes.isEmpty() ) {
	m_allowedFilterTypes << FilterByVehicleType << FilterByTarget << FilterByVia
	    << FilterByTransportLine << FilterByTransportLineNumber << FilterByDelay;
    } else {
	m_allowedFilterTypes = allowedFilterTypes;
    }
    setWidgetCountRange( 1, 10, false );
    setAutoRaiseButtons( true );
    setRemoveButtonIcon( "edit-delete" );
}

void FilterWidget::setAllowedFilterTypes( const QList< FilterType >& allowedFilterTypes ) {
    m_allowedFilterTypes = allowedFilterTypes;
}

FilterType FilterWidget::firstUnusedFilterType() const {
    QList< FilterType > filterTypes;
    foreach ( ConstraintWidget* constraint, constraintWidgets() )
	filterTypes << constraint->type();

    foreach ( FilterType filterType, m_allowedFilterTypes ) {
	if ( !filterTypes.contains(filterType) )
	    return filterType;
    }

    return FilterByTarget; // Already used
}

void FilterWidget::setFilter( const Filter& filter ) {
    if ( !dynamicWidgets().isEmpty() ) {
	// Clear old filter widgets
	int minWidgetCount = minimumWidgetCount();
	int maxWidgetCount = maximumWidgetCount();
	setWidgetCountRange();
	removeAllWidgets();
	
	// Setup ConstraintWidgets from filters
	foreach ( Constraint constraint, filter )
	    addConstraint( constraint );
	
	// Restor widget count range
	setWidgetCountRange( minWidgetCount, maxWidgetCount );
    } else {
	foreach ( Constraint constraint, filter )
	    addConstraint( constraint );
    }
}
void FilterWidget::addConstraint( ConstraintWidget* filter ) {
    KComboBox *cmbFilterType = qobject_cast< KComboBox* >( createNewLabelWidget(0) );
    Q_ASSERT( cmbFilterType );
    DynamicWidget *dynamicWidget = addWidget( cmbFilterType, filter );
    if ( !dynamicWidget )
	kDebug() << "COULDN'T ADD CONSTRAINT";
}

void FilterWidget::removeConstraint( ConstraintWidget *widget ) {
    removeWidget( widget );
}

int FilterWidget::removeWidget( QWidget* widget ) {
    ConstraintWidget *filter = qobject_cast< ConstraintWidget* >( widget );
    Constraint removedConstraint;
    if ( filter )
	removedConstraint = filter->constraint();
	
    int index = AbstractDynamicLabeledWidgetContainer::removeWidget( widget );
    if ( index != -1 ) {
	m_filterTypes.removeAt( index );
	emit changed();
	if ( filter )
	    emit constraintRemoved( removedConstraint );
    }
    return index;
}

ConstraintWidget* ConstraintWidget::create( FilterType type, FilterVariant variant,
					    const QVariant &value, QWidget *parent ) {
    switch ( type ) {
    case FilterByVehicleType:
    {
	QList< VehicleType > filterVehicleTypes;
	QList< ConstraintListWidget::ListItem > values;
	filterVehicleTypes = QList< VehicleType >()
		<< Unknown << Tram << Bus << TrolleyBus << Subway << TrainInterurban
		<< Metro << TrainRegional << TrainRegionalExpress << TrainInterregio
		<< TrainIntercityEurocity << TrainIntercityExpress << Ferry << Plane;
        foreach ( VehicleType vehicleType, filterVehicleTypes ) {
	    values << ConstraintListWidget::ListItem(
		    Global::vehicleTypeToString(vehicleType),
		    static_cast<int>(vehicleType),
		    Global::vehicleTypeToIcon(vehicleType) );
        }
        return new ConstraintListWidget( type, variant, values, value.toList(), parent );
    }

    case FilterByTransportLine:
    case FilterByTarget:
    case FilterByVia:
	return new ConstraintStringWidget( type, variant, value.toString(), parent );

    case FilterByTransportLineNumber:
    case FilterByDelay:
	return new ConstraintIntWidget( type, variant, value.toInt(), 0, 10000, parent );

    case FilterByDeparture:
	return new ConstraintTimeWidget( type, variant, value.toTime(), parent );
	
    case FilterByDayOfWeek:
    {
	QList< int > filterDaysOfWeek;
	QList< ConstraintListWidget::ListItem > values;
	int weekStartDay = KGlobal::locale()->weekStartDay();
	for ( int weekday = weekStartDay; weekday <= 7; ++weekday ) {
	    filterDaysOfWeek << weekday;
	    if ( (weekStartDay > 1 && weekday == weekStartDay - 1)
		 || (weekStartDay == 1 && weekday == 7) )
	    {
		break;
	    } else if ( weekday == 7 )
		weekday = 1;
	}
	
	foreach ( int dayOfWeek, filterDaysOfWeek ) {
	    values << ConstraintListWidget::ListItem(
		    QDate::longDayName(dayOfWeek, QDate::StandaloneFormat),
		    dayOfWeek, KIcon() );
        }
        ConstraintListWidget *listWidget = new ConstraintListWidget( type,
				    variant, values, value.toList(), parent );
	listWidget->list()->setAllSelectedText( i18n("(all days)") );
	listWidget->list()->setMultipleSelectionOptions( CheckCombobox::ShowStringList );
        return listWidget;
    }

    default:
        kDebug() << "Unknown filter type" << type;
        return NULL;
    }
}

ConstraintWidget* FilterWidget::createConstraint( FilterType type ) {
    switch ( type ) {
    case FilterByVehicleType:
	return ConstraintWidget::create( type, FilterIsOneOf,
					 QVariantList() << Unknown, this );

    case FilterByTransportLine:
    case FilterByTarget:
    case FilterByVia:
	return ConstraintWidget::create( type, FilterContains, QString(), this );

    case FilterByTransportLineNumber:
    case FilterByDelay:
	return ConstraintWidget::create( type, FilterEquals, 0, this );

    case FilterByDeparture:
	return ConstraintWidget::create( type, FilterEquals, QTime::currentTime(), this );
    case FilterByDayOfWeek:
	return ConstraintWidget::create( type, FilterIsOneOf,
			QVariantList() << 1 << 2 << 3 << 4 << 5 << 6 << 7, this );

    default:
        kDebug() << "Unknown filter type" << type;
        return NULL;
    }
}

QString FilterWidget::filterName( FilterType filter ) const {
    switch ( filter ) {
    case FilterByVehicleType:
        return i18n("Vehicle");
    case FilterByTransportLine:
        return i18n("Line string");
    case FilterByTransportLineNumber:
	return i18n("Line number");
    case FilterByTarget:
        return i18n("Target");
    case FilterByVia:
	return i18n("Via");
    case FilterByDelay:
	return i18n("Delay");
    case FilterByDeparture:
	return i18n("Departure");
    case FilterByDayOfWeek:
	return i18n("Day of Week");

    default:
        kDebug() << "Filter unknown" << filter;
        return QString();
    }
}

void FilterWidget::filterTypeChanged( int index ) {
    if( index < 0 ) {
	kDebug() << "No new index (-1)";
	return;
    }

    KComboBox *cmbFilterType = qobject_cast< KComboBox* >( sender() );
    if ( !cmbFilterType ) // sender is this, called from addConstraint()...
        cmbFilterType = m_filterTypes.last(); // ...therefore the combo box is the last in the list
    Q_ASSERT( cmbFilterType );
    int filterIndex = m_filterTypes.indexOf( cmbFilterType );

    FilterType type = static_cast< FilterType >( cmbFilterType->itemData(index).toInt() );
    ConstraintWidget *newConstraint = createConstraint( type );
    dynamicWidgets().at( filterIndex )->replaceContentWidget( newConstraint );
    connect( newConstraint, SIGNAL(changed()), this, SLOT(changed()) );
    emit changed();
}

QString ConstraintWidget::filterVariantName( FilterVariant filterVariant ) const {
    switch ( filterVariant ) {
    case FilterContains:
        return i18n("Contains");
    case FilterDoesntContain:
        return i18n("Does not Contain");
    case FilterEquals:
        return i18n("Equals");
    case FilterDoesntEqual:
        return i18n("Does not Equal");
    case FilterMatchesRegExp:
        return i18n("Matches Regular Expr.");
    case FilterDoesntMatchRegExp:
        return i18n("Doesn't Match Reg. Expr.");
	
    case FilterIsOneOf:
	return i18n("One of");
    case FilterIsntOneOf:
	return i18n("None of");

    case FilterGreaterThan:
	return i18n("Greater Than");
    case FilterLessThan:
	return i18n("Less Than");

    default:
        kDebug() << "Filter variant unknown" << filterVariant;
        return QString();
    }
}

ConstraintWidget::ConstraintWidget( FilterType type,
			    QList< FilterVariant > availableVariants,
			    FilterVariant initialVariant, QWidget* parent )
			    : QWidget( parent ) {
    m_constraint.type = type;
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
//     QHBoxLayout *layout = new QHBoxLayout( this );
    QFormLayout *layout = new QFormLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );
    layout->setRowWrapPolicy( QFormLayout::WrapLongRows );
    setLayout( layout );

    if ( !availableVariants.isEmpty() ) {
	if ( !availableVariants.contains(initialVariant) ) {
	    kDebug() << "Initial variant" << initialVariant
		     << "not found in" << availableVariants << "for type" << type;
	    initialVariant = availableVariants.first();
	}
	
        m_variantsCmb = new KComboBox( this );
	m_variantsCmb->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Fixed );
        foreach ( FilterVariant variant, availableVariants ) {
            m_variantsCmb->addItem( filterVariantName(variant),
                                    static_cast<int>(variant) );
        }
        int index = m_variantsCmb->findData( static_cast<int>(initialVariant) );
	connect( m_variantsCmb, SIGNAL(currentIndexChanged(int)),
		 this, SLOT(variantChanged(int)) );
	m_variantsCmb->setCurrentIndex( index );
	m_constraint.variant = initialVariant;
        layout->addWidget( m_variantsCmb );
    } else
	m_constraint.variant = FilterNoVariant;
}

ConstraintListWidget::ConstraintListWidget( FilterType type, FilterVariant initialVariant,
				    const QList< ListItem > &values,
				    const QVariantList& initialValues, QWidget* parent )
				    : ConstraintWidget( type, QList< FilterVariant >()
				      << FilterIsOneOf << FilterIsntOneOf,
				      initialVariant, parent ) {
    m_list = new CheckCombobox( this );
    QStandardItemModel *model = new QStandardItemModel( this );
    foreach ( const ListItem &listItem, values ) {
	QStandardItem *item = new QStandardItem( listItem.icon, listItem.text );
	item->setData( listItem.value, Qt::UserRole );
        item->setFlags( Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable );
        item->setData( Qt::Unchecked, Qt::CheckStateRole );
        model->appendRow( item );
    }
    m_list->setModel( model );
    m_list->setAllowNoCheckedItem( false );
    addWidget( m_list );

    setValue( initialValues );
    checkedItemsChanged();

    connect( m_list, SIGNAL(checkedItemsChanged()),
             this, SLOT(checkedItemsChanged()) );
}

ConstraintStringWidget::ConstraintStringWidget( FilterType type, FilterVariant initialVariant,
					const QString& filterText, QWidget* parent )
					: ConstraintWidget( type, QList< FilterVariant >()
					  << FilterContains << FilterDoesntContain
					  << FilterEquals << FilterDoesntEqual
					  << FilterMatchesRegExp << FilterDoesntMatchRegExp,
					  initialVariant, parent ) {
    m_string = new KLineEdit( this );
    m_string->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    m_string->setMinimumWidth( 50 );
    m_string->setClearButtonShown( true );
    m_string->setText( filterText );
    m_string->setMinimumWidth( 60 );
    addWidget( m_string );

    connect( m_string, SIGNAL(textChanged(QString)), this, SLOT(stringChanged(QString)) );
}

ConstraintIntWidget::ConstraintIntWidget( FilterType type, FilterVariant initialVariant,
				  int value, int min, int max, QWidget* parent )
				  : ConstraintWidget( type, QList< FilterVariant >()
				    << FilterEquals << FilterDoesntEqual
				    << FilterGreaterThan << FilterLessThan,
				    initialVariant, parent ) {
    m_num = new KIntSpinBox( this );
    m_num->setRange( min, max );
    m_num->setValue( value );
    addWidget( m_num );

    connect( m_num, SIGNAL(valueChanged(int)), this, SLOT(intChanged(int)) );
}

ConstraintTimeWidget::ConstraintTimeWidget( FilterType type,
			FilterVariant initialVariant, QTime value, QWidget* parent)
			: ConstraintWidget( type, QList< FilterVariant >()
					    << FilterEquals << FilterDoesntEqual
					    << FilterGreaterThan << FilterLessThan,
					    initialVariant, parent ) {
    m_time = new QTimeEdit( this );
    value.setHMS( value.hour(), value.minute(), 0 );
    m_time->setTime( value );
    addWidget( m_time );

    connect( m_time, SIGNAL(timeChanged(QTime)), this, SLOT(timeChanged(QTime)) );
}

void ConstraintListWidget::setValue( const QVariant& value ) {
    QModelIndexList indices;
    if ( value.isValid() ) {
	QVariantList values = value.toList();
	foreach ( QVariant value, values ) {
	    QModelIndex index = indexFromValue( value );
	    if ( index.isValid() )
		indices << index;
	    else
		kDebug() << "Value" << value << "not found";
	}
    }

    m_list->setCheckedItems( indices );
}

QModelIndex ConstraintListWidget::indexFromValue( const QVariant& value ) {
    QModelIndexList indices = m_list->model()->match(
	    m_list->model()->index(0, 0), Qt::UserRole, value, 1, Qt::MatchExactly );
    
    return indices.isEmpty() ? QModelIndex() : indices.first();
}

FilterListWidget::FilterListWidget( QWidget* parent ) : AbstractDynamicWidgetContainer(
			    RemoveButtonsBesideWidgets, AddButtonAfterLastWidget,
			    ShowSeparators, parent ) {
    setWidgetCountRange( 1, 10, false );
    addButton()->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    addButton()->setText( i18n("&Add Filter") );
    addButton()->setToolTip( i18n("Add another filter") );
}

void ConstraintWidget::variantChanged( int index ) {
    FilterVariant newVariant = static_cast< FilterVariant >(
	    m_variantsCmb->itemData(index).toInt() );

    if ( m_constraint.variant != newVariant ) {
	m_constraint.variant = newVariant;
// 	bool visible = true; //m_variant != FilterDisregard;
// 	foreach ( QWidget *w, m_widgets )
// 	    w->setVisible( visible );
	emit changed();
    }
}
	
QWidget* FilterWidget::createNewLabelWidget( int ) {
    KComboBox *cmbFilterType = new KComboBox( this );
    foreach ( FilterType filterType, m_allowedFilterTypes ) {
	cmbFilterType->addItem( filterName(filterType) + ":",
				static_cast<int>(filterType) );
    }
    return cmbFilterType;
}

QWidget* FilterWidget::createSeparator( const QString& separatorText ) {
    return AbstractDynamicWidgetContainer::createSeparator(
	    separatorText.isEmpty() ? m_separatorText : separatorText );
}

DynamicWidget* FilterWidget::addWidget( QWidget* labelWidget, QWidget* widget ) {
    KComboBox *cmbFilterType = qobject_cast< KComboBox* >( labelWidget );
    Q_ASSERT_X( cmbFilterType, "FilterWidget::addWidget",
		"Wrong label widget type or NULL label widget" );
    DynamicWidget *dynamicWidget =
	AbstractDynamicLabeledWidgetContainer::addWidget( labelWidget, widget );
    if ( dynamicWidget ) {
	m_filterTypes << cmbFilterType;

	ConstraintWidget *constraintWidget =
		dynamicWidget->contentWidget< ConstraintWidget* >();
	cmbFilterType->setCurrentIndex( cmbFilterType->findData(
		static_cast<int>(constraintWidget->type())) );

	connect( cmbFilterType, SIGNAL(currentIndexChanged(int)),
		this, SLOT(filterTypeChanged(int)) );
	connect( constraintWidget, SIGNAL(changed()), this, SIGNAL(changed()) );

	if ( dynamicWidget->removeButton() )
	    dynamicWidget->removeButton()->setToolTip(
		    i18n("Remove this criterion from the filter") );
	if ( dynamicWidget->addButton() )
	    dynamicWidget->addButton()->setToolTip(
		    i18n("Add another criterion to this filter") );

	emit changed();
	emit constraintAdded( constraintWidget );
    }
    return dynamicWidget;
}
	
DynamicWidget* FilterListWidget::addWidget( QWidget* widget ) {
    DynamicWidget *newWidget = AbstractDynamicWidgetContainer::addWidget( widget );
    if ( newWidget->removeButton() ) {
	newWidget->removeButton()->setToolTip(
		i18n("Remove this filter with all it's criteria") );
    }

    emit changed();
    return newWidget;
}

DynamicWidget* FilterListWidget::createDynamicWidget( QWidget* widget ) {
    // Set spacing between the contained constraint list and the remove filter button
    DynamicWidget *dynamicWidget = AbstractDynamicWidgetContainer::createDynamicWidget( widget );
    dynamicWidget->layout()->setSpacing( 1 );
    return dynamicWidget;
}

QWidget* FilterListWidget::createSeparator( const QString& separatorText ) {
    return AbstractDynamicWidgetContainer::createSeparator(
	    separatorText.isEmpty() ? i18n("or") : separatorText );
}

QList< FilterWidget* > FilterListWidget::filterWidgets() const {
    QList< FilterWidget* > list;
    foreach ( DynamicWidget *dynamicWidget, dynamicWidgets() )
	list << qobject_cast< FilterWidget* >( dynamicWidget->contentWidget() );
    return list;
}

FilterList FilterListWidget::filters() const {
    FilterList list;
    foreach ( DynamicWidget *dynamicWidget, dynamicWidgets() )
	list << qobject_cast< FilterWidget* >( dynamicWidget->contentWidget() )->filter();
    return list;
}

void ConstraintListWidget::checkedItemsChanged() {
    m_values.clear();
    foreach ( QModelIndex index, m_list->checkedItems() )
	m_values << index.data( Qt::UserRole );
    emit changed();
}

