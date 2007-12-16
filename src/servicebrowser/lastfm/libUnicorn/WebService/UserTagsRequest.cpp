/***************************************************************************
 *   Copyright (C) 2005 - 2007 by                                          *
 *      Max Howell, Last.fm Ltd <max@last.fm>                              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.          *
 ***************************************************************************/

#include "logger.h"
#include "Request.h"
#include "WebService.h"
#include "UnicornCommon.h"

UserTagsRequest::UserTagsRequest()
        : TagsRequest( TypeUserTags, "UserTags" )
{}


QString
UserTagsRequest::path() const
{
    return "/tags.xml";
}


void
UserTagsRequest::start()
{
    if (m_username.isEmpty())
        m_username = The::webService()->currentUsername();

    get( "/1.0/user/" +  UnicornUtils::urlEncodeItem( m_username ) + path() );
}


void
UserTagsRequest::success( QByteArray data )
{
    QDomDocument xml;
    xml.setContent( data );

    QDomNodeList values = xml.elementsByTagName( "tag" );

    for (int i = 0; i < values.count(); i++) {
        QDomNode item = values.item( i );

        QString name = item.namedItem( "name" ).toElement().text();
        int count = item.namedItem( "count" ).toElement().text().toInt();

        m_tags += WeightedString::counted( name, count );
    }
}
