// (c) 2004 Christian Muehlhaeuser <chris@chris.de>
// See COPYING file for licensing information


#include "config.h"

#include "amarokconfig.h"
#include "collectionbrowser.h"
#include "collectiondb.h"
#include "contextbrowser.h"
#include "coverfetcher.h"
#include "enginecontroller.h"
#include "metabundle.h"
#include "playlist.h"     //appendMedia()
#include "qstringx.h"
#include "sqlite/sqlite3.h"

#include <kapplication.h> //kapp->config(), QApplication::setOverrideCursor()
#include <kconfig.h>      //config object
#include <kdebug.h>
#include <kfiledialog.h>
#include <kglobal.h>
#include <khtml_part.h>
#include <klineedit.h>
#include <klocale.h>
#include <krun.h>
#include <kstandarddirs.h>
#include <kurl.h>
#include <kurlcombobox.h>

#include <qfile.h>
#include <qimage.h>
#include <qlabel.h>
#include <qpushbutton.h>

using amaroK::QStringx;


ContextBrowser::ContextBrowser( const char *name )
        : QVBox( 0, name )
        , m_currentTrack( 0 )
        , m_db( new CollectionDB() )
//         , m_loaded( false )
{
    kdDebug() << k_funcinfo << endl;
    EngineController::instance()->attach( this );

    setSpacing( 4 );
    setMargin( 5 );
    QWidget::setFont( AmarokConfig::useCustomFonts() ? AmarokConfig::playlistWindowFont() : QApplication::font() );

    QHBox *hb1 = new QHBox( this );
    hb1->setSpacing( 4 );

    browser = new KHTMLPart( hb1 );
    browser->setDNDEnabled( true );
    setStyleSheet();

    connect( browser->browserExtension(), SIGNAL( openURLRequest( const KURL &, const KParts::URLArgs & ) ),
             this,                          SLOT( openURLRequest( const KURL & ) ) );

    if ( m_db->isEmpty() || !m_db->isDbValid() )
        showIntroduction();
    else
        showHome();

    setFocusProxy( hb1 ); //so focus is given to a sensible widget when the tab is opened
}


ContextBrowser::~ContextBrowser()
{
    delete m_db;
    delete m_currentTrack;

    EngineController::instance()->detach( this );
}

/*void ContextBrowser::showEvent( QShowEvent *)
{
    kdDebug() << k_funcinfo << endl;
    if( m_loaded ) return;
    QTime t;
    t.start();

    if ( m_db->isEmpty() || !m_db->isDbValid() )
        showIntroduction();
    else
        showHome();
    kdDebug()<<"END show"<<endl;
    qDebug( "Context show elapsed: %d ms", t.elapsed() );

    m_loaded = true;
}*/

//////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS
//////////////////////////////////////////////////////////////////////////////////////////

void ContextBrowser::setFont( const QFont &newFont ) //virtual
{
    QWidget::setFont( newFont );
    setStyleSheet();
    browser->setUserStyleSheet( m_styleSheet );
    browser->setStandardFont( newFont.key() );
    showCurrentTrack();
}

//////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC SLOTS
//////////////////////////////////////////////////////////////////////////////////////////

