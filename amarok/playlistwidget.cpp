/***************************************************************************
                       playlistwidget.cpp  -  description
                          -------------------
 begin                : Don Dez 5 2002
 copyright            : (C) 2002 by Mark Kretschmann
 email                :
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "playlistwidget.h"
#include "playerapp.h" //FIXME remove the need for this please!
#include "playlistitem.h"
#include "playlistloader.h"
#include "metabundle.h"

#include "amarokconfig.h"

#include <qheader.h> //installEventFilter
#include <qcolor.h>
#include <qevent.h>
#include <qfile.h>
#include <qpainter.h>
#include <qpoint.h>
#include <qrect.h>
#include <qstringlist.h>
#include <qtimer.h>

#include <kdebug.h>
#include <kiconloader.h>
#include <klistview.h>
#include <klocale.h>
#include <kpopupmenu.h>
//#include <krootpixmap.h>
#include <kstandarddirs.h>
#include <kurl.h>
#include <kmessagebox.h>
#include <krandomsequence.h>
#include <kurldrag.h>
#include <kcursor.h>



PlaylistWidget::PlaylistWidget( QWidget *parent, const char *name )
    : KListView( parent, name )
//    , m_rootPixmap( viewport()
    , m_GlowTimer( new QTimer( this ) )
    , m_GlowCount( 100 )
    , m_GlowAdd( 5 )
    , m_pCurrentTrack( 0 )
    , m_undoCounter( 0 )
    , m_tagReader( new TagReader( this ) )
{
    kdDebug() << "PlaylistWidget::PlaylistWidget()" << endl;

    //name has to set here as we _depend_ on the name in a few classes
    setName( "PlaylistWidget" );
    //setFocusPolicy( QWidget::ClickFocus ); //users will expect to tab to this widget, although it's quicker to tab to the lineedit and push "key-down"
    setShowSortIndicator( true );
    setDropVisualizer( false );      //we handle the drawing for ourselves
    setDropVisualizerWidth( 3 );
    setItemsRenameable( true );
    setSorting( 200 );
    setAcceptDrops( true );
    setSelectionMode( QListView::Extended );
    setAllColumnsShowFocus( true );

    //    setStaticBackground( true );
    //    m_rootPixmap.setFadeEffect( 0.5, Qt::black );
    //    m_rootPixmap.start();

    //NOTE order is critical because we can't set indexes or ids
    addColumn( i18n( "Trackname" ), 280 );
    addColumn( i18n( "Title"     ), 200 );
    addColumn( i18n( "Artist"    ), 100 );
    addColumn( i18n( "Album"     ), 100 );
    addColumn( i18n( "Year"      ),  0 ); //0 means hidden
    addColumn( i18n( "Comment"   ),  0 );
    addColumn( i18n( "Genre"     ),  0 );
    addColumn( i18n( "Track"     ),  0 );
    addColumn( i18n( "Directory" ),  0 );
    addColumn( i18n( "Length"    ),  80 );
    addColumn( i18n( "Bitrate"   ),  0 );

    setRenameable( 0, false ); //TODO allow renaming of the filename
    setRenameable( 1 );
    setRenameable( 2 );
    setRenameable( 3 );
    setRenameable( 4 );
    setRenameable( 5 );
    setRenameable( 6 );
    setRenameable( 7 );
    setColumnAlignment( 7, Qt::AlignRight );
    setColumnAlignment( 9, Qt::AlignRight );

    connect( this, SIGNAL( contentsMoving( int, int ) ),  this, SLOT( slotEraseMarker() ) );
    connect( this, SIGNAL( doubleClicked( QListViewItem* ) ), this, SLOT( activate( QListViewItem* ) ) );
    connect( this, SIGNAL( returnPressed( QListViewItem* ) ), this, SLOT( activate( QListViewItem* ) ) );
    connect( this, SIGNAL( rightButtonPressed( QListViewItem*, const QPoint&, int ) ),
             this, SLOT( showContextMenu( QListViewItem*, const QPoint&, int ) ) );
    connect( this, SIGNAL( itemRenamed( QListViewItem*, const QString&, int ) ),
                   SLOT( writeTag( QListViewItem*, const QString&, int ) ) );

    //install header eventFilter
    header()->installEventFilter( this );

    m_GlowColor.setRgb( 0xff, 0x40, 0x40 );

    connect( m_GlowTimer, SIGNAL( timeout() ), this, SLOT( slotGlowTimer() ) );
    m_GlowTimer->start( 70 );

    // Read playlist columns layout
    restoreLayout( KGlobal::config(), "PlaylistColumnsLayout" );

    initUndo();
}


PlaylistWidget::~PlaylistWidget()
{
   saveLayout( KGlobal::config(), "PlaylistColumnsLayout" );

   if( m_tagReader->running() )
   {
      kdDebug() << "Shutting down TagReader..\n";
      m_tagReader->halt();
      m_tagReader->wait();
      kdDebug() << "TagReader Shutdown complete..\n";
   }

   delete m_tagReader;
}



//PUBLIC INTERFACE ===================================================

void PlaylistWidget::insertMedia( const QString &path )
{
   KURL url;
   url.setPath( path );
   insertMedia( url );
}

void PlaylistWidget::insertMedia( const KURL &url )
{
   if( !url.isEmpty() )
   {
      insertMedia( KURL::List( url ), (PlaylistItem *)0 );
   }
}

void PlaylistWidget::insertMedia( const KURL::List &list, bool doclear )
{
  if( !list.isEmpty() )
  {
     if( doclear )
        clear( false );

     insertMedia( list, (PlaylistItem *)0 );
  }
}

void PlaylistWidget::insertMedia( const KURL::List &list, PlaylistItem *after )
{
   kdDebug() << "PlaylistWidget::insertMedia()\n";
   if( after == 0 ) kdDebug() << "Insert After 0\n";

   if( !list.isEmpty() )
   {
      writeUndo();       //remember current state
      setSorting( 200 ); //disable sorting or will not be inserted where we expect

      startLoader( list, after );
   }
}


bool PlaylistWidget::request( RequestType rt, bool b )
{
   //FIXME bug #1 if there's a duplicate of the last track then instead of stopping it will advance to that one!

   PlaylistItem* item = currentTrack();

   if( item == NULL && ( item = restoreCurrentTrack() ) == NULL )
   {
      //no point advancing/receding track since there was no currentTrack!
      rt = Current;

      //if still NULL, then play first selected track
      for( item = (PlaylistItem*)firstChild(); item; item = (PlaylistItem*)item->nextSibling() )
          if( item->isSelected() ) break;

      //if still NULL, then play first track
      if( item == NULL )
         item = (PlaylistItem*)firstChild();

      //if still null then playlist is empty
      //NOTE an initial ( childCount == 0 ) is possible, but this is safer
   }

   switch( rt )
   {
   case Prev:

      //I've talked on a few channels, people hate it when media players restart the current track
      //first before going to the previous one (most players do this), so let's not do it!

      item = (PlaylistItem*)item->itemAbove();

      if ( item == NULL && AmarokConfig::repeatPlaylist() )
      {
         //if repeat then previous track = last track
         item = (PlaylistItem*)lastItem();
      }

      break;

   case Next:

       // random mode
      if( AmarokConfig::randomMode() && childCount() > 3 ) //FIXME is childCount O(1)?
      {
         do
         {
            item = (PlaylistItem *)itemAtIndex( KApplication::random() % childCount() );
         }
         while ( item == currentTrack() );    // try not to play same track twice in a row
      }
      else if( !AmarokConfig::repeatTrack() ) //repeatTrack means keep item the same!
      {
         item = (PlaylistItem*)item->itemBelow();

         if ( item == NULL && AmarokConfig::repeatPlaylist() )
         {
            //if repeat then previous track = last track
            item = (PlaylistItem*)firstChild();
         }
      }

      break;

   case Current:
   default:

      break;
   }

   //this function handles null
   if( b ) activate( item ); //will call setCurrentTrack
   else setCurrentTrack( item );

   //return whether anything new was selected
   return ( item != NULL );
}


void PlaylistWidget::saveM3u( QString fileName )
{
    QFile file( fileName );

    if ( !file.open( IO_WriteOnly ) )
        return ;

    PlaylistItem* item = static_cast<PlaylistItem*>( firstChild() );
    QTextStream stream( &file );
    stream << "#EXTM3U\n";

    while ( item != NULL )
    {
        if ( item->url().protocol() == "file" )
            stream << item->url().path();
        else
        {
            stream << "#EXTINF:-1," + item->text( 0 ) + "\n";
            stream << item->url().url();
        }

        stream << "\n";
        item = static_cast<PlaylistItem*>( item->nextSibling() );
    }

    file.close();
}




// EVENTS =======================================================

void PlaylistWidget::contentsDragEnterEvent( QDragEnterEvent* e )
{
    e->accept();
}

void PlaylistWidget::contentsDragMoveEvent( QDragMoveEvent* e )
{
    QListViewItem *parent;
    QListViewItem *after;
    findDrop( e->pos(), parent, after );

    QRect tmpRect = drawDropVisualizer( 0, parent, after );

    if ( tmpRect != m_marker )
    {
        slotEraseMarker();
        m_marker = tmpRect;
        viewport() ->repaint( tmpRect );
    }
}


void PlaylistWidget::contentsDragLeaveEvent( QDragLeaveEvent* )
{
    slotEraseMarker();
}


void PlaylistWidget::contentsDropEvent( QDropEvent* e )
{
    slotEraseMarker();

    //FIXME perhaps we should drop where the marker was rather than drop point is as if there
    //is an inconsistency we should give the user at least visual coherency

    //just in case
    e->acceptAction();

    QListViewItem *parent, *after;
    findDrop( e->pos(), parent, after );

    if ( e->source() == viewport() )
    {
        setSorting( 200 );
        writeUndo();
        movableDropEvent( parent, after );
    }
    else
    {
       KURL::List urlList;

       if ( KURLDrag::decode( e, urlList ) )
       {
/*
          if ( AmarokConfig::dropMode() == "Ask" )
          {
             for ( KURL::List::Iterator url = media.begin(); url != media.end(); ++url )
             {
                KFileItem file( KFileItem::Unknown, KFileItem::Unknown, *url, true );
                if( file->isDir() ) break;
             }

             if ( url != media.end() )
             {
                QPopupMenu popup( this );
                popup.insertItem( i18n( "Add Recursively" ), this, 0, 1 ) );
                if( popup.exec( mapToGlobal( QPoint( e->pos().x() - 120, e->pos().y() - 20 ) ) );
             }

             //FIXME inform thread of user-decision
          }
*/
          insertMedia( urlList, (PlaylistItem *)after );
       }
    }

    //restoreCurrentTrack(); //why do this here?
}


