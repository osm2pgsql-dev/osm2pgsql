# Copyright 1999-2013 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2

EAPI="5"

inherit cmake-multilib cmake-utils git-2

DESCRIPTION="OpenStreetMap data to PostgreSQL converter."
HOMEPAGE="https://github.com/openstreetmap/osm2pgsql"
EGIT_REPO_URI="https://github.com/openstreetmap/osm2pgsql.git"

LICENSE="GPL-2"
SLOT="0/9999"
KEYWORDS=""

IUSE="internal-libosmium lua test"

RDEPEND="
    sys-libs/zlib
    dev-libs/expat
    app-arch/bzip2
    sci-libs/geos
    sci-libs/proj
    dev-libs/boost:=[threads]
    dev-db/postgresql
    !internal-libosmium? ( sci-geosciences/libosmium[protozero,utfcpp] )
    lua? ( dev-lang/lua )
    test? ( dev-lang/python )
"
DEPEND="${RDEPEND}"

src_unpack() {
    git-2_src_unpack
}

src_configure() {
    local mycmakeargs=(
        $(cmake-utils_use !internal-libosmium EXTERNAL_LIBOSMIUM)

        $(cmake-utils_use lua  WITH_LUA)
        $(cmake-utils_use test BUILD_TESTS)
    )
    cmake-multilib_src_configure
}

src_test() {
    cmake-multilib_src_test
}

src_install() {
    cmake-multilib_src_install
}
