vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

# Panthera uses SystemC's C++11 ABI; SystemC 3 requires C++17.
vcpkg_download_distfile(ARCHIVE
    URLS "https://www.accellera.org/images/downloads/standards/systemc/systemc-${VERSION}.tar.gz"
    FILENAME "systemc-${VERSION}.tar.gz"
    SHA512 831255f8e76e1cf776e0407c92c7a0b70ba63cf6b1785a2df372a6394440944540a8ae62264ef5293a7b951bd8b37de089ef0dbc0ee2d0354e69ce552c020ca2
)

vcpkg_extract_source_archive_ex(
    OUT_SOURCE_PATH SOURCE_PATH
    ARCHIVE "${ARCHIVE}"
    REF "${VERSION}"
    PATCHES
        install.patch
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DCMAKE_CXX_STANDARD=11
        -DDISABLE_COPYRIGHT_MESSAGE=ON
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/SystemCLanguage PACKAGE_NAME systemclanguage)
vcpkg_cmake_config_fixup(CONFIG_PATH share/cmake/SystemCTLM PACKAGE_NAME systemctlm)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")
# SystemC 2.3.3 public headers include its bundled Boost compatibility headers.
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/include/sysc/packages/qt/time")

file(INSTALL "${SOURCE_PATH}/NOTICE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
