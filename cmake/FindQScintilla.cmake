# Locates QScintilla built against Qt 6.
#
# Ubuntu's libqscintilla2-qt6-dev ships no CMake package, so the header and
# library are searched for directly. Defines the imported target
# QScintilla::QScintilla.

find_path(QScintilla_INCLUDE_DIR
    NAMES Qsci/qsciscintilla.h
    HINTS ${QT6_INSTALL_PREFIX}/${QT6_INSTALL_HEADERS} ${Qt6Core_INCLUDE_DIRS}
    PATH_SUFFIXES qt6 x86_64-linux-gnu/qt6 aarch64-linux-gnu/qt6)

find_library(QScintilla_LIBRARY
    NAMES qscintilla2_qt6
    HINTS ${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBS})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(QScintilla
    REQUIRED_VARS QScintilla_LIBRARY QScintilla_INCLUDE_DIR)

if(QScintilla_FOUND AND NOT TARGET QScintilla::QScintilla)
    add_library(QScintilla::QScintilla UNKNOWN IMPORTED)
    set_target_properties(QScintilla::QScintilla PROPERTIES
        IMPORTED_LOCATION "${QScintilla_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${QScintilla_INCLUDE_DIR}")
endif()

mark_as_advanced(QScintilla_INCLUDE_DIR QScintilla_LIBRARY)