void ContextBrowser::openURLRequest( const KURL &url )
{
    m_url = url;

    QStringList info = QStringList::split( " @@@ ", url.path() );

    if ( url.protocol() == "album" )
    {
        QStringList values;
        QStringList names;

        m_db->execSql( QString( "SELECT DISTINCT url FROM tags WHERE artist = %1 AND album = %2 ORDER BY track;" )
                       .arg( info[0] )
                       .arg( info[1] ), &values, &names );

        for ( uint i = 0; i < values.count(); i++ )
        {
            if ( values[i].isEmpty() ) continue;

            KURL tmp;
            tmp.setPath( values[i] );
            Playlist::instance()->appendMedia( tmp, false, true );
        }
    }

    if ( url.protocol() == "file" )
        Playlist::instance()->appendMedia( url, true, true );

    if ( m_url.protocol() == "show" )
    {
        if ( m_url.path() == "home" )
            showHome();
        else if ( m_url.path() == "context" )
            showCurrentTrack();
        else if ( m_url.path() == "stream" )
            showCurrentStream();
        else if ( m_url.path() == "collectionSetup" )
        {
            //TODO if we do move the configuration to the main configdialog change this,
            //     otherwise we need a better solution
            QObject *o = parent()->child( "CollectionBrowser" );
            if ( o ) static_cast<CollectionBrowser*>( o )->setupDirs();

            if ( CollectionView::instance() ) {
                connect( CollectionView::instance(), SIGNAL( sigScanDone() ), SLOT( showHome() ) );
                showScanning();
            }
            else
                showHome();
        }
    }

#ifdef AMAZON_SUPPORT
    /* fetch covers from amazon on click */
    if ( m_url.protocol() == "fetchcover" )
        m_db->fetchCover( this, info[0], info[1], false );
#else
    if ( m_url.protocol() == "fetchcover" )
    {
        if( m_db->getImageForAlbum( info[0], info[1], 0 ) != locate( "data", "amarok/images/nocover.png" ) )
        {
            /* if a cover exists, open a widget with the image on click */
            QWidget *widget = new QWidget( 0, 0, WDestructiveClose );
            widget->setCaption( i18n( "Cover Viewer" ) + " - amaroK" );
            QPixmap pixmap( m_db->getImageForAlbum( info[0], info[1], 0 ) );
            widget->setPaletteBackgroundPixmap( pixmap );
            widget->setMinimumSize( pixmap.size() );
            widget->setFixedSize( pixmap.size() );
            widget->show();
        }
        else
        {
            /* if no cover exists, open a file dialog to add a cover */
            KURL file = KFileDialog::getImageOpenURL( ":homedir", this, i18n( "Select cover image file - amaroK" ) );
            if ( !file.isEmpty() )
            {
                QImage img( file.directory() + "/" + file.fileName() );
                QString filename( QFile::encodeName( info[0] + " - " + info[1] ) );
                filename.replace( " ", "_" ).append( ".png" );
                img.save( KGlobal::dirs()->saveLocation( "data", kapp->instanceName() )+"/albumcovers/"+filename.lower(), "PNG" );
                ContextBrowser::showCurrentTrack();
            }
        }
     }
#endif
    /* open konqueror with musicbrainz search result for artist-album */
    if ( url.protocol() == "musicbrainz" )
    {
        const QString command = "kfmclient openURL 'http://www.musicbrainz.org/taglookup.html?artist=''%1''&album=''%2'''";
        KRun::runCommand( command.arg( info[0] ).arg( info[1] ), "kfmclient", "konqueror" );
    }

}

//////////////////////////////////////////////////////////////////////////////////////////
// PROTECTED
//////////////////////////////////////////////////////////////////////////////////////////

void ContextBrowser::engineTrackEnded( int finalPosition, int trackLength )
{
    //This is where percentages are calculated
    //TODO statistics are not calculated when currentTrack doesn't exist

    if ( m_currentTrack )
    {
        // sanity check
        if ( finalPosition > trackLength )
            finalPosition = trackLength;

        int pct = (int) ( ( (double) finalPosition / (double) trackLength ) * 100 );

        // increase song counter & calculate new statistics
        float score = m_db->addSongPercentage( m_currentTrack->url().path(), pct );

        // TODO reimplement playlist-update
/*        if ( score )
            m_currentTrack->setText( PlaylistItem::Score, QString::number( score ) );*/
    }
}


void ContextBrowser::engineNewMetaData( const MetaBundle &bundle, bool /*trackChanged*/ )
{
    delete m_currentTrack;
    m_currentTrack = new MetaBundle( bundle );

    if ( m_db->isEmpty() || !m_db->isDbValid() )
        showIntroduction();
    else if ( EngineController::instance()->isStream() )
        showCurrentStream();
    else
        showCurrentTrack();
}


