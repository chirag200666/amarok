project(context-lastfm)

set( lastfmevents_SRCS LastFmEvents.cpp )

include_directories( ../..
                    ../../.. )

kde4_add_plugin(amarok_context_applet_lastfmevents ${lastfmevents_SRCS})

if(APPLE)
   SET_TARGET_PROPERTIES(amarok_context_applet_lastfmevents PROPERTIES LINK_FLAGS "-undefined dynamic_lookup")
endif(APPLE)

target_link_libraries(amarok_context_applet_lastfmevents amaroklib ${KDE4_PLASMA_LIBS} ${KDE4_KIO_LIBS})

install(TARGETS amarok_context_applet_lastfmevents DESTINATION ${PLUGIN_INSTALL_DIR})
install(FILES amarok-context-applet-lastfmevents.desktop DESTINATION ${SERVICES_INSTALL_DIR})
install(FILES amarok-lastfm.svg DESTINATION ${DATA_INSTALL_DIR}/desktoptheme/default/widgets/ )