void PlaylistWidget::customEvent( QCustomEvent *e )
{
   switch( e->type() )
   {
   case 65431: //set the overrideCursor

      //FIXME This is done here rather than startLoader()
      //because Qt DnD sets the overrideCursor and then when it
      //restores the cursor it removes the waitCursor we set!
      //Qt4 may fix this (?) (if we're lucky)
      //FIXME report to Trolltech?

      QApplication::setOverrideCursor( KCursor::workingCursor() );
      break;

   case 65432: //LoaderEvent

      if( PlaylistItem *item = static_cast<PlaylistLoader::LoaderEvent*>(e)->makePlaylistItem( this ) ) //this is thread-safe
      {
         //if returns true we need to load tags and register this item

         if( AmarokConfig::showMetaInfo() ) m_tagReader->append( item );

         //nonlocal downloads can fail <-- <mxcl> what did I mean?!
         searchTokens.append( item->text( 0 ) );
         searchPtrs.append( item );
      }
      break;

   case 65433: //LoaderDoneEvent

       static_cast<PlaylistLoader::LoaderDoneEvent*>(e)->dispose();
       QApplication::restoreOverrideCursor();
       restoreCurrentTrack();
       break;

   case 65434: //TagReaderEvent

      static_cast<TagReader::TagReaderEvent*>(e)->bindTags();
      break;

   case 65435: //TagReaderDoneEvent

      //unsetCursor();
      QApplication::restoreOverrideCursor();
      break;

   default: ;
   }
}