void ContextBrowser::engineStateChanged( Engine::State state )
{
    if ( m_db->isEmpty() || !m_db->isDbValid() ) {
        showIntroduction();
        return;
    }

    switch( state )
    {
        case Engine::Playing:
            if ( EngineController::instance()->isStream() )
                showCurrentStream();
            break;
        case Engine::Empty:
            showHome();
        default:
            break;
    }
}


void ContextBrowser::paletteChange( const QPalette& pal )
{
    kdDebug() << k_funcinfo << endl;

    QVBox::paletteChange( pal );

    setStyleSheet();
    showHome();
}


//////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE SLOTS
//////////////////////////////////////////////////////////////////////////////////////////

void ContextBrowser::showHome() //SLOT
{
    if ( m_db->isEmpty() || !m_db->isDbValid() ) {
        showIntroduction();
        return;
    }

    QStringList values;
    QStringList names;

    // take care of sql updates (schema changed errors)
    delete m_db;
    m_db = new CollectionDB();
    // Triggers redisplay when new cover image is downloaded
    #ifdef AMAZON_SUPPORT
    connect( m_db, SIGNAL( coverFetched(const QString&) ), this, SLOT( showCurrentTrack() ) );
    #endif
    browser->begin();
    browser->setUserStyleSheet( m_styleSheet );

    // <Favorite Tracks Information>
    if ( EngineController::instance()->isStream() )
        browser->write( "<html><div class='menu'><a class='menu' href='show:home'>" + i18n( "Home" ) + "</a>&nbsp;&nbsp;<a class='menu' href='show:stream'>"
                        + i18n( "Current Stream" ) + "</a></div>");
    else
        browser->write( "<html><div class='menu'><a class='menu' href='show:home'>" + i18n( "Home" ) + "</a>&nbsp;&nbsp;<a class='menu' href='show:context'>"
                        + i18n( "Current Track" ) + "</a></div>");

    browser->write( "<div class='rbcontent'>" );
    browser->write( "<table width='100%' border='0' cellspacing='0' cellpadding='0'>" );
    browser->write( "<tr><td class='head'>&nbsp;" + i18n( "Your Favorite Tracks:" ) + "</td></tr>" );
    browser->write( "<tr><td height='1' bgcolor='black'></td></tr>" );
    browser->write( "</table>" );
    browser->write( "<table width='100%' border='0' cellspacing='1' cellpadding='1'>" );

    m_db->execSql( QString( "SELECT tags.title, tags.url, round( statistics.percentage + 0.5 ), artist.name, album.name "
                            "FROM tags, artist, album, statistics "
                            "WHERE artist.id = tags.artist AND album.id = tags.album AND statistics.url = tags.url "
                            "ORDER BY statistics.percentage DESC "
                            "LIMIT 0,10;" ), &values, &names );

    if ( values.count() )
    {
        for ( uint i = 0; i < values.count(); i = i + 5 )
            browser->write( QString ( "<tr><td class='song'><a class='song' href=\"file:"
                                    + values[i+1].replace( "\"", QCString( "%22" ) ) + "\"><b>" + values[i]
                                    + "</b> <i>(" + i18n( "Score:" ) + " " + values[i+2] + ")</i><br>" + values[i+3] + " - " + values[i+4] + "</a></td></tr>" ) );
    }

    values.clear();
    names.clear();

    browser->write( "</table></div>" );
    // </Favorite Tracks Information>

    // <Recent Tracks Information>
    browser->write( "<br><div class='rbcontent'>" );
    browser->write( "<table width='100%' border='0' cellspacing='0' cellpadding='0'>" );
    browser->write( "<tr><td class='head'>&nbsp;" + i18n( "Newest Tracks:" ) + "</td></tr>" );
    browser->write( "<tr><td height='1' bgcolor='black'></td></tr>" );
    browser->write( "</table>" );
    browser->write( "<table width='100%' border='0' cellspacing='1' cellpadding='1'>" );

    m_db->execSql( QString( "SELECT tags.title, tags.url, artist.name, album.name "
                            "FROM tags, artist, album "
                            "WHERE artist.id = tags.artist AND album.id = tags.album "
                            "ORDER BY tags.createdate DESC "
                            "LIMIT 0,10;" ), &values, &names );

    if ( values.count() )
    {
        for ( uint i = 0; i < values.count(); i = i + 4 )
            browser->write( QString ( "<tr><td class='song'><a class='song' href=\"file:"
                                    + values[i+1].replace( "\"", QCString( "%22" ) ) + "\"'><b>" + values[i]
                                    + "</b><br>" + values[i+2] + " - " + values[i+3] + "</a></td></tr>" ) );
    }

    values.clear();
    names.clear();

    browser->write( "</table></div>" );
    // </Recent Tracks Information>

    browser->write( "<br></html>" );
    browser->end();
}


