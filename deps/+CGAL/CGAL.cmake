add_cmake_project(
    CGAL
    URL      https://github.com/CGAL/cgal/archive/refs/tags/v5.6.2.zip
    URL_HASH SHA256=29acaeee5a76a95029fac23131bb1c3a4a75df9a0e7e43b465a1f32d0628f45d
    PATCH_COMMAND patch -p2 -i ${CMAKE_CURRENT_LIST_DIR}/CGAL.patch
)

include(GNUInstallDirs)

set(DEP_CGAL_DEPENDS Boost GMP MPFR)