void PlaylistWidget::viewportPaintEvent( QPaintEvent *e )
{
    KListView::viewportPaintEvent( e );

    if ( m_marker.isValid() && e->rect().intersects( m_marker ) )
    {
        QPainter painter( viewport() );
        QBrush brush( Qt::red, QBrush::Dense4Pattern );

        // This is where we actually draw the drop-visualizer
        painter.fillRect( m_marker, brush );
    }
}


//FIXME we don't want to have to include these :)
#include "browserwin.h"
#include <klineedit.h>

void PlaylistWidget::keyPressEvent( QKeyEvent *e )
{
   kdDebug() << "PlaylistWidget::keyPressEvent()\n";

   switch ( e->key() )
   {
   case Qt::Key_Delete:
      removeSelectedItems();
      e->accept();
      break;

   //trust me, I wish there was a better way to do this!
   case Key_0: case Key_1: case Key_2: case Key_3: case Key_4: case Key_5: case Key_6: case Key_7: case Key_8: case Key_9:
   case Key_A: case Key_B: case Key_C: case Key_D: case Key_E: case Key_F: case Key_G: case Key_H: case Key_I: case Key_J: case Key_K: case Key_L: case Key_M: case Key_N: case Key_O: case Key_P: case Key_Q: case Key_R: case Key_S: case Key_T: case Key_U: case Key_V: case Key_W: case Key_X: case Key_Y: case Key_Z:
     {
      KLineEdit *le = pApp->m_pBrowserWin->m_pPlaylistLineEdit;
      le->setFocus();
      QApplication::sendEvent( le, e );
      break;
     }
   default:
      KListView::keyPressEvent( e );
      //the base handler will set accept() or ignore()
   }
}