void ContextBrowser::showCurrentTrack() //SLOT
{
    #define escapeHTML(s)     QString(s).replace( "<", "&lt;" ).replace( ">", "&gt;" )
    #define escapeHTMLAttr(s) QString(s).replace( "%", "%25" ).replace( "'", "%27" )

    if ( !m_currentTrack )
        return;

    QStringList values;
    QStringList names;

    // take care of sql updates (schema changed errors)
    delete m_db;
    m_db = new CollectionDB();

    // Triggers redisplay when new cover image is downloaded
    connect( m_db, SIGNAL( coverFetched() ), this, SLOT( showCurrentTrack() ) );

    browser->begin();
    browser->setUserStyleSheet( m_styleSheet );

    browser->write( "<html><div class='menu'><a class='menu' href='show:home'>" + i18n( "Home" ) + "</a>&nbsp;&nbsp;<a class='menu' href='show:context'>"
                    + i18n( "Current Track" ) + "</a></div>");
    if ( !m_db->isFileInCollection( m_currentTrack->url().path() ) )
    {
        browser->write( "<div><br>");
        browser->write( i18n( "This song is not in your current collection. If you like further contextual information about this song, add it to your collection!" )
                        + "&nbsp;<a href='show:collectionSetup'>" + i18n( "Click here to change your collection setup." ) + "</a>" );
        browser->write( "<br><br></div>");
    }

    // <Current Track Information>
    browser->write( "<div class='rbcontent'>" );
    browser->write( "<table width='100%' border='0' cellspacing='0' cellpadding='0'>" );
    browser->write( "<tr><td class='head'>&nbsp;" + i18n( "Currently playing:" ) + "</td></tr>" );
    browser->write( "<tr><td height='1' bgcolor='black'></td></tr>" );
    browser->write( "</table>" );
    browser->write( "<table width='100%' border='0' cellspacing='1' cellpadding='1'>" );

    m_db->execSql( QString( "SELECT album.name, artist.name, datetime( datetime( statistics.createdate, 'unixepoch' ), 'localtime' ), "
                            "datetime( datetime( statistics.accessdate, 'unixepoch' ), 'localtime' ), statistics.playcounter, round( statistics.percentage + 0.5 ) "
                            "FROM album, tags, artist, statistics "
                            "WHERE album.id = tags.album AND artist.id = tags.artist AND statistics.url = tags.url AND tags.url = '%1';" )
                   .arg( m_db->escapeString( m_currentTrack->url().path() ) ), &values, &names, true );

    if ( !values.isEmpty() )
         /* making 2 tables is most probably not the cleanest way to do it, but it works. */
         browser->write( QStringx ( "<tr><td height='42' valign='top' class='rbcurrent' width='90%'>"
                                    "<span class='album'><b>%1 - %2</b></span><br>%3</td><td valign='top' align='right' width='10%'>"
                                    "<a href='musicbrainz:%4 @@@ %5'><img src='%6'></a></td></tr></table>"
                                    "<table width='100%'><tr><td width='20%'><a class='menu' href='fetchcover:%7 @@@ %8'>"
                                    "<img hspace='2' src='%9'></a></td>"
                                    "<td valign='bottom' align='right' width='80%'>" +
                                    i18n( "Track played 1 time", "Track played %n times", values[4].toInt() ) + "<br>" +
                                    i18n( "Score:" ) + " %11" + "<br>" +
                                    i18n( "Last play:" ) + " %12" + "<br>" +
                                    i18n( "First play:" ) + " %13" + "</i></td></tr>" )
                         .args( QStringList()
                                << escapeHTML( m_currentTrack->artist() )
                                << escapeHTML( m_currentTrack->title() )
                                << escapeHTML( m_currentTrack->album() )
                                << escapeHTMLAttr( m_db->escapeString( m_currentTrack->artist() ) )
                                << escapeHTMLAttr( m_db->escapeString( m_currentTrack->album() ) )
                                << escapeHTML( locate( "data", "amarok/images/musicbrainz.png" ) )
                                << escapeHTMLAttr( m_currentTrack->artist() )
                                << escapeHTMLAttr( m_currentTrack->album() )
                                << escapeHTMLAttr( m_db->getImageForAlbum( values[1], values[0] ) )
                                << values[5]
                                << values[2].left( values[2].length() - 3 )
                                << values[3].left( values[3].length() - 3 )
                                )
                         );
    else
    {
        m_db->execSql( QString( "SELECT album.name, artist.name "
                                "FROM album, tags, artist "
                                "WHERE album.id = tags.album AND artist.id = tags.artist AND tags.url = '%1';" )
                      .arg( m_db->escapeString( m_currentTrack->url().path() ) ), &values, &names, true );

             browser->write( QStringx ( "<tr><td height='42' valign='top' class='rbcurrent' width='90%'>"
                                        "<span class='album'><b>%1 - %2</b></span><br>%3</td>"
                                        "<td valign='top' align='right' width='10%'><a href='musicbrainz:%4 @@@ %5'>"
                                        "<img src='%6'></a></td></tr></table> <table width='100%'><tr><td width='20%'>"
                                        "<a class='menu' href='fetchcover:%6 @@@ %7'><img align='left' valign='center' hspace='2' src='%8'></a>"
                                        "</td><td width='80%' valign='bottom' align='right'>"
                                        "<i>" + i18n( "Never played before" )  + "</i></td>"
                                        "</tr>" )
                             .args( QStringList()
                                    << escapeHTML( m_currentTrack->artist() )
                                    << escapeHTML( m_currentTrack->title() )
                                    << escapeHTML( m_currentTrack->album() )
                                    << escapeHTMLAttr( m_db->escapeString( m_currentTrack->artist() ) )
                                    << escapeHTMLAttr( m_db->escapeString( m_currentTrack->album() ) )
                                    << escapeHTML( locate( "data", "amarok/images/musicbrainz.png" ) )
                                    << escapeHTMLAttr( m_currentTrack->artist() )
                                    << escapeHTMLAttr( m_currentTrack->album() )
                                    << escapeHTMLAttr( m_db->getImageForAlbum( values[1], values[0] ) )
                                    )
                             );
    }
    values.clear();
    names.clear();

    browser->write( "</table></div>" );
    // </Current Track Information>

    // <Favourite Tracks Information>
    m_db->execSql( QString( "SELECT tags.title, tags.url, round( statistics.percentage + 0.5 ) "
                            "FROM tags, artist, statistics "
                            "WHERE tags.artist = artist.id AND artist.name LIKE '%1' AND statistics.url = tags.url "
                            "ORDER BY statistics.percentage DESC "
                            "LIMIT 0,5;" )
                   .arg( m_db->escapeString( m_currentTrack->artist() ) ), &values, &names, true );

    if ( !values.isEmpty() )
    {
        browser->write( "<br><div class='rbcontent'>" );
        browser->write( "<table width='100%' border='0' cellspacing='0' cellpadding='0'>" );
        browser->write( "<tr><td class='head'>&nbsp;" + i18n( "Favorite tracks by this artist:" ) + "</td></tr>" );
        browser->write( "<tr><td height='1' bgcolor='black'></td></tr>" );
        browser->write( "</table>" );
        browser->write( "<table width='100%' border='0' cellspacing='1' cellpadding='1'>" );

        for ( uint i = 0; i < values.count(); i += 3 )
            browser->write( QString ( "<tr><td class='song'><a class='song' href=\"file:" + values[i + 1].replace( "\"", QCString( "%22" ) ) + "\">"
                                      + values[i] + " <i>(" + i18n( "Score:" ) + " " + values[i + 2] + ")</i></a></td></tr>" ) );

        values.clear();
        names.clear();

        browser->write( "</table></div>" );
    }
    // </Favourite Tracks Information>

    // <Tracks on this album>
    if ( !m_currentTrack->album().isEmpty() && !m_currentTrack->artist().isEmpty() )
    {
        m_db->execSql( QString( "SELECT tags.title, tags.url, tags.track "
                                "FROM tags, artist, album "
                                "WHERE tags.album = album.id AND album.name LIKE '%1' AND "
                                      "tags.artist = artist.id AND "
                                      "( tags.sampler = 1 OR artist.name LIKE '%2' ) "
                                "ORDER BY tags.track;" )
                       .arg( m_db->escapeString( m_currentTrack->album() ) )
                       .arg( m_db->escapeString( m_currentTrack->artist() ) ), &values, &names, true );

        if ( !values.isEmpty() )
        {
            browser->write( "<br><div class='rbcontent'>" );
            browser->write( "<table width='100%' border='0' cellspacing='0' cellpadding='0'>" );
            browser->write( "<tr><td class='head'>&nbsp;" + i18n( "Tracks on this album:" ) + "</td></tr>" );
            browser->write( "<tr><td height='1' bgcolor='black'></td></tr>" );
            browser->write( "</table>" );
            browser->write( "<table width='100%' border='0' cellspacing='1' cellpadding='1'>" );

            for ( uint i = 0; i < values.count(); i += 3 )
            {
                QString tmp = values[i + 2].stripWhiteSpace() == "" ? "" : values[i + 2] + ". ";
                browser->write( QString ( "<tr><td><a class='song' href=\"file:" + values[i + 1].replace( "\"", QCString( "%22" ) ) + "\">" + tmp + values[i] + "</a></td></tr>" ) );
            }

            values.clear();
            names.clear();

            browser->write( "</table></div>" );
        }
    }
    // </Tracks on this album>

    // <Albums by this artist>
    m_db->execSql( QString( "SELECT DISTINCT album.name, artist.name, album.id, artist.id "
                            "FROM album, tags, artist "
                            "WHERE album.id = tags.album AND tags.artist = artist.id AND album.name <> '' AND artist.name LIKE '%1' "
                            "ORDER BY album.name;" )
                   .arg( m_db->escapeString( m_currentTrack->artist() ) ), &values, &names, true );

    if ( !values.isEmpty() )
    {
        browser->write( "<br><div class='rbcontent'>" );
        browser->write( "<table width='100%' border='0' cellspacing='0' cellpadding='0'>" );
        browser->write( "<tr><td class='head'>&nbsp;" + i18n( "Albums by this artist:" ) + "</td></tr>" );
        browser->write( "<tr><td height='1' bgcolor='black'></td></tr>" );
        browser->write( "</table>" );
        browser->write( "<table width='100%' border='0' cellspacing='1' cellpadding='1'>" );

        for ( uint i = 0; i < values.count(); i += 4 )
        {
             browser->write( QStringx ( "<tr><td onClick='window.location.href=\"album:%1 @@@ %2\"' height='42' valign='top' class='rbalbum'>"
                                        "<a class='menu' href='fetchcover:%3 @@@ %4'><img align='left' hspace='2' src='%5'></a><span class='album'>%6</span><br>%7 Tracks</td>"
                                        "</tr>" )
                             .args( QStringList()
                                    << values[i + 3].replace( "\"", "%22" ) // artist.id
                                    << values[i + 2].replace( "\"", "%22" ) // album.id
                                    << escapeHTMLAttr( values[i + 1] ) // artist.name
                                    << escapeHTMLAttr( values[i + 0] ) // album.name
                                    << escapeHTMLAttr( m_db->getImageForAlbum( values[i + 1], values[i + 0], 50 ) )
                                    << escapeHTML( values[i + 0] ) // album.name
                                    << m_db->albumSongCount( values[i + 3], values[i + 2] )
                                    )
                             );
        }
        browser->write( "</table></div>" );
    }
    // </Albums by this artist>

    browser->write( "<br></html>" );
    browser->end();
}


