//Maintainer: Max Howell <max.howell@methylblue.com>, (C) 2004
//Copyright:  See COPYING file that comes with this distribution

#include "config.h"

#include <dirent.h>
#include <dlfcn.h>    //dlopen etc.
#include <gtk/gtk.h>  //gtk_init(), gtk_rgb_init()
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <xmms/configfile.h> //visplugins use this stuff, see extern "C" block
#include "xmmswrapper.h"

#include <iostream>
#include <list>
#include <vector>

#define SHARED_LIB_EXT ".so"


#include "fft.c"
// #include "fullscreen.c"


GtkWidget dummy;                  //required by msa visplugin
GtkWidget *equalizerwin = &dummy; //required by msa visplugin
GtkWidget *playlistwin = &dummy;  //required by msa visplugin
GtkWidget *mainwin = &dummy;      //required by msa visplugin


using std::string;


int    tryConnect();
string getPlugin( int, char** );
void   vis_disable_plugin( VisPlugin* ) { exit( 0 ); } //this is called by the plugin when it wants to quit



int
main( int argc, char** argv )
{
    //connect to socket
    const int sockfd = tryConnect();
    if( sockfd == -1 ) exit( 1 );


    //register fd/pid combo with amaroK
    pid_t pid = getpid();
    send( sockfd, "REG", 4, 0 );
    send( sockfd, &pid, sizeof(pid_t), 0 );


    //start gtk
    gtk_init( &argc, &argv ); //xmms plugins require this
    gdk_rgb_init();


    //load plugin
    XmmsWrapper wrap( getPlugin( argc, argv ) );


    //main loop
    // 1. we sleep for a bit, listening for messages from amaroK
    // 2. render a frame
    // 3. do a gtk_event_loop iteration

    float   float_data[ 512 ];
    timeval tv;
    fd_set  fds;
    int     nbytes = 0;

    while( nbytes != -1 )
    {
        gtk_main_iteration_do( FALSE );

        //set the time to wait, we have to do this everytime on linux
        tv.tv_sec  = 0;
        tv.tv_usec = 16*1000; //60Hz

        //get select to watch the right file descriptor
        FD_ZERO( &fds );
        FD_SET( sockfd, &fds );

        select( sockfd+1, &fds, NULL, NULL, &tv );

        if( FD_ISSET( sockfd, &fds) )
        {
            char c[16];
            recv( sockfd, c, 16, 0 );
            std::string command( c );

            if( command == "fullscreen" );
            if( command == "configure" ) wrap.configure();
        }

        ::send( sockfd, "PCM", 4, 0 );
        nbytes = ::recv( sockfd, float_data, 512 * sizeof( float ), 0 );

        wrap.render( float_data );
    }

    ::close( sockfd );

    return 0;
}

#include <qstring.h>
#include <kinstance.h>
#include <kstandarddirs.h>

int
tryConnect()
{
    //TODO remove qt and kde isms

    const KInstance instance( "xmmswrapper" );
    const QString socketpath = locateLocal( "socket", "amarok.visualization_socket", &instance );

    int fd = ::socket( AF_UNIX, SOCK_STREAM, 0 );

    if( fd != -1 )
    {
        struct sockaddr_un local;

        strcpy( &local.sun_path[ 0 ], socketpath.local8Bit() );
        local.sun_family = AF_UNIX;

        std::cout << "[amK] Connecting to: " << socketpath.local8Bit() << '\n';

        if( ::connect( fd, ( struct sockaddr* ) & local, sizeof( local ) ) == -1 )
        {
            ::close( fd );
            fd = -1;

            std::cerr << "[amK] tryConnect() failed\n";
        }
    }

    return fd;
}

std::string
getPlugin( int argc, char **argv )
{
    if( argc == 1 )
    {
        std::list<string> list;

        //scan plugins
        string dirname = XMMS_PLUGIN_PATH;
        dirname.append( "/" );
        DIR *dir;
        struct dirent *ent;
        struct stat statbuf;

        dir = opendir( dirname.c_str() );
        if ( !dir ) { std::cerr << "Please edit the PLUGIN_PATH in xmmswrapper.cpp\n"; exit( 1 ); }

        while ( (ent = readdir( dir )) )
        {
            string filename = ent->d_name;
            uint index = filename.find_last_of( '.' );
            if ( index == string::npos ) continue;
            string extension = filename.substr( index );
            string fullpath = dirname + filename;

            if ( !stat( fullpath.c_str(), &statbuf )
                    && S_ISREG( statbuf.st_mode )
                    && extension == SHARED_LIB_EXT )
                list.push_back( filename );
        }
        closedir( dir );

        std::cout << "Please select a plugin: \n";

        std::vector<string> v( list.size() );
        std::copy( list.begin(), list.end(), v.begin() );

        for ( uint n = 0; n < v.size(); ++n )
        {
            std::cout << n + 1 << ": " << v[ n ] << '\n';
        }

        char c[8];
        std::cin >> c;
        uint selection = atoi( c );
        if( selection > v.size() ) exit( 1 );

        return v[ selection - 1 ];
    }

    return argv[ 1 ];
}



/////////////////////////////////////////////////////////////////////////////////////////
// CLASS XmmsWrapper
/////////////////////////////////////////////////////////////////////////////////////////