bool PlaylistWidget::eventFilter( QObject *o, QEvent *e )
{
    if( o == header() && e->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent *>(e)->button() == Qt::RightButton )
    {
        //currently the only use for this filter is to get mouse clicks on the header()
        KPopupMenu popup;
        popup.setCheckable( true );
        popup.insertTitle( "Available Columns" ); //FIXME are you here because this isn't i18n? In that case first think up a better menu-title! Thanks! -mxcl

        for( int i = 0; i < columns(); ++i ) //columns() references a property
        {
            popup.insertItem( columnText( i ), i, i );
            popup.setItemChecked( i, columnWidth( i ) != 0 );
        }

        int col = popup.exec( static_cast<QMouseEvent *>(e)->globalPos() );

        if( col != -1 )
        {
            if( columnWidth( col ) == 0 ) adjustColumn( col );
            else hideColumn( col );
        }

        return TRUE; // eat event

    } else {
        //allow the header to process this
        return KListView::eventFilter( o, e );
    }
}


// PRIVATE METHODS ===============================================

void PlaylistWidget::startLoader( const KURL::List &list, PlaylistItem *after )
{
    //FIXME lastItem() has to go through entire list to find lastItem! Not scalable!
    if( after == 0 ) after = static_cast<PlaylistItem *>(lastItem());
    PlaylistLoader *loader = new PlaylistLoader( list, this, after );

    if( loader )
    {
        QApplication::postEvent( this, new QCustomEvent( 65431 ) ); //see customEvent for explanation

        loader->setOptions( ( AmarokConfig::dropMode() == AmarokConfig::EnumDropMode::Recursively ), AmarokConfig::followSymlinks(), AmarokConfig::browserSortingSpec() );
        loader->start();
    }
    else kdDebug() << "[loader] Unable to create loader-thread!\n";
}


