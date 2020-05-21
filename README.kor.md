# osm2pgsql #

Osm2pgsql은 openstreetmap 데이터를 지도에 렌더링, Nominatim 또는 일반적인 분석을 이용한 geocoding을 하면서 
지도에 rendering하는 것과 같은 애플리케이션에 적합한 PostgreSQL과 PostGIS database로 로딩하기 위한 도구이다.

[![Travis Build Status](https://secure.travis-ci.org/openstreetmap/osm2pgsql.svg?branch=master)](https://travis-ci.org/openstreetmap/osm2pgsql)
[![Appveyor Build status](https://ci.appveyor.com/api/projects/status/7abwls7hfmb83axj/branch/master?svg=true)](https://ci.appveyor.com/project/openstreetmap/osm2pgsql/branch/master)
[![Packaging status](https://repology.org/badge/tiny-repos/osm2pgsql.svg)](https://repology.org/project/osm2pgsql/versions)

## 특징 ##

* OSM 파일들을 PostgreSQL DB로 전환한다.
* 태그를 열로 변환할 수 있는 스타일 파일
* .gz, .bz2, .pbf, .o5m 파일들을 즉시 읽기 가능하다.
* diff를 적용하여 데이터베이스를 최신상태로 유지할 수 있다.
* 출력 투영의 선택을 지지한다.
* 테이블 이름을 설정할 수 있다.
* Nominatim을 위한 가제터 백엔드
* 만약 원하는 경우 하나의 데이터베이스 필드에 전체 태그 집합을 저장하는 hstore 필드 유형을 지원한다.

## 설치 ##

대부분의 리눅스 배포는 osm2pgsql을 포함한다. 
이것은 또한 [Homebrew](https://brew.sh/).가 있는 macOS에서도 이용 가능하다.

[AppVeyor](https://ci.appveyor.com/project/openstreetmap/osm2pgsql/history)에서 윈도우용 비공식적 빌드를 사용할 수 있지만 올바른 빌드 아티팩아티 찾는 것이 필요하다. 
또한, 배포용 빌드는 [OpenStreetMap Dev server](https://lonvia.dev.openstreetmap.org/osm2pgsql-winbuild/releases/)로부터 다운 받을 수 있다.

## Building ##

가장 최신의 소스 코드는 Github의 osm2pgsql git저장소에서 이용 가능하며 다음과 같이 다운로드 할 수 있다.

```sh
$ git clone git://github.com/openstreetmap/osm2pgsql.git
```

Osm2pgsql은 자체 구성 및 빌드하기 위하여 cross-platform  [CMake build system](https://cmake.org/)을 사용한다.

필수 라이브러리 :

* [expat](https://libexpat.github.io/)
* [proj](https://proj.org/)
* [bzip2](http://www.bzip.org/)
* [zlib](https://www.zlib.net/)
* 시스템과 파일시스템을 포함하는 [Boost libraries](https://www.boost.org/) 
* [PostgreSQL](https://www.postgresql.org/) 고객 라이브러리
* [Lua](https://www.lua.org/) (선택 사항, used for [Lua tag transforms](docs/lua.md))
* [Python](https://python.org/) (테스트 실행 전용)
* [Psycopg](http://initd.org/psycopg/) (테스트 실행 전용)

이것은 또한[PostgreSQL](https://www.postgresql.org/) 9.3+ 와
[PostGIS](http://www.postgis.net/) 2.2+ 를 실행하는 데이터베이스 서버에 엑세스 하길 요청한다.

요구 사항 섹션에 언급 된 라이브러리 및 C++ 11을 지원하는 C++ 컴파일러용 개발 패키지를 설치했는지 확실히 해야한다. 
GCC5 이상과 Clang 3.5 이상이 작동하는 것으로 알려져 있다.

먼저 종속성들을 설치해라.

데비안 또는 우분투 시스템에서 이것은 수행될 수 있다:

```sh
sudo apt-get install make cmake g++ libboost-dev libboost-system-dev \
  libboost-filesystem-dev libexpat1-dev zlib1g-dev \
  libbz2-dev libpq-dev libproj-dev lua5.2 liblua5.2-dev
```

페도라 시스템에서,


```sh
sudo dnf install cmake make gcc-c++ boost-devel expat-devel zlib-devel \
  bzip2-devel postgresql-devel proj-devel proj-epsg lua-devel
```

RedHat / CentOS에서 첫번째로 sudo yum install epel-release 을 실행하여 종속성을 설치해야 한다.

```sh
sudo yum install cmake make gcc-c++ boost-devel expat-devel zlib-devel \
  bzip2-devel postgresql-devel proj-devel proj-epsg lua-devel
```

FreeBSD 시스템에서,

```sh
pkg install devel/cmake devel/boost-libs textproc/expat2 \
  databases/postgresql94-client graphics/proj lang/lua52
```

일단 종속성이 설치되면, CMake를 사용하여 분리된 폴더에 Makefile들을 빌드해라.

```sh
mkdir build && cd build
cmake ..
```

만약 CMake에 의해 몇몇 설치된 종속성이 발견되지 못하면 더 많은 옵션들을 설치하는 것이 필요하다. 
일반적으로, `CMAKE_PREFIX_PATH`  적절한 경로 목록으로 설정하면 충분하다.

Makefiles가 성공적으로 빌드될 때 컴파일은

```sh
make
```

컴파일된 파일이 설치될 때

```sh
sudo make install
```

기본적으로 디버그 정보가 포함된 배포빌드가 작성되고 테스트는 컴파일되지 않는다. 
다음과 같이 추가 옵션을 사용하여 해당 동작을 변경할 수 있다.

```sh
cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
```

## 용법 ##

Osam2pgsql에는 하나의 프로그램을 가졌다. 
이 프로그램은 **43** 개의 명령 줄 옵션이 있는 실행 프로그램 자체가 있다.

데이터베이스로 로딩하기 전에, 그 데이터 베이스는 창조되어야 하고 PostGIS 와 선택적 hstore 확장이 로드 되어야 한다.
 PostgreSQL 설정에 대한 전체 안내서는 이 추가 정보의 범위를 벗어나지만 합리적으로 최신 버전의 PostgreSQL 및 PostGIS를 사용하면 다음을 수행할 수 있다.

```sh
createdb gis
psql -d gis -c 'CREATE EXTENSION postgis; CREATE EXTENSION hstore;'
```

`gis`렌더링을 위해 데이터베이스에 데이터를 로드하는 기본 호출은 다음과 같다.

```sh
osm2pgsql --create --database gis data.osm.pbf
```

이것은 데이터를 `data.osm.pbf` 를  `planet_osm_point`,
`planet_osm_line`, `planet_osm_roads`, 그리고  `planet_osm_polygon` 테이블로 로딩할 것이다.

그 완전한 행성과 같은 대용량의 데이터를 가져올 때에는 전형적인 코맨드라인은 다음과 같다.

```sh
osm2pgsql -c -d gis --slim -C <cache size> \
  --flat-nodes <flat nodes> planet-latest.osm.pbf
```

* `<cache size>` 는 MiB 메모리의 75%, 최대 대략 30000맥시멈이다. 추가적인 램은 사용될 수 없다.
* `<flat nodes>`는 36GiB+ 파일을 저장할 수 있는 위치이다.

많은 다른 데이터 파일(pbf) 은  [planet.osm.org](https://planet.osm.org/)에서 찾아질 수 있다.

[Mapnik](https://mapnik.org/)은 이러한 명령 중 하나의 데이터베이스를 [renderd/mod_tile](https://github.com/openstreetmap/mod_tile), [TileMill](https://tilemill-project.github.io/tilemill/), [Nik4](https://github.com/Zverik/Nik4)등의 표준 도구로 맵을 렌더링 하는데 즉시 사용할 수 있다. 
그것은 또한 [spatial analysis](docs/analysis.md)이나[shapefile exports](docs/export.md)에도 쓰일 수 있다.

[Additional documentation is available on writing command lines](docs/usage.md).

## 대체 백앤드 ##

게다가 렌더링을 위해 설계된 표준[pgsql](docs/pgsql.md)백엔드 외에, [Nominatim](https://www.nominatim.org/)을 사용한 지오코딩을 위한  [gazetteer](docs/gazetteer.md)데이터 베이스와 테스트를 위한 null backend도 있다. 
유연성을 위해 pgsql백엔드에 제공된 테이블 대신 사용자 정의 PostgreSQL 테이블을 구성할 수 있는 새로운 [multi](docs/multi.md) backend도 사용할 수 있다. 

또한 새로운 [flex](docs/flex.md)backend도 제공된다. 다른 backend보다 훨씬 유연하다. 
이것은 현재 실험적이며 변경 될 수 있는 객체이다. Flex backend는 lua를 지원하는 osm2pgsql을 컴파일 한 경우에만 사용이 가능하다. 더욱 자세한 내용은 https://github.com/openstreetmap/osm2pgsql/issues/1036 . 에서 확인 가능하다.

## LuaJIT 지원 ##

Lua 태그 변환 속도를 높이기 위해 지원되는 플랫폼에서 [LuaJIT](https://luajit.org/)를 선택적으로 활성화 할 수 있다. 
성능측정 결과, planet 가져오기의 경우 25%의 런타임 감소와 40%의 구문 분석 시간의 감소가 나타났다.

데비안 또는 우분투 시스템에서 다음을 수행할 수 있다.

```sh
sudo apt install libluajit-5.1-dev
```

`WITH_LUAJIT=ON` LuaJIT를 활성화하려면 구성 매개 변수를 추가해야 합니다.
 그렇지 않으면 만들고 설치하는 단계들은 위의 설명과 동일하다. 

```sh
cmake -D WITH_LUAJIT=ON ..
```

osm2pgsql –version 빌드에 LuaJIT 지원이 포함되어 있는지 확인하는데 사용해라.

```sh
./osm2pgsql --version
osm2pgsql version 0.96.0 (64 bit id space)

Compiled using the following library versions:
Libosmium 2.15.0
Lua 5.1.4 (LuaJIT 2.1.0-beta3)
```


## 기여 ##

우리는 osm2pgsql에 대한 기여를 환영한다. 만약 너가 이슈를 보고하려면, [issue tracker on GitHub](https://github.com/openstreetmap/osm2pgsql/issues)를 사용하시길 바랍니다.

더 많은 정보는  [CONTRIBUTING.md](CONTRIBUTING.md)에서 찾을 수 있습니다.
일반적인 쿼리들은 tile-serving@ 또는 dev@[mailing lists](https://wiki.openstreetmap.org/wiki/Mailing_lists) 로 보낼 수 있습니다.
