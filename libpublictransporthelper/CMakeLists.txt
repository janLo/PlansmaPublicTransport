project( publictransporthelper )

# Find the required Libaries
find_package( KDE4 REQUIRED )
include( KDE4Defaults )

add_definitions( ${QT_DEFINITIONS} ${KDE4_DEFINITIONS} )
include_directories(
	${CMAKE_SOURCE_DIR}
	${CMAKE_BINARY_DIR}
	${KDE4_INCLUDES} )

enable_testing()

set( publictransporthelper_LIB_SRCS
	locationmodel.cpp
	serviceprovidermodel.cpp
	dynamicwidget.cpp
	categorycombobox.cpp
	checkcombobox.cpp
	stopwidget.cpp
	accessorinfodialog.cpp
	stopsettingsdialog.cpp
	stopfinder.cpp
	htmldelegate.cpp
	stopsettings.cpp
	global.cpp
)

kde4_add_ui_files( publictransporthelper_LIB_SRCS
	accessorInfo.ui
	stopConfig.ui )

set( publictransporthelper_LIB_HDRS
	publictransporthelper_export.h
	
	# Models
	locationmodel.h
	serviceprovidermodel.h
	
	# Widgets
	dynamicwidget.h
	categorycombobox.h
	checkcombobox.h
	stopwidget.h
	
	# Dialogs
	accessorinfodialog.h
	stopsettingsdialog.h

	# Others
	global.h
	enums.h
	stopsettings.h
	stopfinder.h
# 	htmldelegate.h
)

kde4_add_library( publictransporthelper SHARED ${publictransporthelper_LIB_SRCS} )

add_subdirectory( tests )

target_link_libraries( publictransporthelper
	${KDE4_PLASMA_LIBS}
	${KDE4_KDEUI_LIBS}
	${KDE4_KIO_LIBS}
	${KDE4_KNEWSTUFF3_LIBS}
)
set_target_properties( publictransporthelper
	PROPERTIES VERSION ${GENERIC_LIB_VERSION} SOVERSION ${GENERIC_LIB_SOVERSION}
)

install( TARGETS publictransporthelper ${INSTALL_TARGETS_DEFAULT_ARGS} )

install( FILES ${publictransporthelper_LIB_HDRS}
	DESTINATION ${INCLUDE_INSTALL_DIR}/publictransporthelper
	COMPONENT Devel
)

install( FILES publictransporthelper.desktop DESTINATION ${SERVICES_INSTALL_DIR} )