void PlaylistWidget::setCurrentTrack( PlaylistItem *item )
{
    PlaylistItem *tmp = PlaylistItem::GlowItem;
    PlaylistItem::GlowItem = item;
    repaintItem( tmp ); //new glowItem will be repainted by glowTime::timeout()

    //the following 2 statements may seem strange, they are important however:
    //1. if nothing is current and then playback starts, the user needs to be shown the currentTrack
    //2. if we are setting to NULL (eg reached end of playlist) we need to unselect the item as well as unglow it
    //   as otherwise we will play that track next time the user presses play (rather than say the first track)
    //   because that is a feature of amaroK
    if( m_pCurrentTrack == NULL ) ensureItemVisible( item ); //handles NULL gracefully
    else if( item == NULL ) m_pCurrentTrack->setSelected( false );

    m_pCurrentTrack = item;
}


PlaylistItem *PlaylistWidget::restoreCurrentTrack()
{
   if( !pApp->isPlaying() ) return 0;

   KURL url( pApp->m_playingURL );

   if( !(m_pCurrentTrack && m_pCurrentTrack->url() == url) )
   {
      PlaylistItem* item;

      for( item = static_cast<PlaylistItem*>( firstChild() );
           item && item->url() != url;
           item = static_cast<PlaylistItem*>( item->nextSibling() ) )
      {}

      setCurrentTrack( item ); //set even if NULL
   }

   return m_pCurrentTrack;
}


void PlaylistWidget::setSorting( int i, bool b )
{
  //TODO consider removing this IF (relying on the fact you always call setSorting to write the undo)

  //we overide so we can always write an undo
  if( i < 200 ) //FIXME 200 is arbituray, use sensible number like sizeof(short)
  {
    writeUndo();
  }

  KListView::setSorting( i, b );

  //this is one of the rare cases that we ensure the currentTrack is visible
  ensureItemVisible( currentTrack() );
}




// SLOTS ============================================

#include <qclipboard.h>
void PlaylistWidget::copyAction( QListViewItem *item )
{
    if( item == NULL ) item = currentTrack();

    if( item != NULL )
    {
        QApplication::clipboard()->setText( item->text( 0 ) );
    }
}

void PlaylistWidget::activate( QListViewItem *item )
{
   //NOTE  potentially dangerous down-casting
   //FIXME consider adding a hidden "empty" playlistitem that would be useful in these situations perhaps
   //FIXME handle when reaches end of playlist and track, should reset to beginning of list
   //FIXME get audiodata on demand for tracks

   PlaylistItem *_item = static_cast<PlaylistItem *>(item);

   setCurrentTrack( _item );

   if( _item != NULL )
   {
      const MetaBundle *meta = _item->metaBundle();

      emit activated( _item->url(), meta );

      delete meta;
   }
}


void PlaylistWidget::showContextMenu( QListViewItem *item, const QPoint &p, int col )
{
    if( item == NULL ) return; //technically we should show "Remove" but this is easier

    QPopupMenu popup( this );
    popup.insertItem( SmallIcon( "player_play" ), i18n( "&Play" ), 0, 0, Key_Enter, 0 );
    popup.insertItem( SmallIcon( "info" ), i18n( "&View meta information..." ), 1 );
    popup.insertItem( SmallIcon( "edit" ), i18n( "&Edit tag: %1" ).arg( columnText( col ) ), 2 );
    popup.insertItem( SmallIcon( "editcopy" ), i18n( "&Copy trackname" ), 0, 0, CTRL+Key_C, 3 ); //FIXME use KAction
    popup.insertSeparator();
    //NOTE we did use "editdelete" but it that makes it seem like you will delete the file!
    //NOTE at least one item will always be selected ( item )
    popup.insertItem( SmallIcon( "editclear" ), i18n( "&Remove selected" ), this, SLOT( removeSelectedItems() ), Key_Delete );

    //only enable for columns that have editable tags
    popup.setItemEnabled( 2, isRenameable( col ) );

    switch( popup.exec( p ) )
    {
    case 0:
        activate( item );
        break;
    case 1:
        showTrackInfo( static_cast<const PlaylistItem *>(item) );
        break;
    case 2:
        rename( item, col );
        break;
    case 3:
        copyAction( item );
        break;
    }
}