//////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE
//////////////////////////////////////////////////////////////////////////////////////////

void ContextBrowser::setStyleSheet()
{
    int pxSize = fontMetrics().height() - 4;

    m_styleSheet =  QString( "div { color: %1; font-size: %2px; text-decoration: none; }" )
                    .arg( colorGroup().text().name() ).arg( pxSize );
    m_styleSheet += QString( "td { color: %1; font-size: %2px; text-decoration: none; }" )
                    .arg( colorGroup().text().name() ).arg( pxSize );
    m_styleSheet += QString( ".menu { color: %1; font-weight: bold; }" )
                    .arg( colorGroup().text().name() );
    m_styleSheet += QString( ".song { color: %1; font-size: %2px; text-decoration: none; }" )
                    .arg( colorGroup().text().name() ).arg( pxSize );
    m_styleSheet += QString( ".song:hover { color: %1; cursor: default; background-color: %2; }" )
                    .arg( colorGroup().highlightedText().name() ).arg( colorGroup().highlight().name() );
    m_styleSheet += QString( "A.song { color: %1; font-size: %2px; text-decoration: none; display: block; }" )
                    .arg( colorGroup().text().name() ).arg( pxSize );
    m_styleSheet += QString( "A.song:hover { color: %1; font-size: %2px; text-decoration: none; display: block; }" )
                    .arg( colorGroup().highlightedText().name() ).arg( pxSize );
    m_styleSheet += QString( ".album { font-weight: bold; font-size: %1px; text-decoration: none; }" )
                    .arg( pxSize );
    m_styleSheet += QString( ".title { color: %1; font-size: %2px; font-weight: bold; }" )
                    .arg( colorGroup().text().name() ).arg( pxSize + 3 );
    m_styleSheet += QString( ".head { color: %1; font-size: %2px; font-weight: bold; background-color: %3; }" )
                    .arg( colorGroup().highlightedText().name() ).arg( pxSize + 2 ).arg( colorGroup().highlight().name() );
    m_styleSheet += QString( ".rbcurrent { color: %1; border: solid %2 1px; }" )
                    .arg( colorGroup().text().name() ).arg( colorGroup().base().name() );
    m_styleSheet += QString( ".rbalbum { color: %1; border: solid %2 1px; }" )
                    .arg( colorGroup().text().name() ).arg( colorGroup().base().name() );
    m_styleSheet += QString( ".rbalbum:hover { color: %1; cursor: default; background-color: %2; border: solid %3 1px; }" )
                    .arg( colorGroup().highlightedText().name() ).arg( colorGroup().highlight().name() ).arg( colorGroup().text().name() );
    m_styleSheet += QString( ".rbcontent { border: solid %1 1px; }" )
                    .arg( colorGroup().highlight().name() );
    m_styleSheet += QString( ".rbcontent:hover { border: solid %1 1px; }" )
                    .arg( colorGroup().text().name() );
}


