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

#ifndef CHECKCOMBOBOX_HEADER
#define CHECKCOMBOBOX_HEADER

#include <KComboBox>

class CheckComboboxPrivate;
/** A combobox to select multiple items of the list by adding check boxes.
* If no or one item is selected KComboBox paints the combobox in the default way.
* If more than one item is selected, the icons of all selected items are painted
* and the text shows how many items are selected ("x/y"). */
class CheckCombobox : public KComboBox {
    Q_OBJECT
    Q_PROPERTY( int allowNoCheckedItem READ allowNoCheckedItem WRITE setAllowNoCheckedItem )
    Q_PROPERTY( QModelIndexList checkedItems READ checkedItems WRITE setCheckedItems )
    
    public:
	/** Creates a new CheckCombobox. */
	CheckCombobox( QWidget* parent = 0 );

	/** Destructor. */
	~CheckCombobox();

	/** Gets whether or not it's allowed that no item is checked.
	* If this is false, the last checked item can't be unchecked. */
	bool allowNoCheckedItem() const;
	/** Sets whether or not it's allowed that no item is checked.
	* If set to false, the last checked item can't be unchecked (true is default) */
	void setAllowNoCheckedItem( bool allow = true );

	/** Returns a list of indices of the model that are currently checked. */
	QModelIndexList checkedItems() const;
	/** Sets all items for the given @p indices checked. All other items get
	* unchecked. */
	void setCheckedItems( const QModelIndexList &indices );
	/** Sets the check state of the given @p index to @p checkState. */
	void setItemCheckState( const QModelIndex &index, Qt::CheckState checkState );
	/** Checks if the model has at least @p count checked items. */
	bool hasCheckedItems( int count = 1 ) const;
	
    signals:
	/** Emitted when an item's check state changes. */
	void checkedItemsChanged();
	
    protected:
	CheckComboboxPrivate* const d_ptr;
	/** Reimplemented to change the check state of the current item when 
	* space is pressed. */
	virtual void keyPressEvent( QKeyEvent *event );
	/** Reimplemented to not close the drop down list if an item is clicked
	* but instead toggle it's check state. */
	virtual bool eventFilter( QObject *object, QEvent *event );
	/** Reimplemented to paint multiple checked items. */
	virtual void paintEvent( QPaintEvent* );
	/** Reimplemented to give enough space for multiple selected item's icons. */
	virtual QSize sizeHint() const;
	
    private:
	Q_DECLARE_PRIVATE( CheckCombobox )
};

#endif // Multiple inclusion guard