#include <qmessagebox.h>
void PlaylistWidget::showTrackInfo( const PlaylistItem *pItem )
{
    // FIXME KMessageBoxize?
    QMessageBox *box = new QMessageBox( "Track Information", 0,
                                        QMessageBox::Information, QMessageBox::Ok, QMessageBox::NoButton,
                                        QMessageBox::NoButton, 0, "Track Information", true,
                                        Qt::WDestructiveClose | Qt::WStyle_DialogBorder );

    QString str( "<html><body><table border=\"1\">" );

    if ( AmarokConfig::showMetaInfo() )
    {
         const MetaBundle *mb = pItem->metaBundle();

         str += "<tr><td>" + i18n( "Title"   ) + "</td><td>" + mb->m_title   + "</td></tr>";
         str += "<tr><td>" + i18n( "Artist"  ) + "</td><td>" + mb->m_artist  + "</td></tr>";
         str += "<tr><td>" + i18n( "Album"   ) + "</td><td>" + mb->m_album   + "</td></tr>";
         str += "<tr><td>" + i18n( "Genre"   ) + "</td><td>" + mb->m_genre   + "</td></tr>";
         str += "<tr><td>" + i18n( "Year"    ) + "</td><td>" + mb->m_year    + "</td></tr>";
         str += "<tr><td>" + i18n( "Comment" ) + "</td><td>" + mb->m_comment + "</td></tr>";
         str += "<tr><td>" + i18n( "Length"  ) + "</td><td>" + QString::number( mb->m_length ) + "</td></tr>";
         str += "<tr><td>" + i18n( "Bitrate" ) + "</td><td>" + QString::number( mb->m_bitrate ) + " kbps</td></tr>";
         str += "<tr><td>" + i18n( "Samplerate" ) + "</td><td>" + QString::number( mb->m_sampleRate ) + " Hz</td></tr>";

         delete mb;
    }
    else
    {
        str += "<tr><td>" + i18n( "Stream" ) + "</td><td>" + pItem->url().prettyURL() + "</td></tr>";
        str += "<tr><td>" + i18n( "Title"  ) + "</td><td>" + pItem->text( 0 ) + "</td></tr>";
    }

    str.append( "</table></body></html>" );
    box->setText( str );
    box->setTextFormat( Qt::RichText );
    box->show();
}


void PlaylistWidget::clear( bool saveState )
{
    if( saveState )
    {
       writeUndo();
    }

    m_tagReader->cancel(); //stop tag reading (very important!)
    searchTokens.clear();
    searchPtrs.clear();
    KListView::clear();
    setCurrentTrack( NULL );

    emit cleared();
}


#include <qpen.h>
void PlaylistWidget::slotGlowTimer()
{
    if ( PlaylistItem *item = currentTrack() )
    {
        if ( m_GlowCount > 120 )
            m_GlowAdd = -m_GlowAdd;

        if ( m_GlowCount < 90 )
            m_GlowAdd = -m_GlowAdd;

        m_GlowCount += m_GlowAdd;

        //draw glowing rectangle around current track, to indicate activity
        QRect rect = itemRect( item ); //FIXME slow function!

        if ( rect.isValid() ) {
            QPainter p( viewport() );
            p.setPen( m_GlowColor.light( m_GlowCount ) );

            rect.setTop   ( rect.top()    + 1 );
            rect.setBottom( rect.bottom() - 1 );
            rect.setWidth ( contentsWidth()   );    //neccessary to draw on the complete width

            p.drawRect( rect );
        }
    }
}


