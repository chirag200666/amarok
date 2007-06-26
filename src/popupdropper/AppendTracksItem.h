/*
 *  Copyright (c) 2007 Maximilian Kossick <maximilian.kossick@googlemail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef AMAROK_POPUPDROPPER_APPENDTRACKSITEM_H
#define AMAROK_POPUPDROPPER_APPENDTRACKSITEM_H

#include "PopupDropperBaseItem.h"

#include <QString>
#include <QGraphicsSceneDragDropEvent>

class QSvgRenderer;

namespace PopupDropperNS
{
    class AppendTracksItem : public PopupDropperBaseItem
    {
        Q_OBJECT
        public:
            AppendTracksItem( int whichami, int total, QString element_id, QSvgRenderer *renderer = 0, QGraphicsItem *parent = 0 );
            virtual ~AppendTracksItem();

        protected:
            virtual void dropEvent( QGraphicsSceneDragDropEvent *event );
    };
}

#endif