XmmsWrapper::XmmsWrapper( const string &plugin )
{
    std::cout << "[amK] loading xmms plugin: " << plugin << '\n';

    string
    path = XMMS_PLUGIN_PATH;
    path += "/";
    path += plugin;

    { //<load plugin>

        void *h;
        void *( *gpi ) ( void );

        if( ( h = dlopen( path.c_str(), RTLD_NOW ) ) == NULL )
        {
            std::cout << dlerror() << "\n";
            return ;
        }

        if( ( gpi = ( void * ( * ) () ) dlsym( h, "get_vplugin_info" ) ) != NULL )
        {
            VisPlugin * p = ( VisPlugin* ) gpi();
            p->handle = h;
            p->filename = strdup( path.c_str() );
            p->xmms_session = 0; //ctrlsocket_get_session_id(); //FIXME
            p->disable_plugin = vis_disable_plugin; //FIXME what is this symbol?

            m_vis = p;
        }
        else { dlclose( h ); return ; }

    } //</load plugin>

    if ( m_vis->init ) { std::cout << "[amK] init()\n"; m_vis->init(); }
    if ( m_vis->playback_start ) { std::cout << "[amK] start()\n"; m_vis->playback_start(); }
}


XmmsWrapper::~XmmsWrapper()
{
    dlclose( m_vis );

    std::cout << "[amK] ~\n";
}


void XmmsWrapper::configure()
{
    if ( m_vis->configure ) {
        std::cout << "[amK] configure()\n";
        m_vis->configure();
    }
}


void XmmsWrapper::render( float *float_data )
{
    gint16 pcm_data[ 2 ][ 512 ];

    for ( uint x = 0; x < 512; ++x )
    {
        //NOTE we times by 1<<14 rather than 1<<15 (maximum value of signed 16bit)
        //     this is because values of pcm data tend to range 0-2 (although there
        //     is no theoretical maximum.
        //NOTE actually the maximum value is signed 16 bit, just like on a CD

        pcm_data[ 0 ][ x ] = pcm_data[ 1 ][ x ] = gint16( float_data[ x ] * ( 1 << 14 ) );
    }


    if( renderPCM() )
    {
       vis()->render_pcm( pcm_data );

        /*
        if (wrap.vis()->num_pcm_chs_wanted == 1)
        {
            gint16 mono_pcm[2][512];
            calc_mono_pcm(mono_pcm, pcm_data, nch);
            wrap.vis()->render_pcm( mono_pcm );
        }
        else //nch == 2
        {
            gint16 stereo_pcm[2][512];
            calc_stereo_pcm(stereo_pcm, pcm_data, nch);
            wrap.vis()->render_pcm( stereo_pcm );
        }
        */
    }

    if( renderFFT() )   //NOTE some vis's may render both data types
    {
        gint16 fft_data[ 2 ][ 256 ];

        static fft_state *state = NULL;
        gfloat tmp_out[ 257 ];

        if ( !state ) state = fft_init();

        fft_perform( pcm_data[ 0 ], tmp_out, state );

        for ( uint i = 0; i < 256; i++ )
        {
            fft_data[ 0 ][ i ] = fft_data[ 1 ][ i ] = ( ( gint ) sqrt( tmp_out[ i + 1 ] ) ) >> 8;
        }

        vis() ->render_freq( fft_data );
        /*
        if (wrap.vis()->num_freq_chs_wanted == 1)
        {
            gint16 mono_freq[2][256];
            calc_mono_freq(mono_freq, pcm_data, nch);
            wrap.vis()->render_freq(mono_freq);
        }
        else
        {
            gint16 stereo_freq[2][256];
            calc_stereo_freq(stereo_freq, pcm_data, nch);
            wrap.vis()->render_freq(stereo_freq);
        }
        */
    }
}

//NOTE as yet, these functions are a little mysterious to me
/*
static void calc_stereo_pcm( gint16 dest[ 2 ][ 512 ], gint16 src[ 2 ][ 512 ], gint nch )
{
    memcpy( dest[ 0 ], src[ 0 ], 512 * sizeof( gint16 ) );
    if ( nch == 1 )
        memcpy( dest[ 1 ], src[ 0 ], 512 * sizeof( gint16 ) );
    else
        memcpy( dest[ 1 ], src[ 1 ], 512 * sizeof( gint16 ) );
}

static void calc_mono_pcm( gint16 dest[ 2 ][ 512 ], gint16 src[ 2 ][ 512 ], gint nch )
{
    gint i;
    gint16 *d, *sl, *sr;

    if ( nch == 1 )
        memcpy( dest[ 0 ], src[ 0 ], 512 * sizeof( gint16 ) );
    else {
        d = dest[ 0 ];
        sl = src[ 0 ];
        sr = src[ 1 ];
        for ( i = 0; i < 512; i++ ) {
            *( d++ ) = ( *( sl++ ) + *( sr++ ) ) >> 1;
        }
    }
}


static void calc_freq( gint16 *dest, gint16 *src )
{

            static fft_state *state = NULL;
            gfloat tmp_out[257];
            gint i;

            if(!state)
                    state = fft_init();

            fft_perform(src,tmp_out,state);

            for(i = 0; i < 256; i++)
                    dest[i] = ((gint)sqrt(tmp_out[i + 1])) >> 8;
}


static void calc_mono_freq( gint16 dest[ 2 ][ 256 ], gint16 src[ 2 ][ 512 ], gint nch )
{
            gint i;
            gint16 *d, *sl, *sr, tmp[512];

            if(nch == 1)
                    calc_freq(dest[0], src[0]);
            else
            {
                    d = tmp;
                    sl = src[0];
                    sr = src[1];
                    for(i = 0; i < 512; i++)
                    {
                            *(d++) = (*(sl++) + *(sr++)) >> 1;
                    }
                    calc_freq(dest[0], tmp);
            }
}


static void calc_stereo_freq( gint16 dest[ 2 ][ 256 ], gint16 src[ 2 ][ 512 ], gint nch )
{
            calc_freq(dest[0], src[0]);

            if(nch == 2)
                    calc_freq(dest[1], src[1]);
            else
                    memcpy(dest[1], dest[0], 256 * sizeof(gint16));
}
*/