void PlaylistWidget::slotReturnPressed()
{
    //TODO please move to keyPressEvent if possible

    QListViewItemIterator it( this, QListViewItemIterator::Visible );
    if ( it.current() )
    {
        activate( it.current() );

        //FIXME: ugly dependency
        KLineEdit *le = pApp->m_pBrowserWin->m_pPlaylistLineEdit;
        le->setText( "" );

        ensureItemVisible( it.current() );
    }
}


void PlaylistWidget::slotTextChanged( const QString &str )
{
    QListViewItem *pVisibleItem = NULL;
    unsigned int x = 0;
    bool b;

    if ( str.contains( lastSearch ) && !lastSearch.isEmpty() )
        b = true;
    else
        b = false;

    lastSearch = str.lower();
    QStringList tokens = QStringList::split( " ", lastSearch );

    if (b)
    {
        pVisibleItem = firstChild();
        while ( pVisibleItem )
        {
            for ( uint y = 0; y < tokens.count(); ++y )
            {

                if ( !pVisibleItem->text(0).lower().contains( tokens[y] ) )
                    pVisibleItem->setVisible( false );
            }

            // iterate
            pVisibleItem = pVisibleItem -> itemBelow();
        }
    }
    else
        for ( QStringList::Iterator it = searchTokens.begin(); it != searchTokens.end(); ++it )
        {
            pVisibleItem = searchPtrs.at( x );

            pVisibleItem->setVisible( true );
            for ( uint y = 0; y < tokens.count(); ++y )
            {
                if ( !(*it).lower().contains( tokens[y] ) )
                    pVisibleItem->setVisible( false );
            }

            x++;
        }

    clearSelection();
    triggerUpdate();
}


void PlaylistWidget::slotEraseMarker()
{
    if ( m_marker.isValid() )
    {
        QRect rect = m_marker;
        m_marker = QRect();
        viewport()->repaint( rect, true );
    }
}


void PlaylistWidget::shuffle()
{
    writeUndo();

    // not evil, but corrrrect :)
    QPtrList<QListViewItem> list;

    while ( childCount() )
    {
        list.append( firstChild() );
        takeItem( firstChild() );
    }

    // initalize with seed
    KRandomSequence seq( static_cast<long>( KApplication::random() ) );
    seq.randomize( &list );

    for ( unsigned int i = 0; i < list.count(); i++ )
    {
        insertItem( list.at( i ) );
    }
}


void PlaylistWidget::removeSelectedItems()
{
  //FIXME this is the only method with which the user can remove items, however to properly future proof
  //      you need to somehow make it so creation and deletion of playlistItems handle the search
  //      tokens and pointers (and removal from tagReader queue!)

    //two loops because:
    //1)the code is neater
    //2)If we remove m_currentTrack we select the next track because when m_currentTrack == NULL
    //  we play the first selected item. In order to be sure what we select won't be removed by
    //  this function, we use two loops

    //FIXME when we implement a "play this track next" feature, you can scrap this selection method
    //FIXME also if you delete the last track when set current the playlist repeats on track end

    QPtrList<PlaylistItem> list;

    for( QListViewItemIterator it( this, QListViewItemIterator::Selected ); it.current(); ++it )
        if( it.current() != m_pCurrentTrack )
            list.append( (PlaylistItem *)it.current() );

    //currenTrack must be last to ensure the item after it won't be removed
    //we select the item after currentTrack so it's played when currentTrack finishes
    if ( m_pCurrentTrack != NULL && m_pCurrentTrack->isSelected() ) list.append( m_pCurrentTrack );
    if ( !list.isEmpty() ) writeUndo();

    for ( PlaylistItem *item = list.first(); item; item = list.next() )
    {
        if ( m_pCurrentTrack == item )
        {
            m_pCurrentTrack = NULL;
            //now we select the next item if available so playback will continue from there next iteration
            if( pApp->isPlaying() )
                if( QListViewItem *tmp = item->nextSibling() )
                    tmp->setSelected( true );
        }

        //keep search system synchronised
        int x = searchPtrs.find( item );
        if ( x >= 0 )
        {
            searchTokens.remove( searchTokens.at( x ) );
            searchPtrs.remove( searchPtrs.at( x ) );
        }

        //if tagreader is running don't let tags be read for this item and delete later
        if ( m_tagReader->running() )
        {
            //FIXME make a customEvent to deleteLater(), can't use QObject::deleteLater() as we don't inherit QObject!
            item->setVisible( false ); //will be removed next time playlist is cleared
            m_tagReader->remove( item );
        }
        else
            delete item;
    }
}


