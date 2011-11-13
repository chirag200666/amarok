/****************************************************************************************
 * Copyright (c) 2011 Lucas Lira Gomes <x8lucas8x@gmail.com>                            *
 *                                                                                      *
 * This program is free software; you can redistribute it and/or modify it under        *
 * the terms of the GNU General Public License as published by the Free Software        *
 * Foundation; either version 2 of the License, or (at your option) any later           *
 * version.                                                                             *
 *                                                                                      *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY      *
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A      *
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.             *
 *                                                                                      *
 * You should have received a copy of the GNU General Public License along with         *
 * this program.  If not, see <http://www.gnu.org/licenses/>.                           *
 ****************************************************************************************/

#include "SyncedPodcast.h"
#include "src/core/playlists/Playlist.h"

#include "core/support/Debug.h"

#include <KLocale>

using namespace Meta;

SyncedPodcast::SyncedPodcast( Podcasts::PodcastChannelPtr podcast )
{
    m_master = podcast;
    addPlaylist( Playlists::PlaylistPtr::dynamicCast( m_master ) );
}