void ContextBrowser::showIntroduction()
{
    browser->begin();
    browser->setUserStyleSheet( m_styleSheet );

    browser->write( "<html><div>");
    browser->write( i18n( "Hello amaroK user!" ) );
    browser->write( "<br><br>" );
    browser->write( i18n( "This is the Context Browser: it shows you contextual information about the currently playing track."
                          "In order to use this feature of amaroK, you need to build a collection." )
                    + "&nbsp;<a href='show:collectionSetup'>" + i18n( "Click here to build one." ) + "</a>" );
    browser->write( "</div></html>");

    browser->end();
}


void ContextBrowser::showScanning()
{
    browser->begin();
    browser->setUserStyleSheet( m_styleSheet );

    browser->write( "<html><div>");
    browser->write( i18n( "Building Collection Database.." ) );
    browser->write( "</div></html>");

    browser->end();
}


void ContextBrowser::showCurrentStream()
{
    browser->begin();
    browser->setUserStyleSheet( m_styleSheet );

    browser->write( "<html><div class='menu'><a class='menu' href='show:home'>" + i18n( "Home" ) + "</a>&nbsp;&nbsp;<a class='menu' href='show:stream'>"
                    + i18n( "Current Stream" ) + "</a></div>");

    // <Stream Information>
    browser->write( "<div class='rbcontent'>" );
    browser->write( "<table width='100%' border='0' cellspacing='0' cellpadding='0'>" );
    browser->write( "<tr><td class='head'>&nbsp;" + i18n( "Playing Stream:" ) + "</td></tr>" );
    browser->write( "<tr><td height='1' bgcolor='black'></td></tr>" );
    browser->write( "</table>" );
    browser->write( "<table width='100%' border='0' cellspacing='1' cellpadding='1'>" );


    if ( m_currentTrack )
    {
        browser->write( QStringx ( "<tr><td height='42' valign='top' class='rbcurrent' width='90%'>"
                                   "<span class='album'><b>%1 - %2</b></span><br>%3</td><td valign='top' align='right' width='10%'>"
                                   "</td></tr></table>"
                                   "<table width='100%'><tr><td width='20%'><a class='menu' href='fetchcover:%7 @@@ %8'>"
                                   "<img hspace='2' src='%9'></a></td></tr>" )
                        .args( QStringList()
                            << escapeHTML( m_currentTrack->artist() )
                            << escapeHTML( m_currentTrack->title() )
                            << escapeHTML( m_currentTrack->album() )
                            << escapeHTMLAttr( m_currentTrack->artist() )
                            << escapeHTMLAttr( m_currentTrack->album() )
                            << escapeHTMLAttr( m_db->getImageForAlbum( "stream", "stream" ) )
                            )
                        );
    }
    else
    {
        browser->write( QStringx ( "<tr><td height='42' valign='top' class='rbcurrent' width='90%'>"
                                   "<span class='album'><b>%1 - %2</b></span><br>%3</td><td valign='top' align='right' width='10%'>"
                                   "</td></tr></table>"
                                   "<table width='100%'><tr><td width='20%'><a class='menu' href='fetchcover:%7 @@@ %8'>"
                                   "<img hspace='2' src='%9'></a></td></tr>" )
                        .args( QStringList()
                            << escapeHTML( m_url.path() )
                            << escapeHTML( QString::null )
                            << escapeHTML( QString::null )
                            << escapeHTMLAttr( QString::null )
                            << escapeHTMLAttr( QString::null )
                            << escapeHTMLAttr( m_db->getImageForAlbum( "stream", "stream" ) )
                            )
                        );
    }

    browser->write( "</table></div>" );
    // </Stream Information>

    browser->write( "</div></html>");

    browser->end();
}


#include "contextbrowser.moc"