void PlaylistWidget::writeTag( QListViewItem *lvi, const QString &tag, int col )
{
    //Surely we don't need to test for NULL here?
    static_cast<PlaylistItem*>(lvi)->writeTag( tag, col );
}


// UNDO SYSTEM ==========================================================

void PlaylistWidget::initUndo()
{
    // create undo buffer directory
    m_undoDir.setPath( kapp->dirs()->saveLocation( "data", kapp->instanceName() + "/" ) );

    if ( !m_undoDir.exists( "undo", false ) )
        m_undoDir.mkdir( "undo", false );

    m_undoDir.cd( "undo" );

    // clean directory
    QStringList dirList = m_undoDir.entryList();
    for ( QStringList::Iterator it = dirList.begin(); it != dirList.end(); ++it )
        m_undoDir.remove( *it );
}


void PlaylistWidget::writeUndo()
{
   if ( saveState( m_undoList ) )
   {
      m_redoList.clear();

      emit sigUndoState( true );
      emit sigRedoState( false );
   }
}


bool PlaylistWidget::saveState( QStringList &list )
{
   //don't store blank intermediates in the undo/redo sets, perhaps a little inconsistent, but
   //probably desired by the user, if you disagree comment this out as it's debatable <mxcl>
   if ( childCount() > 0 )
   {
      QString fileName;
      m_undoCounter %= AmarokConfig::undoLevels();
      fileName.setNum( m_undoCounter++ );
      fileName.prepend( m_undoDir.absPath() + "/" );
      fileName += ".m3u";

      if ( list.count() >= AmarokConfig::undoLevels() )
      {
         m_undoDir.remove( list.first() );
         list.pop_front();
      }

      saveM3u( fileName );
      list.append( fileName );

      kdDebug() << "Saved state: " << fileName << endl;

      return true;
   }

   return false;
}


//inline
bool PlaylistWidget::canUndo()
{
    return !m_undoList.isEmpty();
}


//inline
bool PlaylistWidget::canRedo()
{
    return !m_redoList.isEmpty();
}


//TODO replace these two functions with one undoRedo( int ) and use a QSignalMapper
void PlaylistWidget::doUndo()
{
    if ( canUndo() )
    {
        //restore previous playlist
        kdDebug() << "loading state: " << m_undoList.last() << endl;

        KURL::List playlist( KURL( m_undoList.last() ) );
        m_undoList.pop_back();   //pop one of undo stack
        saveState( m_redoList ); //save currentState to redo stack
        clear( false );
        setSorting( 200 );
        startLoader( playlist, 0 );

        restoreCurrentTrack();
        //triggerUpdate();
    }

    emit sigUndoState( canUndo() );
    emit sigRedoState( canRedo() );
}


void PlaylistWidget::doRedo()
{
    if ( canRedo() )
    {
        //restore previous playlist
        KURL::List playlist( KURL( m_redoList.last() ) );
        m_redoList.pop_back();
        saveState( m_undoList );
        clear( false );
        setSorting( 200 );
        startLoader( playlist, 0 );

        restoreCurrentTrack();
        //triggerUpdate();
    }

    emit sigUndoState( canUndo() );
    emit sigRedoState( canRedo() );
}

#include "playlistwidget.moc"
