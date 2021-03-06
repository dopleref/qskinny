/******************************************************************************
 * QSkinny - Copyright (C) 2016 Uwe Rathmann
 * This file may be used under the terms of the QSkinny License, Version 1.0
 *****************************************************************************/

#include "QskDirtyItemFilter.h"
#include "QskWindow.h"
#include "QskControl.h"

#include <QQuickWindow>

QSK_QT_PRIVATE_BEGIN
#include <private/qquickwindow_p.h>
#include <private/qquickitem_p.h>
QSK_QT_PRIVATE_END

static inline bool qskIsUpdateBlocked( const QQuickItem* item )
{
    if ( !item->isVisible() )
    {
        if ( const auto control = qobject_cast< const QskControl* >( item ) )
            return control->testControlFlag( QskControl::DeferredUpdate );
    }
    return false;
}

static inline void qskBlockDirty( QQuickItem* item, bool on )
{
    if ( qskIsUpdateBlocked( item ) )
        QQuickItemPrivate::get( item )->componentComplete = !on;

    const auto children = item->childItems();
    for ( auto child : children )
        qskBlockDirty( child, on );
}

QskDirtyItemFilter::QskDirtyItemFilter( QObject* parent ):
    QObject( parent )
{
}

QskDirtyItemFilter::~QskDirtyItemFilter()
{
}

void QskDirtyItemFilter::addWindow( QQuickWindow* window )
{
    if ( m_windows.contains( window ) )
        return;

    m_windows.insert( window );

    /*
        Depending on the configration the scene graph runs on
        a different thread and we need a direct connection to
        filter the dirty list in the scene graph thread.

        As QObject::sender() is not valid, when using a direct
        connection between different threads, we need an
        extra lambda to pass window as parameter.
     */

    connect( window, &QQuickWindow::beforeSynchronizing,
        window, [ this, window ] { beforeSynchronizing( window ); },
        Qt::DirectConnection );

    connect( window, &QObject::destroyed, this, &QskDirtyItemFilter::cleanUp );
}

void QskDirtyItemFilter::beforeSynchronizing( QQuickWindow* window )
{
    filterDirtyList( window, qskIsUpdateBlocked );

    if ( QQuickWindowPrivate::get( window )->renderer == nullptr )
    {
        /*
            In this specific initial situation QQuickWindow updates
            the dirtyList after emitting "beforeSynchronizing".
            So we need a special hack setting/resetting componentComplete
            to avoid having all items in the dirtyList.
         */
        qskBlockDirty( window->contentItem(), true );
        connect( window, &QQuickWindow::afterSynchronizing,
            this, &QskDirtyItemFilter::resetBlockedDirty );
    }
}

void QskDirtyItemFilter::resetBlockedDirty()
{
    auto window = qobject_cast< QQuickWindow* >( sender() );
    Q_ASSERT( window );

    qskBlockDirty( window->contentItem(), false );
    disconnect( window, &QQuickWindow::afterSynchronizing,
        this, &QskDirtyItemFilter::resetBlockedDirty );
}

void QskDirtyItemFilter::cleanUp( QObject* window )
{
    disconnect( window );
    m_windows.remove( window );
}

void QskDirtyItemFilter::filterDirtyList( QQuickWindow* window,
    bool ( *isBlocked )( const QQuickItem* ) )
{
    if ( window == nullptr )
        return;

    auto d = QQuickWindowPrivate::get( window );
    for ( QQuickItem* item = d->dirtyItemList; item != nullptr; )
    {
        auto nextItem = QQuickItemPrivate::get( item )->nextDirtyItem;

        if ( isBlocked( item ) )
            QQuickItemPrivate::get( item )->removeFromDirtyList();

        item = nextItem;
    }
}

